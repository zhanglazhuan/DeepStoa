# LXJ Audio

The original refer audio page and its headers are preserved in `legacy/`.
The registered adapter uses the existing `system/audio` playback session, so
it does not create a second I2S task or codec owner. The existing Music Player
application remains unchanged.

