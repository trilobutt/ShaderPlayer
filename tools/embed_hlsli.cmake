# Embeds a text file into a C++ header as a raw string literal.
#
#   cmake -DINPUT=<path> -DOUTPUT=<path> -DSYMBOL=<identifier> -P embed_hlsli.cmake
#
# Content restriction: the input must not contain the delimiter sequence
# )SPHLSL" (it would terminate the raw string early). Backslashes and quotes are
# safe — CMake does not re-expand the result of a variable expansion, and a raw
# string literal needs no escaping.

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "embed_hlsli.cmake requires -DINPUT, -DOUTPUT and -DSYMBOL")
endif()

file(READ "${INPUT}" _content)

string(FIND "${_content}" ")SPHLSL\"" _bad)
if(NOT _bad EQUAL -1)
    message(FATAL_ERROR "${INPUT} contains the raw-string delimiter )SPHLSL\"")
endif()

get_filename_component(_name "${INPUT}" NAME)

file(WRITE "${OUTPUT}"
"// Generated from ${_name} by tools/embed_hlsli.cmake — do not edit.\n"
"#pragma once\n"
"\n"
"namespace SP {\n"
"\n"
"inline constexpr const char* ${SYMBOL} = R\"SPHLSL(\n"
"${_content}"
")SPHLSL\";\n"
"\n"
"} // namespace SP\n"
)
