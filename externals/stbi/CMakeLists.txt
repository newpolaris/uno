project(stbi)

add_library(
        stbi STATIC
        stb_image.cpp
        stb_image.h
        )

target_include_directories(stbi PRIVATE ${stbi_SOURCE_DIR})
