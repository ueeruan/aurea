// =============================================================================
//  Aurea / project / Project.hpp
//
//  O documento aberto.
//
//  Um Project reúne: a timeline, os assets importados, as cenas 3D, os
//  ambientes, os ajustes de export e o estado de autosave. É o que é salvo em
//  .aurea e o que é restaurado num crash.
//
//  Sobre autosave: o projeto NUNCA é perdido porque o app fechou. Duas defesas
//  independentes:
//
//    - AUTOSAVE INCREMENTAL. O log de comandos é appendado a um arquivo de
//      journal a cada N segundos, não o projeto inteiro. Gravar 40 MB a cada
//      30 s trava o editor; gravar 20 KB de comandos não.
//
//    - PONTO DE RECUPERAÇÃO. A cada M minutos (ou N comandos) o projeto
//      completo é gravado num arquivo separado. Se o journal estiver corrompido
//      (queda no meio da escrita), o ponto de recuperação vale.
//
//  Na abertura, o motor carrega o último ponto de recuperação e reaplica o
//  journal. O usuário vê o projeto como estava, não como estava há 5 minutos.
// =============================================================================
#pragma once

#include "aurea/timeline/Timeline.hpp"
#include "aurea/project/Asset.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/core/Result.hpp"

#include <string>
#include <vector>

namespace aurea {

/// Configurações de export do projeto. Persistidas com o projeto para que
/// reexportar depois use os mesmos ajustes.
struct ExportSettings {
    u32  width  = 1920;
    u32  height = 1080;
    f64  fps    = 60.0;

    ExportCodec videoCodec = ExportCodec::H264;
    u32  videoBitrateMbps  = 20;
    /// Modo de taxa: 0 = CBR, 1 = VBR, 2 = qualidade constante (quando o
    /// encoder de hardware suporta).
    u32  rateMode = 1;
    u32  keyframeIntervalFrames = 0;   ///< 0 = automático (2 s)

    AudioCodec audioCodec = AudioCodec::AAC;
    u32  audioBitrateKbps = 192;
    u32  audioSampleRate  = 48000;
    u32  audioChannels    = 2;

    /// 0 = MP4, 1 = MOV.
    u32  container = 0;

    ColorSpace outputColorSpace = ColorSpace::SRGB;
    bool toneMapToSdr = true;

    /// Paralelismo do export. 1 = sequencial. O export planeja os segmentos
    /// independentes e os distribui; a decisão de quantos vem daqui.
    u32  parallelSegments = 1;

    /// Qualidade do motion blur / optical flow no export. O preview tem os seus
    /// próprios números e não os altera.
    u32  motionBlurSamples = 32;
    u32  opticalFlowQuality = 1;   ///< 0 rápido, 1 equilibrado, 2 alta

    /// Escala de saída relativa à composição (1.0 = mesma resolução).
    f32  scale = 1.0f;
};

/// Ajustes de interface persistidos com o projeto — zoom da timeline, escala
/// do preview, painéis abertos. Ficam aqui e não no projeto porque o usuário
/// espera reabrir e encontrar a tela como deixou.
struct EditorSettings {
    PreviewScale previewScale = PreviewScale::Auto;
    f32  timelineZoom = 1.0f;
    f32  timelineScroll = 0.0f;
    f32  viewportZoom  = 1.0f;
    Vec2 viewportPan{0.0f, 0.0f};
    bool loop = false;
    bool snapEnabled = true;
    bool showSafeArea = false;
    bool showGrid = false;
    u32  selectedLayerCount = 0;
    std::vector<u64> selectedLayers;   ///< handles empacotados
};

struct ProjectMetadata {
    std::string title = "Projeto sem título";
    std::string author;
    std::string description;
    u64 createdUnixMs = 0;
    u64 modifiedUnixMs = 0;
    /// Versão do app que gravou. Usado no aviso de "projeto gravado por uma
    /// versão mais nova" — o arquivo é recusado em vez de aberto pela metade.
    u32 appVersionMajor = 0;
    u32 appVersionMinor = 0;
    u32 appVersionPatch = 0;
    std::string appVersionLabel;
};

/// Estado do autosave. Exposto para a UI poder dizer "salvando..." e para o
/// app saber se há algo a recuperar na próxima abertura.
struct AutosaveState {
    std::string journalPath;
    std::string recoveryPath;
    u64  lastJournalWriteMs  = 0;
    u64  lastRecoveryWriteMs = 0;
    u32  commandsSinceRecovery = 0;
    bool dirty = false;              ///< há alteração não gravada
    bool recoveryAvailable = false;  ///< há journal/recovery de sessão anterior
    u32  recoveryCommandCount = 0;
};

class Project {
public:
    Project() = default;

    // --- Ciclo de vida --------------------------------------------------------

    /// Cria um projeto novo com uma composição 1080p já pronta. É o caminho do
    /// "Novo Projeto" da Home: o usuário nunca cai numa tela vazia sem saber o
    /// que fazer.
    [[nodiscard]] static Result<Project> create_new(u32 width = 1920, u32 height = 1080,
                                                     f64 fps = 60.0,
                                                     std::string title = "Projeto sem título");

    [[nodiscard]] Status load(const std::string& path);
    [[nodiscard]] Status save(const std::string& path);

    /// Caminho do arquivo aberto. Vazio = projeto nunca salvo.
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] bool has_path() const noexcept { return !path_.empty(); }
    void set_path(std::string p) { path_ = std::move(p); }

    // --- Conteúdo -------------------------------------------------------------

    [[nodiscard]] Timeline& timeline() noexcept { return timeline_; }
    [[nodiscard]] const Timeline& timeline() const noexcept { return timeline_; }

    [[nodiscard]] ProjectMetadata& metadata() noexcept { return metadata_; }
    [[nodiscard]] const ProjectMetadata& metadata() const noexcept { return metadata_; }

    [[nodiscard]] ExportSettings& export_settings() noexcept { return export_; }
    [[nodiscard]] const ExportSettings& export_settings() const noexcept { return export_; }

    [[nodiscard]] EditorSettings& editor_settings() noexcept { return editor_; }
    [[nodiscard]] const EditorSettings& editor_settings() const noexcept { return editor_; }

    // --- Assets ---------------------------------------------------------------

    [[nodiscard]] AssetId add_asset(Asset asset);
    [[nodiscard]] Asset* asset(AssetId id) noexcept { return assets_.get(id); }
    [[nodiscard]] const Asset* asset(AssetId id) const noexcept { return assets_.get(id); }
    bool remove_asset(AssetId id) noexcept;

    /// Assets que nenhuma layer referencia. A UI oferece removê-los; o motor
    /// não os apaga sozinho porque um asset pode estar só num estado de undo.
    [[nodiscard]] std::vector<AssetId> unreferenced_assets() const;

    template <typename Fn>
    void for_each_asset(Fn&& fn) { assets_.for_each(std::forward<Fn>(fn)); }
    template <typename Fn>
    void for_each_asset(Fn&& fn) const { assets_.for_each(std::forward<Fn>(fn)); }

    [[nodiscard]] u32 asset_count() const noexcept { return assets_.count(); }

    /// Hash de conteúdo já importado — evita duplicar o mesmo vídeo quando o
    /// usuário importa duas vezes.
    [[nodiscard]] AssetId find_asset_by_hash(u64 contentHash) const noexcept;

    // --- Autosave -------------------------------------------------------------

    /// Configura os caminhos e as cadências de autosave.
    void configure_autosave(std::string journalPath, std::string recoveryPath,
                            u32 journalIntervalMs, u32 recoveryIntervalMs,
                            u32 recoveryCommandInterval) noexcept;

    [[nodiscard]] AutosaveState& autosave() noexcept { return autosave_; }
    [[nodiscard]] const AutosaveState& autosave() const noexcept { return autosave_; }

    /// Verifica se há recuperação pendente de uma sessão anterior.
    [[nodiscard]] bool check_recovery() noexcept;

    /// Aplica o journal de recuperação por cima do projeto carregado.
    ///
    /// Os comandos ficam guardados aqui; quem os aplica é o Engine, que é quem
    /// sabe aplicar comandos e registrar undo. Um Project que aplicasse os
    /// próprios comandos precisaria conhecer o avaliador de animação e o
    /// registrador de efeitos — duas dependências que ele não deve ter.
    [[nodiscard]] Status apply_recovery() noexcept;

    /// Comandos recuperados do journal, na ordem em que foram escritos.
    /// Vazio quando não há recuperação pendente.
    [[nodiscard]] const std::vector<Command>& recovered_commands() const noexcept {
        return recoveredCommands_;
    }
    void clear_recovered_commands() noexcept { recoveredCommands_.clear(); }

    /// Descarta a recuperação (o usuário escolheu começar do zero).
    void discard_recovery() noexcept;

    // --- Estado ---------------------------------------------------------------

    [[nodiscard]] bool dirty() const noexcept { return autosave_.dirty; }
    void mark_dirty() noexcept { autosave_.dirty = true; ++autosave_.commandsSinceRecovery; }
    void mark_clean() noexcept { autosave_.dirty = false; }

    [[nodiscard]] u64 revision() const noexcept { return revision_; }
    void touch() noexcept { ++revision_; }

private:
    std::string path_;

    Timeline        timeline_;
    ProjectMetadata metadata_;
    ExportSettings  export_;
    EditorSettings  editor_;
    AutosaveState   autosave_;

    SlotTable<Asset, AssetTag> assets_;

    /// Comandos lidos do journal de recuperação, aguardando o Engine aplicá-los.
    std::vector<Command> recoveredCommands_;

    u64 revision_ = 1;
};

} // namespace aurea
