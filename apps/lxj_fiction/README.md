# lxj_fiction

EPUB support is migrated through a compatibility adapter. The original ZIP,
OPF/NCX, HTML entity, and text conversion sources remain under `legacy/epub/`
and are now compiled by this component. Opening an EPUB creates or reuses a
text cache under `/sdcard/bookmarks/epub_cache/`; the existing LVGL reader then
uses the same paging path as TXT files.

The migrated rich reader now performs HTML layout for text pages and exposes
the NCX table of contents to the LVGL page for chapter jumps. If rich layout
cannot load a book, the text-cache converter remains available as a fallback.
The current EPUB chapter/page position is persisted under
`/sdcard/bookmarks/epub_progress/` using an FNV-1a path key and an atomic
temporary-file replacement.

PNG and baseline JPEG image blocks are extracted directly from the EPUB and
handed to LVGL's in-memory LodePNG/TJPGD decoders. Unsupported image formats
remain represented by the text/layout fallback.

The legacy e-paper renderer is still not used. This keeps display ownership in
the current LVGL/display service while preserving the parser's behavior.

