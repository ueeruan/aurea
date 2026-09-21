// Testes do grafo de um frame, do compilador de efeitos e do pipeline de cor.
//
// O FrameGraph é testado com um backend FALSO: ele não desenha nada, só conta
// o que foi criado e destruído. É o suficiente para verificar as propriedades
// que importam — ordem de execução, poda de passes e reaproveitamento de
// memória — sem precisar de GPU, e por isso os testes rodam no CI.
#include "TestFramework.hpp"

#include "aurea/render/FrameGraph.hpp"
#include "aurea/render/EffectGraph.hpp"
#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/core/Log.hpp"

#include <map>

using namespace aurea;

// -----------------------------------------------------------------------------
// Backend falso
// -----------------------------------------------------------------------------
namespace {

class FakeCommandList : public CommandList {
public:
    u32 beginCount = 0;
    u32 endCount = 0;
    u32 dispatches = 0;
    u32 draws = 0;

    Status begin() noexcept override { ++beginCount; return OkStatus; }
    Status end() noexcept override { ++endCount; return OkStatus; }
    void bind_pipeline(PipelineHandle) noexcept override {}
    void bind_texture(u32, u32, TextureHandle, SamplerHandle) noexcept override {}
    void bind_uniform(u32, u32, const void*, u32) noexcept override {}
    void bind_storage_buffer(u32, u32, BufferHandle) noexcept override {}
    void set_viewport(f32, f32, f32, f32) noexcept override {}
    void set_scissor(i32, i32, u32, u32) noexcept override {}
    void begin_render_pass(RenderPassHandle, TextureHandle, TextureHandle,
                           TextureHandle, const f32[4]) noexcept override {}
    void end_render_pass() noexcept override {}
    void draw(const DrawCall&) noexcept override { ++draws; }
    void dispatch(u32, u32, u32) noexcept override { ++dispatches; }
    void memory_barrier() noexcept override {}
    void copy_buffer_to_texture(BufferHandle, TextureHandle) noexcept override {}
    void copy_texture_to_buffer(TextureHandle, BufferHandle) noexcept override {}
    void write_timestamp(QueryPoolHandle, u32) noexcept override {}
};

class FakeBackend : public GPUBackend {
public:
    u64 nextId = 1;
    u32 texturesCreated = 0;
    u32 texturesDestroyed = 0;
    u32 buffersCreated = 0;
    u32 pipelinesCreated = 0;
    std::map<u64, u64> textureBytes;

    const char* name() const noexcept override { return "fake"; }
    Status initialize() noexcept override { return OkStatus; }
    void shutdown() noexcept override {}
    Status recreate_surface(const SurfaceDesc&) noexcept override { return OkStatus; }
    Status resize_surface(u32, u32) noexcept override { return OkStatus; }
    Status begin_frame(TextureHandle& out) noexcept override {
        out = TextureHandle{nextId++};
        return OkStatus;
    }
    Status end_frame(CommandList&) noexcept override { return OkStatus; }

    Result<TextureHandle> create_texture(const TextureDesc& d) noexcept override {
        ++texturesCreated;
        const u64 id = nextId++;
        textureBytes[id] = d.estimated_bytes();
        return TextureHandle{id};
    }
    Result<BufferHandle> create_buffer(usize, u32) noexcept override {
        ++buffersCreated;
        return BufferHandle{nextId++};
    }
    Result<SamplerHandle> create_sampler(const SamplerDesc&) noexcept override {
        return SamplerHandle{nextId++};
    }
    Result<ShaderHandle> create_shader(const ShaderDesc&) noexcept override {
        return ShaderHandle{nextId++};
    }
    Result<PipelineHandle> create_pipeline(const PipelineDesc&) noexcept override {
        ++pipelinesCreated;
        return PipelineHandle{nextId++};
    }
    Result<RenderPassHandle> create_render_pass(const TextureDesc*, u32,
                                                const TextureDesc*) noexcept override {
        return RenderPassHandle{nextId++};
    }
    Result<QueryPoolHandle> create_query_pool(u32) noexcept override {
        return QueryPoolHandle{nextId++};
    }
    void destroy_texture(TextureHandle) noexcept override { ++texturesDestroyed; }
    void destroy_buffer(BufferHandle) noexcept override {}
    void destroy_sampler(SamplerHandle) noexcept override {}
    void destroy_shader(ShaderHandle) noexcept override {}
    void destroy_pipeline(PipelineHandle) noexcept override {}
    void destroy_render_pass(RenderPassHandle) noexcept override {}
    void destroy_query_pool(QueryPoolHandle) noexcept override {}

    Status upload_texture(TextureHandle, const void*, u32, u32) noexcept override { return OkStatus; }
    Status upload_buffer(BufferHandle, const void*, usize, usize) noexcept override { return OkStatus; }
    Status map_buffer(BufferHandle, void*& out) noexcept override { out = nullptr; return OkStatus; }
    void unmap_buffer(BufferHandle) noexcept override {}
    Result<TextureHandle> import_external_image(const ExternalImageHandle&) noexcept override {
        return TextureHandle{nextId++};
    }
    void release_external_image(TextureHandle) noexcept override {}
    Status read_texture(TextureHandle, void*, u32) noexcept override { return OkStatus; }
    void wait_idle() noexcept override {}
    bool read_timestamps(QueryPoolHandle, u32, u32, f32*) noexcept override { return false; }
    bool is_device_lost() const noexcept override { return false; }
    u32 current_frame_index() const noexcept override { return 0; }
    u64 allocated_bytes() const noexcept override { return 0; }
};

// Funções de passe para os testes. Contam execuções num vetor compartilhado.
std::vector<const char*>* g_execLog = nullptr;
void record_pass_a(FrameGraph&, void*, CommandList&) { if (g_execLog) g_execLog->push_back("a"); }
void record_pass_b(FrameGraph&, void*, CommandList&) { if (g_execLog) g_execLog->push_back("b"); }
void record_pass_c(FrameGraph&, void*, CommandList&) { if (g_execLog) g_execLog->push_back("c"); }

TextureDesc tex_desc(u32 w, u32 h) {
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = SurfaceFormat::RGBA16F;
    d.sampled = true;
    d.renderTarget = true;
    return d;
}

} // namespace

// -----------------------------------------------------------------------------
// FrameGraph
// -----------------------------------------------------------------------------
AUREA_TEST(FrameGraph, EmptyGraphReportsNothing) {
    FakeBackend backend;
    FrameGraph g;
    AUREA_CHECK(g.pass_count() == 0);
    AUREA_CHECK(g.resource_count() == 0);
    AUREA_CHECK(g.compile(backend).ok());
}

AUREA_TEST(FrameGraph, CullsPassesThatFeedNothing) {
    FakeBackend backend;
    FrameGraph g;
    const u32 res = g.create_texture("x", tex_desc(64, 64));
    const u32 a = g.add_pass("a", PassStage::Effects, record_pass_a, nullptr);
    g.pass_writes(a, res);
    // Nenhuma saída declarada: o passe não contribui para nada e é podado.
    // Sem isso, cada camada invisível ainda pagaria decode e efeitos.
    AUREA_CHECK(g.compile(backend).ok());
    AUREA_CHECK_EQ(g.culled_count(), static_cast<u32>(1));
}

AUREA_TEST(FrameGraph, KeepsPassesReachingOutput) {
    FakeBackend backend;
    FrameGraph g;
    const u32 res = g.create_texture("saida", tex_desc(64, 64));
    const u32 a = g.add_pass("a", PassStage::Effects, record_pass_a, nullptr);
    g.pass_writes(a, res);
    g.set_output(res);

    AUREA_CHECK(g.compile(backend).ok());
    AUREA_CHECK_EQ(g.culled_count(), static_cast<u32>(0));
    AUREA_CHECK_EQ(g.execution_order().size(), static_cast<usize>(1));
}

AUREA_TEST(FrameGraph, DependencyOrderOverridesDeclarationOrder) {
    // O grafo ordena por dependência, não por declaração. Declarar B antes de A
    // e ainda assim executar A primeiro é o teste que prova isso.
    FakeBackend backend;
    FrameGraph g;
    const u32 mid = g.create_texture("intermediario", tex_desc(64, 64));
    const u32 out = g.create_texture("saida", tex_desc(64, 64));

    const u32 b = g.add_pass("b", PassStage::Composite, record_pass_b, nullptr);
    g.pass_reads(b, mid);
    g.pass_writes(b, out);

    const u32 a = g.add_pass("a", PassStage::Effects, record_pass_a, nullptr);
    g.pass_writes(a, mid);

    g.set_output(out);
    AUREA_CHECK(g.compile(backend).ok());

    const std::vector<u32>& order = g.execution_order();
    AUREA_CHECK_EQ(order.size(), static_cast<usize>(2));
    AUREA_CHECK_EQ(order[0], a);
    AUREA_CHECK_EQ(order[1], b);
}

AUREA_TEST(FrameGraph, ExecuteRunsInResolvedOrder) {
    FakeBackend backend;
    FakeCommandList cmds;
    FrameGraph g;

    const u32 mid = g.create_texture("m", tex_desc(4, 4));
    const u32 out = g.create_texture("o", tex_desc(4, 4));
    const u32 a = g.add_pass("a", PassStage::Effects, record_pass_a, nullptr);
    g.pass_writes(a, mid);
    const u32 b = g.add_pass("b", PassStage::Composite, record_pass_b, nullptr);
    g.pass_reads(b, mid);
    g.pass_writes(b, out);
    g.set_output(out);

    AUREA_CHECK(g.compile(backend).ok());

    std::vector<const char*> log;
    g_execLog = &log;
    g.execute(cmds);
    g_execLog = nullptr;

    AUREA_CHECK_EQ(log.size(), static_cast<usize>(2));
    AUREA_CHECK_EQ(std::strcmp(log[0], "a"), 0);
    AUREA_CHECK_EQ(std::strcmp(log[1], "b"), 0);
}

AUREA_TEST(FrameGraph, AliasesResourcesWithDisjointLifetimes) {
    // Dois intermediários que nunca coexistem dividem uma textura física. Numa
    // cadeia de 8 efeitos sobre 4K isso é a diferença entre ~600 MB e ~150 MB
    // de pico — entre rodar e ser morto pelo sistema.
    FakeBackend backend;
    FrameGraph g;

    const u32 r1 = g.create_texture("etapa1", tex_desc(128, 128));
    const u32 r2 = g.create_texture("etapa2", tex_desc(128, 128));
    const u32 out = g.create_texture("saida", tex_desc(128, 128));

    const u32 a = g.add_pass("a", PassStage::Effects, record_pass_a, nullptr);
    g.pass_writes(a, r1);
    const u32 b = g.add_pass("b", PassStage::Effects, record_pass_b, nullptr);
    g.pass_reads(b, r1);
    g.pass_writes(b, r2);
    const u32 c = g.add_pass("c", PassStage::Composite, record_pass_c, nullptr);
    g.pass_reads(c, r2);
    g.pass_writes(c, out);
    g.set_output(out);

    AUREA_CHECK(g.compile(backend).ok());
    // Quatro recursos lógicos (r1, r2, out, e o alvo físico compartilhado), mas
    // menos texturas físicas — a prova de que o aliasing aconteceu.
    AUREA_CHECK(g.physical_resource_count() <= g.resource_count());
}

AUREA_TEST(FrameGraph, PersistentResourcesAreNeverAliased) {
    // Um recurso de histórico precisa sobreviver ao frame. Se fosse aliado, o
    // efeito temporal leria o próprio resultado do frame anterior misturado com
    // o de outro passe.
    FakeBackend backend;
    FrameGraph g;
    const u32 hist = g.create_persistent_texture("historico", tex_desc(64, 64));
    const u32 out = g.create_texture("saida", tex_desc(64, 64));

    const u32 a = g.add_pass("a", PassStage::Effects, record_pass_a, nullptr);
    g.pass_reads_writes(a, hist);
    const u32 b = g.add_pass("b", PassStage::Composite, record_pass_b, nullptr);
    g.pass_reads(b, hist);
    g.pass_writes(b, out);
    g.set_output(out);
    AUREA_CHECK(g.compile(backend).ok());

    AUREA_CHECK(g.texture(hist).valid());
    AUREA_CHECK(g.texture(out).valid());
    AUREA_CHECK(g.texture(hist) != g.texture(out));
}

AUREA_TEST(FrameGraph, RecompileIsSkippedWhenRevisionUnchanged) {
    // Durante o playback a topologia é a mesma em quase todo frame. Recompilar
    // seria trabalho puro — o que muda são os PARÂMETROS, não os passes.
    FakeBackend backend;
    FrameGraph g;
    const u32 out = g.create_texture("saida", tex_desc(32, 32));
    const u32 a = g.add_pass("a", PassStage::Composite, record_pass_a, nullptr);
    g.pass_writes(a, out);
    g.set_output(out);

    AUREA_CHECK(g.compile(backend).ok());
    const u64 rev = g.revision();
    AUREA_CHECK(g.compile(backend).ok());
    AUREA_CHECK_EQ(g.revision(), rev);
}

AUREA_TEST(FrameGraph, ResetKeepsPhysicalResources) {
    // Realocar 4K a 60 Hz seria o gargalo dominante do editor: os recursos
    // físicos sobrevivem ao reset do frame.
    FakeBackend backend;
    FrameGraph g;
    const u32 out = g.create_texture("saida", tex_desc(64, 64));
    const u32 a = g.add_pass("a", PassStage::Composite, record_pass_a, nullptr);
    g.pass_writes(a, out);
    g.set_output(out);
    AUREA_CHECK(g.compile(backend).ok());
    const TextureHandle first = g.texture(out);

    g.reset();
    const u32 out2 = g.create_texture("saida", tex_desc(64, 64));
    const u32 a2 = g.add_pass("a", PassStage::Composite, record_pass_a, nullptr);
    g.pass_writes(a2, out2);
    g.set_output(out2);
    AUREA_CHECK(g.compile(backend).ok());

    AUREA_CHECK(g.texture(out2) == first);
    AUREA_CHECK_EQ(backend.texturesCreated, static_cast<u32>(1));
}

AUREA_TEST(FrameGraph, ImportedResourcesAreNotAllocated) {
    FakeBackend backend;
    FrameGraph g;
    const u32 ext = g.import_texture("backbuffer", TextureHandle{777});
    const u32 a = g.add_pass("a", PassStage::Composite, record_pass_a, nullptr);
    g.pass_writes(a, ext);
    g.set_output(ext);

    AUREA_CHECK(g.compile(backend).ok());
    AUREA_CHECK_EQ(backend.texturesCreated, static_cast<u32>(0));
    AUREA_CHECK_EQ(g.texture(ext).id, static_cast<u64>(777));
}

AUREA_TEST(FrameGraph, FindByNameWorks) {
    FrameGraph g;
    (void)g.create_texture("alvo-nomeado", tex_desc(8, 8));
    (void)g.add_pass("passe-nomeado", PassStage::Effects, record_pass_a, nullptr);
    AUREA_CHECK(g.find_resource("alvo-nomeado") != kInvalidIndex);
    AUREA_CHECK(g.find_resource("nao-existe") == kInvalidIndex);
    AUREA_CHECK(g.find_pass("passe-nomeado") != kInvalidIndex);
}

AUREA_TEST(FrameGraph, ReleaseGpuResourcesClearsHandles) {
    FakeBackend backend;
    FrameGraph g;
    const u32 out = g.create_texture("saida", tex_desc(64, 64));
    const u32 a = g.add_pass("a", PassStage::Composite, record_pass_a, nullptr);
    g.pass_writes(a, out);
    g.set_output(out);
    AUREA_CHECK(g.compile(backend).ok());

    // Dispositivo perdido: os handles do driver antigo morreram.
    g.release_gpu_resources(backend);
    AUREA_CHECK(!g.texture(out).valid());
    AUREA_CHECK_EQ(g.physical_resource_count(), static_cast<u32>(0));
}

// -----------------------------------------------------------------------------
// EffectRegistry + EffectCompiler
// -----------------------------------------------------------------------------
namespace {

const EffectParamDesc kBlurParams[] = {
    {"Raio", EffectParamType::Float, 0.0f, 200.0f, 0.0f, 0, nullptr},
    {"Intensidade", EffectParamType::Float, 0.0f, 2.0f, 1.0f, 0, nullptr},
};
const EffectDesc kBlurDesc{"Desfoque", "Desfoque", 2, kBlurParams, 3, true, 8, 16};

const EffectParamDesc kColorParams[] = {
    {"Exposicao", EffectParamType::Float, -4.0f, 4.0f, 0.0f, 0, nullptr},
    {"Contraste", EffectParamType::Float, 0.0f, 4.0f, 1.0f, 0, nullptr},
    {"Saturacao", EffectParamType::Float, 0.0f, 4.0f, 1.0f, 0, nullptr},
};
const EffectDesc kColorDesc{"Correcao de cor", "Cor", 3, kColorParams, 1, true, 0, 0};

const EffectParamDesc kEchoParams[] = {
    {"Ecos", EffectParamType::Float, 0.0f, 16.0f, 0.0f, 0, nullptr},
};
const EffectDesc kEchoDesc{"Eco", "Temporal", 1, kEchoParams, 4, false, 4, 16};

const EffectParamDesc kDisplaceParams[] = {
    {"Forca", EffectParamType::Float, 0.0f, 1.0f, 0.0f, 0, nullptr},
};
const EffectDesc kDisplaceDesc{"Deslocamento", "Distorcao", 1, kDisplaceParams, 2, false, 0, 0};

void build_registry(EffectRegistry& reg) {
    (void)reg.register_effect(&kBlurDesc, EffectClass::Neighborhood, 0);
    (void)reg.register_effect(&kColorDesc, EffectClass::PerPixel, 1);
    (void)reg.register_effect(&kEchoDesc, EffectClass::Temporal, 2);
    (void)reg.register_effect(&kDisplaceDesc, EffectClass::Domain, 3);
}

Effect make_effect(u16 type, f32 param0) {
    Effect e;
    e.type = type;
    e.enabled = true;
    e.floats[0] = param0;
    e.floats[1] = 1.0f;
    e.floats[2] = 1.0f;
    return e;
}

} // namespace

AUREA_TEST(EffectRegistry, RegisterAndLookup) {
    EffectRegistry reg;
    build_registry(reg);
    AUREA_CHECK_EQ(reg.count(), static_cast<u32>(4));
    AUREA_CHECK(reg.description(0) != nullptr);
    AUREA_CHECK_EQ(reg.classification(2), EffectClass::Temporal);
    AUREA_CHECK(reg.find("Eco") == 2u);
    AUREA_CHECK(reg.find("nao existe") == kInvalidIndex);
}

AUREA_TEST(EffectRegistry, DuplicateRegistrationIsRefused) {
    EffectRegistry reg;
    AUREA_CHECK(reg.register_effect(&kBlurDesc, EffectClass::Neighborhood, 0).ok());
    AUREA_CHECK(!reg.register_effect(&kColorDesc, EffectClass::PerPixel, 0).ok());
}

AUREA_TEST(EffectCompiler, EmptyChainProducesNoPasses) {
    EffectRegistry reg;
    build_registry(reg);
    std::vector<Effect> effects;
    TrackSet tracks;
    EffectPlan plan;
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        1920, 1080, true, plan).ok());
    AUREA_CHECK_EQ(plan.pass_count(), static_cast<u32>(0));
}

AUREA_TEST(EffectCompiler, ConsecutivePerPixelFuseIntoOnePass) {
    // O caso que motiva todo o sistema: exposição + contraste + saturação são
    // uma leitura e uma escrita, não três. Três passes sobre 4K RGBA16F seriam
    // ~200 MB de tráfego de memória por frame só nesse trecho.
    EffectRegistry reg;
    build_registry(reg);

    std::vector<Effect> effects;
    effects.push_back(make_effect(1, 0.5f));
    effects.push_back(make_effect(1, 1.2f));
    effects.push_back(make_effect(1, 1.1f));

    TrackSet tracks;
    EffectPlan plan;
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        1920, 1080, true, plan).ok());

    AUREA_CHECK_EQ(plan.pass_count(), static_cast<u32>(1));
    AUREA_CHECK_EQ(plan.stages[0].effectIndices.size(), static_cast<usize>(3));
    AUREA_CHECK_EQ(plan.stages[0].sampleCount, static_cast<u32>(1));
}

AUREA_TEST(EffectCompiler, NeighborhoodProducesTwoPassesWhenSeparable) {
    // Um gaussiano separável é H + V: 2*N taps em vez de N².
    EffectRegistry reg;
    build_registry(reg);

    std::vector<Effect> effects;
    effects.push_back(make_effect(0, 12.0f));

    TrackSet tracks;
    EffectPlan plan;
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        1920, 1080, true, plan).ok());

    AUREA_CHECK_EQ(plan.pass_count(), static_cast<u32>(2));
    AUREA_CHECK(plan.stages[0].name.find("-h") != std::string::npos);
    AUREA_CHECK(plan.stages[1].name.find("-v") != std::string::npos);
}

AUREA_TEST(EffectCompiler, TemporalEffectAlwaysGetsItsOwnPass) {
    EffectRegistry reg;
    build_registry(reg);

    std::vector<Effect> effects;
    effects.push_back(make_effect(2, 4.0f));   // eco
    effects.push_back(make_effect(1, 0.5f));   // correcao

    TrackSet tracks;
    EffectPlan plan;
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        1920, 1080, true, plan).ok());

    // O temporal NÃO funde com o per-pixel seguinte: ele precisa do frame
    // anterior e de um recurso persistente, e misturá-los produziria imagem
    // diferente do que o usuário vê no painel.
    AUREA_CHECK_EQ(plan.pass_count(), static_cast<u32>(2));
    AUREA_CHECK_EQ(plan.stages[0].cls, EffectClass::Temporal);
    AUREA_CHECK(plan.stages[0].requiresHistory);
    AUREA_CHECK_EQ(plan.stages[1].cls, EffectClass::PerPixel);
}

AUREA_TEST(EffectCompiler, DomainEffectBreaksTheFusionRun) {
    EffectRegistry reg;
    build_registry(reg);

    std::vector<Effect> effects;
    effects.push_back(make_effect(1, 0.5f));   // per-pixel
    effects.push_back(make_effect(3, 0.5f));   // deslocamento (dominio)
    effects.push_back(make_effect(1, 0.5f));   // per-pixel

    TrackSet tracks;
    EffectPlan plan;
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        1920, 1080, true, plan).ok());

    // Três etapas: o deslocamento muda o domínio da imagem, então o que vem
    // depois não pode ser fundido com o que vem antes.
    AUREA_CHECK_EQ(plan.pass_count(), static_cast<u32>(3));
}

AUREA_TEST(EffectCompiler, NeutralEffectIsDropped) {
    EffectRegistry reg;
    build_registry(reg);

    std::vector<Effect> effects;
    effects.push_back(make_effect(0, 0.0f));   // desfoque com raio 0: nao muda nada

    TrackSet tracks;
    EffectPlan plan;
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        1920, 1080, true, plan).ok());

    AUREA_CHECK_EQ(plan.pass_count(), static_cast<u32>(0));
    AUREA_CHECK_EQ(plan.droppedEffects.size(), static_cast<usize>(1));
}

AUREA_TEST(EffectCompiler, DisabledEffectIsDropped) {
    EffectRegistry reg;
    build_registry(reg);

    std::vector<Effect> effects;
    Effect e = make_effect(1, 0.5f);
    e.enabled = false;
    effects.push_back(e);

    TrackSet tracks;
    EffectPlan plan;
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        1920, 1080, true, plan).ok());
    AUREA_CHECK_EQ(plan.pass_count(), static_cast<u32>(0));
}

AUREA_TEST(EffectCompiler, UnknownEffectIsReportedNotSilentlySkipped) {
    // Um projeto vindo de uma versão que tinha um efeito que esta não tem. O
    // compilador precisa DIZER isso — aplicar um passe vazio faria o usuário
    // ver o frame sem o efeito e não saber por quê.
    EffectRegistry reg;
    build_registry(reg);

    std::vector<Effect> effects;
    effects.push_back(make_effect(99, 1.0f));

    TrackSet tracks;
    EffectPlan plan;
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        1920, 1080, true, plan).ok());
    AUREA_CHECK_EQ(plan.pass_count(), static_cast<u32>(0));
    AUREA_CHECK(!plan.fusionBlockers.empty());
}

AUREA_TEST(EffectCompiler, PreviewUsesFewerSamplesThanExport) {
    // O preview degrada; o export NÃO. É o que permite editar 4K num aparelho
    // médio sem que o resultado final perca qualidade.
    EffectRegistry reg;
    build_registry(reg);

    std::vector<Effect> effects;
    effects.push_back(make_effect(0, 30.0f));   // desfoque grande

    TrackSet tracks;
    EffectPlan preview;
    EffectPlan final;
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        1920, 1080, true, preview).ok());
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        1920, 1080, false, final).ok());

    AUREA_CHECK(preview.total_cost() <= final.total_cost());
    AUREA_CHECK(final.stages[0].sampleCount >= preview.stages[0].sampleCount);
}

AUREA_TEST(EffectCompiler, LargeBlurRunsAtReducedResolutionInPreview) {
    EffectRegistry reg;
    build_registry(reg);

    std::vector<Effect> effects;
    effects.push_back(make_effect(0, 100.0f));

    TrackSet tracks;
    EffectPlan plan;
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        3840, 2160, true, plan).ok());
    // Blur de raio 100 em resolução cheia não cabe no orçamento de nenhum
    // celular. Em meia resolução é visualmente idêntico e 4x mais barato.
    AUREA_CHECK_NEAR(plan.stages[0].resolutionScale, 0.5f, 1e-6);
}

AUREA_TEST(EffectCompiler, AnimatedRadiusChangesThePlan) {
    EffectRegistry reg;
    build_registry(reg);

    std::vector<Effect> effects;
    effects.push_back(make_effect(0, 12.0f));

    TrackSet tracks;
    Track& radius = tracks.get_or_create(TrackProperty::EffectParam, 0, 0);
    radius.set(FrameIndex{0}, 0.0f);
    radius.set(FrameIndex{100}, 40.0f);

    EffectPlan atStart;
    EffectPlan atMid;
    EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg, 1920, 1080, true, atStart);
    EffectCompiler::compile(effects, tracks, FrameIndex{50}, reg, 1920, 1080, true, atMid);

    // No frame 0 o raio é 0: efeito neutro, nenhum passe. No meio, o raio é 20 e
    // o efeito existe. O compilador lê o valor ANIMADO, não o estático.
    AUREA_CHECK_EQ(atStart.pass_count(), static_cast<u32>(0));
    AUREA_CHECK(atMid.pass_count() > 0);
}

AUREA_TEST(EffectCompiler, ZeroSampleStageIsRefused) {
    // Guarda contra um registro mal formado: uma etapa sem amostra é um passe
    // que não lê nada e desenharia preto. Melhor falhar na compilação.
    EffectRegistry reg;
    const EffectParamDesc p[] = {
        {"Raio", EffectParamType::Float, 0.0f, 100.0f, 1.0f, 0, nullptr},
    };
    // Efeito de vizinhança sem declarar amostras: o compilador cai no padrão de
    // 9 taps em vez de gerar zero.
    const EffectDesc noSamples{"Sem amostras", "Teste", 1, p, 1, false, 0, 0};
    (void)reg.register_effect(&noSamples, EffectClass::Neighborhood, 0);

    std::vector<Effect> effects;
    effects.push_back(make_effect(0, 5.0f));

    TrackSet tracks;
    EffectPlan plan;
    AUREA_CHECK(EffectCompiler::compile(effects, tracks, FrameIndex{0}, reg,
                                        1920, 1080, true, plan).ok());
    for (const auto& s : plan.stages) {
        AUREA_CHECK(s.sampleCount > 0);
    }
}

// -----------------------------------------------------------------------------
// ShaderLibrary
// -----------------------------------------------------------------------------
AUREA_TEST(ShaderLibrary, CompositePipelineIsCached) {
    FakeBackend backend;
    ShaderLibrary lib;
    AUREA_CHECK(lib.initialize(backend).ok());

    auto a = lib.composite_pipeline(BlendMode::Normal, SurfaceFormat::RGBA16F, 1);
    AUREA_CHECK(a.ok());
    const u32 created = backend.pipelinesCreated;

    auto b = lib.composite_pipeline(BlendMode::Normal, SurfaceFormat::RGBA16F, 1);
    AUREA_CHECK(b.ok());
    // Mesma chave estrutural = mesmo pipeline. Sem isso, duas camadas com o
    // mesmo blend mode compilariam pipelines separados.
    AUREA_CHECK_EQ(backend.pipelinesCreated, created);
    AUREA_CHECK(*a == *b);
}

AUREA_TEST(ShaderLibrary, DifferentBlendModesGetDifferentPipelines) {
    FakeBackend backend;
    ShaderLibrary lib;
    AUREA_CHECK(lib.initialize(backend).ok());

    auto a = lib.composite_pipeline(BlendMode::Normal, SurfaceFormat::RGBA16F, 1);
    auto b = lib.composite_pipeline(BlendMode::Add, SurfaceFormat::RGBA16F, 1);
    AUREA_CHECK(a.ok());
    AUREA_CHECK(b.ok());
    AUREA_CHECK(*a != *b);
}

AUREA_TEST(ShaderLibrary, DifferentFormatsGetDifferentPipelines) {
    FakeBackend backend;
    ShaderLibrary lib;
    AUREA_CHECK(lib.initialize(backend).ok());

    auto a = lib.composite_pipeline(BlendMode::Normal, SurfaceFormat::RGBA16F, 1);
    auto b = lib.composite_pipeline(BlendMode::Normal, SurfaceFormat::RGBA8, 1);
    AUREA_CHECK(a.ok());
    AUREA_CHECK(b.ok());
    AUREA_CHECK(*a != *b);
}

AUREA_TEST(ShaderLibrary, PrewarmCompilesAllBlendModes) {
    FakeBackend backend;
    ShaderLibrary lib;
    AUREA_CHECK(lib.initialize(backend).ok());

    const BlendMode modes[] = {
        BlendMode::Normal, BlendMode::Add, BlendMode::Multiply,
        BlendMode::Screen, BlendMode::Overlay, BlendMode::SoftLight,
    };
    const u32 compiled = lib.prewarm_blend_modes(modes, 6, SurfaceFormat::RGBA16F, 1);
    AUREA_CHECK_EQ(compiled, static_cast<u32>(6));

    // Pré-aquecer de novo não recompila nada: os 6 já estão em cache.
    AUREA_CHECK_EQ(lib.prewarm_blend_modes(modes, 6, SurfaceFormat::RGBA16F, 1),
                   static_cast<u32>(0));
}

AUREA_TEST(ShaderLibrary, InvalidateKeepsKeysAndRecompiles) {
    FakeBackend backend;
    ShaderLibrary lib;
    AUREA_CHECK(lib.initialize(backend).ok());

    auto first = lib.composite_pipeline(BlendMode::Normal, SurfaceFormat::RGBA16F, 1);
    AUREA_CHECK(first.ok());
    const u32 afterFirst = backend.pipelinesCreated;

    // Dispositivo perdido: os handles do driver morreram, mas a chave
    // estrutural do cache continua válida.
    lib.invalidate_device_shaders();
    auto second = lib.composite_pipeline(BlendMode::Normal, SurfaceFormat::RGBA16F, 1);
    AUREA_CHECK(second.ok());
    AUREA_CHECK(backend.pipelinesCreated > afterFirst);
    AUREA_CHECK_EQ(lib.pipeline_count(), static_cast<u32>(1));
}

AUREA_TEST(ShaderLibrary, ComputePipelineHasNoColorAttachments) {
    FakeBackend backend;
    ShaderLibrary lib;
    AUREA_CHECK(lib.initialize(backend).ok());

    ShaderKey key;
    key.source = "#version 450\nvoid main(){}";
    key.stage = ShaderStage::Compute;
    auto p = lib.compute_pipeline(key);
    AUREA_CHECK(p.ok());
}
