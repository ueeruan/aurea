#include "aurea/project/Serialization.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/vector/Vector.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/tracking/CameraTrackData.hpp"
#include "aurea/tracking/MotionTrackData.hpp"
#include <cmath>
#include <limits>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace aurea {
namespace {

// -----------------------------------------------------------------------------
// CRC-32 (polinômio IEEE, o mesmo do zlib).
//
// Serve para detectar corrupção de seção, não para segurança. Uma queda no meio
// de uma escrita produz uma seção com bytes faltando, e sem checksum o leitor
// tentaria interpretar lixo como layers — e um id inválido vindo de lixo pode
// fazer o motor acessar memória errada. Com checksum, a seção é recusada.
// -----------------------------------------------------------------------------
u32 g_crc_table[256];
bool  g_crc_ready = false;

void init_crc_table() noexcept {
    for (u32 i = 0; i < 256; ++i) {
        u32 c = i;
        for (u32 k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        g_crc_table[i] = c;
    }
    g_crc_ready = true;
}

u32 crc32(const void* data, usize size) noexcept {
    if (!g_crc_ready) init_crc_table();
    const u8* p = static_cast<const u8*>(data);
    u32 crc = 0xFFFFFFFFu;
    for (usize i = 0; i < size; ++i) {
        crc = g_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/// CRC do caminho combinado com o tamanho — usa uma semente diferente para
/// que dois arquivos de conteúdo idêntico em pastas diferentes não colidam.
u64 content_hash(const void* data, usize size) noexcept {
    const u32 a = crc32(data, size);
    const u32 b = crc32(data, size) ^ 0x5A5A5A5Au;   // segunda passada com sal
    return (static_cast<u64>(a) << 32) | b;
}

// -----------------------------------------------------------------------------
// Escritor e leitor de bytes.
//
// Sem <fstream>: o comportamento de exceção do iostream não combina com um
// motor compilado sem exceções, e o custo de formatar via stream é
// desnecessário para dados binários.
// -----------------------------------------------------------------------------
class ByteWriter {
public:
    void u8v(u8 v) { buf_.push_back(v); }
    void u16v(u16 v) { raw(&v, 2); }
    void u32v(u32 v) { raw(&v, 4); }
    void u64v(u64 v) { raw(&v, 8); }
    void i32v(i32 v) { raw(&v, 4); }
    void i64v(i64 v) { raw(&v, 8); }
    void f32v(f32 v) { raw(&v, 4); }
    void f64v(f64 v) { raw(&v, 8); }
    void boolv(bool v) { u8v(v ? 1 : 0); }

    void raw(const void* p, usize n) {
        const auto* b = static_cast<const u8*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }

    void str(const std::string& s) {
        u32v(static_cast<u32>(s.size()));
        raw(s.data(), s.size());
    }

    void vec2(Vec2 v) { f32v(v.x); f32v(v.y); }
    void vec3(Vec3 v) { f32v(v.x); f32v(v.y); f32v(v.z); }
    void vec4(Vec4 v) { f32v(v.x); f32v(v.y); f32v(v.z); f32v(v.w); }
    void rect(Rect r) { f32v(r.x); f32v(r.y); f32v(r.w); f32v(r.h); }
    void color(Color c) { f32v(c.r); f32v(c.g); f32v(c.b); f32v(c.a); }

    [[nodiscard]] const std::vector<u8>& bytes() const noexcept { return buf_; }
    [[nodiscard]] usize size() const noexcept { return buf_.size(); }

private:
    std::vector<u8> buf_;
};

class ByteReader {
public:
    ByteReader(const u8* data, usize size) noexcept : data_(data), size_(size) {}

    [[nodiscard]] bool good() const noexcept { return !failed_; }
    [[nodiscard]] usize remaining() const noexcept { return failed_ ? 0 : size_ - pos_; }

    u8 u8v() {
        if (!need(1)) return 0;
        return data_[pos_++];
    }
    u16 u16v() { u16 v = 0; raw(&v, 2); return v; }
    u32 u32v() { u32 v = 0; raw(&v, 4); return v; }
    u64 u64v() { u64 v = 0; raw(&v, 8); return v; }
    i32 i32v() { i32 v = 0; raw(&v, 4); return v; }
    i64 i64v() { i64 v = 0; raw(&v, 8); return v; }
    f32 f32v() { f32 v = 0; raw(&v, 4); return v; }
    f64 f64v() { f64 v = 0; raw(&v, 8); return v; }
    bool boolv() { return u8v() != 0; }

    void raw(void* out, usize n) {
        if (!need(n)) {
            std::memset(out, 0, n);
            return;
        }
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
    }

    std::string str() {
        const u32 n = u32v();
        if (!good() || !need(n)) return {};
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }

    Vec2 vec2() { Vec2 v; v.x = f32v(); v.y = f32v(); return v; }
    Vec3 vec3() { Vec3 v; v.x = f32v(); v.y = f32v(); v.z = f32v(); return v; }
    Vec4 vec4() { Vec4 v; v.x = f32v(); v.y = f32v(); v.z = f32v(); v.w = f32v(); return v; }
    Rect rect() { Rect r; r.x = f32v(); r.y = f32v(); r.w = f32v(); r.h = f32v(); return r; }
    Color color() { Color c; c.r = f32v(); c.g = f32v(); c.b = f32v(); c.a = f32v(); return c; }

    /// Consome até o fim sem falhar. Usado quando uma seção ganha campos novos
    /// numa versão futura (ou perde, numa anterior): a leitura do que existe é
    /// válida e o resto fica no padrão.
    void skip_to_end() noexcept { pos_ = size_; }
    void fail() noexcept { failed_ = true; pos_ = size_; }

private:
    [[nodiscard]] bool need(usize n) noexcept {
        if (failed_) return false;
        if (n > size_ - pos_) { failed_ = true; return false; }
        return true;
    }

    const u8* data_ = nullptr;
    usize     size_ = 0;
    usize     pos_  = 0;
    bool      failed_ = false;
};

// -----------------------------------------------------------------------------
// Gravação de filhos — helpers que percorrem o modelo.
// -----------------------------------------------------------------------------

/// Versão da seção Timeline que está sendo LIDA (ver o histórico em read_layer).
thread_local u32 g_readingTimelineVersion = kTimelineSectionVersion;

void write_track(ByteWriter& w, const Track& t) {
    w.u16v(static_cast<u16>(t.property));
    w.u32v(t.effectIndex);
    w.u32v(t.effectParamIndex);
    w.f32v(t.staticValue);
    w.u32v(static_cast<u32>(t.keys.size()));
    for (const Keyframe& k : t.keys) {
        w.i64v(k.time.value);
        w.f32v(k.value);
        w.u8v(static_cast<u8>(k.interp));
        w.f32v(k.bx1); w.f32v(k.by1); w.f32v(k.bx2); w.f32v(k.by2);
        w.f32v(k.tangentIn); w.f32v(k.tangentOut);
        w.u16v(k.easingPreset);
        w.u8v(clamp_ease_power(k.easePower));   // v35
    }
}

/// Enum lido de arquivo: valor fora da faixa é corrupção (ou fuzz) e vira o
/// padrão. Sem isto um byte ruim chega ao renderer como índice de tabela.
template <typename E, typename Raw>
E checked_enum(Raw raw, E last, E fallback) noexcept {
    return static_cast<u32>(raw) <= static_cast<u32>(last) ? static_cast<E>(raw) : fallback;
}

void read_track(ByteReader& r, Track& t) {
    t.property = checked_enum(r.u16v(), static_cast<TrackProperty>(static_cast<u16>(TrackProperty::_Count) - 1),
                              TrackProperty::_Count);
    t.effectIndex = r.u32v();
    t.effectParamIndex = r.u32v();
    t.staticValue = r.f32v();
    const u32 count = r.u32v();

    // Guarda contra um arquivo corrompido que declare um bilhão de keyframes:
    // o limite é o mesmo do motor, então um valor acima é lixo.
    if (count > kMaxKeyframes) { r.skip_to_end(); return; }

    t.keys.clear();
    t.keys.reserve(count);
    for (u32 i = 0; i < count && r.good(); ++i) {
        Keyframe k;
        k.time = FrameIndex{r.i64v()};
        k.value = r.f32v();
        k.interp = checked_enum(r.u8v(), kLastInterpolation, Interpolation::Linear);
        k.bx1 = r.f32v(); k.by1 = r.f32v(); k.bx2 = r.f32v(); k.by2 = r.f32v();
        k.tangentIn = r.f32v(); k.tangentOut = r.f32v();
        k.easingPreset = r.u16v();
        k.easePower = g_readingTimelineVersion >= 35 ? clamp_ease_power(r.u8v()) : u8{1};
        t.keys.push_back(k);
    }
    // Projetos antigos podem trazer tempos repetidos ou fora de ordem.
    // O último registro vence, como na importação de presets: preservamos o
    // keyframe completo (valor, curva e tangentes), não só o valor visível.
    // Evita o caso em que find_exact edita uma duplicata e sample lê outra.
    const auto byTime = [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; };
    if (!std::is_sorted(t.keys.begin(), t.keys.end(), byTime)) {
        std::stable_sort(t.keys.begin(), t.keys.end(), byTime);
    }
    usize countUnique = 0;
    for (usize i = 0; i < t.keys.size(); ++i) {
        if (countUnique > 0 && t.keys[countUnique - 1].time == t.keys[i].time) {
            t.keys[countUnique - 1] = t.keys[i];
        } else {
            if (countUnique != i) t.keys[countUnique] = t.keys[i];
            ++countUnique;
        }
    }
    t.keys.resize(countUnique);
    t.lastIndex = 0;
}

// Efeito no formato da API nova: o tipo é o id estável (hash da chave), e cada
// parâmetro é um slot genérico (4 componentes + referência + origem). Um tipo
// desconhecido é lido e guardado inteiro — o projeto abre, o efeito não
// desenha, e nada do que a pessoa configurou se perde ao salvar de novo.
void write_effect(ByteWriter& w, const EffectInstance& e) {
    w.u32v(e.id);
    w.u32v(e.type);
    w.boolv(e.enabled);
    w.boolv(e.expanded);
    w.u32v(static_cast<u32>(e.params.size()));
    for (const ParamSlot& s : e.params) {
        for (f32 f : s.constant.v) w.f32v(f);
        w.u64v(s.constant.ref);
        w.u32v(s.expression);
        w.u8v(static_cast<u8>(s.source));
    }
    w.u32v(static_cast<u32>(e.curves.size()));
    for (const CurveData& c : e.curves) {
        for (const auto& ch : c.channel) {
            w.u32v(static_cast<u32>(ch.size()));
            for (const CurveData::Point& p : ch) { w.f32v(p.x); w.f32v(p.y); }
        }
    }
    w.u32v(static_cast<u32>(e.gradients.size()));
    for (const GradientData& g : e.gradients) {
        w.u32v(static_cast<u32>(g.stops.size()));
        for (const GradientStop& s : g.stops) { w.f32v(s.position); w.vec4(s.color); }
    }
    w.u64v(e.mask.pack());
}

void read_effect(ByteReader& r, EffectInstance& e) {
    e.id = r.u32v();
    e.type = r.u32v();
    e.enabled = r.boolv();
    e.expanded = r.boolv();
    const u32 paramCount = r.u32v();
    if (paramCount > 256) { r.skip_to_end(); return; }
    e.params.resize(paramCount);
    for (ParamSlot& s : e.params) {
        for (f32& f : s.constant.v) f = r.f32v();
        s.constant.ref = r.u64v();
        s.expression = r.u32v();
        const u8 src = r.u8v();
        s.source = src <= static_cast<u8>(ParamSource::Expression) ? static_cast<ParamSource>(src)
                                                                   : ParamSource::Constant;
    }
    const u32 curveCount = r.u32v();
    if (curveCount > 64) { r.skip_to_end(); return; }
    e.curves.resize(curveCount);
    for (CurveData& c : e.curves) {
        for (auto& ch : c.channel) {
            const u32 n = r.u32v();
            if (n > 4096) { r.skip_to_end(); return; }
            ch.resize(n);
            for (CurveData::Point& p : ch) { p.x = r.f32v(); p.y = r.f32v(); }
        }
    }
    const u32 gradientCount = r.u32v();
    if (gradientCount > 64) { r.skip_to_end(); return; }
    e.gradients.resize(gradientCount);
    for (GradientData& g : e.gradients) {
        const u32 n = r.u32v();
        if (n > 4096) { r.skip_to_end(); return; }
        g.stops.resize(n);
        for (GradientStop& s : g.stops) { s.position = r.f32v(); s.color = r.vec4(); }
    }
    e.mask = MaskId::unpack(r.u64v());
}

void write_mask(ByteWriter& w, const Mask& m) {
    w.u32v(m.id);
    w.str(m.name);
    w.u8v(static_cast<u8>(m.operation));
    w.boolv(m.inverted);
    w.f32v(m.feather);
    w.f32v(m.expansion);
    w.f32v(m.opacity);
    w.boolv(m.closed);
    w.u32v(static_cast<u32>(m.points.size()));
    for (const MaskPoint& p : m.points) {
        w.vec2(p.position);
        w.vec2(p.inTangent);
        w.vec2(p.outTangent);
    }
    w.u32v(m.previewPointLimit);
}

void read_mask(ByteReader& r, Mask& m) {
    m.id = r.u32v();
    m.name = r.str();
    m.operation = checked_enum(r.u8v(), MaskOperation::None, MaskOperation::Add);
    m.inverted = r.boolv();
    m.feather = r.f32v();
    m.expansion = r.f32v();
    m.opacity = r.f32v();
    m.closed = r.boolv();
    const u32 count = r.u32v();
    m.points.clear();
    // Uma máscara com mais pontos que isso não é útil e indica corrupção.
    if (count > 100000u) { r.skip_to_end(); return; }
    m.points.reserve(count);
    for (u32 i = 0; i < count && r.good(); ++i) {
        MaskPoint p;
        p.position = r.vec2();
        p.inTangent = r.vec2();
        p.outTangent = r.vec2();
        m.points.push_back(p);
    }
    m.previewPointLimit = r.u32v();
    // O cache da máscara é derivado; a chave gravada não vale porque a
    // rasterização depende da GPU desta sessão.
    m.cacheKey = 0;
}

void write_motion_track(ByteWriter& w, const tracking::MotionTrackData& d) {
    w.u32v(static_cast<u32>(d.tool)); w.u32v(static_cast<u32>(d.model));
    w.u32v(d.sourceW); w.u32v(d.sourceH); w.u32v(d.analysisW); w.u32v(d.analysisH);
    w.u32v(d.lost); w.u32v(d.reacquired); w.u32v(d.pointCount); w.f64v(d.fps);
    w.u32v(static_cast<u32>(d.path.size()));
    for (usize i=0;i<d.path.size();++i) {
        w.i64v(d.localFrames[i]); w.i64v(d.sourceUs[i]);
        const auto& p=d.path[i]; w.boolv(p.valid); w.f32v(p.confidence); w.f32v(p.rms); w.u32v(p.inliers);
        for (auto v:p.path.m) w.f64v(v);
        for (auto q:d.points[i]) w.vec2(q);
    }
}
std::shared_ptr<const tracking::MotionTrackData> read_motion_track(ByteReader& r) {
    auto d=std::make_shared<tracking::MotionTrackData>();
    const auto tool=r.u32v(),model=r.u32v();d->tool=static_cast<tracking::MotionTool>(tool);d->model=static_cast<tracking::MotionModel>(model);
    d->sourceW=r.u32v();d->sourceH=r.u32v();d->analysisW=r.u32v();d->analysisH=r.u32v();
    d->lost=r.u32v();d->reacquired=r.u32v();d->pointCount=r.u32v();d->fps=r.f64v();const auto n=r.u32v();
    if(tool>4||model>3||d->pointCount!=(tool==0?1u:tool==1?2u:tool==4?0u:4u)||n<2||n>tracking::kMaxMotionTrackFrames||d->lost>n||!std::isfinite(d->fps)||d->fps<=0||d->fps>1000||
       !d->sourceW||!d->sourceH||d->sourceW>65536||d->sourceH>65536||!d->analysisW||!d->analysisH||d->analysisW>d->sourceW||d->analysisH>d->sourceH||r.remaining()<static_cast<u64>(n)*133){r.fail();return {};}
    d->path.resize(n);d->localFrames.resize(n);d->sourceUs.resize(n);d->points.resize(n);
    for(u32 i=0;i<n;++i){
        d->localFrames[i]=r.i64v();d->sourceUs[i]=r.i64v();auto& p=d->path[i];p.valid=r.boolv();p.confidence=r.f32v();p.rms=r.f32v();p.inliers=r.u32v();
        if(d->localFrames[i]<0||d->localFrames[i]>2147483646||d->sourceUs[i]<0||!std::isfinite(p.confidence)||p.confidence<0||p.confidence>1||!std::isfinite(p.rms)||p.rms<0){r.fail();return {};}
        for(auto& v:p.path.m){v=r.f64v();if(!std::isfinite(v)){r.fail();return {};}}
        for(auto& q:d->points[i]){q=r.vec2();if(!tracking::Tracks2D::present(q)){r.fail();return {};}}
        if(i&&std::abs(d->localFrames[i]-d->localFrames[i-1])!=1){r.fail();return {};}
        if(i>1&&(d->localFrames[i]-d->localFrames[i-1])!=(d->localFrames[1]-d->localFrames[0])){r.fail();return {};}
    }
    if(d->lost!=std::count_if(d->path.begin(),d->path.end(),[](const auto& p){return !p.valid;})){r.fail();return {};}
    return d;
}

void write_camera_track(ByteWriter& w, const tracking::CameraTrackData& d) {
    w.u64v(d.cacheKey); w.u32v(d.mode);
    w.u32v(d.frames); w.u32v(d.analysisW); w.u32v(d.analysisH);
    for (i64 us : d.sourceUs) w.i64v(us);
    const auto& s = d.solution;
    w.boolv(s.ok); w.boolv(s.rotationOnly); w.f32v(s.fovY);
    w.u32v(s.tracks); w.u32v(s.inliers); w.u32v(s.framesSolved);
    w.f32v(s.rmsError); w.f32v(s.confidence); w.str(s.failure);
    w.u32v(static_cast<u32>(s.poses.size()));
    for (const auto& p : s.poses) {
        w.boolv(p.valid);
        for (f64 v : p.R) w.f64v(v);
        for (f64 v : p.t) w.f64v(v);
    }
    w.u32v(static_cast<u32>(s.points.size()));
    for (Vec3 p : s.points) w.vec3(p);
    w.u32v(static_cast<u32>(d.tracks.pos.size()));
    for (usize t = 0; t < d.tracks.pos.size(); ++t) {
        w.u8v(t < s.trackSolved.size() ? s.trackSolved[t] : 0);
        const auto& row = d.tracks.pos[t];
        u32 count = 0;
        for (Vec2 p : row) if (tracking::Tracks2D::present(p)) ++count;
        w.u32v(count);
        // Sparse observations: absent frames consume no coordinates on disk.
        for (u32 f = 0; f < row.size(); ++f) if (tracking::Tracks2D::present(row[f])) {
            w.u16v(static_cast<u16>(f)); w.vec2(row[f]);
        }
    }
}

std::shared_ptr<const tracking::CameraTrackData> read_camera_track(ByteReader& r) {
    auto d = std::make_shared<tracking::CameraTrackData>();
    d->cacheKey = r.u64v(); d->mode = r.u32v();
    d->frames = r.u32v(); d->analysisW = r.u32v(); d->analysisH = r.u32v();
    if (!r.good() || d->frames < 10 || d->frames > tracking::kMaxCameraTrackFrames ||
        !d->analysisW || !d->analysisH || d->analysisW > 8192 || d->analysisH > 8192 || d->mode > 2 ||
        r.remaining() < d->frames * sizeof(i64)) { r.fail(); return {}; }
    d->sourceUs.resize(d->frames);
    for (i64& us : d->sourceUs) { us = r.i64v(); if (us < 0) { r.fail(); return {}; } }
    auto& s = d->solution;
    s.ok = r.boolv(); s.rotationOnly = r.boolv(); s.fovY = r.f32v();
    s.tracks = r.u32v(); s.inliers = r.u32v(); s.framesSolved = r.u32v();
    s.rmsError = r.f32v(); s.confidence = r.f32v(); s.failure = r.str();
    const u32 poses = r.u32v();
    if (!r.good() || poses != d->frames || !std::isfinite(s.fovY) || s.fovY <= 0 || s.fovY >= kPi ||
        !std::isfinite(s.rmsError) || s.rmsError < 0 || !std::isfinite(s.confidence) || s.confidence < 0 || s.confidence > 1 ||
        s.framesSolved > d->frames || r.remaining() < static_cast<u64>(poses) * 97) { r.fail(); return {}; }
    s.poses.resize(poses);
    for (auto& p : s.poses) {
        p.valid = r.boolv();
        for (f64& v : p.R) { v = r.f64v(); if (!std::isfinite(v)) { r.fail(); return {}; } }
        for (f64& v : p.t) { v = r.f64v(); if (!std::isfinite(v)) { r.fail(); return {}; } }
    }
    const u32 points = r.u32v();
    if (points > 100000 || r.remaining() < static_cast<u64>(points) * 12) { r.fail(); return {}; }
    s.points.resize(points);
    for (Vec3& p : s.points) {
        p = r.vec3();
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) { r.fail(); return {}; }
    }
    const u32 tracks = r.u32v();
    if (tracks > 100000 || static_cast<u64>(tracks) * d->frames > tracking::kMaxCameraTrackCells ||
        r.remaining() < static_cast<u64>(tracks) * 5) { r.fail(); return {}; }
    d->tracks.frames = d->frames; d->tracks.width = d->analysisW; d->tracks.height = d->analysisH;
    d->tracks.pos.resize(tracks); s.trackSolved.resize(tracks);
    u32 solved = 0;
    for (u32 t = 0; t < tracks; ++t) {
        s.trackSolved[t] = r.u8v(); solved += s.trackSolved[t] ? 1 : 0;
        const u32 count = r.u32v();
        if (s.trackSolved[t] > 1 || count > d->frames || r.remaining() < static_cast<u64>(count) * 10) { r.fail(); return {}; }
        auto& row = d->tracks.pos[t]; row.assign(d->frames, Vec2{NAN, NAN});
        i32 previous = -1;
        for (u32 i = 0; i < count; ++i) {
            const u32 frame = r.u16v(); const Vec2 p = r.vec2();
            if (frame >= d->frames || static_cast<i32>(frame) <= previous || !tracking::Tracks2D::present(p) ||
                p.x < 0 || p.y < 0 || p.x >= d->analysisW || p.y >= d->analysisH) { r.fail(); return {}; }
            row[frame] = p; previous = static_cast<i32>(frame);
        }
    }
    if (!r.good() || (!s.rotationOnly && solved != points) || s.inliers != solved || s.tracks > tracks ||
        s.framesSolved != std::count_if(s.poses.begin(), s.poses.end(), [](const auto& p) { return p.valid; })) { r.fail(); return {}; }
    return d;
}

void write_layer(ByteWriter& w, const Layer& l) {
    w.u8v(static_cast<u8>(l.kind));
    w.str(l.name);

    w.i64v(l.start.value);
    w.i64v(l.end.value);
    w.i64v(l.offset.value);
    write_track(w, l.timeRemap);
    w.boolv(l.timeRemapEnabled);

    w.u64v(l.parent.pack());
    w.u32v(l.zOrder);
    w.u16v(static_cast<u16>(l.blendMode));
    w.boolv(l.visible);
    w.boolv(l.locked);
    w.boolv(l.solo);
    w.boolv(l.threeD);

    w.u64v(l.source.pack());
    w.u64v(l.nested.composition.pack());
    w.boolv(l.nested.collapsed);

    // Transform
    w.vec3(l.transform.position);
    w.vec3(l.transform.scale);
    w.vec3(l.transform.rotation);
    w.vec3(l.transform.anchor);
    w.f32v(l.transform.opacity);
    w.f32v(l.transform.skewX);
    w.f32v(l.transform.skewY);
    w.f32v(l.transform.motionBlurAmount);
    w.boolv(l.transform.motionBlurEnabled);

    // Tracks
    w.u32v(l.tracks.size());
    for (u32 i = 0; i < l.tracks.size(); ++i) write_track(w, l.tracks.at(i));

    // Efeitos
    w.u32v(static_cast<u32>(l.effects.size()));
    for (const EffectInstance& e : l.effects) write_effect(w, e);

    // Máscaras
    w.u32v(static_cast<u32>(l.masks.size()));
    for (const Mask& m : l.masks) write_mask(w, m);

    // Específico de tipo — gravado sempre, mesmo quando não se aplica. É mais
    // bytes e muito menos código de migração, e o custo é de dezenas de bytes
    // por layer.
    w.str(l.text.content);
    w.u64v(l.text.font.pack());
    w.f32v(l.text.size);
    w.vec4(l.text.color);
    w.f32v(l.text.strokeWidth);
    w.vec4(l.text.strokeColor);
    w.u32v(l.text.alignment);
    w.f32v(l.text.lineHeight);
    w.f32v(l.text.tracking);
    w.boolv(l.text.rtl);
    w.boolv(l.text.autoSize);
    w.rect(l.text.box);

    w.u32v(l.shape.shapeType);
    w.rect(l.shape.bounds);
    w.f32v(l.shape.cornerRadius);
    w.f32v(l.shape.points);
    w.f32v(l.shape.innerRadius);
    w.vec4(l.shape.fillColor);
    w.vec4(l.shape.strokeColor);
    w.f32v(l.shape.strokeWidth);
    w.boolv(l.shape.filled);
    w.f32v(l.shape.trimStart);
    w.f32v(l.shape.trimEnd);
    w.f32v(l.shape.trimOffset);
    w.boolv(l.shape.trimEnabled);
    w.u32v(static_cast<u32>(l.shape.path.size()));
    for (const Vec2& p : l.shape.path) w.vec2(p);

    w.f32v(l.camera.fov);
    w.f32v(l.camera.focalLength);
    w.f32v(l.camera.nearPlane);
    w.f32v(l.camera.farPlane);
    w.f32v(l.camera.focusDistance);
    w.f32v(l.camera.aperture);
    w.boolv(l.camera.active);

    w.u8v(static_cast<u8>(l.light.kind));
    w.vec4(l.light.color);
    w.f32v(l.light.intensity);
    w.f32v(l.light.range);
    w.f32v(l.light.coneAngle);
    w.f32v(l.light.penumbra);
    w.boolv(l.light.castShadows);
    w.f32v(l.light.shadowBias);

    w.u64v(l.model.scene.pack());
    w.i32v(l.model.animationClip);
    w.f32v(l.model.timeScale);
    w.boolv(l.model.castShadows);
    w.boolv(l.model.receiveShadows);
    w.i32v(l.model.forcedLod);
    w.f32v(l.model.unitScale);
    w.f32v(l.model.pivot.x);
    w.f32v(l.model.pivot.y);
    w.f32v(l.model.pivot.z);

    w.u32v(l.particles.emitterType);
    w.f32v(l.particles.rate);
    w.f32v(l.particles.lifetime);
    w.vec3(l.particles.gravity);
    w.f32v(l.particles.startSize);
    w.f32v(l.particles.endSize);
    w.f32v(l.particles.startOpacity);
    w.f32v(l.particles.endOpacity);
    w.f32v(l.particles.speed);
    w.f32v(l.particles.spread);
    w.u32v(l.particles.maxParticles);
    w.u32v(l.particles.blendMode);
    w.boolv(l.particles.collideEnvironment);

    w.f32v(l.gain);
    w.f32v(l.pan);
    w.boolv(l.muted);
    w.i64v(l.fadeIn.value);
    w.i64v(l.fadeOut.value);

    w.u32v(l.nextEffectId);
    w.u32v(l.nextMaskId);
    // v3
    w.f32v(l.speed);
    w.boolv(l.reversed);
    // v6
    w.boolv(l.motionBlur);
    // v7
    w.vec4(l.particles.startColor);
    w.vec4(l.particles.endColor);
    w.f32v(l.particles.direction);
    w.u32v(l.particles.seed);
    w.f32v(l.particles.emitterSize.x);
    w.f32v(l.particles.emitterSize.y);
    w.f32v(l.particles.emitterOffset.x);
    w.f32v(l.particles.emitterOffset.y);
    // v8
    w.u8v(l.transitionIn);
    w.u8v(l.transitionOut);
    w.u32v(l.transitionInFrames);
    w.u32v(l.transitionOutFrames);
    // v9
    w.u32v(l.echoCount);
    w.f32v(l.echoDelay);
    w.f32v(l.echoDecay);
    w.f32v(l.rgbDelay);
    // v10
    w.u8v(l.frameBlend);
    // v11
    w.f32v(l.vectorBlur);
    // v12
    w.str(l.text.fontFamily);
    w.u32v(l.text.fontWeight);
    w.boolv(l.text.fontItalic);
    w.str(l.text.fontPath);
    // v13: rich text, caixa, fundo, sombra
    w.u32v(l.text.boxMode);
    w.u32v(static_cast<u32>(l.text.spans.size()));
    for (const TextSpan& sp : l.text.spans) {
        w.u32v(sp.start);
        w.u32v(sp.end);
        w.boolv(sp.hasColor);
        w.vec4(sp.color);
        w.u32v(sp.weight);
        w.f32v(sp.scale);
    }
    w.boolv(l.text.background);
    w.vec4(l.text.backgroundColor);
    w.f32v(l.text.backgroundPadding);
    w.f32v(l.text.backgroundRadius);
    w.boolv(l.text.shadow);
    w.vec4(l.text.shadowColor);
    w.f32v(l.text.shadowOffset.x);
    w.f32v(l.text.shadowOffset.y);
    w.f32v(l.text.shadowBlur);
    // v14: animadores de texto
    w.u32v(static_cast<u32>(l.text.animators.size()));
    for (const TextAnimator& a : l.text.animators) {
        w.str(a.name);
        w.boolv(a.enabled);
        const TextSelector& s = a.selector;
        w.u32v(s.basedOn); w.u32v(s.type); w.u32v(s.shape); w.boolv(s.randomOrder); w.u32v(s.seed);
        w.f32v(s.start); w.f32v(s.end); w.f32v(s.offset); w.f32v(s.amount); w.f32v(s.easeHigh); w.f32v(s.easeLow); w.f32v(s.wiggleRate);
        w.u32v(a.props);
        w.f32v(a.position.x); w.f32v(a.position.y); w.f32v(a.position.z);
        w.f32v(a.scale.x); w.f32v(a.scale.y);
        w.f32v(a.rotation.x); w.f32v(a.rotation.y); w.f32v(a.rotation.z);
        w.f32v(a.opacity); w.f32v(a.tracking); w.f32v(a.blur); w.f32v(a.skew); w.f32v(a.strokeWidth); w.f32v(a.charOffset);
        w.vec4(a.fill); w.vec4(a.stroke);
    }
    // v15: legenda de qual camada
    w.u64v(l.text.captionSource);
    // v16: camada de ajuste, guia e etiqueta de cor
    w.boolv(l.adjustment);
    w.boolv(l.guide);
    w.u8v(l.label);
    // v17: track matte e caminho animado das máscaras (na ordem de l.masks)
    w.u64v(l.matteSource.pack());
    w.u8v(static_cast<u8>(l.matteMode));
    w.u32v(static_cast<u32>(l.masks.size()));
    for (const Mask& m : l.masks) {
        w.u32v(static_cast<u32>(m.pathKeys.size()));
        for (const MaskPathKey& k : m.pathKeys) {
            w.i64v(k.frame);
            w.u8v(k.interp);
            w.u32v(static_cast<u32>(k.points.size()));
            for (const MaskPoint& pt : k.points) {
                w.vec2(pt.position);
                w.vec2(pt.inTangent);
                w.vec2(pt.outTangent);
            }
        }
    }
    // v18: expressões por trilha (a chave da trilha + ligada + fonte). Só as
    // trilhas que têm expressão; o programa é recompilado na leitura.
    u32 exprCount = l.timeRemap.expression ? 1u : 0u;
    for (u32 i = 0; i < l.tracks.size(); ++i) exprCount += l.tracks.at(i).expression ? 1u : 0u;
    w.u32v(exprCount);
    auto writeExpr = [&](const Track& t, u8 where) {
        w.u8v(where);   // 0 = TrackSet, 1 = time remap
        w.u16v(static_cast<u16>(t.property));
        w.u32v(t.effectIndex);
        w.u32v(t.effectParamIndex);
        w.boolv(t.expressionEnabled);
        w.str(t.expression->source);
    };
    if (l.timeRemap.expression) writeExpr(l.timeRemap, 1);
    for (u32 i = 0; i < l.tracks.size(); ++i) {
        if (l.tracks.at(i).expression) writeExpr(l.tracks.at(i), 0);
    }
    // v19: camada vetorial (o documento em floats, o mesmo codec da UI) e texto no caminho
    {
        std::vector<f32> doc;
        std::string names;
        vector::encode_document(l.shape.vector, doc, names);
        w.str(names);
        w.u32v(static_cast<u32>(doc.size()));
        for (f32 v : doc) w.f32v(v);
        w.u64v(l.text.pathLayer);
        w.f32v(l.text.pathOffset);
        w.boolv(l.text.pathPerpendicular);
        w.boolv(l.text.pathReverse);
    }
    // v20: Aurea Particular. O sistema de particulas deixou de ser uma taxonomia
    // de tres presets fixos e virou UM sistema parametrizavel — emissor, fisica,
    // rastro, aux e colisao. Os tres nomes antigos (Faiscas/Neve/Poeira de luz)
    // continuam existindo como PRESET, nao como sistema separado.
    w.u32v(l.particles.emitterType);
    w.f32v(l.particles.emitterRadius);
    w.f32v(l.particles.emitterRotation);
    w.f32v(l.particles.emitterDepth);
    w.u32v(l.particles.gridX);
    w.u32v(l.particles.gridY);
    w.boolv(l.particles.emitFill);
    w.u32v(l.particles.burst);
    w.f32v(l.particles.lifeRandom);
    w.f32v(l.particles.speedRandom);
    w.f32v(l.particles.inheritVelocity);
    w.u32v(l.particles.particleType);
    w.f32v(l.particles.softness);
    w.f32v(l.particles.rotation);
    w.f32v(l.particles.rotationRandom);
    w.f32v(l.particles.spin);
    w.f32v(l.particles.drag);
    w.vec3(l.particles.wind);
    w.f32v(l.particles.turbulence);
    w.f32v(l.particles.turbulenceScale);
    w.f32v(l.particles.turbulenceSpeed);
    w.f32v(l.particles.vortex);
    w.f32v(l.particles.attractor);
    w.f32v(l.particles.trailLength);
    w.f32v(l.particles.trailTaper);
    w.u32v(l.particles.auxCount);
    w.f32v(l.particles.auxAt);
    w.f32v(l.particles.auxLife);
    w.f32v(l.particles.auxSpeed);
    w.f32v(l.particles.auxSize);
    w.f32v(l.particles.auxSpread);
    w.vec4(l.particles.auxColor);
    w.u32v(l.particles.collision);
    w.f32v(l.particles.collisionY);
    w.f32v(l.particles.collisionBounce);
    // v21: Aurea Particular completo (espaço, fonte do emissor, textura/malha,
    // aleatórios, colisão esfera/caixa, curvas ao longo da vida).
    const ParticleData& pp = l.particles;
    w.u32v(pp.emitterSpace);
    w.u32v(pp.emitFrom);
    w.u64v(pp.emitterSource);
    w.u64v(pp.textureAsset);
    w.u64v(pp.meshSource);
    w.f32v(pp.auxProbability);
    w.f32v(pp.trailWidth);
    w.f32v(pp.trailOpacity);
    w.f32v(pp.sizeRandom);
    w.f32v(pp.opacityRandom);
    w.f32v(pp.colorRandom);
    w.vec3(pp.collisionCenter);
    w.f32v(pp.collisionRadius);
    w.vec3(pp.collisionBox);
    w.f32v(pp.meshScale);
    w.boolv(pp.meshLit);
    w.u32v(pp.colorStopCount);
    for (u32 i = 0; i < pp.colorStopCount; ++i) w.vec4(pp.colorStops[i]);
    w.u32v(pp.sizeCurveCount);
    for (u32 i = 0; i < pp.sizeCurveCount; ++i) { w.f32v(pp.sizeCurve[i].x); w.f32v(pp.sizeCurve[i].y); }
    w.u32v(pp.opacityCurveCount);
    for (u32 i = 0; i < pp.opacityCurveCount; ++i) { w.f32v(pp.opacityCurve[i].x); w.f32v(pp.opacityCurve[i].y); }
    // v22: ambiente por objeto (a luz que o objeto usa é dele, não do projeto).
    w.u32v(l.environmentSource);
    w.u64v(l.environmentAsset);
    w.f32v(l.environmentIntensity);
    w.f32v(l.environmentExposure);
    w.f32v(l.environmentRotation);
    w.boolv(l.environmentBackground);
    // v23: retired remap aliases remain recoverable, but cannot create ghost keys.
    w.boolv(l.timeRemapLegacyMigrated);
    w.u32v(static_cast<u32>(l.timeRemapLegacyTracks.size()));
    for (const Track& track : l.timeRemapLegacyTracks) {
        write_track(w, track);
        w.boolv(track.expressionEnabled);
        w.str(track.expression ? track.expression->source : std::string{});
    }
    // v24: material factors belong to the object, not its shared scene asset.
    w.u32v(static_cast<u32>(l.model.materials.size()));
    for (const MaterialOverride& material : l.model.materials) {
        w.u32v(material.materialIndex);
        w.u32v(material.mask);
        w.vec4(material.baseColor);
        w.f32v(material.metallic);
        w.f32v(material.roughness);
    }
    // v25: one caption track with timed words; old text layers remain readable.
    w.u32v(l.captionOptions.style); w.boolv(l.captionOptions.highlight); w.vec4(l.captionOptions.highlightColor);
    w.u32v(static_cast<u32>(l.captions.size()));
    for (const auto& segment : l.captions) {
        w.u64v(segment.id); w.i64v(segment.start); w.i64v(segment.end); w.str(segment.text);
        w.u32v(static_cast<u32>(segment.words.size()));
        for (const auto& word : segment.words) { w.str(word.text); w.i64v(word.start); w.i64v(word.end); }
    }
    // v27: exact 3D parent bind space, independent of animated TRS.
    w.boolv(l.hasParentBasis);
    if (l.hasParentBasis) for (const Vec4& column : l.parentBasis.col) w.vec4(column);
    // v28: portable camera analysis and generated-layer provenance.
    w.boolv(l.cameraTrack != nullptr);
    if (l.cameraTrack) write_camera_track(w, *l.cameraTrack);
    w.u64v(l.cameraTrackSource.pack()); w.u64v(l.cameraTrackKey);
    w.boolv(l.motionTrack != nullptr);
    if (l.motionTrack) write_motion_track(w, *l.motionTrack);
    w.vec2(l.camera.trackingSourceSize);
    w.u32v(l.motionTrackEffect);
    if(l.cameraTrack)for(const auto& c:l.cameraTrack->sceneCalibration.col)w.vec4(c);
    if(l.cameraTrack)w.u64v(l.cameraTrack->sourceSignature);
    if(l.motionTrack)w.u64v(l.motionTrack->sourceSignature);
    // v33: lente da câmera 3D (profundidade de campo e força do desfoque).
    w.boolv(l.camera.dofEnabled);
    w.f32v(l.camera.blurAmount);
    // v34: linha magnética da camada (edição de vídeo por faixa) e a linha a
    // que o trecho pertence — o split dá a MESMA linha aos dois pedaços.
    w.boolv(l.magneticTrack);
    w.u32v(l.trackId);
    // v36: animadores de camada (entrada/saída/wiggle), camadas afetadas pelo
    // ajuste e a câmera que atravessa o grupo.
    w.u32v(static_cast<u32>(l.layerAnimators.size()));
    for (const LayerAnimator& a : l.layerAnimators) {
        w.str(a.name);
        w.boolv(a.enabled);
        // Loop no bit alto do byte da curva: leitor antigo lê curva 3 e segue.
        w.u8v(a.unit); w.boolv(a.exit); w.u8v(static_cast<u8>(std::min<u8>(a.ease, 3) | (a.loop ? 0x80u : 0u)));
        w.boolv(a.scaleSeparated);
        w.u32v(a.wiggleSeed);
        for (f32 v : {a.progress, a.strength, a.delayMs, a.fromOpacity, a.fromPosX, a.fromPosY, a.fromScale, a.fromScaleY,
                      a.fromRotation, a.fromRotX, a.fromRotY, a.fromTracking, a.wigglePosX, a.wigglePosY, a.wiggleScale,
                      a.wiggleRotation, a.wiggleSpeed, a.wiggleHold})
            w.f32v(v);
    }
    w.u8v(l.adjustmentScope);
    w.boolv(l.nested.cameraPassThrough);
    // v37: as camadas escolhidas do ajuste (escopo 2).
    w.u32v(static_cast<u32>(l.adjustmentTargets.size()));
    for (LayerId t : l.adjustmentTargets) w.u64v(t.pack());
    // v38: rig 2D (juntas: id, pai, posição de montagem). A pose está nas
    // trilhas RigBone, que já viajam com as outras.
    w.u32v(l.rig.nextJointId);
    w.u32v(static_cast<u32>(l.rig.joints.size()));
    for (const RigJoint& j : l.rig.joints) {
        w.u32v(j.id);
        w.u32v(j.parent);
        w.vec2(j.pos);
    }
    // v39: manter o tom do áudio (remapeamento/velocidade).
    w.boolv(l.keepPitch);
    w.f32v(l.light.shadowStrength);
    // v43: mostrar interior (dupla face) do objeto 3D.
    w.u8v(static_cast<u8>(l.model.interior));
    // v44: parâmetros das formas paramétricas (shape::Param 7..14).
    w.f32v(l.shape.depth);
    w.f32v(l.shape.tip);
    w.f32v(l.shape.thickness);
    w.f32v(l.shape.sweep);
    w.f32v(l.shape.head);
    w.f32v(l.shape.shaft);
    w.f32v(l.shape.amplitude);
    w.f32v(l.shape.seed);
}

/// Versão da seção Timeline. v2: layer de modelo 3D guarda escala de unidade
/// e pivô (o enquadramento do import). v1 continua sendo lida (campos novos
/// com o padrão).
/// v3: velocidade e reverso da layer.
/// v16: camada de ajuste, guia (não exporta) e etiqueta de cor.
/// v17: track matte (camada + modo) e keyframes do caminho das máscaras.
/// v18: expressões por trilha (fonte + ligada), no fim de cada layer.
/// v19: camada vetorial (VectorData) e texto no caminho, no fim da camada.
/// v20: Aurea Particular (emissor, fisica, rastro, aux e colisao).
/// v21: Aurea Particular completo (espaço, emissor de camada/texto/caminho/malha,
///      textura/malha, colisão esfera/caixa, curvas ao longo da vida).
/// v22: ambiente por objeto 3D (Scene ou Custom, com HDRI, intensidade,
///      exposição e rotação próprios).
// v26: Particle World emitter/model IDs 10..12. Byte layout is unchanged,
// but older renderers must not silently reinterpret these as legacy emitters.
// v27: lossless 3D parenting compensation, including nested nulls.
// v28 also persists camera observations; generated layers refer back to this
// analysis after reopening instead of relying on an in-memory worker result.
// v32: ambiente de estúdio procedural e o chão do grupo 3D (FloorSettings).
// v33: lente da câmera (DOF ligado e força do desfoque) no fim da camada.
// v34: linha magnética da camada, no fim da camada. Projeto anterior a ela lê
//      com a opção DESLIGADA e continua com o modo Edição da composição.
// v36: animadores de camada, escopo do ajuste e câmera que atravessa o grupo,
//      no fim da camada. Antes dela: nenhum animador, ajuste em tudo abaixo e
//      grupo fechado para a câmera (o render de sempre).
// v43: mostrar interior do objeto 3D (Model3DData::interior, um byte no fim
//      da camada). Antes dela: automático.
// v44: parâmetros das formas paramétricas (profundidade, ponta, espessura,
//      abertura, ponta e haste da seta, amplitude, variante), 8 floats no fim
//      da camada. Antes dela: os padrões da ShapeData — profundidade, ponta,
//      espessura e ponta da seta "da forma" (negativos), abertura 270°, haste
//      0,44 —, que desenham cada forma antiga exatamente como antes.
// v37: camadas escolhidas do ajuste (escopo 2), depois da câmera do grupo.
//      Antes dela a lista é vazia (e o escopo só vai até 1).
// v38: rig 2D da camada de imagem (juntas), no fim da camada. Antes dela: sem
//      rig. A rotação dos ossos são trilhas (TrackProperty::RigBone).
// v39: "Manter o tom do áudio" da camada, no fim dela. Antes dela: desligado
//      (o som reamostra e o tom acompanha a velocidade, como sempre).
// v35: força da bézier (Keyframe::easePower) depois de cada keyframe; antes
//      dela todo keyframe lê com força 1 — a mesma curva de sempre.
// O número vive no cabeçalho público (Serialization.hpp) para os testes o
// compararem sem escrever um literal que envelhece (declarado lá em cima, com
// read_track, que é quem mais o consulta).

void read_layer(ByteReader& r, Layer& l) {
    l.kind = checked_enum(r.u8v(), LayerKind::Composition, LayerKind::Unknown);
    l.name = r.str();

    l.start = FrameIndex{r.i64v()};
    l.end = FrameIndex{r.i64v()};
    l.offset = FrameIndex{r.i64v()};
    read_track(r, l.timeRemap);
    l.timeRemapEnabled = r.boolv();

    l.parent = LayerId::unpack(r.u64v());
    l.zOrder = r.u32v();
    l.blendMode = checked_enum(r.u16v(), BlendMode::LinearBurn, BlendMode::Normal);
    l.visible = r.boolv();
    l.locked = r.boolv();
    l.solo = r.boolv();
    l.threeD = r.boolv();

    l.source = AssetId::unpack(r.u64v());
    l.nested.composition = CompositionId::unpack(r.u64v());
    l.nested.collapsed = r.boolv();

    l.transform.position = r.vec3();
    l.transform.scale = r.vec3();
    l.transform.rotation = r.vec3();
    l.transform.anchor = r.vec3();
    l.transform.opacity = r.f32v();
    l.transform.skewX = r.f32v();
    l.transform.skewY = r.f32v();
    l.transform.motionBlurAmount = r.f32v();
    // Recover malformed legacy/project values without passing NaN into
    // exposure timestamps (and subsequent floating-to-integer conversions).
    l.transform.motionBlurAmount = std::isfinite(l.transform.motionBlurAmount)
        ? std::clamp(l.transform.motionBlurAmount, 0.f, 4.f) : 0.f;
    l.transform.motionBlurEnabled = r.boolv();

    const u32 trackCount = r.u32v();
    if (trackCount > kMaxTrackCount) { r.skip_to_end(); return; }
    l.tracks.clear();
    for (u32 i = 0; i < trackCount && r.good(); ++i) {
        Track t;
        read_track(r, t);
        // Propriedade fora do enum (corrupção): a trilha não tem dono, cai fora.
        if (t.property == TrackProperty::_Count) continue;
        l.tracks.get_or_create(t.property, t.effectIndex, t.effectParamIndex) = t;
    }

    const u32 effectCount = r.u32v();
    if (effectCount > kMaxEffectCount) { r.skip_to_end(); return; }
    l.effects.clear();
    l.effects.reserve(effectCount);
    for (u32 i = 0; i < effectCount && r.good(); ++i) {
        EffectInstance e;
        read_effect(r, e);
        l.effects.push_back(e);
    }

    const u32 maskCount = r.u32v();
    if (maskCount > kMaxMaskCount) { r.skip_to_end(); return; }
    l.masks.clear();
    l.masks.reserve(maskCount);
    for (u32 i = 0; i < maskCount && r.good(); ++i) {
        Mask m;
        read_mask(r, m);
        l.masks.push_back(m);
    }

    l.text.content = r.str();
    l.text.font = FontId::unpack(r.u64v());
    l.text.size = r.f32v();
    l.text.color = r.vec4();
    l.text.strokeWidth = r.f32v();
    l.text.strokeColor = r.vec4();
    l.text.alignment = r.u32v();
    l.text.lineHeight = r.f32v();
    l.text.tracking = r.f32v();
    l.text.rtl = r.boolv();
    l.text.autoSize = r.boolv();
    l.text.box = r.rect();

    l.shape.shapeType = r.u32v();
    l.shape.bounds = r.rect();
    l.shape.cornerRadius = r.f32v();
    l.shape.points = r.f32v();
    l.shape.innerRadius = r.f32v();
    l.shape.fillColor = r.vec4();
    l.shape.strokeColor = r.vec4();
    l.shape.strokeWidth = r.f32v();
    l.shape.filled = r.boolv();
    l.shape.trimStart = r.f32v();
    l.shape.trimEnd = r.f32v();
    l.shape.trimOffset = r.f32v();
    l.shape.trimEnabled = r.boolv();
    {
        const u32 pathCount = r.u32v();
        l.shape.path.clear();
        if (pathCount <= 100000u) {
            l.shape.path.reserve(pathCount);
            for (u32 i = 0; i < pathCount && r.good(); ++i) l.shape.path.push_back(r.vec2());
        }
    }

    l.camera.fov = r.f32v();
    l.camera.focalLength = r.f32v();
    l.camera.nearPlane = r.f32v();
    l.camera.farPlane = r.f32v();
    l.camera.focusDistance = r.f32v();
    l.camera.aperture = r.f32v();
    l.camera.active = r.boolv();

    l.light.kind = checked_enum(r.u8v(), LightKind::Ambient, LightKind::Directional);
    l.light.color = r.vec4();
    l.light.intensity = r.f32v();
    l.light.range = r.f32v();
    l.light.coneAngle = r.f32v();
    l.light.penumbra = r.f32v();
    l.light.castShadows = r.boolv();
    l.light.shadowBias = r.f32v();

    l.model.scene = AssetId::unpack(r.u64v());
    l.model.animationClip = r.i32v();
    l.model.timeScale = r.f32v();
    l.model.castShadows = r.boolv();
    l.model.receiveShadows = r.boolv();
    l.model.forcedLod = r.i32v();
    if (g_readingTimelineVersion >= 2) {
        l.model.unitScale = r.f32v();
        l.model.pivot.x = r.f32v();
        l.model.pivot.y = r.f32v();
        l.model.pivot.z = r.f32v();
    }

    l.particles.emitterType = r.u32v();
    l.particles.rate = r.f32v();
    l.particles.lifetime = r.f32v();
    l.particles.gravity = r.vec3();
    l.particles.startSize = r.f32v();
    l.particles.endSize = r.f32v();
    l.particles.startOpacity = r.f32v();
    l.particles.endOpacity = r.f32v();
    l.particles.speed = r.f32v();
    l.particles.spread = r.f32v();
    l.particles.maxParticles = r.u32v();
    l.particles.blendMode = r.u32v();
    l.particles.collideEnvironment = r.boolv();
    // Projeto antigo: o emissor dos três presets de antes era sempre a caixa.
    migrate_legacy_particles(l.particles, g_readingTimelineVersion);

    l.gain = r.f32v();
    l.pan = r.f32v();
    l.muted = r.boolv();
    l.fadeIn = FrameIndex{r.i64v()};
    l.fadeOut = FrameIndex{r.i64v()};

    l.nextEffectId = r.u32v();
    l.nextMaskId = r.u32v();
    if (g_readingTimelineVersion >= 3) {
        l.speed = r.f32v();
        l.reversed = r.boolv();
    }
    if (g_readingTimelineVersion >= 6) l.motionBlur = r.boolv();
    if (g_readingTimelineVersion >= 7) {
        l.particles.startColor = r.vec4();
        l.particles.endColor = r.vec4();
        l.particles.direction = r.f32v();
        l.particles.seed = r.u32v();
        l.particles.emitterSize.x = r.f32v();
        l.particles.emitterSize.y = r.f32v();
        l.particles.emitterOffset.x = r.f32v();
        l.particles.emitterOffset.y = r.f32v();
    }
    if (g_readingTimelineVersion >= 8) {
        l.transitionIn = r.u8v();
        l.transitionOut = r.u8v();
        l.transitionInFrames = r.u32v();
        l.transitionOutFrames = r.u32v();
    }
    if (g_readingTimelineVersion >= 9) {
        l.echoCount = r.u32v();
        l.echoDelay = r.f32v();
        l.echoDecay = r.f32v();
        l.rgbDelay = r.f32v();
    }
    if (g_readingTimelineVersion >= 10) l.frameBlend = r.u8v();
    if (g_readingTimelineVersion >= 11) l.vectorBlur = r.f32v();
    if (g_readingTimelineVersion >= 12) {
        l.text.fontFamily = r.str();
        l.text.fontWeight = static_cast<u16>(r.u32v());
        l.text.fontItalic = r.boolv();
        l.text.fontPath = r.str();
    }
    if (g_readingTimelineVersion >= 13) {
        l.text.boxMode = r.u32v();
        const u32 n = std::min<u32>(r.u32v(), 4096);
        l.text.spans.resize(n);
        for (TextSpan& sp : l.text.spans) {
            sp.start = r.u32v();
            sp.end = r.u32v();
            sp.hasColor = r.boolv();
            sp.color = r.vec4();
            sp.weight = static_cast<u16>(r.u32v());
            sp.scale = r.f32v();
        }
        l.text.background = r.boolv();
        l.text.backgroundColor = r.vec4();
        l.text.backgroundPadding = r.f32v();
        l.text.backgroundRadius = r.f32v();
        l.text.shadow = r.boolv();
        l.text.shadowColor = r.vec4();
        l.text.shadowOffset.x = r.f32v();
        l.text.shadowOffset.y = r.f32v();
        l.text.shadowBlur = r.f32v();
    }
    if (g_readingTimelineVersion >= 14) {
        const u32 n = std::min<u32>(r.u32v(), 64);
        l.text.animators.resize(n);
        for (TextAnimator& a : l.text.animators) {
            a.name = r.str();
            a.enabled = r.boolv();
            TextSelector& s = a.selector;
            s.basedOn = static_cast<u8>(r.u32v()); s.type = static_cast<u8>(r.u32v()); s.shape = static_cast<u8>(r.u32v());
            s.randomOrder = r.boolv(); s.seed = r.u32v();
            s.start = r.f32v(); s.end = r.f32v(); s.offset = r.f32v(); s.amount = r.f32v(); s.easeHigh = r.f32v(); s.easeLow = r.f32v();
            s.wiggleRate = r.f32v();
            a.props = r.u32v();
            a.position.x = r.f32v(); a.position.y = r.f32v(); a.position.z = r.f32v();
            a.scale.x = r.f32v(); a.scale.y = r.f32v();
            a.rotation.x = r.f32v(); a.rotation.y = r.f32v(); a.rotation.z = r.f32v();
            a.opacity = r.f32v(); a.tracking = r.f32v(); a.blur = r.f32v(); a.skew = r.f32v(); a.strokeWidth = r.f32v(); a.charOffset = r.f32v();
            a.fill = r.vec4(); a.stroke = r.vec4();
        }
    }
    if (g_readingTimelineVersion >= 15) l.text.captionSource = r.u64v();
    if (g_readingTimelineVersion >= 16) {
        l.adjustment = r.boolv();
        l.guide = r.boolv();
        const u8 label = r.u8v();
        l.label = label < kLayerLabelCount ? label : 0;   // etiqueta de versão futura: nenhuma
    }
    if (g_readingTimelineVersion >= 17) {
        l.matteSource = LayerId::unpack(r.u64v());
        const u8 mm = r.u8v();
        l.matteMode = mm <= static_cast<u8>(MatteMode::LumaInverted) ? static_cast<MatteMode>(mm) : MatteMode::None;
        const u32 nm = r.u32v();
        for (u32 i = 0; i < nm && r.good(); ++i) {
            const u32 nk = r.u32v();
            if (nk > 100000u) { r.skip_to_end(); return; }
            std::vector<MaskPathKey> keys(nk);
            for (MaskPathKey& k : keys) {
                k.frame = r.i64v();
                k.interp = r.u8v();
                const u32 np = r.u32v();
                if (np > 100000u || !r.good()) { r.skip_to_end(); return; }
                k.points.resize(np);
                for (MaskPoint& pt : k.points) {
                    pt.position = r.vec2();
                    pt.inTangent = r.vec2();
                    pt.outTangent = r.vec2();
                }
            }
            if (i < l.masks.size()) l.masks[i].pathKeys = std::move(keys);
        }
    }
    if (g_readingTimelineVersion >= 18) {
        const u32 n = r.u32v();
        if (n > kMaxTrackCount + 1) { r.skip_to_end(); return; }
        for (u32 i = 0; i < n && r.good(); ++i) {
            const u8 where = r.u8v();
            const auto prop = checked_enum(r.u16v(), static_cast<TrackProperty>(static_cast<u16>(TrackProperty::_Count) - 1),
                                           TrackProperty::Opacity);
            const u32 effectIndex = r.u32v();
            const u32 paramIndex = r.u32v();
            const bool enabled = r.boolv();
            std::string source = r.str();
            if (!r.good() || source.size() > expr::kMaxSourceBytes) break;
            Track& t = where == 1 ? l.timeRemap : l.tracks.get_or_create(prop, effectIndex, paramIndex);
            t.expression = expr::compile(source);
            t.expressionEnabled = enabled;
        }
    }
    if (g_readingTimelineVersion >= 19) {
        const std::string names = r.str();
        const u32 n = r.u32v();
        if (n <= 64u * 1024u * 1024u && r.good()) {
            std::vector<f32> doc(n);
            for (u32 i = 0; i < n && r.good(); ++i) doc[i] = r.f32v();
            if (r.good() && n > 0 && !vector::decode_document(doc.data(), doc.size(), names, l.shape.vector)) l.shape.vector = VectorData{};
        }
        l.text.pathLayer = r.u64v();
        l.text.pathOffset = r.f32v();
        l.text.pathPerpendicular = r.boolv();
        l.text.pathReverse = r.boolv();
    }
    if (g_readingTimelineVersion >= 20) {
        l.particles.emitterType = r.u32v();
        l.particles.emitterRadius = r.f32v();
        l.particles.emitterRotation = r.f32v();
        l.particles.emitterDepth = r.f32v();
        l.particles.gridX = r.u32v();
        l.particles.gridY = r.u32v();
        l.particles.emitFill = r.boolv();
        l.particles.burst = r.u32v();
        l.particles.lifeRandom = r.f32v();
        l.particles.speedRandom = r.f32v();
        l.particles.inheritVelocity = r.f32v();
        l.particles.particleType = r.u32v();
        l.particles.softness = r.f32v();
        l.particles.rotation = r.f32v();
        l.particles.rotationRandom = r.f32v();
        l.particles.spin = r.f32v();
        l.particles.drag = r.f32v();
        l.particles.wind = r.vec3();
        l.particles.turbulence = r.f32v();
        l.particles.turbulenceScale = r.f32v();
        l.particles.turbulenceSpeed = r.f32v();
        l.particles.vortex = r.f32v();
        l.particles.attractor = r.f32v();
        l.particles.trailLength = r.f32v();
        l.particles.trailTaper = r.f32v();
        l.particles.auxCount = r.u32v();
        l.particles.auxAt = r.f32v();
        l.particles.auxLife = r.f32v();
        l.particles.auxSpeed = r.f32v();
        l.particles.auxSize = r.f32v();
        l.particles.auxSpread = r.f32v();
        l.particles.auxColor = r.vec4();
        l.particles.collision = r.u32v();
        l.particles.collisionY = r.f32v();
        l.particles.collisionBounce = r.f32v();
    }
    if (g_readingTimelineVersion >= 21) {
        ParticleData& pp = l.particles;
        pp.emitterSpace = r.u32v();
        pp.emitFrom = r.u32v();
        pp.emitterSource = r.u64v();
        pp.textureAsset = r.u64v();
        pp.meshSource = r.u64v();
        pp.auxProbability = r.f32v();
        pp.trailWidth = r.f32v();
        pp.trailOpacity = r.f32v();
        pp.sizeRandom = r.f32v();
        pp.opacityRandom = r.f32v();
        pp.colorRandom = r.f32v();
        pp.collisionCenter = r.vec3();
        pp.collisionRadius = r.f32v();
        pp.collisionBox = r.vec3();
        pp.meshScale = r.f32v();
        pp.meshLit = r.boolv();
        auto stops = [&]() { return std::min<u32>(r.u32v(), ParticleData::kMaxLifeStops); };
        pp.colorStopCount = stops();
        for (u32 i = 0; i < pp.colorStopCount && r.good(); ++i) pp.colorStops[i] = r.vec4();
        pp.sizeCurveCount = stops();
        for (u32 i = 0; i < pp.sizeCurveCount && r.good(); ++i) { pp.sizeCurve[i].x = r.f32v(); pp.sizeCurve[i].y = r.f32v(); }
        pp.opacityCurveCount = stops();
        for (u32 i = 0; i < pp.opacityCurveCount && r.good(); ++i) { pp.opacityCurve[i].x = r.f32v(); pp.opacityCurve[i].y = r.f32v(); }
    }
    if (g_readingTimelineVersion >= 22) {
        l.environmentSource = r.u32v();
        l.environmentAsset = r.u64v();
        l.environmentIntensity = r.f32v();
        l.environmentExposure = r.f32v();
        l.environmentRotation = r.f32v();
        l.environmentBackground = r.boolv();
    }
    if (g_readingTimelineVersion >= 23) {
        l.timeRemapLegacyMigrated = r.boolv();
        const u32 count = r.u32v();
        if (count > kMaxTrackCount) { r.skip_to_end(); return; }
        l.timeRemapLegacyTracks.resize(count);
        for (Track& track : l.timeRemapLegacyTracks) {
            read_track(r, track);
            track.expressionEnabled = r.boolv();
            const std::string source = r.str();
            if (source.size() > expr::kMaxSourceBytes) { r.skip_to_end(); return; }
            if (!source.empty()) track.expression = expr::compile(source);
        }
    }
    if (g_readingTimelineVersion >= 24) {
        const u32 count = r.u32v();
        if (count > 4096) { r.fail(); return; }
        l.model.materials.resize(count);
        std::vector<u32> indices;
        indices.reserve(count);
        auto validFactor = [](f32 value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; };
        for (MaterialOverride& material : l.model.materials) {
            material.materialIndex = r.u32v();
            material.mask = r.u32v();
            material.baseColor = r.vec4();
            material.metallic = r.f32v();
            material.roughness = r.f32v();
            if (material.materialIndex >= 4096 || (material.mask & ~63u) ||
                !validFactor(material.baseColor.x) || !validFactor(material.baseColor.y) ||
                !validFactor(material.baseColor.z) || !validFactor(material.baseColor.w) ||
                !validFactor(material.metallic) || !validFactor(material.roughness)) {
                r.fail(); return;
            }
            indices.push_back(material.materialIndex);
        }
        std::sort(indices.begin(), indices.end());
        if (std::adjacent_find(indices.begin(), indices.end()) != indices.end()) { r.fail(); return; }
    }
    if (g_readingTimelineVersion >= 25) {
        l.captionOptions.style = r.u32v(); l.captionOptions.highlight = r.boolv(); l.captionOptions.highlightColor = r.vec4();
        const u32 count = r.u32v();
        if (count > 100000 || l.captionOptions.style >= text::kCaptionStyleCount) { r.fail(); return; }
        l.captions.resize(count);
        for (auto& segment : l.captions) {
            segment.id = r.u64v(); segment.start = r.i64v(); segment.end = r.i64v(); segment.text = r.str();
            const u32 words = r.u32v(); if (words > 1024) { r.fail(); return; }
            segment.words.resize(words);
            for (auto& word : segment.words) { word.text = r.str(); word.start = r.i64v(); word.end = r.i64v(); }
        }
        if (!text::valid_caption_track(l.captions)) { r.fail(); return; }
    }
    if (g_readingTimelineVersion >= 27) {
        l.hasParentBasis = r.boolv();
        if (l.hasParentBasis) {
            for (Vec4& column : l.parentBasis.col) {
                column = r.vec4();
                if (!std::isfinite(column.x) || !std::isfinite(column.y) || !std::isfinite(column.z) || !std::isfinite(column.w)) { r.fail(); return; }
            }
            if (l.parentBasis.col[0].w != 0 || l.parentBasis.col[1].w != 0 || l.parentBasis.col[2].w != 0 || l.parentBasis.col[3].w != 1) { r.fail(); return; }
        }
    }
    if (g_readingTimelineVersion >= 28) {
        if (r.boolv()) l.cameraTrack = read_camera_track(r);
        l.cameraTrackSource = LayerId::unpack(r.u64v()); l.cameraTrackKey = r.u64v();
    }
    if (g_readingTimelineVersion >= 29) {
        if (r.boolv()) l.motionTrack = read_motion_track(r);
        l.camera.trackingSourceSize = r.vec2();
        const auto size = l.camera.trackingSourceSize;
        if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x<0 || size.y<0 || size.x>65536 || size.y>65536) r.fail();
        l.motionTrackEffect=r.u32v();
        if(l.cameraTrack){
            auto data=std::make_shared<tracking::CameraTrackData>(*l.cameraTrack);
            for(auto& c:data->sceneCalibration.col){c=r.vec4();if(!std::isfinite(c.x)||!std::isfinite(c.y)||!std::isfinite(c.z)||!std::isfinite(c.w))r.fail();}
            if(data->sceneCalibration.col[0].w!=0||data->sceneCalibration.col[1].w!=0||data->sceneCalibration.col[2].w!=0||data->sceneCalibration.col[3].w!=1)r.fail();
            l.cameraTrack=std::move(data);
        }
    }
    if (g_readingTimelineVersion >= 30) {
        if(l.cameraTrack){auto data=std::make_shared<tracking::CameraTrackData>(*l.cameraTrack);data->sourceSignature=r.u64v();l.cameraTrack=std::move(data);}
        if(l.motionTrack){auto data=std::make_shared<tracking::MotionTrackData>(*l.motionTrack);data->sourceSignature=r.u64v();l.motionTrack=std::move(data);}
    }
    if (g_readingTimelineVersion >= 33) {
        l.camera.dofEnabled = r.boolv();
        const f32 blur = r.f32v();
        l.camera.blurAmount = std::isfinite(blur) ? std::clamp(blur, 0.0f, 4.0f) : 1.0f;
    }
    if (g_readingTimelineVersion >= 34) {
        l.magneticTrack = r.boolv();
        l.trackId = r.u32v();
    } else {
        // Projeto gravado antes da linha magnética: a camada segue o modo
        // Edição da composição, que é o comportamento com que ela foi montada.
        l.magneticTrack = false;
        l.trackId = 0;   // sem linha: cada trecho antigo é a linha dele
    }
    if (g_readingTimelineVersion >= 36) {
        const u32 n = r.u32v();
        if (n > 1024 || r.remaining() < static_cast<u64>(n) * 80) { r.fail(); return; }
        l.layerAnimators.resize(n);
        auto fin = [&](f32 v, f32 fallback) { return std::isfinite(v) ? v : fallback; };
        for (LayerAnimator& a : l.layerAnimators) {
            a.name = r.str();
            a.enabled = r.boolv();
            a.unit = std::min<u8>(r.u8v(), 3);
            a.exit = r.boolv();
            const u8 ease = r.u8v();
            a.loop = (ease & 0x80u) != 0;
            a.ease = std::min<u8>(static_cast<u8>(ease & 0x7Fu), 3);
            a.scaleSeparated = r.boolv();
            a.wiggleSeed = r.u32v();
            for (f32* v : {&a.progress, &a.strength, &a.delayMs, &a.fromOpacity, &a.fromPosX, &a.fromPosY, &a.fromScale,
                           &a.fromScaleY, &a.fromRotation, &a.fromRotX, &a.fromRotY, &a.fromTracking, &a.wigglePosX,
                           &a.wigglePosY, &a.wiggleScale, &a.wiggleRotation, &a.wiggleSpeed, &a.wiggleHold})
                *v = fin(r.f32v(), *v);
        }
        l.adjustmentScope = std::min<u8>(r.u8v(), g_readingTimelineVersion >= 37 ? 2 : 1);
        l.nested.cameraPassThrough = r.boolv();
        l.adjustmentTargets.clear();
        if (g_readingTimelineVersion >= 37) {
            const u32 n = r.u32v();
            if (n > 256 || r.remaining() < static_cast<u64>(n) * 8) { r.fail(); return; }
            l.adjustmentTargets.reserve(n);
            for (u32 i = 0; i < n; ++i) l.adjustmentTargets.push_back(LayerId::unpack(r.u64v()));
        }
    } else {
        // Projeto anterior: nenhum animador de camada, o ajuste vale para tudo
        // abaixo e o grupo é fechado para a câmera — o mesmo quadro de antes.
        l.layerAnimators.clear();
        l.adjustmentScope = 0;
        l.nested.cameraPassThrough = false;
    }
    l.rig = RigData{};
    if (g_readingTimelineVersion >= 38) {
        // Projeto anterior: sem rig (a imagem desenha como sempre).
        l.rig.nextJointId = r.u32v();
        const u32 jointCount = r.u32v();
        if (jointCount > 64 || r.remaining() < static_cast<u64>(jointCount) * 16) { r.fail(); return; }
        l.rig.joints.resize(jointCount);
        for (RigJoint& j : l.rig.joints) {
            j.id = r.u32v();
            j.parent = r.u32v();
            j.pos = r.vec2();
            if (!std::isfinite(j.pos.x) || !std::isfinite(j.pos.y)) j.pos = Vec2{0.0f, 0.0f};
            l.rig.nextJointId = std::max(l.rig.nextJointId, j.id + 1);
        }
    }
    // Projeto anterior à v39: o tom acompanha a velocidade (reamostra).
    l.keepPitch = g_readingTimelineVersion >= 39 ? r.boolv() : false;
    l.light.shadowStrength = g_readingTimelineVersion >= 40 ? r.f32v() : 1.0f;
    if (!std::isfinite(l.light.shadowStrength)) l.light.shadowStrength = 1.0f;
    l.light.shadowStrength = std::clamp(l.light.shadowStrength, 0.0f, 1.0f);
    // Projeto anterior à v43: automático (a forma 3D pronta mostra o lado de
    // dentro; modelo importado e texto 3D abrem como antes).
    l.model.interior = g_readingTimelineVersion >= 43
        ? checked_enum(r.u8v(), ModelInterior::Off, ModelInterior::Auto) : ModelInterior::Auto;
    {
        // v44: parâmetros das formas paramétricas. Valor estragado (não
        // finito) volta ao padrão; negativo é válido ("o padrão da forma").
        const ShapeData defaults{};
        auto read_param = [&](f32& field, f32 fallback) {
            const f32 v = r.f32v();
            field = std::isfinite(v) ? v : fallback;
        };
        l.shape.depth = defaults.depth;
        l.shape.tip = defaults.tip;
        l.shape.thickness = defaults.thickness;
        l.shape.sweep = defaults.sweep;
        l.shape.head = defaults.head;
        l.shape.shaft = defaults.shaft;
        l.shape.amplitude = defaults.amplitude;
        l.shape.seed = defaults.seed;
        if (g_readingTimelineVersion >= 44) {
            read_param(l.shape.depth, defaults.depth);
            read_param(l.shape.tip, defaults.tip);
            read_param(l.shape.thickness, defaults.thickness);
            read_param(l.shape.sweep, defaults.sweep);
            read_param(l.shape.head, defaults.head);
            read_param(l.shape.shaft, defaults.shaft);
            read_param(l.shape.amplitude, defaults.amplitude);
            read_param(l.shape.seed, defaults.seed);
        }
    }
}

// Values in the old effect's time parameter are seconds; direct TimeRemap
// tracks already store composition frames. Never infer units from magnitudes,
// source FPS, file duration or VFR sample count.
void migrate_legacy_time_remap(Layer& layer, f64 fps) {
    if (layer.timeRemapLegacyMigrated || !std::isfinite(fps) || fps <= 0.0) return;
    auto units = [&](const Track& track) -> f64 {
        if (track.property == TrackProperty::TimeRemap) return 1.0;
        if (track.property != TrackProperty::EffectParam || track.effectParamIndex != 0) return 0.0;
        for (const EffectInstance& effect : layer.effects)
            if (effect.id == track.effectIndex && effect.type == effect_type_id(effect_keys::kTimeRemap)) return fps;
        return 0.0; // Dangling IDs/other components are ambiguous, so leave them alone.
    };
    auto valid = [](const Track& track, f64 scale) {
        if (track.keys.empty() || track.expression) return false;
        auto fits = [scale](f32 value) {
            const f64 converted = static_cast<f64>(value) * scale;
            return std::isfinite(converted) && std::abs(converted) <= std::numeric_limits<f32>::max();
        };
        if (!fits(track.staticValue)) return false;
        for (const Keyframe& key : track.keys)
            if (!fits(key.value) || !fits(key.tangentIn) || !fits(key.tangentOut)) return false;
        return true;
    };
    // Existing canonical animation wins as a whole: merging conflicting curves
    // would invent timing between keys even when their timestamps differ.
    bool hasLegacy = false;
    for (u32 i = 0; i < layer.tracks.size(); ++i) hasLegacy |= units(layer.tracks.at(i)) != 0.0;
    if (!hasLegacy) return;
    const bool canonical = valid(layer.timeRemap, 1.0) || layer.timeRemap.has_expression();
    if (!canonical) {
        const Track* chosen = nullptr;
        f64 scale = 0.0;
        bool ambiguous = false;
        for (u32 i = 0; i < layer.tracks.size(); ++i) {
            const Track& track = layer.tracks.at(i);
            const f64 candidate = units(track);
            if (candidate == 0.0 || !valid(track, candidate)) continue;
            if (!chosen || (track.property == TrackProperty::TimeRemap && chosen->property != TrackProperty::TimeRemap)) {
                chosen = &track;
                scale = candidate;
                ambiguous = false;
            } else if (track.property == chosen->property) ambiguous = true;
        }
        if (!chosen || ambiguous) return;
        // Preserve even an unusable previous canonical record for recovery.
        if (!layer.timeRemap.keys.empty() || layer.timeRemap.expression)
            layer.timeRemapLegacyTracks.push_back(layer.timeRemap);
        layer.timeRemap = *chosen;
        layer.timeRemap.property = TrackProperty::TimeRemap;
        layer.timeRemap.effectIndex = kInvalidIndex;
        layer.timeRemap.effectParamIndex = 0;
        layer.timeRemap.staticValue = static_cast<f32>(static_cast<f64>(layer.timeRemap.staticValue) * scale);
        for (Keyframe& key : layer.timeRemap.keys) {
            key.value = static_cast<f32>(static_cast<f64>(key.value) * scale);
            key.tangentIn = static_cast<f32>(static_cast<f64>(key.tangentIn) * scale);
            key.tangentOut = static_cast<f32>(static_cast<f64>(key.tangentOut) * scale);
        }
        layer.timeRemap.lastIndex = 0;
        if (chosen->property == TrackProperty::TimeRemap) layer.timeRemapEnabled = true;
        else {
            for (const EffectInstance& effect : layer.effects)
                if (effect.id == chosen->effectIndex) layer.timeRemapEnabled = effect.enabled;
        }
    }
    layer.tracks.remove_if([&](const Track& track) {
        if (units(track) == 0.0) return false;
        layer.timeRemapLegacyTracks.push_back(track);
        return true;
    });
    layer.timeRemapLegacyMigrated = true;
}

void write_asset(ByteWriter& w, const Asset& a) {
    w.u8v(static_cast<u8>(a.kind));
    w.str(a.name);
    w.str(a.sourcePath);
    w.str(a.proxyPath);
    w.u32v(a.proxyWidth);
    w.u32v(a.proxyHeight);
    w.str(a.thumbnailPath);
    w.str(a.waveformPath);
    w.u32v(a.waveformBuckets);

    w.u32v(a.profile.codecTag);
    w.u32v(a.profile.profile);
    w.u32v(a.profile.level);
    w.u8v(a.profile.bitDepth);
    w.u8v(a.profile.chromaSubsampling);
    w.boolv(a.profile.hdr);
    w.u16v(static_cast<u16>(a.profile.transfer));
    w.u16v(static_cast<u16>(a.profile.primaries));

    w.u32v(a.video.index);
    w.u32v(a.video.width);
    w.u32v(a.video.height);
    w.f64v(a.video.fps);
    w.i64v(a.video.frameCount.value);
    w.boolv(a.video.variableFrameRate);
    w.u32v(a.video.timescale);

    w.u32v(a.audio.index);
    w.u32v(a.audio.sampleRate);
    w.u32v(a.audio.channels);
    w.i64v(a.audio.sampleCount.value);

    w.i64v(a.duration.value);
    w.f64v(a.timebaseFps);
    w.u64v(a.fileSizeBytes);
    w.u64v(a.contentHash);
    w.str(a.originalFilename);

    w.u32v(a.model.meshCount);
    w.u32v(a.model.materialCount);
    w.u32v(a.model.animationCount);
    w.u32v(a.model.triangleCount);
    w.u32v(a.model.lodCount);
    w.boolv(a.model.hasSkeleton);
    w.boolv(a.model.hasMorphTargets);
    w.u32v(static_cast<u32>(a.model.animationNames.size()));
    for (const std::string& n : a.model.animationNames) w.str(n);
    w.u32v(static_cast<u32>(a.model.lodTriangleCounts.size()));
    for (u32 n : a.model.lodTriangleCounts) w.u32v(n);
}

void read_asset(ByteReader& r, Asset& a) {
    a.kind = checked_enum(r.u8v(), AssetKind::Shape, AssetKind::Unknown);
    a.name = r.str();
    a.sourcePath = r.str();
    a.proxyPath = r.str();
    a.proxyWidth = r.u32v();
    a.proxyHeight = r.u32v();
    a.thumbnailPath = r.str();
    a.waveformPath = r.str();
    a.waveformBuckets = r.u32v();

    a.profile.codecTag = r.u32v();
    a.profile.profile = r.u32v();
    a.profile.level = r.u32v();
    a.profile.bitDepth = r.u8v();
    a.profile.chromaSubsampling = r.u8v();
    a.profile.hdr = r.boolv();
    a.profile.transfer = checked_enum(r.u16v(), ColorSpace::HLG, ColorSpace::Unknown);
    a.profile.primaries = checked_enum(r.u16v(), ColorSpace::HLG, ColorSpace::Unknown);

    a.video.index = r.u32v();
    a.video.width = r.u32v();
    a.video.height = r.u32v();
    a.video.fps = r.f64v();
    a.video.frameCount = FrameIndex{r.i64v()};
    a.video.variableFrameRate = r.boolv();
    a.video.timescale = r.u32v();

    a.audio.index = r.u32v();
    a.audio.sampleRate = r.u32v();
    a.audio.channels = r.u32v();
    a.audio.sampleCount = FrameIndex{r.i64v()};

    a.duration = FrameIndex{r.i64v()};
    a.timebaseFps = r.f64v();
    a.fileSizeBytes = r.u64v();
    a.contentHash = r.u64v();
    a.originalFilename = r.str();

    a.model.meshCount = r.u32v();
    a.model.materialCount = r.u32v();
    a.model.animationCount = r.u32v();
    a.model.triangleCount = r.u32v();
    a.model.lodCount = r.u32v();
    a.model.hasSkeleton = r.boolv();
    a.model.hasMorphTargets = r.boolv();
    {
        const u32 n = r.u32v();
        a.model.animationNames.clear();
        if (n <= 4096u) {
            for (u32 i = 0; i < n && r.good(); ++i) a.model.animationNames.push_back(r.str());
        }
    }
    {
        const u32 n = r.u32v();
        a.model.lodTriangleCounts.clear();
        if (n <= 64u) {
            for (u32 i = 0; i < n && r.good(); ++i) a.model.lodTriangleCounts.push_back(r.u32v());
        }
    }
}

// -----------------------------------------------------------------------------
// Seções
// -----------------------------------------------------------------------------

std::vector<u8> build_project_section(const Project& p) {
    ByteWriter w;
    w.str(p.metadata().title);
    w.str(p.metadata().author);
    w.str(p.metadata().description);
    w.u64v(p.metadata().createdUnixMs);
    w.u64v(p.metadata().modifiedUnixMs);
    w.u32v(p.metadata().appVersionMajor);
    w.u32v(p.metadata().appVersionMinor);
    w.u32v(p.metadata().appVersionPatch);
    w.str(p.metadata().appVersionLabel);

    const ExportSettings& e = p.export_settings();
    w.u32v(e.width); w.u32v(e.height); w.f64v(e.fps);
    w.u16v(static_cast<u16>(e.videoCodec)); w.u32v(e.videoBitrateMbps);
    w.u32v(e.rateMode); w.u32v(e.keyframeIntervalFrames);
    w.u16v(static_cast<u16>(e.audioCodec)); w.u32v(e.audioBitrateKbps);
    w.u32v(e.audioSampleRate); w.u32v(e.audioChannels);
    w.u32v(e.container);
    w.u16v(static_cast<u16>(e.outputColorSpace));
    w.boolv(e.toneMapToSdr);
    w.u32v(e.parallelSegments);
    w.u32v(e.motionBlurSamples);
    w.u32v(e.opticalFlowQuality);
    w.f32v(e.scale);

    const EditorSettings& ed = p.editor_settings();
    w.u8v(static_cast<u8>(ed.previewScale));
    w.f32v(ed.timelineZoom);
    w.f32v(ed.timelineScroll);
    w.f32v(ed.viewportZoom);
    w.vec2(ed.viewportPan);
    w.boolv(ed.loop);
    w.boolv(ed.snapEnabled);
    w.boolv(ed.showSafeArea);
    w.boolv(ed.showGrid);
    w.u32v(static_cast<u32>(ed.selectedLayers.size()));
    for (u64 id : ed.selectedLayers) w.u64v(id);

    return std::vector<u8>(w.bytes().begin(), w.bytes().end());
}

std::vector<u8> build_timeline_section(const Project& p) {
    ByteWriter w;
    const Timeline& t = p.timeline();

    w.u64v(t.root().pack());
    w.u64v(t.current().pack());
    w.i64v(t.playhead().value);
    w.f32v(t.speed());
    w.boolv(t.loop());

    w.u32v(t.composition_count());
    t.for_each_composition([&w](CompositionId id, const Composition& c) {
        w.u64v(id.pack());
        w.str(c.name());
        w.u32v(c.width());
        w.u32v(c.height());
        w.f64v(c.fps());
        w.i64v(c.duration().value);
        w.color(c.background());
        w.boolv(c.transparent_background());
        w.u32v(c.nesting_depth());

        w.u64v(c.active_camera().pack());

        // Layers na ordem vertical — a ordem é dado, não derivada.
        w.u32v(c.order().size());
        for (u32 i = 0; i < c.order().size(); ++i) {
            const LayerId lid = c.order().at(i);
            w.u64v(lid.pack());
            write_layer(w, *c.layer(lid));
        }

        const ShadowSettings& sh = c.shadows();
        w.boolv(sh.enabled);
        w.u32v(sh.mapResolution);
        w.u32v(sh.cascadeCount);
        w.f32v(sh.cascadeSplitLambda);
        w.f32v(sh.bias);
        w.f32v(sh.normalBias);
        w.boolv(sh.softShadows);
        w.u32v(sh.pcfSamples);

        const EnvironmentSettings& env = c.environment();
        w.u64v(env.hdri.pack());
        w.f32v(env.intensity);
        w.f32v(env.rotation);
        w.color(env.ambientColor);
        w.boolv(env.showBackground);
        w.f32v(env.backgroundBlur);

        const PostProcessSettings& pp = c.post_process();
        w.boolv(pp.ssao); w.f32v(pp.ssaoRadius); w.f32v(pp.ssaoIntensity);
        w.boolv(pp.bloom); w.f32v(pp.bloomThreshold); w.f32v(pp.bloomIntensity);
        w.boolv(pp.dof); w.f32v(pp.dofAperture);
        w.boolv(pp.fog); w.vec4(pp.fogColor); w.f32v(pp.fogDensity);
        w.boolv(pp.vignette); w.f32v(pp.vignetteAmount);
        w.boolv(pp.colorGrade); w.u32v(pp.lutIndex);

        const MotionBlurSettings& mb = c.motion_blur();
        w.boolv(mb.enabled);
        w.u32v(mb.samples);
        w.u32v(mb.previewSamples);
        w.f32v(mb.shutterAngle);
        w.boolv(mb.vectorBlur);

        w.u64v(c.scene().pack());

        // v4: marcas.
        w.u32v(static_cast<u32>(c.markers().size()));
        for (const Marker& m : c.markers()) {
            w.i64v(m.frame.value);
            w.u32v(m.color);
            w.u32v(m.kind);
            w.str(m.label);
        }
        // v5: modo da timeline.
        w.boolv(c.edit_mode());
        // v31: qualidade do 3D, operador de tone map e exposição do grupo.
        w.u32v(pp.quality3d);
        w.u32v(pp.toneMapper);
        w.f32v(pp.exposure);
        // v32: estúdio procedural e chão do grupo 3D.
        w.u32v(c.environment().studioPreset);
        {
            const FloorSettings& fl = c.floor();
            w.u32v(fl.mode);
            w.color(fl.color);
            w.f32v(fl.roughness);
            w.f32v(fl.reflectivity);
            w.f32v(fl.contactShadow);
            w.f32v(fl.fade);
        }
        // v41: explicit shutter phase and adaptive sampling. Append only: old
        // files keep every preceding field at its original offset.
        w.f32v(mb.shutterPhase);
        w.u32v(mb.adaptiveLimit);
        // v42: independently timed panorama; legacy files stay unbounded.
        w.i64v(env.backgroundStart.value);
        w.i64v(env.backgroundEnd.value);
    });

    return std::vector<u8>(w.bytes().begin(), w.bytes().end());
}

std::vector<u8> build_assets_section(const Project& p) {
    ByteWriter w;
    u32 count = 0;
    p.for_each_asset([&](AssetId, const Asset&) { ++count; });
    w.u32v(count);
    p.for_each_asset([&w](AssetId id, const Asset& a) {
        w.u64v(id.pack());
        write_asset(w, a);
    });
    return std::vector<u8>(w.bytes().begin(), w.bytes().end());
}

void apply_project_section(const u8* data, usize size, Project& p) {
    ByteReader r(data, size);
    ProjectMetadata& m = p.metadata();
    m.title = r.str();
    m.author = r.str();
    m.description = r.str();
    m.createdUnixMs = r.u64v();
    m.modifiedUnixMs = r.u64v();
    m.appVersionMajor = r.u32v();
    m.appVersionMinor = r.u32v();
    m.appVersionPatch = r.u32v();
    m.appVersionLabel = r.str();

    ExportSettings& e = p.export_settings();
    e.width = r.u32v();
    e.height = r.u32v();
    e.fps = r.f64v();
    e.videoCodec = checked_enum(r.u16v(), ExportCodec::ProRes, ExportCodec::H264);
    e.videoBitrateMbps = r.u32v();
    e.rateMode = r.u32v();
    e.keyframeIntervalFrames = r.u32v();
    e.audioCodec = checked_enum(r.u16v(), AudioCodec::PCM, AudioCodec::AAC);
    e.audioBitrateKbps = r.u32v();
    e.audioSampleRate = r.u32v();
    e.audioChannels = r.u32v();
    e.container = r.u32v();
    e.outputColorSpace = checked_enum(r.u16v(), ColorSpace::HLG, ColorSpace::Rec709);
    e.toneMapToSdr = r.boolv();
    e.parallelSegments = r.u32v();
    e.motionBlurSamples = r.u32v();
    e.opticalFlowQuality = r.u32v();
    e.scale = r.f32v();

    EditorSettings& ed = p.editor_settings();
    ed.previewScale = checked_enum(r.u8v(), PreviewScale::Eighth, PreviewScale::Auto);
    ed.timelineZoom = r.f32v();
    ed.timelineScroll = r.f32v();
    ed.viewportZoom = r.f32v();
    ed.viewportPan = r.vec2();
    ed.loop = r.boolv();
    ed.snapEnabled = r.boolv();
    ed.showSafeArea = r.boolv();
    ed.showGrid = r.boolv();
    {
        const u32 n = r.u32v();
        ed.selectedLayers.clear();
        if (n <= 4096u) {
            for (u32 i = 0; i < n && r.good(); ++i) ed.selectedLayers.push_back(r.u64v());
        }
    }
}

bool apply_timeline_section(const u8* data, usize size, Project& p) {
    ByteReader r(data, size);
    Timeline& t = p.timeline();

    const u64 rootPack = r.u64v();
    const u64 currentPack = r.u64v();
    t.seek(FrameIndex{r.i64v()});
    t.set_speed(r.f32v());
    t.set_loop(r.boolv());

    const u32 compCount = r.u32v();
    if (compCount > 256u) return false;
    std::vector<std::pair<u64, CompositionId>> compMap;
    compMap.reserve(compCount);

    for (u32 ci = 0; ci < compCount && r.good(); ++ci) {
        const u64 idPack = r.u64v();
        const std::string name = r.str();
        const u32 w = r.u32v();
        const u32 h = r.u32v();
        const f64 fps = r.f64v();

        const CompositionId cid = t.create_composition(name, w, h, fps);
        Composition* c = t.composition(cid);
        if (!c) { r.skip_to_end(); break; }

        // O id original é reatribuído ao criado: os slots desta sessão nascem
        // na ordem do arquivo, sem os buracos de composições apagadas — o
        // pack gravado só vale como chave, e as referências (pré-composição,
        // raiz, atual) são remapeadas no fim.
        compMap.emplace_back(idPack, cid);

        c->set_duration(FrameIndex{r.i64v()});
        c->set_background(r.color());
        c->set_transparent_background(r.boolv());
        c->set_nesting_depth(r.u32v());
        c->set_active_camera(LayerId::unpack(r.u64v()));

        const u32 layerCount = r.u32v();
        if (layerCount > kMaxLayerCount) { r.skip_to_end(); break; }

        // Ids do arquivo → ids desta sessão. Os slots nascem na ordem VERTICAL,
        // não na de criação: sem remapear, pai e câmera ativa apontariam para
        // outra camada (ou nenhuma) ao reabrir.
        std::vector<std::pair<u64, LayerId>> idMap;
        idMap.reserve(layerCount);
        const u64 activeCamPack = c->active_camera().pack();
        for (u32 li = 0; li < layerCount && r.good(); ++li) {
            const u64 layerPack = r.u64v();
            Layer layer;
            read_layer(r, layer);
            if (!r.good()) return false;
            migrate_legacy_time_remap(layer, fps);
            // O nome vindo do arquivo é preservado (add_layer só usa o padrão
            // quando o nome está vazio).
            const LayerId lid = c->add_layer(layer.kind, layer.name);
            if (Layer* dst = c->layer(lid)) {
                const std::string keepName = dst->name;
                *dst = std::move(layer);
                dst->name = keepName;
            }
            idMap.emplace_back(layerPack, lid);
        }
        auto remap = [&idMap](LayerId old) {
            if (!old.valid()) return LayerId{};
            for (const auto& [pack, now] : idMap) if (pack == old.pack()) return now;
            return LayerId{};
        };
        // TODA referência a camada passa pelo mapa. Antes só pai e câmera
        // passavam: track matte, legenda (camada de origem) e texto no caminho
        // apontavam para OUTRA camada depois de duplicar/reordenar/apagar e
        // reabrir (teste Stability.LayerReferencesSurviveReopen…).
        auto remapPack = [&remap](u64 old) {
            const LayerId now = old ? remap(LayerId::unpack(old)) : LayerId{};
            return now.valid() ? now.pack() : u64{0};   // 0 = nenhuma (contrato desses campos)
        };
        for (const auto& [pack, now] : idMap) {
            (void)pack;
            if (Layer* dst = c->layer(now)) {
                dst->parent = remap(dst->parent);
                dst->matteSource = remap(dst->matteSource);
                dst->cameraTrackSource = remap(dst->cameraTrackSource);
                dst->text.captionSource = remapPack(dst->text.captionSource);
                dst->text.pathLayer = remapPack(dst->text.pathLayer);
                // Parâmetro de efeito que aponta para outra camada ("Camada de
                // áudio" da Forma de onda / do Espectro): o mesmo mapa.
                for (EffectInstance& fx : dst->effects) {
                    const ParameterRegistry* specs = expr::builtin_effects().params(fx.type);
                    if (!specs) continue;
                    for (u32 k = 0; k < specs->count() && k < fx.params.size(); ++k) {
                        if (specs->at(k).type != ParamType::LayerReference) continue;
                        ParamValue& v = fx.params[k].constant;
                        v.ref = remapPack(v.ref);
                        v.v[0] = v.ref ? static_cast<f32>(LayerId::unpack(v.ref).index) : -1.0f;
                    }
                }
            }
        }
        c->set_active_camera(remap(LayerId::unpack(activeCamPack)));

        if (ShadowSettings* sh = &c->shadows(); true) {
            sh->enabled = r.boolv();
            sh->mapResolution = r.u32v();
            sh->cascadeCount = r.u32v();
            sh->cascadeSplitLambda = r.f32v();
            sh->bias = r.f32v();
            sh->normalBias = r.f32v();
            sh->softShadows = r.boolv();
            sh->pcfSamples = r.u32v();
        }

        EnvironmentSettings& env = c->environment();
        env.hdri = AssetId::unpack(r.u64v());
        env.intensity = r.f32v();
        env.rotation = r.f32v();
        env.ambientColor = r.color();
        env.showBackground = r.boolv();
        env.backgroundBlur = r.f32v();

        PostProcessSettings& pp = c->post_process();
        pp.ssao = r.boolv(); pp.ssaoRadius = r.f32v(); pp.ssaoIntensity = r.f32v();
        pp.bloom = r.boolv(); pp.bloomThreshold = r.f32v(); pp.bloomIntensity = r.f32v();
        pp.dof = r.boolv(); pp.dofAperture = r.f32v();
        pp.fog = r.boolv(); pp.fogColor = r.vec4(); pp.fogDensity = r.f32v();
        pp.vignette = r.boolv(); pp.vignetteAmount = r.f32v();
        pp.colorGrade = r.boolv(); pp.lutIndex = r.u32v();

        MotionBlurSettings& mb = c->motion_blur();
        mb.enabled = r.boolv();
        mb.samples = r.u32v();
        mb.previewSamples = r.u32v();
        mb.shutterAngle = r.f32v();
        mb.vectorBlur = r.boolv();

        c->set_scene(Scene3DId::unpack(r.u64v()));
        if (g_readingTimelineVersion >= 4) {
            const u32 mc = r.u32v();
            if (mc > 100000u) { r.skip_to_end(); break; }
            for (u32 mi = 0; mi < mc && r.good(); ++mi) {
                Marker m;
                m.frame = FrameIndex{r.i64v()};
                m.color = r.u32v();
                m.kind = r.u32v();
                m.label = r.str();
                c->put_marker(std::move(m));
            }
        }
        if (g_readingTimelineVersion >= 5) c->set_edit_mode(r.boolv());
        if (g_readingTimelineVersion >= 31) {
            pp.quality3d = std::min<u32>(r.u32v(), 4u);
            pp.toneMapper = std::min<u32>(r.u32v(), 1u);
            const f32 ev = r.f32v();
            pp.exposure = std::isfinite(ev) ? std::clamp(ev, 0.01f, 64.0f) : 1.0f;
        }
        if (g_readingTimelineVersion >= 32) {
            auto fin = [](f32 v, f32 lo, f32 hi, f32 def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
            c->environment().studioPreset = std::min<u32>(r.u32v(), 3u);
            FloorSettings& fl = c->floor();
            fl.mode = std::min<u32>(r.u32v(), 2u);
            fl.color = r.color();
            fl.roughness = fin(r.f32v(), 0.0f, 1.0f, 0.35f);
            fl.reflectivity = fin(r.f32v(), 0.0f, 1.0f, 0.5f);
            fl.contactShadow = fin(r.f32v(), 0.0f, 1.0f, 0.8f);
            fl.fade = fin(r.f32v(), 1.0f, 100.0f, 6.0f);
        }
        mb.shutterAngle = std::isfinite(mb.shutterAngle) ? std::clamp(mb.shutterAngle, 0.0f, 720.0f) : 180.0f;
        mb.samples = std::clamp(mb.samples, 2u, 64u);
        mb.previewSamples = std::clamp(mb.previewSamples, 1u, 64u);
        mb.shutterPhase = -0.5f * mb.shutterAngle;
        mb.adaptiveLimit = std::max(128u, mb.samples);
        if (g_readingTimelineVersion >= 41) {
            const f32 phase = r.f32v();
            mb.shutterPhase = std::isfinite(phase) ? std::clamp(phase, -360.0f, 360.0f) : mb.shutterPhase;
            mb.adaptiveLimit = std::clamp(r.u32v(), mb.samples, 256u);
        }
        if (g_readingTimelineVersion >= 42) {
            env.backgroundStart = FrameIndex{std::max<i64>(0, r.i64v())};
            env.backgroundEnd = FrameIndex{r.i64v()};
            if (env.backgroundEnd.value < 0) env.backgroundEnd = FrameIndex{-1};
            else env.backgroundEnd = FrameIndex{std::max(env.backgroundStart.value, env.backgroundEnd.value)};
        }
        c->rebuild_draw_order();
    }

    // Pré-composições apontam para a composição pelo id do ARQUIVO: com uma
    // composição apagada no meio (buraco de slot) ou reaproveitada (geração
    // nova), o id desta sessão é outro e a pré-composição abriria vazia.
    auto remapComp = [&compMap](CompositionId old) {
        if (!old.valid()) return CompositionId{};
        for (const auto& [pack, now] : compMap) if (pack == old.pack()) return now;
        return CompositionId{};
    };
    for (const auto& [pack, cid] : compMap) {
        (void)pack;
        if (Composition* c = t.composition(cid)) {
            c->layers().for_each([&](LayerId, Layer& l) {
                if (l.nested.composition.valid()) l.nested.composition = remapComp(l.nested.composition);
            });
        }
    }
    if (const CompositionId root = remapComp(CompositionId::unpack(rootPack)); root.valid()) t.set_root(root);
    else if (rootPack != 0) t.set_root(t.current());
    if (const CompositionId cur = remapComp(CompositionId::unpack(currentPack)); cur.valid()) (void)t.set_current(cur);
    return r.good();
}

void apply_assets_section(const u8* data, usize size, Project& p) {
    ByteReader r(data, size);
    const u32 count = r.u32v();
    if (count > 65536u) { r.skip_to_end(); return; }
    for (u32 i = 0; i < count && r.good(); ++i) {
        r.u64v();   // id antigo, remapeado
        Asset a;
        read_asset(r, a);
        (void)p.add_asset(std::move(a));
    }
}

/// Seção Scene3D (v1): o "Otimizar modelo" de cada asset, na MESMA ordem da
/// seção Assets (os ids são remapeados na leitura; a ordem não). Seção à parte
/// de propósito: a Assets (v1) tem layout fixo e um leitor antigo pula seção
/// desconhecida sem perder nada; o novo, sem esta seção, abre tudo Original.
///   u32 quantos · por asset: u8 qualidade · u32 triângulos do arquivo
std::vector<u8> build_scene3d_section(const Project& p) {
    ByteWriter w;
    u32 count = 0;
    p.for_each_asset([&](AssetId, const Asset&) { ++count; });
    w.u32v(count);
    p.for_each_asset([&w](AssetId, const Asset& a) {
        w.u8v(a.model.importQuality);
        w.u32v(a.model.sourceTriangles);
    });
    return std::vector<u8>(w.bytes().begin(), w.bytes().end());
}

bool project_has_optimized_models(const Project& p) {
    bool any = false;
    p.for_each_asset([&](AssetId, const Asset& a) { any = any || a.model.importQuality != 0 || a.model.sourceTriangles != 0; });
    return any;
}

void apply_scene3d_section(const u8* data, usize size, Project& p) {
    ByteReader r(data, size);
    const u32 count = r.u32v();
    u32 assets = 0;
    p.for_each_asset([&](AssetId, const Asset&) { ++assets; });
    if (!r.good() || count != assets) return;   // outra lista de assets: não adivinha
    p.for_each_asset([&](AssetId, Asset& a) {
        if (!r.good()) return;
        const u8 q = r.u8v();
        const u32 tris = r.u32v();
        if (!r.good() || a.kind != AssetKind::Model3D) return;
        a.model.importQuality = q <= 2 ? q : 0;
        a.model.sourceTriangles = tris;
    });
}

// -----------------------------------------------------------------------------
// Migrações registradas. A E/S de arquivo mora em FileIO (escrita atômica com
// fsync checado, .bak e injeção de falha para os testes de disco cheio).
// -----------------------------------------------------------------------------

struct MigrationEntry {
    SectionKind kind;
    u32 fromVersion;
    SectionMigrationFn fn;
};
MigrationEntry g_migrations[32]{};
u32 g_migrationCount = 0;

// -----------------------------------------------------------------------------
// Índice de seções no arquivo.
//
// Layout:
//   [FileHeader]
//   [SectionHeader x sectionCount]
//   [bytes da secao 0]
//   [bytes da secao 1]
//   ...
//
// Os cabeçalhos ficam TODOS no início, contíguos. Assim `peek` lê só o começo
// do arquivo para montar o índice completo — a Home lista 200 projetos sem ler
// 200 vezes dezenas de MB.
// -----------------------------------------------------------------------------
constexpr usize kFileHeaderSize = 64;
constexpr usize kSectionHeaderSize = 40;

void encode_file_header(u8* buf, const FileHeader& h) noexcept {
    std::memset(buf, 0, kFileHeaderSize);
    ByteWriter w;
    w.u32v(h.magic);
    w.u16v(h.formatVersion);
    w.u16v(h.minReaderVersion);
    w.u32v(h.sectionCount);
    w.u64v(h.indexOffset);
    w.u64v(h.totalSize);
    w.raw(h.appVersion, 32);
    std::memcpy(buf, w.bytes().data(), w.size() < kFileHeaderSize ? w.size() : kFileHeaderSize);
}

void decode_file_header(const u8* buf, FileHeader& h) noexcept {
    ByteReader r(buf, kFileHeaderSize);
    h.magic = r.u32v();
    h.formatVersion = r.u16v();
    h.minReaderVersion = r.u16v();
    h.sectionCount = r.u32v();
    h.indexOffset = r.u64v();
    h.totalSize = r.u64v();
    r.raw(h.appVersion, 32);
}

void encode_section_header(u8* buf, const SectionHeader& s) noexcept {
    std::memset(buf, 0, kSectionHeaderSize);
    ByteWriter w;
    w.u16v(static_cast<u16>(s.kind));
    w.u32v(s.version);
    w.u64v(s.offset);
    w.u64v(s.size);
    w.u64v(s.rawSize);
    w.u32v(s.crc32);
    w.u32v(s.flags);
    std::memcpy(buf, w.bytes().data(), w.size() < kSectionHeaderSize ? w.size() : kSectionHeaderSize);
}

void decode_section_header(const u8* buf, SectionHeader& s) noexcept {
    ByteReader r(buf, kSectionHeaderSize);
    s.kind = static_cast<SectionKind>(r.u16v());
    s.version = r.u32v();
    s.offset = r.u64v();
    s.size = r.u64v();
    s.rawSize = r.u64v();
    s.crc32 = r.u32v();
    s.flags = r.u32v();
}

} // namespace

// -----------------------------------------------------------------------------
// API pública
// -----------------------------------------------------------------------------
Status ProjectSerializer::encode(const Project& project, const SaveOptions& options,
                                 std::vector<u8>& file) {
    (void)options;
    struct PendingSection {
        SectionKind kind;
        u32 version;
        std::vector<u8> data;
    };

    std::vector<PendingSection> sections;
    sections.push_back(PendingSection{SectionKind::Project, 1, build_project_section(project)});
    sections.push_back(PendingSection{SectionKind::Timeline, kTimelineSectionVersion, build_timeline_section(project)});
    sections.push_back(PendingSection{SectionKind::Assets, 1, build_assets_section(project)});
    // Só quando algum modelo foi otimizado: projeto sem 3D fica byte a byte igual.
    if (project_has_optimized_models(project))
        sections.push_back(PendingSection{SectionKind::Scene3D, 1, build_scene3d_section(project)});

    // As seções Animations, Effects, Scene3D, Particles, Fonts e Thumbnails
    // existem no enum e no índice, mas ainda não são gravadas separadamente:
    // keyframes vão dentro da seção Timeline junto com as layers. Declarar as
    // seções vazias seria pior do que não as declarar — um leitor futuro as
    // encontraria vazias e concluiria que o projeto não tem animação.
    //
    // A gravação incremental por seção (`SaveOptions::incremental`) depende
    // disso estar separado e NÃO está implementada. Está registrado aqui e em
    // Serialization.hpp.

    const u64 indexOffset = kFileHeaderSize;
    const u64 dataOffset = indexOffset + kSectionHeaderSize * sections.size();

    usize total = static_cast<usize>(dataOffset);
    for (const PendingSection& s : sections) total += s.data.size();
    file.clear();
    file.reserve(total);
    file.resize(static_cast<usize>(dataOffset));

    std::vector<SectionHeader> headers;
    headers.reserve(sections.size());

    u64 cursor = dataOffset;
    for (const PendingSection& s : sections) {
        SectionHeader h;
        h.kind = s.kind;
        h.version = s.version;
        h.offset = cursor;
        h.size = s.data.size();
        h.rawSize = s.data.size();
        h.crc32 = crc32(s.data.data(), s.data.size());
        // Bit 0 = comprimido. Fica DESLIGADO porque a compressão (deflate) não
        // está implementada. Ligar sem comprimir produziria um arquivo que
        // mente sobre o próprio conteúdo.
        h.flags = 0;
        headers.push_back(h);

        file.insert(file.end(), s.data.begin(), s.data.end());
        cursor += s.data.size();
    }

    for (usize i = 0; i < headers.size(); ++i) {
        encode_section_header(file.data() + indexOffset + kSectionHeaderSize * i, headers[i]);
    }

    FileHeader fh;
    fh.sectionCount = static_cast<u32>(sections.size());
    fh.indexOffset = indexOffset;
    fh.totalSize = cursor;
    std::snprintf(fh.appVersion, sizeof(fh.appVersion), "%d.%d.%d",
                  AUREA_VERSION_MAJOR, AUREA_VERSION_MINOR, AUREA_VERSION_PATCH);
    encode_file_header(file.data(), fh);
    return OkStatus;
}

Status ProjectSerializer::write_encoded(const std::vector<u8>& file, const std::string& path,
                                        const SaveOptions& options, std::string* outError) {
    // Escrita atômica (FileIO): temporário → fflush/fsync/fclose checados →
    // .bak da versão anterior → rename. Uma queda ou um disco cheio no meio
    // deixa o arquivo antigo intacto — nunca um .aurea pela metade, que seria
    // pior do que não ter salvado.
    fileio::AtomicWriteOptions wo;
    wo.fsync = options.fsyncOnComplete;
    wo.keepBackup = options.keepBackup;
    const Status s = fileio::write_atomic(path, file.data(), file.size(), wo, outError);
    if (!s.ok()) return s;
    AUREA_LOG_INFO("projeto gravado (%llu bytes)", static_cast<unsigned long long>(file.size()));
    return OkStatus;
}

Status ProjectSerializer::save(const Project& project, const std::string& path,
                               const SaveOptions& options,
                               std::string* outError) {
    std::vector<u8> file;
    if (const Status s = encode(project, options, file); !s.ok()) return s;
    return write_encoded(file, path, options, outError);
}

Status ProjectSerializer::load(Project& out, const std::string& path,
                               const LoadOptions& options,
                               LoadReport* outReport,
                               std::string* outError) {
    std::vector<u8> file;
    // Teto de 2 GB: um .aurea maior que isso é corrupção ou um arquivo que não
    // é do Aurea, e tentar alocar seria o caminho para o OOM killer.
    if (!fileio::read_all(path, file, 2ull * 1024 * 1024 * 1024)) {
        const bool present = fileio::exists(path);
        if (outError) *outError = present ? "nao foi possivel ler o arquivo" : "arquivo nao encontrado";
        return present ? Status{Errc::IoError, "nao foi possivel ler o arquivo"}
                       : Status{Errc::NotFound, "arquivo nao encontrado"};
    }
    return load_bytes(out, file.data(), file.size(), options, outReport, outError);
}

Status ProjectSerializer::load_bytes(Project& out, const u8* fileData, usize fileSize,
                                     const LoadOptions& options,
                                     LoadReport* outReport,
                                     std::string* outError) {
    auto fail = [&](Errc code, const char* msg) -> Status {
        if (outError) *outError = msg;
        return Status{code, msg};
    };

    if (!fileData || fileSize < kFileHeaderSize) {
        return fail(Errc::CorruptData, "arquivo menor que o cabecalho");
    }

    FileHeader fh;
    decode_file_header(fileData, fh);
    if (fh.magic != FileHeader::kMagic) {
        return fail(Errc::UnsupportedFormat, "nao e um arquivo .aurea");
    }
    if (fh.minReaderVersion > FileHeader::kCurrentFormatVersion) {
        // O arquivo exige um leitor mais novo. Recusar com mensagem clara é
        // melhor do que abrir e perder silenciosamente o que esta versão não
        // entende.
        return fail(Errc::UnsupportedVersion,
                    "projeto gravado por uma versao mais nova do Aurea");
    }
    if (fh.sectionCount > 64u) {
        return fail(Errc::CorruptData, "indice de secoes invalido");
    }

    LoadReport report;

    for (u32 i = 0; i < fh.sectionCount; ++i) {
        // Aritmética sem estouro: `indexOffset`, `offset` e `size` vêm do
        // arquivo, e um u64 perto do máximo somado a qualquer coisa dá a volta
        // e passaria no teste de limite — leitura fora do buffer.
        const u64 hdrRel = static_cast<u64>(kSectionHeaderSize) * i;
        if (fh.indexOffset > fileSize || hdrRel > fileSize - fh.indexOffset
            || kSectionHeaderSize > fileSize - fh.indexOffset - hdrRel) {
            if (!options.tolerateCorruptSections) {
                return fail(Errc::CorruptData, "indice de secoes truncado");
            }
            report.partial = true;
            break;
        }
        const u64 hdrOffset = fh.indexOffset + hdrRel;

        SectionHeader sh;
        decode_section_header(fileData + hdrOffset, sh);

        if (sh.offset > fileSize || sh.size > fileSize - sh.offset) {
            report.sectionsCorrupt.push_back(sh.kind);
            report.partial = true;
            if (!options.tolerateCorruptSections) {
                return fail(Errc::CorruptData, "secao aponta para fora do arquivo");
            }
            continue;
        }

        const u8* data = fileData + sh.offset;
        const usize dataSize = static_cast<usize>(sh.size);

        // Checksum antes de interpretar. Interpretar lixo como layers pode
        // produzir handles inválidos e, a partir daí, acesso a memória errada.
        const u32 actual = crc32(data, dataSize);
        if (actual != sh.crc32) {
            report.sectionsCorrupt.push_back(sh.kind);
            report.partial = true;
            if (!options.tolerateCorruptSections) {
                return fail(Errc::ChecksumMismatch, "secao corrompida");
            }
            continue;
        }

        // Migração quando a versão da seção é antiga.
        std::vector<u8> migrated;
        const u8* effective = data;
        usize effectiveSize = dataSize;

        // Versões lidas nativamente (sem migração): 1 de toda seção; 1 a
        // kTimelineSectionVersion da Timeline.
        const bool native = sh.version == 1 || (sh.kind == SectionKind::Timeline && sh.version >= 1
                                                && sh.version <= kTimelineSectionVersion);
        if (!native) {
            bool handled = false;
            for (u32 m = 0; m < g_migrationCount; ++m) {
                if (g_migrations[m].kind == sh.kind && g_migrations[m].fromVersion == sh.version) {
                    const Status s = g_migrations[m].fn(sh.kind, sh.version, data, dataSize, migrated);
                    if (!s.ok()) {
                        return fail(s.code(), "falha ao migrar secao");
                    }
                    effective = migrated.data();
                    effectiveSize = migrated.size();
                    report.sectionsMigrated.push_back(sh.kind);
                    handled = true;
                    break;
                }
            }
            if (!handled) {
                const bool known = sh.kind == SectionKind::Project || sh.kind == SectionKind::Timeline
                                || sh.kind == SectionKind::Assets;
                const u32 current = sh.kind == SectionKind::Timeline ? kTimelineSectionVersion : 1u;
                if (known && sh.version > current && !options.metadataOnly) {
                    // Seção que ESTA versão sabe ler, gravada numa versão mais
                    // nova do formato: abrir sem ela e o usuário salvar por cima
                    // apagaria a timeline. Recusa com a causa (§124).
                    return fail(Errc::UnsupportedVersion,
                                "projeto gravado por uma versao mais nova do Aurea");
                }
                // Sem caminho de migração: pular a seção e reportar. Abrir o
                // resto é melhor que recusar o arquivo inteiro, e o usuário é
                // avisado de que algo não veio.
                report.sectionsSkipped.push_back(sh.kind);
                report.partial = true;
                continue;
            }
        }

        if (options.metadataOnly && sh.kind != SectionKind::Project
            && sh.kind != SectionKind::Thumbnails) {
            report.sectionsSkipped.push_back(sh.kind);
            continue;
        }

        switch (sh.kind) {
            case SectionKind::Project:
                apply_project_section(effective, effectiveSize, out);
                break;
            case SectionKind::Timeline: {
                report.timelineVersion = sh.version;
                if (sh.version < kTimelineSectionVersion) report.olderFormat = true;
                g_readingTimelineVersion = sh.version;
                const bool valid = apply_timeline_section(effective, effectiveSize, out);
                g_readingTimelineVersion = kTimelineSectionVersion;
                if (!valid) {
                    report.sectionsCorrupt.push_back(sh.kind);
                    report.partial = true;
                    if (outReport) *outReport = report;
                    return fail(Errc::CorruptData, "invalid timeline data");
                }
                break;
            }
            case SectionKind::Assets:
                apply_assets_section(effective, effectiveSize, out);
                break;
            case SectionKind::Scene3D:
                apply_scene3d_section(effective, effectiveSize, out);
                break;
            default:
                // Seções que esta versão não grava ainda. Se aparecerem, foram
                // gravadas por outra versão — pular e registrar é honesto.
                report.sectionsSkipped.push_back(sh.kind);
                break;
        }
        report.sectionsRead.push_back(sh.kind);
    }

    if (report.sectionsRead.empty()) {
        return fail(Errc::CorruptData, "nenhuma secao legivel no arquivo");
    }
    if (report.partial) {
        report.warning = "o projeto abriu com partes faltando";
    }
    if (!report.sectionsMigrated.empty()) report.olderFormat = true;

    out.mark_clean();
    if (outReport) *outReport = report;
    return OkStatus;
}

Status ProjectSerializer::peek(const std::string& path, FileHeader& outHeader,
                               std::vector<SectionHeader>& outSections,
                               std::string* outError) {
    std::vector<u8> head;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (outError) *outError = "arquivo nao encontrado";
        return Errc::NotFound;
    }

    // Lê só o suficiente para o cabeçalho e o índice: 64 bytes + 64 * 40 bytes.
    head.resize(kFileHeaderSize + kSectionHeaderSize * 64u);
    const usize read = std::fread(head.data(), 1, head.size(), f);
    std::fclose(f);

    // O magic é checado ANTES do tamanho quando há bytes suficientes para ele.
    // A diferença de diagnóstico importa: "não é um arquivo .aurea" e "arquivo
    // truncado" levam a ações diferentes de quem recebeu o erro — um é escolher
    // outro arquivo, o outro é baixar de novo.
    if (read >= 4) {
        u32 magic = 0;
        std::memcpy(&magic, head.data(), 4);
        if (magic != FileHeader::kMagic) {
            if (outError) *outError = "nao e um arquivo .aurea";
            return Errc::UnsupportedFormat;
        }
    }
    if (read < kFileHeaderSize) {
        if (outError) *outError = "arquivo truncado";
        return Errc::CorruptData;
    }

    decode_file_header(head.data(), outHeader);
    if (outHeader.magic != FileHeader::kMagic) {
        if (outError) *outError = "nao e um arquivo .aurea";
        return Errc::UnsupportedFormat;
    }

    outSections.clear();
    const u32 count = outHeader.sectionCount < 64u ? outHeader.sectionCount : 64u;
    for (u32 i = 0; i < count; ++i) {
        // Sem estouro: `indexOffset` vem do arquivo (ver load_bytes).
        const u64 rel = static_cast<u64>(kSectionHeaderSize) * i;
        if (outHeader.indexOffset > read || rel > read - outHeader.indexOffset
            || kSectionHeaderSize > read - outHeader.indexOffset - rel) break;
        const u64 off = outHeader.indexOffset + rel;
        SectionHeader sh;
        decode_section_header(head.data() + off, sh);
        outSections.push_back(sh);
    }
    return OkStatus;
}

// -----------------------------------------------------------------------------
// Journal de comandos (autosave incremental)
//
// Formato: um cabeçalho com a contagem, seguido pelos comandos crus e pelo
// blob de strings. É append-only: cada gravação acrescenta um bloco novo, e a
// leitura concatena todos. Uma queda no meio deixa um bloco truncado, e o
// leitor para ali — os blocos anteriores continuam válidos.
// -----------------------------------------------------------------------------
namespace {

struct JournalBlockHeader {
    static constexpr u32 kMagic = 0x4A524E4C;   // 'JRNL'
    u32 magic = kMagic;
    u32 version = 1;
    u32 commandCount = 0;
    u32 stringBlobSize = 0;
    u64 commandsCrc = 0;
};

} // namespace

Status ProjectSerializer::append_journal(const std::string& journalPath,
                                         const Command* commands, u32 count,
                                         const char* stringBlob, u32 stringBlobSize) {
    if (journalPath.empty()) return Errc::InvalidArgument;
    if (!commands || count == 0) return OkStatus;

    std::FILE* f = std::fopen(journalPath.c_str(), "ab");
    if (!f) return Errc::IoError;

    JournalBlockHeader h;
    h.commandCount = count;
    h.stringBlobSize = stringBlobSize;
    h.commandsCrc = content_hash(commands, sizeof(Command) * count);

    // Cada escrita é checada: disco cheio no meio deixa um bloco truncado que
    // o leitor descarta, mas quem chamou precisa saber que o autosave falhou.
    bool ok = std::fwrite(&h, sizeof(h), 1, f) == 1;
    ok = ok && std::fwrite(commands, sizeof(Command), count, f) == count;
    if (ok && stringBlob && stringBlobSize) ok = std::fwrite(stringBlob, 1, stringBlobSize, f) == stringBlobSize;
    ok = ok && std::fflush(f) == 0;
    const int err = ok ? 0 : errno;
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) return err == ENOSPC ? Status{Errc::StorageFull, "armazenamento cheio"} : Status{Errc::IoError};
    return OkStatus;
}

Status ProjectSerializer::read_journal(const std::string& journalPath,
                                       std::vector<Command>& outCommands) {
    outCommands.clear();
    if (journalPath.empty()) return Errc::NotFound;

    std::vector<u8> bytes;
    if (!fileio::read_all(journalPath, bytes, 512ull * 1024 * 1024)) return Errc::NotFound;

    // Lido da memória: as contagens do cabeçalho são checadas contra o que
    // SOBROU do arquivo antes de qualquer alocação — um bloco corrompido
    // declarando 4 GB de strings não pode virar um `vector` de 4 GB.
    usize pos = 0;
    while (bytes.size() - pos >= sizeof(JournalBlockHeader)) {
        JournalBlockHeader h;
        std::memcpy(&h, bytes.data() + pos, sizeof(h));
        pos += sizeof(h);
        if (h.magic != JournalBlockHeader::kMagic) break;
        const usize remaining = bytes.size() - pos;
        const u64 cmdBytes = static_cast<u64>(h.commandCount) * sizeof(Command);
        if (cmdBytes > remaining || h.stringBlobSize > remaining - cmdBytes) break;   // bloco truncado

        std::vector<Command> block(h.commandCount);
        if (cmdBytes) std::memcpy(block.data(), bytes.data() + pos, static_cast<usize>(cmdBytes));
        pos += static_cast<usize>(cmdBytes) + h.stringBlobSize;

        // Checksum por bloco: um bloco corrompido é descartado sozinho, sem
        // invalidar os anteriores. É o que faz a recuperação funcionar mesmo
        // quando a queda aconteceu exatamente no meio da última gravação.
        if (content_hash(block.data(), sizeof(Command) * block.size()) != h.commandsCrc) {
            break;
        }

        outCommands.insert(outCommands.end(), block.begin(), block.end());
    }

    return outCommands.empty() ? Status{Errc::NotFound, "journal vazio"} : Status{OkStatus};
}

void ProjectSerializer::register_migration(SectionKind kind, u32 fromVersion,
                                           SectionMigrationFn fn) noexcept {
    if (!fn || g_migrationCount >= 32) return;
    g_migrations[g_migrationCount++] = MigrationEntry{kind, fromVersion, fn};
}

} // namespace aurea
