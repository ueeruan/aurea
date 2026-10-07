// =============================================================================
//  Aurea / scene3d / GltfImporter.cpp
//
//  glTF 2.0 / GLB → SceneAsset, com cgltf (parser) e meshoptimizer (ordem de
//  vértices). O parser só existe aqui dentro: nenhuma estrutura cgltf sai
//  deste arquivo.
// =============================================================================
#include "aurea/scene3d/Importer.hpp"
#include "aurea/scene3d/Environment.hpp"
#include "aurea/scene3d/ModelBudget.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <cstddef>
#include <limits>
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
#define STBI_ONLY_HDR
// Texturas de FBX (embutidas ou ao lado) vêm muito em TGA/BMP/PSD (Maya,
// 3ds Max, pacotes de jogo): sem esses decodificadores a textura "sumia".
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_PSD
#define STBI_FAILURE_USERMSG
#include "stb_image.h"

#include "meshoptimizer.h"
#include "ufbx.h"
#include "basisu_transcoder.h"
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

bool read_file(const std::string& path, std::vector<u8>& out, u64 limit = 0, bool* exceeded = nullptr) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) { std::fclose(f); return false; }
    if (limit && static_cast<u64>(size) > limit) {
        if (exceeded) *exceeded = true;
        std::fclose(f); return false;
    }
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
struct ParseBudget {
    u64 limit = 0, held = 0;
    bool exceeded = false;
    struct alignas(std::max_align_t) Header { usize size; };
    static void* allocate(void* user, cgltf_size size) {
        auto& budget = *static_cast<ParseBudget*>(user);
        if (size > std::numeric_limits<usize>::max() - sizeof(Header)
            || (budget.limit && size + sizeof(Header) > budget.limit - budget.held)) {
            budget.exceeded = true; return nullptr;
        }
        auto* memory = static_cast<Header*>(std::malloc(size + sizeof(Header)));
        if (!memory) return nullptr;
        memory->size = size + sizeof(Header); budget.held += memory->size;
        return memory + 1;
    }
    static void release(void* user, void* data) {
        if (!data) return;
        auto& budget = *static_cast<ParseBudget*>(user);
        auto* memory = static_cast<Header*>(data) - 1;
        budget.held -= memory->size; std::free(memory);
    }
};
struct ReaderCtx {
    const ImportOptions* options = nullptr;
    std::string baseDir;
    ParseBudget* budget = nullptr;
};

cgltf_result file_read(const cgltf_memory_options* memory, const cgltf_file_options* fo, const char* path,
                       cgltf_size* size, void** data) {
    auto* ctx = static_cast<ReaderCtx*>(fo->user_data);
    // The temporary vector and retained cgltf buffer coexist during the copy.
    const u64 limit = ctx->budget->limit ? (ctx->budget->limit - ctx->budget->held) / 2 : 0;
    if (ctx->budget->limit && (!limit || *size > limit)) {
        ctx->budget->exceeded = true; return cgltf_result_out_of_memory;
    }
    std::vector<u8> bytes;
    bool ok = false;
    if (ctx->options->reader) {
        // cgltf passa o caminho já composto com o diretório base; o leitor da
        // plataforma quer só a parte relativa.
        std::string rel = path;
        if (!ctx->baseDir.empty() && rel.rfind(ctx->baseDir, 0) == 0) rel = rel.substr(ctx->baseDir.size());
        ok = ctx->options->reader(rel.c_str(), bytes, ctx->options->readerUser);
    } else {
        ok = read_file(path, bytes, limit, &ctx->budget->exceeded);
    }
    if (ctx->budget->exceeded || (limit && bytes.size() > limit)) {
        ctx->budget->exceeded = true; return cgltf_result_out_of_memory;
    }
    if (!ok) return cgltf_result_file_not_found;
    void* mem = memory->alloc_func(memory->user_data, bytes.empty() ? 1 : bytes.size());
    if (!mem) return cgltf_result_out_of_memory;
    if (!bytes.empty()) std::memcpy(mem, bytes.data(), bytes.size());
    *size = bytes.size();
    *data = mem;
    return cgltf_result_success;
}

void file_release(const cgltf_memory_options* memory, const cgltf_file_options*, void* data) {
    memory->free_func(memory->user_data, data);
}

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

void compact_vertices(Primitive& p);

/// Reordena índices (cache de vértices, overdraw) e vértices (localidade de
/// leitura). Não muda a aparência: os mesmos triângulos, em outra ordem.
void optimize_primitive(Primitive& p) {
    const usize vc = p.positions.size();
    if (vc == 0 || p.indices.empty()) return;
    // Transparência depende da ordem dos triângulos do autor; não mexe.
    std::vector<u32> tmp(p.indices.size());
    meshopt_optimizeVertexCache(tmp.data(), p.indices.data(), p.indices.size(), vc);
    meshopt_optimizeOverdraw(p.indices.data(), tmp.data(), tmp.size(), &p.positions[0].x, vc, sizeof(Vec3), 1.05f);
    compact_vertices(p);
}

u32 pack_rgba8(Vec4 c) noexcept {
    auto q = [](f32 v) { return static_cast<u32>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return q(c.x) | (q(c.y) << 8) | (q(c.z) << 16) | (q(c.w) << 24);
}

bool build_primitive(const cgltf_primitive& src, const cgltf_data* data, Primitive& out, Failure& fail,
                     std::vector<std::string>& warnings, f32 keep = 1.0f, f32 simplifyError = 0.01f) {
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
    // Orçamento: simplifica AQUI, parte por parte, antes de a próxima ser
    // desempacotada — o pico é uma parte cheia, não o modelo inteiro. E antes
    // das normais planas (que triplicam os vértices e soltam as arestas).
    if (keep < 1.0f && out.indices.size() >= 3 * 64) simplify_primitive(out, keep, simplifyError, true);
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

/// Fator (potência de 2) que leva w × h para dentro de `maxSide` — o mesmo
/// número de metades que `downscale_to` faria.
u32 reduce_factor(u32 w, u32 h, u32 maxSide) noexcept {
    u32 f = 1;
    while (maxSide > 0 && (w / f > maxSide || h / f > maxSide) && w / f > 1 && h / f > 1 && f < (1u << 15)) f *= 2;
    return f;
}

/// Pixels RGBA8 do decodificador → Image JÁ no teto, numa passada (caixa
/// f × f). Antes a imagem inteira era copiada em resolução cheia e só depois
/// reduzida: uma 8K custava 256 MB do stb + 256 MB da cópia ao mesmo tempo.
void adopt_pixels(const u8* px, u32 w, u32 h, u32 maxSide, Image& out) {
    const u32 f = reduce_factor(w, h, maxSide);
    if (f == 1) {
        out.rgba.assign(px, px + static_cast<usize>(w) * h * 4);
        out.width = w;
        out.height = h;
        return;
    }
    const u32 ow = std::max(1u, w / f), oh = std::max(1u, h / f);
    out.rgba.assign(static_cast<usize>(ow) * oh * 4, 0);
    for (u32 y = 0; y < oh; ++y) {
        for (u32 x = 0; x < ow; ++x) {
            u32 s[4] = {0, 0, 0, 0};
            u32 n = 0;
            for (u32 dy = 0; dy < f; ++dy) {
                const u32 sy = std::min(h - 1, y * f + dy);
                const u8* row = px + static_cast<usize>(sy) * w * 4;
                for (u32 dx = 0; dx < f; ++dx) {
                    const u8* p = row + static_cast<usize>(std::min(w - 1, x * f + dx)) * 4;
                    s[0] += p[0]; s[1] += p[1]; s[2] += p[2]; s[3] += p[3];
                    ++n;
                }
            }
            u8* o = &out.rgba[(static_cast<usize>(y) * ow + x) * 4];
            for (u32 c = 0; c < 4; ++c) o[c] = static_cast<u8>((s[c] + n / 2) / n);
        }
    }
    out.width = ow;
    out.height = oh;
}

/// Pico transitório de decodificar uma imagem w × h: a saída RGBA8 mais os
/// planos internos do decodificador (JPEG guarda os componentes; PNG de 16
/// bits guarda o dobro). 8 bytes por pixel cobre os dois.
constexpr u64 kDecodeBytesPerPixel = 8;

enum class Decode : u8 { Ok, Failed, TooBig };

/// Decodifica PNG/JPEG/TGA/BMP/PSD já reduzido ao teto. `transientLimit`
/// (0 = sem limite): a imagem cujo decode passaria disso nem começa — o
/// modelo entra sem aquele mapa, com aviso, em vez de derrubar o app.
Decode decode_capped(const u8* data, usize size, u32 maxSide, u64 transientLimit, Image& out, u32& w0, u32& h0) {
    if (!data || size == 0 || size > static_cast<usize>(INT32_MAX)) return Decode::Failed;
    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(data, static_cast<int>(size), &w, &h, &comp) || w <= 0 || h <= 0) return Decode::Failed;
    w0 = static_cast<u32>(w);
    h0 = static_cast<u32>(h);
    if (transientLimit != ~0ull && static_cast<u64>(w) * static_cast<u64>(h) * kDecodeBytesPerPixel > transientLimit) return Decode::TooBig;
    stbi_uc* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
    if (!px) return Decode::Failed;
    adopt_pixels(px, static_cast<u32>(w), static_cast<u32>(h), maxSide, out);
    stbi_image_free(px);
    out.hasAlpha = comp == 2 || comp == 4;
    return Decode::Ok;
}

/// Mantém só os vértices que os índices usam, na ordem de leitura
/// (meshoptimizer), com TODOS os atributos — skin e morph juntos.
void compact_vertices(Primitive& p) {
    const usize vc = p.positions.size();
    if (vc == 0 || p.indices.empty()) return;
    std::vector<u32> remap(vc);
    const usize unique = meshopt_optimizeVertexFetchRemap(remap.data(), p.indices.data(), p.indices.size(), vc);
    meshopt_remapIndexBuffer(p.indices.data(), p.indices.data(), p.indices.size(), remap.data());
    auto apply = [&](auto& v) {
        using T = typename std::decay_t<decltype(v)>::value_type;
        if (v.size() != vc) return;
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
    if (p.joints.size() == vc * 4) {
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

std::string mb_text(u64 bytes) { return std::to_string((bytes + (1u << 19)) >> 20) + " MB"; }

/// Bytes que uma primitiva segura na CPU (orçamento do import).
u64 primitive_bytes(const Primitive& p) {
    u64 n = p.positions.size() * sizeof(Vec3) + p.normals.size() * sizeof(Vec3) + p.tangents.size() * sizeof(Vec4)
          + (p.uv0.size() + p.uv1.size()) * sizeof(Vec2) + p.colors.size() * 4 + p.joints.size() * 2
          + p.weights.size() * sizeof(Vec4) + p.indices.size() * 4;
    for (const auto& l : p.lods) n += l.size() * 4;
    for (const MorphTarget& t : p.morphTargets)
        n += (t.positions.size() + t.normals.size() + t.tangents.size()) * sizeof(Vec3);
    return n;
}

/// Teto do decode de UMA imagem agora: o que sobra do orçamento depois do que
/// o import já segura (no máximo ¾ dele). 0 = não cabe nenhuma (pular).
/// Sem orçamento: sem limite (~0).
u64 decode_room(u64 budget, u64 held) {
    if (budget == 0) return ~0ull;
    const u64 room = budget > held ? budget - held : 0;
    return std::min(room, budget * 3 / 4);
}

} // namespace

void downscale_image(Image& img, u32 maxSide) { downscale_to(img, maxSide); }

u32 simplify_primitive(Primitive& p, f32 ratio, f32 error, bool sloppy, bool lockBorder) {
    const usize ic = p.indices.size(), vc = p.positions.size();
    if (ic < 3 || vc == 0 || !(ratio < 1.0f)) return static_cast<u32>(ic / 3);
    const usize target = std::max<usize>(1, static_cast<usize>(static_cast<f64>(ic / 3) * std::max(0.0f, ratio))) * 3;
    // Normais e UV pesam no erro: a costura de UV e as quinas duras ficam.
    const bool hasN = p.normals.size() == vc, hasUV = p.uv0.size() == vc;
    const usize ac = (hasN ? 3 : 0) + (hasUV ? 2 : 0);
    std::vector<f32> attrs(ac * vc);
    f32 weights[5] = {};
    if (ac) {
        usize k = 0;
        if (hasN) { weights[k++] = 0.5f; weights[k++] = 0.5f; weights[k++] = 0.5f; }
        if (hasUV) { weights[k++] = 1.0f; weights[k++] = 1.0f; }
        for (usize v = 0; v < vc; ++v) {
            f32* a = &attrs[v * ac];
            if (hasN) { *a++ = p.normals[v].x; *a++ = p.normals[v].y; *a++ = p.normals[v].z; }
            if (hasUV) { *a++ = p.uv0[v].x; *a++ = p.uv0[v].y; }
        }
    }
    const unsigned options = lockBorder ? meshopt_SimplifyLockBorder : 0u;
    std::vector<u32> out(ic);
    auto run = [&](f32 err) {
        f32 got = 0.0f;
        return ac ? meshopt_simplifyWithAttributes(out.data(), p.indices.data(), ic, &p.positions[0].x, vc, sizeof(Vec3),
                                                   attrs.data(), ac * sizeof(f32), weights, ac, nullptr, target, err,
                                                   options, &got)
                  : meshopt_simplify(out.data(), p.indices.data(), ic, &p.positions[0].x, vc, sizeof(Vec3), target, err,
                                     options, &got);
    };
    usize n = run(std::max(error, 1e-4f));
    // O erro pedido segurou longe do alvo: solta o erro (a topologia vira o limite).
    if (n > target * 3 / 2) n = run(1.0f);
    // Ainda longe (malha toda em pedaços soltos, sopa de triângulos): o modo
    // que ignora topologia. Não nas partes de um modelo lido em pedaços — ele
    // não respeita a borda travada e abriria frestas entre os pedaços.
    if (sloppy && !lockBorder && n > target * 3 / 2) {
        f32 got = 0.0f;
        const usize s = meshopt_simplifySloppy(out.data(), p.indices.data(), ic, &p.positions[0].x, vc, sizeof(Vec3),
                                               target, 1.0f, &got);
        if (s >= 3) n = s;
    }
    if (n < 3) return static_cast<u32>(ic / 3);   // nunca apaga a malha
    out.resize(n);
    p.indices.swap(out);
    p.lods.clear();
    compact_vertices(p);
    p.bounds = Aabb{};
    for (const Vec3& v : p.positions) p.bounds.add(v);
    return static_cast<u32>(n / 3);
}

bool is_ktx2(const u8* data, usize size) noexcept {
    static const u8 id[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};   // identificador do KTX2
    return data && size > sizeof(id) && std::memcmp(data, id, sizeof(id)) == 0;
}

bool decode_ktx2(const u8* data, usize size, Image& out) {
    static const bool ready = [] { basist::basisu_transcoder_init(); return true; }();
    (void)ready;
    basist::ktx2_transcoder t;
    if (!t.init(data, static_cast<u32>(size)) || t.get_faces() != 1 || !t.start_transcoding()) return false;
    const u32 w = t.get_width(), h = t.get_height();
    if (w == 0 || h == 0 || static_cast<u64>(w) * h > 8192ull * 8192ull) return false;
    std::vector<u8> rgba(static_cast<usize>(w) * h * 4);
    // Nível 0, camada 0, face 0; RGBA32 = pixels (sem blocos), largura = w.
    if (!t.transcode_image_level(0, 0, 0, rgba.data(), w * h, basist::transcoder_texture_format::cTFRGBA32, 0, w, h)) {
        return false;
    }
    out.width = w;
    out.height = h;
    out.rgba = std::move(rgba);
    out.hasAlpha = t.get_has_alpha();
    return true;
}

namespace {

bool decode_image(const cgltf_image& src, const cgltf_options& opts, const std::string& baseDir, Image& out,
                  Failure& fail, u32 maxSide = 0, u64 transientLimit = ~0ull, u32* w0 = nullptr, u32* h0 = nullptr,
                  bool* skipped = nullptr) {
    out.name = src.name ? src.name : "";
    std::vector<u8> bytes;
    const u8* data = nullptr;
    usize size = 0;
    void* b64 = nullptr;
    struct Base64Guard {
        const cgltf_options& options;
        void*& data;
        ~Base64Guard() { if (data) options.memory.free_func(options.memory.user_data, data); }
    } base64Guard{opts, b64};
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
        if (len < 4 || len % 4 != 0) {
            fail = {ImportError::TextureDecodeFailed, "imagem base64 invalida"};
            return false;
        }
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

    // KTX2 (KHR_texture_basisu): transcodificado para RGBA no import.
    if (is_ktx2(data, size)) {
        const bool ok = decode_ktx2(data, size, out);
        if (!ok) fail = {ImportError::TextureDecodeFailed, "textura KTX2 '" + (out.uri.empty() ? out.name : out.uri) + "' ilegivel"};
        if (ok) {
            if (w0) *w0 = out.width;
            if (h0) *h0 = out.height;
            if (maxSide) downscale_to(out, maxSide);
        }
        return ok;
    }
    u32 ow = 0, oh = 0;
    const Decode d = decode_capped(data, size, maxSide, transientLimit, out, ow, oh);
    if (w0) *w0 = ow;
    if (h0) *h0 = oh;
    if (d == Decode::TooBig) {
        // Grande demais para decodificar neste aparelho: o modelo entra sem
        // este mapa (aviso), em vez de o decode derrubar o app.
        if (skipped) *skipped = true;
        out.rgba.clear();
        out.width = out.height = 0;
        return true;
    }
    if (d != Decode::Ok) {
        const char* mime = src.mime_type ? src.mime_type : "";
        const bool ktx = std::strstr(mime, "ktx2") != nullptr;
        const char* why = stbi_failure_reason();
        fail = {ktx ? ImportError::UnsupportedFeature : ImportError::TextureDecodeFailed,
                std::string("textura '") + (out.uri.empty() ? out.name : out.uri) + "': " +
                (ktx ? "KTX2 sem alternativa PNG/JPEG" : (why ? why : "formato ilegivel"))};
        return false;
    }
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

/// KHR_materials_pbrSpecularGlossiness: o que a conversão por pixel precisa
/// depois do decode (a textura especular/brilho e os fatores originais).
struct SpecGlossSource {
    bool active = false;
    TextureRef tex;                 ///< specularGlossinessTexture (RGB especular sRGB, A brilho)
    TextureRef diffuseTex;          ///< diffuseTexture original
    Vec4 diffuse{1.0f, 1.0f, 1.0f, 1.0f};
    Vec3 specular{1.0f, 1.0f, 1.0f};
    f32 glossiness = 1.0f;
};

constexpr f32 kDielectricSpecular = 0.04f;

f32 perceived_brightness(f32 r, f32 g, f32 b) noexcept {
    return std::sqrt(0.299f * r * r + 0.587f * g * g + 0.114f * b * b);
}

/// Metalicidade que reproduz o par difusa/especular (conversão Khronos/Babylon
/// de especular-brilho para metal-rugosidade).
f32 solve_metallic(f32 diffuse, f32 specular, f32 oneMinusSpecularStrength) noexcept {
    if (specular < kDielectricSpecular) return 0.0f;
    const f32 a = kDielectricSpecular;
    const f32 b = diffuse * oneMinusSpecularStrength / (1.0f - a) + specular - 2.0f * a;
    const f32 c = a - specular;
    const f32 d = std::max(0.0f, b * b - 4.0f * a * c);
    return std::clamp((-b + std::sqrt(d)) / (2.0f * a), 0.0f, 1.0f);
}

/// Difusa + especular (lineares) -> cor base (linear) e metal.
void spec_gloss_to_metal(const f32 diffuse[3], const f32 specular[3], f32 base[3], f32& metallic) noexcept {
    const f32 specStrength = std::max({specular[0], specular[1], specular[2]});
    const f32 oneMinus = 1.0f - specStrength;
    metallic = solve_metallic(perceived_brightness(diffuse[0], diffuse[1], diffuse[2]),
                              perceived_brightness(specular[0], specular[1], specular[2]), oneMinus);
    const f32 a = kDielectricSpecular;
    const f32 t = metallic * metallic;
    for (int k = 0; k < 3; ++k) {
        const f32 fromDiffuse = diffuse[k] * oneMinus / (1.0f - a) / std::max(1.0f - metallic, 1e-4f);
        const f32 fromSpecular = (specular[k] - a * (1.0f - metallic)) / std::max(metallic, 1e-4f);
        base[k] = std::clamp(fromDiffuse + (fromSpecular - fromDiffuse) * t, 0.0f, 1.0f);
    }
}

Material build_material(const cgltf_material& m, const cgltf_data* data, SpecGlossSource* sgOut = nullptr) {
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
        // Legado (KHR_materials_pbrSpecularGlossiness, comum nos exports antigos
        // do Sketchfab): convertido para metal-rugosidade. Os fatores aqui; a
        // textura especular/brilho vira mapa de metal/rugosidade (e, com a
        // difusa do mesmo tamanho, cor base) por pixel depois do decode.
        const cgltf_pbr_specular_glossiness& sg = m.pbr_specular_glossiness;
        const f32 diffuse[3] = {sg.diffuse_factor[0], sg.diffuse_factor[1], sg.diffuse_factor[2]};
        const f32 specular[3] = {sg.specular_factor[0], sg.specular_factor[1], sg.specular_factor[2]};
        f32 base[3] = {diffuse[0], diffuse[1], diffuse[2]};
        f32 metallic = 0.0f;
        const TextureRef diffuseTex = texture_ref(sg.diffuse_texture, data);
        // Com textura difusa, o fator multiplica a textura: a conversão da cor
        // fica para o pixel (aqui só o metal estimado com a difusa a meio-tom).
        if (!diffuseTex.valid()) spec_gloss_to_metal(diffuse, specular, base, metallic);
        else metallic = solve_metallic(perceived_brightness(diffuse[0], diffuse[1], diffuse[2]) * 0.5f,
                                       perceived_brightness(specular[0], specular[1], specular[2]),
                                       1.0f - std::max({specular[0], specular[1], specular[2]}));
        out.baseColor = Vec4{base[0], base[1], base[2], sg.diffuse_factor[3]};
        out.metallic = metallic;
        out.roughness = std::clamp(1.0f - sg.glossiness_factor, 0.0f, 1.0f);
        out.baseColorTex = diffuseTex;
        if (sgOut) {
            sgOut->active = true;
            sgOut->tex = texture_ref(sg.specular_glossiness_texture, data);
            sgOut->diffuseTex = diffuseTex;
            sgOut->diffuse = Vec4{sg.diffuse_factor[0], sg.diffuse_factor[1], sg.diffuse_factor[2], sg.diffuse_factor[3]};
            sgOut->specular = Vec3{specular[0], specular[1], specular[2]};
            sgOut->glossiness = sg.glossiness_factor;
        }
    }
    // EXT_texture_webp sem PNG/JPEG de reserva: este decodificador não lê WebP;
    // o mapa fica de fora com aviso (o modelo entra mesmo assim).
    for (const cgltf_texture_view* v : {&m.pbr_metallic_roughness.base_color_texture,
                                        &m.pbr_metallic_roughness.metallic_roughness_texture,
                                        &m.pbr_specular_glossiness.diffuse_texture,
                                        &m.pbr_specular_glossiness.specular_glossiness_texture,
                                        &m.normal_texture, &m.occlusion_texture, &m.emissive_texture}) {
        if (v->texture && !v->texture->image && !v->texture->has_basisu && v->texture->has_webp) {
            out.ignoredExtensions.push_back("EXT_texture_webp sem PNG/JPEG (textura ignorada)");
            break;
        }
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

/// Contagens de um glTF pelo JSON (acessores), sem tocar nos buffers.
struct GltfCounts {
    u64 triangles = 0, vertices = 0, largestPart = 0;
};

GltfCounts count_gltf(const cgltf_data* data) {
    GltfCounts c;
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
            const cgltf_primitive& prim = data->meshes[m].primitives[p];
            const cgltf_accessor* pos = nullptr;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a)
                if (prim.attributes[a].type == cgltf_attribute_type_position) pos = prim.attributes[a].data;
            const u64 verts = pos ? pos->count : 0;
            const u64 n = prim.indices ? prim.indices->count : verts;
            u64 tris = 0;
            if (prim.type == cgltf_primitive_type_triangles) tris = n / 3;
            else if (prim.type == cgltf_primitive_type_triangle_strip || prim.type == cgltf_primitive_type_triangle_fan) tris = n >= 3 ? n - 2 : 0;
            c.triangles += tris;
            c.vertices += verts;
            c.largestPart = std::max(c.largestPart, tris);
        }
    }
    return c;
}

/// Custo da geometria de um glTF: a maior primitiva desempacotada inteira
/// (índices crus + triangulados + atributos em float) antes de simplificar.
ModelCost cost_from_counts(const GltfCounts& g) {
    ModelCost c;
    c.valid = true;
    c.triangles = g.triangles;
    c.vertices = g.vertices;
    c.largestPartTriangles = g.largestPart;
    const f64 vpt = g.triangles ? std::clamp(static_cast<f64>(g.vertices) / static_cast<f64>(g.triangles), 0.3, 3.0) : 1.0;
    c.partBytesPerTriangle = static_cast<u32>(24.0 + vpt * 84.0);
    return c;
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
    if (A.stats.sourceTriangles == 0) A.stats.sourceTriangles = A.stats.triangles;
    // Conferência final do orçamento com o que de fato ficou (a estimativa de
    // antes era pelas contagens do arquivo): o que passar não vai para a GPU.
    if (options.memoryBudget) {
        ModelCost kept;
        kept.valid = true;
        kept.triangles = A.stats.triangles;
        kept.vertices = A.stats.vertices;
        u64 imageBytes = 0;
        for (const Image& img : A.images) imageBytes += img.pixels().size();
        kept.texturePixels = imageBytes / 4;
        kept.textures = static_cast<u32>(A.images.size());
        const u64 resident = estimate_import_peak(kept, ModelBudget{});
        if (resident > options.memoryBudget) {
            return fail_result(ImportError::TooHeavy, "o modelo otimizado ainda precisa de ~" + mb_text(resident) +
                                                          "; este aparelho aguenta " + mb_text(options.memoryBudget));
        }
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

/// Otimização, níveis de detalhe, caixa e estatísticas para uma cena montada
/// em código (texto 3D): o mesmo acabamento de um arquivo importado.
ImportResult finalize_scene_asset(std::unique_ptr<SceneAsset> asset, const ImportOptions& options) {
    return finalize_asset(std::move(asset), options, nullptr, 0);
}

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

namespace {

f32 srgb_to_linear_u8(u8 v) noexcept {
    static const auto lut = [] {
        std::array<f32, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const f32 c = static_cast<f32>(i) / 255.0f;
            t[static_cast<usize>(i)] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return lut[v];
}

u8 linear_to_srgb_u8(f32 c) noexcept {
    c = std::clamp(c, 0.0f, 1.0f);
    const f32 s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return static_cast<u8>(std::lround(std::clamp(s, 0.0f, 1.0f) * 255.0f));
}

/// Especular/brilho por pixel -> mapa de metal (B) e rugosidade (G); com a
/// difusa do mesmo tamanho, também a cor base. As imagens novas entram no fim
/// de A.images; as originais que só a conversão usava são liberadas.
void convert_spec_gloss_textures(SceneAsset& A, const std::vector<SpecGlossSource>& specGloss) {
    std::vector<u32> refs(A.images.size(), 0);
    auto count = [&](const TextureRef& t) {
        if (t.valid() && t.image < static_cast<i32>(refs.size())) ++refs[static_cast<usize>(t.image)];
    };
    for (const Material& m : A.materials)
        for (const TextureRef* t : {&m.baseColorTex, &m.metallicRoughnessTex, &m.normalTex, &m.occlusionTex, &m.emissiveTex}) count(*t);
    for (const SpecGlossSource& sg : specGloss) if (sg.active) count(sg.tex);

    for (usize mi = 0; mi < specGloss.size() && mi < A.materials.size(); ++mi) {
        const SpecGlossSource& sg = specGloss[mi];
        if (!sg.active || !sg.tex.valid() || sg.tex.image >= static_cast<i32>(refs.size())) continue;
        Material& mat = A.materials[mi];
        const usize si = static_cast<usize>(sg.tex.image);
        if (A.images[si].rgba.empty() || !A.images[si].width || !A.images[si].height) continue;
        const u32 w = A.images[si].width, h = A.images[si].height;
        const usize px = static_cast<usize>(w) * h;
        if (A.images[si].rgba.size() < px * 4) continue;
        // A difusa só entra no pixel quando casa 1:1 com a especular (mesmo
        // tamanho, UV e transformação); senão fica a textura difusa como cor base.
        i32 di = -1;
        if (sg.diffuseTex.valid() && sg.diffuseTex.image < static_cast<i32>(refs.size())) {
            const Image& d = A.images[static_cast<usize>(sg.diffuseTex.image)];
            const bool same = d.width == w && d.height == h && d.rgba.size() >= px * 4
                           && sg.diffuseTex.texCoord == sg.tex.texCoord
                           && sg.diffuseTex.offset.x == sg.tex.offset.x && sg.diffuseTex.offset.y == sg.tex.offset.y
                           && sg.diffuseTex.scale.x == sg.tex.scale.x && sg.diffuseTex.scale.y == sg.tex.scale.y
                           && sg.diffuseTex.rotation == sg.tex.rotation;
            if (same) di = sg.diffuseTex.image;
        }
        Image mr;
        mr.name = A.images[si].name + " (metal/rugosidade)";
        mr.width = w;
        mr.height = h;
        mr.rgba.assign(px * 4, 255);
        Image base;
        if (di >= 0) {
            base.name = A.images[static_cast<usize>(di)].name + " (cor base)";
            base.width = w;
            base.height = h;
            base.rgba.resize(px * 4);
            base.hasAlpha = A.images[static_cast<usize>(di)].hasAlpha;
        }
        {
            const std::vector<u8>& S = A.images[si].rgba;
            const std::vector<u8>* D = di >= 0 ? &A.images[static_cast<usize>(di)].rgba : nullptr;
            // Sem difusa por pixel: o brilho da difusa vem do fator (meio-tom se
            // ainda houver textura difusa, que não dá para casar aqui).
            const f32 k = sg.diffuseTex.valid() ? 0.5f : 1.0f;
            const f32 flatDiffuse[3] = {sg.diffuse.x * k, sg.diffuse.y * k, sg.diffuse.z * k};
            for (usize p = 0; p < px; ++p) {
                const u8* s = &S[p * 4];
                const f32 spec[3] = {srgb_to_linear_u8(s[0]) * sg.specular.x, srgb_to_linear_u8(s[1]) * sg.specular.y,
                                     srgb_to_linear_u8(s[2]) * sg.specular.z};
                f32 diff[3] = {flatDiffuse[0], flatDiffuse[1], flatDiffuse[2]};
                if (D) {
                    const u8* d = &(*D)[p * 4];
                    diff[0] = srgb_to_linear_u8(d[0]) * sg.diffuse.x;
                    diff[1] = srgb_to_linear_u8(d[1]) * sg.diffuse.y;
                    diff[2] = srgb_to_linear_u8(d[2]) * sg.diffuse.z;
                }
                f32 rgb[3];
                f32 metallic = 0.0f;
                spec_gloss_to_metal(diff, spec, rgb, metallic);
                const f32 gloss = (static_cast<f32>(s[3]) / 255.0f) * sg.glossiness;
                mr.rgba[p * 4 + 1] = static_cast<u8>(std::lround(std::clamp(1.0f - gloss, 0.0f, 1.0f) * 255.0f));
                mr.rgba[p * 4 + 2] = static_cast<u8>(std::lround(metallic * 255.0f));
                if (D) {
                    base.rgba[p * 4 + 0] = linear_to_srgb_u8(rgb[0]);
                    base.rgba[p * 4 + 1] = linear_to_srgb_u8(rgb[1]);
                    base.rgba[p * 4 + 2] = linear_to_srgb_u8(rgb[2]);
                    base.rgba[p * 4 + 3] = (*D)[p * 4 + 3];
                }
            }
        }
        A.stats.imageBytes += mr.rgba.size() + base.rgba.size();
        TextureRef mrRef = sg.tex;
        mrRef.image = static_cast<i32>(A.images.size());
        A.images.push_back(std::move(mr));
        mat.metallicRoughnessTex = mrRef;
        mat.metallic = 1.0f;    // o mapa já traz o valor final
        mat.roughness = 1.0f;
        auto release = [&](usize i) {
            if (--refs[i] != 0) return;
            A.stats.imageBytes -= std::min<u64>(A.stats.imageBytes, A.images[i].rgba.size());
            A.images[i].rgba.clear();
            A.images[i].rgba.shrink_to_fit();
        };
        if (di >= 0) {
            TextureRef baseRef = sg.diffuseTex;
            baseRef.image = static_cast<i32>(A.images.size());
            A.images.push_back(std::move(base));
            release(static_cast<usize>(di));
            mat.baseColorTex = baseRef;
            mat.baseColor = Vec4{1.0f, 1.0f, 1.0f, sg.diffuse.w};
        }
        release(si);
    }
}

} // namespace

ImportResult import_gltf_memory(const u8* bytes, usize size, const std::string& baseDir,
                                const ImportOptions& options, ImportProgress* progress) {
    const u64 tParse = monotonic_ns();
    set_phase(progress, ImportPhase::Parsing);
    if (!bytes || size < 12) return fail_result(ImportError::InvalidFormat, "arquivo vazio ou curto demais");
    if (cancelled(progress)) return fail_result(ImportError::Cancelled, "cancelado");
    if (options.memoryBudget && size >= options.memoryBudget)
        return fail_result(ImportError::TooHeavy, "arquivo 3D excede a memoria disponivel para importar");
    ParseBudget budget{options.memoryBudget ? options.memoryBudget - size : 0};
    ReaderCtx rctx{&options, baseDir, &budget};
    cgltf_options opts{};
    opts.memory.alloc_func = &ParseBudget::allocate;
    opts.memory.free_func = &ParseBudget::release;
    opts.memory.user_data = &budget;
    opts.file.read = &file_read;
    opts.file.release = &file_release;
    opts.file.user_data = &rctx;

    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse(&opts, bytes, size, &data);
    if (r != cgltf_result_success) {
        if (budget.exceeded) return fail_result(ImportError::TooHeavy, "estrutura 3D excede a memoria disponivel para importar");
        return fail_result(r == cgltf_result_unknown_format ? ImportError::InvalidFormat : from_cgltf(r),
                           "nao e um glTF/GLB valido");
    }
    struct Guard { cgltf_data* d; ~Guard() { cgltf_free(d); } } guard{data};

    // Extensões obrigatórias: compressão de malha sem suporte = não finge; as
    // outras viram aviso.
    std::vector<std::string> requiredWarnings;
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i) {
        const std::string ext = data->extensions_required[i];
        if (ext == "KHR_draco_mesh_compression" || ext == "EXT_meshopt_compression") {
            return fail_result(ImportError::UnsupportedCompression, ext + " (ainda nao suportado)");
        }
        static const char* known[] = {"KHR_texture_transform", "KHR_materials_unlit", "KHR_mesh_quantization",
                                      "KHR_texture_basisu", "KHR_materials_emissive_strength", "KHR_lights_punctual",
                                      "KHR_materials_pbrSpecularGlossiness", "EXT_texture_webp"};
        bool ok = false;
        for (const char* k : known) ok = ok || ext == k;
        // Sketchfab e outros exportadores marcam extensões de material como
        // obrigatórias: a geometria continua legível, então o modelo entra
        // com aviso em vez de recusar o arquivo inteiro.
        if (!ok) requiredWarnings.push_back("extensao obrigatoria nao suportada (ignorada): " + ext);
    }

    const std::string loadPath = baseDir + "x.gltf";   // cgltf compõe URIs relativas a partir deste caminho
    r = cgltf_load_buffers(&opts, data, loadPath.c_str());
    if (r != cgltf_result_success) {
        if (budget.exceeded) return fail_result(ImportError::TooHeavy, "buffers 3D excedem a memoria disponivel para importar");
        return fail_result(from_cgltf(r), r == cgltf_result_file_not_found ? "arquivo .bin do modelo ausente"
                                                                           : "buffers do modelo ilegiveis");
    }
    r = cgltf_validate(data);
    if (r != cgltf_result_success) return fail_result(ImportError::InvalidAccessor, "acessores ou indices invalidos");
    if (cancelled(progress)) return fail_result(ImportError::Cancelled, "cancelado");

    auto asset = std::make_unique<SceneAsset>();
    SceneAsset& A = *asset;
    A.stats.parseMs = ms_since(tParse);
    A.warnings = std::move(requiredWarnings);

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

    // --- Orçamento: contagens do JSON, antes de desempacotar nada -----------------
    const GltfCounts counts = count_gltf(data);
    u64 heldBytes = size;   // o arquivo lido + buffers externos (cópias próprias)
    for (cgltf_size i = 0; i < data->buffers_count; ++i)
        if (data->buffers[i].uri) heldBytes += data->buffers[i].size;
    A.stats.sourceTriangles = static_cast<u32>(std::min<u64>(counts.triangles, UINT32_MAX));
    f32 keep = 1.0f;
    if (options.maxTriangles && counts.triangles > options.maxTriangles)
        keep = static_cast<f32>(static_cast<f64>(options.maxTriangles) / static_cast<f64>(counts.triangles));
    if (options.memoryBudget) {
        ModelCost c = cost_from_counts(counts);
        c.parseBytes = heldBytes;
        c.fileBytes = c.parseBytes;
        // Texturas: no máximo o teto cada (o decode confere uma por uma).
        c.textures = static_cast<u32>(data->images_count);
        c.texturePixels = static_cast<u64>(c.textures) * options.maxTextureSize * options.maxTextureSize;
        ModelBudget b;
        b.memoryBytes = options.memoryBudget;
        b.maxTriangles = options.maxTriangles;
        b.maxTextureSize = options.maxTextureSize;
        const u64 peak = estimate_import_peak(c, b);
        if (peak > options.memoryBudget) {
            return fail_result(ImportError::TooHeavy, std::to_string(counts.triangles) + " triangulos precisam de ~" +
                                                          mb_text(peak) + "; este aparelho aguenta " +
                                                          mb_text(options.memoryBudget));
        }
    }

    // --- Geometria -----------------------------------------------------------
    const u64 tGeo = monotonic_ns();
    set_phase(progress, keep < 1.0f ? ImportPhase::Simplifying : ImportPhase::Geometry);
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
            if (!build_primitive(src.primitives[p], data, prim, fail, A.warnings, keep, options.simplifyError)) {
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
    std::vector<SpecGlossSource> specGloss(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        A.materials.push_back(build_material(data->materials[i], data, &specGloss[i]));
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
    for (const SpecGlossSource& sg : specGloss)
        if (sg.active && sg.tex.valid() && sg.tex.image < static_cast<i32>(used.size())) used[sg.tex.image] = 1;
    const u64 tImg = monotonic_ns();
    set_phase(progress, ImportPhase::Textures);
    A.images.resize(data->images_count);
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        if (cancelled(progress)) return fail_result(ImportError::Cancelled, "cancelado");
        if (!used[i]) {
            A.images[i].name = data->images[i].name ? data->images[i].name : "";
            continue;
        }
        // Uma imagem por vez, já reduzida no decode; com orçamento, a que
        // passaria do teto (decode ou soma guardada) é pulada com aviso.
        u32 w0 = 0, h0 = 0;
        bool skipped = false;
        // O que sobra do teto com o arquivo, a geometria e as texturas já guardadas.
        u64 held = heldBytes + A.stats.imageBytes;
        for (const Mesh& m : A.meshes) for (const Primitive& p : m.primitives) held += primitive_bytes(p);
        const u64 transient = decode_room(options.memoryBudget, held);
        if (options.memoryBudget && A.stats.imageBytes * 7 / 3 > options.memoryBudget / 2) {
            skipped = true;
        } else if (!decode_image(data->images[i], opts, baseDir, A.images[i], fail, options.maxTextureSize, transient,
                                 &w0, &h0, &skipped)) {
            if (budget.exceeded) return fail_result(ImportError::TooHeavy, "texturas 3D excedem a memoria disponivel para importar");
            // Textura ausente ou ilegível (WebP, KTX2 sem reserva, arquivo
            // corrompido): o modelo entra sem ela, com aviso. Antes o import
            // inteiro falhava e o preview nem abria. A ausente vai para a lista
            // de "Importar texturas".
            A.images[i].rgba.clear();
            A.images[i].width = A.images[i].height = 0;
            if (fail.error == ImportError::MissingBuffer && !A.images[i].uri.empty()) {
                const std::string& u = A.images[i].uri;
                const usize slash = u.find_last_of("/\\");
                const std::string name = slash == std::string::npos ? u : u.substr(slash + 1);
                if (std::find(A.missingTextures.begin(), A.missingTextures.end(), name) == A.missingTextures.end())
                    A.missingTextures.push_back(name);
            }
            A.warnings.push_back("textura ignorada: " + fail.detail);
            fail = {};
            continue;
        }
        if (skipped) {
            A.images[i].rgba.clear();
            A.images[i].width = A.images[i].height = 0;
            ++A.stats.texturesSkipped;
            A.warnings.push_back("textura " + (w0 ? std::to_string(w0) + "x" + std::to_string(h0) + " " : std::string()) +
                                 "grande demais para a memoria deste aparelho: ignorada");
        } else if (w0 && (A.images[i].width != w0 || A.images[i].height != h0)) {
            ++A.stats.texturesReduced;
            A.warnings.push_back("textura " + std::to_string(w0) + "x" + std::to_string(h0) + " reduzida para " +
                                 std::to_string(A.images[i].width) + "x" + std::to_string(A.images[i].height));
        }
        A.stats.imageBytes += A.images[i].rgba.size();
        set_fraction(progress, static_cast<f32>(i + 1) / static_cast<f32>(data->images_count));
    }
    convert_spec_gloss_textures(A, specGloss);
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
    bool exceeded = false;
    if (!read_file(path, bytes, options.memoryBudget, &exceeded))
        return fail_result(exceeded ? ImportError::TooHeavy : ImportError::FileNotFound,
            exceeded ? "arquivo 3D excede a memoria disponivel para importar" : path);
    ImportResult r = import_gltf_memory(bytes.data(), bytes.size(), dir_of(path), options, progress);
    if (r.ok()) {
        const usize slash = path.find_last_of("/\\");
        r.asset->sourceName = slash == std::string::npos ? path : path.substr(slash + 1);
    }
    return r;
}

// O leitor de panoramas mora em HdriImage.cpp (Radiance, EXR, zip, redução).
std::shared_ptr<HdriPixels> decode_hdri(const u8* bytes, usize size, f32 ldrGain) noexcept {
    return decode_hdri_detailed(bytes, size, ldrGain).pixels;
}

#include "UfbxImport.inl"

// =============================================================================
// Custo antes de importar (ModelBudget.hpp): só cabeçalhos e contagens.
// =============================================================================
namespace {

std::string lower_extension(const std::string& path) {
    const usize dot = path.find_last_of('.');
    const usize slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    return lower_ascii(path.substr(dot + 1));
}

u64 file_bytes(const std::string& path) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(std::filesystem::u8path(path), ec);
    return ec ? 0 : static_cast<u64>(n);
}

/// Até `max` bytes a partir de `offset` (o cabeçalho de uma imagem cabe nos
/// primeiros 256 KB, mesmo um JPEG com EXIF grande).
bool read_range(const std::string& path, u64 offset, usize max, std::vector<u8>& out) {
    out.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#if defined(_MSC_VER)
    const bool sought = _fseeki64(f, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    const bool sought = fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
    if (!sought) { std::fclose(f); return false; }
    out.resize(max);
    const usize n = std::fread(out.data(), 1, max, f);
    std::fclose(f);
    out.resize(n);
    return n > 0;
}

constexpr usize kImageHead = 256 * 1024;

void add_texture(ModelCost& c, u32 w, u32 h) {
    const u64 px = static_cast<u64>(w) * h;
    ++c.textures;
    c.texturePixels += px;
    c.largestTexturePixels = std::max(c.largestTexturePixels, px);
    c.largestTextureSide = std::max({c.largestTextureSide, w, h});
}

bool header_dims(const std::vector<u8>& head, u32& w, u32& h) {
    int x = 0, y = 0, comp = 0;
    if (head.empty() || !stbi_info_from_memory(head.data(), static_cast<int>(head.size()), &x, &y, &comp) || x <= 0 || y <= 0) return false;
    w = static_cast<u32>(x);
    h = static_cast<u32>(y);
    return true;
}

/// Sem cabeçalho legível (base64, KTX2): conta como uma 2K, o tamanho comum.
void add_unknown_texture(ModelCost& c) { add_texture(c, 2048, 2048); }

/// Imagens soltas na pasta do modelo (FBX/OBJ: as texturas vão juntas para a
/// pasta dele). Limite superior: entra até a que o modelo não usa.
void scan_folder_images(const std::string& dir, ModelCost& c) {
    if (dir.empty()) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<u8> head;
    for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const auto u8 = it->path().u8string();
        const std::string full(u8.begin(), u8.end());
        const std::string ext = lower_extension(full);
        if (ext != "png" && ext != "jpg" && ext != "jpeg" && ext != "tga" && ext != "bmp" && ext != "psd") continue;
        u32 w = 0, h = 0;
        if (read_range(full, 0, kImageHead, head) && header_dims(head, w, h)) add_texture(c, w, h);
    }
}

/// Imagens que algum material usa (as outras nem são decodificadas).
std::vector<u8> gltf_used_images(const cgltf_data* data) {
    std::vector<u8> used(data->images_count, 0);
    auto mark = [&](const cgltf_texture_view& v) {
        if (!v.texture) return;
        const cgltf_image* img = v.texture->image ? v.texture->image : (v.texture->has_basisu ? v.texture->basisu_image : nullptr);
        if (img) used[static_cast<usize>(img - data->images)] = 1;
    };
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        mark(m.pbr_metallic_roughness.base_color_texture);
        mark(m.pbr_metallic_roughness.metallic_roughness_texture);
        mark(m.pbr_specular_glossiness.diffuse_texture);
        mark(m.normal_texture);
        mark(m.occlusion_texture);
        mark(m.emissive_texture);
    }
    return used;
}

ModelCost estimate_gltf_cost(const std::string& path, bool glb) {
    ModelCost c;
    const u64 size = file_bytes(path);
    if (size < 12) return c;
    // Inspection is a cheap preflight, never another unrestricted import.
    // Unknown counts retain the heavy-model warning; the actual importer will
    // decide whether the requested quality fits its current aggregate quota.
    const auto boundedUnknown = [size] {
        ModelCost unknown; unknown.valid = true; unknown.exact = false;
        unknown.fileBytes = unknown.parseBytes = size;
        return unknown;
    };
    constexpr u64 jsonLimit = 16ull << 20;
    std::vector<u8> json;
    u64 binOffset = 0;   // início dos dados do chunk BIN no arquivo (GLB)
    if (glb) {
        std::vector<u8> head;
        if (!read_range(path, 0, 20, head) || head.size() < 20 || std::memcmp(head.data(), "glTF", 4) != 0) return c;
        u32 jsonLen = 0;
        std::memcpy(&jsonLen, head.data() + 12, 4);
        if (jsonLen == 0 || jsonLen > size) return c;
        if (jsonLen > jsonLimit) return boundedUnknown();
        if (!read_range(path, 20, jsonLen, json) || json.size() != jsonLen) return c;
        binOffset = 20ull + jsonLen + 8ull;
    } else {
        if (size > jsonLimit) return boundedUnknown();
        if (!read_file(path, json, jsonLimit)) return c;
    }
    ParseBudget budget; budget.limit = 32ull << 20;
    cgltf_options opts{};
    opts.memory.alloc_func = &ParseBudget::allocate;
    opts.memory.free_func = &ParseBudget::release;
    opts.memory.user_data = &budget;
    cgltf_data* data = nullptr;
    if (cgltf_parse(&opts, json.data(), json.size(), &data) != cgltf_result_success || !data)
        return budget.exceeded ? boundedUnknown() : c;
    struct Guard { cgltf_data* d; ~Guard() { cgltf_free(d); } } guard{data};
    c = cost_from_counts(count_gltf(data));
    c.fileBytes = size;
    c.parseBytes = glb ? size : size;
    const std::string baseDir = dir_of(path);
    for (cgltf_size i = 0; i < data->buffers_count; ++i) {
        const cgltf_buffer& b = data->buffers[i];
        if (!glb || b.uri) { c.parseBytes += b.size; c.fileBytes += b.size; }
    }
    const std::vector<u8> used = gltf_used_images(data);
    std::vector<u8> head;
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        if (!used[i]) continue;
        const cgltf_image& img = data->images[i];
        u32 w = 0, h = 0;
        bool known = false;
        if (img.buffer_view && img.buffer_view->buffer) {
            const cgltf_buffer& b = *img.buffer_view->buffer;
            if (!b.uri && glb) {
                known = read_range(path, binOffset + img.buffer_view->offset, std::min<usize>(kImageHead, img.buffer_view->size), head)
                     && header_dims(head, w, h);
            } else if (b.uri && std::strncmp(b.uri, "data:", 5) != 0) {
                known = read_range(baseDir + uri_decode(b.uri), img.buffer_view->offset,
                                   std::min<usize>(kImageHead, img.buffer_view->size), head) && header_dims(head, w, h);
            }
        } else if (img.uri && std::strncmp(img.uri, "data:", 5) != 0) {
            known = read_range(baseDir + uri_decode(img.uri), 0, kImageHead, head) && header_dims(head, w, h);
        }
        if (known) add_texture(c, w, h); else add_unknown_texture(c);
    }
    c.exact = true;
    return c;
}

/// OBJ: contagem das linhas (v / vt / vn / f), em blocos de 1 MB.
ModelCost estimate_obj_cost(const std::string& path) {
    ModelCost c;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return c;
    u64 v = 0, vt = 0, vn = 0, tris = 0, corners = 0, faces = 0, part = 0, largest = 0;
    std::vector<char> buf(1 << 20);
    std::string line;
    auto finish_line = [&]() {
        usize i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i + 1 < line.size() && line[i] == 'v') {
            const char n = line[i + 1];
            if (n == ' ' || n == '\t') ++v;
            else if (n == 't') ++vt;
            else if (n == 'n') ++vn;
        } else if (i + 1 < line.size() && line[i] == 'f' && (line[i + 1] == ' ' || line[i + 1] == '\t')) {
            u64 tokens = 0;
            bool in = false;
            for (usize k = i + 1; k < line.size(); ++k) {
                const bool ws = line[k] == ' ' || line[k] == '\t' || line[k] == '\r';
                if (!ws && !in) ++tokens;
                in = !ws;
            }
            if (tokens >= 3) { tris += tokens - 2; part += tokens - 2; corners += tokens; ++faces; }
        } else if (line.compare(i, 6, "usemtl") == 0 || (i + 1 < line.size() && (line[i] == 'o' || line[i] == 'g') && line[i + 1] == ' ')) {
            largest = std::max(largest, part);
            part = 0;
        }
        line.clear();
    };
    usize n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
        for (usize k = 0; k < n; ++k) {
            if (buf[k] == '\n') finish_line();
            else if (line.size() < 4096) line.push_back(buf[k]);
        }
    }
    std::fclose(f);
    finish_line();
    largest = std::max(largest, part);
    c.valid = tris > 0;
    c.exact = true;
    c.fileBytes = file_bytes(path);
    c.triangles = tris;
    c.vertices = std::max(v, vt);
    c.largestPartTriangles = std::min<u64>(largest, kUfbxChunkTriangles);
    c.partBytesPerTriangle = kUfbxPartBytesPerTriangle;
    // A ufbx guarda posições/UV/normais em double e um índice por canto.
    c.parseBytes = v * 24 + vt * 16 + vn * 24 + corners * 16 + faces * 8;
    scan_folder_images(dir_of(path), c);
    return c;
}

/// FBX: as contagens só existem depois de ler e descomprimir a geometria —
/// justamente o que pode não caber. Estimativa pelo tamanho (binário guarda
/// ~48 bytes por triângulo comprimido; ASCII ~150), marcada como inexata.
ModelCost estimate_fbx_cost(const std::string& path) {
    ModelCost c;
    const u64 size = file_bytes(path);
    if (size == 0) return c;
    std::vector<u8> head;
    const bool binary = read_range(path, 0, 21, head) && head.size() >= 18 && std::memcmp(head.data(), "Kaydara FBX Binary", 18) == 0;
    c.valid = true;
    c.exact = false;
    c.fileBytes = size;
    c.triangles = size / (binary ? 48 : 150);
    c.vertices = c.triangles * 6 / 10;
    c.largestPartTriangles = std::min<u64>(c.triangles, kUfbxChunkTriangles);
    c.partBytesPerTriangle = kUfbxPartBytesPerTriangle;
    c.parseBytes = binary ? size * 3 : size + size / 4;
    scan_folder_images(dir_of(path), c);
    return c;
}

} // namespace

ModelCost estimate_model_cost(const std::string& path) noexcept {
    // Só cabeçalhos: o JSON de um glTF (até 256 MB) e 256 KB por imagem.
    const std::string ext = lower_extension(path);
    if (ext == "obj") return estimate_obj_cost(path);
    if (ext == "fbx") return estimate_fbx_cost(path);
    // glTF/GLB (e arquivo sem extensão: o import também tenta glTF).
    std::vector<u8> magic;
    const bool glb = read_range(path, 0, 4, magic) && magic.size() == 4 && std::memcmp(magic.data(), "glTF", 4) == 0;
    return estimate_gltf_cost(path, glb);
}

} // namespace aurea::scene3d
