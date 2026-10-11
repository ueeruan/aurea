// =============================================================================
//  Aurea / scene3d / Ground.cpp
//
//  O CHÃO do grupo 3D (FloorSettings / SceneFloor). INCLUÍDO por
//  SceneRenderer.cpp (não é uma unidade de compilação própria — não entra no
//  CMakeLists): usa os tipos privados de lá (SceneBlock, MeshPush,
//  SkinVertex) e a lista de desenho do quadro (tipo local do build, por isso
//  as funções que a percorrem são templates).
//
//  Três peças, todas montadas num bloco só do SceneRenderer::build, antes do
//  passe principal:
//
//  1. REFLEXO PLANAR — os desenhos OPACOS do quadro de novo, com a vista
//     espelhada no plano do chão (viewProj · R, R = reflexão y' = 2h − y) e
//     a câmera espelhada (o V do PBR sai do ponto certo: o raio que bate no
//     chão e volta até a câmera é a reta da câmera espelhada até o ponto).
//     A reflexão inverte o sentido das faces: os pipelines são a variante com
//     frontFace trocado. Uma amostra (mesmo com MSAA no passe principal),
//     metade da resolução no preview, inteira no export. Pulado com
//     refletividade 0 ou no nível BAIXO. Sem plano de corte: o chão fica no
//     ponto MAIS BAIXO de todos os modelos, então nada fica abaixo dele.
//     Depois, desfoque gaussiano separável pela rugosidade do chão.
//
//  2. SOMBRA DE CONTATO — a mesma lista, só profundidade, vista DE BAIXO
//     (ortográfica olhando para cima, faixa de altura H = 0,35·raio acima do
//     chão) num mapa pequeno (256², 512² no export). O desfoque converte
//     profundidade em oclusão (1 − d)² e espalha: a "pegada" macia que assenta
//     o carro mesmo sem luz que projete sombra.
//
//  3. O PLANO — ground.vert/.frag desenhado no passe principal depois dos
//     opacos (antes dos planos 2D e dos transparentes), com a MESMA sombra da
//     luz principal (common/shadow.glsl), IBL do grupo, o reflexo e o
//     contato. Desbota radialmente até o horizonte.
// =============================================================================

namespace {

// Espelho exato de shaders/scene3d/common/ground.glsl (std140).
struct alignas(16) GroundBlock {
    Mat4 viewProj;
    Vec4 cameraPos;
    Vec4 center;
    Vec4 color;
    Vec4 params;
    Vec4 envParams;
    Vec4 viewport;
    Vec4 skyColor;
    Vec4 groundColor;
    Vec4 lightCount;
    Vec4 lightPos[4];
    Vec4 lightColor[4];
    Vec4 lightSpot[4];
    Vec4 lightSpot2[4];
    Mat4 shadowMatrix;
    Vec4 shadowParams;
    Vec4 shadowParams2;
    Mat4 contactMatrix;
    Vec4 contactParams;
};
static_assert(sizeof(GroundBlock) <= binding::kMaxUniformBytes, "bloco do chao maior que o limite de uniform");

PipelineKey ground_key(u32 mode, u32 samples, bool mrt) noexcept {
    PipelineKey k = PipelineKey::graphics(ShaderId::scene3d_ground_vert, ShaderId::scene3d_ground_frag, SurfaceFormat::RGBA16F,
                                          true, BlendMode::Normal);   // pré-multiplicado: o desbotamento mistura
    k.hasDepth = true;
    k.depthTest = true;
    k.depthWrite = mode == 1;          // o shadow catcher não tapa nada
    k.depthCompare = CompareOp::GreaterOrEqual;
    k.depthFormat = SurfaceFormat::Depth32F;
    k.cull = CullMode::None;
    k.sampleCount = static_cast<u8>(samples);
    k.hasColor1 = mrt;
    return k;
}

PipelineKey ground_blur_key() noexcept {
    return PipelineKey::fullscreen(ShaderId::scene3d_ground_blur_frag, SurfaceFormat::RGBA16F);
}

/// Variante de um pipeline de modelo para o passe do reflexo: uma amostra,
/// MRT, sem alpha-to-coverage e com o sentido das faces trocado (a reflexão
/// espelha a malha).
PipelineKey ground_reflection_key(PipelineKey k) noexcept {
    k.sampleCount = 1;
    k.hasColor1 = true;
    k.alphaToCoverage = false;
    k.frontFaceCCW = !k.frontFaceCCW;
    return k;
}

/// Profundidade da malha vista de baixo (sombra de contato): o vértice do PBR
/// (instâncias e skin iguais aos da cor) e o fragmento vazio da sombra.
PipelineKey ground_contact_key(bool skinned) noexcept {
    PipelineKey k = PipelineKey::graphics(skinned ? ShaderId::scene3d_pbr_mesh_skinned_vert : ShaderId::scene3d_pbr_mesh_vert,
                                          ShaderId::scene3d_shadow_shadow_frag, SurfaceFormat::RGBA16F, false,
                                          BlendMode::Normal);
    k.mesh = skinned ? MeshLayout::Skinned : MeshLayout::Static;
    k.hasDepth = true;
    k.depthOnly = true;
    k.depthTest = true;
    k.depthWrite = true;
    k.depthCompare = CompareOp::LessOrEqual;   // Z comum: 0 = no chão, limpa em 1
    k.depthFormat = SurfaceFormat::Depth32F;
    k.cull = CullMode::None;
    return k;
}

/// Pipelines do chão para aquecer junto (collect_pipelines). `first` = início
/// dos pipelines de modelo em `out` (ganham a variante do reflexo).
void collect_ground_pipelines(std::vector<PipelineKey>& out, usize first, usize last, u32 samples) {
    for (usize i = first; i < last && i < out.size(); ++i) {
        if (out[i].fragment == ShaderId::scene3d_pbr_pbr_frag && !out[i].blendEnabled) out.push_back(ground_reflection_key(out[i]));
    }
    out.push_back(ground_key(1, samples, true));
    out.push_back(ground_key(2, samples, true));
    out.push_back(ground_blur_key());
    out.push_back(ground_contact_key(false));
    out.push_back(ground_contact_key(true));
}

/// O que o passe principal precisa para desenhar o chão.
struct GroundDraw {
    PipelineHandle pipeline{};
    const GroundBlock* block = nullptr;
    FGTexture reflection{}, reflectionDisplay{}, contact{}, shadow{};
    TextureHandle fallback{};
    TextureHandle irradiance{}, prefiltered{}, brdf{};
    SamplerHandle cubeSampler{};
    u64 linearClamp = 0, nearest = 0, compare = 0;
    [[nodiscard]] bool valid() const noexcept { return block && pipeline.valid(); }
};

/// Recursos do quadro para percorrer a lista de desenho fora do passe principal.
struct GroundMeshContext {
    TextureHandle white{}, flatNormal{};
    TextureHandle irradiance{}, prefiltered{}, brdf{};   ///< ambiente do grupo (desenho sem ambiente próprio)
    SamplerHandle cubeSampler{};
    u64 linearSampler = 0, clampSampler = 0, nearestSampler = 0, compareSampler = 0;
    FGTexture shadow{};
    BufferHandle joints{}, morph{}, inst{};
};

/// Grava a lista de desenho (a mesma do passe principal). `sharedBlock` não
/// nulo = só profundidade (contato): um bloco para todos, sem texturas.
template <class DrawT>
void ground_record_draws(CommandList& c, PassContext& pc, const DrawT* draws, u32 count, const GroundMeshContext& x,
                         const void* sharedBlock) {
    PipelineHandle bound{};
    const GpuModel* boundModel = nullptr;
    const GpuMaterial* boundMat = nullptr;
    const void* boundEnv = nullptr;
    for (u32 i = 0; i < count; ++i) {
        const DrawT& d = draws[i];
        if (!d.pipeline.valid()) continue;
        if (!(d.pipeline == bound)) {
            c.bind_pipeline(d.pipeline);
            bound = d.pipeline;
            boundMat = nullptr;
        }
        if (!sharedBlock && (d.material != boundMat || d.env != boundEnv)) {
            const TextureHandle fallback[5] = {x.white, x.white, x.flatNormal, x.white, x.white};
            for (u32 k = 0; k < 5; ++k) {
                const bool has = d.material->tex[k].valid();
                c.bind_texture(k, has ? d.material->tex[k] : fallback[k],
                               has && d.material->samp[k].valid() ? d.material->samp[k] : SamplerHandle{x.linearSampler});
            }
            c.bind_texture(5, d.env ? d.env->irradiance : x.irradiance, x.cubeSampler);
            c.bind_texture(6, d.env ? d.env->prefiltered : x.prefiltered, x.cubeSampler);
            c.bind_texture(7, d.env ? d.env->brdf : x.brdf, SamplerHandle{x.clampSampler});
            if (x.shadow.valid()) {
                c.bind_texture(8, pc.texture(x.shadow), SamplerHandle{x.compareSampler});
                c.bind_texture(11, pc.texture(x.shadow), SamplerHandle{x.nearestSampler});
            }
            boundMat = d.material;
            boundEnv = d.env;
        }
        const void* block = sharedBlock ? sharedBlock : static_cast<const void*>(d.block);
        if (d.skinned) c.bind_storage_buffer(x.joints);
        if (d.morph) {
            c.bind_vertex_buffer(0, x.morph, d.morphPos);
            c.bind_vertex_buffer(1, x.morph, d.morphShade);
            if (d.model->skin.valid()) {
                c.bind_vertex_buffer(2, d.model->skin, static_cast<u64>(d.prim->vertexOffset) * sizeof(SkinVertex));
            }
            c.bind_index_buffer(d.model->indices, 0, d.model->indexType);
            if (d.instanceCount > 1) c.bind_storage_buffer(x.inst);
            boundModel = nullptr;
            c.set_uniforms(block, sizeof(SceneBlock));
            c.push_constants(&d.push, sizeof(MeshPush));
            c.draw_indexed(d.indexCount, std::max(1u, d.instanceCount), d.firstIndex, 0, 0);
            continue;
        }
        if (d.model != boundModel || d.skinned) {
            c.bind_vertex_buffer(0, d.model->positions, 0);
            c.bind_vertex_buffer(1, d.model->shading, 0);
            if (d.model->skin.valid()) c.bind_vertex_buffer(2, d.model->skin, 0);
            c.bind_index_buffer(d.model->indices, 0, d.model->indexType);
            boundModel = d.model;
        }
        if (d.instanceCount > 1) c.bind_storage_buffer(x.inst);
        c.set_uniforms(block, sizeof(SceneBlock));
        c.push_constants(&d.push, sizeof(MeshPush));
        c.draw_indexed(d.indexCount, std::max(1u, d.instanceCount), d.firstIndex, d.prim->vertexOffset, 0);
    }
}

/// Desfoque separável (dois passes). `contactMode`: o primeiro eixo converte
/// a profundidade de contato em oclusão.
FGTexture ground_blur(FrameGraph& graph, ShaderLibrary& shaders, FGTexture src, u32 w, u32 h, f32 sigmaTexels,
                      bool contactMode) noexcept {
    auto pipe = shaders.pipeline(ground_blur_key());
    if (!pipe.ok() || w == 0 || h == 0) return FGTexture{};
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = SurfaceFormat::RGBA16F;
    d.sampled = true;
    d.renderTarget = true;
    const u64 sampler = shaders.sampler(CommonSampler::LinearClamp).id;
    // 17 taps; o passo cresce com o sigma para cobrir ±2,5σ.
    const f32 step = std::max(1.0f, sigmaTexels * 2.5f / 8.0f);
    const f32 sigmaSteps = sigmaTexels / step;
    struct Params { Vec4 dir; Vec4 cfg; };
    FGTexture in = src;
    for (int axis = 0; axis < 2; ++axis) {
        const FGTexture out = graph.create_texture(axis == 0 ? "3d-chao-desfoque-h" : "3d-chao-desfoque-v", d);
        const Params p{Vec4{axis == 0 ? step / static_cast<f32>(w) : 0.0f, axis == 1 ? step / static_cast<f32>(h) : 0.0f, 0, 0},
                       Vec4{contactMode && axis == 0 ? 1.0f : 0.0f, sigmaSteps, 0, 0}};
        struct Cap { PipelineHandle p; FGTexture src; u64 sampler; Params u; } cap{*pipe, in, sampler, p};
        const u32 pass = graph.add_raster_pass(axis == 0 ? "3d-chao-desfoque-h" : "3d-chao-desfoque-v", PassStage::Scene3D,
                                               out, LoadOp::DontCare, Vec4{}, [cap](PassContext& pc) {
            pc.cmds.bind_pipeline(cap.p);
            pc.cmds.bind_texture(0, pc.texture(cap.src), SamplerHandle{cap.sampler});
            pc.cmds.set_uniforms(&cap.u, sizeof(cap.u));
            pc.cmds.draw(3);
        });
        graph.read(pass, in);
        in = out;
    }
    return in;
}

struct GroundInputs {
    const SceneFrame* frame = nullptr;
    std::span<const GpuModel* const> models;
    const SceneBlock* header = nullptr;   ///< luzes, sombra e ambiente do quadro
    Mat4 viewProj = Mat4::identity();
    u32 width = 0, height = 0;
    u32 samples = 1;                      ///< amostras do passe principal
    bool exportQuality = false;           ///< reflexo inteiro, contato 512²
    bool lowTier = false;                 ///< nível BAIXO: sem reflexo
    TextureHandle white{}, black{};
    GroundMeshContext mesh;
};

/// Monta o reflexo e o contato (passes próprios, antes do principal) e o
/// bloco do plano. Inválido = sem chão neste quadro.
template <class DrawT, class KeyFn>
GroundDraw build_ground(FrameGraph& graph, Arena& arena, ShaderLibrary& shaders, const GroundInputs& in,
                        const DrawT* draws, u32 count, KeyFn&& keyFor) noexcept {
    GroundDraw gd;
    const SceneFrame& frame = *in.frame;
    const SceneFloor& fl = frame.floor;
    if (fl.mode == 0 || fl.mode > 2 || !in.header) return gd;
    const GroundPlacement place = ground_placement(frame, in.models);
    if (!place.valid) return gd;
    const f32 h = place.center.y;
    const f32 R = place.radius;
    const Vec3 cam = frame.camera.position;
    // Câmera abaixo do chão: não há o que ver dele.
    if (cam.y >= h) return gd;

    // --- 1. Reflexo planar -----------------------------------------------------
    const f32 reflectivity = std::isfinite(fl.reflectivity) ? std::clamp(fl.reflectivity, 0.0f, 1.0f) : 0.0f;
    const f32 roughness = std::isfinite(fl.roughness) ? std::clamp(fl.roughness, 0.0f, 1.0f) : 0.35f;
    FGTexture reflection{}, reflectionDisplay{};
    const u32 div = in.exportQuality ? 1u : 2u;
    const u32 rw = std::max(1u, in.width / div), rh = std::max(1u, in.height / div);
    if (reflectivity > 0.0f && !in.lowTier && count > 0) {
        Mat4 mirror = Mat4::identity();
        mirror.col[1] = Vec4{0, -1, 0, 0};
        mirror.col[3] = Vec4{0, 2.0f * h, 0, 1};
        DrawT* list = arena.alloc_array<DrawT>(count);
        if (list) {
            // Um bloco espelhado por bloco original (materiais/ambientes).
            std::vector<std::pair<const SceneBlock*, const SceneBlock*>> remap;
            u32 n = 0;
            bool hasDisplayReflection = false;
            for (u32 i = 0; i < count; ++i) {
                DrawT d = draws[i];
                const SceneBlock* nb = nullptr;
                for (const auto& kv : remap) if (kv.first == d.block) { nb = kv.second; break; }
                if (!nb) {
                    auto* b = static_cast<SceneBlock*>(arena.alloc(sizeof(SceneBlock), 16));
                    if (!b) continue;
                    *b = *d.block;
                    // Exposure samples can carry a different camera for each
                    // object (including the center camera when its blur is off).
                    // Reflect that camera, as the main PBR draw does.
                    b->viewProj = d.block->viewProj * mirror;
                    b->cameraPos = Vec4{d.block->cameraPos.x, 2.0f * h - d.block->cameraPos.y,
                                       d.block->cameraPos.z, d.block->cameraPos.w};
                    if (b->alpha.y > 2.5f) b->alpha.y = 1.0f;   // MASK por cobertura → descarte (1 amostra)
                    remap.emplace_back(d.block, b);
                    nb = b;
                }
                auto pipe = shaders.pipeline(ground_reflection_key(keyFor(d)));
                if (!pipe.ok()) continue;
                d.block = nb;
                d.pipeline = *pipe;
                hasDisplayReflection |= d.block->alpha.z > 0.5f;
                list[n++] = d;
            }
            if (n > 0) {
                TextureDesc cd;
                cd.width = rw;
                cd.height = rh;
                cd.format = SurfaceFormat::RGBA16F;
                cd.sampled = true;
                cd.renderTarget = true;
                TextureDesc dd = cd;
                dd.format = SurfaceFormat::Depth32F;
                dd.sampled = false;
                dd.transient = true;
                TextureDesc md = cd;
                // Unlit writes display-linear color in MRT0, while PBR writes
                // encoded radiance in MRT1. Retain the first target only when
                // it contains color, so PBR-only floors keep their old cost.
                md.sampled = hasDisplayReflection;
                const FGTexture reflDisplay = graph.create_texture("3d-chao-reflexo-exibicao", md);
                const FGTexture reflHdr = graph.create_texture("3d-chao-reflexo", cd);
                const FGTexture reflDepth = graph.create_texture("3d-chao-reflexo-prof", dd);
                struct Cap { DrawT* draws; u32 count; GroundMeshContext x; } cap{list, n, in.mesh};
                const u32 pass = graph.add_raster_pass_depth("3d-chao-reflexo", PassStage::Scene3D, reflDisplay, LoadOp::Clear,
                                                             Vec4{0, 0, 0, 0}, reflDepth, LoadOp::Clear, false, 0.0f,
                                                             [cap](PassContext& pc) {
                    ground_record_draws(pc.cmds, pc, cap.draws, cap.count, cap.x, nullptr);
                });
                graph.set_color1(pass, reflHdr);
                if (in.mesh.shadow.valid()) graph.read(pass, in.mesh.shadow);
                reflection = reflHdr;
                if (hasDisplayReflection) reflectionDisplay = reflDisplay;
                // Desfoque pela rugosidade (σ em px da resolução inteira).
                const f32 sigma = roughness * 0.03f * static_cast<f32>(in.height) / static_cast<f32>(div);
                if (sigma >= 0.6f) {
                    const FGTexture blurred = ground_blur(graph, shaders, reflHdr, rw, rh, sigma, false);
                    if (blurred.valid()) reflection = blurred;
                    if (hasDisplayReflection) {
                        const FGTexture displayBlur = ground_blur(graph, shaders, reflDisplay, rw, rh, sigma, false);
                        if (displayBlur.valid()) reflectionDisplay = displayBlur;
                    }
                }
            }
        }
    }

    // --- 2. Sombra de contato ------------------------------------------------------
    FGTexture contact{};
    Mat4 contactMatrix = Mat4::identity();
    const f32 contactStrength = std::isfinite(fl.contactShadow) ? std::clamp(fl.contactShadow, 0.0f, 1.0f) : 0.0f;
    if (contactStrength > 0.0f && count > 0) {
        const f32 extent = R * 1.3f;          // meia largura do mapa (mundo)
        const f32 H = R * 0.35f;              // faixa de altura que ocluí
        const f32 eps = R * 0.01f;
        // Olhando PARA CIMA (−Y) a partir de um pouco abaixo do chão.
        const Vec3 fwd{0, -1, 0};
        const Vec3 right{1, 0, 0};
        const Vec3 up = fwd.cross(right);
        const Vec3 eye{place.center.x, h + eps, place.center.z};
        Mat4 view;
        view.col[0] = Vec4{right.x, up.x, fwd.x, 0};
        view.col[1] = Vec4{right.y, up.y, fwd.y, 0};
        view.col[2] = Vec4{right.z, up.z, fwd.z, 0};
        view.col[3] = Vec4{-right.dot(eye), -up.dot(eye), -fwd.dot(eye), 1};
        Mat4 ortho;
        ortho.col[0] = Vec4{1.0f / extent, 0, 0, 0};
        ortho.col[1] = Vec4{0, 1.0f / extent, 0, 0};
        ortho.col[2] = Vec4{0, 0, 1.0f / (H + eps), 0};
        ortho.col[3] = Vec4{0, 0, 0, 1};
        const Mat4 contactViewProj = ortho * view;
        Mat4 toUv;
        toUv.col[0] = Vec4{0.5f, 0, 0, 0};
        toUv.col[1] = Vec4{0, 0.5f, 0, 0};
        toUv.col[3] = Vec4{0.5f, 0.5f, 0, 1};
        contactMatrix = toUv * contactViewProj;
        DrawT* list = arena.alloc_array<DrawT>(count);
        auto* block = static_cast<SceneBlock*>(arena.alloc(sizeof(SceneBlock), 16));
        if (list && block) {
            *block = *in.header;
            block->viewProj = contactViewProj;
            u32 n = 0;
            for (u32 i = 0; i < count; ++i) {
                DrawT d = draws[i];
                if (d.material && d.material->factors.alphaMode == AlphaMode::Mask) continue;   // folha/grade: sem recorte aqui
                auto pipe = shaders.pipeline(ground_contact_key(d.skinned));
                if (!pipe.ok()) continue;
                d.pipeline = *pipe;
                list[n++] = d;
            }
            if (n > 0) {
                const u32 size = in.exportQuality ? 512u : 256u;
                TextureDesc sd;
                sd.width = sd.height = size;
                sd.format = SurfaceFormat::Depth32F;
                sd.sampled = true;
                sd.renderTarget = true;
                const FGTexture depthMap = graph.create_texture("3d-chao-contato", sd);
                struct Cap { DrawT* draws; u32 count; GroundMeshContext x; const SceneBlock* block; } cap{list, n, in.mesh, block};
                graph.add_raster_pass_depth("3d-chao-contato", PassStage::Scene3D, FGTexture{}, LoadOp::DontCare, Vec4{},
                                            depthMap, LoadOp::Clear, true, 1.0f, [cap](PassContext& pc) {
                    ground_record_draws(pc.cmds, pc, cap.draws, cap.count, cap.x, cap.block);
                });
                // σ ≈ 4% do raio: a pegada macia do "assentado".
                const f32 sigma = 0.04f * R / (2.0f * extent / static_cast<f32>(size));
                contact = ground_blur(graph, shaders, depthMap, size, size, sigma, true);
            }
        }
    }

    // --- 3. O plano ------------------------------------------------------------------
    auto pipe = shaders.pipeline(ground_key(fl.mode, in.samples, true));
    if (!pipe.ok()) return gd;
    auto* gb = static_cast<GroundBlock*>(arena.alloc(sizeof(GroundBlock), 16));
    if (!gb) return gd;
    *gb = {};
    const SceneBlock& hd = *in.header;
    const f32 fadeMul = std::isfinite(fl.fade) ? std::clamp(fl.fade, 1.0f, 100.0f) : 6.0f;
    const f32 fadeEnd = R * std::max(fadeMul, 1.5f);
    gb->viewProj = in.viewProj;
    gb->cameraPos = Vec4{cam, 1.0f};
    gb->center = Vec4{place.center.x, h, place.center.z, fadeEnd * 1.02f};
    gb->color = Vec4{std::clamp(fl.color.x, 0.0f, 1.0f), std::clamp(fl.color.y, 0.0f, 1.0f), std::clamp(fl.color.z, 0.0f, 1.0f),
                     static_cast<f32>(fl.mode)};
    gb->params = Vec4{roughness, reflectivity, contactStrength, fadeEnd};
    gb->envParams = hd.envParams;
    gb->viewport = Vec4{1.0f / static_cast<f32>(in.width), 1.0f / static_cast<f32>(in.height), reflection.valid() ? 1.0f : 0.0f,
                        std::min(R * 1.2f, fadeEnd * 0.5f)};
    gb->skyColor = hd.skyColor;
    gb->groundColor = hd.groundColor;
    gb->lightCount = hd.lightCount;
    for (int i = 0; i < 4; ++i) {
        gb->lightPos[i] = hd.lightPos[i];
        gb->lightColor[i] = hd.lightColor[i];
        gb->lightSpot[i] = hd.lightSpot[i];
        gb->lightSpot2[i] = hd.lightSpot2[i];
    }
    gb->shadowMatrix = hd.shadowMatrix;
    gb->shadowParams = hd.shadowParams;
    gb->shadowParams2 = hd.shadowParams2;
    gb->contactMatrix = contactMatrix;
    gb->contactParams = Vec4{contact.valid() ? 1.0f : 0.0f, reflectionDisplay.valid() ? 1.0f : 0.0f, 0, 0};
    gd.pipeline = *pipe;
    gd.block = gb;
    gd.reflection = reflection;
    gd.reflectionDisplay = reflectionDisplay;
    gd.contact = contact;
    gd.shadow = in.mesh.shadow;
    gd.fallback = in.black.valid() ? in.black : in.white;
    gd.irradiance = in.mesh.irradiance;
    gd.prefiltered = in.mesh.prefiltered;
    gd.brdf = in.mesh.brdf;
    gd.cubeSampler = in.mesh.cubeSampler;
    gd.linearClamp = in.mesh.clampSampler;
    gd.nearest = in.mesh.nearestSampler;
    gd.compare = in.mesh.compareSampler;
    return gd;
}

/// Desenha o plano (dentro do passe principal, depois dos opacos).
void draw_ground(CommandList& c, PassContext& pc, const GroundDraw& g) noexcept {
    if (!g.valid()) return;
    c.bind_pipeline(g.pipeline);
    c.bind_texture(0, g.reflection.valid() ? pc.texture(g.reflection) : g.fallback, SamplerHandle{g.linearClamp});
    c.bind_texture(1, g.contact.valid() ? pc.texture(g.contact) : g.fallback, SamplerHandle{g.linearClamp});
    c.bind_texture(2, g.reflectionDisplay.valid() ? pc.texture(g.reflectionDisplay) : g.fallback, SamplerHandle{g.linearClamp});
    c.bind_texture(5, g.irradiance, g.cubeSampler);
    c.bind_texture(6, g.prefiltered, g.cubeSampler);
    c.bind_texture(7, g.brdf, SamplerHandle{g.linearClamp});
    if (g.shadow.valid()) {
        c.bind_texture(8, pc.texture(g.shadow), SamplerHandle{g.compare});
        c.bind_texture(11, pc.texture(g.shadow), SamplerHandle{g.nearest});
    }
    c.set_uniforms(g.block, sizeof(GroundBlock));
    c.draw(6);
}

/// Leituras do passe principal (o grafo ordena e faz as transições).
void ground_reads(FrameGraph& graph, u32 pass, const GroundDraw& g) noexcept {
    if (!g.valid()) return;
    if (g.reflection.valid()) graph.read(pass, g.reflection);
    if (g.reflectionDisplay.valid()) graph.read(pass, g.reflectionDisplay);
    if (g.contact.valid()) graph.read(pass, g.contact);
}

} // namespace
