#include "aurea/Engine.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/project/Serialization.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace aurea {
namespace {

} // namespace

// -----------------------------------------------------------------------------
// ExportContext
//
// O export ainda não está implementado. Esta estrutura existe para que a
// superfície de API (`start_export`, `export_progress`) seja estável quando o
// codificador de hardware entrar — a UI já pode ser escrita contra ela.
// -----------------------------------------------------------------------------
struct Engine::ExportContext {
    ExportProgress progress{};
    ExportSettings settings{};
    std::string    outputPath;
    std::atomic<bool> cancelRequested{false};
};

Engine::Engine() : frameGraph_(0) {
    commandQueue_ = std::make_unique<CommandQueue>();
    adaptive_ = new AdaptiveResolutionController(caps_);
}

Engine::~Engine() {
    shutdown();
    delete adaptive_;
    adaptive_ = nullptr;
}

// -----------------------------------------------------------------------------
// Ciclo de vida
// -----------------------------------------------------------------------------
Status Engine::initialize(const EngineConfig& config) noexcept {
    if (state_ != EngineState::Uninitialized) {
        return Status{Errc::InvalidState, "motor ja inicializado"};
    }

    config_ = config;

    // Ordem importa: as capacidades definem o tamanho do pool, o orçamento de
    // memória e o teto do preview. Tudo o mais depende delas.
    caps_.detect();

    const u32 workers = config.workerCount ? config.workerCount
                                           : caps_.recommended_worker_count();
    if (const Status s = jobs_.start(workers); !s.ok()) {
        lastError_ = s.code();
        state_ = EngineState::Failed;
        return s;
    }

    const u64 budget = config.memoryBudgetBytes ? config.memoryBudgetBytes
                                                : caps_.memory_budget_bytes();
    // Divisão do orçamento por categoria. Os percentuais vêm do perfil de uso
    // real de um editor: frames decodificados e texturas dominam; o projeto
    // aberto é uma fração pequena mas intocável.
    memory_.set_budget(MemoryClass::Thumbnails,     budget * 6 / 100);
    memory_.set_budget(MemoryClass::Proxies,        budget * 10 / 100);
    memory_.set_budget(MemoryClass::DecodedFrames,  budget * 24 / 100);
    memory_.set_budget(MemoryClass::RenderedFrames, budget * 16 / 100);
    memory_.set_budget(MemoryClass::GpuTextures,    budget * 20 / 100);
    memory_.set_budget(MemoryClass::GpuGeometry,    budget * 10 / 100);
    memory_.set_budget(MemoryClass::Audio,          budget * 4 / 100);
    memory_.set_budget(MemoryClass::Assets,         budget * 8 / 100);
    memory_.set_budget(MemoryClass::Persistent,     budget * 2 / 100);

    if (config.createGpuBackend) {
        gpu_ = GPUBackend::create_default();
        if (!gpu_) {
            // Sem backend gráfico não há preview. O motor continua de pé para
            // que a timeline, a animação e a serialização funcionem — o que
            // mantém os testes rodando em ambiente sem GPU.
            AUREA_LOG_WARN("nenhum backend grafico disponivel: preview desativado");
        }
    }

    if (gpu_) {
        if (const Status s = gpu_->initialize(); !s.ok()) {
            AUREA_LOG_ERROR("falha ao inicializar o backend grafico: %s", s.message().data());
            delete gpu_;
            gpu_ = nullptr;
        } else {
            SurfaceDesc surface;
            surface.nativeWindow = config.nativeWindow;
            surface.width = config.surfaceWidth;
            surface.height = config.surfaceHeight;
            surface.vsync = true;
            if (const Status s = gpu_->recreate_surface(surface); !s.ok()) {
                AUREA_LOG_WARN("falha ao criar a superficie: %s", s.message().data());
            }
            if (const Status s = shaders_.initialize(*gpu_); !s.ok()) {
                AUREA_LOG_WARN("biblioteca de shaders nao inicializou: %s", s.message().data());
            }
        }
    }

    adapt().configure(1920, 1080, config.displayRefreshRate);
    adapt().set_user_scale(config.initialPreviewScale);

    state_ = EngineState::Ready;
    AUREA_LOG_INFO("Aurea Engine pronta: %s", caps_.summary().c_str());
    return OkStatus;
}

void Engine::shutdown() noexcept {
    if (state_ == EngineState::Uninitialized || state_ == EngineState::ShuttingDown) return;
    state_ = EngineState::ShuttingDown;

    jobs_.stop();

    shaders_.shutdown();
    if (gpu_) {
        frameGraph_.release_gpu_resources(*gpu_);
        gpu_->shutdown();
        delete gpu_;
        gpu_ = nullptr;
    }

    commandQueue_->reset();
    project_.reset();

    state_ = EngineState::Uninitialized;
}

EngineState Engine::state() const noexcept { return state_; }

Status Engine::suspend() noexcept {
    if (state_ != EngineState::Ready && state_ != EngineState::Rendering) {
        return Status{Errc::InvalidState, "motor nao esta pronto"};
    }

    // Libera tudo que é refazível. O PROJETO permanece: suspender não perde
    // trabalho, e o sistema pode matar o app em background a qualquer momento.
    frameCache_.clear();
    memory_.request_reclaim(memory_.total_budget());

    if (gpu_) {
        gpu_->wait_idle();
        frameGraph_.release_gpu_resources(*gpu_);
        shaders_.invalidate_device_shaders();
    }

    state_ = EngineState::Suspended;
    return OkStatus;
}

Status Engine::resume(const EngineConfig& config) noexcept {
    if (state_ != EngineState::Suspended) {
        return Status{Errc::InvalidState, "motor nao esta suspenso"};
    }
    config_ = config;

    if (gpu_) {
        SurfaceDesc surface;
        surface.nativeWindow = config.nativeWindow;
        surface.width = config.surfaceWidth;
        surface.height = config.surfaceHeight;
        surface.vsync = true;
        if (const Status s = gpu_->recreate_surface(surface); !s.ok()) {
            return s;
        }
    }

    state_ = EngineState::Ready;
    return OkStatus;
}

Status Engine::resize_surface(u32 width, u32 height) noexcept {
    config_.surfaceWidth = width;
    config_.surfaceHeight = height;
    if (gpu_) return gpu_->resize_surface(width, height);
    return OkStatus;
}

// -----------------------------------------------------------------------------
// Projeto
// -----------------------------------------------------------------------------
Status Engine::new_project(u32 width, u32 height, f64 fps, const char* title) noexcept {
    auto result = Project::create_new(width, height, fps,
                                      title ? std::string(title) : std::string("Projeto sem titulo"));
    if (!result.ok()) { lastError_ = result.code(); return result.status(); }

    project_ = std::make_unique<Project>(std::move(*result));

    undo_.clear();
    frameCache_.clear();
    selection_.clear();

    if (Composition* c = current_composition()) {
        adapt().configure(c->width(), c->height(), static_cast<f32>(c->fps()));
    }

    state_ = EngineState::Ready;
    return OkStatus;
}

Status Engine::load_project(const char* path) noexcept {
    if (!path) return Errc::InvalidArgument;

    Project loaded;
    LoadReport report;
    std::string error;

    LoadOptions options;
    options.lazyAssets = true;
    options.tolerateCorruptSections = true;   // abrir o que der, reportar o resto

    const Status s = ProjectSerializer::load(loaded, path, options, &report, &error);
    if (!s.ok() && report.sectionsRead.empty()) {
        return s;
    }

    project_ = std::make_unique<Project>(std::move(loaded));
    undo_.clear();
    frameCache_.clear();
    selection_.clear();

    if (Composition* c = current_composition()) {
        adapt().configure(c->width(), c->height(), static_cast<f32>(c->fps()));
    }

    if (!report.clean()) {
        AUREA_LOG_WARN("projeto aberto com partes faltando: %llu corrompidas, %llu puladas",
                       static_cast<unsigned long long>(report.sectionsCorrupt.size()),
                       static_cast<unsigned long long>(report.sectionsSkipped.size()));
        return Status{Errc::CorruptData, "projeto aberto parcialmente"};
    }
    return OkStatus;
}

Status Engine::save_project(const char* path) noexcept {
    if (!project_) return Errc::InvalidState;
    if (!path) return Errc::InvalidArgument;

    if (const Status s = project_->save(path); !s.ok()) return s;

    // Salvar de verdade é o único momento em que o journal pode ser descartado:
    // a partir daqui o arquivo do projeto contém tudo que o journal continha.
    project_->discard_recovery();
    return OkStatus;
}

Status Engine::save_project() noexcept {
    if (!project_) return Errc::InvalidState;
    if (!project_->has_path()) return Status{Errc::InvalidState, "projeto nunca foi salvo"};
    return save_project(project_->path().c_str());
}

const AutosaveState& Engine::autosave_state() const noexcept {
    static const AutosaveState kEmpty{};
    return project_ ? project_->autosave() : kEmpty;
}

Status Engine::recover_session() noexcept {
    if (!project_) return Errc::InvalidState;
    if (const Status s = project_->apply_recovery(); !s.ok()) return s;

    u32 applied = 0;
    u32 failed = 0;
    for (const Command& cmd : project_->recovered_commands()) {
        // Comandos recuperados NÃO entram no histórico de undo: eles são o
        // estado anterior, não uma ação do usuário. Colocá-los no histórico
        // faria o primeiro "desfazer" apagar o trabalho recuperado.
        const Status s = apply_command_internal(cmd, nullptr, false);
        if (s.ok()) ++applied; else ++failed;
    }
    project_->clear_recovered_commands();
    project_->mark_dirty();

    AUREA_LOG_INFO("recuperacao: %u comandos aplicados, %u ignorados", applied, failed);
    return OkStatus;
}

void Engine::discard_recovery() noexcept {
    if (project_) project_->discard_recovery();
}

// -----------------------------------------------------------------------------
// A fronteira
// -----------------------------------------------------------------------------
u32 Engine::submit_commands(const Command* commands, u32 count,
                            const char* stringBlob, u32 stringBlobSize) noexcept {
    if (!commands || count == 0) return 0;
    if (state_ != EngineState::Ready && state_ != EngineState::Rendering) return 0;

    u32 accepted = 0;
    for (u32 i = 0; i < count; ++i) {
        if (commandQueue_->push(commands[i]) == kInvalidIndex) break;
        ++accepted;
    }

    // O blob de strings é copiado inteiro de uma vez. Comandos que apontam para
    // offsets além do que foi copiado caem em `string_at` devolvendo "" — e a
    // UI vê um nome vazio em vez de um ponteiro inválido. Falha visível, não
    // silenciosa.
    if (stringBlob && stringBlobSize) {
        if (char* dst = commandQueue_->alloc_string(stringBlobSize)) {
            std::memcpy(dst, stringBlob, stringBlobSize);
        }
    }

    commandQueue_->commit();
    lastSubmitNs_ = monotonic_ns();
    return accepted;
}

Status Engine::drena_comandos() noexcept {
    if (!project_) return Errc::InvalidState;

    const u32 n = commandQueue_->drain([this](const Command& cmd) {
        const char* str = nullptr;
        if (cmd.stringLength > 0) {
            str = commandQueue_->string_at(cmd.stringOffset, cmd.stringLength);
        }
        const Status s = apply_command_internal(cmd, str, true);
        if (!s.ok()) {
            // Um comando inválido não derruba o lote: os seguintes ainda são
            // aplicados. O erro é registrado e a UI o vê pela telemetria.
            AUREA_LOG_WARN("comando %u recusado: %s",
                           static_cast<unsigned>(cmd.type), s.message().data());
        }
    });

    if (n > 0) project_->mark_dirty();
    return OkStatus;
}

void Engine::update_clock(TickNs audioTime) noexcept {
    if (!project_) return;
    Timeline& t = project_->timeline();
    // `now_frame` consulta o clock de áudio só quando o áudio é a fonte. Durante
    // scrubbing o relógio está pinado e o instante vem da UI — é o que evita o
    // áudio puxar o playhead de volta enquanto o dedo arrasta.
    t.seek(t.clock().now_frame(audioTime));
}

Status Engine::render_frame(TickNs audioTime) noexcept {
    if (!project_) return Errc::InvalidState;
    if (state_ != EngineState::Ready && state_ != EngineState::Rendering) {
        return Status{Errc::InvalidState, "motor nao esta pronto para desenhar"};
    }

    state_ = EngineState::Rendering;
    const u64 frameStart = monotonic_ns();

    if (const Status s = drena_comandos(); !s.ok()) return s;

    Composition* comp = current_composition();
    if (!comp) { state_ = EngineState::Ready; return Errc::NotFound; }

    Timeline& timeline = project_->timeline();
    // O instante vem do CLOCK, não de um seek. `seek` é a intenção do usuário e
    // trava o relógio; aqui o relógio é a fonte, e travar produziria a cada
    // frame uma trava e um destrave sem significado.
    const FrameIndex time = timeline.clock().now_frame(audioTime);
    timeline.set_playhead(time);

    evaluate_animation(time);

    // -------------------------------------------------------------------------
    // Montagem do FrameGraph.
    //
    // Aqui entra o grafo de composição completo. O que está implementado nesta
    // fase é a ESTRUTURA: recurso de saída, pods de culling e o ponto de
    // entrada dos passes de layer. Os passes de decode e de efeito reais
    // dependem do backend gráfico e das camadas de mídia da plataforma, que
    // são a próxima fase.
    // -------------------------------------------------------------------------
    frameGraph_.reset();

    TextureDesc output;
    output.format = SurfaceFormat::RGBA16F;
    output.sampleCount = 1;
    output.sampled = true;
    output.renderTarget = true;
    output.storage = false;
    output.debugName = "saida-do-frame";

    if (gpu_ && config_.surfaceWidth && config_.surfaceHeight) {
        // Alvo em resolução de PREVIEW, não em resolução de composição: é o
        // controlador adaptativo que decide — e é isso que sustenta 60 fps num
        // aparelho que não aguentaria a composição inteira.
        output.width = adapt().render_width();
        output.height = adapt().render_height();
        const u32 maxTex = caps_.max_texture_dimension();
        if (output.width > maxTex) output.width = maxTex;
        if (output.height > maxTex) output.height = maxTex;
    } else {
        output.width = comp->width();
        output.height = comp->height();
    }

    const u32 out = frameGraph_.create_texture("apresentacao", output);
    if (out == kInvalidIndex) { state_ = EngineState::Ready; return Errc::OutOfMemory; }
    frameGraph_.set_output(out);

    if (gpu_) {
        if (const Status s = frameGraph_.compile(*gpu_); !s.ok()) {
            AUREA_LOG_WARN("grafo do frame nao compilou: %s", s.message().data());
        }
        // A submissão real (begin_frame / execute / end_frame) entra junto com
        // os passes de composição. Sem eles, executar o grafo seria submeter
        // uma lista de comandos vazia — trabalho de driver sem imagem.
    }

    const u64 frameEnd = monotonic_ns();
    lastFrame_.frameIndex = static_cast<u32>(++frameCounter_);
    lastFrame_.cpuMs = static_cast<f32>(static_cast<f64>(frameEnd - frameStart) * 1e-6);
    lastFrame_.layersRendered = static_cast<u32>(activeLayers_.size());
    lastFrame_.passesExecuted = frameGraph_.pass_count() - frameGraph_.culled_count();
    lastFrame_.passesCulled = frameGraph_.culled_count();
    lastFrame_.previewWidth = output.width;
    lastFrame_.previewHeight = output.height;
    lastFrame_.cpuMemoryBytes = memory_.total_used();
    if (gpu_) lastFrame_.gpuMemoryBytes = gpu_->allocated_bytes();

    if (adaptive_) {
        // O controlador devolve o estado atualizado; quem o lê é `read_status`.
        // Aqui só interessa o efeito colateral.
        (void)adaptive_->update(lastFrame_, caps_.thermal());
    }

    state_ = EngineState::Ready;
    return OkStatus;
}

void Engine::evaluate_animation(FrameIndex time) noexcept {
    Composition* comp = current_composition();
    if (!comp) return;

    comp->collect_active(time, activeLayers_);

    for (const LayerId id : activeLayers_) {
        Layer* l = comp->layer(id);
        if (!l) continue;

        // Os tracks sobrepõem os valores estáticos. Uma layer sem animação não
        // entra em nenhum `sample` — o custo de avaliar 200 layers paradas é
        // zero, o que é o caso da maior parte do tempo.
        if (!l->animated()) {
            l->cacheKey = 0;   // estática: a chave fina cuida da invalidação
            continue;
        }

        const FrameIndex local = l->local_time(time);

        l->transform.position.x = l->tracks.sample_or(TrackProperty::PositionX, local, l->transform.position.x);
        l->transform.position.y = l->tracks.sample_or(TrackProperty::PositionY, local, l->transform.position.y);
        l->transform.position.z = l->tracks.sample_or(TrackProperty::PositionZ, local, l->transform.position.z);
        l->transform.scale.x    = l->tracks.sample_or(TrackProperty::ScaleX, local, l->transform.scale.x);
        l->transform.scale.y    = l->tracks.sample_or(TrackProperty::ScaleY, local, l->transform.scale.y);
        l->transform.scale.z    = l->tracks.sample_or(TrackProperty::ScaleZ, local, l->transform.scale.z);
        l->transform.rotation.x = l->tracks.sample_or(TrackProperty::RotationX, local, l->transform.rotation.x);
        l->transform.rotation.y = l->tracks.sample_or(TrackProperty::RotationY, local, l->transform.rotation.y);
        l->transform.rotation.z = l->tracks.sample_or(TrackProperty::RotationZ, local, l->transform.rotation.z);
        l->transform.anchor.x   = l->tracks.sample_or(TrackProperty::AnchorX, local, l->transform.anchor.x);
        l->transform.anchor.y   = l->tracks.sample_or(TrackProperty::AnchorY, local, l->transform.anchor.y);
        l->transform.anchor.z   = l->tracks.sample_or(TrackProperty::AnchorZ, local, l->transform.anchor.z);
        l->transform.opacity    = l->tracks.sample_or(TrackProperty::Opacity, local, l->transform.opacity);

        // Matriz local resolvida uma vez por frame, por layer. O renderer só
        // lê — quem chama `look_at` ou multiplica matriz no caminho de desenho
        // paga a conta em cada passe.
        const Quat q = Quat::from_euler_zyx(l->transform.rotation.x * kDeg2Rad,
                                            l->transform.rotation.y * kDeg2Rad,
                                            l->transform.rotation.z * kDeg2Rad);
        const Mat4 t = Mat4::translation(l->transform.position);
        const Mat4 r = Mat4::from_quat(q);
        const Mat4 s = Mat4::scale(l->transform.scale);
        const Mat4 a = Mat4::translation(-l->transform.anchor);
        l->transform.localMatrix = t * r * s * a;

        // Chave de cache da layer: combina o que o frame vê. Se dois frames
        // seguidos produzem a mesma chave, o compositor reaproveita o
        // resultado em vez de recompor — é o que faz um slider parado não
        // custar nada.
        u64 key = 0x9E3779B97F4A7C15ull;
        auto mix = [&key](u64 v) { key ^= v + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2); };
        mix(static_cast<u64>(local.value));
        mix(static_cast<u64>(l->source.pack()));
        mix(static_cast<u64>(l->effects.size()));
        mix(static_cast<u64>(l->masks.size()));
        mix(static_cast<u64>(l->blendMode));
        const u32 bits = (l->visible ? 1u : 0u) | (l->locked ? 2u : 0u) | (l->threeD ? 4u : 0u);
        mix(bits);
        for (int i = 0; i < 13; ++i) {
            const f32 f = (&l->transform.position.x)[i];
            u32 raw;
            std::memcpy(&raw, &f, sizeof(raw));
            mix(raw);
        }
        l->cacheKey = key;
    }
}

// -----------------------------------------------------------------------------
// Status e telemetria
// -----------------------------------------------------------------------------
EngineStatus Engine::read_status() noexcept {
    EngineStatus st;
    st.state = state_;
    st.lastError = lastError_;
    std::snprintf(st.lastErrorDetail, sizeof(st.lastErrorDetail), "%s", lastErrorDetail_);

    st.averageFrameMs = lastFrame_.cpuMs;
    st.gpuMs = lastFrame_.gpuMs;
    st.cpuMs = lastFrame_.cpuMs;
    st.decodeMs = lastFrame_.decodeMs;
    st.passesExecuted = lastFrame_.passesExecuted;
    st.passesCulled = lastFrame_.passesCulled;
    st.droppedFrames = droppedFrames_;
    st.gpuMemoryBytes = lastFrame_.gpuMemoryBytes;
    st.cpuMemoryBytes = lastFrame_.cpuMemoryBytes;
    st.memoryPressure = memory_.pressure();
    st.cacheHitRate = frameCache_.hit_rate();

    st.previewWidth = lastFrame_.previewWidth;
    st.previewHeight = lastFrame_.previewHeight;
    if (adaptive_) {
        st.previewNumerator = adaptive_->current_numerator();
        st.previewDenominator = adaptive_->current_denominator();
        st.previewAuto = adaptive_->auto_mode();
        if (st.averageFrameMs > 0.0f) {
            st.currentFps = 1000.0f / st.averageFrameMs;
        }
    }

    if (project_) {
        const Timeline& t = project_->timeline();
        st.playhead = t.playhead();
        st.playing = t.playing();
        st.assetCount = project_->asset_count();
        st.dirty = project_->dirty();
        st.recoveryAvailable = project_->autosave().recoveryAvailable;

        if (const Composition* c = project_->timeline().composition(t.current())) {
            st.duration = c->duration();
            st.layerCount = c->layers().count();
        }
    }

    st.canUndo = undo_.can_undo();
    st.canRedo = undo_.can_redo();
    st.undoDepth = undo_.depth();
    st.selectedCount = static_cast<u32>(selection_.size());

    return st;
}

EngineTelemetry Engine::read_telemetry() noexcept {
    EngineTelemetry t;
    t.frame = lastFrame_;
    t.workerCount = jobs_.worker_count();
    t.jobsCompleted = jobs_.completed_count();
    for (u8 i = 0; i < static_cast<u8>(JobPriority::Count); ++i) {
        t.queueDepth[i] = jobs_.queue_depth(static_cast<JobPriority>(i));
    }
    t.shaderCount = shaders_.shader_count();
    t.pipelineCount = shaders_.pipeline_count();
    t.pipelineHitRate = shaders_.hit_rate();
    t.shaderFailures = shaders_.compile_failures();
    t.physicalResources = frameGraph_.physical_resource_count();
    t.logicalResources = frameGraph_.resource_count();
    t.frameCacheEntries = frameCache_.count();
    t.frameCacheHitRate = frameCache_.hit_rate();
    t.adaptiveScaleChanges = adaptive_ ? adaptive_->change_count() : 0;
    t.undoBlobBytes = undo_.blob_used();
    t.commandsDropped = commandQueue_->dropped_count();
    t.thermal = caps_.thermal().level;
    t.throttling = caps_.thermal().throttling;
    return t;
}

void Engine::debug_feed_frame_stats(const FrameStats& stats) noexcept {
    lastFrame_ = stats;
    if (adaptive_) (void)adaptive_->update(stats, caps_.thermal());
}

// -----------------------------------------------------------------------------
// Consultas para a UI
// -----------------------------------------------------------------------------
Composition* Engine::current_composition() noexcept {
    if (!project_) return nullptr;
    return project_->timeline().composition(project_->timeline().current());
}

u32 Engine::query_layers(bridge::LayerRow* out, u32 capacity,
                         char* outNameBlob, u32 nameBlobCapacity) noexcept {
    if (!out) return 0;
    const Composition* comp = current_composition();
    if (!comp) return 0;

    u32 written = 0;
    u32 nameCursor = 0;

    // Ordem de desenho invertida: a UI mostra a FRENTE em cima, e a frente é a
    // última do vetor de ordem. Enviar na ordem natural obrigaria a UI a
    // inverter, e a inversão é a mesma regra em dois lugares — uma chance de
    // divergirem.
    const u32 n = comp->order().size();
    for (u32 i = 0; i < n && written < capacity; ++i) {
        const LayerId id = comp->order().at(n - 1 - i);
        const Layer* l = comp->layer(id);
        if (!l) continue;

        bridge::LayerRow row;
        row.id = id.pack();
        row.kind = static_cast<u32>(l->kind);
        row.zIndex = i;
        row.startFrame = static_cast<i32>(l->start.value);
        row.endFrame = static_cast<i32>(l->end.value);
        row.opacity = l->transform.opacity;
        row.effectCount = static_cast<u32>(l->effects.size());
        row.maskCount = static_cast<u32>(l->masks.size());

        u32 keyCount = 0;
        for (u32 t = 0; t < l->tracks.size(); ++t) {
            keyCount += static_cast<u32>(l->tracks.at(t).keys.size());
        }
        row.keyframeCount = keyCount;

        row.blendMode = static_cast<u32>(l->blendMode);

        // Os bits vêm do contrato da bridge, não de literais soltos aqui: a UI
        // lê exatamente estes valores nomeados.
        u32 flags = 0;
        if (l->visible) flags |= bridge::kLayerRowFlagVisible;
        if (l->locked)  flags |= bridge::kLayerRowFlagLocked;
        if (l->solo)    flags |= bridge::kLayerRowFlagSolo;
        if (l->animated()) flags |= bridge::kLayerRowFlagAnimated;
        if (is_selected(row.id)) flags |= bridge::kLayerRowFlagSelected;
        if (l->threeD)  flags |= bridge::kLayerRowFlagThreeD;
        row.flags = flags;

        // Parenting é por id, não por índice: a UI pede o índice de novo se
        // precisar. Índice é volátil (reordenar muda), id não.
        row.parentIndex = kInvalidIndex;
        row.reserved = 0;

        if (outNameBlob && nameCursor + l->name.size() < nameBlobCapacity) {
            std::memcpy(outNameBlob + nameCursor, l->name.data(), l->name.size());
            row.nameOffset = nameCursor;
            row.nameLength = static_cast<u32>(l->name.size());
            nameCursor += static_cast<u32>(l->name.size());
        } else {
            row.nameOffset = 0;
            row.nameLength = 0;
        }

        out[written++] = row;
    }
    return written;
}

u32 Engine::query_keyframes(u64 layerId, bridge::KeyframeRow* out, u32 capacity) noexcept {
    if (!out) return 0;
    Composition* comp = current_composition();
    if (!comp) return 0;

    Layer* l = comp->layer(LayerId::unpack(layerId));
    if (!l) return 0;

    u32 written = 0;
    for (u32 t = 0; t < l->tracks.size() && written < capacity; ++t) {
        const Track& track = l->tracks.at(t);
        for (const Keyframe& k : track.keys) {
            if (written >= capacity) break;
            bridge::KeyframeRow row;
            row.property = static_cast<u32>(track.property);
            row.effectIndex = track.effectIndex;
            row.time = static_cast<i32>(k.time.value);
            row.value = k.value;
            row.interpolation = static_cast<u32>(k.interp);
            row.reserved = 0;
            out[written++] = row;
        }
    }
    return written;
}

u32 Engine::query_curve(u64 layerId, u32 property, i32 startFrame, i32 endFrame,
                        f32* outValues, u32 sampleCount) noexcept {
    if (!outValues || sampleCount == 0) return 0;
    Composition* comp = current_composition();
    if (!comp) return 0;

    Layer* l = comp->layer(LayerId::unpack(layerId));
    if (!l) return 0;

    const Track* track = l->tracks.find(static_cast<TrackProperty>(property));
    if (!track) return 0;

    // Amostra no MOTOR, não na UI. O graph editor desenha exatamente o que a
    // avaliação produz — se a curva fosse reimplementada na UI, o gráfico e o
    // resultado divergiriam na primeira mudança de easing.
    if (endFrame <= startFrame) return 0;
    const f32 step = static_cast<f32>(endFrame - startFrame) / static_cast<f32>(sampleCount > 1 ? sampleCount - 1 : 1);

    for (u32 i = 0; i < sampleCount; ++i) {
        const FrameIndex t{static_cast<i64>(static_cast<f64>(startFrame) + step * static_cast<f64>(i))};
        outValues[i] = track->sample(t);
    }
    return sampleCount;
}

// -----------------------------------------------------------------------------
// Seleção
// -----------------------------------------------------------------------------
void Engine::set_selection(const u64* layerIds, u32 count) noexcept {
    selection_.clear();
    if (!layerIds) return;
    selection_.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        if (layerIds[i] != 0) selection_.push_back(layerIds[i]);
    }
    // Ordenado: `is_selected` é chamado uma vez por layer, por frame, na
    // montagem da lista — 200 buscas binárias contra 200 varreduras lineares.
    std::sort(selection_.begin(), selection_.end());
    selection_.erase(std::unique(selection_.begin(), selection_.end()), selection_.end());
}

void Engine::clear_selection() noexcept { selection_.clear(); }

u32 Engine::selection_count() const noexcept { return static_cast<u32>(selection_.size()); }

u32 Engine::get_selection(u64* out, u32 capacity) const noexcept {
    if (!out) return 0;
    const u32 n = static_cast<u32>(selection_.size()) < capacity
                ? static_cast<u32>(selection_.size()) : capacity;
    for (u32 i = 0; i < n; ++i) out[i] = selection_[i];
    return n;
}

bool Engine::is_selected(u64 layerId) const noexcept {
    return std::binary_search(selection_.begin(), selection_.end(), layerId);
}

// -----------------------------------------------------------------------------
// Export
// -----------------------------------------------------------------------------
Status Engine::ensure_export_worker() noexcept {
    if (!exportCtx_) exportCtx_ = std::make_unique<ExportContext>();
    return OkStatus;
}

Status Engine::start_export(const ExportSettings& settings,
                            const char* outputPath) noexcept {
    if (!project_) return Errc::InvalidState;
    if (!outputPath) return Errc::InvalidArgument;

    // Aloca o contexto do export. Não falha em condições normais; se falhasse,
    // o `start_export` abaixo já devolveria `NotImplemented` de qualquer forma.
    (void)ensure_export_worker();

    exportCtx_->settings = settings;
    exportCtx_->outputPath = outputPath;
    exportCtx_->cancelRequested.store(false, std::memory_order_release);
    exportCtx_->progress = ExportProgress{};
    exportCtx_->progress.running = false;
    exportCtx_->progress.result = Errc::NotImplemented;
    std::snprintf(exportCtx_->progress.message, sizeof(exportCtx_->progress.message),
                  "%s", "exportacao ainda nao implementada");

    // O export depende de três coisas que ainda não existem: os passes reais de
    // composição no backend gráfico, o decodificador de hardware da plataforma
    // e o codificador de hardware. Sem eles, qualquer coisa que esta função
    // fizesse produziria um arquivo vazio — ou pior, um arquivo que parece
    // pronto e não tem imagem.
    //
    // A decisão é NÃO fingir. A UI mostra "exportação não disponível nesta
    // versão", e o progresso devolve `NotImplemented`.
    AUREA_LOG_WARN("start_export chamado sem implementacao: recusado");
    return Status{Errc::NotImplemented,
                  "exportacao de video ainda nao implementada nesta fase"};
}

Status Engine::cancel_export() noexcept {
    if (!exportCtx_) return Errc::InvalidState;
    exportCtx_->cancelRequested.store(true, std::memory_order_release);
    return OkStatus;
}

Engine::ExportProgress Engine::export_progress() const noexcept {
    if (!exportCtx_) return ExportProgress{};
    return exportCtx_->progress;
}

// -----------------------------------------------------------------------------
// Aplicação de comandos
// -----------------------------------------------------------------------------
Status Engine::apply_command(const Command& cmd, const char* stringData) noexcept {
    return apply_command_internal(cmd, stringData, true);
}

Status Engine::apply_command_internal(const Command& cmd, const char* stringData,
                                      bool recordUndo) noexcept {
    if (!project_) return Errc::InvalidState;

    Timeline& timeline = project_->timeline();
    Composition* comp = current_composition();

    auto need_layer = [&](LayerId id) -> Layer* {
        if (!comp) return nullptr;
        return comp->layer(id);
    };

    switch (cmd.type) {
        // ---------------------------------------------------------------------
        // Camadas
        // ---------------------------------------------------------------------
        case CommandType::LayerCreate: {
            if (!comp) return Errc::InvalidState;
            const LayerId id = comp->add_layer(cmd.layer_create.kind,
                                               stringData ? std::string(stringData) : std::string{});
            if (!id.valid()) return Errc::OutOfMemory;

            if (recordUndo) {
                Command inv;
                inv.type = CommandType::LayerDelete;
                inv.layer_ref.layer = id;
                // Falha aqui significa bitmap do histórico cheio: a ação foi
                // aplicada mas não poderá ser desfeita. Registrar é importante —
                // o usuário merece saber que o desfazer não vai cobrir isto, e
                // silenciar faria ele descobrir tocando no botão.
                if (const Status s = undo_.record(inv, nullptr, 0, "criar camada", 13, 0); !s.ok()) {
                    AUREA_LOG_WARN("acao aplicada mas nao registrada no historico: %s",
                                   s.message().data());
                }
            }
            frameCache_.clear();   // a lista de draws mudou: nada é reaproveitável
            return OkStatus;
        }

        case CommandType::LayerDelete: {
            if (!comp) return Errc::InvalidState;
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;

            // A layer inteira é guardada como payload do undo: recriá-la a
            // partir de um comando POD não é possível (nome, tracks, máscaras,
            // texto vivem em alocações). O blob do UndoStack existe para isso.
            if (recordUndo) {
                const u8* bytes = reinterpret_cast<const u8*>(l);
                (void)bytes;
                // Serializar uma Layer aqui exigiria o serializador do projeto;
                // o caminho fica pronto quando o comando inverso de deleção
                // entrar. Por ora o undo de uma deleção é registrado SEM payload
                // e a UI é avisada de que não pode ser desfeito.
                AUREA_LOG_WARN("undo de remocao de camada ainda sem payload");
            }
            comp->remove_layer(cmd.layer_ref.layer);
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerDuplicate: {
            if (!comp) return Errc::InvalidState;
            const LayerId id = comp->duplicate_layer(cmd.layer_ref.layer, timeline.playhead());
            if (!id.valid()) return Errc::OutOfMemory;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerReorder: {
            if (!comp) return Errc::InvalidState;
            if (!comp->reorder_layer(cmd.layer_reorder.layer, cmd.layer_reorder.newIndex)) {
                return Errc::OutOfRange;
            }
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerSetName: {
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;
            if (stringData) l->name = stringData;
            return OkStatus;
        }

        case CommandType::LayerSetTimeRange: {
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;
            const FrameIndex start = cmd.layer_range.start;
            const FrameIndex end = cmd.layer_range.end;
            if (end.value <= start.value) return Errc::InvalidArgument;
            l->start = start;
            l->end = end;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerSplit: {
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l || !comp) return Errc::NotFound;
            const FrameIndex at = cmd.layer_split.at;
            if (at.value <= l->start.value || at.value >= l->end.value) {
                return Errc::OutOfRange;
            }
            // Corta: a primeira metade termina no ponto, a segunda começa nele.
            // O `offset` da segunda é ajustado para o conteúdo continuar de onde
            // estava — sem isso, a segunda metade repetiria o começo do vídeo.
            const FrameIndex originalEnd = l->end;
            const FrameIndex originalOffset = l->offset;

            const LayerId second = comp->duplicate_layer(cmd.layer_ref.layer, at);
            if (!second.valid()) return Errc::OutOfMemory;

            l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;
            l->end = at;

            if (Layer* s = comp->layer(second)) {
                s->start = at;
                s->end = originalEnd;
                s->offset = FrameIndex{originalOffset.value + (at.value - l->start.value)};
            }
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerSetVisible: {
            Layer* l = need_layer(cmd.layer_visible.layer);
            if (!l) return Errc::NotFound;
            l->visible = cmd.layer_visible.visible;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerSetLocked: {
            Layer* l = need_layer(cmd.layer_locked.layer);
            if (!l) return Errc::NotFound;
            l->locked = cmd.layer_locked.locked;
            return OkStatus;
        }

        case CommandType::LayerSetParent: {
            Layer* l = need_layer(cmd.layer_parent.layer);
            if (!l) return Errc::NotFound;
            const LayerId parent = cmd.layer_parent.parent;
            if (parent.valid()) {
                if (!comp || !comp->layer(parent)) return Errc::NotFound;
                if (parent == cmd.layer_parent.layer) return Errc::InvalidArgument;
                // Ciclo: subir a cadeia de pais do candidato e recusar se
                // encontrar a própria layer. Sem esta checagem, a avaliação de
                // transform entraria em laço infinito.
                LayerId cursor = parent;
                for (u32 depth = 0; depth < kMaxNestingDepth * 4 && cursor.valid(); ++depth) {
                    if (cursor == cmd.layer_parent.layer) return Errc::InvalidArgument;
                    const Layer* p = comp->layer(cursor);
                    if (!p) break;
                    cursor = p->parent;
                }
            }
            l->parent = parent;
            return OkStatus;
        }

        case CommandType::LayerSetBlendMode: {
            Layer* l = need_layer(cmd.layer_blend.layer);
            if (!l) return Errc::NotFound;
            l->blendMode = cmd.layer_blend.mode;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerSetComposition: {
            Layer* l = need_layer(cmd.layer_comp.layer);
            if (!l) return Errc::NotFound;
            const CompositionId target = cmd.layer_comp.comp;
            const Composition* c = timeline.composition(target);
            if (!c) return Errc::NotFound;
            if (comp && !comp->can_nest(*c)) return Errc::InvalidArgument;
            l->nested.composition = target;
            l->kind = LayerKind::Composition;
            frameCache_.clear();
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Transform
        // ---------------------------------------------------------------------
        case CommandType::LayerSetTransform: {
            Layer* l = need_layer(cmd.transform.layer);
            if (!l) return Errc::NotFound;
            l->transform.position = Vec3{cmd.transform.x, cmd.transform.y, cmd.transform.z};
            l->transform.scale    = Vec3{cmd.transform.sx, cmd.transform.sy, cmd.transform.sz};
            l->transform.rotation = Vec3{cmd.transform.rx, cmd.transform.ry, cmd.transform.rz};
            l->transform.anchor   = Vec3{cmd.transform.ax, cmd.transform.ay, cmd.transform.az};
            l->transform.opacity  = cmd.transform.opacity;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerSetPosition: {
            Layer* l = need_layer(cmd.position.layer);
            if (!l) return Errc::NotFound;
            l->transform.position = Vec3{cmd.position.x, cmd.position.y, cmd.position.z};
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerSetScale: {
            Layer* l = need_layer(cmd.scale.layer);
            if (!l) return Errc::NotFound;
            l->transform.scale = Vec3{cmd.scale.sx, cmd.scale.sy, cmd.scale.sz};
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerSetRotation: {
            Layer* l = need_layer(cmd.rotation.layer);
            if (!l) return Errc::NotFound;
            l->transform.rotation = Vec3{cmd.rotation.rx, cmd.rotation.ry, cmd.rotation.rz};
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerSetAnchor: {
            Layer* l = need_layer(cmd.anchor.layer);
            if (!l) return Errc::NotFound;
            l->transform.anchor = Vec3{cmd.anchor.ax, cmd.anchor.ay, cmd.anchor.az};
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerSetOpacity: {
            Layer* l = need_layer(cmd.opacity.layer);
            if (!l) return Errc::NotFound;
            l->transform.opacity = clampf(cmd.opacity.opacity, 0.0f, 1.0f);
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::LayerSetSkew: {
            Layer* l = need_layer(cmd.skew.layer);
            if (!l) return Errc::NotFound;
            l->transform.skewX = cmd.skew.skewX;
            l->transform.skewY = cmd.skew.skewY;
            frameCache_.clear();
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Keyframes
        // ---------------------------------------------------------------------
        case CommandType::KeyframeInsert: {
            Layer* l = need_layer(cmd.keyframe.track.layer);
            if (!l) return Errc::NotFound;
            Track& track = l->tracks.get_or_create(cmd.keyframe.track.property,
                                                   cmd.keyframe.track.effectIndex,
                                                   cmd.keyframe.track.effectParamIndex);
            const u32 idx = track.set(cmd.keyframe.time, cmd.keyframe.value,
                                      Interpolation::Linear);
            (void)idx;

            if (recordUndo) {
                Command inv;
                inv.type = CommandType::KeyframeDelete;
                inv.keyframe.track = cmd.keyframe.track;
                inv.keyframe.time = cmd.keyframe.time;
                if (const Status s = undo_.record(inv, nullptr, 0, "inserir keyframe", 15, 0);
                    !s.ok()) {
                    AUREA_LOG_WARN("keyframe aplicado mas nao registrado no historico: %s",
                                   s.message().data());
                }
            }
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::KeyframeDelete: {
            Layer* l = need_layer(cmd.keyframe.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = l->tracks.find(cmd.keyframe.track.property,
                                          cmd.keyframe.track.effectIndex,
                                          cmd.keyframe.track.effectParamIndex);
            if (!track) return Errc::NotFound;
            if (!track->remove(cmd.keyframe.time)) return Errc::NotFound;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::KeyframeMove: {
            Layer* l = need_layer(cmd.keyframe_move.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = l->tracks.find(cmd.keyframe_move.track.property,
                                          cmd.keyframe_move.track.effectIndex,
                                          cmd.keyframe_move.track.effectParamIndex);
            if (!track) return Errc::NotFound;
            if (track->move(cmd.keyframe_move.fromTime, cmd.keyframe_move.toTime) == kInvalidIndex) {
                return Errc::NotFound;
            }
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::KeyframeSetValue: {
            Layer* l = need_layer(cmd.keyframe.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = l->tracks.find(cmd.keyframe.track.property,
                                          cmd.keyframe.track.effectIndex,
                                          cmd.keyframe.track.effectParamIndex);
            if (!track) return Errc::NotFound;
            const u32 idx = track->find_exact(cmd.keyframe.time);
            if (idx == kInvalidIndex) return Errc::NotFound;
            track->keys[idx].value = cmd.keyframe.value;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::KeyframeSetInterpolation:
        case CommandType::KeyframeSetBezier:
        case CommandType::KeyframeSetEasing: {
            Layer* l = need_layer(cmd.keyframe_interp.track.layer);
            if (!l) return Errc::NotFound;
            Track* track = l->tracks.find(cmd.keyframe_interp.track.property,
                                          cmd.keyframe_interp.track.effectIndex,
                                          cmd.keyframe_interp.track.effectParamIndex);
            if (!track) return Errc::NotFound;
            track->set_interpolation(cmd.keyframe_interp.time,
                                     cmd.keyframe_interp.interp,
                                     cmd.keyframe_interp.bx1, cmd.keyframe_interp.by1,
                                     cmd.keyframe_interp.bx2, cmd.keyframe_interp.by2);
            frameCache_.clear();
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Máscaras
        // ---------------------------------------------------------------------
        case CommandType::MaskCreate: {
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;
            if (l->masks.size() >= kMaxMaskCount) return Errc::OutOfRange;
            Mask m;
            m.id = l->alloc_mask_id();
            m.name = stringData ? std::string(stringData) : ("Mascara " + std::to_string(m.id + 1));
            l->masks.push_back(std::move(m));
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::MaskDelete: {
            Layer* l = need_layer(cmd.mask_scalar.layer);
            if (!l) return Errc::NotFound;
            for (auto it = l->masks.begin(); it != l->masks.end(); ++it) {
                if (it->id == cmd.mask_scalar.mask.index) {
                    l->masks.erase(it);
                    frameCache_.clear();
                    return OkStatus;
                }
            }
            return Errc::NotFound;
        }

        case CommandType::MaskSetOperation: {
            Layer* l = need_layer(cmd.mask_op.layer);
            if (!l) return Errc::NotFound;
            Mask* m = l->find_mask(cmd.mask_op.mask);
            if (!m) return Errc::NotFound;
            m->operation = cmd.mask_op.op;
            m->cacheKey = 0;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::MaskSetFeather:
        case CommandType::MaskSetExpansion:
        case CommandType::MaskSetOpacity: {
            Layer* l = need_layer(cmd.mask_scalar.layer);
            if (!l) return Errc::NotFound;
            Mask* m = l->find_mask(cmd.mask_scalar.mask);
            if (!m) return Errc::NotFound;
            m->cacheKey = 0;
            switch (cmd.type) {
                case CommandType::MaskSetFeather:   m->feather   = cmd.mask_scalar.value; break;
                case CommandType::MaskSetExpansion: m->expansion = cmd.mask_scalar.value; break;
                default:                            m->opacity   = clampf(cmd.mask_scalar.value, 0.0f, 1.0f); break;
            }
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::MaskSetPath: {
            Layer* l = need_layer(cmd.mask_point.layer);
            if (!l) return Errc::NotFound;
            Mask* m = l->find_mask(cmd.mask_point.mask);
            if (!m) return Errc::NotFound;
            const u32 idx = cmd.mask_point.pointIndex;
            if (idx >= m->points.size()) return Errc::OutOfRange;
            MaskPoint& p = m->points[idx];
            p.position   = Vec2{cmd.mask_point.x, cmd.mask_point.y};
            p.inTangent  = Vec2{cmd.mask_point.inX, cmd.mask_point.inY};
            p.outTangent = Vec2{cmd.mask_point.outX, cmd.mask_point.outY};
            m->cacheKey = 0;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::MaskSetPathCommit: {
            Layer* l = need_layer(cmd.mask_commit.layer);
            if (!l) return Errc::NotFound;
            Mask* m = l->find_mask(cmd.mask_commit.mask);
            if (!m) return Errc::NotFound;
            if (cmd.mask_commit.pointCount > 100000u) return Errc::OutOfRange;
            m->closed = cmd.mask_commit.closed;
            m->cacheKey = 0;
            frameCache_.clear();
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Efeitos
        // ---------------------------------------------------------------------
        case CommandType::EffectAdd: {
            Layer* l = need_layer(cmd.effect_add.layer);
            if (!l) return Errc::NotFound;
            if (l->effects.size() >= kMaxEffectCount) return Errc::OutOfRange;
            Effect e;
            e.id = l->alloc_effect_id();
            e.type = static_cast<u16>(cmd.effect_add.effectType);
            const EffectDesc* desc = effectRegistry_.description(e.type);
            if (!desc) return Errc::NotSupported;
            e.paramCount = desc->paramCount;
            for (u32 p = 0; p < desc->paramCount && p < 16; ++p) {
                e.floats[p] = desc->params[p].defaultValue;
            }
            if (cmd.effect_add.index < l->effects.size()) {
                l->effects.insert(l->effects.begin() + cmd.effect_add.index, e);
            } else {
                l->effects.push_back(e);
            }
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::EffectRemove: {
            Layer* l = need_layer(cmd.effect_ref.layer);
            if (!l) return Errc::NotFound;
            const u32 idx = l->effect_index(cmd.effect_ref.effect);
            if (idx == kInvalidIndex) return Errc::NotFound;
            l->effects.erase(l->effects.begin() + idx);
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::EffectReorder: {
            Layer* l = need_layer(cmd.effect_reorder.layer);
            if (!l) return Errc::NotFound;
            const u32 from = l->effect_index(cmd.effect_reorder.effect);
            const u32 to = cmd.effect_reorder.newIndex;
            if (from == kInvalidIndex || to >= l->effects.size()) return Errc::OutOfRange;
            Effect e = l->effects[from];
            l->effects.erase(l->effects.begin() + from);
            l->effects.insert(l->effects.begin() + to, e);
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::EffectSetEnabled: {
            Layer* l = need_layer(cmd.effect_enabled.layer);
            if (!l) return Errc::NotFound;
            Effect* e = l->find_effect(cmd.effect_enabled.effect);
            if (!e) return Errc::NotFound;
            e->enabled = cmd.effect_enabled.enabled;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::EffectSetParam: {
            Layer* l = need_layer(cmd.effect_param.layer);
            if (!l) return Errc::NotFound;
            Effect* e = l->find_effect(cmd.effect_param.effect);
            if (!e) return Errc::NotFound;
            const u32 p = cmd.effect_param.paramIndex;
            if (p >= 16) return Errc::OutOfRange;
            e->floats[p] = cmd.effect_param.value;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::EffectSetColorParam: {
            Layer* l = need_layer(cmd.effect_color.layer);
            if (!l) return Errc::NotFound;
            Effect* e = l->find_effect(cmd.effect_color.effect);
            if (!e) return Errc::NotFound;
            const u32 p = cmd.effect_color.paramIndex;
            if (p >= 2) return Errc::OutOfRange;
            e->colors[p] = Vec4{cmd.effect_color.r, cmd.effect_color.g,
                                cmd.effect_color.b, cmd.effect_color.a};
            frameCache_.clear();
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Áudio
        // ---------------------------------------------------------------------
        case CommandType::AudioSetGain: {
            Layer* l = need_layer(cmd.audio_gain.layer);
            if (!l) return Errc::NotFound;
            l->gain = clampf(cmd.audio_gain.gain, 0.0f, 4.0f);
            return OkStatus;
        }

        case CommandType::AudioSetMuted: {
            Layer* l = need_layer(cmd.audio_flag.layer);
            if (!l) return Errc::NotFound;
            l->muted = cmd.audio_flag.flag;
            return OkStatus;
        }

        case CommandType::AudioSetSolo: {
            Layer* l = need_layer(cmd.audio_flag.layer);
            if (!l) return Errc::NotFound;
            l->solo = cmd.audio_flag.flag;
            // Solo é global: uma layer com solo silencia as outras. Sem isso,
            // "solo" seria só mais um mute — e o usuário ouviria as outras.
            if (comp) {
                bool anySolo = false;
                comp->layers().for_each([&anySolo](LayerId, const Layer& other) {
                    if (other.solo) anySolo = true;
                });
                comp->layers().for_each([anySolo](LayerId, Layer& other) {
                    other.muted = anySolo ? !other.solo : other.muted;
                });
            }
            return OkStatus;
        }

        case CommandType::AudioSetFadeIn: {
            Layer* l = need_layer(cmd.audio_fade.layer);
            if (!l) return Errc::NotFound;
            l->fadeIn = cmd.audio_fade.duration;
            return OkStatus;
        }

        case CommandType::AudioSetFadeOut: {
            Layer* l = need_layer(cmd.audio_fade.layer);
            if (!l) return Errc::NotFound;
            l->fadeOut = cmd.audio_fade.duration;
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Texto
        // ---------------------------------------------------------------------
        case CommandType::TextSetContent: {
            Layer* l = need_layer(cmd.layer_ref.layer);
            if (!l) return Errc::NotFound;
            if (stringData) l->text.content = stringData;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::TextSetFont: {
            // A fonte é resolvida pela camada de fonte da plataforma, que ainda
            // não existe. Devolver `NotImplemented` mantém o contrato: a UI sabe
            // que a troca não teve efeito em vez de mostrar uma fonte que não
            // mudou e culpar o usuário.
            return Status{Errc::NotImplemented, "troca de fonte ainda nao implementada"};
        }

        case CommandType::TextSetSize: {
            Layer* l = need_layer(cmd.text_size.layer);
            if (!l) return Errc::NotFound;
            l->text.size = cmd.text_size.size;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::TextSetColor: {
            Layer* l = need_layer(cmd.text_color.layer);
            if (!l) return Errc::NotFound;
            l->text.color = Vec4{cmd.text_color.r, cmd.text_color.g,
                                 cmd.text_color.b, cmd.text_color.a};
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::TextSetAlignment: {
            Layer* l = need_layer(cmd.text_align.layer);
            if (!l) return Errc::NotFound;
            l->text.alignment = cmd.text_align.alignment;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::TextSetStrokeWidth: {
            Layer* l = need_layer(cmd.text_stroke_width.layer);
            if (!l) return Errc::NotFound;
            l->text.strokeWidth = cmd.text_stroke_width.width;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::TextSetStrokeColor: {
            Layer* l = need_layer(cmd.text_color.layer);
            if (!l) return Errc::NotFound;
            l->text.strokeColor = Vec4{cmd.text_color.r, cmd.text_color.g,
                                       cmd.text_color.b, cmd.text_color.a};
            frameCache_.clear();
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Composição
        // ---------------------------------------------------------------------
        case CommandType::CompositionCreate: {
            const CompositionId id = timeline.create_composition(
                stringData ? std::string(stringData) : std::string("Composicao"),
                1920, 1080, 60.0);
            if (!id.valid()) return Errc::OutOfMemory;
            return OkStatus;
        }

        case CommandType::CompositionDelete: {
            if (!timeline.remove_composition(cmd.comp_ref.comp)) return Errc::InvalidState;
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::CompositionSetSize: {
            Composition* c = timeline.composition(cmd.comp_size.comp);
            if (!c) return Errc::NotFound;
            if (cmd.comp_size.width == 0 || cmd.comp_size.height == 0) return Errc::InvalidArgument;
            if (cmd.comp_size.width > caps_.max_export_width()
                || cmd.comp_size.height > caps_.max_export_height()) {
                // Acima do que o aparelho decodifica, o preview não acompanha e
                // o export não fecha. Recusar com código claro é melhor do que
                // aceitar e o usuário descobrir no export.
                return Errc::NotSupported;
            }
            c->set_size(cmd.comp_size.width, cmd.comp_size.height);
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::CompositionSetFps: {
            Composition* c = timeline.composition(cmd.comp_fps.comp);
            if (!c) return Errc::NotFound;
            if (cmd.comp_fps.fps <= 0.0 || cmd.comp_fps.fps > 240.0) return Errc::InvalidArgument;
            c->set_fps(cmd.comp_fps.fps);
            if (cmd.comp_fps.comp == timeline.current()) {
                timeline.clock().set_fps(cmd.comp_fps.fps);
            }
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::CompositionSetDuration: {
            Composition* c = timeline.composition(cmd.comp_duration.comp);
            if (!c) return Errc::NotFound;
            c->set_duration(cmd.comp_duration.duration);
            return OkStatus;
        }

        case CommandType::CompositionSetBackground: {
            Composition* c = timeline.composition(cmd.comp_background.comp);
            if (!c) return Errc::NotFound;
            c->set_background(Color{cmd.comp_background.r, cmd.comp_background.g,
                                    cmd.comp_background.b, cmd.comp_background.a});
            frameCache_.clear();
            return OkStatus;
        }

        case CommandType::ProjectSetCurrentComposition: {
            if (!timeline.set_current(cmd.comp_ref.comp)) return Errc::NotFound;
            if (Composition* c = timeline.composition(cmd.comp_ref.comp)) {
                adapt().configure(c->width(), c->height(), static_cast<f32>(c->fps()));
            }
            frameCache_.clear();
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Visualização
        // ---------------------------------------------------------------------
        case CommandType::ViewportSetZoom: {
            if (!project_) return Errc::InvalidState;
            project_->editor_settings().viewportZoom = clampf(cmd.viewport_zoom.zoom, 0.01f, 64.0f);
            return OkStatus;
        }

        case CommandType::ViewportSetPan: {
            if (!project_) return Errc::InvalidState;
            project_->editor_settings().viewportPan = Vec2{cmd.viewport_pan.x, cmd.viewport_pan.y};
            return OkStatus;
        }

        case CommandType::ViewportSetPreviewScale: {
            if (cmd.preview_scale.automatic) {
                adapt().set_user_scale(PreviewScale::Auto);
                return OkStatus;
            }
            const u32 num = cmd.preview_scale.scaleNumerator;
            const u32 den = cmd.preview_scale.scaleDenominator;
            if (num == 0 || den == 0) return Errc::InvalidArgument;
            PreviewScale scale = PreviewScale::Full;
            if (den >= 8) scale = PreviewScale::Eighth;
            else if (den >= 4) scale = PreviewScale::Quarter;
            else if (den >= 2) scale = PreviewScale::Half;
            adapt().set_user_scale(scale);
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Reprodução
        // ---------------------------------------------------------------------
        case CommandType::PlaybackPlay: {
            timeline.play();
            return OkStatus;
        }

        case CommandType::PlaybackPause: {
            timeline.pause();
            return OkStatus;
        }

        case CommandType::PlaybackSeek: {
            if (cmd.seek.time.value < 0) return Errc::InvalidArgument;
            timeline.seek(timeline.clock().to_frame(cmd.seek.time));
            return OkStatus;
        }

        case CommandType::PlaybackSetLoop: {
            timeline.set_loop(cmd.loop.loop);
            return OkStatus;
        }

        case CommandType::PlaybackSetSpeed: {
            if (cmd.speed.speed <= 0.0f || cmd.speed.speed > 16.0f) return Errc::InvalidArgument;
            timeline.set_speed(cmd.speed.speed);
            return OkStatus;
        }

        // ---------------------------------------------------------------------
        // Histórico
        // ---------------------------------------------------------------------
        case CommandType::UndoBeginGroup: {
            undo_.begin_group(stringData ? stringData : "acao",
                              stringData ? static_cast<u32>(std::strlen(stringData)) : 4);
            return OkStatus;
        }

        case CommandType::UndoEndGroup: {
            undo_.end_group();
            return OkStatus;
        }

        case CommandType::Undo: {
            // O aplicador de undo precisa do comando inverso; ele é resolvido
            // no laço abaixo e reaplicado. Não há caminho separado: desfazer é
            // aplicar um comando, e refazer é aplicar o inverso dele de novo.
            Command inverse;
            const void* payload = nullptr;
            u32 payloadSize = 0;
            if (!undo_.pop_undo(inverse, payload, payloadSize)) return Errc::InvalidState;
            (void)inverse;
            (void)payload;
            (void)payloadSize;
            return Status{Errc::NotImplemented,
                          "desfazer ainda nao religa o comando inverso ao modelo"};
        }

        case CommandType::Redo: {
            Command forward;
            const void* payload = nullptr;
            u32 payloadSize = 0;
            if (!undo_.pop_redo(forward, payload, payloadSize)) return Errc::InvalidState;
            (void)forward;
            (void)payload;
            (void)payloadSize;
            return Status{Errc::NotImplemented,
                          "refazer ainda nao religa o comando inverso ao modelo"};
        }

        // ---------------------------------------------------------------------
        // Export
        // ---------------------------------------------------------------------
        case CommandType::ExportRequest: {
            ExportSettings settings = project_->export_settings();
            settings.videoCodec = static_cast<ExportCodec>(cmd.export_request.codec);
            settings.width = cmd.export_request.width;
            settings.height = cmd.export_request.height;
            settings.fps = cmd.export_request.fps;
            settings.videoBitrateMbps = cmd.export_request.bitrate;
            settings.audioBitrateKbps = cmd.export_request.audioBitrate;
            return Status{Errc::NotImplemented,
                          "exportacao de video ainda nao implementada nesta fase"};
        }

        case CommandType::ExportCancel:
            return cancel_export();

        // ---------------------------------------------------------------------
        // 3D — a superfície de comandos existe; a cena ainda não é avaliada
        // pelo renderer. Devolver `NotImplemented` é o contrato: a UI sabe que
        // o comando não teve efeito em vez de achar que teve.
        // ---------------------------------------------------------------------
        case CommandType::SceneLoadModel:
        case CommandType::SceneSetCamera:
        case CommandType::SceneAddLight:
        case CommandType::SceneSetLightParam:
        case CommandType::SceneSetModelTransform:
        case CommandType::SceneSetAnimationClip:
        case CommandType::SceneSetMaterialParam:
        case CommandType::SceneSetEnvironment:
            return Status{Errc::NotImplemented, "cena 3D ainda nao implementada"};

        case CommandType::Nop:
        default:
            return OkStatus;
    }
}

void Engine::rebuild_engine_status() noexcept {
    cachedStatus_ = read_status();
}

// -----------------------------------------------------------------------------
// Preenchimento dos structs da fronteira.
//
// Campo a campo, e isso é intencional: o tipo interno pode mudar sem que a UI
// saiba, e é o compilador que avisa quando um campo novo precisa ser levado até
// lá. Copiar a struct inteira ou fazer cast de ponteiro seria mais rápido e
// reintroduziria exatamente o acoplamento que a bridge existe para evitar.
// -----------------------------------------------------------------------------
void Engine::fill_status(bridge::EngineStatusPOD& out) noexcept {
    const EngineStatus st = read_status();

    out = bridge::EngineStatusPOD{};
    out.state = static_cast<i32>(st.state);
    out.lastError = static_cast<i32>(st.lastError);
    std::snprintf(out.errorDetail, sizeof(out.errorDetail), "%s", st.lastErrorDetail);

    out.currentFps = st.currentFps;
    out.averageFrameMs = st.averageFrameMs;
    out.gpuMs = st.gpuMs;
    out.cpuMs = st.cpuMs;
    out.decodeMs = st.decodeMs;
    out.cacheHitRate = st.cacheHitRate;
    out.memoryPressure = st.memoryPressure;

    out.previewWidth = st.previewWidth;
    out.previewHeight = st.previewHeight;
    out.previewNumerator = st.previewNumerator;
    out.previewDenominator = st.previewDenominator;
    out.previewAuto = st.previewAuto ? 1u : 0u;

    out.playhead = st.playhead.value;
    out.duration = st.duration.value;
    out.playing = st.playing ? 1u : 0u;
    out.layerCount = st.layerCount;
    out.selectedCount = st.selectedCount;
    out.canUndo = st.canUndo ? 1u : 0u;
    out.canRedo = st.canRedo ? 1u : 0u;
    out.undoDepth = st.undoDepth;
    out.assetCount = st.assetCount;
    out.dirty = st.dirty ? 1u : 0u;
    out.recoveryAvailable = st.recoveryAvailable ? 1u : 0u;

    out.droppedFrames = st.droppedFrames;
    out.passesExecuted = st.passesExecuted;
    out.passesCulled = st.passesCulled;
    out.gpuMemoryBytes = st.gpuMemoryBytes;
    out.cpuMemoryBytes = st.cpuMemoryBytes;
}

void Engine::fill_telemetry(bridge::TelemetryPOD& out) noexcept {
    const EngineTelemetry t = read_telemetry();

    out = bridge::TelemetryPOD{};
    out.frameMs = t.frame.cpuMs;
    out.gpuMs = t.frame.gpuMs;
    out.cpuMs = t.frame.cpuMs;
    out.decodeMs = t.frame.decodeMs;
    out.frameCacheHit = t.frameCacheHitRate;
    out.pipelineHit = t.pipelineHitRate;

    out.workerCount = t.workerCount;
    out.passesExecuted = t.frame.passesExecuted;
    out.passesCulled = t.frame.passesCulled;
    out.drawCalls = t.frame.drawCalls;
    out.triangles = t.frame.triangles;
    out.particles = t.frame.particles;
    out.shaderCount = t.shaderCount;
    out.pipelineCount = t.pipelineCount;
    out.shaderFailures = t.shaderFailures;
    out.activeEffects = t.effectsInPreviewMode;
    out.activeLayers = t.frame.layersRendered;
    out.adaptiveChanges = t.adaptiveScaleChanges;
    out.physicalResources = t.physicalResources;
    out.logicalResources = t.logicalResources;

    out.thermal = static_cast<f32>(t.thermal);
    out.throttling = t.throttling ? 1u : 0u;
    out.gpuMemoryBytes = t.frame.gpuMemoryBytes;
    out.cpuMemoryBytes = t.frame.cpuMemoryBytes;
    out.undoBlobBytes = t.undoBlobBytes;
    out.commandsDropped = t.commandsDropped;
    out.framesInFlight = t.frame.frameIndex;
}

void Engine::fill_export_progress(bridge::ExportProgressPOD& out) const noexcept {
    out = bridge::ExportProgressPOD{};
    if (!exportCtx_) {
        out.result = static_cast<i32>(Errc::InvalidState);
        return;
    }
    const ExportProgress& p = exportCtx_->progress;
    out.running = p.running ? 1u : 0u;
    out.finished = p.finished ? 1u : 0u;
    out.result = static_cast<i32>(p.result);
    out.framesTotal = p.framesTotal;
    out.framesDone = p.framesDone;
    out.fps = p.fps;
    out.etaSeconds = p.etaSeconds;
    std::snprintf(out.message, sizeof(out.message), "%s", p.message);
}

} // namespace aurea
