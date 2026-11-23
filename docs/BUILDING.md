# Build and install

## New 3DS

Use devkitPro's 3ds-dev environment (devkitARM, libctru, citro3d, 3DS zlib and 3DS tools). Ensure
DEVKITPRO and DEVKITARM are set and the tool binaries are on PATH.
From the repository root:

```sh
make -C nfs_undercover -j2
```

For a clean rebuild:

```sh
make -C nfs_undercover clean
make -C nfs_undercover -j2
```

This port is self-contained in nfs_undercover/ and does not use MGSM's desktop
CMake or original mgs_native/3ds build scripts. Those scripts build Metal Gear.
The bundled decoder builds directly. Hardware rendering uses citro3d. Tested: devkitARM r68/GCC 16.1.0 and libctru 2.7.1.

Copy the resulting nfs_undercover.3dsx and nfs_undercover.smdh into
sdmc:/3ds/nfs_undercover/. Copy the prepared game directory from the complete
installation package into the same directory. Keep existing user/ saves.
There is no on-console .n-gage installer. Prepare the files on your computer:

```sh
python3 tools/prepare_nfsuc.py "Need for Speed Undercover.n-gage"
```

Then copy the generated `3ds` folder to the SD card root. See
[GAME_PREPARATION.md](GAME_PREPARATION.md) for output paths and supported input.

The embedded bottom artwork is bottomscreen.png. bottom_art.h is included;
regenerate after editing with:

```sh
python3 -m pip install Pillow
python3 nfs_undercover/generate_bottom_art.py nfs_undercover/bottomscreen.png nfs_undercover/bottom_art.h
```

## Optional Linux desktop diagnostic

Requires a C++17 g++ compiler, zlib, EGL and OpenGL development libraries, and Python package unicorn==2.1.4:

```sh
python3 -m pip install unicorn==2.1.4
python3 nfs_undercover/build_desktop.py
./nfs_undercover/re_hybrid_probe /path/to/game 300 /tmp/nfs-frames /tmp/nfs-test-saves
```

The positional arguments are game directory, frame limit, output directory,
separate save directory and optionally an input-script file. Script lines are
`begin_frame end_frame key_code`; end_frame is exclusive. Key codes: A/confirm
78, B/Back 210, START/Pause 209, directions 9/10/11/12.
The probe captures 320×240 PPM frames and raw signed 16-bit mono PCM at 22050 Hz.
NFS_LIVE_AUDIO=1 enables optional dynamically loaded SDL2 queued audio on Linux.
This diagnostic is not a polished desktop game launcher. Its traces and captures
are intentionally separate from the silent native release frontend.

## Button and startup artwork

The original PNGs are included in `nfs_undercover/artwork/`, alongside their generated
RGBA headers. Regenerate without scaling using:

```sh
python3 nfs_undercover/generate_prompts.py
python3 nfs_undercover/generate_startup_art.py
```

Make tracks the image and generator dependencies. Normal builds can use the supplied
headers without installing Pillow when inputs have not changed.

## Focused runtime checks

```sh
python3 nfs_undercover/tests/build_and_run.py /path/to/game
```

These test prompt mappings, ADPCM overlap suppression, independent sounds, engine
exemption, active-wave request rejection, and real PCM sample completion with empty
or stale USER_VAR values. They run at both stock and relocated image addresses.
`tests/generator.bin` is a tiny synthetic ARM/Thumb test fixture, with its source in
`tests/generator.S`; it is not game data. To regenerate it with devkitARM tools:

```sh
arm-none-eabi-as -march=armv5te nfs_undercover/tests/generator.S -o /tmp/nfs_generator.o
arm-none-eabi-objcopy -O binary /tmp/nfs_generator.o nfs_undercover/tests/generator.bin
```
