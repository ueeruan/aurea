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
#include "aurea/render/MaskRaster.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/text/TextAnimator.hpp"

#include <algorithm>
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
    FloatImage render(FrameIndex t = FrameIndex{0}, u32 den = 1, bool finalQuality = false) {
        Gpu& g = gpu();
        RenderSettings rs;
        rs.previewDenominator = den;
        rs.dither = false;
        rs.gpuTimers = true;
        rs.finalQuality = finalQuality;
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


// -----------------------------------------------------------------------------
// 3D no renderer do Aurea (5A): import → layer → frame.
// -----------------------------------------------------------------------------
namespace {

std::string gltf_data(const char* rel) { return std::string(AUREA_TEST_DATA_DIR) + "/gltf/" + rel; }

bool file_exists(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

/// Um triângulo glTF de um lado só, virado para +Z (para quem olha do lado
/// +Z, que é onde o observador do glTF fica) ou para −Z. Buffer em base64
/// dentro do JSON — o teste não depende de arquivo externo.
std::string write_triangle_gltf(bool facingViewer, f32 r, f32 g, f32 b) {
    const f32 pos[9] = {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f};
    const u16 idx[3] = {0, static_cast<u16>(facingViewer ? 1 : 2), static_cast<u16>(facingViewer ? 2 : 1)};
    std::vector<u8> bin(36 + 6 + 2);
    std::memcpy(bin.data(), pos, 36);
    std::memcpy(bin.data() + 36, idx, 6);
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc;
    for (usize i = 0; i < bin.size(); i += 3) {
        const u32 v = (static_cast<u32>(bin[i]) << 16) | (i + 1 < bin.size() ? static_cast<u32>(bin[i + 1]) << 8 : 0u)
                    | (i + 2 < bin.size() ? bin[i + 2] : 0u);
        enc += b64[(v >> 18) & 63];
        enc += b64[(v >> 12) & 63];
        enc += i + 1 < bin.size() ? b64[(v >> 6) & 63] : '=';
        enc += i + 2 < bin.size() ? b64[v & 63] : '=';
    }
    char json[2048];
    std::snprintf(json, sizeof(json),
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)"
        R"("materials":[{"pbrMetallicRoughness":{"baseColorFactor":[%f,%f,%f,1],"metallicFactor":0,"roughnessFactor":1},"extensions":{"KHR_materials_unlit":{}}}],)"
        R"("extensionsUsed":["KHR_materials_unlit"],)"
        R"("buffers":[{"byteLength":%u,"uri":"data:application/octet-stream;base64,%s"}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-0.5,-0.5,0],"max":[0.5,0.5,0]},)"
        R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})",
        r, g, b, static_cast<unsigned>(bin.size()), enc.c_str());
    const std::string path = std::string("aurea_teste_triangulo_") + (facingViewer ? "frente" : "costas") + ".gltf";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(json, 1, std::strlen(json), f);
    std::fclose(f);
    return path;
}

struct Scene3DRig {
    Engine e;
    explicit Scene3DRig(u32 w = 256, u32 h = 144) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(w, h, 30.0, nullptr).ok());
    }
    ~Scene3DRig() { e.shutdown(); }
    Image8 capture(u32 maxDim) {
        Image8 img;
        std::vector<u8> rgba;
        u32 w = 0, h = 0;
        AUREA_CHECK(e.capture_frame_rgba(maxDim, rgba, w, h).ok());
        img.width = w;
        img.height = h;
        img.rgba = std::move(rgba);
        return img;
    }
};

/// Fração de pixels que diferem do fundo (preto da composição).
f32 coverage(const Image8& img) {
    u32 n = 0;
    for (u32 y = 0; y < img.height; ++y) {
        for (u32 x = 0; x < img.width; ++x) {
            const u8* p = img.at(x, y);
            if (p[0] > 8 || p[1] > 8 || p[2] > 8) ++n;
        }
    }
    return static_cast<f32>(n) / static_cast<f32>(std::max(1u, img.width * img.height));
}

} // namespace

AUREA_TEST(Gpu, Scene3DFrontFaceIsVisibleAndBackFaceIsCulled) {
    AUREA_REQUIRE_GPU();
    {
        Scene3DRig rig;
        ModelImport mi;
        mi.path = write_triangle_gltf(true, 1.0f, 0.0f, 0.0f);
        AUREA_CHECK(rig.e.import_model(mi).ok());
        const Image8 img = rig.capture(256);
        // Unlit vermelho puro: o centro da composição é o triângulo.
        const u8* c = img.at(img.width / 2, img.height / 2);
        AUREA_CHECK(c[0] > 240 && c[1] < 10 && c[2] < 10);
        AUREA_CHECK(coverage(img) > 0.05f);
        // Ponta do triângulo PARA CIMA (glTF +Y = cima na tela): a linha de
        // cima do quadro no centro tem menos cobertura que a de baixo.
        u32 top = 0, bottom = 0;
        for (u32 x = 0; x < img.width; ++x) {
            top += img.at(x, img.height * 3 / 10)[0] > 128;
            bottom += img.at(x, img.height * 7 / 10)[0] > 128;
        }
        AUREA_CHECK(top < bottom);
    }
    {
        Scene3DRig rig;
        ModelImport mi;
        mi.path = write_triangle_gltf(false, 0.0f, 1.0f, 0.0f);
        AUREA_CHECK(rig.e.import_model(mi).ok());
        // Face de trás, material de um lado só: nada desenhado.
        AUREA_CHECK_NEAR(coverage(rig.capture(256)), 0.0f, 1e-6f);
    }
}

AUREA_TEST(Gpu, Scene3DModelsAppearFramedWithPbr) {
    AUREA_REQUIRE_GPU();
    const char* models[] = {"Box.glb", "DamagedHelmet.glb", "MetalRoughSpheres.glb", "BoxTextured/BoxTextured.gltf",
                            "AlphaBlendModeTest.glb", "Fox.glb"};
    for (const char* m : models) {
        const std::string path = gltf_data(m);
        if (!file_exists(path)) {
            std::printf("\n    (modelo de amostra ausente: %s)", m);
            continue;
        }
        Scene3DRig rig(512, 288);
        ModelImport mi;
        mi.path = path;
        std::string detail;
        const Result<u64> r = rig.e.import_model(mi, nullptr, &detail);
        AUREA_CHECK_MSG(r.ok(), m);
        if (!r.ok()) continue;
        const Image8 img = rig.capture(512);
        const f32 cov = coverage(img);
        // Enquadrado: aparece (nem sumido, nem estourando o quadro inteiro).
        AUREA_CHECK_MSG(cov > 0.03f && cov < 0.95f, m);
        std::string out = std::string("scene3d_") + m;
        for (char& ch : out) if (ch == '/' || ch == '.') ch = '_';
        (void)write_png(out + ".png", img);
    }
}


AUREA_TEST(Gpu, Scene3DSkinnedModelAnimatesOnTheTimelineClock) {
    AUREA_REQUIRE_GPU();
    const std::string path = gltf_data("Fox.glb");
    if (!file_exists(path)) return;
    Scene3DRig rig(384, 216);
    ModelImport mi;
    mi.path = path;
    AUREA_CHECK(rig.e.import_model(mi).ok());
    auto seek = [&](i64 frame) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = TickNs{frame * 1'000'000'000LL / 30};
        AUREA_CHECK(rig.e.apply_command(c).ok());
    };
    seek(0);
    const Image8 a = rig.capture(384);
    seek(12);
    const Image8 b = rig.capture(384);
    seek(0);
    const Image8 a2 = rig.capture(384);
    AUREA_CHECK(coverage(a) > 0.02f && coverage(b) > 0.02f);
    // A pose mudou entre 0 e 0,4 s (a raposa anda), e voltar ao 0 dá o mesmo quadro.
    u64 diff = 0, back = 0;
    for (usize i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i) {
        diff += static_cast<u64>(std::abs(a.rgba[i] - b.rgba[i]) > 24);
        back = std::max<u64>(back, static_cast<u64>(std::abs(a.rgba[i] - a2.rgba[i])));
    }
    std::printf("    pixels que mudaram entre 0 e 0,4 s: %llu\n", static_cast<unsigned long long>(diff));
    AUREA_CHECK(diff > 500);
    AUREA_CHECK(back <= 2);
    (void)write_png("scene3d_fox_t0.png", a);
    (void)write_png("scene3d_fox_t12.png", b);
}

AUREA_TEST(Gpu, Scene3DMorphTargetsFollowTheAnimation) {
    AUREA_REQUIRE_GPU();
    const std::string path = gltf_data("AnimatedMorphCube.glb");
    if (!file_exists(path)) return;
    Scene3DRig rig(384, 216);
    ModelImport mi;
    mi.path = path;
    AUREA_CHECK(rig.e.import_model(mi).ok());
    auto seek = [&](i64 frame) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = TickNs{frame * 1'000'000'000LL / 30};
        AUREA_CHECK(rig.e.apply_command(c).ok());
    };
    seek(0);
    const Image8 a = rig.capture(384);
    seek(30);
    const Image8 b = rig.capture(384);
    u64 diff = 0;
    for (usize i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i) diff += static_cast<u64>(std::abs(a.rgba[i] - b.rgba[i]) > 24);
    AUREA_CHECK(coverage(a) > 0.02f && coverage(b) > 0.02f);
    AUREA_CHECK(diff > 300);
    (void)write_png("scene3d_morph_t0.png", a);
    (void)write_png("scene3d_morph_t30.png", b);
}

AUREA_TEST(Gpu, Scene3DKeyLightCastsShadows) {
    AUREA_REQUIRE_GPU();
    const std::string path = gltf_data("MetalRoughSpheres.glb");
    if (!file_exists(path)) return;
    Scene3DRig rig(512, 288);
    ModelImport mi;
    mi.path = path;
    auto id = rig.e.import_model(mi);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    const Image8 lit = rig.capture(512);
    // Sem projetar sombra: a mesma cena, só o passe de sombra desligado.
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    comp->layer(LayerId::unpack(*id))->model.castShadows = false;
    rig.e.request_render();
    const Image8 noShadow = rig.capture(512);
    f64 sumLit = 0, sumNo = 0;
    u64 darker = 0;
    for (usize i = 0; i + 3 < lit.rgba.size() && i + 3 < noShadow.rgba.size(); i += 4) {
        const int a = lit.rgba[i] + lit.rgba[i + 1] + lit.rgba[i + 2];
        const int b = noShadow.rgba[i] + noShadow.rgba[i + 1] + noShadow.rgba[i + 2];
        sumLit += a;
        sumNo += b;
        darker += static_cast<u64>(b - a > 30);
    }
    std::printf("    pixels escurecidos pela sombra: %llu (luz media %.1f -> %.1f)\n",
                static_cast<unsigned long long>(darker), sumNo / (lit.rgba.size() / 4), sumLit / (lit.rgba.size() / 4));
    // As esferas de trás recebem a sombra das da frente: há área mais escura,
    // e nada fica mais claro com a sombra ligada.
    AUREA_CHECK(darker > 120);   // medido: 182 (o IBL de estúdio domina; a chave é 1,2)
    AUREA_CHECK(sumLit < sumNo);
    (void)write_png("scene3d_sombra_on.png", lit);
    (void)write_png("scene3d_sombra_off.png", noShadow);
}

/// Modelos reais do usuário (fora do git): AUREA_EXTRA_MODELS=<pasta>. Sem a
/// variável, o teste não faz nada.
AUREA_TEST(Gpu, Scene3DExtraModelsImportAndRender) {
    AUREA_REQUIRE_GPU();
    const char* dir = std::getenv("AUREA_EXTRA_MODELS");
    if (!dir || !*dir) return;
    const char* names[] = {"final astronaut motion.fbx", "FINAL_MODEL_23.fbx", "Minecraft2.fbx", "Torch.obj",
                           "anel_teste.obj", "cc8b74aeda164d6a8f58b0d5e5e40349.fbx.fbx", "Mineways2Skfb.obj"};
    for (const char* n : names) {
        const std::string path = std::string(dir) + "/" + n;
        if (!file_exists(path)) continue;
        Scene3DRig rig(384, 216);
        ModelImport mi;
        mi.path = path;
        std::string detail;
        const u64 t0 = monotonic_ns();
        const Result<u64> r = rig.e.import_model(mi, nullptr, &detail);
        const f64 ms = static_cast<f64>(monotonic_ns() - t0) / 1e6;
        std::printf("    %s: %s (%.0f ms) %s\n", n, r.ok() ? "ok" : "FALHOU", ms, detail.c_str());
        AUREA_CHECK_MSG(r.ok(), n);
        if (!r.ok()) continue;
        const Image8 img = rig.capture(384);
        std::printf("      cobertura %.3f\n", coverage(img));
        AUREA_CHECK_MSG(coverage(img) > 0.005f, n);
        std::string out = std::string("extra_") + n + ".png";
        for (char& ch : out) if (ch == ' ') ch = '_';
        (void)write_png(out, img);
    }
}

AUREA_TEST(Gpu, ImportedImageIsVisibleOnTheVeryFirstFrame) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(128, 72);
    std::vector<u8> px(64 * 64 * 4);
    for (usize i = 0; i < px.size(); i += 4) { px[i] = 255; px[i + 1] = 0; px[i + 2] = 0; px[i + 3] = 255; }
    auto id = rig.e.import_image(px.data(), 64, 64, "vermelho");
    AUREA_CHECK(id.ok());
    // O primeiro quadro depois do import já mostra a imagem (nada de preto
    // até o próximo redesenho).
    const Image8 img = rig.capture(128);
    const u8* c = img.at(img.width / 2, img.height / 2);
    AUREA_CHECK_MSG(c[0] > 200 && c[1] < 40 && c[2] < 40, "imagem nova saiu preta no primeiro quadro");
}

AUREA_TEST(Gpu, ShapesRenderAsCrispVectorSdfs) {
    AUREA_REQUIRE_GPU();
    for (u32 preset = 0; preset < 15; ++preset) {
        Scene3DRig rig(256, 256);
        auto id = rig.e.add_shape(preset);
        AUREA_CHECK(id.ok());
        const Image8 img = rig.capture(256);
        const f32 cov = coverage(img);
        // Aparece e não estoura: toda forma ocupa uma parte do quadro.
        AUREA_CHECK_MSG(cov > 0.02f && cov < 0.5f, "forma sem pixels ou enchendo o quadro");
        char name[48];
        std::snprintf(name, sizeof(name), "forma_%02u.png", preset);
        (void)write_png(name, img);
        if (preset == 0) {
            // Círculo: centro branco, canto da caixa vazio (é um círculo, não um quadrado).
            const u8* c = img.at(128, 128);
            AUREA_CHECK(c[0] > 240 && c[1] > 240 && c[2] > 240);
            const u8* corner = img.at(128 - 36, 128 - 36);
            AUREA_CHECK(corner[0] < 20);
        }
    }
    // Contorno e cor: um anel vermelho de contorno azul.
    Scene3DRig rig(256, 256);
    auto id = rig.e.add_shape(10);   // quadrado
    Command f;
    f.type = CommandType::ShapeSetFill;
    f.text_color.layer = LayerId::unpack(*id);
    f.text_color.r = 1; f.text_color.g = 0; f.text_color.b = 0; f.text_color.a = 1;
    AUREA_CHECK(rig.e.apply_command(f).ok());
    Command st;
    st.type = CommandType::ShapeSetStroke;
    st.text_color.layer = LayerId::unpack(*id);
    st.text_color.r = 0; st.text_color.g = 0; st.text_color.b = 1; st.text_color.a = 1;
    AUREA_CHECK(rig.e.apply_command(st).ok());
    Command w;
    w.type = CommandType::ShapeSetParam;
    w.shape_param.layer = LayerId::unpack(*id);
    w.shape_param.param = 4;
    w.shape_param.value = 8.0f;
    AUREA_CHECK(rig.e.apply_command(w).ok());
    const Image8 img = rig.capture(256);
    const u8* center = img.at(128, 128);
    AUREA_CHECK(center[0] > 240 && center[2] < 20);                   // preenchimento vermelho
    // Borda da caixa (~38 px do centro): azul do contorno.
    const u8* edge = img.at(128 - 36, 128);
    AUREA_CHECK_MSG(edge[2] > 200 && edge[0] < 60, "contorno azul ausente");
    (void)write_png("forma_contorno.png", img);
}

AUREA_TEST(Gpu, TextRendersCrispWithStrokeAndStaysCentered) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(512, 288);
    auto id = rig.e.add_text("Aurea");
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    const Image8 a = rig.capture(512);
    const f32 cov = coverage(a);
    AUREA_CHECK_MSG(cov > 0.004f && cov < 0.3f, "texto sem pixels");
    // Branco puro em algum lugar (miolo das letras).
    u32 white = 0;
    for (usize i = 0; i + 3 < a.rgba.size(); i += 4) white += a.rgba[i] > 245 && a.rgba[i + 1] > 245 && a.rgba[i + 2] > 245;
    AUREA_CHECK(white > 50);
    (void)write_png("texto_simples.png", a);

    // Cor, contorno e duas linhas: a caixa cresce mas o centro fica.
    bridge::LayerDetailPOD d0{};
    AUREA_CHECK(rig.e.query_layer_detail(*id, d0));
    Command col;
    col.type = CommandType::TextSetColor;
    col.text_color.layer = LayerId::unpack(*id);
    col.text_color.r = 1; col.text_color.g = 0.8f; col.text_color.b = 0; col.text_color.a = 1;
    AUREA_CHECK(rig.e.apply_command(col).ok());
    Command sw;
    sw.type = CommandType::TextSetStrokeWidth;
    sw.text_stroke_width.layer = LayerId::unpack(*id);
    sw.text_stroke_width.width = 4.0f;
    AUREA_CHECK(rig.e.apply_command(sw).ok());
    Command sc;
    sc.type = CommandType::TextSetStrokeColor;
    sc.text_color.layer = LayerId::unpack(*id);
    sc.text_color.r = 0; sc.text_color.g = 0; sc.text_color.b = 1; sc.text_color.a = 1;
    AUREA_CHECK(rig.e.apply_command(sc).ok());
    const char* two = "Aurea\nEditor";
    Command ct;
    ct.type = CommandType::TextSetContent;
    ct.layer_ref.layer = LayerId::unpack(*id);
    AUREA_CHECK(rig.e.apply_command(ct, two).ok());
    bridge::LayerDetailPOD d1{};
    AUREA_CHECK(rig.e.query_layer_detail(*id, d1));
    AUREA_CHECK(d1.sourceHeight > d0.sourceHeight * 1.6f);             // duas linhas
    AUREA_CHECK_NEAR(d1.anchor[0], d1.sourceWidth * 0.5f, 1.0f);       // âncora no centro
    AUREA_CHECK_NEAR(d1.anchor[1], d1.sourceHeight * 0.5f, 1.0f);
    const Image8 b = rig.capture(512);
    u32 yellow = 0, blue = 0;
    for (usize i = 0; i + 3 < b.rgba.size(); i += 4) {
        yellow += b.rgba[i] > 230 && b.rgba[i + 1] > 180 && b.rgba[i + 2] < 40;
        blue += b.rgba[i] < 40 && b.rgba[i + 1] < 40 && b.rgba[i + 2] > 200;
    }
    AUREA_CHECK(yellow > 50);
    AUREA_CHECK(blue > 50);
    (void)write_png("texto_contorno.png", b);
}

namespace {
/// Caixa dos pixels acesos (>8) de uma captura.
struct Box8 { u32 x0 = ~0u, y0 = ~0u, x1 = 0, y1 = 0; u32 w() const { return x1 >= x0 ? x1 - x0 + 1 : 0; } u32 h() const { return y1 >= y0 ? y1 - y0 + 1 : 0; } };
Box8 lit_box(const Image8& img) {
    Box8 b;
    for (u32 y = 0; y < img.height; ++y) for (u32 x = 0; x < img.width; ++x) {
        const u8* p = img.at(x, y);
        if (p[0] > 8 || p[1] > 8 || p[2] > 8) { b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y); b.x1 = std::max(b.x1, x); b.y1 = std::max(b.y1, y); }
    }
    return b;
}
} // namespace

AUREA_TEST(Gpu, TwoDLayerRotationXYIsRealPerspective) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(400, 400);
    auto id = rig.e.add_shape(10);   // quadrado
    AUREA_CHECK(id.ok());
    const Image8 flat = rig.capture(400);
    const Box8 b0 = lit_box(flat);
    auto rot = [&](f32 rx, f32 ry, f32 rz) {
        Command c;
        c.type = CommandType::LayerSetRotation;
        c.rotation.layer = LayerId::unpack(*id);
        c.rotation.rx = rx; c.rotation.ry = ry; c.rotation.rz = rz;
        AUREA_CHECK(rig.e.apply_command(c).ok());
    };
    rot(60, 0, 0);
    const Box8 bx = lit_box(rig.capture(400));
    rot(0, 60, 0);
    const Box8 by = lit_box(rig.capture(400));
    rot(0, 0, 0);
    const Image8 back = rig.capture(400);
    std::printf("    quadrado %ux%u; X60 %ux%u; Y60 %ux%u\n", b0.w(), b0.h(), bx.w(), bx.h(), by.w(), by.h());
    // X 60°: a altura cai para ~cos 60 = metade (perspectiva: ±15%); a largura quase igual.
    AUREA_CHECK(bx.h() < b0.h() * 0.65f && bx.h() > b0.h() * 0.35f);
    AUREA_CHECK(bx.w() > b0.w() * 0.8f);
    // Y 60°: a largura cai para ~metade.
    AUREA_CHECK(by.w() < b0.w() * 0.65f && by.w() > b0.w() * 0.35f);
    AUREA_CHECK(by.h() > b0.h() * 0.8f);
    // Voltar a zero: o mesmo quadro do 2D (caminho 2D de novo).
    u32 worst = 0;
    for (usize i = 0; i < flat.rgba.size() && i < back.rgba.size(); ++i) worst = std::max<u32>(worst, static_cast<u32>(std::abs(flat.rgba[i] - back.rgba[i])));
    AUREA_CHECK(worst <= 2);
}

AUREA_TEST(Gpu, ChildOfMovedNullRendersWhole) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(640, 360);
    auto tid = rig.e.add_text("Texto Aurea");
    AUREA_CHECK(tid.ok());
    const Box8 b0 = lit_box(rig.capture(640));
    auto nid = rig.e.add_null(false);
    AUREA_CHECK(nid.ok());
    Command pc;
    pc.type = CommandType::LayerSetParent;
    pc.layer_parent.layer = LayerId::unpack(*tid);
    pc.layer_parent.parent = LayerId::unpack(*nid);
    AUREA_CHECK(rig.e.apply_command(pc).ok());
    const Box8 b1 = lit_box(rig.capture(640));
    Command mv;
    mv.type = CommandType::LayerSetPosition;
    mv.position.layer = LayerId::unpack(*nid);
    mv.position.x = 320; mv.position.y = 80; mv.position.z = 0;
    AUREA_CHECK(rig.e.apply_command(mv).ok());
    const Box8 b2 = lit_box(rig.capture(640));
    std::printf("    texto %ux%u @%u,%u; com pai %ux%u @%u,%u; pai movido %ux%u @%u,%u\n",
                b0.w(), b0.h(), b0.x0, b0.y0, b1.w(), b1.h(), b1.x0, b1.y0, b2.w(), b2.h(), b2.x0, b2.y0);
    AUREA_CHECK(b1.x0 == b0.x0 && b1.y0 == b0.y0 && b1.w() == b0.w() && b1.h() == b0.h());
    AUREA_CHECK(b2.w() == b0.w() && b2.h() == b0.h());
    AUREA_CHECK(b2.y0 + 100 == b0.y0);
}

AUREA_TEST(Gpu, MotionBlurSmearsAlongTheMotionAndKeepsEnergy) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(400, 200);
    auto id = rig.e.add_shape(10);   // quadrado
    AUREA_CHECK(id.ok());
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    Layer* l = comp->layer(LayerId::unpack(*id));
    // 20 px por quadro na horizontal (keyframes lineares 0 → 10).
    Track& px = l->tracks.get_or_create(TrackProperty::PositionX);
    px.set(l->local_time(FrameIndex{0}), 100.0f);
    px.set(l->local_time(FrameIndex{10}), 300.0f);
    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{5}, 30.0);
    AUREA_CHECK(rig.e.apply_command(seek).ok());
    auto measure = [&](const Image8& img, u32& partial, f64& energy) {
        Box8 b = lit_box(img);
        partial = 0;
        energy = 0.0;
        const u32 y = (b.y0 + b.y1) / 2;
        for (u32 x = 0; x < img.width; ++x) {
            const u8 v = img.at(x, y)[0];
            if (v > 8 && v < 200) ++partial;
        }
        for (usize i = 0; i < img.rgba.size(); i += 4) energy += img.rgba[i];
        return b;
    };
    u32 p0 = 0, p1 = 0;
    f64 e0 = 0, e1 = 0;
    const Box8 sharp = measure(rig.capture(400), p0, e0);
    AUREA_CHECK(rig.e.set_motion_blur(*id, true));
    const Box8 blur = measure(rig.capture(400), p1, e1);
    std::printf("    sem desfoque %ux%u (%u meio-tons); com desfoque %ux%u (%u meio-tons); energia %.3f\n",
                sharp.w(), sharp.h(), p0, blur.w(), blur.h(), p1, e1 / e0);
    // 180° de obturador a 20 px/quadro = rastro de ~10 px (±3).
    AUREA_CHECK(blur.w() >= sharp.w() + 7 && blur.w() <= sharp.w() + 13);
    AUREA_CHECK(blur.h() == sharp.h());          // nada na vertical
    AUREA_CHECK(p1 >= 8 && p0 <= 2);              // ~5 px de degradê em cada borda
    // Energia em gama sRGB não é linear: tolerância larga, mas sem sumir/estourar.
    AUREA_CHECK(e1 / e0 > 0.85 && e1 / e0 < 1.15);
    // Parado: o desfoque não muda nada.
    px.clear();
    const Image8 still = rig.capture(400);
    AUREA_CHECK(rig.e.set_motion_blur(*id, false));
    const Image8 stillOff = rig.capture(400);
    u32 worst = 0;
    for (usize i = 0; i < still.rgba.size(); ++i) worst = std::max<u32>(worst, static_cast<u32>(std::abs(still.rgba[i] - stillOff.rgba[i])));
    AUREA_CHECK(worst <= 1);
}

AUREA_TEST(Gpu, PrecomposeLooksIdenticalAndTransformsAsOne) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    auto a = rig.e.add_shape(0);    // círculo
    auto b = rig.e.add_shape(9);    // seta
    AUREA_CHECK(a.ok() && b.ok());
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    comp->layer(LayerId::unpack(*a))->transform.position = Vec3{100, 90, 0};
    comp->layer(LayerId::unpack(*b))->transform.position = Vec3{220, 90, 0};
    const Image8 before = rig.capture(320);
    const u64 ids[2] = {*a, *b};
    auto pre = rig.e.precompose(ids, 2);
    AUREA_CHECK(pre.ok());
    comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    AUREA_CHECK_EQ(comp->layers().count(), 1u);
    const Image8 after = rig.capture(320);
    u32 worst = 0;
    for (usize i = 0; i < before.rgba.size() && i < after.rgba.size(); ++i)
        worst = std::max<u32>(worst, static_cast<u32>(std::abs(before.rgba[i] - after.rgba[i])));
    // Metade da escala: o conjunto encolhe junto, em volta do centro.
    Layer* p = comp->layer(LayerId::unpack(*pre));
    p->transform.scale = Vec3{0.5f, 0.5f, 1};
    const Box8 half = lit_box(rig.capture(320));
    const Box8 full = lit_box(before);
    std::printf("    pre-composicao: diferenca maxima %u; caixa %ux%u -> %ux%u\n", worst, full.w(), full.h(), half.w(), half.h());
    AUREA_CHECK(worst <= 2);
    AUREA_CHECK(std::abs(static_cast<i32>(half.w()) * 2 - static_cast<i32>(full.w())) <= 4);
    // Dentro dela: as duas camadas, nos mesmos tempos; voltar funciona.
    AUREA_CHECK(rig.e.open_precomp(*pre));
    AUREA_CHECK_EQ(rig.e.precomp_depth(), 1u);
    const Composition* child = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    AUREA_CHECK(child && child->layers().count() == 2u);
    AUREA_CHECK(rig.e.close_precomp());
    AUREA_CHECK_EQ(rig.e.precomp_depth(), 0u);
    // Desfazer devolve as duas camadas à principal.
    Command u;
    u.type = CommandType::Undo;
    AUREA_CHECK(rig.e.apply_command(u).ok());   // a escala foi direta: desfaz o pré-compor
    comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    AUREA_CHECK_EQ(comp->layers().count(), 2u);
}

AUREA_TEST(Gpu, PrecomposeChildOfOutsideParentStaysInPlace) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    auto nul = rig.e.add_null(false);
    auto shp = rig.e.add_shape(9);   // seta
    AUREA_CHECK(nul.ok() && shp.ok());
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    Layer* n = comp->layer(LayerId::unpack(*nul));
    n->transform.position = Vec3{200, 70, 0};
    n->transform.rotation = Vec3{0, 0, 25};
    Command pc;
    pc.type = CommandType::LayerSetParent;
    pc.layer_parent.layer = LayerId::unpack(*shp);
    pc.layer_parent.parent = LayerId::unpack(*nul);
    AUREA_CHECK(rig.e.apply_command(pc).ok());
    n->transform.position = Vec3{230, 100, 0};   // o pai anda: a seta vai junto
    const Image8 before = rig.capture(320);
    const u64 ids[1] = {*shp};
    AUREA_CHECK(rig.e.precompose(ids, 1).ok());
    const Image8 after = rig.capture(320);
    u32 worst = 0;
    for (usize i = 0; i < before.rgba.size() && i < after.rgba.size(); ++i)
        worst = std::max<u32>(worst, static_cast<u32>(std::abs(before.rgba[i] - after.rgba[i])));
    std::printf("    filho de pai de fora: diferenca maxima %u\n", worst);
    AUREA_CHECK(worst <= 3);
}

AUREA_TEST(Gpu, ParticlesAreDeterministicAndSeekable) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    auto id = rig.e.add_particles(0);   // faiscas
    AUREA_CHECK(id.ok());
    // A acumulacao e medida por CONTAGEM, nao por area: com o tamanho fixo em
    // 1,5 px cada particula cobre o mesmo tanto em qualquer instante, e a
    // cobertura passa a ser proporcional ao numero de particulas vivas.
    //
    // Antes isto era medido com a arte padrao (que encolhe de 10 para 2 px ao
    // longo da vida): a area caia 25x enquanto a contagem subia 10x, e o
    // limiar de 3x media a razao entre as duas coisas — nao a acumulacao. O
    // teste passava por coincidencia com aquela arte, e quebrava a cada ajuste
    // de aparencia sem que nada de errado tivesse acontecido.
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::StartSize), 1.5f));
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::EndSize), 1.5f));
    // Rastro tambem desligado: o comprimento dele nao depende do tamanho e
    // encurta com a idade, o que confundiria contagem com arte de novo.
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::TrailLength), 0.0f));
    auto seekTo = [&](i64 f) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(rig.e.apply_command(c).ok());
    };
    seekTo(3);
    const f32 early = coverage(rig.capture(320));
    seekTo(30);
    const Image8 a = rig.capture(320);
    const f32 at1s = coverage(a);
    seekTo(90);
    (void)rig.capture(320);
    seekTo(30);   // voltar: o mesmo quadro, sem simulação a refazer
    const Image8 b = rig.capture(320);
    u32 worst = 0;
    for (usize i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i)
        worst = std::max<u32>(worst, static_cast<u32>(std::abs(a.rgba[i] - b.rgba[i])));
    const Box8 box = lit_box(a);
    std::printf("    particulas: cobertura 0,1 s %.4f -> 1 s %.4f; caixa %ux%u; ida e volta diferenca %u\n",
                early, at1s, box.w(), box.h(), worst);
    AUREA_CHECK(at1s > early * 3.0f && at1s > 0.002f);
    AUREA_CHECK(worst == 0);
    // Jato para cima com gravidade: mais alto que largo.
    AUREA_CHECK(box.h() > box.w() / 2);
}

AUREA_TEST(Gpu, ParticularPresetsAllDrawAndDifferFromEachOther) {
    AUREA_REQUIRE_GPU();
    // Os dez presets precisam DESENHAR e precisam ser DIFERENTES entre si. Um
    // preset que nao desenha e um botao falso; dois presets iguais sao um
    // preset a menos do que a lista promete.
    std::vector<Image8> shots;
    for (u32 preset = 0; preset < 10; ++preset) {
        Scene3DRig rig(320, 180);
        auto id = rig.e.add_particles(preset);
        if (!id.ok()) { AUREA_CHECK(false); return; }
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{45}, 30.0);
        AUREA_CHECK(rig.e.apply_command(c).ok());
        const Image8 img = rig.capture(320);
        const f32 cov = coverage(img);
        std::printf("    preset %u: cobertura %.4f\n", preset, cov);
        AUREA_CHECK(cov > 0.001f);
        shots.push_back(img);
    }
    for (usize i = 0; i < shots.size(); ++i) {
        for (usize j = i + 1; j < shots.size(); ++j) {
            u32 worst = 0;
            for (usize k = 0; k < shots[i].rgba.size() && k < shots[j].rgba.size(); ++k)
                worst = std::max<u32>(worst, static_cast<u32>(std::abs(shots[i].rgba[k] - shots[j].rgba[k])));
            AUREA_CHECK(worst > 8);
        }
    }
}

AUREA_TEST(Gpu, ParticularIsDeterministicWithAuxTurbulenceAndCollision) {
    AUREA_REQUIRE_GPU();
    // Tudo o que o Particular acrescentou e FECHADO: com aux, turbulencia,
    // vortice e colisao ligados, seek repetido tem de dar o MESMO quadro — e
    // sem passar por nenhum estado acumulado entre quadros.
    Scene3DRig rig(320, 180);
    auto id = rig.e.add_particles(5);   // brasas: ja traz aux e turbulencia
    AUREA_CHECK(id.ok());
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::Vortex), 120.0f));
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::AuxCount), 4.0f));
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::Collision),
                                         static_cast<f32>(ParticleCollision::Plane)));
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::CollisionY), 60.0f));
    auto seekTo = [&](i64 f) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(rig.e.apply_command(c).ok());
    };
    seekTo(40);
    const Image8 a = rig.capture(320);
    seekTo(200);           // longe daqui: se houvesse estado, ele teria mudado
    (void)rig.capture(320);
    seekTo(40);
    const Image8 b = rig.capture(320);
    u32 worst = 0;
    for (usize i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i)
        worst = std::max<u32>(worst, static_cast<u32>(std::abs(a.rgba[i] - b.rgba[i])));
    std::printf("    particular completo: ida e volta diferenca %u\n", worst);
    AUREA_CHECK(worst == 0);
    AUREA_CHECK(coverage(a) > 0.002f);
}

AUREA_TEST(Gpu, ParticularParamKeyframesDriveTheRender) {
    AUREA_REQUIRE_GPU();
    // Um parametro do Particular com keyframe tem de MUDAR o desenho no tempo:
    // a taxa vai de 0 a 400/s num segundo. Sem keyframes o valor parado ficaria
    // valendo em todo lugar e os dois instantes sairiam iguais.
    Scene3DRig rig(320, 180);
    auto id = rig.e.add_particles(2);   // poeira de luz
    AUREA_CHECK(id.ok());
    // Tamanho fixo e sem rastro: a cobertura volta a medir a CONTAGEM.
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::StartSize), 1.5f));
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::EndSize), 1.5f));
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::TrailLength), 0.0f));
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::StartOpacity), 1.0f));
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::EndOpacity), 1.0f));
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::Rate), 0.0f));
    auto seekTo = [&](i64 f) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(rig.e.apply_command(c).ok());
    };
    auto key = [&](i64 f, f32 v) {
        Command c;
        c.type = CommandType::KeyframeInsert;
        c.keyframe.track.layer = LayerId::unpack(*id);
        c.keyframe.track.property = TrackProperty::ParticleParam;
        c.keyframe.track.effectIndex = kInvalidIndex;
        c.keyframe.track.effectParamIndex = static_cast<u32>(ParticleParam::Rate);
        c.keyframe.time = FrameIndex{f};
        c.keyframe.value = v;
        AUREA_CHECK(rig.e.apply_command(c).ok());
    };
    seekTo(0);
    key(0, 0.0f);
    key(60, 400.0f);
    seekTo(6);
    const f32 early = coverage(rig.capture(320));
    seekTo(54);
    const f32 late = coverage(rig.capture(320));
    std::printf("    particular com keyframe na taxa: cobertura q6 %.4f -> q54 %.4f\n", early, late);
    AUREA_CHECK(late > early * 3.0f);
    AUREA_CHECK(late > 0.002f);
    // E o valor lido pela UI acompanha o instante (o controle mostra o que a
    // previa desenha), nao o campo parado.
    f32 out[static_cast<usize>(ParticleParam::Count)]{};
    AUREA_CHECK(rig.e.query_particles(*id, out));
    const f32 shownLate = out[static_cast<usize>(ParticleParam::Rate)];
    seekTo(6);
    AUREA_CHECK(rig.e.query_particles(*id, out));
    const f32 shownEarly = out[static_cast<usize>(ParticleParam::Rate)];
    std::printf("    taxa lida: q54 %.1f -> q6 %.1f\n", shownLate, shownEarly);
    AUREA_CHECK(shownLate > shownEarly * 3.0f);
}

AUREA_TEST(Gpu, TransitionsFadeAndSlideAtTheEdges) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    auto id = rig.e.add_shape(10);
    AUREA_CHECK(id.ok());
    auto seekTo = [&](i64 f) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(rig.e.apply_command(c).ok());
    };
    auto energy = [](const Image8& img) { f64 e = 0; for (usize i = 0; i < img.rgba.size(); i += 4) e += img.rgba[i]; return e; };
    seekTo(20);
    const Image8 full = rig.capture(320);
    const Box8 fb = lit_box(full);
    AUREA_CHECK(rig.e.set_transition(*id, false, 1, 10));   // dissolver 10 quadros
    seekTo(0);
    const f64 e0 = energy(rig.capture(320));
    seekTo(5);
    const f64 e5 = energy(rig.capture(320));
    seekTo(20);
    const Image8 after = rig.capture(320);
    u32 worst = 0;
    for (usize i = 0; i < full.rgba.size(); ++i) worst = std::max<u32>(worst, static_cast<u32>(std::abs(full.rgba[i] - after.rgba[i])));
    // Deslizar para cima: no meio da entrada a caixa está mais baixa.
    AUREA_CHECK(rig.e.set_transition(*id, false, 2, 10));
    seekTo(5);
    const Box8 sb = lit_box(rig.capture(320));
    std::printf("    dissolver: q0 %.3f q5 %.3f do cheio; depois diferenca %u; deslizar: y %u -> %u\n",
                e0 / energy(full), e5 / energy(full), worst, fb.y0, sb.y0);
    AUREA_CHECK(e0 / energy(full) < 0.01);
    AUREA_CHECK(e5 / energy(full) > 0.3 && e5 / energy(full) < 0.95);
    AUREA_CHECK(worst == 0);
    AUREA_CHECK(sb.y0 > fb.y0 + 5);
}

AUREA_TEST(Gpu, EchoTrailsAndRgbTimeSplitsChannels) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(400, 200);
    auto id = rig.e.add_shape(10);
    AUREA_CHECK(id.ok());
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    Layer* l = comp->layer(LayerId::unpack(*id));
    l->transform.scale = Vec3{0.4f, 0.4f, 1};   // quadrado pequeno (24 px)
    Track& px = l->tracks.get_or_create(TrackProperty::PositionX);
    px.set(l->local_time(FrameIndex{0}), 60.0f);
    px.set(l->local_time(FrameIndex{15}), 360.0f);   // 20 px/quadro
    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{12}, 30.0);
    AUREA_CHECK(rig.e.apply_command(seek).ok());
    const Box8 plain = lit_box(rig.capture(400));
    AUREA_CHECK(rig.e.set_echo(*id, 3, 3.0f, 0.5f));
    const Image8 ec = rig.capture(400);
    const Box8 echoBox = lit_box(ec);
    // A cópia mais velha (9 quadros = 180 px atrás) é mais fraca que a atual.
    const u32 y = (plain.y0 + plain.y1) / 2;
    const u8 now = ec.at((plain.x0 + plain.x1) / 2, y)[0];
    const u8 old = ec.at((plain.x0 + plain.x1) / 2 - 180, y)[0];
    AUREA_CHECK(rig.e.set_echo(*id, 0, 3.0f, 0.5f));
    AUREA_CHECK(rig.e.set_rgb_time(*id, 2.0f));
    const Image8 rgb = rig.capture(400);
    const u8* lead = rgb.at(plain.x1 - 3, y);             // só a amostra atual (vermelho) chega aqui
    const u8* tail = rgb.at(plain.x0 - 80 + 3, y);        // só a de 4 quadros atrás (azul)
    std::printf("    eco: caixa %u -> %u px; atual %u, mais velha %u; rgb: frente (%u,%u,%u) cauda (%u,%u,%u)\n",
                plain.w(), echoBox.w(), now, old, lead[0], lead[1], lead[2], tail[0], tail[1], tail[2]);
    AUREA_CHECK(echoBox.w() >= plain.w() + 170);
    AUREA_CHECK(old > 20 && old < now);
    AUREA_CHECK(lead[0] > 200 && lead[1] < 20 && lead[2] < 20);
    AUREA_CHECK(tail[2] > 200 && tail[0] < 20 && tail[1] < 20);

    // Eco como EFEITO ("Eco e rastro"): o mesmo quadro do eco antigo da camada.
    auto diff8 = [](const Image8& a, const Image8& b) {
        u32 w = 0;
        for (usize k = 0; k < a.rgba.size() && k < b.rgba.size(); ++k) w = std::max<u32>(w, static_cast<u32>(std::abs(a.rgba[k] - b.rgba[k])));
        return a.rgba.size() == b.rgba.size() ? w : 999u;
    };
    AUREA_CHECK(rig.e.set_rgb_time(*id, 0.0f));
    AUREA_CHECK(rig.e.set_echo(*id, 3, 3.0f, 0.5f));
    const Image8 viaLayer = rig.capture(400);
    AUREA_CHECK(rig.e.set_echo(*id, 0, 3.0f, 0.5f));
    const EffectTypeId echoType = rig.e.effects().find_key(effect_keys::kEchoTrail);
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(*id);
    add.effect_add.effectType = echoType;
    add.effect_add.index = 0xFFFFFFFFu;
    AUREA_CHECK(rig.e.apply_command(add).ok());
    l = comp->layer(LayerId::unpack(*id));
    AUREA_CHECK_EQ(l->effects.size(), 1u);
    l->effects[0].params[0].constant = ParamValue::scalar(3.0f);
    l->effects[0].params[1].constant = ParamValue::scalar(3.0f);
    l->effects[0].params[2].constant = ParamValue::scalar(0.5f);
    const Image8 viaEffect = rig.capture(400);
    const u32 effDiff = diff8(viaLayer, viaEffect);
    // Projeto antigo (eco nos campos da camada) abre com o efeito no lugar.
    l->effects.clear();
    AUREA_CHECK(rig.e.set_echo(*id, 3, 3.0f, 0.5f));
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_eco.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.apply_command(seek).ok());
    comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    l = comp->layer(LayerId::unpack(*id));
    AUREA_CHECK(l != nullptr);
    u32 migDiff = 999;
    if (l) {
        AUREA_CHECK(l->echoCount == 0 && l->effects.size() == 1 && l->effects[0].type == echoType);
        migDiff = diff8(viaLayer, rig.capture(400));
    }
    std::printf("    eco como efeito: dif %u; projeto antigo migrado: dif %u\n", effDiff, migDiff);
    AUREA_CHECK(effDiff <= 1);
    AUREA_CHECK(migDiff <= 1);
    std::remove(path.c_str());
}

AUREA_TEST(Gpu, StressThreeHundredAnimatedLayers) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(640, 360);
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    const u64 t0 = monotonic_ns();
    for (u32 i = 0; i < 300; ++i) {
        auto id = rig.e.add_shape(i % 11);
        if (!id.ok()) { AUREA_CHECK(false); return; }
        Layer* l = comp->layer(LayerId::unpack(*id));
        l->transform.scale = Vec3{0.15f, 0.15f, 1};
        Track& x = l->tracks.get_or_create(TrackProperty::PositionX);
        Track& r = l->tracks.get_or_create(TrackProperty::RotationZ);
        for (i64 k = 0; k < 10; ++k) {   // 3000 keyframes de posição + 3000 de rotação
            x.set(FrameIndex{k * 6}, static_cast<f32>((i * 37 + k * 50) % 640));
            r.set(FrameIndex{k * 6}, static_cast<f32>(k * 36));
        }
    }
    const f64 buildMs = static_cast<f64>(monotonic_ns() - t0) / 1e6;
    (void)rig.capture(640);   // pipelines prontos
    const u64 t1 = monotonic_ns();
    constexpr int kFrames = 10;
    for (int f = 0; f < kFrames; ++f) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{f * 5}, 30.0);
        AUREA_CHECK(rig.e.apply_command(c).ok());
        (void)rig.capture(640);
    }
    const f64 frameMs = static_cast<f64>(monotonic_ns() - t1) / 1e6 / kFrames;
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_estresse.aurea";
    const u64 t2 = monotonic_ns();
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    const f64 saveMs = static_cast<f64>(monotonic_ns() - t2) / 1e6;
    FILE* fp = std::fopen(path.c_str(), "rb");
    long bytes = 0;
    if (fp) { std::fseek(fp, 0, SEEK_END); bytes = std::ftell(fp); std::fclose(fp); }
    const u64 t3 = monotonic_ns();
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    const f64 loadMs = static_cast<f64>(monotonic_ns() - t3) / 1e6;
    comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    AUREA_CHECK_EQ(comp->layers().count(), 300u);
    std::printf("    300 camadas / 6000 keyframes: montar %.0f ms, quadro (captura) %.1f ms, salvar %.1f ms (%ld KB), abrir %.1f ms\n",
                buildMs, frameMs, saveMs, bytes / 1024, loadMs);
    AUREA_CHECK(frameMs < 500.0);
    AUREA_CHECK(loadMs < 2000.0);
    std::remove(path.c_str());
}

AUREA_TEST(Gpu, Scene3DLodDropsTrianglesWhenSmallOnScreen) {
    AUREA_REQUIRE_GPU();
    const std::string path = gltf_data("DamagedHelmet.glb");
    if (!file_exists(path)) return;
    Scene3DRig rig(512, 288);
    ModelImport mi;
    mi.path = path;
    auto id = rig.e.import_model(mi);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    (void)rig.capture(512);
    const u32 near = rig.e.renderer().scene_stats().triangles;
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    Layer* l = comp->layer(LayerId::unpack(*id));
    const Vec3 s0 = l->transform.scale;
    l->transform.scale = s0 * 0.25f;
    (void)rig.capture(512);
    const u32 mid = rig.e.renderer().scene_stats().triangles;
    l->transform.scale = s0 * 0.08f;
    (void)rig.capture(512);
    const u32 far = rig.e.renderer().scene_stats().triangles;
    std::printf("    LOD na tela: perto %u, 1/4 %u, 1/12 %u triangulos\n", near, mid, far);
    AUREA_CHECK(near >= 15000);
    AUREA_CHECK(far < near / 2);
    AUREA_CHECK(far <= mid);
}

namespace {
/// HDRI Radiance mínimo (sem RLE): metade de cima = `top`, de baixo = `bottom`.
std::string write_test_hdr(Vec3 top, Vec3 bottom) {
    const u32 w = 64, h = 32;
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste.hdr";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return {};
    std::fprintf(f, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %u +X %u\n", h, w);
    auto rgbe = [](Vec3 c, u8* o) {
        const f32 m = std::max(c.x, std::max(c.y, c.z));
        if (m < 1e-32f) { o[0] = o[1] = o[2] = o[3] = 0; return; }
        int e = 0;
        const f32 k = std::frexp(m, &e) * 256.0f / m;
        o[0] = static_cast<u8>(c.x * k); o[1] = static_cast<u8>(c.y * k); o[2] = static_cast<u8>(c.z * k);
        o[3] = static_cast<u8>(e + 128);
    };
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            u8 px[4];
            rgbe(y < h / 2 ? top : bottom, px);
            std::fwrite(px, 1, 4, f);
        }
    }
    std::fclose(f);
    return path;
}
} // namespace

AUREA_TEST(Gpu, HdriLightsTheModelAndSurvivesReopen) {
    AUREA_REQUIRE_GPU();
    const std::string model = gltf_data("DamagedHelmet.glb");
    if (!file_exists(model)) return;
    Scene3DRig rig(320, 180);
    ModelImport mi;
    mi.path = model;
    AUREA_CHECK(rig.e.import_model(mi).ok());
    auto tint = [](const Image8& img) {
        f64 r = 0, g = 0;
        for (usize i = 0; i < img.rgba.size(); i += 4) { r += img.rgba[i]; g += img.rgba[i + 1]; }
        return r / std::max(1.0, g);
    };
    const f64 studio = tint(rig.capture(320));
    const std::string hdr = write_test_hdr(Vec3{4.0f, 0.3f, 0.2f}, Vec3{0.6f, 0.05f, 0.03f});
    auto id = rig.e.import_hdri(hdr.c_str());
    AUREA_CHECK(id.ok());
    const f64 red = tint(rig.capture(320));
    std::printf("    hdri: vermelho/verde estudio %.3f -> hdri vermelho %.3f\n", studio, red);
    AUREA_CHECK(red > studio * 1.3);
    // Salvar e reabrir: o HDRI volta (lido do arquivo).
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_hdri.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    const f64 back = tint(rig.capture(320));
    std::printf("    reaberto: %.3f\n", back);
    AUREA_CHECK(std::fabs(back - red) < 0.05 * red);
    // Voltar ao estúdio.
    AUREA_CHECK(rig.e.clear_hdri());
    const f64 again = tint(rig.capture(320));
    AUREA_CHECK(std::fabs(again - studio) < 0.05 * studio);
    std::remove(path.c_str());
    std::remove(hdr.c_str());
}

namespace {
u32 max_diff(const Image8& a, const Image8& b) {
    u32 w = 0;
    for (usize i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i) w = std::max<u32>(w, static_cast<u32>(std::abs(a.rgba[i] - b.rgba[i])));
    return w;
}
void set_parent(Engine& e, u64 child, u64 parent) {
    Command pc;
    pc.type = CommandType::LayerSetParent;
    pc.layer_parent.layer = LayerId::unpack(child);
    pc.layer_parent.parent = parent ? LayerId::unpack(parent) : LayerId{};
    AUREA_CHECK(e.apply_command(pc).ok());
}
void seek_frame(Engine& e, i64 f) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{f}, 30.0);
    AUREA_CHECK(e.apply_command(c).ok());
}
} // namespace

AUREA_TEST(Gpu, ParentingKeepsModelAndAnimatedLayerInPlace) {
    AUREA_REQUIRE_GPU();
    // Modelo 3D vinculado a um nulo deslocado e girado: a tela não muda.
    {
        const std::string path = gltf_data("Box.glb");
        if (file_exists(path)) {
            Scene3DRig rig(320, 180);
            ModelImport mi;
            mi.path = path;
            auto m = rig.e.import_model(mi);
            auto n = rig.e.add_null(false);
            AUREA_CHECK(m.ok() && n.ok());
            Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
            Layer* nl = comp->layer(LayerId::unpack(*n));
            nl->transform.position = Vec3{230, 60, 0};
            nl->transform.rotation = Vec3{0, 0, 35};
            nl->transform.scale = Vec3{1.5f, 1.5f, 1};
            const Image8 before = rig.capture(320);
            set_parent(rig.e, *m, *n);
            const u32 d1 = max_diff(before, rig.capture(320));
            set_parent(rig.e, *m, 0);   // soltar volta ao mesmo lugar
            const u32 d2 = max_diff(before, rig.capture(320));
            std::printf("    modelo 3D: vincular diferenca %u, soltar %u\n", d1, d2);
            AUREA_CHECK(d1 <= 3);
            AUREA_CHECK(d2 <= 3);
        }
    }
    // Forma com posição e rotação ANIMADAS: igual em dois instantes.
    {
        Scene3DRig rig(320, 180);
        auto sh = rig.e.add_shape(9);
        auto n = rig.e.add_null(false);
        AUREA_CHECK(sh.ok() && n.ok());
        Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        Layer* l = comp->layer(LayerId::unpack(*sh));
        l->transform.scale = Vec3{0.4f, 0.4f, 1};
        Track& px = l->tracks.get_or_create(TrackProperty::PositionX);
        px.set(l->local_time(FrameIndex{0}), 60.0f);
        px.set(l->local_time(FrameIndex{20}), 260.0f);
        Track& rz = l->tracks.get_or_create(TrackProperty::RotationZ);
        rz.set(l->local_time(FrameIndex{0}), 0.0f);
        rz.set(l->local_time(FrameIndex{20}), 90.0f);
        Layer* nl = comp->layer(LayerId::unpack(*n));
        nl->transform.position = Vec3{200, 120, 0};
        nl->transform.rotation = Vec3{0, 0, -20};
        nl->transform.scale = Vec3{0.8f, 0.8f, 1};
        seek_frame(rig.e, 5);
        const Image8 a5 = rig.capture(320);
        seek_frame(rig.e, 15);
        const Image8 a15 = rig.capture(320);
        set_parent(rig.e, *sh, *n);
        seek_frame(rig.e, 5);
        const u32 d5 = max_diff(a5, rig.capture(320));
        seek_frame(rig.e, 15);
        const u32 d15 = max_diff(a15, rig.capture(320));
        std::printf("    forma animada: diferenca q5 %u, q15 %u\n", d5, d15);
        AUREA_CHECK(d5 <= 3);
        AUREA_CHECK(d15 <= 3);
    }
}

AUREA_TEST(Gpu, Scene3DSurvivesSaveAndReopenIdentically) {
    AUREA_REQUIRE_GPU();
    const std::string path = gltf_data("DamagedHelmet.glb");
    if (!file_exists(path)) return;
    Image8 before, after;
    {
        Scene3DRig rig(384, 216);
        ModelImport mi;
        mi.path = path;
        AUREA_CHECK(rig.e.import_model(mi).ok());
        before = rig.capture(384);
        AUREA_CHECK(rig.e.save_project("aurea_teste_3d.aurea").ok());
    }
    {
        Scene3DRig rig(384, 216);
        AUREA_CHECK(rig.e.load_project("aurea_teste_3d.aurea").ok());
        after = rig.capture(384);
    }
    AUREA_CHECK_EQ(before.width, after.width);
    AUREA_CHECK_EQ(before.height, after.height);
    if (before.rgba.size() != after.rgba.size()) return;
    u32 worst = 0;
    for (usize i = 0; i < before.rgba.size(); ++i) worst = std::max<u32>(worst, static_cast<u32>(std::abs(before.rgba[i] - after.rgba[i])));
    AUREA_CHECK_MSG(worst <= 2, "o modelo reaberto nao e o mesmo quadro");
    if (worst > 2) { (void)write_png("reaberto_antes.png", before); (void)write_png("reaberto_depois.png", after); }
}

AUREA_TEST(Gpu, ExpressionOnPositionMovesTheLayer) {
    AUREA_REQUIRE_GPU();
    // Centro de massa dos pixels acesos (a forma branca sobre o preto).
    auto centroid = [](const Image8& img, f32& cx, f32& cy) {
        f64 sx = 0, sy = 0, n = 0;
        for (u32 y = 0; y < img.height; ++y) {
            for (u32 x = 0; x < img.width; ++x) {
                if (img.at(x, y)[0] > 128) { sx += x; sy += y; n += 1; }
            }
        }
        cx = n > 0 ? static_cast<f32>(sx / n) : -1.0f;
        cy = n > 0 ? static_cast<f32>(sy / n) : -1.0f;
        return n;
    };
    Scene3DRig rig(256, 256);
    auto id = rig.e.add_shape(0);   // círculo
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    f32 x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    AUREA_CHECK(centroid(rig.capture(256), x0, y0) > 100);
    const u32 px = static_cast<u32>(TrackProperty::PositionX);
    const u32 py = static_cast<u32>(TrackProperty::PositionY);
    AUREA_CHECK(rig.e.set_expression(*id, px, kInvalidIndex, 0, "value + 60").ok());
    AUREA_CHECK(rig.e.set_expression(*id, py, kInvalidIndex, 0, "[value[0], value[1] - 40]").ok());
    AUREA_CHECK(centroid(rig.capture(256), x1, y1) > 100);
    AUREA_CHECK_NEAR(x1 - x0, 60.0f, 1.5f);
    AUREA_CHECK_NEAR(y1 - y0, -40.0f, 1.5f);
    // Dependente do tempo: no quadro 15 (0,5 s) a expressão "time*120" desloca 60 px.
    AUREA_CHECK(rig.e.set_expression(*id, px, kInvalidIndex, 0, "value + time * 120").ok());
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = TickNs{15 * 1'000'000'000LL / 30};
    AUREA_CHECK(rig.e.apply_command(c).ok());
    AUREA_CHECK(centroid(rig.capture(256), x2, y2) > 100);
    AUREA_CHECK_NEAR(x2 - x0, 60.0f, 1.5f);
    // Desligada: volta ao lugar (Y também desligada).
    AUREA_CHECK(rig.e.set_expression_enabled(*id, px, kInvalidIndex, 0, false));
    AUREA_CHECK(rig.e.set_expression_enabled(*id, py, kInvalidIndex, 0, false));
    f32 x3 = 0, y3 = 0;
    AUREA_CHECK(centroid(rig.capture(256), x3, y3) > 100);
    AUREA_CHECK_NEAR(x3, x0, 1.0f);
    AUREA_CHECK_NEAR(y3, y0, 1.0f);
    std::printf("    centro (%.1f, %.1f) -> expressao (%.1f, %.1f) [esperado +60, -40]; time*120 em 0,5 s: +%.1f px; desligada: (%.1f, %.1f)\n",
                x0, y0, x1, y1, x2 - x0, x3, y3);
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

// -----------------------------------------------------------------------------
// Export — o mesmo renderer do preview até os planos NV12 que o encoder recebe.
// -----------------------------------------------------------------------------
namespace {

/// Sink de teste: guarda os quadros e registra a ordem das chamadas.
struct CapturedExport {
    VideoStreamConfig video{};
    bool hasAudio = false;
    AudioStreamConfig audio{};
    std::vector<i16> pcm;
    std::vector<i64> audioPts;
    std::vector<u32> audioFrames;
    bool opened = false, finished = false, aborted = false;
    std::vector<i64> pts;
    std::vector<std::vector<u8>> y, uv;
};

class CaptureSink final : public ExportSink {
public:
    CaptureSink(CapturedExport* out, u32 delayMs) : out_(out), delayMs_(delayMs) {}
    Status open(const char*, const VideoStreamConfig& v, const AudioStreamConfig* a) noexcept override {
        out_->video = v;
        out_->hasAudio = a != nullptr;
        if (a) out_->audio = *a;
        out_->opened = true;
        return OkStatus;
    }
    Status write_video(const u8* y, u32 yStride, const u8* uv, u32 uvStride, i64 ptsUs) noexcept override {
        const u32 w = out_->video.width, h = out_->video.height;
        std::vector<u8> yy(static_cast<usize>(w) * h), cc(static_cast<usize>(w) * (h / 2));
        for (u32 r = 0; r < h; ++r) std::memcpy(&yy[static_cast<usize>(r) * w], y + static_cast<usize>(r) * yStride, w);
        for (u32 r = 0; r < h / 2; ++r) std::memcpy(&cc[static_cast<usize>(r) * w], uv + static_cast<usize>(r) * uvStride, w);
        out_->y.push_back(std::move(yy));
        out_->uv.push_back(std::move(cc));
        out_->pts.push_back(ptsUs);
        if (delayMs_) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs_));
        return OkStatus;
    }
    Status write_audio(const i16* pcm, u32 frames, i64 ptsUs) noexcept override {
        out_->pcm.insert(out_->pcm.end(), pcm, pcm + static_cast<usize>(frames) * out_->audio.channels);
        out_->audioPts.push_back(ptsUs);
        out_->audioFrames.push_back(frames);
        return OkStatus;
    }
    Status finish() noexcept override { out_->finished = true; return OkStatus; }
    void abort() noexcept override { out_->aborted = true; }
private:
    CapturedExport* out_;
    u32 delayMs_;
};

struct SinkCtx { CapturedExport* out; u32 delayMs; };

std::unique_ptr<ExportSink> make_capture_sink(void* user) {
    auto* c = static_cast<SinkCtx*>(user);
    return std::make_unique<CaptureSink>(c->out, c->delayMs);
}

bool wait_export(Engine& e, int timeoutMs = 20000) {
    for (int i = 0; i < timeoutMs / 5; ++i) {
        if (e.export_progress().finished) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

void set_duration(Engine& e, i64 frames) {
    Command c;
    c.type = CommandType::CompositionSetDuration;
    c.comp_duration.comp = e.project()->timeline().current();
    c.comp_duration.duration = FrameIndex{frames};
    AUREA_CHECK(e.apply_command(c).ok());
}

/// Motor com Vulkan de verdade, vídeo sintético e o sink de captura.
struct ExportRig {
    SyntheticFactory factory;
    CapturedExport cap;
    SinkCtx sc;
    Engine e;
    ExportRig(const SyntheticConfig& cfg, u32 delayMs) : factory(cfg), sc{&cap, delayMs} {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.mediaFactory = &factory;
        ec.exportSinkFactory = &make_capture_sink;
        ec.exportSinkContext = &sc;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(cfg.width, cfg.height, 30.0, nullptr).ok());
        VideoImport vi;
        vi.sourcePath = "sintetico";
        vi.displayName = "sintetico";
        AUREA_CHECK(e.import_video(vi).ok());
    }
    ~ExportRig() { e.shutdown(); }
};

} // namespace

AUREA_TEST(Gpu, ExportWritesBt709LimitedNv12WithExactTimestamps) {
    Gpu& g = gpu();
    if (!g.ok) return;
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    ExportRig rig(cfg, 0);
    set_duration(rig.e, 6);

    ExportSettings s;
    s.height = 36;          // lado menor = o da composição
    s.fps = 0.0;            // o da composição
    s.dither = false;       // códigos exatos
    AUREA_CHECK(rig.e.start_export(s, "nao-usado.mp4").ok());
    AUREA_CHECK(wait_export(rig.e));
    const Engine::ExportProgress p = rig.e.export_progress();
    AUREA_CHECK_EQ(p.result, Errc::Ok);
    AUREA_CHECK_EQ(p.framesDone, 6u);
    const CapturedExport& cap = rig.cap;
    AUREA_CHECK(cap.opened && cap.finished && !cap.aborted);
    AUREA_CHECK_EQ(cap.video.width, 64u);
    AUREA_CHECK_EQ(cap.video.height, 36u);
    AUREA_CHECK_EQ(cap.y.size(), static_cast<usize>(6));
    for (usize i = 0; i < cap.pts.size(); ++i) {
        AUREA_CHECK_EQ(cap.pts[i], static_cast<i64>(std::llround(static_cast<f64>(i) * 1e6 / 30.0)));
    }
    if (cap.y.size() < 4) return;
    // Vermelho (canto superior esquerdo) e branco (inferior direito) em BT.709
    // limitado: Y 63/235, Cb 102/128, Cr 240/128. ±2 pela ida e volta 8 bits.
    auto near = [](int v, int want) { return std::abs(v - want) <= 2; };
    const std::vector<u8>& Y = cap.y[3];
    const std::vector<u8>& C = cap.uv[3];
    AUREA_CHECK(near(Y[4 * 64 + 4], 63));
    AUREA_CHECK(near(C[2 * 64 + 2 * 2 + 0], 102));
    AUREA_CHECK(near(C[2 * 64 + 2 * 2 + 1], 240));
    AUREA_CHECK(near(Y[30 * 64 + 60], 235));
    AUREA_CHECK(near(C[15 * 64 + 30 * 2 + 0], 128));
    AUREA_CHECK(near(C[15 * 64 + 30 * 2 + 1], 128));
}

AUREA_TEST(Gpu, ExportMixesTheTimelineAudioSampleExact) {
    Gpu& g = gpu();
    if (!g.ok) return;
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    cfg.audioRate = 44100;          // reamostrado para 48 kHz no caminho
    cfg.audioChannels = 2;
    cfg.audioSeconds = 10.0;
    ExportRig rig(cfg, 0);
    set_duration(rig.e, 45);        // 1,5 s a 30 fps
    ExportSettings s;
    s.height = 36;
    s.dither = false;
    AUREA_CHECK(rig.e.start_export(s, "nao-usado.mp4").ok());
    AUREA_CHECK(wait_export(rig.e));
    AUREA_CHECK_EQ(rig.e.export_progress().result, Errc::Ok);
    const CapturedExport& cap = rig.cap;
    AUREA_CHECK(cap.hasAudio);
    AUREA_CHECK_EQ(cap.audio.sampleRate, 48000u);
    AUREA_CHECK_EQ(cap.audio.channels, 2u);
    // Exatamente 1,5 s de som, em trechos contíguos (pts = amostras já escritas).
    AUREA_CHECK_EQ(cap.pcm.size(), static_cast<usize>(72000 * 2));
    i64 written = 0;
    for (usize i = 0; i < cap.audioPts.size(); ++i) {
        AUREA_CHECK_EQ(cap.audioPts[i], audio::sample_to_ns(written) / 1000);
        written += cap.audioFrames[i];
    }
    // Conteúdo: a senoide da fonte na amostra 30000 (0,625 s), em 16 bits.
    const f64 t = 30000.0 / 48000.0;
    const f32 wantL = synthetic_audio_value(cfg, 0, t), wantR = synthetic_audio_value(cfg, 1, t);
    AUREA_CHECK_NEAR(cap.pcm[2 * 30000] / 32767.0, wantL, 2e-4);
    AUREA_CHECK_NEAR(cap.pcm[2 * 30000 + 1] / 32767.0, wantR, 2e-4);

    // Sem som na timeline (mudo): o arquivo sai só com vídeo.
    ExportRig silent(cfg, 0);
    set_duration(silent.e, 10);
    const Composition* comp = silent.e.project()->timeline().composition(silent.e.project()->timeline().current());
    LayerId only{};
    comp->layers().for_each([&](LayerId id, const Layer&) { only = id; });
    Command m;
    m.type = CommandType::AudioSetMuted;
    m.audio_flag.layer = only;
    m.audio_flag.flag = true;
    AUREA_CHECK(silent.e.apply_command(m).ok());
    AUREA_CHECK(silent.e.start_export(s, "nao-usado.mp4").ok());
    AUREA_CHECK(wait_export(silent.e));
    AUREA_CHECK(!silent.cap.hasAudio && silent.cap.pcm.empty());
}

AUREA_TEST(Gpu, ExportAtDoubleFpsRepeatsEachCompositionFrame) {
    Gpu& g = gpu();
    if (!g.ok) return;
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    cfg.pattern = SyntheticPattern::FrameGray;
    ExportRig rig(cfg, 0);
    set_duration(rig.e, 4);

    ExportSettings s;
    s.height = 36;
    s.fps = 60.0;
    s.dither = false;
    AUREA_CHECK(rig.e.start_export(s, "nao-usado.mp4").ok());
    AUREA_CHECK(wait_export(rig.e));
    AUREA_CHECK_EQ(rig.e.export_progress().result, Errc::Ok);
    const CapturedExport& cap = rig.cap;
    AUREA_CHECK_EQ(cap.y.size(), static_cast<usize>(8));   // 4 quadros a 30 = 8 a 60
    if (cap.y.size() < 8) return;
    // O mesmo instante da composição em pares (0,1)(2,3)…, e o nível muda.
    const usize mid = 18 * 64 + 32;
    for (usize k = 0; k < 4; ++k) {
        AUREA_CHECK_EQ(static_cast<int>(cap.y[2 * k][mid]), static_cast<int>(cap.y[2 * k + 1][mid]));
        if (k > 0) AUREA_CHECK(cap.y[2 * k][mid] != cap.y[2 * k - 2][mid]);
    }
    AUREA_CHECK_EQ(cap.pts[1], static_cast<i64>(16667));
}

AUREA_TEST(Gpu, ExportCancelAbortsTheSinkAndFreesThePreview) {
    Gpu& g = gpu();
    if (!g.ok) return;
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    ExportRig rig(cfg, 30);   // 30 ms por quadro: dá tempo de cancelar no meio
    set_duration(rig.e, 120);

    ExportSettings s;
    s.height = 36;
    AUREA_CHECK(rig.e.start_export(s, "nao-usado.mp4").ok());
    // Um segundo export ao mesmo tempo é recusado.
    AUREA_CHECK_EQ(rig.e.start_export(s, "outro.mp4").code(), Errc::InvalidState);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    AUREA_CHECK(rig.e.cancel_export().ok());
    AUREA_CHECK(wait_export(rig.e));
    const Engine::ExportProgress p = rig.e.export_progress();
    AUREA_CHECK_EQ(p.result, Errc::Cancelled);
    AUREA_CHECK(p.framesDone < 120u);
    AUREA_CHECK(rig.cap.aborted && !rig.cap.finished);
    // A GPU volta a atender o preview (capture usa o mesmo caminho).
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    AUREA_CHECK(rig.e.capture_frame_rgba(32, rgba, w, h).ok());
}

AUREA_TEST(Gpu, ExportAt60OverA30FpsVideoNeverWaitsForAnInBetweenFrame) {
    // Regressão: composição a 60 sobre vídeo a 30. O instante de cada quadro
    // ímpar ficava a meia distância de dois quadros da fonte; nenhum contava
    // como "exato" e o export esperava 4 s por quadro.
    Gpu& g = gpu();
    if (!g.ok) return;
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    cfg.fps = 30.0;
    cfg.pattern = SyntheticPattern::FrameGray;
    SyntheticFactory factory(cfg);
    CapturedExport cap;
    SinkCtx sc{&cap, 0};
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.mediaFactory = &factory;
    ec.exportSinkFactory = &make_capture_sink;
    ec.exportSinkContext = &sc;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(64, 36, 60.0, nullptr).ok());
    VideoImport vi;
    vi.sourcePath = "sintetico";
    vi.displayName = "cinza";
    AUREA_CHECK(e.import_video(vi).ok());   // o 1º vídeo traz a composição para 30
    Command fps;
    fps.type = CommandType::CompositionSetFps;
    fps.comp_fps.comp = e.project()->timeline().current();
    fps.comp_fps.fps = 60.0;
    AUREA_CHECK(e.apply_command(fps).ok());
    set_duration(e, 60);   // 1 s a 60

    ExportSettings s;
    s.height = 36;
    s.dither = false;
    const auto t0 = std::chrono::steady_clock::now();
    AUREA_CHECK(e.start_export(s, "nao-usado.mp4").ok());
    AUREA_CHECK(wait_export(e));
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    AUREA_CHECK_EQ(e.export_progress().result, Errc::Ok);
    AUREA_CHECK_EQ(cap.y.size(), static_cast<usize>(60));
    AUREA_CHECK(secs < 3.0);   // antes: ~2 min
    // Cada quadro da fonte aparece em dois quadros de saída seguidos.
    const usize mid = 18 * 64 + 32;
    if (cap.y.size() == 60) {
        for (usize k = 0; k < 30; ++k) AUREA_CHECK_EQ(static_cast<int>(cap.y[2 * k][mid]), static_cast<int>(cap.y[2 * k + 1][mid]));
    }
    e.shutdown();
}

#if defined(AUREA_TEST_VULKAN)
namespace {
std::string gltf_data3(const char* rel) { return std::string(AUREA_TEST_DATA_DIR) + "/gltf/" + rel; }
bool file_exists3(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}
} // namespace

AUREA_TEST(Gpu, Scene3DIsInTheExportedFrames) {
    // O 3D do export sai do MESMO renderer do preview: o quadro exportado tem
    // o modelo no centro (luma bem acima do preto de faixa limitada, 16).
    AUREA_REQUIRE_GPU();
    const std::string path = gltf_data3("Box.glb");
    if (!file_exists3(path)) return;
    CapturedExport cap;
    SinkCtx sc{&cap, 0};
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.exportSinkFactory = &make_capture_sink;
    ec.exportSinkContext = &sc;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(128, 72, 30.0, nullptr).ok());
    ModelImport mi;
    mi.path = path;
    AUREA_CHECK(e.import_model(mi).ok());
    set_duration(e, 3);
    ExportSettings s;
    s.height = 72;
    s.dither = false;
    AUREA_CHECK(e.start_export(s, "nao-usado.mp4").ok());
    AUREA_CHECK(wait_export(e));
    AUREA_CHECK_EQ(e.export_progress().result, Errc::Ok);
    AUREA_CHECK_EQ(cap.y.size(), static_cast<usize>(3));
    if (cap.y.size() == 3) {
        const int center = cap.y[1][36 * 128 + 64];
        const int corner = cap.y[1][2 * 128 + 2];
        AUREA_CHECK(center > 40);
        AUREA_CHECK(corner <= 17);
    }
    e.shutdown();
}
#endif

AUREA_TEST(Gpu, Text3DRendersEditsUndoesAndSurvivesReopen) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    scene3d::Text3DSpec spec;
    spec.content = "AUREA";
    spec.color = Vec4{1.0f, 0.45f, 0.0f, 1.0f};
    auto id = rig.e.add_text3d(spec);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    auto orange = [](const Image8& img) {
        u32 n = 0;
        for (usize i = 0; i < img.rgba.size(); i += 4)
            if (img.rgba[i] > 90 && img.rgba[i] > img.rgba[i + 1] + 30 && img.rgba[i + 1] > img.rgba[i + 2] + 10) ++n;
        return n;
    };
    const Image8 a = rig.capture(320);
    const u32 na = orange(a);
    // Girada em Y a 60°: as laterais aparecem (a malha tem profundidade).
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    Layer* l = comp->layer(LayerId::unpack(*id));
    l->transform.rotation = Vec3{0, 60, 0};
    const Image8 t60 = rig.capture(320);
    const u32 turned = orange(t60);
    l->transform.rotation = Vec3{0, 0, 0};
    // Editar: texto maior ocupa mais pixels; desfazer volta ao original.
    scene3d::Text3DSpec longer = spec;
    longer.content = "AUREA\nAUREA";
    AUREA_CHECK(rig.e.set_text3d(*id, longer).ok());
    const u32 nb = orange(rig.capture(320));
    scene3d::Text3DSpec q;
    AUREA_CHECK(rig.e.query_text3d(*id, q) && q.content == longer.content);
    Command u;
    u.type = CommandType::Undo;
    AUREA_CHECK(rig.e.apply_command(u).ok());
    const u32 undone = max_diff(a, rig.capture(320));
    u.type = CommandType::Redo;
    AUREA_CHECK(rig.e.apply_command(u).ok());
    // Salvar e reabrir: a malha é gerada de novo da receita.
    const Image8 before = rig.capture(320);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_texto3d.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    const u32 reopened = max_diff(before, rig.capture(320));
    std::printf("    texto 3D: %u px laranja, girado 60 graus %u, 2 linhas %u, desfazer dif %u, reaberto dif %u\n", na, turned, nb,
                undone, reopened);
    AUREA_CHECK(na > 700);
    AUREA_CHECK(turned > 300 && max_diff(a, t60) > 100);
    AUREA_CHECK(nb > na);
    AUREA_CHECK(undone <= 3);
    AUREA_CHECK(reopened <= 3);
    std::remove(path.c_str());
}

AUREA_TEST(Gpu, UngroupPrecompKeepsTheScreenAndRefusesWhatWouldChange) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    auto a = rig.e.add_shape(0);
    auto b = rig.e.add_shape(9);
    AUREA_CHECK(a.ok() && b.ok());
    auto cur = [&] { return rig.e.project()->timeline().composition(rig.e.project()->timeline().current()); };
    cur()->layer(LayerId::unpack(*a))->transform.position = Vec3{100, 90, 0};
    cur()->layer(LayerId::unpack(*b))->transform.position = Vec3{220, 90, 0};
    const Image8 orig = rig.capture(320);
    u64 ids[2] = {*a, *b};
    // 1. Grupo sem transform: volta direto, sem Nulo.
    auto pre = rig.e.precompose(ids, 2);
    AUREA_CHECK(pre.ok());
    auto n1 = rig.e.ungroup_precomp(*pre);
    AUREA_CHECK(n1.ok() && *n1 == 2u);
    const u32 layers1 = cur()->layers().count();
    const u32 d1 = max_diff(orig, rig.capture(320));
    // 2. Grupo movido, girado, com escala animada e entrada aparada: um Nulo
    //    leva o transform; a tela fica igual em dois instantes.
    std::vector<u64> now;
    for (u32 i = 0; i < cur()->order().size(); ++i) now.push_back(cur()->order().at(i).pack());
    pre = rig.e.precompose(now.data(), static_cast<u32>(now.size()));
    AUREA_CHECK(pre.ok());
    Layer* p = cur()->layer(LayerId::unpack(*pre));
    p->transform.position = Vec3{180, 100, 0};
    p->transform.rotation = Vec3{0, 0, 20};
    Track& sx = p->tracks.get_or_create(TrackProperty::ScaleX);
    sx.keys = {Keyframe{FrameIndex{0}, 0.6f}, Keyframe{FrameIndex{30}, 1.2f}};
    p->start = FrameIndex{p->start.value + 5};
    p->offset = FrameIndex{p->offset.value + 5};
    seek_frame(rig.e, 8);
    const Image8 g8 = rig.capture(320);
    seek_frame(rig.e, 20);
    const Image8 g20 = rig.capture(320);
    auto n2 = rig.e.ungroup_precomp(*pre);
    AUREA_CHECK(n2.ok() && *n2 == 2u);
    const u32 layers2 = cur()->layers().count();
    seek_frame(rig.e, 8);
    // Com transform o grupo é amostrado de uma textura intermediária; solto,
    // cada forma é vetorial: só a borda antisserrilhada muda. Mede-se a média
    // e a fração de pixels que mudam MUITO (forma fora do lugar).
    auto edge = [](const Image8& x, const Image8& y, f64* mean) {
        u64 sum = 0, big = 0, n = 0;
        for (usize i = 0; i + 3 < x.rgba.size() && i + 3 < y.rgba.size(); i += 4, ++n) {
            const u32 d = static_cast<u32>(std::abs(x.rgba[i] - y.rgba[i]));
            sum += d;
            big += d > 128;
        }
        *mean = static_cast<f64>(sum) / std::max<u64>(1, n);
        return static_cast<f64>(big) / std::max<u64>(1, n);
    };
    f64 m8 = 0, m20 = 0;
    const f64 b8 = edge(g8, rig.capture(320), &m8);
    seek_frame(rig.e, 20);
    const f64 b20 = edge(g20, rig.capture(320), &m20);
    // Desfazer devolve a camada do grupo.
    Command u;
    u.type = CommandType::Undo;
    AUREA_CHECK(rig.e.apply_command(u).ok());
    const bool back = cur()->layer(LayerId::unpack(*pre)) != nullptr;
    // 3. Opacidade no grupo mudaria a mistura das sobreposições: recusa.
    cur()->layer(LayerId::unpack(*pre))->transform.opacity = 0.5f;
    std::string why;
    auto n3 = rig.e.ungroup_precomp(*pre, &why);
    std::printf("    desagrupar: direto dif %u (%u camadas); com Nulo media %.3f/%.3f, pixels muito diferentes %.4f%%/%.4f%% (%u camadas); desfazer %s; opacidade recusada: \"%s\"\n", d1,
                layers1, m8, m20, b8 * 100, b20 * 100, layers2, back ? "ok" : "falhou", why.c_str());
    AUREA_CHECK(layers1 == 2u && d1 <= 2);
    AUREA_CHECK(layers2 == 3u && m8 < 0.5 && m20 < 0.5 && b8 < 0.002 && b20 < 0.002);
    AUREA_CHECK(back);
    AUREA_CHECK(!n3.ok() && !why.empty());
}

AUREA_TEST(Gpu, MotionBlurWorksOn3DLayersAndModels) {
    AUREA_REQUIRE_GPU();
    auto run = [&](bool model, u32& sharpW, u32& blurW, u32& stillDiff) {
        Scene3DRig rig(400, 200);
        Result<u64> id = Status{Errc::NotFound};
        if (model) {
            const std::string path = gltf_data("Box.glb");
            if (!file_exists(path)) return false;
            ModelImport mi;
            mi.path = path;
            id = rig.e.import_model(mi);
        } else {
            id = rig.e.add_shape(10);
        }
        AUREA_CHECK(id.ok());
        if (!id.ok()) return false;
        Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        Layer* l = comp->layer(LayerId::unpack(*id));
        if (model) l->model.unitScale *= 0.35f;              // caixa pequena: o rastro cabe no quadro
        else l->transform.rotation = Vec3{0, 30, 0};         // forma inclinada: vive no espaço 3D
        Track& px = l->tracks.get_or_create(TrackProperty::PositionX);
        px.set(l->local_time(FrameIndex{0}), 100.0f);
        px.set(l->local_time(FrameIndex{10}), 300.0f);
        seek_frame(rig.e, 5);
        sharpW = lit_box(rig.capture(400)).w();
        AUREA_CHECK(rig.e.set_motion_blur(*id, true));
        blurW = lit_box(rig.capture(400)).w();
        px.clear();                                           // parado: o desfoque não muda nada
        const Image8 still = rig.capture(400);
        AUREA_CHECK(rig.e.set_motion_blur(*id, false));
        stillDiff = max_diff(still, rig.capture(400));
        return true;
    };
    u32 s2 = 0, b2 = 0, d2 = 0, s3 = 0, b3 = 0, d3 = 0;
    const bool shape = run(false, s2, b2, d2);
    const bool box = run(true, s3, b3, d3);
    std::printf("    forma em 3D: largura %u -> %u (parada dif %u); modelo 3D: largura %u -> %u (parado dif %u)\n", s2, b2, d2, s3, b3, d3);
    // 180° a 20 px/quadro = rastro de ~10 px.
    if (shape) {
        AUREA_CHECK(b2 >= s2 + 6 && b2 <= s2 + 14);
        AUREA_CHECK(d2 <= 1);
    }
    if (box) {
        AUREA_CHECK(b3 >= s3 + 6 && b3 <= s3 + 14);
        AUREA_CHECK(d3 <= 1);
    }
}

AUREA_TEST(Gpu, FrameBlendMixesNeighbourSourceFramesInSlowMotion) {
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
    AUREA_CHECK(e.new_project(96, 54, 30.0, "mistura").ok());
    VideoImport imp;
    imp.sourcePath = "sintetico";
    imp.displayName = "clipe";
    auto layer = e.import_video(imp);
    AUREA_CHECK(layer.ok());
    if (!layer.ok()) { e.shutdown(); return; }
    {
        Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
        Layer* l = c->layer(LayerId::unpack(*layer));
        l->speed = 0.5f;                                   // câmera lenta: 2 quadros da tela por quadro da fonte
        l->end = FrameIndex{l->start.value + 400};
        c->set_duration(FrameIndex{400});
    }
    TextureDesc d;
    d.width = 96;
    d.height = 54;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    const TextureHandle target = *e.gpu()->create_texture(d);
    std::vector<u16> half(96 * 54 * 4);
    auto shown = [&](i64 frame) {
        Command seek;
        seek.type = CommandType::PlaybackSeek;
        seek.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(e.submit_commands(&seek, 1) == 1);
        AUREA_CHECK(e.render_offscreen(target, 96, 54).ok());
        AUREA_CHECK(e.gpu()->read_texture(target, half.data(), 96 * 8).ok());
        return srgb_encode(half_to_float(half[(27 * 96 + 48) * 4])) * 219.0f + 16.0f;
    };
    const f32 off = shown(101);                             // fonte 50,5 sem mistura = quadro 50
    AUREA_CHECK(e.set_frame_blend(*layer, 1));
    const f32 mixed = shown(101);
    const f32 onGrid = shown(100);                          // fonte 50,0: exato, nada a misturar
    auto lin = [](u32 f) { return srgb_decode((static_cast<f32>(frame_gray_code(f)) - 16.0f) / 219.0f); };
    const f32 expected = srgb_encode((lin(50) + lin(51)) * 0.5f) * 219.0f + 16.0f;
    std::printf("    mistura de quadros: sem %.2f (quadro 50 = %u), com %.2f (esperado %.2f; 51 = %u), na grade %.2f\n", off,
                frame_gray_code(50), mixed, expected, frame_gray_code(51), onGrid);
    AUREA_CHECK_NEAR(off, frame_gray_code(50), 0.5);
    AUREA_CHECK_NEAR(mixed, expected, 0.8);
    AUREA_CHECK_NEAR(onGrid, frame_gray_code(50), 0.5);
    e.gpu()->destroy_texture(target);
    e.shutdown();
}

AUREA_TEST(Gpu, OpticalFlowPlacesTheMovingSquareBetweenFrames) {
    AUREA_REQUIRE_GPU();
    SyntheticConfig cfg;
    cfg.width = 96;
    cfg.height = 54;
    cfg.frameCount = 300;
    cfg.pattern = SyntheticPattern::FastSquare;
    SyntheticFactory factory(cfg);
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.framesInFlight = 2;
    ec.mediaFactory = &factory;
    ec.workerCount = 2;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(96, 54, 30.0, "flow").ok());
    VideoImport imp;
    imp.sourcePath = "sintetico";
    imp.displayName = "clipe";
    auto layer = e.import_video(imp);
    AUREA_CHECK(layer.ok());
    if (!layer.ok()) { e.shutdown(); return; }
    {
        Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
        Layer* l = c->layer(LayerId::unpack(*layer));
        l->speed = 0.5f;
        l->end = FrameIndex{l->start.value + 400};
        c->set_duration(FrameIndex{400});
    }
    TextureDesc d;
    d.width = 96;
    d.height = 54;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    const TextureHandle target = *e.gpu()->create_texture(d);
    std::vector<u16> half(96 * 54 * 4);
    // Linha do meio: colunas com contraste de xadrez "cheio" (preto ou branco
    // de verdade) e o centro delas. Fantasma de mistura = meio-tom.
    struct Row { u32 full; f32 center; };
    auto row = [&](i64 frame) {
        Command seek;
        seek.type = CommandType::PlaybackSeek;
        seek.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(e.submit_commands(&seek, 1) == 1);
        AUREA_CHECK(e.render_offscreen(target, 96, 54).ok());
        AUREA_CHECK(e.gpu()->read_texture(target, half.data(), 96 * 8).ok());
        Row r{0, 0};
        f32 sum = 0, n = 0;
        for (u32 x = 0; x < 96; ++x) {
            const f32 v = srgb_encode(half_to_float(half[(27 * 96 + x) * 4])) * 219.0f + 16.0f;
            if (v > 215.0f || v < 40.0f) { ++r.full; sum += static_cast<f32>(x); n += 1; }
        }
        r.center = n > 0 ? sum / n : -1;
        return r;
    };
    const Row a = row(100);                                  // fonte 50 (quadrado em x = 28)
    const Row b = row(102);                                  // fonte 51 (x = 36)
    AUREA_CHECK(e.set_frame_blend(*layer, 1));
    const Row mix = row(101);                                // fonte 50,5
    AUREA_CHECK(e.set_frame_blend(*layer, 2));
    const Row flow = row(101);
    std::printf("    optical flow: quadros %u px cheios em %.1f / %u em %.1f; mistura %u px cheios; movimento %u px cheios em %.1f (esperado %.1f)\n",
                a.full, a.center, b.full, b.center, mix.full, flow.full, flow.center, (a.center + b.center) * 0.5f);
    AUREA_CHECK(std::fabs(a.center - static_cast<f32>(fast_square_x(50))) < 1.0f);
    AUREA_CHECK(mix.full < a.full / 2);                       // mistura: dois fantasmas em meio-tom
    AUREA_CHECK(flow.full >= a.full * 7 / 10);                // movimento: um quadrado nítido
    AUREA_CHECK(std::fabs(flow.center - (a.center + b.center) * 0.5f) < 1.5f);
    e.gpu()->destroy_texture(target);
    e.shutdown();
}

AUREA_TEST(Gpu, TimeRemapGraphEditsPointsAndTheVideoFollows) {
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
    AUREA_CHECK(e.new_project(96, 54, 30.0, "remap").ok());
    VideoImport imp;
    imp.sourcePath = "sintetico";
    imp.displayName = "clipe";
    auto layer = e.import_video(imp);
    AUREA_CHECK(layer.ok());
    if (!layer.ok()) { e.shutdown(); return; }
    TextureDesc d;
    d.width = 96;
    d.height = 54;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    const TextureHandle target = *e.gpu()->create_texture(d);
    std::vector<u16> half(96 * 54 * 4);
    auto shown = [&](i64 frame) {
        Command seek;
        seek.type = CommandType::PlaybackSeek;
        seek.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(e.submit_commands(&seek, 1) == 1);
        AUREA_CHECK(e.render_offscreen(target, 96, 54).ok());
        AUREA_CHECK(e.gpu()->read_texture(target, half.data(), 96 * 8).ok());
        return srgb_encode(half_to_float(half[(27 * 96 + 48) * 4])) * 219.0f + 16.0f;
    };
    AUREA_CHECK(e.set_time_remap(*layer, true));
    f32 q[5 + 7 * 16];
    AUREA_CHECK(e.query_time_remap(*layer, q, 5 + 7 * 16) == 5 + 7 * 2);
    const f32 before = shown(150);
    // Inserir no meio: a curva não muda.
    const i32 mid = e.edit_time_remap_key(*layer, -1, 150, 0.0f, -1);
    const u32 floats = e.query_time_remap(*layer, q, 5 + 7 * 16);
    const f32 inserted = shown(150);
    // Mover o ponto do meio para o quadro 40 da fonte: no 150 da tela aparece o 40.
    AUREA_CHECK(e.edit_time_remap_key(*layer, mid, 150, 40.0f, -1) == mid);
    const f32 moved = shown(150);
    const f32 halfway = shown(75);                                  // 0 → 40 em 150 quadros: fonte 20
    // Congelar (Hold) do início até o ponto: tudo mostra o quadro 0.
    AUREA_CHECK(e.edit_time_remap_key(*layer, 0, 0, 0.0f, static_cast<i32>(Interpolation::Hold)) == 0);
    const f32 frozen = shown(100);
    // Apagar o ponto do meio: volta à reta original.
    AUREA_CHECK(e.edit_time_remap_key(*layer, 0, 0, 0.0f, static_cast<i32>(Interpolation::Linear)) == 0);
    AUREA_CHECK(e.remove_time_remap_key(*layer, static_cast<u32>(mid)));
    const f32 removed = shown(150);
    AUREA_CHECK(!e.remove_time_remap_key(*layer, 0));                // ficam pelo menos dois
    std::printf("    curva de tempo: 150 -> %.1f; inserido %.1f; ponto em 40 -> %.1f (75 = %.1f); congelado %.1f; apagado %.1f; floats %u\n",
                before, inserted, moved, halfway, frozen, removed, floats);
    AUREA_CHECK_NEAR(before, frame_gray_code(150), 0.5);
    AUREA_CHECK(mid == 1 && floats == 5 + 7 * 3);
    AUREA_CHECK_NEAR(inserted, frame_gray_code(150), 0.5);
    AUREA_CHECK_NEAR(moved, frame_gray_code(40), 0.5);
    AUREA_CHECK_NEAR(halfway, frame_gray_code(20), 0.5);
    AUREA_CHECK_NEAR(frozen, frame_gray_code(0), 0.5);
    AUREA_CHECK_NEAR(removed, frame_gray_code(150), 0.5);
    e.gpu()->destroy_texture(target);
    e.shutdown();
}

AUREA_TEST(Gpu, VectorBlurSmearsFootageMotionAndFlowIsCached) {
    AUREA_REQUIRE_GPU();
    SyntheticConfig cfg;
    cfg.width = 96;
    cfg.height = 54;
    cfg.frameCount = 300;
    cfg.pattern = SyntheticPattern::FastSquare;
    SyntheticFactory factory(cfg);
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.framesInFlight = 2;
    ec.mediaFactory = &factory;
    ec.workerCount = 2;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(96, 54, 30.0, "vetor").ok());
    VideoImport imp;
    imp.sourcePath = "sintetico";
    imp.displayName = "clipe";
    auto layer = e.import_video(imp);
    AUREA_CHECK(layer.ok());
    if (!layer.ok()) { e.shutdown(); return; }
    TextureDesc d;
    d.width = 96;
    d.height = 54;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    const TextureHandle target = *e.gpu()->create_texture(d);
    std::vector<u16> half(96 * 54 * 4);
    // Linha e coluna do meio: meio-tons na horizontal (o quadrado corre em x)
    // e na vertical (não corre em y).
    auto measure = [&](i64 frame, u32& halfX, u32& halfY) {
        Command seek;
        seek.type = CommandType::PlaybackSeek;
        seek.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(e.submit_commands(&seek, 1) == 1);
        AUREA_CHECK(e.render_offscreen(target, 96, 54).ok());
        AUREA_CHECK(e.gpu()->read_texture(target, half.data(), 96 * 8).ok());
        auto code = [&](u32 x, u32 y) { return srgb_encode(half_to_float(half[(y * 96 + x) * 4])) * 219.0f + 16.0f; };
        halfX = halfY = 0;
        const u32 cx = static_cast<u32>(fast_square_x(frame));
        // Borda de fora do quadrado (fundo ~100..130 contra xadrez 20/235): meio-tom = nem fundo nem xadrez.
        for (u32 x = 0; x < 96; ++x) { const f32 v = code(x, 17 + 3); if (v > 45.0f && v < 90.0f) ++halfX; }
        for (u32 y = 0; y < 54; ++y) { const f32 v = code(cx - 8, y); if (v > 45.0f && v < 90.0f) ++halfY; }
    };
    u32 sx = 0, sy = 0, bx = 0, by = 0;
    measure(50, sx, sy);
    AUREA_CHECK(e.set_vector_blur(*layer, 1.0f));
    u32 h0 = 0, m0 = 0;
    e.flow_cache_stats(h0, m0);
    measure(50, bx, by);
    u32 bx2 = 0, by2 = 0;
    measure(50, bx2, by2);                      // o mesmo quadro de novo: fluxo do cache
    u32 h1 = 0, m1 = 0;
    e.flow_cache_stats(h1, m1);
    std::printf("    desfoque vetorial: meio-tons em x %u -> %u (y %u -> %u); repetido x %u; cache do fluxo +%u acertos, +%u calculos\n", sx, bx, sy, by,
                bx2, h1 - h0, m1 - m0);
    AUREA_CHECK(bx >= sx + 4);                  // 8 px/quadro × 180° = rastro de ~4 px em cada borda vertical
    AUREA_CHECK(by <= sy + 2);                  // nada na vertical
    AUREA_CHECK(bx2 == bx);
    AUREA_CHECK(h1 > h0);
    e.gpu()->destroy_texture(target);
    e.shutdown();
}

AUREA_TEST(Gpu, ThermalReducesPreviewButNotExport) {
    AUREA_REQUIRE_GPU();
    // Optical flow no preview: aparelho crítico = mistura (mais barata); o
    // export (render sem "preview") mantém o movimento de pixels.
    SyntheticConfig cfg;
    cfg.width = 96;
    cfg.height = 54;
    cfg.frameCount = 300;
    cfg.pattern = SyntheticPattern::FastSquare;
    SyntheticFactory factory(cfg);
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.framesInFlight = 2;
    ec.mediaFactory = &factory;
    ec.workerCount = 2;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(96, 54, 30.0, "termico").ok());
    VideoImport imp;
    imp.sourcePath = "sintetico";
    imp.displayName = "clipe";
    auto layer = e.import_video(imp);
    AUREA_CHECK(layer.ok());
    if (!layer.ok()) { e.shutdown(); return; }
    {
        Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
        Layer* l = c->layer(LayerId::unpack(*layer));
        l->speed = 0.5f;
        l->end = FrameIndex{l->start.value + 400};
        c->set_duration(FrameIndex{400});
    }
    AUREA_CHECK(e.set_frame_blend(*layer, 2));
    TextureDesc d;
    d.width = 96;
    d.height = 54;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    const TextureHandle target = *e.gpu()->create_texture(d);
    std::vector<u16> half(96 * 54 * 4);
    auto sharp = [&](bool preview) {
        Command seek;
        seek.type = CommandType::PlaybackSeek;
        seek.seek.time = tick_at(FrameIndex{101}, 30.0);
        AUREA_CHECK(e.submit_commands(&seek, 1) == 1);
        AUREA_CHECK(e.render_offscreen(target, 96, 54, preview).ok());
        AUREA_CHECK(e.gpu()->read_texture(target, half.data(), 96 * 8).ok());
        u32 full = 0;
        for (u32 x = 0; x < 96; ++x) {
            const f32 v = srgb_encode(half_to_float(half[(27 * 96 + x) * 4])) * 219.0f + 16.0f;
            if (v > 215.0f || v < 40.0f) ++full;
        }
        return full;
    };
    const u32 cool = sharp(true);
    e.set_thermal(3, true);                        // crítico
    const f32 scale = e.preview_heavy_scale();
    const u32 hotPreview = sharp(true);
    const u32 hotExport = sharp(false);
    e.set_thermal(0, false);
    std::printf("    termico: frio %u px nitidos; critico (escala %.2f) preview %u px (mistura), export %u px (movimento)\n", cool, scale, hotPreview,
                hotExport);
    AUREA_CHECK(cool >= 18);
    AUREA_CHECK(scale <= 0.25f);
    AUREA_CHECK(hotPreview < cool / 2);
    AUREA_CHECK(hotExport >= 18);
    e.gpu()->destroy_texture(target);
    e.shutdown();
}

// -----------------------------------------------------------------------------
// Benchmarks (item 109): só com AUREA_BENCH=1 (a suíte normal não paga o custo).
// Tempo de parede por quadro no host; os quadros de vídeo já decodificados
// (o tempo do decoder sintético não entra) e o cache do fluxo desligado.
// -----------------------------------------------------------------------------
namespace {
bool bench_enabled() { const char* v = std::getenv("AUREA_BENCH"); return v && *v == '1'; }

f64 bench_video(u32 w, u32 h, f32 speed, u32 blendMode, u32 frames, bool exportQuality) {
    SyntheticConfig cfg;
    cfg.width = w;
    cfg.height = h;
    cfg.frameCount = 300;
    cfg.pattern = SyntheticPattern::FastSquare;
    SyntheticFactory factory(cfg);
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.framesInFlight = 2;
    ec.mediaFactory = &factory;
    ec.workerCount = 2;
    ec.disableAutosave = true;
    ec.memoryBudgetBytes = 1024ull << 20;
    if (!e.initialize(ec).ok() || !e.new_project(w, h, 30.0, "bench").ok()) return -1;
    VideoImport imp;
    imp.sourcePath = "sintetico";
    auto layer = e.import_video(imp);
    if (!layer.ok()) { e.shutdown(); return -1; }
    {
        Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
        Layer* l = c->layer(LayerId::unpack(*layer));
        l->speed = speed;
        l->end = FrameIndex{l->start.value + 1000};
        c->set_duration(FrameIndex{1000});
    }
    (void)e.set_frame_blend(*layer, blendMode);
    e.set_flow_cache_enabled(false);
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    const TextureHandle target = *e.gpu()->create_texture(d);
    f64 total = 0;
    for (u32 i = 0; i < frames; ++i) {
        Command seek;
        seek.type = CommandType::PlaybackSeek;
        seek.seek.time = tick_at(FrameIndex{static_cast<i64>(11 + i * 2 + 1)}, 30.0);   // entre dois quadros da fonte
        e.submit_commands(&seek, 1);
        (void)e.render_offscreen(target, w, h, !exportQuality);   // aquece: decodifica os dois quadros
        const auto t0 = std::chrono::steady_clock::now();
        (void)e.render_offscreen(target, w, h, !exportQuality);
        total += std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    e.gpu()->destroy_texture(target);
    e.shutdown();
    return total / frames;
}
} // namespace

AUREA_TEST(Gpu, BenchTemporalAndParticles) {
    AUREA_REQUIRE_GPU();
    if (!bench_enabled()) { std::printf("    (pulado: AUREA_BENCH=1 para medir)\n"); return; }
    const f64 a = bench_video(1920, 1080, 0.5f, 1, 12, false);
    const f64 b = bench_video(1920, 1080, 0.2f, 2, 12, false);
    const f64 c = bench_video(3840, 2160, 0.5f, 2, 6, true);
    std::printf("    Temporal A 1080p 50%% mistura: %.2f ms/quadro (%.0f fps)\n", a, 1000.0 / a);
    std::printf("    Temporal B 1080p 20%% optical flow: %.2f ms/quadro (%.0f fps)\n", b, 1000.0 / b);
    std::printf("    Temporal C 4K optical flow (export): %.2f ms/quadro\n", c);
    for (u32 count : {100000u, 500000u, 1000000u}) {
        Scene3DRig rig(1920, 1080);
        auto p = rig.e.add_particles(0);
        if (!p.ok()) continue;
        Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        Layer* l = comp->layer(LayerId::unpack(*p));
        l->particles.maxParticles = count;
        l->particles.lifetime = 4.0f;
        l->particles.rate = static_cast<f32>(count) / 4.0f;
        l->particles.startSize = 3.0f;
        l->particles.endSize = 1.0f;
        TextureDesc d;
        d.width = 1920;
        d.height = 1080;
        d.format = SurfaceFormat::RGBA16F;
        d.renderTarget = true;
        d.transferSrc = true;
        const TextureHandle target = *rig.e.gpu()->create_texture(d);
        f64 total = 0;
        for (i64 f = 150; f < 160; ++f) {
            Command seek;
            seek.type = CommandType::PlaybackSeek;
            seek.seek.time = tick_at(FrameIndex{f}, 30.0);
            rig.e.submit_commands(&seek, 1);
            const auto t0 = std::chrono::steady_clock::now();
            (void)rig.e.render_offscreen(target, 1920, 1080);
            total += std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        rig.e.gpu()->destroy_texture(target);
        std::printf("    Particulas %uk (1080p): %.2f ms/quadro (%.0f fps)\n", count / 1000, total / 10, 10000.0 / total);
    }
}

AUREA_TEST(Gpu, TwoDLayersInSceneOccludeAndAreOccludedByModels) {
    AUREA_REQUIRE_GPU();
    const std::string path = gltf_data("Box.glb");
    if (!file_exists(path)) return;
    // Forma azul ACIMA do modelo na pilha. Atrás dele em profundidade = o
    // modelo tapa; na frente = ela tapa; inclinada atravessando = metade de cada.
    auto run = [&](f32 z, f32 rotY, Image8& out, f32 k = 2.5f) {
        Scene3DRig rig(320, 180);
        ModelImport mi;
        mi.path = path;
        auto m = rig.e.import_model(mi);
        auto s = rig.e.add_shape(10);
        AUREA_CHECK(m.ok() && s.ok());
        Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        Layer* sh = comp->layer(LayerId::unpack(*s));
        sh->shape.fillColor = Vec4{0, 0.3f, 1, 1};
        sh->transform.position = Vec3{160, 90, z};
        sh->transform.rotation = Vec3{0, rotY, 0};
        sh->transform.scale = Vec3{k, k, 1};
        out = rig.capture(320);
    };
    auto redAt = [](const Image8& img, u32 x, u32 y) { const u8* p = img.at(x, y); return p[2] > 150 && p[0] < 80; };   // azul da forma
    Image8 behind, front, cross;
    run(150.0f, 0.0f, behind, 8.0f);   // câmera padrão em z = −216; a caixa vai de ~−50 a +50
    run(-100.0f, 0.0f, front);
    run(0.0f, 60.0f, cross);
    // Coluna do meio: quantos pixels vermelhos (a forma cobre o centro nos três casos).
    auto redCount = [&](const Image8& img) { u32 n = 0; for (u32 x = 0; x < 320; ++x) n += redAt(img, x, 90) ? 1 : 0; return n; };
    const u32 rb = redCount(behind), rf = redCount(front), rc = redCount(cross);
    const bool centerHidden = !redAt(behind, 160, 90);
    const bool centerShown = redAt(front, 160, 90);
    std::printf("    camada 2D na cena: atras do modelo %u px azuis (centro escondido %d); na frente %u (centro visivel %d); atravessando %u\n", rb,
                centerHidden ? 1 : 0, rf, centerShown ? 1 : 0, rc);
    AUREA_CHECK(centerHidden);
    AUREA_CHECK(centerShown);
    AUREA_CHECK(rf > rb && rb > 0);   // atrás: só as bordas aparecem em volta da caixa
    AUREA_CHECK(rc > 0 && rc < rf);   // atravessando: parte some dentro do modelo
}



AUREA_TEST(Gpu, ZoomingANullZoomsItsModelUniformly) {
    AUREA_REQUIRE_GPU();
    const std::string path = gltf_data("DamagedHelmet.glb");
    if (!file_exists(path)) return;
    // Nulo com escala 2 (a UI só mexe em X/Y) = o próprio modelo com escala 2:
    // zoom de verdade, sem achatar a profundidade.
    Image8 viaNull, direct;
    {
        Scene3DRig rig(320, 180);
        ModelImport mi;
        mi.path = path;
        auto m = rig.e.import_model(mi);
        auto n = rig.e.add_null(false);
        AUREA_CHECK(m.ok() && n.ok());
        Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        Layer* model = comp->layer(LayerId::unpack(*m));
        model->transform.rotation = Vec3{0, 35, 0};
        set_parent(rig.e, *m, *n);
        comp->layer(LayerId::unpack(*n))->transform.scale = Vec3{1.6f, 1.6f, 1};
        viaNull = rig.capture(320);
    }
    {
        Scene3DRig rig(320, 180);
        ModelImport mi;
        mi.path = path;
        auto m = rig.e.import_model(mi);
        AUREA_CHECK(m.ok());
        Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        Layer* model = comp->layer(LayerId::unpack(*m));
        model->transform.rotation = Vec3{0, 35, 0};
        model->transform.scale = Vec3{1.6f, 1.6f, 1};
        direct = rig.capture(320);
    }
    f64 sum = 0;
    for (usize i = 0; i < viaNull.rgba.size() && i < direct.rgba.size(); ++i) sum += std::abs(viaNull.rgba[i] - direct.rgba[i]);
    const f64 mean = sum / static_cast<f64>(std::max<usize>(1, viaNull.rgba.size()));
    std::printf("    zoom pelo nulo vs escala do modelo: diferenca media %.3f, maxima %u\n", mean, max_diff(viaNull, direct));
    AUREA_CHECK(mean < 0.5);
}

AUREA_TEST(Gpu, GpuTextIsSharpScalesAndStrokes) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(640, 360);
    auto t = rig.e.add_text("Aurea Glifo");
    AUREA_CHECK(t.ok());
    if (!t.ok()) return;
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    Layer* l = comp->layer(LayerId::unpack(*t));
    l->text.size = 80;
    l->text.color = Vec4{1, 1, 1, 1};
    const Image8 plain = rig.capture(640);
    l->text.strokeWidth = 4;
    l->text.strokeColor = Vec4{1, 0, 0, 1};
    const Image8 stroked = rig.capture(640);
    l->text.strokeWidth = 0;
    l->transform.scale = Vec3{3, 3, 1};           // ampliado: o SDF continua nítido (sem rasterizar de novo)
    const Image8 big = rig.capture(640);
    auto count = [](const Image8& img, auto pred) { u32 n = 0; for (usize i = 0; i + 3 < img.rgba.size(); i += 4) n += pred(&img.rgba[i]) ? 1u : 0u; return n; };
    const u32 white = count(plain, [](const u8* p) { return p[0] > 200 && p[1] > 200 && p[2] > 200; });
    const u32 red = count(stroked, [](const u8* p) { return p[0] > 200 && p[1] < 60 && p[2] < 60; });
    // Borda ampliada: pixels de transição (nem fundo nem branco) por pixel de borda — nítido = poucos.
    const u32 edgeBig = count(big, [](const u8* p) { return p[0] > 30 && p[0] < 225; });
    const u32 fullBig = count(big, [](const u8* p) { return p[0] >= 225; });
    std::printf("    texto GPU: %u px brancos; contorno %u px vermelhos; ampliado 3x: %u cheios, %u de transicao (%.2f)\n", white, red, fullBig,
                edgeBig, static_cast<f64>(edgeBig) / std::max(1u, fullBig));
    AUREA_CHECK(white > 3000);
    AUREA_CHECK(red > 1500);
    AUREA_CHECK(fullBig > white * 4);
    AUREA_CHECK(edgeBig * 5 < fullBig);
}

AUREA_TEST(Gpu, TextBackgroundAndShadowRender) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(640, 360);
    auto t = rig.e.add_text("Legenda");
    AUREA_CHECK(t.ok());
    if (!t.ok()) return;
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    Layer* l = comp->layer(LayerId::unpack(*t));
    l->text.size = 70;
    const Image8 plain = rig.capture(640);
    l->text.background = true;
    l->text.backgroundColor = Vec4{0.1f, 0.2f, 0.9f, 1};
    const Image8 bg = rig.capture(640);
    l->text.background = false;
    l->text.shadow = true;
    l->text.shadowColor = Vec4{1, 0, 0, 1};
    l->text.shadowOffset = Vec2{8, 8};
    const Image8 sh = rig.capture(640);
    auto count = [](const Image8& img, auto pred) { u32 n = 0; for (usize i = 0; i + 3 < img.rgba.size(); i += 4) n += pred(&img.rgba[i]) ? 1u : 0u; return n; };
    const u32 blue = count(bg, [](const u8* p) { return p[2] > 150 && p[0] < 80; });
    const u32 red = count(sh, [](const u8* p) { return p[0] > 150 && p[1] < 80; });
    const u32 whitePlain = count(plain, [](const u8* p) { return p[0] > 200 && p[1] > 200 && p[2] > 200; });
    const u32 whiteShadow = count(sh, [](const u8* p) { return p[0] > 200 && p[1] > 200 && p[2] > 200; });
    std::printf("    fundo %u px azuis; sombra %u px vermelhos; texto branco %u -> %u com sombra (por cima)\n", blue, red, whitePlain, whiteShadow);
    AUREA_CHECK(blue > 10000);
    AUREA_CHECK(red > 1000);
    AUREA_CHECK(whiteShadow * 10 > whitePlain * 8);   // a sombra fica ATRÁS do texto
}

AUREA_TEST(Gpu, TextAnimatorTypewriterPerCharRotationAndMotionBlur) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(640, 360);
    auto tid = rig.e.add_text("Texto Aurea");
    AUREA_CHECK(tid.ok());
    auto seek = [&](i64 f) {
        Command s;
        s.type = CommandType::PlaybackSeek;
        s.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(rig.e.apply_command(s).ok());
    };
    const Box8 full = lit_box(rig.capture(640));
    const f32 cov0 = coverage(rig.capture(640));
    // Typewriter (preset 8): nada no começo, metade no meio, tudo no fim.
    AUREA_CHECK(rig.e.apply_text_preset(*tid, 8));
    seek(0);
    const f32 c0 = coverage(rig.capture(640));
    seek(15);
    const Image8 mid = rig.capture(640);
    const f32 c15 = coverage(mid);
    const Box8 bmid = lit_box(mid);
    seek(40);
    const f32 c40 = coverage(rig.capture(640));
    std::printf("    typewriter: cobertura %.4f -> %.4f -> %.4f (texto %.4f), meio x %u..%u de %u..%u\n", c0, c15, c40, cov0, bmid.x0, bmid.x1,
                full.x0, full.x1);
    AUREA_CHECK(c0 < cov0 * 0.05f);
    AUREA_CHECK(c15 > cov0 * 0.25f && c15 < cov0 * 0.75f);
    AUREA_CHECK(std::fabs(c40 - cov0) < cov0 * 0.02f);
    AUREA_CHECK(bmid.x0 <= full.x0 + 2 && bmid.x1 < full.x1 - 40);   // revela da esquerda

    // 3D por caractere: rotação X de 60° achata a altura das letras (~cos 60°).
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    Layer* l = comp->layer(LayerId::unpack(*tid));
    l->tracks.remove_if([](const Track& t) { return t.property == TrackProperty::TextAnimParam; });
    l->text.animators.clear();
    const i32 ai = rig.e.add_text_animator(*tid, kTextPropRotation);
    AUREA_CHECK_EQ(ai, 0);
    AUREA_CHECK(rig.e.set_text_anim_param(*tid, 0, text::kRotX, 60.0f));
    const Box8 rx = lit_box(rig.capture(640));
    std::printf("    rotacao X 60: altura %u de %u, largura %u de %u\n", rx.h(), full.h(), rx.w(), full.w());
    AUREA_CHECK(rx.h() < full.h() * 0.7f && rx.h() > full.h() * 0.35f);
    AUREA_CHECK(rx.w() + 6 >= full.w());

    // Desfoque de movimento POR LETRA: posição X animada 20 px/quadro.
    AUREA_CHECK(rig.e.remove_text_animator(*tid, 0));
    AUREA_CHECK(rig.e.add_text_animator(*tid, kTextPropPosition) == 0);
    AUREA_CHECK(rig.e.set_text_anim_param(*tid, 0, text::kPosX, 100.0f));
    const Box8 st100 = lit_box(rig.capture(640));
    AUREA_CHECK(st100.x0 >= full.x0 + 98 && st100.x0 <= full.x0 + 102);   // margem maior, texto no lugar
    AUREA_CHECK(rig.e.set_text_anim_param(*tid, 0, text::kPosX, 0.0f));
    seek(10);
    AUREA_CHECK(rig.e.toggle_text_anim_key(*tid, 0, text::kPosX));   // 0 px no quadro 10
    seek(20);
    AUREA_CHECK(rig.e.set_text_anim_param(*tid, 0, text::kPosX, 200.0f));
    // Linear para medir o rastro.
    if (Track* px = l->tracks.find(TrackProperty::TextAnimParam, 0, text::kPosX)) {
        AUREA_CHECK_EQ(px->keys.size(), 2u);
        for (const i64 f : {10, 20}) px->set_interpolation(FrameIndex{f}, Interpolation::Linear, 0, 0, 1, 1);
    }
    seek(15);
    const Box8 sharp = lit_box(rig.capture(640));
    AUREA_CHECK(rig.e.set_motion_blur(*tid, true));
    const Box8 blur = lit_box(rig.capture(640));
    std::printf("    desfoque por letra: %u -> %u px de largura\n", sharp.w(), blur.w());
    AUREA_CHECK(sharp.x0 >= full.x0 + 90 && sharp.x0 <= full.x0 + 110);   // meio caminho (100 px)
    AUREA_CHECK(blur.w() >= sharp.w() + 6 && blur.w() <= sharp.w() + 14);

    // Salvar e reabrir: animadores e keyframes voltam iguais.
    const Image8 before = rig.capture(640);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_animtexto.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    seek(15);
    const u32 reopened = max_diff(before, rig.capture(640));
    std::printf("    reaberto dif %u\n", reopened);
    AUREA_CHECK(reopened <= 3);
    std::remove(path.c_str());
}

// =============================================================================
// 7H — modos de mistura, camada de ajuste, guia e solo
// =============================================================================
namespace {

/// Referência INDEPENDENTE do shader (W3C Compositing and Blending), em cor
/// linear reta. Mesmas regras de faixa do blend.frag: os modos que supõem
/// [0,1] recebem as cores presas; luminância Rec.709.
struct BlendRef {
    static f32 lum(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
    static Vec3 clip(Vec3 c) {
        const f32 l = lum(c);
        const f32 n = std::min({c.x, c.y, c.z}), x = std::max({c.x, c.y, c.z});
        if (n < 0.0f) c = Vec3{l + (c.x - l) * l / (l - n), l + (c.y - l) * l / (l - n), l + (c.z - l) * l / (l - n)};
        if (x > 1.0f) c = Vec3{l + (c.x - l) * (1 - l) / (x - l), l + (c.y - l) * (1 - l) / (x - l), l + (c.z - l) * (1 - l) / (x - l)};
        return c;
    }
    static Vec3 set_lum(Vec3 c, f32 l) { const f32 d = l - lum(c); return clip(Vec3{c.x + d, c.y + d, c.z + d}); }
    static f32 sat(Vec3 c) { return std::max({c.x, c.y, c.z}) - std::min({c.x, c.y, c.z}); }
    static Vec3 set_sat(Vec3 c, f32 s) {
        const f32 mx = std::max({c.x, c.y, c.z}), mn = std::min({c.x, c.y, c.z});
        if (mx - mn <= 1e-6f) return Vec3{0, 0, 0};
        const f32 k = s / (mx - mn);
        return Vec3{(c.x - mn) * k, (c.y - mn) * k, (c.z - mn) * k};
    }
    static f32 ch(BlendMode m, f32 b, f32 s) {
        switch (m) {
            case BlendMode::Subtract: return std::max(b - s, 0.0f);
            case BlendMode::Multiply: return b * s;
            case BlendMode::Darken: return std::min(b, s);
            case BlendMode::Lighten: return std::max(b, s);
            case BlendMode::Difference: return std::fabs(b - s);
            default: break;
        }
        b = std::clamp(b, 0.0f, 1.0f);
        s = std::clamp(s, 0.0f, 1.0f);
        auto hard = [](f32 bb, f32 ss) { return ss <= 0.5f ? bb * 2 * ss : bb + (2 * ss - 1) - bb * (2 * ss - 1); };
        switch (m) {
            case BlendMode::Screen: return b + s - b * s;
            case BlendMode::Overlay: return hard(s, b);
            case BlendMode::HardLight: return hard(b, s);
            case BlendMode::ColorDodge: return b <= 0 ? 0.0f : s >= 1 ? 1.0f : std::min(1.0f, b / (1 - s));
            case BlendMode::ColorBurn: return b >= 1 ? 1.0f : s <= 0 ? 0.0f : 1 - std::min(1.0f, (1 - b) / s);
            case BlendMode::SoftLight: {
                if (s <= 0.5f) return b - (1 - 2 * s) * b * (1 - b);
                const f32 d = b <= 0.25f ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
                return b + (2 * s - 1) * (d - b);
            }
            case BlendMode::Exclusion: return b + s - 2 * b * s;
            default: return s;
        }
    }
    static Vec3 mix(BlendMode m, Vec3 b, Vec3 s) {
        auto c01 = [](Vec3 v) { return Vec3{std::clamp(v.x, 0.0f, 1.0f), std::clamp(v.y, 0.0f, 1.0f), std::clamp(v.z, 0.0f, 1.0f)}; };
        switch (m) {
            case BlendMode::Hue: return set_lum(set_sat(c01(s), sat(c01(b))), lum(c01(b)));
            case BlendMode::Saturation: return set_lum(set_sat(c01(b), sat(c01(s))), lum(c01(b)));
            case BlendMode::Color: return set_lum(c01(s), lum(c01(b)));
            case BlendMode::Luminosity: return set_lum(c01(b), lum(c01(s)));
            default: return Vec3{ch(m, b.x, s.x), ch(m, b.y, s.y), ch(m, b.z, s.z)};
        }
    }
};

Vec3 lin3(f32 r, f32 g, f32 b) { return Vec3{srgb_decode(r), srgb_decode(g), srgb_decode(b)}; }

const char* blend_name(BlendMode m) {
    static const char* const k[] = {"Normal", "Add", "Subtract", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
                                    "ColorDodge", "ColorBurn", "HardLight", "SoftLight", "Difference", "Exclusion",
                                    "Hue", "Saturation", "Color", "Luminosity"};
    return k[static_cast<u16>(m)];
}

} // namespace

AUREA_TEST(Gpu, EveryBlendModeMatchesTheFormulaPixelForPixel) {
    AUREA_REQUIRE_GPU();
    // Dois pares de cor: o primeiro passa pelos dois ramos de HardLight/Overlay
    // e SoftLight (canais acima e abaixo de 0,5 / 0,25); o segundo tem fonte
    // clara para Dodge/Burn/Screen perto dos limites.
    const f32 pairs[2][6] = {{0.80f, 0.35f, 0.10f, 0.30f, 0.70f, 0.90f},
                             {0.25f, 0.55f, 0.95f, 0.85f, 0.20f, 0.60f}};
    const f32 opacity = 0.6f;
    const f32 tol = 3.0f / 255.0f;
    f32 worst = 0.0f;
    BlendMode worstMode = BlendMode::Normal;
    for (const auto& pc : pairs) {
        const Vec3 cb = lin3(pc[0], pc[1], pc[2]);
        const Vec3 cs = lin3(pc[3], pc[4], pc[5]);
        for (u16 mi = 0; mi <= static_cast<u16>(BlendMode::Luminosity); ++mi) {
            const BlendMode m = static_cast<BlendMode>(mi);
            Scene s(64, 64);
            s.solid(64, 64, Vec4{pc[0], pc[1], pc[2], 1}, 32, 32);
            // Camada de cima só no quadrado central (32×32): o canto mostra que
            // o fundo fora dela sai intacto no passe de ping-pong.
            const LayerId top = s.solid(32, 32, Vec4{pc[3], pc[4], pc[5], 1}, 32, 32);
            s.comp->layer(top)->blendMode = m;
            s.comp->layer(top)->transform.opacity = opacity;
            const FloatImage img = s.render();
            Vec3 expect;
            if (m == BlendMode::Normal) {
                expect = Vec3{cs.x * opacity + cb.x * (1 - opacity), cs.y * opacity + cb.y * (1 - opacity), cs.z * opacity + cb.z * (1 - opacity)};
            } else if (m == BlendMode::Add) {
                expect = Vec3{cb.x + cs.x * opacity, cb.y + cs.y * opacity, cb.z + cs.z * opacity};   // B = cb + cs, alfa continua 1
            } else {
                // Fundo opaco (ab = 1): co = as·B + (1 − as)·cb.
                const Vec3 B = BlendRef::mix(m, cb, cs);
                expect = Vec3{opacity * B.x + (1 - opacity) * cb.x, opacity * B.y + (1 - opacity) * cb.y, opacity * B.z + (1 - opacity) * cb.z};
            }
            const Vec4 got = img.v(32, 32);
            const f32 err = std::max({std::fabs(got.x - expect.x), std::fabs(got.y - expect.y), std::fabs(got.z - expect.z), std::fabs(got.w - 1.0f)});
            if (err > worst) { worst = err; worstMode = m; }
            AUREA_CHECK_MSG(err <= tol, blend_name(m));
            if (err > tol) {
                std::printf("\n    %s: obtido (%.4f %.4f %.4f) esperado (%.4f %.4f %.4f)", blend_name(m), got.x, got.y, got.z,
                            expect.x, expect.y, expect.z);
            }
            const Vec4 corner = img.v(4, 4);
            AUREA_CHECK_MSG(near4(corner, Vec4{cb.x, cb.y, cb.z, 1}, tol), blend_name(m));
        }
    }
    std::printf("    18 modos x 2 pares, pior erro %.5f (%.2f/255) em %s ", worst, worst * 255.0f, blend_name(worstMode));
}

AUREA_TEST(Gpu, BlendModesChainAndLandInTheOffscreenTarget) {
    AUREA_REQUIRE_GPU();
    // Três modos seguidos (ping-pong duas vezes) com uma Normal entre eles e
    // outra no fim: a ordem da pilha é respeitada e o último passe cai no alvo.
    Scene s(32, 32);
    s.solid(32, 32, Vec4{0.5f, 0.5f, 0.5f, 1}, 16, 16);
    const LayerId a = s.solid(32, 32, Vec4{0.8f, 0.4f, 0.2f, 1}, 16, 16);
    s.comp->layer(a)->blendMode = BlendMode::Multiply;
    const LayerId b = s.solid(32, 32, Vec4{0.3f, 0.3f, 0.3f, 1}, 16, 16);
    s.comp->layer(b)->blendMode = BlendMode::Screen;
    const LayerId n = s.solid(8, 8, Vec4{0, 1, 0, 1}, 4, 4);   // Normal no canto
    (void)n;
    const LayerId d = s.solid(32, 32, Vec4{0.1f, 0.9f, 0.5f, 1}, 16, 16);
    s.comp->layer(d)->blendMode = BlendMode::Difference;
    s.comp->layer(d)->transform.opacity = 0.5f;
    const FloatImage img = s.render();
    const Vec3 g = lin3(0.5f, 0.5f, 0.5f), ca = lin3(0.8f, 0.4f, 0.2f), cbb = lin3(0.3f, 0.3f, 0.3f), cd = lin3(0.1f, 0.9f, 0.5f);
    auto step = [](Vec3 base, Vec3 x, f32 op, BlendMode m) {
        const Vec3 B = BlendRef::mix(m, base, x);
        return Vec3{op * B.x + (1 - op) * base.x, op * B.y + (1 - op) * base.y, op * B.z + (1 - op) * base.z};
    };
    Vec3 e = step(g, ca, 1.0f, BlendMode::Multiply);
    e = step(e, cbb, 1.0f, BlendMode::Screen);
    const Vec3 centre = step(e, cd, 0.5f, BlendMode::Difference);
    const Vec3 corner = step(Vec3{0, 1, 0}, cd, 0.5f, BlendMode::Difference);
    AUREA_CHECK(near4(img.v(16, 16), Vec4{centre.x, centre.y, centre.z, 1}, 3.0f / 255.0f));
    AUREA_CHECK(near4(img.v(2, 2), Vec4{corner.x, corner.y, corner.z, 1}, 3.0f / 255.0f));
}

AUREA_TEST(Gpu, AdjustmentLayerAffectsOnlyTheLayersBelow) {
    AUREA_REQUIRE_GPU();
    const Vec3 a = lin3(0.4f, 0.3f, 0.2f), b = lin3(0.2f, 0.6f, 0.3f);
    auto build = [&](Scene& s, f32 opacity) {
        s.solid(64, 64, Vec4{0.4f, 0.3f, 0.2f, 1}, 32, 32);                 // abaixo
        const LayerId adj = s.solid(8, 8, Vec4{1, 0, 1, 1}, 4, 4);          // o conteúdo NÃO aparece
        s.comp->layer(adj)->adjustment = true;
        s.comp->layer(adj)->transform.opacity = opacity;
        s.add_effect(adj, effect_keys::kExposure).params[0].constant.v[0] = 1.0f;   // ×2 linear
        s.solid(16, 16, Vec4{0.2f, 0.6f, 0.3f, 1}, 32, 32);                 // acima
        return adj;
    };
    {
        Scene s(64, 64);
        build(s, 1.0f);
        const FloatImage img = s.render();
        AUREA_CHECK(near4(img.v(60, 60), Vec4{a.x * 2, a.y * 2, a.z * 2, 1}, 0.004f));   // abaixo: exposto
        AUREA_CHECK(near4(img.v(2, 2), Vec4{a.x * 2, a.y * 2, a.z * 2, 1}, 0.004f));     // onde estaria o sólido da camada de ajuste
        AUREA_CHECK(near4(img.v(32, 32), Vec4{b.x, b.y, b.z, 1}, 0.004f));               // acima: intacta
        std::printf("    abaixo %.4f (esperado %.4f), acima %.4f (esperado %.4f) ", img.v(60, 60).x, a.x * 2, img.v(32, 32).y, b.y);
    }
    {
        // Opacidade 50 %: metade do caminho entre o fundo e o fundo exposto.
        Scene s(64, 64);
        build(s, 0.5f);
        const FloatImage img = s.render();
        AUREA_CHECK(near4(img.v(60, 60), Vec4{a.x * 1.5f, a.y * 1.5f, a.z * 1.5f, 1}, 0.004f));
    }
    {
        // Fora do trecho de tempo dela: não faz nada.
        Scene s(64, 64);
        const LayerId adj = build(s, 1.0f);
        s.comp->layer(adj)->end = FrameIndex{10};
        const FloatImage img = s.render(FrameIndex{20});
        AUREA_CHECK(near4(img.v(60, 60), Vec4{a.x, a.y, a.z, 1}, 0.004f));
    }
    {
        // Sem efeito vivo: nenhum passe, e o sólido dela não aparece.
        Scene s(64, 64);
        s.solid(64, 64, Vec4{0.4f, 0.3f, 0.2f, 1}, 32, 32);
        const LayerId adj = s.solid(64, 64, Vec4{1, 0, 1, 1}, 32, 32);
        s.comp->layer(adj)->adjustment = true;
        const FloatImage img = s.render();
        AUREA_CHECK(near4(img.v(32, 32), Vec4{a.x, a.y, a.z, 1}, 0.004f));
    }
}

AUREA_TEST(Gpu, GuideLayerShowsInPreviewButNeverInExport) {
    AUREA_REQUIRE_GPU();
    Scene s(32, 32);
    s.solid(32, 32, Vec4{0.2f, 0.2f, 0.8f, 1}, 16, 16);
    const LayerId g = s.solid(16, 16, Vec4{1, 1, 0, 1}, 16, 16);
    s.comp->layer(g)->guide = true;
    const Vec3 blue = lin3(0.2f, 0.2f, 0.8f);
    const FloatImage preview = s.render(FrameIndex{0}, 1, false);
    const FloatImage exported = s.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(near4(preview.v(16, 16), Vec4{1, 1, 0, 1}, 0.004f));
    AUREA_CHECK(near4(exported.v(16, 16), Vec4{blue.x, blue.y, blue.z, 1}, 0.004f));
}

AUREA_TEST(Gpu, SoloIsolatesLayersInPreviewOnly) {
    AUREA_REQUIRE_GPU();
    Scene s(32, 32);
    const LayerId bottom = s.solid(32, 32, Vec4{0.2f, 0.8f, 0.2f, 1}, 16, 16);
    s.solid(16, 16, Vec4{1, 0, 0, 1}, 16, 16);
    const Vec3 green = lin3(0.2f, 0.8f, 0.2f);
    AUREA_CHECK(near4(s.render().v(16, 16), Vec4{1, 0, 0, 1}, 0.004f));
    s.comp->layer(bottom)->solo = true;
    AUREA_CHECK(near4(s.render().v(16, 16), Vec4{green.x, green.y, green.z, 1}, 0.004f));   // só a de solo
    AUREA_CHECK(near4(s.render(FrameIndex{0}, 1, true).v(16, 16), Vec4{1, 0, 0, 1}, 0.004f));   // export ignora
    // Camada em solo mas oculta não isola nada.
    s.comp->layer(bottom)->visible = false;
    AUREA_CHECK(near4(s.render().v(16, 16), Vec4{1, 0, 0, 1}, 0.004f));
}

// Preset de efeitos (Fase 7F): a camada que recebe o preset renderiza IGUAL à
// camada de onde ele saiu — parâmetros, keyframes (no tempo relativo) e a
// ordem da pilha fazem a ida e volta pelo JSON sem perda.
AUREA_TEST(Gpu, EffectsPresetRendersLikeTheSourceLayer) {
    AUREA_REQUIRE_GPU();
    auto seek = [](Engine& e, i64 f) {
        Command s;
        s.type = CommandType::PlaybackSeek;
        s.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(e.apply_command(s).ok());
    };
    auto add_fx = [](Engine& e, u64 layer, const char* key) {
        Command fx;
        fx.type = CommandType::EffectAdd;
        fx.effect_add.layer = LayerId::unpack(layer);
        fx.effect_add.effectType = effect_type_id(key);
        fx.effect_add.index = kInvalidIndex;
        AUREA_CHECK(e.apply_command(fx).ok());
    };
    auto layer_of = [](Engine& e, u64 id) {
        Timeline& tl = e.project()->timeline();
        return tl.composition(tl.current())->layer(LayerId::unpack(id));
    };
    std::string js;
    Image8 source;
    {
        Scene3DRig rig(384, 216);
        const u64 s = *rig.e.add_shape(0);
        add_fx(rig.e, s, effect_keys::kGaussianBlur);
        add_fx(rig.e, s, effect_keys::kTint);
        Layer* l = layer_of(rig.e, s);
        l->shape.fillColor = Vec4{0.9f, 0.8f, 0.2f, 1.0f};
        // Raio animado de 0 a 12 entre os quadros 0 e 20; no 10 vale ~6.
        Track& t = l->tracks.get_or_create(TrackProperty::EffectParam, l->effects[0].id, param_track_key(0, 0));
        t.set(l->local_time(FrameIndex{0}), 0.0f, Interpolation::Linear);
        t.set(l->local_time(FrameIndex{20}), 12.0f, Interpolation::Linear);
        seek(rig.e, 10);
        source = rig.capture(384);
        js = rig.e.save_preset(s, presets::PresetKind::Effects, "Desfoque animado");
    }
    AUREA_CHECK(!js.empty());
    Scene3DRig rig(384, 216);
    const u64 s = *rig.e.add_shape(0);
    layer_of(rig.e, s)->shape.fillColor = Vec4{0.9f, 0.8f, 0.2f, 1.0f};
    seek(rig.e, 10);
    const Image8 plain = rig.capture(384);
    AUREA_CHECK(rig.e.apply_preset(s, js));
    const Image8 applied = rig.capture(384);
    const u32 before = max_diff(source, plain), after = max_diff(source, applied);
    std::printf("    preset de efeitos: dif sem preset %u, com preset %u\n", before, after);
    AUREA_CHECK(before > 20);   // os efeitos mudam a imagem de verdade
    AUREA_CHECK(after <= 1);    // e o preset reproduz a origem
}

// =============================================================================
// Fase 7E — máscaras, track matte e keying
// =============================================================================
namespace {

/// Máscara retangular (px da camada) sem tangentes.
Mask rect_mask(u32 id, f32 x0, f32 y0, f32 x1, f32 y1, MaskOperation op = MaskOperation::Add) {
    Mask m;
    m.id = id;
    m.operation = op;
    m.points = {MaskPoint{Vec2{x0, y0}}, MaskPoint{Vec2{x1, y0}}, MaskPoint{Vec2{x1, y1}}, MaskPoint{Vec2{x0, y1}}};
    return m;
}

/// Camada branca que cobre a composição 1:1 (px da camada = px da composição).
LayerId white_full(Scene& s) {
    return s.solid(static_cast<f32>(s.comp->width()), static_cast<f32>(s.comp->height()), Vec4{1, 1, 1, 1},
                   s.comp->width() * 0.5f, s.comp->height() * 0.5f);
}

} // namespace

AUREA_TEST(Gpu, MaskRectangleCoverageIsPixelExactWithinAntialiasing) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 96);
    const LayerId id = white_full(s);
    s.comp->layer(id)->masks.push_back(rect_mask(0, 20, 16, 100, 80));
    FloatImage img = s.render();
    // Bordas inteiras: dentro 1, fora 0, em todo pixel.
    f32 worst = 0;
    for (u32 y = 0; y < img.height; ++y) {
        for (u32 x = 0; x < img.width; ++x) {
            const f32 want = (x >= 20 && x < 100 && y >= 16 && y < 80) ? 1.0f : 0.0f;
            worst = std::max(worst, std::fabs(img.v(x, y).x - want));
        }
    }
    // Bordas fracionárias: o pixel da borda recebe a área coberta.
    Layer* l = s.comp->layer(id);
    l->masks[0] = rect_mask(0, 20.25f, 16.5f, 100.75f, 80.5f);
    img = s.render();
    const f32 left = img.v(20, 48).x, right = img.v(100, 48).x, top = img.v(60, 16).x, bottom = img.v(60, 80).x;
    const f32 corner = img.v(20, 16).x;
    std::printf("    borda inteira: pior %.4f | fracionaria: esq %.3f dir %.3f topo %.3f base %.3f canto %.3f (area %.3f)\n", worst, left,
                right, top, bottom, corner, 0.75f * 0.5f);
    AUREA_CHECK(worst <= 1.0f / 255.0f + 1e-3f);
    AUREA_CHECK_NEAR(left, 0.75f, 0.01f);
    AUREA_CHECK_NEAR(right, 0.75f, 0.01f);
    AUREA_CHECK_NEAR(top, 0.5f, 0.01f);
    AUREA_CHECK_NEAR(bottom, 0.5f, 0.01f);
    AUREA_CHECK_NEAR(corner, 0.375f, 0.15f);
    AUREA_CHECK_NEAR(img.v(60, 48).x, 1.0f, 0.004f);
    AUREA_CHECK(img.v(10, 48).x < 0.004f);
    // O mesmo bloco na CPU (mask::coverage_at) dá o mesmo número do shader.
    std::vector<Vec4> block;
    f32 start = 0;
    u64 key = 0;
    const u32 n = mask::build_block(*l, 0.0, Vec2{0, 0}, 0.2f, block, start, key);
    AUREA_CHECK_EQ(n, 1u);
    AUREA_CHECK_NEAR(mask::coverage_at(block.data(), n, start, Vec2{20.5f, 48.5f}, 1.0f), left, 0.006f);
    AUREA_CHECK_NEAR(mask::coverage_at(block.data(), n, start, Vec2{20.5f, 16.5f}, 1.0f), corner, 0.006f);
}

AUREA_TEST(Gpu, MaskModesCombineInStackOrder) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 96);
    const LayerId id = white_full(s);
    Layer* l = s.comp->layer(id);
    l->masks = {rect_mask(0, 10, 10, 70, 70), rect_mask(1, 40, 40, 100, 90, MaskOperation::Subtract)};
    auto px = [&](const FloatImage& im, u32 x, u32 y) { return im.v(x, y).x; };
    FloatImage sub = s.render();
    l->masks[1].operation = MaskOperation::Intersect;
    FloatImage inter = s.render();
    l->masks[1].operation = MaskOperation::Difference;
    FloatImage diff = s.render();
    l->masks[1].operation = MaskOperation::None;   // fora da pilha
    FloatImage none = s.render();
    l->masks = {rect_mask(0, 10, 10, 70, 70)};
    l->masks[0].inverted = true;
    l->masks[0].opacity = 0.5f;
    FloatImage inv = s.render();
    std::printf("    subtrair %.2f/%.2f/%.2f  intersectar %.2f/%.2f/%.2f  diferenca %.2f/%.2f/%.2f  invertida 50%% %.3f/%.3f\n",
                px(sub, 20, 20), px(sub, 50, 50), px(sub, 80, 80), px(inter, 20, 20), px(inter, 50, 50), px(inter, 80, 80),
                px(diff, 20, 20), px(diff, 50, 50), px(diff, 80, 80), px(inv, 20, 20), px(inv, 100, 20));
    const f32 t = 0.004f;
    AUREA_CHECK(std::fabs(px(sub, 20, 20) - 1) < t && px(sub, 50, 50) < t && px(sub, 80, 80) < t);
    AUREA_CHECK(px(inter, 20, 20) < t && std::fabs(px(inter, 50, 50) - 1) < t && px(inter, 80, 80) < t);
    AUREA_CHECK(std::fabs(px(diff, 20, 20) - 1) < t && px(diff, 50, 50) < t && std::fabs(px(diff, 80, 80) - 1) < t);
    AUREA_CHECK(std::fabs(px(none, 50, 50) - 1) < t && px(none, 80, 80) < t);
    AUREA_CHECK(px(inv, 20, 20) < t && std::fabs(px(inv, 100, 20) - 0.5f) < t);
}

AUREA_TEST(Gpu, MaskFeatherIsAGaussianRampOfTheExpectedWidth) {
    AUREA_REQUIRE_GPU();
    Scene s(192, 96);
    const LayerId id = white_full(s);
    Layer* l = s.comp->layer(id);
    l->masks = {rect_mask(0, 60, -50, 250, 150)};   // só a borda esquerda (x = 60) aparece
    l->masks[0].feather = 24.0f;
    // Posição (px) onde a linha do meio cruza o nível `v`, interpolando.
    auto crossing = [](const FloatImage& im, f32 v) {
        const u32 y = im.height / 2;
        for (u32 x = 1; x < im.width; ++x) {
            const f32 a = im.v(x - 1, y).x, b = im.v(x, y).x;
            if (a < v && b >= v) return static_cast<f32>(x - 1) + 0.5f + (v - a) / std::max(1e-6f, b - a);
        }
        return -1.0f;
    };
    FloatImage img = s.render();
    bool monotone = true;
    for (u32 x = 1; x < img.width; ++x) monotone &= img.v(x, 48).x + 1e-3f >= img.v(x - 1, 48).x;
    const f32 x10 = crossing(img, 0.1f), x50 = crossing(img, 0.5f), x90 = crossing(img, 0.9f);
    // σ = feather/4 (e o antisserrilhado de 1/√12 texel): 10–90 % = 2·1,2816·σ.
    const f32 sigma = std::sqrt(6.0f * 6.0f + 1.0f / 12.0f);
    const f32 expected = 2.0f * 1.28155f * sigma;
    l->masks[0].expansion = 5.0f;
    const f32 x50e = crossing(s.render(), 0.5f);
    std::printf("    feather 24 px: 10-90%% = %.2f px (esperado %.2f), meio em x=%.2f; expansao +5 -> meio em x=%.2f\n", x90 - x10,
                expected, x50, x50e);
    AUREA_CHECK(monotone);
    AUREA_CHECK(std::fabs((x90 - x10) - expected) < 1.0f);
    AUREA_CHECK(std::fabs(x50 - 60.0f) < 0.5f);
    AUREA_CHECK(std::fabs(x50e - 55.0f) < 0.5f);
}

AUREA_TEST(Gpu, AnimatedMaskPathMovesAndIsCached) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 64);
    const LayerId id = white_full(s);
    Layer* l = s.comp->layer(id);
    Mask m = rect_mask(0, 10, 10, 40, 50);
    MaskPathKey a, b;
    a.frame = 0;
    a.points = m.points;
    b.frame = 10;
    b.points = rect_mask(0, 60, 10, 90, 50).points;
    m.pathKeys = {a, b};
    l->masks = {m};
    u32 h0 = 0, m0 = 0;
    gpu().renderer.mask_cache_stats(h0, m0);
    const FloatImage f0 = s.render(FrameIndex{0});
    const FloatImage f5 = s.render(FrameIndex{5});
    const FloatImage f5again = s.render(FrameIndex{5});
    const FloatImage f10 = s.render(FrameIndex{10});
    u32 h1 = 0, m1 = 0;
    gpu().renderer.mask_cache_stats(h1, m1);
    // No quadro 5 (linear): x 35..65.
    std::printf("    quadro 0: x20=%.2f x50=%.2f | quadro 5: x30=%.2f x50=%.2f x70=%.2f | quadro 10: x75=%.2f | cache +%u acertos +%u raster\n",
                f0.v(20, 30).x, f0.v(50, 30).x, f5.v(30, 30).x, f5.v(50, 30).x, f5.v(70, 30).x, f10.v(75, 30).x, h1 - h0, m1 - m0);
    AUREA_CHECK(f0.v(20, 30).x > 0.99f && f0.v(50, 30).x < 0.01f);
    AUREA_CHECK(f5.v(30, 30).x < 0.01f && f5.v(50, 30).x > 0.99f && f5.v(70, 30).x < 0.01f);
    AUREA_CHECK(f5.v(34, 30).x < 0.01f && f5.v(35, 30).x > 0.99f);   // borda exatamente em x = 35
    AUREA_CHECK(f10.v(75, 30).x > 0.99f && f10.v(20, 30).x < 0.01f);
    AUREA_CHECK_EQ(m1 - m0, 3u);   // três formas diferentes
    AUREA_CHECK(h1 - h0 >= 1u);    // o quadro 5 repetido reaproveita a cobertura
    (void)f5again;
}

AUREA_TEST(Gpu, TrackMatteAlphaAndLuma) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 64);
    // Camada vermelha em tela cheia (embaixo) e a matte em cima (como no AE).
    const LayerId red = s.solid(128, 64, Vec4{1, 0, 0, 1}, 64, 32);
    const LayerId matte = s.solid(40, 20, Vec4{1, 1, 1, 1}, 64, 32);   // x 44..84, y 22..42
    Layer* r = s.comp->layer(red);
    r->matteSource = matte;
    r->matteMode = MatteMode::Alpha;
    const FloatImage alpha = s.render();
    r->matteMode = MatteMode::AlphaInverted;
    const FloatImage alphaInv = s.render();
    // Luma: matte cinza sRGB 0,25 cobrindo tudo → 25 % (e 75 % invertida).
    Layer* m = s.comp->layer(matte);
    m->shape.bounds = Rect{0, 0, 128, 64};
    m->shape.fillColor = Vec4{0.25f, 0.25f, 0.25f, 1};
    m->transform.anchor = Vec3{64, 32, 0};
    r->matteMode = MatteMode::Luma;
    const FloatImage luma = s.render();
    r->matteMode = MatteMode::LumaInverted;
    const FloatImage lumaInv = s.render();
    std::printf("    alfa: dentro %.3f fora %.3f (g %.3f) | invertido %.3f/%.3f | luma %.3f | luma invertida %.3f\n", alpha.v(64, 32).x,
                alpha.v(10, 10).x, alpha.v(64, 32).y, alphaInv.v(64, 32).x, alphaInv.v(10, 10).x, luma.v(10, 10).x, lumaInv.v(10, 10).x);
    // A matte não aparece por conta própria (o verde/azul do branco seria > 0).
    AUREA_CHECK(near4(alpha.v(64, 32), Vec4{1, 0, 0, 1}, 0.004f));
    AUREA_CHECK(near4(alpha.v(10, 10), Vec4{0, 0, 0, 1}, 0.004f));
    AUREA_CHECK(near4(alpha.v(43, 32), Vec4{0, 0, 0, 1}, 0.004f));
    AUREA_CHECK(near4(alphaInv.v(64, 32), Vec4{0, 0, 0, 1}, 0.004f));
    AUREA_CHECK(near4(alphaInv.v(10, 10), Vec4{1, 0, 0, 1}, 0.004f));
    AUREA_CHECK_NEAR(luma.v(10, 10).x, 0.25f, 0.01f);
    AUREA_CHECK_NEAR(lumaInv.v(10, 10).x, 0.75f, 0.01f);
    AUREA_CHECK(luma.v(10, 10).y < 0.004f);
}

AUREA_TEST(Gpu, ChromaKeyRemovesGreenAndKeepsSkin) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 64);
    s.comp->set_transparent_background(true);
    // Esquerda: fundo verde com variação (luz desigual); direita: tons de pele.
    ImagePixels px = uniform_image(128, 64, 0, 0, 0);
    const u8 greens[4][3] = {{25, 184, 56}, {40, 170, 70}, {18, 200, 48}, {30, 150, 60}};
    const u8 skins[3][3] = {{222, 171, 140}, {141, 85, 60}, {250, 214, 190}};
    for (u32 y = 0; y < 64; ++y) {
        for (u32 x = 0; x < 128; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * 128 + x) * 4];
            const u8* c = x < 64 ? greens[(x / 16 + y / 16) % 4] : skins[((x - 64) / 22) % 3];
            p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
        }
    }
    const LayerId id = s.image(std::move(px), 64, 32);
    s.add_effect(id, effect_keys::kChromaKey);   // cor-chave padrão: verde de fundo
    const FloatImage img = s.render();
    f32 greenMax = 0, skinMin = 1, skinShift = 0;
    for (u32 y = 2; y < 62; ++y) {
        for (u32 x = 2; x < 126; ++x) {
            if (x > 60 && x < 68) continue;   // fronteira (reamostragem)
            const Vec4 v = img.v(x, y);
            if (x < 64) {
                greenMax = std::max(greenMax, v.w);
            } else {
                skinMin = std::min(skinMin, v.w);
                const u8* c = skins[((x - 64) / 22) % 3];
                if ((x - 64) % 22 > 1 && (x - 64) % 22 < 20) skinShift = std::max(skinShift, std::fabs(srgb_encode(v.y) - c[1] / 255.0f));
            }
        }
    }
    // Chave de luma: tira os escuros.
    Scene s2(64, 32);
    s2.comp->set_transparent_background(true);
    ImagePixels bw = uniform_image(64, 32, 0, 0, 0);
    for (u32 y = 0; y < 32; ++y) for (u32 x = 32; x < 64; ++x) for (int c = 0; c < 3; ++c) bw.rgba[(static_cast<usize>(y) * 64 + x) * 4 + c] = 230;
    const LayerId lk = s2.image(std::move(bw), 32, 16);
    s2.add_effect(lk, effect_keys::kLumaKey);
    const FloatImage lum = s2.render();
    std::printf("    croma: alfa max no verde %.4f, alfa min na pele %.4f, desvio do verde da pele %.4f | luma: escuro %.3f claro %.3f\n",
                greenMax, skinMin, skinShift, lum.v(10, 16).w, lum.v(50, 16).w);
    AUREA_CHECK(greenMax < 0.02f);
    AUREA_CHECK(skinMin > 0.98f);
    AUREA_CHECK(skinShift < 0.02f);
    AUREA_CHECK(lum.v(10, 16).w < 0.01f && lum.v(50, 16).w > 0.99f);
}

AUREA_TEST(Gpu, MasksMatteAndKeysSurviveSaveAndReopen) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(256, 144);
    auto a = rig.e.add_shape(0);
    auto b = rig.e.add_shape(1);
    AUREA_CHECK(a.ok() && b.ok());
    if (!a.ok() || !b.ok()) return;
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    const Rect box = comp->layer(LayerId::unpack(*a))->shape.bounds;
    const f32 w = box.w, h = box.h;
    // Losango com tangentes (bezier), feather e expansão; caminho animado.
    const f32 p0[4 * 6] = {w * 0.5f, 0, -8, 0, 8, 0,   w, h * 0.5f, 0, -8, 0, 8,   w * 0.5f, h, 8, 0, -8, 0,   0, h * 0.5f, 0, 8, 0, -8};
    const i32 mid = rig.e.add_mask(*a, p0, 4, true);
    AUREA_CHECK(mid >= 0);
    AUREA_CHECK(rig.e.set_mask_props(*a, static_cast<u32>(mid), 0, false, 6.0f, 2.0f, 0.9f));
    seek_frame(rig.e, 0);
    bool keyed = false;
    AUREA_CHECK(rig.e.toggle_mask_path_key(*a, static_cast<u32>(mid), &keyed) && keyed);
    seek_frame(rig.e, 20);
    f32 p1[4 * 6];
    for (int i = 0; i < 24; ++i) p1[i] = p0[i] * ((i % 6) < 2 ? 0.6f : 1.0f);
    AUREA_CHECK(rig.e.set_mask_path(*a, static_cast<u32>(mid), p1, 4, true, true));
    // Track matte por luma na elipse, e chave de croma na camada.
    AUREA_CHECK(rig.e.set_track_matte(*a, *b, 3));
    Command fx;
    fx.type = CommandType::EffectAdd;
    fx.effect_add.layer = LayerId::unpack(*a);
    fx.effect_add.effectType = effect_type_id(effect_keys::kChromaKey);
    AUREA_CHECK(rig.e.apply_command(fx).ok());
    std::vector<f32> q(rig.e.query_masks(*a, nullptr, 0));
    AUREA_CHECK(rig.e.query_masks(*a, q.data(), static_cast<u32>(q.size())) == q.size());
    AUREA_CHECK(q.size() > 7 && q[6] == 1.0f && q[7 + 8] == 2.0f);   // 1 máscara, 2 keys
    seek_frame(rig.e, 10);
    const Image8 before = rig.capture(256);
    const f32 cov = coverage(before);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_mascaras.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    seek_frame(rig.e, 10);
    const Image8 after = rig.capture(256);
    u64 matte = 0;
    u32 mode = 0;
    AUREA_CHECK(rig.e.query_track_matte(*a, matte, mode) && matte == *b && mode == 3);
    const u32 diff = max_diff(before, after);
    std::printf("    salvo e reaberto: cobertura %.4f, diferenca maxima %u\n", cov, diff);
    AUREA_CHECK(cov > 0.005f);
    AUREA_CHECK_EQ(diff, 0u);
    std::remove(path.c_str());
}

AUREA_TEST(Gpu, TextStaysVisibleAcrossRepeatedFrames) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(640, 360);
    auto tid = rig.e.add_text("Texto");
    AUREA_CHECK(tid.ok());
    f32 c[6];
    for (int i = 0; i < 6; ++i) c[i] = coverage(rig.capture(640));
    std::printf("    cobertura por quadro: %.4f %.4f %.4f %.4f %.4f %.4f\n", c[0], c[1], c[2], c[3], c[4], c[5]);
    for (int i = 0; i < 6; ++i) AUREA_CHECK(c[i] > 0.002f && std::fabs(c[i] - c[0]) < 1e-6f);
}

AUREA_TEST(Gpu, ShapeSizeAndRadiusAnimateWithKeyframes) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(400, 400);
    auto id = rig.e.add_shape(1);   // quadrado
    AUREA_CHECK(id.ok());
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    Layer* l = comp->layer(LayerId::unpack(*id));
    AUREA_CHECK(rig.e.set_shape_param(*id, 5, 100.0f, false));
    AUREA_CHECK(rig.e.set_shape_param(*id, 6, 100.0f, false));
    const Box8 small = lit_box(rig.capture(400));
    // Keyframe no quadro 0 e outro no 10 com o dobro do tamanho.
    AUREA_CHECK(rig.e.toggle_shape_param_key(*id, 5));
    AUREA_CHECK(rig.e.toggle_shape_param_key(*id, 6));
    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{10}, 30.0);
    AUREA_CHECK(rig.e.apply_command(seek).ok());
    AUREA_CHECK(rig.e.set_shape_param(*id, 5, 200.0f, false));
    AUREA_CHECK(rig.e.set_shape_param(*id, 6, 200.0f, false));
    const Box8 big = lit_box(rig.capture(400));
    seek.seek.time = tick_at(FrameIndex{5}, 30.0);
    AUREA_CHECK(rig.e.apply_command(seek).ok());
    const Box8 mid = lit_box(rig.capture(400));
    f32 v[Engine::kShapeParamFloats];
    AUREA_CHECK_EQ(rig.e.query_shape_params(*id, v, Engine::kShapeParamFloats), Engine::kShapeParamFloats);
    std::printf("    forma animada: %ux%u -> %ux%u (meio %ux%u), centro %u,%u; bits anim %.0f\n", small.w(), small.h(), big.w(), big.h(),
                mid.w(), mid.h(), (mid.x0 + mid.x1) / 2, (mid.y0 + mid.y1) / 2, v[7]);
    AUREA_CHECK(big.w() >= small.w() * 2 - 3 && big.w() <= small.w() * 2 + 3);
    AUREA_CHECK(mid.w() > small.w() + 20 && mid.w() < big.w() - 20);
    // O centro fica no lugar enquanto o tamanho cresce.
    AUREA_CHECK(std::abs(static_cast<int>((mid.x0 + mid.x1) / 2) - 200) <= 2);
    AUREA_CHECK(std::abs(static_cast<int>((mid.y0 + mid.y1) / 2) - 200) <= 2);
    AUREA_CHECK(static_cast<u32>(v[7]) == ((1u << 5) | (1u << 6)));
    // Salvar e reabrir mantém a animação da forma.
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_forma.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.apply_command(seek).ok());
    const Box8 again = lit_box(rig.capture(400));
    AUREA_CHECK(again.w() == mid.w() && again.h() == mid.h());
    std::remove(path.c_str());
    (void)l;
}

// =============================================================================
// Prévia de efeito (Fase 7.3 §13–§15)
//
// A cartela de demonstração roda o efeito REAL. O que se testa aqui não é a
// arte da cartela: é que a prévia sai com o tamanho pedido, com alfa cheio, e
// que ela MUDA quando o efeito faz algo — uma prévia que devolvesse a cartela
// intacta para todo efeito seria uma prévia que mente.
// =============================================================================
AUREA_TEST(Gpu, EffectPreviewRendersThePlateAtTheRequestedSize) {
    AUREA_REQUIRE_GPU();
    Gpu& g = gpu();
    std::vector<u8> rgba;
    AUREA_CHECK(g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kExposure), 160, 100, rgba).ok());
    AUREA_CHECK_EQ(rgba.size(), static_cast<usize>(160) * 100 * 4);
    usize opaque = 0;
    for (usize i = 3; i < rgba.size(); i += 4) if (rgba[i] == 255) ++opaque;
    AUREA_CHECK_EQ(opaque, static_cast<usize>(160) * 100);   // a cartela é opaca
    // A cartela tem conteúdo: se saísse preta, nenhuma prévia diria nada.
    usize bright = 0;
    for (usize i = 0; i < rgba.size(); i += 4) if (rgba[i] > 40) ++bright;
    AUREA_CHECK(bright > 100);
}

AUREA_TEST(Gpu, EffectPreviewShowsWhatTheEffectDoes) {
    AUREA_REQUIRE_GPU();
    Gpu& g = gpu();
    std::vector<u8> mild;
    std::vector<u8> sharp;
    AUREA_CHECK(g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kGaussianBlur), 160, 100, mild).ok());
    AUREA_CHECK(g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kGaussianBlur), 160, 100, sharp).ok());
    // Determinística: a cartela é gerada no shader, sem estado escondido.
    AUREA_CHECK(mild == sharp);

    std::vector<u8> crisp;
    AUREA_CHECK(g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kSharpen), 160, 100, crisp).ok());
    // As prévias usam os VALORES DE DEMONSTRAÇÃO, não o padrão neutro: se
    // saíssem iguais, a prévia não estaria mostrando nada (§13).
    AUREA_CHECK(mild != crisp);

    // Energia de borda: soma do salto de luma entre pixels vizinhos. Desfocar
    // TEM de baixar isto, e não afiar. É a medida de "o efeito apareceu".
    const u32 w = 160;
    auto edge_energy = [w](const std::vector<u8>& px) {
        f64 e = 0.0;
        const u32 h = static_cast<u32>(px.size() / 4) / w;
        for (u32 y = 0; y < h; ++y) {
            for (u32 x = 1; x < w; ++x) {
                const usize a = (static_cast<usize>(y) * w + x) * 4;
                e += std::fabs(static_cast<f64>(px[a]) - static_cast<f64>(px[a - 4]));
            }
        }
        return e;
    };
    AUREA_CHECK(edge_energy(mild) < edge_energy(crisp));
}

AUREA_TEST(Gpu, EffectPreviewRefusesWhatOneFrameCannotShow) {
    AUREA_REQUIRE_GPU();
    Gpu& g = gpu();
    std::vector<u8> rgba;
    // Eco precisa de uma SEQUÊNCIA: um quadro solto não o representa, e a UI
    // prefere a cartela genérica a uma imagem que mentiria.
    const Status s = g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kEchoTrail), 96, 60, rgba);
    AUREA_CHECK(!s.ok());
    std::vector<u8> none;
    AUREA_CHECK(!g.renderer.render_effect_preview(g.effects, 0u, 96, 60, none).ok());
    AUREA_CHECK(!g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kGaussianBlur), 0, 60, none).ok());
}

// =============================================================================
// A PRÉVIA DE TODO O CATÁLOGO (Fase 7.3 §13)
//
// Um efeito novo sem prévia é um cartão cinza no navegador — e, pior, é um
// efeito cujo shader provavelmente não compila. Este teste percorre o catálogo
// INTEIRO e exige que cada efeito ou desenhe a cartela, ou seja de uma classe
// que um quadro solto não representa (temporal/global) e que a classe esteja
// declarada como tal.
//
// É o teste que pega o erro mais fácil de cometer ao acrescentar um efeito:
// registrar sem que o passe monte, ou montar um uniforme com o layout errado.
// =============================================================================
AUREA_TEST(Gpu, EveryCatalogEffectEitherRendersAPreviewOrSaysWhyNot) {
    AUREA_REQUIRE_GPU();
    Gpu& g = gpu();
    std::vector<u8> rgba;
    u32 rendered = 0, refused = 0;
    for (u32 i = 0; i < g.effects.count(); ++i) {
        const Effect& e = g.effects.at(i);
        rgba.clear();
        const Status s = g.renderer.render_effect_preview(g.effects, e.type_id(), 96, 60, rgba);
        const bool structural = e.effect_class() == EffectClass::Temporal
                             || e.effect_class() == EffectClass::Global;
        if (s.ok()) {
            if (rgba.size() != static_cast<usize>(96) * 60 * 4) {
                std::printf("\n    '%s' devolveu %zu bytes", e.info().name, rgba.size());
            }
            AUREA_CHECK(rgba.size() == static_cast<usize>(96) * 60 * 4);
            ++rendered;
        } else {
            if (!structural) {
                std::printf("\n    '%s' nao desenhou a previa: %s", e.info().name, s.message().data());
            }
            AUREA_CHECK_MSG(structural, "efeito sem previa e sem motivo estrutural");
            ++refused;
        }
    }
    std::printf("(previas: %u desenhadas, %u recusadas por classe) ", rendered, refused);
    // Um catálogo em que quase nada desenha não é um catálogo.
    AUREA_CHECK(rendered > g.effects.count() * 3 / 4);
}

// =============================================================================
// Efeitos temporais (Fase 7.3 §25, §26, §61)
//
// Os dois são DECLARATIVOS: quem age é o renderer, lendo os parâmetros no
// instante. Estes testes provam que eles agem de verdade e não são dois nomes
// bonitos numa lista (§21) — e provam contra uma cena que MUDA com o tempo,
// que é o único jeito de um efeito temporal mostrar alguma coisa.
// =============================================================================
namespace {

FloatImage first_diff_frame(Scene& s, const FloatImage& base, FrameIndex from, FrameIndex to) {
    // Devolve o primeiro quadro a partir de `from` que difere de `base`.
    for (i64 i = from.value; i <= to.value; ++i) {
        const FloatImage img = s.render(FrameIndex{i});
        f32 d = 0.0f;
        for (usize k = 0; k < img.px.size(); ++k) d = std::max(d, std::fabs(img.px[k] - base.px[k]));
        if (d > 0.01f) return img;
    }
    return base;
}

} // namespace

AUREA_TEST(Gpu, PosterizeTimeHoldsTheFrameAndThenLetsItGo) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 72, 30.0);
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    cfg.pattern = SyntheticPattern::MovingSquare;
    cfg.frameCount = 120;
    const LayerId id = s.video(cfg, 64, 36, 0.5f);

    // Sem o efeito, o quadro anda a cada quadro.
    const FloatImage plain0 = s.render(FrameIndex{0});
    const FloatImage plain2 = s.render(FrameIndex{2});
    f32 moved = 0.0f;
    for (usize k = 0; k < plain0.px.size(); ++k) moved = std::max(moved, std::fabs(plain0.px[k] - plain2.px[k]));
    AUREA_CHECK(moved > 0.01f);

    // A 10 quadros por segundo, a composição de 30 mostra o mesmo quadro em
    // 0, 1 e 2 — e um quadro novo em 3.
    EffectInstance& e = s.add_effect(id, effect_keys::kPosterizeTime);
    e.params[0].constant = ParamValue::scalar(10.0f);
    e.params[1].constant = ParamValue::boolean(true);

    const FloatImage held0 = s.render(FrameIndex{0});
    const FloatImage held2 = s.render(FrameIndex{2});
    f32 heldDiff = 0.0f;
    for (usize k = 0; k < held0.px.size(); ++k) heldDiff = std::max(heldDiff, std::fabs(held0.px[k] - held2.px[k]));
    AUREA_CHECK_MSG(heldDiff < 0.01f, "o quadro devia estar SEGURADO de 0 a 2");

    const FloatImage held3 = s.render(FrameIndex{3});
    f32 nextDiff = 0.0f;
    for (usize k = 0; k < held0.px.size(); ++k) nextDiff = std::max(nextDiff, std::fabs(held0.px[k] - held3.px[k]));
    AUREA_CHECK_MSG(nextDiff > 0.01f, "o quadro devia DESTRAVAR no 3");
}

AUREA_TEST(Gpu, TimeWarpRgbReadsEachChannelFromItsOwnInstant) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 72, 30.0);
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    cfg.pattern = SyntheticPattern::MovingSquare;
    cfg.frameCount = 120;
    const LayerId id = s.video(cfg, 64, 36, 0.5f);

    // Referência: o que o quadro 10 mostra, sem efeito nenhum.
    const FloatImage ref10 = s.render(FrameIndex{10});
    const FloatImage ref20 = s.render(FrameIndex{20});

    // Vermelho 10 quadros ATRÁS, verde e azul no instante.
    EffectInstance& e = s.add_effect(id, effect_keys::kTimeWarpRgb);
    e.params[0].constant = ParamValue::scalar(-10.0f);
    e.params[1].constant = ParamValue::scalar(0.0f);
    e.params[2].constant = ParamValue::scalar(0.0f);
    e.params[3].constant = ParamValue::scalar(0.0f);   // quadros = 0
    e.params[4].constant = ParamValue::scalar(100.0f);
    e.params[5].constant = ParamValue::boolean(false);

    const FloatImage warped = s.render(FrameIndex{20});

    // O vermelho do quadro 20 com o efeito tem de ser o vermelho do QUADRO 10,
    // e o verde e o azul continuam os do quadro 20.
    f32 rErr = 0.0f, gErr = 0.0f, bErr = 0.0f;
    for (u32 y = 0; y < warped.height; ++y) {
        for (u32 x = 0; x < warped.width; ++x) {
            const usize k = (static_cast<usize>(y) * warped.width + x) * 4;
            rErr = std::max(rErr, std::fabs(warped.px[k + 0] - ref10.px[k + 0]));
            gErr = std::max(gErr, std::fabs(warped.px[k + 1] - ref20.px[k + 1]));
            bErr = std::max(bErr, std::fabs(warped.px[k + 2] - ref20.px[k + 2]));
        }
    }
    // Cada canal no seu instante, no MESMO pixel: é a definição do efeito.
    AUREA_CHECK_MSG(rErr < 0.02f, "o vermelho devia vir do quadro 10");
    AUREA_CHECK_MSG(gErr < 0.02f, "o verde devia ficar no quadro 20");
    AUREA_CHECK_MSG(bErr < 0.02f, "o azul devia ficar no quadro 20");
}

// =============================================================================
// O catálogo INTEIRO: todo efeito monta, compila e desenha
//
// O catálogo não pode listar efeito que fecha o app. Este teste percorre o
// registro inteiro pelo MESMO caminho do botão "aplicar": pré-aquece os
// pipelines (como o renderer faz ao abrir o projeto), adiciona o efeito numa
// camada de verdade e desenha um quadro com a validação Vulkan ligada.
//
// Shader que não compila, uniform do tamanho errado, textura faltando ou um
// índice fora do lugar aparecem AQUI — com o nome do efeito — em vez de na mão
// de quem está editando. Os valores são os de demonstração (§13), porque o
// padrão de fábrica é neutro em quase tudo e um efeito neutro sai da cadeia
// sem passar pelo shader: o teste não provaria nada.
// =============================================================================
AUREA_TEST(Gpu, EveryRegisteredEffectBuildsAndRenders) {
    AUREA_REQUIRE_GPU();

    const u32 total = gpu().effects.count();
    AUREA_CHECK_MSG(total > 0, "o registro de efeitos está vazio");

    // Pré-aquecimento: é o que o Renderer faz na inicialização com os pipelines
    // de TODOS os efeitos. Se um shader do catálogo não compilar, o erro sai
    // daqui — antes de qualquer projeto abrir.
    {
        std::vector<PipelineKey> keys;
        gpu().effects.collect_pipelines(keys, SurfaceFormat::RGBA16F);
        AUREA_CHECK_MSG(!keys.empty(), "nenhum pipeline de efeito para pré-aquecer");
        for (const PipelineKey& k : keys) {
            AUREA_CHECK_MSG(gpu().renderer.shaders().pipeline(k).ok(),
                            "pipeline do catálogo não compilou");
        }
    }

    std::vector<std::string> broken;
    for (u32 i = 0; i < total; ++i) {
        const Effect& fx = gpu().effects.at(i);
        const char* key = fx.info().key;
        const ParameterRegistry& params = gpu().effects.params_at(i);

        Scene s(96, 96);
        // Imagem com degradê, disco claro e linhas finas: dá borda afiada para
        // o que depende de vizinhança e área clara para o que depende de luz.
        const LayerId id = s.image(reference_image(96, 96), 48, 48);
        Layer* l = s.comp->layer(id);

        EffectInstance e;
        e.id = l->alloc_effect_id();
        e.type = fx.type_id();
        initialize_instance(e, params);
        {
            std::vector<ParamValue> values(params.count());
            for (u32 p = 0; p < params.count(); ++p) values[p] = e.params[p].constant;
            if (fx.demo_values(e, values)) {
                for (u32 p = 0; p < params.count(); ++p) e.params[p].constant = values[p];
            }
        }
        l->effects.push_back(std::move(e));

        // O caminho do EDITOR: os MESMOS parâmetros, mas varridos de ponta a
        // ponta. O botão "aplicar" usa os padrões de fábrica e o usuário empurra
        // cada controle até o fim — um raio gigante, uma escala enorme. É aí que
        // mora a alocação de textura no limite do aparelho e a divisão por zero
        // que a prévia (sempre com os valores de demonstração) nunca vê.
        for (u32 p = 0; p < params.count(); ++p) {
            const ParamSpec& spec = params.at(p);
            if (spec.type != ParamType::Float && spec.type != ParamType::Int) continue;
            for (f32 v : {spec.minValue, spec.maxValue}) {
                Layer* ll = s.comp->layer(id);
                ll->effects[0].params[p].constant = ParamValue::scalar(v);
                const FloatImage im = s.render();
                for (const f32 x : im.px) {
                    if (!std::isfinite(x)) {
                        broken.emplace_back(std::string(key) + ": parametro " + std::to_string(p) +
                                            " em " + std::to_string(v) + " deu pixel nao-finito");
                        break;
                    }
                }
            }
        }

        const FloatImage img = s.render();
        for (const f32 v : img.px) {
            if (!std::isfinite(v)) { broken.emplace_back(std::string(key) + ": pixel nao-finito"); break; }
        }
        if (!img.px.empty()) {
            f32 sum = 0.0f;
            for (const f32 v : img.px) sum += std::fabs(v);
            if (sum <= 0.0f) broken.emplace_back(std::string(key) + ": quadro preto");
        }
    }

    for (const std::string& b : broken) std::printf("\n    efeito quebrado: %s", b.c_str());
    AUREA_CHECK_MSG(broken.empty(), "efeito do catálogo nao desenha");
}

// Fase 8A (custo por efeito): Varredura, Grão e Colorama eram PerPixel sem
// ColorOp — o EffectGraph os tirava do plano como identidade e, aplicados no
// projeto, não mudavam nada (só a prévia do catálogo, que chama o build
// direto, mostrava o efeito). Aqui cada efeito do catálogo, com os valores de
// demonstração, tem de MUDAR o quadro do projeto. Ficam de fora os controles
// de expressão (não desenham por definição) e os temporais (numa imagem
// parada não há outro instante para misturar).
AUREA_TEST(Gpu, EveryCatalogEffectChangesTheProjectFrame) {
    AUREA_REQUIRE_GPU();
    std::vector<std::string> inert;
    for (u32 i = 0; i < gpu().effects.count(); ++i) {
        const Effect& fx = gpu().effects.at(i);
        const char* key = fx.info().key;
        if (fx.effect_class() == EffectClass::Temporal || std::strncmp(key, "aurea.control.", 14) == 0) continue;
        const ParameterRegistry& params = gpu().effects.params_at(i);
        Scene s(96, 96);
        const LayerId id = s.image(reference_image(96, 96), 48, 48);
        const FloatImage plain = s.render();
        EffectInstance e;
        e.id = s.comp->layer(id)->alloc_effect_id();
        e.type = fx.type_id();
        initialize_instance(e, params);
        std::vector<ParamValue> values(params.count());
        for (u32 p = 0; p < params.count(); ++p) values[p] = e.params[p].constant;
        if (fx.demo_values(e, values)) {
            for (u32 p = 0; p < params.count(); ++p) e.params[p].constant = values[p];
        }
        s.comp->layer(id)->effects.push_back(std::move(e));
        const FloatImage with = s.render();
        u32 changed = 0;
        for (usize k = 0; k < with.px.size() && k < plain.px.size(); ++k) changed += std::fabs(with.px[k] - plain.px[k]) > 0.004f ? 1u : 0u;
        if (changed == 0) inert.emplace_back(key);
    }
    for (const std::string& k : inert) std::printf("\n    efeito sem efeito no projeto: %s", k.c_str());
    AUREA_CHECK_MSG(inert.empty(), "efeito do catalogo aplicado nao muda o quadro");
}

// A foto do app como base das prévias: o efeito é mostrado sobre ela.
AUREA_TEST(Gpu, EffectPreviewUsesThePhotoWhenGiven) {
    AUREA_REQUIRE_GPU();
    Gpu& g = gpu();
    // "Foto" 64×64: metade de cima vermelha, metade de baixo azul.
    std::vector<u8> photo(64 * 64 * 4);
    for (u32 y = 0; y < 64; ++y) for (u32 x = 0; x < 64; ++x) {
        u8* p = &photo[(y * 64 + x) * 4];
        p[0] = y < 32 ? 220 : 20; p[1] = 30; p[2] = y < 32 ? 20 : 220; p[3] = 255;
    }
    g.renderer.set_effect_preview_source(photo, 64, 64);
    std::vector<u8> rgba;
    AUREA_CHECK(g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kSaturation), 160, 100, rgba).ok());
    // Card 1,6:1 recorta em cima/embaixo com o corte puxado para cima: o topo
    // do card é a parte vermelha e o pé é a azul.
    const u8* top = &rgba[(5 * 160 + 80) * 4];
    const u8* bottom = &rgba[(95 * 160 + 80) * 4];
    std::printf("    previa com foto: topo (%u,%u,%u) pe (%u,%u,%u)\n", top[0], top[1], top[2], bottom[0], bottom[1], bottom[2]);
    AUREA_CHECK(top[0] > top[2]);
    AUREA_CHECK(bottom[2] > bottom[0]);
    g.renderer.set_effect_preview_source({}, 0, 0);   // os outros testes usam a cartela
    std::vector<u8> plate;
    AUREA_CHECK(g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kSaturation), 160, 100, plate).ok());
    AUREA_CHECK(plate != rgba);
}

// Auditoria das prévias sobre a foto real (opcional): AUREA_PREVIEW_AUDIT=<jpg>
// [AUREA_PREVIEW_DUMP=<pasta>]. Mede, por efeito, a fração de pixels estourados
// (branco) e apagados (preto): um card que vira só branco ou só preto não diz
// nada do efeito.
extern "C" unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
extern "C" void stbi_image_free(void*);
AUREA_TEST(Gpu, EffectPreviewAuditOnThePhoto) {
    const char* path = std::getenv("AUREA_PREVIEW_AUDIT");
    if (!path || !*path) return;
    AUREA_REQUIRE_GPU();
    Gpu& g = gpu();
    std::FILE* f = std::fopen(path, "rb");
    AUREA_CHECK(f != nullptr);
    if (!f) return;
    std::vector<u8> bytes;
    u8 buf[65536];
    for (usize n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    int w = 0, h = 0, c = 0;
    u8* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c, 4);
    AUREA_CHECK(px != nullptr);
    if (!px) return;
    g.renderer.set_effect_preview_source(std::vector<u8>(px, px + static_cast<usize>(w) * h * 4), static_cast<u32>(w), static_cast<u32>(h));
    stbi_image_free(px);
    const char* dump = std::getenv("AUREA_PREVIEW_DUMP");
    constexpr u32 W = 320, H = 200;
    for (u32 i = 0; i < g.effects.count(); ++i) {
        const Effect& e = g.effects.at(i);
        std::vector<u8> rgba;
        if (!g.renderer.render_effect_preview(g.effects, e.type_id(), W, H, rgba).ok()) {
            std::printf("\n    %-28s sem previa", e.info().key);
            continue;
        }
        u32 white = 0, black = 0;
        for (u32 p = 0; p < W * H; ++p) {
            const u8* q = &rgba[p * 4];
            if (std::min({q[0], q[1], q[2]}) >= 245) ++white;
            if (std::max({q[0], q[1], q[2]}) <= 10) ++black;
        }
        std::printf("\n    %-34s branco %5.1f%%  preto %5.1f%%", e.info().key, 100.0 * white / (W * H), 100.0 * black / (W * H));
        if (dump && *dump) {
            test::Image8 img{W, H, rgba};
            (void)test::write_png(std::string(dump) + "/" + e.info().key + ".png", img);
        }
    }
    std::printf("\n");
    g.renderer.set_effect_preview_source({}, 0, 0);
}

// Efeito que aumenta a área da camada não pode ESTICAR a borda da imagem para
// fora (amostrador repetindo o último pixel): fora da imagem é transparente, e
// só o que o efeito desloca/espalha de verdade aparece lá. Mede, a cada
// distância fora da caixa da camada, a fração de pixels que não são fundo.
namespace {
f32 outside_fill(const FloatImage& img, u32 x0, u32 y0, u32 x1, u32 y1, u32 d) {
    u32 hit = 0, n = 0;
    auto lit = [&](u32 x, u32 y) {
        const Vec4 c = img.v(x, y);
        ++n;
        if (c.x + c.y + c.z > 0.03f) ++hit;
    };
    for (u32 y = y0 + 8; y < y1 - 8; ++y) { lit(x0 - d, y); lit(x1 - 1 + d, y); }
    for (u32 x = x0 + 8; x < x1 - 8; ++x) { lit(x, y0 - d); lit(x, y1 - 1 + d); }
    return n ? static_cast<f32>(hit) / static_cast<f32>(n) : 0.0f;
}
} // namespace

AUREA_TEST(Gpu, EffectsDoNotStretchTheLayerEdgeOutward) {
    AUREA_REQUIRE_GPU();
    static const char* const kKeys[] = {
        effect_keys::kTurbulence, effect_keys::kWaveWarp, effect_keys::kWarp, effect_keys::kRippleDissolve,
        effect_keys::kShake, effect_keys::kGlitchify, effect_keys::kVhs, effect_keys::kUniVhs,
        effect_keys::kSignal, effect_keys::kCrossGlitch, effect_keys::kHoloMatrix, effect_keys::kFilmDamage,
        effect_keys::kJpegDamage, effect_keys::kPixelSort, effect_keys::kHalftone, effect_keys::kMinimax,
        effect_keys::kUnsharp, effect_keys::kLensBlur, effect_keys::kDeepGlow,
    };
    constexpr u32 kComp = 320, kSide = 160, kX0 = (kComp - kSide) / 2, kX1 = kX0 + kSide;
    // Xadrez de 8 px (verde/azul): conteúdo deslocado para fora não é uniforme.
    ImagePixels px = uniform_image(kSide, kSide, 0, 0, 0);
    for (u32 y = 0; y < kSide; ++y) for (u32 x = 0; x < kSide; ++x) {
        u8* p = &px.rgba[(static_cast<usize>(y) * kSide + x) * 4];
        const bool a = ((x / 8) + (y / 8)) % 2 == 0;
        p[0] = 40; p[1] = a ? 200 : 60; p[2] = a ? 60 : 200;
    }
    const bool verbose = std::getenv("AUREA_EDGE_VERBOSE") != nullptr;
    for (const char* key : kKeys) {
        Scene s(kComp, kComp);
        const LayerId id = s.image(px, kComp * 0.5f, kComp * 0.5f);
        EffectInstance& e = s.add_effect(id, key);
        const Effect* fx = gpu().effects.find(e.type);
        AUREA_CHECK(fx != nullptr);
        if (!fx) continue;
        std::vector<ParamValue> v(e.params.size());
        for (usize i = 0; i < v.size(); ++i) v[i] = e.params[i].constant;
        if (fx->demo_values(e, v)) for (usize i = 0; i < v.size(); ++i) e.params[i].constant = v[i];
        const FloatImage img = s.render(FrameIndex{12});
        const f32 f2 = outside_fill(img, kX0, kX0, kX1, kX1, 2);
        const f32 f24 = outside_fill(img, kX0, kX0, kX1, kX1, 24);
        const f32 f48 = outside_fill(img, kX0, kX0, kX1, kX1, 48);
        if (verbose) std::printf("\n    %-30s fora a 2px %5.1f%%  24px %5.1f%%  48px %5.1f%%", key, f2 * 100, f24 * 100, f48 * 100);
        // Esticada, a borda enche a margem inteira (~100% em qualquer distância
        // até o alcance do efeito). Sem esticar, longe da caixa sobra pouco.
        AUREA_CHECK_MSG(f48 < 0.35f, key);
    }
    if (verbose) std::printf("\n");
}

// Keyframe em parâmetro do Aurea Particular: o valor do instante chega à UI e
// ao desenho; e o projeto v21 (campos novos + trilha) volta igual do disco.
AUREA_TEST(Gpu, ParticularParamsAnimateAndSurviveSaveReopen) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    auto id = rig.e.add_particles(0);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    auto key = [&](ParticleParam p, i64 f, f32 v) {
        Command c;
        c.type = CommandType::KeyframeInsert;
        c.keyframe.track.layer = LayerId::unpack(*id);
        c.keyframe.track.property = TrackProperty::ParticleParam;
        c.keyframe.track.effectIndex = kInvalidIndex;
        c.keyframe.track.effectParamIndex = static_cast<u32>(p);
        c.keyframe.time = FrameIndex{f};
        c.keyframe.value = v;
        AUREA_CHECK(rig.e.apply_command(c).ok());
    };
    auto seekTo = [&](i64 f) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(rig.e.apply_command(c).ok());
    };
    key(ParticleParam::StartSize, 0, 2.0f);
    key(ParticleParam::StartSize, 60, 40.0f);
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::ColorRandom), 0.5f));
    AUREA_CHECK(rig.e.set_particle_param(*id, static_cast<u32>(ParticleParam::Collision),
                                         static_cast<f32>(ParticleCollision::Sphere)));
    std::vector<f32> q(static_cast<usize>(ParticleParam::Count));
    seekTo(30);
    AUREA_CHECK(rig.e.query_particles(*id, q.data()));
    const f32 mid = q[static_cast<usize>(ParticleParam::StartSize)];
    const Image8 small = rig.capture(320);
    seekTo(60);
    const Image8 big = rig.capture(320);
    std::printf("    tamanho animado: quadro 30 = %.2f; cobertura 30 %.4f -> 60 %.4f\n", mid, coverage(small), coverage(big));
    AUREA_CHECK_NEAR(mid, 21.0, 0.5);
    AUREA_CHECK(coverage(big) > coverage(small));

    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_particular.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    seekTo(60);
    const Image8 again = rig.capture(320);
    AUREA_CHECK(rig.e.query_particles(*id, q.data()));
    u32 worst = 0;
    for (usize i = 0; i < big.rgba.size() && i < again.rgba.size(); ++i)
        worst = std::max<u32>(worst, static_cast<u32>(std::abs(big.rgba[i] - again.rgba[i])));
    std::printf("    salvo e reaberto: diferenca %u\n", worst);
    AUREA_CHECK(worst == 0);
    AUREA_CHECK_NEAR(q[static_cast<usize>(ParticleParam::ColorRandom)], 0.5, 1e-6);
    AUREA_CHECK_NEAR(q[static_cast<usize>(ParticleParam::Collision)], static_cast<f64>(ParticleCollision::Sphere), 1e-6);
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// Partículas no espaço da cena (8.2, frente C): cena 3D com profundidade,
// câmera, Null 3D, espaço mundo/local, emissão no nascimento, desfoque por
// tempo, seek e salvar/reabrir.
// -----------------------------------------------------------------------------
namespace {

Composition* current_comp(Engine& e) { return e.project()->timeline().composition(e.project()->timeline().current()); }

/// Partícula de medida: ponto parado no centro, sem forças, quadrado opaco
/// branco de tamanho fixo — a caixa acesa mede posição e tamanho.
u64 measuring_particles(Engine& e, f32 size, f32 rate, f32 life) {
    auto r = e.add_particles(0);
    AUREA_CHECK(r.ok());
    if (!r.ok()) return 0;
    Layer* l = current_comp(e)->layer(LayerId::unpack(*r));
    ParticleData& p = l->particles;
    p.emitterType = static_cast<u32>(ParticleEmitter::Point);
    p.emitterSize = Vec2{0, 0};
    p.emitterOffset = Vec2{0, 0};
    p.rate = rate; p.burst = 0; p.lifetime = life; p.lifeRandom = 0;
    p.speed = 0; p.speedRandom = 0; p.spread = 0; p.direction = 0;
    p.gravity = Vec3{0, 0, 0}; p.wind = Vec3{0, 0, 0}; p.drag = 0;
    p.turbulence = 0; p.vortex = 0; p.attractor = 0; p.inheritVelocity = 0;
    p.trailLength = 0; p.auxCount = 0; p.collision = 0;
    p.startSize = size; p.endSize = size;
    p.startOpacity = 1; p.endOpacity = 1;
    p.startColor = Vec4{1, 1, 1, 1}; p.endColor = Vec4{1, 1, 1, 1};
    p.particleType = static_cast<u32>(ParticleShape::Square);
    p.softness = 0; p.rotation = 0; p.rotationRandom = 0; p.spin = 0;
    p.sizeRandom = 0; p.opacityRandom = 0; p.colorRandom = 0;
    p.blendMode = 0;
    p.maxParticles = 20000;
    return *r;
}

/// Câmera ativa na posição (px do mundo), olhando para +Z.
u64 add_test_camera(Engine& e, Vec3 pos, f32 fovDeg) {
    Composition* comp = current_comp(e);
    const LayerId cid = comp->add_layer(LayerKind::Camera, "camera de teste");
    Layer* c = comp->layer(cid);
    c->start = FrameIndex{0};
    c->end = comp->duration();
    c->transform.anchor = Vec3{0, 0, 0};
    c->transform.position = pos;
    c->camera.fov = fovDeg;
    c->camera.active = true;
    return cid.pack();
}

/// A câmera padrão de uma composição de 180 px de altura: z = −216, FOV 45,24°.
constexpr f32 kDefaultFov180 = 45.2397f;

f32 box_cx(const Box8& b) { return 0.5f * static_cast<f32>(b.x0 + b.x1); }

u32 count_where(const Image8& img, u32 x0, u32 y0, u32 x1, u32 y1, bool (*pred)(const u8*)) {
    u32 n = 0;
    for (u32 y = y0; y <= y1 && y < img.height; ++y)
        for (u32 x = x0; x <= x1 && x < img.width; ++x) n += pred(img.at(x, y)) ? 1u : 0u;
    return n;
}
bool is_white(const u8* p) { return p[0] > 200 && p[1] > 200 && p[2] > 200; }
bool is_lit(const u8* p) { return p[0] > 8 || p[1] > 8 || p[2] > 8; }

void particle_key(Engine& e, u64 pid, ParticleParam p, i64 f, f32 v) {
    Command c;
    c.type = CommandType::KeyframeInsert;
    c.keyframe.track.layer = LayerId::unpack(pid);
    c.keyframe.track.property = TrackProperty::ParticleParam;
    c.keyframe.track.effectIndex = kInvalidIndex;
    c.keyframe.track.effectParamIndex = static_cast<u32>(p);
    c.keyframe.time = FrameIndex{f};
    c.keyframe.value = v;
    AUREA_CHECK(e.apply_command(c).ok());
}

} // namespace

AUREA_TEST(Gpu, Particles3DAreHiddenBehindPlanesAndModelsAndShownInFront) {
    AUREA_REQUIRE_GPU();
    // Partículas 3D brancas cobrindo o quadro, com um oclusor opaco ACIMA na
    // pilha: atrás dele em profundidade = escondidas; na frente = visíveis.
    auto run = [&](f32 partZ, bool model) {
        Scene3DRig rig(320, 180);
        const u64 pid = measuring_particles(rig.e, 10.0f, 4000.0f, 5.0f);
        if (model) {
            ModelImport mi;
            mi.path = write_triangle_gltf(true, 1.0f, 0.0f, 0.0f);
            AUREA_CHECK(rig.e.import_model(mi).ok());
        } else {
            auto s = rig.e.add_shape(0);
            AUREA_CHECK(s.ok());
            if (!s.ok()) return 0u;
            Layer* sh = current_comp(rig.e)->layer(LayerId::unpack(*s));
            sh->shape.shapeType = 0;
            sh->shape.bounds = Rect{0, 0, 120, 120};
            sh->shape.cornerRadius = 0;
            sh->shape.strokeWidth = 0;
            sh->shape.filled = true;
            sh->shape.fillColor = Vec4{0, 0.3f, 1, 1};
            sh->transform.anchor = Vec3{60, 60, 0};
            sh->transform.position = Vec3{160, 90, 0};
            sh->transform.scale = Vec3{1, 1, 1};
            sh->threeD = true;
        }
        Layer* pl = current_comp(rig.e)->layer(LayerId::unpack(pid));
        pl->threeD = true;
        pl->particles.emitterType = static_cast<u32>(ParticleEmitter::Box);
        pl->particles.emitterSize = Vec2{320, 180};
        pl->transform.position.z = partZ;
        seek_frame(rig.e, 30);
        const Image8 img = rig.capture(320);
        return count_where(img, 157, 87, 163, 93, is_white);   // 49 px no meio do oclusor
    };
    const u32 behindPlane = run(100.0f, false), frontPlane = run(-100.0f, false);
    const u32 behindModel = run(100.0f, true), frontModel = run(-100.0f, true);
    std::printf("    particulas 3D (px brancos no centro, de 49): plano atras %u / frente %u; modelo atras %u / frente %u\n",
                behindPlane, frontPlane, behindModel, frontModel);
    AUREA_CHECK_EQ(behindPlane, 0u);
    AUREA_CHECK(frontPlane > 30u);
    AUREA_CHECK_EQ(behindModel, 0u);
    AUREA_CHECK(frontModel > 30u);
}

AUREA_TEST(Gpu, Particles3DFollowTheCamera) {
    AUREA_REQUIRE_GPU();
    // Um bloco de partículas de 20 px no centro, visto pela câmera: longe =
    // menor, FOV fechado = maior, girar = sai do centro, câmera animada = muda
    // entre quadros.
    Scene3DRig rig(320, 180);
    const u64 pid = measuring_particles(rig.e, 20.0f, 30.0f, 5.0f);
    const u64 cid = add_test_camera(rig.e, Vec3{160, 90, -216}, kDefaultFov180);
    current_comp(rig.e)->layer(LayerId::unpack(pid))->threeD = true;
    auto cam = [&]() { return current_comp(rig.e)->layer(LayerId::unpack(cid)); };
    seek_frame(rig.e, 20);
    const Box8 base = lit_box(rig.capture(320));
    cam()->transform.position.z = -432.0f;
    const Box8 far = lit_box(rig.capture(320));
    cam()->transform.position.z = -216.0f;
    cam()->camera.fov = 2.0f * std::atan(0.5f * std::tan(kDefaultFov180 * 0.5f * kDeg2Rad)) / kDeg2Rad;   // zoom 2x
    const Box8 narrow = lit_box(rig.capture(320));
    cam()->camera.fov = kDefaultFov180;
    cam()->transform.rotation = Vec3{0, 15, 0};
    const Box8 turned = lit_box(rig.capture(320));
    cam()->transform.rotation = Vec3{0, 0, 0};
    Track& pz = cam()->tracks.get_or_create(TrackProperty::PositionZ);
    pz.set(FrameIndex{0}, -216.0f);
    pz.set(FrameIndex{60}, -432.0f);
    seek_frame(rig.e, 0);
    const Box8 a0 = lit_box(rig.capture(320));
    seek_frame(rig.e, 60);
    const Box8 a60 = lit_box(rig.capture(320));
    std::printf("    camera: base %ux%u em x %.1f; longe %u; zoom 2x %u; girada Y15 x %.1f; animada q0 %u -> q60 %u\n",
                base.w(), base.h(), box_cx(base), far.w(), narrow.w(), box_cx(turned), a0.w(), a60.w());
    AUREA_CHECK(base.w() >= 19 && base.w() <= 22);
    AUREA_CHECK(std::fabs(box_cx(base) - 160.0f) < 2.0f);
    AUREA_CHECK(far.w() >= 9 && far.w() <= 12);
    AUREA_CHECK(narrow.w() >= 39 && narrow.w() <= 43);
    AUREA_CHECK(std::fabs(box_cx(turned) - 160.0f) > 30.0f);
    AUREA_CHECK(a0.w() >= 19 && a0.w() <= 22);
    AUREA_CHECK(a60.w() >= 9 && a60.w() <= 12);
}

AUREA_TEST(Gpu, ParticlesChildOfAnimated3DNullFollowIt) {
    AUREA_REQUIRE_GPU();
    // Filha de um Null 3D que anda em X e se afasta em Z: o emissor vai junto
    // (posição na tela e tamanho pela perspectiva).
    Scene3DRig rig(320, 180);
    const u64 pid = measuring_particles(rig.e, 20.0f, 30.0f, 5.0f);
    auto n = rig.e.add_null(true);
    AUREA_CHECK(n.ok());
    if (!n.ok()) return;
    set_parent(rig.e, pid, *n);
    Layer* nl = current_comp(rig.e)->layer(LayerId::unpack(*n));
    Track& px = nl->tracks.get_or_create(TrackProperty::PositionX);
    Track& pz = nl->tracks.get_or_create(TrackProperty::PositionZ);
    px.set(FrameIndex{0}, 100.0f);
    px.set(FrameIndex{30}, 220.0f);
    pz.set(FrameIndex{0}, 0.0f);
    pz.set(FrameIndex{30}, 216.0f);
    seek_frame(rig.e, 0);
    const Box8 b0 = lit_box(rig.capture(320));
    seek_frame(rig.e, 30);
    const Box8 b30 = lit_box(rig.capture(320));
    std::printf("    filha do Null 3D: q0 x %.1f larg %u; q30 x %.1f larg %u\n", box_cx(b0), b0.w(), box_cx(b30), b30.w());
    // q0: x 100, 20 px. q30: x 220 a 2x a distância = 160 + 60/2 = 190, 10 px.
    AUREA_CHECK(std::fabs(box_cx(b0) - 100.0f) < 3.0f);
    AUREA_CHECK(b0.w() >= 19 && b0.w() <= 22);
    AUREA_CHECK(std::fabs(box_cx(b30) - 190.0f) < 3.0f);
    AUREA_CHECK(b30.w() >= 9 && b30.w() <= 12);
}

AUREA_TEST(Gpu, ParticlesWorldSpaceStayWhereBornLocalSpaceIsDragged) {
    AUREA_REQUIRE_GPU();
    // A camada anda de x 60 a 260 em 2 s. Local: todas as vivas andam com
    // ela (um bloco só). Mundo: cada uma fica onde nasceu (um rastro).
    auto run = [&](bool world, bool threeD) {
        Scene3DRig rig(320, 180);
        const u64 pid = measuring_particles(rig.e, 6.0f, 30.0f, 3.0f);
        Layer* l = current_comp(rig.e)->layer(LayerId::unpack(pid));
        l->threeD = threeD;
        l->particles.emitterSpace = world ? 1u : 0u;
        Track& px = l->tracks.get_or_create(TrackProperty::PositionX);
        px.set(FrameIndex{0}, 60.0f);
        px.set(FrameIndex{60}, 260.0f);
        seek_frame(rig.e, 60);
        return lit_box(rig.capture(320));
    };
    const Box8 local2 = run(false, false), world2 = run(true, false);
    const Box8 local3 = run(false, true), world3 = run(true, true);
    std::printf("    espaco local/mundo: 2D %u / %u px (x %.1f / %.1f); 3D %u / %u px\n", local2.w(), world2.w(),
                box_cx(local2), box_cx(world2), local3.w(), world3.w());
    AUREA_CHECK(local2.w() <= 8 && std::fabs(box_cx(local2) - 260.0f) < 2.0f);
    AUREA_CHECK(world2.w() >= 190 && world2.x1 >= 258 && world2.x0 <= 63);
    AUREA_CHECK(local3.w() <= 8);
    AUREA_CHECK(world3.w() >= 190);
}

AUREA_TEST(Gpu, ParticlesAnimatedEmissionKeepsBornParticles) {
    AUREA_REQUIRE_GPU();
    // Jato para a direita a 60 px/s. Em 1 s a direção vira para a esquerda
    // (ou a taxa vai a zero): quem nasceu antes continua EXATAMENTE onde
    // estaria sem o keyframe; só quem nasce depois muda.
    enum Mode { Still, Direction, Rate };
    auto run = [&](Mode mode) {
        Scene3DRig rig(320, 180);
        const u64 pid = measuring_particles(rig.e, 6.0f, 20.0f, 3.0f);
        current_comp(rig.e)->layer(LayerId::unpack(pid))->particles.speed = 60.0f;
        if (mode == Direction) {
            particle_key(rig.e, pid, ParticleParam::Direction, 30, 0.0f);
            particle_key(rig.e, pid, ParticleParam::Direction, 31, 180.0f);
        }
        if (mode == Rate) {
            particle_key(rig.e, pid, ParticleParam::Rate, 30, 20.0f);
            particle_key(rig.e, pid, ParticleParam::Rate, 31, 0.0f);
        }
        seek_frame(rig.e, 60);
        return rig.capture(320);
    };
    const Image8 still = run(Still), dir = run(Direction), rate = run(Rate);
    // Nascidas antes de 1 s estão em x 220..280 (as de depois, em 160..220).
    auto regionDiff = [&](const Image8& a, const Image8& b) {
        u32 w = 0;
        for (u32 y = 80; y <= 100; ++y)
            for (u32 x = 226; x <= 274; ++x)
                for (int c = 0; c < 3; ++c) w = std::max<u32>(w, static_cast<u32>(std::abs(a.at(x, y)[c] - b.at(x, y)[c])));
        return w;
    };
    const u32 dDir = regionDiff(still, dir), dRate = regionDiff(still, rate);
    const u32 oldLit = count_where(still, 226, 80, 274, 100, is_lit);
    const u32 leftDir = count_where(dir, 100, 80, 154, 100, is_lit), leftStill = count_where(still, 100, 80, 154, 100, is_lit);
    const u32 newRate = count_where(rate, 166, 80, 214, 100, is_lit), newStill = count_where(still, 166, 80, 214, 100, is_lit);
    std::printf("    emissao animada: nascidas antes (%u px acesos) dif direcao %u, taxa %u; esquerda %u (parado %u); "
                "novas com taxa 0: %u (parado %u)\n", oldLit, dDir, dRate, leftDir, leftStill, newRate, newStill);
    AUREA_CHECK(oldLit > 50u);
    AUREA_CHECK_EQ(dDir, 0u);
    AUREA_CHECK_EQ(dRate, 0u);
    AUREA_CHECK(leftDir > 50u && leftStill == 0u);
    AUREA_CHECK(newRate == 0u && newStill > 50u);
}

AUREA_TEST(Gpu, ParticlesMotionBlurSpreadsAlongMotionAndExportMatchesFullPreview) {
    AUREA_REQUIRE_GPU();
    // Uma partícula a 600 px/s para a direita. Com o desfoque (180°), ela
    // vira um traço de ~10 px NA DIREÇÃO do movimento; a altura não muda.
    auto run = [&](bool threeD, bool blur) {
        Scene3DRig rig(320, 180);
        const u64 pid = measuring_particles(rig.e, 4.0f, 0.1f, 5.0f);
        Layer* l = current_comp(rig.e)->layer(LayerId::unpack(pid));
        l->threeD = threeD;
        l->particles.speed = 600.0f;
        l->particles.emitterOffset = Vec2{-140.0f, 0.0f};
        if (blur) AUREA_CHECK(rig.e.set_motion_blur(pid, true));
        seek_frame(rig.e, 10);
        return lit_box(rig.capture(320));
    };
    const Box8 s2 = run(false, false), b2 = run(false, true), s3 = run(true, false), b3 = run(true, true);
    std::printf("    desfoque: 2D %ux%u em x %.1f -> %ux%u; 3D %ux%u -> %ux%u\n", s2.w(), s2.h(), box_cx(s2), b2.w(), b2.h(),
                s3.w(), s3.h(), b3.w(), b3.h());
    AUREA_CHECK(std::fabs(box_cx(s2) - 220.0f) < 2.0f);
    AUREA_CHECK(b2.w() >= s2.w() + 6 && b2.w() <= s2.w() + 12);
    AUREA_CHECK(b2.h() + 1 >= s2.h() && b2.h() <= s2.h() + 1);
    AUREA_CHECK(b3.w() >= s3.w() + 6 && b3.w() <= s3.w() + 12);
    AUREA_CHECK(b3.h() + 1 >= s3.h() && b3.h() <= s3.h() + 1);

    // Export (qualidade final) = prévia com as mesmas amostras e qualidade cheia.
    auto frames = [&](bool threeD, f32& worst, f32& lit) {
        Scene sc(320, 180);
        const LayerId id = sc.comp->add_layer(LayerKind::ParticleSystem, "particulas");
        Layer* l = sc.comp->layer(id);
        l->transform.anchor = Vec3{160, 90, 0};
        l->transform.position = Vec3{160, 90, 0};
        l->threeD = threeD;
        l->motionBlur = true;
        ParticleData& p = l->particles;
        p = ParticleData{};
        p.emitterType = static_cast<u32>(ParticleEmitter::Point);
        p.rate = 30; p.lifetime = 2; p.lifeRandom = 0; p.speed = 300; p.spread = 40; p.direction = 0;
        p.gravity = Vec3{0, -200, 0}; p.trailLength = 0;
        p.emitterOffset = Vec2{-100, 0};
        p.particleType = static_cast<u32>(ParticleShape::Circle);
        MotionBlurSettings& mb = sc.comp->motion_blur();
        mb.enabled = true;
        mb.samples = 16;
        mb.previewSamples = 16;
        const FloatImage prev = sc.render(FrameIndex{20}, 1, false);
        const FloatImage fin = sc.render(FrameIndex{20}, 1, true);
        worst = 0.0f;
        for (usize i = 0; i < prev.px.size() && i < fin.px.size(); ++i) worst = std::max(worst, std::fabs(prev.px[i] - fin.px[i]));
        lit = 0.0f;
        for (usize i = 0; i < fin.px.size(); i += 4) lit += (fin.px[i] + fin.px[i + 1] + fin.px[i + 2]) > 0.05f ? 1.0f : 0.0f;
    };
    f32 w2 = 0, w3 = 0, lit2 = 0, lit3 = 0;
    frames(false, w2, lit2);
    frames(true, w3, lit3);
    std::printf("    export x previa cheia: 2D dif %.6f (%.0f px), 3D dif %.6f (%.0f px)\n", w2, lit2, w3, lit3);
    AUREA_CHECK(lit2 > 100.0f && lit3 > 100.0f);
    AUREA_CHECK(w2 <= 1e-3f);
    AUREA_CHECK(w3 <= 1e-3f);
}

namespace {
/// Cena com tudo ligado: câmera animada, Null 3D animado com as partículas
/// filhas (3D, espaço mundo, taxa e velocidade animadas, desfoque), um modelo
/// e um plano.
void build_everything_on(Engine& e) {
    const u64 pid = measuring_particles(e, 6.0f, 60.0f, 2.0f);
    ModelImport mi;
    mi.path = write_triangle_gltf(true, 1.0f, 0.0f, 0.0f);
    AUREA_CHECK(e.import_model(mi).ok());
    auto s = e.add_shape(0);
    auto n = e.add_null(true);
    AUREA_CHECK(s.ok() && n.ok());
    if (!s.ok() || !n.ok()) return;
    const u64 cid = add_test_camera(e, Vec3{160, 90, -216}, kDefaultFov180);
    set_parent(e, pid, *n);
    Composition* comp = current_comp(e);
    Layer* sh = comp->layer(LayerId::unpack(*s));
    sh->shape.fillColor = Vec4{0, 0.3f, 1, 1};
    sh->transform.position = Vec3{230, 60, 40};
    sh->threeD = true;
    Layer* nl = comp->layer(LayerId::unpack(*n));
    Track& nx = nl->tracks.get_or_create(TrackProperty::PositionX);
    nx.set(FrameIndex{0}, 90.0f);
    nx.set(FrameIndex{120}, 230.0f);
    Track& nry = nl->tracks.get_or_create(TrackProperty::RotationY);
    nry.set(FrameIndex{0}, 0.0f);
    nry.set(FrameIndex{120}, 40.0f);
    Layer* cl = comp->layer(LayerId::unpack(cid));
    Track& cz = cl->tracks.get_or_create(TrackProperty::PositionZ);
    cz.set(FrameIndex{0}, -216.0f);
    cz.set(FrameIndex{150}, -300.0f);
    Layer* pl = comp->layer(LayerId::unpack(pid));
    pl->threeD = true;
    pl->particles.emitterSpace = 1;
    pl->particles.speed = 120.0f;
    pl->particles.spread = 90.0f;
    pl->particles.direction = -90.0f;
    pl->particles.gravity = Vec3{0, -200, 50};
    pl->particles.emitterType = static_cast<u32>(ParticleEmitter::Sphere);
    pl->particles.emitterRadius = 20.0f;
    particle_key(e, pid, ParticleParam::Rate, 0, 20.0f);
    particle_key(e, pid, ParticleParam::Rate, 90, 120.0f);
    particle_key(e, pid, ParticleParam::Speed, 0, 60.0f);
    particle_key(e, pid, ParticleParam::Speed, 90, 220.0f);
    AUREA_CHECK(e.set_motion_blur(pid, true));
}
} // namespace

AUREA_TEST(Gpu, ParticlesSceneSeekRoundTripIsExactWithEverythingOn) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    build_everything_on(rig.e);
    seek_frame(rig.e, 40);
    const Image8 a = rig.capture(320);
    seek_frame(rig.e, 150);
    (void)rig.capture(320);
    seek_frame(rig.e, 3);
    (void)rig.capture(320);
    seek_frame(rig.e, 40);
    const Image8 b = rig.capture(320);
    const u32 worst = max_diff(a, b);
    std::printf("    cena com tudo ligado: cobertura %.4f; ida e volta diferenca %u\n", coverage(a), worst);
    AUREA_CHECK(coverage(a) > 0.01f);
    AUREA_CHECK_EQ(worst, 0u);
}

AUREA_TEST(Gpu, ParticlesSceneSurvivesSaveReopen) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    build_everything_on(rig.e);
    seek_frame(rig.e, 70);
    const Image8 before = rig.capture(320);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_particulas_cena.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    seek_frame(rig.e, 70);
    const Image8 after = rig.capture(320);
    const u32 worst = max_diff(before, after);
    std::printf("    cena de particulas salva e reaberta: diferenca %u (cobertura %.4f)\n", worst, coverage(before));
    AUREA_CHECK(coverage(before) > 0.01f);
    AUREA_CHECK_EQ(worst, 0u);
    std::remove(path.c_str());
}
