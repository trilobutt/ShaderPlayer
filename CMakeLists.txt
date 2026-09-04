cmake_minimum_required(VERSION 3.20)
project(ShaderPlayer VERSION 1.0.0 LANGUAGES CXX C)

# kissfft and spout2 use FetchContent_Populate directly to avoid running their
# upstream CMakeLists (which would pull in unwanted targets / break WIN32_LEAN_AND_MEAN).
# CMP0169 OLD suppresses the deprecation warning for this intentional usage.
cmake_policy(SET CMP0169 OLD)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# CMakePresets.json points CMAKE_PREFIX_PATH at the conventional Qt install path, but a
# clone lands wherever that machine put Qt, and a REQUIRED find_package turns any other
# location into a hard failure before anything else has been checked. Try the configured
# path first, then the environment, then the standard Windows install root.
find_package(Qt6 6.9 QUIET COMPONENTS Widgets Gui Core)
if(NOT Qt6_FOUND)
    set(_SP_QT_CANDIDATES "$ENV{QT_ROOT_DIR}" "$ENV{QTDIR}" "$ENV{Qt6_DIR}")
    file(GLOB _SP_QT_INSTALLS "$ENV{SystemDrive}/Qt/6.*/msvc*_64" "C:/Qt/6.*/msvc*_64")
    list(REMOVE_DUPLICATES _SP_QT_INSTALLS)
    list(SORT _SP_QT_INSTALLS COMPARE NATURAL ORDER DESCENDING)  # newest Qt first
    list(APPEND _SP_QT_CANDIDATES ${_SP_QT_INSTALLS})
    foreach(_SP_QT_DIR IN LISTS _SP_QT_CANDIDATES)
        if(_SP_QT_DIR AND EXISTS "${_SP_QT_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
            message(STATUS "Qt6: discovered at ${_SP_QT_DIR}")
            list(APPEND CMAKE_PREFIX_PATH "${_SP_QT_DIR}")
            break()
        endif()
    endforeach()
    find_package(Qt6 6.9 REQUIRED COMPONENTS Widgets Gui Core)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# A cache whose CMAKE_CXX_FLAGS* entries have been blanked (an IDE, a bad edit, a
# half-written configure) is unrecoverable by re-running the preset: CMake initialises
# those entries once and never again. The build then succeeds, links, and runs, while
# silently dropping /O2, /EHsc, /Zi and /DNDEBUG. That produced an unoptimised binary with
# no debug symbols, live asserts, and no unwind semantics, and nothing said so beyond a
# C4530 from <chrono>. Fail loudly instead; the fix is to delete CMakeCache.txt and
# CMakeFiles/ and reconfigure.
if(MSVC)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _sp_build_type)
    if(NOT CMAKE_CXX_FLAGS MATCHES "/EHsc")
        message(FATAL_ERROR
            "CMAKE_CXX_FLAGS is missing /EHsc (value: '${CMAKE_CXX_FLAGS}'). This cache has "
            "lost its compiler defaults. Delete build/CMakeCache.txt and build/CMakeFiles/, "
            "then re-run: cmake --preset windows-msvc")
    endif()
    if(_sp_build_type STREQUAL "RELWITHDEBINFO" OR _sp_build_type STREQUAL "RELEASE")
        if(NOT CMAKE_CXX_FLAGS_${_sp_build_type} MATCHES "/O2")
            message(FATAL_ERROR
                "CMAKE_CXX_FLAGS_${_sp_build_type} is missing /O2 (value: "
                "'${CMAKE_CXX_FLAGS_${_sp_build_type}}'). This cache has lost its compiler "
                "defaults and would produce an unoptimised build under a release name. "
                "Delete build/CMakeCache.txt and build/CMakeFiles/, then re-run: "
                "cmake --preset windows-msvc")
        endif()
    endif()
endif()

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
    # avdevice is deliberately absent: it is needed only for DirectShow webcam capture,
    # it is the most expensive FFmpeg DLL to map (~25 ms of every startup), and
    # VideoDecoder::EnsureDevicesRegistered resolves its one entry point through
    # LoadLibrary instead. Its DLL is still copied to the output directory below.
    set(FFMPEG_LIBRARIES    avcodec avformat avutil swscale swresample)
    set(FFMPEG_FOUND TRUE)
    link_directories(${FFMPEG_LIBRARY_DIRS})
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG IMPORTED_TARGET
            libavcodec libavdevice libavformat libavutil libswscale libswresample
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

include(FetchContent)

# extra-cmake-modules ships CMake modules only, but KSyntaxHighlighting locates it with
# find_package(ECM NO_MODULE) and then *overwrites* CMAKE_MODULE_PATH with ECM_MODULE_PATH,
# so a populated source tree is not enough: a real ECMConfig.cmake must exist on disk.
# Configure and install ECM into the build tree at configure time. Nothing is compiled
# (ECM is pure .cmake files), so this is a file copy and runs once.
FetchContent_Declare(ecm
    GIT_REPOSITORY https://invent.kde.org/frameworks/extra-cmake-modules.git
    GIT_TAG v6.10.0 GIT_SHALLOW TRUE)
FetchContent_GetProperties(ecm)
if(NOT ecm_POPULATED)
    FetchContent_Populate(ecm)
endif()
set(_ECM_PREFIX "${CMAKE_BINARY_DIR}/ecm-install")
if(NOT EXISTS "${_ECM_PREFIX}/share/ECM/cmake/ECMConfig.cmake")
    message(STATUS "ECM: configuring and installing into ${_ECM_PREFIX}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${ecm_SOURCE_DIR}" -B "${ecm_BINARY_DIR}"
                "-DCMAKE_INSTALL_PREFIX=${_ECM_PREFIX}" -DBUILD_TESTING=OFF
        RESULT_VARIABLE _ECM_RESULT OUTPUT_QUIET)
    if(NOT _ECM_RESULT EQUAL 0)
        message(FATAL_ERROR "ECM configure failed (${_ECM_RESULT})")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${ecm_BINARY_DIR}"
        RESULT_VARIABLE _ECM_RESULT OUTPUT_QUIET)
    if(NOT _ECM_RESULT EQUAL 0)
        message(FATAL_ERROR "ECM install failed (${_ECM_RESULT})")
    endif()
endif()
set(ECM_DIR "${_ECM_PREFIX}/share/ECM/cmake")

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

# KSyntaxHighlighting does find_package(Perl REQUIRED) to auto-generate its PHP syntax
# definitions, and Windows ships no Perl. Git for Windows carries one at usr/bin/perl.exe,
# but only Git's cmd/ directory goes on PATH, so FindPerl misses it and the configure dies
# here, long after Qt and FFmpeg have been found, which makes it read like a Qt problem.
# Resolve it ourselves, and fail with an actionable message rather than FindPerl's.
if(NOT PERL_EXECUTABLE)
    find_program(PERL_EXECUTABLE NAMES perl)
endif()
if(NOT PERL_EXECUTABLE AND WIN32)
    find_program(_SP_GIT NAMES git)
    if(_SP_GIT)
        # <root>/cmd/git.exe or <root>/bin/git.exe -> <root>
        get_filename_component(_SP_GIT_ROOT "${_SP_GIT}" DIRECTORY)
        get_filename_component(_SP_GIT_ROOT "${_SP_GIT_ROOT}" DIRECTORY)
    endif()
    find_program(PERL_EXECUTABLE NAMES perl
        HINTS "${_SP_GIT_ROOT}/usr/bin"
              "$ENV{ProgramFiles}/Git/usr/bin"
              "$ENV{SystemDrive}/Strawberry/perl/bin"
        NO_DEFAULT_PATH)
    if(PERL_EXECUTABLE)
        message(STATUS "Perl: discovered at ${PERL_EXECUTABLE}")
    else()
        message(FATAL_ERROR
            "Perl not found, and KSyntaxHighlighting requires it to generate its syntax "
            "definitions. Install Git for Windows (which bundles one) or Strawberry Perl, "
            "or pass -DPERL_EXECUTABLE=<path to perl.exe> to cmake.")
    endif()
endif()

# ECM's KDE_CLANG_FORMAT (KDEClangFormat.cmake), pulled in by KSyntaxHighlighting via
# KDEFrameworkCompilerSettings, calls add_custom_command(TARGET clang-format COMMAND ...)
# with none of PRE_BUILD/PRE_LINK/POST_BUILD on the branch it takes when no clang-format
# is on the machine. CMake 3.31 added CMP0175 to reject that, and leaving the policy unset
# emits a dev warning on every configure that lands in the VS Code Problems panel. The
# code is upstream's and the target is inert (it only echoes a message), so pin the policy
# OLD for the duration of this dependency rather than carrying the warning. Restore it
# straight after so nothing else in the tree silently inherits a relaxed policy.
set(_SP_SAVED_CMP0175 "${CMAKE_POLICY_DEFAULT_CMP0175}")
set(CMAKE_POLICY_DEFAULT_CMP0175 OLD)

FetchContent_Declare(ksyntaxhighlighting
    GIT_REPOSITORY https://invent.kde.org/frameworks/syntax-highlighting.git
    GIT_TAG v6.10.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ksyntaxhighlighting)

set(CMAKE_POLICY_DEFAULT_CMP0175 "${_SP_SAVED_CMP0175}")
unset(_SP_SAVED_CMP0175)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
    kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG        131.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(kissfft)
if(NOT kissfft_POPULATED)
    FetchContent_Populate(kissfft)
endif()

add_library(kissfft_lib STATIC
    ${kissfft_SOURCE_DIR}/kiss_fft.c
    ${kissfft_SOURCE_DIR}/kiss_fftr.c
)
target_include_directories(kissfft_lib PUBLIC
    ${kissfft_SOURCE_DIR}
)
target_compile_definitions(kissfft_lib PUBLIC kiss_fft_scalar=float)
if(MSVC)
    target_compile_options(kissfft_lib PRIVATE /W0)
else()
    target_compile_options(kissfft_lib PRIVATE -w)
endif()

FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.25
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()

FetchContent_Declare(
    spout2
    GIT_REPOSITORY https://github.com/leadedge/Spout2.git
    GIT_TAG        2.007h
    GIT_SHALLOW    TRUE
)
# Use EXCLUDE_FROM_ALL / SOURCE_SUBDIR trick: declare a dummy subdir so
# MakeAvailable doesn't recurse into Spout2's CMakeLists (which builds GL libs
# we don't need and has WIN32_LEAN_AND_MEAN conflicts).
FetchContent_GetProperties(spout2)
if(NOT spout2_POPULATED)
    FetchContent_Populate(spout2)  # downloads sources, sets spout2_SOURCE_DIR
endif()

set(SPOUT_DX_DIR  ${spout2_SOURCE_DIR}/SPOUTSDK/SpoutDirectX/SpoutDX)
set(SPOUT_GL_DIR  ${spout2_SOURCE_DIR}/SPOUTSDK/SpoutGL)

add_library(spout_lib STATIC
    ${SPOUT_DX_DIR}/SpoutDX.cpp
    ${SPOUT_GL_DIR}/SpoutDirectX.cpp
    ${SPOUT_GL_DIR}/SpoutSenderNames.cpp
    ${SPOUT_GL_DIR}/SpoutSharedMemory.cpp
    ${SPOUT_GL_DIR}/SpoutUtils.cpp
    ${SPOUT_GL_DIR}/SpoutFrameCount.cpp
    ${SPOUT_GL_DIR}/SpoutCopy.cpp
)
target_include_directories(spout_lib PUBLIC
    ${SPOUT_DX_DIR}
    ${SPOUT_GL_DIR}
)
target_link_libraries(spout_lib PRIVATE d3d11 dxgi winmm)
# Suppress warnings; also undefine WIN32_LEAN_AND_MEAN so SpoutFrameCount.cpp
# can pull in <mmsystem.h> for timeGetDevCaps/timeBeginPeriod/timeEndPeriod.
if(MSVC)
    target_compile_options(spout_lib PRIVATE /W0 /UWIN32_LEAN_AND_MEAN)
else()
    target_compile_options(spout_lib PRIVATE -w)
endif()

# src/ShaderCommon.hlsli is the single source of truth for the injected shader
# helper library: embedded here as a raw string literal for the runtime compiler,
# and read directly off disk by tools/validate_shaders.py.
set(SHADER_COMMON_HLSLI "${CMAKE_SOURCE_DIR}/src/ShaderCommon.hlsli")
set(SHADER_COMMON_HEADER "${CMAKE_BINARY_DIR}/generated/ShaderCommonEmbedded.h")
add_custom_command(
    OUTPUT  "${SHADER_COMMON_HEADER}"
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${SHADER_COMMON_HLSLI}
            -DOUTPUT=${SHADER_COMMON_HEADER}
            -DSYMBOL=kShaderCommonHLSL
            -P "${CMAKE_SOURCE_DIR}/tools/embed_hlsli.cmake"
    DEPENDS "${SHADER_COMMON_HLSLI}" "${CMAKE_SOURCE_DIR}/tools/embed_hlsli.cmake"
    COMMENT "Embedding ShaderCommon.hlsli"
    VERBATIM
)

# Main executable
add_executable(ShaderPlayer WIN32
    ${SHADER_COMMON_HEADER}
    src/main.cpp
    src/Application.cpp
    src/FrameProfiler.cpp
    src/AudioAnalyzer.cpp
    src/VideoDecoder.cpp
    src/D3D11Renderer.cpp
    src/ShaderManager.cpp
    src/VideoEncoder.cpp
    src/ConfigManager.cpp
    src/WorkspaceManager.cpp
    src/KeyframeTimeline.cpp
    src/VideoOutputWindow.cpp
    src/SpoutOutput.cpp
    src/AudioPlayer.cpp
    src/ui/Theme.cpp
    src/ui/ViewportWidget.cpp
    src/ui/HlslHighlighter.cpp
    src/ui/EditorPanel.cpp
    src/ui/LibraryPanel.cpp
    src/ui/ParamsPanel.cpp
    src/ui/KeyframeDetail.cpp
    src/ui/TransportPanel.cpp
    src/ui/RecordingPanel.cpp
    src/ui/NoisePanel.cpp
    src/ui/SpoutPanel.cpp
    src/ui/AudioPanel.cpp
    src/ui/dialogs/Dialog.cpp
    src/ui/dialogs/KeybindingDialog.cpp
    src/ui/dialogs/KeybindingsReferenceDialog.cpp
    src/ui/dialogs/NewShaderDialog.cpp
    src/ui/dialogs/CaptureDialog.cpp
    src/ui/dialogs/WorkspacesDialog.cpp
    src/ui/Toast.cpp
    src/ui/BezierEditor.cpp
    src/ui/MainWindow.cpp
    resources/resources.qrc
    resources/ShaderPlayer.rc
)

target_include_directories(ShaderPlayer PRIVATE
    src
    ${CMAKE_BINARY_DIR}/generated
    ${FFMPEG_INCLUDE_DIRS}
    ${miniaudio_SOURCE_DIR}
)

target_link_libraries(ShaderPlayer PRIVATE
    nlohmann_json::nlohmann_json
    kissfft_lib
    spout_lib
    d3d11
    d3dcompiler
    dxgi
    dxguid
    strmiids
    dbghelp
    ole32
    winmm
    ${FFMPEG_LIBRARIES}
    Qt6::Widgets
    Qt6::Gui
    Qt6::Core
    # Built in-tree by FetchContent, so the unqualified target name applies; the
    # KF6::SyntaxHighlighting alias exists only in the installed export.
    KF6SyntaxHighlighting
)

# Copy FFmpeg DLLs to the output directory. Delegated to a script so the glob runs at
# BUILD time: globbing here would fix the file list at configure time, and populating
# third_party/ffmpeg/bin afterwards would then change nothing until CMake was re-run,
# silently yielding an exe that links but cannot start. See tools/copy_ffmpeg.cmake.
if(DEFINED FFMPEG_BIN_DIR AND EXISTS "${FFMPEG_BIN_DIR}")
    add_custom_command(TARGET ShaderPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DBIN_DIR=${FFMPEG_BIN_DIR}"
                "-DDEST_DIR=$<TARGET_FILE_DIR:ShaderPlayer>"
                -P "${CMAKE_SOURCE_DIR}/tools/copy_ffmpeg.cmake"
        COMMENT "Copying FFmpeg runtime")
endif()

# Copy KSyntaxHighlighting DLL (LGPL: kept as a shared library, never statically
# linked, to avoid relinking obligations)
add_custom_command(TARGET ShaderPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:KF6SyntaxHighlighting>" $<TARGET_FILE_DIR:ShaderPlayer>
)

# Deploy Qt runtime (DLLs, plugins) next to the executable. Wrapped in a script so the
# pre-port state (exe links Qt but references none of it) is not a build failure.
if(TARGET Qt6::windeployqt)
    set(_WINDEPLOYQT $<TARGET_FILE:Qt6::windeployqt>)
else()
    get_target_property(_QT_CORE_LOC Qt6::Core IMPORTED_LOCATION_RELEASE)
    if(NOT _QT_CORE_LOC)
        get_target_property(_QT_CORE_LOC Qt6::Core IMPORTED_LOCATION)
    endif()
    get_filename_component(_QT_BIN "${_QT_CORE_LOC}" DIRECTORY)
    set(_WINDEPLOYQT "${_QT_BIN}/windeployqt.exe")
endif()

add_custom_command(TARGET ShaderPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            "-DWINDEPLOYQT=${_WINDEPLOYQT}"
            "-DTARGET_EXE=$<TARGET_FILE:ShaderPlayer>"
            -P "${CMAKE_SOURCE_DIR}/tools/deploy_qt.cmake"
    COMMENT "Deploying Qt runtime")

# The shipped shader set beside the application, so a fresh clone starts with a populated
# library. AppConfig::shaderDirectory defaults to the CWD-relative "shaders", and
# Application::InitializeQt falls back to <exe>/shaders when that does not exist; without
# this copy neither path exists in a fresh build tree and the library opens empty.
add_custom_command(TARGET ShaderPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/default_shaders"
            "$<TARGET_FILE_DIR:ShaderPlayer>/shaders"
    COMMENT "Copying default_shaders beside ShaderPlayer")

# ---------------------------------------------------------------------------
# shaderfx — headless frame runner (console, no Qt, no FFmpeg, no window).
#
# Links exactly the three translation units that own the shader pipeline, so it
# runs the same ISF parse, the same preamble, the same custom[] packing and the
# same draw the app does. Anything that needs a decoder, a swap chain or a widget
# is deliberately absent: this target must stay buildable and runnable on a
# machine with no Qt deployment beside it.
# ---------------------------------------------------------------------------
add_executable(shaderfx
    ${SHADER_COMMON_HEADER}
    src/headless/main.cpp
    src/D3D11Renderer.cpp
    src/ShaderManager.cpp
    src/KeyframeTimeline.cpp
    src/FrameProfiler.cpp
)

# AUTOMOC scans every source for Q_OBJECT and finds none here; the moc pass is pure
# build time on a target that links no Qt at all.
set_target_properties(shaderfx PROPERTIES AUTOMOC OFF AUTORCC OFF)

target_include_directories(shaderfx PRIVATE
    src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(shaderfx PRIVATE
    nlohmann_json::nlohmann_json
    d3d11
    d3dcompiler
    dxgi
    dxguid
)

# The shader library beside the exe, so a bare `--shader colour_grading` resolves in
# a fresh build tree without SHADERPLAYER_SHADER_DIR or --shader-dir. Mirrors the
# FFmpeg copy above: done at build time, not glob-expanded at configure time.
add_custom_command(TARGET shaderfx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/default_shaders"
            "$<TARGET_FILE_DIR:shaderfx>/default_shaders"
    COMMENT "Copying default_shaders beside shaderfx")

# Install. `shaders/` is the user's own directory, created at runtime, and config.json is
# written by the application on first run: neither is in the repository, and installing
# from them failed the install outright.
install(TARGETS ShaderPlayer shaderfx RUNTIME DESTINATION bin)
install(DIRECTORY default_shaders/ DESTINATION bin/default_shaders)
