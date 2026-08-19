add_library(v3dCommon SHARED
    Rendering/renderheadless.cpp
    VulkanTools/VulkanTools.cpp
    V3dFile/V3dFile.cpp
    V3dFile/V3dObjects.cpp
    V3dFile/V3dObject.cpp
    V3dFile/V3dUtil.cpp
    V3dFile/V3dHeaderInfo.cpp
    Utility/ProtectedFunctionCaller.cpp
    Utility/EventFilter.cpp
    Utility/ApplicationEventFilter.cpp
    Utility/Arcball.cpp
    V3dModel.cpp
    V3dModelManager.cpp

    # Asymptote sources required by v3dCommon (globals used in V3dModelManager)
    ${CMAKE_CURRENT_SOURCE_DIR}/../asymptote/tinyexr/tinyexr.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../asymptote/tinyexr/deps/miniz/miniz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../asymptote/xstream.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../asymptote/bezierpatch.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../asymptote/beziercurve.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../asymptote/bound.cc
)

target_include_directories(v3dCommon PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${V3D_OKULAR_CORE_DIR}
    /usr/include/tirpc
    /usr/include/glslang
    ${CMAKE_CURRENT_SOURCE_DIR}/../asymptote
    ${CMAKE_CURRENT_SOURCE_DIR}/../asymptote/tinyexr
    ${CMAKE_CURRENT_SOURCE_DIR}/../asymptote/tinyexr/deps/miniz
    ${CMAKE_CURRENT_SOURCE_DIR}/../asymptote/prc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../asymptote/LspCpp/include
)

set(SPIRV_TOOLS_LIBRARIES "SPIRV-Tools")

find_package(SPIRV-Tools QUIET)
if(SPIRV-Tools_FOUND)
    set(SPIRV_TOOLS_LIBRARIES SPIRV-Tools)
endif()

set(SPIRV_TOOLS_OPT_LIBRARIES "SPIRV-Tools-opt")

find_package(SPIRV-Tools-opt QUIET)
if(SPIRV-Tools-opt_FOUND)
    set(SPIRV_TOOLS_OPT_LIBRARIES SPIRV-Tools-opt)
endif()

target_link_libraries(v3dCommon PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    okularcore
    vulkan
    tirpc
    glslang
    ${SPIRV_TOOLS_LIBRARIES}
    ${SPIRV_TOOLS_OPT_LIBRARIES}
)

target_compile_definitions(v3dCommon PUBLIC
    HAVE_LIBTIRPC
    HAVE_LIBGLM
    HAVE_RENDERER
)

target_compile_options(v3dCommon PUBLIC -fexceptions -fno-extended-identifiers)

# Override the project-level -fvisibility=hidden so that all v3dCommon symbols
# are exported from the shared library and linkable by both generators.
set_target_properties(v3dCommon PROPERTIES
    C_VISIBILITY_PRESET default
    CXX_VISIBILITY_PRESET default
    CMAKE_C_VISIBILITY_PRESET default
    CMAKE_CXX_VISIBILITY_PRESET default
)

install(TARGETS v3dCommon LIBRARY DESTINATION lib64/qt6/plugins/okular_generators)

