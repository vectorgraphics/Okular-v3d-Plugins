remove_definitions(-DTRANSLATION_DOMAIN="okular")
add_definitions(-DTRANSLATION_DOMAIN="okular_epub")

add_subdirectory(data)

########### next target ###############

set(okularGenerator_epub_PART_SRCS
  converter.cpp
  epubdocument.cpp
  generator_epub.cpp
)

okular_add_generator(okularGenerator_epub ${okularGenerator_epub_PART_SRCS})

target_include_directories(okularGenerator_epub PRIVATE ${EPUB_INCLUDE_DIR})
target_link_libraries(okularGenerator_epub okularcore ${EPUB_LIBRARIES} KF6::I18n Qt6::Widgets Qt6::Xml)


########### autotests ###############

add_definitions( -DKDESRCDIR="${CMAKE_CURRENT_SOURCE_DIR}/" )
ecm_add_test(autotests/epubgeneratortest.cpp
    TEST_NAME "epubgeneratortest"
    LINK_LIBRARIES Qt6::Test KF6::CoreAddons okularcore
)


########### install files ###############
if (BUILD_DESKTOP)
  install( PROGRAMS okularApplication_epub.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
if (BUILD_MOBILE)
  install( PROGRAMS org.kde.mobile.okular_epub.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
install( FILES org.kde.okular-epub.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )

