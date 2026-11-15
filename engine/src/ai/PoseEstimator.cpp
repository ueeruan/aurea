// =============================================================================
//  Aurea / ai / PoseEstimator.cpp — Motion Capture do Aurea PC (só Windows).
//  Ver aurea/ai/PoseEstimator.hpp e engine/assets/mocap/README.md.
//
//  Referência de COMPORTAMENTO (não de código): os grafos do MediaPipe
//  pose_detection_cpu, pose_detection_to_roi, pose_landmark_by_roi_cpu e
//  pose_landmarks_to_roi (Apache-2.0), cujas opções estão citadas abaixo.
// =============================================================================
#include "aurea/ai/PoseEstimator.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"

#include <datareader.h>
#include <gpu.h>
#include <net.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string_view>
#include <fstream>
#include <new>
#include <thread>
#include <vector>

namespace aurea::mocap {

namespace {

constexpr f32 kPi = 3.14159265358979323846f;
/// Coordenada da célula do heatmap no refino: coluna/64, sem o meio pixel (a
/// conta do MediaPipe). Medido nos vídeos do dono contra os pontos regredidos:
/// com 0 a mediana do deslocamento fica em (−0,7; −0,2) px; com +0,5 iria a (+10; +4) px.
constexpr f32 kHeatmapCenter = 0.0f;

f32 sigmoid(f32 x) noexcept { return 1.0f / (1.0f + std::exp(-std::clamp(x, -100.0f, 100.0f))); }

f32 normalize_radians(f32 a) noexcept {
    // MediaPipe NormalizeRadians: a − 2π·floor((a + π) / 2π) → [−π, π).
    return a - 2.0f * kPi * std::floor((a + kPi) / (2.0f * kPi));
}

/// SsdAnchorsCalculator (pose_detection_cpu.pbtxt): 5 camadas, passos
/// 8/16/32/32/32, razão 1 + a interpolada, tamanho fixo. As camadas seguidas
/// de mesmo passo dividem o mapa: 28² × 2, 14² × 2, 7² × 6 = 2254 âncoras.
struct AnchorLayer {
    u32 grid;
    u32 perCell;
};
constexpr AnchorLayer kAnchorLayers[] = {{28, 2}, {14, 2}, {7, 6}};
constexpr u32 kAnchorCount = 28 * 28 * 2 + 14 * 14 * 2 + 7 * 7 * 6;

/// Amostra bilinear de um RGBA8 com a borda repetida (o ImageToTensor dos
/// landmarks usa BORDER_REPLICATE).
inline void sample_bilinear(const u8* rgba, u32 w, u32 h, u32 stride, f32 x, f32 y, f32 out[3]) noexcept {
    // Centro do pixel em (i + 0,5).
    x -= 0.5f;
    y -= 0.5f;
    const f32 fx = std::floor(x), fy = std::floor(y);
    const f32 ax = x - fx, ay = y - fy;
    const i32 x0 = static_cast<i32>(fx), y0 = static_cast<i32>(fy);
    const i32 maxX = static_cast<i32>(w) - 1, maxY = static_cast<i32>(h) - 1;
    const i32 xa = std::clamp(x0, 0, maxX), xb = std::clamp(x0 + 1, 0, maxX);
    const i32 ya = std::clamp(y0, 0, maxY), yb = std::clamp(y0 + 1, 0, maxY);
    const u8* r0 = rgba + static_cast<usize>(ya) * stride;
    const u8* r1 = rgba + static_cast<usize>(yb) * stride;
    for (u32 c = 0; c < 3; ++c) {
        const f32 top = r0[xa * 4 + c] + (r0[xb * 4 + c] - static_cast<f32>(r0[xa * 4 + c])) * ax;
        const f32 bot = r1[xa * 4 + c] + (r1[xb * 4 + c] - static_cast<f32>(r1[xa * 4 + c])) * ax;
        out[c] = top + (bot - top) * ay;
    }
}

bool read_file(const std::string& dir, const char* name, std::vector<u8>& out) {
    // UTF-8 → caminho do sistema (no Windows, a API larga: pastas com acento).
    const std::u8string u8dir(reinterpret_cast<const char8_t*>(dir.data()), dir.size());
    const std::filesystem::path path = std::filesystem::path(u8dir) / name;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0 || size > (256ll << 20)) return false;
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<usize>(size) + 1);
    in.read(reinterpret_cast<char*>(out.data()), size);
    out[static_cast<usize>(size)] = 0;   // o .param é lido como texto terminado em zero
    return static_cast<std::streamoff>(in.gcount()) == size;
}

/// Amostra de área: `step` = pixels do nível 0 por pixel de saída. Acima de 1
/// sobe na pirâmide (cada nível é a média 2×2 do anterior) e faz 2×2 amostras
/// bilineares dentro do pixel de saída — um recorte de 1000 px para 256 sem
/// serrilhado (o serrilhado muda de um quadro para o outro: os pontos tremem).
struct Sampler {
    const PoseImage& img;
    bool antialias = false;

    void rgb(f32 x, f32 y, f32 step, f32 out[3]) const noexcept {
        const PoseImage::Level& l0 = img.levels[0];
        if (!antialias || step <= 1.0f) {
            sample_bilinear(l0.rgba, l0.width, l0.height, l0.stride, x, y, out);
            return;
        }
        u32 level = 0;
        f32 scale = 1.0f;
        while (level + 1 < img.levels.size() && step >= 2.0f * scale) {
            ++level;
            scale *= 2.0f;
        }
        const PoseImage::Level& L = img.levels[level];
        const f32 lx = x / scale, ly = y / scale, ls = step / scale;
        if (ls <= 1.05f) {
            sample_bilinear(L.rgba, L.width, L.height, L.stride, lx, ly, out);
            return;
        }
        // 2×2 dentro do pixel de saída (quartos do passo).
        const f32 o = 0.25f * ls;
        f32 acc[3]{0.0f, 0.0f, 0.0f}, s[3];
        for (const f32 dy : {-o, o})
            for (const f32 dx : {-o, o}) {
                sample_bilinear(L.rgba, L.width, L.height, L.stride, lx + dx, ly + dy, s);
                acc[0] += s[0]; acc[1] += s[1]; acc[2] += s[2];
            }
        out[0] = 0.25f * acc[0];
        out[1] = 0.25f * acc[1];
        out[2] = 0.25f * acc[2];
    }
};

/// Ver DepthEstimator.cpp: uma rede que sobrevive ao atexit do ncnn não pode
/// soltar buffers de um dispositivo Vulkan já destruído.
void drop_net(std::unique_ptr<ncnn::Net>& net) noexcept {
    if (!net) return;
#if NCNN_VULKAN
    if (net->opt.use_vulkan_compute && ncnn::get_gpu_instance() == VK_NULL_HANDLE) {
        (void)net.release();
        return;
    }
#endif
    net.reset();
}

} // namespace

// ---- ROI ----------------------------------------------------------------------

bool PoseRoi::valid() const noexcept {
    return size > 1.0f && std::isfinite(cx) && std::isfinite(cy) && std::isfinite(size) && std::isfinite(rotation);
}

PoseRoi roi_from_alignment(f32 x0, f32 y0, f32 x1, f32 y1, f32 scale) noexcept {
    const f32 dx = x1 - x0, dy = y1 - y0;
    PoseRoi r;
    r.cx = x0;
    r.cy = y0;
    // box_size = 2·|p1 − p0|; square_long + scale 1,25 (pose_*_to_roi.pbtxt).
    r.size = 2.0f * std::sqrt(dx * dx + dy * dy) * scale;
    // rotation_vector_target_angle_degrees: 90 → p0→p1 vira "para cima".
    r.rotation = normalize_radians(0.5f * kPi - std::atan2(-dy, dx));
    return r;
}

void roi_to_image(const PoseRoi& roi, f32 u, f32 v, f32& x, f32& y) noexcept {
    const f32 du = (u - 0.5f) * roi.size, dv = (v - 0.5f) * roi.size;
    const f32 c = std::cos(roi.rotation), s = std::sin(roi.rotation);
    x = roi.cx + c * du - s * dv;
    y = roi.cy + s * du + c * dv;
}

void image_to_roi(const PoseRoi& roi, f32 x, f32 y, f32& u, f32& v) noexcept {
    const f32 dx = x - roi.cx, dy = y - roi.cy;
    const f32 c = std::cos(roi.rotation), s = std::sin(roi.rotation);
    const f32 du = c * dx + s * dy, dv = -s * dx + c * dy;
    const f32 k = roi.size > 0.0f ? 1.0f / roi.size : 0.0f;
    u = du * k + 0.5f;
    v = dv * k + 0.5f;
}

PoseRoi roi_from_landmarks(const Landmarks& pts, u32 width, u32 height, f32 minConfidence, f32 margin) noexcept {
    f32 x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    u32 n = 0;
    for (const auto& p : pts) {
        if (p.confidence < minConfidence || !std::isfinite(p.x) || !std::isfinite(p.y)) continue;
        const f32 x = p.x * static_cast<f32>(width), y = p.y * static_cast<f32>(height);
        x0 = std::min(x0, x); x1 = std::max(x1, x);
        y0 = std::min(y0, y); y1 = std::max(y1, y);
        ++n;
    }
    if (n < 4) return {};
    PoseRoi r;
    r.cx = 0.5f * (x0 + x1);
    r.cy = 0.5f * (y0 + y1);
    r.size = std::max(x1 - x0, y1 - y0) * margin;
    r.rotation = 0.0f;
    return r;
}

Letterbox letterbox(u32 width, u32 height, u32 size) noexcept {
    Letterbox l;
    const f32 longest = static_cast<f32>(std::max(1u, std::max(width, height)));
    l.scale = static_cast<f32>(size) / longest;
    l.padX = 0.5f * (static_cast<f32>(size) - static_cast<f32>(width) * l.scale);
    l.padY = 0.5f * (static_cast<f32>(size) - static_cast<f32>(height) * l.scale);
    return l;
}

u32 detector_anchor_count() noexcept { return kAnchorCount; }

void detector_anchor(u32 index, f32& cx, f32& cy) noexcept {
    cx = cy = 0.0f;
    for (const AnchorLayer& layer : kAnchorLayers) {
        const u32 n = layer.grid * layer.grid * layer.perCell;
        if (index < n) {
            const u32 cell = index / layer.perCell;
            const f32 g = static_cast<f32>(layer.grid);
            // anchor_offset 0,5: centro da célula.
            cx = (static_cast<f32>(cell % layer.grid) + 0.5f) / g;
            cy = (static_cast<f32>(cell / layer.grid) + 0.5f) / g;
            return;
        }
        index -= n;
    }
}

void pack_frame(f64 timeSeconds, const Landmarks& pts, f32* out) noexcept {
    out[0] = static_cast<f32>(timeSeconds);
    out[1] = 0.0f;   // sem pontos de mundo, como o ML Kit
    for (u32 j = 0; j < rig::kPoseLandmarks; ++j) {
        f32* o = out + 2 + j * 4;
        o[0] = pts[j].x;
        o[1] = pts[j].y;
        o[2] = pts[j].z;
        o[3] = pts[j].confidence;
    }
}

void pack_world_frame(f64 timeSeconds, const PoseOutput& pose, f32* out) noexcept {
    out[0] = static_cast<f32>(timeSeconds);
    out[1] = rig::kMocapWorldFlag;
    for (u32 j = 0; j < rig::kPoseLandmarks; ++j) {
        f32* o = out + 2 + j * 4;
        o[0] = pose.world[j].x;
        o[1] = pose.world[j].y;
        o[2] = pose.world[j].z;
        o[3] = pose.presence > 0.0f ? pose.visibility[j] : 0.0f;
    }
}

// ---- Pirâmide -------------------------------------------------------------------

void PoseImage::wrap(const u8* rgba, u32 width, u32 height, u32 stride) {
    levels.assign(1, Level{rgba, width, height, stride});
}

void PoseImage::build(const u8* rgba, u32 width, u32 height, u32 stride, u32 maxLevels) {
    wrap(rgba, width, height, stride);
    storage_.resize(maxLevels);
    for (u32 k = 0; k < maxLevels; ++k) {
        const Level src = levels.back();
        const u32 w = src.width / 2, h = src.height / 2;
        if (w < 64 || h < 64) break;
        std::vector<u8>& dst = storage_[k];
        dst.resize(static_cast<usize>(w) * h * 4);
        for (u32 y = 0; y < h; ++y) {
            const u8* r0 = src.rgba + static_cast<usize>(2 * y) * src.stride;
            const u8* r1 = r0 + src.stride;
            u8* o = dst.data() + static_cast<usize>(y) * w * 4;
            for (u32 x = 0; x < w; ++x) {
                const u8* a = r0 + x * 8;
                const u8* b = r1 + x * 8;
                for (u32 c = 0; c < 4; ++c) o[x * 4 + c] = static_cast<u8>((a[c] + a[c + 4] + b[c] + b[c + 4] + 2) >> 2);
            }
        }
        levels.push_back(Level{dst.data(), w, h, w * 4});
    }
}

// ---- Rede ---------------------------------------------------------------------

struct PoseEstimator::Impl {
    std::unique_ptr<ncnn::Net> detector, landmark;
    std::vector<u8> detectorParam, detectorBin, landmarkParam, landmarkBin;
    Backend backend = Backend::Cpu;
    LandmarkModel model = LandmarkModel::Full;
    LandmarkModel loadedModel = LandmarkModel::Full;
    bool hasHeatmap = false;
    bool refine = false, antialias = false;
    bool ready = false;
    bool fallback = false;
    u32 threads = 1;
    f32 ms = 0.0f;
    std::string dir;
};

PoseEstimator::PoseEstimator() noexcept {
    try { impl_.reset(new (std::nothrow) Impl); }
    catch (const std::bad_alloc&) { }
}

PoseEstimator::~PoseEstimator() {
    if (!impl_) return;
    drop_net(impl_->detector);
    drop_net(impl_->landmark);
}

bool PoseEstimator::loaded() const noexcept { return impl_ && impl_->ready; }
PoseEstimator::Backend PoseEstimator::backend() const noexcept { return impl_ ? impl_->backend : Backend::Cpu; }
f32 PoseEstimator::last_ms() const noexcept { return impl_ ? impl_->ms : 0.0f; }
void PoseEstimator::add_ms(f32 ms) noexcept { if (impl_) impl_->ms += ms; }
void PoseEstimator::reset_ms() noexcept { if (impl_) impl_->ms = 0.0f; }
LandmarkModel PoseEstimator::model() const noexcept { return impl_ ? impl_->model : LandmarkModel::Full; }
void PoseEstimator::set_refine(bool heatmap, bool antialias) noexcept {
    if (!impl_) return;
    impl_->refine = heatmap;
    impl_->antialias = antialias;
}

Status PoseEstimator::load(const std::string& modelDir, Backend requested, u32 cpuThreads, LandmarkModel wanted) try {
    if (!impl_) return Errc::OutOfMemory;
    Impl& m = *impl_;
    m.ready = false;
    drop_net(m.detector);
    drop_net(m.landmark);
    if (m.dir != modelDir || m.detectorBin.empty() || m.loadedModel != wanted) {
        if (!read_file(modelDir, "pose_detection.param", m.detectorParam) || !read_file(modelDir, "pose_detection.bin", m.detectorBin)) {
            m.dir.clear();
            m.detectorBin.clear();
            return Status{Errc::NotFound, "pose models not found"};
        }
        // O heavy é opcional: sem ele, o full (o mesmo grafo, menos preciso).
        bool heavy = wanted == LandmarkModel::Heavy && read_file(modelDir, "pose_landmark_heavy.param", m.landmarkParam)
                     && read_file(modelDir, "pose_landmark_heavy.bin", m.landmarkBin);
        if (wanted == LandmarkModel::Heavy && !heavy) AUREA_LOG_WARN("mocap: pose_landmark_heavy ausente; usando o full");
        if (!heavy && (!read_file(modelDir, "pose_landmark_full.param", m.landmarkParam)
                       || !read_file(modelDir, "pose_landmark_full.bin", m.landmarkBin))) {
            m.dir.clear();
            m.detectorBin.clear();
            return Status{Errc::NotFound, "pose models not found"};
        }
        m.model = heavy ? LandmarkModel::Heavy : LandmarkModel::Full;
        m.loadedModel = wanted;
        m.hasHeatmap = std::string_view(reinterpret_cast<const char*>(m.landmarkParam.data())).find(" heatmap ") != std::string_view::npos;
        m.dir = modelDir;
    }
    const u32 hw = std::max(1u, std::thread::hardware_concurrency());
    m.threads = cpuThreads ? cpuThreads : std::clamp(hw / 2, 1u, 4u);
    m.fallback = requested == Backend::Auto;
    m.backend = Backend::Cpu;
    int gpu = -1;
#if NCNN_VULKAN
    if (requested != Backend::Cpu && ncnn::get_gpu_count() > 0) {
        const int index = ncnn::get_default_gpu_index();
        // Tipo 3 = Vulkan de software (lavapipe/SwiftShader): mais lento que a CPU.
        if (index >= 0 && ncnn::get_gpu_device(index) && ncnn::get_gpu_device(index)->is_valid()
            && (requested == Backend::Vulkan || ncnn::get_gpu_info(index).type() != 3)) {
            gpu = index;
            m.backend = Backend::Vulkan;
        }
    }
#endif
    if (requested == Backend::Vulkan && m.backend != Backend::Vulkan)
        return Status{Errc::NotSupported, "Vulkan inference unavailable"};
    auto make = [&](std::unique_ptr<ncnn::Net>& net, const std::vector<u8>& param, const std::vector<u8>& bin) {
        net = std::make_unique<ncnn::Net>();
        ncnn::Option& o = net->opt;
        o.num_threads = static_cast<int>(m.threads);
        o.lightmode = true;
        // fp32 na conta, como o DepthEstimator: o mesmo resultado na CPU e na GPU.
        o.use_fp16_packed = false;
        o.use_fp16_storage = false;
        o.use_fp16_uniform = false;
        o.use_fp16_arithmetic = false;
        o.use_bf16_storage = false;
        o.use_vulkan_compute = false;
        o.use_winograd_convolution = gpu < 0;
        // Os kernels com memória local do ncnn fixado derrubam o dispositivo
        // numa RTX 3050 (DepthEstimator.cpp): desligados aqui também.
        o.use_shader_local_memory = false;
#if NCNN_VULKAN
        if (gpu >= 0) {
            net->set_vulkan_device(gpu);
            o.use_vulkan_compute = true;
        }
#endif
        if (net->load_param_mem(reinterpret_cast<const char*>(param.data())) != 0) return false;
        const unsigned char* cursor = bin.data();        // os pesos ficam vivos em Impl
        ncnn::DataReaderFromMemory reader(cursor);
        return net->load_model(reader) == 0;
    };
    if (!make(m.detector, m.detectorParam, m.detectorBin) || !make(m.landmark, m.landmarkParam, m.landmarkBin)) {
        drop_net(m.detector);
        drop_net(m.landmark);
        if (m.backend == Backend::Vulkan && m.fallback) return load(modelDir, Backend::Cpu, cpuThreads, wanted);
        return Status{Errc::CorruptData, "pose models could not be loaded"};
    }
    m.ready = true;
    AUREA_LOG_INFO("mocap: BlazePose %s em %s (%u threads de CPU)", m.model == LandmarkModel::Heavy ? "heavy" : "full",
                   m.backend == Backend::Vulkan ? "Vulkan" : "CPU", m.threads);
    return OkStatus;
} catch (const std::bad_alloc&) {
    if (impl_) impl_->ready = false;
    return Errc::OutOfMemory;
}

bool PoseEstimator::detect(const u8* rgba, u32 w, u32 h, u32 stride, PoseRoi& roi, f32* scoreOut) {
    roi = {};
    if (scoreOut) *scoreOut = 0.0f;
    if (!rgba || !w || !h || stride < w * 4) return false;
    PoseImage img;
    img.wrap(rgba, w, h, stride);
    return detect(img, roi, scoreOut);
}

bool PoseEstimator::detect(const PoseImage& image, PoseRoi& roi, f32* scoreOut, const PoseRoi* prefer) try {
    roi = {};
    if (scoreOut) *scoreOut = 0.0f;
    if (!impl_ || !impl_->ready || !image.valid()) return false;
    Impl& m = *impl_;
    const u32 w = image.width(), h = image.height();
    const Sampler sampler{image, m.antialias};
    constexpr u32 S = kDetectorSize;
    // ImageToTensor (keep_aspect_ratio, BORDER_ZERO, faixa −1..1): a borda é preta.
    const Letterbox lb = letterbox(w, h, S);
    ncnn::Mat in(static_cast<int>(S), static_cast<int>(S), 3);
    if (in.empty()) return false;
    float* plane[3] = {in.channel(0), in.channel(1), in.channel(2)};
    for (u32 y = 0; y < S; ++y) {
        for (u32 x = 0; x < S; ++x) {
            const f32 sx = (static_cast<f32>(x) + 0.5f - lb.padX) / lb.scale;
            const f32 sy = (static_cast<f32>(y) + 0.5f - lb.padY) / lb.scale;
            f32 rgb[3]{0.0f, 0.0f, 0.0f};
            if (sx >= 0.0f && sy >= 0.0f && sx < static_cast<f32>(w) && sy < static_cast<f32>(h))
                sampler.rgb(sx, sy, 1.0f / lb.scale, rgb);
            for (int c = 0; c < 3; ++c) plane[c][y * S + x] = rgb[c] / 127.5f - 1.0f;
        }
    }
    static const char* kCls[] = {"cls8", "cls16", "cls32"};
    static const char* kReg[] = {"reg8", "reg16", "reg32"};
    ncnn::Mat cls[3], reg[3];
    const u64 t0 = monotonic_ns();
    auto infer = [&]() {
        ncnn::Extractor ex = m.detector->create_extractor();
        if (ex.input("in0", in) != 0) return false;
        for (int i = 0; i < 3; ++i)
            if (ex.extract(kCls[i], cls[i]) != 0 || ex.extract(kReg[i], reg[i]) != 0 || cls[i].empty() || reg[i].empty()) return false;
        return true;
    };
    bool ok = infer();
    if (!ok && m.backend == Backend::Vulkan && m.fallback) {
        AUREA_LOG_WARN("mocap: Vulkan falhou no detector; seguindo na CPU");
        if (load(m.dir, Backend::Cpu, m.threads, m.loadedModel).ok()) ok = infer();
    }
    add_ms(static_cast<f32>(static_cast<f64>(monotonic_ns() - t0) * 1e-6));
    if (!ok) return false;

    // TensorsToDetections: sigmoide com corte em ±100, reverse_output_order
    // (x, y, w, h), escala 224, 4 pontos-chave.
    struct Candidate { f32 score; u32 head, cell, anchor, index; };
    std::vector<Candidate> found;
    u32 base = 0;
    for (u32 i = 0; i < 3; ++i) {
        const AnchorLayer& L = kAnchorLayers[i];
        if (cls[i].c != static_cast<int>(L.perCell) || cls[i].w != static_cast<int>(L.grid) || cls[i].h != static_cast<int>(L.grid)
            || reg[i].c != static_cast<int>(L.perCell * 12)) return false;
        for (u32 a = 0; a < L.perCell; ++a) {
            const float* row = cls[i].channel(static_cast<int>(a));
            for (u32 cell = 0; cell < L.grid * L.grid; ++cell) {
                const f32 s = sigmoid(row[cell]);
                if (s >= kMinDetection) found.push_back({s, i, cell, a, base + cell * L.perCell + a});
            }
        }
        base += L.grid * L.grid * L.perCell;
    }
    if (found.empty()) return false;
    struct Box { f32 x0, y0, x1, y1, kp[8]; };
    auto decode = [&](const Candidate& c) {
        f32 ax = 0.0f, ay = 0.0f;
        detector_anchor(c.index, ax, ay);
        auto v = [&](u32 k) { return reg[c.head].channel(static_cast<int>(c.anchor * 12 + k))[c.cell] / static_cast<f32>(S); };
        Box b{};
        const f32 xc = v(0) + ax, yc = v(1) + ay, bw = v(2), bh = v(3);
        b.x0 = xc - bw * 0.5f; b.x1 = xc + bw * 0.5f;
        b.y0 = yc - bh * 0.5f; b.y1 = yc + bh * 0.5f;
        for (u32 k = 0; k < 4; ++k) {
            b.kp[k * 2] = v(4 + k * 2) + ax;
            b.kp[k * 2 + 1] = v(5 + k * 2) + ay;
        }
        return b;
    };
    auto iou = [](const Box& a, const Box& b) {
        const f32 ix = std::max(0.0f, std::min(a.x1, b.x1) - std::max(a.x0, b.x0));
        const f32 iy = std::max(0.0f, std::min(a.y1, b.y1) - std::max(a.y0, b.y0));
        const f32 inter = ix * iy;
        const f32 uni = (a.x1 - a.x0) * (a.y1 - a.y0) + (b.x1 - b.x0) * (b.y1 - b.y0) - inter;
        return uni > 0.0f ? inter / uni : 0.0f;
    };
    // NonMaxSuppression WEIGHTED (limiar 0,3), grupo por grupo: uma ROI por pessoa.
    std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    struct Person { PoseRoi roi; f32 score; };
    std::vector<Person> people;
    std::vector<u8> used(found.size(), 0);
    // DetectionLetterboxRemoval: do quadrado 224 para os pixels da imagem.
    auto px = [&](f32 nx) { return (nx * S - lb.padX) / lb.scale; };
    auto py = [&](f32 ny) { return (ny * S - lb.padY) / lb.scale; };
    for (usize i = 0; i < found.size() && people.size() < 8; ++i) {
        if (used[i]) continue;
        const Box top = decode(found[i]);
        Box sum{};
        f32 total = 0.0f;
        for (usize j = i; j < found.size(); ++j) {
            if (used[j]) continue;
            const Box b = decode(found[j]);
            if (j != i && iou(top, b) <= 0.3f) continue;
            used[j] = 1;
            const f32 w = found[j].score;
            sum.x0 += b.x0 * w; sum.y0 += b.y0 * w; sum.x1 += b.x1 * w; sum.y1 += b.y1 * w;
            for (u32 k = 0; k < 8; ++k) sum.kp[k] += b.kp[k] * w;
            total += w;
        }
        const f32 k = 1.0f / total;
        // Pontos 0 (meio do quadril) e 1 (tamanho do corpo): pose_detection_to_roi.
        const PoseRoi r = roi_from_alignment(px(sum.kp[0] * k), py(sum.kp[1] * k), px(sum.kp[2] * k), py(sum.kp[3] * k));
        if (r.valid()) people.push_back({r, found[i].score});
    }
    if (people.empty()) return false;
    // Com uma ROI de antes, a MESMA pessoa (centro e tamanho parecidos) vale
    // mais que outra mais provável: o rastreio não pula para quem passa atrás.
    usize pick = 0;
    if (prefer && prefer->valid()) {
        for (usize i = 0; i < people.size(); ++i) {
            const PoseRoi& r = people[i].roi;
            const f32 ratio = r.size / prefer->size;
            if (std::hypot(r.cx - prefer->cx, r.cy - prefer->cy) < 0.35f * prefer->size && ratio > 0.6f && ratio < 1.6f) {
                pick = i;
                break;
            }
        }
    }
    roi = people[pick].roi;
    if (scoreOut) *scoreOut = people[pick].score;
    return roi.valid();
} catch (const std::bad_alloc&) {
    roi = {};
    return false;
}

f32 PoseEstimator::landmarks(const u8* rgba, u32 w, u32 h, u32 stride, const PoseRoi& roi, Landmarks& out, PoseRoi* next) {
    out = {};
    if (next) *next = {};
    if (!rgba || !w || !h || stride < w * 4 || !roi.valid()) return 0.0f;
    PoseImage img;
    img.wrap(rgba, w, h, stride);
    PoseOutput full;
    const f32 presence = landmarks(img, roi, full);
    if (presence < kMinPresence) return presence;
    out = full.image;
    if (next) {
        PoseRoi r = roi_from_alignment(full.aux[0], full.aux[1], full.aux[2], full.aux[3]);
        // ROI absurda (pontos auxiliares degenerados): a caixa dos pontos.
        const f32 shortSide = static_cast<f32>(std::min(w, h));
        if (!r.valid() || r.size < 0.05f * shortSide || r.size > 8.0f * static_cast<f32>(std::max(w, h)))
            r = roi_from_landmarks(out, w, h);
        *next = r;
    }
    return presence;
}

f32 PoseEstimator::landmarks(const PoseImage& image, const PoseRoi& roi, PoseOutput& out) try {
    out = {};
    if (!impl_ || !impl_->ready || !image.valid() || !roi.valid()) return 0.0f;
    Impl& m = *impl_;
    const u32 w = image.width(), h = image.height();
    const Sampler sampler{image, m.antialias};
    constexpr u32 S = kLandmarkSize;
    // ImageToTensor da ROI girada, faixa 0..1.
    ncnn::Mat in(static_cast<int>(S), static_cast<int>(S), 3);
    if (in.empty()) return 0.0f;
    float* plane[3] = {in.channel(0), in.channel(1), in.channel(2)};
    const f32 c = std::cos(roi.rotation), s = std::sin(roi.rotation);
    const f32 step = roi.size / static_cast<f32>(S);
    for (u32 y = 0; y < S; ++y) {
        const f32 dv = (static_cast<f32>(y) + 0.5f) * step - 0.5f * roi.size;
        for (u32 x = 0; x < S; ++x) {
            const f32 du = (static_cast<f32>(x) + 0.5f) * step - 0.5f * roi.size;
            f32 rgb[3];
            sampler.rgb(roi.cx + c * du - s * dv, roi.cy + s * du + c * dv, step, rgb);
            for (int ch = 0; ch < 3; ++ch) plane[ch][y * S + x] = rgb[ch] * (1.0f / 255.0f);
        }
    }
    const bool wantHeatmap = m.refine && m.hasHeatmap;
    ncnn::Mat lm, flag, world, heat;
    const u64 t0 = monotonic_ns();
    auto infer = [&]() {
        ncnn::Extractor ex = m.landmark->create_extractor();
        if (ex.input("in0", in) != 0 || ex.extract("flag", flag) != 0 || ex.extract("lm", lm) != 0
            || ex.extract("world", world) != 0 || flag.empty() || lm.empty() || world.empty()) return false;
        if (wantHeatmap && (ex.extract("heatmap", heat) != 0 || heat.empty())) heat = ncnn::Mat();
        return true;
    };
    bool ok = infer();
    if (!ok && m.backend == Backend::Vulkan && m.fallback) {
        AUREA_LOG_WARN("mocap: Vulkan falhou nos pontos; seguindo na CPU");
        if (load(m.dir, Backend::Cpu, m.threads, m.loadedModel).ok()) ok = infer();
    }
    add_ms(static_cast<f32>(static_cast<f64>(monotonic_ns() - t0) * 1e-6));
    if (!ok) return 0.0f;
    // As saídas são [n, 1, 1] (um valor por canal; o passo entre canais tem
    // alinhamento): achatadas aqui na ordem do tensor do TFLite.
    auto flatten = [](const ncnn::Mat& mat, f32* dst, usize n) {
        usize k = 0;
        for (int ch = 0; ch < mat.c && k < n; ++ch) {
            const float* p = mat.channel(ch);
            for (int i = 0; i < mat.w * mat.h && k < n; ++i) dst[k++] = p[i];
        }
        return k == n;
    };
    std::array<f32, 39 * 5> v{};
    std::array<f32, 39 * 3> wv{};
    if (!flatten(lm, v.data(), v.size()) || !flatten(world, wv.data(), wv.size())) return 0.0f;
    const f32 presence = std::isfinite(flag[0]) ? std::clamp(static_cast<f32>(flag[0]), 0.0f, 1.0f) : 0.0f;
    out.presence = presence;
    if (presence < kMinPresence) return presence;
    // RefineLandmarksFromHeatmap (kernel 7, refino com confiança >= 0,5): média
    // ponderada pela sigmoide do heatmap 64×64 em volta de cada ponto.
    if (!heat.empty() && heat.c == 39 && heat.w > 0 && heat.h > 0) {
        const int hw = heat.w, hh = heat.h;
        constexpr int kOffset = 3;
        for (u32 j = 0; j < 39; ++j) {
            const f32 nx = v[j * 5] / static_cast<f32>(S), ny = v[j * 5 + 1] / static_cast<f32>(S);
            if (!(nx >= 0.0f && nx < 1.0f && ny >= 0.0f && ny < 1.0f)) continue;
            const int cx = static_cast<int>(nx * static_cast<f32>(hw)), cy = static_cast<int>(ny * static_cast<f32>(hh));
            const float* plane0 = heat.channel(static_cast<int>(j));
            f32 sum = 0.0f, wx = 0.0f, wy = 0.0f, peak = 0.0f;
            for (int row = std::max(0, cy - kOffset); row < std::min(hh, cy + kOffset + 1); ++row)
                for (int col = std::max(0, cx - kOffset); col < std::min(hw, cx + kOffset + 1); ++col) {
                    const f32 conf = sigmoid(plane0[row * hw + col]);
                    sum += conf;
                    peak = std::max(peak, conf);
                    wx += (static_cast<f32>(col) + kHeatmapCenter) * conf;
                    wy += (static_cast<f32>(row) + kHeatmapCenter) * conf;
                }
            if (peak >= 0.5f && sum > 0.0f) {
                v[j * 5] = wx / sum / static_cast<f32>(hw) * static_cast<f32>(S);
                v[j * 5 + 1] = wy / sum / static_cast<f32>(hh) * static_cast<f32>(S);
            }
        }
    }
    // TensorsToLandmarks (39 × x, y, z, visibilidade, presença em pixels do
    // recorte 256) + LandmarkProjection: x, y voltam pela ROI; z / 256 × lado
    // da ROI, normalizado pela LARGURA (a escala de x, como o ML Kit).
    const f32 inv = 1.0f / static_cast<f32>(S);
    auto project = [&](u32 j, f32& x, f32& y) {
        roi_to_image(roi, v[j * 5] * inv, v[j * 5 + 1] * inv, x, y);
    };
    for (u32 j = 0; j < rig::kPoseLandmarks; ++j) {
        f32 x = 0.0f, y = 0.0f;
        project(j, x, y);
        rig::PoseLandmark& p = out.image[j];
        p.x = x / static_cast<f32>(w);
        p.y = y / static_cast<f32>(h);
        p.z = v[j * 5 + 2] * inv * roi.size / static_cast<f32>(w);
        // A "presença" do BlazePose é o inFrameLikelihood do ML Kit.
        p.confidence = sigmoid(v[j * 5 + 4]);
        out.visibility[j] = sigmoid(v[j * 5 + 3]);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) { p = {}; out.visibility[j] = 0.0f; }
        // WorldLandmarkProjection: o mundo sai no referencial do recorte; o giro
        // da ROI volta aqui (uma pessoa deitada fica deitada).
        const f32 wx = wv[j * 3], wy = wv[j * 3 + 1], wz = wv[j * 3 + 2];
        out.world[j] = Vec3{c * wx - s * wy, s * wx + c * wy, wz};
        if (!std::isfinite(out.world[j].x) || !std::isfinite(out.world[j].y) || !std::isfinite(out.world[j].z)) {
            out.world[j] = Vec3{0, 0, 0};
            out.visibility[j] = 0.0f;
        }
    }
    // pose_landmarks_to_roi: auxiliares 33 (meio do quadril) e 34 (tamanho do corpo).
    project(33, out.aux[0], out.aux[1]);
    project(34, out.aux[2], out.aux[3]);
    return presence;
} catch (const std::bad_alloc&) {
    out = {};
    return 0.0f;
}

bool estimate_pose(PoseEstimator& est, const u8* rgba, u32 w, u32 h, u32 stride, PoseRoi& roi, Landmarks& out, bool* detected) {
    est.reset_ms();
    if (detected) *detected = false;
    bool fresh = false;
    if (!roi.valid()) {
        if (!est.detect(rgba, w, h, stride, roi)) {
            roi = {};
            out = {};
            return false;
        }
        fresh = true;
        if (detected) *detected = true;
    }
    PoseRoi next{};
    f32 presence = est.landmarks(rgba, w, h, stride, roi, out, &next);
    if (presence < PoseEstimator::kMinPresence && !fresh) {
        // A pessoa saiu da ROI rastreada (corte, movimento rápido): procura de novo no mesmo quadro.
        PoseRoi found{};
        if (est.detect(rgba, w, h, stride, found)) {
            if (detected) *detected = true;
            presence = est.landmarks(rgba, w, h, stride, found, out, &next);
        }
    }
    if (presence < PoseEstimator::kMinPresence) {
        roi = {};
        out = {};
        return false;
    }
    roi = next;
    return true;
}

bool PoseTracker::track(const u8* rgba, u32 w, u32 h, u32 stride, Landmarks& out) {
    bool detected = false;
    const bool ok = estimate_pose(estimator_, rgba, w, h, stride, roi_, out, &detected);
    if (detected) ++detections_;
    return ok;
}

// ---- Rastreio de vídeo do PC ------------------------------------------------------

f32 OneEuro1::filter(f32 value, f32 dt, f32 speedScale) noexcept {
    if (!started || !(dt > 0.0f)) {
        started = true;
        x = value;
        dx = 0.0f;
        return value;
    }
    auto alpha = [dt](f32 cutoff) {
        const f32 tau = 1.0f / (2.0f * kPi * std::max(cutoff, 1e-4f));
        return 1.0f / (1.0f + tau / dt);
    };
    const f32 d = (value - x) / dt;
    dx += alpha(dCutoff) * (d - dx);
    x += alpha(minCutoff + beta * std::fabs(dx) * speedScale) * (value - x);
    return x;
}

namespace {

/// Pares esquerda/direita por membro (do cotovelo e do joelho para baixo: os
/// ombros e o quadril definem para onde a pessoa olha e ficam como vieram).
constexpr u32 kArmPairs[][2] = {{13, 14}, {15, 16}, {17, 18}, {19, 20}, {21, 22}};
constexpr u32 kLegPairs[][2] = {{25, 26}, {27, 28}, {29, 30}, {31, 32}};

bool similar_scale(const PoseRoi& a, const PoseRoi& b) noexcept {
    const f32 r = a.size / std::max(b.size, 1.0f);
    return r > 0.5f && r < 2.0f;
}

f32 dist_px(const rig::PoseLandmark& a, const rig::PoseLandmark& b, f32 w, f32 h) noexcept {
    const f32 dx = (a.x - b.x) * w, dy = (a.y - b.y) * h;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

void VideoPoseTracker::reset() noexcept {
    roi_ = {};
    for (OneEuro1& f : auxFilter_) f.started = false;
    havePrev_ = false;
    sinceCheck_ = 0;
}

bool VideoPoseTracker::run(const PoseImage& image, const PoseRoi& roi, PoseOutput& out, f32& presence) {
    presence = estimator_.landmarks(image, roi, out);
    return presence >= PoseEstimator::kMinPresence;
}

PoseRoi VideoPoseTracker::roi_from_aux(const PoseOutput& o, f32 dt, u32 width, u32 height) {
    f32 a[4]{o.aux[0], o.aux[1], o.aux[2], o.aux[3]};
    if (options_.filterRoi) {
        // PoseLandmarkFiltering do MediaPipe para os auxiliares: One Euro com
        // min_cutoff 0,01 e beta 10 sobre a velocidade em "tamanhos do corpo"
        // por segundo — parado, a ROI quase não mexe; andando, acompanha.
        const f32 size = std::max(roi_.valid() ? roi_.size : 1.0f, 1.0f);
        for (u32 k = 0; k < 4; ++k) {
            OneEuro1& f = auxFilter_[k];
            f.minCutoff = 0.01f;
            f.beta = 10.0f;
            f.dCutoff = 1.0f;
            // A velocidade conta em lados da ROI por segundo (relativa ao corpo).
            a[k] = f.filter(a[k], dt, 1.0f / size);
        }
    }
    PoseRoi r = roi_from_alignment(a[0], a[1], a[2], a[3]);
    const f32 shortSide = static_cast<f32>(std::min(width, height));
    if (!r.valid() || r.size < 0.05f * shortSide || r.size > 8.0f * static_cast<f32>(std::max(width, height))) {
        r = roi_from_landmarks(o.image, width, height);
        for (OneEuro1& f : auxFilter_) f.started = false;
    }
    return r;
}

void VideoPoseTracker::fix_swaps(PoseOutput& o) {
    if (!havePrev_) return;
    const f32 w = static_cast<f32>(std::max(1u, lastWidth_)), h = static_cast<f32>(std::max(1u, lastHeight_));
    // Escala: tronco (ombros → quadril) do quadro anterior.
    const auto mid = [&](const PoseOutput& p, u32 a, u32 b) {
        return rig::PoseLandmark{0.5f * (p.image[a].x + p.image[b].x), 0.5f * (p.image[a].y + p.image[b].y), 0.0f, 1.0f};
    };
    const f32 torso = std::max(dist_px(mid(prev_, 11, 12), mid(prev_, 23, 24), w, h), 8.0f);
    auto group = [&](const u32 (*pairs)[2], usize count, u32& run) {
        f32 same = 0.0f, swapped = 0.0f;
        for (usize i = 0; i < count; ++i) {
            const u32 l = pairs[i][0], r = pairs[i][1];
            same += dist_px(o.image[l], prev_.image[l], w, h) + dist_px(o.image[r], prev_.image[r], w, h);
            swapped += dist_px(o.image[l], prev_.image[r], w, h) + dist_px(o.image[r], prev_.image[l], w, h);
        }
        const f32 perJoint = same / static_cast<f32>(2 * count);
        // Os dois lados trocaram de lugar num quadro só: a troca explica o
        // quadro muito melhor que o movimento. Seguido por mais de 10 quadros,
        // vale o que o modelo diz (pode ser o quadro anterior que estava errado).
        if (swapped < 0.45f * same && perJoint > 0.12f * torso && run < 10) {
            for (usize i = 0; i < count; ++i) {
                const u32 l = pairs[i][0], r = pairs[i][1];
                std::swap(o.image[l], o.image[r]);
                std::swap(o.visibility[l], o.visibility[r]);
                std::swap(o.world[l], o.world[r]);
            }
            ++run;
            ++swaps_;
        } else {
            run = 0;
        }
    };
    group(kArmPairs, std::size(kArmPairs), armRun_);
    group(kLegPairs, std::size(kLegPairs), legRun_);
}

bool VideoPoseTracker::track(const PoseImage& image, f32 dt, PoseOutput& out) {
    out = {};
    if (!image.valid()) return false;
    const u32 w = image.width(), h = image.height();
    lastWidth_ = w;
    lastHeight_ = h;
    f32 presence = 0.0f;
    bool fresh = false;
    if (!roi_.valid()) {
        PoseRoi found;
        // Depois de perder a pessoa, a mesma (perto de onde sumiu) ganha das outras.
        const bool recent = lost_.valid() && lostFrames_ < 45;
        if (!estimator_.detect(image, found, nullptr, lost_.valid() ? &lost_ : nullptr)
            || (recent && !similar_scale(found, lost_))) {
            // Logo depois de perder a pessoa, outra de tamanho bem diferente (alguém
            // pequeno no fundo, uma miniatura na tela) não é ela: melhor "sem
            // pessoa" (o motor segura/interpola) que animar o personagem com outro.
            ++lostFrames_;
            reset();
            return false;
        }
        ++detections_;
        roi_ = found;
        for (OneEuro1& f : auxFilter_) f.started = false;
        fresh = true;
    }
    PoseOutput o;
    bool ok = run(image, roi_, o, presence);
    if (!fresh) {
        // Pontos visíveis encostando na borda do recorte: a ROI ficou para trás.
        bool border = false;
        if (ok && options_.borderRedetect) {
            u32 outside = 0;
            for (u32 j = 0; j < rig::kPoseLandmarks; ++j) {
                if (o.visibility[j] < 0.5f) continue;
                f32 u = 0.0f, v = 0.0f;
                image_to_roi(roi_, o.image[j].x * static_cast<f32>(w), o.image[j].y * static_cast<f32>(h), u, v);
                if (u < 0.02f || u > 0.98f || v < 0.02f || v > 0.98f) ++outside;
            }
            border = outside >= 1;
        }
        const bool periodic = options_.recheckEvery && ++sinceCheck_ >= options_.recheckEvery;
        if (!ok || border || periodic) {
            sinceCheck_ = 0;
            PoseRoi found;
            if (estimator_.detect(image, found, nullptr, &roi_)) {
                ++detections_;
                const f32 dc = std::hypot(found.cx - roi_.cx, found.cy - roi_.cy);
                const f32 ratio = found.size / std::max(roi_.size, 1.0f);
                const bool disagree = dc > 0.25f * roi_.size || ratio < 0.7f || ratio > 1.45f;
                // Trocar de ROI só para a MESMA pessoa (ROI que escorregou), nunca
                // para outra pessoa longe ou de outro tamanho.
                const bool samePerson = dc < 0.6f * roi_.size && similar_scale(found, roi_);
                if ((!ok || border || disagree) && samePerson) {
                    PoseOutput o2;
                    f32 p2 = 0.0f;
                    if (run(image, found, o2, p2) && (!ok || p2 >= presence)) {
                        o = o2;
                        presence = p2;
                        ok = true;
                        roi_ = found;
                        for (OneEuro1& f : auxFilter_) f.started = false;
                    }
                }
            }
        }
    }
    if (!ok) {
        if (roi_.valid()) {
            lost_ = roi_;
            lostFrames_ = 0;
        }
        reset();
        return false;
    }
    lost_ = {};
    lostFrames_ = 0;
    if (options_.fixSwaps) fix_swaps(o);
    roi_ = roi_from_aux(o, dt, w, h);
    prev_ = o;
    havePrev_ = true;
    out = o;
    return true;
}

} // namespace aurea::mocap
