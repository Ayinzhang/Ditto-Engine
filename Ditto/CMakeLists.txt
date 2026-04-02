cmake_minimum_required(VERSION 3.16)
project(Ditto)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    3rdParty/GLAD/glad.c
    3rdParty/GLM/detail/glm.cpp
    3rdParty/ImGui/imgui.cpp
    3rdParty/ImGui/imgui_demo.cpp
    3rdParty/ImGui/imgui_draw.cpp
    3rdParty/ImGui/imgui_impl_glfw.cpp
    3rdParty/ImGui/imgui_impl_opengl3.cpp
    3rdParty/ImGui/imgui_impl_win32.cpp
    3rdParty/ImGui/imgui_tables.cpp
    3rdParty/ImGui/imgui_widgets.cpp
    Editor/Editor.cpp
    Editor/InspectorWindow.cpp
    Editor/SceneWindow.cpp
    Editor/LayoutManager.cpp
    Editor/ProjectWindow.cpp
    Engine/Core/CSharpScript.cpp
    Engine/Core/Engine.cpp
    Engine/Core/GameObject.cpp
    Engine/Core/ProjectManager.cpp
    Engine/Core/Scene.cpp
    Engine/Graphics/Camera.cpp
    Engine/Graphics/Shader.cpp
    Engine/Main.cpp
    Engine/Physics/CollisionShape.cpp
    Engine/Physics/ComputePhysics.cpp
    Engine/Physics/GJK.cpp
    Engine/Physics/ParallelPhysics.cpp
    Engine/Physics/ParallelPhysicsBarrier.cpp
    Engine/Physics/Physics.cpp
    Engine/Physics/ThreadPool.cpp
    Engine/Resources/Resource.cpp
)

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

# Define GLFW include directory for CMake
target_include_directories(Ditto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLFW)

# Copy Assets and shaders to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/Assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})