// =============================================================================
//  Aurea / scene3d / SceneRenderer.cpp
// =============================================================================
#include "aurea/scene3d/SceneRenderer.hpp"

#include "aurea/core/Log.hpp"

#include <algorithm>
#include <deque>
#include <tuple>
#include <chrono>
#include <cmath>
#include <cstring>
#include <new>

namespace aurea::scene3d {
namespace {

// Bloco de parâmetros (std140) — espelho exato de shaders/scene3d/common/scene.glsl.
struct alignas(16) SceneBlock {
    Mat4 viewProj;
    Vec4 cameraPos;
    Vec4 envParams;
    Vec4 skyColor;
    Vec4 groundColor;
    Vec4 lightCount;
    Vec4 lightPos[4];
    Vec4 lightColor[4];
    Vec4 lightSpot[4];
    Vec4 lightSpot2[4];
    Vec4 baseColor;
    Vec4 emissive;
    Vec4 mr;
    Vec4 alpha;
    Vec4 uvXform[5];
    Vec4 uvRot0;
    Vec4 uvRot1;
    i32  uvSet0[4];
    i32  uvSet1[4];
    i32  texMask0[4];
    i32  texMask1[4];
    Mat4 shadowMatrix;      ///< uv/profundidade do mapa ← mundo
    Vec4 shadowParams;      ///< x = amostras do PCF (0 = sem sombra), y = texel (uv), z = viés, w = índice da luz
    Vec4 clearcoat;         ///< x = verniz, y = rugosidade do verniz, z = IOR, w = força do specular
    Vec4 specularColor;     ///< rgb = cor do specular dielétrico, w = transmissão
    Vec4 shadowParams2;     ///< x = k da penumbra, y = normal offset (mundo), z = S/R, w = amostras de bloqueador
};
static_assert(sizeof(SceneBlock) <= binding::kMaxUniformBytes, "bloco da cena 3D maior que o limite de uniform");

struct MeshPush {
    Mat4 model;
    Vec4 normalCol[3];
};
static_assert(sizeof(MeshPush) <= binding::kPushConstantBytes, "push constants da malha");

struct ShadingVertex {
    f32 normal[3];
    f32 tangent[4];
    f32 uv0[2];
    f32 uv1[2];
    u32 color;
};
static_assert(sizeof(ShadingVertex) == kMeshShadingStride, "fluxo de shading");

struct SkinVertex {
    u16 joints[4];
    u16 weights[4];
};
static_assert(sizeof(SkinVertex) == kMeshSkinStride, "fluxo de skin");

u32 mip_count(u32 w, u32 h) noexcept {
    u32 n = 1;
    while ((w | h) >> n) ++n;
    return n;
}

SamplerDesc::Wrap to_desc(Wrap w) noexcept {
    return w == Wrap::Clamp ? SamplerDesc::Wrap::ClampToEdge
         : w == Wrap::Mirror ? SamplerDesc::Wrap::MirroredRepeat : SamplerDesc::Wrap::Repeat;
}

/// Inversa transposta do 3×3 (matriz de normais), em colunas.
void normal_matrix(const Mat4& m, Vec4 out[3]) noexcept {
    const Vec3 a{m.col[0].x, m.col[0].y, m.col[0].z};
    const Vec3 b{m.col[1].x, m.col[1].y, m.col[1].z};
    const Vec3 c{m.col[2].x, m.col[2].y, m.col[2].z};
    const Vec3 r0 = b.cross(c), r1 = c.cross(a), r2 = a.cross(b);
    const f32 det = a.dot(r0);
    const f32 inv = std::fabs(det) > 1e-30f ? 1.0f / det : 0.0f;
    // (M⁻¹)ᵀ = cofatores / det; as colunas da transposta da inversa são r0, r1, r2.
    out[0] = Vec4{r0.x * inv, r0.y * inv, r0.z * inv, 0.0f};
    out[1] = Vec4{r1.x * inv, r1.y * inv, r1.z * inv, 0.0f};
    out[2] = Vec4{r2.x * inv, r2.y * inv, r2.z * inv, 0.0f};
}

/// Caixa totalmente fora do frustum (clip com Z reverso e far infinito).
bool outside_frustum(const Mat4& clipFromLocal, const Aabb& b, f32 nearZ) noexcept {
    if (!b.valid()) return false;
    u32 outL = 0, outR = 0, outT = 0, outB = 0, outN = 0;
    for (int i = 0; i < 8; ++i) {
        const Vec3 p{(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z};
        const Vec4 c = clipFromLocal * Vec4{p, 1.0f};
        outL += c.x < -c.w;
        outR += c.x > c.w;
        outT += c.y < -c.w;
        outB += c.y > c.w;
        outN += c.w < nearZ;
    }
    return outL == 8 || outR == 8 || outT == 8 || outB == 8 || outN == 8;
}

} // namespace

// =============================================================================
// Câmera
// =============================================================================
Mat4 reverse_z_perspective(f32 fovY, f32 aspect, f32 nearZ) noexcept {
    const f32 f = 1.0f / std::tan(fovY * 0.5f);
    Mat4 m;
    m.col[0] = Vec4{f / std::max(aspect, 1e-6f), 0, 0, 0};
    m.col[1] = Vec4{0, f, 0, 0};
    m.col[2] = Vec4{0, 0, 0, 1};
    m.col[3] = Vec4{0, 0, nearZ, 0};
    return m;
}

DofLens dof_lens(const SceneCamera& c, u32 imageHeight) noexcept {
    DofLens lens;
    if (!c.dof || imageHeight == 0 || !(c.focusDistance > 0.0f) || !(c.fStop > 0.0f) || !(c.blurAmount > 0.0f)
        || !std::isfinite(c.focusDistance) || !std::isfinite(c.fStop) || !std::isfinite(c.blurAmount)) return lens;
    const f32 H = static_cast<f32>(imageHeight);
    // Focal equivalente do FOV vertical no sensor full frame (24 mm de altura).
    const f32 f = 0.5f * kCameraSensorHeightMm / std::tan(std::clamp(c.fovY, 1e-3f, 3.1f) * 0.5f);
    const f32 ppm = std::max(1e-3f, c.pixelsPerMeter);
    // Foco antes da focal não forma imagem real: o plano mais perto possível
    // fica logo depois dela.
    const f32 s1 = std::max(c.focusDistance / ppm * 1000.0f, f * 1.05f);
    const f32 diameterMm = f * f / (std::max(c.fStop, 0.1f) * (s1 - f));
    lens.cocScale = 0.5f * diameterMm / kCameraSensorHeightMm * H * c.blurAmount;
    lens.focusOverNear = (s1 / 1000.0f * ppm) / std::max(c.nearZ, 1e-3f);
    lens.maxRadius = std::min(0.02f * H * std::max(1.0f, c.blurAmount), 0.05f * H);
    if (!std::isfinite(lens.cocScale) || !std::isfinite(lens.focusOverNear)) return DofLens{};
    return lens;
}

SceneCamera default_camera(u32 compWidth, u32 compHeight) noexcept {
    SceneCamera c;
    const f32 h = static_cast<f32>(std::max(1u, compHeight));
    const f32 dist = h * 1.2f;
    c.fovY = 2.0f * std::atan(0.5f * h / dist);
    c.position = Vec3{static_cast<f32>(compWidth) * 0.5f, h * 0.5f, -dist};
    c.view = Mat4::translation(-c.position);
    c.nearZ = std::max(1.0f, dist * 0.01f);
    return c;
}

// =============================================================================
// GpuModel
// =============================================================================
Status GpuModel::upload(GPUBackend& gpu, const SceneAsset& asset) noexcept {
    // --- Geometria: três fluxos concatenados de todas as primitivas ------------
    usize vertexTotal = 0, indexTotal = 0;
    bool anySkin = false, fits16 = true;
    for (const Mesh& m : asset.meshes) {
        for (const Primitive& p : m.primitives) {
            vertexTotal += p.positions.size();
            indexTotal += p.indices.size();
            for (const auto& l : p.lods) indexTotal += l.size();
            anySkin = anySkin || p.skinned();
            fits16 = fits16 && p.positions.size() <= 65536;
        }
    }
    if (vertexTotal == 0 || indexTotal == 0) return Status{Errc::InvalidArgument, "modelo sem geometria"};
    indexType = fits16 ? IndexType::U16 : IndexType::U32;

    std::vector<Vec3> pos;
    std::vector<ShadingVertex> shade;
    std::vector<SkinVertex> skinV;
    std::vector<u16> idx16;
    std::vector<u32> idx32;
    pos.reserve(vertexTotal);
    shade.reserve(vertexTotal);
    if (anySkin) skinV.reserve(vertexTotal);
    if (fits16) idx16.reserve(indexTotal); else idx32.reserve(indexTotal);

    meshes.resize(asset.meshes.size());
    for (usize mi = 0; mi < asset.meshes.size(); ++mi) {
        for (const Primitive& p : asset.meshes[mi].primitives) {
            GpuPrimitive g;
            g.firstIndex = static_cast<u32>(fits16 ? idx16.size() : idx32.size());
            g.indexCount = static_cast<u32>(p.indices.size());
            g.vertexOffset = static_cast<i32>(pos.size());
            g.vertexCount = p.vertex_count();
            g.material = p.material;
            g.skinned = p.skinned();
            g.bounds = p.bounds;
            const usize n = p.positions.size();
            for (usize v = 0; v < n; ++v) {
                pos.push_back(p.positions[v]);
                ShadingVertex s{};
                const Vec3 nn = v < p.normals.size() ? p.normals[v] : Vec3{0, 0, 1};
                s.normal[0] = nn.x; s.normal[1] = nn.y; s.normal[2] = nn.z;
                const Vec4 t = v < p.tangents.size() ? p.tangents[v] : Vec4{1, 0, 0, 1};
                s.tangent[0] = t.x; s.tangent[1] = t.y; s.tangent[2] = t.z; s.tangent[3] = t.w;
                const Vec2 a = v < p.uv0.size() ? p.uv0[v] : Vec2{0, 0};
                const Vec2 b = v < p.uv1.size() ? p.uv1[v] : a;
                s.uv0[0] = a.x; s.uv0[1] = a.y;
                s.uv1[0] = b.x; s.uv1[1] = b.y;
                s.color = v < p.colors.size() ? p.colors[v] : 0xFFFFFFFFu;
                shade.push_back(s);
                if (anySkin) {
                    SkinVertex k{};
                    if (p.skinned()) {
                        for (int c = 0; c < 4; ++c) k.joints[c] = p.joints[v * 4 + c];
                        const Vec4 w = p.weights[v];
                        const f32 ws[4] = {w.x, w.y, w.z, w.w};
                        for (int c = 0; c < 4; ++c) k.weights[c] = static_cast<u16>(std::lround(std::clamp(ws[c], 0.0f, 1.0f) * 65535.0f));
                    } else {
                        k.weights[0] = 65535;
                    }
                    skinV.push_back(k);
                }
            }
            for (u32 i : p.indices) {
                if (fits16) idx16.push_back(static_cast<u16>(i)); else idx32.push_back(i);
            }
            g.lodFirst[0] = g.firstIndex;
            g.lodCount[0] = g.indexCount;
            for (const auto& lod : p.lods) {
                if (g.lodLevels >= GpuPrimitive::kMaxLods) break;
                g.lodFirst[g.lodLevels] = static_cast<u32>(fits16 ? idx16.size() : idx32.size());
                g.lodCount[g.lodLevels] = static_cast<u32>(lod.size());
                for (u32 i : lod) {
                    if (fits16) idx16.push_back(static_cast<u16>(i)); else idx32.push_back(i);
                }
                ++g.lodLevels;
            }
            meshes[mi].push_back(g);
        }
    }

    auto make = [&](usize bytes, BufferUsage usage, const void* data, const char* name, BufferHandle& out) -> Status {
        BufferDesc d;
        d.bytes = bytes;
        d.usage = usage | BufferUsage::TransferDst;
        d.access = MemoryAccess::GpuOnly;
        d.debugName = name;
        auto b = gpu.create_buffer(d);
        if (!b.ok()) return b.status();
        out = *b;
        geometryBytes += bytes;
        return gpu.write_buffer(out, 0, data, bytes);
    };
    if (Status s = make(pos.size() * sizeof(Vec3), BufferUsage::Vertex, pos.data(), "3d-posicoes", positions); !s.ok()) return s;
    if (Status s = make(shade.size() * sizeof(ShadingVertex), BufferUsage::Vertex, shade.data(), "3d-shading", shading); !s.ok()) return s;
    if (anySkin) {
        if (Status s = make(skinV.size() * sizeof(SkinVertex), BufferUsage::Vertex, skinV.data(), "3d-skin", skin); !s.ok()) return s;
    }
    if (fits16) {
        if (Status s = make(idx16.size() * 2, BufferUsage::Index, idx16.data(), "3d-indices", indices); !s.ok()) return s;
    } else {
        if (Status s = make(idx32.size() * 4, BufferUsage::Index, idx32.data(), "3d-indices", indices); !s.ok()) return s;
    }

    // --- Texturas: uma por (imagem, espaço de cor) --------------------------
    Errc resourceFailure = Errc::Ok;
    std::unordered_map<u64, TextureHandle> byImage;
    auto texture_for = [&](const TextureRef& ref, bool srgb) -> TextureHandle {
        if (!ref.valid() || ref.image >= static_cast<i32>(asset.images.size())) return {};
        const Image& img = asset.images[ref.image];
        const auto& pixels = img.pixels();
        if (pixels.empty() || img.width == 0) return {};
        const u64 key = (static_cast<u64>(ref.image) << 1) | (srgb ? 1u : 0u);
        if (auto it = byImage.find(key); it != byImage.end()) return it->second;
        TextureDesc d;
        d.width = img.width;
        d.height = img.height;
        d.mipLevels = mip_count(img.width, img.height);
        d.format = srgb ? SurfaceFormat::RGBA8_sRGB : SurfaceFormat::RGBA8;
        d.sampled = true;
        d.transferDst = true;
        d.debugName = srgb ? "3d-textura-cor" : "3d-textura-dado";
        auto t = gpu.create_texture(d);
        if (!t.ok()) { resourceFailure = t.code(); return {}; }
        const Status uploaded = gpu.upload_texture_level(*t, 0, 0, pixels.data(), pixels.size());
        if (!uploaded.ok()) {
            resourceFailure = uploaded.code();
            gpu.destroy_texture(*t);
            return {};
        }
        // Sem blit linear no formato: fica sem mips (correto, só mais serrilhado).
        if (!gpu.generate_mipmaps(*t).ok()) AUREA_LOG_WARN("3D: mips nao gerados para uma textura");
        ownedTextures_.push_back(*t);
        textureBytes += d.estimated_bytes();
        byImage.emplace(key, *t);
        return *t;
    };
    const f32 aniso = std::min(8.0f, gpu.capabilities().maxSamplerAnisotropy);
    std::unordered_map<i32, SamplerHandle> bySampler;
    auto sampler_for = [&](const TextureRef& ref) -> SamplerHandle {
        if (auto it = bySampler.find(ref.sampler); it != bySampler.end()) return it->second;
        Sampler s = ref.sampler >= 0 && ref.sampler < static_cast<i32>(asset.samplers.size()) ? asset.samplers[ref.sampler] : Sampler{};
        SamplerDesc d;
        d.magFilter = s.mag == Filter::Nearest ? SamplerDesc::Filter::Nearest : SamplerDesc::Filter::Linear;
        d.minFilter = s.min == Filter::Nearest ? SamplerDesc::Filter::Nearest : SamplerDesc::Filter::Linear;
        d.mipmap = SamplerDesc::Mipmap::Linear;
        d.wrapU = to_desc(s.wrapS);
        d.wrapV = to_desc(s.wrapT);
        d.maxAnisotropy = aniso;
        auto h = gpu.create_sampler(d);
        if (!h.ok()) resourceFailure = h.code();
        const SamplerHandle out = h.ok() ? *h : SamplerHandle{};
        if (h.ok()) ownedSamplers_.push_back(out);
        bySampler.emplace(ref.sampler, out);
        return out;
    };

    materials.resize(asset.materials.size());
    for (usize i = 0; i < asset.materials.size(); ++i) {
        const Material& m = asset.materials[i];
        GpuMaterial& g = materials[i];
        g.factors = m;
        const TextureRef* refs[5] = {&m.baseColorTex, &m.metallicRoughnessTex, &m.normalTex, &m.occlusionTex, &m.emissiveTex};
        const bool srgb[5] = {true, false, false, false, true};
        for (int k = 0; k < 5; ++k) {
            g.tex[k] = texture_for(*refs[k], srgb[k]);
            if (g.tex[k].valid()) g.samp[k] = sampler_for(*refs[k]);
        }
    }
    // Material padrão do glTF: branco, metal 1, rugosidade 1 — como o spec manda
    // para primitivas sem material.
    defaultMaterial.factors = Material{};
    if (resourceFailure != Errc::Ok) return Status{resourceFailure, "recurso do modelo 3D nao subiu para GPU"};
    return OkStatus;
}

void GpuModel::release(GPUBackend& gpu) noexcept {
    for (BufferHandle* b : {&positions, &shading, &skin, &indices}) {
        if (b->valid()) gpu.destroy_buffer(*b);
        *b = BufferHandle{};
    }
    for (TextureHandle t : ownedTextures_) gpu.destroy_texture(t);
    for (SamplerHandle s : ownedSamplers_) gpu.destroy_sampler(s);
    ownedTextures_.clear();
    ownedSamplers_.clear();
    meshes.clear();
    materials.clear();
}

// Chão do grupo 3D (reflexo planar, sombra de contato, plano): ver Ground.cpp.
#include "Ground.cpp"

// =============================================================================
// SceneRenderer
// =============================================================================
Status SceneRenderer::initialize(GPUBackend& gpu, ShaderLibrary& shaders) noexcept {
    gpu_ = &gpu;
    shaders_ = &shaders;
    auto solid = [&](u8 r, u8 g, u8 b, u8 a, SurfaceFormat fmt, const char* name) -> TextureHandle {
        TextureDesc d;
        d.width = d.height = 1;
        d.format = fmt;
        d.sampled = true;
        d.transferDst = true;
        d.debugName = name;
        auto t = gpu.create_texture(d);
        if (!t.ok()) return {};
        const u8 px[4] = {r, g, b, a};
        (void)gpu.upload_texture_level(*t, 0, 0, px, 4);
        return *t;
    };
    white_ = solid(255, 255, 255, 255, SurfaceFormat::RGBA8, "3d-branco");
    flatNormal_ = solid(128, 128, 255, 255, SurfaceFormat::RGBA8, "3d-normal-plana");
    black_ = solid(0, 0, 0, 255, SurfaceFormat::RGBA8, "3d-preto");
    brdfLut_ = solid(255, 0, 0, 255, SurfaceFormat::RGBA8, "3d-brdf-padrao");
    {
        TextureDesc d;
        d.width = d.height = 1;
        d.cube = true;
        d.layers = 6;
        d.format = SurfaceFormat::RGBA8;
        d.sampled = true;
        d.transferDst = true;
        d.debugName = "3d-ambiente-padrao";
        auto t = gpu.create_texture(d);
        if (t.ok()) {
            envCube_ = *t;
            const u8 px[4] = {0, 0, 0, 255};
            for (u32 f = 0; f < 6; ++f) (void)gpu.upload_texture_level(envCube_, 0, f, px, 4);
        }
    }
    SamplerDesc sd;
    sd.mipmap = SamplerDesc::Mipmap::Linear;
    auto cs = gpu.create_sampler(sd);
    if (cs.ok()) cubeSampler_ = *cs;
    if (!white_.valid() || !flatNormal_.valid() || !envCube_.valid() || !cubeSampler_.valid()) {
        return Status{Errc::OutOfDeviceMemory, "texturas padrao do 3D"};
    }
    return OkStatus;
}

void SceneRenderer::release_environment() noexcept {
    if (!gpu_) return;
    // A finished job from the previous project must never repopulate this cache.
    if (pendingEnv_.valid()) { pendingEnv_.wait(); pendingEnv_ = {}; }
    for (auto& env : sceneEnvironments_) {
        for (TextureHandle t : {env.irradiance, env.prefiltered, env.brdf, env.background})
            if (t.valid()) gpu_->destroy_texture(t);
    }
    sceneEnvironments_.clear();
    for (auto& kv : envSets_) {
        for (TextureHandle* t : {&kv.second.irradiance, &kv.second.prefiltered, &kv.second.brdf}) {
            if (t->valid()) gpu_->destroy_texture(*t);
        }
    }
    envSets_.clear();
    for (TextureHandle* t : {&irradiance_, &prefiltered_, &iblLut_, &background_}) {
        if (t->valid()) gpu_->destroy_texture(*t);
        *t = TextureHandle{};
    }
    // As texturas foram embora, então o que o renderer ACHA que está na GPU
    // também tem de ser esquecido — senão `request_environment` compara a
    // chave nova com uma antiga ainda "montada", não gera nada, e a cena fica
    // sem ambiente nenhum. The 1x1 black `envCube_` is a renderer-owned
    // typed fallback, not project HDR data. Keep it until shutdown: the
    // first asynchronous frame still binds samplerCube at PBR slots 5/6.
    envKey_ = pendingKey_ = ~0ull;
    envSpecTier_ = envBgTier_ = pendingSpecTier_ = pendingBgTier_ = 0;
    environmentBytes_ = 0;
    prefilteredSize_ = backgroundSize_ = 0;
    envRequested_ = false;
}

Status SceneRenderer::upload_environment(const EnvironmentMaps& maps, EnvSet& out) noexcept {
    if (!gpu_) return Errc::InvalidState;
    auto cube = [&](const CubeData& c, const char* name, TextureHandle& out) -> Status {
        TextureDesc d;
        d.width = d.height = c.size;
        d.cube = true;
        d.layers = 6;
        d.mipLevels = c.mips;
        d.format = SurfaceFormat::RGBA16F;
        d.sampled = true;
        d.transferDst = true;
        d.debugName = name;
        auto t = gpu_->create_texture(d);
        if (!t.ok()) return t.status();
        for (u32 m = 0; m < c.mips; ++m) {
            const u32 s = std::max(1u, c.size >> m);
            const usize faceHalfs = static_cast<usize>(s) * s * 4;
            for (u32 f = 0; f < 6; ++f) {
                const Status st = gpu_->upload_texture_level(*t, m, f, c.levels[m].data() + faceHalfs * f, faceHalfs * 2);
                if (!st.ok()) {
                    gpu_->destroy_texture(*t);
                    return st;
                }
            }
        }
        out = *t;
        return OkStatus;
    };
    TextureHandle irr{}, pre{}, lut{};
    if (Status s = cube(maps.irradiance, "3d-irradiancia", irr); !s.ok()) return s;
    if (Status s = cube(maps.prefiltered, "3d-especular", pre); !s.ok()) {
        gpu_->destroy_texture(irr);
        return s;
    }
    TextureDesc ld;
    ld.width = ld.height = maps.lutSize;
    ld.format = SurfaceFormat::RGBA16F;
    ld.sampled = true;
    ld.transferDst = true;
    ld.debugName = "3d-brdf";
    auto l = gpu_->create_texture(ld);
    if (!l.ok() || !gpu_->upload_texture_level(*l, 0, 0, maps.brdfLut.data(), maps.brdfLut.size() * 2).ok()) {
        if (l.ok()) gpu_->destroy_texture(*l);
        gpu_->destroy_texture(irr);
        gpu_->destroy_texture(pre);
        return Status{Errc::OutOfDeviceMemory, "LUT da BRDF"};
    }
    lut = *l;
    out.irradiance = irr;
    out.prefiltered = pre;
    out.brdf = lut;
    out.mips = maps.prefiltered.mips;
    out.bytes = maps.brdfLut.size() * sizeof(u16);
    for (const auto* cube : {&maps.irradiance, &maps.prefiltered})
        for (const auto& level : cube->levels) out.bytes += level.size() * sizeof(u16);
    return OkStatus;
}

Status SceneRenderer::set_environment(const EnvironmentMaps& maps) noexcept {
    EnvSet novo;
    const Status s = upload_environment(maps, novo);
    if (!s.ok()) return s;
    // Troca SÓ os mapas do grupo. Não é `release_environment()`: ela esquece
    // também a chave e o pedido pendente (e os ambientes por objeto) — com a
    // chave zerada, quem chamou gravava `envKey_ = pendingKey_ = ~0`, o
    // próximo quadro não reconhecia o ambiente que acabou de subir e pedia
    // outro: o IBL era refeito em laço durante o preview (texto/modelo 3D
    // piscando entre a luz do HDRI e a do estúdio, e CPU esquentando).
    if (irradiance_.valid()) sceneEnvironments_.push_back(current_environment());
    irradiance_ = prefiltered_ = iblLut_ = background_ = TextureHandle{};
    environmentBytes_ = novo.bytes;
    // Fundo com a cadeia de mips inteira: o céu escolhe o LOD pela pegada do
    // pixel (nítido sem serrilhar) e o desfoque sobe nela.
    backgroundSize_ = 0;
    const CubeData& b = maps.background;
    if (b.size > 0 && b.mips > 0 && b.levels.size() >= b.mips) {
        TextureDesc bg;
        bg.width = bg.height = b.size; bg.layers = 6; bg.cube = true; bg.mipLevels = b.mips;
        bg.format = SurfaceFormat::RGBA16F; bg.sampled = true; bg.transferDst = true; bg.debugName = "3d-fundo";
        auto texture = gpu_->create_texture(bg);
        if (texture.ok()) {
            bool uploaded = true;
            for (u32 m = 0; m < b.mips && uploaded; ++m) {
                const u32 side = std::max(1u, b.size >> m);
                const usize count = static_cast<usize>(side) * side * 4;
                for (u32 f = 0; f < 6 && uploaded; ++f)
                    uploaded = gpu_->upload_texture_level(*texture, m, f, b.levels[m].data() + count * f, count * 2).ok();
            }
            if (uploaded) {
                background_ = *texture; backgroundSize_ = b.size;
                for (const auto& level : b.levels) environmentBytes_ += level.size() * sizeof(u16);
            } else gpu_->destroy_texture(*texture);
        }
    }
    ++envUploads_;
    irradiance_ = novo.irradiance;
    prefiltered_ = novo.prefiltered;
    iblLut_ = novo.brdf;
    prefilteredMips_ = novo.mips;
    prefilteredSize_ = maps.prefiltered.size;
    return OkStatus;
}

SceneRenderer::CachedSceneEnvironment SceneRenderer::current_environment() const noexcept {
    return {envKey_, envLastFrame_, irradiance_, prefiltered_, iblLut_, background_,
            prefilteredMips_, prefilteredSize_, backgroundSize_, envSpecTier_, envBgTier_, environmentBytes_};
}

void SceneRenderer::use_environment(const CachedSceneEnvironment& e) noexcept {
    envKey_ = e.key; envLastFrame_ = e.lastFrame;
    irradiance_ = e.irradiance; prefiltered_ = e.prefiltered; iblLut_ = e.brdf; background_ = e.background;
    prefilteredMips_ = e.mips; prefilteredSize_ = e.specularSize; backgroundSize_ = e.backgroundSize;
    envSpecTier_ = e.specularTier; envBgTier_ = e.backgroundTier;
    environmentBytes_ = e.bytes;
}

bool SceneRenderer::restore_environment(u64 key) noexcept {
    auto best = sceneEnvironments_.end();
    u64 quality = key == envKey_ ? (static_cast<u64>(envSpecTier_) << 32) | envBgTier_ : 0;
    for (auto it = sceneEnvironments_.begin(); it != sceneEnvironments_.end(); ++it) {
        const u64 tier = (static_cast<u64>(it->specularTier) << 32) | it->backgroundTier;
        if (it->key == key && tier > quality) { best = it; quality = tier; }
    }
    if (best != sceneEnvironments_.end()) {
        const auto previous = current_environment();
        use_environment(*best); *best = previous;
    }
    return key == envKey_;
}

void SceneRenderer::trim_scene_environments(u64 frameNumber) noexcept {
    // Keep four recently used environments. More may coexist in a complex
    // frame, but never evict a texture captured by an unsubmitted pass.
    constexpr u64 cacheBudget = 64ull * 1024 * 1024;
    u64 cachedBytes = 0;
    for (const auto& e : sceneEnvironments_) cachedBytes += e.bytes;
    for (const auto& e : envSets_) cachedBytes += e.second.bytes;
    while (sceneEnvironments_.size() > kMaxEnvSets || cachedBytes > cacheBudget) {
        auto oldest = sceneEnvironments_.end();
        for (auto it = sceneEnvironments_.begin(); it != sceneEnvironments_.end(); ++it)
            if (frameNumber > it->lastFrame && frameNumber - it->lastFrame > 2
                && (oldest == sceneEnvironments_.end() || it->lastFrame < oldest->lastFrame)) oldest = it;
        if (oldest == sceneEnvironments_.end()) break;
        for (TextureHandle t : {oldest->irradiance, oldest->prefiltered, oldest->brdf, oldest->background})
            if (t.valid()) gpu_->destroy_texture(t);
        cachedBytes -= oldest->bytes;
        sceneEnvironments_.erase(oldest);
    }
    while (envSets_.size() > kMaxEnvSets || cachedBytes > cacheBudget) {
        auto oldest = envSets_.end();
        for (auto it = envSets_.begin(); it != envSets_.end(); ++it)
            if (frameNumber > it->second.lastFrame && frameNumber - it->second.lastFrame > 2
                && (oldest == envSets_.end() || it->second.lastFrame < oldest->second.lastFrame)) oldest = it;
        if (oldest == envSets_.end()) break;
        for (TextureHandle t : {oldest->second.irradiance, oldest->second.prefiltered, oldest->second.brdf})
            if (t.valid()) gpu_->destroy_texture(t);
        cachedBytes -= oldest->second.bytes;
        envSets_.erase(oldest);
    }
}

namespace {
/// Constrói os mapas de um ambiente (fora da thread de render).
EnvironmentMaps build_for(const SceneEnvironment& env, const EnvironmentQuality& q) {
    if (env.hdri && env.hdri->width > 0 && !env.hdri->rgb.empty()) {
        return build_environment_from_equirect(env.hdri->rgb.data(), env.hdri->width, env.hdri->height, q);
    }
    return build_studio_environment(q);
}
/// A chave do ambiente na GPU: o HDRI (0 = estúdio neutro do projeto).
u64 key_of(const SceneEnvironment& env) noexcept { return env.hdri ? env.hdriKey : 0ull; }
} // namespace

const SceneRenderer::EnvSet* SceneRenderer::environment_set(const SceneEnvironment& env, u64 frameNumber) noexcept {
    if (!gpu_) return nullptr;
    const u64 key = key_of(env);
    for (auto& kv : envSets_) {
        if (kv.first == key) {
            kv.second.lastFrame = frameNumber;
            return &kv.second;
        }
    }
    // Reuse inactive entries; maps recently referenced by other objects or
    // precompositions stay alive and are accounted in resident_bytes().
    if (envSets_.size() >= kMaxEnvSets) {
        usize pior = envSets_.size();
        for (usize i = 0; i < envSets_.size(); ++i) {
            if (frameNumber > envSets_[i].second.lastFrame && frameNumber - envSets_[i].second.lastFrame > 2
                && (pior == envSets_.size() || envSets_[i].second.lastFrame < envSets_[pior].second.lastFrame)) pior = i;
        }
        // Several precompositions can use more than four maps in one frame.
        // They must coexist: the earlier draw lists already captured handles.
        if (pior < envSets_.size()) {
            for (TextureHandle* t : {&envSets_[pior].second.irradiance, &envSets_[pior].second.prefiltered, &envSets_[pior].second.brdf}) {
                if (t->valid()) gpu_->destroy_texture(*t);
            }
            envSets_.erase(envSets_.begin() + static_cast<ptrdiff_t>(pior));
        }
    }
    // Ambiente por objeto: só luz (sem fundo), na qualidade do preview.
    EnvironmentQuality q = envPreview_;
    q.backgroundSize = 0;
    EnvSet novo;
    if (!upload_environment(build_for(env, q), novo).ok()) { incomplete_ = true; return nullptr; }
    novo.lastFrame = frameNumber;
    envSets_.emplace_back(key, novo);
    return &envSets_.back().second;
}

void SceneRenderer::request_environment(const SceneEnvironment& env) noexcept {
    if (!gpu_) return;
    const u64 key = key_of(env);
    if (pendingEnv_.valid() && pendingEnv_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const Status s = set_environment(pendingEnv_.get());
        if (!s.ok()) { incomplete_ = true; AUREA_LOG_WARN("3D: ambiente nao subiu: %s", s.message().data()); }
        else { envKey_ = pendingKey_; envSpecTier_ = pendingSpecTier_; envBgTier_ = pendingBgTier_; }
    }
    // Atendido: mesma chave, qualidade ≥ a do preview e fundo (se pedido).
    // O que está na GPU nunca é rebaixado (um final do export serve ao preview).
    const u32 wantBg = env.showBackground ? envPreview_.backgroundSize : 0u;
    restore_environment(key);
    const bool same = key == envKey_;
    if (same) envLastFrame_ = statsFrame_ == ~0ull ? 0 : statsFrame_;
    if (same && envSpecTier_ >= envPreview_.specularSize && envBgTier_ >= wantBg
        && (wantBg == 0 || background_.valid())) return;
    if (pendingEnv_.valid()) { incomplete_ = true; return; }
    envRequested_ = true;
    pendingKey_ = key;
    EnvironmentQuality q = envPreview_;
    q.specularSize = same ? std::max(envSpecTier_, q.specularSize) : q.specularSize;
    q.backgroundSize = same ? std::max(envBgTier_, wantBg) : wantBg;
    pendingSpecTier_ = q.specularSize;
    pendingBgTier_ = q.backgroundSize;
    SceneEnvironment copy = env;
    pendingEnv_ = std::async(std::launch::async, [copy, q] { return build_for(copy, q); });
    incomplete_ = true;
}

void SceneRenderer::finish_environment(const SceneEnvironment& env) noexcept {
    // Export/captura: o ambiente CERTO, na qualidade final, já no primeiro
    // quadro (sem esperar a troca). Um preview em andamento da mesma chave e
    // qualidade serve; se for menor, espera e gera o final.
    if (!gpu_) return;
    const u64 key = key_of(env);
    restore_environment(key);
    const u32 wantBg = env.showBackground ? envFinal_.backgroundSize : 0u;
    auto enough = [&](u64 k, u32 spec, u32 bg) { return k == key && spec >= envFinal_.specularSize && bg >= wantBg; };
    if (enough(envKey_, envSpecTier_, envBgTier_) && (wantBg == 0 || background_.valid()) && !pendingEnv_.valid()) return;
    EnvironmentMaps maps;
    u32 spec = 0, backgroundTier = 0;
    if (pendingEnv_.valid() && enough(pendingKey_, pendingSpecTier_, pendingBgTier_)) {
        maps = pendingEnv_.get();
        spec = pendingSpecTier_;
        backgroundTier = pendingBgTier_;
    } else {
        if (pendingEnv_.valid()) pendingEnv_.wait();
        pendingEnv_ = {};
        if (enough(envKey_, envSpecTier_, envBgTier_) && (wantBg == 0 || background_.valid())) return;
        EnvironmentQuality q = envFinal_;
        q.backgroundSize = key == envKey_ ? std::max(envBgTier_, wantBg) : wantBg;
        q.specularSize = key == envKey_ ? std::max(envSpecTier_, q.specularSize) : q.specularSize;
        maps = build_for(env, q);
        spec = q.specularSize;
        backgroundTier = q.backgroundSize;
    }
    envRequested_ = true;
    if (const Status s = set_environment(maps); !s.ok()) {
        incomplete_ = true;
        AUREA_LOG_WARN("3D: ambiente nao subiu: %s", s.message().data());
    }
    else { envKey_ = key; envSpecTier_ = spec; envBgTier_ = backgroundTier; }
}

std::shared_ptr<SceneRenderer::UploadPool> SceneRenderer::make_upload_pool(BufferUsage usage, usize minBytes, const char* name) {
    auto pool = std::make_shared<UploadPool>();
    pool->usage = usage;
    pool->minBytes = minBytes;
    pool->name = name;
    return pool;
}

namespace {
/// Um buffer emprestado a um build; volta ao pool quando a GPU termina o quadro.
struct UploadTicket {
    std::shared_ptr<SceneRenderer::UploadPool> pool;
    SceneRenderer::UploadPool::Buf buf;
    u32 generation = 0;
};
} // namespace

BufferHandle SceneRenderer::take_upload(const std::shared_ptr<UploadPool>& pool, usize bytes) noexcept {
    UploadPool::Buf buf{};
    BufferHandle retire{};
    {
        std::lock_guard<std::mutex> lock(pool->mutex);
        // O menor livre que cabe (em regime as capacidades ficam parecidas).
        usize pick = pool->free.size();
        for (usize i = 0; i < pool->free.size(); ++i) {
            if (pool->free[i].cap >= bytes && (pick == pool->free.size() || pool->free[i].cap < pool->free[pick].cap)) pick = i;
        }
        if (pick < pool->free.size()) {
            buf = pool->free[pick];
            pool->free[pick] = pool->free.back();
            pool->free.pop_back();
        } else if (!pool->free.empty()) {
            // Nenhum cabe: um pequeno sai, para o pool não acumular sobras.
            retire = pool->free.back().handle;
            pool->free.pop_back();
            pool->all.erase(std::remove_if(pool->all.begin(), pool->all.end(), [retire](const UploadPool::Buf& b) { return b.handle == retire; }), pool->all.end());
        }
    }
    if (retire.valid()) gpu_->destroy_buffer(retire);
    if (!buf.handle.valid()) {
        BufferDesc bd;
        // Exact demand avoids doubling the peak for large morph/instance uploads.
        bd.bytes = std::max<usize>(bytes, pool->minBytes);
        bd.usage = pool->usage;
        bd.access = MemoryAccess::Upload;
        bd.debugName = pool->name;
        auto b = gpu_->create_buffer(bd);
        if (!b.ok()) return BufferHandle{};
        buf = UploadPool::Buf{*b, bd.bytes};
        std::lock_guard<std::mutex> lock(pool->mutex);
        pool->all.push_back(buf);
    }
    u32 generation = 0;
    {
        std::lock_guard<std::mutex> lock(pool->mutex);
        generation = pool->generation;
    }
    // Sem memória para o bilhete o buffer só não volta à fila (o shutdown o
    // destrói pela lista `all`): nunca é reusado antes da GPU terminar.
    if (auto* ticket = new (std::nothrow) UploadTicket{pool, buf, generation}) {
        gpu_->defer_until_gpu_done([](void* p) {
            std::unique_ptr<UploadTicket> t(static_cast<UploadTicket*>(p));
            std::lock_guard<std::mutex> lock(t->pool->mutex);
            if (t->generation == t->pool->generation) t->pool->free.push_back(t->buf);
        }, ticket);
    }
    return buf.handle;
}

void SceneRenderer::drop_upload_pools(bool destroy) noexcept {
    for (const std::shared_ptr<UploadPool>& pool : {jointPool_, morphPool_, instPool_}) {
        std::lock_guard<std::mutex> lock(pool->mutex);
        if (destroy && gpu_) {
            for (const auto& b : pool->all) gpu_->destroy_buffer(b.handle);
        }
        pool->all.clear();
        pool->free.clear();
        ++pool->generation;
    }
}

u32 SceneRenderer::trim_upload_pools() noexcept {
    u32 released = 0;
    if (!gpu_) return released;
    for (const auto& pool : {jointPool_, morphPool_, instPool_}) {
        std::lock_guard<std::mutex> lock(pool->mutex);
        for (const auto& b : pool->free) {
            gpu_->destroy_buffer(b.handle);
            pool->all.erase(std::remove_if(pool->all.begin(), pool->all.end(), [&b](const UploadPool::Buf& held) { return held.handle == b.handle; }), pool->all.end());
            ++released;
        }
        pool->free.clear();
    }
    return released;
}

void SceneRenderer::shutdown() noexcept {
    if (pendingEnv_.valid()) pendingEnv_.wait();
    if (!gpu_) return;
    drop_upload_pools(true);
    release_all();
    release_environment();
    for (TextureHandle* t : {&white_, &flatNormal_, &black_, &envCube_, &brdfLut_}) {
        if (t->valid()) gpu_->destroy_texture(*t);
        *t = TextureHandle{};
    }
    if (cubeSampler_.valid()) gpu_->destroy_sampler(cubeSampler_);
    cubeSampler_ = SamplerHandle{};
    gpu_ = nullptr;
}

void SceneRenderer::forget_device() noexcept {
    models_.clear();
    drop_upload_pools(false);
    irradiance_ = prefiltered_ = iblLut_ = background_ = TextureHandle{};
    white_ = flatNormal_ = black_ = envCube_ = brdfLut_ = TextureHandle{};
    cubeSampler_ = SamplerHandle{};
    sceneEnvironments_.clear();
    envSets_.clear();
    envKey_ = pendingKey_ = ~0ull;
    environmentBytes_ = 0;
    if (pendingEnv_.valid()) { pendingEnv_.wait(); pendingEnv_ = {}; }
    gpu_ = nullptr;
}

const GpuModel* SceneRenderer::model(u64 assetKey, const SceneAsset& asset) noexcept {
    Entry& e = models_[assetKey];
    if (e.model) return e.model.get();
    const u64 now = static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (e.failed || !gpu_ || now < e.retryAfterNs) { incomplete_ = true; return nullptr; }
    auto m = std::make_unique<GpuModel>();
    if (const Status s = m->upload(*gpu_, asset); !s.ok()) {
        AUREA_LOG_ERROR("3D: upload do modelo falhou: %s", s.message().data());
        m->release(*gpu_);
        // Resource pressure can clear after trimming/export. Permanent failure
        // would make the model disappear until the project was reopened.
        e.failed = s.code() != Errc::OutOfDeviceMemory && s.code() != Errc::OutOfMemory
            && s.code() != Errc::BudgetExceeded && s.code() != Errc::Timeout;
        e.retryAfterNs = now + 500'000'000ull;
        incomplete_ = true;
        return nullptr;
    }
    AUREA_LOG_INFO("3D: modelo na GPU (%.1f MB geometria, %.1f MB texturas)", m->geometryBytes / 1048576.0,
                   m->textureBytes / 1048576.0);
    e.model = std::move(m);
    return e.model.get();
}

void SceneRenderer::collect(u64 frameNumber, u64 idleFrames) noexcept {
    if (gpu_) trim_scene_environments(frameNumber);
    for (auto it = models_.begin(); it != models_.end();) {
        if (frameNumber > it->second.lastFrame + idleFrames) {
            if (it->second.model && gpu_) it->second.model->release(*gpu_);
            it = models_.erase(it);
        } else {
            ++it;
        }
    }
}

u32 SceneRenderer::trim_environment_cache(u64 frameNumber) noexcept {
    if (!gpu_) return 0;
    u32 released = 0;
    const auto release = [&](std::initializer_list<TextureHandle> textures) {
        for (TextureHandle texture : textures) if (texture.valid()) {
            gpu_->destroy_texture(texture);
            ++released;
        }
    };
    for (auto it = sceneEnvironments_.begin(); it != sceneEnvironments_.end();) {
        if (it->lastFrame >= frameNumber) { ++it; continue; }
        release({it->irradiance, it->prefiltered, it->brdf, it->background});
        it = sceneEnvironments_.erase(it);
    }
    for (auto it = envSets_.begin(); it != envSets_.end();) {
        if (it->second.lastFrame >= frameNumber) { ++it; continue; }
        release({it->second.irradiance, it->second.prefiltered, it->second.brdf});
        it = envSets_.erase(it);
    }
    if (envLastFrame_ < frameNumber) {
        release({irradiance_, prefiltered_, iblLut_, background_});
        use_environment(CachedSceneEnvironment{});
        envRequested_ = false;
    }
    // Do not wait for a background HDR build in an OS pressure callback.
    // Its future owns CPU data only; request_environment consumes it safely.
    return released;
}

void SceneRenderer::release_all() noexcept {
    drop_upload_pools(true);
    for (auto& [k, e] : models_) {
        if (e.model && gpu_) e.model->release(*gpu_);
    }
    models_.clear();
    // O AMBIENTE também é do projeto, não do aparelho. Este cache é indexado
    // pelo id do asset do HDRI (`key_of`), e dois projetos REAPROVEITAM os
    // mesmos ids: o slot 1 de um projeto não é o slot 1 do seguinte. Sem
    // soltar aqui, abrir um projeto novo continuava desenhando com o HDRI do
    // anterior — era o que o teste Gpu.HdriLightsTheModelAndSurvivesReopen
    // pegava (o verde ficava vermelho).
    release_environment();
}

u64 SceneRenderer::resident_bytes() const noexcept {
    u64 b = environmentBytes_;
    for (const auto& e : sceneEnvironments_) b += e.bytes;
    for (const auto& e : envSets_) b += e.second.bytes;
    for (const auto& [k, e] : models_) {
        if (e.model) b += e.model->geometryBytes + e.model->textureBytes;
    }
    for (const auto& pool : {jointPool_, morphPool_, instPool_}) {
        std::lock_guard<std::mutex> lock(pool->mutex);
        for (const auto& upload : pool->all) b += upload.cap;
    }
    return b;
}

PipelineKey SceneRenderer::shadow_key(bool skinned) const noexcept {
    PipelineKey k = PipelineKey::graphics(skinned ? ShaderId::scene3d_shadow_shadow_skinned_vert : ShaderId::scene3d_shadow_shadow_vert,
                                          ShaderId::scene3d_shadow_shadow_frag, SurfaceFormat::RGBA16F, false,
                                          BlendMode::Normal);
    k.mesh = skinned ? MeshLayout::SkinnedPosition : MeshLayout::PositionOnly;
    k.hasDepth = true;
    k.depthOnly = true;
    k.depthTest = true;
    k.depthWrite = true;
    k.depthCompare = CompareOp::LessOrEqual;   // mapa de sombra: Z comum, limpa em 1
    k.depthFormat = SurfaceFormat::Depth32F;
    k.cull = CullMode::None;                   // dupla face e malhas abertas projetam sombra
    k.depthBiasConstant = 1.25f;
    k.depthBiasSlope = 1.75f;
    return k;
}

PipelineKey SceneRenderer::key_for(AlphaMode mode, bool doubleSided, bool skinned) const noexcept {
    PipelineKey k = PipelineKey::graphics(skinned ? ShaderId::scene3d_pbr_mesh_skinned_vert : ShaderId::scene3d_pbr_mesh_vert,
                                          ShaderId::scene3d_pbr_pbr_frag, SurfaceFormat::RGBA16F,
                                          mode == AlphaMode::Blend, BlendMode::Normal);
    k.mesh = skinned ? MeshLayout::Skinned : MeshLayout::Static;
    k.hasDepth = true;
    k.depthTest = true;
    k.depthWrite = mode != AlphaMode::Blend;
    k.depthCompare = CompareOp::GreaterOrEqual;
    k.depthFormat = SurfaceFormat::Depth32F;
    k.cull = doubleSided ? CullMode::None : CullMode::Back;
    // Passe da cena: MSAA e MRT do quadro; MASK por cobertura com MSAA.
    k.sampleCount = static_cast<u8>(passSamples_);
    k.hasColor1 = passMrt_;
    k.alphaToCoverage = passA2C_ && mode == AlphaMode::Mask;
    // A face da frente do glTF (anti-horária vista de fora) continua anti-
    // horária no framebuffer: o giro de 180° em X (glTF → espaço da
    // composição) e o Y para baixo da projeção se cancelam. Travado pelo teste
    // Gpu.Scene3DFrontFaceIsVisibleAndBackFaceIsCulled.
    k.frontFaceCCW = true;
    return k;
}

PipelineKey SceneRenderer::plane_key(bool translucent) const noexcept {
    PipelineKey k = PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::scene3d_plane_frag, SurfaceFormat::RGBA16F, true,
                                          BlendMode::Normal);
    k.hasDepth = true;
    k.depthTest = true;
    k.depthWrite = !translucent;               // só pixels opacos podem bloquear o fundo
    k.depthCompare = CompareOp::GreaterOrEqual;
    k.depthFormat = SurfaceFormat::Depth32F;
    k.cull = CullMode::None;                   // camada vista de costas continua visível
    k.sampleCount = static_cast<u8>(passSamples_);
    k.hasColor1 = passMrt_;
    return k;
}

u32 SceneRenderer::pass_samples() const noexcept {
    return gpu_ && antialias_ ? gpu_->capabilities().msaa_samples(std::min(postMsaa_, msaaCap_)) : 1u;
}

void SceneRenderer::collect_pipelines(std::vector<PipelineKey>& out) const {
    // As chaves do passe da cena dependem das amostras (qualidade) e do MRT:
    // aquece o que a qualidade atual pede — modelos com MRT, planos com e sem.
    auto* self = const_cast<SceneRenderer*>(this);
    const u32 savedSamples = passSamples_;
    const bool savedMrt = passMrt_, savedA2C = passA2C_;
    self->passSamples_ = pass_samples();
    self->passMrt_ = true;
    self->passA2C_ = passSamples_ > 1 && gpu_ && gpu_->capabilities().alphaToOne;
    const usize groundFirst = out.size();   // chão: variantes do reflexo (Ground.cpp)
    for (AlphaMode mode : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend}) {
        for (bool twoSided : {false, true}) {
            for (bool skinned : {false, true}) {
                auto key = key_for(mode, twoSided, skinned);
                out.push_back(key);
                if (!twoSided) {
                    key.frontFaceCCW = false;
                    out.push_back(key);
                }
            }
        }
    }
    collect_ground_pipelines(out, groundFirst, out.size(), passSamples_);
    out.push_back(shadow_key(false));
    out.push_back(shadow_key(true));
    out.push_back(plane_key());
    out.push_back(plane_key(true));
    self->passMrt_ = false;
    out.push_back(plane_key());
    out.push_back(plane_key(true));
    auto sky = PipelineKey::graphics(ShaderId::common_fullscreen_vert, ShaderId::scene3d_environment_frag, SurfaceFormat::RGBA16F);
    sky.hasDepth = true; sky.depthFormat = SurfaceFormat::Depth32F;
    sky.sampleCount = static_cast<u8>(passSamples_);
    sky.hasColor1 = true;
    out.push_back(sky);
    // Pós do grupo.
    out.push_back(PipelineKey::fullscreen(ShaderId::scene3d_post_tonemap_frag, SurfaceFormat::RGBA16F));
    out.push_back(PipelineKey::fullscreen(ShaderId::scene3d_post_bloom_down_frag, SurfaceFormat::RGBA16F));
    out.push_back(PipelineKey::graphics(ShaderId::common_fullscreen_vert, ShaderId::scene3d_post_bloom_up_frag,
                                        SurfaceFormat::RGBA16F, true, BlendMode::Add));
    out.push_back(PipelineKey::fullscreen(ShaderId::scene3d_post_fxaa_frag, SurfaceFormat::RGBA16F));
    {
        out.push_back(PipelineKey::fullscreen(ShaderId::scene3d_post_dof_coc_frag, SurfaceFormat::RGBA16F));
        auto dof = PipelineKey::fullscreen(ShaderId::scene3d_post_dof_frag, SurfaceFormat::RGBA16F);
        dof.hasColor1 = true;
        out.push_back(dof);
    }
    self->passSamples_ = savedSamples;
    self->passMrt_ = savedMrt;
    self->passA2C_ = savedA2C;
}

bool SceneRenderer::build(FrameGraph& graph, Arena& arena, const SceneFrame& frame, u32 width, u32 height,
                          u64 frameNumber, FGTexture& outColor, const std::vector<ScenePlane>* planes,
                          const SceneParticleDraw* particles, u32 particleCount, FGTexture* outDepth, FGTexture* outExposureHdr) noexcept {
    if (outDepth) *outDepth = FGTexture{};
    if (outExposureHdr) *outExposureHdr = FGTexture{};
    if (frameNumber != statsFrame_) {
        trim_scene_environments(frameNumber);
        stats_ = SceneStats{};
        statsFrame_ = frameNumber;
        // Estado de LOD de primitivas que sumiram há 10 s sai (sem crescer à toa).
        if (lodState_.size() > 4096 || frameNumber % 600 == 0) {
            for (auto it = lodState_.begin(); it != lodState_.end();) {
                if (it->second.lastFrame + 600 < frameNumber) it = lodState_.erase(it); else ++it;
            }
        }
    }
    if (!gpu_ || !shaders_ || width == 0 || height == 0) return false;
    // Ambiente (IBL): o estúdio neutro ou o HDRI do projeto, gerado fora da
    // thread de render; pronto → vale no próximo quadro.
    request_environment(frame.environment);
    const bool ibl = irradiance_.valid() && envKey_ == key_of(frame.environment);

    // --- Alvos do passe da cena ----------------------------------------------
    // MSAA (a qualidade pede, o aparelho corta) e MRT: 0 = 2D de exibição,
    // 1 = cena HDR — só quando há luz de cena (modelo, texto 3D, céu). Os
    // multiamostrados e a profundidade são TRANSITÓRIOS (vivem no tile: LAZY no
    // Vulkan, memoryless no Metal); o resolve do fim do passe entrega os de 1
    // amostra. O MSAA num alvo RGBA16F é caro em banda SÓ se sair do tile — e
    // aqui ele não sai.
    // Grupo só de fundo (ver o passe direto do céu mais abaixo): 1 amostra.
    const bool skyOnly = frame.instances.empty() && frame.environment.showBackground && (!planes || planes->empty())
                      && !(particles && particleCount);
    passSamples_ = skyOnly ? 1u : pass_samples();
    passMrt_ = wants_hdr(frame) && !skyOnly;
    // Profundidade de campo da lente: precisa da profundidade de 1 amostra.
    // Com MSAA ela sai do resolve da amostra 0; aparelho SEM resolve de
    // profundidade desenha este quadro sem MSAA (o FXAA cobre as bordas, e o
    // DOF desfoca o que está fora do foco de qualquer jeito) em vez de perder
    // o efeito — preview e export iguais em todo aparelho.
    const bool dofOn = passMrt_ && dofTaps_ > 0 && dof_lens(frame.camera, height).active();
    if (dofOn && passSamples_ > 1 && !gpu_->capabilities().depthResolveSampleZero) passSamples_ = 1;
    passA2C_ = passSamples_ > 1 && gpu_->capabilities().alphaToOne;
    // O pipeline PBR do passe TEM de existir nesta contagem de amostras: sem
    // ele cada desenho era pulado em silêncio e o 3D sumia — no export e na
    // captura (que pedem 4×) mas não no preview do aparelho de entrada (2×).
    // Driver que recusa: desce a contagem (até 1 + FXAA) e lembra.
    if (!skyOnly) {
        while (passSamples_ > 1 && !shaders_->pipeline(key_for(AlphaMode::Opaque, false, false)).ok()) {
            const u32 next = passSamples_ / 2;
            AUREA_LOG_WARN("3D: pipeline PBR com MSAA %ux nao foi criado neste aparelho; usando %ux", passSamples_,
                           next);
            msaaCap_ = std::max(1u, next);
            passSamples_ = pass_samples();
            passA2C_ = passSamples_ > 1 && gpu_->capabilities().alphaToOne;
        }
    }
    const bool msaa = passSamples_ > 1;
    TextureDesc cd;
    cd.width = width;
    cd.height = height;
    cd.format = SurfaceFormat::RGBA16F;
    cd.sampled = true;
    cd.renderTarget = true;
    TextureDesc msd = cd;
    msd.sampleCount = passSamples_;
    msd.sampled = false;
    msd.transient = true;
    TextureDesc dd = cd;
    dd.format = SurfaceFormat::Depth32F;
    dd.sampled = false;
    dd.sampleCount = passSamples_;
    dd.transient = true;
    // Profundidade para quem lê depois: de 1 amostra direto (sem MSAA) ou pelo
    // resolve da amostra 0 (com MSAA, onde o aparelho resolve profundidade).
    const bool depthOut = (outDepth || dofOn) && (!msaa || gpu_->capabilities().depthResolveSampleZero);
    if (depthOut && !msaa) {
        dd.sampled = true;
        dd.transient = false;
    }
    const FGTexture color = graph.create_texture("3d-cor", cd);
    const FGTexture sceneHdr = passMrt_ ? graph.create_texture("3d-hdr", cd) : FGTexture{};
    const FGTexture colorMs = msaa ? graph.create_texture("3d-cor-msaa", msd) : FGTexture{};
    const FGTexture sceneMs = msaa && passMrt_ ? graph.create_texture("3d-hdr-msaa", msd) : FGTexture{};
    const FGTexture depth = graph.create_texture("3d-profundidade", dd);
    FGTexture depthResolved{};
    if (depthOut && msaa) {
        TextureDesc dr = cd;
        dr.format = SurfaceFormat::Depth32F;
        depthResolved = graph.create_texture("3d-profundidade-1x", dr);
    }

    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
    const Mat4 proj = frame.camera.imageTransform * reverse_z_perspective(frame.camera.fovY, aspect, frame.camera.nearZ);
    const Mat4 viewProj = proj * frame.camera.view;

    // --- Cabeçalho comum a todos os desenhos do frame ---------------------------
    auto* headerStorage = arena.alloc_array<SceneBlock>(1);
    if (!headerStorage) return false;
    *headerStorage = {};
    SceneBlock& header = *headerStorage;
    header.viewProj = viewProj;
    // A exposição do GRUPO é do pós (vale para céu, modelos e bloom juntos);
    // no shader fica só a de um objeto com ambiente próprio (relativa).
    header.cameraPos = Vec4{frame.camera.position, 1.0f};
    header.envParams = Vec4{frame.environment.intensity, ibl ? 1.0f : 0.0f, static_cast<f32>(prefilteredMips_),
                            frame.environment.rotation};
    header.skyColor = Vec4{frame.environment.sky, 1.0f};
    header.groundColor = Vec4{frame.environment.ground, 1.0f};
    const u32 lights = static_cast<u32>(std::min<usize>(frame.lights.size(), 4));
    header.lightCount = Vec4{static_cast<f32>(lights), 0, 0, 0};
    for (u32 i = 0; i < lights; ++i) {
        const SceneLight& l = frame.lights[i];
        const f32 type = static_cast<f32>(static_cast<u8>(l.kind));
        header.lightPos[i] = l.kind == LightKindGpu::Directional ? Vec4{(-l.direction).normalized(), type}
                                                                  : Vec4{l.position, type};
        header.lightColor[i] = Vec4{l.color * l.intensity, l.range};
        header.lightSpot[i] = Vec4{l.direction.normalized(), std::cos(l.outerCone)};
        header.lightSpot2[i] = Vec4{std::cos(l.innerCone), std::clamp(l.shadowStrength, 0.0f, 1.0f), 0, 0};
    }

    // --- Lista de desenho ------------------------------------------------------
    struct EnvSlot {
        u64 key;
        const SceneBlock* block;
        TextureHandle irradiance{}, prefiltered{}, brdf{};
        u64 sampler = 0;
    };

    struct Draw {
        const GpuModel* model;
        const GpuPrimitive* prim;
        const GpuMaterial* material;
        const SceneBlock* block;
        MeshPush push;
        PipelineHandle pipeline;
        f32 viewDepth;
        u32 sortKey;
        bool skinned;
        bool morph;
        usize morphPos, morphShade;
        u32 firstIndex, indexCount;   ///< o nível de detalhe escolhido
        u32 instanceCount;            ///< > 1 = instanciado (instBase no SSBO)
        const EnvSlot* env = nullptr; ///< ambiente deste objeto (v22)
    };
    // Nível de detalhe pelo tamanho na tela: diâmetro projetado da caixa.
    const f32 pxPerUnit = static_cast<f32>(height) * 0.5f / std::tan(frame.camera.fovY * 0.5f);
    auto pick_lod = [&](const GpuPrimitive& p, const Mat4& world, const Mat4& viewFromLocal, u64 stateKey, u32& first, u32& count) {
        first = p.firstIndex;
        count = p.indexCount;
        if (p.lodLevels <= 1 || !p.bounds.valid()) return;
        const Vec3 c = viewFromLocal.transform_point(p.bounds.center());
        const f32 depth = std::max(std::fabs(c.z), frame.camera.nearZ);
        const f32 sx = Vec3{world.col[0].x, world.col[0].y, world.col[0].z}.length();
        const f32 sy = Vec3{world.col[1].x, world.col[1].y, world.col[1].z}.length();
        const f32 sz = Vec3{world.col[2].x, world.col[2].y, world.col[2].z}.length();
        const f32 diameter = p.bounds.extent().length() * std::max(sx, std::max(sy, sz));
        // Viés do preview (HeavyQuality::lodBias < 1 troca antes para a malha
        // simplificada); o export usa 1.
        const f32 px = diameter / depth * pxPerUnit * lodBias_;
        auto level_at = [](f32 v, f32 k) { return v < 60.0f * k ? 2u : (v < 160.0f * k ? 1u : 0u); };
        u32 level = level_at(px, 1.0f);
        if (lodHysteresis_) {
            // Histerese: engrossa só abaixo de 85% do limiar, refina só acima
            // de 115% — um tamanho oscilando em cima do limiar não troca a
            // malha a cada quadro (o "popping" que pisca).
            auto it = lodState_.find(stateKey);
            if (it != lodState_.end()) {
                const u32 prev = it->second.level;
                const u32 coarser = level_at(px, 0.85f), finer = level_at(px, 1.15f);
                level = coarser > prev ? coarser : (finer < prev ? finer : prev);
            }
            LodState& st = lodState_[stateKey];
            st.level = static_cast<u8>(level);
            st.lastFrame = frameNumber;
        }
        level = std::min(level, p.lodLevels - 1);
        first = p.lodFirst[level];
        count = p.lodCount[level];
    };
    // Ambiente por objeto (v22): cada ambiente distinto usado no quadro tem o
    // seu cabeçalho (parâmetros) e o seu conjunto de mapas na GPU. Sem nenhum
    // objeto com ambiente próprio, é só o do grupo — o caminho de sempre.
    // Draw commands execute after this builder returns. Their environment
    // pointers must live in the frame arena, not a local vector. At most one
    // distinct environment per instance plus the scene environment is needed.
    auto* envSlots = arena.alloc_array<EnvSlot>(frame.instances.size() + 1);
    if (!envSlots) return false;
    usize envCount = 1;
    envSlots[0] = EnvSlot{ibl ? envKey_ : ~0ull, &header, ibl ? irradiance_ : envCube_, ibl ? prefiltered_ : envCube_,
                         ibl ? iblLut_ : brdfLut_, 0};
    auto env_of = [&](const SceneInstance& inst) -> const EnvSlot* {
        if (!inst.ownEnvironment) return &envSlots[0];
        const u64 k = key_of(inst.environment);
        for (usize i = 0; i < envCount; ++i) {
            const EnvSlot& e = envSlots[i];
            if (e.key == k) return &e;
        }
        const EnvSet* set = environment_set(inst.environment, frameNumber);
        if (!set) return &envSlots[0];   // sem como subir: cai no do grupo
        auto* b = static_cast<SceneBlock*>(arena.alloc(sizeof(SceneBlock), 16));
        if (!b) return &envSlots[0];
        *b = header;
        b->cameraPos.w = inst.environment.exposure;
        b->envParams = Vec4{inst.environment.intensity, 1.0f, static_cast<f32>(set->mips), inst.environment.rotation};
        b->skyColor = Vec4{inst.environment.sky, 1.0f};
        b->groundColor = Vec4{inst.environment.ground, 1.0f};
        envSlots[envCount] = EnvSlot{k, b, set->irradiance, set->prefiltered, set->brdf, 0};
        return &envSlots[envCount++];
    };

    // Stable per-frame copies change factors only; texture/buffer handles remain shared.
    auto overriddenMaterials = std::make_shared<std::deque<GpuMaterial>>();
    // MOSTRAR INTERIOR (SceneInstance::doubleSided): cópia de dupla face por
    // material de ORIGEM — dois objetos iguais com o interior ligado dividem a
    // mesma cópia e continuam instanciados juntos.
    std::vector<std::pair<const GpuMaterial*, const GpuMaterial*>> twoSidedCopies;
    auto two_sided = [&](const GpuMaterial* source) -> const GpuMaterial* {
        if (source->factors.doubleSided) return source;
        for (const auto& c : twoSidedCopies) if (c.first == source) return c.second;
        overriddenMaterials->push_back(*source);
        auto& mat = overriddenMaterials->back();
        mat.factors.doubleSided = true;
        twoSidedCopies.emplace_back(source, &mat);
        return &mat;
    };
    std::vector<std::tuple<const SceneInstance*, i32, const GpuMaterial*>> materialCopies;
    auto material_for = [&](const SceneInstance& inst, const GpuModel& model, i32 index) -> const GpuMaterial* {
        const GpuMaterial* source = index >= 0 && index < static_cast<i32>(model.materials.size()) ? &model.materials[index] : &model.defaultMaterial;
        for (const auto& cached : materialCopies) if (std::get<0>(cached) == &inst && std::get<1>(cached) == index) return std::get<2>(cached);
        for (const auto& over : inst.pose().materials) if (index >= 0 && over.materialIndex == static_cast<u32>(index) && over.mask) {
            overriddenMaterials->push_back(*source);
            auto& mat = overriddenMaterials->back();
            if (over.mask & 1) mat.factors.baseColor.x = over.baseColor.x;
            if (over.mask & 2) mat.factors.baseColor.y = over.baseColor.y;
            if (over.mask & 4) mat.factors.baseColor.z = over.baseColor.z;
            if (over.mask & 8) {
                mat.factors.baseColor.w = over.baseColor.w;
                // Opaque glTF factors ignore alpha. An explicit opacity edit
                // opts this instance into blending, preserving the source mode.
                if (mat.factors.alphaMode == AlphaMode::Opaque && over.baseColor.w < 1.0f)
                    mat.factors.alphaMode = AlphaMode::Blend;
            }
            if (over.mask & 16) mat.factors.metallic = over.metallic;
            if (over.mask & 32) mat.factors.roughness = over.roughness;
            if (over.mask & 48) mat.factors.unlit = false; // An explicit PBR edit opts this instance into lighting.
            source = &mat; break;
        }
        if (inst.doubleSided) source = two_sided(source);
        materialCopies.emplace_back(&inst, index, source); return source;
    };
    // Letra do texto 3D sumindo (SceneInstance::nodeOpacity): cópia do
    // material com a opacidade multiplicada, misturada como transparente.
    std::vector<std::tuple<const GpuMaterial*, u32, const GpuMaterial*>> fadedCopies;
    auto faded = [&](const GpuMaterial* source, f32 alpha) -> const GpuMaterial* {
        const u32 q = static_cast<u32>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
        if (q >= 255) return source;
        for (const auto& c : fadedCopies) if (std::get<0>(c) == source && std::get<1>(c) == q) return std::get<2>(c);
        overriddenMaterials->push_back(*source);
        auto& mat = overriddenMaterials->back();
        mat.factors.baseColor.w *= static_cast<f32>(q) / 255.0f;
        mat.factors.alphaMode = AlphaMode::Blend;
        fadedCopies.emplace_back(source, q, &mat);
        return &mat;
    };
    std::vector<Draw> opaque, blended;
    auto tinted = [&](const GpuMaterial* source, const Vec4& fill) -> const GpuMaterial* {
        if (fill.w <= 0) return source;
        overriddenMaterials->push_back(*source);
        auto& mat = overriddenMaterials->back();
        const f32 w = std::clamp(fill.w, 0.f, 1.f);
        mat.factors.baseColor.x += (fill.x - mat.factors.baseColor.x) * w;
        mat.factors.baseColor.y += (fill.y - mat.factors.baseColor.y) * w;
        mat.factors.baseColor.z += (fill.z - mat.factors.baseColor.z) * w;
        return &mat;
    };
    // Chave = (material, ambiente): dois objetos com o mesmo material e
    // ambientes diferentes NÃO podem dividir o mesmo bloco.
    std::vector<std::pair<std::pair<const GpuMaterial*, u64>, const SceneBlock*>> blocks;

    auto block_for = [&](const GpuMaterial* gm, const EnvSlot* env) -> const SceneBlock* {
        const u64 ek = env->key;
        for (const auto& kv : blocks) {
            if (kv.first.first == gm && kv.first.second == ek) return kv.second;
        }
        auto* b = static_cast<SceneBlock*>(arena.alloc(sizeof(SceneBlock), 16));
        if (!b) return nullptr;
        *b = header;
        if (env->block != &header) {
            // Parâmetros do ambiente deste objeto; o material sobrescreve o
            // resto logo abaixo.
            b->cameraPos.w = env->block->cameraPos.w;
            b->envParams = env->block->envParams;
            b->skyColor = env->block->skyColor;
            b->groundColor = env->block->groundColor;
        }
        const Material& m = gm->factors;
        b->baseColor = m.baseColor;
        b->emissive = Vec4{m.emissive * m.emissiveStrength, 0.0f};
        b->mr = Vec4{m.metallic, m.roughness, m.normalScale, m.occlusionStrength};
        // MASK com MSAA vira o modo 3 (recorte por cobertura, ver pbr.frag).
        const f32 alphaMode = m.alphaMode == AlphaMode::Mask && passA2C_ ? 3.0f
                            : static_cast<f32>(static_cast<u8>(m.alphaMode));
        b->alpha = Vec4{m.alphaCutoff, alphaMode, m.unlit ? 1.0f : 0.0f,
                        m.doubleSided ? 1.0f : 0.0f};
        // Verniz (pintura de carro), IOR/specular (F0 do dielétrico) e
        // transmissão (vidro fino) — lidos do glTF e antes ignorados.
        b->clearcoat = Vec4{std::clamp(m.clearcoat, 0.0f, 1.0f), std::clamp(m.clearcoatRoughness, 0.0f, 1.0f),
                            std::clamp(m.ior, 1.0f, 3.0f), std::clamp(m.specular, 0.0f, 1.0f)};
        b->specularColor = Vec4{m.specularColor.x, m.specularColor.y, m.specularColor.z, std::clamp(m.transmission, 0.0f, 1.0f)};
        const TextureRef* refs[5] = {&m.baseColorTex, &m.metallicRoughnessTex, &m.normalTex, &m.occlusionTex, &m.emissiveTex};
        for (int k = 0; k < 5; ++k) {
            b->uvXform[k] = Vec4{refs[k]->offset.x, refs[k]->offset.y, refs[k]->scale.x, refs[k]->scale.y};
            const i32 has = gm->tex[k].valid() ? 1 : 0;
            if (k < 4) {
                (&b->uvRot0.x)[k] = refs[k]->rotation;
                b->uvSet0[k] = static_cast<i32>(refs[k]->texCoord);
                b->texMask0[k] = has;
            } else {
                b->uvRot1.x = refs[k]->rotation;
                b->uvSet1[0] = static_cast<i32>(refs[k]->texCoord);
                b->texMask1[0] = has;
            }
        }
        blocks.emplace_back(std::make_pair(gm, ek), b);
        return b;
    };

    // Juntas de todas as instâncias num SSBO só; cada desenho com skin recebe
    // o deslocamento da sua skin no push constant (normalCol[0].w).
    std::vector<u32> instJointBase(frame.instances.size(), 0);
    BufferHandle joints{};
    {
        usize total = 0;
        for (usize i = 0; i < frame.instances.size(); ++i) {
            instJointBase[i] = static_cast<u32>(total);
            total += frame.instances[i].pose().jointMatrices.size();
        }
        if (total > 0) {
            const BufferHandle buf = take_upload(jointPool_, total * sizeof(Mat4));
            void* ptr = nullptr;
            if (buf.valid() && gpu_->map_buffer(buf, ptr).ok() && ptr) {
                auto* dst = static_cast<Mat4*>(ptr);
                for (usize i = 0; i < frame.instances.size(); ++i) {
                    const auto& jm = frame.instances[i].pose().jointMatrices;
                    std::copy(jm.begin(), jm.end(), dst + instJointBase[i]);
                }
                gpu_->unmap_buffer(buf);
                joints = buf;
            } else incomplete_ = true;
        }
    }

    // --- Morph (blend shapes): deformação na CPU, um bloco por primitiva -------
    // (instância, nó, primitiva) → deslocamento no buffer do quadro. Só entra
    // quem tem alvo E peso ≠ 0; o resto desenha direto da malha na GPU.
    struct MorphJob {
        usize inst, node, prim;
        const Primitive* src;
        const std::vector<f32>* weights;
        usize posOffset, shadeOffset;
        Aabb bounds;
    };
    std::vector<MorphJob> morphJobs;
    BufferHandle morphBuf{};
    {
        usize bytes = 0;
        for (usize ii = 0; ii < frame.instances.size(); ++ii) {
            const SceneInstance& inst = frame.instances[ii];
            if (!inst.asset) continue;
            const std::vector<Node>& nodes = inst.asset->nodes;
            for (usize n = 0; n < nodes.size(); ++n) {
                const i32 mi = nodes[n].mesh;
                if (mi < 0 || mi >= static_cast<i32>(inst.asset->meshes.size())) continue;
                const Mesh& mesh = inst.asset->meshes[static_cast<usize>(mi)];
                const std::vector<f32>* w = n < inst.pose().morphWeights.size() && !inst.pose().morphWeights[n].empty()
                                          ? &inst.pose().morphWeights[n] : &mesh.morphWeights;
                bool any = false;
                for (f32 v : *w) any = any || std::fabs(v) > 1e-5f;
                if (!any) continue;
                for (usize k = 0; k < mesh.primitives.size(); ++k) {
                    const Primitive& p = mesh.primitives[k];
                    if (p.morphTargets.empty()) continue;
                    MorphJob j{ii, n, k, &p, w, bytes, 0};
                    bytes += p.positions.size() * sizeof(Vec3);
                    j.shadeOffset = bytes;
                    bytes += p.positions.size() * sizeof(ShadingVertex);
                    bytes = (bytes + 255) & ~usize(255);
                    morphJobs.push_back(j);
                }
            }
        }
        if (bytes > 0) {
            const BufferHandle buf = take_upload(morphPool_, bytes);
            void* ptr = nullptr;
            if (buf.valid() && gpu_->map_buffer(buf, ptr).ok() && ptr) {
                u8* base = static_cast<u8*>(ptr);
                for (MorphJob& j : morphJobs) {
                    const Primitive& p = *j.src;
                    auto* pos = reinterpret_cast<Vec3*>(base + j.posOffset);
                    auto* sh = reinterpret_cast<ShadingVertex*>(base + j.shadeOffset);
                    const usize nv = p.positions.size();
                    const usize nt = std::min(p.morphTargets.size(), j.weights->size());
                    for (usize v = 0; v < nv; ++v) {
                        Vec3 P = p.positions[v];
                        Vec3 N = v < p.normals.size() ? p.normals[v] : Vec3{0, 0, 1};
                        for (usize t = 0; t < nt; ++t) {
                            const f32 wt = (*j.weights)[t];
                            if (wt == 0.0f) continue;
                            const MorphTarget& mt = p.morphTargets[t];
                            if (v < mt.positions.size()) P = P + mt.positions[v] * wt;
                            if (v < mt.normals.size()) N = N + mt.normals[v] * wt;
                        }
                        pos[v] = P;
                        j.bounds.add(P);
                        ShadingVertex s{};
                        const Vec3 nn = N.normalized();
                        s.normal[0] = nn.x; s.normal[1] = nn.y; s.normal[2] = nn.z;
                        const Vec4 tg = v < p.tangents.size() ? p.tangents[v] : Vec4{1, 0, 0, 1};
                        s.tangent[0] = tg.x; s.tangent[1] = tg.y; s.tangent[2] = tg.z; s.tangent[3] = tg.w;
                        const Vec2 a = v < p.uv0.size() ? p.uv0[v] : Vec2{0, 0};
                        const Vec2 b = v < p.uv1.size() ? p.uv1[v] : a;
                        s.uv0[0] = a.x; s.uv0[1] = a.y;
                        s.uv1[0] = b.x; s.uv1[1] = b.y;
                        s.color = v < p.colors.size() ? p.colors[v] : 0xFFFFFFFFu;
                        sh[v] = s;
                    }
                }
                gpu_->unmap_buffer(buf);
                morphBuf = buf;
            } else {
                incomplete_ = true;
                morphJobs.clear();
            }
        }
    }
    auto morph_for = [&](usize ii, usize n, usize k) -> const MorphJob* {
        for (const MorphJob& j : morphJobs) if (j.inst == ii && j.node == n && j.prim == k) return &j;
        return nullptr;
    };


    // --- Sombra da luz principal ------------------------------------------------
    // Ortográfica ao longo da primeira direcional com sombra, ajustada à caixa
    // dos PROJETORES no espaço da luz: vista da luz, a sombra de um objeto cai
    // dentro da pegada dele — receptor fora dela nunca está na sombra, então o
    // mapa inteiro serve aos texels que importam (antes: esfera da cena ×1,25).
    // Estável: o lado é quantizado em degraus de 2^(1/4) e o centro preso à
    // grade de texels do espaço da luz — o objeto que anda não faz a sombra
    // dos outros "nadar", e a câmera não entra no ajuste. As caixas são as da
    // pose ATUAL: nó animado (matriz do nó), morph (caixa deformada) e skin
    // (união das juntas × caixa da primitiva, conservadora e barata).
    struct ShadowDraw {
        const GpuModel* model;
        const GpuPrimitive* prim;
        MeshPush push;   // model = luz ← local; normalCol[0].x = início das juntas
        PipelineHandle pipeline;
        bool skinned;
        bool morph;
        usize morphPos;
        u32 instanceCount;
    };
    std::vector<ShadowDraw> shadowDraws;
    Mat4 shadowMatrix = Mat4::identity();
    i32 shadowLight = -1;
    for (u32 i = 0; i < frame.lights.size() && i < 4; ++i) {
        if (frame.lights[i].castShadows && frame.lights[i].kind == LightKindGpu::Directional) {
            shadowLight = static_cast<i32>(i);
            break;
        }
    }
    // Nível (HeavyQuality → set_quality): amostras do PCF e da busca de
    // bloqueadores do PCSS. ShadowSettings da composição só sobe o piso.
    static constexpr u32 kPcfTaps[4] = {8, 16, 24, 32};
    static constexpr u32 kBlockerTaps[4] = {0, 0, 12, 16};
    const u32 shadowTier = std::min(shadowFilter_, 3u);
    const u32 pcfTaps = std::clamp(std::max(kPcfTaps[shadowTier], frame.shadow.pcfSamples), 1u, 32u);
    const u32 blockerTaps = kBlockerTaps[shadowTier] ? kBlockerTaps[shadowTier] : (frame.shadow.softShadows ? 8u : 0u);
    // O export pede 4096; a GPU manda no teto (maxImageDimension2D) — um
    // mapa maior que ela não é criado e o quadro do export sairia sem 3D.
    const u32 mapCap = std::clamp(gpu_->capabilities().maxTexture2D, 256u, 4096u);
    const u32 mapSize = std::clamp(std::max(shadowSize_, std::min(frame.shadow.mapResolution, 4096u)), 256u, mapCap);
    Vec4 shadowParams2{};
    f32 shadowBiasDepth = 0.0f;
    if (shadowLight >= 0) {
        const SceneLight& sl = frame.lights[static_cast<usize>(shadowLight)];
        const Vec3 fwd = sl.direction.normalized();
        const Vec3 upRef = std::fabs(fwd.y) > 0.95f ? Vec3{1, 0, 0} : Vec3{0, -1, 0};
        const Vec3 right = upRef.cross(fwd).normalized();
        const Vec3 up = fwd.cross(right);
        // Luz ← mundo, só rotação: a grade de texels vive neste espaço.
        Mat4 lightRot;
        lightRot.col[0] = Vec4{right.x, up.x, fwd.x, 0};
        lightRot.col[1] = Vec4{right.y, up.y, fwd.y, 0};
        lightRot.col[2] = Vec4{right.z, up.z, fwd.z, 0};
        lightRot.col[3] = Vec4{0, 0, 0, 1};
        Aabb ls;   // projetores, no espaço da luz
        for (usize instIndex = 0; instIndex < frame.instances.size(); ++instIndex) {
            const SceneInstance& inst = frame.instances[instIndex];
            if (!inst.asset || !inst.castShadows) continue;
            const GpuModel* gm = model(inst.assetKey, *inst.asset);
            if (!gm) { incomplete_ = true; continue; }
            const std::vector<Node>& nodes = inst.asset->nodes;
            for (usize n = 0; n < nodes.size(); ++n) {
                const i32 mi = nodes[n].mesh;
                if (mi < 0 || mi >= static_cast<i32>(gm->meshes.size())) continue;
                if (n < inst.pose().nodeOpacity.size() && inst.pose().nodeOpacity[n] < 0.5f) continue;   // letra sumindo não projeta
                const i32 skinIndex = nodes[n].skin;
                const bool skinnedNode = joints.valid() && skinIndex >= 0
                                       && skinIndex < static_cast<i32>(inst.pose().skinJointOffset.size()) && gm->skin.valid();
                const Mat4 world = skinnedNode ? inst.world
                                               : inst.world * (n < inst.pose().nodeWorld.size() ? inst.pose().nodeWorld[n] : Mat4::identity());
                Aabb skinBox;   // primitivas com skin deste nó (espaço de bind)
                for (usize primIndex = 0; primIndex < gm->meshes[mi].size(); ++primIndex) {
                    const GpuPrimitive& p = gm->meshes[mi][primIndex];
                    const MorphJob* mj = morph_for(instIndex, n, primIndex);
                    const auto& primitive = inst.asset->meshes[mi].primitives[primIndex];
                    const GpuMaterial* mat = material_for(inst, *gm, nodes[n].material_for(primitive));
                    if (mat->factors.alphaMode == AlphaMode::Blend) continue;   // transparente não projeta
                    const bool sk = skinnedNode && p.skinned;
                    auto pipe = shaders_->pipeline(shadow_key(sk));
                    if (!pipe.ok()) { incomplete_ = true; continue; }
                    ShadowDraw sd{};
                    sd.model = gm;
                    sd.prim = &p;
                    sd.push.model = world;   // × luz depois do ajuste
                    sd.push.normalCol[0].x = sk ? static_cast<f32>(instJointBase[instIndex]
                                                                    + inst.pose().skinJointOffset[static_cast<usize>(skinIndex)]) : 0.0f;
                    sd.pipeline = *pipe;
                    sd.skinned = sk;
                    sd.morph = mj != nullptr;
                    sd.morphPos = mj ? mj->posOffset : 0;
                    sd.instanceCount = 1;
                    shadowDraws.push_back(sd);
                    const Aabb& box = mj && mj->bounds.valid() ? mj->bounds : p.bounds;
                    if (sk) skinBox.add(box);
                    else if (box.valid()) ls.add(box.transformed(lightRot * world));
                }
                if (skinBox.valid()) {
                    const usize first = inst.pose().skinJointOffset[static_cast<usize>(skinIndex)];
                    const usize last = static_cast<usize>(skinIndex) + 1 < inst.pose().skinJointOffset.size()
                                     ? inst.pose().skinJointOffset[static_cast<usize>(skinIndex) + 1] : inst.pose().jointMatrices.size();
                    const Mat4 lw = lightRot * inst.world;
                    for (usize j = first; j < last && j < inst.pose().jointMatrices.size(); ++j)
                        ls.add(skinBox.transformed(lw * inst.pose().jointMatrices[j]));
                }
            }
        }
        if (ls.valid() && !shadowDraws.empty()) {
            // Folga: o raio máximo da penumbra (2·tan do raio angular da luz,
            // até 0,12 do mapa — common/shadow.glsl) dos dois lados + 4%;
            // depois o degrau de 2^(1/4).
            const f32 extent = std::max(ls.max.x - ls.min.x, ls.max.y - ls.min.y);
            const f32 maxPenumbra = std::min(2.0f * std::max(sl.sourceRadius, 0.0f), 0.12f);
            f32 size = std::max(extent * 1.04f / (1.0f - 2.0f * maxPenumbra), 1.0f);
            size = std::exp2(std::ceil(std::log2(size) * 4.0f) * 0.25f);
            const f32 texelWorld = size / static_cast<f32>(mapSize);
            const f32 cx = std::round((ls.min.x + ls.max.x) * 0.5f / texelWorld) * texelWorld;
            const f32 cy = std::round((ls.min.y + ls.max.y) * 0.5f / texelWorld) * texelWorld;
            // Profundidade: só os projetores (o receptor atrás do último fica
            // em 1 no shader e continua recebendo). Mesmo degrau na faixa.
            const f32 zPad = std::max((ls.max.z - ls.min.z) * 0.05f, 4.0f * texelWorld);
            f32 zSpan = std::max((ls.max.z - ls.min.z + 2.0f * zPad) * 1.02f, 1.0f);
            zSpan = std::exp2(std::ceil(std::log2(zSpan) * 4.0f) * 0.25f);
            const f32 zStep = zSpan / 64.0f;
            const f32 z0 = std::floor((ls.min.z - zPad) / zStep) * zStep;
            Mat4 ortho;
            ortho.col[0] = Vec4{2.0f / size, 0, 0, 0};
            ortho.col[1] = Vec4{0, 2.0f / size, 0, 0};
            ortho.col[2] = Vec4{0, 0, 1.0f / zSpan, 0};
            ortho.col[3] = Vec4{-cx * 2.0f / size, -cy * 2.0f / size, -z0 / zSpan, 1};
            const Mat4 lightViewProj = ortho * lightRot;
            // NDC → uv do mapa (o Y do Vulkan já desce com o v da textura).
            Mat4 toUv;
            toUv.col[0] = Vec4{0.5f, 0, 0, 0};
            toUv.col[1] = Vec4{0, 0.5f, 0, 0};
            toUv.col[3] = Vec4{0.5f, 0.5f, 0, 1};
            shadowMatrix = toUv * lightViewProj;
            for (ShadowDraw& d : shadowDraws) d.push.model = lightViewProj * d.push.model;
            // Unidades do shader (common/shadow.glsl): viés em profundidade,
            // normal offset em px, k = tan·R/S (raio uv por profundidade), S/R.
            shadowBiasDepth = std::max(sl.shadowBias, 0.0f) / 0.001f * 0.5f * texelWorld / zSpan;
            const f32 normalOffset = std::max(frame.shadow.normalBias, 0.0f) / 0.02f * 1.5f * texelWorld;
            shadowParams2 = Vec4{std::max(sl.sourceRadius, 0.0f) * zSpan / size, normalOffset, size / zSpan,
                                 static_cast<f32>(blockerTaps)};
        } else {
            shadowDraws.clear();
        }
    }
    const bool shadowsOn = !shadowDraws.empty();
    header.shadowMatrix = shadowMatrix;
    header.shadowParams = Vec4{shadowsOn ? static_cast<f32>(pcfTaps) : 0.0f, 1.0f / static_cast<f32>(mapSize),
                               shadowBiasDepth, static_cast<f32>(shadowLight)};
    header.shadowParams2 = shadowParams2;
    // Sem sombra o mapa é 1×1 limpo: o slot de comparação do PBR precisa de uma
    // textura de PROFUNDIDADE (Metal: depth2d; Vulkan: formato com comparação).
    FGTexture shadowTex{};
    {
        TextureDesc sd;
        sd.width = sd.height = shadowsOn ? mapSize : 1u;
        sd.format = SurfaceFormat::Depth32F;
        sd.sampled = true;
        sd.renderTarget = true;
        shadowTex = graph.create_texture("3d-sombra", sd);
        if (shadowsOn) stats_.shadowMapSize = std::max(stats_.shadowMapSize, mapSize);
    }


    for (usize instIndex = 0; instIndex < frame.instances.size(); ++instIndex) {
        const SceneInstance& inst = frame.instances[instIndex];
        if (!inst.asset) continue;
        const GpuModel* gm = model(inst.assetKey, *inst.asset);
        if (auto it = models_.find(inst.assetKey); it != models_.end()) it->second.lastFrame = frameNumber;
        if (!gm) { incomplete_ = true; continue; }
        const std::vector<Node>& nodes = inst.asset->nodes;
        std::unordered_map<const SceneBlock*, const SceneBlock*> sampleBlocks;
        for (usize n = 0; n < nodes.size(); ++n) {
            const i32 mi = nodes[n].mesh;
            if (mi < 0 || mi >= static_cast<i32>(gm->meshes.size())) continue;
            const f32 nodeAlpha = n < inst.pose().nodeOpacity.size() ? inst.pose().nodeOpacity[n] : 1.0f;
            if (nodeAlpha <= 0.004f) continue;   // letra ainda invisível: nada a desenhar
            // Malha com skin: a pose vem das juntas (já no espaço da cena do
            // modelo); o nó da malha não entra (regra do glTF).
            const i32 skinIndex = nodes[n].skin;
            const bool skinnedNode = joints.valid() && skinIndex >= 0
                                   && skinIndex < static_cast<i32>(inst.pose().skinJointOffset.size())
                                   && gm->skin.valid();
            const Mat4 world = skinnedNode ? inst.world
                                           : inst.world * (n < inst.pose().nodeWorld.size() ? inst.pose().nodeWorld[n] : Mat4::identity());
            const Mat4 clipFromLocal = (inst.cameraOverride ? inst.sampleViewProj : viewProj) * world;
            const Mat4 viewFromLocal = (inst.cameraOverride ? inst.sampleView : frame.camera.view) * world;
            const Vec3 worldX{world.col[0].x, world.col[0].y, world.col[0].z};
            const Vec3 worldY{world.col[1].x, world.col[1].y, world.col[1].z};
            const Vec3 worldZ{world.col[2].x, world.col[2].y, world.col[2].z};
            const bool mirrored = worldX.dot(worldY.cross(worldZ)) < 0.0f;
            for (usize primIndex = 0; primIndex < gm->meshes[mi].size(); ++primIndex) {
                const GpuPrimitive& p = gm->meshes[mi][primIndex];
                const MorphJob* mj = morph_for(instIndex, n, primIndex);
                // Com skin (ou morph) a caixa de repouso não vale para a pose: sem recorte.
                if (!(skinnedNode && p.skinned) && !mj && outside_frustum(clipFromLocal, p.bounds, frame.camera.nearZ)) {
                    ++stats_.culledPrimitives;
                    continue;
                }
                const auto& primitive = inst.asset->meshes[mi].primitives[primIndex];
                const GpuMaterial* mat = faded(material_for(inst, *gm, nodes[n].material_for(primitive)), nodeAlpha);
                if (n < inst.pose().nodeFill.size()) mat = tinted(mat, inst.pose().nodeFill[n]);
                const bool skinDraw = skinnedNode && p.skinned;
                // Vidro (KHR_materials_transmission) mistura como transparente:
                // o que está atrás aparece e o reflexo continua inteiro.
                const AlphaMode drawMode = mat->factors.transmission > 0.0f ? AlphaMode::Blend : mat->factors.alphaMode;
                PipelineKey key = key_for(drawMode, mat->factors.doubleSided, skinDraw);
                // A reflected node/parent reverses winding without changing
                // which authored surface is its front face.
                if (mirrored && !mat->factors.doubleSided) key.frontFaceCCW = false;
                auto pipe = shaders_->pipeline(key);
                if (!pipe.ok()) { incomplete_ = true; continue; }
                const EnvSlot* env = env_of(inst);
                const SceneBlock* blk = block_for(mat, env);
                if (!blk) { incomplete_ = true; continue; }
                if (inst.cameraOverride) {
                    const auto found = sampleBlocks.find(blk);
                    if (found != sampleBlocks.end()) blk = found->second;
                    else {
                        auto* sampled = arena.alloc_array<SceneBlock>(1);
                        if (!sampled) { incomplete_ = true; continue; }
                        *sampled = *blk;
                        sampled->viewProj = inst.sampleViewProj;
                        sampled->cameraPos = Vec4{inst.sampleCameraPosition, blk->cameraPos.w};
                        sampleBlocks.emplace(blk, sampled);
                        blk = sampled;
                    }
                }
                Draw d{};
                d.model = gm;
                d.prim = &p;
                d.material = mat;
                d.block = blk;
                d.env = env;
                d.push.model = world;
                normal_matrix(world, d.push.normalCol);
                d.skinned = skinDraw;
                d.morph = mj != nullptr;
                if (mj) {
                    d.morphPos = mj->posOffset;
                    d.morphShade = mj->shadeOffset;
                }
                if (skinDraw) {
                    d.push.normalCol[0].w = static_cast<f32>(instJointBase[instIndex]
                                                             + inst.pose().skinJointOffset[static_cast<usize>(skinIndex)]);
                }
                d.pipeline = *pipe;
                d.instanceCount = 1;
                if (skinDraw || d.morph) { d.firstIndex = p.firstIndex; d.indexCount = p.indexCount; }
                else {
                    const u64 lodKey = (inst.layerKey * 0x9E3779B97F4A7C15ull) ^ (static_cast<u64>(n) << 20) ^ static_cast<u64>(primIndex)
                                     ^ (static_cast<u64>(instIndex) << 44);
                    pick_lod(p, world, viewFromLocal, lodKey, d.firstIndex, d.indexCount);
                }
                d.viewDepth = viewFromLocal.transform_point(p.bounds.center()).z;
                d.sortKey = static_cast<u32>(pipe->id & 0xFFFF) << 16 | static_cast<u32>(reinterpret_cast<uintptr_t>(mat) >> 4 & 0xFFFF);
                (drawMode == AlphaMode::Blend ? blended : opaque).push_back(d);
                ++stats_.visiblePrimitives;
                stats_.triangles += d.indexCount / 3;
            }
        }
    }
    // Opacos agrupados por pipeline/material (menos trocas de estado); os
    // transparentes do mais longe para o mais perto (ordem correta do blend).
    std::sort(opaque.begin(), opaque.end(), [](const Draw& a, const Draw& b) { return a.sortKey < b.sortKey; });
    std::sort(blended.begin(), blended.end(), [](const Draw& a, const Draw& b) { return a.viewDepth > b.viewDepth; });

    // --- Instancing (8E) ------------------------------------------------------
    // Objetos iguais (mesma malha, primitiva, LOD, material e pipeline; sem
    // skin nem morph) viram UM desenho com N instâncias. As matrizes vão num
    // SSBO do quadro: duas mat4 por instância na cor (mundo + normais), uma na
    // sombra (luz ← local). Transparentes ficam fora: a ordem do blend é por
    // profundidade, desenho a desenho. O recorte por frustum já aconteceu —
    // instância fora da tela nem entra no grupo.
    std::vector<Mat4> instData;
    {
        auto mix = [](u64 h, u64 v) { return (h ^ v) * 0x100000001B3ull; };
        std::unordered_map<u64, u32> groupOf;
        std::vector<std::vector<u32>> members;
        std::vector<Draw> merged;
        merged.reserve(opaque.size());
        for (u32 i = 0; i < opaque.size(); ++i) {
            const Draw& d = opaque[i];
            if (d.skinned || d.morph || !instancing_) { merged.push_back(d); members.emplace_back(); continue; }
            u64 h = 0xCBF29CE484222325ull;
            h = mix(h, reinterpret_cast<uintptr_t>(d.model));
            h = mix(h, reinterpret_cast<uintptr_t>(d.prim));
            h = mix(h, reinterpret_cast<uintptr_t>(d.material));
            h = mix(h, reinterpret_cast<uintptr_t>(d.block));
            h = mix(h, d.pipeline.id);
            h = mix(h, (static_cast<u64>(d.firstIndex) << 32) | d.indexCount);
            auto [it, fresh] = groupOf.try_emplace(h, static_cast<u32>(merged.size()));
            if (fresh) { merged.push_back(d); members.emplace_back(1, i); }
            else members[it->second].push_back(i);
        }
        for (u32 g = 0; g < merged.size(); ++g) {
            if (members[g].size() < 2) continue;
            const u32 base = static_cast<u32>(instData.size());
            for (u32 idx : members[g]) {
                const Draw& m = opaque[idx];
                instData.push_back(m.push.model);
                Mat4 nm;
                nm.col[0] = Vec4{m.push.normalCol[0].x, m.push.normalCol[0].y, m.push.normalCol[0].z, 0};
                nm.col[1] = Vec4{m.push.normalCol[1].x, m.push.normalCol[1].y, m.push.normalCol[1].z, 0};
                nm.col[2] = Vec4{m.push.normalCol[2].x, m.push.normalCol[2].y, m.push.normalCol[2].z, 0};
                nm.col[3] = Vec4{0, 0, 0, 0};
                instData.push_back(nm);
            }
            merged[g].instanceCount = static_cast<u32>(members[g].size());
            merged[g].push.normalCol[0].w = static_cast<f32>(base + 1u);
            ++stats_.instancedDraws;
        }
        opaque.swap(merged);

        // Sombra: o mesmo, por (malha, primitiva, pipeline).
        std::unordered_map<u64, u32> sgroupOf;
        std::vector<std::vector<u32>> smembers;
        std::vector<ShadowDraw> smerged;
        for (u32 i = 0; i < shadowDraws.size(); ++i) {
            const ShadowDraw& d = shadowDraws[i];
            if (d.skinned || d.morph || !instancing_) { smerged.push_back(d); smembers.emplace_back(); continue; }
            u64 h = 0xCBF29CE484222325ull;
            h = mix(h, reinterpret_cast<uintptr_t>(d.model));
            h = mix(h, reinterpret_cast<uintptr_t>(d.prim));
            h = mix(h, d.pipeline.id);
            auto [it, fresh] = sgroupOf.try_emplace(h, static_cast<u32>(smerged.size()));
            if (fresh) { smerged.push_back(d); smembers.emplace_back(1, i); }
            else smembers[it->second].push_back(i);
        }
        for (u32 g = 0; g < smerged.size(); ++g) {
            if (smembers[g].size() < 2) continue;
            const u32 base = static_cast<u32>(instData.size());
            for (u32 idx : smembers[g]) instData.push_back(shadowDraws[idx].push.model);
            smerged[g].instanceCount = static_cast<u32>(smembers[g].size());
            smerged[g].push.normalCol[0].y = static_cast<f32>(base + 1u);
        }
        shadowDraws.swap(smerged);
    }
    BufferHandle instBuf{};
    if (!instData.empty()) {
        const BufferHandle buf = take_upload(instPool_, instData.size() * sizeof(Mat4));
        void* ptr = nullptr;
        if (buf.valid() && gpu_->map_buffer(buf, ptr).ok() && ptr) {
            std::copy(instData.begin(), instData.end(), static_cast<Mat4*>(ptr));
            gpu_->unmap_buffer(buf);
            instBuf = buf;
        }
        // Sem o buffer (memória de GPU): o grupo não desenha — melhor faltar
        // um quadro do que desenhar todas as instâncias no lugar da primeira.
        if (!instBuf.valid()) return false;
    }

    {
        // Sem sombra: o passe só limpa o mapa 1×1 (nenhum desenho).
        const u32 n = static_cast<u32>(shadowDraws.size());
        ShadowDraw* sl = n ? arena.alloc_array<ShadowDraw>(n) : nullptr;
        if (n && !sl) return false;
        if (n) std::copy(shadowDraws.begin(), shadowDraws.end(), sl);
        stats_.shadowDrawCalls += n;
        struct SCap {
            ShadowDraw* draws;
            u32 count;
            BufferHandle joints;
            BufferHandle inst;
            BufferHandle morph;
        } scap{sl, n, joints, instBuf, morphBuf};
        graph.add_raster_pass_depth("3d-sombra", PassStage::Scene3D, FGTexture{}, LoadOp::DontCare, Vec4{}, shadowTex,
                                    LoadOp::Clear, true, 1.0f, [scap](PassContext& pc) {
            CommandList& c = pc.cmds;
            PipelineHandle bound{};
            for (u32 i = 0; i < scap.count; ++i) {
                const ShadowDraw& d = scap.draws[i];
                if (!(d.pipeline == bound)) {
                    c.bind_pipeline(d.pipeline);
                    bound = d.pipeline;
                }
                if (d.skinned) c.bind_storage_buffer(scap.joints);
                else if (d.instanceCount > 1) c.bind_storage_buffer(scap.inst);
                c.bind_vertex_buffer(0, d.morph ? scap.morph : d.model->positions, d.morph ? d.morphPos : 0);
                if (d.skinned) c.bind_vertex_buffer(2, d.model->skin, d.morph ? static_cast<u64>(d.prim->vertexOffset) * sizeof(SkinVertex) : 0);
                c.bind_index_buffer(d.model->indices, 0, d.model->indexType);
                c.push_constants(&d.push, sizeof(MeshPush));
                c.draw_indexed(d.prim->indexCount, std::max(1u, d.instanceCount), d.prim->firstIndex, d.morph ? 0 : d.prim->vertexOffset, 0);
            }
        });
    }

    const u32 total = static_cast<u32>(opaque.size() + blended.size());
    Draw* list = total ? arena.alloc_array<Draw>(total) : nullptr;
    if (total && !list) return false;
    // Planos (camadas 2D na cena): depois dos opacos, do mais longe para o
    // mais perto, antes dos modelos transparentes.
    u32 planeCount = 0;
    ScenePlane* planeList = nullptr;
    PipelineHandle planePipe{}, planeBlendPipe{};
    if (planes && !planes->empty()) {
        auto pp = shaders_->pipeline(plane_key());
        auto blend = shaders_->pipeline(plane_key(true));
        if (!pp.ok() || !blend.ok()) incomplete_ = true;
        if (pp.ok() && blend.ok()) {
            planePipe = *pp;
            planeBlendPipe = *blend;
            planeCount = static_cast<u32>(planes->size());
            planeList = arena.alloc_array<ScenePlane>(planeCount);
            if (!planeList) return false;
            for (u32 i = 0; i < planeCount; ++i) {
                planeList[i] = (*planes)[i];
                // A face vista é a iluminada: a câmera DESTE (sub)quadro.
                PlaneLightBlock& lb = planeList[i].light;
                lb.cameraPos = Vec4{frame.camera.position.x, frame.camera.position.y, frame.camera.position.z, lb.cameraPos.w};
            }
            std::sort(planeList, planeList + planeCount, [](const ScenePlane& a, const ScenePlane& b) { return a.viewDepth > b.viewDepth; });
        }
    }
    // Partículas 3D (8.2): por último, com o depth de tudo o que é sólido.
    SceneParticleDraw* partList = nullptr;
    if (particles && particleCount) {
        partList = arena.alloc_array<SceneParticleDraw>(particleCount);
        if (!partList) return false;
        for (u32 i = 0; i < particleCount; ++i) {
            partList[i] = particles[i];
            // O pipeline da partícula tem de casar com o passe: amostras e MRT.
            PipelineKey k = particles[i].key;
            if (k.fragment == ShaderId::Count) continue;
            k.sampleCount = static_cast<u8>(passSamples_);
            k.hasColor1 = passMrt_;
            if (auto pp = shaders_->pipeline(k); pp.ok()) partList[i].pipeline = *pp;
            else { incomplete_ = true; return false; }
        }
    }
    const u32 opaqueCount = static_cast<u32>(opaque.size());
    for (usize i = 0; i < opaque.size(); ++i) list[i] = opaque[i];
    for (usize i = 0; i < blended.size(); ++i) list[opaque.size() + i] = blended[i];
    stats_.drawCalls += total;

    struct Cap {
        Draw* draws;
        u32 count;
        TextureHandle white, flatNormal, irradiance, prefiltered, brdf;
        SamplerHandle cubeSampler;
        u64 linearSampler;
        u64 clampSampler;
        BufferHandle joints;
        FGTexture shadow;
        u64 nearestSampler;
        u64 compareSampler;
        BufferHandle morph;
        BufferHandle inst;
        ScenePlane* planes;
        u32 planeCount;
        u32 opaqueCount;
        PipelineHandle planePipe, planeBlendPipe;
        SceneParticleDraw* parts;
        u32 partCount;
        EnvSlot sceneEnv;             ///< o ambiente do grupo (d.env nulo)
    } cap{list, total, white_, flatNormal_, ibl ? irradiance_ : envCube_, ibl ? prefiltered_ : envCube_,
          ibl ? iblLut_ : brdfLut_, cubeSampler_, shaders_->sampler(CommonSampler::LinearRepeat).id,
          shaders_->sampler(CommonSampler::LinearClamp).id, joints, shadowTex,
          shaders_->sampler(CommonSampler::NearestClamp).id, shaders_->sampler(CommonSampler::ShadowCompare).id, morphBuf, instBuf, planeList, planeCount, opaqueCount, planePipe, planeBlendPipe,
          partList, partList ? particleCount : 0u, envSlots[0]};

    // ===== CHÃO DO GRUPO 3D (Ground.cpp) =========================================
    // Reflexo planar (os opacos espelhados, 1 amostra) e sombra de contato
    // (profundidade vista de baixo) em passes próprios ANTES do principal; o
    // plano é desenhado no principal depois dos opacos (`draw_ground`).
    GroundDraw groundDraw{};
    if (frame.floor.mode != 0 && passMrt_) {
        GroundInputs gi;
        gi.frame = &frame;
        gi.header = &header;
        gi.viewProj = viewProj;
        gi.width = width;
        gi.height = height;
        gi.samples = passSamples_;
        gi.exportQuality = !lodHysteresis_;       // export/captura: sem histerese de LOD
        gi.lowTier = frame.post.quality == 1u;    // BAIXO: sem reflexo planar
        gi.white = white_;
        gi.black = black_;
        gi.mesh.white = white_;
        gi.mesh.flatNormal = flatNormal_;
        gi.mesh.irradiance = cap.irradiance;
        gi.mesh.prefiltered = cap.prefiltered;
        gi.mesh.brdf = cap.brdf;
        gi.mesh.cubeSampler = cubeSampler_;
        gi.mesh.linearSampler = cap.linearSampler;
        gi.mesh.clampSampler = cap.clampSampler;
        gi.mesh.nearestSampler = cap.nearestSampler;
        gi.mesh.compareSampler = cap.compareSampler;
        gi.mesh.shadow = shadowTex;
        gi.mesh.joints = joints;
        gi.mesh.morph = morphBuf;
        gi.mesh.inst = instBuf;
        groundDraw = build_ground(graph, arena, *shaders_, gi, list, opaqueCount, [this](const Draw& d) {
            const GpuMaterial& m = *d.material;
            PipelineKey k = key_for(m.factors.alphaMode == AlphaMode::Mask ? AlphaMode::Mask : AlphaMode::Opaque,
                                    m.factors.doubleSided, d.skinned);
            const Mat4& w = d.push.model;
            const Vec3 x{w.col[0].x, w.col[0].y, w.col[0].z}, y{w.col[1].x, w.col[1].y, w.col[1].z},
                       z{w.col[2].x, w.col[2].y, w.col[2].z};
            if (x.dot(y.cross(z)) < 0.0f && !m.factors.doubleSided) k.frontFaceCCW = false;
            return k;
        });
    }
    // ===== fim do chão ==========================================================
    PipelineHandle skyPipe{};
    const bool skyReady = frame.environment.showBackground && background_.valid() && envKey_ == key_of(frame.environment);
    if (frame.environment.showBackground && !skyReady) incomplete_ = true;
    if (skyReady) {
        auto key = PipelineKey::graphics(ShaderId::common_fullscreen_vert, ShaderId::scene3d_environment_frag, SurfaceFormat::RGBA16F);
        key.hasDepth = true; key.depthFormat = SurfaceFormat::Depth32F;
        key.sampleCount = static_cast<u8>(passSamples_);
        key.hasColor1 = passMrt_;
        auto pipe = shaders_->pipeline(key); if (pipe.ok()) skyPipe = *pipe;
        else incomplete_ = true;
    }
    struct SkyBlock { Mat4 view; Vec4 projection; Vec4 light; Vec4 post; };
    // Fundo: LOD pela pegada do pixel (textureGrad no shader) × o desfoque;
    // desfoque forte mistura o especular pré-filtrado (liso, GGX).
    const BackgroundSampling bgs = background_sampling(frame.environment.backgroundBlur, frame.camera.fovY, height,
                                                       backgroundSize_, prefilteredMips_);
    const SkyBlock sky{frame.camera.view, Vec4{aspect, std::tan(frame.camera.fovY*.5f), bgs.gradScale, bgs.specLod},
        // Exposição do grupo = 1 aqui: ela é aplicada no pós, junto dos modelos.
        Vec4{frame.environment.rotation, frame.environment.intensity, 1.0f, bgs.specBlend}, Vec4{}};
    const TextureHandle skyTexture = background_;
    // Grupo SÓ de fundo (o panorama no fundo da pilha, sem modelo, plano,
    // partícula nem chão): um passe de tela cheia de 1 amostra, sem
    // profundidade e sem pós — o tone map vai no próprio shader do céu. Custa
    // um passe, não um passe 3D com MSAA + a cadeia do pós.
    if (frame.instances.empty() && planeCount == 0 && !partList && !groundDraw.valid() && frame.environment.showBackground) {
        PipelineHandle direct{};
        if (skyReady) {
            auto key = PipelineKey::graphics(ShaderId::common_fullscreen_vert, ShaderId::scene3d_environment_frag,
                                             SurfaceFormat::RGBA16F);
            if (auto pipe = shaders_->pipeline(key); pipe.ok()) direct = *pipe;
            else incomplete_ = true;
        }
        SkyBlock skyDirect = sky;
        skyDirect.post = Vec4{std::clamp(frame.environment.exposure, 0.01f, 64.0f),
                              frame.post.toneMapper >= 1 ? 1.0f : 0.0f, 1.0f, 0.0f};
        struct SkyCap { PipelineHandle p; TextureHandle panorama, blurred; SamplerHandle sampler; SkyBlock u; }
            skyCap{direct, skyTexture, cap.prefiltered, cubeSampler_, skyDirect};
        graph.add_raster_pass("3d-fundo", PassStage::Scene3D, color, LoadOp::Clear, Vec4{0, 0, 0, 0},
                              [skyCap](PassContext& pc) {
            if (!skyCap.p.valid()) return;   // ambiente ainda gerando: transparente até ficar pronto
            pc.cmds.bind_pipeline(skyCap.p);
            pc.cmds.set_uniforms(&skyCap.u, sizeof(skyCap.u));
            pc.cmds.bind_texture(0, skyCap.panorama, skyCap.sampler);
            pc.cmds.bind_texture(1, skyCap.blurred, skyCap.sampler);
            pc.cmds.draw(3);
        });
        outColor = color;
        return true;
    }
    // Z reverso: limpa a profundidade com 0 (o infinito).
    // Com MSAA desenha nos multiamostrados (transitórios) e resolve no fim;
    // sem, direto nos de 1 amostra.
    const u32 pbrPass = graph.add_raster_pass_depth("3d-pbr", PassStage::Scene3D, msaa ? colorMs : color, LoadOp::Clear,
                                Vec4{0, 0, 0, 0}, depth, LoadOp::Clear, depthOut && !msaa, 0.0f,
                                [cap, overriddenMaterials, skyPipe, skyTexture, sky, groundDraw](PassContext& pc) {
        CommandList& c = pc.cmds;
        PipelineHandle bound{};
        const GpuModel* boundModel = nullptr;
        const GpuMaterial* boundMat = nullptr;
        const EnvSlot* boundEnv = nullptr;
        if (skyPipe.valid()) {
            c.bind_pipeline(skyPipe); c.set_uniforms(&sky, sizeof(sky));
            c.bind_texture(0, skyTexture, cap.cubeSampler);
            c.bind_texture(1, cap.prefiltered, cap.cubeSampler);   // desfoque forte do fundo
            c.draw(3);
        }
        auto drawPlanes = [&]() {
            if (!cap.planeCount) return;
            // Establish solid depth for every plane before blending any edges/fades.
            // A tilted translucent pixel must blend over the already drawn background,
            // even when the plane centers sort in the opposite depth order.
            for (u32 phase = 0; phase < 2; ++phase) {
                c.bind_pipeline(phase ? cap.planeBlendPipe : cap.planePipe);
                for (u32 k = 0; k < cap.planeCount; ++k) {
                    const ScenePlane& p = cap.planes[k];
                    c.bind_texture(0, pc.texture(p.texture), SamplerHandle{p.sampler});
                    c.set_uniforms(&p.light, sizeof(p.light));   // luzes da composição (plane.frag)
                    struct { Mat4 m; Vec4 region; Vec4 uv; Vec4 params; } push{p.clipFromLayer, p.region, Vec4{0, 0, 1, 1}, Vec4{p.opacity, static_cast<f32>(phase), 0, 0}};
                    c.push_constants(&push, sizeof(push));
                    c.draw(6);
                }
            }
            bound = PipelineHandle{};
            boundMat = nullptr;
            boundModel = nullptr;
        };
        for (u32 i = 0; i < cap.count; ++i) {
            if (i == cap.opaqueCount) {
                if (groundDraw.valid()) { draw_ground(c, pc, groundDraw); bound = PipelineHandle{}; boundMat = nullptr; boundEnv = nullptr; }   // chão
                drawPlanes();
            }
            const Draw& d = cap.draws[i];
            if (!(d.pipeline == bound)) {
                c.bind_pipeline(d.pipeline);
                bound = d.pipeline;
                boundMat = nullptr;
            }
            if (d.material != boundMat || d.env != boundEnv) {
                const TextureHandle fallback[5] = {cap.white, cap.white, cap.flatNormal, cap.white, cap.white};
                for (u32 k = 0; k < 5; ++k) {
                    const bool has = d.material->tex[k].valid();
                    c.bind_texture(k, has ? d.material->tex[k] : fallback[k],
                                   has && d.material->samp[k].valid() ? d.material->samp[k] : SamplerHandle{cap.linearSampler});
                }
                // O ambiente é o DESTE objeto (v22): sem ambiente próprio é o do
                // grupo, como sempre foi.
                const EnvSlot* e = d.env ? d.env : &cap.sceneEnv;
                c.bind_texture(5, e->irradiance, cap.cubeSampler);
                c.bind_texture(6, e->prefiltered, cap.cubeSampler);
                c.bind_texture(7, e->brdf, SamplerHandle{cap.clampSampler});
                // 8 = comparação do hardware (PCF); 11 = o mesmo mapa cru (busca do PCSS).
                c.bind_texture(8, pc.texture(cap.shadow), SamplerHandle{cap.compareSampler});
                c.bind_texture(11, pc.texture(cap.shadow), SamplerHandle{cap.nearestSampler});
                boundMat = d.material;
                boundEnv = d.env;
            }
            if (d.skinned) c.bind_storage_buffer(cap.joints);
            if (d.morph) {
                // Vértices deformados deste quadro: só os desta primitiva, a
                // partir do 0 (a skin continua vindo da malha, deslocada).
                c.bind_vertex_buffer(0, cap.morph, d.morphPos);
                c.bind_vertex_buffer(1, cap.morph, d.morphShade);
                if (d.model->skin.valid()) {
                    c.bind_vertex_buffer(2, d.model->skin, static_cast<u64>(d.prim->vertexOffset) * sizeof(SkinVertex));
                }
                c.bind_index_buffer(d.model->indices, 0, d.model->indexType);
                boundModel = nullptr;   // o próximo desenho normal re-liga a malha
                c.set_uniforms(d.block, sizeof(SceneBlock));
                c.push_constants(&d.push, sizeof(MeshPush));
                c.draw_indexed(d.indexCount, 1, d.firstIndex, 0, 0);
                continue;
            }
            if (d.model != boundModel || d.skinned) {
                c.bind_vertex_buffer(0, d.model->positions, 0);
                c.bind_vertex_buffer(1, d.model->shading, 0);
                if (d.model->skin.valid()) c.bind_vertex_buffer(2, d.model->skin, 0);
                c.bind_index_buffer(d.model->indices, 0, d.model->indexType);
                boundModel = d.model;
            }
            if (d.instanceCount > 1) c.bind_storage_buffer(cap.inst);
            c.set_uniforms(d.block, sizeof(SceneBlock));
            c.push_constants(&d.push, sizeof(MeshPush));
            c.draw_indexed(d.indexCount, std::max(1u, d.instanceCount), d.firstIndex, d.prim->vertexOffset, 0);
        }
        if (cap.opaqueCount >= cap.count) {
            draw_ground(c, pc, groundDraw);   // chão (sem transparentes na lista)
            drawPlanes();
        }
        // Partículas: testam o depth de modelos e planos, não escrevem nele.
        for (u32 k = 0; k < cap.partCount; ++k) {
            const SceneParticleDraw& d = cap.parts[k];
            c.bind_pipeline(d.pipeline);
            c.set_uniforms(d.uniforms, d.uniformBytes);
            c.bind_storage_buffer_at(1, d.history);
            c.bind_storage_buffer(d.extras);   // 8.2: inválido = nulo (o shader não lê sem extras.z)
            if (d.texture.valid()) c.bind_texture(0, d.texture, SamplerHandle{d.sampler});
            c.push_constants(d.push, d.pushBytes);
            if (d.meshVertices) {
                c.draw(d.meshVertices, d.instances, 0);
                continue;
            }
            c.bind_index_buffer(d.quad, 0, IndexType::U16);
            c.draw_indexed(6, d.instances, 0, 0, 0);
        }
    });
    if (passMrt_) graph.set_color1(pbrPass, msaa ? sceneMs : sceneHdr);
    if (msaa) graph.set_resolve(pbrPass, color, passMrt_ ? sceneHdr : FGTexture{}, depthResolved);
    const FGTexture depth1x = depthOut ? (msaa ? depthResolved : depth) : FGTexture{};
    if (outDepth) *outDepth = depth1x;
    if (shadowTex.valid()) graph.read(pbrPass, shadowTex);
    ground_reads(graph, pbrPass, groundDraw);   // chão
    for (u32 k = 0; k < planeCount; ++k) graph.read(pbrPass, planeList[k].texture);
    // Memória residente: por modelo (instâncias do mesmo asset dividem malha,
    // texturas e materiais — um GpuModel por asset).
    stats_.geometryBytes = stats_.textureBytes = 0;
    for (const auto& [k, e] : models_) {
        if (!e.model) continue;
        stats_.geometryBytes += e.model->geometryBytes;
        stats_.textureBytes += e.model->textureBytes;
    }
    // --- Pós do grupo --------------------------------------------------------
    // Com luz de cena: exposição → bloom → tone map → + 2D. Sem (só planos e
    // partículas): o alvo de exibição já é a saída, como sempre foi.
    FGTexture display = color, scene = sceneHdr;
    if (dofOn && depth1x.valid() && !build_dof(graph, frame, width, height, display, scene, depth1x)) incomplete_ = true;
    if (outExposureHdr) {
        *outExposureHdr = scene;
        outColor = display;
        return true;
    }
    outColor = finish_exposure(graph, frame, width, height, display, scene);
    return outColor.valid();
}

FGTexture SceneRenderer::finish_exposure(FrameGraph& graph, const SceneFrame& frame, u32 width, u32 height,
                                        FGTexture display, FGTexture scene) noexcept {
    FGTexture result = scene.valid() ? build_post(graph, frame, width, height, display, scene) : display;
    if (!result.valid()) { incomplete_ = true; result = display; }
    // Sem MSAA (GLES, nível BAIXO): FXAA na imagem já em espaço de exibição.
    // FXAA: nível BAIXO, ou o aparelho não tem o MSAA pedido (GLES).
    if (passSamples_ <= 1 && (postFxaa_ || postMsaa_ > 1) && antialias_) {
        const FGTexture aa = build_fxaa(graph, width, height, result);
        if (aa.valid()) result = aa;
        else incomplete_ = true;
    }
    return result;
}

FGTexture SceneRenderer::build_post(FrameGraph& graph, const SceneFrame& frame, u32 width, u32 height,
                                    FGTexture display, FGTexture scene) noexcept {
    const u64 linearClamp = shaders_->sampler(CommonSampler::LinearClamp).id;
    auto tonemapPipe = shaders_->pipeline(PipelineKey::fullscreen(ShaderId::scene3d_post_tonemap_frag, SurfaceFormat::RGBA16F));
    if (!tonemapPipe.ok()) return FGTexture{};
    const f32 exposure = std::clamp(frame.environment.exposure, 0.01f, 64.0f);

    // --- Bloom (Jimenez 2014): descida de 13 amostras a partir de 1/div da
    // resolução (o primeiro nível já decodifica, expõe, aplica o limiar e a
    // média de Karis), subida em tenda somada nível a nível. -----------------
    const ScenePost& post = frame.post;
    const f32 intensity = std::clamp(post.bloomIntensity, 0.0f, 4.0f);
    const bool bloomOn = post.bloom && intensity > 0.0f;
    constexpr u32 kMaxLevels = 8;
    FGTexture levels[kMaxLevels]{};
    u32 lw[kMaxLevels]{}, lh[kMaxLevels]{};
    u32 levelCount = 0;
    if (bloomOn) {
        auto downPipe = shaders_->pipeline(PipelineKey::fullscreen(ShaderId::scene3d_post_bloom_down_frag, SurfaceFormat::RGBA16F));
        auto upPipe = shaders_->pipeline(PipelineKey::graphics(ShaderId::common_fullscreen_vert, ShaderId::scene3d_post_bloom_up_frag,
                                                               SurfaceFormat::RGBA16F, true, BlendMode::Add));
        if (!downPipe.ok() || !upPipe.ok()) incomplete_ = true;
        if (downPipe.ok() && upPipe.ok()) {
            u32 w = std::max(1u, width / bloomDiv_), h = std::max(1u, height / bloomDiv_);
            const u32 want = std::min(bloomLevels_, kMaxLevels);
            // Para quando o menor lado fica abaixo de 4 px: nível que não tem
            // mais o que espalhar só custa um passe.
            while (levelCount < want && std::min(w, h) >= 4) {
                TextureDesc d;
                d.width = w;
                d.height = h;
                d.format = SurfaceFormat::RGBA16F;
                d.sampled = true;
                d.renderTarget = true;
                levels[levelCount] = graph.create_texture("3d-bloom", d);
                lw[levelCount] = w;
                lh[levelCount] = h;
                ++levelCount;
                w = std::max(1u, w / 2);
                h = std::max(1u, h / 2);
            }
            struct DownParams { Vec4 texel; Vec4 cfg; };
            for (u32 i = 0; i < levelCount; ++i) {
                const bool first = i == 0;
                const FGTexture src = first ? scene : levels[i - 1];
                const f32 sw = static_cast<f32>(first ? width : lw[i - 1]);
                const f32 sh = static_cast<f32>(first ? height : lh[i - 1]);
                const f32 threshold = std::max(0.0f, post.bloomThreshold);
                const DownParams params{Vec4{1.0f / sw, 1.0f / sh, first ? 1.0f : 0.0f, threshold},
                                        Vec4{std::max(threshold * 0.2f, 1e-3f), exposure, 0, 0}};
                struct Cap { PipelineHandle p; FGTexture src; u64 sampler; DownParams u; } cap{*downPipe, src, linearClamp, params};
                const u32 pass = graph.add_raster_pass("3d-bloom-desce", PassStage::PostProcess, levels[i], LoadOp::DontCare,
                                                       Vec4{0, 0, 0, 0}, [cap](PassContext& pc) {
                    pc.cmds.bind_pipeline(cap.p);
                    pc.cmds.bind_texture(0, pc.texture(cap.src), SamplerHandle{cap.sampler});
                    pc.cmds.set_uniforms(&cap.u, sizeof(cap.u));
                    pc.cmds.draw(3);
                });
                graph.read(pass, src);
            }
            for (u32 i = levelCount; i-- > 1;) {
                const Vec4 texel{1.0f / static_cast<f32>(lw[i]), 1.0f / static_cast<f32>(lh[i]), 1.0f, 0.0f};
                struct Cap { PipelineHandle p; FGTexture src; u64 sampler; Vec4 u; } cap{*upPipe, levels[i], linearClamp, texel};
                const u32 pass = graph.add_raster_pass("3d-bloom-sobe", PassStage::PostProcess, levels[i - 1], LoadOp::Load,
                                                       Vec4{0, 0, 0, 0}, [cap](PassContext& pc) {
                    pc.cmds.bind_pipeline(cap.p);
                    pc.cmds.bind_texture(0, pc.texture(cap.src), SamplerHandle{cap.sampler});
                    pc.cmds.set_uniforms(&cap.u, sizeof(cap.u));
                    pc.cmds.draw(3);
                });
                graph.read(pass, levels[i]);
            }
        }
    }

    // --- Tone map + 2D -------------------------------------------------------
    // Força do bloom: com limiar (o padrão, 1,25 com joelho de 20%: só a luz
    // HDR de verdade — farol, emissivo, reflexo do sol — espalha; a pintura
    // difusa perto de 1 não ganha halo), soma de intensidade/10 (0,6 → 6%).
    // Sem limiar, mistura conservadora de intensidade/15 (0,6 → 4%, estilo
    // COD/Unreal: tudo espalha um pouco).
    const bool additive = post.bloomThreshold > 0.0f;
    const f32 mode = levelCount ? (additive ? 2.0f : 1.0f) : 0.0f;
    const f32 strength = additive ? intensity * 0.1f : std::min(intensity / 15.0f, 1.0f);
    struct ToneParams { Vec4 cfg; Vec4 bloom; };
    const ToneParams tp{Vec4{exposure, strength, mode, post.toneMapper >= 1 ? 1.0f : 0.0f},
                        Vec4{levelCount ? 1.0f / static_cast<f32>(levelCount) : 0.0f, 0, 0, 0}};
    TextureDesc od;
    od.width = width;
    od.height = height;
    od.format = SurfaceFormat::RGBA16F;
    od.sampled = true;
    od.renderTarget = true;
    const FGTexture out = graph.create_texture("3d-saida", od);
    const FGTexture bloom = levelCount ? levels[0] : display;
    struct Cap { PipelineHandle p; FGTexture scene, display, bloom; u64 sampler; ToneParams u; }
        cap{*tonemapPipe, scene, display, bloom, linearClamp, tp};
    const u32 pass = graph.add_raster_pass("3d-tonemap", PassStage::PostProcess, out, LoadOp::DontCare, Vec4{0, 0, 0, 0},
                                           [cap](PassContext& pc) {
        pc.cmds.bind_pipeline(cap.p);
        pc.cmds.bind_texture(0, pc.texture(cap.scene), SamplerHandle{cap.sampler});
        pc.cmds.bind_texture(1, pc.texture(cap.display), SamplerHandle{cap.sampler});
        pc.cmds.bind_texture(2, pc.texture(cap.bloom), SamplerHandle{cap.sampler});
        pc.cmds.set_uniforms(&cap.u, sizeof(cap.u));
        pc.cmds.draw(3);
    });
    graph.read(pass, scene);
    graph.read(pass, display);
    if (levelCount) graph.read(pass, levels[0]);
    return out;
}

bool SceneRenderer::build_dof(FrameGraph& graph, const SceneFrame& frame, u32 width, u32 height,
                              FGTexture& display, FGTexture& scene, FGTexture depth) noexcept {
    const DofLens lens = dof_lens(frame.camera, height);
    if (!lens.active() || !depth.valid() || !scene.valid() || !display.valid()) return false;
    auto cocPipe = shaders_->pipeline(PipelineKey::fullscreen(ShaderId::scene3d_post_dof_coc_frag, SurfaceFormat::RGBA16F));
    PipelineKey key = PipelineKey::fullscreen(ShaderId::scene3d_post_dof_frag, SurfaceFormat::RGBA16F);
    key.hasColor1 = true;
    auto pipe = shaders_->pipeline(key);
    if (!pipe.ok() || !cocPipe.ok()) return false;
    TextureDesc d;
    d.width = width;
    d.height = height;
    d.format = SurfaceFormat::RGBA16F;
    d.sampled = true;
    d.renderTarget = true;
    const FGTexture coc = graph.create_texture("3d-dof-coc", d);
    const FGTexture outDisplay = graph.create_texture("3d-dof-2d", d);
    const FGTexture outScene = graph.create_texture("3d-dof-hdr", d);
    const u64 nearest = shaders_->sampler(CommonSampler::NearestClamp).id;
    const Vec4 texel{1.0f / static_cast<f32>(width), 1.0f / static_cast<f32>(height), lens.maxRadius, 0.0f};
    // 1) Raio do círculo de confusão por pixel (profundidade mais perto do 3×3).
    {
        struct Params { Vec4 texel; Vec4 lens; };
        const Params u{texel, Vec4{lens.cocScale, lens.focusOverNear, 0, 0}};
        struct Cap { PipelineHandle p; FGTexture depth; u64 sampler; Params u; } cap{*cocPipe, depth, nearest, u};
        const u32 pass = graph.add_raster_pass("3d-dof-coc", PassStage::PostProcess, coc, LoadOp::DontCare, Vec4{0, 0, 0, 0},
                                               [cap](PassContext& pc) {
            pc.cmds.bind_pipeline(cap.p);
            pc.cmds.bind_texture(0, pc.texture(cap.depth), SamplerHandle{cap.sampler});
            pc.cmds.set_uniforms(&cap.u, sizeof(cap.u));
            pc.cmds.draw(3);
        });
        graph.read(pass, depth);
    }
    // 2) Gather no disco. Amostras: ~1 a cada 9 px² do maior disco (passo
    // ~3 px), no teto do nível (preview poucas, export muitas).
    const u32 taps = std::clamp(static_cast<u32>(lens.maxRadius * lens.maxRadius * 0.35f), 8u, std::max(8u, dofTaps_));
    const Vec4 u{texel.x, texel.y, texel.z, static_cast<f32>(taps)};
    struct Cap { PipelineHandle p; FGTexture scene, display, coc; u64 sampler; Vec4 u; }
        cap{*pipe, scene, display, coc, nearest, u};
    const u32 pass = graph.add_raster_pass("3d-dof", PassStage::PostProcess, outDisplay, LoadOp::DontCare, Vec4{0, 0, 0, 0},
                                           [cap](PassContext& pc) {
        pc.cmds.bind_pipeline(cap.p);
        pc.cmds.bind_texture(0, pc.texture(cap.scene), SamplerHandle{cap.sampler});
        pc.cmds.bind_texture(1, pc.texture(cap.display), SamplerHandle{cap.sampler});
        pc.cmds.bind_texture(2, pc.texture(cap.coc), SamplerHandle{cap.sampler});
        pc.cmds.set_uniforms(&cap.u, sizeof(cap.u));
        pc.cmds.draw(3);
    });
    graph.set_color1(pass, outScene);
    graph.read(pass, scene);
    graph.read(pass, display);
    graph.read(pass, coc);
    display = outDisplay;
    scene = outScene;
    return true;
}

FGTexture SceneRenderer::build_fxaa(FrameGraph& graph, u32 width, u32 height, FGTexture src) noexcept {
    auto pipe = shaders_->pipeline(PipelineKey::fullscreen(ShaderId::scene3d_post_fxaa_frag, SurfaceFormat::RGBA16F));
    if (!pipe.ok()) return FGTexture{};
    TextureDesc d;
    d.width = width;
    d.height = height;
    d.format = SurfaceFormat::RGBA16F;
    d.sampled = true;
    d.renderTarget = true;
    const FGTexture out = graph.create_texture("3d-fxaa", d);
    // Subpixel 0,75 e limiar 0,125: o "alta qualidade" do FXAA 3.11.
    const Vec4 params{1.0f / static_cast<f32>(width), 1.0f / static_cast<f32>(height), 0.75f, 0.125f};
    struct Cap { PipelineHandle p; FGTexture src; u64 sampler; Vec4 u; }
        cap{*pipe, src, shaders_->sampler(CommonSampler::LinearClamp).id, params};
    const u32 pass = graph.add_raster_pass("3d-fxaa", PassStage::PostProcess, out, LoadOp::DontCare, Vec4{0, 0, 0, 0},
                                           [cap](PassContext& pc) {
        pc.cmds.bind_pipeline(cap.p);
        pc.cmds.bind_texture(0, pc.texture(cap.src), SamplerHandle{cap.sampler});
        pc.cmds.set_uniforms(&cap.u, sizeof(cap.u));
        pc.cmds.draw(3);
    });
    graph.read(pass, src);
    return out;
}

} // namespace aurea::scene3d
