# Release validation — 2026-11-17

- User confirmed the clean-polish build on New 3DS hardware.
- The packaged source compiled from a fresh build directory with devkitARM r68,
  GCC 16.1.0 and libctru 2.7.1.
- The rebuilt 3DSX matches the hardware-confirmed distributed binary byte-for-byte.
- The optional Linux/Unicorn probe compiled and ran three startup frames with
  the exact installation package's game files and a fresh temporary save directory.
- Installation package contains all 20 prepared game files and no user saves.
- Source package contains no game executable, .dz archive, saves or build outputs.
- This is not an exhaustive full-game completion test.

Confirmed/rebuilt 3DSX SHA-256:
`08660092b8e87ecfa43fce9d53149c9cfd6176da2c882f657922290359ca1074`

The prepared game-file hashes are recorded in GAME_FILES.json. The complete
installation ZIP additionally includes SHA256SUMS.txt for its own file contents.
