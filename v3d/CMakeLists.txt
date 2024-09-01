remove_definitions(-DTRANSLATION_DOMAIN="okular")
add_definitions(-DTRANSLATION_DOMAIN="okular_v3d" -DGLFW_INCLUDE_VULKAN -DVULKAN_DEBUG=1 -DGLM_FORCE_RIGHT_HANDED)

include_directories(
    "src/Rendering" 
    "src/" 
    "src/V3dFile" 
    "dependencies/" 
    "dependencies/VulkanTools" 
    "/usr/include/tirpc" 
    "/home/benjaminb/kde/build/kparts/src/" 
    "/home/benjaminb/kde/build/kparts/" 
    "../3rdParty/V3D-Common"
    "/home/benjaminb/kde/src/okular/core"
    "../3rdParty/gzip-hpp/include" 
    "/usr/include/poppler/qt5"
)

add_compile_options(-fexceptions)

########### next target ###############
set(
    okularGenerator_v3d_SRCS src/v3d_generator.cpp 
    ../3rdParty/V3D-Common/Rendering/renderheadless.cpp 
    ../3rdParty/V3D-Common/3rdParty/VulkanTools/VulkanTools.cpp 
    ../3rdParty/V3D-Common/V3dFile/V3dFile.cpp 
    ../3rdParty/V3D-Common/V3dFile/V3dObjects.cpp 
    ../3rdParty/V3D-Common/V3dFile/V3dObject.cpp 
    ../3rdParty/V3D-Common/V3dFile/V3dUtil.cpp 
    ../3rdParty/V3D-Common/Utility/ProtectedFunctionCaller.cpp 
    ../3rdParty/V3D-Common/Utility/EventFilter.cpp 
    ../3rdParty/V3D-Common/Utility/Arcball.cpp
    ../3rdParty/V3D-Common/V3dModel.cpp
    ../3rdParty/V3D-Common/V3dModelManager.cpp
)

okular_add_generator(okularGenerator_v3d ${okularGenerator_v3d_SRCS})

target_link_libraries(okularGenerator_v3d okularcore Qt5::Core KF5::I18n GLEW glfw GL vulkan dl pthread X11 Xxf86vm Xrandr Xi tirpc KF5::Parts Okular5Core)

#list( APPEND CMAKE_INSTALL_RPATH "/usr/lib/dependencies" )
set_target_properties(okularGenerator_v3d
    PROPERTIES
    LINK_FLAGS "-L/home/benjaminb/kde/build/okular/bin/ -L/home/benjaminb/"
    #-Wl,-rpath,/usr/lib:/usr/lib/dependencies
)

########### install files ###############
install( PROGRAMS okularApplication_v3d.desktop org.kde.mobile.okular_v3d.desktop  DESTINATION  ${KDE_INSTALL_APPDIR} )
install( FILES org.kde.okular-v3d.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )

install( FILES v3d-mime.xml PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ DESTINATION "/usr/share/mime/packages" )
# IMPORTANT - need to update mime database
