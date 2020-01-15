#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_glfw_gl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp> 

#include <cmath>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <sstream>

#include "handle_alloc.h"

#define USE_CORE_PROFILE 0
#define USE_TEST_CODE 0

#if USE_CORE_PROFILE
auto imgui_init = ImGui_ImplGlfwGL3_Init;
auto imgui_shutdown = ImGui_ImplGlfwGL3_Shutdown;
auto imgui_newframe = ImGui_ImplGlfwGL3_NewFrame;
#else
auto imgui_init = ImGui_ImplGlfwGL2_Init;
auto imgui_shutdown = ImGui_ImplGlfwGL2_Shutdown;
auto imgui_newframe = ImGui_ImplGlfwGL2_NewFrame;
#endif

namespace gl3 {
    
    const char* vertex_shader_code = R"__(
#version 330 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;
out vec2 v_texcoord;

void main()
{
    v_texcoord = a_texcoord;
    gl_Position = vec4(a_position, 0, 1);
}
)__";

    const char* fragment_shader_code = R"__(
#version 330 core

uniform sampler2D u_sampler;
layout(std140) uniform u_fragment
{
    vec4 data[4];
} u_frag;

in vec2 v_texcoord;
out vec4 color_out;

void main()
{
    color_out = texture(u_sampler, v_texcoord) * vec4(1.0 + 0.05*u_frag.data[0].rrr, 1.0);
}
)__";
}

namespace gl2
{ 
    const char* vertex_shader_code = R"__(
#version 120

attribute vec2 a_position;
attribute vec2 a_texcoord;
varying vec2 v_texcoord;

void main()
{
    v_texcoord = a_texcoord;
    gl_Position = vec4(a_position, 0, 1);
}
)__";

    const char* fragment_shader_code = R"__(
#version 120

struct u_frags
{
    vec4 data[4];
};

uniform u_frags u_frag;
uniform sampler2D u_sampler;
varying vec2 v_texcoord;

void main()
{
    gl_FragColor = texture2D(u_sampler, v_texcoord) * vec4(1.0 + 0.05*u_frag.data[0].rrr, 1.0);
}
)__";
}

#if _WIN32
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* _str);
#else
#   if defined(__OBJC__)
#       import <Foundation/NSObjCRuntime.h>
#   else
#       include <CoreFoundation/CFString.h>
        extern "C" void NSLog(CFStringRef _format, ...);
#   endif
#endif

void debug_output(const char* message);
void trace(const char* format, ...);

void trace(const char* format, ...)
{
    const int kLength = 1024;
    char buffer[kLength + 1] = {0,};
    
    va_list argList;
    va_start(argList, format);
    int len = vsnprintf(buffer, kLength, format, argList);
    va_end(argList);
    if (len > kLength)
        len = kLength;
    buffer[len] = '\0';

    debug_output(buffer);
}

void debug_output(const char* message)
{
#if _WIN32
    OutputDebugStringA(message);
#else
#   if defined(__OBJC__)
    NSLog(@"%s", message);
#   else
    NSLog(CFSTR("%s"), message);
#   endif
#endif
}

namespace {
    int num_frac = 10;

    GLint samples = 4;
    GLint uniform_alignment = 0;
    int width = 600;
    int height = 400;
    float cpu_time = 0.f;
    float gpu_time = 0.f;
    float draws_per_sec = 0.f;
    float per_frame_sec = 0.f;

    uint32_t draw_count = 0;

#if USE_CORE_PROFILE
    int gl_version_major = 4;
    int gl_version_minor = 1;
    int profile = GLFW_OPENGL_CORE_PROFILE;
    int forward = GLFW_TRUE;
#else
    int gl_version_major = 2;
    int gl_version_minor = 1;
#endif
}

typedef uint32_t index_t;

struct vertex_t
{
    float pos[2];
    float uv[2];
};

struct mesh_t {
    int32_t offset;
    int32_t size;
};

struct draw_command_t 
{
    struct uniform_t {
        GLuint id;
        GLint offset;
        GLuint size;
        GLint slot;
    };

    mesh_t mesh;
    uniform_t uniform;
    GLuint texture;
};

struct uniform_t
{
    glm::vec4 data[4];
};

struct uniform_block_t
{
    union {
        glm::vec4 data[4];
        uint8_t _[256];
    };
};

#define GL_TEXTURE_EXTERNAL_OES 0x00008d65

constexpr size_t get_index_for_texture_target(GLuint target) noexcept
{
    switch (target)
    {
    case GL_TEXTURE_2D:             return 0;
    case GL_TEXTURE_2D_ARRAY:       return 1;
    case GL_TEXTURE_CUBE_MAP:       return 2;
    case GL_TEXTURE_2D_MULTISAMPLE: return 3;
    case GL_TEXTURE_EXTERNAL_OES:   return 4;
    default:                        return 0;
    }
}

struct texture_state_t {
    GLuint activate = 0;
    struct {
        struct {
            GLuint instance = 0;
        } target[5];
    } unit[8];
};

template <typename T, typename F>
inline void update_state(T& state, const T& expected, F functor, bool force = false) noexcept
{
    if (force || state != expected)
    {
        state = expected;
        functor();
    }
}

struct draw_list_t 
{
    struct command_t 
    {
        int32_t count;
        int32_t offset;
    };

    draw_list_t();

    void reserve(int32_t vertex_count, int32_t index_count);
    void draw(const vertex_t* vertex, int32_t vertex_count, const index_t* index, int32_t index_count);

    std::vector<vertex_t> vertices; 
    std::vector<index_t> indices;
    std::vector<command_t> commands;

    vertex_t* vertex_pointer;
    index_t* index_pointer;
};

draw_list_t::draw_list_t() :
    vertex_pointer(nullptr),
    index_pointer(nullptr)
{
}

void draw_list_t::reserve(int32_t vertex_count, int32_t index_count)
{
    assert(index_count >= 0);
    assert(vertex_count >= 0);

    size_t old_vertex_size = vertices.size();
    vertices.resize(old_vertex_size + vertex_count);
    vertex_pointer = vertices.data();
    vertex_pointer += old_vertex_size;

    size_t old_index_size = indices.size();
    indices.resize(old_index_size + index_count);
    index_pointer = indices.data();
    index_pointer += old_index_size;
}

void draw_list_t::draw(const vertex_t* vertex, int32_t vertex_count, const index_t* index, int32_t index_count)
{
    const size_t index_offset = indices.size();
    const size_t vertex_offset = vertices.size();

    reserve(vertex_count, index_count);
    for (int i = 0; i < vertex_count; i++)
        vertex_pointer[i] = vertex[i];
    for (int i = 0; i < index_count; i++)
        index_pointer[i] = index[i] + vertex_offset;

    commands.push_back({index_count, (int32_t)index_offset});
}

struct texture_handle_t
{
    handle_t index;
};

struct texture_desc_t
{
    int32_t width;
    int32_t height;
    uint8_t* data;
};

struct renderer_opengl_t
{
public:

    virtual bool setup();
    virtual void begin_frame();
    virtual void end_frame();
    virtual void uniform(const uniform_t& uniform) = 0;
    virtual void draw(vertex_t* vertices, int vertex_count, index_t* indices, int index_count) = 0;
    virtual void texture(texture_handle_t texture) = 0;

    virtual void render_ui();
    virtual void render_profile_ui();
    virtual void cleanup();

    inline void activate_texture(GLuint unit);
    inline void bind_texture(GLuint unit, GLuint target, GLuint instance);

    virtual GLuint create_shader(GLenum type, const char* shaderCode);
    virtual GLuint create_program(GLuint vertex, GLuint fragment);

    GLuint create_texture_impl(int32_t width, int32_t height, uint8_t* data);
    GLuint create_texture_impl(std::string path);

    virtual texture_handle_t create_texture(const texture_desc_t& desc);
    virtual void destroy_texture(texture_handle_t handle);

    static const uint8_t max_texture = 128;
    handle_alloc_t<max_texture> handle_alloc;
    GLuint textures[max_texture];

    texture_state_t texture_state;
};

void renderer_opengl_t::activate_texture(GLuint unit)
{
    update_state(texture_state.activate, unit, [&]() {
        glActiveTexture(GL_TEXTURE0 + unit);
    });
}

void renderer_opengl_t::bind_texture(GLuint unit, GLuint target, GLuint instance)
{
    uint8_t target_index = (uint8_t)get_index_for_texture_target(target);
    update_state(texture_state.unit[unit].target[target_index].instance, instance, [&](){
        activate_texture(unit);
        glBindTexture(target, instance);
    });
}

GLuint renderer_opengl_t::create_shader(GLenum type, const char* shaderCode)
{
    GLuint id = glCreateShader(type);
    if (id == 0)
        return 0;

    glShaderSource(id, 1, &shaderCode, 0);
    glCompileShader(id);

    GLint compiled = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE)
    {
        GLint length = 0;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
        std::vector<GLchar> buffer(length + 1);
        glGetShaderInfoLog(id, length, 0, buffer.data());
        trace("%s (%d) %s\n", __FILE__, __LINE__, buffer.data());
        glDeleteShader(id);
        return 0;
    }
    return id;
}

GLuint renderer_opengl_t::create_program(GLuint vertex, GLuint fragment)
{
    GLuint id = glCreateProgram();

    GLint status = 0;
    if (vertex != 0)
    {
        glAttachShader(id, vertex);
        if (fragment != 0)
            glAttachShader(id, fragment);
        glLinkProgram(id);
        glGetProgramiv(id, GL_LINK_STATUS, &status);

        if (status == GL_FALSE)
        {
            const uint32_t kBufferSize = 512u;  
            char log[kBufferSize];
            glGetProgramInfoLog(id, sizeof(log), nullptr, log);
            trace("%s:%d %d: %s", __FILE__, __LINE__, status, log);
            return 0;
        }
    }

    if (status == GL_FALSE)
    {
        glDeleteProgram(id);
        id = 0;
        return id;
    }

    return id;
}

GLuint renderer_opengl_t::create_texture_impl(int32_t width, int32_t height, uint8_t* data)
{
    GLenum format = GL_RGBA;
    GLenum internalFormat = GL_RGBA;

    GLuint instance = 0;
    glGenTextures(1, &instance);
    glBindTexture(GL_TEXTURE_2D, instance);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_FLOAT, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    return instance;
}

GLuint renderer_opengl_t::create_texture_impl(std::string path)
{
    stbi_set_flip_vertically_on_load(true);

    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == NULL) 
        return 0;

    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::vector<char> storage(length);;
    length = fread(storage.data(), 1, length, fp);
    fclose(fp);

	GLenum target = GL_TEXTURE_2D;
    GLenum type = GL_UNSIGNED_BYTE;
    int width = 0, height = 0, nrComponents = 0;
    stbi_uc* imagedata = stbi_load_from_memory((stbi_uc*)storage.data(), (int)length, &width, &height, &nrComponents, 0);
    if (!imagedata) 
        return 0;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    GLuint instance = 0;
    glGenTextures(1, &instance);
    glBindTexture(target, instance);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(target, 0, GL_RGB, width, height, 0, GL_RGB, type, imagedata);
    glBindTexture(target, 0);

    stbi_image_free(imagedata);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    return instance;
}

texture_handle_t renderer_opengl_t::create_texture(const texture_desc_t& desc)
{
    texture_handle_t handle = { handle_alloc.alloc() };
    textures[handle.index] = create_texture_impl(desc.width, desc.height, desc.data);
    return handle;
}

void renderer_opengl_t::destroy_texture(texture_handle_t handle)
{
    if (handle.index == invalid_handle_t )
        return;

    GLuint& texture = textures[handle.index];
    glDeleteTextures(1, &texture);
    texture = 0;

    handle_alloc.free(handle.index);
}

bool renderer_opengl_t::setup()
{
    memset(textures, 0, sizeof(textures));
    return true;
}

void renderer_opengl_t::begin_frame()
{ 
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, width, height);
    glClearDepth(1.0);
    glClearColor(0.3f, 0.3f, 0.5f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_count = num_frac;
}

void renderer_opengl_t::end_frame()
{ 
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void renderer_opengl_t::cleanup()
{
}

class renderer_gl2_t : public renderer_opengl_t
{
public:

    bool setup() override;
    void begin_frame() override;
    void draw(vertex_t* vertices, int vertex_count, index_t* indices, int index_count) override;
    void uniform(const uniform_t& uniform) override;
    void texture(texture_handle_t texture) override;
    void end_frame() override;
    void cleanup() override;

    GLint position_attribute;
    GLint texcoord_attribute;
    GLint sampler_location;
    GLint uniform_location[4];

    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
};

bool renderer_gl2_t::setup()
{
    renderer_opengl_t::setup();

    vertex_shader = create_shader(GL_VERTEX_SHADER, gl2::vertex_shader_code);
    if (vertex_shader == GL_NONE)
        return false;

    fragment_shader = create_shader(GL_FRAGMENT_SHADER, gl2::fragment_shader_code);
    if (fragment_shader == GL_NONE)
        return false;

    program = create_program(vertex_shader, fragment_shader);
    if (program == GL_NONE)
        return false;

    position_attribute = glGetAttribLocation(program, "a_position");
    texcoord_attribute = glGetAttribLocation(program, "a_texcoord");
    sampler_location = glGetUniformLocation(program, "u_sampler");
    uniform_location[0] = glGetUniformLocation(program, "u_frag.data[0]");
    uniform_location[1] = glGetUniformLocation(program, "u_frag.data[1]");
    uniform_location[2] = glGetUniformLocation(program, "u_frag.data[2]");
    uniform_location[3] = glGetUniformLocation(program, "u_frag.data[3]");

    assert(position_attribute >= 0);
    assert(texcoord_attribute >= 0);
    assert(sampler_location >= 0);
    assert(uniform_location[0] >= 0);

    glUseProgram(program);

    // initialize once will be ok
    glUniform1i(sampler_location, 0);

    return true;
}

void renderer_gl2_t::begin_frame()
{
    renderer_opengl_t::begin_frame();

    glUseProgram(program);

    glEnableVertexAttribArray(position_attribute);
    glEnableVertexAttribArray(texcoord_attribute);
}

void renderer_gl2_t::draw(vertex_t* vertices, int vertex_count, index_t*, int)
{
    const void* position = (size_t*)&vertices->pos;
	const void* texcoord = (size_t*)&vertices->uv;

    glVertexAttribPointer(position_attribute, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), position);
    glVertexAttribPointer(texcoord_attribute, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), texcoord);

    glDrawArrays(GL_TRIANGLES, 0, vertex_count);
}

void renderer_gl2_t::uniform(const uniform_t& uniform)
{
    glUniform4fv(uniform_location[0], 1, (const float*)&uniform.data[0]);
    glUniform4fv(uniform_location[1], 1, (const float*)&uniform.data[1]);
    glUniform4fv(uniform_location[2], 1, (const float*)&uniform.data[2]);
    glUniform4fv(uniform_location[3], 1, (const float*)&uniform.data[3]);
}

void renderer_gl2_t::texture(texture_handle_t texture)
{
    bind_texture(0, GL_TEXTURE_2D, textures[texture.index]);
    // glActiveTexture(GL_TEXTURE0);
    // glBindTexture(GL_TEXTURE_2D, textures[texture.index]);
}

void renderer_gl2_t::end_frame() 
{
    renderer_opengl_t::end_frame();

    glDisableVertexAttribArray(position_attribute);
    glDisableVertexAttribArray(texcoord_attribute);
}

void renderer_gl2_t::cleanup()
{
    renderer_opengl_t::cleanup();

    glDeleteProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
}

class renderer_gl3_t : public renderer_opengl_t
{
public:

    bool setup() override;
    void begin_frame() override;
    void draw(vertex_t* vertices, int vertex_count, index_t* indices, int index_count) override;
    void uniform(const uniform_t& uniform) override;
    void texture(texture_handle_t texture) override;
    void end_frame() override;
    void cleanup() override;

    texture_handle_t create_texture(const texture_desc_t& desc) override;
    void destroy_texture(texture_handle_t handle) override;

    GLint position_attribute;
    GLint texcoord_attribute;
    GLint sampler_location;

    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;

    GLuint vao;
    GLuint vbo;
    GLuint ibo;
    GLuint ubo;
    GLint block_index;
    draw_list_t draw_list;

    std::vector<texture_handle_t> free_textures;
    std::vector<texture_handle_t> bind_textures;
    std::vector<uniform_t> uniforms;
    std::vector<char> uniform_buffer;

    std::vector<draw_command_t> draw_commands;
};

bool renderer_gl3_t::setup()
{
    renderer_opengl_t::setup();

    vertex_shader = create_shader(GL_VERTEX_SHADER, gl3::vertex_shader_code);
    if (vertex_shader == GL_NONE)
        return false;

    fragment_shader = create_shader(GL_FRAGMENT_SHADER, gl3::fragment_shader_code);
    if (fragment_shader == GL_NONE)
        return false;

    program = create_program(vertex_shader, fragment_shader);
    if (program == GL_NONE)
        return false;

    position_attribute = glGetAttribLocation(program, "a_position");
    texcoord_attribute = glGetAttribLocation(program, "a_texcoord");
    sampler_location = glGetUniformLocation(program, "u_sampler");
    block_index = glGetUniformBlockIndex(program, "u_fragment");

    assert(position_attribute >= 0);
    assert(texcoord_attribute >= 0);
    assert(sampler_location >= 0);
    assert(block_index >= 0);

    glUseProgram(program);

    // initialize once will be ok
    glUniform1i(sampler_location, 0);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    glGenBuffers(1, &ubo);

    return true;
}

void renderer_gl3_t::begin_frame()
{
    renderer_opengl_t::begin_frame();

    draw_list.index_pointer = nullptr;
    draw_list.vertex_pointer = nullptr;
    draw_list.vertices.clear();
    draw_list.indices.clear();
    draw_list.commands.clear();

    uniforms.clear();
    uniform_buffer.clear();
    draw_commands.clear();
    bind_textures.clear();
}

void renderer_gl3_t::draw(vertex_t* vertices, int vertex_count, index_t* indices, int index_count)
{
    draw_list.draw((vertex_t*)vertices, vertex_count, indices, index_count);
}

void renderer_gl3_t::uniform(const uniform_t& uniform)
{
    uniforms.push_back(uniform);
}

void renderer_gl3_t::texture(texture_handle_t texture)
{
    bind_textures.push_back(texture);
}

void renderer_gl3_t::end_frame() 
{
    GLsizeiptr vertex_buffer_size = sizeof(vertex_t) * draw_list.vertices.size();
    const void *vertex_buffer_pointer = draw_list.vertices.data();

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertex_buffer_size, vertex_buffer_pointer, GL_STREAM_DRAW);

    GLsizeiptr index_buffer_size = sizeof(index_t) * draw_list.indices.size();
    const void *index_buffer_pointer = draw_list.indices.data();

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_buffer_size, index_buffer_pointer, GL_STREAM_DRAW);

    struct uniform_offset_t
    {
        int32_t offset;
        uint32_t size;
    };
    std::vector<uniform_offset_t> uniform_offsets(uniforms.size());

    const int block_size = sizeof(uniform_block_t);
    uniform_buffer.resize(block_size * uniforms.size());

    char* data = uniform_buffer.data();

    for (int32_t i = 0; i < (int32_t)(uniforms.size()); i++) 
    {
        memcpy(data, &uniforms[i], sizeof(uniform_t));
        data += block_size;
        uniform_offsets[i] = uniform_offset_t({ i * block_size, block_size });
    }

    GLsizeiptr ubo_buffer_size = uniform_buffer.size();
    const void *ubo_buffer_pointer = uniform_buffer.data();

    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, ubo_buffer_size, ubo_buffer_pointer, GL_DYNAMIC_DRAW);

    draw_commands.resize(num_frac);
    for (int i = 0; i < num_frac; i++)
    {
        draw_commands[i].mesh.size = draw_list.commands[i].count;
        draw_commands[i].mesh.offset = draw_list.commands[i].offset;
        draw_commands[i].uniform = { ubo, uniform_offsets[i].offset, uniform_offsets[i].size, block_index };
        draw_commands[i].texture = textures[bind_textures[i].index];
    }

    glUseProgram(program);

    // since 3.0
    // without below line, bind block_point according to shader code's define
    // const GLuint block_point = 0;
    // glUniformBlockBinding(program, block_index, block_point);

    glEnableVertexAttribArray(position_attribute);
    glEnableVertexAttribArray(texcoord_attribute);

    const void* position = (size_t*)0;
    const void* texcoord = (size_t*)(2 * sizeof(float));

    glVertexAttribPointer(position_attribute, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), position);
    glVertexAttribPointer(texcoord_attribute, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), texcoord);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    for (int i = 0; i < num_frac; i++) {
        const auto& call = draw_commands[i];
        const auto& ubo = call.uniform;
        glBindBufferRange(GL_UNIFORM_BUFFER, ubo.slot, ubo.id, ubo.offset, ubo.size);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, call.texture);

        glDrawElements(GL_TRIANGLES, call.mesh.size, GL_UNSIGNED_INT, (const void*)(call.mesh.offset * sizeof(4)));
    }

    glDisableVertexAttribArray(position_attribute);
    glDisableVertexAttribArray(texcoord_attribute);

    for (auto handle : free_textures) {
        GLuint& texture = textures[handle.index];
        glDeleteTextures(1, &texture);
        texture = 0;
        handle_alloc.free(handle.index);
    }
    free_textures.clear();
}

void renderer_gl3_t::cleanup()
{
    renderer_opengl_t::cleanup();

    for (auto handle : free_textures) {
        GLuint& texture = textures[handle.index];
        glDeleteTextures(1, &texture);
        texture = 0;
    }
    free_textures.clear();

    glDeleteProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &vbo);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &ibo);

    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glDeleteBuffers(1, &ubo);
}

texture_handle_t renderer_gl3_t::create_texture(const texture_desc_t& desc)
{
    texture_handle_t handle = { handle_alloc.alloc() };
    textures[handle.index] = create_texture_impl(desc.width, desc.height, desc.data);
    return handle;
}

void renderer_gl3_t::destroy_texture(texture_handle_t handle)
{
    if (handle.index == invalid_handle_t)
        return;
    free_textures.push_back(handle);
}

// buffer update per drawcall
class renderer_gl31_t : public renderer_gl3_t
{
public:
    
    void begin_frame() override;
    void draw(vertex_t* vertices, int vertex_count, index_t* indices, int index_count) override;
    void uniform(const uniform_t& uniform) override;
    void texture(texture_handle_t texture) override;
    void end_frame() override;
};

void renderer_gl31_t::begin_frame()
{
    renderer_opengl_t::begin_frame();
    
    glUseProgram(program);
    
    glEnableVertexAttribArray(position_attribute);
    glEnableVertexAttribArray(texcoord_attribute);
    
    const void* position = (size_t*)0;
    const void* texcoord = (size_t*)(2 * sizeof(float));
    
    glVertexAttribPointer(position_attribute, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), position);
    glVertexAttribPointer(texcoord_attribute, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), texcoord);
}

void renderer_gl31_t::draw(vertex_t* vertices, int vertex_count, index_t*, int)
{
    auto vertex_buffer_size = vertex_count * sizeof(vertex_t);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_buffer_size, vertices, GL_STREAM_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, vertex_count);
}

void renderer_gl31_t::uniform(const uniform_t& uniform)
{
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(uniform_t), &uniform, GL_DYNAMIC_DRAW);

    glBindBufferBase(GL_UNIFORM_BUFFER, block_index, ubo);
}

void renderer_gl31_t::texture(texture_handle_t texture)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[texture.index]);
}

void renderer_gl31_t::end_frame()
{
    glDisableVertexAttribArray(position_attribute);
    glDisableVertexAttribArray(texcoord_attribute);
}

static void error_callback(int error, const char* description)
{
    trace("Error: %s\n", description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void renderer_opengl_t::render_profile_ui()
{
    ImGui::SetNextWindowPos(
        ImVec2(width - 200.f - 10.f, 10.f),
        ImGuiSetCond_FirstUseEver);

    ImGui::Begin("Profiler",
        NULL,
        ImVec2(200.f, height - 20.0f),
        ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::PushItemWidth(180.0f);
    ImGui::Indent();
    ImGui::Text("CPU %s: %10.5f ms\n", "Main", cpu_time);
    ImGui::Text("GPU %s: %10.5f ms\n", "Main", gpu_time);
    ImGui::Text("Draws/s: %.2f", draws_per_sec);
    ImGui::Text("Draw Count: %d\n", draw_count);
    ImGui::Text("FPS: %f\n", 1.f/per_frame_sec);
    ImGui::Separator();
    ImGui::Unindent();
    ImGui::SliderInt("", &num_frac, 10, 10000);
    ImGui::End();
}

void renderer_opengl_t::render_ui()
{
    imgui_newframe();
    render_profile_ui();
    ImGui::Render();
    ImGui::EndFrame();
}

void APIENTRY opengl_callback(GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar* message,
    const void* userParam)
{
    using namespace std;

    // ignore these non-significant error codes
    if (id == 131169 || id == 131185 || id == 131218 || id == 131204 || id == 131184)
        return;

    stringstream out;

    out << "---------------------OPENGL-CALLBACK-START------------" << endl;
    out << "message: " << message << endl;
    out << "type: ";
    switch (type) {
    case GL_DEBUG_TYPE_ERROR:
        out << "ERROR";
        break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        out << "DEPRECATED_BEHAVIOR";
        break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        out << "UNDEFINED_BEHAVIOR";
        break;
    case GL_DEBUG_TYPE_PORTABILITY:
        out << "PORTABILITY";
        break;
    case GL_DEBUG_TYPE_PERFORMANCE:
        out << "PERFORMANCE";
        break;
    case GL_DEBUG_TYPE_OTHER:
        out << "OTHER";
        break;
    }
    out << endl;

    out << "id: " << id << endl;
    out << "severity: ";
    switch (severity) {
    case GL_DEBUG_SEVERITY_LOW:
        out << "LOW";
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        out << "MEDIUM";
        break;
    case GL_DEBUG_SEVERITY_HIGH:
        out << "HIGH";
        break;
    }
    out << endl;
    out << "---------------------OPENGL-CALLBACK-END--------------" << endl;

    trace(out.str().c_str());
}

void render_background_texture(renderer_opengl_t& render)
{
    render.begin_frame();

    static texture_handle_t texture = { invalid_handle_t };

    int texture_index = -1;

	for (int i = 0; i < num_frac; i++)
	{
		float sx = -1.f + 2.f / num_frac * i;
		float ex = -1.f + 2.f / num_frac * (i + 1);
		float tsx = 0.f + 1.f / num_frac * i;
		float tex = 0.f + 1.f / num_frac * (i + 1);

		float vertices[] = {
			sx, -1.0, tsx, 0.0,
			ex, -1.0, tex, 0.0,
			sx, 1.0, tsx, 1.0,

			sx, 1.0, tsx, 1.0,
			ex, -1.0, tex, 0.0,
			ex, 1.0, tex, 1.0,
		};

        uint32_t indices[] = { 0, 1, 2, 3, 4, 5 };

        uniform_t data;
        memset(&data, 0, sizeof(data));
        data.data[0].r = float(i + 1) / num_frac;

        int index = i * 4 / num_frac;
        if (index != texture_index) 
        {
            render.destroy_texture(texture);

            float f = float(index+1) / 4;
            glm::vec4 texel[4] = {
                {   f, 0.0,  0.0, 1.0 },
                { 0.0,   f,  0.0, 1.0 },
                { 0.0, 0.0,    f, 1.0 },
                {   f, 1.0,  0.0, 1.0 },
            };
            texture = render.create_texture({ 2, 2, (uint8_t*)texel});

            texture_index = index;
        }

        render.uniform(data);
        render.texture(texture);
        render.draw((vertex_t*)vertices, 6, indices, 6);
	}
    render.end_frame();
}

int main(void)
{
    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, gl_version_major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, gl_version_minor);
#if USE_CORE_PROFILE
    glfwWindowHint(GLFW_OPENGL_PROFILE, profile);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, forward);
#endif

    GLFWwindow* window = glfwCreateWindow(640, 480, "uno", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(0);
    glfwSetKeyCallback(window, key_callback);

    imgui_init(window, false);

    glGetIntegerv(GL_SAMPLES, &samples);
    if (samples)
        trace("Context reports MSAA is available with %i samples\n", samples);
    else
        trace("Context reports MSAA is unavailable\n");

    trace("%s\n%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    if (glDebugMessageCallback) {
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
        glDebugMessageCallback(opengl_callback, nullptr);
    }

    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniform_alignment);

#if USE_CORE_PROFILE
#   if USE_TEST_CODE
    auto render = renderer_gl31_t();
#   else
    auto render = renderer_gl3_t();
#   endif
#else
    auto render = renderer_gl2_t();
#endif

    if (!render.setup()) {
        glfwTerminate();
        exit(EXIT_SUCCESS);
    }

    // https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_timer_query.txt

    GLuint query;
    bool query_issued = false;
    bool wait_gpu = false;
    int running = GLFW_TRUE;

    auto a = std::chrono::high_resolution_clock::now();

    while (running)
    {
        glfwGetFramebufferSize(window, &width, &height);

        if (!query_issued) {
            glGenQueries(1, &query);
            glBeginQuery(GL_TIME_ELAPSED, query);
            query_issued = true;
        }

        auto cpu_tick = std::chrono::high_resolution_clock::now();

        render_background_texture(render);

        auto cpu_tock = std::chrono::high_resolution_clock::now();
        auto cpu_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(cpu_tock - cpu_tick);
        auto cpu_frame = static_cast<float>(cpu_elapsed.count() / 1000.0);

        cpu_time = glm::mix(cpu_time, cpu_frame, 0.05);

        if (query_issued && !wait_gpu) {
            glEndQuery(GL_TIME_ELAPSED);
            wait_gpu = true;
        }

        GLint stopTimerAvailable = 0;
        glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &stopTimerAvailable);

        if (stopTimerAvailable) {
            GLuint64 result_time = 0;
            if (glGetQueryObjectui64v)
                glGetQueryObjectui64v(query, GL_QUERY_RESULT, &result_time);
            wait_gpu = false;
            query_issued = false;
            auto gpu_frame = static_cast<float>(result_time / 1e6f);

            gpu_time = glm::mix(gpu_time, gpu_frame, 0.05);

            draws_per_sec = draw_count / (gpu_time * 1e-3f);
        }

        render.render_ui();
        auto b = std::chrono::high_resolution_clock::now();
        auto c = std::chrono::duration_cast<std::chrono::microseconds>(b - a);
        auto d = static_cast<float>(c.count() * 1e-6);

        a = b;

        per_frame_sec = glm::mix(per_frame_sec, d, 0.05);

        glfwSwapBuffers(window);
        glfwPollEvents();
        if (glfwWindowShouldClose(window))
            running = GLFW_FALSE;
    }

    render.cleanup();

	imgui_shutdown();
    glfwHideWindow(window);
    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}

