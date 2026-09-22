// =============================================================================
//  Aurea / scene3d / SceneRenderer.cpp
// =============================================================================
#include "aurea/scene3d/SceneRenderer.hpp"

#include "aurea/core/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

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
    Vec4 shadowParams;      ///< x = ligado, y = texel, z = viés, w = índice da luz
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
    std::unordered_map<u64, TextureHandle> byImage;
    auto texture_for = [&](const TextureRef& ref, bool srgb) -> TextureHandle {
        if (!ref.valid() || ref.image >= static_cast<i32>(asset.images.size())) return {};
        const Image& img = asset.images[ref.image];
        if (img.rgba.empty() || img.width == 0) return {};
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
        if (!t.ok()) return {};
        if (!gpu.upload_texture_level(*t, 0, 0, img.rgba.data(), img.rgba.size()).ok()) {
            gpu.destroy_texture(*t);
            return {};
        }
        // Sem blit linear no formato: fica sem mips (correto, só mais serrilhado).
        if (!gpu.generate_mipmaps(*t).ok()) AUREA_LOG_WARN("3D: mips nao gerados para uma textura");
        ownedTextures_.push_back(*t);
        textureBytes += d.estimated_bytes() * 4 / 3;
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
    for (TextureHandle* t : {&irradiance_, &prefiltered_, &iblLut_}) {
        if (t->valid()) gpu_->destroy_texture(*t);
        *t = TextureHandle{};
    }
}

Status SceneRenderer::set_environment(const EnvironmentMaps& maps) noexcept {
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
    release_environment();
    irradiance_ = irr;
    prefiltered_ = pre;
    iblLut_ = lut;
    prefilteredMips_ = maps.prefiltered.mips;
    return OkStatus;
}

void SceneRenderer::finish_environment() noexcept {
    if (irradiance_.valid() || !gpu_) return;
    EnvironmentMaps maps = pendingEnv_.valid() ? pendingEnv_.get() : build_studio_environment();
    envRequested_ = true;
    if (const Status s = set_environment(maps); !s.ok()) AUREA_LOG_WARN("3D: ambiente nao subiu: %s", s.message().data());
}

void SceneRenderer::shutdown() noexcept {
    if (pendingEnv_.valid()) pendingEnv_.wait();
    if (!gpu_) return;
    for (u32 i = 0; i < kJointRing; ++i) {
        if (jointBuf_[i].valid()) gpu_->destroy_buffer(jointBuf_[i]);
        if (morphBuf_[i].valid()) gpu_->destroy_buffer(morphBuf_[i]);
        jointBuf_[i] = morphBuf_[i] = BufferHandle{};
        jointCap_[i] = morphCap_[i] = 0;
    }
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
    for (u32 i = 0; i < kJointRing; ++i) {
        jointBuf_[i] = morphBuf_[i] = BufferHandle{};
        jointCap_[i] = morphCap_[i] = 0;
    }
    irradiance_ = prefiltered_ = iblLut_ = TextureHandle{};
    white_ = flatNormal_ = black_ = envCube_ = brdfLut_ = TextureHandle{};
    cubeSampler_ = SamplerHandle{};
    gpu_ = nullptr;
}

const GpuModel* SceneRenderer::model(u64 assetKey, const SceneAsset& asset) noexcept {
    Entry& e = models_[assetKey];
    if (e.model) return e.model.get();
    if (e.failed || !gpu_) return nullptr;
    auto m = std::make_unique<GpuModel>();
    if (const Status s = m->upload(*gpu_, asset); !s.ok()) {
        AUREA_LOG_ERROR("3D: upload do modelo falhou: %s", s.message().data());
        m->release(*gpu_);
        e.failed = true;
        return nullptr;
    }
    AUREA_LOG_INFO("3D: modelo na GPU (%.1f MB geometria, %.1f MB texturas)", m->geometryBytes / 1048576.0,
                   m->textureBytes / 1048576.0);
    e.model = std::move(m);
    return e.model.get();
}

void SceneRenderer::collect(u64 frameNumber, u64 idleFrames) noexcept {
    for (auto it = models_.begin(); it != models_.end();) {
        if (frameNumber > it->second.lastFrame + idleFrames) {
            if (it->second.model && gpu_) it->second.model->release(*gpu_);
            it = models_.erase(it);
        } else {
            ++it;
        }
    }
}

void SceneRenderer::release_all() noexcept {
    for (auto& [k, e] : models_) {
        if (e.model && gpu_) e.model->release(*gpu_);
    }
    models_.clear();
}

u64 SceneRenderer::resident_bytes() const noexcept {
    u64 b = 0;
    for (const auto& [k, e] : models_) {
        if (e.model) b += e.model->geometryBytes + e.model->textureBytes;
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
    // A face da frente do glTF (anti-horária vista de fora) continua anti-
    // horária no framebuffer: o giro de 180° em X (glTF → espaço da
    // composição) e o Y para baixo da projeção se cancelam. Travado pelo teste
    // Gpu.Scene3DFrontFaceIsVisibleAndBackFaceIsCulled.
    k.frontFaceCCW = true;
    return k;
}

void SceneRenderer::collect_pipelines(std::vector<PipelineKey>& out) const {
    for (AlphaMode mode : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend}) {
        for (bool twoSided : {false, true}) {
            out.push_back(key_for(mode, twoSided, false));
            out.push_back(key_for(mode, twoSided, true));
        }
    }
    out.push_back(shadow_key(false));
    out.push_back(shadow_key(true));
}

bool SceneRenderer::build(FrameGraph& graph, Arena& arena, const SceneFrame& frame, u32 width, u32 height,
                          u64 frameNumber, FGTexture& outColor) noexcept {
    stats_ = SceneStats{};
    if (!gpu_ || !shaders_ || width == 0 || height == 0) return false;
    if (!irradiance_.valid()) {
        // Primeiro grupo 3D: o estúdio neutro (IBL), gerado fora da thread de
        // render. Pronto → sobe e passa a valer no próximo quadro.
        if (!envRequested_) {
            envRequested_ = true;
            pendingEnv_ = std::async(std::launch::async, [] { return build_studio_environment(); });
        } else if (pendingEnv_.valid()
                   && pendingEnv_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const Status s = set_environment(pendingEnv_.get());
            if (!s.ok()) AUREA_LOG_WARN("3D: ambiente padrao nao subiu: %s", s.message().data());
        }
    }
    const bool ibl = irradiance_.valid();

    TextureDesc cd;
    cd.width = width;
    cd.height = height;
    cd.format = SurfaceFormat::RGBA16F;
    cd.sampled = true;
    cd.renderTarget = true;
    TextureDesc dd = cd;
    dd.format = SurfaceFormat::Depth32F;
    dd.sampled = false;
    const FGTexture color = graph.create_texture("3d-cor", cd);
    const FGTexture depth = graph.create_texture("3d-profundidade", dd);

    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
    const Mat4 proj = reverse_z_perspective(frame.camera.fovY, aspect, frame.camera.nearZ);
    const Mat4 viewProj = proj * frame.camera.view;

    // --- Cabeçalho comum a todos os desenhos do frame ---------------------------
    SceneBlock header{};
    header.viewProj = viewProj;
    header.cameraPos = Vec4{frame.camera.position, frame.environment.exposure};
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
        header.lightSpot2[i] = Vec4{std::cos(l.innerCone), 0, 0, 0};
    }

    // --- Lista de desenho ------------------------------------------------------
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
    };
    // Nível de detalhe pelo tamanho na tela: diâmetro projetado da caixa.
    const f32 pxPerUnit = static_cast<f32>(height) * 0.5f / std::tan(frame.camera.fovY * 0.5f);
    auto pick_lod = [&](const GpuPrimitive& p, const Mat4& world, const Mat4& viewFromLocal, u32& first, u32& count) {
        first = p.firstIndex;
        count = p.indexCount;
        if (p.lodLevels <= 1 || !p.bounds.valid()) return;
        const Vec3 c = viewFromLocal.transform_point(p.bounds.center());
        const f32 depth = std::max(std::fabs(c.z), frame.camera.nearZ);
        const f32 sx = Vec3{world.col[0].x, world.col[0].y, world.col[0].z}.length();
        const f32 sy = Vec3{world.col[1].x, world.col[1].y, world.col[1].z}.length();
        const f32 sz = Vec3{world.col[2].x, world.col[2].y, world.col[2].z}.length();
        const f32 diameter = p.bounds.extent().length() * std::max(sx, std::max(sy, sz));
        const f32 px = diameter / depth * pxPerUnit;
        u32 level = px < 60.0f ? 2u : (px < 160.0f ? 1u : 0u);
        level = std::min(level, p.lodLevels - 1);
        first = p.lodFirst[level];
        count = p.lodCount[level];
    };
    std::vector<Draw> opaque, blended;
    std::unordered_map<const GpuMaterial*, const SceneBlock*> blocks;

    auto block_for = [&](const GpuMaterial* gm) -> const SceneBlock* {
        if (auto it = blocks.find(gm); it != blocks.end()) return it->second;
        auto* b = static_cast<SceneBlock*>(arena.alloc(sizeof(SceneBlock), 16));
        if (!b) return nullptr;
        *b = header;
        const Material& m = gm->factors;
        b->baseColor = m.baseColor;
        b->emissive = Vec4{m.emissive * m.emissiveStrength, 0.0f};
        b->mr = Vec4{m.metallic, m.roughness, m.normalScale, m.occlusionStrength};
        b->alpha = Vec4{m.alphaCutoff, static_cast<f32>(static_cast<u8>(m.alphaMode)), m.unlit ? 1.0f : 0.0f,
                        m.doubleSided ? 1.0f : 0.0f};
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
        blocks.emplace(gm, b);
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
            total += frame.instances[i].jointMatrices.size();
        }
        if (total > 0) {
            const u32 slot = jointSlot_++ % kJointRing;
            const usize bytes = total * sizeof(Mat4);
            if (jointCap_[slot] < bytes) {
                if (jointBuf_[slot].valid()) gpu_->destroy_buffer(jointBuf_[slot]);
                BufferDesc bd;
                bd.bytes = std::max<usize>(bytes * 2, 64 * sizeof(Mat4));
                bd.usage = BufferUsage::Storage;
                bd.access = MemoryAccess::Upload;
                bd.debugName = "3d-juntas";
                auto b = gpu_->create_buffer(bd);
                jointBuf_[slot] = b.ok() ? *b : BufferHandle{};
                jointCap_[slot] = b.ok() ? bd.bytes : 0;
            }
            void* ptr = nullptr;
            if (jointBuf_[slot].valid() && gpu_->map_buffer(jointBuf_[slot], ptr).ok() && ptr) {
                auto* dst = static_cast<Mat4*>(ptr);
                for (usize i = 0; i < frame.instances.size(); ++i) {
                    const auto& jm = frame.instances[i].jointMatrices;
                    std::copy(jm.begin(), jm.end(), dst + instJointBase[i]);
                }
                gpu_->unmap_buffer(jointBuf_[slot]);
                joints = jointBuf_[slot];
            }
        }
    }

    // --- Sombra da luz principal ------------------------------------------------
    // Ortográfica ao longo da luz, ajustada à caixa dos modelos do grupo (com
    // folga: a pose animada sai da caixa de repouso).
    struct ShadowDraw {
        const GpuModel* model;
        const GpuPrimitive* prim;
        MeshPush push;   // model = luz ← local; normalCol[0].x = início das juntas
        PipelineHandle pipeline;
        bool skinned;
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
    if (shadowLight >= 0) {
        Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        bool any = false;
        for (const SceneInstance& inst : frame.instances) {
            if (!inst.asset) continue;
            const Aabb& b = inst.asset->bounds;
            for (int c = 0; c < 8; ++c) {
                const Vec3 pt{(c & 1) ? b.max.x : b.min.x, (c & 2) ? b.max.y : b.min.y, (c & 4) ? b.max.z : b.min.z};
                const Vec3 w = inst.world.transform_point(pt);
                lo = Vec3{std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
                hi = Vec3{std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
                any = true;
            }
        }
        if (any) {
            const Vec3 center = (lo + hi) * 0.5f;
            const f32 radius = std::max(1.0f, (hi - lo).length() * 0.5f * 1.25f);
            const Vec3 fwd = frame.lights[static_cast<usize>(shadowLight)].direction.normalized();
            const Vec3 upRef = std::fabs(fwd.y) > 0.95f ? Vec3{1, 0, 0} : Vec3{0, -1, 0};
            const Vec3 right = upRef.cross(fwd).normalized();
            const Vec3 up = fwd.cross(right);
            const Vec3 eye = center - fwd * (radius * 2.0f);
            Mat4 view;
            view.col[0] = Vec4{right.x, up.x, fwd.x, 0};
            view.col[1] = Vec4{right.y, up.y, fwd.y, 0};
            view.col[2] = Vec4{right.z, up.z, fwd.z, 0};
            view.col[3] = Vec4{-right.dot(eye), -up.dot(eye), -fwd.dot(eye), 1};
            // Ortográfica: x,y em ±radius → [−1, 1]; z em [radius, 3·radius] → [0, 1].
            Mat4 ortho;
            ortho.col[0] = Vec4{1.0f / radius, 0, 0, 0};
            ortho.col[1] = Vec4{0, 1.0f / radius, 0, 0};
            ortho.col[2] = Vec4{0, 0, 1.0f / (radius * 2.0f), 0};
            ortho.col[3] = Vec4{0, 0, -radius / (radius * 2.0f), 1};
            const Mat4 lightViewProj = ortho * view;
            // NDC → uv do mapa (o Y do Vulkan já desce com o v da textura).
            Mat4 toUv;
            toUv.col[0] = Vec4{0.5f, 0, 0, 0};
            toUv.col[1] = Vec4{0, 0.5f, 0, 0};
            toUv.col[3] = Vec4{0.5f, 0.5f, 0, 1};
            shadowMatrix = toUv * lightViewProj;
            for (usize instIndex = 0; instIndex < frame.instances.size(); ++instIndex) {
                const SceneInstance& inst = frame.instances[instIndex];
                if (!inst.asset || !inst.castShadows) continue;
                const GpuModel* gm = model(inst.assetKey, *inst.asset);
                if (!gm) continue;
                const std::vector<Node>& nodes = inst.asset->nodes;
                for (usize n = 0; n < nodes.size(); ++n) {
                    const i32 mi = nodes[n].mesh;
                    if (mi < 0 || mi >= static_cast<i32>(gm->meshes.size())) continue;
                    const i32 skinIndex = nodes[n].skin;
                    const bool skinnedNode = joints.valid() && skinIndex >= 0
                                           && skinIndex < static_cast<i32>(inst.skinJointOffset.size()) && gm->skin.valid();
                    const Mat4 world = skinnedNode ? inst.world
                                                   : inst.world * (n < inst.nodeWorld.size() ? inst.nodeWorld[n] : Mat4::identity());
                    for (const GpuPrimitive& p : gm->meshes[mi]) {
                        const GpuMaterial* mat = p.material >= 0 && p.material < static_cast<i32>(gm->materials.size())
                                               ? &gm->materials[p.material] : &gm->defaultMaterial;
                        if (mat->factors.alphaMode == AlphaMode::Blend) continue;   // transparente não projeta
                        const bool sk = skinnedNode && p.skinned;
                        auto pipe = shaders_->pipeline(shadow_key(sk));
                        if (!pipe.ok()) continue;
                        ShadowDraw sd{};
                        sd.model = gm;
                        sd.prim = &p;
                        sd.push.model = lightViewProj * world;
                        sd.push.normalCol[0].x = sk ? static_cast<f32>(instJointBase[instIndex]
                                                                        + inst.skinJointOffset[static_cast<usize>(skinIndex)]) : 0.0f;
                        sd.pipeline = *pipe;
                        sd.skinned = sk;
                        shadowDraws.push_back(sd);
                    }
                }
            }
        }
    }
    const bool shadowsOn = !shadowDraws.empty();
    header.shadowMatrix = shadowMatrix;
    header.shadowParams = Vec4{shadowsOn ? 1.0f : 0.0f, 1.0f / static_cast<f32>(shadowSize_), 0.0015f,
                               static_cast<f32>(shadowLight)};
    FGTexture shadowTex{};
    if (shadowsOn) {
        TextureDesc sd;
        sd.width = sd.height = shadowSize_;
        sd.format = SurfaceFormat::Depth32F;
        sd.sampled = true;
        sd.renderTarget = true;
        shadowTex = graph.create_texture("3d-sombra", sd);
        const u32 n = static_cast<u32>(shadowDraws.size());
        ShadowDraw* sl = arena.alloc_array<ShadowDraw>(n);
        if (!sl) return false;
        std::copy(shadowDraws.begin(), shadowDraws.end(), sl);
        struct SCap {
            ShadowDraw* draws;
            u32 count;
            BufferHandle joints;
        } scap{sl, n, joints};
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
                c.bind_vertex_buffer(0, d.model->positions, 0);
                if (d.skinned) c.bind_vertex_buffer(2, d.model->skin, 0);
                c.bind_index_buffer(d.model->indices, 0, d.model->indexType);
                c.push_constants(&d.push, sizeof(MeshPush));
                c.draw_indexed(d.prim->indexCount, 1, d.prim->firstIndex, d.prim->vertexOffset, 0);
            }
        });
    }

    // --- Morph (blend shapes): deformação na CPU, um bloco por primitiva -------
    // (instância, nó, primitiva) → deslocamento no buffer do quadro. Só entra
    // quem tem alvo E peso ≠ 0; o resto desenha direto da malha na GPU.
    struct MorphJob {
        usize inst, node, prim;
        const Primitive* src;
        const std::vector<f32>* weights;
        usize posOffset, shadeOffset;
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
                const std::vector<f32>* w = n < inst.morphWeights.size() && !inst.morphWeights[n].empty()
                                          ? &inst.morphWeights[n] : &mesh.morphWeights;
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
            const u32 slot = morphSlot_++ % kJointRing;
            if (morphCap_[slot] < bytes) {
                if (morphBuf_[slot].valid()) gpu_->destroy_buffer(morphBuf_[slot]);
                BufferDesc bd;
                bd.bytes = bytes * 2;
                bd.usage = BufferUsage::Vertex;
                bd.access = MemoryAccess::Upload;
                bd.debugName = "3d-morph";
                auto b = gpu_->create_buffer(bd);
                morphBuf_[slot] = b.ok() ? *b : BufferHandle{};
                morphCap_[slot] = b.ok() ? bd.bytes : 0;
            }
            void* ptr = nullptr;
            if (morphBuf_[slot].valid() && gpu_->map_buffer(morphBuf_[slot], ptr).ok() && ptr) {
                u8* base = static_cast<u8*>(ptr);
                for (const MorphJob& j : morphJobs) {
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
                gpu_->unmap_buffer(morphBuf_[slot]);
                morphBuf = morphBuf_[slot];
            } else {
                morphJobs.clear();
            }
        }
    }
    auto morph_for = [&](usize ii, usize n, usize k) -> const MorphJob* {
        for (const MorphJob& j : morphJobs) if (j.inst == ii && j.node == n && j.prim == k) return &j;
        return nullptr;
    };

    for (usize instIndex = 0; instIndex < frame.instances.size(); ++instIndex) {
        const SceneInstance& inst = frame.instances[instIndex];
        if (!inst.asset) continue;
        const GpuModel* gm = model(inst.assetKey, *inst.asset);
        if (auto it = models_.find(inst.assetKey); it != models_.end()) it->second.lastFrame = frameNumber;
        if (!gm) continue;
        const std::vector<Node>& nodes = inst.asset->nodes;
        for (usize n = 0; n < nodes.size(); ++n) {
            const i32 mi = nodes[n].mesh;
            if (mi < 0 || mi >= static_cast<i32>(gm->meshes.size())) continue;
            // Malha com skin: a pose vem das juntas (já no espaço da cena do
            // modelo); o nó da malha não entra (regra do glTF).
            const i32 skinIndex = nodes[n].skin;
            const bool skinnedNode = joints.valid() && skinIndex >= 0
                                   && skinIndex < static_cast<i32>(inst.skinJointOffset.size())
                                   && gm->skin.valid();
            const Mat4 world = skinnedNode ? inst.world
                                           : inst.world * (n < inst.nodeWorld.size() ? inst.nodeWorld[n] : Mat4::identity());
            const Mat4 clipFromLocal = viewProj * world;
            const Mat4 viewFromLocal = frame.camera.view * world;
            for (usize primIndex = 0; primIndex < gm->meshes[mi].size(); ++primIndex) {
                const GpuPrimitive& p = gm->meshes[mi][primIndex];
                const MorphJob* mj = morph_for(instIndex, n, primIndex);
                // Com skin (ou morph) a caixa de repouso não vale para a pose: sem recorte.
                if (!(skinnedNode && p.skinned) && !mj && outside_frustum(clipFromLocal, p.bounds, frame.camera.nearZ)) {
                    ++stats_.culledPrimitives;
                    continue;
                }
                const GpuMaterial* mat = p.material >= 0 && p.material < static_cast<i32>(gm->materials.size())
                                       ? &gm->materials[p.material] : &gm->defaultMaterial;
                const bool skinDraw = skinnedNode && p.skinned;
                const PipelineKey key = key_for(mat->factors.alphaMode, mat->factors.doubleSided, skinDraw);
                auto pipe = shaders_->pipeline(key);
                if (!pipe.ok()) continue;
                const SceneBlock* blk = block_for(mat);
                if (!blk) continue;
                Draw d{};
                d.model = gm;
                d.prim = &p;
                d.material = mat;
                d.block = blk;
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
                                                             + inst.skinJointOffset[static_cast<usize>(skinIndex)]);
                }
                d.pipeline = *pipe;
                if (skinDraw || d.morph) { d.firstIndex = p.firstIndex; d.indexCount = p.indexCount; }
                else pick_lod(p, world, viewFromLocal, d.firstIndex, d.indexCount);
                d.viewDepth = viewFromLocal.transform_point(p.bounds.center()).z;
                d.sortKey = static_cast<u32>(pipe->id & 0xFFFF) << 16 | static_cast<u32>(reinterpret_cast<uintptr_t>(mat) >> 4 & 0xFFFF);
                (mat->factors.alphaMode == AlphaMode::Blend ? blended : opaque).push_back(d);
                ++stats_.visiblePrimitives;
                stats_.triangles += d.indexCount / 3;
            }
        }
        stats_.geometryBytes += gm->geometryBytes;
        stats_.textureBytes += gm->textureBytes;
    }
    // Opacos agrupados por pipeline/material (menos trocas de estado); os
    // transparentes do mais longe para o mais perto (ordem correta do blend).
    std::sort(opaque.begin(), opaque.end(), [](const Draw& a, const Draw& b) { return a.sortKey < b.sortKey; });
    std::sort(blended.begin(), blended.end(), [](const Draw& a, const Draw& b) { return a.viewDepth > b.viewDepth; });

    const u32 total = static_cast<u32>(opaque.size() + blended.size());
    Draw* list = total ? arena.alloc_array<Draw>(total) : nullptr;
    if (total && !list) return false;
    for (usize i = 0; i < opaque.size(); ++i) list[i] = opaque[i];
    for (usize i = 0; i < blended.size(); ++i) list[opaque.size() + i] = blended[i];
    stats_.drawCalls = total;

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
        BufferHandle morph;
    } cap{list, total, white_, flatNormal_, ibl ? irradiance_ : envCube_, ibl ? prefiltered_ : envCube_,
          ibl ? iblLut_ : brdfLut_, cubeSampler_, shaders_->sampler(CommonSampler::LinearRepeat).id,
          shaders_->sampler(CommonSampler::LinearClamp).id, joints, shadowTex,
          shaders_->sampler(CommonSampler::NearestClamp).id, morphBuf};

    // Z reverso: limpa a profundidade com 0 (o infinito).
    const u32 pbrPass = graph.add_raster_pass_depth("3d-pbr", PassStage::Scene3D, color, LoadOp::Clear, Vec4{0, 0, 0, 0}, depth,
                                LoadOp::Clear, false, 0.0f, [cap](PassContext& pc) {
        CommandList& c = pc.cmds;
        PipelineHandle bound{};
        const GpuModel* boundModel = nullptr;
        const GpuMaterial* boundMat = nullptr;
        for (u32 i = 0; i < cap.count; ++i) {
            const Draw& d = cap.draws[i];
            if (!(d.pipeline == bound)) {
                c.bind_pipeline(d.pipeline);
                bound = d.pipeline;
                boundMat = nullptr;
            }
            if (d.material != boundMat) {
                const TextureHandle fallback[5] = {cap.white, cap.white, cap.flatNormal, cap.white, cap.white};
                for (u32 k = 0; k < 5; ++k) {
                    const bool has = d.material->tex[k].valid();
                    c.bind_texture(k, has ? d.material->tex[k] : fallback[k],
                                   has && d.material->samp[k].valid() ? d.material->samp[k] : SamplerHandle{cap.linearSampler});
                }
                c.bind_texture(5, cap.irradiance, cap.cubeSampler);
                c.bind_texture(6, cap.prefiltered, cap.cubeSampler);
                c.bind_texture(7, cap.brdf, SamplerHandle{cap.clampSampler});
                c.bind_texture(8, cap.shadow.valid() ? pc.texture(cap.shadow) : cap.white, SamplerHandle{cap.nearestSampler});
                boundMat = d.material;
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
            c.set_uniforms(d.block, sizeof(SceneBlock));
            c.push_constants(&d.push, sizeof(MeshPush));
            c.draw_indexed(d.indexCount, 1, d.firstIndex, d.prim->vertexOffset, 0);
        }
    });
    if (shadowTex.valid()) graph.read(pbrPass, shadowTex);
    outColor = color;
    return true;
}

} // namespace aurea::scene3d
