# Tovo

Independent MVC app registered alongside the standard TodoList.
The app reads `/sdcard/lxj_todolist/todolist.csv`, falling back to the legacy
`/sdcard/todolist/todolist.csv`. A task click writes the existing compact CSV to the
first path. The previous LXJ adapter and reference source are in `archive/lxj_todolist`.

The app-specific storage code owns CSV parsing and writes; the reusable LVGL
list panel lives in `system/uilv/widgets`. The UI disables writes if the source
has too many tasks, truncated lines, or unsupported rows, to avoid silently
discarding data. Refresh reads the SD card again.
