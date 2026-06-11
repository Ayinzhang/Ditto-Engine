cmake_minimum_required(VERSION 3.16)
project(Ditto)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Automatically collect source files using GLOB
file(GLOB_RECURSE SOURCES
    "3rdParty/GLAD/*.c"
    "3rdParty/GLM/detail/*.cpp"
    "3rdParty/ImGui/*.cpp"
    "Editor/*.cpp"
    "Engine/*.cpp"
    "Engine/*/*.cpp"
    "Engine/*/*/*.cpp"
)

# Filter out non-source files
list(FILTER SOURCES EXCLUDE REGEX ".*\\.cppm$")

# Vulkan is optional. Without the SDK, build an OpenGL-only editor/runtime.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Vulkan SDK found; building Vulkan RHI")
else()
    message(STATUS "Vulkan SDK not found; building OpenGL-only")
    list(FILTER SOURCES EXCLUDE REGEX ".*/Engine/Graphics/RHI/Vulkan/.*\\.cpp$")
    list(FILTER SOURCES EXCLUDE REGEX ".*/3rdParty/ImGui/imgui_impl_vulkan\\.cpp$")
endif()

# Find GLFW (use built-in if not found)
find_package(GLFW QUIET)
if(NOT GLFW_FOUND)
    set(GLFW_USE_SYSTEM_LIBRARY OFF)
    add_library(GLFW INTERFACE)
    target_include_directories(GLFW INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLFW")
else()
    message(STATUS "Using system GLFW")
endif()

# Executable
add_executable(Ditto ${SOURCES})

# Include directories
target_include_directories(Ditto PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLAD
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLFW
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLM
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/ImGui
)

# Link libraries - use local GLFW library
target_link_libraries(Ditto PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLFW/glfw3.lib
    opengl32
    gdi32
    user32
    shell32
    comdlg32
    advapi32
)

if(Vulkan_FOUND)
    target_compile_definitions(Ditto PRIVATE DITTO_ENABLE_VULKAN=1)
    target_link_libraries(Ditto PRIVATE Vulkan::Vulkan)
endif()

set(ASSIMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/Assimp")
set(ASSIMP_INCLUDE "${ASSIMP_ROOT}/include")
set(ASSIMP_LIB "${ASSIMP_ROOT}/lib/assimp-vc143-mt.lib")
set(ASSIMP_DLL "${ASSIMP_ROOT}/bin/assimp-vc143-mt.dll")
if(EXISTS "${ASSIMP_INCLUDE}/assimp/Importer.hpp" AND EXISTS "${ASSIMP_LIB}")
    message(STATUS "Assimp found in 3rdParty/Assimp")
    target_compile_definitions(Ditto PRIVATE DITTO_ENABLE_ASSIMP=1)
    target_include_directories(Ditto PRIVATE "${ASSIMP_INCLUDE}")
    target_link_libraries(Ditto PRIVATE "${ASSIMP_LIB}")
    if(EXISTS "${ASSIMP_DLL}")
        add_custom_command(TARGET Ditto POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ASSIMP_DLL}"
                "$<TARGET_FILE_DIR:Ditto>/assimp-vc143-mt.dll")
    endif()
else()
    message(STATUS "Assimp not found in 3rdParty/Assimp; using legacy mesh loaders only")
endif()

# Define GLFW include directory for CMake
target_include_directories(Ditto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLFW)

# Copy Assets and shaders to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/Assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
