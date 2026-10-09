# LXJ legacy applications

This directory is the namespace boundary for applications migrated from
`refer/esp32_paper-v2.0`.

## Rules

- Every migrated application lives in its own `apps/lxj_<name>/` directory.
- LXJ applications are independent applications; they must not replace or
  merge with the existing `apps/` applications.
- The `lxj_` prefix applies to directories, public symbols, app names, and
  app-specific events.
- An LXJ application may depend on `system` services and drivers through their
  public APIs, but must not call another application or access hardware
  directly.
- A migrated application is not added to the firmware registration list until
  it has passed its own compile and runtime smoke test.

## Migration order

The initial candidates are:

1. `lxj_clock`
2. `lxj_todolist`
3. `lxj_picture`
4. `lxj_audio`
5. `lxj_fiction`
6. `lxj_chat`

The source under `refer/esp32_paper-v2.0` remains the reference copy. Ported
code belongs under its `apps/lxj_<name>/` directory and must be adapted to the
current `app_manager`, `uilv`, `events`, filesystem, audio, and network APIs.

