idf_component_register(
    SRCS
        "app.c"
        "controller.c"
        "model.c"
        "storage.c"
        "view.c"
        "subpages/view_library.c"
        "subpages/view_reading.c"
        "epub/fiction_epub_adapter.cpp"
        "epub/fiction_epub_rich_adapter.cpp"
        "epub/engine/epub_converter.cc"
        "epub/engine/Epub.cc"
        "epub/engine/htmlEntities.cc"
        "epub/engine/ZipFile.cc"
        "epub/engine/miniz/miniz.c"
        "epub/engine/tinyxml2/tinyxml2.cpp"
        "epub/engine/rich/EpubRichReader.cpp"
        "epub/engine/rich/RubbishHtmlParser.cpp"
        "epub/engine/rich/blocks/TextBlock.cpp"
    INCLUDE_DIRS "." "subpages"
    REQUIRES lvgl
    PRIV_REQUIRES uilv appmgr controller flash
)
