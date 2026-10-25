# embed_file(<output.cpp> <input> <symbol>)
#
# Writes a C++ source defining
#   extern const unsigned char <symbol>[];
#   extern const unsigned int  <symbol>Size;
# with the bytes of <input>, followed by a 0 byte that <symbol>Size does not count, so text
# files (shaders) can be used directly as C strings. Runs at configure time; the output is
# only rewritten when its content changes (no needless rebuilds), and editing <input>
# triggers a re-configure.
function(embed_file output input symbol)
    file(READ "${input}" _hex HEX)
    string(LENGTH "${_hex}" _len)
    math(EXPR _size "${_len} / 2")
    # 16 bytes per line
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
    string(REPEAT "0x..," 16 _line)
    string(REGEX REPLACE "(${_line})" "\\1\n    " _bytes "${_bytes}")
    get_filename_component(_name "${input}" NAME)
    file(CONFIGURE OUTPUT "${output}" @ONLY CONTENT
"// Generated from ${_name} by cmake/embed-file.cmake. Do not edit.
extern const unsigned char @symbol@[] = {
    @_bytes@
    0x00
};
extern const unsigned int @symbol@Size = @_size@;
")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
endfunction()
