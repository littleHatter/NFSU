# Release validation

The Cruiser Tumble Minimap Test executable was confirmed on a New 3DS. The Complete Install packages those exact bytes. See RELEASE.json for its SHA-256.

## Earlier release validation

# Current release validation — 2026-11-17

The user confirmed the current build on New 3DS hardware, including the final
startup Loading graphic handover. Earlier confirmations covered saving, Back/Pause,
original HUD, music/engine audio, demo timing, soft-key suppression, A/B/START prompts,
updated icon, and the PCM impact overlap/retrigger fix.

The PCM fix was based on hardware traces: the same 1760-sample, 8000-Hz PCM effect
started on seven consecutive frames across seven channels before the first ended.
The earlier ADPCM-only filters did not cover its generator; PCM USER_VAR was empty
or stale. The current code captures wave identity from the game-owned sound voice
and guards both effect formats before voice allocation.

Focused ARM callback tests pass at stock and relocated bases. Real PCM decoder tests
reject seven retriggers while preserving the original decoder cursor, complete once,
then accept the next request, with both empty and deliberately stale USER_VAR.
Distinct sounds mix, engine voices are exempt, and completion callbacks free channels.
The last startup presentation change leaves the confirmed audio runtime unchanged.

The source package is rebuilt independently before delivery. Its 3DSX hash and the
hardware-confirmed distributed build hash are recorded in RELEASE.json.
Tested native toolchain: devkitARM r68 / GCC 16.1.0, libctru 2.7.1.
This is not an exhaustive full-game completion test.

The preparation tool and GAME_FILES.json are retained. The source ZIP excludes game
executable/archives, user saves, SDK files, built executables and diagnostic captures.
The small synthetic generator.bin test fixture is included with its assembly source.

## Tutorial and metadata consistency update

The five requested English tutorial sentences are replaced in the t_en override. The offset table is rebuilt, all other strings and the trailing marketing table are preserved, and the extraction script reproduces the verified override. The updated manifest contains 21 files. The runtime, input, save and audio source files are unchanged from the confirmed startup-loading/PCM-impact baseline. The native build and six preparation regressions pass. This update awaits hardware verification. RELEASE.json retains the previous confirmed binary hash and identifies the new packaged executable separately.
