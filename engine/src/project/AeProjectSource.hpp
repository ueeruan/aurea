#pragma once

#include "aurea/project/AeProject.hpp"

#include <filesystem>
#include <string>

namespace aurea::ae {

inline constexpr usize kMaxProjectBytes = 256u << 20;

// Owns the newly created media directory until the project is committed.
struct ProjectImportMedia {
    explicit ProjectImportMedia(std::filesystem::path mediaRoot) : root(std::move(mediaRoot)) {}
    ~ProjectImportMedia();
    ProjectImportMedia(const ProjectImportMedia&) = delete;
    ProjectImportMedia& operator=(const ProjectImportMedia&) = delete;

    std::filesystem::path root;
    std::filesystem::path directory;
    bool keep = false;
};

[[nodiscard]] bool is_project_source_name(std::string_view name);
/// Projeto (.aep/.aet), .zip ou .rar do AE pelo nome ou, sem extensão
/// conhecida (URI "msf:123"), pelos primeiros bytes: RIFX+Egg!, PK, Rar!.
/// Devolve "aep", "zip", "rar" ou vazio.
[[nodiscard]] std::string project_source_kind(const std::string& path);
[[nodiscard]] std::string project_source_kind_of_header(std::string_view header);
[[nodiscard]] bool read_project_source(const std::string& path, ProjectDocument& document,
                                       ProjectImportMedia& media, std::string& error);

} // namespace aurea::ae
