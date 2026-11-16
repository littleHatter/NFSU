"""Safety/regression checks that need no game data. Run with Python 3 unittest."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location('prepare_nfsuc', Path(__file__).with_name('prepare_nfsuc.py'))
prep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prep)


class PreparationTests(unittest.TestCase):
    def test_truncated_sis_field(self):
        with self.assertRaises(prep.PreparationError):
            list(prep.fields(struct.pack('<II', 1, 100) + b'abc'))

    def test_extended_length_out_of_bounds(self):
        with self.assertRaises(prep.PreparationError):
            list(prep.fields(struct.pack('<III', 1, 0x80000000, 1)))

    def test_decompression_bomb_exceeds_declared_size(self):
        data = struct.pack('<IQ', 1, 16) + zlib.compress(b'a' * 100000)
        with self.assertRaises(prep.PreparationError):
            prep.decompress(data)

    def test_missing_game_section(self):
        with self.assertRaises(prep.PreparationError):
            prep.package_files(b'not a package')

    def test_conflict_preflight_writes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'game'
            output.mkdir()
            (output / 'b').write_bytes(b'existing')
            with self.assertRaises(prep.PreparationError):
                prep.write_files(output, {'a': b'new', 'b': b'different'}, False)
            self.assertFalse((output / 'a').exists())
            self.assertEqual((output / 'b').read_bytes(), b'existing')

    def test_repeated_run_and_save_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'user').mkdir()
            save = base / 'user/progress.bin'
            save.write_bytes(b'save')
            output = base / 'game'
            files = {'arena/config.xml': b'config', 'nfsuc.xe3u': b'code'}
            prep.write_files(output, files, False)
            prep.write_files(output, files, False)
            prep.write_files(output, {'nfsuc.xe3u': b'updated'}, True)
            self.assertEqual(save.read_bytes(), b'save')
            self.assertEqual((output / 'nfsuc.xe3u').read_bytes(), b'updated')


if __name__ == '__main__':
    unittest.main()
