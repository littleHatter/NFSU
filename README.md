# Need for Speed Undercover — New 3DS native port

A native New Nintendo 3DS compatibility port of the community-modified N-Gage
2.0 edition of **Need for Speed Undercover**, derived from
[LeithSilver/mgsm](https://github.com/LeithSilver/mgsm).

The original ARM game code executes directly on the New 3DS CPU. The game's
software renderer produces the 320×240 image; the compatibility runtime handles
files, input, audio, timers, memory and callbacks. This version uses neither a
Symbian OS emulator nor a NovaGL/OpenGL ES translation layer.

The clean-polish build was confirmed working by the hardware tester on
2026-11-17, including the demo timing and removal of the remaining soft-key icon.
This is a compatibility port, not recovered game source or a decompilation.

## Build for New 3DS

Use a devkitPro **3ds-dev** shell with devkitARM, libctru and the 3DS tools:

```sh
make -C nfs_undercover -j2
```

Outputs: `nfs_undercover/nfs_undercover.3dsx` and `nfs_undercover/nfs_undercover.smdh`.
Tested toolchain: devkitARM r68 / GCC 16.1.0, libctru 2.7.1.
The supplied bottom-screen header is already generated. Python 3 with Pillow is
needed only if regenerating it from `bottomscreen.png`.
See [build instructions](docs/BUILDING.md) for details and optional desktop probes.

## Install

Copy the built `.3dsx` and `.smdh` to `sdmc:/3ds/nfs_undercover/`.
Place the prepared game directory at `sdmc:/3ds/nfs_undercover/game/`.
The complete installation ZIP already supplies that layout.
Saves are created separately under `sdmc:/3ds/nfs_undercover/user/`.

Requires **New 3DS / New 3DS XL / New 2DS XL** with homebrew support.
This source package does not contain the game executable, game archives, or saves.
It does contain the icon and supplied bottom-screen branding needed for the build.
Use the specific community-modified N-Gage data used by this project; stock
protected N-Gage packages are not automatically extracted or supported here.

## Controls

| Button | Action |
|---|---|
| D-pad / Circle Pad | Directions / steering |
| A | Confirm / centre key |
| B | Back / cancel |
| START | Pause |
| X / Y | Numeric 5 / 0 |
| L / R | Numeric 7 / 9 |
| SELECT | Unassigned |
| HOME | System HOME menu |

## Included improvements

- Game-specific S3E import, callback, filesystem and memory services.
- Functional software-rendered gameplay and original HUD artwork.
- Independent sequenced-music and engine/effect mixing through NDSP.
- 2× gain at the final 3DS PCM output, covering music and effects.
- FAT-compatible save replacement and backup recovery.
- Reduced sound-event stalls by suppressing routine logging and reusing mixer buffers.
- Correct 64-bit timer return, fixing rapid attract-mode vehicle switching.
- Dedicated soft-key artwork draw suppression, preserving physical key handling.
- Supplied bottom-screen artwork, NFS icon and updated application metadata.
- SELECT unassigned; HOME handling and artwork restoration retained.

Normal output is silent. Runtime faults can show a bottom-screen error console.
[Port notes](docs/PORT_NOTES.md) document the architecture, supported data and limits.

## Using this over an MGSM fork

Extract this source ZIP into the **root** of your MGSM fork and commit the files.
The new port lives in `nfs_undercover/`; the existing `mgs_native/`, Android and
Vita code are left in place. This README becomes the fork's landing page; the
original README is retained as [README_MGSM_UPSTREAM.md](README_MGSM_UPSTREAM.md).
See [fork upload instructions](docs/FORK_UPLOAD.md).
This archive has no Git history and does not create or publish a fork by itself.

## Credits and license

The game and original engine were created by their original developers and
publishers. The native CPU backend, ARM interpreter, heap and loader foundations
come from LeithSilver's MGSM project, based on upstream revision
`4e6d91ceadfb898322f535f82f24a394197d892b`.
The NFS compatibility work and presentation changes were developed with ChatGPT,
with iterative testing and artwork supplied by the project user.

Port code is GPL version 2 or, at your option, any later version, consistent with
MGSM. Preserve [LICENSE](LICENSE) and [third-party notices](licenses/THIRD_PARTY.txt).
The bundled LZMA decoder is public domain. Game data, logos, icons and trademarks
retain their respective ownership; the code license does not grant rights to them.
This is an unofficial project, not affiliated with or endorsed by the game publisher.
