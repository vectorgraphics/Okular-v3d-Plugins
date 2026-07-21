remove_definitions(-DTRANSLATION_DOMAIN="okular")
add_definitions(-DTRANSLATION_DOMAIN="okular_poppler")

add_subdirectory( conf )

set(CMAKE_REQUIRED_LIBRARIES Poppler::Qt6 Qt6::Core Qt6::Gui)

include_directories(
   ${CMAKE_CURRENT_SOURCE_DIR}/../..

   # v3d includes (v3dCommon provides most via PUBLIC; keep gzip-hpp here)
   "../../../../../3rdParty/gzip-hpp/include"
)

########### next target ###############

set(okularGenerator_poppler_PART_SRCS
   generator_pdf.cpp
   formfields.cpp
   annots.cpp
   pdfsignatureutils.cpp
   pdfsettingswidget.cpp
   imagescaling.cpp
)

ki18n_wrap_ui(okularGenerator_poppler_PART_SRCS
   conf/pdfsettingswidget.ui
)

kconfig_add_kcfg_files(okularGenerator_poppler_PART_SRCS GENERATE_MOC conf/pdfsettings.kcfgc )

okular_add_generator(okularGenerator_poppler ${okularGenerator_poppler_PART_SRCS})

target_link_libraries(okularGenerator_poppler okularcore KF6::I18n KF6::Completion KF6::KIOWidgets Poppler::Qt6 Qt6::Xml
   # Link against v3dCommon for shared v3d rendering/parsing code.
   # v3dCommon PUBLIC definitions propagate: HAVE_LIBTIRPC, HAVE_LIBGLM, HAVE_RENDERER.
   # v3dCommon PUBLIC libraries propagate: vulkan, tirpc, glslang, SPIRV-Tools, SPIRV-Tools-opt.
   v3dCommon
   z  # zlib for gzip-hpp (used by PDF generator directly)
)

# Set RUNPATH to $ORIGIN so the generator finds libv3dCommon.so in its own
# installation directory (okular_generators/) regardless of distro path.
# Override all CMake automatic RPATH to prevent build-dir leakage.
set_target_properties(okularGenerator_poppler PROPERTIES
    SKIP_BUILD_RPATH TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "\$ORIGIN"
)

########### autotests ###############

if(BUILD_TESTING)
    ecm_add_test(autotests/testimagescaling.cpp
        TEST_NAME "imageScalingTest"
        LINK_LIBRARIES Qt6::Test Qt6::Gui
    )
endif()

########### install files ###############
if (BUILD_DESKTOP)
  install( PROGRAMS okularApplication_pdf.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
if (BUILD_MOBILE)
  install( PROGRAMS org.kde.mobile.okular_pdf.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
install( FILES org.kde.okular-poppler.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )

