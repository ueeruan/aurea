// Native driver validation of the build-time ES shader translation. No mocks.
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(display, nullptr, nullptr)) { std::printf("EGL initialize failed\n"); return 1; }
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config{}; EGLint count = 0;
    if (!eglChooseConfig(display, attributes, &config, 1, &count) || count != 1) return 1;
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE || !eglMakeCurrent(display, surface, surface, context)) return 1;
    std::printf("GL_VERSION: %s\nGL_RENDERER: %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    GLint extensionCount = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    for (GLint i = 0; i < extensionCount; ++i) {
        const std::string extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        if (extension.find("border") != std::string::npos || extension.find("color_buffer") != std::string::npos
            || extension.find("timer_query") != std::string::npos) std::printf("EXT: %s\n", extension.c_str());
    }
    std::map<std::string, GLuint> shaders;
    unsigned failures = 0, programs = 0;
    for (const auto& file : std::filesystem::recursive_directory_iterator(argv[1])) {
        if (file.path().extension() != ".gles") continue;
        const auto path = std::filesystem::relative(file.path(), argv[1]).generic_string();
        const GLenum stage = path.find(".vert.") != std::string::npos ? GL_VERTEX_SHADER
                           : path.find(".frag.") != std::string::npos ? GL_FRAGMENT_SHADER : GL_COMPUTE_SHADER;
        std::ifstream stream(file.path()); std::ostringstream contents; contents << stream.rdbuf();
        const auto text = contents.str(); const char* source = text.c_str();
        GLuint shader = glCreateShader(stage);
        glShaderSource(shader, 1, &source, nullptr); glCompileShader(shader);
        GLint compiled = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            char log[8192]{}; glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::printf("FAIL compile %s: %s\n", path.c_str(), log); ++failures; glDeleteShader(shader);
        } else shaders.emplace(path, shader);
    }
    auto link = [&](const std::string& vertex, const std::string& other) {
        if ((!vertex.empty() && !shaders.count(vertex)) || !shaders.count(other)) { ++failures; return; }
        const GLuint program = glCreateProgram();
        if (!vertex.empty()) glAttachShader(program, shaders.at(vertex));
        glAttachShader(program, shaders.at(other)); glLinkProgram(program);
        GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[8192]{}; glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::printf("FAIL link %s + %s: %s\n", vertex.c_str(), other.c_str(), log); ++failures;
        } else ++programs;
        glDeleteProgram(program);
    };
    for (const auto& [name, shader] : shaders) {
        if (name.find(".comp.") != std::string::npos) { link("", name); continue; }
        if (name.find(".frag.") == std::string::npos) continue;
        std::string vertex = "common/fullscreen.vert.spv.gles";
        if (name == "composite/layer.frag.spv.gles" || name == "composite/blend.frag.spv.gles"
            || name == "scene3d/plane.frag.spv.gles") vertex = "composite/layer.vert.spv.gles";
        else if (name.find("scene3d/pbr/") == 0) vertex = "scene3d/pbr/mesh.vert.spv.gles";
        else if (name.find("scene3d/shadow/") == 0) vertex = "scene3d/shadow/shadow.vert.spv.gles";
        else if (name.find("particles/") == 0) vertex = "particles/particles.vert.spv.gles";
        else if (name.find("text/") == 0) vertex = "text/glyph.vert.spv.gles";
        else if (name.find("vector/") == 0) vertex = "vector/path.vert.spv.gles";
        link(vertex, name);
    }
    for (const auto& [name, shader] : shaders) glDeleteShader(shader);
    std::printf("shaders=%zu programs=%u failures=%u\n", shaders.size(), programs, failures);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface); eglDestroyContext(display, context); eglTerminate(display);
    return failures || shaders.empty() ? 1 : 0;
}
