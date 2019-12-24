#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw_gl3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace {
    float width = 600;
    float height = 400;
    float cpu_time = 0.f;
    float gpu_time = 0.f;
}

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
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

int main(void)
{
    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

    GLFWwindow* window = glfwCreateWindow(640, 480, "uno", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSetKeyCallback(window, key_callback);

    ImGui_ImplGlfwGL3_Init(window, false);

    float tick = 0;
    int running = GLFW_TRUE;
    while (running)
    {
        int width, height;
        float c = sin(tick)*0.5f + 0.5f;
        glfwGetFramebufferSize(window, &width, &height);

        GLuint time_query[2];

        auto cpu_tick = std::chrono::high_resolution_clock::now();
        glBeginQuery(GL_TIME_ELAPSED, time_query[0]);
        
        glViewport(0, 0, width, height);
        glClearColor(c, 0.3f, 0.5f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

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

        glfwSwapBuffers(window);
        glfwPollEvents();
        if (glfwWindowShouldClose(window))
            running = GLFW_FALSE;
        tick += 0.1f;
    }

	ImGui_ImplGlfwGL3_Shutdown();
    glfwHideWindow(window);
    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}

