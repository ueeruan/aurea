// =============================================================================
//  Substituir mídia (camada mantém tudo, tempo encolhe com clipe curto, um
//  desfazer) e arquivo do projeto (pacote com/sem mídia, religar, versão
//  futura, arquivo que não é projeto, nunca sobrescreve).
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/ProjectPackage.hpp"
#include "aurea/project/Serialization.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

namespace fs = std::filesystem;

std::string s8(const fs::path& p) {
    const auto u = p.u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

/// Dois clipes sintéticos: "longo" (10 s) e "curto" (2 s, outro tamanho).
class TwoClips final : public VideoSourceFactory {
public:
    TwoClips() : longo_(cfg(300, 64, 36)), curto_(cfg(60, 32, 32)) {}
    bool probe(const char* path, MediaProbe& out) override { return pick(path).probe(path, out); }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset& a, MediaPriority p) override {
        return pick(a.sourcePath.c_str()).open_video(a, p);
    }
    std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char*) override { return nullptr; }

private:
    static SyntheticConfig cfg(u32 frames, u32 w, u32 h) {
        SyntheticConfig c;
        c.frameCount = frames;
        c.width = w;
        c.height = h;
        return c;
    }
    SyntheticFactory& pick(const char* path) { return std::string(path).find("curto") != std::string::npos ? curto_ : longo_; }
    SyntheticFactory longo_, curto_;
};

struct Rig {
    TwoClips factory;
    Engine e;
    LayerId layer{};
    Rig() {
        EngineConfig ec;
        ec.workerCount = 2;
        ec.memoryBudgetBytes = 64ull << 20;
        ec.disableAutosave = true;
        ec.mediaFactory = &factory;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(64, 36, 30.0, nullptr).ok());
        VideoImport vi;
        vi.sourcePath = "longo.mp4";
        vi.displayName = "longo";
        auto id = e.import_video(vi);
        AUREA_CHECK(id.ok());
        if (id.ok()) layer = LayerId::unpack(*id);
    }
    ~Rig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L() { return comp()->layer(layer); }
};

void write_text(const std::string& path, const std::string& text) {
    std::FILE* f = fileio::open_file(path, "wb");
    AUREA_CHECK(f != nullptr);
    if (!f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

std::string read_text(const std::string& path) {
    std::string out;
    std::FILE* f = fileio::open_file(path, "rb");
    if (!f) return out;
    char buf[4096];
    usize n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "aurea_pacote_teste" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

} // namespace

AUREA_TEST(ReplaceMedia, KeepsLayerStateClampsShortClipAndUndoes) {
    Rig r;
    Layer* l = r.L();
    AUREA_CHECK(l != nullptr);
    if (!l) return;
    l->transform.position = Vec3{10.0f, 12.0f, 0.0f};
    l->transform.rotation.z = 30.0f;
    l->offset = FrameIndex{30};
    l->end = FrameIndex{200};
    Mask m;
    m.id = 1;
    m.points = {{Vec2{64.0f, 36.0f}}, {Vec2{0.0f, 0.0f}}, {Vec2{64.0f, 0.0f}}};
    l->masks.push_back(m);
    Command fx;
    fx.type = CommandType::EffectAdd;
    fx.effect_add.layer = r.layer;
    fx.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
    fx.effect_add.index = kInvalidIndex;
    AUREA_CHECK(r.e.apply_command(fx).ok());
    const AssetId before = r.L()->source;
    const f32 scaleBefore = r.L()->transform.scale.x;

    VideoImport vi;
    vi.sourcePath = "curto.mp4";
    vi.displayName = "curto";
    const auto res = r.e.replace_layer_video(r.layer.pack(), vi);
    AUREA_CHECK(res.ok() && *res == r.layer.pack());
    l = r.L();
    AUREA_CHECK(l->source != before);
    AUREA_CHECK(l->transform.position.x == 10.0f && l->transform.rotation.z == 30.0f);
    AUREA_CHECK_EQ(l->effects.size(), usize{1});
    // 2 s de fonte = 60 quadros; entrada no 30 → cabem 30 quadros.
    AUREA_CHECK_EQ(l->offset.value, i64{30});
    AUREA_CHECK_EQ(l->end.value, i64{30});
    // 32×32 dentro da caixa de 64×36: escala ×(36/32), âncora no centro novo, máscara na proporção.
    AUREA_CHECK_NEAR(l->transform.scale.x, scaleBefore * (36.0f / 32.0f), 1e-4f);
    AUREA_CHECK_NEAR(l->transform.anchor.x, 16.0f, 1e-4f);
    AUREA_CHECK_NEAR(l->transform.anchor.y, 16.0f, 1e-4f);
    AUREA_CHECK_NEAR(l->masks[0].points[0].position.x, 32.0f, 1e-4f);
    AUREA_CHECK_NEAR(l->masks[0].points[0].position.y, 32.0f, 1e-4f);

    Command u;
    u.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(u).ok());
    l = r.L();
    AUREA_CHECK(l->source == before);
    AUREA_CHECK_EQ(l->end.value, i64{200});
    AUREA_CHECK_NEAR(l->transform.anchor.x, 32.0f, 1e-4f);

    // Foto no lugar do vídeo: vira camada de imagem, tempo inteiro.
    std::vector<u8> px(8 * 4 * 4, 255);
    const auto img = r.e.replace_layer_image(r.layer.pack(), px.data(), 8, 4, "foto", "foto.png");
    AUREA_CHECK(img.ok());
    AUREA_CHECK(r.L()->kind == LayerKind::Image && r.L()->end.value == 200);
    AUREA_CHECK(!r.e.replace_layer_image(0xDEAD, px.data(), 8, 4, "x", "x").ok());
}

AUREA_TEST(ProjectPackage, ExportImportRelinksMediaAndNeverOverwrites) {
    const fs::path dir = scratch("ida_volta");
    const std::string media = s8(dir / "longo.mp4");
    write_text(media, std::string(5000, 'v'));
    const std::string aurea = s8(dir / "origem.aurea");
    {
        Rig r;
        VideoImport vi;
        vi.sourcePath = media;   // "longo" no nome: o clipe de 10 s
        vi.displayName = "longo.mp4";
        AUREA_CHECK(r.e.import_video(vi).ok());
        AUREA_CHECK(r.e.save_project(aurea.c_str()).ok());
        std::vector<package::MediaRef> refs;
        AUREA_CHECK(r.e.project_file_media(aurea.c_str(), refs).ok());
        AUREA_CHECK_EQ(refs.size(), usize{2});   // "longo.mp4" do Rig e o arquivo real
    }

    std::vector<package::MediaRef> refs;
    AUREA_CHECK(package::list_media(aurea, refs).ok());
    std::vector<package::MediaFile> files;
    for (const auto& m : refs) files.push_back({m.stored, m.stored, m.name});
    const std::string pkg = s8(dir / "Meu projeto.aureaproj");
    package::ExportResult ex;
    AUREA_CHECK(package::write_package(aurea, pkg, "Meu projeto", "2.0", files, &ex).ok());
    AUREA_CHECK_EQ(ex.included, 1u);   // "longo.mp4" relativo não existe em disco: pulado
    AUREA_CHECK_EQ(ex.skipped, 1u);
    AUREA_CHECK(!fs::exists(pkg + ".tmp"));

    const std::string out = s8(dir / "importado.aurea");
    const std::string mediaDir = s8(dir / "midia");
    package::ImportResult in;
    AUREA_CHECK(package::read_package(pkg, out, mediaDir, in).ok());
    AUREA_CHECK(in.title == "Meu projeto" && in.appVersion == "2.0");
    AUREA_CHECK_EQ(in.relinked, 1u);
    AUREA_CHECK_EQ(in.missing, 1u);
    std::vector<package::MediaRef> after;
    AUREA_CHECK(package::list_media(out, after).ok());
    bool found = false;
    for (const auto& m : after) {
        if (m.stored.find("midia") != std::string::npos) {
            found = true;
            AUREA_CHECK(read_text(m.stored) == std::string(5000, 'v'));
            AUREA_CHECK(s8(fs::u8path(m.stored).filename()) == "longo.mp4");
        }
    }
    AUREA_CHECK(found);
    // Nunca por cima de um projeto que já existe.
    AUREA_CHECK(package::read_package(pkg, out, mediaDir, in).code() == Errc::AlreadyExists);

    // Sem mídia: o projeto vem, a mídia fica contada como ausente.
    const std::string pkg2 = s8(dir / "so_projeto.aureaproj");
    AUREA_CHECK(package::write_package(aurea, pkg2, "So", "2.0", {}, nullptr).ok());
    package::ImportResult in2;
    AUREA_CHECK(package::read_package(pkg2, s8(dir / "b.aurea"), s8(dir / "midia_b"), in2).ok());
    AUREA_CHECK_EQ(in2.missing, 2u);
    AUREA_CHECK(!fs::exists(dir / "midia_b"));

    // Um `.aurea` solto também serve.
    package::ImportResult in3;
    AUREA_CHECK(package::read_package(aurea, s8(dir / "c.aurea"), s8(dir / "midia_c"), in3).ok());
    AUREA_CHECK_EQ(in3.missing, 2u);
}

AUREA_TEST(ProjectPackage, RejectsForeignNewerAndCorruptFiles) {
    const fs::path dir = scratch("erros");
    const std::string junk = s8(dir / "lixo.aureaproj");
    write_text(junk, "isto nao e um projeto");
    package::ImportResult r;
    AUREA_CHECK(package::read_package(junk, s8(dir / "a.aurea"), s8(dir / "m"), r).code() == Errc::UnsupportedFormat);
    AUREA_CHECK(!fs::exists(dir / "a.aurea"));

    // Pacote de uma versão futura do app: recusa com o motivo certo.
    const std::string aurea = s8(dir / "p.aurea");
    {
        Rig rig;
        AUREA_CHECK(rig.e.save_project(aurea.c_str()).ok());
    }
    const std::string pkg = s8(dir / "futuro.aureaproj");
    AUREA_CHECK(package::write_package(aurea, pkg, "F", "9.0", {}, nullptr).ok());
    std::string bytes = read_text(pkg);
    const usize at = bytes.find("aurea-project 1");
    AUREA_CHECK(at != std::string::npos);
    // Mesmo tamanho: "1" → "9". Só o CRC muda — refeito nos dois cabeçalhos.
    bytes[at + 14] = '9';
    const usize end = bytes.find('\0', at);   // o manifesto é a última entrada antes do diretório
    (void)end;
    const usize local = bytes.find("manifest.txt");
    const usize central = bytes.find("manifest.txt", local + 1);
    AUREA_CHECK(local != std::string::npos && central != std::string::npos);
    const usize manifestSize = static_cast<unsigned char>(bytes[local - 30 + 22]) | (static_cast<unsigned char>(bytes[local - 30 + 23]) << 8);
    u32 crc = 0xFFFFFFFFu;
    for (usize i = 0; i < manifestSize; ++i) {
        crc ^= static_cast<unsigned char>(bytes[local + 12 + i]);
        for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    crc = ~crc;
    for (int b = 0; b < 4; ++b) {
        bytes[local - 30 + 14 + b] = static_cast<char>((crc >> (8 * b)) & 0xFF);
        bytes[central - 46 + 16 + b] = static_cast<char>((crc >> (8 * b)) & 0xFF);
    }
    write_text(pkg, bytes);
    AUREA_CHECK(package::read_package(pkg, s8(dir / "b.aurea"), s8(dir / "m"), r).code() == Errc::UnsupportedVersion);
    AUREA_CHECK(!fs::exists(dir / "b.aurea"));
    // Um byte do manifesto trocado sem refazer o CRC: pacote danificado.
    bytes[local + 12] = 'X';
    write_text(pkg, bytes);
    AUREA_CHECK(package::read_package(pkg, s8(dir / "b.aurea"), s8(dir / "m"), r).code() == Errc::ChecksumMismatch);

    // Projeto por dentro ilegível (bytes trocados no meio do .aurea).
    std::string proj = read_text(aurea);
    for (usize i = proj.size() / 2; i < proj.size() / 2 + 64 && i < proj.size(); ++i) proj[i] = static_cast<char>(proj[i] ^ 0x5A);
    const std::string bad = s8(dir / "ruim.aurea");
    write_text(bad, proj);
    const std::string pkgBad = s8(dir / "ruim.aureaproj");
    // write_package recusa mandar adiante um projeto que não abre.
    AUREA_CHECK(!package::write_package(bad, pkgBad, "R", "1", {}, nullptr).ok());
}

// Galaxy A32: "importei o arquivo do projeto e o app fechou". Pacote de outro
// aparelho é entrada NÃO confiável: bytes trocados no diretório, no cabeçalho
// local, no manifesto ou no fim cortado têm de virar um erro — nunca crash,
// nunca arquivo fora da pasta de mídia, nunca projeto pela metade no lugar.
AUREA_TEST(ProjectPackage, FuzzedPackagesFailCleanlyAndStayInsideTheMediaDir) {
    const fs::path dir = scratch("fuzz");
    const std::string media = s8(dir / "longo.mp4");
    write_text(media, std::string(3000, 'v'));
    const std::string aurea = s8(dir / "origem.aurea");
    {
        Rig r;
        VideoImport vi;
        vi.sourcePath = media;
        vi.displayName = "longo.mp4";
        AUREA_CHECK(r.e.import_video(vi).ok());
        AUREA_CHECK(r.e.save_project(aurea.c_str()).ok());
    }
    std::vector<package::MediaRef> refs;
    AUREA_CHECK(package::list_media(aurea, refs).ok());
    std::vector<package::MediaFile> files;
    for (const auto& m : refs) files.push_back({m.stored, m.stored, m.name});
    const std::string pkg = s8(dir / "base.aureaproj");
    AUREA_CHECK(package::write_package(aurea, pkg, "Base", "2.0", files, nullptr).ok());
    const std::string base = read_text(pkg);
    AUREA_CHECK(base.size() > 200);
    if (base.size() <= 200) return;

    const fs::path work = dir / "caso";
    u64 state = 0xA32A32A32ull;
    auto next = [&]() { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; };
    u32 opened = 0, refused = 0;
    for (u32 it = 0; it < 400; ++it) {
        std::error_code ec;
        fs::remove_all(work, ec);
        fs::create_directories(work, ec);
        std::string bytes = base;
        const u32 kind = static_cast<u32>(next() % 4);
        if (kind == 3) {
            bytes.resize(static_cast<usize>(next() % bytes.size()));   // arquivo cortado
        } else {
            const u32 mutations = 1 + static_cast<u32>(next() % 6);
            for (u32 m = 0; m < mutations; ++m) {
                // Metade das trocas no fim (diretório central + EOCD), onde
                // moram contagens, tamanhos e deslocamentos.
                const usize tail = std::min<usize>(bytes.size(), 160);
                const usize at = (next() & 1) ? bytes.size() - 1 - static_cast<usize>(next() % tail)
                                              : static_cast<usize>(next() % bytes.size());
                switch (kind) {
                    case 0: bytes[at] = static_cast<char>(bytes[at] ^ (1 << (next() % 8))); break;
                    case 1: bytes[at] = static_cast<char>(next()); break;
                    default: bytes[at] = static_cast<char>(0xFF); break;
                }
            }
        }
        const std::string casePkg = s8(work / "caso.aureaproj");
        write_text(casePkg, bytes);
        const fs::path out = work / "importado.aurea";
        const fs::path mediaDir = work / "midia";
        package::ImportResult res;
        const Status s = package::read_package(casePkg, s8(out), s8(mediaDir), res);
        if (s.ok()) {
            ++opened;
            Project p;
            LoadReport report;
            AUREA_CHECK(ProjectSerializer::load(p, s8(out), LoadOptions{}, &report, nullptr).ok());
        } else {
            ++refused;
            // Recusado: nada de projeto pela metade.
            AUREA_CHECK(!fs::exists(out));
        }
        // Nada fora de `work`: só o pacote, o projeto e a pasta de mídia.
        for (const auto& entry : fs::directory_iterator(work, ec)) {
            const fs::path name = entry.path().filename();
            AUREA_CHECK(name == "caso.aureaproj" || name == "importado.aurea" || name == "midia");
        }
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            const fs::path name = entry.path().filename();
            AUREA_CHECK(name == "longo.mp4" || name == "origem.aurea" || name == "base.aureaproj" || name == "caso"
                        || name.u8string().rfind(u8"origem.aurea.", 0) == 0);
        }
    }
    AUREA_CHECK(refused > 0);
    std::printf("(%u pacotes mutados: %u abriram, %u recusados) ", opened + refused, opened, refused);
}
