#include <glad/glad.h>
#include <GLFW/glfw3.h>
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

#if _WIN32
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* _str);
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
    OutputDebugStringA(message); // visual studio
}

namespace {
    GLint samples = 4;
    int width = 600;
    int height = 400;
    float cpu_time = 0.f;
    float gpu_time = 0.f;
}

namespace triangle
{
    bool setup();
    void render();
    void render_frame();
    void begin_frame();
    void end_frame();
    void render_ui();
    void render_profile_ui();
    void cleanup();

    GLuint create_shader(GLenum type, const char* shaderCode);
    GLuint create_program(GLuint vertex, GLuint fragment);

    GLuint framebuffer;
    GLuint texture;
    GLuint vao;
    GLuint vbo;
    GLuint ubo;

    GLint position_attribute;
    GLint texcoord_attribute;
    GLint sampler_location;
    GLint block_index;

    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;

const char* vertex_shader_code = R"__(
#version 450

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 0) out vec2 v_texcoord;

void main()
{
    v_texcoord = a_texcoord;
    gl_Position = vec4(a_position, 0, 1);
}
)__";

const char* fragment_shader_code = R"__(
#version 450

layout(binding = 0) uniform sampler2D u_sampler;
layout(std140, binding = 0) uniform u_fragment
{
    vec4 color;
} u_frag;

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 color_out;

void main()
{
    color_out = texture2D(u_sampler, v_texcoord) * u_frag.color;
}
)__";

} // namespace triangle

GLuint triangle::create_shader(GLenum type, const char* shaderCode)
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

GLuint triangle::create_program(GLuint vertex, GLuint fragment)
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

bool triangle::setup()
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, 2, 2, 0, format, GL_FLOAT, texel);
    glBindTexture(GL_TEXTURE_2D, 0);

    texture = instance;

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 64*1024*1024, 0, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // up-to 16kb
    glGenBuffers(1, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, 16*1024, 0, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    return true;
}

void triangle::begin_frame()
{ 
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, width, height);
    glClearDepth(1.0);
    glClearColor(0.3f, 0.3f, 0.5f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnableVertexAttribArray(position_attribute);
    glEnableVertexAttribArray(texcoord_attribute);
}

void triangle::render_frame()
{
    static float f = 0.f;
    glUseProgram(program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(sampler_location, 0);

    float c = std::cos(f += 0.11f)*0.5f+0.5f;
    glm::vec4 color(c, 1., 1., 1.);

    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::vec4), glm::value_ptr(color));
    glBindBufferRange(GL_UNIFORM_BUFFER, block_index, ubo, 0, sizeof(glm::vec4));
    // without below line, bind block_point acording to shader
    // glUniformBlockBinding(program, block_index, block_point);
    const GLuint block_point = 0;
    glBindBufferBase(GL_UNIFORM_BUFFER, block_point, ubo);

    float vertices[] = {
        -1.0, -1.0, 0.0, 0.0,
        +3.0, -1.0, 2.0, 0.0,
        -1.0, +3.0, 0.0, 2.0
    };
    const void* position = (size_t*)0;
    const void* texcoord = (size_t*)(2*sizeof(float));

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

    glVertexAttribPointer(position_attribute, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), position);
    glVertexAttribPointer(texcoord_attribute, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), texcoord);

    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void triangle::end_frame()
{ 
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDisableVertexAttribArray(position_attribute);
    glDisableVertexAttribArray(texcoord_attribute);
}

void triangle::render()
{
    begin_frame();
    render_frame();
    end_frame();
}

void triangle::cleanup()
{
    glDeleteVertexArrays(1, &vao);

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

void triangle::render_profile_ui()
{
    bool bUpdated = false;

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
    ImGui::Separator();
    ImGui::Unindent();
    ImGui::End();
}

void triangle::render_ui()
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
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(640, 480, "uno", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(1);
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

    triangle::setup();

    int running = GLFW_TRUE;
    while (running)
    {
        glfwGetFramebufferSize(window, &width, &height);

        GLuint time_query[2];
        glGenQueries(2, time_query);

        auto cpu_tick = std::chrono::high_resolution_clock::now();
        glBeginQuery(GL_TIME_ELAPSED, time_query[0]);

        triangle::render();

        glEndQuery(GL_TIME_ELAPSED);

        auto cpu_tock = std::chrono::high_resolution_clock::now();
        auto cpu_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(cpu_tock - cpu_tick);
        cpu_time = static_cast<float>(cpu_elapsed.count() / 1000.0);

        GLint stopTimerAvailable = 0;
        while (!stopTimerAvailable) {
            glGetQueryObjectiv(time_query[0], GL_QUERY_RESULT_AVAILABLE, &stopTimerAvailable);
        }

        // get query results
        GLuint64 gpu_elapsed = 0;
        glGetQueryObjectui64v(time_query[0], GL_QUERY_RESULT, &gpu_elapsed);

        gpu_time = static_cast<float>(gpu_elapsed / 1e6f);

        triangle::render_ui();

        glfwSwapBuffers(window);
        glfwPollEvents();
        if (glfwWindowShouldClose(window))
            running = GLFW_FALSE;
    }

    triangle::cleanup();

	ImGui_ImplGlfwGL3_Shutdown();
    glfwHideWindow(window);
    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}

