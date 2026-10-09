# LXJ TodoList

The original refer implementation is preserved under `legacy/` and is not
compiled directly. It owns the e-paper device and a blocking key loop, so it
must be adapted to the current LVGL application lifecycle before its legacy
page is enabled.

The current adapter is intentionally visible as a separate `LXJ TodoList`
application. It proves registration and lifecycle isolation without touching
the existing `TodoList` app.

