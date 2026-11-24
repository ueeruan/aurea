#pragma once

#include "aurea/project/AeImport.hpp"

namespace aurea::ae {

struct ProjectMedia {
    u32 id = 0;
    std::string name;
    std::string filename;
    std::string sourceReference;
    std::string resolvedPath;
    std::string recoveredFrom;
    std::string linkProblem;
    u64 fileBytes = 0;
    u32 sampleRate = 0;
    u32 codecTag = 0;
    u32 width = 0, height = 0, channels = 0;
    f64 duration = 0, fps = 0;
    bool solid = false;
    f32 color[4]{0, 0, 0, 1};
};

struct ProjectLayer {
    u32 id = 0, source = 0, parent = 0, matte = 0;
    std::string name;
    u8 type = 0, flagsA = 0, flagsB = 0, flagsC = 0;
    u8 blend = 2, transfer = 0, matteMode = 0, light = 0;
    bool legacyMatte = false;
    f64 start = 0, in = 0, out = 0, stretch = 1;
    Node properties;
};

struct ProjectComp {
    u32 id = 0, width = 0, height = 0;
    /// Tamanho que cabe no Aurea (textura máxima): igual ao do AE quando cabe;
    /// menor = o conteúdo é reduzido na mesma proporção (resize_content).
    u32 fitWidth = 0, fitHeight = 0;
    std::string name;
    f64 fps = 0, duration = 0;
    bool motionBlur = false;
    i32 shutterAngle = 180, shutterPhase = -90;
    std::vector<ProjectLayer> layers; // AE top to bottom.
};

/// Item que a leitura pulou (malformado, além dos limites) ou aproximou: o
/// projeto entra sem ele e o relatório diz qual foi.
struct ProjectIssue {
    std::string kind, name, reason;
    bool skipped = true;   ///< falso = aproximação (nota)
};

struct ProjectDocument {
    std::vector<ProjectComp> comps;
    std::vector<ProjectMedia> media;
    std::vector<ProjectIssue> issues;
};

[[nodiscard]] bool is_project_file(std::string_view bytes) noexcept;
[[nodiscard]] bool parse_project(std::string_view bytes, ProjectDocument& out, std::string& error);

// Shared bounded decoder for the four-byte group type and its child chunks.
[[nodiscard]] bool parse_property_tree(std::string_view payload, u32 fallbackTimescale,
                                       usize& expandedBytes, Node& out, std::string& error);

// The same effect and transform binders as the preset importer, with project timing.
void apply_project_properties(ProjectLayer& source, Host& host, LayerId target, ImportReport& report);

} // namespace aurea::ae
