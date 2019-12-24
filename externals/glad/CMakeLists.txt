project(glad)

set(GLAD_DIR "${DEPENDENCIES_PATH}/glad")
add_library("glad" "${GLAD_DIR}/src/glad.c")
target_include_directories("glad" PRIVATE "${GLAD_DIR}/include")
