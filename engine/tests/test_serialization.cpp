// Testes do formato .aurea.
//
// O que estes testes protegem, em uma frase: o trabalho do usuário. Um bug de
// serialização não trava o app — ele abre o projeto com uma camada faltando, ou
// com a animação deslocada, e o usuário descobre depois de exportar.
#include "TestFramework.hpp"

#include "aurea/project/Serialization.hpp"
#include "aurea/Engine.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/command/CommandQueue.hpp"

#include <cstdio>
#include <string>

using namespace aurea;

namespace {

std::string temp_path(const char* name) {
    std::string p = "aurea_test_";
    p += name;
    p += ".aurea";
    return p;
}

Project make_project() {
    auto r = Project::create_new(1280, 720, 30.0, "Projeto de teste");
    Project p = std::move(*r);
    p.metadata().author = "Aurea";
    return p;
}

} // namespace

AUREA_TEST(Serialization, SaveAndLoadRoundTrip) {
    const std::string path = temp_path("roundtrip");
    std::remove(path.c_str());

    Project original = make_project();
    original.metadata().title = "Viagem";
    original.metadata().author = "Dono";
    original.export_settings().width = 3840;
    original.export_settings().height = 2160;
    original.export_settings().videoBitrateMbps = 45;

    std::string error;
    AUREA_CHECK_MSG(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok(),
                    error.c_str());

    Project loaded;
    LoadReport report;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, &report, &error).ok());
    AUREA_CHECK(report.clean());

    AUREA_CHECK_EQ(loaded.metadata().title, std::string("Viagem"));
    AUREA_CHECK_EQ(loaded.metadata().author, std::string("Dono"));
    AUREA_CHECK_EQ(loaded.export_settings().width, static_cast<u32>(3840));
    AUREA_CHECK_EQ(loaded.export_settings().height, static_cast<u32>(2160));
    AUREA_CHECK_EQ(loaded.export_settings().videoBitrateMbps, static_cast<u32>(45));

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, LayersSurviveRoundTrip) {
    const std::string path = temp_path("layers");
    std::remove(path.c_str());

    Project original = make_project();
    const CompositionId cid = original.timeline().root();
    Composition* c = original.timeline().composition(cid);
    AUREA_CHECK(c != nullptr);

    const LayerId video = c->add_layer(LayerKind::Video, "Video principal");
    const LayerId text = c->add_layer(LayerKind::Text, "Titulo");
    const LayerId null = c->add_layer(LayerKind::Null, "Controlador");

    c->layer(video)->start = FrameIndex{30};
    c->layer(video)->end = FrameIndex{300};
    c->layer(video)->offset = FrameIndex{15};
    c->layer(video)->transform.position = Vec3{120.0f, -40.0f, 5.0f};
    c->layer(video)->transform.scale = Vec3{1.5f, 0.75f, 1.0f};
    c->layer(video)->transform.rotation = Vec3{0.0f, 0.0f, 45.0f};
    c->layer(video)->transform.opacity = 0.65f;
    c->layer(video)->blendMode = BlendMode::Screen;
    c->layer(video)->gain = 0.8f;
    c->layer(video)->muted = true;

    c->layer(text)->text.content = "Ola, Aurea";
    c->layer(text)->text.size = 144.0f;
    c->layer(text)->text.color = Vec4{0.2f, 0.4f, 0.9f, 1.0f};
    c->layer(text)->text.alignment = 1;
    c->layer(text)->text.rtl = true;

    c->layer(null)->visible = false;
    c->layer(null)->locked = true;
    c->layer(text)->parent = null;
    c->layer(text)->threeD = true;

    std::string error;
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    const Composition* lc = loaded.timeline().composition(loaded.timeline().root());
    AUREA_CHECK(lc != nullptr);
    AUREA_CHECK_EQ(lc->layers().count(), static_cast<u32>(3));

    // Ordem vertical preservada — é o que decide o que aparece na frente.
    AUREA_CHECK_EQ(lc->order().size(), static_cast<u32>(3));
    const Layer* l0 = lc->layer(lc->order().at(0));
    const Layer* l1 = lc->layer(lc->order().at(1));
    const Layer* l2 = lc->layer(lc->order().at(2));
    AUREA_CHECK_EQ(l0->name, std::string("Video principal"));
    AUREA_CHECK_EQ(l1->name, std::string("Titulo"));
    AUREA_CHECK_EQ(l2->name, std::string("Controlador"));

    AUREA_CHECK_EQ(l0->kind, LayerKind::Video);
    AUREA_CHECK_EQ(l0->start.value, static_cast<i64>(30));
    AUREA_CHECK_EQ(l0->end.value, static_cast<i64>(300));
    AUREA_CHECK_EQ(l0->offset.value, static_cast<i64>(15));
    AUREA_CHECK_NEAR(l0->transform.position.x, 120.0f, 1e-5);
    AUREA_CHECK_NEAR(l0->transform.position.y, -40.0f, 1e-5);
    AUREA_CHECK_NEAR(l0->transform.scale.y, 0.75f, 1e-5);
    AUREA_CHECK_NEAR(l0->transform.rotation.z, 45.0f, 1e-5);
    AUREA_CHECK_NEAR(l0->transform.opacity, 0.65f, 1e-5);
    AUREA_CHECK_EQ(l0->blendMode, BlendMode::Screen);
    AUREA_CHECK_NEAR(l0->gain, 0.8f, 1e-5);
    AUREA_CHECK(l0->muted);

    AUREA_CHECK_EQ(l1->text.content, std::string("Ola, Aurea"));
    AUREA_CHECK_NEAR(l1->text.size, 144.0f, 1e-4);
    AUREA_CHECK_NEAR(l1->text.color.z, 0.9f, 1e-5);
    AUREA_CHECK_EQ(l1->text.alignment, static_cast<u32>(1));
    AUREA_CHECK(l1->text.rtl);
    AUREA_CHECK(l1->threeD);

    AUREA_CHECK(!l2->visible);
    AUREA_CHECK(l2->locked);

    // Parenting reconstruído: o filho aponta para a camada certa depois do
    // round-trip, e não para um índice que virou outra camada.
    AUREA_CHECK(l1->parent.valid());

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, KeyframesSurviveRoundTrip) {
    const std::string path = temp_path("keyframes");
    std::remove(path.c_str());

    Project original = make_project();
    Composition* c = original.timeline().composition(original.timeline().root());
    const LayerId id = c->add_layer(LayerKind::Video, "Animada");

    Track& opacity = c->layer(id)->tracks.get_or_create(TrackProperty::Opacity);
    opacity.set(FrameIndex{0}, 0.0f, Interpolation::EaseIn);
    opacity.set(FrameIndex{30}, 1.0f, Interpolation::Bezier);
    opacity.set(FrameIndex{60}, 0.5f, Interpolation::Hold);
    opacity.keys[1].bx1 = 0.11f;
    opacity.keys[1].by1 = 0.22f;
    opacity.keys[1].bx2 = 0.33f;
    opacity.keys[1].by2 = 0.44f;

    std::string error;
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    const Composition* lc = loaded.timeline().composition(loaded.timeline().root());
    const Layer* ll = lc->layer(lc->order().at(0));
    AUREA_CHECK(ll != nullptr);

    const Track* t = ll->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(t != nullptr);
    AUREA_CHECK_EQ(t->keys.size(), static_cast<usize>(3));
    AUREA_CHECK_NEAR(t->sample(FrameIndex{15}), 0.25f, 0.05f);
    AUREA_CHECK_EQ(t->keys[0].interp, Interpolation::EaseIn);
    AUREA_CHECK_EQ(t->keys[1].interp, Interpolation::Bezier);
    AUREA_CHECK_EQ(t->keys[2].interp, Interpolation::Hold);
    AUREA_CHECK_NEAR(t->keys[1].bx1, 0.11f, 1e-6);
    AUREA_CHECK_NEAR(t->keys[1].by2, 0.44f, 1e-6);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, EffectsAndMasksSurviveRoundTrip) {
    const std::string path = temp_path("effects");
    std::remove(path.c_str());

    Project original = make_project();
    Composition* c = original.timeline().composition(original.timeline().root());
    const LayerId id = c->add_layer(LayerKind::Video, "Com efeitos");

    // Efeito no formato novo: tipo = id estável, slots genéricos, curva. O
    // tipo é um que esta versão NÃO registra — o projeto tem de guardar tudo
    // mesmo assim (abrir numa versão sem o efeito não pode apagar o ajuste).
    EffectInstance e;
    e.id = c->layer(id)->alloc_effect_id();
    e.type = effect_type_id("aurea.teste.desconhecido");
    e.enabled = false;
    e.expanded = true;
    e.params.resize(3);
    e.params[0].constant = ParamValue::scalar(12.5f);
    e.params[1].constant = ParamValue::color(0.1f, 0.2f, 0.3f, 0.4f);
    e.params[2].source = ParamSource::Expression;
    e.params[2].expression = 3;
    CurveData curve = CurveData::identity();
    curve.channel[0].insert(curve.channel[0].begin() + 1, CurveData::Point{0.5f, 0.7f});
    e.curves.push_back(curve);
    c->layer(id)->effects.push_back(e);

    Mask m;
    m.id = c->layer(id)->alloc_mask_id();
    m.name = "Recorte";
    m.operation = MaskOperation::Subtract;
    m.feather = 4.5f;
    m.expansion = -2.0f;
    m.opacity = 0.9f;
    m.inverted = true;
    m.closed = false;
    m.points.push_back(MaskPoint{Vec2{10.0f, 20.0f}, Vec2{1, 0}, Vec2{0, 1}});
    m.points.push_back(MaskPoint{Vec2{30.0f, 40.0f}, Vec2{0, 0}, Vec2{0, 0}});
    // v17: caminho animado e track matte.
    MaskPathKey k0;
    k0.frame = 3;
    k0.interp = 2;
    k0.points = m.points;
    MaskPathKey k1 = k0;
    k1.frame = 12;
    k1.interp = 0;
    k1.points[0].position = Vec2{55.0f, 66.0f};
    k1.points[1].outTangent = Vec2{-7.5f, 2.25f};
    m.pathKeys = {k0, k1};
    c->layer(id)->masks.push_back(m);
    // A matte aponta para uma camada que EXISTE: ao reabrir, os ids de camada
    // são refeitos e a referência é remapeada (um id solto, sem camada, vira
    // "nenhuma" — antes era copiado cru e apontava para outra camada).
    const LayerId matte = c->add_layer(LayerKind::Shape, "Matte");
    c->layer(id)->matteSource = matte;
    c->layer(id)->matteMode = MatteMode::LumaInverted;

    std::string error;
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    const Composition* lc = loaded.timeline().composition(loaded.timeline().root());
    const Layer* ll = lc->layer(lc->order().at(0));

    AUREA_CHECK_EQ(ll->effects.size(), static_cast<usize>(1));
    const EffectInstance& le = ll->effects[0];
    AUREA_CHECK_EQ(le.type, effect_type_id("aurea.teste.desconhecido"));
    AUREA_CHECK(!le.enabled);
    AUREA_CHECK_EQ(le.params.size(), static_cast<usize>(3));
    AUREA_CHECK_NEAR(le.params[0].constant.v[0], 12.5f, 1e-5);
    AUREA_CHECK_NEAR(le.params[1].constant.v[3], 0.4f, 1e-5);
    AUREA_CHECK(le.params[2].source == ParamSource::Expression);
    AUREA_CHECK_EQ(le.params[2].expression, static_cast<u32>(3));
    AUREA_CHECK_EQ(le.curves.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(le.curves[0].channel[0].size(), static_cast<usize>(3));
    AUREA_CHECK_NEAR(le.curves[0].channel[0][1].y, 0.7f, 1e-6);

    AUREA_CHECK_EQ(ll->masks.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(ll->masks[0].name, std::string("Recorte"));
    AUREA_CHECK_EQ(ll->masks[0].operation, MaskOperation::Subtract);
    AUREA_CHECK_NEAR(ll->masks[0].feather, 4.5f, 1e-5);
    AUREA_CHECK(ll->masks[0].inverted);
    AUREA_CHECK(!ll->masks[0].closed);
    AUREA_CHECK_EQ(ll->masks[0].points.size(), static_cast<usize>(2));
    AUREA_CHECK_NEAR(ll->masks[0].points[1].position.y, 40.0f, 1e-5);
    AUREA_CHECK_EQ(ll->masks[0].pathKeys.size(), static_cast<usize>(2));
    if (ll->masks[0].pathKeys.size() == 2) {
        const MaskPathKey& lk = ll->masks[0].pathKeys[1];
        AUREA_CHECK_EQ(lk.frame, static_cast<i64>(12));
        AUREA_CHECK_EQ(lk.interp, static_cast<u8>(0));
        AUREA_CHECK_EQ(ll->masks[0].pathKeys[0].interp, static_cast<u8>(2));
        AUREA_CHECK_EQ(lk.points.size(), static_cast<usize>(2));
        AUREA_CHECK_NEAR(lk.points[0].position.x, 55.0f, 1e-6);
        AUREA_CHECK_NEAR(lk.points[1].outTangent.x, -7.5f, 1e-6);
        AUREA_CHECK_NEAR(lk.points[1].outTangent.y, 2.25f, 1e-6);
    }
    AUREA_CHECK(lc->layer(ll->matteSource) != nullptr && lc->layer(ll->matteSource)->name == "Matte");
    AUREA_CHECK(ll->matteMode == MatteMode::LumaInverted);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, AssetsSurviveRoundTrip) {
    const std::string path = temp_path("assets");
    std::remove(path.c_str());

    Project original = make_project();
    Asset a;
    a.kind = AssetKind::Video;
    a.name = "clipe.mp4";
    a.sourcePath = "/media/clipe.mp4";
    a.proxyPath = "/cache/clipe_proxy.mp4";
    a.proxyWidth = 960;
    a.proxyHeight = 540;
    a.video.width = 3840;
    a.video.height = 2160;
    a.video.fps = 29.97;
    a.video.variableFrameRate = true;
    a.audio.sampleRate = 48000;
    a.audio.channels = 2;
    a.duration = FrameIndex{1800};
    a.contentHash = 0xDEADBEEFCAFEull;
    a.profile.codecTag = 0x68766331u;   // 'hvc1'
    a.profile.bitDepth = 10;
    a.profile.hdr = true;
    a.profile.transfer = ColorSpace::HLG;
    (void)original.add_asset(std::move(a));

    std::string error;
    AUREA_CHECK(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    AUREA_CHECK_EQ(loaded.asset_count(), static_cast<u32>(1));

    const AssetId found = loaded.find_asset_by_hash(0xDEADBEEFCAFEull);
    AUREA_CHECK(found.valid());
    const Asset* la = loaded.asset(found);
    AUREA_CHECK(la != nullptr);
    AUREA_CHECK_EQ(la->video.width, static_cast<u32>(3840));
    AUREA_CHECK_EQ(la->video.height, static_cast<u32>(2160));
    AUREA_CHECK(la->video.variableFrameRate);
    AUREA_CHECK_NEAR(la->video.fps, 29.97, 1e-6);
    AUREA_CHECK_EQ(la->profile.bitDepth, static_cast<u8>(10));
    AUREA_CHECK(la->profile.hdr);
    AUREA_CHECK_EQ(la->profile.transfer, ColorSpace::HLG);
    AUREA_CHECK(la->has_audio());
    AUREA_CHECK(la->proxy_ready());

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, AssetDeduplicationByHash) {
    Project p = make_project();
    Asset a;
    a.name = "primeiro";
    a.contentHash = 12345;
    const AssetId first = p.add_asset(a);

    Asset b;
    b.name = "segundo (mesmo arquivo)";
    b.contentHash = 12345;
    const AssetId second = p.add_asset(b);

    // Importar o mesmo vídeo duas vezes não deve ocupar duas vezes o cache de
    // decoders nem duplicar o proxy em disco.
    AUREA_CHECK(first == second);
    AUREA_CHECK_EQ(p.asset_count(), static_cast<u32>(1));
}

AUREA_TEST(Serialization, PeekReadsHeaderWithoutFullLoad) {
    const std::string path = temp_path("peek");
    std::remove(path.c_str());

    Project p = make_project();
    std::string error;
    AUREA_CHECK(ProjectSerializer::save(p, path, SaveOptions{}, &error).ok());

    FileHeader header;
    std::vector<SectionHeader> sections;
    AUREA_CHECK(ProjectSerializer::peek(path, header, sections, &error).ok());
    AUREA_CHECK_EQ(header.magic, FileHeader::kMagic);
    AUREA_CHECK_EQ(header.formatVersion, FileHeader::kCurrentFormatVersion);
    AUREA_CHECK(sections.size() >= 3);
    AUREA_CHECK(header.appVersion[0] != 0);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, RejectsFileThatIsNotAurea) {
    const std::string path = temp_path("bogus");
    std::FILE* f = std::fopen(path.c_str(), "wb");
    AUREA_CHECK(f != nullptr);
    // Maior que o cabeçalho, para exercitar a checagem do magic e não a de
    // tamanho: são dois caminhos distintos e cada um tem o seu teste.
    const char junk[] =
        "isto nao e um projeto do aurea, e grande o bastante para passar do "
        "cabecalho de sessenta e quatro bytes do formato";
    AUREA_CHECK(sizeof(junk) > 64);
    (void)std::fwrite(junk, 1, sizeof(junk), f);
    std::fclose(f);

    Project p;
    std::string error;
    const Status s = ProjectSerializer::load(p, path, LoadOptions{}, nullptr, &error);
    AUREA_CHECK(!s.ok());
    AUREA_CHECK_EQ(s.code(), Errc::UnsupportedFormat);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, RejectsFileShorterThanHeader) {
    // Um arquivo menor que o cabeçalho não pode ser lido nem para checar o
    // magic. O diagnóstico correto é "truncado", não "não é do Aurea" — a
    // diferença importa para quem recebeu um arquivo cortado por um download
    // interrompido.
    const std::string path = temp_path("tiny");
    std::FILE* f = std::fopen(path.c_str(), "wb");
    AUREA_CHECK(f != nullptr);
    (void)std::fwrite("AURE", 1, 4, f);
    std::fclose(f);

    Project p;
    const Status s = ProjectSerializer::load(p, path, LoadOptions{});
    AUREA_CHECK(!s.ok());
    AUREA_CHECK_EQ(s.code(), Errc::CorruptData);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, PeekRejectsForeignFile) {
    const std::string path = temp_path("peek_bogus");
    std::FILE* f = std::fopen(path.c_str(), "wb");
    AUREA_CHECK(f != nullptr);
    const char junk[] = "conteudo qualquer que nao e um projeto do aurea engine";
    (void)std::fwrite(junk, 1, sizeof(junk), f);
    std::fclose(f);

    FileHeader header;
    std::vector<SectionHeader> sections;
    const Status s = ProjectSerializer::peek(path, header, sections, nullptr);
    AUREA_CHECK(!s.ok());
    AUREA_CHECK_EQ(s.code(), Errc::UnsupportedFormat);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, RejectsTruncatedFile) {
    const std::string path = temp_path("truncated");
    std::remove(path.c_str());

    Project p = make_project();
    std::string error;
    AUREA_CHECK(ProjectSerializer::save(p, path, SaveOptions{}, &error).ok());

    // Corta o arquivo pela metade: é o que uma queda no meio da escrita produz.
    std::vector<u8> data;
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        AUREA_CHECK(f != nullptr);
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        data.resize(static_cast<usize>(size / 2));
        const usize read = data.empty() ? 0 : std::fread(data.data(), 1, data.size(), f);
        AUREA_CHECK_EQ(read, data.size());
        std::fclose(f);
    }
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        AUREA_CHECK(f != nullptr);
        (void)std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }

    Project loaded;
    LoadReport report;
    // Sem tolerância: recusa. Um projeto aberto pela metade é pior do que um
    // projeto que não abre, porque o usuário pode salvar por cima.
    const Status strict = ProjectSerializer::load(loaded, path, LoadOptions{}, &report, &error);
    AUREA_CHECK(!strict.ok());

    // Com tolerância: abre o que der e REPORTA. É o caminho da recuperação
    // pós-crash, onde algo é melhor que nada.
    Project tolerant;
    LoadReport tolerantReport;
    const Status lenient = ProjectSerializer::load(tolerant, path,
                                                   LoadOptions{false, true, true},
                                                   &tolerantReport, &error);
    if (lenient.ok()) {
        AUREA_CHECK(!tolerantReport.clean() || tolerantReport.sectionsRead.size() > 0);
    }

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, CorruptedSectionIsDetectedByChecksum) {
    const std::string path = temp_path("corrupt");
    std::remove(path.c_str());

    Project p = make_project();
    std::string error;
    AUREA_CHECK(ProjectSerializer::save(p, path, SaveOptions{}, &error).ok());

    // Corrompe um byte no meio do arquivo (dentro dos dados de alguma seção).
    std::vector<u8> data;
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        data.resize(static_cast<usize>(size));
        const usize read = std::fread(data.data(), 1, data.size(), f);
        AUREA_CHECK_EQ(read, data.size());
        std::fclose(f);
    }
    AUREA_CHECK(data.size() > 200);
    data[data.size() - 50] ^= 0xFF;
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        (void)std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }

    Project loaded;
    LoadReport report;
    const Status s = ProjectSerializer::load(loaded, path, LoadOptions{}, &report, &error);
    // O checksum existe justamente para isto: interpretar lixo como camadas
    // poderia produzir handles inválidos e, a partir daí, acesso a memória
    // errada. Detectar e recusar é o comportamento correto.
    AUREA_CHECK(!s.ok() || !report.sectionsCorrupt.empty());

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, SaveIsAtomicLeavesNoTempFile) {
    const std::string path = temp_path("atomic");
    const std::string tmp = path + ".tmp";
    std::remove(path.c_str());
    std::remove(tmp.c_str());

    Project p = make_project();
    AUREA_CHECK(ProjectSerializer::save(p, path, SaveOptions{}).ok());

    // Um temporário deixado para trás indica que a gravação atômica não
    // completou — e o arquivo bom não foi trocado.
    std::FILE* leftover = std::fopen(tmp.c_str(), "rb");
    AUREA_CHECK(leftover == nullptr);
    if (leftover) std::fclose(leftover);

    std::remove(path.c_str());
}

AUREA_TEST(Serialization, SaveOverExistingKeepsOldUntilComplete) {
    const std::string path = temp_path("overwrite");
    std::remove(path.c_str());

    Project first = make_project();
    first.metadata().title = "Primeira versao";
    AUREA_CHECK(ProjectSerializer::save(first, path, SaveOptions{}).ok());

    Project second = make_project();
    second.metadata().title = "Segunda versao";
    AUREA_CHECK(ProjectSerializer::save(second, path, SaveOptions{}).ok());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}).ok());
    AUREA_CHECK_EQ(loaded.metadata().title, std::string("Segunda versao"));

    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// Journal de autosave
// -----------------------------------------------------------------------------
AUREA_TEST(Journal, AppendAndReadBack) {
    const std::string path = temp_path("journal");
    std::remove(path.c_str());

    Command cmds[3];
    for (u32 i = 0; i < 3; ++i) {
        cmds[i].type = CommandType::LayerSetOpacity;
        cmds[i].opacity.opacity = 0.1f * static_cast<f32>(i);
    }
    AUREA_CHECK(ProjectSerializer::append_journal(path, cmds, 3, nullptr, 0).ok());

    // Segunda gravação: o journal é append-only, e cada bloco é independente.
    Command more[2];
    for (u32 i = 0; i < 2; ++i) {
        more[i].type = CommandType::LayerSetPosition;
    }
    AUREA_CHECK(ProjectSerializer::append_journal(path, more, 2, nullptr, 0).ok());

    std::vector<Command> readBack;
    AUREA_CHECK(ProjectSerializer::read_journal(path, readBack).ok());
    AUREA_CHECK_EQ(readBack.size(), static_cast<usize>(5));

    std::remove(path.c_str());
}

AUREA_TEST(Journal, TruncatedBlockStopsButKeepsEarlierOnes) {
    // A queda acontece no meio da última gravação. Os blocos anteriores
    // continuam válidos — é o que faz a recuperação pós-crash valer a pena.
    const std::string path = temp_path("journal_trunc");
    std::remove(path.c_str());

    Command cmds[4];
    for (u32 i = 0; i < 4; ++i) cmds[i].type = CommandType::LayerSetOpacity;
    AUREA_CHECK(ProjectSerializer::append_journal(path, cmds, 4, nullptr, 0).ok());
    AUREA_CHECK(ProjectSerializer::append_journal(path, cmds, 4, nullptr, 0).ok());

    std::vector<u8> data;
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        data.resize(static_cast<usize>(size));
        const usize read = std::fread(data.data(), 1, data.size(), f);
        AUREA_CHECK_EQ(read, data.size());
        std::fclose(f);
    }
    data.resize(data.size() - 32);   // corta o fim do segundo bloco
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        (void)std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }

    std::vector<Command> readBack;
    const Status s = ProjectSerializer::read_journal(path, readBack);
    AUREA_CHECK(s.ok());
    AUREA_CHECK_EQ(readBack.size(), static_cast<usize>(4));

    std::remove(path.c_str());
}

AUREA_TEST(Journal, CorruptedBlockIsDiscarded) {
    const std::string path = temp_path("journal_corrupt");
    std::remove(path.c_str());

    Command cmds[4];
    for (u32 i = 0; i < 4; ++i) cmds[i].type = CommandType::LayerSetOpacity;
    AUREA_CHECK(ProjectSerializer::append_journal(path, cmds, 4, nullptr, 0).ok());

    std::vector<u8> data;
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        data.resize(static_cast<usize>(size));
        const usize read = std::fread(data.data(), 1, data.size(), f);
        AUREA_CHECK_EQ(read, data.size());
        std::fclose(f);
    }
    // Corrompe um byte DENTRO dos comandos, não no cabeçalho.
    data[data.size() - 10] ^= 0x5A;
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        (void)std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }

    std::vector<Command> readBack;
    // O checksum por bloco descarta o bloco corrompido sozinho, sem invalidar
    // os anteriores.
    (void)ProjectSerializer::read_journal(path, readBack);
    AUREA_CHECK(readBack.empty());

    std::remove(path.c_str());
}

AUREA_TEST(Journal, ReadMissingFileReportsNotFound) {
    std::vector<Command> cmds;
    const Status s = ProjectSerializer::read_journal("nao_existe_journal.bin", cmds);
    AUREA_CHECK(!s.ok());
    AUREA_CHECK_EQ(s.code(), Errc::NotFound);
}

AUREA_TEST(Serialization, IncrementalSaveIsDeclaredUnimplemented) {
    // Honestidade do contrato: a gravação incremental NÃO está implementada, e
    // o motor diz isso em vez de aceitar a opção e gravar tudo em silêncio.
    AUREA_CHECK(!ProjectSerializer::incremental_save_implemented());
}

AUREA_TEST(Serialization, AdjustmentGuideAndLabelSurviveRoundTrip) {
    const std::string path = temp_path("papel_da_camada");
    std::remove(path.c_str());
    Project original = make_project();
    Composition* c = original.timeline().composition(original.timeline().root());
    const LayerId adj = c->add_layer(LayerKind::Shape, "Ajuste");
    const LayerId guide = c->add_layer(LayerKind::Shape, "Guia");
    const LayerId plain = c->add_layer(LayerKind::Shape, "Comum");
    c->layer(adj)->adjustment = true;
    c->layer(adj)->label = 3;
    c->layer(guide)->guide = true;
    c->layer(guide)->solo = true;
    c->layer(guide)->label = 12;
    std::string error;
    AUREA_CHECK_MSG(ProjectSerializer::save(original, path, SaveOptions{}, &error).ok(), error.c_str());

    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    const Composition* lc = loaded.timeline().composition(loaded.timeline().root());
    AUREA_CHECK(lc != nullptr);
    AUREA_CHECK_EQ(lc->order().size(), 3u);
    const Layer* la = lc->layer(lc->order().at(0));
    const Layer* lg = lc->layer(lc->order().at(1));
    const Layer* lp = lc->layer(lc->order().at(2));
    AUREA_CHECK(la && lg && lp);
    AUREA_CHECK(la->name == "Ajuste" && la->adjustment && !la->guide && la->label == 3);
    AUREA_CHECK(lg->name == "Guia" && lg->guide && lg->solo && !lg->adjustment && lg->label == 12);
    AUREA_CHECK(lp->name == "Comum" && !lp->adjustment && !lp->guide && lp->label == 0);
    (void)plain;
    std::remove(path.c_str());
}

AUREA_TEST(Serialization, LegacyParticleProjectsComeBackAsBoxEmitters) {
    // Faíscas, Neve e Poeira de luz emitiam SEMPRE da caixa da camada, e os
    // projetos delas gravaram `emitterType` no zero (o campo nem era escrito
    // pela UI da época). Sem esta correção a neve de um projeto antigo reabriria
    // saindo de um ponto, no centro — o trabalho do usuário mudaria de cara.
    ParticleData p;
    p.emitterType = 0;
    p.emitterSize = Vec2{320.0f, 10.0f};
    p.emitterOffset = Vec2{0.0f, -100.0f};
    migrate_legacy_particles(p, 19);
    AUREA_CHECK_EQ(p.emitterType, static_cast<u32>(ParticleEmitter::Box));
    AUREA_CHECK(p.emitterSize.x == 320.0f && p.emitterOffset.y == -100.0f);
    // O emissor de verdade do arquivo antigo continua onde estava: só o TIPO
    // muda, porque era o único que o formato não guardava.

    // De v20 em diante o campo é do usuário e NÃO se mexe — inclusive Ponto,
    // que é uma escolha legítima de quem montou o sistema no Particular.
    ParticleData q;
    q.emitterType = static_cast<u32>(ParticleEmitter::Point);
    migrate_legacy_particles(q, 20);
    AUREA_CHECK_EQ(q.emitterType, static_cast<u32>(ParticleEmitter::Point));
    migrate_legacy_particles(q, 21);
    AUREA_CHECK_EQ(q.emitterType, static_cast<u32>(ParticleEmitter::Point));
    q.emitterType = static_cast<u32>(ParticleEmitter::Mesh);
    migrate_legacy_particles(q, 21);
    AUREA_CHECK_EQ(q.emitterType, static_cast<u32>(ParticleEmitter::Mesh));
}

AUREA_TEST(Serialization, ObjectEnvironmentIsIndependentAndSurvivesReopen) {
    // Ambiente por objeto (v22): cada modelo 3D tem o SEU ambiente. Mexer num
    // não pode mexer no outro nem no do projeto — era o que o dono via
    // ("coloco HDRI no objeto A e o B muda").
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const LayerId a = comp->add_layer(LayerKind::Model3D, "A");
    const LayerId b = comp->add_layer(LayerKind::Model3D, "B");
    comp->layer(a)->threeD = true;
    comp->layer(b)->threeD = true;

    // A com ambiente próprio; B e o projeto ficam como estavam.
    AUREA_CHECK(e.set_object_environment(a.pack(), 1, 4242, 2.5f, 45.0f, 1.5f));
    f32 va[5]{}, vb[5]{}, proj[3]{};
    AUREA_CHECK(e.query_object_environment(a.pack(), va));
    AUREA_CHECK(e.query_object_environment(b.pack(), vb));
    AUREA_CHECK(e.query_environment(proj));
    AUREA_CHECK(va[0] == 1.0f && va[1] == 4242.0f && va[2] == 2.5f);
    AUREA_CHECK(vb[0] == 0.0f && vb[1] == 0.0f);          // B: do projeto
    AUREA_CHECK(proj[1] == 1.0f && proj[2] == 0.0f);      // o do projeto intacto

    // Mexer no ambiente do PROJETO não muda o estado do objeto.
    AUREA_CHECK(e.set_environment_params(3.0f, 90.0f));
    AUREA_CHECK(e.query_object_environment(a.pack(), va));
    AUREA_CHECK(va[2] == 2.5f && va[3] == 45.0f);

    // Salvar e reabrir: o ambiente de cada objeto volta igual.
    const std::string path = temp_path("ambiente_por_objeto");
    std::remove(path.c_str());
    std::string err;
    AUREA_CHECK_MSG(ProjectSerializer::save(*e.project(), path, SaveOptions{}, &err).ok(), err.c_str());
    Project reopened;
    AUREA_CHECK(ProjectSerializer::load(reopened, path, LoadOptions{}, nullptr, &err).ok());
    const Composition* c2 = reopened.timeline().composition(reopened.timeline().current());
    const Layer* la = nullptr;
    const Layer* lb = nullptr;
    for (usize i = 0; i < c2->order().size(); ++i) {
        const Layer* l = c2->layer(c2->order().at(i));
        if (l && l->name == "A") la = l;
        if (l && l->name == "B") lb = l;
    }
    AUREA_CHECK(la && lb);
    std::printf("    reaberto: A fonte %u hdri %llu int %.2f giro %.1f exp %.2f | B fonte %u\n",
                la->environmentSource, static_cast<unsigned long long>(la->environmentAsset),
                static_cast<f64>(la->environmentIntensity), static_cast<f64>(la->environmentRotation),
                static_cast<f64>(la->environmentExposure), lb->environmentSource);
    AUREA_CHECK(la->environmentSource == 1u && la->environmentAsset == 4242u);
    AUREA_CHECK(std::fabs(la->environmentIntensity - 2.5f) < 1e-4f);
    AUREA_CHECK(std::fabs(la->environmentRotation - 45.0f) < 1e-4f);
    AUREA_CHECK(std::fabs(la->environmentExposure - 1.5f) < 1e-4f);
    AUREA_CHECK(lb->environmentSource == 0u && lb->environmentAsset == 0u);
    e.shutdown();
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// O projeto INTEIRO: o formato é um só, e nada se perde
// -----------------------------------------------------------------------------

namespace {

std::vector<u8> ler_arquivo(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<u8> bytes(n > 0 ? static_cast<usize>(n) : 0);
    if (!bytes.empty()) {
        const usize lidos = std::fread(bytes.data(), 1, bytes.size(), f);
        bytes.resize(lidos);
    }
    std::fclose(f);
    return bytes;
}

/// Quantos de cada coisa o projeto tem. Serve para o teste não ser vazio: se a
/// releitura devolvesse um projeto limpo, os bytes bateriam e a contagem não.
struct Censo {
    u32 camadas = 0, efeitos = 0, keyframes = 0, mascaras = 0, trilhas = 0;
    u32 assets = 0, composicoes = 0, texto = 0, forma = 0, modelo3d = 0, precomp = 0;
};

Censo censo(Engine& e) {
    Censo c;
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    if (comp) {
        comp->layers().for_each([&](LayerId, const Layer& l) {
            ++c.camadas;
            if (l.kind == LayerKind::Text) ++c.texto;
            if (l.kind == LayerKind::Shape) ++c.forma;
            if (l.kind == LayerKind::Model3D) ++c.modelo3d;
            if (l.kind == LayerKind::Composition) ++c.precomp;
            c.efeitos += static_cast<u32>(l.effects.size());
            c.mascaras += static_cast<u32>(l.masks.size());
            c.trilhas += l.tracks.size();
            for (u32 t = 0; t < l.tracks.size(); ++t) c.keyframes += static_cast<u32>(l.tracks.at(t).keys.size());
            c.keyframes += static_cast<u32>(l.timeRemap.keys.size());
        });
    }
    c.assets = e.project()->asset_count();
    c.composicoes = e.project()->timeline().composition_count();
    return c;
}

} // namespace

AUREA_TEST(Serialization, WholeProjectWithEveryFeatureIsByteStable) {
    // O formato `.aurea` é UM SÓ: o Android e o iOS escrevem e leem por este
    // mesmo código (a ponte do iOS chama `new_project`/`save_project`/
    // `load_project`, e a serialização não tem um ramo por plataforma). O que
    // este teste prova é a outra metade da promessa do dono — que um projeto
    // com TUDO dentro volta idêntico:
    //
    //   salvar → abrir → serializar de novo dá BYTES IGUAIS.
    // A segunda gravação usa o serializador para preservar modifiedUnixMs:
    // Engine::save_project atualiza legitimamente esse campo a cada gravação.
    //
    // Comparar byte a byte cobre cada campo que o formato grava, inclusive os
    // que alguém esquecer de conferir num teste por campo. E o censo ao lado
    // garante que o teste não está comparando dois projetos vazios.
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, "Projeto completo").ok());

    // Imagem
    std::vector<u8> px(32 * 32 * 4, 200);
    const Result<u64> img = e.import_image(px.data(), 32, 32, "quadrado.png", nullptr);
    AUREA_CHECK(img.ok());
    // Forma, texto, nulo 3D, particular e texto 3D
    const Result<u64> forma = e.add_shape(0);
    AUREA_CHECK(forma.ok());
    const Result<u64> texto = e.add_text("Aurea");
    AUREA_CHECK(texto.ok());
    const Result<u64> nulo = e.add_null(true);
    AUREA_CHECK(nulo.ok());
    const Result<u64> part = e.add_particles(0);
    AUREA_CHECK(part.ok());
    scene3d::Text3DSpec t3;
    t3.content = "AUREA";
    t3.bevel = true;
    t3.bevelWidth = 0.03f;
    t3.bevelSegments = 3;
    t3.regionMaterials = true;
    t3.bevelMat.metallic = 1.0f;
    const Result<u64> t3d = e.add_text3d(t3);
    AUREA_CHECK(t3d.ok());

    // Máscara, efeito com keyframes, ambiente por objeto, sombras, remap.
    AUREA_CHECK(e.add_mask(*forma, nullptr, 0, true) >= 0);
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(*texto);
    add.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
    AUREA_CHECK(e.apply_command(add).ok());
    // O id do efeito é do núcleo (alloc_effect_id) — não um número escolhido.
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const u32 idEfeito = comp->layer(LayerId::unpack(*texto))->effects.back().id;
    Command set;
    set.type = CommandType::EffectSetParam;
    set.effect_param.layer = LayerId::unpack(*texto);
    set.effect_param.effect = EffectId{idEfeito, 0};
    set.effect_param.paramIndex = 0;
    set.effect_param.value = 12.5f;
    AUREA_CHECK(e.apply_command(set).ok());

    auto key = [&](Result<u64> camada, TrackProperty p, FrameIndex t, f32 v) {
        if (!camada.ok()) return;
        Command k;
        k.type = CommandType::KeyframeInsert;
        k.keyframe.track.layer = LayerId::unpack(*camada);
        k.keyframe.track.property = p;
        k.keyframe.track.effectIndex = kInvalidIndex;
        k.keyframe.track.effectParamIndex = 0;
        k.keyframe.time = t;
        k.keyframe.value = v;
        AUREA_CHECK(e.apply_command(k).ok());
    };
    key(*forma, TrackProperty::PositionX, FrameIndex{0}, 40.0f);
    key(*forma, TrackProperty::PositionX, FrameIndex{40}, 260.0f);
    key(*forma, TrackProperty::RotationZ, FrameIndex{0}, 0.0f);
    key(*forma, TrackProperty::RotationZ, FrameIndex{40}, 90.0f);
    key(*texto, TrackProperty::Opacity, FrameIndex{0}, 1.0f);
    key(*texto, TrackProperty::Opacity, FrameIndex{30}, 0.2f);

    if (t3d.ok()) {
        AUREA_CHECK(e.set_object_environment(*t3d, 1, 0, 2.0f, 45.0f, 1.2f));
        AUREA_CHECK(e.set_model_shadows(*t3d, false, true));
    }
    AUREA_CHECK(e.set_time_remap(*texto, true));
    AUREA_CHECK(e.edit_time_remap_key(*texto, -1, 20, 10.0f, 0) >= 0);

    const Censo antes = censo(e);
    std::printf("\n    projeto completo: %u camadas, %u efeitos, %u keyframes, %u trilhas, %u mascaras, %u assets\n",
                antes.camadas, antes.efeitos, antes.keyframes, antes.trilhas, antes.mascaras, antes.assets);
    AUREA_CHECK(antes.camadas >= 6);
    AUREA_CHECK(antes.efeitos >= 1);
    AUREA_CHECK(antes.keyframes >= 5);
    AUREA_CHECK(antes.mascaras >= 1);

    const std::string a = temp_path("completo_a");
    const std::string b = temp_path("completo_b");
    std::remove(a.c_str());
    std::remove(b.c_str());
    AUREA_CHECK(e.save_project(a.c_str()).ok());
    const std::vector<u8> bytesA = ler_arquivo(a);
    AUREA_CHECK(!bytesA.empty());

    AUREA_CHECK(e.load_project(a.c_str()).ok());
    const Censo depois = censo(e);
    AUREA_CHECK(depois.camadas == antes.camadas);
    AUREA_CHECK(depois.efeitos == antes.efeitos);
    AUREA_CHECK(depois.keyframes == antes.keyframes);
    AUREA_CHECK(depois.trilhas == antes.trilhas);
    AUREA_CHECK(depois.mascaras == antes.mascaras);
    AUREA_CHECK(depois.assets == antes.assets);
    AUREA_CHECK(depois.composicoes == antes.composicoes);

    AUREA_CHECK(ProjectSerializer::save(*e.project(), b, SaveOptions{}).ok());
    const std::vector<u8> bytesB = ler_arquivo(b);
    AUREA_CHECK(bytesB.size() == bytesA.size());
    usize iguais = 0;
    for (usize i = 0; i < bytesA.size() && i < bytesB.size(); ++i) iguais += bytesA[i] == bytesB[i];
    std::printf("    ida e volta: %zu bytes, %zu iguais\n", bytesA.size(), iguais);
    if (iguais != bytesA.size()) {
        for (usize i = 0, shown = 0; i < bytesA.size() && i < bytesB.size() && shown < 16; ++i) {
            if (bytesA[i] == bytesB[i]) continue;
            std::printf("    byte %zu: %02x -> %02x\n", i, bytesA[i], bytesB[i]);
            ++shown;
        }
    }
    AUREA_CHECK(iguais == bytesA.size());

    std::remove(a.c_str());
    std::remove(b.c_str());
}
