# apps/anki/CMakeLists.txt
# Anki flashcard app — ported from EPOS
idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "controller.c"
        "view.c"
        "storage.c"
        "index_store.c"
        "blob_store.c"
        "rating_journal.c"
        "scheduler.c"
        "source_sync.c"
        "trash.c"
        "pages/controller_decks.c"
        "pages/controller_settings.c"
        "pages/controller_study.c"
        "pages/view_deck_form.c"
        "pages/view_decks.c"
        "pages/view_file_selector.c"
        "pages/view_import.c"
        "pages/view_trash.c"
        "pages/view_settings.c"
        "pages/view_stats.c"
        "pages/view_study.c"
    INCLUDE_DIRS "." "pages"
    REQUIRES driver lvgl
    PRIV_REQUIRES appmgr uilv controller time
)
