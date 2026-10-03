# RTR-Bench - turns a binary file into a C++ source with a byte array, so
# that fonts are embedded in the executable (the program is portable: no
# resource files beside it).
#
# Called in script mode:
#   cmake -DINPUT=file -DOUTPUT=file.cpp -DSYMBOL=name -P embed.cmake
# Produces:
#   extern const unsigned char SYMBOL[]; extern const unsigned long SYMBOL_size;
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR size "${hex_length} / 2")
# "0x" before every byte, a comma after it, a newline every 32 bytes.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}"
    "// Generated from ${INPUT} by cmake/embed.cmake. Do not edit.\n"
    "extern const unsigned char ${SYMBOL}[] = {\n${bytes}\n};\n"
    "extern const unsigned long ${SYMBOL}_size = ${size}UL;\n")
