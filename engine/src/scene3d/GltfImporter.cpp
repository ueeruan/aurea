// =============================================================================
//  Aurea / scene3d / GltfImporter.cpp
//
//  glTF 2.0 / GLB → SceneAsset, com cgltf (parser) e meshoptimizer (ordem de
//  vértices). O parser só existe aqui dentro: nenhuma estrutura cgltf sai
//  deste arquivo.
// =============================================================================
#include "aurea/scene3d/Importer.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <unordered_map>

// Implementações de terceiros: os avisos deles não são nossos.
#if defined(_MSC_VER)
    #pragma warning(push, 0)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wall"
    #pragma GCC diagnostic ignored "-Wextra"
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_FAILURE_USERMSG
#include "stb_image.h"

#include "meshoptimizer.h"
#include "ufbx.h"
#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

namespace aurea::scene3d {
namespace {

struct Failure {
    ImportError error = ImportError::None;
    std::string detail;
};

bool cancelled(ImportProgress* p) noexcept { return p && p->cancel.load(std::memory_order_relaxed); }

void set_phase(ImportProgress* p, ImportPhase phase, f32 fraction = 0.0f) noexcept {
    if (!p) return;
    p->phase.store(phase, std::memory_order_relaxed);
    p->fraction.store(fraction, std::memory_order_relaxed);
}

void set_fraction(ImportProgress* p, f32 f) noexcept {
    if (p) p->fraction.store(std::clamp(f, 0.0f, 1.0f), std::memory_order_relaxed);
}

f32 ms_since(u64 t0) noexcept { return static_cast<f32>(static_cast<f64>(monotonic_ns() - t0) / 1e6); }

std::string dir_of(const std::string& path) {
    const usize slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

bool read_file(const std::string& path, std::vector<u8>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) { std::fclose(f); return false; }
    out.resize(static_cast<usize>(size));
    const bool ok = size == 0 || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

/// Decodifica %XX de uma URI relativa (glTF permite espaços codificados).
std::string uri_decode(const char* uri) {
    std::string s(uri ? uri : "");
    cgltf_decode_uri(s.data());
    s.resize(std::strlen(s.c_str()));
    return s;
}

// --- Leitura de recursos externos pelo leitor da plataforma --------------------
struct ReaderCtx {
    const ImportOptions* options = nullptr;
    std::string baseDir;
};

cgltf_result file_read(const cgltf_memory_options*, const cgltf_file_options* fo, const char* path,
                       cgltf_size* size, void** data) {
    auto* ctx = static_cast<ReaderCtx*>(fo->user_data);
    std::vector<u8> bytes;
    bool ok = false;
    if (ctx->options->reader) {
        // cgltf passa o caminho já composto com o diretório base; o leitor da
        // plataforma quer só a parte relativa.
        std::string rel = path;
        if (!ctx->baseDir.empty() && rel.rfind(ctx->baseDir, 0) == 0) rel = rel.substr(ctx->baseDir.size());
        ok = ctx->options->reader(rel.c_str(), bytes, ctx->options->readerUser);
    } else {
        ok = read_file(path, bytes);
    }
    if (!ok) return cgltf_result_file_not_found;
    void* mem = std::malloc(bytes.empty() ? 1 : bytes.size());
    if (!mem) return cgltf_result_out_of_memory;
    if (!bytes.empty()) std::memcpy(mem, bytes.data(), bytes.size());
    *size = bytes.size();
    *data = mem;
    return cgltf_result_success;
}

void file_release(const cgltf_memory_options*, const cgltf_file_options*, void* data) { std::free(data); }

// --- Acessores ------------------------------------------------------------------
template <typename T>
bool unpack(const cgltf_accessor* a, std::vector<T>& out, cgltf_size components, Failure& fail, const char* what) {
    if (!a) return true;
    if (cgltf_num_components(a->type) != components) {
        fail = {ImportError::InvalidAccessor, std::string(what) + " com tipo inesperado"};
        return false;
    }
    if (a->buffer_view && !a->buffer_view->buffer->data && !a->is_sparse) {
        fail = {ImportError::MissingBuffer, std::string(what) + ": buffer nao carregado"};
        return false;
    }
    out.resize(a->count);
    const cgltf_size n = cgltf_accessor_unpack_floats(a, reinterpret_cast<cgltf_float*>(out.data()), a->count * components);
    if (n != a->count * components) {
        fail = {ImportError::InvalidAccessor, std::string(what) + " fora do buffer"};
        return false;
    }
    return true;
}

// --- Geometria --------------------------------------------------------------------

/// "Desfaz" o compartilhamento de vértices: um vértice por índice. É o que
/// normais planas exigem (spec glTF: sem NORMAL → normais planas).
template <typename T>
void expand(std::vector<T>& v, const std::vector<u32>& idx) {
    if (v.empty()) return;
    std::vector<T> out(idx.size());
    for (usize i = 0; i < idx.size(); ++i) out[i] = v[idx[i]];
    v.swap(out);
}

void expand_joints(std::vector<u16>& j, const std::vector<u32>& idx) {
    if (j.empty()) return;
    std::vector<u16> out(idx.size() * 4);
    for (usize i = 0; i < idx.size(); ++i) std::memcpy(&out[i * 4], &j[idx[i] * 4], 8);
    j.swap(out);
}

void flat_normals(Primitive& p) {
    const std::vector<u32> idx = p.indices;
    expand(p.positions, idx);
    expand(p.tangents, idx);
    expand(p.uv0, idx);
    expand(p.uv1, idx);
    expand(p.colors, idx);
    expand(p.weights, idx);
    expand_joints(p.joints, idx);
    for (MorphTarget& t : p.morphTargets) {
        expand(t.positions, idx);
        expand(t.normals, idx);
        expand(t.tangents, idx);
    }
    p.normals.assign(p.positions.size(), Vec3{0.0f, 0.0f, 1.0f});
    for (usize i = 0; i + 2 < p.positions.size(); i += 3) {
        const Vec3 n = (p.positions[i + 1] - p.positions[i]).cross(p.positions[i + 2] - p.positions[i]);
        const f32 l = n.length();
        const Vec3 nn = l > 1e-20f ? n * (1.0f / l) : Vec3{0.0f, 0.0f, 1.0f};
        p.normals[i] = p.normals[i + 1] = p.normals[i + 2] = nn;
    }
    p.indices.resize(p.positions.size());
    for (u32 i = 0; i < p.indices.size(); ++i) p.indices[i] = i;
    p.generatedNormals = true;
}

/// Tangentes por acumulação por triângulo + Gram-Schmidt, com o sinal da
/// bitangente em w. Mesma convenção de espaço tangente do MikkTSpace para
/// malhas bem condicionadas; malhas com UV espelhado dentro de um triângulo
/// podem divergir (o arquivo deveria trazer TANGENT nesse caso — o glTF
/// recomenda).
void generate_tangents(Primitive& p, const std::vector<Vec2>& uv) {
    const usize n = p.positions.size();
    std::vector<Vec3> tan(n, Vec3{0, 0, 0}), bit(n, Vec3{0, 0, 0});
    for (usize t = 0; t + 2 < p.indices.size(); t += 3) {
        const u32 i0 = p.indices[t], i1 = p.indices[t + 1], i2 = p.indices[t + 2];
        const Vec3 e1 = p.positions[i1] - p.positions[i0];
        const Vec3 e2 = p.positions[i2] - p.positions[i0];
        const Vec2 d1 = uv[i1] - uv[i0];
        const Vec2 d2 = uv[i2] - uv[i0];
        const f32 det = d1.x * d2.y - d2.x * d1.y;
        if (std::fabs(det) < 1e-20f) continue;
        const f32 r = 1.0f / det;
        const Vec3 sdir = (e1 * d2.y - e2 * d1.y) * r;
        const Vec3 tdir = (e2 * d1.x - e1 * d2.x) * r;
        for (u32 i : {i0, i1, i2}) {
            tan[i] = tan[i] + sdir;
            bit[i] = bit[i] + tdir;
        }
    }
    p.tangents.resize(n);
    for (usize i = 0; i < n; ++i) {
        const Vec3 nn = p.normals[i];
        Vec3 t = tan[i] - nn * nn.dot(tan[i]);
        if (t.length_sq() < 1e-20f) {
            // Sem UV útil: qualquer eixo perpendicular à normal.
            t = std::fabs(nn.x) < 0.9f ? Vec3{1, 0, 0}.cross(nn) : Vec3{0, 1, 0}.cross(nn);
        }
        t = t.normalized();
        const f32 w = nn.cross(t).dot(bit[i]) < 0.0f ? -1.0f : 1.0f;
        p.tangents[i] = Vec4{t.x, t.y, t.z, w};
    }
    p.generatedTangents = true;
}

/// Reordena índices (cache de vértices, overdraw) e vértices (localidade de
/// leitura). Não muda a aparência: os mesmos triângulos, em outra ordem.
void optimize_primitive(Primitive& p) {
    const usize vc = p.positions.size();
    if (vc == 0 || p.indices.empty()) return;
    // Transparência depende da ordem dos triângulos do autor; não mexe.
    std::vector<u32> tmp(p.indices.size());
    meshopt_optimizeVertexCache(tmp.data(), p.indices.data(), p.indices.size(), vc);
    meshopt_optimizeOverdraw(p.indices.data(), tmp.data(), tmp.size(), &p.positions[0].x, vc, sizeof(Vec3), 1.05f);
    std::vector<u32> remap(vc);
    const usize unique = meshopt_optimizeVertexFetchRemap(remap.data(), p.indices.data(), p.indices.size(), vc);
    meshopt_remapIndexBuffer(p.indices.data(), p.indices.data(), p.indices.size(), remap.data());
    auto apply = [&](auto& v) {
        using T = typename std::decay_t<decltype(v)>::value_type;
        if (v.empty()) return;
        std::vector<T> out(unique);
        meshopt_remapVertexBuffer(out.data(), v.data(), vc, sizeof(T), remap.data());
        v.swap(out);
    };
    apply(p.positions);
    apply(p.normals);
    apply(p.tangents);
    apply(p.uv0);
    apply(p.uv1);
    apply(p.colors);
    apply(p.weights);
    if (!p.joints.empty()) {
        std::vector<u16> out(unique * 4);
        meshopt_remapVertexBuffer(out.data(), p.joints.data(), vc, sizeof(u16) * 4, remap.data());
        p.joints.swap(out);
    }
    for (MorphTarget& t : p.morphTargets) {
        apply(t.positions);
        apply(t.normals);
        apply(t.tangents);
    }
}

u32 pack_rgba8(Vec4 c) noexcept {
    auto q = [](f32 v) { return static_cast<u32>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return q(c.x) | (q(c.y) << 8) | (q(c.z) << 16) | (q(c.w) << 24);
}

bool build_primitive(const cgltf_primitive& src, const cgltf_data* data, Primitive& out, Failure& fail,
                     std::vector<std::string>& warnings) {
    if (src.has_draco_mesh_compression) {
        fail = {ImportError::UnsupportedCompression, "geometria comprimida com Draco (ainda nao suportado)"};
        return false;
    }
    const cgltf_accessor* pos = nullptr;
    const cgltf_accessor *nrm = nullptr, *tng = nullptr, *uv0 = nullptr, *uv1 = nullptr, *col = nullptr;
    const cgltf_accessor *jnt = nullptr, *wgt = nullptr;
    for (cgltf_size i = 0; i < src.attributes_count; ++i) {
        const cgltf_attribute& a = src.attributes[i];
        switch (a.type) {
            case cgltf_attribute_type_position: pos = a.data; break;
            case cgltf_attribute_type_normal:   nrm = a.data; break;
            case cgltf_attribute_type_tangent:  tng = a.data; break;
            case cgltf_attribute_type_texcoord: if (a.index == 0) uv0 = a.data; else if (a.index == 1) uv1 = a.data; break;
            case cgltf_attribute_type_color:    if (a.index == 0) col = a.data; break;
            case cgltf_attribute_type_joints:   if (a.index == 0) jnt = a.data; break;
            case cgltf_attribute_type_weights:  if (a.index == 0) wgt = a.data; break;
            default: break;
        }
    }
    if (!pos || pos->count == 0) {
        fail = {ImportError::InvalidAccessor, "primitiva sem POSITION"};
        return false;
    }
    if (!unpack(pos, out.positions, 3, fail, "POSITION")) return false;
    if (!unpack(nrm, out.normals, 3, fail, "NORMAL")) return false;
    if (!unpack(tng, out.tangents, 4, fail, "TANGENT")) return false;
    if (!unpack(uv0, out.uv0, 2, fail, "TEXCOORD_0")) return false;
    if (!unpack(uv1, out.uv1, 2, fail, "TEXCOORD_1")) return false;
    if (col) {
        const cgltf_size comps = cgltf_num_components(col->type);
        std::vector<f32> tmp(col->count * comps);
        if (cgltf_accessor_unpack_floats(col, tmp.data(), tmp.size()) != tmp.size()) {
            fail = {ImportError::InvalidAccessor, "COLOR_0 fora do buffer"};
            return false;
        }
        out.colors.resize(col->count);
        for (cgltf_size v = 0; v < col->count; ++v) {
            const f32* c = &tmp[v * comps];
            out.colors[v] = pack_rgba8(Vec4{c[0], c[1], c[2], comps == 4 ? c[3] : 1.0f});
        }
    }
    if (jnt && wgt) {
        if (cgltf_num_components(jnt->type) != 4 || cgltf_num_components(wgt->type) != 4) {
            fail = {ImportError::InvalidAccessor, "JOINTS_0/WEIGHTS_0 com tipo inesperado"};
            return false;
        }
        out.joints.resize(jnt->count * 4);
        for (cgltf_size v = 0; v < jnt->count; ++v) {
            cgltf_uint j[4] = {0, 0, 0, 0};
            if (!cgltf_accessor_read_uint(jnt, v, j, 4)) {
                fail = {ImportError::InvalidAccessor, "JOINTS_0 fora do buffer"};
                return false;
            }
            for (int k = 0; k < 4; ++k) out.joints[v * 4 + k] = static_cast<u16>(j[k]);
        }
        if (!unpack(wgt, out.weights, 4, fail, "WEIGHTS_0")) return false;
        for (Vec4& w : out.weights) {
            const f32 s = w.x + w.y + w.z + w.w;
            w = s > 1e-8f ? w * (1.0f / s) : Vec4{1, 0, 0, 0};
        }
    }

    const usize vc = out.positions.size();
    auto same_count = [&](usize n, const char* what) {
        if (n == 0 || n == vc) return true;
        fail = {ImportError::InvalidAccessor, std::string(what) + " com contagem diferente de POSITION"};
        return false;
    };
    if (!same_count(out.normals.size(), "NORMAL") || !same_count(out.tangents.size(), "TANGENT")
        || !same_count(out.uv0.size(), "TEXCOORD_0") || !same_count(out.uv1.size(), "TEXCOORD_1")
        || !same_count(out.colors.size(), "COLOR_0") || !same_count(out.weights.size(), "WEIGHTS_0")
        || !same_count(out.joints.size() / 4, "JOINTS_0")) {
        return false;
    }

    // Índices (ou sequenciais), triangulados.
    std::vector<u32> raw;
    if (src.indices) {
        raw.resize(src.indices->count);
        for (cgltf_size i = 0; i < src.indices->count; ++i) {
            raw[i] = static_cast<u32>(cgltf_accessor_read_index(src.indices, i));
            if (raw[i] >= vc) {
                fail = {ImportError::InvalidAccessor, "indice aponta alem dos vertices"};
                return false;
            }
        }
    } else {
        raw.resize(vc);
        for (u32 i = 0; i < vc; ++i) raw[i] = i;
    }
    switch (src.type) {
        case cgltf_primitive_type_triangles:
            out.indices = std::move(raw);
            out.indices.resize(out.indices.size() / 3 * 3);
            break;
        case cgltf_primitive_type_triangle_strip:
            for (usize i = 2; i < raw.size(); ++i) {
                if (i & 1) out.indices.insert(out.indices.end(), {raw[i - 1], raw[i - 2], raw[i]});
                else out.indices.insert(out.indices.end(), {raw[i - 2], raw[i - 1], raw[i]});
            }
            break;
        case cgltf_primitive_type_triangle_fan:
            for (usize i = 2; i < raw.size(); ++i) out.indices.insert(out.indices.end(), {raw[i - 1], raw[i], raw[0]});
            break;
        default:
            warnings.push_back("primitiva de pontos/linhas ignorada (o motor desenha triangulos)");
            return true;   // não é erro: só não gera triângulos
    }

    // Alvos de morph.
    for (cgltf_size t = 0; t < src.targets_count; ++t) {
        MorphTarget mt;
        for (cgltf_size a = 0; a < src.targets[t].attributes_count; ++a) {
            const cgltf_attribute& at = src.targets[t].attributes[a];
            if (at.type == cgltf_attribute_type_position) { if (!unpack(at.data, mt.positions, 3, fail, "morph POSITION")) return false; }
            else if (at.type == cgltf_attribute_type_normal) { if (!unpack(at.data, mt.normals, 3, fail, "morph NORMAL")) return false; }
            else if (at.type == cgltf_attribute_type_tangent) { if (!unpack(at.data, mt.tangents, 3, fail, "morph TANGENT")) return false; }
        }
        if (mt.positions.empty()) mt.positions.assign(vc, Vec3{0, 0, 0});
        if (mt.positions.size() != vc) {
            fail = {ImportError::InvalidAccessor, "alvo de morph com contagem diferente"};
            return false;
        }
        out.morphTargets.push_back(std::move(mt));
    }

    out.material = src.material ? static_cast<i32>(src.material - data->materials) : -1;
    if (out.normals.empty() && !out.indices.empty()) flat_normals(out);
    for (Vec3& n : out.normals) {
        const f32 l = n.length();
        n = l > 1e-20f ? n * (1.0f / l) : Vec3{0, 0, 1};
    }
    for (const Vec3& p : out.positions) out.bounds.add(p);
    return true;
}

// --- Imagens ------------------------------------------------------------------------

/// Reduz pela metade (caixa 2×2, em linear para cor sRGB não escurecer não é
/// feito aqui: reduções no import só acontecem acima do teto e o ganho é
/// memória, não fidelidade) até caber no teto.
void downscale_to(Image& img, u32 maxSize) {
    while (maxSize > 0 && (img.width > maxSize || img.height > maxSize) && img.width > 1 && img.height > 1) {
        const u32 w = std::max(1u, img.width / 2), h = std::max(1u, img.height / 2);
        std::vector<u8> out(static_cast<usize>(w) * h * 4);
        for (u32 y = 0; y < h; ++y) {
            for (u32 x = 0; x < w; ++x) {
                for (u32 c = 0; c < 4; ++c) {
                    u32 s = 0;
                    for (u32 dy = 0; dy < 2; ++dy) {
                        for (u32 dx = 0; dx < 2; ++dx) {
                            const u32 sx = std::min(img.width - 1, x * 2 + dx), sy = std::min(img.height - 1, y * 2 + dy);
                            s += img.rgba[(static_cast<usize>(sy) * img.width + sx) * 4 + c];
                        }
                    }
                    out[(static_cast<usize>(y) * w + x) * 4 + c] = static_cast<u8>((s + 2) / 4);
                }
            }
        }
        img.rgba.swap(out);
        img.width = w;
        img.height = h;
    }
}

bool decode_image(const cgltf_image& src, const cgltf_options& opts, const std::string& baseDir, Image& out,
                  Failure& fail) {
    out.name = src.name ? src.name : "";
    std::vector<u8> bytes;
    const u8* data = nullptr;
    usize size = 0;
    void* b64 = nullptr;
    if (src.buffer_view) {
        data = cgltf_buffer_view_data(src.buffer_view);
        size = src.buffer_view->size;
        if (!data) {
            fail = {ImportError::MissingBuffer, "imagem embutida sem buffer"};
            return false;
        }
    } else if (src.uri && std::strncmp(src.uri, "data:", 5) == 0) {
        const char* comma = std::strchr(src.uri, ',');
        if (!comma || std::strstr(src.uri, ";base64,") == nullptr) {
            fail = {ImportError::TextureDecodeFailed, "URI de imagem embutida invalida"};
            return false;
        }
        const usize len = std::strlen(comma + 1);
        const usize decoded = len / 4 * 3 - (len >= 1 && comma[len] == '=') - (len >= 2 && comma[len - 1] == '=');
        if (cgltf_load_buffer_base64(&opts, decoded, comma + 1, &b64) != cgltf_result_success) {
            fail = {ImportError::TextureDecodeFailed, "imagem base64 invalida"};
            return false;
        }
        data = static_cast<const u8*>(b64);
        size = decoded;
    } else if (src.uri) {
        out.uri = uri_decode(src.uri);
        cgltf_size sz = 0;
        void* mem = nullptr;
        const std::string full = baseDir + out.uri;
        if (opts.file.read(&opts.memory, &opts.file, full.c_str(), &sz, &mem) != cgltf_result_success) {
            fail = {ImportError::MissingBuffer, "textura externa ausente: " + out.uri};
            return false;
        }
        bytes.assign(static_cast<u8*>(mem), static_cast<u8*>(mem) + sz);
        opts.file.release(&opts.memory, &opts.file, mem);
        data = bytes.data();
        size = bytes.size();
    } else {
        fail = {ImportError::InvalidFormat, "imagem sem fonte"};
        return false;
    }

    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
    if (b64) std::free(b64);
    if (!px) {
        const char* mime = src.mime_type ? src.mime_type : "";
        const bool ktx = std::strstr(mime, "ktx2") != nullptr;
        fail = {ktx ? ImportError::UnsupportedFeature : ImportError::TextureDecodeFailed,
                std::string("textura '") + (out.uri.empty() ? out.name : out.uri) + "': " +
                (ktx ? "KTX2 sem alternativa PNG/JPEG" : stbi_failure_reason())};
        return false;
    }
    out.width = static_cast<u32>(w);
    out.height = static_cast<u32>(h);
    out.rgba.assign(px, px + static_cast<usize>(w) * h * 4);
    stbi_image_free(px);
    out.hasAlpha = comp == 2 || comp == 4;
    return true;
}

TextureRef texture_ref(const cgltf_texture_view& v, const cgltf_data* data) {
    TextureRef r;
    if (!v.texture) return r;
    // KHR_texture_basisu com alternativa: usa a imagem PNG/JPEG da textura.
    const cgltf_image* img = v.texture->image ? v.texture->image : nullptr;
    if (!img && v.texture->has_basisu) img = v.texture->basisu_image;
    if (!img) return r;
    r.image = static_cast<i32>(img - data->images);
    r.sampler = v.texture->sampler ? static_cast<i32>(v.texture->sampler - data->samplers) : -1;
    r.texCoord = static_cast<u32>(std::max(0, v.texcoord));
    if (v.has_transform) {
        r.offset = Vec2{v.transform.offset[0], v.transform.offset[1]};
        r.scale = Vec2{v.transform.scale[0], v.transform.scale[1]};
        r.rotation = v.transform.rotation;
        if (v.transform.has_texcoord) r.texCoord = static_cast<u32>(std::max(0, v.transform.texcoord));
    }
    return r;
}

Wrap to_wrap(cgltf_int w) noexcept {
    return w == 33071 ? Wrap::Clamp : w == 33648 ? Wrap::Mirror : Wrap::Repeat;
}

Material build_material(const cgltf_material& m, const cgltf_data* data) {
    Material out;
    out.name = m.name ? m.name : "";
    if (m.has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& p = m.pbr_metallic_roughness;
        out.baseColor = Vec4{p.base_color_factor[0], p.base_color_factor[1], p.base_color_factor[2], p.base_color_factor[3]};
        out.metallic = p.metallic_factor;
        out.roughness = p.roughness_factor;
        out.baseColorTex = texture_ref(p.base_color_texture, data);
        out.metallicRoughnessTex = texture_ref(p.metallic_roughness_texture, data);
    } else if (m.has_pbr_specular_glossiness) {
        // Legado (KHR_materials_pbrSpecularGlossiness): aproximação honesta
        // para metal-rugosidade — difusa vira cor base, brilho vira rugosidade.
        const cgltf_pbr_specular_glossiness& sg = m.pbr_specular_glossiness;
        out.baseColor = Vec4{sg.diffuse_factor[0], sg.diffuse_factor[1], sg.diffuse_factor[2], sg.diffuse_factor[3]};
        out.metallic = 0.0f;
        out.roughness = 1.0f - sg.glossiness_factor;
        out.baseColorTex = texture_ref(sg.diffuse_texture, data);
        out.ignoredExtensions.push_back("KHR_materials_pbrSpecularGlossiness (aproximado)");
    }
    out.normalTex = texture_ref(m.normal_texture, data);
    out.normalScale = m.normal_texture.texture ? m.normal_texture.scale : 1.0f;
    out.occlusionTex = texture_ref(m.occlusion_texture, data);
    out.occlusionStrength = m.occlusion_texture.texture ? m.occlusion_texture.scale : 1.0f;
    out.emissiveTex = texture_ref(m.emissive_texture, data);
    out.emissive = Vec3{m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]};
    if (m.has_emissive_strength) out.emissiveStrength = m.emissive_strength.emissive_strength;
    out.alphaMode = m.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask
                  : m.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend : AlphaMode::Opaque;
    out.alphaCutoff = m.alpha_cutoff;
    out.doubleSided = m.double_sided;
    out.unlit = m.unlit;
    if (m.has_ior) out.ior = m.ior.ior;
    if (m.has_clearcoat) {
        out.clearcoat = m.clearcoat.clearcoat_factor;
        out.clearcoatRoughness = m.clearcoat.clearcoat_roughness_factor;
        out.ignoredExtensions.push_back("KHR_materials_clearcoat");
    }
    if (m.has_transmission) {
        out.transmission = m.transmission.transmission_factor;
        out.ignoredExtensions.push_back("KHR_materials_transmission");
    }
    if (m.has_specular) {
        out.specular = m.specular.specular_factor;
        out.specularColor = Vec3{m.specular.specular_color_factor[0], m.specular.specular_color_factor[1],
                                 m.specular.specular_color_factor[2]};
        out.ignoredExtensions.push_back("KHR_materials_specular");
    }
    if (m.has_sheen) {
        out.sheenColor = Vec3{m.sheen.sheen_color_factor[0], m.sheen.sheen_color_factor[1], m.sheen.sheen_color_factor[2]};
        out.sheenRoughness = m.sheen.sheen_roughness_factor;
        out.ignoredExtensions.push_back("KHR_materials_sheen");
    }
    if (m.has_volume) out.ignoredExtensions.push_back("KHR_materials_volume");
    if (m.has_anisotropy) out.ignoredExtensions.push_back("KHR_materials_anisotropy");
    if (m.has_iridescence) out.ignoredExtensions.push_back("KHR_materials_iridescence");
    if (m.has_diffuse_transmission) out.ignoredExtensions.push_back("KHR_materials_diffuse_transmission");
    if (m.has_dispersion) out.ignoredExtensions.push_back("KHR_materials_dispersion");
    return out;
}

void decompose(const f32 m[16], Vec3& t, Quat& r, Vec3& s) {
    t = Vec3{m[12], m[13], m[14]};
    Vec3 c0{m[0], m[1], m[2]}, c1{m[4], m[5], m[6]}, c2{m[8], m[9], m[10]};
    s = Vec3{c0.length(), c1.length(), c2.length()};
    if (c0.cross(c1).dot(c2) < 0.0f) s.x = -s.x;   // espelhado
    if (std::fabs(s.x) > 1e-12f) c0 = c0 * (1.0f / s.x);
    if (std::fabs(s.y) > 1e-12f) c1 = c1 * (1.0f / s.y);
    if (std::fabs(s.z) > 1e-12f) c2 = c2 * (1.0f / s.z);
    const f32 tr = c0.x + c1.y + c2.z;
    if (tr > 0.0f) {
        const f32 k = 0.5f / std::sqrt(tr + 1.0f);
        r = Quat{(c1.z - c2.y) * k, (c2.x - c0.z) * k, (c0.y - c1.x) * k, 0.25f / k};
    } else if (c0.x > c1.y && c0.x > c2.z) {
        const f32 k = 2.0f * std::sqrt(1.0f + c0.x - c1.y - c2.z);
        r = Quat{0.25f * k, (c1.x + c0.y) / k, (c2.x + c0.z) / k, (c1.z - c2.y) / k};
    } else if (c1.y > c2.z) {
        const f32 k = 2.0f * std::sqrt(1.0f + c1.y - c0.x - c2.z);
        r = Quat{(c1.x + c0.y) / k, 0.25f * k, (c2.y + c1.z) / k, (c2.x - c0.z) / k};
    } else {
        const f32 k = 2.0f * std::sqrt(1.0f + c2.z - c0.x - c1.y);
        r = Quat{(c2.x + c0.z) / k, (c2.y + c1.z) / k, 0.25f * k, (c0.y - c1.x) / k};
    }
    r = r.normalized();
}

ImportResult fail_result(ImportError e, std::string detail) {
    ImportResult r;
    r.error = e;
    r.detail = std::move(detail);
    AUREA_LOG_WARN("import 3D: %s (%s)", to_string(e), r.detail.c_str());
    return r;
}

ImportError from_cgltf(cgltf_result r) noexcept {
    switch (r) {
        case cgltf_result_file_not_found: return ImportError::MissingBuffer;
        case cgltf_result_out_of_memory:  return ImportError::OutOfMemory;
        case cgltf_result_data_too_short:
        case cgltf_result_invalid_gltf:   return ImportError::InvalidAccessor;
        default:                          return ImportError::InvalidFormat;
    }
}

/// Etapa comum a todo formato (glTF, FBX, OBJ): otimização das malhas,
/// caixa da cena, validação final e estatísticas.
ImportResult finalize_asset(std::unique_ptr<SceneAsset> asset, const ImportOptions& options, ImportProgress* progress,
                            u32 imagesUsed) {
    // --- Otimização ------------------------------------------------------------
    SceneAsset& A = *asset;
    usize primTotal = 0;
    for (const Mesh& m : A.meshes) primTotal += m.primitives.size();
    const u64 tOpt = monotonic_ns();
    set_phase(progress, ImportPhase::Optimization);
    if (options.optimize) {
        usize done = 0;
        for (Mesh& mesh : A.meshes) {
            for (Primitive& p : mesh.primitives) {
                if (cancelled(progress)) return fail_result(ImportError::Cancelled, "cancelado");
                const bool blend = p.material >= 0 && p.material < static_cast<i32>(A.materials.size())
                                && A.materials[p.material].alphaMode == AlphaMode::Blend;
                if (!blend) optimize_primitive(p);
                set_fraction(progress, static_cast<f32>(++done) / static_cast<f32>(std::max<usize>(1, primTotal)));
            }
        }
    }
    // --- Níveis de detalhe ------------------------------------------------------
    // Malha densa (≥ 512 triângulos) sem skin nem morph: 50 % e 25 % dos
    // triângulos, erro relativo até 2 % do tamanho. Cada nível só entra se
    // de fato encolheu (malha já enxuta não ganha nível inútil).
    if (options.generateLods) {
        for (Mesh& mesh : A.meshes) {
            for (Primitive& p : mesh.primitives) {
                p.lods.clear();
                if (p.skinned() || !p.morphTargets.empty() || p.triangle_count() < 512 || p.positions.empty()) continue;
                usize prev = p.indices.size();
                for (f32 ratio : {0.5f, 0.25f}) {
                    const usize target = static_cast<usize>(static_cast<f32>(p.indices.size()) * ratio) / 3 * 3;
                    std::vector<u32> out(p.indices.size());
                    f32 err = 0.0f;
                    const usize n = meshopt_simplify(out.data(), p.indices.data(), p.indices.size(),
                                                     &p.positions[0].x, p.positions.size(), sizeof(Vec3),
                                                     target, 0.02f, 0, &err);
                    if (n < 3 || n > prev * 8 / 10) break;
                    out.resize(n);
                    prev = n;
                    p.lods.push_back(std::move(out));
                }
            }
        }
    }
    A.stats.optimizeMs = ms_since(tOpt);

    // --- Validação final e estatísticas -----------------------------------------
    const std::vector<Mat4> world = A.rest_world_matrices();
    for (usize i = 0; i < A.nodes.size(); ++i) {
        const i32 m = A.nodes[i].mesh;
        if (m >= 0 && m < static_cast<i32>(A.meshes.size())) A.bounds.add(A.meshes[m].bounds.transformed(world[i]));
    }
    for (const Mesh& mesh : A.meshes) {
        for (const Primitive& p : mesh.primitives) {
            A.stats.vertices += p.vertex_count();
            A.stats.triangles += p.triangle_count();
            A.stats.morphTargets += static_cast<u32>(p.morphTargets.size());
            A.stats.geometryBytes += p.positions.size() * sizeof(Vec3) * 2 + p.indices.size() * sizeof(u32);
            ++A.stats.primitives;
            if (p.generatedNormals) {
                const std::string w = "malha '" + mesh.name + "' sem normais: normais planas geradas";
                if (std::find(A.warnings.begin(), A.warnings.end(), w) == A.warnings.end()) A.warnings.push_back(w);
            }
        }
    }
    if (A.stats.triangles == 0 || !A.bounds.valid()) {
        return fail_result(ImportError::NoGeometry, "nenhuma malha visivel na cena");
    }
    A.stats.nodes = static_cast<u32>(A.nodes.size());
    A.stats.meshes = static_cast<u32>(A.meshes.size());
    A.stats.materials = static_cast<u32>(A.materials.size());
    A.stats.images = imagesUsed;
    A.stats.animations = static_cast<u32>(A.animations.size());
    A.stats.skins = static_cast<u32>(A.skins.size());

    ImportResult res;
    res.asset = std::move(asset);
    AUREA_LOG_INFO("import 3D: %u malhas, %u triangulos, %u materiais, %u imagens, %u animacoes (%.0f+%.0f+%.0f+%.0f ms)",
                   A.stats.meshes, A.stats.triangles, A.stats.materials, A.stats.images, A.stats.animations,
                   A.stats.parseMs, A.stats.geometryMs, A.stats.imagesMs, A.stats.optimizeMs);
    return res;
}

} // namespace

std::vector<Mat4> SceneAsset::rest_world_matrices() const {
    std::vector<Mat4> world(nodes.size());
    std::vector<i32> stack(roots.begin(), roots.end());
    std::vector<u8> seen(nodes.size(), 0);
    // Pais antes de filhos, a partir das raízes.
    std::vector<std::pair<i32, i32>> todo;
    for (i32 r : roots) todo.push_back({r, -1});
    while (!todo.empty()) {
        auto [n, parent] = todo.back();
        todo.pop_back();
        if (n < 0 || n >= static_cast<i32>(nodes.size()) || seen[n]) continue;
        seen[n] = 1;
        world[n] = parent >= 0 ? world[parent] * nodes[n].local_matrix() : nodes[n].local_matrix();
        for (i32 c : nodes[n].children) todo.push_back({c, n});
    }
    return world;
}

ImportResult import_gltf_memory(const u8* bytes, usize size, const std::string& baseDir,
                                const ImportOptions& options, ImportProgress* progress) {
    const u64 tParse = monotonic_ns();
    set_phase(progress, ImportPhase::Parsing);
    if (!bytes || size < 12) return fail_result(ImportError::InvalidFormat, "arquivo vazio ou curto demais");

    ReaderCtx rctx{&options, baseDir};
    cgltf_options opts{};
    opts.file.read = &file_read;
    opts.file.release = &file_release;
    opts.file.user_data = &rctx;

    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse(&opts, bytes, size, &data);
    if (r != cgltf_result_success) {
        return fail_result(r == cgltf_result_unknown_format ? ImportError::InvalidFormat : from_cgltf(r),
                           "nao e um glTF/GLB valido");
    }
    struct Guard { cgltf_data* d; ~Guard() { cgltf_free(d); } } guard{data};

    // Extensões obrigatórias: sem suporte = não finge.
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i) {
        const std::string ext = data->extensions_required[i];
        if (ext == "KHR_draco_mesh_compression" || ext == "EXT_meshopt_compression") {
            return fail_result(ImportError::UnsupportedCompression, ext + " (ainda nao suportado)");
        }
        static const char* known[] = {"KHR_texture_transform", "KHR_materials_unlit", "KHR_mesh_quantization",
                                      "KHR_texture_basisu", "KHR_materials_emissive_strength", "KHR_lights_punctual"};
        bool ok = false;
        for (const char* k : known) ok = ok || ext == k;
        if (!ok) return fail_result(ImportError::UnsupportedFeature, "extensao obrigatoria nao suportada: " + ext);
    }

    const std::string loadPath = baseDir + "x.gltf";   // cgltf compõe URIs relativas a partir deste caminho
    r = cgltf_load_buffers(&opts, data, loadPath.c_str());
    if (r != cgltf_result_success) {
        return fail_result(from_cgltf(r), r == cgltf_result_file_not_found ? "arquivo .bin do modelo ausente"
                                                                           : "buffers do modelo ilegiveis");
    }
    r = cgltf_validate(data);
    if (r != cgltf_result_success) return fail_result(ImportError::InvalidAccessor, "acessores ou indices invalidos");
    if (cancelled(progress)) return fail_result(ImportError::Cancelled, "cancelado");

    auto asset = std::make_unique<SceneAsset>();
    SceneAsset& A = *asset;
    A.stats.parseMs = ms_since(tParse);

    // --- Nós -------------------------------------------------------------------
    A.nodes.resize(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const cgltf_node& n = data->nodes[i];
        Node& o = A.nodes[i];
        o.name = n.name ? n.name : "";
        o.parent = n.parent ? static_cast<i32>(n.parent - data->nodes) : -1;
        for (cgltf_size c = 0; c < n.children_count; ++c) o.children.push_back(static_cast<i32>(n.children[c] - data->nodes));
        if (n.has_matrix) {
            decompose(n.matrix, o.translation, o.rotation, o.scale);
        } else {
            if (n.has_translation) o.translation = Vec3{n.translation[0], n.translation[1], n.translation[2]};
            if (n.has_rotation) o.rotation = Quat{n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]}.normalized();
            if (n.has_scale) o.scale = Vec3{n.scale[0], n.scale[1], n.scale[2]};
        }
        o.mesh = n.mesh ? static_cast<i32>(n.mesh - data->meshes) : -1;
        o.skin = n.skin ? static_cast<i32>(n.skin - data->skins) : -1;
        o.camera = n.camera ? static_cast<i32>(n.camera - data->cameras) : -1;
        o.light = n.light ? static_cast<i32>(n.light - data->lights) : -1;
        o.morphWeights.assign(n.weights, n.weights + n.weights_count);
    }
    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) A.roots.push_back(static_cast<i32>(scene->nodes[i] - data->nodes));
    } else {
        for (i32 i = 0; i < static_cast<i32>(A.nodes.size()); ++i) if (A.nodes[i].parent < 0) A.roots.push_back(i);
    }

    // --- Geometria -----------------------------------------------------------
    const u64 tGeo = monotonic_ns();
    set_phase(progress, ImportPhase::Geometry);
    Failure fail;
    A.meshes.resize(data->meshes_count);
    usize primTotal = 0, primDone = 0;
    for (cgltf_size m = 0; m < data->meshes_count; ++m) primTotal += data->meshes[m].primitives_count;
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        const cgltf_mesh& src = data->meshes[m];
        Mesh& mesh = A.meshes[m];
        mesh.name = src.name ? src.name : "";
        mesh.morphWeights.assign(src.weights, src.weights + src.weights_count);
        for (cgltf_size p = 0; p < src.primitives_count; ++p) {
            if (cancelled(progress)) return fail_result(ImportError::Cancelled, "cancelado");
            Primitive prim;
            if (!build_primitive(src.primitives[p], data, prim, fail, A.warnings)) {
                return fail_result(fail.error, (mesh.name.empty() ? std::string("malha ") + std::to_string(m) : mesh.name)
                                                   + ": " + fail.detail);
            }
            if (!prim.indices.empty()) {
                mesh.bounds.add(prim.bounds);
                mesh.primitives.push_back(std::move(prim));
            }
            set_fraction(progress, static_cast<f32>(++primDone) / static_cast<f32>(std::max<usize>(1, primTotal)));
        }
        if (mesh.morphWeights.empty() && !mesh.primitives.empty() && !mesh.primitives[0].morphTargets.empty()) {
            mesh.morphWeights.assign(mesh.primitives[0].morphTargets.size(), 0.0f);
        }
    }
    A.stats.geometryMs = ms_since(tGeo);

    // --- Materiais e imagens ---------------------------------------------------
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        A.materials.push_back(build_material(data->materials[i], data));
        for (const std::string& e : A.materials.back().ignoredExtensions) {
            const std::string w = "material '" + A.materials.back().name + "': " + e + " lido, ainda nao renderizado";
            if (std::find(A.warnings.begin(), A.warnings.end(), w) == A.warnings.end()) A.warnings.push_back(w);
        }
    }
    for (cgltf_size i = 0; i < data->samplers_count; ++i) {
        const cgltf_sampler& s = data->samplers[i];
        Sampler o;
        o.wrapS = to_wrap(s.wrap_s);
        o.wrapT = to_wrap(s.wrap_t);
        o.mag = s.mag_filter == 9728 ? Filter::Nearest : Filter::Linear;
        o.min = (s.min_filter == 9728 || s.min_filter == 9984 || s.min_filter == 9986) ? Filter::Nearest : Filter::Linear;
        o.mipmaps = s.min_filter == 0 || s.min_filter >= 9984;
        A.samplers.push_back(o);
    }

    // Só decodifica imagens que algum material usa (as outras custariam RAM à toa).
    std::vector<u8> used(data->images_count, 0);
    for (const Material& m : A.materials) {
        for (const TextureRef* t : {&m.baseColorTex, &m.metallicRoughnessTex, &m.normalTex, &m.occlusionTex, &m.emissiveTex}) {
            if (t->valid() && t->image < static_cast<i32>(used.size())) used[t->image] = 1;
        }
    }
    const u64 tImg = monotonic_ns();
    set_phase(progress, ImportPhase::Textures);
    A.images.resize(data->images_count);
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        if (cancelled(progress)) return fail_result(ImportError::Cancelled, "cancelado");
        if (!used[i]) {
            A.images[i].name = data->images[i].name ? data->images[i].name : "";
            continue;
        }
        if (!decode_image(data->images[i], opts, baseDir, A.images[i], fail)) return fail_result(fail.error, fail.detail);
        if (options.maxTextureSize && (A.images[i].width > options.maxTextureSize || A.images[i].height > options.maxTextureSize)) {
            const u32 w0 = A.images[i].width, h0 = A.images[i].height;
            downscale_to(A.images[i], options.maxTextureSize);
            A.warnings.push_back("textura " + std::to_string(w0) + "x" + std::to_string(h0) + " reduzida para " +
                                 std::to_string(A.images[i].width) + "x" + std::to_string(A.images[i].height));
        }
        A.stats.imageBytes += A.images[i].rgba.size();
        set_fraction(progress, static_cast<f32>(i + 1) / static_cast<f32>(data->images_count));
    }
    A.stats.imagesMs = ms_since(tImg);

    // Tangentes: só onde há mapa de normal (é o único consumidor).
    for (Mesh& mesh : A.meshes) {
        for (Primitive& p : mesh.primitives) {
            const bool needs = p.material >= 0 && p.material < static_cast<i32>(A.materials.size())
                            && A.materials[p.material].normalTex.valid();
            if (!needs || !p.tangents.empty()) continue;
            const u32 set = A.materials[p.material].normalTex.texCoord;
            const std::vector<Vec2>& uv = set == 1 ? p.uv1 : p.uv0;
            if (uv.size() != p.positions.size()) continue;
            generate_tangents(p, uv);
        }
    }

    // --- Câmeras, luzes, skins, animações --------------------------------------
    for (cgltf_size i = 0; i < data->cameras_count; ++i) {
        const cgltf_camera& c = data->cameras[i];
        CameraInfo o;
        o.name = c.name ? c.name : "";
        if (c.type == cgltf_camera_type_orthographic) {
            o.perspective = false;
            o.xmag = c.data.orthographic.xmag;
            o.ymag = c.data.orthographic.ymag;
            o.znear = c.data.orthographic.znear;
            o.zfar = c.data.orthographic.zfar;
        } else {
            o.yfov = c.data.perspective.yfov;
            o.aspect = c.data.perspective.has_aspect_ratio ? c.data.perspective.aspect_ratio : 0.0f;
            o.znear = c.data.perspective.znear;
            o.zfar = c.data.perspective.has_zfar ? c.data.perspective.zfar : 0.0f;
        }
        A.cameras.push_back(o);
    }
    for (cgltf_size i = 0; i < data->lights_count; ++i) {
        const cgltf_light& l = data->lights[i];
        LightInfo o;
        o.name = l.name ? l.name : "";
        o.type = l.type == cgltf_light_type_point ? LightType::Point
               : l.type == cgltf_light_type_spot ? LightType::Spot : LightType::Directional;
        o.color = Vec3{l.color[0], l.color[1], l.color[2]};
        o.intensity = l.intensity;
        o.range = l.range;
        o.innerCone = l.spot_inner_cone_angle;
        o.outerCone = l.spot_outer_cone_angle;
        A.lights.push_back(o);
    }
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        const cgltf_skin& s = data->skins[i];
        Skin o;
        o.name = s.name ? s.name : "";
        for (cgltf_size j = 0; j < s.joints_count; ++j) o.joints.push_back(static_cast<i32>(s.joints[j] - data->nodes));
        o.skeleton = s.skeleton ? static_cast<i32>(s.skeleton - data->nodes) : -1;
        o.inverseBind.assign(s.joints_count, Mat4::identity());
        if (s.inverse_bind_matrices) {
            if (s.inverse_bind_matrices->count < s.joints_count
                || cgltf_accessor_unpack_floats(s.inverse_bind_matrices, &o.inverseBind[0].col[0].x, s.joints_count * 16)
                       != s.joints_count * 16) {
                return fail_result(ImportError::InvalidAccessor, "matrizes de bind do esqueleto invalidas");
            }
        }
        A.skins.push_back(std::move(o));
    }
    for (cgltf_size i = 0; i < data->animations_count; ++i) {
        const cgltf_animation& a = data->animations[i];
        Animation o;
        o.name = a.name && *a.name ? a.name : ("Animacao " + std::to_string(i + 1));
        for (cgltf_size s = 0; s < a.samplers_count; ++s) {
            const cgltf_animation_sampler& as = a.samplers[s];
            AnimSampler os;
            os.interpolation = as.interpolation == cgltf_interpolation_type_step ? AnimInterp::Step
                             : as.interpolation == cgltf_interpolation_type_cubic_spline ? AnimInterp::CubicSpline
                                                                                           : AnimInterp::Linear;
            if (!as.input || !as.output) return fail_result(ImportError::InvalidAccessor, "animacao sem entrada/saida");
            os.times.resize(as.input->count);
            if (cgltf_accessor_unpack_floats(as.input, os.times.data(), os.times.size()) != os.times.size()) {
                return fail_result(ImportError::InvalidAccessor, "tempos de animacao invalidos");
            }
            const cgltf_size comps = cgltf_num_components(as.output->type);
            os.values.resize(as.output->count * comps);
            if (cgltf_accessor_unpack_floats(as.output, os.values.data(), os.values.size()) != os.values.size()) {
                return fail_result(ImportError::InvalidAccessor, "valores de animacao invalidos");
            }
            os.components = static_cast<u32>(comps);
            if (!os.times.empty()) o.duration = std::max(o.duration, os.times.back());
            o.samplers.push_back(std::move(os));
        }
        for (cgltf_size c = 0; c < a.channels_count; ++c) {
            const cgltf_animation_channel& ch = a.channels[c];
            if (!ch.target_node || !ch.sampler) continue;
            AnimChannel oc;
            oc.node = static_cast<i32>(ch.target_node - data->nodes);
            oc.sampler = static_cast<u32>(ch.sampler - a.samplers);
            switch (ch.target_path) {
                case cgltf_animation_path_type_translation: oc.path = AnimPath::Translation; break;
                case cgltf_animation_path_type_rotation:    oc.path = AnimPath::Rotation; break;
                case cgltf_animation_path_type_scale:       oc.path = AnimPath::Scale; break;
                case cgltf_animation_path_type_weights:     oc.path = AnimPath::Weights; break;
                default: continue;
            }
            // Pesos de morph: componentes = alvos por chave.
            if (oc.path == AnimPath::Weights) {
                AnimSampler& s = o.samplers[oc.sampler];
                const usize keys = s.times.size() * (s.interpolation == AnimInterp::CubicSpline ? 3 : 1);
                if (keys) s.components = static_cast<u32>(s.values.size() / keys);
            }
            o.channels.push_back(oc);
        }
        A.animations.push_back(std::move(o));
    }

    return finalize_asset(std::move(asset), options, progress,
                          static_cast<u32>(std::count(used.begin(), used.end(), 1)));
}

ImportResult import_gltf_file(const std::string& path, const ImportOptions& options, ImportProgress* progress) {
    std::vector<u8> bytes;
    if (!read_file(path, bytes)) return fail_result(ImportError::FileNotFound, path);
    ImportResult r = import_gltf_memory(bytes.data(), bytes.size(), dir_of(path), options, progress);
    if (r.ok()) {
        const usize slash = path.find_last_of("/\\");
        r.asset->sourceName = slash == std::string::npos ? path : path.substr(slash + 1);
    }
    return r;
}

#include "UfbxImport.inl"

} // namespace aurea::scene3d
