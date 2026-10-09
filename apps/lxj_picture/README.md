# LXJ Picture

The original picture browser is preserved in `legacy/`. It directly owns the
e-paper renderer, image decoders, SD-card paths, and blocking button loop, so
it is not compiled into the current firmware yet.

The registered adapter is a separate LVGL application and does not replace
the existing wallpaper or reader applications.

