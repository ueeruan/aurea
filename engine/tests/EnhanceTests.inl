// =============================================================================
//  Aprimorar (aurea.enhance.video, 0.3.0) e o "Substituir quadros duplicados"
//  do Flow Retime — testes de GPU. Incluído no fim de test_gpu.cpp (usa a
//  `Scene`, gpu(), max_abs_diff e srgb_encode de lá).
//
//  Tudo medido no PIXEL: identidade exata, PSNR contra a imagem limpa, posição
//  da borda que anda (fantasma), métrica de blocos 8×8, acutância e
//  sobressinal, grão determinístico, a escolha do Automático e o quadro do
//  meio refeito no lugar do duplicado. O custo em 1080p é impresso.
// =============================================================================
#include <functional>

namespace {
namespace enh {

const ParameterRegistry& params() { return *gpu().effects.params(effect_type_id(effect_keys::kEnhance)); }

u32 param(const char* id) {
    const u32 i = params().find(id);
    AUREA_CHECK_MSG(i != kInvalidIndex, id);
    return i;
}

void set(EffectInstance& e, const char* id, f32 v) {
    const u32 i = param(id);
    if (i < e.params.size()) e.params[i].constant = ParamValue::scalar(v);
}

/// Aprimorar no Manual, tudo zero e só espacial: cada teste liga o que mede.
EffectInstance& add_manual(Scene& s, LayerId id) {
    EffectInstance& e = s.add_effect(id, effect_keys::kEnhance);
    set(e, "model", 5.0f);
    set(e, "temporal_frames", 0.0f);
    return e;
}

/// Gerador determinístico (splitmix64) com gaussiana de Box-Muller.
struct Rng {
    u64 s;
    explicit Rng(u64 seed) : s(seed) {}
    u64 next() {
        u64 z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    f64 uniform() { return (static_cast<f64>(next() >> 11) + 0.5) / 9007199254740992.0; }
    f64 gauss() { return std::sqrt(-2.0 * std::log(uniform())) * std::cos(6.283185307179586 * uniform()); }
};

u8 to_code(f64 v) { return static_cast<u8>(std::clamp(std::lround(v), 0l, 255l)); }

/// Cena por partes lisa: degradê, disco claro, retângulo escuro e uma faixa de
/// textura média (o que um vídeo tem e o redutor de ruído não pode apagar).
ImagePixels scene_image(u32 w, u32 h) {
    ImagePixels px = uniform_image(w, h, 0, 0, 0);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * w + x) * 4];
            f64 r = 70.0 + 90.0 * x / w, g = 60.0 + 80.0 * y / h, b = 110.0;
            const f64 dx = x - w * 0.3, dy = y - h * 0.5;
            if (dx * dx + dy * dy < (h * 0.22) * (h * 0.22)) { r = 220; g = 210; b = 190; }
            if (x > w * 0.6 && x < w * 0.85 && y > h * 0.2 && y < h * 0.55) { r = 40; g = 45; b = 50; }
            if (y > h * 0.7) {
                const f64 t = 18.0 * std::sin(x * 0.45) * std::sin(y * 0.3);
                r += t; g += t; b += t;
            }
            p[0] = to_code(r); p[1] = to_code(g); p[2] = to_code(b);
        }
    return px;
}

ImagePixels with_noise(const ImagePixels& clean, f64 sigmaCodes, u64 seed) {
    ImagePixels out = clean;
    Rng rng(seed);
    for (usize i = 0; i < out.rgba.size(); i += 4)
        for (int c = 0; c < 3; ++c) out.rgba[i + c] = to_code(clean.rgba[i + c] + sigmaCodes * rng.gauss());
    return out;
}

f32 enc(f32 linear) { return srgb_encode(linear); }
f32 luma(const FloatImage& img, u32 x, u32 y) { return enc(img.v(x, y).y); }

/// PSNR (dB) nos valores codificados (sRGB 0..1), sem a moldura de `m` px.
f64 psnr(const FloatImage& a, const FloatImage& ref, u32 m) {
    f64 se = 0.0;
    u64 n = 0;
    for (u32 y = m; y + m < a.height; ++y)
        for (u32 x = m; x + m < a.width; ++x)
            for (int c = 0; c < 3; ++c) {
                const f64 d = enc(a.at(x, y)[c]) - enc(ref.at(x, y)[c]);
                se += d * d;
                ++n;
            }
    return 10.0 * std::log10(1.0 / std::max(se / std::max<u64>(n, 1), 1e-12));
}

/// Desvio (sRGB) da diferença entre duas imagens num retângulo.
f64 residual(const FloatImage& a, const FloatImage& b, u32 x0, u32 y0, u32 x1, u32 y1) {
    f64 s = 0.0, s2 = 0.0;
    u64 n = 0;
    for (u32 y = y0; y < y1; ++y)
        for (u32 x = x0; x < x1; ++x) {
            const f64 d = luma(a, x, y) - luma(b, x, y);
            s += d; s2 += d * d; ++n;
        }
    const f64 m = s / std::max<u64>(n, 1);
    return std::sqrt(std::max(0.0, s2 / std::max<u64>(n, 1) - m * m));
}

// --- Vídeo de teste: luma de uma função (NV12 de verdade, croma neutro) -------
using LumaFn = std::function<u8(u32 x, u32 y, u32 frame)>;

class FnFrame final : public DecodedFrame {
public:
    std::vector<u8> y, uv;
    bool owns_cpu_backing() const noexcept override {
        return !y.empty() && planes[0] == y.data() && planes[1] == uv.data() && y.size() + uv.size() >= approx_bytes();
    }
};

class FnDecoder final : public VideoDecoderBackend {
public:
    FnDecoder(u32 w, u32 h, u32 frames, LumaFn fn) : w_(w), h_(h), frames_(frames), fn_(std::move(fn)) {
        info_.codedWidth = w;
        info_.codedHeight = h;
        info_.fps = 30.0;
        info_.durationUs = static_cast<i64>(std::llround(frames * 1e6 / 30.0));
        std::snprintf(info_.codec, sizeof(info_.codec), "%s", "video/funcao");
        std::snprintf(info_.decoderName, sizeof(info_.decoderName), "%s", "funcao");
    }
    const VideoStreamInfo& info() const noexcept override { return info_; }
    Status seek_to_keyframe(i64 targetUs) noexcept override {
        pos_ = (frame_of(targetUs) / 10u) * 10u;
        return OkStatus;
    }
    Status next_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs, bool& eos) noexcept override {
        eos = false;
        if (pos_ >= frames_) { eos = true; outPtsUs = pts_of(frames_ - 1); return OkStatus; }
        const u32 index = pos_++;
        outPtsUs = pts_of(index);
        eos = pos_ >= frames_;
        if (outPtsUs < deliverFromUs) return OkStatus;
        auto* f = new FnFrame();
        f->ptsUs = outPtsUs;
        f->width = w_; f->height = h_; f->visibleWidth = w_; f->visibleHeight = h_;
        f->format = PixelFormat::NV12;
        f->bufferId = index;
        f->y.resize(static_cast<usize>(w_) * h_);
        for (u32 yy = 0; yy < h_; ++yy)
            for (u32 xx = 0; xx < w_; ++xx) f->y[static_cast<usize>(yy) * w_ + xx] = fn_(xx, yy, index);
        f->uv.assign(static_cast<usize>((w_ + 1) / 2) * ((h_ + 1) / 2) * 2, 128);
        f->planes[0] = f->y.data();
        f->planes[1] = f->uv.data();
        f->strides[0] = w_;
        f->strides[1] = ((w_ + 1) / 2) * 2;
        f->planeCount = 2;
        out = FrameRef::adopt(f);
        return OkStatus;
    }
    u32 max_live_frames() const noexcept override { return 12; }
    i64 keyframe_interval_us() const noexcept override { return static_cast<i64>(std::llround(10 * 1e6 / 30.0)); }

private:
    i64 pts_of(u32 i) const { return static_cast<i64>(std::llround(i * 1e6 / 30.0)); }
    u32 frame_of(i64 us) const {
        const f64 f = static_cast<f64>(us) * 30.0 / 1e6 + 0.5;
        const u32 i = f <= 0.0 ? 0u : static_cast<u32>(f);
        return std::min(i, frames_ - 1);
    }
    u32 w_, h_, frames_;
    LumaFn fn_;
    VideoStreamInfo info_{};
    u32 pos_ = 0;
};

class FnFactory final : public VideoSourceFactory {
public:
    FnFactory(u32 w, u32 h, u32 frames, LumaFn fn) : w_(w), h_(h), frames_(frames), fn_(std::move(fn)) {}
    bool probe(const char*, MediaProbe& out) override {
        FnDecoder d(w_, h_, frames_, fn_);
        out.video = d.info();
        out.video.color.fromStream = true;
        out.hasVideo = true;
        return true;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        return std::make_unique<FnDecoder>(w_, h_, frames_, fn_);
    }

private:
    u32 w_, h_, frames_;
    LumaFn fn_;
};

/// Camada de vídeo da função `factory` em `host` (a raiz ou uma filha), 1:1.
LayerId add_video(Scene& s, Composition* host, FnFactory& factory, u32 w, u32 h, u32 frames) {
    s.media.set_factory(&factory);
    Asset a;
    a.kind = AssetKind::Video;
    a.video.width = w;
    a.video.height = h;
    a.video.fps = 30.0;
    const AssetId aid = s.project.add_asset(std::move(a));
    const LayerId id = host->add_layer(LayerKind::Video, "video");
    Layer* l = host->layer(id);
    l->source = aid;
    l->end = FrameIndex{static_cast<i64>(frames)};
    l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0};
    l->transform.position = Vec3{w * 0.5f, h * 0.5f, 0};
    return id;
}

/// Ruído determinístico por pixel e quadro (sem estado: o decoder pode
/// decodificar o mesmo quadro de novo e ele sai igual).
f64 pixel_noise(u32 x, u32 y, u32 i) {
    u64 h = (static_cast<u64>(x) * 73856093u) ^ (static_cast<u64>(y) * 19349663u) ^ (static_cast<u64>(i) * 83492791u);
    Rng r(h * 0x9E3779B97F4A7C15ull + 12345u);
    return r.gauss();
}

/// Ruído uniforme barato (desvio ≈ 1) para o vídeo 1080p do teste de custo.
f64 fast_noise(u32 x, u32 y, u32 i) {
    u32 h = x * 0x8DA6B343u ^ y * 0xD8163841u ^ i * 0xCB1AB31Fu;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
    return (static_cast<f64>(h & 0xFFFFu) / 65535.0 - 0.5) * 3.4641;
}

/// A borda que anda: escuro à esquerda, claro à direita, 3 px por quadro.
constexpr u32 kEdgeW = 128, kEdgeH = 72;
f64 edge_x(u32 i) { return 40.0 + 3.0 * i; }
u8 edge_luma(u32 x, u32 y, u32 i, f64 sigma) {
    const f64 base = static_cast<f64>(x) < edge_x(i) ? 70.0 : 170.0;
    return to_code(base + (sigma > 0.0 ? sigma * pixel_noise(x, y, i) : 0.0));
}

/// Cruzamento da metade do degrau na linha `y` (x em índices de pixel).
f64 crossing(const FloatImage& img, u32 y, f64 lo, f64 hi, u32 from, u32 to) {
    const f64 mid = 0.5 * (lo + hi);
    for (u32 x = from; x + 1 < to; ++x) {
        const f64 a = luma(img, x, y), b = luma(img, x + 1, y);
        if (a < mid && b >= mid) return x + (mid - a) / std::max(1e-6, b - a);
    }
    return -1.0;
}

f64 mean_crossing(const FloatImage& img, f64 lo, f64 hi, u32 from, u32 to) {
    f64 s = 0.0;
    u32 n = 0;
    for (u32 y = 8; y + 8 < img.height; ++y) {
        const f64 c = crossing(img, y, lo, hi, from, to);
        if (c >= 0.0) { s += c; ++n; }
    }
    return n ? s / n : -1.0;
}

f64 band_mean(const FloatImage& img, u32 x0, u32 x1) {
    f64 s = 0.0;
    u32 n = 0;
    for (u32 y = 8; y + 8 < img.height; ++y)
        for (u32 x = x0; x < x1; ++x) { s += luma(img, x, y); ++n; }
    return n ? s / n : 0.0;
}

} // namespace enh
} // namespace

AUREA_TEST(EnhanceGpu, DeclaresParametersSectionsPresetsAndFlowRetimeTools) {
    const EffectTypeId type = effect_type_id(effect_keys::kEnhance);
    const Effect* fx = gpu().effects.find(type);
    const ParameterRegistry* p = gpu().effects.params(type);
    AUREA_CHECK(fx != nullptr && p != nullptr);
    if (!fx || !p) return;
    AUREA_CHECK(std::strcmp(fx->info().name, "Aprimorar") == 0);
    AUREA_CHECK(std::strcmp(fx->info().category, "Nitidez") == 0);
    static const char* const kIds[] = {"model", "revert_compression", "recover_details", "sharpen", "reduce_noise", "dehalo",
                                       "antialias_deblur", "add_noise", "grain_size", "recover_original_detail",
                                       "temporal_frames", "quality", "mix"};
    AUREA_CHECK_EQ(p->count(), 13u);
    for (u32 i = 0; i < 13 && i < p->count(); ++i) AUREA_CHECK_MSG(std::strcmp(p->at(i).id, kIds[i]) == 0, kIds[i]);
    AUREA_CHECK_EQ(p->at(0).enumCount, 6u);
    AUREA_CHECK_EQ(p->section_count(), 3u);
    AUREA_CHECK_EQ(fx->presets().size(), static_cast<usize>(6));
    for (const EffectPreset& preset : fx->presets())
        for (const EffectPresetValue& v : preset.values) AUREA_CHECK(v.param < p->count());
    AUREA_CHECK(fx->wants_neighbors());
    // Flow Retime: as duas ferramentas novas no FIM (índices 4 e 5) e a seção.
    const ParameterRegistry* r = gpu().effects.params(effect_type_id(effect_keys::kFlowRetime));
    AUREA_CHECK(r != nullptr);
    if (!r) return;
    AUREA_CHECK_EQ(r->count(), 6u);
    AUREA_CHECK_EQ(r->find("replace_duplicates"), 4u);
    AUREA_CHECK_EQ(r->find("duplicate_sensitivity"), 5u);
    AUREA_CHECK(r->at(4).type == ParamType::Bool && r->at(4).defaultValue.v[0] == 0.0f);
    AUREA_CHECK_NEAR(r->at(5).defaultValue.v[0], 10.0f, 1e-6f);
    AUREA_CHECK_EQ(r->section_count(), 1u);
}

AUREA_TEST(EnhanceGpu, ManualAtZeroIsABitExactPassthrough) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 64);
    const LayerId id = s.image(enh::with_noise(enh::scene_image(96, 64), 6.0, 7), 48, 32);
    const FloatImage plain = s.render(FrameIndex{3});
    EffectInstance& e = enh::add_manual(s, id);
    enh::set(e, "temporal_frames", 3.0f);   // vizinhos pedidos, mas nada ligado: continua identidade
    const FloatImage zero = s.render(FrameIndex{3});
    AUREA_CHECK_MSG(zero.px == plain.px, "Manual com tudo em 0 tem de ser a entrada, bit a bit");
    // Negativo digitado no Manual também é zero.
    enh::set(e, "sharpen", -40.0f);
    AUREA_CHECK_MSG(s.render(FrameIndex{3}).px == plain.px, "negativo no Manual = 0");
    // Mistura 0 em qualquer modelo: identidade.
    enh::set(e, "model", 0.0f);
    enh::set(e, "mix", 0.0f);
    AUREA_CHECK_MSG(s.render(FrameIndex{3}).px == plain.px, "mistura 0 = entrada");
    // E o efeito diz que é identidade (sai da cadeia, nenhum passe).
    const Effect* fx = gpu().effects.find(effect_type_id(effect_keys::kEnhance));
    std::vector<ParamValue> values(e.params.size());
    for (usize i = 0; i < values.size(); ++i) values[i] = e.params[i].constant;
    values[enh::param("model")] = ParamValue::scalar(5.0f);
    values[enh::param("mix")] = ParamValue::scalar(100.0f);
    values[enh::param("sharpen")] = ParamValue::scalar(0.0f);
    EffectEval ev;
    ev.effect = fx; ev.values = values.data(); ev.count = static_cast<u32>(values.size());
    AUREA_CHECK(fx->is_identity(ev));
    i32 offsets[kMaxTemporalNeighbors]{};
    AUREA_CHECK_EQ(fx->temporal_neighbors(ev, offsets), 0u);
    values[enh::param("reduce_noise")] = ParamValue::scalar(30.0f);
    AUREA_CHECK(!fx->is_identity(ev));
    AUREA_CHECK_EQ(fx->temporal_neighbors(ev, offsets), 6u);   // 3 de cada lado, os mais perto primeiro
    AUREA_CHECK(offsets[0] == -1 && offsets[1] == 1 && offsets[4] == -3 && offsets[5] == 3);
}

AUREA_TEST(EnhanceGpu, SpatialDenoiseRaisesPsnrAgainstTheCleanImage) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 192, H = 108;
    const ImagePixels cleanPx = enh::scene_image(W, H);
    FloatImage clean, noisy, denoised;
    {
        Scene s(W, H);
        (void)s.image(cleanPx, W * 0.5f, H * 0.5f);
        clean = s.render();
    }
    Scene s(W, H);
    const LayerId id = s.image(enh::with_noise(cleanPx, 10.0, 99), W * 0.5f, H * 0.5f);
    noisy = s.render();
    EffectInstance& e = enh::add_manual(s, id);
    enh::set(e, "reduce_noise", 60.0f);
    denoised = s.render();
    const f64 p0 = enh::psnr(noisy, clean, 4), p1 = enh::psnr(denoised, clean, 4);
    enh::set(e, "quality", 0.0f);
    const f64 fast = enh::psnr(s.render(), clean, 4);
    enh::set(e, "quality", 2.0f);
    const f64 high = enh::psnr(s.render(), clean, 4);
    std::printf("\n    ruido espacial: PSNR %.2f dB -> %.2f dB (rapida %.2f, alta %.2f)\n", p0, p1, fast, high);
    AUREA_CHECK_MSG(p1 > p0 + 3.0, "o redutor de ruido tem de subir o PSNR em 3 dB ou mais");
    AUREA_CHECK(fast > p0 + 2.0 && high > p0 + 3.0);
}

AUREA_TEST(EnhanceGpu, TemporalDenoiseOnAMovingNoisyEdgeWithoutGhosting) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = enh::kEdgeW, H = enh::kEdgeH, N = 60, T = 10;
    enh::FnFactory cleanVideo(W, H, N, [](u32 x, u32 y, u32 i) { return enh::edge_luma(x, y, i, 0.0); });
    enh::FnFactory noisyVideo(W, H, N, [](u32 x, u32 y, u32 i) { return enh::edge_luma(x, y, i, 9.0); });
    FloatImage clean;
    {
        Scene s(W, H);
        (void)enh::add_video(s, s.comp, cleanVideo, W, H, N);
        clean = s.render(FrameIndex{T}, 1, true);
    }
    Scene s(W, H);
    const LayerId id = enh::add_video(s, s.comp, noisyVideo, W, H, N);
    const FloatImage noisy = s.render(FrameIndex{T}, 1, true);
    EffectInstance& e = enh::add_manual(s, id);
    enh::set(e, "reduce_noise", 60.0f);
    const FloatImage spatial = s.render(FrameIndex{T}, 1, true);
    enh::set(e, "temporal_frames", 2.0f);
    const FloatImage temporal = s.render(FrameIndex{T}, 1, true);
    const f64 pn = enh::psnr(noisy, clean, 4), ps = enh::psnr(spatial, clean, 4), pt = enh::psnr(temporal, clean, 4);
    const f64 lo = enh::band_mean(clean, 8, 40), hi = enh::band_mean(clean, 90, 120);
    const u32 ex = static_cast<u32>(enh::edge_x(T));
    const f64 truth = enh::mean_crossing(clean, lo, hi, ex - 12, ex + 12);
    const f64 got = enh::mean_crossing(temporal, lo, hi, ex - 12, ex + 12);
    // Fantasma: o lado escuro logo atrás da borda (onde ela ESTAVA nos quadros
    // anteriores) e o claro logo à frente (onde ela VAI estar) com a luz certa.
    const f64 behind = enh::band_mean(temporal, ex - 8, ex - 2) - enh::band_mean(clean, ex - 8, ex - 2);
    const f64 ahead = enh::band_mean(temporal, ex + 2, ex + 8) - enh::band_mean(clean, ex + 2, ex + 8);
    std::printf("\n    ruido temporal: PSNR ruidoso %.2f, espacial %.2f, temporal (2+2) %.2f dB; borda %.2f x %.2f px;"
                " fantasma atras %+.4f, a frente %+.4f\n", pn, ps, pt, got, truth, behind, ahead);
    AUREA_CHECK_MSG(pt > ps + 1.0, "os vizinhos alinhados tem de limpar mais que o espacial");
    AUREA_CHECK_MSG(pt > pn + 4.0, "o temporal tem de subir o PSNR em 4 dB ou mais");
    AUREA_CHECK_MSG(truth > 0.0 && std::fabs(got - truth) < 0.5, "a borda que anda tem de ficar no lugar");
    AUREA_CHECK_MSG(std::fabs(behind) < 0.02 && std::fabs(ahead) < 0.02, "sem rastro (fantasma) dos quadros vizinhos");
    // A prévia (sem esperar os vizinhos) não pode sair pior que o espacial.
    const FloatImage preview = s.render(FrameIndex{T}, 1, false);
    AUREA_CHECK(enh::psnr(preview, clean, 4) > ps - 0.5);
}

AUREA_TEST(EnhanceGpu, TemporalNeighborsReachPrecompositionsAndAdjustmentLayers) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = enh::kEdgeW, H = enh::kEdgeH, N = 60, T = 12;
    enh::FnFactory cleanVideo(W, H, N, [](u32 x, u32 y, u32 i) { return enh::edge_luma(x, y, i, 0.0); });
    enh::FnFactory noisyVideo(W, H, N, [](u32 x, u32 y, u32 i) { return enh::edge_luma(x, y, i, 9.0); });
    FloatImage clean;
    {
        Scene s(W, H);
        (void)enh::add_video(s, s.comp, cleanVideo, W, H, N);
        clean = s.render(FrameIndex{T}, 1, true);
    }
    // Pré-composição: o efeito na camada da filha.
    {
        Scene s(W, H);
        const CompositionId childId = s.project.timeline().create_composition("pre-aprimorar", W, H, 30.0);
        Composition* child = s.project.timeline().composition(childId);
        child->set_duration(FrameIndex{N});
        (void)enh::add_video(s, child, noisyVideo, W, H, N);
        const LayerId n = s.comp->add_layer(LayerKind::Composition, "pre-aprimorar");
        Layer* nl = s.comp->layer(n);
        nl->nested.composition = childId;
        nl->transform.anchor = Vec3{W * 0.5f, H * 0.5f, 0};
        nl->transform.position = Vec3{W * 0.5f, H * 0.5f, 0};
        EffectInstance& e = enh::add_manual(s, n);
        enh::set(e, "reduce_noise", 60.0f);
        const f64 spatial = enh::psnr(s.render(FrameIndex{T}, 1, true), clean, 4);
        enh::set(e, "temporal_frames", 2.0f);
        const f64 temporal = enh::psnr(s.render(FrameIndex{T}, 1, true), clean, 4);
        std::printf("\n    pre-composicao: espacial %.2f dB, temporal %.2f dB", spatial, temporal);
        AUREA_CHECK_MSG(temporal > spatial + 1.0, "na pre-composicao os vizinhos sao a filha em t +- k");
    }
    // Camada de ajuste: os vizinhos são a composição abaixo em t ± k.
    {
        Scene s(W, H);
        (void)enh::add_video(s, s.comp, noisyVideo, W, H, N);
        const LayerId adj = s.solid(W, H, Vec4{1, 0, 1, 1}, W * 0.5f, H * 0.5f);
        s.comp->layer(adj)->adjustment = true;
        EffectInstance& e = enh::add_manual(s, adj);
        enh::set(e, "reduce_noise", 60.0f);
        const f64 spatial = enh::psnr(s.render(FrameIndex{T}, 1, true), clean, 4);
        enh::set(e, "temporal_frames", 2.0f);
        const f64 temporal = enh::psnr(s.render(FrameIndex{T}, 1, true), clean, 4);
        std::printf("; camada de ajuste: espacial %.2f dB, temporal %.2f dB\n", spatial, temporal);
        AUREA_CHECK_MSG(temporal > spatial + 1.0, "na camada de ajuste os vizinhos sao o fundo em t +- k");
    }
    // Foto parada: sem vizinhos, cai no espacial (o mesmo resultado).
    {
        Scene s(96, 64);
        const LayerId id = s.image(enh::with_noise(enh::scene_image(96, 64), 8.0, 5), 48, 32);
        EffectInstance& e = enh::add_manual(s, id);
        enh::set(e, "reduce_noise", 50.0f);
        const FloatImage spatial = s.render(FrameIndex{4}, 1, true);
        enh::set(e, "temporal_frames", 3.0f);
        AUREA_CHECK_MSG(s.render(FrameIndex{4}, 1, true).px == spatial.px, "na foto o temporal = o espacial");
    }
}

AUREA_TEST(EnhanceGpu, RevertCompressionRemovesEightByEightBlocks) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 192, H = 104;
    // O liso de verdade e a versão "comprimida": cada bloco 8×8 vira a média
    // dele (o DC de um JPEG muito baixo).
    ImagePixels smooth = uniform_image(W, H, 0, 0, 0), blocky = smooth;
    auto value = [](f64 x, f64 y) { return 60.0 + 0.55 * x + 0.5 * y + 14.0 * std::sin(x * 0.05) * std::cos(y * 0.07); };
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x) {
            const u8 v = enh::to_code(value(x, y));
            f64 m = 0.0;
            for (u32 j = 0; j < 8; ++j) for (u32 i = 0; i < 8; ++i) m += value((x / 8) * 8 + i, (y / 8) * 8 + j);
            const u8 b = enh::to_code(m / 64.0);
            for (int c = 0; c < 3; ++c) {
                smooth.rgba[(static_cast<usize>(y) * W + x) * 4 + c] = v;
                blocky.rgba[(static_cast<usize>(y) * W + x) * 4 + c] = b;
            }
        }
    auto blockiness = [&](const FloatImage& img) {
        // Degrau médio nas bordas da grade menos o de dentro (linhas e colunas).
        f64 edge = 0.0, inner = 0.0;
        u64 ne = 0, ni = 0;
        for (u32 y = 8; y + 8 < H; ++y)
            for (u32 x = 9; x + 8 < W; ++x) {
                const f64 dx = std::fabs(enh::luma(img, x, y) - enh::luma(img, x - 1, y));
                if (x % 8 == 0) { edge += dx; ++ne; } else { inner += dx; ++ni; }
            }
        for (u32 x = 8; x + 8 < W; ++x)
            for (u32 y = 9; y + 8 < H; ++y) {
                const f64 dy = std::fabs(enh::luma(img, x, y) - enh::luma(img, x, y - 1));
                if (y % 8 == 0) { edge += dy; ++ne; } else { inner += dy; ++ni; }
            }
        return edge / std::max<u64>(ne, 1) - inner / std::max<u64>(ni, 1);
    };
    FloatImage truth;
    {
        Scene s(W, H);
        (void)s.image(smooth, W * 0.5f, H * 0.5f);
        truth = s.render();
    }
    Scene s(W, H);
    const LayerId id = s.image(blocky, W * 0.5f, H * 0.5f);
    const FloatImage before = s.render();
    EffectInstance& e = enh::add_manual(s, id);
    enh::set(e, "revert_compression", 80.0f);
    const FloatImage after = s.render();
    const f64 b0 = blockiness(before), b1 = blockiness(after);
    const f64 p0 = enh::psnr(before, truth, 8), p1 = enh::psnr(after, truth, 8);
    std::printf("\n    blocos 8x8: degrau excedente %.4f -> %.4f; PSNR contra o liso %.2f -> %.2f dB\n", b0, b1, p0, p1);
    AUREA_CHECK_MSG(b1 < 0.4 * b0, "o desbloqueio tem de tirar a maior parte do degrau da grade");
    AUREA_CHECK_MSG(p1 > p0, "sem blocos a imagem fica mais perto da lisa");
    // Uma borda de verdade (grande) não é bloco: fica.
    ImagePixels hard = uniform_image(W, H, 40, 40, 40);
    for (u32 y = 0; y < H; ++y) for (u32 x = W / 2; x < W; ++x) for (int c = 0; c < 3; ++c) hard.rgba[(static_cast<usize>(y) * W + x) * 4 + c] = 210;
    Scene s2(W, H);
    const LayerId id2 = s2.image(hard, W * 0.5f, H * 0.5f);
    const FloatImage hardBefore = s2.render();
    enh::set(enh::add_manual(s2, id2), "revert_compression", 80.0f);
    const FloatImage hardAfter = s2.render();
    const f64 stepBefore = enh::luma(hardBefore, W / 2, H / 2) - enh::luma(hardBefore, W / 2 - 1, H / 2);
    const f64 stepAfter = enh::luma(hardAfter, W / 2, H / 2) - enh::luma(hardAfter, W / 2 - 1, H / 2);
    AUREA_CHECK_MSG(stepAfter > 0.95 * stepBefore, "a borda forte na grade nao e bloco");
}

AUREA_TEST(EnhanceGpu, SharpenRaisesAcutanceWithBoundedOvershoot) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 128, H = 48;
    ImagePixels px = uniform_image(W, H, 0, 0, 0);
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x) {
            const f64 t = 0.5 * (1.0 + std::erf((x + 0.5 - 64.0) / (1.6 * std::sqrt(2.0))));
            const u8 v = enh::to_code(255.0 * (0.2 + 0.6 * t));
            for (int c = 0; c < 3; ++c) px.rgba[(static_cast<usize>(y) * W + x) * 4 + c] = v;
        }
    Scene s(W, H);
    const LayerId id = s.image(px, W * 0.5f, H * 0.5f);
    auto measure = [&](const FloatImage& img, f64& acut, f64& over) {
        acut = 0.0;
        f64 mx = 0.0, mn = 1.0;
        for (u32 x = 40; x < 88; ++x) {
            acut = std::max(acut, static_cast<f64>(enh::luma(img, x + 1, H / 2) - enh::luma(img, x, H / 2)));
            mx = std::max(mx, static_cast<f64>(enh::luma(img, x, H / 2)));
            mn = std::min(mn, static_cast<f64>(enh::luma(img, x, H / 2)));
        }
        over = std::max(mx - 0.8, 0.2 - mn);
    };
    f64 a0, o0, a1, o1;
    measure(s.render(), a0, o0);
    EffectInstance& e = enh::add_manual(s, id);
    enh::set(e, "sharpen", 80.0f);
    measure(s.render(), a1, o1);
    enh::set(e, "sharpen", 100.0f);
    enh::set(e, "antialias_deblur", 60.0f);
    f64 a2, o2;
    measure(s.render(), a2, o2);
    std::printf("\n    nitidez: acutancia %.4f -> %.4f (com desborrar %.4f); sobressinal %.4f -> %.4f (%.4f)\n",
                a0, a1, a2, o0, o1, o2);
    AUREA_CHECK_MSG(a1 > 1.15 * a0, "a nitidez tem de deixar a borda mais ingreme");
    AUREA_CHECK_MSG(o1 < 0.025 && o2 < 0.035, "sem halo: o sobressinal fica preso perto da faixa local");
    AUREA_CHECK(a2 > a1);
}

AUREA_TEST(EnhanceGpu, DehaloRemovesTheOvershootOfAnOversharpenedEdge) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 128, H = 48;
    ImagePixels px = uniform_image(W, H, 0, 0, 0);
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x) {
            f64 v = x < 64 ? 0.3 : 0.7;
            if (x >= 62 && x < 64) v = 0.18;   // auréola escura
            if (x >= 64 && x < 66) v = 0.82;   // auréola clara
            for (int c = 0; c < 3; ++c) px.rgba[(static_cast<usize>(y) * W + x) * 4 + c] = enh::to_code(255.0 * v);
        }
    Scene s(W, H);
    const LayerId id = s.image(px, W * 0.5f, H * 0.5f);
    auto overshoot = [&](const FloatImage& img) {
        f64 mx = 0.0, mn = 1.0;
        for (u32 x = 50; x < 78; ++x) {
            mx = std::max(mx, static_cast<f64>(enh::luma(img, x, H / 2)));
            mn = std::min(mn, static_cast<f64>(enh::luma(img, x, H / 2)));
        }
        return std::max(mx - 0.7, 0.3 - mn);
    };
    const FloatImage before = s.render();
    const f64 o0 = overshoot(before);
    EffectInstance& e = enh::add_manual(s, id);
    enh::set(e, "dehalo", 100.0f);
    const FloatImage after = s.render();
    const f64 o1 = overshoot(after);
    const f64 step0 = enh::luma(before, 70, H / 2) - enh::luma(before, 57, H / 2);
    const f64 step1 = enh::luma(after, 70, H / 2) - enh::luma(after, 57, H / 2);
    std::printf("\n    halos: sobressinal %.4f -> %.4f; degrau %.4f -> %.4f\n", o0, o1, step0, step1);
    AUREA_CHECK_MSG(o1 < 0.4 * o0, "remover halos tem de tirar a maior parte da aureola");
    AUREA_CHECK_MSG(step1 > 0.95 * step0, "o degrau entre os platôs fica");
}

AUREA_TEST(EnhanceGpu, AddNoiseIsDeterministicPerFrameAndMovesInTime) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 64);
    ImagePixels px = uniform_image(96, 64, 128, 128, 128);
    for (u32 y = 0; y < 64; ++y) for (u32 x = 0; x < 32; ++x) for (int c = 0; c < 3; ++c) px.rgba[(y * 96 + x) * 4 + c] = 10;
    const LayerId id = s.image(px, 48, 32);
    const FloatImage plain = s.render(FrameIndex{10});
    EffectInstance& e = enh::add_manual(s, id);
    enh::set(e, "add_noise", 60.0f);
    const FloatImage a = s.render(FrameIndex{10});
    const FloatImage b = s.render(FrameIndex{10});
    const FloatImage c = s.render(FrameIndex{11});
    AUREA_CHECK_MSG(a.px == b.px, "o mesmo quadro tem de dar o mesmo grao, byte a byte");
    AUREA_CHECK_MSG(max_abs_diff(a, c) > 0.01f, "o grao muda de um quadro para o outro");
    AUREA_CHECK_MSG(max_abs_diff(a, s.render(FrameIndex{10}, 1, true)) < 1e-3f, "preview = export");
    const f64 gray = enh::residual(a, plain, 40, 4, 92, 60), dark = enh::residual(a, plain, 4, 4, 28, 60);
    const f64 drift = enh::band_mean(a, 40, 92) - enh::band_mean(plain, 40, 92);
    std::printf("\n    grao: desvio nos meios-tons %.4f, nas sombras %.4f, deriva da media %+.4f\n", gray, dark, drift);
    AUREA_CHECK(gray > 0.01);
    AUREA_CHECK_MSG(dark < gray, "pesado pela luz: menos grao no quase preto");
    AUREA_CHECK(std::fabs(drift) < 0.01);
    // Tamanho maior: o grão fica mais grosso (vizinhos mais parecidos).
    auto neighbor_corr = [&](const FloatImage& img) {
        f64 s0 = 0.0, s1 = 0.0;
        for (u32 y = 4; y < 60; ++y)
            for (u32 x = 40; x < 91; ++x) {
                const f64 d0 = enh::luma(img, x, y) - enh::luma(plain, x, y);
                const f64 d1 = enh::luma(img, x + 1, y) - enh::luma(plain, x + 1, y);
                s0 += d0 * d0; s1 += d0 * d1;
            }
        return s1 / std::max(s0, 1e-12);
    };
    const f64 fine = neighbor_corr(a);
    enh::set(e, "grain_size", 3.0f);
    const f64 coarse = neighbor_corr(s.render(FrameIndex{10}));
    AUREA_CHECK(coarse > fine + 0.15);
}

AUREA_TEST(EnhanceGpu, AutomaticPicksStrongerDenoiseForNoisierInput) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 192, H = 108;
    const ImagePixels cleanPx = enh::scene_image(W, H);
    FloatImage clean;
    {
        Scene s(W, H);
        (void)s.image(cleanPx, W * 0.5f, H * 0.5f);
        clean = s.render();
    }
    // Quanto do ruído o Automático tira (região lisa do degradê).
    auto removed = [&](f64 sigma, f64* changeOnClean) {
        Scene s(W, H);
        const LayerId id = s.image(sigma > 0.0 ? enh::with_noise(cleanPx, sigma, 31) : cleanPx, W * 0.5f, H * 0.5f);
        const FloatImage in = s.render();
        EffectInstance& e = s.add_effect(id, effect_keys::kEnhance);
        enh::set(e, "temporal_frames", 0.0f);   // Automático é o padrão do modelo
        const FloatImage out = s.render();
        if (changeOnClean) *changeOnClean = enh::residual(out, in, 120, 4, 188, 20);
        const f64 r0 = enh::residual(in, clean, 120, 4, 188, 20), r1 = enh::residual(out, clean, 120, 4, 188, 20);
        return r0 > 1e-6 ? 1.0 - r1 / r0 : 0.0;
    };
    f64 cleanChange = 0.0;
    (void)removed(0.0, &cleanChange);
    const f64 weak = removed(3.0, nullptr), strong = removed(14.0, nullptr);
    std::printf("\n    automatico: ruido removido %.0f%% (sigma 3) e %.0f%% (sigma 14); no limpo muda %.4f\n",
                weak * 100.0, strong * 100.0, cleanChange);
    AUREA_CHECK_MSG(strong > weak + 0.1, "mais ruido na entrada: o Automatico limpa mais");
    AUREA_CHECK_MSG(strong > 0.4, "no ruido forte o Automatico tem de tirar boa parte");
    AUREA_CHECK_MSG(cleanChange < 0.004, "no limpo o liso fica como esta");
}

AUREA_TEST(EnhanceGpu, PresetsAndModelsRenderFiniteAndDifferent) {
    AUREA_REQUIRE_GPU();
    const Effect* fx = gpu().effects.find(effect_type_id(effect_keys::kEnhance));
    AUREA_CHECK(fx != nullptr);
    if (!fx) return;
    Scene s(96, 64);
    const LayerId id = s.image(enh::with_noise(enh::scene_image(96, 64), 7.0, 3), 48, 32);
    const FloatImage plain = s.render(FrameIndex{5});
    EffectInstance& e = s.add_effect(id, effect_keys::kEnhance);
    std::vector<FloatImage> looks;
    for (const EffectPreset& preset : fx->presets()) {
        for (const EffectPresetValue& v : preset.values)
            if (v.param < e.params.size()) e.params[v.param].constant.v[v.component] = v.value;
        const FloatImage img = s.render(FrameIndex{5});
        bool finite = true;
        for (const f32 v : img.px) finite &= std::isfinite(v);
        AUREA_CHECK_MSG(finite, preset.id);
        AUREA_CHECK_MSG(max_abs_diff(img, plain) > 0.004f, preset.id);
        looks.push_back(img);
    }
    for (usize i = 1; i < looks.size(); ++i) AUREA_CHECK_MSG(max_abs_diff(looks[i], looks[i - 1]) > 1e-4f, fx->presets()[i].id);
}

AUREA_TEST(FlowRetimeGpu, ReplaceDuplicateFramesRebuildsTheMissingMidpoints) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 192, H = 108, N = 90;
    // 30 dentro de 60: cada quadro repetido (conteúdo 2·⌊i/2⌋), e um trecho
    // congelado (quadros 41..44 = 40, o "quadro perdido"). A textura anda a
    // meia velocidade do padrão (2,6 px por quadro): os 5 quadros do trecho
    // congelado são 13 px, longe do período de ~30 px da textura.
    auto content = [](u32 i) -> f64 {
        if (i >= 41 && i <= 44) return 40.0;
        if (i >= 40) return static_cast<f64>(i);
        return 2.0 * std::floor(i * 0.5);
    };
    auto truthAt = [](u32 x, u32 y, f64 t) { return pan_texture_value(x, y, 0.5 * t); };
    enh::FnFactory video(W, H, N, [content, truthAt](u32 x, u32 y, u32 i) {
        return enh::to_code(truthAt(x, y, content(i)));
    });
    Scene s(W, H);
    const LayerId id = enh::add_video(s, s.comp, video, W, H, N);
    EffectInstance& fx = s.add_effect(id, effect_keys::kFlowRetime);
    fx.params[0].constant = ParamValue::scalar(100.0f);   // velocidade normal: só o reparo
    auto error = [&](const FloatImage& img, f64 truth) {
        f64 sum = 0.0;
        u32 n = 0;
        for (u32 y = 16; y < H - 16; ++y)
            for (u32 x = 16; x < W - 16; ++x) {
                sum += std::fabs(enh::luma(img, x, y) * 219.0 + 16.0 - truthAt(x, y, truth));
                ++n;
            }
        return sum / std::max(1u, n);
    };
    const FloatImage off21 = s.render(FrameIndex{21}, 1, true);
    fx.params[4].constant = ParamValue::scalar(1.0f);
    const FloatImage f20 = s.render(FrameIndex{20}, 1, true);
    const FloatImage f21 = s.render(FrameIndex{21}, 1, true);
    const FloatImage f22 = s.render(FrameIndex{22}, 1, true);
    const FloatImage f42 = s.render(FrameIndex{42}, 1, true);
    const f64 eOff = error(off21, 21.0), e21 = error(f21, 21.0), e20 = error(f20, 20.0), e22 = error(f22, 22.0);
    const f64 e42 = error(f42, 42.0);
    std::printf("\n    duplicados: quadro 21 sem reparo erro %.2f, com reparo %.2f (20: %.2f, 22: %.2f); congelado 42: %.2f\n",
                eOff, e21, e20, e22, e42);
    AUREA_CHECK_MSG(e21 < 3.0 && e21 < 0.5 * eOff, "o duplicado vira o quadro do meio (perto do instante 21)");
    AUREA_CHECK_MSG(e20 < 1.0 && e22 < 1.0, "os quadros distintos ficam como estao");
    AUREA_CHECK_MSG(max_abs_diff(f20, f21) > 0.02f && max_abs_diff(f21, f22) > 0.02f, "tres quadros distintos seguidos");
    AUREA_CHECK_MSG(e42 < 3.0, "o trecho congelado e refeito entre os quadros distintos em volta");
    // Sensibilidade 0: só o IDÊNTICO conta (os duplicados daqui são idênticos).
    fx.params[5].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(error(s.render(FrameIndex{21}, 1, true), 21.0) < 3.0);
}

AUREA_TEST(EnhanceGpu, FullHdCostPrintsEveryQuality) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 1920, H = 1080;
    Gpu& g = gpu();
    static u64 frame = 990000;
    // O melhor de 12 quadros (a GPU do host é dividida com outros processos);
    // `breakdown` = o custo por passe (agrupado pelo nome) do melhor quadro.
    std::vector<std::pair<std::string, f32>> breakdown;
    auto cost = [&](Scene& s, i64 t) {
        RenderSettings rs;
        rs.gpuTimers = true;
        rs.finalQuality = true;
        rs.dither = false;
        OffscreenTarget off{g.target(W, H), W, H};
        f32 best = 1e9f, bestConv = 1e9f;
        breakdown.clear();
        for (int r = 0; r < 12; ++r) {
            FrameSnapshot snap;
            for (int attempt = 0; attempt < 6000; ++attempt) {
                g.renderer.prepare(*s.comp, s.project, FrameIndex{t}, &s.media, &Scene::lookup, &s, rs, ++frame, 0,
                                   DecodeMode::Still, 1.0f, snap);
                if (snap.missingVideoFrames == 0 && snap.staleVideoFrames == 0) break;
                for (RenderLayer& l : snap.layers) l.source.frame.reset();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            FrameStats stats;
            RenderTimings timings;
            AUREA_CHECK(g.renderer.render(snap, rs, &off, stats, timings).ok());
            g.backend.wait_idle();
            if (r > 1 && timings.gpuMeasured && timings.gpuEffectsMs > 0.0f && timings.gpuEffectsMs < best) {
                best = timings.gpuEffectsMs;
                bestConv = timings.gpuColorConvMs;
                breakdown.clear();
                const GpuTiming* passes = nullptr;
                const u32 n = g.renderer.last_gpu_passes(passes);
                for (u32 k = 0; passes && k < n; ++k) {
                    const std::string label = passes[k].label ? passes[k].label : "?";
                    auto it = std::find_if(breakdown.begin(), breakdown.end(), [&](const auto& p) { return p.first == label; });
                    if (it == breakdown.end()) breakdown.emplace_back(label, passes[k].ms);
                    else it->second += passes[k].ms;
                }
            }
        }
        return std::pair<f32, f32>{best < 1e8f ? best : 0.0f, best < 1e8f ? bestConv : 0.0f};
    };
    auto print_breakdown = [&] {
        for (const auto& [label, ms] : breakdown)
            if (ms >= 0.02f) std::printf("\n        %-34s %.3f ms", label.c_str(), ms);
    };
    static const char* const kQ[] = {"Rapida", "Normal", "Alta"};
    f32 spatialNormal = 0.0f, temporalNormal = 0.0f;
    {
        Scene s(W, H);
        const LayerId id = s.image(enh::with_noise(enh::scene_image(W, H), 8.0, 1), W * 0.5f, H * 0.5f);
        const f32 none = cost(s, 30).first;
        EffectInstance& e = enh::add_manual(s, id);
        for (const char* k : {"revert_compression", "recover_details", "sharpen", "reduce_noise", "dehalo", "antialias_deblur"})
            enh::set(e, k, 40.0f);
        enh::set(e, "add_noise", 10.0f);
        enh::set(e, "recover_original_detail", 10.0f);
        std::printf("\n    1080p sem o efeito: GPU dos efeitos %.3f ms", none);
        for (u32 q = 0; q < 3; ++q) {
            enh::set(e, "quality", static_cast<f32>(q));
            const f32 ms = cost(s, 30).first;
            if (q == 1) spatialNormal = ms;
            std::printf("\n    1080p espacial (todas as etapas, Manual) %s: %.3f ms", kQ[q], ms);
        }
        enh::set(e, "quality", 1.0f);
        (void)cost(s, 30);
        std::printf("\n      por passe (Manual Normal):");
        print_breakdown();
        enh::set(e, "model", 0.0f);   // Automático: a análise entra
        for (u32 q = 0; q < 3; ++q) {
            enh::set(e, "quality", static_cast<f32>(q));
            std::printf("\n    1080p espacial Automatico %s: %.3f ms", kQ[q], cost(s, 30).first);
            if (q == 1) {
                std::printf("\n      por passe (Automatico Normal):");
                print_breakdown();
            }
        }
    }
    {
        enh::FnFactory video(W, H, 40, [](u32 x, u32 y, u32 i) {
            return enh::to_code(pan_texture_value(x * 0.25, y * 0.25, i * 0.5) + 6.0 * enh::fast_noise(x, y, i));
        });
        Scene s(W, H);
        const LayerId id = enh::add_video(s, s.comp, video, W, H, 40);
        EffectInstance& e = enh::add_manual(s, id);
        for (const char* k : {"revert_compression", "recover_details", "sharpen", "reduce_noise"}) enh::set(e, k, 40.0f);
        for (u32 frames = 1; frames <= 3; ++frames) {
            enh::set(e, "temporal_frames", static_cast<f32>(frames));
            for (u32 q = 0; q < 3; ++q) {
                enh::set(e, "quality", static_cast<f32>(q));
                const auto [ms, conv] = cost(s, 20);
                if (frames == 2 && q == 1) temporalNormal = ms;
                std::printf("\n    1080p temporal %u+%u vizinhos %s: %.3f ms (conversao de cor %.3f ms)", frames, frames, kQ[q], ms, conv);
                if (frames == 2 && q == 1) {
                    std::printf("\n      por passe (temporal 2+2 Normal):");
                    print_breakdown();
                }
            }
        }
    }
    std::printf("\n");
    AUREA_CHECK_MSG(spatialNormal > 0.0f && spatialNormal < 3.0f, "espacial Normal em 1080p: menos de 3 ms");
    AUREA_CHECK_MSG(temporalNormal > 0.0f && temporalNormal < 8.0f, "temporal 2+2 Normal em 1080p: menos de 8 ms");
}
