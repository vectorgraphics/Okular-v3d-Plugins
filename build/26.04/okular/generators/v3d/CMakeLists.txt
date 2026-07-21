remove_definitions(-DTRANSLATION_DOMAIN="okular")
add_definitions(
    -DTRANSLATION_DOMAIN="okular_v3d" 
    -DGLFW_INCLUDE_VULKAN 
    -DVULKAN_DEBUG=1 
    -DGLM_FORCE_RIGHT_HANDED
    -DASYMPTOTE_SYSDIR=""
    -DASYMPTOTE_DOCDIR=""
    -DPACKAGE_NAME="Okular-Plugins"
)

include_directories(
    "../../core/"
)

set(
    okularGenerator_v3d_SRCS 
    src/v3d_generator.cpp
)

okular_add_generator(okularGenerator_v3d ${okularGenerator_v3d_SRCS})

# Link against v3dCommon which provides all shared v3d code (Vulkan renderer,
# V3dFile parser, V3dModelManager, event filters, arcball, etc.).
# v3dCommon PUBLIC definitions propagate: HAVE_LIBTIRPC, HAVE_LIBGLM, HAVE_RENDERER.
target_link_libraries(okularGenerator_v3d okularcore KF6::I18n v3dCommon)

# Set RUNPATH to $ORIGIN so the generator finds libv3dCommon.so in its own
# installation directory (okular_generators/) regardless of distro path.
# Override all CMake automatic RPATH to prevent build-dir leakage.
set_target_properties(okularGenerator_v3d PROPERTIES
    SKIP_BUILD_RPATH TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "\$ORIGIN"
)

########### install files ###############
install(PROGRAMS okularApplication_v3d.desktop DESTINATION ${KDE_INSTALL_APPDIR})

