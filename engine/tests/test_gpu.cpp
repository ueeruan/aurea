// =============================================================================
//  Testes visuais na GPU REAL (Vulkan do host), com os MESMOS shaders SPIR-V e
//  o MESMO backend do Android.
//
//  Três tipos de verificação:
//    1. ANALÍTICA — o resultado calculado à mão (exposição dobra a luz, a cópia
//       central do Motion Tile é idêntica à layer, NV12 BT.709 limitado dá o
//       vermelho exato...). Tolerância numérica pequena.
//    2. PROPRIEDADE — o que tem de valer para qualquer entrada (blur conserva
//       energia, é simétrico; o Motion Tile cobre o quadro inteiro).
//    3. GOLDEN FRAME — o PNG esperado em tests/golden. Se um shader mudar e o
//       resultado mudar, o teste acusa. Regerar: AUREA_UPDATE_GOLDENS=1.
//
//  Sem GPU Vulkan no host, os testes avisam e passam vazios (o CI sem GPU roda
//  o resto).
// =============================================================================
#include "TestFramework.hpp"

#if defined(AUREA_TEST_VULKAN)

#include "ImageIO.hpp"
#include "SyntheticVideo.hpp"
#include "VulkanBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/effects/MotionTile.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/Renderer.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

using namespace aurea;
using namespace aurea::test;

namespace {

// -----------------------------------------------------------------------------
// Utilidades de cor
// -----------------------------------------------------------------------------
f32 half_to_float(u16 h) {
    const u32 sign = (h & 0x8000u) << 16;
    u32 exp = (h >> 10) & 0x1F;
    u32 mant = h & 0x3FF;
    u32 bits;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else {
            exp = 127 - 15 + 1;
            while (!(mant & 0x400)) { mant <<= 1; --exp; }
            mant &= 0x3FF;
            bits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    f32 f;
    std::memcpy(&f, &bits, 4);
    return f;
}

f32 srgb_decode(f32 c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
f32 srgb_encode(f32 c) {
    c = std::fmax(0.0f, c);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

struct FloatImage {
    u32 width = 0, height = 0;
    std::vector<f32> px;   // RGBA linear pré-multiplicado
    [[nodiscard]] const f32* at(u32 x, u32 y) const { return &px[(static_cast<usize>(y) * width + x) * 4]; }
    [[nodiscard]] Vec4 v(u32 x, u32 y) const { const f32* p = at(x, y); return Vec4{p[0], p[1], p[2], p[3]}; }
    [[nodiscard]] Vec4 mean() const {
        f64 s[4] = {0, 0, 0, 0};
        for (usize i = 0; i < px.size(); i += 4) for (int c = 0; c < 4; ++c) s[c] += px[i + c];
        const f64 n = static_cast<f64>(px.size() / 4);
        return Vec4{static_cast<f32>(s[0] / n), static_cast<f32>(s[1] / n), static_cast<f32>(s[2] / n),
                    static_cast<f32>(s[3] / n)};
    }
    [[nodiscard]] Image8 encoded() const {
        Image8 out;
        out.width = width;
        out.height = height;
        out.rgba.resize(px.size());
        for (usize i = 0; i < px.size(); i += 4) {
            for (int c = 0; c < 3; ++c) {
                out.rgba[i + c] = static_cast<u8>(std::lround(std::fmin(1.0f, srgb_encode(px[i + c])) * 255.0f));
            }
            out.rgba[i + 3] = 255;
        }
        return out;
    }
};

bool near4(Vec4 a, Vec4 b, f32 tol) {
    const bool ok = std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol && std::fabs(a.z - b.z) <= tol
                 && std::fabs(a.w - b.w) <= tol;
    if (!ok) {
        std::printf("\n    obtido (%.4f %.4f %.4f %.4f) esperado (%.4f %.4f %.4f %.4f)", a.x, a.y, a.z, a.w,
                    b.x, b.y, b.z, b.w);
    }
    return ok;
}

// -----------------------------------------------------------------------------
// GPU compartilhada entre os testes (subir o Vulkan uma vez só).
// -----------------------------------------------------------------------------
struct Gpu {
    vk::Backend backend;
    EffectRegistry effects;
    Renderer renderer;
    bool ok = false;
    std::unordered_map<u64, TextureHandle> targets;

    Gpu() {
        BackendConfig cfg;
        cfg.enableValidation = true;   // com a camada instalada, erros de uso aparecem no log
        cfg.framesInFlight = 2;
        if (!backend.initialize(cfg).ok()) return;
        register_builtin_effects(effects);
        ok = renderer.initialize(backend, effects).ok();
    }
    ~Gpu() {
        backend.wait_idle();
        for (auto& [k, t] : targets) backend.destroy_texture(t);
        renderer.shutdown();
        backend.shutdown();
    }

    TextureHandle target(u32 w, u32 h) {
        const u64 key = (static_cast<u64>(w) << 32) | h;
        if (auto it = targets.find(key); it != targets.end()) return it->second;
        TextureDesc d;
        d.width = w;
        d.height = h;
        d.format = SurfaceFormat::RGBA16F;
        d.renderTarget = true;
        d.sampled = true;
        d.transferSrc = true;
        auto t = backend.create_texture(d);
        targets[key] = *t;
        return *t;
    }
};

Gpu& gpu() {
    static Gpu g;
    return g;
}

#define AUREA_REQUIRE_GPU()                                            \
    do {                                                               \
        if (!gpu().ok) {                                               \
            std::printf("(sem GPU Vulkan: pulado) ");                  \
            return;                                                    \
        }                                                              \
    } while (0)

// -----------------------------------------------------------------------------
// Cena de teste
// -----------------------------------------------------------------------------
struct Scene {
    Project project;
    Composition* comp = nullptr;
    std::unordered_map<u64, ImagePixels> images;
    std::unique_ptr<SyntheticFactory> factory;
    MediaManager media;

    Scene(u32 w, u32 h, f64 fps = 30.0) {
        auto p = Project::create_new(w, h, fps, "gpu");
        project = std::move(*p);
        comp = project.timeline().composition(project.timeline().root());
        comp->set_background(Color{0, 0, 0, 1});
        comp->set_duration(FrameIndex{300});
        // Projeto novo: o renderer solta as texturas do anterior (como o
        // Engine faz em new_project).
        gpu().renderer.release_project_resources();
    }

    static const ImagePixels* lookup(void* self, AssetId id) {
        auto* s = static_cast<Scene*>(self);
        auto it = s->images.find(id.pack());
        return it == s->images.end() ? nullptr : &it->second;
    }

    LayerId image(ImagePixels px, f32 x, f32 y, f32 scale = 1.0f) {
        Asset a;
        a.kind = AssetKind::Image;
        const AssetId aid = project.add_asset(std::move(a));
        const u32 w = px.width, h = px.height;
        images[aid.pack()] = std::move(px);
        const LayerId id = comp->add_layer(LayerKind::Image, "img");
        Layer* l = comp->layer(id);
        l->source = aid;
        l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0};
        l->transform.position = Vec3{x, y, 0};
        l->transform.scale = Vec3{scale, scale, 1};
        return id;
    }

    LayerId solid(f32 w, f32 h, Vec4 color, f32 x, f32 y) {
        const LayerId id = comp->add_layer(LayerKind::Shape, "solido");
        Layer* l = comp->layer(id);
        l->shape.bounds = Rect{0, 0, w, h};
        l->shape.fillColor = color;
        l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0};
        l->transform.position = Vec3{x, y, 0};
        return id;
    }

    LayerId video(const SyntheticConfig& cfg, f32 x, f32 y, f32 scale = 1.0f) {
        factory = std::make_unique<SyntheticFactory>(cfg);
        media.set_factory(factory.get());
        Asset a;
        a.kind = AssetKind::Video;
        a.video.width = cfg.width;
        a.video.height = cfg.height;
        a.video.fps = cfg.fps;
        const AssetId aid = project.add_asset(std::move(a));
        const LayerId id = comp->add_layer(LayerKind::Video, "video");
        Layer* l = comp->layer(id);
        l->source = aid;
        l->end = FrameIndex{static_cast<i64>(cfg.frameCount)};
        l->transform.anchor = Vec3{cfg.width * 0.5f, cfg.height * 0.5f, 0};
        l->transform.position = Vec3{x, y, 0};
        l->transform.scale = Vec3{scale, scale, 1};
        return id;
    }

    EffectInstance& add_effect(LayerId id, const char* key) {
        Layer* l = comp->layer(id);
        EffectInstance e;
        e.id = l->alloc_effect_id();
        e.type = effect_type_id(key);
        initialize_instance(e, *gpu().effects.params(e.type));
        l->effects.push_back(std::move(e));
        return l->effects.back();
    }

    /// Renderiza o instante `t` fora da tela e lê a composição de volta.
    FloatImage render(FrameIndex t = FrameIndex{0}, u32 den = 1) {
        Gpu& g = gpu();
        RenderSettings rs;
        rs.previewDenominator = den;
        rs.dither = false;
        rs.gpuTimers = true;
        FrameSnapshot snap;
        static u64 frame = 0;
        for (int attempt = 0; attempt < 1500; ++attempt) {
            g.renderer.prepare(*comp, project, t, &media, &Scene::lookup, this, rs, ++frame, 0,
                               DecodeMode::Still, 1.0f, snap);
            if (snap.missingVideoFrames == 0 && snap.staleVideoFrames == 0) break;
            for (RenderLayer& l : snap.layers) l.source.frame.reset();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const u32 w = std::max(1u, comp->width() / den);
        const u32 h = std::max(1u, comp->height() / den);
        OffscreenTarget off{g.target(w, h), w, h};
        FrameStats stats;
        RenderTimings timings;
        const Status s = g.renderer.render(snap, rs, &off, stats, timings);
        AUREA_CHECK_MSG(s.ok(), "render offscreen falhou");
        g.backend.wait_idle();
        std::vector<u16> half(static_cast<usize>(w) * h * 4);
        AUREA_CHECK(g.backend.read_texture(off.texture, half.data(), w * 8).ok());
        FloatImage img;
        img.width = w;
        img.height = h;
        img.px.resize(half.size());
        for (usize i = 0; i < half.size(); ++i) img.px[i] = half_to_float(half[i]);
        return img;
    }
};

ImagePixels uniform_image(u32 w, u32 h, u8 r, u8 g, u8 b, u8 a = 255) {
    ImagePixels px;
    px.width = w;
    px.height = h;
    px.rgba.resize(static_cast<usize>(w) * h * 4);
    for (usize i = 0; i < px.rgba.size(); i += 4) {
        px.rgba[i] = r; px.rgba[i + 1] = g; px.rgba[i + 2] = b; px.rgba[i + 3] = a;
    }
    return px;
}

/// Quadrantes vermelho/verde/azul/branco — orientação e posição visíveis.
ImagePixels quadrants(u32 w, u32 h) {
    ImagePixels px = uniform_image(w, h, 0, 0, 0);
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * w + x) * 4];
            const bool right = x >= w / 2, bottom = y >= h / 2;
            p[0] = (!right && !bottom) || (right && bottom) ? 255 : 0;
            p[1] = right ? 255 : 0;
            p[2] = bottom ? 255 : 0;
        }
    }
    return px;
}

/// Imagem de referência dos golden frames: degradês, disco e linhas finas
/// (bordas afiadas denunciam reamostragem errada).
ImagePixels reference_image(u32 w, u32 h) {
    ImagePixels px = uniform_image(w, h, 0, 0, 0);
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * w + x) * 4];
            p[0] = static_cast<u8>(x * 255 / (w - 1));
            p[1] = static_cast<u8>(y * 255 / (h - 1));
            p[2] = 64;
            const f32 dx = static_cast<f32>(x) - w * 0.5f, dy = static_cast<f32>(y) - h * 0.5f;
            if (dx * dx + dy * dy < (h * 0.25f) * (h * 0.25f)) { p[0] = p[1] = p[2] = 255; }
            if (x % 16 == 0 || y % 16 == 0) { p[0] = p[1] = p[2] = 20; }
        }
    }
    return px;
}

/// Golden: compara com tests/golden/<nome>.png (tolerância de reamostragem
/// entre GPUs), ou regera com AUREA_UPDATE_GOLDENS=1.
void check_golden(const char* name, const FloatImage& img) {
    const Image8 actual = img.encoded();
    const std::string path = std::string(AUREA_GOLDEN_DIR) + "/" + name + ".png";
    const char* update = std::getenv("AUREA_UPDATE_GOLDENS");
    if (update && *update == '1') {
        AUREA_CHECK(write_png(path, actual));
        return;
    }
    Image8 expected;
    if (!read_png(path, expected)) {
        AUREA_CHECK_MSG(false, "golden ausente (rode com AUREA_UPDATE_GOLDENS=1)");
        return;
    }
    AUREA_CHECK_EQ(expected.width, actual.width);
    AUREA_CHECK_EQ(expected.height, actual.height);
    if (expected.width != actual.width || expected.height != actual.height) return;
    u32 maxDiff = 0;
    f64 sum = 0;
    for (usize i = 0; i < actual.rgba.size(); ++i) {
        const u32 d = static_cast<u32>(std::abs(static_cast<int>(actual.rgba[i]) - static_cast<int>(expected.rgba[i])));
        maxDiff = std::max(maxDiff, d);
        sum += d;
    }
    const f64 mean = sum / static_cast<f64>(actual.rgba.size());
    if (maxDiff > 3 || mean > 0.35) {
        (void)write_png(std::string(name) + "_obtido.png", actual);
        std::printf("\n    golden '%s': diferenca maxima %u, media %.3f (salvo %s_obtido.png)\n",
                    name, maxDiff, mean, name);
    }
    AUREA_CHECK(maxDiff <= 3);
    AUREA_CHECK(mean <= 0.35);
}

} // namespace

// =============================================================================
// Backend
// =============================================================================
AUREA_TEST(Gpu, BackendReportsRealCapabilities) {
    AUREA_REQUIRE_GPU();
    const GPUCapabilities& c = gpu().backend.capabilities();
    AUREA_CHECK(c.apiMajor == 1 && c.apiMinor >= 1);
    AUREA_CHECK(!c.deviceName.empty());
    AUREA_CHECK(c.maxTexture2D >= 4096);
    AUREA_CHECK(c.rgba16fRenderable && c.rgba16fFilterable);
    AUREA_CHECK(!c.heaps.empty());
    AUREA_CHECK(gpu().renderer.pipelines_prewarmed() >= 15);
    std::printf("(%s) ", c.summary().c_str());
}

AUREA_TEST(Gpu, SolidLayerLandsWhereTheTransformSays) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    s.solid(64, 32, Vec4{1, 0.5f, 0, 1}, 128, 72);
    const FloatImage img = s.render();
    AUREA_CHECK(near4(img.v(128, 72), Vec4{1, srgb_decode(0.5f), 0, 1}, 0.004f));
    AUREA_CHECK(near4(img.v(100, 60), Vec4{1, srgb_decode(0.5f), 0, 1}, 0.004f));
    AUREA_CHECK(near4(img.v(90, 72), Vec4{0, 0, 0, 1}, 0.001f));    // fora, à esquerda
    AUREA_CHECK(near4(img.v(128, 50), Vec4{0, 0, 0, 1}, 0.001f));   // fora, acima
}

AUREA_TEST(Gpu, ImageKeepsOrientationAndColor) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 72);
    s.image(quadrants(128, 72), 64, 36);
    const FloatImage img = s.render();
    AUREA_CHECK(near4(img.v(20, 15), Vec4{1, 0, 0, 1}, 0.004f));
    AUREA_CHECK(near4(img.v(100, 15), Vec4{0, 1, 0, 1}, 0.004f));
    AUREA_CHECK(near4(img.v(20, 60), Vec4{0, 0, 1, 1}, 0.004f));
    AUREA_CHECK(near4(img.v(100, 60), Vec4{1, 1, 1, 1}, 0.004f));
}

AUREA_TEST(Gpu, RotationTransformMovesTheQuadrants) {
    AUREA_REQUIRE_GPU();
    Scene s(144, 144);
    const LayerId id = s.image(quadrants(128, 128), 72, 72);
    s.comp->layer(id)->transform.rotation = Vec3{0, 0, 90};
    const FloatImage img = s.render();
    // 90° horário (y para baixo): o vermelho (sup. esq.) vai para sup. dir.
    AUREA_CHECK(near4(img.v(110, 30), Vec4{1, 0, 0, 1}, 0.01f));
    AUREA_CHECK(near4(img.v(30, 30), Vec4{0, 0, 1, 1}, 0.01f));
}

AUREA_TEST(Gpu, TwoLayersCompositeInCoreOrderWithOpacity) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 72);
    s.solid(128, 72, Vec4{1, 0, 0, 1}, 64, 36);                    // fundo
    const LayerId front = s.solid(64, 36, Vec4{1, 1, 1, 1}, 64, 36);   // frente
    s.comp->layer(front)->transform.opacity = 0.5f;
    FloatImage img = s.render();
    AUREA_CHECK(near4(img.v(64, 36), Vec4{1, 0.5f, 0.5f, 1}, 0.003f));
    AUREA_CHECK(near4(img.v(5, 5), Vec4{1, 0, 0, 1}, 0.002f));
    // Trocar a ordem no Core troca a composição: o vermelho opaco cobre tudo.
    AUREA_CHECK(s.comp->reorder_layer(front, 0));
    img = s.render();
    AUREA_CHECK(near4(img.v(64, 36), Vec4{1, 0, 0, 1}, 0.002f));
}

AUREA_TEST(Gpu, PreviewScaleKeepsLogicalCoordinates) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    s.image(quadrants(128, 72), 128, 72);
    const FloatImage full = s.render(FrameIndex{0}, 1);
    const FloatImage half = s.render(FrameIndex{0}, 2);
    const FloatImage quarter = s.render(FrameIndex{0}, 4);
    AUREA_CHECK_EQ(half.width, static_cast<u32>(128));
    AUREA_CHECK_EQ(quarter.width, static_cast<u32>(64));
    AUREA_CHECK(near4(full.mean(), half.mean(), 0.01f));
    AUREA_CHECK(near4(full.mean(), quarter.mean(), 0.02f));
    // O mesmo ponto lógico tem a mesma cor em qualquer escala.
    AUREA_CHECK(near4(full.v(100, 55), half.v(50, 27), 0.01f));
}

// =============================================================================
// Vídeo: NV12 → espaço de trabalho, com metadados de cor
// =============================================================================
AUREA_TEST(Gpu, Nv12Bt709LimitedDecodesToExactColors) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 36);
    SyntheticConfig cfg;
    cfg.color.matrix = YCbCrMatrix::BT709;
    cfg.color.fullRange = false;
    s.video(cfg, 32, 18);
    const FloatImage img = s.render();
    AUREA_CHECK(near4(img.v(8, 6), Vec4{1, 0, 0, 1}, 0.02f));
    AUREA_CHECK(near4(img.v(56, 6), Vec4{0, 1, 0, 1}, 0.02f));
    AUREA_CHECK(near4(img.v(8, 30), Vec4{0, 0, 1, 1}, 0.02f));
    AUREA_CHECK(near4(img.v(56, 30), Vec4{1, 1, 1, 1}, 0.02f));
}

AUREA_TEST(Gpu, Nv12Bt601FullRangeUsesItsOwnMetadata) {
    // Nada de assumir BT.709 limitado: o mesmo vermelho codificado em BT.601
    // faixa completa tem de sair vermelho também.
    AUREA_REQUIRE_GPU();
    Scene s(64, 36);
    SyntheticConfig cfg;
    cfg.color.matrix = YCbCrMatrix::BT601;
    cfg.color.primaries = ColorPrimaries::BT601;
    cfg.color.fullRange = true;
    s.video(cfg, 32, 18);
    const FloatImage img = s.render();
    AUREA_CHECK(near4(img.v(8, 6), Vec4{1, 0, 0, 1}, 0.02f));
    AUREA_CHECK(near4(img.v(56, 6), Vec4{0, 1, 0, 1}, 0.02f));
    AUREA_CHECK(near4(img.v(8, 30), Vec4{0, 0, 1, 1}, 0.02f));
}

AUREA_TEST(Gpu, SdrVideoRoundTripsItsCodes) {
    // Um vídeo sem efeito tem de sair com os MESMOS códigos que entrou.
    AUREA_REQUIRE_GPU();
    Scene s(64, 36);
    SyntheticConfig cfg;
    cfg.pattern = SyntheticPattern::FrameGray;
    s.video(cfg, 32, 18);
    for (u32 frame : {0u, 17u, 123u}) {
        const FloatImage img = s.render(FrameIndex{frame});
        const f32 code = (static_cast<f32>(frame_gray_code(frame)) - 16.0f) / 219.0f;
        const f32 encoded = srgb_encode(img.v(32, 18).x);
        AUREA_CHECK_NEAR(encoded, code, 0.002);
    }
}

AUREA_TEST(Gpu, VideoShowsTheFrameOfTheRequestedTime) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 36);
    SyntheticConfig cfg;
    cfg.pattern = SyntheticPattern::FrameGray;
    const LayerId id = s.video(cfg, 32, 18);
    s.comp->layer(id)->offset = FrameIndex{10};   // conteúdo deslocado na layer
    const FloatImage img = s.render(FrameIndex{40});
    const f32 code = (static_cast<f32>(frame_gray_code(50)) - 16.0f) / 219.0f;
    AUREA_CHECK_NEAR(srgb_encode(img.v(20, 20).y), code, 0.002);
}

// =============================================================================
// Efeitos — verificação analítica
// =============================================================================
AUREA_TEST(Gpu, ExposureDoublesTheLightPerStop) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 64);
    const LayerId id = s.image(uniform_image(64, 64, 128, 64, 32), 32, 32);
    s.add_effect(id, effect_keys::kExposure).params[0].constant.v[0] = 1.0f;
    const FloatImage img = s.render();
    const Vec4 in{srgb_decode(128 / 255.0f), srgb_decode(64 / 255.0f), srgb_decode(32 / 255.0f), 1};
    AUREA_CHECK(near4(img.v(32, 32), Vec4{in.x * 2, in.y * 2, in.z * 2, 1}, 0.003f));
}

AUREA_TEST(Gpu, SaturationMinus100IsLuminance) {
    AUREA_REQUIRE_GPU();
    Scene s(32, 32);
    const LayerId id = s.image(uniform_image(32, 32, 200, 40, 90), 16, 16);
    s.add_effect(id, effect_keys::kSaturation).params[0].constant.v[0] = -100.0f;
    const FloatImage img = s.render();
    const f32 r = srgb_decode(200 / 255.0f), g = srgb_decode(40 / 255.0f), b = srgb_decode(90 / 255.0f);
    const f32 l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    AUREA_CHECK(near4(img.v(16, 16), Vec4{l, l, l, 1}, 0.003f));
}

AUREA_TEST(Gpu, FusedColorStackMatchesTheSequence) {
    // Exposição + saturação + matriz no MESMO passe = aplicar uma depois da
    // outra. A fusão não pode mudar o resultado.
    AUREA_REQUIRE_GPU();
    Scene s(32, 32);
    const LayerId id = s.image(uniform_image(32, 32, 180, 120, 60), 16, 16);
    s.add_effect(id, effect_keys::kExposure).params[0].constant.v[0] = 0.5f;
    s.add_effect(id, effect_keys::kSaturation).params[0].constant.v[0] = 50.0f;
    EffectInstance& m = s.add_effect(id, effect_keys::kColorMatrix);
    // Troca R e B.
    for (u32 i = 0; i < 12; ++i) m.params[i].constant.v[0] = 0.0f;
    m.params[2].constant.v[0] = 1.0f;   // R ← B
    m.params[5].constant.v[0] = 1.0f;   // G ← G
    m.params[8].constant.v[0] = 1.0f;   // B ← R
    const FloatImage img = s.render();
    f32 c[3] = {srgb_decode(180 / 255.0f), srgb_decode(120 / 255.0f), srgb_decode(60 / 255.0f)};
    for (f32& v : c) v *= std::pow(2.0f, 0.5f);
    const f32 l = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
    // Luz negativa não existe: cada operação limita em 0 (o shader faz isso
    // por etapa, e a saturação alta empurra o azul abaixo de zero aqui).
    for (f32& v : c) v = std::fmax(0.0f, l + (v - l) * 1.5f);
    AUREA_CHECK(near4(img.v(16, 16), Vec4{c[2], c[1], c[0], 1}, 0.004f));
    AUREA_CHECK_EQ(gpu().renderer.graph_stats().passesExecuted, static_cast<u32>(3));   // imagem, cor, composição
}

AUREA_TEST(Gpu, LevelsAndBrightnessWorkOnEncodedValues) {
    AUREA_REQUIRE_GPU();
    Scene s(32, 32);
    const LayerId id = s.image(uniform_image(32, 32, 128, 128, 128), 16, 16);
    EffectInstance& lv = s.add_effect(id, effect_keys::kLevels);
    lv.params[0].constant.v[0] = 64.0f;    // preto de entrada
    lv.params[1].constant.v[0] = 192.0f;   // branco de entrada
    const FloatImage img = s.render();
    // (128-64)/(192-64) = 0.5 codificado.
    AUREA_CHECK_NEAR(srgb_encode(img.v(16, 16).x), 0.5f, 0.004);
}

AUREA_TEST(Gpu, CurvesInvertEncodedValues) {
    AUREA_REQUIRE_GPU();
    Scene s(32, 32);
    const LayerId id = s.image(uniform_image(32, 32, 51, 51, 51), 16, 16);
    EffectInstance& cv = s.add_effect(id, effect_keys::kCurves);
    cv.curves[0].channel[0] = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    const FloatImage img = s.render();
    AUREA_CHECK_NEAR(srgb_encode(img.v(16, 16).y), 1.0f - 51.0f / 255.0f, 0.006);
}

AUREA_TEST(Gpu, TintMapsLuminanceBetweenTwoColors) {
    AUREA_REQUIRE_GPU();
    Scene s(32, 32);
    const LayerId id = s.image(uniform_image(32, 32, 255, 255, 255), 16, 16);
    EffectInstance& t = s.add_effect(id, effect_keys::kTint);
    t.params[1].constant = ParamValue::color(0.2f, 0.4f, 0.8f, 1.0f);   // branco → azulado
    const FloatImage img = s.render();
    AUREA_CHECK(near4(img.v(16, 16), Vec4{0.2f, 0.4f, 0.8f, 1}, 0.004f));
}

AUREA_TEST(Gpu, GaussianBlurConservesEnergyAndIsSymmetric) {
    AUREA_REQUIRE_GPU();
    for (f32 radius : {6.0f, 60.0f}) {   // 60 passa pela pirâmide de redução
        Scene s(256, 256);
        ImagePixels px = uniform_image(128, 128, 0, 0, 0);
        for (u32 y = 48; y < 80; ++y) for (u32 x = 48; x < 80; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * 128 + x) * 4];
            p[0] = p[1] = p[2] = 255;
        }
        const LayerId id = s.image(px, 128, 128);
        const FloatImage before = s.render();
        s.add_effect(id, effect_keys::kGaussianBlur).params[0].constant.v[0] = radius;
        const FloatImage after = s.render();
        AUREA_CHECK_NEAR(after.mean().x, before.mean().x, before.mean().x * 0.02);
        // Simetria em torno do centro do quadrado (x = 128).
        for (u32 d : {5u, 12u, 20u}) {
            AUREA_CHECK_NEAR(after.v(128 - d, 128).x, after.v(128 + d - 1, 128).x, 0.01);
        }
        if (radius > 16.0f) AUREA_CHECK(after.v(128, 128).x < 1.0f);   // raio maior que o quadrado: o centro espalhou
        AUREA_CHECK(after.v(128 + 16 + 3, 128).x > 0.01f);  // e vazou para fora
        for (f32 v : after.px) AUREA_CHECK(std::isfinite(v));
    }
}

AUREA_TEST(Gpu, GlowAddsLightOnlyAboveThreshold) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    ImagePixels px = uniform_image(128, 128, 0, 0, 0);
    for (u32 y = 56; y < 72; ++y) for (u32 x = 56; x < 72; ++x) {
        u8* p = &px.rgba[(static_cast<usize>(y) * 128 + x) * 4];
        p[0] = p[1] = p[2] = 255;
    }
    const LayerId bright = s.image(px, 64, 64);
    s.add_effect(bright, effect_keys::kGlow);
    const FloatImage img = s.render();
    AUREA_CHECK(img.v(64 + 8 + 4, 64).x > 0.02f);    // halo fora do quadrado
    AUREA_CHECK(img.v(64, 64).x >= 1.0f - 1e-3f);    // o brilho só soma

    Scene d(64, 64);
    const LayerId dark = d.image(uniform_image(64, 64, 60, 60, 60), 32, 32);
    d.add_effect(dark, effect_keys::kGlow);
    const FloatImage dimg = d.render();
    AUREA_CHECK_NEAR(dimg.v(32, 32).x, srgb_decode(60 / 255.0f), 0.002);
}

AUREA_TEST(Gpu, SharpenOvershootsAtEdges) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 64);
    ImagePixels px = uniform_image(64, 64, 64, 64, 64);
    for (u32 y = 0; y < 64; ++y) for (u32 x = 32; x < 64; ++x) {
        u8* p = &px.rgba[(static_cast<usize>(y) * 64 + x) * 4];
        p[0] = p[1] = p[2] = 192;
    }
    const LayerId id = s.image(px, 32, 32);
    s.add_effect(id, effect_keys::kSharpen).params[0].constant.v[0] = 200.0f;
    const FloatImage img = s.render();
    AUREA_CHECK(img.v(32, 32).x > srgb_decode(192 / 255.0f) + 0.02f);
    AUREA_CHECK(img.v(31, 32).x < srgb_decode(64 / 255.0f) - 0.005f);
    AUREA_CHECK_NEAR(img.v(50, 32).x, srgb_decode(192 / 255.0f), 0.003);   // longe da borda, intacto
}

// =============================================================================
// Motion Tile — regressão visual dos defeitos antigos
// =============================================================================
AUREA_TEST(Gpu, MotionTileCoversTheFrameWhenTheLayerShrinks) {
    // "Não ladrilha no preview": com a layer em 50%, o quadro inteiro tem de
    // ficar coberto — nenhum pixel de fundo.
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(quadrants(128, 72), 128, 72, 0.5f);
    s.add_effect(id, effect_keys::kMotionTile);
    const FloatImage img = s.render();
    u32 background = 0;
    for (u32 y = 0; y < img.height; y += 3) {
        for (u32 x = 0; x < img.width; x += 3) {
            const Vec4 v = img.v(x, y);
            if (v.x + v.y + v.z < 0.01f) ++background;
        }
    }
    AUREA_CHECK_EQ(background, static_cast<u32>(0));
}

AUREA_TEST(Gpu, MotionTileCentralCopyIsTheLayerUntouched) {
    // "Dá zoom para baixo" / "muda a posição da layer": a cópia central sai
    // IDÊNTICA à layer sem efeito — mesmo lugar, mesmo tamanho.
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(128, 72), 128, 72, 0.5f);
    const FloatImage plain = s.render();
    s.add_effect(id, effect_keys::kMotionTile);
    const FloatImage tiled = s.render();
    f32 worst = 0.0f;
    for (u32 y = 38; y < 106; ++y) {           // a caixa da layer: 64x36 em (96,54)
        for (u32 x = 97; x < 159; ++x) {
            if (y < 55 || y > 88) continue;
            for (int c = 0; c < 3; ++c) worst = std::fmax(worst, std::fabs(plain.at(x, y)[c] - tiled.at(x, y)[c]));
        }
    }
    AUREA_CHECK(worst < 0.01f);
}

AUREA_TEST(Gpu, MotionTileRepeatsAtTheTilePeriod) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(256, 144), 128, 72);
    EffectInstance& mt = s.add_effect(id, effect_keys::kMotionTile);
    mt.params[motion_tile::kTileWidth].constant.v[0] = 50.0f;
    mt.params[motion_tile::kTileHeight].constant.v[0] = 50.0f;
    const FloatImage img = s.render();
    f32 worst = 0.0f;
    for (u32 y = 10; y < 60; y += 3) {
        for (u32 x = 10; x < 118; x += 3) {
            for (int c = 0; c < 3; ++c) worst = std::fmax(worst, std::fabs(img.at(x, y)[c] - img.at(x + 128, y)[c]));
        }
    }
    AUREA_CHECK(worst < 0.01f);
}

AUREA_TEST(Gpu, MotionTileMirrorFlipsTheNeighborTile) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(256, 144), 128, 72);
    EffectInstance& mt = s.add_effect(id, effect_keys::kMotionTile);
    mt.params[motion_tile::kTileWidth].constant.v[0] = 50.0f;
    mt.params[motion_tile::kTileHeight].constant.v[0] = 50.0f;
    mt.params[motion_tile::kMirror].constant.v[0] = 1.0f;
    const FloatImage img = s.render();
    // A grade de ladrilhos começa em -64 (centro 0.5, tile 0.5): as emendas
    // ficam em x = 64 e 192. Espelhada, a coluna 64-k repete em 64+k-1.
    f32 worst = 0.0f;
    for (u32 k = 1; k < 30; ++k) {
        for (int c = 0; c < 3; ++c) worst = std::fmax(worst, std::fabs(img.at(64 - k, 40)[c] - img.at(64 + k - 1, 40)[c]));
    }
    AUREA_CHECK(worst < 0.02f);
}

// =============================================================================
// Golden frames
// =============================================================================
AUREA_TEST(Gpu, GoldenTransform) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(128, 72), 140, 70, 1.2f);
    s.comp->layer(id)->transform.rotation = Vec3{0, 0, 20};
    check_golden("transform", s.render());
}

AUREA_TEST(Gpu, GoldenGaussianBlur) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(128, 72), 128, 72, 1.5f);
    s.add_effect(id, effect_keys::kGaussianBlur).params[0].constant.v[0] = 9.0f;
    check_golden("gaussian_blur", s.render());
}

AUREA_TEST(Gpu, GoldenGlow) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(128, 72), 128, 72, 1.5f);
    EffectInstance& g = s.add_effect(id, effect_keys::kGlow);
    g.params[1].constant.v[0] = 20.0f;
    g.params[2].constant.v[0] = 1.5f;
    check_golden("glow", s.render());
}

AUREA_TEST(Gpu, GoldenExposure) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(128, 72), 128, 72, 1.5f);
    s.add_effect(id, effect_keys::kExposure).params[0].constant.v[0] = -1.0f;
    check_golden("exposure", s.render());
}

AUREA_TEST(Gpu, GoldenMotionTile) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(128, 72), 110, 80, 0.6f);
    s.comp->layer(id)->transform.rotation = Vec3{0, 0, 15};
    EffectInstance& mt = s.add_effect(id, effect_keys::kMotionTile);
    mt.params[motion_tile::kTileWidth].constant.v[0] = 70.0f;
    mt.params[motion_tile::kMirror].constant.v[0] = 1.0f;
    mt.params[motion_tile::kPhase].constant.v[0] = 90.0f;
    check_golden("motion_tile", s.render());
}

// =============================================================================
// Performance e ciclo de vida no backend real
// =============================================================================
AUREA_TEST(Gpu, SteadyPlaybackCreatesNoTexturesAndMeasuresGpu) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    SyntheticConfig cfg;
    cfg.width = 128;
    cfg.height = 72;
    const LayerId v = s.video(cfg, 128, 72, 2.0f);
    s.add_effect(v, effect_keys::kGaussianBlur).params[0].constant.v[0] = 8.0f;
    const LayerId top = s.image(reference_image(64, 36), 180, 40);
    s.add_effect(top, effect_keys::kGlow);
    for (u32 f = 0; f < 4; ++f) (void)s.render(FrameIndex{f});
    const u32 alive = gpu().backend.memory_stats().textureCount;
    for (u32 f = 4; f < 24; ++f) {
        (void)s.render(FrameIndex{f});
        AUREA_CHECK_EQ(gpu().renderer.pool_stats().createdThisFrame, static_cast<u32>(0));
    }
    // Texturas vivas estáveis (os planos de vídeo e o pool não crescem).
    AUREA_CHECK(gpu().backend.memory_stats().textureCount <= alive + 1);
    GpuTiming t[64];
    f32 total = 0;
    const u32 n = gpu().backend.read_gpu_timings(t, 64, &total);
    if (gpu().backend.capabilities().timestampQueries) {
        AUREA_CHECK(n > 0);
        AUREA_CHECK(total > 0.0f);
    }
}

AUREA_TEST(Gpu, EngineImportsSeeksAndScrubsARealVideoPipeline) {
    // O fluxo inteiro, sem Android: Engine + MediaManager (vídeo sintético) +
    // playback + FrameGraph + Vulkan, lendo o pixel para saber QUAL frame saiu.
    AUREA_REQUIRE_GPU();
    SyntheticConfig cfg;
    cfg.width = 96;
    cfg.height = 54;
    cfg.frameCount = 300;
    cfg.pattern = SyntheticPattern::FrameGray;
    SyntheticFactory factory(cfg);

    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.framesInFlight = 2;
    ec.mediaFactory = &factory;
    ec.workerCount = 2;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.gpu() != nullptr);
    AUREA_CHECK(e.new_project(1920, 1080, 60.0, "e2e").ok());

    VideoImport imp;
    imp.sourcePath = "sintetico";
    imp.displayName = "clipe";
    auto layer = e.import_video(imp);
    AUREA_CHECK(layer.ok());
    // Primeiro clipe: a composição adota o vídeo.
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(c->width(), static_cast<u32>(96));
    AUREA_CHECK_NEAR(c->fps(), 30.0, 1e-9);
    AUREA_CHECK_EQ(c->duration().value, static_cast<i64>(300));

    TextureDesc d;
    d.width = 96;
    d.height = 54;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    const TextureHandle target = *e.gpu()->create_texture(d);
    std::vector<u16> half(96 * 54 * 4);
    auto shown_code = [&]() {
        AUREA_CHECK(e.render_offscreen(target, 96, 54).ok());
        AUREA_CHECK(e.gpu()->read_texture(target, half.data(), 96 * 8).ok());
        const f32 lin = half_to_float(half[(27 * 96 + 48) * 4]);
        return srgb_encode(lin) * 219.0f + 16.0f;
    };

    AUREA_CHECK_NEAR(shown_code(), frame_gray_code(0), 0.5);

    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{150}, 30.0);
    AUREA_CHECK(e.submit_commands(&seek, 1) == 1);
    AUREA_CHECK_NEAR(shown_code(), frame_gray_code(150), 0.5);

    // Scrub: 12 posições de uma vez (o dedo foi mais rápido que o decode).
    Command batch[14];
    batch[0].type = CommandType::PlaybackScrubBegin;
    for (u32 i = 0; i < 12; ++i) {
        batch[1 + i].type = CommandType::PlaybackScrub;
        batch[1 + i].seek.time = tick_at(FrameIndex{160 + i * 3}, 30.0);
    }
    batch[13].type = CommandType::PlaybackScrubEnd;
    AUREA_CHECK_EQ(e.submit_commands(batch, 14), static_cast<u32>(14));
    AUREA_CHECK_NEAR(shown_code(), frame_gray_code(160 + 11 * 3), 0.5);
    // Os alvos intermediários não custaram um seek cada.
    AUREA_CHECK(factory.last && factory.last->seeks.load() <= 3);

    // Efeito aplicado pela fronteira de comandos (como a UI faz).
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(*layer);
    add.effect_add.effectType = effect_type_id(effect_keys::kExposure);
    add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(e.submit_commands(&add, 1) == 1);
    Command param;
    param.type = CommandType::EffectSetParam;
    param.effect_param.layer = LayerId::unpack(*layer);
    param.effect_param.effect = EffectId{0, 0};
    param.effect_param.paramIndex = 0;
    param.effect_param.value = 1.0f;
    AUREA_CHECK(e.submit_commands(&param, 1) == 1);
    const f32 base = (static_cast<f32>(frame_gray_code(193)) - 16.0f) / 219.0f;
    const f32 expected = srgb_encode(srgb_decode(base) * 2.0f) * 219.0f + 16.0f;
    AUREA_CHECK_NEAR(shown_code(), expected, 0.8);

    bridge::PerfPOD perf;
    e.fill_perf(perf);
    e.gpu()->destroy_texture(target);
    e.shutdown();
}

#endif // AUREA_TEST_VULKAN

AUREA_TEST(Gpu, CaptureFrameGivesSrgbThumbnail) {
    Gpu& g = gpu();
    if (!g.ok) return;
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    SyntheticFactory factory(cfg);
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.mediaFactory = &factory;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(64, 36, 30.0, nullptr).ok());
    VideoImport vi;
    vi.sourcePath = "sintetico";
    vi.displayName = "quadrantes";
    AUREA_CHECK(e.import_video(vi).ok());
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    AUREA_CHECK(e.capture_frame_rgba(32, rgba, w, h).ok());
    AUREA_CHECK_EQ(w, 32u);
    AUREA_CHECK_EQ(h, 18u);
    auto px = [&](u32 x, u32 y, u32 c) { return static_cast<int>(rgba[(static_cast<usize>(y) * w + x) * 4 + c]); };
    AUREA_CHECK(px(4, 4, 0) > 235 && px(4, 4, 1) < 20 && px(4, 4, 2) < 20);     // vermelho
    AUREA_CHECK(px(28, 14, 0) > 235 && px(28, 14, 1) > 235 && px(28, 14, 2) > 235);   // branco
    AUREA_CHECK_EQ(px(16, 9, 3), 255);
    e.shutdown();
}

// -----------------------------------------------------------------------------
// Motion Tile na GPU — cenários dos testes do Aurea antigo que faltavam.
// -----------------------------------------------------------------------------
namespace {
/// Maior diferença na caixa de uma layer 64×36 centrada em (128,72).
f32 central_copy_error(const FloatImage& a, const FloatImage& b) {
    f32 worst = 0.0f;
    for (u32 y = 56; y < 88; ++y) {
        for (u32 x = 98; x < 158; ++x) {
            for (int c = 0; c < 3; ++c) worst = std::fmax(worst, std::fabs(a.at(x, y)[c] - b.at(x, y)[c]));
        }
    }
    return worst;
}
}

AUREA_TEST(Gpu, MotionTileOutputSizeNeverShrinksTheCentralCopy) {
    // "Largura/altura da saída em 200% não encolhem a cópia".
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(128, 72), 128, 72, 0.5f);
    const FloatImage plain = s.render();
    EffectInstance& mt = s.add_effect(id, effect_keys::kMotionTile);
    mt.params[motion_tile::kOutputWidth].constant.v[0] = 200.0f;
    mt.params[motion_tile::kOutputHeight].constant.v[0] = 200.0f;
    AUREA_CHECK(central_copy_error(plain, s.render()) < 0.01f);
}

AUREA_TEST(Gpu, MotionTileCenterSlidesTheGridWithoutResizing) {
    // "Centro X desliza a grade, sem mexer no tamanho": com o centro deslocado
    // meio ladrilho, a imagem é a mesma deslocada — o período não muda.
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(256, 144), 128, 72);
    EffectInstance& mt = s.add_effect(id, effect_keys::kMotionTile);
    mt.params[motion_tile::kTileWidth].constant.v[0] = 50.0f;
    mt.params[motion_tile::kTileHeight].constant.v[0] = 50.0f;
    const FloatImage base = s.render();
    mt.params[motion_tile::kCenter].constant.v[0] = 0.75f;   // +1/4 da layer = meio ladrilho
    const FloatImage slid = s.render();
    f32 worstShift = 0.0f, worstPeriod = 0.0f;
    for (u32 y = 10; y < 60; y += 3) {
        for (u32 x = 10; x < 118; x += 3) {
            for (int c = 0; c < 3; ++c) {
                worstShift = std::fmax(worstShift, std::fabs(slid.at(x + 64, y)[c] - base.at(x, y)[c]));
                worstPeriod = std::fmax(worstPeriod, std::fabs(slid.at(x, y)[c] - slid.at(x + 128, y)[c]));
            }
        }
    }
    AUREA_CHECK(worstShift < 0.02f);
    AUREA_CHECK(worstPeriod < 0.01f);
}

AUREA_TEST(Gpu, MotionTileAt200PercentDrawsADoubleTile) {
    // "Mosaico em 200% desenha um ladrilho do DOBRO": a imagem da layer ocupa o
    // dobro — o centro dela fica no mesmo lugar e a borda vai para o dobro.
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    const LayerId id = s.image(reference_image(64, 36), 128, 72);
    EffectInstance& mt = s.add_effect(id, effect_keys::kMotionTile);
    mt.params[motion_tile::kTileWidth].constant.v[0] = 200.0f;
    mt.params[motion_tile::kTileHeight].constant.v[0] = 200.0f;
    mt.params[motion_tile::kOutputWidth].constant.v[0] = 400.0f;
    mt.params[motion_tile::kOutputHeight].constant.v[0] = 400.0f;
    const FloatImage img = s.render();
    // Na referência, o vermelho vai de 0 a 1 na largura da imagem. Num ladrilho
    // de 128 px (o dobro de 64) centrado em x = 128, o pixel x = 164 é a fonte
    // em u ≈ 0,78 (vermelho sRGB ≈ 199 → linear ≈ 0,57). Com o ladrilho de
    // 64 px ele cairia no começo do vizinho (u ≈ 0,06 → quase preto). O x = 160
    // foi evitado de propósito: é uma linha escura da grade da referência.
    const f32 r = img.at(164, 60)[0];
    AUREA_CHECK(r > 0.40f && r < 0.75f);
}

AUREA_TEST(Gpu, MotionTileLeavesNoHoleInAnyCombination) {
    // "Nada de borda nem de buraco em nenhuma combinação": a layer encolhida
    // e girada com qualquer ladrilho/espelho cobre o quadro inteiro.
    AUREA_REQUIRE_GPU();
    const f32 tiles[] = {10.0f, 37.0f, 100.0f};
    const f32 scales[] = {0.2f, 0.5f};
    const f32 rotations[] = {0.0f, 30.0f, 90.0f};
    u32 holes = 0, cases = 0;
    for (f32 tile : tiles) {
        for (f32 scale : scales) {
            for (f32 rot : rotations) {
                for (int mirror = 0; mirror < 2; ++mirror) {
                    Scene s(160, 90);
                    const LayerId id = s.image(uniform_image(64, 36, 220, 220, 220), 80, 45, scale);
                    s.comp->layer(id)->transform.rotation.z = rot;
                    EffectInstance& mt = s.add_effect(id, effect_keys::kMotionTile);
                    mt.params[motion_tile::kTileWidth].constant.v[0] = tile;
                    mt.params[motion_tile::kTileHeight].constant.v[0] = tile;
                    mt.params[motion_tile::kMirror].constant.v[0] = static_cast<f32>(mirror);
                    const FloatImage img = s.render();
                    ++cases;
                    for (u32 y = 0; y < 90; y += 2) {
                        for (u32 x = 0; x < 160; x += 2) {
                            if (img.at(x, y)[3] < 0.99f) { ++holes; break; }
                        }
                    }
                }
            }
        }
    }
    AUREA_CHECK_EQ(cases, 36u);
    AUREA_CHECK_EQ(holes, 0u);
}
