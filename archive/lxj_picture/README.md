# LXJ Picture

The original picture browser is preserved in `legacy/`. It directly owns the
e-paper renderer, image decoders, SD-card paths, and blocking button loop, so
it is not compiled into the current firmware yet.

The former LVGL adapter only listed image filenames from `/sdcard/picture`;
it did not preview images or set wallpapers. The picture app is archived and
is no longer registered in firmware. The separate `apps/wallpaper` module
contains image display logic but is not currently initialized at startup.

