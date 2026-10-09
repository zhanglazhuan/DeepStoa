# LXJ Clock

This is the isolated migration area for `refer/esp32_paper-v2.0/main/page_clock`.

The files under `legacy/` are a source snapshot only. They are deliberately
not part of the ESP-IDF component graph and are not registered with
`app_manager` yet. The reference implementation owns the e-paper bus,
button polling loop, RTC access, and refresh lifecycle directly; compiling it
inside the current firmware before those boundaries are adapted would allow
two owners to drive the same display.

The eventual adapter must expose the normal application lifecycle:

```c
void lxj_clock_init(void);
```

and translate the legacy page operations to current `system` services before
this directory receives a `CMakeLists.txt` entry or is added to the boot
registration list.

