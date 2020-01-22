#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw_gl3.h>
#include <glm/glm.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <Wingdi.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>

#pragma comment(lib, "opengl32.lib")

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
    void cleanup();

    GLuint create_shader(GLenum type, const char* shaderCode);
    GLuint create_program(GLuint vertex, GLuint fragment);

    GLuint framebuffer;
    GLuint texture;

    GLint position_attribute;
    GLint texcoord_attribute;
    GLint sampler_location;

    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;

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

uniform sampler2D u_sampler;
varying vec2 v_texcoord;
void main()
{
    gl_FragColor = texture2D(u_sampler, v_texcoord);
}
)__";
}

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
        printf("%s (%d) %s\n", __FILE__, __LINE__, buffer.data());
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
            printf("%s:%d %d: %s", __FILE__, __LINE__, status, log);
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

    assert(position_attribute >= 0);
    assert(texcoord_attribute >= 0);
    assert(sampler_location >= 0);

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
    glUseProgram(program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(sampler_location, 0);

    float vertices[] = {
        -1.0, -1.0, 0.0, 0.0,
        +3.0, -1.0, 2.0, 0.0,
        -1.0, +3.0, 0.0, 2.0
    };
    const void* position = vertices;
    const void* texcoord = &vertices[2];
    
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

void render_profile_ui()
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

void render_ui()
{
    ImGui_ImplGlfwGL3_NewFrame();
    render_profile_ui();
    ImGui::Render();
    ImGui::EndFrame();
}

namespace {

    void reportLastWindowsError() {
        LPSTR lpMessageBuffer = nullptr;
        DWORD dwError = GetLastError();

        if (dwError == 0) {
            return;
        }
        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            dwError,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            lpMessageBuffer,
            0, nullptr
        );

        trace("Windows error code: %d . %s\n", dwError, lpMessageBuffer);
        LocalFree(lpMessageBuffer);
    }

    HDC hdc;    
    HWND hwnd;
    HGLRC context;
}

bool wgl_context_create(void* window);
void wgl_context_destroy();

bool wgl_context_create(void* window)
{
    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR),
        1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,    // Flags
        PFD_TYPE_RGBA,        // The kind of framebuffer. RGBA or palette.
        32,                   // Colordepth of the framebuffer.
        0, 0, 0, 0, 0, 0,
        0,
        0,
        0,
        0, 0, 0, 0,
        24,                   // Number of bits for the depthbuffer
        0,                    // Number of bits for the stencilbuffer
        0,                    // Number of Aux buffers in the framebuffer.
        PFD_MAIN_PLANE,
        0,
        0, 0, 0
    };

    hwnd = reinterpret_cast<HWND>(window);
    hdc = ::GetDC(hwnd);

    int pixelFormat = ChoosePixelFormat(hdc, &pfd);
    if (!pixelFormat)
        return false;
    if (!DescribePixelFormat(hdc, pixelFormat, sizeof(PIXELFORMATDESCRIPTOR), &pfd))
        return false;
    if (!SetPixelFormat(hdc, pixelFormat, &pfd))
        return false;

    HGLRC dummyContext = wglCreateContext(hdc);
    if (!dummyContext)
        return false;

    if (!wglMakeCurrent(hdc, dummyContext))
    {
        wglDeleteContext(dummyContext);
        wgl_context_destroy();
        return false;
    }

    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribs =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

    if (wglCreateContextAttribs)
    {
        int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
            WGL_CONTEXT_MINOR_VERSION_ARB, 1,
            WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
            0, 0
        };

        context = wglCreateContextAttribs(hdc, nullptr, attribs);
    }
    else
    {
        context = wglCreateContext(hdc);
    }

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(dummyContext);
    dummyContext = nullptr;

    if (!context || !wglMakeCurrent(hdc, context)) {
        DWORD dwError = GetLastError();
        if (dwError == (0xc0070000 | ERROR_INVALID_VERSION_ARB))
            trace("WGL: Driver does not support OpenGL version");
        else if (dwError == (0xc0070000 | ERROR_INVALID_PROFILE_ARB))
            trace("WGL: Driver does not support the requested OpenGL profile");
        else if (dwError == (0xc0070000 | ERROR_INCOMPATIBLE_DEVICE_CONTEXTS_ARB))
            trace("WGL: The share context is not compatible with the requested context");
        else
            trace("WGL: Failed to create OpenGL context");
        wgl_context_destroy();
        return false;
    }

    gladLoadGL();

    return true;
}

void wgl_context_destroy()
{
    // NOTE: 
    // if you make two consecutive wlgMakeCurrent(NULL, NULL) calls,
    // An invalid handle error may occur.
    // But it seems to work well.
    wglMakeCurrent(NULL, NULL);
    if (context) {
        wglDeleteContext(context);
        context = NULL;
    }
    if (hwnd && hdc) {
        ReleaseDC(hwnd, hdc);
        hdc = NULL;
    }
    hwnd = NULL;
}

int main(void)
{
    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(640, 480, "uno", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

#if defined(GLFW_EXPOSE_NATIVE_WIN32)
    void* window_handle = glfwGetWin32Window(window);
#endif

    wgl_context_create(window_handle);

    glfwSetKeyCallback(window, key_callback);

    ImGui_ImplGlfwGL3_Init(window, false);

    triangle::setup();

    int running = GLFW_TRUE;
    while (running)
    {
        glfwGetFramebufferSize(window, &width, &height);

        GLuint time_query[2];

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

        render_ui();

        SwapBuffers(hdc);

        glfwPollEvents();
        if (glfwWindowShouldClose(window))
            running = GLFW_FALSE;
    }

    triangle::cleanup();

	ImGui_ImplGlfwGL3_Shutdown();

    wgl_context_destroy();

    glfwHideWindow(window);
    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}
