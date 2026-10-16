# Convert a shader file to a compilable C file

# INPUT_FILE, OUTPUT_FILE, SHADER_NAME must be set via -D; VARIABLE_NAME
# names the string instead of fallbackShader_<SHADER_NAME>

file(READ ${INPUT_FILE} CONTENTS)

string(REPLACE "\\" "\\\\" CONTENTS "${CONTENTS}") # Escape backslashes
string(REPLACE "\"" "\\\"" CONTENTS "${CONTENTS}") # Escape double quotes
string(REPLACE "\n" "\\n\"\n\"" CONTENTS "${CONTENTS}") # Escape newlines

if(NOT DEFINED VARIABLE_NAME)
    set(VARIABLE_NAME fallbackShader_${SHADER_NAME})
endif()

set(OUTPUT_CONTENT "const char *${VARIABLE_NAME} =\n\"${CONTENTS}\";\n")

file(WRITE ${OUTPUT_FILE} "${OUTPUT_CONTENT}")
