// Testes da Fase 8G — estabilidade: perda de dados, crash e corrupção (§53–58,
// §115–127, §134).
//
// Regra destes testes: nada de "não crashou, então passou". Cada cenário
// confere o que o usuário veria — o arquivo anterior byte a byte intacto, a
// última cópia válida aberta, o erro com o código padronizado, o estado depois
// de 1000 desfazer/refazer igual ao de antes. Os fuzz são determinísticos
// (semente fixa): uma falha se reproduz rodando o mesmo teste.
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"
#include "OldProjects.hpp"

#include "aurea/Engine.hpp"
#include "aurea/command/History.hpp"
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/text/Captions.hpp"

#if defined(AUREA_TEST_VULKAN)
#include "VulkanBackend.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

// --- Utilidades --------------------------------------------------------------

/// xorshift64: determinístico, sem depender da implementação de <random>.
struct Rng {
    u64 s;
    explicit Rng(u64 seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    u64 next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    u32 u32v() { return static_cast<u32>(next() >> 32); }
    u32 below(u32 n) { return n ? u32v() % n : 0; }
    f32 unit() { return static_cast<f32>(u32v() >> 8) * (1.0f / 16777216.0f); }
};

/// Valores que quebram conta: NaN, infinitos, gigantes, denormais, zero.
f32 wild_float(Rng& r) {
    static const f32 k[] = {0.0f, 1.0f, -1.0f, 0.5f, 2.0f, 100.0f, -100.0f, 1e-30f, 1e6f, -1e6f, 1e30f, -1e30f,
                            std::numeric_limits<f32>::max(), -std::numeric_limits<f32>::max(),
                            std::numeric_limits<f32>::quiet_NaN(), std::numeric_limits<f32>::infinity(),
                            -std::numeric_limits<f32>::infinity(), std::numeric_limits<f32>::denorm_min()};
    if (r.below(3) == 0) return (r.unit() * 2.0f - 1.0f) * 2000.0f;
    return k[r.below(static_cast<u32>(sizeof(k) / sizeof(k[0])))];
}

f64 ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

std::string test_path(const char* name) { return std::string("aurea_estab_") + name + ".aurea"; }

void remove_family(const std::string& p) {
    for (const char* suffix : {"", ".bak", ".tmp", ".corrompido", ".v2.bak", ".v12.bak", ".bak.tmp", ".corrompido.tmp"}) {
        std::remove((p + suffix).c_str());
    }
}

std::vector<u8> read_file(const std::string& p) {
    std::vector<u8> v;
    (void)fileio::read_all(p, v, 1u << 30);
    return v;
}

bool write_raw(const std::string& p, const std::vector<u8>& d) {
    std::FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return false;
    const usize n = d.empty() ? 0 : std::fwrite(d.data(), 1, d.size(), f);
    std::fclose(f);
    return n == d.size();
}

u32 crc32_ieee(const u8* p, usize n) {
    static u32 table[256];
    static bool ready = false;
    if (!ready) {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    u32 c = 0xFFFFFFFFu;
    for (usize i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

template <typename T> T rd(const std::vector<u8>& f, usize o) { T v{}; std::memcpy(&v, f.data() + o, sizeof(T)); return v; }
template <typename T> void wr(std::vector<u8>& f, usize o, T v) { std::memcpy(f.data() + o, &v, sizeof(T)); }

/// Índice de seções do .aurea: cabeçalho de 64 bytes; 40 por seção (kind u16,
/// version u32, offset u64, size u64, raw u64, crc u32, flags u32).
struct SecRef { usize hdr; u16 kind; u64 offset; u64 size; };
std::vector<SecRef> sections_of(const std::vector<u8>& f) {
    std::vector<SecRef> out;
    if (f.size() < 64) return out;
    const u32 count = std::min<u32>(rd<u32>(f, 8), 64u);
    const u64 idx = rd<u64>(f, 12);
    for (u32 i = 0; i < count; ++i) {
        const u64 h = idx + 40ull * i;
        if (h > f.size() || f.size() - h < 40) break;
        const u64 off = rd<u64>(f, static_cast<usize>(h + 6));
        const u64 size = rd<u64>(f, static_cast<usize>(h + 14));
        if (off > f.size() || size > f.size() - off) continue;
        out.push_back(SecRef{static_cast<usize>(h), rd<u16>(f, static_cast<usize>(h)), off, size});
    }
    return out;
}

/// Refaz os checksums: o fuzz quer chegar AO PARSER, não parar no CRC.
void fix_crcs(std::vector<u8>& f) {
    for (const SecRef& s : sections_of(f)) {
        wr<u32>(f, s.hdr + 30, crc32_ieee(f.data() + s.offset, static_cast<usize>(s.size)));
    }
}

std::vector<u8> timeline_bytes(const Project& p) {
    std::vector<u8> all;
    (void)ProjectSerializer::encode(p, SaveOptions{}, all);
    for (const SecRef& s : sections_of(all)) {
        if (s.kind == static_cast<u16>(SectionKind::Timeline)) {
            return std::vector<u8>(all.begin() + static_cast<std::ptrdiff_t>(s.offset),
                                   all.begin() + static_cast<std::ptrdiff_t>(s.offset + s.size));
        }
    }
    return {};
}

EngineConfig headless() {
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 256ull * 1024 * 1024;
    cfg.disableAutosave = true;
    return cfg;
}

u32 layer_count(const Engine& e) {
    const Project* p = e.project();
    if (!p) return 0;
    const Composition* c = p->timeline().composition(p->timeline().current());
    return c ? c->layers().count() : 0;
}

std::vector<u64> layer_ids(const Engine& e) {
    std::vector<u64> out;
    const Project* p = e.project();
    if (!p) return out;
    const Composition* c = p->timeline().composition(p->timeline().current());
    if (!c) return out;
    for (u32 i = 0; i < c->order().size(); ++i) out.push_back(c->order().at(i).pack());
    return out;
}

Command effect_add(u64 layer, EffectTypeId type) {
    Command c;
    c.type = CommandType::EffectAdd;
    c.effect_add.layer = LayerId::unpack(layer);
    c.effect_add.effectType = type;
    c.effect_add.index = kInvalidIndex;
    return c;
}

Command keyframe(u64 layer, TrackProperty prop, i64 frame, f32 value) {
    Command c;
    c.type = CommandType::KeyframeInsert;
    c.keyframe.track.layer = LayerId::unpack(layer);
    c.keyframe.track.property = prop;
    c.keyframe.time = FrameIndex{frame};
    c.keyframe.value = value;
    return c;
}

/// Projeto "de tudo um pouco": texto, formas, vetor, partículas, nulos,
/// efeitos de vários tipos, keyframes e máscara.
void build_rich(Engine& e) {
    AUREA_CHECK(e.new_project(640, 360, 30.0, "rico").ok());
    std::vector<u64> ids;
    for (u32 i = 0; i < 3; ++i) if (auto r = e.add_text("Ola estabilidade"); r.ok()) ids.push_back(*r);
    for (u32 p = 0; p < 4; ++p) if (auto r = e.add_shape(p); r.ok()) ids.push_back(*r);
    if (auto r = e.add_vector_layer(0); r.ok()) ids.push_back(*r);
    if (auto r = e.add_particles(0); r.ok()) ids.push_back(*r);
    if (auto r = e.add_null(false); r.ok()) ids.push_back(*r);
    if (auto r = e.add_null(true); r.ok()) ids.push_back(*r);
    AUREA_CHECK(ids.size() >= 8);
    const EffectRegistry& reg = e.effects();
    for (u32 i = 0; i < ids.size(); ++i) {
        for (u32 k = 0; k < 2; ++k) (void)e.apply_command(effect_add(ids[i], reg.at((i * 2 + k) % reg.count()).type_id()));
        for (i64 f = 0; f < 60; f += 10) {
            (void)e.apply_command(keyframe(ids[i], (f / 10) % 2 ? TrackProperty::Opacity : TrackProperty::PositionX, f,
                                           static_cast<f32>(f) / 60.0f));
        }
    }
    const f32 pts[] = {0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 100, 100, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0};
    AUREA_CHECK(e.add_mask(ids[0], pts, 4, true) >= 0);
}

} // namespace

// =============================================================================
// Projeto em disco (§53–58)
// =============================================================================

AUREA_TEST(Stability, TruncatedFileNeverOpensCleanAndNeverCrashes) {
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    build_rich(e);
    std::vector<u8> full;
    AUREA_CHECK(ProjectSerializer::encode(*e.project(), SaveOptions{}, full).ok());
    AUREA_CHECK(full.size() > 2000);

    // Todo corte nos primeiros/últimos 256 bytes e ~600 cortes no meio: uma
    // queda no meio da escrita produz exatamente um prefixo do arquivo.
    u32 cuts = 0, strictRejected = 0, tolerantOpened = 0;
    const usize step = std::max<usize>(1, full.size() / 600);
    for (usize cut = 0; cut < full.size();) {
        ++cuts;
        Project p;
        LoadReport r;
        if (!ProjectSerializer::load_bytes(p, full.data(), cut, LoadOptions{}, &r).ok()) ++strictRejected;
        Project q;
        LoadReport rq;
        LoadOptions tol;
        tol.tolerateCorruptSections = true;
        if (ProjectSerializer::load_bytes(q, full.data(), cut, tol, &rq).ok()) {
            ++tolerantOpened;
            AUREA_CHECK(!rq.clean());   // aberto, mas nunca "limpo"
        }
        cut += (cut < 256 || cut + 256 > full.size()) ? 1 : step;
    }
    AUREA_CHECK_EQ(strictRejected, cuts);
    Project whole;
    AUREA_CHECK(ProjectSerializer::load_bytes(whole, full.data(), full.size(), LoadOptions{}).ok());
    std::printf("(%u cortes de %zu bytes; tolerante abriu %u) ", cuts, full.size(), tolerantOpened);
    e.shutdown();
}

AUREA_TEST(Stability, RandomBytesNeverCrashTheReader) {
    Rng rng(0xA11CE);
    u32 opened = 0;
    for (u32 it = 0; it < 4000; ++it) {
        std::vector<u8> buf(rng.below(4096));
        for (u8& b : buf) b = static_cast<u8>(rng.u32v());
        // Metade com cabeçalho plausível (magic + versão + índice), para o
        // leitor passar do primeiro portão e cair nos limites do índice.
        if (buf.size() >= 64 && (it & 1)) {
            wr<u32>(buf, 0, FileHeader::kMagic);
            wr<u16>(buf, 4, 1);
            wr<u16>(buf, 6, 1);
            wr<u32>(buf, 8, rng.below(70));
            wr<u64>(buf, 12, rng.below(4) == 0 ? rng.next() : 64);   // às vezes perto de 2^64
            for (const SecRef& s : sections_of(buf)) (void)s;
        }
        Project p;
        LoadOptions o;
        o.tolerateCorruptSections = (it % 3) == 0;
        o.metadataOnly = (it % 5) == 0;
        if (ProjectSerializer::load_bytes(p, buf.data(), buf.size(), o).ok()) ++opened;
    }
    std::printf("(4000 buffers, %u abriram algo) ", opened);
    AUREA_CHECK(true);
}

AUREA_TEST(Stability, SectionOffsetsNearTwoToTheSixtyFourAreRejected) {
    // Bug real (P0): `offset + size > file.size()` em u64 dava a volta com
    // offset perto de 2^64 e passava — leitura fora do buffer.
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    build_rich(e);
    std::vector<u8> good;
    AUREA_CHECK(ProjectSerializer::encode(*e.project(), SaveOptions{}, good).ok());
    const std::vector<SecRef> secs = sections_of(good);
    AUREA_CHECK(!secs.empty());
    for (const u64 off : {~0ull, ~0ull - 7, ~0ull - static_cast<u64>(good.size()) + 1}) {
        std::vector<u8> f = good;
        wr<u64>(f, secs[0].hdr + 6, off);
        wr<u64>(f, secs[0].hdr + 14, 16);
        Project p;
        LoadOptions tol;
        tol.tolerateCorruptSections = true;
        LoadReport r;
        (void)ProjectSerializer::load_bytes(p, f.data(), f.size(), tol, &r);
        AUREA_CHECK(!r.sectionsCorrupt.empty());
        AUREA_CHECK(!ProjectSerializer::load_bytes(p, f.data(), f.size(), LoadOptions{}).ok());
        // Índice perto de 2^64.
        std::vector<u8> g = good;
        wr<u64>(g, 12, off);
        AUREA_CHECK(!ProjectSerializer::load_bytes(p, g.data(), g.size(), LoadOptions{}).ok());
        FileHeader fh;
        std::vector<SectionHeader> sh;
        const std::string path = test_path("indice");
        AUREA_CHECK(write_raw(path, g));
        (void)ProjectSerializer::peek(path, fh, sh);
        AUREA_CHECK(sh.empty());
        std::remove(path.c_str());
    }
    e.shutdown();
}

AUREA_TEST(Stability, CorruptMainOpensLastValidCopyAndKeepsTheBadFile) {
    const std::string path = test_path("recupera");
    remove_family(path);
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    build_rich(e);
    AUREA_CHECK(e.save_project(path.c_str()).ok());              // gravação 1
    const u32 layersV1 = layer_count(e);
    AUREA_CHECK(e.add_text("segunda gravacao").ok());
    AUREA_CHECK(e.save_project(path.c_str()).ok());              // gravação 2; a 1 vira .bak
    AUREA_CHECK(fileio::exists(path + ".bak"));
    AUREA_CHECK(!fileio::exists(path + ".tmp"));
    const std::vector<u8> good = read_file(path);
    const std::vector<u8> bak = read_file(path + ".bak");
    AUREA_CHECK(good.size() > 1000 && !bak.empty() && good != bak);
    e.shutdown();

    Rng rng(77);
    u32 scenarios = 0;
    std::vector<std::vector<u8>> bads;
    for (usize cut : {usize{0}, usize{10}, usize{64}, good.size() / 3, good.size() / 2, good.size() - 1}) {
        bads.emplace_back(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut));
    }
    {
        std::vector<u8> noise(good.size());
        for (u8& b : noise) b = static_cast<u8>(rng.u32v());
        bads.push_back(noise);
        std::vector<u8> flipped = good;
        flipped[flipped.size() / 2] ^= 0x5A;   // CRC pega
        bads.push_back(flipped);
    }
    for (const std::vector<u8>& bad : bads) {
        ++scenarios;
        AUREA_CHECK(write_raw(path, bad));
        AUREA_CHECK(write_raw(path + ".bak", bak));
        std::remove((path + ".corrompido").c_str());
        Engine f;
        AUREA_CHECK(f.initialize(headless()).ok());
        const Status s = f.load_project(path.c_str());
        AUREA_CHECK_MSG(s.ok(), "a ultima copia valida devia abrir");
        AUREA_CHECK((f.last_load_notice() & Engine::kLoadRecoveredCopy) != 0);
        AUREA_CHECK_EQ(layer_count(f), layersV1);
        // O principal ruim não some: fica em .corrompido.
        AUREA_CHECK(read_file(path + ".corrompido") == bad);
        AUREA_CHECK(f.read_status().dirty);
        // A próxima gravação NÃO gira o lixo para .bak.
        AUREA_CHECK(f.save_project(path.c_str()).ok());
        AUREA_CHECK(read_file(path + ".bak") == bak);
        Project check;
        AUREA_CHECK(ProjectSerializer::load(check, path, LoadOptions{}).ok());
        f.shutdown();
    }

    // Queda entre o fsync e o rename: o .tmp inteiro é o estado MAIS novo.
    {
        AUREA_CHECK(write_raw(path, bads[3]));
        AUREA_CHECK(write_raw(path + ".tmp", good));
        Engine f;
        AUREA_CHECK(f.initialize(headless()).ok());
        AUREA_CHECK(f.load_project(path.c_str()).ok());
        AUREA_CHECK_EQ(layer_count(f), layersV1 + 1);
        f.shutdown();
        std::remove((path + ".tmp").c_str());
    }

    // Nenhuma cópia válida: erro padronizado e o projeto aberto antes continua.
    {
        AUREA_CHECK(write_raw(path, bads[6]));
        std::remove((path + ".bak").c_str());
        Engine g;
        AUREA_CHECK(g.initialize(headless()).ok());
        AUREA_CHECK(g.new_project(320, 180, 30.0, "antes").ok());
        AUREA_CHECK(g.add_text("fica").ok());
        const Project* before = g.project();
        const Status s = g.load_project(path.c_str());
        AUREA_CHECK_EQ(s.code(), Errc::ProjectCorrupted);
        AUREA_CHECK(g.project() == before);
        AUREA_CHECK_EQ(layer_count(g), 1u);
        AUREA_CHECK_EQ(g.read_status().lastError, Errc::ProjectCorrupted);
        AUREA_CHECK(error_code_name(s.code()) == "PROJECT_CORRUPTED");
        AUREA_CHECK_EQ(g.load_project((path + ".nao_existe").c_str()).code(), Errc::NotFound);
        g.shutdown();
    }
    std::printf("(%u cenarios de principal ruim) ", scenarios);
    remove_family(path);
}

AUREA_TEST(Stability, FutureVersionIsRefusedWithoutTouchingAnyFile) {
    const std::string path = test_path("futuro");
    remove_family(path);
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    build_rich(e);
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.save_project(path.c_str()).ok());   // .bak existe e é válido
    const std::vector<u8> good = read_file(path);
    const std::vector<u8> bak = read_file(path + ".bak");
    e.shutdown();

    // (a) Cabeçalho exige leitor mais novo.
    std::vector<u8> a = good;
    wr<u16>(a, 6, 99);
    // (b) Seção Timeline de versão futura (o índice não entra no CRC).
    std::vector<u8> b = good;
    for (const SecRef& s : sections_of(b)) {
        if (s.kind == static_cast<u16>(SectionKind::Timeline)) wr<u32>(b, s.hdr + 2, 99);
    }
    for (const std::vector<u8>* f : {&a, &b}) {
        AUREA_CHECK(write_raw(path, *f));
        Engine g;
        AUREA_CHECK(g.initialize(headless()).ok());
        const Status s = g.load_project(path.c_str());
        AUREA_CHECK_EQ(s.code(), Errc::UnsupportedVersion);
        // Nada foi aberto da cópia mais velha nem regravado.
        AUREA_CHECK(read_file(path) == *f);
        AUREA_CHECK(read_file(path + ".bak") == bak);
        AUREA_CHECK(!fileio::exists(path + ".corrompido"));
        g.shutdown();
        Project p;
        AUREA_CHECK_EQ(ProjectSerializer::load(p, path, LoadOptions{}).code(), Errc::UnsupportedVersion);
    }
    // A Home ainda lê o título (só metadados) do arquivo com seção futura.
    LoadOptions meta;
    meta.metadataOnly = true;
    Project p;
    AUREA_CHECK(ProjectSerializer::load_bytes(p, b.data(), b.size(), meta).ok());
    remove_family(path);
}

AUREA_TEST(Stability, DiskFullAndIoFailuresKeepTheOriginalIntact) {
    const std::string path = test_path("disco_cheio");
    remove_family(path);
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    build_rich(e);
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    const std::vector<u8> original = read_file(path);
    const u32 layersBefore = layer_count(e);
    AUREA_CHECK(e.add_text("mudanca nao salva").ok());
    AUREA_CHECK(e.read_status().dirty);

    struct Case { fileio::Fault kind; u64 after; Errc expect; };
    const Case cases[] = {
        {fileio::Fault::DiskFullAfter, 0, Errc::StorageFull},
        {fileio::Fault::DiskFullAfter, 100, Errc::StorageFull},
        {fileio::Fault::DiskFullAfter, original.size() / 2, Errc::StorageFull},
        {fileio::Fault::FlushFails, 0, Errc::StorageFull},
        {fileio::Fault::OpenFails, 0, Errc::IoError},
        {fileio::Fault::RenameFails, 0, Errc::IoError},
    };
    for (const Case& c : cases) {
        fileio::FaultInjection fi;
        fi.kind = c.kind;
        fi.afterBytes = c.after;
        fi.pathContains = "disco_cheio";
        fileio::set_fault_injection(fi);
        const Status s = e.save_project(path.c_str());
        AUREA_CHECK(!s.ok());
        AUREA_CHECK_EQ(s.code(), c.expect);
        AUREA_CHECK_EQ(fileio::injected_failures(), 1u);
        AUREA_CHECK(read_file(path) == original);             // byte a byte
        AUREA_CHECK(!fileio::exists(path + ".tmp"));          // temporário não fica
        AUREA_CHECK(e.read_status().dirty);                   // o autosave tenta de novo
        AUREA_CHECK_EQ(e.save_stats().lastError, c.expect);
        Project p;
        LoadReport r;
        AUREA_CHECK(ProjectSerializer::load(p, path, LoadOptions{}, &r).ok() && r.clean());
    }
    fileio::clear_fault_injection();
    // Espaço de volta: grava e o que estava sujo entra.
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(!e.read_status().dirty);
    Engine g;
    AUREA_CHECK(g.initialize(headless()).ok());
    AUREA_CHECK(g.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(layer_count(g), layersBefore + 1);
    AUREA_CHECK_EQ(e.save_stats().failures, 6u);

    // O serializador direto (sem motor) segue a mesma regra.
    fileio::FaultInjection fi;
    fi.kind = fileio::Fault::DiskFullAfter;
    fi.afterBytes = 1;
    fileio::set_fault_injection(fi);
    const std::vector<u8> now = read_file(path);
    AUREA_CHECK_EQ(ProjectSerializer::save(*g.project(), path, SaveOptions{}).code(), Errc::StorageFull);
    AUREA_CHECK(read_file(path) == now);
    fileio::clear_fault_injection();
    g.shutdown();
    e.shutdown();
    remove_family(path);
}

AUREA_TEST(Stability, ConcurrentSavesAndEditsNeverCorruptTheFile) {
    // Autosave (IO), "Salvar" e ir para segundo plano chegam juntos: antes
    // escreviam intercalados no MESMO .tmp. Agora: uma gravação por vez, e o
    // arquivo final sempre abre limpo.
    const std::string path = test_path("concorrente");
    remove_family(path);
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    build_rich(e);
    std::atomic<bool> stop{false};
    std::atomic<u32> failures{0};
    auto saver = [&] {
        for (u32 i = 0; i < 25 && !stop.load(); ++i) {
            if (!e.save_project(path.c_str()).ok()) failures.fetch_add(1);
        }
    };
    std::thread a(saver), b(saver);
    for (u32 i = 0; i < 40; ++i) (void)e.add_text("editando durante a gravacao");
    a.join();
    b.join();
    stop.store(true);
    AUREA_CHECK_EQ(failures.load(), 0u);
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    Project p;
    LoadReport r;
    AUREA_CHECK(ProjectSerializer::load(p, path, LoadOptions{}, &r).ok() && r.clean());
    const Engine::SaveStats st = e.save_stats();
    std::printf("(51 gravacoes; lock %.3f ms max, escrita %.3f ms a ultima, %llu bytes) ",
                static_cast<f64>(st.maxLockNs) / 1e6, static_cast<f64>(st.lastWriteNs) / 1e6,
                static_cast<unsigned long long>(st.lastBytes));
    e.shutdown();
    remove_family(path);
}

AUREA_TEST(Stability, AutosaveWaitsForPlaybackScrubAndUndoGroups) {
    const std::string path = test_path("idle_autosave");
    remove_family(path);
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, "Autosave").ok());
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    const CommandType begin[] = {CommandType::PlaybackPlay, CommandType::PlaybackScrubBegin, CommandType::UndoBeginGroup};
    const CommandType end[] = {CommandType::PlaybackPause, CommandType::PlaybackScrubEnd, CommandType::UndoEndGroup};
    for (u32 i = 0; i < 3; ++i) {
        AUREA_CHECK(e.add_text("pending edit").ok());
        const auto original = read_file(path);
        const auto saves = e.save_stats().saves;
        Command command; command.type = begin[i];
        AUREA_CHECK_EQ(e.submit_commands(&command, 1), 1u);
        AUREA_CHECK(e.autosave_project().ok());
        AUREA_CHECK_EQ(e.save_stats().saves, saves);
        AUREA_CHECK(read_file(path) == original);
        AUREA_CHECK(e.read_status().dirty);
        command.type = end[i];
        AUREA_CHECK_EQ(e.submit_commands(&command, 1), 1u);
        AUREA_CHECK(e.autosave_project().ok());
        AUREA_CHECK_EQ(e.save_stats().saves, saves + 1);
        AUREA_CHECK(!e.read_status().dirty);
        AUREA_CHECK(read_file(path) != original);
        AUREA_CHECK(e.autosave_project().ok());
        AUREA_CHECK_EQ(e.save_stats().saves, saves + 1);
    }
    Project saved;
    AUREA_CHECK(ProjectSerializer::load(saved, path, LoadOptions{}).ok());
    AUREA_CHECK_EQ(layer_count(e), 3u);
    e.shutdown(); remove_family(path);
}

AUREA_TEST(Stability, SaveCompletionCannotAdoptAReplacedProject) {
    const std::string path = test_path("save_replaced_project");
    remove_family(path);
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, "Original").ok());
    AUREA_CHECK(e.add_text("original content").ok());
    fileio::FaultInjection fi;
    fi.kind = fileio::Fault::BeforeWrite;
    fi.pathContains = "save_replaced_project";
    fi.context = &e;
    fi.beforeWrite = [](void* context) {
        auto& engine = *static_cast<Engine*>(context);
        AUREA_CHECK(engine.new_project(640, 360, 24, "Replacement").ok());
        AUREA_CHECK(engine.add_text("unsaved replacement").ok());
    };
    fileio::set_fault_injection(fi);
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK_EQ(fileio::injected_failures(), 1u);
    fileio::clear_fault_injection();
    AUREA_CHECK(e.project()->path().empty());
    AUREA_CHECK(e.read_status().dirty);
    AUREA_CHECK_EQ(e.save_project().code(), Errc::InvalidState);
    Project saved;
    AUREA_CHECK(ProjectSerializer::load(saved, path, LoadOptions{}).ok());
    AUREA_CHECK(saved.metadata().title == "Original");
    e.shutdown();
    remove_family(path);
}

AUREA_TEST(Stability, CorruptJournalHeaderDoesNotAllocateWhatItDeclares) {
    // Bug real (P1): o leitor do journal alocava pelo cabeçalho — um bloco
    // corrompido declarando 4 GB de strings (ou 1M comandos) virava um vector
    // desse tamanho: OOM/abort na recuperação, justamente depois de uma queda.
    const std::string path = test_path("journal_ruim");
    std::remove(path.c_str());
    Command c;
    c.type = CommandType::LayerSetOpacity;
    AUREA_CHECK(ProjectSerializer::append_journal(path, &c, 1, nullptr, 0).ok());
    std::vector<u8> j = read_file(path);
    AUREA_CHECK(j.size() == 24 + sizeof(Command));
    for (const std::pair<u32, u32>& bad : {std::pair<u32, u32>{0u, 0xFFFFFFF0u}, std::pair<u32, u32>{1u << 20, 0u},
                                           std::pair<u32, u32>{0xFFFFFFFFu, 0xFFFFFFFFu}}) {
        std::vector<u8> f = j;
        std::vector<u8> tail(j.begin(), j.begin() + 24);
        wr<u32>(tail, 8, bad.first);
        wr<u32>(tail, 12, bad.second);
        f.insert(f.end(), tail.begin(), tail.end());   // bloco bom + bloco com cabeçalho absurdo
        AUREA_CHECK(write_raw(path, f));
        std::vector<Command> out;
        AUREA_CHECK(ProjectSerializer::read_journal(path, out).ok());
        AUREA_CHECK_EQ(out.size(), static_cast<usize>(1));   // o bloco bom anterior fica
    }
    std::remove(path.c_str());
}

AUREA_TEST(Stability, EditDuringWriteStaysDirty) {
    // mark_clean_if: uma edição que acontece entre a cópia (encode) e o fim da
    // escrita não pode ser dada como salva.
    auto r = Project::create_new(320, 180, 30.0, "sujo");
    Project p = std::move(*r);
    p.mark_dirty();
    const u64 gen = p.edit_generation();
    p.mark_dirty();                 // edição durante a escrita
    p.mark_clean_if(gen);
    AUREA_CHECK(p.dirty());
    p.mark_clean_if(p.edit_generation());
    AUREA_CHECK(!p.dirty());
}

AUREA_TEST(Stability, LayerReferencesSurviveReopenAfterReorderAndDelete) {
    // Bug real (P1): os ids de camada são refeitos na ordem VERTICAL ao abrir.
    // Pai e câmera ativa eram remapeados; track matte, legenda (camada de
    // origem) e texto no caminho não — depois de duplicar/reordenar/apagar,
    // reabrir apontava para OUTRA camada (ou nenhuma).
    const std::string path = test_path("referencias");
    remove_family(path);
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, "refs").ok());
    auto named = [&](u64 id, const char* name) {
        Project* p = e.project();
        Composition* c = p->timeline().composition(p->timeline().current());
        if (Layer* l = c->layer(LayerId::unpack(id))) l->name = name;
    };
    const u64 a = *e.add_text("a");
    named(a, "A");
    const u64 gone = *e.add_text("apagada");
    const u64 b = *e.add_text("b");
    named(b, "B");
    const u64 c = *e.add_text("c");
    named(c, "C");
    const u64 v = *e.add_vector_layer(0);
    named(v, "V");
    Command dup;
    dup.type = CommandType::LayerDuplicate;
    dup.layer_ref.layer = LayerId::unpack(a);
    AUREA_CHECK(e.apply_command(dup).ok());
    Command del;
    del.type = CommandType::LayerDelete;
    del.layer_ref.layer = LayerId::unpack(gone);
    AUREA_CHECK(e.apply_command(del).ok());
    Command reorder;
    reorder.type = CommandType::LayerReorder;
    reorder.layer_reorder.layer = LayerId::unpack(c);
    reorder.layer_reorder.newIndex = 0;
    AUREA_CHECK(e.apply_command(reorder).ok());
    AUREA_CHECK(e.set_track_matte(c, b, 1));
    AUREA_CHECK(e.set_text_path(a, v, 0.0f, false, false));
    {
        Project* p = e.project();
        Composition* comp = p->timeline().composition(p->timeline().current());
        comp->layer(LayerId::unpack(b))->text.captionSource = v;
    }
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    e.shutdown();

    Engine f;
    AUREA_CHECK(f.initialize(headless()).ok());
    AUREA_CHECK(f.load_project(path.c_str()).ok());
    const Project* p = f.project();
    const Composition* comp = p->timeline().composition(p->timeline().current());
    auto by_name = [&](const char* name) -> const Layer* {
        const Layer* found = nullptr;
        comp->layers().for_each([&](LayerId, const Layer& l) { if (!found && l.name == name) found = &l; });
        return found;
    };
    auto name_of = [&](u64 id) -> std::string {
        const Layer* l = comp->layer(LayerId::unpack(id));
        return l ? l->name : std::string("<nenhuma>");
    };
    const Layer* C = by_name("C");
    const Layer* A = by_name("A");
    const Layer* B = by_name("B");
    AUREA_CHECK(C && A && B);
    if (C && A && B) {
        AUREA_CHECK_EQ(name_of(C->matteSource.pack()), std::string("B"));
        AUREA_CHECK_EQ(name_of(A->text.pathLayer), std::string("V"));
        AUREA_CHECK_EQ(name_of(B->text.captionSource), std::string("V"));
    }
    f.shutdown();
    remove_family(path);
}

AUREA_TEST(Stability, PrecompLinksSurviveReopenAfterACompositionIsDeleted) {
    // Mesmo defeito, nível de composição: apagar uma composição deixa buraco
    // de slot; ao reabrir, as seguintes mudavam de id e a pré-composição
    // apontava para nada (abria vazia).
    const std::string path = test_path("precomp");
    remove_family(path);
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, "precomp").ok());
    const u64 t1 = *e.add_text("um");
    const u64 t2 = *e.add_text("dois");
    const u64 p1 = *e.precompose(&t1, 1, "Primeira");
    const u64 p2 = *e.precompose(&t2, 1, "Segunda");
    CompositionId firstComp{}, secondComp{};
    {
        Project* p = e.project();
        Composition* root = p->timeline().composition(p->timeline().current());
        firstComp = root->layer(LayerId::unpack(p1))->nested.composition;
        secondComp = root->layer(LayerId::unpack(p2))->nested.composition;
    }
    Command del;
    del.type = CommandType::LayerDelete;
    del.layer_ref.layer = LayerId::unpack(p1);
    AUREA_CHECK(e.apply_command(del).ok());
    Command delComp;
    delComp.type = CommandType::CompositionDelete;
    delComp.comp_ref.comp = firstComp;
    (void)e.apply_command(delComp);
    const bool firstGone = e.project()->timeline().composition(firstComp) == nullptr;
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    e.shutdown();

    Engine f;
    AUREA_CHECK(f.initialize(headless()).ok());
    AUREA_CHECK(f.load_project(path.c_str()).ok());
    const Project* p = f.project();
    const Composition* root = p->timeline().composition(p->timeline().current());
    AUREA_CHECK(root != nullptr && root == p->timeline().composition(p->timeline().root()));
    u32 links = 0, alive = 0;
    root->layers().for_each([&](LayerId, const Layer& l) {
        if (l.kind != LayerKind::Composition) return;
        ++links;
        const Composition* c = p->timeline().composition(l.nested.composition);
        if (c && c->name() == "Segunda") ++alive;
    });
    AUREA_CHECK_EQ(links, 1u);
    AUREA_CHECK_EQ(alive, 1u);
    std::printf("(composicao apagada antes: %s) ", firstGone ? "sim" : "nao");
    (void)secondComp;
    f.shutdown();
    remove_family(path);
}

// =============================================================================
// Migração (§123–124)
// =============================================================================

AUREA_TEST(Stability, OldFormatProjectsOpenAndKeepARecoveryCopy) {
    struct Fixture { const u8* data; usize size; u32 version; };
    const Fixture fixtures[] = {
        {kOldProjectTimelineV2, sizeof(kOldProjectTimelineV2), 2},
        {kOldProjectTimelineV12, sizeof(kOldProjectTimelineV12), 12},
    };
    for (const Fixture& fx : fixtures) {
        const std::string path = test_path("antigo");
        remove_family(path);
        const std::vector<u8> original(fx.data, fx.data + fx.size);
        AUREA_CHECK(write_raw(path, original));
        Engine e;
        AUREA_CHECK(e.initialize(headless()).ok());
        const Status s = e.load_project(path.c_str());
        AUREA_CHECK_MSG(s.ok(), "projeto de formato antigo devia abrir");
        AUREA_CHECK((e.last_load_notice() & Engine::kLoadOlderFormat) != 0);
        AUREA_CHECK(layer_count(e) > 0);
        const std::string copy = path + ".v" + std::to_string(fx.version) + ".bak";
        AUREA_CHECK(read_file(copy) == original);
        // Regravar no formato novo: a cópia antiga fica como estava.
        AUREA_CHECK(e.save_project(path.c_str()).ok());
        AUREA_CHECK(read_file(copy) == original);
        Project p;
        LoadReport r;
        AUREA_CHECK(ProjectSerializer::load(p, path, LoadOptions{}, &r).ok());
        AUREA_CHECK(!r.olderFormat);
        const u32 layers = layer_count(e);
        e.shutdown();
        Engine f;
        AUREA_CHECK(f.initialize(headless()).ok());
        AUREA_CHECK(f.load_project(path.c_str()).ok());
        AUREA_CHECK((f.last_load_notice() & Engine::kLoadOlderFormat) == 0);
        AUREA_CHECK_EQ(layer_count(f), layers);
        f.shutdown();
        std::printf("(v%u: %u camadas) ", fx.version, layers);
        remove_family(path);
    }
}

// =============================================================================
// Mídia faltando ou corrompida (§119–122)
// =============================================================================

namespace {

/// Fábrica de mídia que recusa: arquivo que não sonda, vídeo sem dimensões,
/// áudio sem duração, decoder que não abre (arquivo sumiu depois do import).
class BrokenFactory final : public VideoSourceFactory {
public:
    bool probe(const char* path, MediaProbe& out) override {
        const std::string p = path ? path : "";
        if (p == "corrompido") return false;
        out = MediaProbe{};
        if (p == "sem_dimensoes") { out.hasVideo = true; return true; }
        if (p == "audio_sem_duracao") { out.hasAudio = true; out.audioSampleRate = 48000; out.audioChannels = 2; return true; }
        SyntheticFactory ok(SyntheticConfig{});
        return ok.probe(path, out);
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override { return nullptr; }
};

bool image_loader_fails(const char*, ImagePixels&, void*) { return false; }

} // namespace

AUREA_TEST(Stability, CorruptImportsFailWithStandardCodeAndChangeNothing) {
    BrokenFactory factory;
    EngineConfig cfg = headless();
    cfg.mediaFactory = &factory;
    Engine e;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, "import").ok());

    auto video = [&](const char* p) { VideoImport v; v.sourcePath = p; v.displayName = "x"; return v; };
    AUREA_CHECK_EQ(e.import_video(video("corrompido")).code(), Errc::UnsupportedFormat);
    AUREA_CHECK_EQ(e.import_video(video("sem_dimensoes")).code(), Errc::AssetCorrupted);
    AUREA_CHECK_EQ(e.import_audio(video("audio_sem_duracao")).code(), Errc::AssetCorrupted);
    AUREA_CHECK_EQ(e.import_audio(video("corrompido")).code(), Errc::UnsupportedFormat);
    AUREA_CHECK_EQ(layer_count(e), 0u);
    AUREA_CHECK(!e.import_image(nullptr, 0, 0, "vazia").ok());

    // Modelos 3D quebrados: lixo, glTF com buffer impossível, GLB truncado.
    Rng rng(3141);
    u32 refused = 0, files = 0;
    auto try_model = [&](const std::string& name, const std::vector<u8>& bytes) {
        AUREA_CHECK(write_raw(name, bytes));
        ++files;
        ModelImport m;
        m.path = name;
        std::string detail;
        const auto r = e.import_model(m, nullptr, &detail);
        if (!r.ok()) {
            ++refused;
            AUREA_CHECK(r.code() != Errc::Ok);
        }
        std::remove(name.c_str());
    };
    const std::string gltfBad = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":999999999999,"uri":"nao_existe.bin"}],)"
                                R"("bufferViews":[{"buffer":0,"byteLength":999999999999}],)"
                                R"("accessors":[{"bufferView":0,"componentType":5126,"count":4294967295,"type":"VEC3"}],)"
                                R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}]})";
    try_model("aurea_estab_ruim.gltf", std::vector<u8>(gltfBad.begin(), gltfBad.end()));
    {
        std::vector<u8> glb(64, 0);
        std::memcpy(glb.data(), "glTF", 4);
        wr<u32>(glb, 4, 2);
        wr<u32>(glb, 8, 0x7FFFFFFF);   // comprimento declarado absurdo
        wr<u32>(glb, 12, 0xFFFFFF00);  // chunk JSON gigante
        std::memcpy(glb.data() + 16, "JSON", 4);
        try_model("aurea_estab_trunc.glb", glb);
    }
    for (u32 i = 0; i < 150; ++i) {
        std::vector<u8> junk(16 + rng.below(3000));
        for (u8& b : junk) b = static_cast<u8>(rng.u32v());
        if (i % 3 == 0) { std::memcpy(junk.data(), "glTF", 4); wr<u32>(junk, 4, 2); wr<u32>(junk, 8, static_cast<u32>(junk.size())); }
        const char* ext = (i % 4 == 0) ? ".gltf" : (i % 4 == 1) ? ".fbx" : (i % 4 == 2) ? ".obj" : ".glb";
        try_model(std::string("aurea_estab_lixo") + ext, junk);
    }
    AUREA_CHECK_EQ(refused, files);
    AUREA_CHECK_EQ(layer_count(e), 0u);
    AUREA_CHECK(!e.import_font("aurea_estab_fonte_que_nao_existe.ttf").ok());
    {
        std::vector<u8> junk(2048);
        for (u8& b : junk) b = static_cast<u8>(rng.u32v());
        AUREA_CHECK(write_raw("aurea_estab_lixo.ttf", junk));
        AUREA_CHECK(!e.import_font("aurea_estab_lixo.ttf").ok());
        AUREA_CHECK(write_raw("aurea_estab_lixo.hdr", junk));
        AUREA_CHECK(!e.import_hdri("aurea_estab_lixo.hdr").ok());
        std::remove("aurea_estab_lixo.ttf");
        std::remove("aurea_estab_lixo.hdr");
    }
    std::printf("(%u modelos quebrados recusados) ", refused);
    e.shutdown();
}

AUREA_TEST(Stability, MissingMediaOpensWithPlaceholderAndNotice) {
    const std::string path = test_path("midia_faltando");
    remove_family(path);
    BrokenFactory factory;
    EngineConfig cfg = headless();
    cfg.mediaFactory = &factory;
    cfg.imageLoader = &image_loader_fails;
    {
        Engine e;
        AUREA_CHECK(e.initialize(cfg).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, "faltando").ok());
        std::vector<u8> px(4 * 4 * 4, 200);
        AUREA_CHECK(e.import_image(px.data(), 4, 4, "foto", "content://sumiu/foto.jpg").ok());
        VideoImport v;
        v.sourcePath = "video_que_sumiu.mp4";
        v.displayName = "video";
        AUREA_CHECK(e.import_video(v).ok());       // o decoder nunca abre (arquivo sumiu depois)
        auto text = e.add_text("com fonte que sumiu");
        AUREA_CHECK(text.ok());
        {
            Project* p = e.project();
            Composition* c = p->timeline().composition(p->timeline().current());
            if (Layer* l = c->layer(LayerId::unpack(*text))) l->text.fontPath = "aurea_estab_fonte_sumiu.ttf";
            Asset a;
            a.kind = AssetKind::Model3D;
            a.name = "modelo";
            a.sourcePath = "aurea_estab_modelo_sumiu.glb";
            const AssetId mid = p->add_asset(std::move(a));
            const LayerId lid = c->add_layer(LayerKind::Model3D, "modelo");
            if (Layer* l = c->layer(lid)) l->model.scene = mid;
        }
        AUREA_CHECK(e.render_frame().ok());      // sem GPU: drena e avança sem cair
        AUREA_CHECK(e.save_project(path.c_str()).ok());
        e.shutdown();
    }
    Engine f;
    AUREA_CHECK(f.initialize(cfg).ok());
    AUREA_CHECK(f.load_project(path.c_str()).ok());
    AUREA_CHECK((f.last_load_notice() & Engine::kLoadMissingMedia) != 0);
    AUREA_CHECK_EQ(f.last_load_missing_assets(), 3u);   // imagem + fonte + modelo
    AUREA_CHECK_EQ(layer_count(f), 4u);                 // nenhuma camada some: o espaço fica
    AUREA_CHECK(f.render_frame().ok());
    // Salvar de novo não perde a referência (é o que permite religar depois).
    AUREA_CHECK(f.save_project(path.c_str()).ok());
    Project p;
    AUREA_CHECK(ProjectSerializer::load(p, path, LoadOptions{}).ok());
    u32 withSource = 0;
    p.for_each_asset([&](AssetId, const Asset& a) { if (!a.sourcePath.empty()) ++withSource; });
    AUREA_CHECK_EQ(withSource, 3u);
    f.shutdown();
    remove_family(path);
}

// =============================================================================
// Fuzz (§134)
// =============================================================================

AUREA_TEST(Fuzz, ProjectParserWithValidChecksums) {
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    build_rich(e);
    std::vector<u8> base;
    AUREA_CHECK(ProjectSerializer::encode(*e.project(), SaveOptions{}, base).ok());
    const std::vector<SecRef> secs = sections_of(base);
    AUREA_CHECK_EQ(secs.size(), static_cast<usize>(3));
    e.shutdown();

    Rng rng(0xF022);
    const u32 iterations = 6000;
    u32 opened = 0, roundTrips = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (u32 it = 0; it < iterations; ++it) {
        std::vector<u8> f = base;
        const u32 mutations = 1 + rng.below(8);
        for (u32 m = 0; m < mutations; ++m) {
            const SecRef& s = secs[rng.below(static_cast<u32>(secs.size()))];
            if (s.size < 8) continue;
            const usize at = static_cast<usize>(s.offset + rng.below(static_cast<u32>(s.size - 4)));
            switch (rng.below(6)) {
                case 0: f[at] ^= static_cast<u8>(1u << rng.below(8)); break;
                case 1: f[at] = static_cast<u8>(rng.u32v()); break;
                case 2: wr<u32>(f, at, 0xFFFFFFFFu); break;
                case 3: wr<u32>(f, at, 0x7FFFFFFFu); break;
                case 4: wr<u32>(f, at, rng.below(70000)); break;
                case 5: { f32 v = wild_float(rng); std::memcpy(f.data() + at, &v, 4); break; }
            }
        }
        fix_crcs(f);
        Project p;
        LoadReport r;
        if (!ProjectSerializer::load_bytes(p, f.data(), f.size(), LoadOptions{}, &r).ok()) continue;
        ++opened;
        // O que abriu tem de regravar e reabrir: um projeto que abre mas não
        // salva é perda de dados adiada.
        std::vector<u8> again;
        AUREA_CHECK(ProjectSerializer::encode(p, SaveOptions{}, again).ok());
        Project q;
        if (ProjectSerializer::load_bytes(q, again.data(), again.size(), LoadOptions{}).ok()) ++roundTrips;
        // E o desfazer consegue copiar/estimar a composição lida.
        p.timeline().for_each_composition([](CompositionId, const Composition& c) {
            (void)History::estimate_bytes(c);
            (void)c.clone();
        });
    }
    AUREA_CHECK_EQ(roundTrips, opened);
    std::printf("(%u mutacoes com CRC refeito, %u abriram, %.0f ms) ", iterations, opened, ms_since(t0));
}

AUREA_TEST(Fuzz, FuzzedProjectsOpenSaveAndRenderInTheEngine) {
    // Uma amostra dos projetos mutados passa pelo caminho inteiro do app:
    // Engine::load_project (migrações, mídia), render sem GPU, desfazer, salvar.
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    build_rich(e);
    std::vector<u8> base;
    AUREA_CHECK(ProjectSerializer::encode(*e.project(), SaveOptions{}, base).ok());
    const std::vector<SecRef> secs = sections_of(base);
    e.shutdown();
    const std::string path = test_path("fuzz_motor");
    Rng rng(0xBEEF);
    u32 loaded = 0;
    Engine g;
    AUREA_CHECK(g.initialize(headless()).ok());
    for (u32 it = 0; it < 300; ++it) {
        std::vector<u8> f = base;
        for (u32 m = 0; m < 1 + rng.below(4); ++m) {
            const SecRef& s = secs[1];   // a Timeline é onde mora o risco
            const usize at = static_cast<usize>(s.offset + rng.below(static_cast<u32>(s.size - 4)));
            if (rng.below(2)) { f32 v = wild_float(rng); std::memcpy(f.data() + at, &v, 4); }
            else f[at] = static_cast<u8>(rng.u32v());
        }
        fix_crcs(f);
        remove_family(path);
        AUREA_CHECK(write_raw(path, f));
        if (!g.load_project(path.c_str()).ok()) continue;
        ++loaded;
        for (i64 frame : {0, 7, 30, 59}) {
            Command seek;
            seek.type = CommandType::PlaybackSeek;
            seek.seek.time = tick_at(FrameIndex{frame}, 30.0);
            (void)g.apply_command(seek);
            (void)g.render_frame();
        }
        const std::vector<u64> ids = layer_ids(g);
        if (!ids.empty()) {
            Command op;
            op.type = CommandType::LayerSetOpacity;
            op.opacity.layer = LayerId::unpack(ids[0]);
            op.opacity.opacity = 0.5f;
            (void)g.apply_command(op);
            Command undo;
            undo.type = CommandType::Undo;
            (void)g.apply_command(undo);
        }
        AUREA_CHECK(g.save_project(path.c_str()).ok());
    }
    g.shutdown();
    std::printf("(300 projetos mutados, %u abriram no motor) ", loaded);
    remove_family(path);
}

AUREA_TEST(Fuzz, RandomCommandsThroughTheQueueNeverCrash) {
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    build_rich(e);
    Rng rng(0xC0FFEE);
    const u32 maxType = static_cast<u32>(CommandType::ShapeSetParam) + 4;   // inclui tipos que não existem
    const u32 total = 20000;
    u32 sent = 0;
    char blob[96];
    for (u32 i = 0; i < sizeof(blob); ++i) blob[i] = static_cast<char>('a' + i % 26);
    const auto t0 = std::chrono::steady_clock::now();
    for (u32 batch = 0; batch < total / 50; ++batch) {
        std::vector<u64> ids = layer_ids(e);
        Command cmds[50];
        for (Command& c : cmds) {
            c = Command{};
            c.type = static_cast<CommandType>(rng.below(maxType + 1));
            // Payload: bytes aleatórios, depois campos "quase certos".
            u8* raw = reinterpret_cast<u8*>(&c.raw);
            for (usize b = 0; b < bridge::command_layout::kPayloadSize; ++b) raw[b] = static_cast<u8>(rng.u32v());
            f32* fl = reinterpret_cast<f32*>(raw + 8);
            for (u32 k = 0; k < 14 && 8 + 4 * (k + 1) <= bridge::command_layout::kPayloadSize; ++k) {
                if (rng.below(2)) fl[k] = wild_float(rng);
            }
            if (!ids.empty() && rng.below(10) < 7) {
                const u64 id = ids[rng.below(static_cast<u32>(ids.size()))];
                std::memcpy(raw, &id, 8);   // quase todo payload começa com o LayerId
            }
            // Strings: às vezes válidas, às vezes com offset/comprimento que dão a volta em u32.
            switch (rng.below(4)) {
                case 0: c.stringOffset = 0; c.stringLength = 0; break;
                case 1: c.stringOffset = rng.below(64); c.stringLength = rng.below(32); break;
                case 2: c.stringOffset = 0xFFFFFFF0u; c.stringLength = 0x20u; break;
                case 3: c.stringOffset = rng.u32v(); c.stringLength = rng.u32v(); break;
            }
            // Desfazer/refazer e grupos entram de verdade de vez em quando.
            if (rng.below(40) == 0) c.type = rng.below(2) ? CommandType::Undo : CommandType::Redo;
            // Exportar/playback em loop não interessam aqui (threads próprias).
            if (c.type == CommandType::ExportRequest || c.type == CommandType::PlaybackPlay
                || c.type == CommandType::PlaybackToggle) c.type = CommandType::Nop;
        }
        sent += e.submit_commands(cmds, 50, rng.below(3) ? blob : nullptr, rng.below(3) ? sizeof(blob) : 0);
        (void)e.render_frame();
        // Também o atalho direto (testes/recuperação), com string C.
        Command d = cmds[rng.below(50)];
        (void)e.apply_command(d, rng.below(2) ? "texto" : nullptr);
    }
    AUREA_CHECK(sent > 0);
    // O projeto sobrevivente grava, reabre e desfaz tudo sem cair.
    const std::string path = test_path("fuzz_cmd");
    remove_family(path);
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    Engine f;
    AUREA_CHECK(f.initialize(headless()).ok());
    AUREA_CHECK(f.load_project(path.c_str()).ok());
    Command undo;
    undo.type = CommandType::Undo;
    for (u32 i = 0; i < 1100 && e.read_status().canUndo; ++i) (void)e.apply_command(undo);
    std::printf("(%u comandos, %u aceitos pela fila, %.0f ms) ", total, sent, ms_since(t0));
    f.shutdown();
    e.shutdown();
    remove_family(path);
}

AUREA_TEST(Fuzz, EffectParametersWithWildValuesPlanWithoutCrash) {
    // Sem GPU: o planejamento (valores, identidade, operação de cor, margens)
    // é CPU pura e roda para todo efeito com NaN, infinito e gigantes.
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, "efeitos").ok());
    const EffectRegistry& reg = e.effects();
    Rng rng(0xEFF);
    u32 plans = 0;
    for (u32 t = 0; t < reg.count(); ++t) {
        Layer layer;
        layer.kind = LayerKind::Shape;
        EffectInstance inst;
        inst.id = 1;
        inst.type = reg.at(t).type_id();
        initialize_instance(inst, reg.params_at(t));
        layer.effects.push_back(inst);
        for (u32 it = 0; it < 60; ++it) {
            for (ParamSlot& s : layer.effects[0].params) {
                for (f32& v : s.constant.v) v = wild_float(rng);
            }
            LayerPlacement pl;
            pl.compWidth = 320;
            pl.compHeight = 180;
            EffectPlan plan;
            EffectGraph::plan(layer, reg, FrameIndex{static_cast<i64>(it)}, 1.0f, pl, nullptr, plan);
            ++plans;
        }
    }
    std::printf("(%u efeitos x 60 valores = %u planos) ", reg.count(), plans);
    AUREA_CHECK(plans > 0);
    e.shutdown();
}

#if defined(AUREA_TEST_VULKAN)
AUREA_TEST(Fuzz, EffectParametersWithWildValuesRenderOnGpu) {
    // Com GPU: cada efeito montado e desenhado com valores extremos. Falha de
    // montagem vira bypass contado (§117), nunca quadro perdido nem crash.
    SyntheticConfig vc;
    vc.width = 64;
    vc.height = 36;
    SyntheticFactory factory(vc);
    Engine e;
    EngineConfig ec = headless();
    ec.backend = new vk::Backend();
    ec.backendConfig.framesInFlight = 2;
    ec.mediaFactory = &factory;
    AUREA_CHECK(e.initialize(ec).ok());
    if (!e.gpu()) {
        std::printf("(sem GPU Vulkan: pulado) ");
        e.shutdown();
        return;
    }
    AUREA_CHECK(e.new_project(160, 90, 30.0, "efeitos gpu").ok());
    VideoImport v;
    v.sourcePath = "sintetico";
    v.displayName = "clipe";
    auto layer = e.import_video(v);
    AUREA_CHECK(layer.ok());
    TextureDesc d;
    d.width = 160;
    d.height = 90;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    const TextureHandle target = *e.gpu()->create_texture(d);
    const EffectRegistry& reg = e.effects();
    Rng rng(0x6E0);
    u32 frames = 0, failed = 0, slow = 0;
    f64 worstMs = 0.0;
    const u64 bypassBefore = EffectGraph::bypassed_total();
    for (u32 t = 0; t < reg.count(); ++t) {
        {
            Project* p = e.project();
            Composition* c = p->timeline().composition(p->timeline().current());
            Layer* l = c->layer(LayerId::unpack(*layer));
            l->effects.clear();
        }
        AUREA_CHECK(e.apply_command(effect_add(*layer, reg.at(t).type_id())).ok());
        for (u32 it = 0; it < 6; ++it) {
            {
                Project* p = e.project();
                Composition* c = p->timeline().composition(p->timeline().current());
                Layer* l = c->layer(LayerId::unpack(*layer));
                // Direto no modelo: é o que uma expressão pode produzir (NaN,
                // inf), coisa que o comando validado não deixa entrar.
                for (ParamSlot& s : l->effects[0].params) {
                    for (f32& val : s.constant.v) val = wild_float(rng);
                }
            }
            Command bump;   // revisão nova: o render não reaproveita o quadro anterior
            bump.type = CommandType::LayerSetOpacity;
            bump.opacity.layer = LayerId::unpack(*layer);
            bump.opacity.opacity = it % 2 ? 1.0f : 0.99f;
            AUREA_CHECK(e.apply_command(bump).ok());
            const auto t0 = std::chrono::steady_clock::now();
            if (!e.render_offscreen(target, 160, 90).ok()) ++failed;
            const f64 ms = ms_since(t0);
            worstMs = std::max(worstMs, ms);
            if (ms > 250.0) {
                // Quadro que trava a GPU por valor extremo: vira item da lista de bugs.
                ++slow;
                const Project* p = e.project();
                const Composition* c = p->timeline().composition(p->timeline().current());
                const Layer* l = c->layer(LayerId::unpack(*layer));
                std::printf("\n      LENTO %.0f ms: %s [", ms, reg.at(t).info().key);
                for (const ParamSlot& s : l->effects[0].params) std::printf("%g ", static_cast<f64>(s.constant.v[0]));
                std::printf("]");
            }
            ++frames;
        }
    }
    e.gpu()->destroy_texture(target);
    std::printf("(%u efeitos, %u quadros, %u recusados, %llu bypass, pior %.1f ms) ", reg.count(), frames, failed,
                static_cast<unsigned long long>(EffectGraph::bypassed_total() - bypassBefore), worstMs);
    AUREA_CHECK_EQ(failed, 0u);
    e.shutdown();
}
#endif

// =============================================================================
// Desfazer (§125–126)
// =============================================================================

AUREA_TEST(Stability, ThousandUndoRedoStepsAreConsistent) {
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, "desfazer").ok());
    for (u32 i = 0; i < 12; ++i) AUREA_CHECK(e.add_text("camada").ok());
    e.history().clear();
    e.history().set_budget_bytes(512ull << 20);   // aqui o teste é a consistência, não o teto

    Rng rng(1000);
    const EffectRegistry& reg = e.effects();
    std::vector<std::vector<u8>> states;
    states.push_back(timeline_bytes(*e.project()));
    u32 unrecorded = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (states.size() < 1001) {
        const std::vector<u64> ids = layer_ids(e);
        const u64 id = ids[rng.below(static_cast<u32>(ids.size()))];
        Command c;
        switch (rng.below(8)) {
            case 0: c.type = CommandType::LayerSetOpacity; c.opacity.layer = LayerId::unpack(id); c.opacity.opacity = rng.unit(); break;
            case 1: c.type = CommandType::LayerSetPosition; c.position.layer = LayerId::unpack(id);
                    c.position.x = rng.unit() * 640; c.position.y = rng.unit() * 360; c.position.z = 0; break;
            case 2: c = keyframe(id, TrackProperty::Opacity, rng.below(90), rng.unit()); break;
            case 3: c = keyframe(id, TrackProperty::RotationZ, rng.below(90), rng.unit() * 360); break;
            case 4: c = effect_add(id, reg.at(rng.below(reg.count())).type_id()); break;
            case 5: c.type = CommandType::LayerDuplicate; c.layer_ref.layer = LayerId::unpack(id); break;
            case 6: c.type = CommandType::LayerSetTimeRange; c.layer_range.layer = LayerId::unpack(id);
                    c.layer_range.start = FrameIndex{rng.below(30)}; c.layer_range.end = FrameIndex{40 + rng.below(50)}; break;
            case 7: c.type = CommandType::LayerSetVisible; c.layer_visible.layer = LayerId::unpack(id);
                    c.layer_visible.visible = rng.below(2) != 0; break;
        }
        if (ids.size() > 40 && c.type == CommandType::LayerDuplicate) continue;
        const u32 depthBefore = e.history().depth();
        const bool couldRedo = e.history().can_redo();
        (void)couldRedo;
        (void)e.apply_command(c);
        std::vector<u8> now = timeline_bytes(*e.project());
        if (e.history().depth() == depthBefore + 1) {
            states.push_back(std::move(now));
        } else if (now != states.back()) {
            ++unrecorded;   // mudou sem entrar no desfazer: seria uma ação irreversível
            states.back() = std::move(now);
        }
    }
    AUREA_CHECK_EQ(unrecorded, 0u);
    AUREA_CHECK_EQ(e.history().depth(), 1000u);
    const f64 doMs = ms_since(t0);

    Command undo;
    undo.type = CommandType::Undo;
    Command redo;
    redo.type = CommandType::Redo;
    u32 undoMismatch = 0, redoMismatch = 0;
    const auto t1 = std::chrono::steady_clock::now();
    for (usize i = 1000; i-- > 0;) {
        AUREA_CHECK(e.apply_command(undo).ok());
        if (timeline_bytes(*e.project()) != states[i]) ++undoMismatch;
    }
    AUREA_CHECK(!e.history().can_undo());
    for (usize i = 1; i <= 1000; ++i) {
        AUREA_CHECK(e.apply_command(redo).ok());
        if (timeline_bytes(*e.project()) != states[i]) ++redoMismatch;
    }
    AUREA_CHECK_EQ(undoMismatch, 0u);
    AUREA_CHECK_EQ(redoMismatch, 0u);
    std::printf("(1000 acoes em %.0f ms; 1000 desfazer + 1000 refazer em %.0f ms; historico ~%.1f MB) ", doMs,
                ms_since(t1), static_cast<f64>(e.history().bytes()) / (1024.0 * 1024.0));
    e.shutdown();
}

AUREA_TEST(Stability, UndoHistoryRespectsItsMemoryBudget) {
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, "orcamento").ok());
    // Composição pesada: 300 camadas × 200 keyframes ≈ MBs por snapshot.
    for (u32 i = 0; i < 300; ++i) {
        auto id = e.add_text("pesada");
        AUREA_CHECK(id.ok());
        Project* p = e.project();
        Composition* c = p->timeline().composition(p->timeline().current());
        Track& t = c->layer(LayerId::unpack(*id))->tracks.get_or_create(TrackProperty::Opacity);
        for (i64 f = 0; f < 200; ++f) (void)t.set(FrameIndex{f}, 0.5f, Interpolation::Linear);
    }
    const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const u64 snap = History::estimate_bytes(*comp);
    e.history().clear();
    const u64 budget = snap * 5 + snap / 2;   // cabem 5 snapshots
    e.history().set_budget_bytes(budget);
    const std::vector<u64> ids = layer_ids(e);
    for (u32 i = 0; i < 40; ++i) {
        Command c;
        c.type = CommandType::LayerSetOpacity;
        c.opacity.layer = LayerId::unpack(ids[i % ids.size()]);
        c.opacity.opacity = static_cast<f32>(i) / 40.0f;
        AUREA_CHECK(e.apply_command(c).ok());
        AUREA_CHECK(e.history().bytes() <= budget);
    }
    AUREA_CHECK_EQ(e.history().depth(), 5u);
    AUREA_CHECK(e.read_status().canUndo);
    // Um único snapshot acima do teto: a última ação continua desfazível.
    e.history().set_budget_bytes(snap / 2);
    AUREA_CHECK_EQ(e.history().depth(), 1u);
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    std::printf("(snapshot ~%.2f MB; teto respeitado com 40 acoes) ", static_cast<f64>(snap) / (1024.0 * 1024.0));
    e.shutdown();
}

// =============================================================================
// Projeto extremo (§127)
// =============================================================================

AUREA_TEST(Stability, ExtremeProjectOpensSavesAndRenders) {
    SyntheticConfig vc;
    vc.width = 64;
    vc.height = 36;
    vc.frameCount = 9000;   // 5 min: as 600 palavras de legenda cabem no vídeo
    vc.audioRate = 0;
    SyntheticFactory factory(vc);
    EngineConfig ec = headless();
    ec.mediaFactory = &factory;
#if defined(AUREA_TEST_VULKAN)
    ec.backend = new vk::Backend();
    ec.backendConfig.framesInFlight = 2;
#endif
    Engine e;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 30.0, "extremo").ok());
    const auto tBuild = std::chrono::steady_clock::now();

    std::vector<u64> videos, texts, all;
    for (u32 i = 0; i < 100; ++i) {
        VideoImport v;
        v.sourcePath = "sintetico_" + std::to_string(i);
        v.displayName = "video " + std::to_string(i);
        auto r = e.import_video(v);
        AUREA_CHECK(r.ok());
        if (r.ok()) videos.push_back(*r);
    }
    for (u32 i = 0; i < 100; ++i) if (auto r = e.add_text("Texto extremo"); r.ok()) texts.push_back(*r);
    u32 made[5] = {};
    for (u32 i = 0; i < 20; ++i) made[0] += e.add_particles(i % 3).ok() ? 1u : 0u;
    for (u32 i = 0; i < 120; ++i) made[1] += e.add_shape(i % 4).ok() ? 1u : 0u;
    for (u32 i = 0; i < 20; ++i) made[2] += e.add_vector_layer(i % 3).ok() ? 1u : 0u;
    for (u32 i = 0; i < 20; ++i) made[3] += e.add_null(i % 2 == 0).ok() ? 1u : 0u;
    // Legendas de verdade (agrupadas) sobre o primeiro vídeo: 600 palavras.
    {
        std::vector<text::CaptionWord> words;
        for (u32 w = 0; w < 600; ++w) words.push_back(text::CaptionWord{"palavra", w * 0.4, w * 0.4 + 0.35});
        text::CaptionOptions co;
        auto r = e.create_captions(videos[0], words, co);
        AUREA_CHECK(r.ok());
        if (r.ok()) made[4] = *r;
    }
    std::printf("(videos %zu textos %zu particulas %u formas %u vetor %u nulos %u legendas %u) ", videos.size(),
                texts.size(), made[0], made[1], made[2], made[3], made[4]);
    // 20 pré-composições de 2 textos cada.
    for (u32 i = 0; i < 20; ++i) {
        const u64 pair[2] = {texts[i * 2], texts[i * 2 + 1]};
        AUREA_CHECK(e.precompose(pair, 2, nullptr).ok());
    }
    // 50 efeitos em camadas diferentes; keyframes em todas (milhares).
    all = layer_ids(e);
    const EffectRegistry& reg = e.effects();
    for (u32 i = 0; i < 50; ++i) (void)e.apply_command(effect_add(all[(i * 7) % all.size()], reg.at(i % reg.count()).type_id()));
    // Keyframes: o grosso direto no modelo (milhares); 60 pelo caminho da UI
    // (comando + snapshot de desfazer) para medir quanto custa UMA ação aqui.
    u32 keys = 0;
    {
        Project* p = e.project();
        Composition* c = p->timeline().composition(p->timeline().current());
        for (u32 i = 0; i < all.size(); ++i) {
            Layer* l = c->layer(LayerId::unpack(all[i]));
            if (!l) continue;
            Track& op = l->tracks.get_or_create(TrackProperty::Opacity);
            Track& px = l->tracks.get_or_create(TrackProperty::PositionX);
            for (i64 f = 0; f < 300; f += 60) {
                (void)op.set(FrameIndex{f}, 0.8f, Interpolation::Linear);
                (void)px.set(FrameIndex{f + 30}, static_cast<f32>(f), Interpolation::Linear);
                keys += 2;
            }
        }
    }
    f64 actionMs = 0.0;
    for (u32 i = 0; i < 60; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        if (e.apply_command(keyframe(all[(i * 13) % all.size()], TrackProperty::RotationZ, 15 + i, 45.0f)).ok()) ++keys;
        actionMs += ms_since(t0);
    }
    const f64 buildMs = ms_since(tBuild);
    const u32 layers = layer_count(e);
    u32 comps = 0;
    e.project()->timeline().for_each_composition([&](CompositionId, const Composition&) { ++comps; });
    // As 150 legendas agora vivem numa única faixa (antes: 150 camadas de
    // texto, ~1500 keyframes). O projeto continua extremo: 361 camadas e 3670
    // keyframes com 21 composições.
    AUREA_CHECK(layers >= 350);
    AUREA_CHECK(keys >= 3500);

    const std::string path = test_path("extremo");
    remove_family(path);
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.save_project(path.c_str()).ok());   // a segunda gira o .bak (caminho real do autosave)
    const Engine::SaveStats st = e.save_stats();

    // Render: 30 quadros espalhados (com GPU real, 1920×1080 offscreen).
    f64 renderMs = 0.0, worstMs = 0.0;
    u32 rendered = 0;
#if defined(AUREA_TEST_VULKAN)
    if (e.gpu()) {
        TextureDesc d;
        d.width = 1920;
        d.height = 1080;
        d.format = SurfaceFormat::RGBA16F;
        d.renderTarget = true;
        d.transferSrc = true;
        const TextureHandle target = *e.gpu()->create_texture(d);
        for (u32 i = 0; i < 30; ++i) {
            Command seek;
            seek.type = CommandType::PlaybackSeek;
            seek.seek.time = tick_at(FrameIndex{static_cast<i64>(i * 9)}, 30.0);
            AUREA_CHECK(e.apply_command(seek).ok());
            const auto t0 = std::chrono::steady_clock::now();
            AUREA_CHECK(e.render_offscreen(target, 1920, 1080).ok());
            const f64 ms = ms_since(t0);
            renderMs += ms;
            worstMs = std::max(worstMs, ms);
            ++rendered;
        }
        e.gpu()->destroy_texture(target);
    }
#endif
    if (rendered == 0) {
        for (u32 i = 0; i < 30; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            AUREA_CHECK(e.render_frame().ok());
            renderMs += ms_since(t0);
            ++rendered;
        }
    }
    e.shutdown();

    Engine f;
    EngineConfig fc = headless();
    fc.mediaFactory = &factory;
    AUREA_CHECK(f.initialize(fc).ok());
    const auto tLoad = std::chrono::steady_clock::now();
    AUREA_CHECK(f.load_project(path.c_str()).ok());
    const f64 loadMs = ms_since(tLoad);
    AUREA_CHECK_EQ(layer_count(f), layers);
    u32 compsLoaded = 0;
    f.project()->timeline().for_each_composition([&](CompositionId, const Composition&) { ++compsLoaded; });
    AUREA_CHECK_EQ(compsLoaded, comps);
    // Cada pré-composição continua apontando para uma composição que existe.
    u32 nestedOk = 0, nested = 0;
    {
        const Project* p = f.project();
        const Composition* c = p->timeline().composition(p->timeline().current());
        c->layers().for_each([&](LayerId, const Layer& l) {
            if (l.kind != LayerKind::Composition) return;
            ++nested;
            if (p->timeline().composition(l.nested.composition)) ++nestedOk;
        });
    }
    AUREA_CHECK_EQ(nested, 20u);
    AUREA_CHECK_EQ(nestedOk, nested);
    f.shutdown();
    std::printf("\n      extremo: %u camadas, %u comps, %u keyframes, montar %.0f ms, 1 acao (keyframe+desfazer) %.2f ms; "
                "salvar: lock %.1f ms + escrita %.1f ms (%.2f MB); abrir %.0f ms; render %u quadros %s media %.1f ms pior %.1f ms  ",
                layers, comps, keys, buildMs, actionMs / 60.0, static_cast<f64>(st.lastLockNs) / 1e6, static_cast<f64>(st.lastWriteNs) / 1e6,
                static_cast<f64>(st.lastBytes) / (1024.0 * 1024.0), loadMs, rendered,
                worstMs > 0.0 ? "1920x1080 GPU" : "sem GPU", renderMs / rendered, worstMs);
    remove_family(path);
}
