# Copies the FFmpeg runtime DLLs next to the built executable, at BUILD time.
#
# The glob deliberately runs here rather than in CMakeLists.txt. A configure-time
# file(GLOB) is evaluated once, when CMake runs: populating third_party/ffmpeg/bin
# afterwards changes nothing until CMake is re-run, and the build happily produces an
# executable that links (the import libs are committed) but dies at load with
# "avcodec-62.dll was not found". An empty copy list with no diagnostic is exactly how
# that happens, so this fails loudly instead.
#
# Invoke with: cmake -DBIN_DIR=<path> -DDEST_DIR=<path> -P tools/copy_ffmpeg.cmake

file(GLOB _dlls "${BIN_DIR}/*.dll")

if(NOT _dlls)
    message(FATAL_ERROR
        "No FFmpeg DLLs in ${BIN_DIR}.\n"
        "The executable links against the committed import libs but will not start "
        "without the runtime. Download a shared FFmpeg build and copy its bin/*.dll "
        "into that directory, then build again (no re-configure needed).")
endif()

foreach(_dll ${_dlls})
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dll}" "${DEST_DIR}"
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Failed to copy ${_dll} to ${DEST_DIR} (${_result})")
    endif()
endforeach()
