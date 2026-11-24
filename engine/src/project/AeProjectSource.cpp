#include "AeProjectSource.hpp"

#include "aurea/project/FileIO.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#if defined(AUREA_HAS_FFMPEG) && AUREA_HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
}
#endif

namespace aurea::ae {
namespace {

namespace fs = std::filesystem;
constexpr usize kMaxEntries = 32768;
constexpr u64 kMaxMediaBytes = 16ull << 30;
constexpr u64 kMaxArchiveBytes = 8ull << 30;
constexpr u64 kFreeSpaceReserve = 64ull << 20;

std::string utf8(const fs::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

std::string normalized(std::string_view name) {
    std::string result(name);
    for (char& ch : result) {
        if (ch == '\\') ch = '/';
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
    }
    while (result.starts_with("./")) result.erase(0, 2);
    return result;
}

std::string_view basename(std::string_view name) {
    const auto last = name.find_last_of('/');
    return last == name.npos ? name : name.substr(last + 1);
}

std::string_view stem(std::string_view name) {
    name = basename(name);
    return name.substr(0, name.find_last_of('.'));
}

std::string_view extension(std::string_view name) {
    name = basename(name);
    const auto dot = name.find_last_of('.');
    return dot == name.npos ? std::string_view{} : name.substr(dot);
}

bool safe_member(std::string_view name) {
    if (name.empty() || name.size() > 4096 || name.front() == '/' || name.find(':') != name.npos) return false;
    while (!name.empty()) {
        const auto slash = name.find('/');
        const auto part = name.substr(0, slash);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == name.npos) break;
        name.remove_prefix(slash + 1);
    }
    return true;
}

bool ignored(std::string_view name) {
    const auto base = basename(name);
    return name.starts_with("__macosx/") || base.starts_with("._") || name.find("/._") != name.npos;
}

bool is_backup(std::string_view name) {
    return name.find("auto-save/") != name.npos || name.find("autosave/") != name.npos ||
           name.find("salvamento automático/") != name.npos;
}

bool media_extension(std::string_view ext) {
    constexpr std::string_view formats[] = {
        ".mp4", ".mov", ".m4v", ".mxf", ".avi", ".mkv", ".webm", ".mts", ".m2ts", ".mpg", ".mpeg",
        ".wav", ".wave", ".aif", ".aifc", ".aiff", ".m4a", ".aac", ".mp3", ".ogg", ".opus", ".flac",
        ".png", ".jpg", ".jpeg", ".jpe", ".gif", ".bmp", ".tga", ".tif", ".tiff", ".webp", ".heic", ".psd", ".exr"
    };
    return std::find(std::begin(formats), std::end(formats), ext) != std::end(formats);
}

struct Entry {
    usize index;
    std::string path;
    u64 bytes;
};

struct Reader {
    explicit Reader(const std::string& path)
        : file(fileio::open_file(path, "rb"), std::fclose), input(archive_read_new(), archive_read_free) {}

    bool open() {
        if (!file || !input) return false;
        archive_read_support_format_zip_seekable(input.get());
        archive_read_support_format_rar(input.get());
        archive_read_support_format_rar5(input.get());
        return archive_read_open_FILE(input.get(), file.get()) == ARCHIVE_OK;
    }

    template<class Visit>
    bool scan(Visit&& visit, std::string& error, const usize* pending = nullptr) {
        archive_entry* header = nullptr;
        u64 total = 0;
        for (usize index = 0; ; ++index) {
            const int result = archive_read_next_header(input.get(), &header);
            if (result == ARCHIVE_EOF) return true;
            if (result != ARCHIVE_OK || index >= kMaxEntries) {
                error = "Pacote inválido, protegido por senha ou com arquivos demais.";
                return false;
            }
            if (archive_entry_filetype(header) != AE_IFREG || archive_entry_hardlink(header) || archive_entry_symlink(header)) continue;
            const char* filename = archive_entry_pathname_utf8(header);
            if (!filename) filename = archive_entry_pathname(header);
            const auto size = archive_entry_size(header);
            if (!filename || size < 0 || static_cast<u64>(size) > kMaxMediaBytes - total) {
                error = "O conteúdo descompactado do projeto excede o limite de 16 GB ou tem nomes ilegíveis.";
                return false;
            }
            total += static_cast<u64>(size);
            const std::string name = normalized(filename);
            if (!safe_member(name)) {
                error = "O pacote contém um caminho de arquivo inválido.";
                return false;
            }
            if (ignored(name)) continue;
            if (!visit(Entry{index, name, static_cast<u64>(size)}, archive_entry_is_encrypted(header) > 0)) return false;
            if (pending && *pending == 0) return true;
        }
    }

    template<class Write>
    bool read(const Entry& entry, bool encrypted, Write&& write, std::string& error) {
        if (encrypted) {
            error = "O arquivo selecionado pertence a um pacote protegido por senha.";
            return false;
        }
        std::array<char, 64 * 1024> chunk;
        u64 remaining = entry.bytes;
        for (;;) {
            const auto count = archive_read_data(input.get(), chunk.data(), chunk.size());
            if (count == 0 && remaining == 0) return true;
            if (count <= 0 || static_cast<u64>(count) > remaining) {
                error = unsupported_compression()
                    ? "Compressão do pacote não suportada; compacte de novo com Deflate (o ZIP padrão)."
                    : "Um arquivo dentro do pacote está incompleto ou corrompido.";
                return false;
            }
            if (!write(chunk.data(), static_cast<usize>(count))) {
                error = "Não foi possível guardar as mídias originais. Verifique o espaço livre.";
                return false;
            }
            remaining -= static_cast<u64>(count);
        }
    }

    /// LZMA, XZ, BZip2 e Zstandard dentro do ZIP precisam de bibliotecas que
    /// o app não leva; a libarchive diz "Unsupported ZIP compression method".
    [[nodiscard]] bool unsupported_compression() {
        const char* why = archive_error_string(input.get());
        if (!why) return false;
        const std::string text = normalized(why);
        return text.find("unsupported") != text.npos || text.find("compression method") != text.npos ||
               text.find("not supported") != text.npos;
    }

    std::unique_ptr<std::FILE, decltype(&std::fclose)> file;
    std::unique_ptr<archive, decltype(&archive_read_free)> input;
};

// Count equal trailing components. The original path may come from a different OS.
usize shared_suffix(std::string_view archived, std::string_view reference) {
    usize score = 0;
    while (!archived.empty() && !reference.empty() && basename(archived) == basename(reference)) {
        ++score;
        const auto cutArchive = archived.find_last_of('/');
        const auto cutReference = reference.find_last_of('/');
        if (cutArchive == archived.npos || cutReference == reference.npos) break;
        archived = archived.substr(0, cutArchive);
        reference = reference.substr(0, cutReference);
    }
    return score;
}

using MediaIndex = std::unordered_map<std::string, std::vector<Entry>>;

bool is_quicktime_media(std::string_view path) {
    const auto ext = extension(path);
    return ext == ".mp4" || ext == ".mov" || ext == ".m4v" || ext == ".m4a";
}

struct ContainerMedia {
    u32 width = 0, height = 0, sampleRate = 0, channels = 0, codecTag = 0;
    f64 fps = 0, duration = 0;
};

ContainerMedia inspect_quicktime(const std::string& path) {
    ContainerMedia media;
#if defined(AUREA_HAS_FFMPEG) && AUREA_HAS_FFMPEG
    // Force the MOV/MP4 demuxer with external references disabled. No playlists,
    // remote paths or nested documents are evaluated as part of archive import.
    AVFormatContext* raw = nullptr;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    av_dict_set(&options, "enable_drefs", "0", 0);
    av_dict_set(&options, "max_streams", "64", 0);
    const AVInputFormat* format = av_find_input_format("mov");
    const int opened = format ? avformat_open_input(&raw, path.c_str(), format, &options) : -1;
    av_dict_free(&options);
    if (opened != 0) return media;
    const std::unique_ptr<AVFormatContext, void(*)(AVFormatContext*)> input(raw, [](AVFormatContext* ctx) {
        avformat_close_input(&ctx);
    });
    for (u32 index = 0; index < input->nb_streams; ++index) {
        const auto* stream = input->streams[index];
        const auto* parameters = stream->codecpar;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO && !media.width && !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            media.width = static_cast<u32>(std::max(0, parameters->width));
            media.height = static_cast<u32>(std::max(0, parameters->height));
            media.codecTag = parameters->codec_tag;
            if (stream->avg_frame_rate.den) media.fps = av_q2d(stream->avg_frame_rate);
            if (stream->duration > 0 && stream->time_base.den) media.duration = stream->duration * av_q2d(stream->time_base);
            if (!(media.fps > 0) && media.duration > 0 && stream->nb_frames > 0)
                media.fps = static_cast<f64>(stream->nb_frames) / media.duration;
        }
        if (parameters->codec_type == AVMEDIA_TYPE_AUDIO && !media.sampleRate) {
            media.sampleRate = static_cast<u32>(std::max(0, parameters->sample_rate));
            media.channels = static_cast<u32>(std::max(0, parameters->ch_layout.nb_channels));
        }
    }
#else
    (void)path;
#endif
    return media;
}

// Collected projects sometimes contain an alternate container or an explicitly
// labelled frame-rate version of a clip. Require matching file family and then
// independently validate the stream metadata before accepting the replacement.
std::pair<std::string_view, f64> clip_family(std::string_view name) {
    auto base = stem(name);
    if (!base.ends_with("fps")) return {base, 0};
    base.remove_suffix(3);
    auto start = base.size();
    while (start && ((base[start - 1] >= '0' && base[start - 1] <= '9') || base[start - 1] == '.')) --start;
    const auto digits = base.substr(start);
    const auto dot = digits.find('.');
    const auto whole = digits.substr(0, dot);
    const auto fraction = dot == digits.npos ? std::string_view{} : digits.substr(dot + 1);
    u32 fps = 0, decimals = 0;
    const auto integral = std::from_chars(whole.data(), whole.data() + whole.size(), fps);
    if (integral.ec != std::errc{} || integral.ptr != whole.data() + whole.size() || fraction.size() > 6)
        return {stem(name), 0};
    if (!fraction.empty()) {
        const auto parsed = std::from_chars(fraction.data(), fraction.data() + fraction.size(), decimals);
        if (parsed.ec != std::errc{} || parsed.ptr != fraction.data() + fraction.size()) return {stem(name), 0};
    }
    base = base.substr(0, start);
    const auto last = base.find_last_not_of(" _-.");
    return {last == base.npos ? std::string_view{} : base.substr(0, last + 1),
            fps + decimals / std::pow(10.0, static_cast<f64>(fraction.size()))};
}

using ClipIndex = std::unordered_map<std::string, std::vector<std::pair<const Entry*, f64>>>;

ClipIndex index_alternate_clips(const MediaIndex& files) {
    ClipIndex clips;
    for (const auto& [name, entries] : files) {
        if (!is_quicktime_media(name) || extension(name) == ".m4a") continue;
        const auto [family, rate] = clip_family(name);
        if (family.empty()) continue;
        for (const auto& entry : entries) clips[std::string(family)].emplace_back(&entry, rate);
    }
    return clips;
}

const Entry* find_alternate(const ClipIndex& clips, const ProjectMedia& item) {
    const std::string name = normalized(item.filename.empty() ? item.name : item.filename);
    if (!is_quicktime_media(name) || !item.width || !item.height || item.duration <= 0 || item.fps <= 0) return nullptr;
    const auto candidates = clips.find(std::string(clip_family(name).first));
    if (candidates == clips.end()) return nullptr;
    const Entry* match = nullptr;
    for (const auto& [entry, rate] : candidates->second) {
        if (rate > 0 && std::abs(rate - item.fps) > .1) continue;
        if (match) return nullptr;
        match = entry;
    }
    return match;
}

const Entry* find_media(const MediaIndex& files, ProjectMedia& item) {
    const auto name = normalized(item.filename.empty() ? item.name : item.filename);
    const auto list = files.find(name);
    if (list == files.end()) return nullptr;
    const auto reference = item.sourceReference.empty() ? name : normalized(item.sourceReference);
    const Entry* match = nullptr;
    usize best = 0;
    bool tied = false;
    for (const auto& entry : list->second) {
        const usize score = shared_suffix(entry.path, reference);
        if (!match || score > best) {
            best = score;
            match = &entry;
            tied = false;
        } else if (score == best) {
            tied = true;
        }
    }
    if (tied) item.linkProblem = "mais de um arquivo tem esse nome no pacote; vincule a mídia correta";
    return tied ? nullptr : match;
}

bool extract_media(const std::string& path, const MediaIndex& files, ProjectDocument& doc,
                   ProjectImportMedia& media, std::string& error) {
    struct Use { ProjectMedia* item; bool alternate; };
    struct Selection {
        std::string output;
        std::vector<Use> uses;
    };
    std::unordered_map<usize, Selection> selected;
    const auto clips = index_alternate_clips(files);
    std::unordered_set<u32> referenced;
    for (const auto& comp : doc.comps)
        for (const auto& layer : comp.layers)
            if (layer.type == 0 && !(layer.flagsB & 0x82)) referenced.insert(layer.source);
    u64 needed = 0;
    for (auto& item : doc.media) {
        if (item.solid || !referenced.contains(item.id)) continue;
        const auto* match = find_media(files, item);
        const bool alternate = !match;
        if (alternate && item.linkProblem.empty()) match = find_alternate(clips, item);
        if (!match) continue;
        const auto [entry, added] = selected.try_emplace(match->index);
        entry->second.uses.push_back({&item, alternate});
        needed += added ? match->bytes : 0;
    }
    if (selected.empty()) return true;

    std::error_code ec;
    fs::create_directories(media.root, ec);
    if (ec || fs::space(media.root, ec).available < needed + kFreeSpaceReserve || ec) {
        error = "Espaço insuficiente para guardar as mídias originais do projeto.";
        return false;
    }
    static std::atomic<u64> next{0};
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    const auto directory = media.root / ("after-effects-" + std::to_string(stamp) + "-" +
                                         std::to_string(next.fetch_add(1, std::memory_order_relaxed)));
    if (!fs::create_directory(directory, ec)) {
        error = "Não foi possível criar a pasta das mídias do projeto.";
        return false;
    }
    media.directory = directory;

    Reader reader(path);
    if (!reader.open()) {
        error = "Não foi possível reabrir o pacote para vincular as mídias.";
        return false;
    }
    usize pending = selected.size();
    // 1.0.8: uma mídia que não sai do pacote (senha, compressão, dano) fica
    // sem vínculo, com o motivo; o projeto entra com as outras.
    const auto dropMedia = [&](Selection& selection, const std::string& why) {
        for (const auto& use : selection.uses) use.item->linkProblem = why;
        if (!selection.output.empty()) (void)fileio::remove_file(selection.output);
        selection.output.clear();
        --pending;
        return true;
    };
    std::string scanError;
    if (!reader.scan([&](const Entry& entry, bool encrypted) {
        const auto match = selected.find(entry.index);
        if (match == selected.end()) return true;
        auto& selection = match->second;
        selection.output = utf8(directory / (std::to_string(entry.index) + std::string(extension(entry.path))));
        std::unique_ptr<std::FILE, decltype(&std::fclose)> output(fileio::open_file(selection.output, "wb"), std::fclose);
        if (!output) {
            error = "Não foi possível abrir o destino de uma mídia do projeto.";
            return false;
        }
        std::string why;
        if (!reader.read(entry, encrypted, [&](const char* bytes, usize count) {
            return std::fwrite(bytes, 1, count, output.get()) == count;
        }, why)) {
            output.reset();
            // Disco cheio vale para todas: aí, sim, para.
            if (why.find("espaço livre") != std::string::npos) { error = why; return false; }
            return dropMedia(selection, why);
        }
        if (std::fclose(output.release()) != 0) {
            error = "Não foi possível concluir a gravação de uma mídia do projeto.";
            return false;
        }
        const auto probe = is_quicktime_media(entry.path) ? inspect_quicktime(selection.output) : ContainerMedia{};
        bool used = false;
        for (const auto& use : selection.uses) {
            auto& item = *use.item;
            if (use.alternate && (probe.width != item.width || probe.height != item.height ||
                std::abs(probe.fps - item.fps) > .1 || std::abs(probe.duration - item.duration) > .1 ||
                (item.channels > 0 && probe.channels != item.channels))) {
                item.linkProblem = "a variante encontrada tem resolução, duração, taxa de quadros ou áudio incompatíveis";
                continue;
            }
            if (use.alternate) item.recoveredFrom = std::string(basename(entry.path));
            item.resolvedPath = selection.output;
            item.fileBytes = entry.bytes;
            item.codecTag = probe.codecTag;
            item.sampleRate = probe.sampleRate;
            if (probe.sampleRate) item.channels = probe.channels;
            used = true;
        }
        if (!used) (void)fileio::remove_file(selection.output);
        --pending;
        return true;
    }, scanError, &pending)) {
        if (!error.empty()) return false;
        // O resto do pacote não pôde ser lido: as mídias que faltam ficam sem vínculo.
        for (auto& [index, selection] : selected)
            for (const auto& use : selection.uses)
                if (use.item->resolvedPath.empty() && use.item->linkProblem.empty()) use.item->linkProblem = scanError;
        return true;
    }
    if (pending != 0) {
        for (auto& [index, selection] : selected)
            for (const auto& use : selection.uses)
                if (use.item->resolvedPath.empty() && use.item->linkProblem.empty())
                    use.item->linkProblem = "a mídia não foi encontrada ao reler o pacote";
    }
    return true;
}

bool read_archive(const std::string& path, ProjectDocument& document, ProjectImportMedia& media, std::string& error) {
    std::error_code ec;
    if (fs::file_size(fs::u8path(path), ec) > kMaxArchiveBytes || ec) {
        error = "Não foi possível ler o pacote ou ele excede o limite de 8 GB.";
        return false;
    }
    Reader reader(path);
    if (!reader.open()) {
        error = "Não foi possível abrir o ZIP ou RAR do After Effects.";
        return false;
    }
    const auto archiveName = normalized(path);
    std::string project;
    MediaIndex files;
    using Rank = std::tuple<bool, bool, usize>;
    Rank best{};
    bool found = false;
    bool tied = false;
    bool oversized = false;
    std::string chosen, other;
    if (!reader.scan([&](const Entry& entry, bool encrypted) {
        const auto ext = extension(entry.path);
        if (media_extension(ext) && entry.bytes > 0) files[std::string(basename(entry.path))].push_back(entry);
        if (ext != ".aep" && ext != ".aet") return true;
        const Rank rank{is_backup(entry.path), stem(entry.path) != stem(archiveName),
                        static_cast<usize>(std::count(entry.path.begin(), entry.path.end(), '/'))};
        if (found && rank == best && !tied) { tied = true; other = std::string(basename(entry.path)); }
        if (found && rank >= best) return true;
        found = true;
        best = rank;
        tied = false;
        chosen = std::string(basename(entry.path));
        oversized = entry.bytes > kMaxProjectBytes;
        project.clear();
        if (oversized) return true;
        project.reserve(static_cast<usize>(entry.bytes));
        return reader.read(entry, encrypted, [&](const char* bytes, usize count) {
            project.append(bytes, count);
            return true;
        }, error);
    }, error)) return false;
    if (!found) error = "O pacote não contém um projeto .aep do After Effects.";
    else if (oversized) error = "O arquivo .aep dentro do pacote excede o limite de 256 MB.";
    else if (!parse_project(project, document, error)) error = "Projeto do After Effects ilegível: " + error + ".";
    if (!error.empty()) return false;
    // Mais de um .aep principal: entra o primeiro do pacote (o outro fica listado).
    if (tied)
        document.issues.push_back({"arquivo", other, "o pacote tem mais de um projeto principal; entrou " + chosen +
                                   " (importe os outros separadamente)", true});
    std::string{}.swap(project);
    return extract_media(path, files, document, media, error);
}

} // namespace

ProjectImportMedia::~ProjectImportMedia() {
    if (keep || directory.empty()) return;
    std::error_code ec;
    fs::remove_all(directory, ec);
}

bool is_project_source_name(std::string_view name) {
    const auto lower = normalized(name);
    const auto ext = extension(lower);
    return ext == ".aep" || ext == ".aet" || ext == ".zip" || ext == ".rar";
}

std::string project_source_kind_of_header(std::string_view h) {
    if (is_project_file(h)) return "aep";
    if (h.size() >= 4 && (h.substr(0, 4) == std::string_view("PK\x03\x04", 4) ||
                          h.substr(0, 4) == std::string_view("PK\x05\x06", 4))) return "zip";
    if (h.size() >= 6 && h.substr(0, 6) == std::string_view("Rar!\x1A\x07", 6)) return "rar";
    return {};
}

std::string project_source_kind(const std::string& path) {
    const auto lower = normalized(path);
    const auto ext = extension(lower);
    if (ext == ".aep" || ext == ".aet") return "aep";
    if (ext == ".zip") return "zip";
    if (ext == ".rar") return "rar";
    // Sem extensão conhecida (URI "msf:123"): os primeiros bytes decidem.
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(fileio::open_file(path, "rb"), std::fclose);
    if (!file) return {};
    char header[16]{};
    const usize n = std::fread(header, 1, sizeof(header), file.get());
    return project_source_kind_of_header(std::string_view(header, n));
}

bool read_project_source(const std::string& path, ProjectDocument& document,
                         ProjectImportMedia& media, std::string& error) {
    const auto kind = project_source_kind(path);
    if (kind == "zip" || kind == "rar") return read_archive(path, document, media, error);
    std::vector<u8> bytes;
    if (!fileio::read_all(path, bytes, kMaxProjectBytes) || bytes.empty()) {
        error = "Não foi possível ler o projeto .aep, ou ele excede o limite de 256 MB.";
        return false;
    }
    const std::string_view data(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (parse_project(data, document, error)) return true;
    error = "Projeto do After Effects ilegível: " + error + ".";
    return false;
}

} // namespace aurea::ae
