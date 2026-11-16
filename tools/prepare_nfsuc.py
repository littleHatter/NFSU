#!/usr/bin/env python3
"""Prepare the verified NFS Undercover N-Gage data for the New 3DS port.

Python 3.8+; standard library only. No game data is embedded in this script.
Copyright 2026 NFS compatibility project; GPL-2.0-or-later (see LICENSE).
"""
import argparse
import hashlib
import json
import lzma
import os
from pathlib import Path, PurePosixPath
import re
import struct
import sys
import tempfile
import zlib

MAX_FILE = 32 * 1024 * 1024
MAX_PACKAGE = 128 * 1024 * 1024
PRIVATE = 'private/2001a5aa/'


class PreparationError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise PreparationError(message)


def fields(data, typed=True, element_type=None):
    """Read bounded, four-byte-aligned SIS fields, including extended lengths."""
    offset = 0
    while offset < len(data):
        require(len(data) - offset >= (8 if typed else 4), 'Truncated SIS field header.')
        kind = element_type
        if typed:
            kind = struct.unpack_from('<I', data, offset)[0]
            offset += 4
        size = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        if size & 0x80000000:
            require(len(data) - offset >= 4, 'Truncated extended SIS length.')
            size = (size & 0x7fffffff) | (struct.unpack_from('<I', data, offset)[0] << 31)
            offset += 4
        require(size <= len(data) - offset, 'SIS field exceeds its container.')
        yield kind, data[offset:offset + size]
        offset += (size + 3) & ~3
        require(offset <= len(data), 'Truncated SIS alignment padding.')


def array(data):
    require(len(data) >= 4, 'Truncated SIS array.')
    return list(fields(data[4:], False, struct.unpack_from('<I', data)[0]))


def first(data, kind):
    matches = [value for tag, value in fields(data) if tag == kind]
    require(len(matches) == 1, 'Missing or duplicate SIS field %d.' % kind)
    return matches[0]


def decompress(data):
    require(len(data) >= 12, 'Truncated SIS compression header.')
    algorithm, size = struct.unpack_from('<IQ', data)
    require(size <= MAX_FILE, 'SIS decoded file exceeds the supported size limit.')
    if algorithm == 0:
        result = data[12:]
    elif algorithm == 1:
        decoder = zlib.decompressobj()
        result = decoder.decompress(data[12:], size + 1)
        require(decoder.eof and not decoder.unconsumed_tail and not decoder.unused_data,
                'Incomplete or oversized SIS zlib stream.')
    else:
        raise PreparationError('Unsupported SIS compression algorithm %d.' % algorithm)
    require(len(result) == size, 'SIS decoded length mismatch.')
    return result


def descriptions(data, depth=0):
    require(depth <= 32, 'SIS controller nesting is too deep.')
    for kind, value in fields(data):
        if kind == 24:
            yield value
        elif kind in (13, 26, 27, 28):
            yield from descriptions(value, depth + 1)
        elif kind == 2:
            for element_kind, element in array(value):
                if element_kind == 24:
                    yield element
                elif element_kind in (26, 27, 28):
                    yield from descriptions(element, depth + 1)


def package_files(package):
    # N-Gage MIME headers are not consistently terminated with a blank line.
    # Locate the Game part explicitly, then bound the SIS by its Contents field.
    match = re.search(br'Content-ID:\s*<Game>\s*\r?\n(?:[^\r\n]+\r?\n)*\r?\n',
                      package, re.IGNORECASE)
    require(match is not None, 'No Game part found in the N-Gage package.')
    sis = package[match.end():]
    require(len(sis) >= 24 and struct.unpack_from('<I', sis)[0] == 0x10201a7a,
            'The Game part is not a supported SIS package.')
    # Only consume the first field: trailing MIME boundaries are not SIS data.
    contents_kind, contents = next(fields(sis[16:]))
    require(contents_kind == 12, 'Expected SIS Contents field.')
    controller = decompress(first(contents, 3))
    units = array(first(first(contents, 30), 2))
    require(len(units) == 1, 'Expected one game data unit.')
    file_data = array(first(units[0][1], 2))
    extracted = {}
    total = 0
    for description in descriptions(controller):
        name_kind, name_data = next(fields(description))
        require(name_kind == 1, 'Missing SIS target filename.')
        name = name_data.decode('utf-16le')
        normalized = name.replace('\\', '/').lower()
        if not re.match(r'^[a-z!]:/' + PRIVATE, normalized):
            continue
        relative = normalized.split(PRIVATE, 1)[1]
        path = PurePosixPath(relative)
        require(not path.is_absolute() and '..' not in path.parts and ':' not in relative,
                'Unsafe path in SIS controller.')
        index = struct.unpack_from('<I', description, len(description) - 4)[0]
        require(index < len(file_data), 'SIS file index is out of range.')
        raw = decompress(first(file_data[index][1], 3))
        total += len(raw)
        require(total <= MAX_PACKAGE, 'Total decoded payload exceeds the size limit.')
        require(relative not in extracted, 'Duplicate game file: ' + relative)
        extracted[relative] = raw
    return extracted


def lzma_alone(data, expected_size):
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE, memlimit=64 * 1024 * 1024)
    result = decoder.decompress(data, max_length=expected_size + 1)
    require(len(result) == expected_size, 'LZMA decoded length mismatch.')
    # The original game streams may use a declared length rather than an end marker.
    return result


def manifest_path():
    here = Path(__file__).resolve().parent
    for candidate in (here / 'GAME_FILES.json', here.parent / 'docs/GAME_FILES.json'):
        if candidate.is_file():
            return candidate
    raise PreparationError('GAME_FILES.json is missing; keep the supplied manifest with the script.')


def prepare(package, manifest):
    files = package_files(package)
    expected = {entry['path']: entry for entry in manifest}
    require(len(expected) == 20, 'Unsupported game-file manifest.')
    require('nfsuc.s3e' in files, 'No nfsuc.s3e found. This is not the supported NFS package.')
    packed = files.pop('nfsuc.s3e')
    files['nfsuc.xe3u'] = (packed if packed[:4] == b'XE3U' else
                           lzma_alone(packed, expected['nfsuc.xe3u']['size_bytes']))
    require(files['nfsuc.xe3u'][:4] == b'XE3U', 'The decoded executable is not XE3U.')
    # The archive must match before using the known original font stream offset.
    archive = files.get('resources-sw.dz', b'')
    require(hashlib.sha256(archive).hexdigest() == expected['resources-sw.dz']['sha256'],
            'Unsupported resources-sw.dz. Supply the tested community-modified N-Gage edition.')
    files['overrides/0font_tacho.bmp'] = lzma_alone(
        archive[2286840:], expected['overrides/0font_tacho.bmp']['size_bytes'])
    output = {}
    for name, entry in expected.items():
        path = PurePosixPath(name)
        require(not path.is_absolute() and '..' not in path.parts and ':' not in name,
                'Unsafe path in manifest.')
        require(name in files, 'Required file is missing: ' + name)
        data = files[name]
        require(len(data) == entry['size_bytes'] and
                hashlib.sha256(data).hexdigest() == entry['sha256'],
                'Unsupported or damaged game file: ' + name +
                '. Use the tested community-modified N-Gage edition.')
        output[name] = data
    return output


def write_files(output, files, overwrite):
    # Validate every conflict before writing any output; never touch user/ saves.
    require(not output.is_symlink(), 'The output directory must not be a symbolic link.')
    for name, data in files.items():
        target = output / name
        for parent in (target,) + tuple(target.parents):
            if parent == output.parent:
                break
            require(not parent.is_symlink(), 'Symbolic link in output path: ' + str(parent))
        if target.exists():
            require(target.is_file(), 'Output path is not a file: ' + str(target))
            require(target.read_bytes() == data or overwrite,
                    'Different file already exists: ' + str(target) +
                    '. Back it up, then use --overwrite to replace prepared game files.')
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.nfsuc-prepare-', dir=str(output.parent)) as temp:
        staged = Path(temp)
        for name, data in files.items():
            target = staged / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        for name, data in files.items():
            target = output / name
            if target.is_file() and target.read_bytes() == data:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            os.replace(str(staged / name), str(target))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('package', type=Path, help='Supported community-modified NFS .n-gage package')
    parser.add_argument('--output', type=Path, default=Path('3ds/nfs_undercover/game'),
                        help='Destination game directory (default: 3ds/nfs_undercover/game)')
    parser.add_argument('--overwrite', action='store_true',
                        help='Replace conflicting prepared game files; saves are not touched')
    args = parser.parse_args(argv)
    try:
        require(args.package.is_file(), 'Input package does not exist: ' + str(args.package))
        require(args.package.stat().st_size <= MAX_PACKAGE, 'Input package exceeds 128 MiB.')
        manifest = json.loads(manifest_path().read_text(encoding='utf-8'))
        files = prepare(args.package.read_bytes(), manifest)
        write_files(args.output.absolute(), files, args.overwrite)
    except (PreparationError, OSError, ValueError, KeyError, struct.error,
            lzma.LZMAError, zlib.error, StopIteration, RecursionError) as error:
        print('Preparation failed: ' + str(error), file=sys.stderr)
        return 1
    print('Prepared and SHA-256 verified %d game files in %s' % (len(files), args.output))
    print('Copy this game directory beside nfs_undercover.3dsx. Existing user/ saves are untouched.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
