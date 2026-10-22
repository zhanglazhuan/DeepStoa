# Reader

Reader scans `/sdcard/books` for `.txt` and `.epub` files when opened and when
Refresh is pressed. It shows up to 32 books. Books in the former
`/sdcard/fiction` directory must be moved to `/sdcard/books` by the user.
TXT pages are read from the SD card in bounded UTF-8-aligned chunks; per-book
page positions are stored in `/sdcard/bookmarks/reader_progress/`.

The EPUB parser and rich-reader adapter live in `epub/`. EPUB chapter/page
positions remain in `/sdcard/bookmarks/epub_progress/` and converted-text
caches in `/sdcard/bookmarks/epub_cache/`. Reader supports EPUB text, chapter
navigation and supported embedded images, with text-cache conversion as a
fallback when rich layout cannot load a book.

The old LXJ Fiction app and its original sources are preserved in
`archive/lxj_fiction/` and are not compiled or registered.
