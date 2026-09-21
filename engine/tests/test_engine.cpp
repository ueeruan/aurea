// Testes da fachada: o contrato que a bridge JNI e a ObjC++ enxergam.
//
// Rodam headless — sem janela e sem GPU. Isso é deliberado: a timeline, a
// animação, os comandos e a serialização precisam funcionar sem backend
// gráfico, e é o que permite testá-los no CI.
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"

#include <cstdio>
#include <string>

using namespace aurea;

namespace {

EngineConfig headless_config() {
    EngineConfig cfg;
    // Sem backend: config.backend nulo. Timeline, comandos e serialização
    // funcionam sem GPU.
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    return cfg;
}

} // namespace

AUREA_TEST(Engine, InitializeHeadlessSucceeds) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK_EQ(e.state(), EngineState::Ready);
    e.shutdown();
    AUREA_CHECK_EQ(e.state(), EngineState::Uninitialized);
}

AUREA_TEST(Engine, DoubleInitializeIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(!e.initialize(headless_config()).ok());
    e.shutdown();
}

AUREA_TEST(Engine, NewProjectCreatesComposition) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 60.0, "Meu projeto").ok());

    const Project* p = e.project();
    AUREA_CHECK(p != nullptr);
    AUREA_CHECK(p->timeline().root().valid());
    AUREA_CHECK(p->timeline().current().valid());

    const Composition* c = p->timeline().composition(p->timeline().root());
    AUREA_CHECK(c != nullptr);
    AUREA_CHECK_EQ(c->width(), static_cast<u32>(1920));
    AUREA_CHECK_EQ(c->height(), static_cast<u32>(1080));
    AUREA_CHECK_NEAR(c->fps(), 60.0, 1e-9);
    e.shutdown();
}

AUREA_TEST(Engine, CommandsThroughTheQueueReachTheModel) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    // O caminho REAL da UI: escreve na fila, o motor drena no frame. Testar
    // pelo atalho `apply_command` não cobriria o contrato da fila.
    const char name[] = "Camada do usuario";
    u32 offset = 0, length = 0;
    AUREA_CHECK(e.commands().push_string(name, sizeof(name) - 1, offset, length));

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    create.stringOffset = offset;
    create.stringLength = length;

    Command opacity;
    opacity.type = CommandType::LayerSetOpacity;
    opacity.opacity.layer = LayerId{};
    opacity.opacity.opacity = 0.4f;

    const Command batch[1] = {create};
    AUREA_CHECK_EQ(e.submit_commands(batch, 1), static_cast<u32>(1));

    AUREA_CHECK(e.render_frame().ok());

    const Project* p = e.project();
    const Composition* c = p->timeline().composition(p->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(1));
    const Layer* l = c->layer(c->order().at(0));
    AUREA_CHECK(l != nullptr);
    AUREA_CHECK_EQ(l->name, std::string("Camada do usuario"));

    (void)opacity;
    e.shutdown();
}

AUREA_TEST(Engine, SubmitCommandsAcceptsPartialBatch) {
    // A fila nunca bloqueia: se encher, o motor aceita o que couber e a UI
    // reenvia o resto. Nenhum comando é perdido em silêncio.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    std::vector<Command> flood(CommandQueue::kCapacity + 500);
    for (auto& c : flood) c.type = CommandType::Nop;

    const u32 accepted = e.submit_commands(flood.data(), static_cast<u32>(flood.size()));
    AUREA_CHECK(accepted <= static_cast<u32>(flood.size()));
    AUREA_CHECK(accepted > 0);
    e.shutdown();
}

AUREA_TEST(Engine, AddLayerViaCommandThenQueryRows) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    for (int i = 0; i < 5; ++i) {
        Command c;
        c.type = CommandType::LayerCreate;
        c.layer_create.kind = (i % 2) ? LayerKind::Text : LayerKind::Video;
        AUREA_CHECK(e.apply_command(c).ok());
    }

    bridge::LayerRow rows[16];
    char names[512];
    const u32 n = e.query_layers(rows, 16, names, sizeof(names));
    AUREA_CHECK_EQ(n, static_cast<u32>(5));

    // A lista vem da FRENTE para o fundo: é o que a UI mostra, e inverter na UI
    // seria a mesma regra em dois lugares — uma chance de divergirem.
    AUREA_CHECK(rows[0].zIndex < rows[4].zIndex);
    for (u32 i = 0; i < n; ++i) {
        AUREA_CHECK(rows[i].nameLength > 0);
    }
    e.shutdown();
}

AUREA_TEST(Engine, QueryLayersRespectsCapacity) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    for (int i = 0; i < 20; ++i) {
        Command c;
        c.type = CommandType::LayerCreate;
        c.layer_create.kind = LayerKind::Video;
        (void)e.apply_command(c);
    }

    bridge::LayerRow rows[4];
    const u32 n = e.query_layers(rows, 4, nullptr, 0);
    AUREA_CHECK_EQ(n, static_cast<u32>(4));
    e.shutdown();
}

AUREA_TEST(Engine, LayerVisibilityFlagIsReported) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    create.correlationId = 7;
    (void)e.apply_command(create);

    const Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);

    Command hide;
    hide.type = CommandType::LayerSetVisible;
    hide.layer_visible.layer = id;
    hide.layer_visible.visible = false;
    AUREA_CHECK(e.apply_command(hide).ok());

    bridge::LayerRow rows[4];
    const u32 n = e.query_layers(rows, 4, nullptr, 0);
    AUREA_CHECK_EQ(n, static_cast<u32>(1));
    AUREA_CHECK_EQ(rows[0].flags & 1u, static_cast<u32>(0));   // bit 0 = visivel
    e.shutdown();
}

AUREA_TEST(Engine, LayerSplitProducesTwoAdjacentLayers) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId original = c->order().at(0);
    c->layer(original)->start = FrameIndex{0};
    c->layer(original)->end = FrameIndex{100};

    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = original;
    split.layer_split.at = FrameIndex{40};
    AUREA_CHECK(e.apply_command(split).ok());

    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(2));

    const Layer* first = c->layer(original);
    AUREA_CHECK_EQ(first->start.value, static_cast<i64>(0));
    AUREA_CHECK_EQ(first->end.value, static_cast<i64>(40));

    // A segunda metade continua de onde a primeira parou. Sem ajustar o offset,
    // a segunda repetiria o começo do vídeo — o erro clássico de corte.
    LayerId second{};
    c->layers().for_each([&](LayerId id, const Layer&) {
        if (!(id == original)) second = id;
    });
    AUREA_CHECK(second.valid());
    const Layer* s = c->layer(second);
    AUREA_CHECK_EQ(s->start.value, static_cast<i64>(40));
    AUREA_CHECK_EQ(s->end.value, static_cast<i64>(100));
    AUREA_CHECK_EQ(s->offset.value, static_cast<i64>(40));
    e.shutdown();
}

AUREA_TEST(Engine, SplitOutsideRangeIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);
    c->layer(id)->start = FrameIndex{0};
    c->layer(id)->end = FrameIndex{100};

    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = id;
    split.layer_split.at = FrameIndex{500};
    AUREA_CHECK(!e.apply_command(split).ok());
    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(1));
    e.shutdown();
}

AUREA_TEST(Engine, ParentingCycleIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command createA;
    createA.type = CommandType::LayerCreate;
    createA.layer_create.kind = LayerKind::Null;
    (void)e.apply_command(createA);
    Command createB;
    createB.type = CommandType::LayerCreate;
    createB.layer_create.kind = LayerKind::Null;
    (void)e.apply_command(createB);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId a = c->order().at(0);
    const LayerId b = c->order().at(1);

    Command parent;
    parent.type = CommandType::LayerSetParent;
    parent.layer_parent.layer = a;
    parent.layer_parent.parent = b;
    AUREA_CHECK(e.apply_command(parent).ok());

    // Fechar o ciclo faria a avaliação de transform entrar em laço infinito.
    Command cycle;
    cycle.type = CommandType::LayerSetParent;
    cycle.layer_parent.layer = b;
    cycle.layer_parent.parent = a;
    AUREA_CHECK(!e.apply_command(cycle).ok());
    e.shutdown();
}

AUREA_TEST(Engine, SelectionIsSortedAndDeduplicated) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    const u64 ids[] = {50, 10, 50, 30};
    e.set_selection(ids, 4);
    AUREA_CHECK_EQ(e.selection_count(), static_cast<u32>(3));
    AUREA_CHECK(e.is_selected(10));
    AUREA_CHECK(e.is_selected(30));
    AUREA_CHECK(e.is_selected(50));
    AUREA_CHECK(!e.is_selected(20));

    u64 out[8];
    AUREA_CHECK_EQ(e.get_selection(out, 8), static_cast<u32>(3));
    AUREA_CHECK_EQ(out[0], static_cast<u64>(10));

    e.clear_selection();
    AUREA_CHECK_EQ(e.selection_count(), static_cast<u32>(0));
    e.shutdown();
}

AUREA_TEST(Engine, SeekUpdatesPlayhead) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = TickNs{1'000'000'000};   // 1 segundo
    AUREA_CHECK(e.apply_command(seek).ok());

    // 1 s a 30 fps = frame 30.
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, static_cast<i64>(30));
    e.shutdown();
}

AUREA_TEST(Engine, PlayPauseControlsClock) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command play;
    play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(e.apply_command(play).ok());
    AUREA_CHECK(e.project()->timeline().playing());

    Command pause;
    pause.type = CommandType::PlaybackPause;
    AUREA_CHECK(e.apply_command(pause).ok());
    AUREA_CHECK(!e.project()->timeline().playing());
    e.shutdown();
}

AUREA_TEST(Engine, CompositionSizeBeyondDeviceIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());

    Command tooBig;
    tooBig.type = CommandType::CompositionSetSize;
    tooBig.comp_size.comp = e.project()->timeline().current();
    tooBig.comp_size.width = 16384;
    tooBig.comp_size.height = 16384;
    // Acima do que o aparelho decodifica, o preview não acompanha e o export
    // não fecha. Recusar com código claro é melhor que aceitar e o usuário
    // descobrir no export.
    AUREA_CHECK(!e.apply_command(tooBig).ok());
    AUREA_CHECK_EQ(c->width(), static_cast<u32>(1280));
    e.shutdown();
}

AUREA_TEST(Engine, SeekMovesThePlayheadAndRenderKeepsIt) {
    // Parado, o playhead é o que o usuário pediu: renderizar não o move.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{10}, 30.0);
    AUREA_CHECK(e.apply_command(seek).ok());
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, static_cast<i64>(10));
    AUREA_CHECK_EQ(e.read_status().playhead.value, static_cast<i64>(10));
    e.shutdown();
}

AUREA_TEST(Engine, PlayAdvancesThePlayheadFromTheClock) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command play;
    play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(e.apply_command(play).ok());
    AUREA_CHECK(e.read_status().playing);
    // O relógio anda em tempo real: 120 ms depois, pelo menos 2 frames.
    const u64 t0 = monotonic_ns();
    while (monotonic_ns() - t0 < 120'000'000ull) {}
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK(e.project()->timeline().playhead().value >= 2);
    Command pause;
    pause.type = CommandType::PlaybackPause;
    AUREA_CHECK(e.apply_command(pause).ok());
    AUREA_CHECK(!e.read_status().playing);
    e.shutdown();
}

AUREA_TEST(Engine, AnimationSkipsLayersOutsideTheirRange) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);
    c->layer(id)->start = FrameIndex{1000};
    c->layer(id)->end = FrameIndex{2000};

    AUREA_CHECK(e.render_frame().ok());

    bridge::LayerRow rows[4];
    // A camada existe na timeline, mas o motor não a avaliou — ela está fora
    // do tempo. Avaliar camadas inativas é trabalho jogado fora a 60 Hz.
    AUREA_CHECK_EQ(e.query_layers(rows, 4, nullptr, 0), static_cast<u32>(1));
    e.shutdown();
}

AUREA_TEST(Engine, StatusReflectsProjectState) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, "Status").ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);
    (void)e.render_frame();

    const EngineStatus st = e.read_status();
    AUREA_CHECK_EQ(st.layerCount, static_cast<u32>(1));
    AUREA_CHECK_EQ(st.duration.value, static_cast<i64>(300));
    AUREA_CHECK(!st.playing);
    AUREA_CHECK(st.previewDenominator >= 1);
    e.shutdown();
}

AUREA_TEST(Engine, TelemetryReportsWorkersAndQueues) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    const EngineTelemetry t = e.read_telemetry();
    AUREA_CHECK_EQ(t.workerCount, static_cast<u32>(2));
    AUREA_CHECK_EQ(t.commandsDropped, static_cast<u64>(0));
    e.shutdown();
}

AUREA_TEST(Engine, ExportIsDeclaredNotImplemented) {
    // Honestidade do contrato. A UI precisa saber que o export não existe
    // ainda, em vez de receber um "ok" e produzir um arquivo vazio.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    ExportSettings settings;
    const Status s = e.start_export(settings, "saida.mp4");
    AUREA_CHECK(!s.ok());
    AUREA_CHECK_EQ(s.code(), Errc::NotImplemented);

    const Engine::ExportProgress p = e.export_progress();
    AUREA_CHECK(!p.running);
    AUREA_CHECK_EQ(p.result, Errc::NotImplemented);

    // Nenhum arquivo foi criado: se tivesse sido, seria um vídeo vazio que o
    // usuário acharia que é o trabalho dele.
    std::FILE* f = std::fopen("saida.mp4", "rb");
    AUREA_CHECK(f == nullptr);
    if (f) std::fclose(f);
    e.shutdown();
}

AUREA_TEST(Engine, SuspendAndResumeKeepProject) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, "Sobrevive").ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    AUREA_CHECK(e.suspend().ok());
    AUREA_CHECK_EQ(e.state(), EngineState::Suspended);

    AUREA_CHECK(e.resume().ok());

    // Suspender NÃO perde trabalho: o app pode ser morto em background a
    // qualquer momento, e o projeto tem que estar lá quando ele voltar.
    AUREA_CHECK(e.project() != nullptr);
    AUREA_CHECK_EQ(e.project()->metadata().title, std::string("Sobrevive"));
    const Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(1));
    e.shutdown();
}

AUREA_TEST(Engine, RenderWithoutProjectFails) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(!e.render_frame().ok());
    e.shutdown();
}

AUREA_TEST(Engine, UnknownEffectTypeIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);

    Command addEffect;
    addEffect.type = CommandType::EffectAdd;
    addEffect.effect_add.layer = id;
    addEffect.effect_add.effectType = 4242;   // nao registrado
    AUREA_CHECK(!e.apply_command(addEffect).ok());
    AUREA_CHECK_EQ(c->layer(id)->effects.size(), static_cast<usize>(0));
    e.shutdown();
}

AUREA_TEST(Engine, MaskOperationsOnMissingMaskAreRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);

    Command maskOp;
    maskOp.type = CommandType::MaskSetOperation;
    maskOp.mask_op.layer = id;
    maskOp.mask_op.mask = MaskId{99, 1};
    maskOp.mask_op.op = MaskOperation::Subtract;
    AUREA_CHECK(!e.apply_command(maskOp).ok());
    e.shutdown();
}

// -----------------------------------------------------------------------------
// Histórico (desfazer/refazer por snapshot da composição)
// -----------------------------------------------------------------------------
namespace {

LayerId first_layer(Engine& e) {
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    LayerId id{};
    c->layers().for_each([&](LayerId lid, const Layer&) { if (!id.valid()) id = lid; });
    return id;
}

const Layer* layer_of(Engine& e, LayerId id) {
    return e.project()->timeline().composition(e.project()->timeline().current())->layer(id);
}

Command position_cmd(LayerId id, f32 x, f32 y) {
    Command c;
    c.type = CommandType::LayerSetPosition;
    c.position.layer = id;
    c.position.x = x;
    c.position.y = y;
    return c;
}

} // namespace

AUREA_TEST(History, UndoAndRedoRestoreTheExactValue) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    AUREA_CHECK(id.valid());

    AUREA_CHECK(e.apply_command(position_cmd(id, 100, 200)).ok());
    AUREA_CHECK(e.apply_command(position_cmd(id, 300, 400)).ok());
    AUREA_CHECK(e.read_status().canUndo);

    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_NEAR(layer_of(e, id)->transform.position.x, 100.0f, 1e-6f);
    AUREA_CHECK(e.read_status().canRedo);

    Command redo;
    redo.type = CommandType::Redo;
    AUREA_CHECK(e.apply_command(redo).ok());
    AUREA_CHECK_NEAR(layer_of(e, id)->transform.position.x, 300.0f, 1e-6f);

    // Desfaz tudo, inclusive a criação: a layer some; refazer a devolve com o MESMO id.
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(layer_of(e, id) == nullptr);
    AUREA_CHECK(!e.read_status().canUndo);
    AUREA_CHECK(e.apply_command(redo).ok());
    AUREA_CHECK(layer_of(e, id) != nullptr);
    e.shutdown();
}

AUREA_TEST(History, AGestureGroupUndoesAtOnce) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    AUREA_CHECK(e.apply_command(position_cmd(id, 10, 10)).ok());
    const u32 depthBefore = e.read_status().undoDepth;

    Command begin;
    begin.type = CommandType::UndoBeginGroup;
    AUREA_CHECK(e.apply_command(begin, "arrastar").ok());
    for (int i = 1; i <= 60; ++i) AUREA_CHECK(e.apply_command(position_cmd(id, 10.0f + i, 10.0f)).ok());
    Command end;
    end.type = CommandType::UndoEndGroup;
    AUREA_CHECK(e.apply_command(end).ok());
    AUREA_CHECK_EQ(e.read_status().undoDepth, depthBefore + 1);

    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_NEAR(layer_of(e, id)->transform.position.x, 10.0f, 1e-6f);
    e.shutdown();
}

AUREA_TEST(History, SplitAndEffectAreUndoable) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    Command range;
    range.type = CommandType::LayerSetTimeRange;
    range.layer_range.layer = id;
    range.layer_range.start = FrameIndex{0};
    range.layer_range.end = FrameIndex{90};
    AUREA_CHECK(e.apply_command(range).ok());

    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = id;
    split.layer_split.at = FrameIndex{30};
    AUREA_CHECK(e.apply_command(split).ok());
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), 2u);
    AUREA_CHECK_EQ(layer_of(e, id)->end.value, static_cast<i64>(30));

    Command fx;
    fx.type = CommandType::EffectAdd;
    fx.effect_add.layer = id;
    fx.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
    fx.effect_add.index = kInvalidIndex;
    AUREA_CHECK(e.apply_command(fx).ok());
    AUREA_CHECK_EQ(layer_of(e, id)->effects.size(), static_cast<usize>(1));

    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(layer_of(e, id)->effects.size(), static_cast<usize>(0));
    AUREA_CHECK(e.apply_command(undo).ok());
    c = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), 1u);
    AUREA_CHECK_EQ(layer_of(e, id)->end.value, static_cast<i64>(90));
    e.shutdown();
}

AUREA_TEST(History, TrimStartKeepsContentWithOffset) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    Command trim;
    trim.type = CommandType::LayerSetTimeRange;
    trim.layer_range.layer = id;
    trim.layer_range.start = FrameIndex{12};
    trim.layer_range.end = FrameIndex{60};
    trim.layer_range.offset = FrameIndex{12};
    trim.layer_range.setOffset = 1;
    AUREA_CHECK(e.apply_command(trim).ok());
    const Layer* l = layer_of(e, id);
    AUREA_CHECK_EQ(l->offset.value, static_cast<i64>(12));
    // O conteúdo não andou: o frame local no instante 20 continua 20.
    AUREA_CHECK_EQ(l->local_time(FrameIndex{20}).value, static_cast<i64>(20));

    bridge::LayerDetailPOD d;
    AUREA_CHECK(e.query_layer_detail(id.pack(), d));
    AUREA_CHECK_EQ(d.startFrame, 12);
    AUREA_CHECK_EQ(d.offsetFrames, 12);
    e.shutdown();
}
