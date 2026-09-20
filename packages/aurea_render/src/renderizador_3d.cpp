// O RENDERIZADOR 3D EM DILIGENT — A IMPLEMENTACAO.
//
// =========================== AS QUATRO PASSADAS ========================
//
//  1. A SOMBRA, quando pedida. Desenha a cena do ponto de vista da luz
//     direcional num alvo de um canal. E um passe a mais por quadro e so
//     existe quando a qualidade pede (§28).
//
//  2. OS OPACOS, na ordem que a avaliacao entregou (por modelo, por osso e
//     por malha). A ordem aqui quase nao muda o resultado — quem decide o
//     que aparece e o teste de profundidade — mas trocar de pipeline e de
//     buffer o menos possivel e o que faz uma cena de vinte modelos andar.
//
//  3. OS TRANSPARENTES, do mais longe para o mais perto, com teste de
//     profundidade LIGADO e escrita DESLIGADA. Escrita ligada faria o
//     segundo vidro apagar o primeiro; a ordenacao sozinha nao resolve, e a
//     combinacao das duas e o que o 3D sem OIT consegue fazer direito (§16).
//
//  4. A RELEITURA. O alvo vira pixels na memoria e entra na composicao 2D
//     como uma textura. E o unico ponto de contato entre os dois mundos, e
//     e o que garante que a exportacao desenha o mesmo que o preview (§33).
//
// ======================= O QUE ESTA ESCRITO NA PEDRA ===================
// Os numeros de `binding` aqui e nos `shaders/*.vert|frag` TEM de bater. O
// std140 nao guarda nome de campo: um atributo trocado de lugar desenha
// lixo sem dar erro. Os `static_assert` de tamanho dos blocos sao a unica
// rede de seguranca possivel — e por isso eles existem.
//
// ========================= O QUE ESTE ARQUIVO NAO FAZ ==================
// Ele nao sabe o que e uma camada, um keyframe ou um clipe: recebe o quadro
// ja resolvido e desenha. Nao carrega arquivo: o importador entrega o
// `Modelo` e o `garantir_modelo` sobe para a placa. Nao compoe com o video:
// devolve pixels e o compositor 2D faz o resto (§14).
#include "renderizador_3d.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "Buffer.h"
#include "DeviceContext.h"
#include "EngineFactoryVk.h"
#include "GraphicsTypes.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "Sampler.h"
#include "Shader.h"
#include "ShaderResourceBinding.h"
#include "ShaderResourceVariable.h"
#include "Texture.h"

#include "shaders_3d.h"
#include "vetor.h"

#if defined(__ANDROID__)
#include <android/log.h>
#define AUREA_LOG(...) \
  __android_log_print(ANDROID_LOG_INFO, "aurea3d", __VA_ARGS__)
#else
#define AUREA_LOG(...) ((void)0)
#endif

namespace aurea::render::tresd {

// ======================================================= o dispositivo

namespace {

/// O APARELHO E DO PROCESSO, E NAO DE UM RENDERIZADOR (§20).
///
/// Abrir um segundo dispositivo Vulkan custa dezenas de milissegundos e leva
/// memoria que nao volta. Pior do que o custo: dois dispositivos no mesmo
/// processo NAO compartilham textura nenhuma, e a cena desenhada num deles
/// seria lida pelo outro como uma imagem preta. Um so, guardado.
struct Aparelho {
  bool tentou = false;
  bool ok = false;
  std::string motivo;
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> dispositivo;
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext> contexto;
};

Aparelho& aparelho() {
  static Aparelho a;
  return a;
}

}  // namespace

int preparar() {
  Aparelho& a = aparelho();
  if (a.tentou) return a.ok ? 1 : 0;
  a.tentou = true;

  auto* fabrica = Diligent::GetEngineFactoryVk();
  if (fabrica == nullptr) {
    a.motivo = "sem fabrica Vulkan";
    AUREA_LOG("preparar: %s", a.motivo.c_str());
    return 0;
  }

  Diligent::EngineVkCreateInfo ci;
  // SEM CAMADA DE VALIDACAO: ela existe no aparelho de quem programa e nao
  // no aparelho do dono, e um caminho que so funciona com validacao ligada e
  // um caminho que nao funciona.
  ci.EnableValidation = false;

  Diligent::IRenderDevice* dispositivo = nullptr;
  Diligent::IDeviceContext* contexto = nullptr;
  fabrica->CreateDeviceAndContextsVk(ci, &dispositivo, &contexto);
  if (dispositivo == nullptr || contexto == nullptr) {
    a.motivo = "CreateDeviceAndContextsVk devolveu nulo";
    AUREA_LOG("preparar: %s", a.motivo.c_str());
    return 0;
  }
  a.dispositivo = Diligent::RefCntAutoPtr<Diligent::IRenderDevice>{dispositivo};
  a.contexto = Diligent::RefCntAutoPtr<Diligent::IDeviceContext>{contexto};
  a.ok = true;
  AUREA_LOG("preparar: ok (%s)",
            dispositivo->GetDeviceInfo().Type ==
                    Diligent::RENDER_DEVICE_TYPE_VULKAN
                ? "Vulkan"
                : "outro");
  return 1;
}

bool pronto() { return aparelho().ok; }

const char* motivo() {
  const Aparelho& a = aparelho();
  return a.motivo.empty() ? "" : a.motivo.c_str();
}

const char* backend() { return pronto() ? "Diligent/Vulkan" : "nenhum"; }

// ============================================== os blocos de uniformes

namespace {

/// QUANTAS LUZES CABEM. Oito e o numero que o shader declara. Um limite
/// maior obrigaria a um segundo bloco e a um segundo caminho de codigo por
/// causa de um caso que nao existe numa cena de celular.
constexpr std::uint32_t kMaxLuzes = 8;

/// O BLOCO DO QUADRO — camera, luz da sombra, olho e ambiente. Ele e o
/// MESMO no vertice e no fragmento, e por isso e um so.
struct BlocoQuadro {
  geo::Mat4 vista;
  geo::Mat4 projecao;
  geo::Mat4 vista_projecao;
  geo::Mat4 luz_espaco;
  float olho[4];      // xyz olho, w sombra ligada
  float ambiente[4];  // rgb ambiente
  float ajustes[4];   // x luzes, y PCF, z lado do mapa, w inclinacao
};
static_assert(sizeof(BlocoQuadro) == 304, "o bloco do quadro mudou de tamanho");
static_assert(offsetof(BlocoQuadro, olho) == 256, "olho fora de lugar");
static_assert(offsetof(BlocoQuadro, ambiente) == 272, "ambiente fora de lugar");
static_assert(offsetof(BlocoQuadro, ajustes) == 288, "ajustes fora de lugar");

/// O BLOCO DE UM DESENHO. Um por chamada de desenho.
struct BlocoDesenho {
  geo::Mat4 mundo;
  geo::Mat4 normal;
  float cor_base[4];
  float parametros[4];  // metalico, rugosidade, forca emissiva, alfa corte
  float emissivo[4];    // rgb emissivo, w modo
  float bandeiras[4];   // cor, normal, metalico-rugosidade, oclusao
  float bandeiras2[4];  // emissiva, face dupla, reservado, base do osso
  float tinta[4];
};
static_assert(sizeof(BlocoDesenho) == 224,
              "o bloco do desenho mudou de tamanho");
static_assert(offsetof(BlocoDesenho, parametros) == 144, "parametros fora");
static_assert(offsetof(BlocoDesenho, emissivo) == 160, "emissivo fora");
static_assert(offsetof(BlocoDesenho, bandeiras) == 176, "bandeiras fora");
static_assert(offsetof(BlocoDesenho, bandeiras2) == 192, "bandeiras2 fora");
static_assert(offsetof(BlocoDesenho, tinta) == 208, "tinta fora");

struct BlocoLuz {
  float posicao_tipo[4];
  float direcao_alcance[4];
  float cor_intensidade[4];
  float cone[4];
};
static_assert(sizeof(BlocoLuz) == 64, "o bloco da luz mudou de tamanho");

struct BlocoLuzes {
  BlocoLuz luzes[kMaxLuzes];
};
static_assert(sizeof(BlocoLuzes) == 512, "o bloco das luzes mudou");

// ------------------------------------------------------------- utilidades

/// A COR DO PAINEL VIRA LINEAR, AQUI E UMA VEZ POR DESENHO.
///
/// A textura de cor ja chega na GPU marcada como sRGB e o driver a converte
/// ao amostrar (§6). A cor que o dono escolhe no painel nao passa por textura
/// nenhuma e precisa ser convertida em algum lugar — e o lugar e este, e nao
/// o shader: um `pow` por fragmento repetiria em milhoes de pixels uma conta
/// que nao muda dentro do desenho.
inline float canal_para_linear(std::uint8_t v) noexcept {
  const float s = static_cast<float>(v) * (1.0F / 255.0F);
  // A MESMA CURVA DA sRGB, e nao um `pow(x, 2.2)`: as duas quase coincidem
  // no meio e divergem nas pontas, e a diferenca apareceria como um material
  // um pouco mais escuro do que o mesmo material com a cor vindo da textura.
  return s <= 0.04045F ? s * (1.0F / 12.92F)
                       : std::pow((s + 0.055F) * (1.0F / 1.055F), 2.4F);
}

inline void cor_para_linear(const Cor& cor, float* saida) noexcept {
  saida[0] = canal_para_linear(cor.r);
  saida[1] = canal_para_linear(cor.g);
  saida[2] = canal_para_linear(cor.b);
  saida[3] = static_cast<float>(cor.a) * (1.0F / 255.0F);
}

/// O FORMATO DA COR DA COMPOSICAO 3D. RGBA8 e o mesmo do resto do app: um
/// alvo em ponto flutuante daria mais precisao no brilho e obrigaria a uma
/// conversao a mais na saida, e a composicao inteira ja e de 8 bits.
constexpr Diligent::TEXTURE_FORMAT kFormatoDeCor =
    Diligent::TEX_FORMAT_RGBA8_UNORM;

/// O FORMATO DA PROFUNDIDADE. Um so, escolhido aqui e nao em cada lugar:
/// misturar dois formatos entre o passe de sombra e o principal faria o
/// teste comparar laranja com banana.
constexpr Diligent::TEXTURE_FORMAT kFormatoDeProfundidade =
    Diligent::TEX_FORMAT_D32_FLOAT;

/// O MAPA DE SOMBRA GUARDA PROFUNDIDADE NUM CANAL DE COR — ver o comentario
/// longo no `shaders/sombra.frag` para o porque de nao ser um alvo de
/// profundidade lido como textura.
constexpr Diligent::TEXTURE_FORMAT kFormatoDaSombra =
    Diligent::TEX_FORMAT_R32_FLOAT;

/// O LADO DO MAPA DE SOMBRA POR QUALIDADE (§28). Uma janela de 4096 num
/// celular e um quarto da memoria de um aparelho modesto; ela existe porque
/// foi pedida, e nao porque seja barata.
std::uint32_t lado_da_sombra(QualidadeDaSombra q) noexcept {
  switch (q) {
    case QualidadeDaSombra::baixa:
      return 1024;
    case QualidadeDaSombra::media:
      return 2048;
    case QualidadeDaSombra::alta:
      return 4096;
    default:
      return 0;
  }
}

/// QUANTAS AMOSTRAS DE PCF POR EIXO. O shader faz `pcf x pcf`: 2 da 3x3 com
/// meio texel de raio, 3 da 3x3 com um texel, 5 da 5x5. Nao vale mais: o
/// custo e por fragmento e a borda ja ficou suave.
std::uint32_t pcf_da_sombra(QualidadeDaSombra q) noexcept {
  switch (q) {
    case QualidadeDaSombra::baixa:
      return 2;
    case QualidadeDaSombra::media:
      return 3;
    case QualidadeDaSombra::alta:
      return 5;
    default:
      return 2;
  }
}

/// A INCLINACAO DO MAPA DE SOMBRA, em unidades de profundidade ja projetada.
/// Sem ela a superficie que projeta a sombra se sombreia a si mesma e
/// aparecem listras diagonais — o defeito classico, e o que faz alguem achar
/// que "a sombra esta quebrada".
float inclinacao_da_sombra(std::uint32_t lado) noexcept {
  return lado >= 2048 ? 0.0016F : 0.0032F;
}

/// MISTURA OS BYTES DE UM PEDACO NO ACUMULADO. Ele e usado para decidir se
/// vale redesenhar — ver `impressao_do_ultimo_quadro`.
std::uint64_t misturar(std::uint64_t h, const void* dados,
                       std::size_t bytes) noexcept {
  const auto* p = static_cast<const unsigned char*>(dados);
  for (std::size_t i = 0; i < bytes; ++i) {
    h ^= p[i];
    h *= 0x100000001B3ULL;
  }
  return h;
}

/// A SEMENTE DA IMPRESSAO. Um numero primo arbitrario e grande; so precisa
/// ser sempre o mesmo.
constexpr std::uint64_t kSemente = 0xCBF29CE484222325ULL;

}  // namespace

// ==================================================== o estado interno

struct Renderizador3D::Interno {
  Diligent::IRenderDevice* dispositivo = nullptr;
  Diligent::IDeviceContext* contexto = nullptr;

  // ---------------------------------------------------------- os alvos
  Diligent::RefCntAutoPtr<Diligent::ITexture> cor;
  Diligent::RefCntAutoPtr<Diligent::ITexture> cor_msaa;
  Diligent::RefCntAutoPtr<Diligent::ITexture> profundidade;
  Diligent::RefCntAutoPtr<Diligent::ITexture> profundidade_msaa;
  std::uint32_t alvo_largura = 0;
  std::uint32_t alvo_altura = 0;
  std::uint32_t alvo_amostras = 1;

  Diligent::RefCntAutoPtr<Diligent::ITexture> sombra_cor;
  Diligent::RefCntAutoPtr<Diligent::ITexture> sombra_profundidade;
  std::uint32_t sombra_lado = 0;

  // ------------------------------------------------------ o intermediario
  Diligent::RefCntAutoPtr<Diligent::ITexture> leitura;
  std::vector<std::uint8_t> pixels;

  // ------------------------------------------------------------ o quadro
  Diligent::RefCntAutoPtr<Diligent::IBuffer> bloco_quadro;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> bloco_luzes;
  Diligent::RefCntAutoPtr<Diligent::IBuffer> bloco_ossos;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> vista_ossos;
  std::uint64_t bytes_dos_ossos = 0;
  std::vector<Diligent::RefCntAutoPtr<Diligent::IBuffer>> blocos_de_desenho;

  /// A IMPRESSAO DO ULTIMO QUADRO DESENHADO. Ela e calculada sobre os BYTES
  /// QUE FORAM PARA A GPU, e nao sobre o `Quadro3D`. A diferenca importa: um
  /// campo da cena que nao chega a virar bloco nao pode mudar o desenho, e
  /// inclui-lo faria redesenhar sem motivo; esquecer um campo que chega
  /// mostraria um quadro velho com a cena nova — e so o segundo defeito
  /// importa, por isso tudo o que entra no desenho entra na conta.
  std::uint64_t impressao_do_ultimo_quadro = 0;
  bool tem_quadro = false;

  // ---------------------------------------------------------- os modelos
  struct MalhaGpu {
    Diligent::RefCntAutoPtr<Diligent::IBuffer> vertices;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> indices;
    std::uint32_t quantidade = 0;
    std::uint64_t bytes = 0;
  };

  struct ModeloGpu {
    /// O DONO DO DADO. O acervo recicla alca, entao comparar so o numero nao
    /// basta: um modelo novo no mesmo lugar herdaria a geometria do antigo.
    /// Este ponteiro e a prova de que o que esta na GPU ainda e ele.
    const Modelo* fonte = nullptr;
    std::vector<MalhaGpu> malhas;
    /// Uma vista por textura do modelo, na mesma ordem. Entradas nulas
    /// significam "esta textura nao subiu" — e quem desenha cai na neutra.
    std::vector<Diligent::RefCntAutoPtr<Diligent::ITextureView>> texturas;
    std::uint64_t bytes = 0;
  };
  std::unordered_map<std::int32_t, ModeloGpu> modelos;

  // -------------------------------------------------------- o que e neutro
  Diligent::RefCntAutoPtr<Diligent::ITextureView> branca;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> branca_srgb;
  Diligent::RefCntAutoPtr<Diligent::ITextureView> normal_neutra;

  // --------------------------------------------------------- os pipelines
  struct PsoGpu {
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
  };
  /// A CHAVE E UM BITMAPA, e nao uma lista de combinacoes: deformada, duas
  /// faces, mistura e sombra sao quatro perguntas de sim ou nao, e uma chave
  /// assim nunca deixa de cobrir uma combinacao que apareca depois.
  std::unordered_map<std::uint32_t, PsoGpu> pipelines;

  // ---------------------------------------------------------- medicao
  Estatisticas3D stats{};
  std::string erro;

  // ------------------------------------------------------------- metodos
  bool criar_alvos(std::uint32_t largura, std::uint32_t altura,
                   std::uint32_t amostras);
  bool criar_sombra(std::uint32_t lado);
  bool criar_neutras();
  bool garantir_modelo(std::int32_t alca, const AcervoDeModelos& acervo);
  bool pegar_pso(std::uint32_t chave);
  bool preparar_blocos(const Quadro3D& quadro, const AcervoDeModelos& acervo,
                       std::uint64_t& impressao, std::uint32_t& desenhos);
  void desenhar_lista(const Quadro3D& quadro, const std::vector<Desenho>& lista,
                      std::size_t base, bool mistura, bool eh_sombra);
  bool ler_pixels();
};

// ================================================ criacao de recursos

namespace {

/// O LAYOUT DE VERTICE. Os `location` do shader sao estes numeros, e o
/// Diligent copia `InputIndex` direto para la — nao ha traducao no meio, e
/// por isso os dois lados usam o mesmo numero.
///
/// O DESLOCAMENTO E O PASSO SAEM DO `offsetof` E DO `sizeof`, e nao sao
/// numeros digitados. Um campo novo no `Vertice` desloca todos os que vem
/// depois, e um numero a mao erraria em silencio — o defeito apareceria como
/// uma normal lida do lugar da cor.
///
/// O PASSO PRECISA ESTAR EM CADA ELEMENTO. O Diligent o tira do PRIMEIRO
/// elemento de cada `BufferSlot` e exige que os outros concordem; deixar o
/// automatico faria ele calcular um passo empacotado, que nao e o do
/// `Vertice` — e os atributos sairiam deslocados a partir do segundo vertice.
const Diligent::LayoutElement* layout_de_vertice(std::uint32_t& quantos) {
  using D = Diligent::LayoutElement;
  constexpr auto passo = static_cast<Diligent::Uint32>(sizeof(Vertice));
  // A LISTA E ESTATICA, e nao local: ela e DEVOLVIDA por ponteiro e precisa
  // viver mais do que a chamada. Uma lista local daria um ponteiro para a
  // pilha de uma funcao que ja terminou — o layout seria lido de memoria
  // reaproveitada e os atributos sairiam de qualquer lugar.
  static const D elementos[] = {
      D{0, 0, 3, Diligent::VT_FLOAT32, false,
        static_cast<Diligent::Uint32>(offsetof(Vertice, posicao)), passo},
      D{1, 0, 3, Diligent::VT_FLOAT32, false,
        static_cast<Diligent::Uint32>(offsetof(Vertice, normal)), passo},
      D{2, 0, 2, Diligent::VT_FLOAT32, false,
        static_cast<Diligent::Uint32>(offsetof(Vertice, uv0)), passo},
      D{3, 0, 2, Diligent::VT_FLOAT32, false,
        static_cast<Diligent::Uint32>(offsetof(Vertice, uv1)), passo},
      D{4, 0, 4, Diligent::VT_FLOAT32, false,
        static_cast<Diligent::Uint32>(offsetof(Vertice, tangente)), passo},
      // A COR VAI NORMALIZADA: ela e de 8 bits na memoria e de 0 a 1 no
      // shader, e o `true` aqui e o que faz a GPU dividir por 255 sozinha.
      D{5, 0, 4, Diligent::VT_UINT8, true,
        static_cast<Diligent::Uint32>(offsetof(Vertice, cor)), passo},
      D{6, 0, 4, Diligent::VT_UINT16, false,
        static_cast<Diligent::Uint32>(offsetof(Vertice, ossos)), passo},
      D{7, 0, 4, Diligent::VT_FLOAT32, false,
        static_cast<Diligent::Uint32>(offsetof(Vertice, pesos)), passo},
  };
  quantos = static_cast<std::uint32_t>(sizeof(elementos) / sizeof(elementos[0]));
  return elementos;
}

Diligent::RefCntAutoPtr<Diligent::IBuffer> novo_buffer(
    Diligent::IRenderDevice* dispositivo, const char* nome,
    Diligent::BIND_FLAGS uso, Diligent::USAGE modo, std::uint64_t bytes,
    const void* dados, Diligent::BUFFER_MODE modo_do_buffer =
                            Diligent::BUFFER_MODE_UNDEFINED,
    std::uint32_t passo = 0) {
  Diligent::BufferDesc bd;
  bd.Name = nome;
  bd.BindFlags = uso;
  bd.Usage = modo;
  bd.Size = bytes;
  bd.Mode = modo_do_buffer;
  bd.ElementByteStride = passo;
  Diligent::BufferData bdata;
  bdata.pData = dados;
  bdata.DataSize = bytes;
  Diligent::IBuffer* bruto = nullptr;
  dispositivo->CreateBuffer(bd, dados != nullptr ? &bdata : nullptr, &bruto);
  return Diligent::RefCntAutoPtr<Diligent::IBuffer>{bruto};
}

/// UMA TEXTURA SEM MIPMAPS. As texturas de modelo vem do arquivo com o
/// tamanho que tem; gerar a cadeia de mipmaps custaria uma passada por
/// textura na subida e o resultado sem ela e so um pouco mais serrilhado de
/// longe. Um caminho com mipmaps e um degrau posterior e esta declarado.
Diligent::RefCntAutoPtr<Diligent::ITexture> nova_textura(
    Diligent::IRenderDevice* dispositivo, const char* nome, std::uint32_t l,
    std::uint32_t a, Diligent::TEXTURE_FORMAT formato,
    Diligent::BIND_FLAGS uso, const void* dados, std::uint32_t passo,
    std::uint32_t amostras = 1) {
  Diligent::TextureDesc td;
  td.Name = nome;
  td.Type = Diligent::RESOURCE_DIM_TEX_2D;
  td.Width = l;
  td.Height = a;
  td.Format = formato;
  td.MipLevels = 1;
  td.SampleCount = amostras;
  td.Usage = Diligent::USAGE_DEFAULT;
  td.BindFlags = uso;
  Diligent::TextureSubResData nivel;
  nivel.pData = dados;
  nivel.Stride = passo;
  Diligent::TextureData tdados;
  tdados.pSubResources = dados != nullptr ? &nivel : nullptr;
  tdados.NumSubresources = dados != nullptr ? 1 : 0;
  Diligent::ITexture* bruto = nullptr;
  dispositivo->CreateTexture(td, dados != nullptr ? &tdados : nullptr, &bruto);
  return Diligent::RefCntAutoPtr<Diligent::ITexture>{bruto};
}

}  // namespace

/// CRIA AS TEXTURAS NEUTRAS (§43). Um vazio vai para a GPU apesar de ter
/// quatro texels brancos, e a razao e que a falta de textura nao pode deixar
/// buraco nenhum no desenho: o modelo sem mapa de cor sai branco, e nao azul
/// de depuracao — o azul denuncia o defeito para quem programa e arruina o
/// quadro para quem usa.
bool Renderizador3D::Interno::criar_neutras() {
  if (branca && branca_srgb && normal_neutra) return true;

  const std::uint8_t branco[16] = {255, 255, 255, 255, 255, 255, 255, 255,
                                   255, 255, 255, 255, 255, 255, 255, 255};
  // A NORMAL "PARA FORA" E (128, 128, 255) — e nao preto. Um mapa de normais
  // ausente lido como preto da uma normal invalida e a superficie fica preta
  // de lado; lido como o plano neutro, nao muda nada.
  const std::uint8_t plano[16] = {128, 128, 255, 255, 128, 128, 255, 255,
                                  128, 128, 255, 255, 128, 128, 255, 255};

  auto t = nova_textura(dispositivo, "3d neutra branca", 2, 2,
                        Diligent::TEX_FORMAT_RGBA8_UNORM,
                        Diligent::BIND_SHADER_RESOURCE, branco, 8);
  if (!t) return false;
  branca = t->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);

  // A BRANCA EM sRGB E A MESMA COR, SO DECLARADA DIFERENTE. Ela existe para
  // o material sem textura de cor: se ele caísse na branca linear, o
  // `pow(2.2)` que o shader NAO faz sobre a textura... faria a cor base
  // sair multiplicada por 1 de qualquer jeito, e nada mudaria. O que muda e
  // o emissivo: um emissivo sem textura lido como 1 linear e o dobro do que
  // um emissivo com textura branca em sRGB entrega, e as duas cenas
  // pareceriam de programas diferentes.
  auto ts = nova_textura(dispositivo, "3d neutra branca srgb", 2, 2,
                         Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB,
                         Diligent::BIND_SHADER_RESOURCE, branco, 8);
  if (ts) {
    branca_srgb =
        ts->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  }

  auto tp = nova_textura(dispositivo, "3d neutra plano", 2, 2,
                         Diligent::TEX_FORMAT_RGBA8_UNORM,
                         Diligent::BIND_SHADER_RESOURCE, plano, 8);
  if (tp) {
    normal_neutra =
        tp->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  }

  return branca && normal_neutra;
}

bool Renderizador3D::Interno::criar_alvos(std::uint32_t largura,
                                          std::uint32_t altura,
                                          std::uint32_t amostras) {
  if (largura == 0 || altura == 0) {
    erro = "a composicao nao tem tamanho";
    return false;
  }

  // O ANTISSERRILHADO E CONFERIDO CONTRA O APARELHO, e nao confiado. Pedir
  // quatro amostras onde existem duas faz a criacao da textura devolver nulo,
  // e o sintoma seria a cena sumir por causa de um controle de qualidade — o
  // pior tipo de defeito, porque some quando alguem mexe no controle.
  if (amostras > 1) {
    const auto& info = dispositivo->GetTextureFormatInfoExt(kFormatoDeCor);
    if ((info.SampleCounts & static_cast<Diligent::SAMPLE_COUNT>(amostras)) ==
        0) {
      AUREA_LOG("alvo: %u amostras nao suportadas, usando 1", amostras);
      amostras = 1;
    }
  }

  if (cor && profundidade && leitura && alvo_largura == largura &&
      alvo_altura == altura && alvo_amostras == amostras) {
    return true;
  }

  cor_msaa.Release();
  profundidade_msaa.Release();
  cor.Release();
  profundidade.Release();
  leitura.Release();
  alvo_largura = 0;
  alvo_altura = 0;
  alvo_amostras = 1;

  // O ALVO DE COR E TAMBEM UMA TEXTURA AMOSTRAVEL: ele vai para a composicao
  // 2D, e a composicao le textura e nao alvo de desenho.
  cor = nova_textura(dispositivo, "3d cor", largura, altura, kFormatoDeCor,
                     Diligent::BIND_RENDER_TARGET |
                         Diligent::BIND_SHADER_RESOURCE,
                     nullptr, 0);
  if (!cor) {
    erro = "nao foi possivel criar o alvo de cor da 3D";
    return false;
  }

  profundidade = nova_textura(dispositivo, "3d profundidade", largura, altura,
                              kFormatoDeProfundidade,
                              Diligent::BIND_DEPTH_STENCIL, nullptr, 0);
  if (!profundidade) {
    erro = "nao foi possivel criar o alvo de profundidade da 3D";
    return false;
  }

  if (amostras > 1) {
    cor_msaa = nova_textura(dispositivo, "3d cor msaa", largura, altura,
                            kFormatoDeCor, Diligent::BIND_RENDER_TARGET,
                            nullptr, 0, amostras);
    profundidade_msaa =
        nova_textura(dispositivo, "3d profundidade msaa", largura, altura,
                     kFormatoDeProfundidade, Diligent::BIND_DEPTH_STENCIL,
                     nullptr, 0, amostras);
    if (!cor_msaa || !profundidade_msaa) {
      // SEM ALVO MULTIAMOSTRADO NAO SE DESENHA EM MULTIAMOSTRADO. Cair para
      // uma amostra perde a suavidade da borda; insistir seria nao desenhar
      // nada, que e pior.
      AUREA_LOG("alvo: sem alvo com %u amostras, usando 1", amostras);
      cor_msaa.Release();
      profundidade_msaa.Release();
      amostras = 1;
    }
  }

  // O INTERMEDIARIO DA RELEITURA. Ele e recriado junto com o alvo porque o
  // tamanho e o mesmo: guardar um intermediario do tamanho antigo e o
  // defeito classico de quem redimensiona — a copia sai do tamanho errado e
  // a imagem aparece cortada ao meio.
  //
  // ELE E `USAGE_STAGING` COM ACESSO DA CPU. Sem isso o `Map` recusa, e a
  // recusa acontece no meio de um quadro — o defeito aparece como "a 3D nao
  // aparece" sem nenhuma pista de que o problema era a textura de destino.
  {
    Diligent::TextureDesc td;
    td.Name = "3d leitura";
    td.Type = Diligent::RESOURCE_DIM_TEX_2D;
    td.Width = largura;
    td.Height = altura;
    td.Format = kFormatoDeCor;
    td.MipLevels = 1;
    td.SampleCount = 1;
    td.Usage = Diligent::USAGE_STAGING;
    td.BindFlags = Diligent::BIND_NONE;
    td.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    Diligent::ITexture* bruto = nullptr;
    dispositivo->CreateTexture(td, nullptr, &bruto);
    leitura = Diligent::RefCntAutoPtr<Diligent::ITexture>{bruto};
  }
  if (!leitura) {
    erro = "nao foi possivel criar a textura de leitura da 3D";
    return false;
  }

  pixels.assign(static_cast<std::size_t>(largura) * altura * 4, 0);
  alvo_largura = largura;
  alvo_altura = altura;
  alvo_amostras = amostras;
  stats.antisserrilhado = amostras > 1;
  tem_quadro = false;
  return true;
}

bool Renderizador3D::Interno::criar_sombra(std::uint32_t lado) {
  if (lado == 0) return false;
  if (sombra_cor && sombra_lado == lado) return true;

  sombra_cor.Release();
  sombra_profundidade.Release();
  sombra_lado = 0;

  // O FORMATO DE UM CANAL EM PONTO FLUTUANTE PODE NAO SER DESENHAVEL em todo
  // aparelho. Em vez de descobrir isso com uma tela preta, a pergunta e feita
  // ao aparelho — e um segundo formato, mais modesto, responde.
  Diligent::TEXTURE_FORMAT formato = kFormatoDaSombra;
  if ((dispositivo->GetTextureFormatInfoExt(formato).BindFlags &
       Diligent::BIND_RENDER_TARGET) == 0) {
    formato = Diligent::TEX_FORMAT_R16_FLOAT;
    AUREA_LOG("sombra: R32_FLOAT nao e desenhavel, usando R16_FLOAT");
  }

  sombra_cor = nova_textura(dispositivo, "3d sombra", lado, lado, formato,
                            Diligent::BIND_RENDER_TARGET |
                                Diligent::BIND_SHADER_RESOURCE,
                            nullptr, 0);
  sombra_profundidade =
      nova_textura(dispositivo, "3d sombra profundidade", lado, lado,
                   kFormatoDeProfundidade, Diligent::BIND_DEPTH_STENCIL,
                   nullptr, 0);
  if (!sombra_cor || !sombra_profundidade) {
    sombra_cor.Release();
    sombra_profundidade.Release();
    erro = "nao foi possivel criar o mapa de sombra";
    return false;
  }
  sombra_lado = lado;
  stats.lado_da_sombra = lado;
  return true;
}

// ------------------------------------------------------- os modelos

bool Renderizador3D::Interno::garantir_modelo(std::int32_t alca,
                                              const AcervoDeModelos& acervo) {
  const Modelo* m = acervo.obter(alca);
  if (m == nullptr) return false;

  auto achado = modelos.find(alca);
  if (achado != modelos.end() && achado->second.fonte == m) return true;

  // A GEOMETRIA ANTIGA SAI ANTES DA NOVA ENTRAR. Sem isto, reimportar o mesmo
  // modelo no mesmo lugar acumularia uma copia de vertices por vez — e o
  // crescimento seria lento o bastante para ninguem ligar ao modelo.
  modelos.erase(alca);

  ModeloGpu gpu;
  gpu.fonte = m;

  for (const Malha& malha : m->malhas) {
    MalhaGpu mg;
    if (!malha.vertices.empty()) {
      const std::uint64_t bytes_do_vertice =
          static_cast<std::uint64_t>(malha.vertices.size()) * sizeof(Vertice);
      // IMUTAVEL: a geometria nao muda depois de subir (§5). Um buffer
      // dinamico aqui obrigaria a um envio por quadro de vertices que nunca
      // mudam, que e exatamente o que o plano proibe (§24).
      mg.vertices = novo_buffer(dispositivo, "3d vertices",
                                Diligent::BIND_VERTEX_BUFFER,
                                Diligent::USAGE_IMMUTABLE, bytes_do_vertice,
                                malha.vertices.data());
      if (!mg.vertices) {
        erro = "nao foi possivel subir os vertices do modelo";
        return false;
      }
      mg.bytes += bytes_do_vertice;
    }
    if (!malha.indices.empty()) {
      const std::uint64_t bytes_do_indice =
          static_cast<std::uint64_t>(malha.indices.size()) * sizeof(std::uint32_t);
      mg.indices = novo_buffer(dispositivo, "3d indices",
                               Diligent::BIND_INDEX_BUFFER,
                               Diligent::USAGE_IMMUTABLE, bytes_do_indice,
                               malha.indices.data());
      if (!mg.indices) {
        erro = "nao foi possivel subir os indices do modelo";
        return false;
      }
      mg.quantidade = static_cast<std::uint32_t>(malha.indices.size());
      mg.bytes += bytes_do_indice;
    }
    gpu.bytes += mg.bytes;
    gpu.malhas.push_back(std::move(mg));
  }

  gpu.texturas.resize(m->texturas.size());
  for (std::size_t i = 0; i < m->texturas.size(); ++i) {
    const Textura& t = m->texturas[i];
    if (t.largura == 0 || t.altura == 0 ||
        t.pixels.size() < static_cast<std::size_t>(t.largura) * t.altura * 4) {
      continue;  // fica nula, e quem desenha usa a neutra
    }
    // O ESPACO DE COR VEM DA IMPORTACAO, e nao de um chute. Cor e emissivo
    // sao sRGB e a GPU converte na amostragem; normal, rugosidade e oclusao
    // sao lineares e converter de novo escureceria o relevo (§6).
    const Diligent::TEXTURE_FORMAT formato =
        t.srgb ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB
               : Diligent::TEX_FORMAT_RGBA8_UNORM;
    auto tex = nova_textura(dispositivo, "3d textura", t.largura, t.altura,
                            formato, Diligent::BIND_SHADER_RESOURCE,
                            t.pixels.data(), t.largura * 4);
    if (!tex) continue;
    gpu.texturas[i] =
        tex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    gpu.bytes += static_cast<std::uint64_t>(t.largura) * t.altura * 4;
  }

  modelos.emplace(alca, std::move(gpu));
  return true;
}

// ------------------------------------------------------- os pipelines

namespace {

/// AS CHAVES DE PIPELINE. Quatro perguntas de sim ou nao.
constexpr std::uint32_t kPsoPele = 1U << 0;
constexpr std::uint32_t kPsoDuasFaces = 1U << 1;
constexpr std::uint32_t kPsoMistura = 1U << 2;
constexpr std::uint32_t kPsoSombra = 1U << 3;

}  // namespace

bool Renderizador3D::Interno::pegar_pso(std::uint32_t chave) {
  if (pipelines.count(chave) != 0) return true;

  const bool pele = (chave & kPsoPele) != 0;
  const bool duas_faces = (chave & kPsoDuasFaces) != 0;
  const bool mistura = (chave & kPsoMistura) != 0;
  const bool eh_sombra = (chave & kPsoSombra) != 0;
  if (eh_sombra && !sombra_cor) {
    erro = "o passe de sombra foi pedido sem mapa de sombra";
    return false;
  }

  // O BYTECODE E SPIR-V JA COMPILADO, e nao fonte. O `SourceLanguage` fica no
  // padrao de proposito: quando `ByteCode` esta preenchido, o backend Vulkan
  // nem consulta esse campo — ele detecta o SPIR-V pelo proprio conteudo. O
  // que NAO se pode fazer e deixar `Source` com qualquer valor junto com o
  // `ByteCode`: a criacao recusa os dois ao mesmo tempo.
  Diligent::ShaderCreateInfo vs_ci;
  vs_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  if (eh_sombra) {
    vs_ci.Desc.Name = "sombra.vert";
    vs_ci.ByteCode = pele
                         ? static_cast<const void*>(aurea::shaders::kSombraVertPele)
                         : static_cast<const void*>(aurea::shaders::kSombraVert);
    vs_ci.ByteCodeSize = pele ? sizeof(aurea::shaders::kSombraVertPele)
                              : sizeof(aurea::shaders::kSombraVert);
  } else {
    vs_ci.Desc.Name = "pbr.vert";
    vs_ci.ByteCode = pele
                         ? static_cast<const void*>(aurea::shaders::kPbrVertPele)
                         : static_cast<const void*>(aurea::shaders::kPbrVert);
    vs_ci.ByteCodeSize = pele ? sizeof(aurea::shaders::kPbrVertPele)
                              : sizeof(aurea::shaders::kPbrVert);
  }

  Diligent::ShaderCreateInfo fs_ci;
  fs_ci.Desc.Name = eh_sombra ? "sombra.frag" : "pbr.frag";
  fs_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  fs_ci.ByteCode = eh_sombra
                       ? static_cast<const void*>(aurea::shaders::kSombraFrag)
                       : static_cast<const void*>(aurea::shaders::kPbrFrag);
  fs_ci.ByteCodeSize = eh_sombra ? sizeof(aurea::shaders::kSombraFrag)
                                 : sizeof(aurea::shaders::kPbrFrag);

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  Diligent::RefCntAutoPtr<Diligent::IShader> fs;
  {
    Diligent::IShader* bruto = nullptr;
    dispositivo->CreateShader(vs_ci, &bruto);
    vs = Diligent::RefCntAutoPtr<Diligent::IShader>{bruto};
    bruto = nullptr;
    dispositivo->CreateShader(fs_ci, &bruto);
    fs = Diligent::RefCntAutoPtr<Diligent::IShader>{bruto};
  }
  if (!vs || !fs) {
    erro = "nao foi possivel criar os shaders 3D";
    AUREA_LOG("pso: %s (%s)", erro.c_str(), eh_sombra ? "sombra" : "pbr");
    return false;
  }

  std::uint32_t quantos = 0;
  const Diligent::LayoutElement* elementos = layout_de_vertice(quantos);

  Diligent::GraphicsPipelineStateCreateInfo ci;
  ci.PSODesc.Name = eh_sombra ? "3d sombra" : "3d pbr";
  ci.pVS = vs;
  ci.pPS = fs;

  // TODAS AS VARIAVEIS SAO MUTAVEIS, e nao estaticas: uma estatica e fixada
  // uma vez no PSO e nao pode mudar entre desenhos — e a textura de cor muda
  // a cada material.
  ci.PSODesc.ResourceLayout.DefaultVariableType =
      Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

  // OS AMOSTRADORES SAO IMUTAVEIS E VEM NO PSO. Num shader GLSL a textura e o
  // amostrador sao um so (`sampler2D`), e o Diligent precisa que alguem diga
  // qual amostrador acompanha cada textura; declarar aqui e o que permite
  // trocar so a textura por desenho.
  Diligent::SamplerDesc sd_cor;
  sd_cor.MinFilter = Diligent::FILTER_TYPE_LINEAR;
  sd_cor.MagFilter = Diligent::FILTER_TYPE_LINEAR;
  sd_cor.MipFilter = Diligent::FILTER_TYPE_POINT;  // nao ha mipmaps
  sd_cor.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
  sd_cor.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
  sd_cor.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;

  Diligent::SamplerDesc sd_dado = sd_cor;
  // O MAPA DE DADO NAO REPETE: uma normal amostrada com repeticao na borda da
  // uv traria o texel do outro lado da imagem, e o relevo apareceria
  // atravessado numa faixa de um pixel.
  sd_dado.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
  sd_dado.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
  sd_dado.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;

  Diligent::SamplerDesc sd_sombra;
  // EM PONTO, E NAO EM BILINEAR. O PCF do shader ja tira as amostras nas
  // posicoes certas, e o bilinear numa textura de 32 bits custaria uma
  // interpolacao que nem todo celular tem garantida na Vulkan.
  sd_sombra.MinFilter = Diligent::FILTER_TYPE_POINT;
  sd_sombra.MagFilter = Diligent::FILTER_TYPE_POINT;
  sd_sombra.MipFilter = Diligent::FILTER_TYPE_POINT;
  sd_sombra.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
  sd_sombra.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
  sd_sombra.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;

  Diligent::ImmutableSamplerDesc amostradores[6];
  std::uint32_t quantos_amostradores = 0;
  if (!eh_sombra) {
    amostradores[quantos_amostradores++] = Diligent::ImmutableSamplerDesc{
        Diligent::SHADER_TYPE_PIXEL, "tex_cor", sd_cor};
    amostradores[quantos_amostradores++] = Diligent::ImmutableSamplerDesc{
        Diligent::SHADER_TYPE_PIXEL, "tex_normal", sd_dado};
    amostradores[quantos_amostradores++] = Diligent::ImmutableSamplerDesc{
        Diligent::SHADER_TYPE_PIXEL, "tex_metalico_rugosidade", sd_dado};
    amostradores[quantos_amostradores++] = Diligent::ImmutableSamplerDesc{
        Diligent::SHADER_TYPE_PIXEL, "tex_emissiva", sd_cor};
    amostradores[quantos_amostradores++] = Diligent::ImmutableSamplerDesc{
        Diligent::SHADER_TYPE_PIXEL, "tex_oclusao", sd_dado};
    amostradores[quantos_amostradores++] = Diligent::ImmutableSamplerDesc{
        Diligent::SHADER_TYPE_PIXEL, "tex_sombra", sd_sombra};
  }
  ci.PSODesc.ResourceLayout.NumImmutableSamplers = quantos_amostradores;
  ci.PSODesc.ResourceLayout.ImmutableSamplers =
      quantos_amostradores > 0 ? amostradores : nullptr;

  auto& g = ci.GraphicsPipeline;
  g.NumRenderTargets = 1;
  g.RTVFormats[0] = eh_sombra ? sombra_cor->GetDesc().Format : kFormatoDeCor;
  g.DSVFormat = kFormatoDeProfundidade;
  g.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  // AS AMOSTRAS DO PIPELINE TEM DE CASAR COM AS DO ALVO. Um pipeline de uma
  // amostra desenhando num alvo de quatro nao da erro: da lixo.
  g.SmplDesc.Count = static_cast<Diligent::Uint8>(eh_sombra ? 1 : alvo_amostras);
  g.SmplDesc.Quality = 0;
  g.InputLayout.LayoutElements = elementos;
  g.InputLayout.NumElements = quantos;

  // O LADO DE FORA E O DE DENTRO. O glTF define o triangulo com enrolamento
  // anti-horario visto de fora, e o Diligent nega a altura do viewport para
  // casar com a convencao do D3D — o que inverte o enrolamento aparente. Com
  // `FrontCounterClockwise = false` (o padrao) e `CULL_MODE_BACK`, o que
  // sobra desenhado e a face de fora, que e o certo.
  g.RasterizerDesc.CullMode =
      duas_faces ? Diligent::CULL_MODE_NONE : Diligent::CULL_MODE_BACK;
  g.RasterizerDesc.FrontCounterClockwise = false;

  // O TESTE DE PROFUNDIDADE ESTA SEMPRE LIGADO; o que muda e a ESCRITA. Um
  // transparente que escreve profundidade apaga tudo o que vier atras dele, e
  // a ordem de tras para frente nao salva — ela so resolve a mistura.
  g.DepthStencilDesc.DepthEnable = true;
  g.DepthStencilDesc.DepthWriteEnable = mistura ? false : true;
  g.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS;

  if (mistura) {
    auto& alvo = ci.GraphicsPipeline.BlendDesc.RenderTargets[0];
    alvo.BlendEnable = true;
    // A COR QUE CHEGA JA VEM MULTIPLICADA PELO ALFA — o `pbr.frag` faz isso.
    // Entao a soma e `cor*1 + destino*(1-alfa)`, que e a composicao "por
    // cima" correta. Com `SRC_ALPHA` aqui a cor seria multiplicada por alfa
    // DUAS vezes, e um vidro a 50% sairia a 25%.
    alvo.SrcBlend = Diligent::BLEND_FACTOR_ONE;
    alvo.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    alvo.BlendOp = Diligent::BLEND_OPERATION_ADD;
    // O ALFA TAMBEM E COBERTURA, e nao cor: `alfa*1 + destino*(1-alfa)`. Com
    // `SRC_ALPHA` aqui a cobertura seria multiplicada por ela mesma e duas
    // superficies translucidas empilhadas dariam um alfa menor do que a
    // ultima delas — e a composicao 2D escureceria o que esta atras.
    alvo.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    alvo.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    alvo.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
  }

  PsoGpu entrada;
  Diligent::IPipelineState* bruto = nullptr;
  dispositivo->CreatePipelineState(ci, &bruto);
  entrada.pso = Diligent::RefCntAutoPtr<Diligent::IPipelineState>{bruto};
  if (!entrada.pso) {
    erro = "nao foi possivel criar o pipeline 3D";
    AUREA_LOG("pso: %s chave=%u", erro.c_str(), chave);
    return false;
  }
  Diligent::IShaderResourceBinding* srb_bruto = nullptr;
  entrada.pso->CreateShaderResourceBinding(&srb_bruto, true);
  entrada.srb =
      Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>{srb_bruto};
  if (!entrada.srb) {
    erro = "nao foi possivel criar o conjunto de recursos do pipeline 3D";
    return false;
  }

  pipelines.emplace(chave, std::move(entrada));
  stats.pipelines = static_cast<std::uint32_t>(pipelines.size());
  return true;
}

// ================================================== o material no bloco

namespace {

/// O MATERIAL VIRA BLOCO. Um lugar so, usado pelos dois passes: uma segunda
/// copia desta conversao em outro caminho garantiria que um dia elas
/// divergissem.
void material_para_bloco(const Material& m, const Cor& tinta_da_camada,
                         std::uint32_t base_do_osso, const Desenho& d,
                         BlocoDesenho& saida) {
  std::memset(&saida, 0, sizeof(saida));

  saida.mundo = d.mundo;
  // A INVERSA TRANSPOSTA, e nao a inversa. O shader faz `normal * vec4(n, 0)`
  // — matriz vezes coluna — e o que isso precisa e da transposta da inversa.
  // Guardar so a inversa acerta enquanto a escala for uniforme e erra a luz
  // no primeiro objeto esticado.
  geo::Mat4 inverso;
  if (geo::inverter(d.mundo, inverso)) {
    saida.normal = geo::transposta(inverso);
  } else {
    // ESCALA ZERO EM ALGUM EIXO — o painel permite digitar, e o resultado e
    // uma matriz singular. Identidade em vez de NaN: um NaN aqui contamina a
    // normal e o objeto some, enquanto a identidade so nao deforma.
    saida.normal = geo::Mat4::identidade();
  }

  float base[4];
  cor_para_linear(m.cor_base, base);
  std::memcpy(saida.cor_base, base, sizeof(base));

  saida.parametros[0] = m.metalico;
  saida.parametros[1] = m.rugosidade;
  saida.parametros[2] = m.forca_emissiva;
  saida.parametros[3] = m.alfa_corte;

  float emissivo[4];
  cor_para_linear(m.emissivo, emissivo);
  std::memcpy(saida.emissivo, emissivo, sizeof(emissivo));
  saida.emissivo[3] = static_cast<float>(static_cast<int>(m.modo));

  saida.bandeiras[0] = m.textura_cor >= 0 ? 1.0F : 0.0F;
  saida.bandeiras[1] = m.textura_normal >= 0 ? 1.0F : 0.0F;
  saida.bandeiras[2] = m.textura_metalico_rugosidade >= 0 ? 1.0F : 0.0F;
  saida.bandeiras[3] = m.textura_oclusao >= 0 ? 1.0F : 0.0F;

  saida.bandeiras2[0] = m.textura_emissiva >= 0 ? 1.0F : 0.0F;
  saida.bandeiras2[1] = m.face_dupla ? 1.0F : 0.0F;
  saida.bandeiras2[2] = 1.0F;  // reservado
  saida.bandeiras2[3] = static_cast<float>(base_do_osso);

  float tinta[4];
  cor_para_linear(tinta_da_camada, tinta);
  std::memcpy(saida.tinta, tinta, sizeof(tinta));
  saida.tinta[3] = 1.0F;
}

/// A MATRIZ DA LUZ DIRECIONAL (§28).
///
/// A CAIXA COBRE A CENA INTEIRA, e nao o objeto principal: um mapa que so
/// cobrisse o objeto principal deixaria a sombra dos outros cortada na borda,
/// e o corte aparece como uma linha reta no chao — o defeito mais denunciador
/// de um mapa mal dimensionado.
bool montar_luz_espaco(const Quadro3D& quadro, geo::Mat4& saida) {
  if (quadro.luzes.empty() || quadro.limites.vazia) return false;

  // A PRIMEIRA DIRECIONAL E A QUE PROJETA SOMBRA. Sombrear por todas custaria
  // um passe e um mapa por luz, e num celular isso e o quadro inteiro.
  const Luz* sol = nullptr;
  for (const Luz& l : quadro.luzes) {
    if (l.ligada && l.tipo == TipoDeLuz::direcional) {
      sol = &l;
      break;
    }
  }
  if (sol == nullptr) return false;

  const geo::Vec3 centro = quadro.limites.centro();
  // A METADE DA DIAGONAL, e nao a maior aresta: a caixa da luz tem de conter
  // a esfera que envolve a cena, senao um canto sai do mapa quando o objeto
  // esta de esguelha.
  const float raio = std::max(
      geo::comprimento(quadro.limites.maximo - quadro.limites.minimo) * 0.5F,
      0.5F);

  const geo::Vec3 direcao = geo::normalizado(sol->direcao);
  // A CAMERA DA LUZ FICA DUAS VEZES O RAIO ATRAS DO CENTRO: uma vez para
  // alcancar o lado de tras da cena, e outra para nada ficar colado no plano
  // proximo.
  const geo::Vec3 de = centro - direcao * (raio * 2.0F);
  const geo::Vec3 cima = std::abs(direcao.y) > 0.99F
                             ? geo::Vec3{0.0F, 0.0F, 1.0F}
                             : geo::Vec3{0.0F, 1.0F, 0.0F};

  const geo::Mat4 vista = geo::olhar(de, centro, cima);
  const geo::Mat4 projecao =
      geo::ortografica(-raio, raio, -raio, raio, 0.01F, raio * 4.0F + 1.0F);
  saida = projecao * vista;
  return true;
}

}  // namespace

// ======================================================= os blocos

bool Renderizador3D::Interno::preparar_blocos(const Quadro3D& quadro,
                                              const AcervoDeModelos& acervo,
                                              std::uint64_t& impressao,
                                              std::uint32_t& desenhos) {
  desenhos = 0;
  impressao = kSemente;

  // -------------------------------------------------------------- a sombra
  //
  // O MAPA DE SOMBRA NASCE ANTES DO BLOCO, e nao depois: o bloco guarda o
  // lado do mapa e a inclinacao, e os dois sao do mapa que existe de verdade.
  // Montar o bloco com o lado pedido e criar um mapa menor seria uma
  // inclinacao calculada para o mapa errado.
  const std::uint32_t lado = lado_da_sombra(quadro.sombra);
  geo::Mat4 luz_espaco = geo::Mat4::identidade();
  bool sombra_ligada = false;
  if (lado > 0 && criar_sombra(lado) && montar_luz_espaco(quadro, luz_espaco)) {
    sombra_ligada = true;
  }
  stats.sombra_ligada = sombra_ligada;

  // -------------------------------------------------------------- o quadro
  BlocoQuadro bloco;
  std::memset(&bloco, 0, sizeof(bloco));
  bloco.vista = quadro.vista;
  bloco.projecao = quadro.projecao;
  bloco.vista_projecao = quadro.projecao * quadro.vista;
  bloco.luz_espaco = luz_espaco;
  bloco.olho[0] = quadro.olho.x;
  bloco.olho[1] = quadro.olho.y;
  bloco.olho[2] = quadro.olho.z;
  bloco.olho[3] = sombra_ligada ? 1.0F : 0.0F;
  bloco.ambiente[0] = quadro.ambiente[0];
  bloco.ambiente[1] = quadro.ambiente[1];
  bloco.ambiente[2] = quadro.ambiente[2];
  bloco.ambiente[3] = 1.0F;
  const std::size_t luzes_ligadas = static_cast<std::size_t>(
      std::count_if(quadro.luzes.begin(), quadro.luzes.end(),
                    [](const Luz& l) { return l.ligada; }));
  bloco.ajustes[0] = static_cast<float>(luzes_ligadas);
  bloco.ajustes[1] = static_cast<float>(pcf_da_sombra(quadro.sombra));
  bloco.ajustes[2] = static_cast<float>(sombra_lado);
  bloco.ajustes[3] =
      sombra_ligada ? inclinacao_da_sombra(sombra_lado) : 0.0F;

  if (!bloco_quadro) {
    bloco_quadro = novo_buffer(dispositivo, "3d quadro",
                               Diligent::BIND_UNIFORM_BUFFER,
                               Diligent::USAGE_DEFAULT, sizeof(BlocoQuadro),
                               nullptr);
    if (!bloco_quadro) {
      erro = "nao foi possivel criar o bloco do quadro";
      return false;
    }
  }
  contexto->UpdateBuffer(bloco_quadro, 0, sizeof(bloco), &bloco,
                         Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  impressao = misturar(impressao, &bloco, sizeof(bloco));

  // -------------------------------------------------------------- as luzes
  BlocoLuzes luzes;
  std::memset(&luzes, 0, sizeof(luzes));
  std::size_t quantas = 0;
  for (const Luz& l : quadro.luzes) {
    // UMA LUZ APAGADA NAO OCUPA LUGAR NO BLOCO. Se ela entrasse, o shader
    // somaria intensidade de uma luz que o dono desligou — e pior: o
    // `quantas` do bloco do quadro tem de contar exatamente as que vieram,
    // senao o shader leria lixo no fim da lista.
    if (!l.ligada) continue;
    if (quantas >= kMaxLuzes) break;
    BlocoLuz& b = luzes.luzes[quantas];
    ++quantas;
    b.posicao_tipo[0] = l.posicao.x;
    b.posicao_tipo[1] = l.posicao.y;
    b.posicao_tipo[2] = l.posicao.z;
    b.posicao_tipo[3] = static_cast<float>(static_cast<int>(l.tipo));

    const geo::Vec3 d = geo::normalizado(l.direcao);
    b.direcao_alcance[0] = d.x;
    b.direcao_alcance[1] = d.y;
    b.direcao_alcance[2] = d.z;
    b.direcao_alcance[3] = l.alcance;

    b.cor_intensidade[0] = l.vermelho;
    b.cor_intensidade[1] = l.verde;
    b.cor_intensidade[2] = l.azul;
    b.cor_intensidade[3] = l.intensidade;

    // O SHADER COMPARA COSSENOS, e nao angulos: o produto escalar ja da o
    // cosseno, e converter o angulo por fragmento seria um `acos` por pixel.
    b.cone[0] = std::cos(l.angulo_interno_graus * geo::kGrau);
    b.cone[1] = std::cos(l.angulo_externo_graus * geo::kGrau);
    b.cone[2] = 0.0F;
    b.cone[3] = 0.0F;
  }
  if (!bloco_luzes) {
    bloco_luzes = novo_buffer(dispositivo, "3d luzes",
                              Diligent::BIND_UNIFORM_BUFFER,
                              Diligent::USAGE_DEFAULT, sizeof(BlocoLuzes),
                              nullptr);
    if (!bloco_luzes) {
      erro = "nao foi possivel criar o bloco das luzes";
      return false;
    }
  }
  contexto->UpdateBuffer(bloco_luzes, 0, sizeof(luzes), &luzes,
                         Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  impressao = misturar(impressao, &luzes, sizeof(luzes));

  // --------------------------------------------------------------- os ossos
  //
  // UM BLOCO SO PARA TODAS AS CAMADAS DO QUADRO. Cada desenho esqueletico
  // aponta para a fatia dele por um numero no bloco de desenho, e nao por um
  // buffer proprio — e o que faz a pose de vinte personagens custar um envio
  // em vez de vinte (§39).
  const std::size_t ossos = quadro.pele.size();
  const std::uint64_t bytes_dos_ossos_novos =
      std::max<std::size_t>(1, ossos) * sizeof(geo::Mat4);

  if (!bloco_ossos || bytes_dos_ossos_novos > bytes_dos_ossos) {
    bloco_ossos.Release();
    vista_ossos.Release();
    // ESTRUTURADO, COM PASSO DE 64 BYTES: e o que faz um `mat4 ossos[]` num
    // `std430` casar com o buffer. Sem o passo o Diligent nao tem como saber
    // o tamanho de cada elemento.
    bloco_ossos = novo_buffer(dispositivo, "3d ossos",
                              Diligent::BIND_SHADER_RESOURCE,
                              Diligent::USAGE_DEFAULT, bytes_dos_ossos_novos,
                              nullptr, Diligent::BUFFER_MODE_STRUCTURED,
                              sizeof(geo::Mat4));
    if (!bloco_ossos) {
      erro = "nao foi possivel criar o bloco dos ossos";
      return false;
    }
    bytes_dos_ossos = bytes_dos_ossos_novos;
    vista_ossos =
        bloco_ossos->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
    if (!vista_ossos) {
      // Alguns aparelhos so criam a vista sob pedido explicito para um buffer
      // estruturado. Pedir aqui e mais barato do que descobrir com o modelo
      // animado parado na pose de repouso.
      Diligent::BufferViewDesc vd;
      vd.Name = "3d ossos vista";
      vd.ViewType = Diligent::BUFFER_VIEW_SHADER_RESOURCE;
      // O FORMATO FICA NO PADRAO (sem tipo, sem componentes). Um buffer
      // estruturado tem o formato do ELEMENTO, e nao um formato de texel —
      // preencher isso com um formato de imagem pediria uma vista formatada,
      // que e outra coisa e daria um erro obscuro na criacao.
      vd.ByteOffset = 0;
      vd.ByteWidth = static_cast<Diligent::Uint32>(bytes_dos_ossos);
      Diligent::IBufferView* bruto = nullptr;
      bloco_ossos->CreateView(vd, &bruto);
      vista_ossos = Diligent::RefCntAutoPtr<Diligent::IBufferView>{bruto};
    }
    if (!vista_ossos) {
      erro = "nao foi possivel criar a vista do bloco de ossos";
      return false;
    }
  }

  // UMA MATRIZ DE IDENTIDADE QUANDO NAO HA ESQUELETO NENHUM. O shader le o
  // bloco mesmo nos desenhos sem pele — o PSO sem `PELE` otimiza a leitura
  // fora, mas o recurso continua declarado, e um recurso declarado e nao
  // ligado faz o Diligent recusar o desenho.
  {
    std::vector<geo::Mat4> ossos_locais;
    if (ossos > 0) {
      ossos_locais = quadro.pele;
    } else {
      ossos_locais.assign(1, geo::Mat4::identidade());
    }
    contexto->UpdateBuffer(bloco_ossos, 0,
                           static_cast<std::uint64_t>(ossos_locais.size() *
                                                      sizeof(geo::Mat4)),
                           ossos_locais.data(),
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    impressao = misturar(impressao, ossos_locais.data(),
                         ossos_locais.size() * sizeof(geo::Mat4));
  }

  // ----------------------------------------------------------- os desenhos
  const std::size_t total = quadro.opacos.size() + quadro.transparentes.size();
  desenhos = static_cast<std::uint32_t>(total);
  if (blocos_de_desenho.size() < total) {
    // UM BUFFER POR DESENHO, e nao fatias de um buffer gigante. A fatia
    // exigiria alinhar cada deslocamento ao que o aparelho pede e conferir
    // isso em cada desenho; um buffer pequeno por desenho nao tem essa
    // pergunta, e o custo e uma alocacao a mais quando a cena cresce.
    blocos_de_desenho.resize(total);
  }

  std::size_t indice = 0;
  for (const std::vector<Desenho>* lista :
       {&quadro.opacos, &quadro.transparentes}) {
    for (const Desenho& d : *lista) {
      // A BASE DO OSSO E O PROPRIO `Desenho::pele`: a avaliacao ja empacotou
      // as poses numa lista so e ja gravou ali o primeiro osso desta malha.
      const std::uint32_t base_do_osso =
          d.pele > 0 ? static_cast<std::uint32_t>(d.pele) : 0U;

      BlocoDesenho bloco_do_desenho;
      material_para_bloco(d.material, d.cor, base_do_osso, d, bloco_do_desenho);
      impressao = misturar(impressao, &bloco_do_desenho, sizeof(bloco_do_desenho));

      // O QUE NAO ESTA NO BLOCO TAMBEM MUDA A IMAGEM: qual textura cada
      // mapa usa, e de qual modelo veio a geometria. Dois materiais com as
      // mesmas bandeiras e texturas diferentes tem o mesmo bloco, e sem esta
      // linha o segundo nunca seria redesenhado.
      const std::int32_t mapa[5] = {
          d.material.textura_cor, d.material.textura_normal,
          d.material.textura_metalico_rugosidade, d.material.textura_emissiva,
          d.material.textura_oclusao};
      impressao = misturar(impressao, mapa, sizeof(mapa));
      const Modelo* fonte = acervo.obter(d.modelo);
      impressao = misturar(impressao, &fonte, sizeof(fonte));
      const std::int32_t alca_da_malha[2] = {d.modelo, d.malha};
      impressao = misturar(impressao, alca_da_malha, sizeof(alca_da_malha));

      if (!blocos_de_desenho[indice]) {
        blocos_de_desenho[indice] =
            novo_buffer(dispositivo, "3d desenho",
                        Diligent::BIND_UNIFORM_BUFFER,
                        Diligent::USAGE_DEFAULT, sizeof(BlocoDesenho), nullptr);
        if (!blocos_de_desenho[indice]) {
          erro = "nao foi possivel criar o bloco do desenho";
          return false;
        }
      }
      contexto->UpdateBuffer(
          blocos_de_desenho[indice], 0, sizeof(bloco_do_desenho),
          &bloco_do_desenho, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      ++indice;
    }
  }

  // O TAMANHO E AS AMOSTRAS FAZEM PARTE DO QUE MUDA A IMAGEM: um alvo de
  // outro tamanho tem outro enquadramento, e reaproveitar o quadro antigo
  // mostraria a cena no formato errado.
  impressao = misturar(impressao, &quadro.largura, sizeof(quadro.largura));
  impressao = misturar(impressao, &quadro.altura, sizeof(quadro.altura));
  impressao = misturar(impressao, &alvo_amostras, sizeof(alvo_amostras));
  impressao = misturar(impressao, &total, sizeof(total));
  return true;
}

// ======================================================== o desenho

void Renderizador3D::Interno::desenhar_lista(const Quadro3D& quadro,
                                             const std::vector<Desenho>& lista,
                                             std::size_t base, bool mistura,
                                             bool eh_sombra) {
  std::uint32_t ultima_chave = 0xFFFFFFFFU;
  Diligent::IPipelineState* pso_atual = nullptr;
  Diligent::IShaderResourceBinding* srb_atual = nullptr;
  std::size_t indice = base;

  for (const Desenho& d : lista) {
    const std::size_t meu = indice;
    ++indice;
    if (d.modelo < 0 || d.malha < 0 || d.quantidade_indices == 0) continue;
    if (meu >= blocos_de_desenho.size() || !blocos_de_desenho[meu]) continue;

    auto modelo = modelos.find(d.modelo);
    if (modelo == modelos.end()) continue;
    ModeloGpu& gpu = modelo->second;
    if (static_cast<std::size_t>(d.malha) >= gpu.malhas.size()) continue;
    const MalhaGpu& malha = gpu.malhas[static_cast<std::size_t>(d.malha)];
    if (!malha.vertices || !malha.indices) continue;

    const std::uint32_t chave =
        (d.pele >= 0 ? kPsoPele : 0U) |
        (d.material.face_dupla ? kPsoDuasFaces : 0U) |
        (mistura ? kPsoMistura : 0U) | (eh_sombra ? kPsoSombra : 0U);
    if (chave != ultima_chave) {
      if (!pegar_pso(chave)) return;
      auto achado = pipelines.find(chave);
      if (achado == pipelines.end()) return;
      pso_atual = achado->second.pso;
      srb_atual = achado->second.srb;
      contexto->SetPipelineState(pso_atual);
      ultima_chave = chave;
    }
    if (pso_atual == nullptr || srb_atual == nullptr) continue;

    // ------------------------------------------------------ os blocos fixos
    //
    // O MESMO NOME EM DOIS ESTAGIOS SAO DUAS VARIAVEIS. O `Quadro` existe no
    // vertice e no fragmento, e o Diligent os trata como separados: ligar so
    // um deixaria o outro sem recurso e o desenho seria recusado.
    if (auto* v = srb_atual->GetVariableByName(Diligent::SHADER_TYPE_VERTEX,
                                               "Quadro")) {
      v->Set(bloco_quadro);
    }
    if (auto* v = srb_atual->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                               "Quadro")) {
      v->Set(bloco_quadro);
    }
    if (auto* v = srb_atual->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                               "Luzes")) {
      v->Set(bloco_luzes);
    }
    if (vista_ossos) {
      if (auto* v = srb_atual->GetVariableByName(Diligent::SHADER_TYPE_VERTEX,
                                                 "Ossos")) {
        v->Set(vista_ossos);
      }
    }

    // ------------------------------------------------ o que muda por pedaco
    auto* bloco = blocos_de_desenho[meu].RawPtr();
    if (auto* v = srb_atual->GetVariableByName(Diligent::SHADER_TYPE_VERTEX,
                                               "Desenho")) {
      v->Set(bloco);
    }
    if (auto* v = srb_atual->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                               "Desenho")) {
      v->Set(bloco);
    }

    if (!eh_sombra) {
      // AS SEIS TEXTURAS SAO SEMPRE LIGADAS, e as ausentes caem na neutra.
      // Deixar uma sem ligar faria o Diligent recusar o desenho inteiro — e o
      // material sem mapa de normais e a maioria, nao a excecao (§43).
      const Material& m = d.material;
      const auto escolher = [&](std::int32_t qual,
                                Diligent::ITextureView* neutra,
                                Diligent::ITextureView* neutra_srgb,
                                const char* nome) {
        Diligent::ITextureView* vista = nullptr;
        if (qual >= 0 && static_cast<std::size_t>(qual) < gpu.texturas.size()) {
          vista = gpu.texturas[static_cast<std::size_t>(qual)].RawPtr();
        }
        if (vista == nullptr) {
          vista = neutra_srgb != nullptr ? neutra_srgb : neutra;
        }
        if (vista == nullptr) return;
        if (auto* v = srb_atual->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                   nome)) {
          v->Set(vista);
        }
      };
      // COR E EMISSIVO CAEM NA BRANCA EM sRGB, E OS DADOS NA BRANCA LINEAR.
      // A diferenca aparece no emissivo: uma textura branca lida como linear
      // vale 1, e lida como sRGB vale 0,21 na pratica — e o mesmo material
      // sairia com dois brilhos diferentes conforme ele tivesse ou nao o
      // mapa, que e o tipo de incoerencia que ninguem liga a causa.
      escolher(m.textura_cor, branca.RawPtr(), branca_srgb.RawPtr(),
               "tex_cor");
      escolher(m.textura_normal, normal_neutra.RawPtr(), nullptr,
               "tex_normal");
      escolher(m.textura_metalico_rugosidade, branca.RawPtr(), nullptr,
               "tex_metalico_rugosidade");
      escolher(m.textura_emissiva, branca.RawPtr(), branca_srgb.RawPtr(),
               "tex_emissiva");
      escolher(m.textura_oclusao, branca.RawPtr(), nullptr, "tex_oclusao");
      if (auto* v = srb_atual->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                                 "tex_sombra")) {
        auto* vista =
            sombra_cor
                ? sombra_cor->GetDefaultView(
                      Diligent::TEXTURE_VIEW_SHADER_RESOURCE)
                : nullptr;
        if (vista != nullptr) v->Set(vista);
      }
    }

    contexto->CommitShaderResources(
        srb_atual, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::IBuffer* vb = malha.vertices.RawPtr();
    const Diligent::Uint64 deslocamento = 0;
    contexto->SetVertexBuffers(0, 1, &vb, &deslocamento,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    contexto->SetIndexBuffer(malha.indices, 0,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DrawIndexedAttribs da;
    da.NumIndices = d.quantidade_indices;
    da.IndexType = Diligent::VT_UINT32;
    da.Flags = Diligent::DRAW_FLAG_NONE;
    da.NumInstances = 1;
    // A FAIXA JA SABE ONDE COMECA. O primeiro indice e um INDICE, e nao um
    // deslocamento em bytes — trocar os dois leria o meio do buffer e o
    // modelo apareceria com pedacos trocados.
    da.FirstIndexLocation = d.primeiro_indice;
    da.BaseVertex = d.base_do_vertice;
    contexto->DrawIndexed(da);

    if (!eh_sombra) {
      ++stats.desenhos;
      if (d.pele >= 0) ++stats.desenhos_esqueleticos;
      stats.triangulos += d.quantidade_indices / 3;
    }
  }
}

bool Renderizador3D::Interno::ler_pixels() {
  if (!cor || !leitura) return false;

  // SE O QUADRO SAIU MULTIAMOSTRADO, ELE DESCE PARA UMA AMOSTRA ANTES DA
  // COPIA. Copiar um alvo de quatro amostras para um de uma nao e uma copia:
  // e uma resolucao, e o Diligent so a faz por um caminho proprio.
  if (alvo_amostras > 1 && cor_msaa) {
    Diligent::ResolveTextureSubresourceAttribs r;
    r.SrcMipLevel = 0;
    r.SrcSlice = 0;
    r.SrcTextureTransitionMode =
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    r.DstMipLevel = 0;
    r.DstSlice = 0;
    r.DstTextureTransitionMode =
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    r.Format = Diligent::TEX_FORMAT_UNKNOWN;
    contexto->ResolveTextureSubresource(cor_msaa, cor, r);
  }

  Diligent::CopyTextureAttribs cp;
  cp.pSrcTexture = cor;
  cp.SrcTextureTransitionMode =
      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
  cp.pDstTexture = leitura;
  cp.DstTextureTransitionMode =
      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
  contexto->CopyTexture(cp);

  // A ESPERA ACONTECE AQUI. O `Flush` submete o trabalho e o `Map` espera por
  // ele; sem o `Flush`, o mapa esperaria por uma fila que ainda nao recebeu
  // os comandos, e o quadro sairia vazio em alguns aparelhos e certo em
  // outros — o pior tipo de defeito, porque some na bancada.
  contexto->Flush();

  Diligent::MappedTextureSubresource mapeado;
  contexto->MapTextureSubresource(leitura, 0, 0, Diligent::MAP_READ,
                                  Diligent::MAP_FLAG_NONE, nullptr, mapeado);
  if (mapeado.pData == nullptr) {
    erro = "nao foi possivel ler o quadro da 3D";
    return false;
  }

  // O PASSO DAS LINHAS E DO APARELHO, E NAO O DA IMAGEM. Uma linha na memoria
  // pode ser maior do que a linha da imagem por causa do alinhamento exigido
  // pela placa; copiar `largura * 4` de uma vez daria a imagem na diagonal a
  // partir da segunda linha.
  const std::size_t linha = static_cast<std::size_t>(alvo_largura) * 4;
  const auto* origem = static_cast<const std::uint8_t*>(mapeado.pData);
  for (std::uint32_t y = 0; y < alvo_altura; ++y) {
    std::memcpy(pixels.data() + static_cast<std::size_t>(y) * linha,
                origem + static_cast<std::size_t>(y) * mapeado.Stride, linha);
  }
  contexto->UnmapTextureSubresource(leitura, 0, 0);
  return true;
}

// ========================================================= o renderizador

Renderizador3D::Renderizador3D() : interno_(new Interno()) {}

Renderizador3D::~Renderizador3D() { liberar(); }

std::unique_ptr<Renderizador3D> Renderizador3D::criar(std::string& erro) {
  if (!preparar()) {
    erro = motivo();
    if (erro.empty()) erro = "o dispositivo grafico nao subiu";
    return nullptr;
  }
  std::unique_ptr<Renderizador3D> r(new Renderizador3D());
  Aparelho& a = aparelho();
  r->interno_->dispositivo = a.dispositivo;
  r->interno_->contexto = a.contexto;
  if (!r->interno_->criar_neutras()) {
    erro = r->interno_->erro.empty() ? "recursos neutros" : r->interno_->erro;
    return nullptr;
  }
  return r;
}

bool Renderizador3D::desenhar(const Quadro3D& quadro,
                              const AcervoDeModelos& acervo) {
  Interno& s = *interno_;
  const auto comeco = std::chrono::steady_clock::now();
  s.stats.desenhos = 0;
  s.stats.desenhos_esqueleticos = 0;
  s.stats.triangulos = 0;
  s.stats.instancias = 0;
  s.erro.clear();

  if (!pronto() || s.dispositivo == nullptr) {
    s.erro = "o dispositivo grafico nao esta pronto";
    return false;
  }
  if (!s.criar_alvos(quadro.largura, quadro.altura, quadro.amostras)) {
    return false;
  }

  // O MODELO QUE AINDA NAO SUBIU SOBE AGORA. Quem quer que isso nao aconteca
  // no meio de um quadro chama `aquecer` antes (§26) — o caminho existe, e
  // este aqui e a rede de seguranca dele.
  for (const Desenho& d : quadro.opacos) s.garantir_modelo(d.modelo, acervo);
  for (const Desenho& d : quadro.transparentes) {
    s.garantir_modelo(d.modelo, acervo);
  }

  std::uint64_t impressao = 0;
  std::uint32_t quantos = 0;
  if (!s.preparar_blocos(quadro, acervo, impressao, quantos)) return false;

  // NADA MUDOU? NAO REDESENHA E NAO RELÊ.
  //
  // A releitura da GPU para a memoria e o passo mais caro do quadro inteiro:
  // ela obriga a esperar o trabalho da placa terminar. Numa cena parada —
  // um modelo que nao mudou de pose, ou o instante repetido do compositor —
  // repetir isso sessenta vezes por segundo e o que faz o app parecer pesado
  // sem motivo. A comparacao e sobre o que foi para a GPU, e nao sobre a
  // cena: o que nao chegou na placa nao pode ter mudado a imagem.
  if (s.tem_quadro && s.impressao_do_ultimo_quadro == impressao &&
      !s.pixels.empty()) {
    s.stats.ultimo_desenho_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - comeco)
            .count();
    s.stats.quadros += 1;
    return true;
  }

  // ------------------------------------------------ o alvo, limpo e ligado
  auto* alvo_cor =
      (s.alvo_amostras > 1 && s.cor_msaa)
          ? s.cor_msaa->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET)
          : s.cor->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
  auto* alvo_profundidade =
      (s.alvo_amostras > 1 && s.profundidade_msaa)
          ? s.profundidade_msaa->GetDefaultView(
                Diligent::TEXTURE_VIEW_DEPTH_STENCIL)
          : s.profundidade->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
  if (alvo_cor == nullptr || alvo_profundidade == nullptr) {
    s.erro = "o alvo da 3D nao tem vista";
    return false;
  }

  // O LIMPO E TRANSPARENTE E PREMULTIPLICADO: preto com alfa zero. A
  // composicao 2D ve isso como "aqui nao tem 3D" e deixa passar o que esta
  // atras — um limpo opaco taparia o video inteiro com um retangulo preto.
  const float preto[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  s.contexto->ClearRenderTarget(alvo_cor, preto,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  s.contexto->ClearDepthStencil(alvo_profundidade, Diligent::CLEAR_DEPTH_FLAG,
                                1.0F, 0,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  // ------------------------------------------------------------ a sombra
  //
  // ELA VEM ANTES, E NAO DEPOIS: o passe principal LE o mapa de sombra, e um
  // mapa do quadro anterior daria uma sombra que anda um quadro atras do
  // objeto — o defeito classico de quem sombreia depois de desenhar.
  //
  // SO A GEOMETRIA OPACA PROJETA SOMBRA. O `sombra.frag` nao tem `discard`,
  // entao um material mascarado projeta uma sombra solida em vez de recortada;
  // isso esta declarado como limite, e nao escondido.
  if (s.stats.sombra_ligada) {
    auto* vista_sombra =
        s.sombra_cor->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    auto* vista_sombra_prof =
        s.sombra_profundidade->GetDefaultView(
            Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
    if (vista_sombra != nullptr && vista_sombra_prof != nullptr) {
      const float um[4] = {1.0F, 0.0F, 0.0F, 0.0F};
      s.contexto->ClearRenderTarget(
          vista_sombra, um, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      s.contexto->ClearDepthStencil(
          vista_sombra_prof, Diligent::CLEAR_DEPTH_FLAG, 1.0F, 0,
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      s.contexto->SetRenderTargets(
          1, &vista_sombra, vista_sombra_prof,
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      s.desenhar_lista(quadro, quadro.opacos, 0, false, true);
    }
  }

  // -------------------------------------------------------------- o desenho
  s.contexto->SetRenderTargets(1, &alvo_cor, alvo_profundidade,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  s.desenhar_lista(quadro, quadro.opacos, 0, false, false);
  s.desenhar_lista(quadro, quadro.transparentes, quadro.opacos.size(), true,
                   false);

  // --------------------------------------------------------------- a leitura
  if (!s.ler_pixels()) return false;

  s.impressao_do_ultimo_quadro = impressao;
  s.tem_quadro = true;
  s.stats.quadros += 1;
  {
    std::size_t malhas = 0;
    std::size_t texturas = 0;
    std::uint64_t bytes = 0;
    for (const auto& par : s.modelos) {
      malhas += par.second.malhas.size();
      for (const auto& t : par.second.texturas) {
        if (t) ++texturas;
      }
      bytes += par.second.bytes;
    }
    s.stats.malhas_na_gpu = static_cast<std::uint32_t>(malhas);
    s.stats.texturas_na_gpu = static_cast<std::uint32_t>(texturas);
    s.stats.bytes_de_gpu = bytes;
  }
  s.stats.ultimo_desenho_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                comeco)
          .count();
  // UMA INSTANCIA POR DESENHO, PORQUE AINDA NAO HA INSTANCIAMENTO. O §19
  // pede o caminho, e enquanto ele nao existe o numero honesto e este: uma
  // instancia por chamada. Quando o instanciamento entrar, este contador e o
  // que mostra o ganho.
  s.stats.instancias = s.stats.desenhos;
  return true;
}

const std::uint8_t* Renderizador3D::pixels() const noexcept {
  return interno_->pixels.empty() ? nullptr : interno_->pixels.data();
}

std::uint32_t Renderizador3D::largura() const noexcept {
  return interno_->alvo_largura;
}

std::uint32_t Renderizador3D::altura() const noexcept {
  return interno_->alvo_altura;
}

std::uint32_t Renderizador3D::aquecer(const AcervoDeModelos& acervo) {
  Interno& s = *interno_;
  if (s.dispositivo == nullptr) return 0;

  // O AQUECIMENTO NAO DESENHA E NAO PRECISA DE QUADRO. Ele existe para o
  // primeiro quadro com o modelo novo nao pagar a subida — o defeito que se
  // quer evitar e o app travar no instante em que o modelo aparece (§26).
  // A ALCA VAI DE 1 ATE `quantidade()`, E NAO DE 0 ATE `quantidade() - 1`.
  // A alca e o indice mais um (`AcervoDeModelos::guardar`), entao o laco
  // pelo indice pedia a alca zero — que e o "nenhum" — e deixava o ULTIMO
  // modelo do acervo de fora, sempre. O sintoma seria o modelo que nunca
  // aquece, e so ele.
  const std::uint32_t quantos = acervo.quantidade();
  std::uint32_t subiram = 0;
  for (std::uint32_t i = 1; i <= quantos; ++i) {
    const auto alca = static_cast<std::int32_t>(i);
    const Modelo* m = acervo.obter(alca);
    if (m == nullptr) continue;
    auto achado = s.modelos.find(alca);
    if (achado != s.modelos.end() && achado->second.fonte == m) continue;
    if (s.garantir_modelo(alca, acervo)) {
      subiram += static_cast<std::uint32_t>(m->malhas.size());
    }
  }
  // A VISTA DO BLOCO DE OSSOS TAMBEM AQUECE AQUI: criar o buffer no meio do
  // primeiro quadro animado custaria a alocacao no caminho do dedo, que e
  // exatamente o que o aquecimento existe para tirar de la.
  if (subiram > 0) s.tem_quadro = false;
  return subiram;
}

std::uint32_t Renderizador3D::limpar(const AcervoDeModelos& acervo) {
  Interno& s = *interno_;
  std::uint32_t saiu = 0;
  for (auto it = s.modelos.begin(); it != s.modelos.end();) {
    // A ALCA NAO BASTA COMO PROVA. O acervo recicla o numero quando um modelo
    // e apagado, e comparar so a alca manteria viva a geometria de um modelo
    // que ja nao existe — o ponteiro e o que responde "ainda e ele?".
    if (acervo.obter(it->first) != it->second.fonte) {
      it = s.modelos.erase(it);
      ++saiu;
    } else {
      ++it;
    }
  }
  if (saiu > 0) s.tem_quadro = false;
  return saiu;
}

void Renderizador3D::liberar() noexcept {
  if (!interno_) return;
  Interno& s = *interno_;
  // A ORDEM E A INVERSA DA CRIACAO, e nao por estetica: as vistas de textura
  // e as vistas do buffer de ossos sao filhas dos objetos que as criaram, e
  // soltar o pai antes da filha deixaria uma referencia pendurada.
  s.pipelines.clear();
  s.modelos.clear();
  s.blocos_de_desenho.clear();
  s.vista_ossos.Release();
  s.bloco_ossos.Release();
  s.bloco_luzes.Release();
  s.bloco_quadro.Release();
  s.normal_neutra.Release();
  s.branca_srgb.Release();
  s.branca.Release();
  s.leitura.Release();
  s.profundidade_msaa.Release();
  s.profundidade.Release();
  s.cor_msaa.Release();
  s.cor.Release();
  s.sombra_profundidade.Release();
  s.sombra_cor.Release();
  s.pixels.clear();
  s.pixels.shrink_to_fit();
  s.alvo_largura = 0;
  s.alvo_altura = 0;
  s.alvo_amostras = 1;
  s.sombra_lado = 0;
  s.bytes_dos_ossos = 0;
  s.tem_quadro = false;
  s.impressao_do_ultimo_quadro = 0;
  s.stats = Estatisticas3D{};
}

bool Renderizador3D::modelo_na_gpu(std::int32_t alca,
                                   const AcervoDeModelos& acervo) const noexcept {
  const Interno& s = *interno_;
  const Modelo* m = acervo.obter(alca);
  if (m == nullptr) return false;
  const auto achado = s.modelos.find(alca);
  return achado != s.modelos.end() && achado->second.fonte == m;
}

const char* Renderizador3D::ultimo_erro() const noexcept {
  return interno_->erro.c_str();
}

Estatisticas3D Renderizador3D::estatisticas() const noexcept {
  return interno_->stats;
}

std::uint64_t Renderizador3D::memoria_de_gpu() const noexcept {
  // O TOTAL DA PLACA, E NAO O QUE ESTA LIVRE: perguntar o que esta livre nao
  // tem resposta confiavel na Vulkan e na Metal, e um numero inventado seria
  // pior do que nenhum. Zero quando o aparelho nao informa, e ai quem chama
  // decide pelo proprio teto.
  const Aparelho& a = aparelho();
  if (a.dispositivo == nullptr) return 0;
  return a.dispositivo->GetAdapterInfo().Memory.LocalMemory;
}

}  // namespace aurea::render::tresd
