// =============================================================================
//  Corte entre dois clipes: nenhum quadro fantasma (beta 2026-10-05).
//
//  "Quadro fantasma / quadro preso entre a transição": dois clipes encostados
//  na timeline (A termina no cabeçote, B começa ali, em linhas diferentes, com
//  nulos de pai e keyframes) mostravam por um instante um quadro velho no
//  corte — na prévia E no vídeo exportado.
//
//  Os vídeos daqui imitam o aparelho: cada quadro carrega o intervalo exato de
//  apresentação (preciseFrameTiming, como MediaCodec e AVFoundation), GOP
//  longo (o seek cai no keyframe e anda até o alvo) e conteúdo que diz QUAL
//  arquivo e QUAL quadro: o luma codifica o índice, o croma o arquivo.
// =============================================================================
namespace cut_frame_test {

constexpr u32 kW = 64, kH = 36;
constexpr f64 kFps = 30.0;

/// Luma do quadro `i` (0..29): degraus de 5 códigos, longe do corte do RGB.
inline u8 luma_of(u32 i) { return static_cast<u8>(32 + 5 * (i % 30)); }
/// Croma de cada arquivo: A cinza, B azulado (sem estourar o RGB).
inline u8 cb_of(u32 tag) { return tag ? 150 : 128; }

class Frame final : public DecodedFrame {
public:
    std::vector<u8> y, uv;
};

struct Shared {
    std::atomic<u32> opened{0}, seeks{0}, delivered{0}, stalls{0};
    /// Pressão de buffers do decoder no quadro `stallIndex` do arquivo
    /// `stallTag` (−1 = nunca): BudgetExceeded, o quadro fica retido, por
    /// `stallMs` — como o ImageReader com todas as imagens presas.
    i32 stallTag = -1;
    u32 stallIndex = 0, stallMs = 0;
};

/// Decoder com tempo preciso: seek no keyframe anterior, anda até o alvo,
/// descarta o que fica antes de `deliverFrom` (a não ser o quadro que o cobre).
class Decoder final : public VideoDecoderBackend {
public:
    Decoder(u32 tag, u32 frames, u32 gop, u32 costUs, std::shared_ptr<Shared> shared)
        : tag_(tag), frames_(frames), gop_(gop), costUs_(costUs), shared_(std::move(shared)) {
        info_.codedWidth = kW; info_.codedHeight = kH; info_.fps = kFps;
        info_.durationUs = pts_of(frames);
        info_.preciseFrameTiming = true;
        std::snprintf(info_.codec, sizeof(info_.codec), "%s", "video/corte");
        std::snprintf(info_.decoderName, sizeof(info_.decoderName), "%s", "corte");
    }
    const VideoStreamInfo& info() const noexcept override { return info_; }
    Status seek_to_keyframe(i64 us) noexcept override {
        const u32 target = index_at(us);
        pos_ = (target / gop_) * gop_;
        shared_->seeks.fetch_add(1);
        return OkStatus;
    }
    Status next_frame(i64 from, FrameRef& out, i64& pts, bool& eos) noexcept override {
        eos = false;
        if (pos_ >= frames_) { eos = true; pts = pts_of(frames_ - 1); return OkStatus; }
        if (shared_->stallTag == static_cast<i32>(tag_) && pos_ == shared_->stallIndex) {
            const auto now = std::chrono::steady_clock::now();
            if (stallSince_ == std::chrono::steady_clock::time_point{}) stallSince_ = now;
            if (now - stallSince_ < std::chrono::milliseconds(shared_->stallMs)) {
                shared_->stalls.fetch_add(1);
                return Errc::BudgetExceeded;
            }
        }
        if (costUs_) std::this_thread::sleep_for(std::chrono::microseconds(costUs_));
        const u32 i = pos_++;
        pts = pts_of(i);
        const i64 duration = pts_of(i + 1) - pts;
        eos = pos_ >= frames_;
        if (pts < from && pts + duration <= from) return OkStatus;   // antes do alvo: sem imagem
        auto* f = new Frame();
        f->ptsUs = pts; f->durationUs = duration;
        f->width = kW; f->height = kH; f->visibleWidth = kW; f->visibleHeight = kH;
        f->format = PixelFormat::NV12;
        f->bufferId = (static_cast<u64>(tag_) << 32) | i;
        f->y.assign(static_cast<usize>(kW) * kH, luma_of(i));
        f->uv.resize(static_cast<usize>(kW / 2) * (kH / 2) * 2);
        for (usize k = 0; k < f->uv.size(); k += 2) { f->uv[k] = cb_of(tag_); f->uv[k + 1] = 128; }
        f->planes[0] = f->y.data(); f->planes[1] = f->uv.data();
        f->strides[0] = kW; f->strides[1] = kW;
        f->planeCount = 2;
        out = FrameRef::adopt(f);
        shared_->delivered.fetch_add(1);
        return OkStatus;
    }
    u32 max_live_frames() const noexcept override { return 12; }
    i64 keyframe_interval_us() const noexcept override { return pts_of(gop_); }

    static i64 pts_of(u32 i) { return static_cast<i64>(std::llround(static_cast<f64>(i) * 1e6 / kFps)); }
    u32 index_at(i64 us) const {
        u32 i = 0;
        while (i + 1 < frames_ && pts_of(i + 1) <= us) ++i;
        return i;
    }

private:
    u32 tag_, frames_, gop_, costUs_;
    std::shared_ptr<Shared> shared_;
    VideoStreamInfo info_{};
    u32 pos_ = 0;
    std::chrono::steady_clock::time_point stallSince_{};
};

/// Dois arquivos: o caminho que contém "clip-b" é o arquivo B; o resto, A.
class Factory final : public VideoSourceFactory {
public:
    Factory(u32 frames, u32 gop, u32 costUs) : frames_(frames), gop_(gop), costUs_(costUs) {}
    static u32 tag_of(const char* path) { return path && std::strstr(path, "clip-b") ? 1u : 0u; }
    bool probe(const char* path, MediaProbe& out) override {
        Decoder d(tag_of(path), frames_, gop_, 0, shared);
        out.video = d.info();
        out.video.color.fromStream = true;
        out.hasVideo = true;
        return true;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset& a, MediaPriority) override {
        shared->opened.fetch_add(1);
        return std::make_unique<Decoder>(tag_of(a.sourcePath.c_str()), frames_, gop_, costUs_, shared);
    }
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
private:
    u32 frames_, gop_, costUs_;
};

/// O que um quadro de saída mostra: arquivo e índice (−1 = nenhum casou).
struct Seen { i32 tag = -1; i32 index = -1; i32 y = 0, cb = 0; };
inline Seen classify(i32 y, i32 cb) {
    Seen s; s.y = y; s.cb = cb;
    if (std::abs(cb - 128) <= 6) s.tag = 0;
    else if (std::abs(cb - cb_of(1)) <= 6) s.tag = 1;
    const i32 i = static_cast<i32>(std::lround((y - 32) / 5.0));
    if (i >= 0 && i < 30 && std::abs(y - luma_of(static_cast<u32>(i))) <= 2) s.index = i;
    return s;
}
/// Centro de um quadro NV12 do sink (Y w×h, depois CbCr w×h/2).
inline Seen seen_in(const std::vector<u8>& f, u32 w, u32 h) {
    if (f.size() < static_cast<usize>(w) * h * 3 / 2) return {};
    const i32 y = f[static_cast<usize>(h / 2) * w + w / 2];
    const usize c = static_cast<usize>(w) * h + static_cast<usize>(h / 4) * w + ((w / 2) & ~1u);
    return classify(y, f[c]);
}

/// Nulo no centro, com keyframes de escala e rotação, pai de `child` (com a
/// compensação do comando: o filho não sai do lugar ao ganhar o pai).
inline LayerId parent_to_animated_null(Engine& e, Composition* comp, LayerId child, i64 frames) {
    const LayerId n = comp->add_layer(LayerKind::Null, "Nulo");
    Layer* nl = comp->layer(n);
    if (!nl) return {};
    nl->start = FrameIndex{0};
    nl->end = FrameIndex{frames};
    nl->transform.anchor = Vec3{0, 0, 0};
    nl->transform.position = Vec3{kW * 0.5f, kH * 0.5f, 0};
    Command p;
    p.type = CommandType::LayerSetParent;
    p.layer_parent.layer = child;
    p.layer_parent.parent = n;
    if (!e.apply_command(p).ok()) return {};
    nl = comp->layer(n);
    auto& sx = nl->tracks.get_or_create(TrackProperty::ScaleX);
    auto& sy = nl->tracks.get_or_create(TrackProperty::ScaleY);
    auto& rz = nl->tracks.get_or_create(TrackProperty::RotationZ);
    sx.set(FrameIndex{0}, 1.0f); sx.set(FrameIndex{frames - 1}, 1.25f);
    sy.set(FrameIndex{0}, 1.0f); sy.set(FrameIndex{frames - 1}, 1.25f);
    rz.set(FrameIndex{0}, 0.0f); rz.set(FrameIndex{frames - 1}, 4.0f);
    return n;
}

inline bool set_range(Engine& e, LayerId id, i64 start, i64 end, i64 offset) {
    Command cmd;
    cmd.type = CommandType::LayerSetTimeRange;
    cmd.layer_range.layer = id;
    cmd.layer_range.start = FrameIndex{start};
    cmd.layer_range.end = FrameIndex{end};
    cmd.layer_range.offset = FrameIndex{offset};
    cmd.layer_range.setOffset = true;
    return e.apply_command(cmd).ok();
}

inline bool set_duration(Engine& e, i64 frames) {
    Command d;
    d.type = CommandType::CompositionSetDuration;
    d.comp_duration.comp = e.project()->timeline().current();
    d.comp_duration.duration = FrameIndex{frames};
    return e.apply_command(d).ok();
}

inline bool seek(Engine& e, i64 frame) {
    Command s;
    s.type = CommandType::PlaybackSeek;
    s.seek.time = tick_at(FrameIndex{frame}, kFps);
    return e.submit_commands(&s, 1) == 1;
}

/// Prévia como no aparelho: o cabeçote passeia por B e para EM CIMA do corte
/// (a imagem da tela), com o render pedindo quadros a cada tique.
inline void preview_around_cut(Engine& e, i64 cut, i64 inside) {
    for (i64 at : {inside, cut + 1, cut}) {
        (void)seek(e, at);
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(120);
        while (std::chrono::steady_clock::now() < until) {
            (void)e.render_frame(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }
}

} // namespace cut_frame_test

/// Clipes de ARQUIVOS DIFERENTES encostados, em linhas diferentes, cada um
/// com um nulo animado de pai. Todo quadro exportado é o quadro certo do
/// arquivo certo — inclusive o do corte e os vizinhos.
AUREA_TEST(CutFrame, AdjacentClipsExportEveryFrameFromTheRightFile) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    using namespace cut_frame_test;
    constexpr i64 kCut = 12, kTotal = 24, kOffsetB = 3;
    SyntheticConfig cfg; cfg.width = kW; cfg.height = kH;
    Factory factory(30, 30, 3000);
    Rig r(cfg, kFps, kTotal, 3, &factory);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    const LayerId a = r.video_layer();
    VideoImport vi; vi.sourcePath = "clip-b"; vi.displayName = "clip-b";
    const auto bid = r.e.import_video(vi);
    AUREA_CHECK(bid.ok()); if (!bid.ok()) return;
    const LayerId b = LayerId::unpack(*bid);
    AUREA_CHECK(set_range(r.e, a, 0, kCut, 0));
    AUREA_CHECK(set_range(r.e, b, kCut, kTotal, kOffsetB));
    AUREA_CHECK(set_duration(r.e, kTotal));
    r.comp()->layer(a)->trackId = 0;
    r.comp()->layer(b)->trackId = 1;
    AUREA_CHECK(parent_to_animated_null(r.e, r.comp(), a, kTotal).valid());
    AUREA_CHECK(parent_to_animated_null(r.e, r.comp(), b, kTotal).valid());
    preview_around_cut(r.e, kCut, kCut + 7);
    r.cap.keepFrames = true;
    const Outcome o = run_export(r, kH, kFps, false, 60);
    AUREA_CHECK(o.finished && o.p.result == Errc::Ok);
    AUREA_CHECK_EQ(r.cap.frames.size(), static_cast<usize>(kTotal));
    AUREA_CHECK_EQ(o.p.flags & Engine::kExportFrameFallback, 0u);
    for (usize t = 0; t < r.cap.frames.size(); ++t) {
        const Seen s = seen_in(r.cap.frames[t], r.cap.video.width, r.cap.video.height);
        const i32 tag = static_cast<i64>(t) < kCut ? 0 : 1;
        const i32 index = static_cast<i32>(static_cast<i64>(t) < kCut ? static_cast<i64>(t) : static_cast<i64>(t) - kCut + kOffsetB);
        if (s.tag != tag || s.index != index)
            std::printf("\n    quadro %zu: esperado arquivo %d quadro %d, saiu arquivo %d quadro %d (Y %d Cb %d)",
                        t, tag, index, s.tag, s.index, s.y, s.cb);
        AUREA_CHECK_EQ(s.tag, tag);
        AUREA_CHECK_EQ(s.index, index);
    }
}

/// Split do MESMO arquivo: a segunda metade começa no quadro seguinte ao
/// último da primeira — nunca repete nem mostra um quadro de outro ponto.
AUREA_TEST(CutFrame, SplitOfTheSameFileExportsContinuousFrames) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    using namespace cut_frame_test;
    constexpr i64 kCut = 12, kTotal = 24;
    SyntheticConfig cfg; cfg.width = kW; cfg.height = kH;
    Factory factory(30, 30, 3000);
    Rig r(cfg, kFps, kTotal, 3, &factory);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    const LayerId a = r.video_layer();
    AUREA_CHECK(set_range(r.e, a, 0, kTotal, 0));
    AUREA_CHECK(set_duration(r.e, kTotal));
    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = a;
    split.layer_split.at = FrameIndex{kCut};
    AUREA_CHECK(r.e.apply_command(split).ok());
    LayerId b{};
    r.comp()->layers().for_each([&](LayerId id, const Layer& l) { if (l.kind == LayerKind::Video && !(id == a)) b = id; });
    AUREA_CHECK(b.valid()); if (!b.valid()) return;
    r.comp()->layer(b)->trackId = 1;
    AUREA_CHECK(parent_to_animated_null(r.e, r.comp(), a, kTotal).valid());
    AUREA_CHECK(parent_to_animated_null(r.e, r.comp(), b, kTotal).valid());
    preview_around_cut(r.e, kCut, kCut + 7);
    r.cap.keepFrames = true;
    const Outcome o = run_export(r, kH, kFps, false, 60);
    AUREA_CHECK(o.finished && o.p.result == Errc::Ok);
    AUREA_CHECK_EQ(r.cap.frames.size(), static_cast<usize>(kTotal));
    AUREA_CHECK_EQ(o.p.flags & Engine::kExportFrameFallback, 0u);
    for (usize t = 0; t < r.cap.frames.size(); ++t) {
        const Seen s = seen_in(r.cap.frames[t], r.cap.video.width, r.cap.video.height);
        if (s.tag != 0 || s.index != static_cast<i32>(t))
            std::printf("\n    quadro %zu: esperado quadro %zu, saiu arquivo %d quadro %d (Y %d Cb %d)",
                        t, t, s.tag, s.index, s.y, s.cb);
        AUREA_CHECK_EQ(s.tag, 0);
        AUREA_CHECK_EQ(s.index, static_cast<i32>(t));
    }
}
/// O decoder de B empaca no quadro do corte (pressão de buffers) depois de
/// já ter entregue o quadro ANTERIOR ao ponto de entrada — no split, a última
/// imagem da primeira metade. Passado o prazo, o export gravava esse quadro no
/// corte: o quadro "preso" entre os clipes, dentro do arquivo final. Agora o
/// arquivo nunca recebe imagem de fora do trecho do clipe: ou sai o quadro
/// certo, ou o export para com erro de mídia (sem fingir sucesso).
AUREA_TEST(CutFrame, ExportNeverWritesAnotherClipsPictureWhenTheCutStalls) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    using namespace cut_frame_test;
    constexpr i64 kCut = 12, kTotal = 16;
    for (const bool split : {false, true}) {
        SyntheticConfig cfg; cfg.width = kW; cfg.height = kH;
        Factory factory(30, 30, 1000);
        const i64 offsetB = split ? kCut : 3;
        factory.shared->stallTag = split ? 0 : 1;
        factory.shared->stallIndex = static_cast<u32>(offsetB);
        factory.shared->stallMs = 6000;
        Rig r(cfg, kFps, kTotal, 3, &factory);
        AUREA_CHECK(r.ok); if (!r.ok) return;
        const LayerId a = r.video_layer();
        LayerId b{};
        if (split) {
            AUREA_CHECK(set_range(r.e, a, 0, kTotal, 0));
            Command cut;
            cut.type = CommandType::LayerSplit;
            cut.layer_split.layer = a;
            cut.layer_split.at = FrameIndex{kCut};
            AUREA_CHECK(r.e.apply_command(cut).ok());
            r.comp()->layers().for_each([&](LayerId id, const Layer& l) { if (l.kind == LayerKind::Video && !(id == a)) b = id; });
        } else {
            VideoImport vi; vi.sourcePath = "clip-b"; vi.displayName = "clip-b";
            const auto bid = r.e.import_video(vi);
            AUREA_CHECK(bid.ok()); if (!bid.ok()) return;
            b = LayerId::unpack(*bid);
            AUREA_CHECK(set_range(r.e, a, 0, kCut, 0));
            AUREA_CHECK(set_range(r.e, b, kCut, kTotal, offsetB));
        }
        AUREA_CHECK(b.valid()); if (!b.valid()) return;
        AUREA_CHECK(set_duration(r.e, kTotal));
        r.comp()->layer(b)->trackId = 1;
        r.cap.keepFrames = true;
        const Outcome o = run_export(r, kH, kFps, false, 60);
        AUREA_CHECK(o.finished);
        AUREA_CHECK(factory.shared->stalls.load() > 0u);
        u32 foreign = 0;
        for (usize t = 0; t < r.cap.frames.size(); ++t) {
            const Seen s = seen_in(r.cap.frames[t], r.cap.video.width, r.cap.video.height);
            const bool afterCut = static_cast<i64>(t) >= kCut;
            const i32 tag = afterCut && !split ? 1 : 0;
            const i32 index = static_cast<i32>(afterCut ? static_cast<i64>(t) - kCut + offsetB : static_cast<i64>(t));
            if (s.tag != tag || s.index != index) {
                ++foreign;
                std::printf("\n    %s, quadro %zu: esperado arquivo %d quadro %d, saiu arquivo %d quadro %d",
                            split ? "split" : "dois arquivos", t, tag, index, s.tag, s.index);
            }
        }
        AUREA_CHECK_EQ(foreign, 0u);
        if (o.p.result == Errc::Ok) {
            AUREA_CHECK_EQ(r.cap.frames.size(), static_cast<usize>(kTotal));
        } else {
            // Sem o quadro do clipe: falha de mídia honesta, arquivo descartado.
            AUREA_CHECK_EQ(o.p.result, Errc::DecodeFailed);
            AUREA_CHECK(r.cap.aborted && !r.cap.finished);
            AUREA_CHECK(r.cap.frames.size() <= static_cast<usize>(kCut));
        }
    }
}


namespace cut_frame_test {

/// Prévia sem GPU: o `prepare` do renderer decide QUAL quadro de cada camada
/// vai para a tela. A e B encostados no quadro `kCut`; B pode ser outro
/// arquivo (com ponto de entrada) ou a segunda metade de um split.
struct PreviewRig {
    static constexpr i64 kCut = 12, kTotal = 24;
    MockBackend backend;
    EffectRegistry effects;
    Renderer renderer;
    Project project;
    Composition* comp = nullptr;
    Factory factory;
    MediaManager media;
    LayerId a{}, b{};
    std::vector<std::pair<std::string, AssetId>> assets;
    i64 offsetB = 0;
    u64 frameNumber = 1;
    FrameSnapshot snap;
    u32 ghosts = 0, ahead = 0, aAfterCut = 0, shown = 0;
    bool exactAtCut = false;

    PreviewRig(bool split, u32 costUs) : factory(30, 30, costUs) {
        register_builtin_effects(effects);
        (void)renderer.initialize(backend, effects);
        auto created = Project::create_new(kW, kH, kFps, "corte");
        project = std::move(*created);
        comp = project.timeline().composition(project.timeline().root());
        media.set_factory(&factory);
        offsetB = split ? kCut : 3;
        a = add("clip-a", 0, kCut, 0);
        b = add(split ? "clip-a" : "clip-b", kCut, kTotal, offsetB);
        comp->layer(b)->trackId = 1;
    }
    ~PreviewRig() { snap.release_video_frames(); media.close_all(); }

    LayerId add(const char* path, i64 start, i64 end, i64 offset) {
        Asset asset; asset.kind = AssetKind::Video; asset.sourcePath = path;
        asset.video.width = kW; asset.video.height = kH; asset.video.fps = kFps;
        AssetId aid{};
        for (const auto& [known, id] : assets) if (known == path) aid = id;   // o split usa o MESMO asset
        if (!aid.valid()) { aid = project.add_asset(std::move(asset)); assets.emplace_back(path, aid); }
        const LayerId id = comp->add_layer(LayerKind::Video, path);
        Layer* l = comp->layer(id);
        l->source = aid; l->start = FrameIndex{start}; l->end = FrameIndex{end}; l->offset = FrameIndex{offset};
        l->transform.anchor = Vec3{kW * 0.5f, kH * 0.5f, 0}; l->transform.position = Vec3{kW * 0.5f, kH * 0.5f, 0};
        return id;
    }
    const RenderLayer* find(LayerId id) const {
        for (const RenderLayer& rl : snap.layers) if (rl.id == id) return &rl;
        return nullptr;
    }
    void prepare(i64 t, const RenderSettings& rs, i32 direction, DecodeMode mode) {
        snap.release_video_frames();
        renderer.prepare(*comp, project, FrameIndex{t}, &media, nullptr, nullptr, rs, frameNumber++, direction, mode, 1.f, snap);
    }
    /// Quadro de B (índice na fonte) que o instante `t` mostra.
    i64 b_index(i64 t) const { return t - kCut + offsetB; }
    /// Confere o que a tela recebeu em `t`: depois do corte, A sumiu; B, se
    /// aparece, mostra um quadro DELE (dentro do trecho) e nunca um adiante.
    void check(i64 t, bool forward) {
        if (t < kCut) return;
        if (find(a)) ++aAfterCut;
        const RenderLayer* rb = find(b);
        if (!rb || !rb->source.frame) return;
        ++shown;
        if (t == kCut && rb->source.frameExact) exactAtCut = true;
        const DecodedFrame& f = *rb->source.frame.get();
        const i64 first = Decoder::pts_of(static_cast<u32>(offsetB));
        const i64 last = Decoder::pts_of(static_cast<u32>(b_index(kTotal - 1)));
        const i64 want = Decoder::pts_of(static_cast<u32>(b_index(t)));
        const bool inside = f.ptsUs + f.durationUs > first && f.ptsUs <= last;
        if (!inside) {
            ++ghosts;
            std::printf("\n    t=%lld: B mostrou %lld us, fora do trecho dele [%lld, %lld] (alvo %lld)", static_cast<long long>(t),
                        static_cast<long long>(f.ptsUs), static_cast<long long>(first), static_cast<long long>(last),
                        static_cast<long long>(want));
        } else if (forward && f.ptsUs > want) {
            ++ahead;
            std::printf("\n    t=%lld: B mostrou %lld us, adiante do alvo %lld us", static_cast<long long>(t),
                        static_cast<long long>(f.ptsUs), static_cast<long long>(want));
        }
    }
    /// Parado em `t` até o quadro exato de B chegar (a fonte guarda quadros dali).
    bool still_at(i64 t, u64 epoch) {
        RenderSettings rs; rs.mediaGeneration = epoch;
        for (auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3); std::chrono::steady_clock::now() < until;) {
            prepare(t, rs, 0, DecodeMode::Still);
            const RenderLayer* rb = find(b);
            if (rb && rb->source.frame && rb->source.frameExact) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }
    /// Toca de `from` a `to` no relógio de 30 fps (época nova, como um seek).
    void play(i64 from, i64 to, u64 epoch) {
        RenderSettings rs; rs.mediaGeneration = epoch;
        const auto start = std::chrono::steady_clock::now();
        for (i64 t = from; t <= to; ++t) {
            std::this_thread::sleep_until(start + std::chrono::microseconds(static_cast<i64>((t - from) * 1e6 / kFps)));
            prepare(t, rs, 1, DecodeMode::Playback);
            check(t, true);
        }
    }
    /// Dedo arrastando para a frente por cima do corte, um evento a cada 16 ms.
    void scrub(i64 from, i64 to, u64 epoch) {
        RenderSettings rs; rs.mediaGeneration = epoch;
        for (i64 t = from; t <= to; ++t) {
            for (u32 k = 0; k < 3; ++k) {
                prepare(t, rs, 1, DecodeMode::Scrub);
                check(t, false);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        }
    }
};

} // namespace cut_frame_test

/// Prévia tocando por cima do corte depois de conferir o clipe B em outro
/// ponto (parado dentro de B, volta para logo antes do corte, play — o jeito
/// comum de conferir um corte) e com o decoder mais lento que um quadro. B
/// nunca entra com quadro de outro instante: ou o dele, ou fica de fora por um
/// instante (a tela segura a imagem inteira anterior — PreviewRefill).
AUREA_TEST(CutFrame, PreviewPlaybackNeverShowsAStaleFrameOfTheIncomingClip) {
    using namespace cut_frame_test;
    for (const bool split : {false, true}) {
        PreviewRig r(split, 20000);
        AUREA_CHECK(r.still_at(20, 1));
        r.play(PreviewRig::kCut - 1, PreviewRig::kCut + 10, 2);
        std::printf(" [%s: B na tela %u, fora do trecho %u, adiante %u, A depois do corte %u]",
                    split ? "split" : "dois arquivos", r.shown, r.ghosts, r.ahead, r.aAfterCut);
        AUREA_CHECK_EQ(r.ghosts, 0u);
        AUREA_CHECK_EQ(r.ahead, 0u);
        AUREA_CHECK_EQ(r.aAfterCut, 0u);
        AUREA_CHECK(r.shown > 0);   // o quadro certo chega e aparece
    }
}

/// Scrub por cima do corte de um split com GOP longo: o seek do decoder de B
/// cai no keyframe ANTES do ponto de entrada (um quadro que pertence à
/// primeira metade) e esse keyframe ia para a tela como "prévia" de B — a
/// imagem de A reaparecendo depois do corte.
AUREA_TEST(CutFrame, ScrubAcrossASplitNeverShowsTheFirstHalfInTheSecond) {
    using namespace cut_frame_test;
    PreviewRig r(true, 15000);
    r.scrub(PreviewRig::kCut - 2, PreviewRig::kCut + 8, 1);
    std::printf(" [B na tela %u, fora do trecho %u, A depois do corte %u]", r.shown, r.ghosts, r.aAfterCut);
    AUREA_CHECK_EQ(r.ghosts, 0u);
    AUREA_CHECK_EQ(r.aAfterCut, 0u);
    AUREA_CHECK(r.shown > 0);
}

/// Tocando desde bem antes do corte, a pré-rolagem deixa o 1º quadro de B
/// pronto: o corte sai exato, sem quadro preso nem fantasma.
AUREA_TEST(CutFrame, PrerolledCutIsExactOnTheFirstFrame) {
    using namespace cut_frame_test;
    for (const bool split : {false, true}) {
        PreviewRig r(split, 8000);
        AUREA_CHECK(r.still_at(20, 1));
        r.play(0, PreviewRig::kCut + 3, 2);
        AUREA_CHECK_EQ(r.ghosts, 0u);
        AUREA_CHECK_EQ(r.ahead, 0u);
        AUREA_CHECK_EQ(r.aAfterCut, 0u);
        AUREA_CHECK(r.exactAtCut);
    }
}
