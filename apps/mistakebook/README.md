# Mistakebook

The active app follows the project's `app` / `model` / `controller` / `view`
structure. Its list and detail screens use `page_navigator` and `lv_page`.
`storage.c` reads the existing `/sdcard/mistakebook/cards.csv` format; no SD
card migration is required. The original `lxj_mistakebook` sources are
preserved under `archive/lxj_mistakebook/`; this is the active app.

The app displays at most 32 cards. If a file has more cards, malformed rows,
or fields longer than 159 bytes, review writes are disabled so saving cannot
silently discard source data.

