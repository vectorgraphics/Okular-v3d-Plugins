remove_definitions(-DTRANSLATION_DOMAIN="okular")
add_definitions(-DTRANSLATION_DOMAIN="okular_v3d" -DGLFW_INCLUDE_VULKAN -DVULKAN_DEBUG=1 -DGLM_FORCE_RIGHT_HANDED)

include_directories(
    "../../../../../3rdParty/v3d-Common/"
    "../../core/"
    "/usr/include/tirpc" 
)

add_compile_options(-fexceptions)

########### next target ###############
set(
    okularGenerator_v3d_SRCS 
    src/v3d_generator.cpp 

    ../../../../../3rdParty/v3d-Common/Rendering/renderheadless.cpp
    ../../../../../3rdParty/v3d-Common/3rdParty/VulkanTools/VulkanTools.cpp
    ../../../../../3rdParty/v3d-Common/V3dFile/V3dFile.cpp
    ../../../../../3rdParty/v3d-Common/V3dFile/V3dObjects.cpp
    ../../../../../3rdParty/v3d-Common/V3dFile/V3dObject.cpp
    ../../../../../3rdParty/v3d-Common/V3dFile/V3dUtil.cpp
    ../../../../../3rdParty/v3d-Common/V3dFile/V3dHeaderInfo.cpp
    ../../../../../3rdParty/v3d-Common/Utility/ProtectedFunctionCaller.cpp
    ../../../../../3rdParty/v3d-Common/Utility/EventFilter.cpp
    ../../../../../3rdParty/v3d-Common/Utility/Arcball.cpp
    ../../../../../3rdParty/v3d-Common/V3dModel.cpp
    ../../../../../3rdParty/v3d-Common/V3dModelManager.cpp
)

okular_add_generator(okularGenerator_v3d ${okularGenerator_v3d_SRCS})

target_link_libraries(okularGenerator_v3d
    okularcore
    KF6::I18n
    vulkan
    tirpc
)

########### install files ###############
install( PROGRAMS okularApplication_v3d.desktop org.kde.mobile.okular_v3d.desktop  DESTINATION  ${KDE_INSTALL_APPDIR} )
install( FILES org.kde.okular-v3d.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )
