#pragma once

#include "aurea/project/AeProject.hpp"

#include <filesystem>

namespace aurea::ae {

inline constexpr usize kMaxProjectBytes = 64u << 20;

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
[[nodiscard]] bool read_project_source(const std::string& path, ProjectDocument& document,
                                       ProjectImportMedia& media, std::string& error);

} // namespace aurea::ae
