# LXJ Audio

The original reference page and its headers are preserved in `legacy/`.
The former DeepStoa LXJ Audio entry listed `/sdcard/music` and controlled the
shared audio playback service. These active features now belong to `apps/player`:
SD card scanning, direct track selection, playback, pause, previous/next, stop,
volume, and current track status. The separate launcher entry was removed.

The reference firmware also contained recording and other hardware-specific
features that were never wired into the DeepStoa LXJ Audio entry. The active
audio output currently decodes PCM WAV; MP3 files appear as unsupported.

