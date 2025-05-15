remove_definitions(-DTRANSLATION_DOMAIN="okular")
add_definitions(-DTRANSLATION_DOMAIN="okular_poppler")

add_subdirectory( conf )

set(CMAKE_REQUIRED_LIBRARIES Poppler::Qt6 Qt6::Core Qt6::Gui)

include_directories(
   ${CMAKE_CURRENT_SOURCE_DIR}/../..

   # ========== begin v3d ==========
   "../../../../../3rdParty/v3d-Common/"
   "../../../../../3rdParty/gzip-hpp/include"
   "../../core/"
   "/usr/include/tirpc"
   # ========== end v3d ==========
)

########### next target ###############

set(okularGenerator_poppler_PART_SRCS
   generator_pdf.cpp
   formfields.cpp
   annots.cpp
   pdfsignatureutils.cpp
   pdfsettingswidget.cpp
   imagescaling.cpp

   # ========== begin v3d ==========
   ../../../../../3rdParty/v3d-Common/Rendering/renderheadless.cpp
   ../../../../../3rdParty/v3d-Common/3rdParty/VulkanTools/VulkanTools.cpp
   ../../../../../3rdParty/v3d-Common/V3dFile/V3dFile.cpp
   ../../../../../3rdParty/v3d-Common/V3dFile/V3dObject.cpp
   ../../../../../3rdParty/v3d-Common/V3dFile/V3dObjects.cpp
   ../../../../../3rdParty/v3d-Common/V3dFile/V3dUtil.cpp
   ../../../../../3rdParty/v3d-Common/V3dFile/V3dHeaderInfo.cpp
   ../../../../../3rdParty/v3d-Common/Utility/Arcball.cpp
   ../../../../../3rdParty/v3d-Common/Utility/ProtectedFunctionCaller.cpp
   ../../../../../3rdParty/v3d-Common/Utility/EventFilter.cpp
   ../../../../../3rdParty/v3d-Common/V3dModel.cpp
   ../../../../../3rdParty/v3d-Common/V3dModelManager.cpp
   # ========== end v3d ==========
)

ki18n_wrap_ui(okularGenerator_poppler_PART_SRCS
   conf/pdfsettingswidget.ui
)

kconfig_add_kcfg_files(okularGenerator_poppler_PART_SRCS GENERATE_MOC conf/pdfsettings.kcfgc )

okular_add_generator(okularGenerator_poppler ${okularGenerator_poppler_PART_SRCS})

target_link_libraries(okularGenerator_poppler okularcore KF6::I18n KF6::Completion KF6::KIOWidgets Poppler::Qt6 Qt6::Xml

# ========== begin v3d ==========
vulkan tirpc z
# ========== end v3d ==========

)

########### autotests ###############

ecm_add_test(autotests/testimagescaling.cpp
    TEST_NAME "imageScalingTest"
    LINK_LIBRARIES Qt6::Test Qt6::Gui
)

########### install files ###############
install( PROGRAMS okularApplication_pdf.desktop org.kde.mobile.okular_pdf.desktop  DESTINATION  ${KDE_INSTALL_APPDIR} )
install( FILES org.kde.okular-poppler.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )
