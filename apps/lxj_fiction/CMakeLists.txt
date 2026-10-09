idf_component_register(
    SRCS
        "fiction_epub_adapter.cpp"
        "fiction_epub_rich_adapter.cpp"
        "legacy/epub/epub_converter.cc"
        "legacy/epub/Epub.cc"
        "legacy/epub/htmlEntities.cc"
        "legacy/epub/ZipFile.cc"
        "legacy/epub/miniz/miniz.c"
        "legacy/epub/tinyxml2/tinyxml2.cpp"
        "legacy/epub/rich/EpubRichReader.cpp"
        "legacy/epub/rich/RubbishHtmlParser.cpp"
        "legacy/epub/rich/blocks/TextBlock.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES log
)
