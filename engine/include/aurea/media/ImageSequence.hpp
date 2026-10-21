// =============================================================================
//  Aurea / media / ImageSequence.hpp
//
//  Sequência de imagens (render do Blender: render_0001.png … render_0240.png,
//  também JPG, TGA, BMP e EXR) tratada como um CLIPE DE VÍDEO.
//
//  O asset é um AssetKind::Video comum. O `sourcePath` não aponta para um
//  arquivo: é uma RECEITA ("aurea-seq:"), como a forma 3D ("aurea-shape3d:").
//  Assim corte, velocidade, time remap, reverso, mistura de quadros,
//  miniaturas, export e projeto aberto de novo funcionam pelo caminho de
//  sempre dos vídeos — só o decoder muda.
//
//  RECEITA (texto, versionada; chave desconhecida é ignorada):
//
//      aurea-seq:v1;d=<pasta>;p=<prefixo>;s=<sufixo>;z=<dígitos>;a=<primeiro>;
//                b=<último>;n=<faixas presentes>;r=<fps>;al=<alfa>;c=<cor>;m=h;
//
//    d, p, s   em hexadecimal (UTF-8): ';' e '=' no nome não quebram a receita.
//              A pasta segue `Engine::store_asset_path` ("docs:<relativo>"
//              dentro de Documentos, absoluta fora) — sobrevive à troca do
//              contêiner do app no iOS e ao pacote do projeto.
//    z         zeros à esquerda do número (0 = largura livre: frame1, frame10).
//    a, b      numeração do primeiro e do último quadro.
//    n         faixas presentes "1-120,122-240" (ausente = todas de a até b).
//    r         quadros por segundo da sequência.
//    al        0 ignorar alfa (opaco), 1 alfa reto, 2 pré-multiplicado.
//    c         0 sRGB, 1 linear (EXR).
//    m         quadro ausente: h = segura o anterior (único modo por ora).
//
//  O nome do quadro N é prefixo + N (com `z` dígitos) + sufixo; o sufixo traz
//  a extensão ("render_" + "0001" + ".png"; "frame_" + "0001" + "_beauty.exr").
//
//  DECODER (ImageSequenceBackend, ImageSequence.cpp): stb_image (PNG 8 e 16
//  bits, JPG, TGA, BMP) e tinyexr (EXR meia precisão/float, com alfa, linear,
//  pré-multiplicado por convenção). Quadros de 8 bits saem RGBA8; PNG de 16
//  bits e EXR saem RGBA16F. Qualquer quadro é "quadro-chave" (acesso direto).
//  Tocando para a frente, um grupo pequeno de threads decodifica os próximos
//  quadros em paralelo, dentro do orçamento DecodedFrames do motor.
//
//  A fábrica `ImageSequenceFactory` embrulha a da plataforma: intercepta
//  "aurea-seq:" em probe/open e repassa todo o resto. Não usa sessão de
//  decoder de hardware do aparelho.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/media/VideoSource.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace aurea {

class MemoryManager;

inline constexpr const char* kImageSequenceScheme = "aurea-seq:";
/// Quadros no máximo na linha do tempo de uma sequência (do primeiro ao
/// último número, ausentes incluídos).
inline constexpr u32 kImageSequenceMaxFrames = 100000;
/// Lado maior de um quadro decodificado. Acima disso o quadro é reduzido por
/// um fator inteiro (média de caixa): um 8K RGBA16F são 265 MB por quadro.
inline constexpr u32 kImageSequenceMaxSide = 4096;

/// Valores estáveis: atravessam as bridges (Android/iOS) como inteiros.
enum class SequenceAlphaMode : u8 { Auto = 0, Straight = 1, Premultiplied = 2, Ignore = 3 };
enum class SequenceColorMode : u8 { Auto = 0, Srgb = 1, Linear = 2 };

struct ImageSequenceSpec {
    std::string directory;    ///< como gravado: "docs:<relativo>" ou absoluto
    std::string prefix;       ///< "render_"
    std::string suffix;       ///< ".png" (tudo depois do número, com a extensão)
    u32 padding = 0;          ///< dígitos com zeros à esquerda (0 = largura livre)
    i64 first = 1;
    i64 last = 1;
    /// Faixas [a, b] de números presentes, ordenadas e disjuntas. Vazio = todos
    /// de `first` até `last`.
    std::vector<std::pair<i64, i64>> present;
    f64 fps = 24.0;
    SequenceAlphaMode alpha = SequenceAlphaMode::Straight;   ///< nunca Auto numa receita
    SequenceColorMode color = SequenceColorMode::Srgb;        ///< nunca Auto numa receita

    /// Quadros na linha do tempo (ausentes contam: seguram o anterior).
    [[nodiscard]] i64 frame_count() const noexcept { return last >= first ? last - first + 1 : 0; }
    [[nodiscard]] bool has(i64 number) const noexcept;
    /// O número cujo arquivo mostra o quadro `number`: ele mesmo, ou o último
    /// presente antes dele (quadro ausente segura o anterior).
    [[nodiscard]] i64 held_number(i64 number) const noexcept;
    [[nodiscard]] std::string file_name(i64 number) const;
    /// Números presentes, em ordem (para listar os arquivos).
    [[nodiscard]] std::vector<i64> present_numbers() const;
    [[nodiscard]] u64 present_count() const noexcept;
    /// "render_####.png" — para o usuário reconhecer o que importou.
    [[nodiscard]] std::string display_pattern() const;
};

[[nodiscard]] bool is_image_sequence(const std::string& sourcePath) noexcept;
[[nodiscard]] std::string encode_image_sequence(const ImageSequenceSpec& spec);
[[nodiscard]] bool decode_image_sequence(const std::string& sourcePath, ImageSequenceSpec& out);

/// Extensão aceita como quadro (png, jpg, jpeg, tga, bmp, exr; sem distinguir
/// maiúsculas).
[[nodiscard]] bool image_sequence_extension(const std::string& fileName) noexcept;

/// O que a sondagem descobriu (Engine::probe_image_sequence).
struct ImageSequenceProbe {
    std::string directory;     ///< pasta dos quadros (caminho absoluto deste aparelho)
    std::string prefix, suffix;
    std::string pattern;       ///< "render_####.png"
    std::string firstFile;     ///< caminho do primeiro quadro presente
    std::string format;        ///< "png", "jpg", "tga", "bmp", "exr"
    u32 padding = 0;
    i64 first = 0, last = 0;
    u32 frameCount = 0;        ///< quadros na linha do tempo (last - first + 1)
    u32 presentCount = 0;      ///< arquivos encontrados
    u32 missingCount = 0;      ///< números ausentes entre first e last
    std::vector<i64> missing;  ///< os primeiros números ausentes (até 256)
    u32 width = 0, height = 0;
    u32 channels = 0;          ///< canais do arquivo (1..4)
    u32 bitDepth = 8;          ///< 8, 16 (PNG 16 bits, EXR meia) ou 32 (EXR float)
    bool hasAlpha = false;
    bool linear = false;       ///< EXR: luz linear, pré-multiplicada
    f64 suggestedFps = 24.0;   ///< EXR framesPerSecond; senão 24 (padrão do Blender)
    SequenceAlphaMode suggestedAlpha = SequenceAlphaMode::Straight;
    SequenceColorMode suggestedColor = SequenceColorMode::Srgb;
    u32 candidates = 0;        ///< padrões diferentes vistos (o maior foi o escolhido)
    bool fromArchive = false;  ///< veio de um .zip (extraído em `directory`)
    u64 totalBytes = 0;        ///< soma dos arquivos
};

/// Agrupa os arquivos (caminhos absolutos) por pasta + prefixo + sufixo e
/// devolve o maior grupo. `preferred` (opcional): o grupo que contém este
/// arquivo ganha (a pessoa escolheu UM quadro e o motor acha os irmãos).
/// Só preenche o padrão (pasta, prefixo, sufixo, dígitos, faixas); não abre
/// nenhum arquivo. false = nenhum nome com número e extensão de imagem.
[[nodiscard]] bool detect_image_sequence(const std::vector<std::string>& files, const std::string& preferred,
                                         ImageSequenceSpec& out, u32* candidates = nullptr);

/// Lê só o cabeçalho de um quadro (PNG/JPG/TGA/BMP pelo stb, EXR pelo tinyexr).
struct SequenceFrameHeader {
    u32 width = 0, height = 0, channels = 0, bitDepth = 8;
    bool hasAlpha = false, exr = false;
    f64 fps = 0.0;   ///< EXR framesPerSecond (0 = não declarado)
};
[[nodiscard]] Status read_sequence_frame_header(const std::string& path, SequenceFrameHeader& out) noexcept;

/// Pixels de um quadro, como o decoder entrega: RGBA8 (8 bits) ou RGBA16F
/// (16 bits / EXR), linhas de cima para baixo, sem preenchimento.
struct SequencePixels {
    u32 width = 0, height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    bool hasAlpha = false;
    std::vector<u8> bytes;
    [[nodiscard]] u32 stride() const noexcept { return width * (format == PixelFormat::RGBA16F ? 8u : 4u); }
};
/// Decodifica um arquivo inteiro (reduzido a kImageSequenceMaxSide; a redução
/// pondera a cor pelo alfa, a não ser que o arquivo já seja `premultiplied`).
[[nodiscard]] Status decode_sequence_frame(const std::string& path, SequencePixels& out,
                                           bool premultiplied = false) noexcept;

/// Quadros decodificados ANTES de serem pedidos (antecipação), somando todos os
/// decoders do processo. Diagnóstico e teste.
[[nodiscard]] u64 image_sequence_ahead_decodes() noexcept;

/// O decoder de uma sequência (`directory` já resolvido para este aparelho).
/// `width`/`height`: tamanho do asset (0 = lê do primeiro quadro).
[[nodiscard]] std::unique_ptr<VideoDecoderBackend> open_image_sequence(const ImageSequenceSpec& spec,
    const std::string& directory, MediaPriority priority, MemoryManager* memory, u32 width = 0, u32 height = 0);

/// Fábrica que intercepta "aurea-seq:" e repassa o resto à da plataforma.
class ImageSequenceFactory final : public VideoSourceFactory {
public:
    using PathResolver = std::function<std::string(const std::string&)>;

    explicit ImageSequenceFactory(VideoSourceFactory* inner = nullptr) noexcept : inner_(inner) {}
    /// Antes do primeiro uso (o motor liga na inicialização).
    void set_inner(VideoSourceFactory* inner) noexcept { inner_ = inner; }
    void set_path_resolver(PathResolver resolver) { resolve_ = std::move(resolver); }
    void set_memory(MemoryManager* memory) noexcept { memory_ = memory; }
    [[nodiscard]] VideoSourceFactory* inner() const noexcept { return inner_; }
    /// Pasta da receita resolvida para este aparelho ("" = não resolve).
    [[nodiscard]] std::string resolve_directory(const std::string& stored) const;

    [[nodiscard]] bool probe(const char* sourcePath, MediaProbe& out) override;
    [[nodiscard]] std::string cache_identity(const char* sourcePath) override;
    [[nodiscard]] std::unique_ptr<VideoDecoderBackend> open_video(const Asset& asset, MediaPriority priority) override;
    [[nodiscard]] std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char* sourcePath) override;
    [[nodiscard]] std::unique_ptr<audio::AudioDecoderBackend> open_audio_software(const char* sourcePath) override;

private:
    VideoSourceFactory* inner_ = nullptr;
    PathResolver resolve_;
    MemoryManager* memory_ = nullptr;
};

} // namespace aurea
