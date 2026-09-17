# apps/anki/CMakeLists.txt
# Anki flashcard app — ported from EPOS
idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "controller.c"
        "view.c"
        "storage.c"
        "subpages/controller_decks.c"
        "subpages/controller_settings.c"
        "subpages/controller_study.c"
        "subpages/view_deck_form.c"
        "subpages/view_decks.c"
        "subpages/view_file_selector.c"
        "subpages/view_import.c"
        "subpages/view_settings.c"
        "subpages/view_stats.c"
        "subpages/view_study.c"
    INCLUDE_DIRS "." "subpages"
    REQUIRES driver lvgl
    PRIV_REQUIRES appmgr uilv controller
)
