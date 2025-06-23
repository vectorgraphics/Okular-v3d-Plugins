remove_definitions(-DTRANSLATION_DOMAIN="okular")
add_definitions(-DTRANSLATION_DOMAIN="okular_v3d" -DGLFW_INCLUDE_VULKAN -DVULKAN_DEBUG=1 -DGLM_FORCE_RIGHT_HANDED)

include_directories(
    "../../../../../3rdParty/v3d-Common/"
    "../../../../../3rdParty/v3d-Common/3rdParty/"
    "../../core/"
    "$ENV{VK_SDK_PATH}/Include"
)

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
    ../../../../../3rdParty/v3d-Common/3rdParty/xstream.cc
    ../../../../../3rdParty/v3d-Common/3rdParty/win32xdr.cc
)

link_directories("$ENV{VK_SDK_PATH}/Lib")

okular_add_generator(okularGenerator_v3d ${okularGenerator_v3d_SRCS})

include(FetchContent)

if (WIN32)
    # win32 does not have native open_memstream support
    FetchContent_Declare(
            fmem
            GIT_REPOSITORY https://github.com/Kreijstal/fmem.git
            GIT_TAG 5f79fef3606be5dac54d62c7d0e2123363afabd7
    )
    FetchContent_MakeAvailable(fmem)
    include_directories($<TARGET_PROPERTY:fmem,INCLUDE_DIRECTORIES>)
endif()

FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG master
)
FetchContent_MakeAvailable(glm)
include_directories($<TARGET_PROPERTY:glm,INCLUDE_DIRECTORIES>)

target_link_libraries(okularGenerator_v3d okularcore KF6::I18n vulkan-1 fmem Ws2_32)

add_compile_definitions(HAVE_LIBTIRPC)

########### install files ###############
install(PROGRAMS okularApplication_v3d.desktop DESTINATION ${KDE_INSTALL_APPDIR})
