// =============================================================================
//  Aurea / scene3d / SceneAsset.hpp
//
//  O formato 3D interno do Aurea.
//
//  Todo importador (glTF/GLB agora; FBX, OBJ… depois) produz ISTO. O renderer,
//  a timeline e a serialização nunca veem estruturas do parser — trocar de
//  biblioteca de leitura não toca no motor.
//
//  É imutável depois do import: várias layers (ModelInstance) compartilham o
//  mesmo SceneAsset e os mesmos recursos de GPU. O que uma layer muda
//  (transform, clipe de animação, material) mora na layer, como override.
//
//  Convenções (as do glTF): metros, Y para cima, destro, frente = +Z do
//  modelo, quaternions (x,y,z,w), matrizes coluna-maior.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <string>
#include <vector>

namespace aurea::scene3d {

/// Versão do formato. Muda sempre que um campo muda de significado ou de
/// layout serializado no cache: cache de versão diferente é reconstruído.
inline constexpr u32 kSceneAssetVersion = 1;

/// Por que um import falhou. Nunca só "falhou": a UI diz o motivo certo.
enum class ImportError : u8 {
    None = 0,
    FileNotFound,
    InvalidFormat,          ///< não é glTF/GLB válido (JSON quebrado, cabeçalho)
    MissingBuffer,          ///< .bin externo ausente
    InvalidAccessor,        ///< acessor aponta fora do buffer / tipo errado
    UnsupportedCompression, ///< Draco, meshopt sem suporte, etc.
    TextureDecodeFailed,    ///< imagem ilegível (e o material a exige)
    OutOfMemory,
    UnsupportedFeature,     ///< extensão obrigatória que o motor não tem
    NoGeometry,             ///< nada renderizável no arquivo
    Cancelled,
};

[[nodiscard]] constexpr const char* to_string(ImportError e) noexcept {
    switch (e) {
        case ImportError::None:                   return "ok";
        case ImportError::FileNotFound:           return "arquivo nao encontrado";
        case ImportError::InvalidFormat:          return "arquivo 3D invalido";
        case ImportError::MissingBuffer:          return "buffer do modelo ausente";
        case ImportError::InvalidAccessor:        return "dados de geometria invalidos";
        case ImportError::UnsupportedCompression: return "compressao de geometria nao suportada";
        case ImportError::TextureDecodeFailed:    return "textura ilegivel";
        case ImportError::OutOfMemory:            return "sem memoria";
        case ImportError::UnsupportedFeature:     return "recurso obrigatorio nao suportado";
        case ImportError::NoGeometry:             return "o arquivo nao tem geometria";
        case ImportError::Cancelled:              return "cancelado";
    }
    return "?";
}

struct Aabb {
    Vec3 min{ 1e30f,  1e30f,  1e30f};
    Vec3 max{-1e30f, -1e30f, -1e30f};

    [[nodiscard]] bool valid() const noexcept { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    void add(Vec3 p) noexcept {
        min = Vec3{std::fmin(min.x, p.x), std::fmin(min.y, p.y), std::fmin(min.z, p.z)};
        max = Vec3{std::fmax(max.x, p.x), std::fmax(max.y, p.y), std::fmax(max.z, p.z)};
    }
    void add(const Aabb& o) noexcept {
        if (!o.valid()) return;
        add(o.min);
        add(o.max);
    }
    [[nodiscard]] Vec3 center() const noexcept { return (min + max) * 0.5f; }
    [[nodiscard]] Vec3 extent() const noexcept { return max - min; }
    /// A caixa depois de uma transformação afim (os 8 cantos).
    [[nodiscard]] Aabb transformed(const Mat4& m) const noexcept {
        Aabb r;
        if (!valid()) return r;
        for (int i = 0; i < 8; ++i) {
            const Vec3 c{(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z};
            r.add(m.transform_point(c));
        }
        return r;
    }
};

// -----------------------------------------------------------------------------
// Geometria
// -----------------------------------------------------------------------------

/// Um alvo de morph: deltas por vértice (posição obrigatória; normal e
/// tangente quando o arquivo traz).
struct MorphTarget {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec3> tangents;
};

/// Primitiva = uma chamada de desenho: um material, uma topologia.
/// Os fluxos opcionais ficam vazios quando o arquivo não os tem; o upload
/// decide o layout de vértice a partir do que existe.
struct Primitive {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;      ///< vazio = gerado no import (flat → suave)
    std::vector<Vec4> tangents;     ///< w = sinal da bitangente (±1)
    std::vector<Vec2> uv0;
    std::vector<Vec2> uv1;
    std::vector<u32>  colors;       ///< RGBA8 linear
    std::vector<u16>  joints;       ///< 4 por vértice
    std::vector<Vec4> weights;      ///< 4 por vértice, somando 1
    std::vector<MorphTarget> morphTargets;

    std::vector<u32> indices;       ///< triângulos; o upload usa u16 quando cabe
    /// Níveis de detalhe: índices mais leves sobre os MESMOS vértices
    /// (meshoptimizer), do mais fino para o mais grosso. Vazio = só o nível 0.
    std::vector<std::vector<u32>> lods;
    i32  material = -1;             ///< -1 = material padrão
    Aabb bounds{};
    bool generatedNormals = false;
    bool generatedTangents = false;

    [[nodiscard]] u32 vertex_count() const noexcept { return static_cast<u32>(positions.size()); }
    [[nodiscard]] u32 triangle_count() const noexcept { return static_cast<u32>(indices.size() / 3); }
    [[nodiscard]] bool skinned() const noexcept { return !joints.empty() && !weights.empty(); }
};

struct Mesh {
    std::string name;
    std::vector<Primitive> primitives;
    std::vector<f32> morphWeights;   ///< pesos padrão (o nó pode sobrescrever)
    Aabb bounds{};
};

// -----------------------------------------------------------------------------
// Materiais e texturas
// -----------------------------------------------------------------------------

enum class Wrap : u8 { Repeat = 0, Clamp, Mirror };
enum class Filter : u8 { Linear = 0, Nearest };

struct Sampler {
    Wrap wrapS = Wrap::Repeat;
    Wrap wrapT = Wrap::Repeat;
    Filter mag = Filter::Linear;
    Filter min = Filter::Linear;
    bool mipmaps = true;
};

/// Imagem já decodificada (RGBA8). O espaço de cor NÃO é da imagem, é do uso:
/// a mesma imagem pode ser cor (sRGB) num material e dado (linear) noutro —
/// o upload cria a textura com o formato certo para cada uso.
struct Image {
    std::string name;
    std::string uri;          ///< externa (relativa ao .gltf) ou vazia (embutida)
    u32 width = 0, height = 0;
    std::vector<u8> rgba;     ///< vazio se a decodificação ficou para o streaming
    bool hasAlpha = false;
};

struct TextureRef {
    i32 image = -1;           ///< -1 = sem textura
    i32 sampler = -1;         ///< -1 = sampler padrão (repeat, linear, mips)
    u32 texCoord = 0;         ///< UV0 ou UV1
    // KHR_texture_transform
    Vec2 offset{0.0f, 0.0f};
    Vec2 scale{1.0f, 1.0f};
    f32  rotation = 0.0f;

    [[nodiscard]] bool valid() const noexcept { return image >= 0; }
};

enum class AlphaMode : u8 { Opaque = 0, Mask, Blend };

struct Material {
    std::string name;

    // metallic-roughness (núcleo do glTF)
    Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};   ///< linear
    f32  metallic = 1.0f;
    f32  roughness = 1.0f;
    TextureRef baseColorTex;           ///< sRGB
    TextureRef metallicRoughnessTex;   ///< linear (G = rugosidade, B = metal)
    TextureRef normalTex;              ///< linear
    f32  normalScale = 1.0f;
    TextureRef occlusionTex;           ///< linear (R)
    f32  occlusionStrength = 1.0f;
    TextureRef emissiveTex;            ///< sRGB
    Vec3 emissive{0.0f, 0.0f, 0.0f};   ///< linear
    f32  emissiveStrength = 1.0f;      ///< KHR_materials_emissive_strength

    AlphaMode alphaMode = AlphaMode::Opaque;
    f32  alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool unlit = false;                ///< KHR_materials_unlit

    // Extensões lidas e guardadas. As que o shader ainda não usa ficam em
    // `ignoredExtensions` — o import não finge suporte.
    f32  ior = 1.5f;                   ///< KHR_materials_ior
    f32  clearcoat = 0.0f;             ///< KHR_materials_clearcoat
    f32  clearcoatRoughness = 0.0f;
    f32  transmission = 0.0f;          ///< KHR_materials_transmission
    Vec3 specularColor{1.0f, 1.0f, 1.0f};   ///< KHR_materials_specular
    f32  specular = 1.0f;
    Vec3 sheenColor{0.0f, 0.0f, 0.0f};      ///< KHR_materials_sheen
    f32  sheenRoughness = 0.0f;
    std::vector<std::string> ignoredExtensions;
};

// -----------------------------------------------------------------------------
// Cena
// -----------------------------------------------------------------------------

struct CameraInfo {
    std::string name;
    bool perspective = true;
    f32 yfov = 0.8f;          ///< radianos
    f32 aspect = 0.0f;        ///< 0 = o da composição
    f32 znear = 0.01f;
    f32 zfar = 0.0f;          ///< 0 = infinito
    f32 xmag = 1.0f, ymag = 1.0f;
};

enum class LightType : u8 { Directional = 0, Point, Spot };

struct LightInfo {           ///< KHR_lights_punctual
    std::string name;
    LightType type = LightType::Directional;
    Vec3 color{1.0f, 1.0f, 1.0f};
    f32  intensity = 1.0f;    ///< lux (direcional) ou candela (ponto/spot)
    f32  range = 0.0f;        ///< 0 = infinito
    f32  innerCone = 0.0f;
    f32  outerCone = 0.7853982f;
};

struct Node {
    std::string name;
    i32 parent = -1;
    std::vector<i32> children;
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Quat rotation = Quat::identity();
    Vec3 scale{1.0f, 1.0f, 1.0f};
    i32 mesh = -1;
    i32 skin = -1;
    i32 camera = -1;
    i32 light = -1;
    std::vector<f32> morphWeights;    ///< vazio = os da malha

    [[nodiscard]] Mat4 local_matrix() const noexcept {
        return Mat4::translation(translation) * Mat4::from_quat(rotation) * Mat4::scale(scale);
    }
};

struct Skin {
    std::string name;
    std::vector<i32>  joints;          ///< índices de nó
    std::vector<Mat4> inverseBind;     ///< um por junta
    i32 skeleton = -1;
};

enum class AnimPath : u8 { Translation = 0, Rotation, Scale, Weights };
enum class AnimInterp : u8 { Linear = 0, Step, CubicSpline };

struct AnimSampler {
    std::vector<f32> times;            ///< segundos, crescentes
    std::vector<f32> values;           ///< achatados; cubicspline: (in, valor, out) por chave
    AnimInterp interpolation = AnimInterp::Linear;
    u32 components = 3;                ///< 3 (T/S), 4 (R), N (pesos de morph)
};

struct AnimChannel {
    i32 node = -1;
    AnimPath path = AnimPath::Translation;
    u32 sampler = 0;
};

struct Animation {
    std::string name;
    std::vector<AnimSampler> samplers;
    std::vector<AnimChannel> channels;
    f32 duration = 0.0f;               ///< segundos
};

struct ImportStats {
    u32 nodes = 0, meshes = 0, primitives = 0;
    u32 vertices = 0, triangles = 0;
    u32 materials = 0, images = 0, animations = 0, skins = 0, morphTargets = 0;
    u64 geometryBytes = 0, imageBytes = 0;
    f32 parseMs = 0, geometryMs = 0, imagesMs = 0, optimizeMs = 0;
};

struct SceneAsset {
    u32 version = kSceneAssetVersion;
    std::string sourceName;

    std::vector<Node> nodes;
    std::vector<i32> roots;            ///< cena padrão
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Image> images;
    std::vector<Sampler> samplers;
    std::vector<CameraInfo> cameras;
    std::vector<LightInfo> lights;
    std::vector<Skin> skins;
    std::vector<Animation> animations;

    /// Caixa da cena em repouso (todos os nós com malha, no mundo). É o que
    /// enquadra o modelo recém-importado: IMPORTOU → APARECE.
    Aabb bounds{};
    ImportStats stats{};
    /// Avisos que não impedem o import (extensão de material ignorada,
    /// normais geradas…). A UI mostra; nunca silencioso.
    std::vector<std::string> warnings;

    /// Matrizes de mundo em repouso (sem animação), uma por nó.
    [[nodiscard]] std::vector<Mat4> rest_world_matrices() const;
};

} // namespace aurea::scene3d
