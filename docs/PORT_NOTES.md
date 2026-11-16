# Port notes

## Baseline and supported game image

Based on MGSM upstream 4e6d91ceadfb898322f535f82f24a394197d892b.
The user confirmed the final clean-polish build on New 3DS hardware.
The exact prepared game-file hashes are in GAME_FILES.json. That manifest
contains names, sizes and hashes only, not game data. The supported XE3U SHA-256:
549f30110c704b9d8e64e6deb0685fcb1a5051a991a5fb9bd4a92e5d789d71ac.
The source kit is for the community-modified N-Gage edition; it is not a generic
installer or general Marmalade compatibility guarantee.

## Main files

- main_3ds.cpp: physical input, presentation, bottom artwork, HOME/APT and NDSP.
- runtime_s3e.cpp: host services, guest callbacks, file/decompression routing,
  mixing, generated music and sampled effects.
- loader.cpp: generic XE3U relocation and all import-fixup kinds, including
  type-4 function pointers; guarded soft-key draw patch.
- cpu_native.cpp, native_arm.S, guest_interp.*: MGSM-derived native execution,
  trampolines, fault handling and displaced-instruction support.
- heap.*: guest allocation. save_store.h: FAT-compatible replacement/backup.
- runtime_clock.h: monotonic milliseconds. audio_gain.h: final PCM gain.

The native frontend uses 64 MiB of guest heap, 12 MiB of host heap, 4 MiB of
linear memory and a 1 MiB stack. Game output is 320×240 and presented in 2D.
Rendering is the game's own software path. NovaGL is not required by this build.

## Timer fix

s3eTimerGetMs returns a full 64-bit result in R0/R1. Leaving R1 unchanged fed
unrelated values into 64-bit elapsed-time comparisons. The demo vehicle selector
at stock-image offset 0x84198 uses such a comparison with 10000 ms. The fix
corrects the service ABI; it does not change the game's original interval.

## Soft-key drawing

loader.cpp validates the eight-byte prologue at image offset 0x75a44, then writes
Thumb BX LR to that dedicated artwork renderer in the loaded memory image.
The on-disk XE3U is untouched. The control mappings and physical key handlers
are elsewhere; no framebuffer rectangle or global sprite atlas is blanked.

## Audio, saves and presentation

Sound-event tracing is opt-in for diagnostics. The release discards routine
stdout rather than flushing SD logs per event. Mixer scratch allocations are
reused. Music and engine generators mix independently; the final New 3DS PCM
stage applies the 2× gain. Original 0font_tacho.bmp is retained, not reconstructed.
SaveStore rotates temp/main/backup files with rollback and backup recovery.
No diagnostic saves are shipped. The UI uses B=210 and START=209.

The artwork renderer and native timer fixes were desktop checked, then confirmed
by the hardware tester. This is not an exhaustive full-game completion test.
Other platforms, protected stock N-Gage packages, and original 3DS/2DS hardware
have not been validated by this NFS branch.
