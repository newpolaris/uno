#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <imgui.h>
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
    color_out = texture(u_sampler, v_texcoord) * vec4(u_frag.data[0].rrr, 1.0);
}
)__";


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
    int num_frac = 1000;

    GLint samples = 4;
    GLint uniform_alignment = 0;
    int width = 600;
    int height = 400;
    float cpu_time = 0.f;
    float gpu_time = 0.f;
    float draws_per_sec = 0.f;

    uint32_t draw_count = 0;
}

typedef uint32_t index_t;

struct vertex_t
{
    float pos[2];
    float uv[2];
};

struct command_t 
{
    int32_t count;
    int32_t offset;

    struct uniform_t {
        GLuint index;
        GLuint offset;
        GLuint size;
    } uniform;
};

struct uniform_block_t
{
    union {
        glm::vec4 data[4];
        uint8_t _[256];
    };
};

struct draw_list_t 
{
    draw_list_t();

    void reserve(int32_t vertex_count, int32_t index_count);
    void triangles(const uniform_block_t& uniform, const vertex_t* vertex, int32_t vertex_count, const index_t* index, int32_t index_count);

    std::vector<vertex_t> vertices; 
    std::vector<index_t> indices;
    std::vector<command_t> commands;
    std::vector<uniform_block_t> uniforms[8];

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

void draw_list_t::triangles(const uniform_block_t& uniform, const vertex_t* vertex, int32_t vertex_count, const index_t* index, int32_t index_count)
{
    const size_t index_offset = indices.size();
    const size_t vertex_offset = vertices.size();

    reserve(vertex_count, index_count);
    for (int i = 0; i < vertex_count; i++)
        vertex_pointer[i] = vertex[i];
    for (int i = 0; i < index_count; i++)
        index_pointer[i] = index[i] + vertex_offset;

    command_t command = {0, };
    command.count = index_count;
    command.offset = (int32_t)index_offset;
    command.uniform.index = 0;
    command.uniform.offset = sizeof(uniform_block_t) * uniforms[0].size();
    command.uniform.size = sizeof(uniform_block_t);
    commands.push_back(command);

    uniforms[0].push_back(uniform);
}

namespace simple_render
{
    bool setup();
    void render();
    void render_delta(int k, float c);
    void render_frame();
    void begin_frame();
    void end_frame();
    void render_ui();
    void render_profile_ui();
    void cleanup();

    GLuint create_shader(GLenum type, const char* shaderCode);
    GLuint create_program(GLuint vertex, GLuint fragment);
    GLuint create_texture(std::string path);

    GLuint framebuffer;
    GLuint texture;
    GLuint vao;
    GLuint vbo;
    GLuint ibo;
    GLuint ubo;

    GLint position_attribute;
    GLint texcoord_attribute;
    GLint sampler_location;
    GLint block_index;

    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;

    draw_list_t draw_list;

} // namespace triangle

GLuint simple_render::create_shader(GLenum type, const char* shaderCode)
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

GLuint simple_render::create_program(GLuint vertex, GLuint fragment)
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

GLuint simple_render::create_texture(std::string path)
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

bool simple_render::setup()
{
    vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_code);
    if (vertex_shader == GL_NONE)
        return false;

    fragment_shader = create_shader(GL_FRAGMENT_SHADER, fragment_shader_code);
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

    GLenum format = GL_RGBA;
    GLenum internalFormat = GL_RGBA;

    glm::vec4 texel[4] = { 
        { 1.0, 0.0, 0.0, 1.0 },
        { 0.0, 1.0, 0.0, 1.0 },
        { 0.0, 0.0, 1.0, 1.0 },
        { 1.0, 1.0, 0.0, 1.0 },
    };

    GLuint instance = 0;
    glGenTextures(1, &instance);
    glBindTexture(GL_TEXTURE_2D, instance);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, 2, 2, 0, format, GL_FLOAT, texel);
    glBindTexture(GL_TEXTURE_2D, 0);

    texture = instance;

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    glGenBuffers(1, &ubo);

    return true;
}

void simple_render::begin_frame()
{ 
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, width, height);
    glClearDepth(1.0);
    glClearColor(0.3f, 0.3f, 0.5f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void simple_render::render_delta(int k, float c)
{
    glUseProgram(program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    // initialize once will be ok
    glUniform1i(sampler_location, 0);

    auto ubo = draw_list.commands[k].uniform;
    glBindBufferRange(GL_UNIFORM_BUFFER, block_index, simple_render::ubo, ubo.offset, ubo.size);

    glEnableVertexAttribArray(position_attribute);
    glEnableVertexAttribArray(texcoord_attribute);

    const void* position = (size_t*)0;
    const void* texcoord = (size_t*)(2 * sizeof(float));

    glVertexAttribPointer(position_attribute, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), position);
    glVertexAttribPointer(texcoord_attribute, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), texcoord);

    glDrawElements(GL_TRIANGLES, draw_list.commands[k].count, GL_UNSIGNED_INT, (const void*)(draw_list.commands[k].offset * sizeof(4)));

    glDisableVertexAttribArray(position_attribute);
    glDisableVertexAttribArray(texcoord_attribute);

    draw_count = draw_list.commands.size();
}

void simple_render::render_frame()
{
	// TODO: move to end_frame etc.
    draw_list.index_pointer = nullptr;
    draw_list.vertex_pointer = nullptr;
    draw_list.vertices.resize(0);
    draw_list.indices.resize(0);
    draw_list.commands.resize(0);
    draw_list.uniforms[0].resize(0);

	static float f = 0.f;

	float c = std::cos(f += 0.11f)*0.5f + 0.5f;

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

        uniform_block_t block;
        memset(&block, 0, sizeof(block));
        block.data[0].r = float(i + 1) / num_frac;

        draw_list.triangles(block, (vertex_t*)vertices, 6, indices, 6);
	}

    GLsizeiptr vertex_buffer_size = sizeof(vertex_t) * draw_list.vertices.size();
    const void *vertex_buffer_pointer = draw_list.vertices.data();

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertex_buffer_size, vertex_buffer_pointer, GL_STREAM_DRAW);

    GLsizeiptr index_buffer_size = sizeof(index_t) * draw_list.indices.size();
    const void *index_buffer_pointer = draw_list.indices.data();

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_buffer_size, index_buffer_pointer, GL_STREAM_DRAW);

    GLsizeiptr ubo_buffer_size = sizeof(uniform_block_t) * draw_list.uniforms[0].size();
    const void *ubo_buffer_pointer = draw_list.uniforms[0].data();

    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, ubo_buffer_size, ubo_buffer_pointer, GL_DYNAMIC_DRAW);

    // since 3.0
    // without below line, bind block_point acording to shader
    // glUniformBlockBinding(program, block_index, block_point);
    const GLuint block_point = 0;
    // since 3.0
    glBindBufferBase(GL_UNIFORM_BUFFER, block_point, ubo);

	for (int i = 0; i < num_frac; i++)
		render_delta(i, c);
}

void simple_render::end_frame()
{ 
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void simple_render::render()
{
    begin_frame();
    render_frame();
    end_frame();
}

void simple_render::cleanup()
{
    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &vbo);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &ibo);

    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glDeleteBuffers(1, &ubo);

    glDeleteTextures(1, &texture);

    glDeleteProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
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

void simple_render::render_profile_ui()
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
    ImGui::Separator();
    ImGui::Unindent();
    ImGui::End();
}

void simple_render::render_ui()
{
    ImGui_ImplGlfwGL3_NewFrame();
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

int main(void)
{
    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_SAMPLES, samples);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);

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

    ImGui_ImplGlfwGL3_Init(window, false);

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

    simple_render::setup();

    // https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_timer_query.txt

    GLuint query;
    bool query_issued = false;
    bool wait_gpu = false;
    int running = GLFW_TRUE;

    while (running)
    {
        glfwGetFramebufferSize(window, &width, &height);

        if (!query_issued) {
            glGenQueries(1, &query);
            glBeginQuery(GL_TIME_ELAPSED, query);
            query_issued = true;
        }

        auto cpu_tick = std::chrono::high_resolution_clock::now();

        simple_render::render();

        auto cpu_tock = std::chrono::high_resolution_clock::now();
        auto cpu_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(cpu_tock - cpu_tick);
        auto cpu_frame = static_cast<float>(cpu_elapsed.count() / 1000.0);

        if (query_issued && !wait_gpu) {
            glEndQuery(GL_TIME_ELAPSED);
            wait_gpu = true;
        }

        GLint stopTimerAvailable = 0;
        glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &stopTimerAvailable);

        if (stopTimerAvailable) {
            GLuint64 result_time;
            glGetQueryObjectui64v(query, GL_QUERY_RESULT, &result_time);
            wait_gpu = false;
            query_issued = false;
            auto gpu_frame = static_cast<float>(result_time / 1e6f);

            cpu_time = cpu_time * 0.95 + cpu_frame * 0.05;
            gpu_time = gpu_time * 0.95 + gpu_frame * 0.05;

            draws_per_sec = draw_count / (gpu_time * 1e-3);
        }

        simple_render::render_ui();

        glfwSwapBuffers(window);
        glfwPollEvents();
        if (glfwWindowShouldClose(window))
            running = GLFW_FALSE;
    }

    simple_render::cleanup();

	ImGui_ImplGlfwGL3_Shutdown();
    glfwHideWindow(window);
    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}

