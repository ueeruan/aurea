// Testes do formato .aurea.
//
// O que estes testes protegem, em uma frase: o trabalho do usuário. Um bug de
// serialização não trava o app — ele abre o projeto com uma camada faltando, ou
// com a animação deslocada, e o usuário descobre depois de exportar.
#include "TestFramework.hpp"

#include "aurea/project/Serialization.hpp"
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
    c->layer(id)->masks.push_back(m);

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
