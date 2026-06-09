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
)

# Filter out non-source files
list(FILTER SOURCES EXCLUDE REGEX ".*\\.cppm$")

# Find GLFW (use built-in if not found)
find_package(GLFW QUIET)
if(NOT GLFW_FOUND)
    set(GLFW_USE_SYSTEM_LIBRARY OFF)
    add_library(GLFW INTERFACE)
    target_include_directories(GLFW INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLFW")
else()
    message(STATUS "Using system GLFW")
endif()

# Vulkan (RHI backend). Uses the VULKAN_SDK environment set by the LunarG SDK.
find_package(Vulkan REQUIRED)

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
    Vulkan::Vulkan
    opengl32
    gdi32
    user32
    shell32
    comdlg32
    advapi32
)

# Define GLFW include directory for CMake
target_include_directories(Ditto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLFW)

# Copy Assets and shaders to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/Assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})