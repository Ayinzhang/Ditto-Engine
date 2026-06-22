cmake_minimum_required(VERSION 3.16)
project(Ditto)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

function(ditto_disable_vcpkg_msbuild TARGET_NAME)
    if(MSVC)
        # The Visual Studio vcpkg integration appends installed\x64-windows\lib\*.lib
        # to CMake-generated projects. Ditto links explicit local/SDK libraries, so
        # the wildcard pulls unrelated release CRT libs into Debug builds and runs
        # vcpkg's AppLocal pwsh step on machines without PowerShell Core.
        set_property(TARGET ${TARGET_NAME} PROPERTY VS_GLOBAL_VcpkgEnabled false)
    endif()
endfunction()

function(ditto_ignore_release_crt_defaultlib TARGET_NAME)
    if(MSVC)
        # Local third-party .lib files are release CRT builds. Keep Ditto Debug
        # objects on /MDd, but ignore the release CRT defaultlib they advertise.
        target_link_options(${TARGET_NAME} PRIVATE "$<$<CONFIG:Debug>:/NODEFAULTLIB:MSVCRT>")
    endif()
endfunction()

# Automatically collect source files using GLOB
file(GLOB_RECURSE SOURCES
    "3rdParty/GLAD/*.c"
    "3rdParty/GLM/detail/*.cpp"
    "3rdParty/ImGui/*.cpp"
    "3rdParty/ImGuizmo/*.cpp"
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
ditto_disable_vcpkg_msbuild(Ditto)
ditto_ignore_release_crt_defaultlib(Ditto)

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

enable_testing()

find_program(DOTNET_EXECUTABLE dotnet
    HINTS "$ENV{ProgramFiles}/dotnet" "$ENV{ProgramFiles\(x86\)}/dotnet")
if(DOTNET_EXECUTABLE)
    add_custom_target(DittoEngineCSharp
        COMMAND "${DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/Mono/DittoEngine.csproj" -c Release
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/Mono"
        COMMENT "Building DittoEngine C# API")
endif()

set(DITTO_HEADLESS_CORE_SOURCES
    Engine/Core/GameObject.cpp
    Engine/Core/Scene.cpp
    Engine/Core/SceneRaycast.cpp
    Engine/Core/SceneSerialization.cpp
    Engine/Core/PathUtils.cpp
    Engine/Core/JsonConfig.cpp
    Engine/Core/RuntimeContext.cpp
    Engine/Core/Logger.cpp
    Engine/Core/ProjectManager.cpp
    Engine/Core/CSharpScript.cpp
    Engine/Core/MonoRuntime.cpp
    Engine/Graphics/Camera.cpp
    Engine/Physics/Physics.cpp
    Engine/Physics/Physics2D.cpp
    Engine/Physics/PhysicsMaterial2DAsset.cpp
    Engine/Physics/CollisionShape.cpp
    Engine/Physics/GJK.cpp
    Engine/Resources/AssetDatabase.cpp
    Engine/Resources/AssetReferenceIO.cpp
    Engine/Resources/AssetPath.cpp
    Engine/Resources/Resource.cpp
    Engine/Graphics/Materials/MaterialAsset.cpp
    Engine/Graphics/Shaders/ShaderAsset.cpp
    Engine/Animation/AnimatorComponent.cpp
    Engine/Graphics/ParticleSystemComponent.cpp
)

add_executable(DittoTests
    Tests/TestMain.cpp
    ${DITTO_HEADLESS_CORE_SOURCES}
)
ditto_disable_vcpkg_msbuild(DittoTests)

target_compile_definitions(DittoTests PRIVATE DITTO_HEADLESS_TESTS=1 DITTO_HEADLESS_SCENE=1)
target_include_directories(DittoTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLAD
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLFW
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLM
)
target_link_libraries(DittoTests PRIVATE advapi32 user32)
if(TARGET DittoEngineCSharp)
    add_dependencies(DittoTests DittoEngineCSharp)
endif()

add_test(NAME Ditto.FileOps COMMAND DittoTests --stage file)
add_test(NAME Ditto.CSharp COMMAND DittoTests --stage csharp)
set_tests_properties(Ditto.FileOps PROPERTIES LABELS "file")
set_tests_properties(Ditto.CSharp PROPERTIES LABELS "csharp")

add_executable(DittoRenderSmoke
    Tests/RenderSmoke.cpp
    Tests/StbImage.cpp
    ${DITTO_HEADLESS_CORE_SOURCES}
    Engine/Graphics/RHI/GLRenderer.cpp
    Engine/Graphics/Shaders/ShaderCompiler.cpp
    Engine/Graphics/UI/FontAtlas.cpp
    Engine/Graphics/UI/UIRenderer.cpp
    3rdParty/GLAD/glad.c
    3rdParty/ImGui/imgui.cpp
    3rdParty/ImGui/imgui_draw.cpp
    3rdParty/ImGui/imgui_tables.cpp
    3rdParty/ImGui/imgui_widgets.cpp
    3rdParty/ImGui/imgui_impl_glfw.cpp
    3rdParty/ImGui/imgui_impl_opengl3.cpp
)
ditto_disable_vcpkg_msbuild(DittoRenderSmoke)
ditto_ignore_release_crt_defaultlib(DittoRenderSmoke)

if(Vulkan_FOUND)
    target_compile_definitions(DittoRenderSmoke PRIVATE DITTO_ENABLE_VULKAN=1)
    target_sources(DittoRenderSmoke PRIVATE
        Engine/Graphics/RHI/Vulkan/VulkanRenderer.cpp
        3rdParty/ImGui/imgui_impl_vulkan.cpp
    )
    target_link_libraries(DittoRenderSmoke PRIVATE Vulkan::Vulkan)
endif()

target_compile_definitions(DittoRenderSmoke PRIVATE DITTO_HEADLESS_TESTS=1)
target_include_directories(DittoRenderSmoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLAD
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLFW
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLM
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/ImGui
)
target_link_libraries(DittoRenderSmoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/GLFW/glfw3.lib
    opengl32
    gdi32
    user32
    shell32
    advapi32
)
if(TARGET DittoEngineCSharp)
    add_dependencies(DittoRenderSmoke DittoEngineCSharp)
endif()

add_test(NAME Ditto.Render.OpenGL
    COMMAND DittoRenderSmoke --backend opengl --out ${CMAKE_CURRENT_BINARY_DIR}/TestOutput/RenderSmokeGL)
set_tests_properties(Ditto.Render.OpenGL PROPERTIES LABELS "render;opengl")
if(Vulkan_FOUND)
    add_test(NAME Ditto.Render.Vulkan
        COMMAND DittoRenderSmoke --backend vulkan --out ${CMAKE_CURRENT_BINARY_DIR}/TestOutput/RenderSmokeVK)
    set_tests_properties(Ditto.Render.Vulkan PROPERTIES LABELS "render;vulkan")
endif()

add_test(NAME Ditto.Simulation COMMAND DittoTests --stage simulation)
set_tests_properties(Ditto.Simulation PROPERTIES LABELS "simulation")

add_test(NAME Ditto.FullFlow
    COMMAND powershell -ExecutionPolicy Bypass -File
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/RunBuiltTests.ps1
        -TestsExe $<TARGET_FILE:DittoTests>
        -RenderExe $<TARGET_FILE:DittoRenderSmoke>
        -RenderOut ${CMAKE_CURRENT_BINARY_DIR}/TestOutput/RenderSmoke
        -RunVulkan:$<BOOL:${Vulkan_FOUND}>)
set_tests_properties(Ditto.FullFlow PROPERTIES LABELS "full")
