remove_definitions(-DTRANSLATION_DOMAIN="okular")
add_definitions(-DTRANSLATION_DOMAIN="okular_xps")

########### next target ###############

set(okularGenerator_xps_SRCS
   generator_xps.cpp
)

okular_add_generator(okularGenerator_xps ${okularGenerator_xps_SRCS})

target_link_libraries(okularGenerator_xps okularcore KF6::Archive KF6::I18n Qt6::PrintSupport Qt6::Xml)

########### install files ###############
if (BUILD_DESKTOP)
  install( PROGRAMS okularApplication_xps.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
if (BUILD_MOBILE)
  install( PROGRAMS org.kde.mobile.okular_xps.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
install( FILES org.kde.okular-xps.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )
