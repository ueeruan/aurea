// =============================================================================
//  Aurea / platform / android / GlVideoBridge.cpp — ver GlVideoBridge.hpp.
// =============================================================================
#include "GlVideoBridge.hpp"

#include "aurea/core/Log.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace aurea::android {
namespace {

bool has_token(const char* list, const char* name) noexcept {
    if (!list || !name) return false;
    const size_t n = std::strlen(name);
    for (const char* p = list; (p = std::strstr(p, name)) != nullptr; p += n) {
        const bool startOk = p == list || p[-1] == ' ';
        const bool endOk = p[n] == ' ' || p[n] == '\0';
        if (startOk && endOk) return true;
    }
    return false;
}

// Vértice: o quadrilátero da tela inteira → uv da região visível no buffer.
// pos.y = -1 é a linha 0 da memória do alvo (y da janela 0 no FBO), e recebe
// v0, a linha de CIMA da região visível: a ordem das linhas se mantém do buffer
// do decoder ao alvo — e o Vulkan lê a linha 0 como o topo. Sem inverter.
constexpr const char* kVertex =
    "attribute vec2 a_pos;\n"
    "uniform vec4 u_rect;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 k = a_pos * 0.5 + 0.5;\n"
    "    v_uv = vec2(mix(u_rect.x, u_rect.z, k.x), mix(u_rect.y, u_rect.w, k.y));\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

// O sampler externo: o driver converte YUV (matriz/faixa do buffer), passo,
// fatia e compressão. highp nas coordenadas: mediump (10 bits) erra o texel
// num buffer de 1920.
constexpr const char* kFragment =
    "#extension GL_OES_EGL_image_external : require\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform samplerExternalOES u_tex;\n"
    "uniform vec4 u_clamp;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(texture2D(u_tex, clamp(v_uv, u_clamp.xy, u_clamp.zw)).rgb, 1.0);\n"
    "}\n";

GLuint compile(GLenum type, const char* src) noexcept {
    const GLuint s = glCreateShader(type);
    if (!s) return 0;
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        AUREA_LOG_WARN("video GL: shader nao compilou: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/// Os alvos RGBA de um decoder. Vive enquanto houver quadro com alvo dele
/// (o `shared_ptr` do quadro segura o conjunto).
struct Pool {
    std::mutex mutex;
    std::condition_variable freed;
    std::vector<AHardwareBuffer*> free;
    u32 width = 0, height = 0;
    u32 live = 0;          ///< alvos do tamanho atual (livres + em uso)
    u32 cap = 6;
    bool closed = false;

    ~Pool() {
        for (AHardwareBuffer* b : free) AHardwareBuffer_release(b);
    }
};

struct Holder final : GlVideoBridge::Target {
    std::shared_ptr<Pool> pool;
};

void give_back(Holder* h) noexcept {
    if (!h) return;
    if (h->pool && h->buffer) {
        std::lock_guard<std::mutex> lock(h->pool->mutex);
        if (!h->pool->closed && h->width == h->pool->width && h->height == h->pool->height) {
            h->pool->free.push_back(h->buffer);
            h->buffer = nullptr;
        } else if (h->width == h->pool->width && h->height == h->pool->height && h->pool->live > 0) {
            --h->pool->live;
        }
        h->pool->freed.notify_all();
    }
    if (h->buffer) AHardwareBuffer_release(h->buffer);
    delete h;
}

} // namespace

struct GlVideoBridge::Impl {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    bool fenceSync = false;

    PFNEGLCREATEIMAGEKHRPROC createImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage = nullptr;
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC clientBuffer = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC imageTargetTexture = nullptr;
    PFNEGLCREATESYNCKHRPROC createSync = nullptr;
    PFNEGLCLIENTWAITSYNCKHRPROC clientWaitSync = nullptr;
    PFNEGLDESTROYSYNCKHRPROC destroySync = nullptr;

    GLuint program = 0;
    GLint aPos = -1, uRect = -1, uClamp = -1, uTex = -1;

    struct Source { EGLImageKHR image = EGL_NO_IMAGE_KHR; GLuint texture = 0; };
    struct Dest { EGLImageKHR image = EGL_NO_IMAGE_KHR; GLuint texture = 0; GLuint fbo = 0; u32 width = 0, height = 0; };
    std::unordered_map<AHardwareBuffer*, Source> sources;   // cada entrada segura uma referência do buffer
    std::unordered_map<AHardwareBuffer*, Dest> dests;       // idem

    std::shared_ptr<Pool> pool = std::make_shared<Pool>();
    std::mutex use;   // uma conversão por vez; o destrutor espera

    // O que estava corrente na thread antes (normalmente nada).
    EGLDisplay prevDisplay = EGL_NO_DISPLAY;
    EGLContext prevContext = EGL_NO_CONTEXT;
    EGLSurface prevDraw = EGL_NO_SURFACE, prevRead = EGL_NO_SURFACE;

    bool make_current() noexcept {
        prevDisplay = eglGetCurrentDisplay();
        prevContext = eglGetCurrentContext();
        prevDraw = eglGetCurrentSurface(EGL_DRAW);
        prevRead = eglGetCurrentSurface(EGL_READ);
        return eglMakeCurrent(display, surface, surface, context) == EGL_TRUE;
    }

    void release_current() noexcept {
        if (prevContext != EGL_NO_CONTEXT && prevDisplay != EGL_NO_DISPLAY) {
            eglMakeCurrent(prevDisplay, prevDraw, prevRead, prevContext);
        } else {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        prevDisplay = EGL_NO_DISPLAY;
        prevContext = EGL_NO_CONTEXT;
    }

    void drop_source(AHardwareBuffer* b, Source& s) noexcept {   // contexto corrente
        if (s.texture) glDeleteTextures(1, &s.texture);
        if (s.image != EGL_NO_IMAGE_KHR) destroyImage(display, s.image);
        AHardwareBuffer_release(b);
    }

    void drop_dest(AHardwareBuffer* b, Dest& d) noexcept {   // contexto corrente
        if (d.fbo) glDeleteFramebuffers(1, &d.fbo);
        if (d.texture) glDeleteTextures(1, &d.texture);
        if (d.image != EGL_NO_IMAGE_KHR) destroyImage(display, d.image);
        AHardwareBuffer_release(b);
    }

    EGLImageKHR image_of(AHardwareBuffer* b) noexcept {
        const EGLClientBuffer cb = clientBuffer(b);
        if (!cb) return EGL_NO_IMAGE_KHR;
        const EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        return createImage(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, cb, attrs);
    }

    ~Impl() {
        std::lock_guard<std::mutex> lock(use);
        {
            std::lock_guard<std::mutex> pl(pool->mutex);
            pool->closed = true;
            for (AHardwareBuffer* b : pool->free) AHardwareBuffer_release(b);
            pool->free.clear();
            pool->freed.notify_all();
        }
        if (display == EGL_NO_DISPLAY) return;
        if (context != EGL_NO_CONTEXT && make_current()) {
            for (auto& [b, s] : sources) drop_source(b, s);
            for (auto& [b, d] : dests) drop_dest(b, d);
            if (program) glDeleteProgram(program);
            release_current();
        } else {
            // Sem contexto não há como apagar os objetos GL; o buffer ainda é solto.
            for (auto& [b, s] : sources) AHardwareBuffer_release(b);
            for (auto& [b, d] : dests) AHardwareBuffer_release(b);
        }
        sources.clear();
        dests.clear();
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        // eglTerminate NÃO: o display é do processo (o backend GLES pode usá-lo).
    }
};

GlVideoBridge::GlVideoBridge(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
GlVideoBridge::~GlVideoBridge() = default;

std::unique_ptr<GlVideoBridge> GlVideoBridge::create() noexcept {
    auto impl = std::make_unique<Impl>();
    Impl& g = *impl;
    g.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g.display == EGL_NO_DISPLAY) return nullptr;
    EGLint major = 0, minor = 0;
    if (!eglInitialize(g.display, &major, &minor)) {
        g.display = EGL_NO_DISPLAY;
        return nullptr;
    }
    const char* egl = eglQueryString(g.display, EGL_EXTENSIONS);
    if (!has_token(egl, "EGL_KHR_image_base") || !has_token(egl, "EGL_ANDROID_image_native_buffer")) {
        AUREA_LOG_WARN("video GL: EGL sem imagem de buffer nativo");
        return nullptr;
    }
    g.createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    g.destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    g.clientBuffer = reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
        eglGetProcAddress("eglGetNativeClientBufferANDROID"));
    if (!g.createImage || !g.destroyImage || !g.clientBuffer) {
        AUREA_LOG_WARN("video GL: eglGetNativeClientBufferANDROID indisponivel");
        return nullptr;
    }
    if (has_token(egl, "EGL_KHR_fence_sync")) {
        g.createSync = reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(eglGetProcAddress("eglCreateSyncKHR"));
        g.clientWaitSync = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(eglGetProcAddress("eglClientWaitSyncKHR"));
        g.destroySync = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(eglGetProcAddress("eglDestroySyncKHR"));
        g.fenceSync = g.createSync && g.clientWaitSync && g.destroySync;
    }

    const EGLint cfgAttrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE,
    };
    EGLint count = 0;
    if (!eglChooseConfig(g.display, cfgAttrs, &g.config, 1, &count) || count < 1) {
        AUREA_LOG_WARN("video GL: nenhuma config EGL ES2");
        return nullptr;
    }
    const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    g.context = eglCreateContext(g.display, g.config, EGL_NO_CONTEXT, ctxAttrs);
    if (g.context == EGL_NO_CONTEXT) {
        AUREA_LOG_WARN("video GL: contexto EGL nao criado (0x%x)", eglGetError());
        return nullptr;
    }
    const EGLint pbAttrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    g.surface = eglCreatePbufferSurface(g.display, g.config, pbAttrs);
    if (g.surface == EGL_NO_SURFACE) {
        AUREA_LOG_WARN("video GL: pbuffer nao criado (0x%x)", eglGetError());
        return nullptr;
    }
    if (!g.make_current()) {
        AUREA_LOG_WARN("video GL: eglMakeCurrent falhou (0x%x)", eglGetError());
        return nullptr;
    }
    bool ok = false;
    do {
        const char* gl = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (!has_token(gl, "GL_OES_EGL_image_external")) {
            AUREA_LOG_WARN("video GL: sem GL_OES_EGL_image_external");
            break;
        }
        g.imageTargetTexture = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            eglGetProcAddress("glEGLImageTargetTexture2DOES"));
        if (!g.imageTargetTexture) break;
        const GLuint vs = compile(GL_VERTEX_SHADER, kVertex);
        const GLuint fs = compile(GL_FRAGMENT_SHADER, kFragment);
        if (!vs || !fs) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            break;
        }
        g.program = glCreateProgram();
        glAttachShader(g.program, vs);
        glAttachShader(g.program, fs);
        glBindAttribLocation(g.program, 0, "a_pos");
        glLinkProgram(g.program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint linked = 0;
        glGetProgramiv(g.program, GL_LINK_STATUS, &linked);
        if (!linked) {
            AUREA_LOG_WARN("video GL: programa nao linkou");
            glDeleteProgram(g.program);
            g.program = 0;
            break;
        }
        g.aPos = 0;
        g.uRect = glGetUniformLocation(g.program, "u_rect");
        g.uClamp = glGetUniformLocation(g.program, "u_clamp");
        g.uTex = glGetUniformLocation(g.program, "u_tex");
        ok = true;
    } while (false);
    g.release_current();
    if (!ok) return nullptr;
    AUREA_LOG_INFO("video GL: contexto do driver pronto (EGL %d.%d, fence %s)", major, minor, g.fenceSync ? "sim" : "glFinish");
    return std::unique_ptr<GlVideoBridge>(new GlVideoBridge(std::move(impl)));
}

void GlVideoBridge::set_max_live_targets(u32 n) noexcept {
    std::lock_guard<std::mutex> lock(impl_->pool->mutex);
    impl_->pool->cap = n < 2 ? 2 : n;
}

u32 GlVideoBridge::max_live_targets() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->pool->mutex);
    return impl_->pool->cap;
}

void GlVideoBridge::forget_sources() noexcept {
    Impl& g = *impl_;
    std::lock_guard<std::mutex> lock(g.use);
    if (g.sources.empty()) return;
    if (g.make_current()) {
        for (auto& [b, s] : g.sources) g.drop_source(b, s);
        g.release_current();
    } else {
        for (auto& [b, s] : g.sources) AHardwareBuffer_release(b);
    }
    g.sources.clear();
}

Status GlVideoBridge::convert(AHardwareBuffer* source, const ExternalQuad& quad,
                              std::shared_ptr<const Target>& out) noexcept {
    out.reset();
    Impl& g = *impl_;
    if (!source || !quad.width || !quad.height) return Status{Errc::InvalidArgument, "quadro vazio"};

    // Alvo livre (ou um novo, até o teto). Sem nenhum: espera um pouco — o
    // renderer solta o seu depois do fence; desistir perderia este quadro.
    AHardwareBuffer* target = nullptr;
    {
        std::unique_lock<std::mutex> pl(g.pool->mutex);
        Pool& p = *g.pool;
        if (p.width != quad.width || p.height != quad.height) {
            for (AHardwareBuffer* b : p.free) AHardwareBuffer_release(b);
            p.free.clear();
            p.width = quad.width;
            p.height = quad.height;
            p.live = 0;
        }
        const bool got = p.freed.wait_for(pl, std::chrono::milliseconds(250),
                                          [&] { return !p.free.empty() || p.live < p.cap || p.closed; });
        if (p.closed) return Status{Errc::InvalidState, "decoder fechando"};
        if (!got) return Status{Errc::BudgetExceeded, "todos os quadros RGBA em uso"};
        if (!p.free.empty()) {
            target = p.free.back();
            p.free.pop_back();
        } else {
            AHardwareBuffer_Desc d{};
            d.width = quad.width;
            d.height = quad.height;
            d.layers = 1;
            d.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
            d.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
            if (AHardwareBuffer_allocate(&d, &target) != 0 || !target) {
                return Status{Errc::OutOfMemory, "AHardwareBuffer RGBA nao alocado"};
            }
            ++p.live;
        }
    }
    auto* holder = new Holder();
    holder->buffer = target;
    holder->width = quad.width;
    holder->height = quad.height;
    holder->pool = g.pool;
    std::shared_ptr<const Target> lease(holder, [](const Target* t) { give_back(static_cast<Holder*>(const_cast<Target*>(t))); });

    std::lock_guard<std::mutex> lock(g.use);
    if (!g.make_current()) return Status{Errc::InvalidState, "eglMakeCurrent falhou"};
    struct Release { Impl& g; ~Release() { g.release_current(); } } release{g};
    while (glGetError() != GL_NO_ERROR) {}

    // Fonte: EGLImage do buffer do decoder (cacheada: o ImageReader recicla um
    // conjunto fixo) religada a cada quadro — o conteúdo mudou.
    auto src = g.sources.find(source);
    if (src == g.sources.end()) {
        if (g.sources.size() >= 48) {   // leitor recriado sem aviso: recomeça
            for (auto& [b, s] : g.sources) g.drop_source(b, s);
            g.sources.clear();
        }
        Impl::Source s;
        s.image = g.image_of(source);
        if (s.image == EGL_NO_IMAGE_KHR) return Status{Errc::UnsupportedFormat, "EGLImage do buffer do decoder recusado"};
        glGenTextures(1, &s.texture);
        AHardwareBuffer_acquire(source);
        src = g.sources.emplace(source, s).first;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, src->second.texture);
    g.imageTargetTexture(GL_TEXTURE_EXTERNAL_OES, static_cast<GLeglImageOES>(src->second.image));
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (glGetError() != GL_NO_ERROR) return Status{Errc::UnsupportedFormat, "textura externa recusou o buffer do decoder"};

    // Alvo: EGLImage do AHardwareBuffer RGBA + FBO (cacheados por buffer).
    auto dst = g.dests.find(target);
    if (dst == g.dests.end()) {
        Impl::Dest d;
        d.width = quad.width;
        d.height = quad.height;
        d.image = g.image_of(target);
        if (d.image == EGL_NO_IMAGE_KHR) return Status{Errc::UnsupportedFeature, "EGLImage do alvo RGBA recusado"};
        glGenTextures(1, &d.texture);
        glBindTexture(GL_TEXTURE_2D, d.texture);
        g.imageTargetTexture(GL_TEXTURE_2D, static_cast<GLeglImageOES>(d.image));
        glGenFramebuffers(1, &d.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, d.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, d.texture, 0);
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        AHardwareBuffer_acquire(target);
        dst = g.dests.emplace(target, d).first;
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            AUREA_LOG_WARN("video GL: FBO RGBA incompleto (0x%x)", status);
            return Status{Errc::UnsupportedFeature, "FBO do alvo RGBA incompleto"};
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, dst->second.fbo);
    glViewport(0, 0, static_cast<GLsizei>(quad.width), static_cast<GLsizei>(quad.height));
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    // Limpar antes: num GPU de tiles, o conteúdo anterior não é carregado.
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(g.program);
    glUniform1i(g.uTex, 0);
    glUniform4f(g.uRect, quad.u0, quad.v0, quad.u1, quad.v1);
    glUniform4f(g.uClamp, quad.minU, quad.minV, quad.maxU, quad.maxV);
    static const GLfloat kQuad[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer(static_cast<GLuint>(g.aPos), 2, GL_FLOAT, GL_FALSE, 0, kQuad);
    glEnableVertexAttribArray(static_cast<GLuint>(g.aPos));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(static_cast<GLuint>(g.aPos));
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        AUREA_LOG_WARN("video GL: desenho falhou (0x%x)", err);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return Status{Errc::InvalidState, "passe GL do video falhou"};
    }

    // O quadro só segue depois que a GPU terminou de escrever o alvo. (Um
    // fence nativo importado no Vulkan pouparia esta espera; ela custa o
    // tempo de um quadrilátero, na thread de decode — não na de render.)
    bool waited = false;
    if (g.fenceSync) {
        const EGLSyncKHR sync = g.createSync(g.display, EGL_SYNC_FENCE_KHR, nullptr);
        glFlush();
        if (sync != EGL_NO_SYNC_KHR) {
            const EGLint r = g.clientWaitSync(g.display, sync, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, 2'000'000'000);
            g.destroySync(g.display, sync);
            waited = r == EGL_CONDITION_SATISFIED_KHR;
        }
    }
    if (!waited) glFinish();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    out = std::move(lease);
    return OkStatus;
}

} // namespace aurea::android
