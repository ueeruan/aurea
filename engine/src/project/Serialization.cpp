#include "aurea/project/Serialization.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/vector/Vector.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/expr/Expression.hpp"

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

private:
    [[nodiscard]] bool need(usize n) noexcept {
        if (failed_) return false;
        if (pos_ + n > size_) { failed_ = true; return false; }
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
        k.interp = checked_enum(r.u8v(), Interpolation::CustomCurve, Interpolation::Linear);
        k.bx1 = r.f32v(); k.by1 = r.f32v(); k.bx2 = r.f32v(); k.by2 = r.f32v();
        k.tangentIn = r.f32v(); k.tangentOut = r.f32v();
        k.easingPreset = r.u16v();
        t.keys.push_back(k);
    }
    // Reordena por segurança: um arquivo corrompido com keyframes fora de ordem
    // faria a busca binária devolver lixo. O custo de ordenar na leitura é
    // irrelevante comparado a abrir com animação errada.
    for (usize i = 1; i < t.keys.size(); ++i) {
        Keyframe key = t.keys[i];
        usize j = i;
        while (j > 0 && t.keys[j - 1].time.value > key.time.value) {
            t.keys[j] = t.keys[j - 1];
            --j;
        }
        t.keys[j] = key;
    }
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
}

/// Versão da seção Timeline. v2: layer de modelo 3D guarda escala de unidade
/// e pivô (o enquadramento do import). v1 continua sendo lida (campos novos
/// com o padrão).
/// v3: velocidade e reverso da layer.
/// v16: camada de ajuste, guia (não exporta) e etiqueta de cor.
/// v17: track matte (camada + modo) e keyframes do caminho das máscaras.
/// v18: expressões por trilha (fonte + ligada), no fim de cada layer.
/// v19: camada vetorial (VectorData) e texto no caminho, no fim da camada.
constexpr u32 kTimelineSectionVersion = 19;
thread_local u32 g_readingTimelineVersion = kTimelineSectionVersion;

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
    l.blendMode = checked_enum(r.u16v(), BlendMode::Luminosity, BlendMode::Normal);
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

void apply_timeline_section(const u8* data, usize size, Project& p) {
    ByteReader r(data, size);
    Timeline& t = p.timeline();

    const u64 rootPack = r.u64v();
    const u64 currentPack = r.u64v();
    t.seek(FrameIndex{r.i64v()});
    t.set_speed(r.f32v());
    t.set_loop(r.boolv());

    const u32 compCount = r.u32v();
    if (compCount > 256u) { r.skip_to_end(); return; }

    for (u32 ci = 0; ci < compCount && r.good(); ++ci) {
        const u64 idPack = r.u64v();
        const std::string name = r.str();
        const u32 w = r.u32v();
        const u32 h = r.u32v();
        const f64 fps = r.f64v();

        const CompositionId cid = t.create_composition(name, w, h, fps);
        Composition* c = t.composition(cid);
        if (!c) { r.skip_to_end(); return; }

        // O id original é reatribuído ao criado: os handles do arquivo são
        // remapeados na leitura, porque os índices de slot dependem da ordem
        // de criação desta sessão. O pack gravado serve só como referência
        // dentro do próprio arquivo — a remoção real acontece abaixo.
        (void)idPack;

        c->set_duration(FrameIndex{r.i64v()});
        c->set_background(r.color());
        c->set_transparent_background(r.boolv());
        c->set_nesting_depth(r.u32v());
        c->set_active_camera(LayerId::unpack(r.u64v()));

        const u32 layerCount = r.u32v();
        if (layerCount > kMaxLayerCount) { r.skip_to_end(); return; }

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
        for (const auto& [pack, now] : idMap) {
            (void)pack;
            if (Layer* dst = c->layer(now)) dst->parent = remap(dst->parent);
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
            if (mc > 100000u) { r.skip_to_end(); return; }
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
        c->rebuild_draw_order();
    }

    if (rootPack != 0) t.set_root(t.current());
    if (currentPack != 0 && t.current().valid()) t.set_current(t.current());
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
            case SectionKind::Timeline:
                report.timelineVersion = sh.version;
                if (sh.version < kTimelineSectionVersion) report.olderFormat = true;
                g_readingTimelineVersion = sh.version;
                apply_timeline_section(effective, effectiveSize, out);
                g_readingTimelineVersion = kTimelineSectionVersion;
                break;
            case SectionKind::Assets:
                apply_assets_section(effective, effectiveSize, out);
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
