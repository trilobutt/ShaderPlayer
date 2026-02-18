cmake_minimum_required(VERSION 3.20)
project(ShaderPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows-specific settings
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif()

# Find FFmpeg
# Priority: 1) third_party/ffmpeg (bundled, portable), 2) pkg-config, 3) FFMPEG_ROOT
set(_FFMPEG_BUNDLED "${CMAKE_SOURCE_DIR}/third_party/ffmpeg")

if(EXISTS "${_FFMPEG_BUNDLED}/include/libavcodec/avcodec.h")
    message(STATUS "FFmpeg: using bundled third_party/ffmpeg")
    set(FFMPEG_INCLUDE_DIRS "${_FFMPEG_BUNDLED}/include")
    set(FFMPEG_LIBRARY_DIRS "${_FFMPEG_BUNDLED}/lib")
    set(FFMPEG_BIN_DIR      "${_FFMPEG_BUNDLED}/bin")
    set(FFMPEG_LIBRARIES    avcodec avformat avutil swscale swresample)
    set(FFMPEG_FOUND TRUE)
    link_directories(${FFMPEG_LIBRARY_DIRS})
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG IMPORTED_TARGET
            libavcodec libavformat libavutil libswscale libswresample
        )
        if(FFMPEG_FOUND)
            message(STATUS "FFmpeg: found via pkg-config")
        endif()
    endif()

    if(NOT FFMPEG_FOUND)
        set(FFMPEG_ROOT "" CACHE PATH "FFmpeg installation root (fallback when third_party/ffmpeg is absent)")
        if(FFMPEG_ROOT)
            message(STATUS "FFmpeg: using FFMPEG_ROOT=${FFMPEG_ROOT}")
            set(FFMPEG_INCLUDE_DIRS "${FFMPEG_ROOT}/include")
            set(FFMPEG_LIBRARY_DIRS "${FFMPEG_ROOT}/lib")
            set(FFMPEG_BIN_DIR      "${FFMPEG_ROOT}/bin")
            set(FFMPEG_LIBRARIES    avcodec avformat avutil swscale swresample)
            link_directories(${FFMPEG_LIBRARY_DIRS})
        else()
            message(FATAL_ERROR
                "FFmpeg not found. Either:\n"
                "  1. Populate third_party/ffmpeg/ with include/ and lib/ from a shared FFmpeg build\n"
                "  2. Pass -DFFMPEG_ROOT=<path> to cmake")
        endif()
    endif()
endif()

# ImGui sources (fetched or local)
include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    imgui_color_text_edit
    GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
    GIT_TAG master
)
FetchContent_MakeAvailable(imgui_color_text_edit)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# ImGui library
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_color_text_edit_SOURCE_DIR}/TextEditor.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_color_text_edit_SOURCE_DIR}
)

# Main executable
add_executable(ShaderPlayer WIN32
    src/main.cpp
    src/Application.cpp
    src/VideoDecoder.cpp
    src/D3D11Renderer.cpp
    src/ShaderManager.cpp
    src/VideoEncoder.cpp
    src/UIManager.cpp
    src/ConfigManager.cpp
)

target_include_directories(ShaderPlayer PRIVATE
    src
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(ShaderPlayer PRIVATE
    imgui_lib
    nlohmann_json::nlohmann_json
    d3d11
    d3dcompiler
    dxgi
    dxguid
    ${FFMPEG_LIBRARIES}
)

# Copy FFmpeg DLLs to output directory
if(DEFINED FFMPEG_BIN_DIR AND EXISTS "${FFMPEG_BIN_DIR}")
    file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
    foreach(DLL ${FFMPEG_DLLS})
        add_custom_command(TARGET ShaderPlayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL}" $<TARGET_FILE_DIR:ShaderPlayer>
        )
    endforeach()
endif()

# Install
install(TARGETS ShaderPlayer RUNTIME DESTINATION bin)
install(DIRECTORY shaders/ DESTINATION bin/shaders)
install(FILES config.json DESTINATION bin)
