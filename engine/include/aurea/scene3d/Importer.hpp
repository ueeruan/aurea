// =============================================================================
//  Aurea / scene3d / Importer.hpp
//
//  Arquivo 3D → SceneAsset, em etapas que a UI enxerga:
//
//    Parse → Validate → Resolve Resources → Build Scene → Optimize
//
//  Roda FORA da thread de UI (JobSystem). Cancelável entre etapas e dentro das
//  longas (imagens, primitivas). Sucesso só quando o asset é representável:
//  geometria válida, buffers presentes, texturas decodificadas. Qualquer
//  falha devolve o motivo específico (ImportError) e um detalhe legível.
// =============================================================================
#pragma once

#include "aurea/scene3d/SceneAsset.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace aurea::scene3d {

enum class ImportPhase : u8 { Queued = 0, Parsing, Geometry, Textures, Optimization, GpuUpload, Complete };

[[nodiscard]] constexpr const char* to_string(ImportPhase p) noexcept {
    switch (p) {
        case ImportPhase::Queued:       return "na fila";
        case ImportPhase::Parsing:      return "lendo o arquivo";
        case ImportPhase::Geometry:     return "geometria";
        case ImportPhase::Textures:     return "texturas";
        case ImportPhase::Optimization: return "otimizando";
        case ImportPhase::GpuUpload:    return "enviando para a GPU";
        case ImportPhase::Complete:     return "pronto";
    }
    return "?";
}

/// Progresso compartilhado entre a thread do import e quem acompanha.
struct ImportProgress {
    std::atomic<ImportPhase> phase{ImportPhase::Queued};
    std::atomic<f32> fraction{0.0f};     ///< 0..1 dentro da etapa
    std::atomic<bool> cancel{false};
};

/// Lê um recurso externo referenciado pelo arquivo (o .bin ou a textura de
/// um .gltf). `uri` já vem relativo ao arquivo principal. A plataforma troca
/// o leitor quando o arquivo vem de `content://` ou de um sandbox.
using ResourceReader = bool (*)(const char* uri, std::vector<u8>& out, void* user);

struct ImportOptions {
    ResourceReader reader = nullptr;   ///< nulo = arquivos ao lado do principal
    void* readerUser = nullptr;
    bool optimize = true;              ///< ordem de vértices/índices (meshoptimizer)
    bool generateLods = true;          ///< níveis de detalhe (50 % e 25 %) para malhas densas
    /// Maior lado aceito para textura. Maior que isso é reduzido NO IMPORT
    /// (o celular não amostra 8K de qualquer forma). 0 = sem limite.
    u32 maxTextureSize = 4096;
};

struct ImportResult {
    ImportError error = ImportError::None;
    std::string detail;                ///< frase curta para a UI e o log
    std::unique_ptr<SceneAsset> asset;

    [[nodiscard]] bool ok() const noexcept { return error == ImportError::None && asset != nullptr; }
};

/// FBX (binário/ASCII, com skin e animações assadas) ou OBJ (+ .mtl), via ufbx.
[[nodiscard]] ImportResult import_ufbx_file(const std::string& path, const ImportOptions& options,
                                            ImportProgress* progress = nullptr);

/// Pela extensão: .fbx/.obj → ufbx; o resto → glTF.
[[nodiscard]] ImportResult import_scene_file(const std::string& path, const ImportOptions& options,
                                             ImportProgress* progress = nullptr);

/// Importa um .glb ou .gltf do disco.
[[nodiscard]] ImportResult import_gltf_file(const std::string& path, const ImportOptions& options,
                                            ImportProgress* progress = nullptr);

/// Importa de memória (GLB inteiro, ou JSON de um .gltf). `baseDir` resolve
/// URIs externas quando não há `reader`.
/// KTX2 (Basis Universal ETC1S/UASTC, com ou sem Zstd): transcodifica o
/// nível 0 para RGBA8. Usado pelo import de KHR_texture_basisu.
[[nodiscard]] bool is_ktx2(const u8* data, usize size) noexcept;
[[nodiscard]] bool decode_ktx2(const u8* data, usize size, Image& out);

[[nodiscard]] ImportResult import_gltf_memory(const u8* data, usize size, const std::string& baseDir,
                                              const ImportOptions& options, ImportProgress* progress = nullptr);

} // namespace aurea::scene3d
