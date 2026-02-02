cmake_minimum_required(VERSION 3.20)
project(ShaderPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows-specific settings
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif()

# Find FFmpeg
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG IMPORTED_TARGET
        libavcodec
        libavformat
        libavutil
        libswscale
        libswresample
    )
endif()

# If pkg-config didn't find FFmpeg, try manual paths
if(NOT FFMPEG_FOUND)
    set(FFMPEG_ROOT "" CACHE PATH "FFmpeg installation root")
    if(FFMPEG_ROOT)
        set(FFMPEG_INCLUDE_DIRS "${FFMPEG_ROOT}/include")
        set(FFMPEG_LIBRARY_DIRS "${FFMPEG_ROOT}/lib")
        set(FFMPEG_LIBRARIES
            avcodec avformat avutil swscale swresample
        )
        link_directories(${FFMPEG_LIBRARY_DIRS})
    endif()
endif()

# ImGui sources (fetched or local)
include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.1
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

# Copy FFmpeg DLLs to output directory (if using shared FFmpeg)
if(FFMPEG_ROOT AND EXISTS "${FFMPEG_ROOT}/bin")
    file(GLOB FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
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
