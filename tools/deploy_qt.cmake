# Runs windeployqt over the built executable.
#
# During the ImGui-to-Qt port the executable links Qt but does not yet reference it, so
# the linker emits no Qt imports and windeployqt exits non-zero with "does not seem to be
# a Qt executable". That is the correct answer for that state, not a build failure: there
# is nothing to deploy. Every other failure is propagated.
#
# Invoke with: cmake -DWINDEPLOYQT=<path> -DTARGET_EXE=<path> -P tools/deploy_qt.cmake

execute_process(
    COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
            "${TARGET_EXE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)

if(result EQUAL 0)
    return()
endif()

if(output MATCHES "does not seem to be a Qt executable")
    message(STATUS "windeployqt: ${TARGET_EXE} has no Qt imports yet; nothing to deploy")
    return()
endif()

message(FATAL_ERROR "windeployqt failed (${result}):\n${output}")
