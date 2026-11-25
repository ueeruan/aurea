// =============================================================================
//  Aurea / tests / AureaPindownBuild.inl  (incluído no fim de test_editor_reference.cpp)
//
//  Ferramenta, não verificação: monta pelo motor o projeto editável do vídeo
//  "aurea-pindown" e grava o arquivo do projeto (.aureaproj) com a mídia dentro.
//  Sem as variáveis abaixo, não faz nada (as rodadas normais pulam).
//
//    AUREA_PINDOWN_PLAN     plano (build/agents/pindown/plan_pindown.py): fundo,
//                           camadas de imagem, keys já reduzidos, áudio
//    AUREA_PINDOWN_AUREA    onde gravar o .aurea montado
//    AUREA_PINDOWN_PACKAGE  onde gravar o .aureaproj
//    AUREA_PINDOWN_CHECK    pasta de conferência: o pacote é importado de novo
//                           (read_package + load_project) e os quadros saem em
//                           PNG (f_00000.png…) para comparar com o vídeo final
//    AUREA_PINDOWN_CHECK_ONLY=1  só a conferência de um pacote já gravado
//
//  Camadas: imagem (PNG em 2×, escala base 50 %) ou, com a linha TEXT do plano,
//  texto de verdade (fonte importada; o pacote leva o arquivo da fonte).
//
//  Tudo passa pela API do app: new_project, import_image, import_audio e os
//  comandos de camada/keyframe/efeito; nada do formato é escrito à mão.
// =============================================================================

#include "aurea/core/Version.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/export/ImageEncode.hpp"
#include "aurea/project/ProjectPackage.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

extern "C" unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
extern "C" void stbi_image_free(void*);

namespace pindown {

namespace fs = std::filesystem;

std::string s8(const fs::path& p) {
    const auto u = p.u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

bool read_file(const std::string& path, std::vector<u8>& out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

/// O `imageLoader` do host: PNG/JPEG por caminho, RGBA8 alfa reto.
bool load_image(const char* path, ImagePixels& out, void*) {
    std::vector<u8> bytes;
    if (!path || !read_file(path, bytes)) return false;
    int w = 0, h = 0, c = 0;
    u8* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c, 4);
    if (!px) return false;
    out.width = static_cast<u32>(w);
    out.height = static_cast<u32>(h);
    out.rgba.assign(px, px + static_cast<usize>(w) * h * 4);
    stbi_image_free(px);
    return true;
}

/// Só a sondagem de um WAV PCM (taxa, canais, duração): é o que o import de
/// áudio grava no projeto. Tocar fica com o decoder do aparelho.
class WavProbe final : public VideoSourceFactory {
public:
    bool probe(const char* path, MediaProbe& out) override {
        std::vector<u8> b;
        if (!read_file(path, b) || b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return false;
        auto u16le = [&](usize p) { return static_cast<u32>(b[p] | (b[p + 1] << 8)); };
        auto u32le = [&](usize p) { return static_cast<u32>(b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | (static_cast<u32>(b[p + 3]) << 24)); };
        u32 rate = 0, channels = 0, align = 0;
        u64 dataBytes = 0;
        for (usize p = 12; p + 8 <= b.size();) {
            const u32 len = u32le(p + 4);
            if (!std::memcmp(b.data() + p, "fmt ", 4) && p + 24 <= b.size()) {
                channels = u16le(p + 10);
                rate = u32le(p + 12);
                align = u16le(p + 20);
            } else if (!std::memcmp(b.data() + p, "data", 4)) {
                dataBytes = std::min<u64>(len, b.size() - p - 8);
            }
            p += 8 + len + (len & 1u);
        }
        if (!rate || !channels || !align || !dataBytes) return false;
        out.hasAudio = true;
        out.audioSampleRate = rate;
        out.audioChannels = channels;
        out.audioDurationUs = static_cast<i64>(dataBytes / align * 1'000'000ull / rate);
        out.fileBytes = b.size();
        return true;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override { return nullptr; }
};

struct Key { i64 frame = 0; f32 value = 0; char interp = 'L'; f32 b[4]{0.33f, 0.f, 0.67f, 1.f}; };
struct LayerPlan {
    std::string id, png, name;
    f32 ax = 0, ay = 0;
    i64 start = 0, end = 0;
    std::map<std::string, f32> statics;
    std::map<std::string, std::vector<Key>> keys;
    // Texto de verdade (linha TEXT): fonte importada, tamanho do motor,
    // tracking (milésimos), cor sRGB e a âncora na caneta da linha de base.
    bool text = false;
    std::string font, content;
    u32 weight = 400;
    f32 size = 0, tracking = 0, rgb[3]{1, 1, 1};
};
struct Plan {
    u32 w = 0, h = 0, frames = 0;
    f64 fps = 30;
    f32 bg[4]{0, 0, 0, 1};
    std::vector<LayerPlan> layers;
    std::string audio, audioName;
};

bool read_plan(const std::string& path, Plan& plan) {
    std::ifstream f(fs::u8path(path));
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream s(line);
        std::string tag;
        s >> tag;
        if (tag == "COMP") {
            u32 r, g, b;
            s >> plan.w >> plan.h >> plan.fps >> plan.frames >> r >> g >> b;
            plan.bg[0] = r / 255.f; plan.bg[1] = g / 255.f; plan.bg[2] = b / 255.f;
        } else if (tag == "LAYER") {
            LayerPlan l;
            s >> l.id >> l.png >> l.name >> l.ax >> l.ay >> l.start >> l.end;
            for (char& c : l.name) if (c == '_') c = ' ';
            plan.layers.push_back(std::move(l));
        } else if (tag == "TEXT" && !plan.layers.empty()) {
            LayerPlan& l = plan.layers.back();
            u32 r, g, b;
            l.text = true;
            s >> l.font >> l.weight >> l.size >> l.tracking >> r >> g >> b >> l.ax >> l.ay >> l.content;
            for (char& c : l.content) if (c == '_') c = ' ';
            l.rgb[0] = r / 255.f; l.rgb[1] = g / 255.f; l.rgb[2] = b / 255.f;
        } else if (tag == "STATIC" && !plan.layers.empty()) {
            std::string prop; f32 v;
            s >> prop >> v;
            plan.layers.back().statics[prop] = v;
        } else if (tag == "KEY" && !plan.layers.empty()) {
            std::string prop; Key k;
            s >> prop >> k.frame >> k.value >> k.interp;
            if (k.interp == 'B') s >> k.b[0] >> k.b[1] >> k.b[2] >> k.b[3];
            plan.layers.back().keys[prop].push_back(k);
        } else if (tag == "AUDIO") {
            s >> plan.audio >> plan.audioName;
        }
    }
    return plan.w && plan.h && plan.frames && !plan.layers.empty();
}

TrackProperty transform_property(const std::string& p) {
    if (p == "px") return TrackProperty::PositionX;
    if (p == "py") return TrackProperty::PositionY;
    if (p == "sx") return TrackProperty::ScaleX;
    if (p == "sy") return TrackProperty::ScaleY;
    if (p == "rot") return TrackProperty::RotationZ;
    return TrackProperty::Opacity;
}

Status insert_keys(Engine& e, const TrackRef& ref, const std::vector<Key>& keys) {
    for (const Key& k : keys) {
        Command c;
        c.type = CommandType::KeyframeInsert;
        c.keyframe.track = ref;
        c.keyframe.time = FrameIndex{k.frame};   // tempo LOCAL = da composição (offset = início)
        c.keyframe.value = k.value;
        c.keyframe.onlyIfChanged = 0;
        if (Status s = e.apply_command(c); !s.ok()) return s;
    }
    for (const Key& k : keys) {
        Command c;
        c.type = CommandType::KeyframeSetInterpolation;
        c.keyframe_interp.track = ref;
        c.keyframe_interp.time = FrameIndex{k.frame};
        c.keyframe_interp.interp = k.interp == 'B' ? Interpolation::Bezier : k.interp == 'H' ? Interpolation::Hold : Interpolation::Linear;
        c.keyframe_interp.power = 1;
        c.keyframe_interp.linkAxes = 0;
        c.keyframe_interp.bx1 = k.b[0]; c.keyframe_interp.by1 = k.b[1];
        c.keyframe_interp.bx2 = k.b[2]; c.keyframe_interp.by2 = k.b[3];
        if (Status s = e.apply_command(c); !s.ok()) return s;
    }
    return OkStatus;
}

bool init_engine(Engine& e, WavProbe& wav) {
    EngineConfig config;
    config.backend = new vk::Backend();
    config.backendConfig.enableValidation = false;
    config.disableAutosave = true;
    config.workerCount = 2;
    config.mediaFactory = &wav;
    config.imageLoader = &load_image;
    return e.initialize(config).ok();
}

Composition* current(Engine& e) { return e.project()->timeline().composition(e.project()->timeline().current()); }

} // namespace pindown

AUREA_TEST(Pindown, BuildEditableProjectPackage) {
    using namespace pindown;
    const char* planPath = std::getenv("AUREA_PINDOWN_PLAN");
    const char* aureaPath = std::getenv("AUREA_PINDOWN_AUREA");
    const char* packagePath = std::getenv("AUREA_PINDOWN_PACKAGE");
    const char* checkDir = std::getenv("AUREA_PINDOWN_CHECK");
    if (!planPath || !aureaPath || !packagePath || !*planPath || !*aureaPath || !*packagePath) return;
    Plan plan;
    AUREA_CHECK(read_plan(planPath, plan));
    if (plan.layers.empty()) return;

    WavProbe wav;
    u32 keyCount = 0;
    // AUREA_PINDOWN_CHECK_ONLY=1: só a conferência de um pacote já gravado (outro
    // processo: nada da montagem fica em memória; as fontes vêm só do pacote).
    const char* checkOnly = std::getenv("AUREA_PINDOWN_CHECK_ONLY");
    const bool build = !(checkOnly && *checkOnly == '1');
    if (build) {
        Engine e;
        AUREA_CHECK(init_engine(e, wav));
        AUREA_CHECK(e.new_project(plan.w, plan.h, plan.fps, "Aurea", plan.bg).ok());
        Composition* comp = current(e);
        AUREA_CHECK(comp && !comp->edit_mode());
        if (!comp) return;
        Command dur;
        dur.type = CommandType::CompositionSetDuration;
        dur.comp_duration.comp = e.project()->timeline().current();
        dur.comp_duration.duration = FrameIndex{static_cast<i64>(plan.frames)};
        AUREA_CHECK(e.apply_command(dur).ok());

        for (const LayerPlan& lp : plan.layers) {
            u64 idv = 0;
            if (lp.text) {
                const auto font = e.import_font(lp.font.c_str());
                AUREA_CHECK(font.ok());
                if (!font.ok()) return;
                const auto id = e.add_text(lp.content.c_str());
                AUREA_CHECK(id.ok());
                if (!id.ok()) return;
                idv = *id;
                AUREA_CHECK(e.set_text_font(idv, font->family, font->weight, font->italic, font->path));
                std::printf("    fonte %s %u (%s)\n", font->family.c_str(), static_cast<u32>(font->weight), font->path.c_str());
                Command c;
                c.type = CommandType::TextSetAlignment;
                c.text_align.layer = LayerId::unpack(idv);
                c.text_align.alignment = 0;
                AUREA_CHECK(e.apply_command(c).ok());
                c = Command{};
                c.type = CommandType::TextSetSize;
                c.text_size.layer = LayerId::unpack(idv);
                c.text_size.size = lp.size;
                AUREA_CHECK(e.apply_command(c).ok());
                c = Command{};
                c.type = CommandType::TextSetColor;
                c.text_color.layer = LayerId::unpack(idv);
                c.text_color.r = lp.rgb[0]; c.text_color.g = lp.rgb[1]; c.text_color.b = lp.rgb[2]; c.text_color.a = 1.0f;
                AUREA_CHECK(e.apply_command(c).ok());
                f32 style[22]{};
                AUREA_CHECK(e.query_text_style(idv, style, 22));
                style[19] = lp.tracking;
                AUREA_CHECK(e.set_text_style(idv, style, 20));
                Command name;
                name.type = CommandType::LayerSetName;
                name.layer_ref.layer = LayerId::unpack(idv);
                AUREA_CHECK(e.apply_command(name, lp.name.c_str()).ok());
            } else {
                ImagePixels px;
                AUREA_CHECK(load_image(lp.png.c_str(), px, nullptr));
                const auto id = e.import_image(px.rgba.data(), px.width, px.height, lp.name.c_str(), lp.png.c_str());
                AUREA_CHECK(id.ok());
                if (!id.ok()) return;
                idv = *id;
                const Layer* l = current(e)->layer(LayerId::unpack(idv));
                AUREA_CHECK(l && l->kind == LayerKind::Image);
                // A imagem não pode ter sido reduzida: a âncora é em px do PNG.
                const Asset* a = l ? e.project()->asset(l->source) : nullptr;
                AUREA_CHECK(a && a->video.width == px.width && a->video.height == px.height);
            }
            const LayerId lid = LayerId::unpack(idv);
            Command range;
            range.type = CommandType::LayerSetTimeRange;
            range.layer_range.layer = lid;
            range.layer_range.start = FrameIndex{lp.start};
            range.layer_range.end = FrameIndex{lp.end};
            range.layer_range.offset = FrameIndex{lp.start};   // corte no início: os keys ficam no quadro da composição
            range.layer_range.setOffset = 1;
            AUREA_CHECK(e.apply_command(range).ok());

            auto first = [&](const char* p, f32 fallback) {
                if (auto k = lp.keys.find(p); k != lp.keys.end() && !k->second.empty()) return k->second.front().value;
                if (auto s = lp.statics.find(p); s != lp.statics.end()) return s->second;
                return fallback;
            };
            Command t;
            t.type = CommandType::LayerSetTransform;
            t.transform.layer = lid;
            t.transform.x = first("px", plan.w * 0.5f); t.transform.y = first("py", plan.h * 0.5f); t.transform.z = 0;
            const f32 baseScale = lp.text ? 1.0f : 0.5f;   // PNG em 2×; texto em px da composição
            t.transform.sx = first("sx", 0.5f) / 0.5f * baseScale; t.transform.sy = first("sy", 0.5f) / 0.5f * baseScale; t.transform.sz = 1;
            t.transform.rx = 0; t.transform.ry = 0; t.transform.rz = first("rot", 0);
            t.transform.ax = lp.ax; t.transform.ay = lp.ay; t.transform.az = 0;
            t.transform.opacity = first("opa", 1);
            AUREA_CHECK(e.apply_command(t).ok());

            u32 fxId = kInvalidIndex;
            const bool wantsExposure = lp.keys.count("exp") || lp.statics.count("exp");
            if (wantsExposure) {
                // Brilho multiplicativo da referência = Exposição no "Valor
                // codificado" (v·2^stops sobre o valor sRGB, preso em 0..1).
                Command add;
                add.type = CommandType::EffectAdd;
                add.effect_add.layer = lid;
                add.effect_add.effectType = e.effects().find_key(effect_keys::kExposure);
                add.effect_add.index = kInvalidIndex;
                AUREA_CHECK(e.apply_command(add).ok());
                const Layer* l = current(e)->layer(lid);
                AUREA_CHECK(l && !l->effects.empty());
                if (!l || l->effects.empty()) return;
                fxId = l->effects.back().id;
                Command space;
                space.type = CommandType::EffectSetParam;
                space.effect_param.layer = lid;
                space.effect_param.effect = EffectId{fxId, 0};
                space.effect_param.paramIndex = 3;   // "Aplicar em" → Valor codificado
                space.effect_param.value = 1.0f;
                AUREA_CHECK(e.apply_command(space).ok());
                if (auto s = lp.statics.find("exp"); s != lp.statics.end()) {
                    Command v = space;
                    v.effect_param.paramIndex = 0;
                    v.effect_param.value = s->second;
                    AUREA_CHECK(e.apply_command(v).ok());
                }
            }
            for (const auto& [prop, keys] : lp.keys) {
                TrackRef ref;
                ref.layer = lid;
                if (prop == "exp") {
                    ref.property = TrackProperty::EffectParam;
                    ref.effectIndex = fxId;
                    ref.effectParamIndex = param_track_key(0, 0);
                } else {
                    ref.property = transform_property(prop);
                }
                std::vector<Key> ks = keys;
                if (lp.text && (prop == "sx" || prop == "sy")) for (Key& k : ks) k.value *= 2.0f;
                const Status s = insert_keys(e, ref, ks);
                if (!s.ok()) std::printf("    %s.%s: %s\n", lp.id.c_str(), prop.c_str(), std::string(s.message()).c_str());
                AUREA_CHECK(s.ok());
                keyCount += static_cast<u32>(keys.size());
            }
        }
        if (!plan.audio.empty()) {
            VideoImport vi;
            vi.sourcePath = plan.audio;
            vi.displayName = plan.audioName;
            const auto id = e.import_audio(vi);
            AUREA_CHECK(id.ok());
            if (id.ok()) {
                Command range;
                range.type = CommandType::LayerSetTimeRange;
                range.layer_range.layer = LayerId::unpack(*id);
                range.layer_range.start = FrameIndex{0};
                range.layer_range.end = FrameIndex{static_cast<i64>(plan.frames)};
                range.layer_range.offset = FrameIndex{0};
                range.layer_range.setOffset = 1;
                AUREA_CHECK(e.apply_command(range).ok());
            }
        }
        AUREA_CHECK_EQ(current(e)->duration().value, static_cast<i64>(plan.frames));
        Command seek;
        seek.type = CommandType::PlaybackSeek;
        seek.seek.time = tick_at(FrameIndex{0}, plan.fps);
        AUREA_CHECK(e.apply_command(seek).ok());
        std::error_code ec;
        fs::create_directories(fs::u8path(aureaPath).parent_path(), ec);
        fs::remove(fs::u8path(aureaPath), ec);
        AUREA_CHECK(e.save_project(aureaPath).ok());
        std::printf("    %u camadas, %u keys, salvo em %s\n", current(e)->layers().count(), keyCount, aureaPath);
        e.shutdown();
    }

    if (build) {
        std::vector<package::MediaRef> refs;
        AUREA_CHECK(package::list_media(aureaPath, refs).ok());
        std::vector<package::MediaFile> files;
        // Entrada do pacote com o nome do ARQUIVO (com extensão: o decoder do
        // iOS escolhe o formato por ela), não o nome da camada.
        for (const auto& m : refs) files.push_back({m.stored, m.stored, s8(fs::u8path(m.stored).filename())});
        package::ExportResult ex;
        AUREA_CHECK(package::write_package(aureaPath, packagePath, "Aurea", AUREA_VERSION_LABEL, files, &ex).ok());
        std::printf("    pacote %s: %u mídias, %u puladas, %llu bytes\n", packagePath, ex.included, ex.skipped,
                    static_cast<unsigned long long>(ex.bytes));
        AUREA_CHECK_EQ(ex.skipped, 0u);
    }

    if (!checkDir || !*checkDir) return;
    // Conferência: o pacote como o app o importa, aberto do zero, quadro a quadro.
    const fs::path check = fs::u8path(checkDir);
    std::error_code ec;
    fs::remove_all(check / "import", ec);
    fs::create_directories(check / "import", ec);
    fs::create_directories(check / "frames", ec);
    const std::string reopened = s8(check / "import" / "Aurea.aurea");
    package::ImportResult in;
    AUREA_CHECK(package::read_package(packagePath, reopened, s8(check / "import" / "media"), in).ok());
    {
        std::vector<package::MediaRef> after;
        AUREA_CHECK(package::list_media(reopened, after).ok());
        for (const auto& m : after) std::printf("    mídia %u: %s\n", static_cast<u32>(m.kind), m.stored.c_str());
    }
    std::printf("    importado: %u mídias religadas, %u ausentes\n", in.relinked, in.missing);
    AUREA_CHECK_EQ(in.missing, 0u);

    Engine e;
    AUREA_CHECK(init_engine(e, wav));
    AUREA_CHECK(e.load_project(reopened.c_str()).ok());
    AUREA_CHECK_EQ(e.last_load_missing_assets(), 0u);
    Composition* comp = current(e);
    AUREA_CHECK(comp != nullptr);
    if (!comp) return;
    u32 layers = 0, keys = 0;
    comp->layers().for_each([&](LayerId, const Layer& l) {
        ++layers;
        for (u32 i = 0; i < l.tracks.size(); ++i) keys += static_cast<u32>(l.tracks.at(i).keys.size());
    });
    std::printf("    reaberto: %u camadas, %u keys, %lld quadros\n", layers, keys, static_cast<long long>(comp->duration().value));
    for (u32 f = 0; f < plan.frames; ++f) {
        Command seek;
        seek.type = CommandType::PlaybackSeek;
        seek.seek.time = tick_at(FrameIndex{static_cast<i64>(f)}, plan.fps);
        AUREA_CHECK(e.apply_command(seek).ok());
        std::vector<u8> rgba;
        u32 w = 0, h = 0;
        const Status s = e.capture_frame_rgba(std::max(plan.w, plan.h), rgba, w, h);
        AUREA_CHECK(s.ok() && w == plan.w && h == plan.h);
        if (!s.ok()) break;
        std::vector<u8> png;
        AUREA_CHECK(encode_png(rgba.data(), w, h, false, png).ok());
        char name[32];
        std::snprintf(name, sizeof name, "f_%05u.png", f);
        std::ofstream o(check / "frames" / name, std::ios::binary);
        o.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    }
    e.shutdown();
}
