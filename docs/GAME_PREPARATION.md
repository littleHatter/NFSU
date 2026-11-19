# Preparing game data from an N-Gage package

Requires Python **3.8 or newer**, with its standard-library `lzma` module.
No additional Python packages, emulator, Symbian installation, external extractor,
network access, or keys are required for the supported community-modified edition.

From the source repository root:

```sh
python3 tools/prepare_nfsuc.py "Need for Speed Undercover.n-gage"
```

Windows equivalent:

```powershell
py -3 tools/prepare_nfsuc.py "C:\Games\Need for Speed Undercover.n-gage"
```

The default output is `3ds/nfs_undercover/game/`, relative to the working directory.
Copy the generated `3ds` folder to the SD card root. Place the built
`nfs_undercover.3dsx` and `.smdh` beside the game directory.

For another output location:

```sh
python3 tools/prepare_nfsuc.py "game.n-gage" --output "/path/to/SD/3ds/nfs_undercover/game"
```

The standalone preparation ZIP has the same script at its root; run
`python3 prepare_nfsuc.py "game.n-gage"` there. Keep GAME_FILES.json beside it.
The source-repository script finds the manifest in docs/GAME_FILES.json.

## Supported input and verification

This tool supports the specific **community-modified Need for Speed Undercover
N-Gage edition** used by this port. A renamed copy of the same package is fine.
It does not decrypt or recover files from protected stock releases, accept arbitrary
game revisions, or modify the port to match incompatible game executables.

It locates the Game SIS payload inside the .n-gage container, decodes its files,
normalizes the supported private-directory paths, expands nfsuc.s3e to
nfsuc.xe3u, and extracts the original tacho font from the verified resources-sw.dz.
The font is copied exactly; it is not redrawn or reconstructed.
Only the 20 expected game files are written. SIS executable/DLL installers and
metadata outside the supported private game directory are not installed.
Every output is checked for its exact length and SHA-256 before any output is
written. Unknown, damaged or incompatible data produces an error.

Existing identical output files are left alone, so rerunning is safe. Conflicting
files cause an error before writing anything. Back them up, then add
`--overwrite` if you intend to replace them. The sibling `user/` save directory
and unrelated output files are not removed.
The script does not copy/build a 3DSX or fetch a game package for you.

## Validation

The supplied community-modified .n-gage package reproduced every file of the
hardware-confirmed complete installation, byte for byte (20 files).
Repeat runs passed; a Resident Evil package was rejected without creating output.
Six data-free regression checks cover malformed SIS fields, oversized decoded
streams, missing Game sections, conflict preflight, repeated writes and save
preservation:

```sh
python3 -m unittest discover -s tools -p test_prepare_nfsuc.py
```

The current manifest contains 21 files, including the 3DS tutorial text override overrides/t_en. The original archive is preserved. The English string offsets are rebuilt after replacing only the five requested tutorial sentences, and the remaining content is retained. Copy this override on updates; the executable alone does not replace tutorial text.
