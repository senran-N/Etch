# Match the checked-in offline Metal byte arrays, including the trailing NUL.
file(READ "${IN}" _hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " _body "${_hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f], ){24})" "\\1\n    " _body "${_body}")
file(WRITE "${OUT}" "// Offline Metal 2.1 source.\nconst unsigned char ${VAR}[] = {\n    ${_body}0x00\n};\n")
