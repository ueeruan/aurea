// A PORTA DO RENDERCORE PARA O FLUTTER.
//
// ============================ A REGRA UNICA ============================
// NENHUMA EXCECAO ATRAVESSA O FFI. Uma excecao C++ que chega do outro lado
// do `dart:ffi` nao vira erro do Dart: ela atravessa uma fronteira onde
// nao ha tratamento, e o processo morre com SIGABRT sem mensagem. Por
// isso TODO simbolo `extern "C"` deste arquivo fecha o corpo num
// `try { ... } catch (...)` e devolve um codigo. Nao ha excecao a esta
// regra, nem nos getters de duas linhas.
//
// ============================ O SEGUNDO TRATADO ========================
// NADA DE `dart:ffi` APONTA PARA MEMORIA VIVA DO C++ DEPOIS DA CHAMADA.
// O ponteiro da cena e copiado para dentro do nucleo antes do retorno; o
// `void*` do handle e a unica coisa que sobrevive, e ele so morre em
// `fechar`. Um ponteiro guardado do lado do Dart para um `vector` que
// realocou e a segunda forma classica de derrubar o app por aqui.
#include "backend_vulkan.h"
#include "nucleo.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace aurea::render;

/// O SIMBOLO TEM DE SAIR DA BIBLIOTECA.
///
/// No Windows, `extern "C"` sozinho NAO exporta de uma DLL: sem
/// `__declspec(dllexport)` o simbolo existe dentro dela e nao tem nome
/// para o `dart:ffi` achar — e o erro que chega e "error code 127", que
/// nao diz nada sobre exportacao. Nas outras plataformas o padrao ja e
/// exportar, mas a visibilidade explicita protege contra um build com
/// `-fvisibility=hidden`.
#if defined(_WIN32)
#define AUREA_API __declspec(dllexport)
#else
#define AUREA_API __attribute__((visibility("default")))
#endif

namespace {

/// A CENA, COMO O DART A MONTA. Campo por campo, sem ponteiro.
///
/// TUDO DE 4 BYTES, E NENHUM `double`: o alinhamento fica previsivel dos
/// dois lados e nao ha buraco de preenchimento para os dois compiladores
/// discordarem. O Dart declara esta mesma struct em `aurea_render.dart`.
struct CamadaC {
  std::uint32_t tipo;
  std::uint32_t textura;
  float x;
  float y;
  float largura;
  float altura;
  float ancora_x;
  float ancora_y;
  float escala_x;
  float escala_y;
  float rotacao_graus;
  float opacidade;
  std::uint32_t mistura;
  std::uint32_t cor;
};
// SO DE 4 BYTES: nao ha alinhamento a respeitar e nao ha buraco possivel.
static_assert(sizeof(CamadaC) == 56, "CamadaC mudou de tamanho: a ABI quebrou");

/// OS DOIS INTEIROS VEM NO FIM, E ISSO NAO E ESTILO.
///
/// Um `int32_t` no meio de uma struct de `double` abre quatro bytes de
/// preenchimento que cada compilador posiciona como quer. Com os nove
/// doubles primeiro, o layout e 72 bytes de double seguidos de dois
/// inteiros — sem buraco nenhum, e o `static_assert` abaixo e a garantia
/// de que ninguem reordena isto sem perceber. O Dart declara os campos na
/// MESMA ordem, e ha teste conferindo o tamanho dos dois lados.
struct CurvaC {
  double x1, y1, x2, y2;
  double suavidade, intensidade, resposta, amortecimento, velocidade_inicial;
  std::int32_t tipo;
  std::int32_t contagem;
};
static_assert(sizeof(CurvaC) == 80, "CurvaC mudou de tamanho: a ABI quebrou");

struct KeyframeC {
  double tempo_s;
  double valor;
  CurvaC curva;
};
static_assert(sizeof(KeyframeC) == 96,
              "KeyframeC mudou de tamanho: a ABI quebrou");

/// O HANDLE. Um `void*` opaco para o Dart, e um ponteiro cru para o
/// C++ — a posse e do chamador, que chama `fechar` uma vez. O nome existe
/// para o `static_cast` nao virar um `reinterpret_cast` solto.
Nucleo* como_nucleo(void* p) noexcept { return static_cast<Nucleo*>(p); }

Curva converter(const CurvaC& c) noexcept {
  Curva k;
  k.tipo = static_cast<TipoDeCurva>(c.tipo);
  k.x1 = c.x1;
  k.y1 = c.y1;
  k.x2 = c.x2;
  k.y2 = c.y2;
  k.contagem = c.contagem;
  k.suavidade = c.suavidade;
  k.intensidade = c.intensidade;
  k.resposta = c.resposta;
  k.amortecimento = c.amortecimento;
  k.velocidade_inicial = c.velocidade_inicial;
  return k;
}
}  // namespace

extern "C" {

/// A VERSAO DA PORTA. Sobe quando a ABI muda de forma — um campo a mais
/// numa struct, uma ordem diferente no vetor de estatisticas. O lado Dart
/// confere antes de usar qualquer coisa.
AUREA_API std::int32_t aurea_render_versao(void) {
  try {
    return 4;
  } catch (...) {
    return -1;
  }
}

/// O TAMANHO DAS STRUCTS DA PORTA.
///
/// UM `Struct` DO `dart:ffi` MAL DECLARADO NAO AVISA: ele le o campo do
/// vizinho e devolve um numero que parece plausivel. O teste compara estes
/// tres numeros com o `sizeOf` do lado Dart, e uma divergencia aparece
/// como falha de teste em vez de como cor na tela errada.
AUREA_API std::uint32_t aurea_render_tamanho_camada(void) {
  try {
    return static_cast<std::uint32_t>(sizeof(CamadaC));
  } catch (...) {
    return 0;
  }
}

AUREA_API std::uint32_t aurea_render_tamanho_curva(void) {
  try {
    return static_cast<std::uint32_t>(sizeof(CurvaC));
  } catch (...) {
    return 0;
  }
}

AUREA_API std::uint32_t aurea_render_tamanho_keyframe(void) {
  try {
    return static_cast<std::uint32_t>(sizeof(KeyframeC));
  } catch (...) {
    return 0;
  }
}

/// O NOME DO BACKEND EM USO. Ponteiro para uma string estatica dentro da
/// biblioteca: NAO pertence a quem chama e nao pode ser liberado.
AUREA_API const char* aurea_render_backend(void* n) {
  try {
    if (n == nullptr) return "?";
    return nome_do_backend(como_nucleo(n)->backend()).data();
  } catch (...) {
    return "?";
  }
}

/// O MOTIVO DA ULTIMA FALHA, EM TEXTO.
///
/// UM `nullptr` NAO E UM DIAGNOSTICO. Quando a GPU nao sobe, alguem
/// precisa dizer POR QUE — e o "por que" tem de chegar ate a tela ou ate
/// o relatorio de bug, e nao morrer num `if (n == nullptr)` que so sabe
/// cair no caminho antigo. Custa duzentos bytes estaticos e transforma
/// "nao abriu" em "backend nao implementado: vulkan".
///
/// NAO E THREAD-SAFE, e nao precisa ser: a abertura acontece uma vez, na
/// thread que monta o editor, antes de qualquer render.
char g_motivo[192] = "";

void anotar_motivo(const char* texto) noexcept {
  std::size_t i = 0;
  if (texto != nullptr) {
    for (; texto[i] != 0 && i + 1 < sizeof(g_motivo); ++i) {
      g_motivo[i] = texto[i];
    }
  }
  g_motivo[i] = 0;
}

/// O NOME DO BACKEND PEDIDO, para o motivo ficar legivel.
const char* nome_do_backend_pedido(std::uint32_t b) noexcept {
  switch (b) {
    case 0: return "referencia";
    case 1: return "metal";
    case 2: return "vulkan";
    case 3: return "gles";
    default: return "desconhecido";
  }
}

/// ABRE O NUCLEO. Devolve 0 quando falha.
///
/// [backend]: 0 referencia (CPU), 1 metal, 2 vulkan, 3 gles. SO O 0 EXISTE
/// HOJE. Pedir outro devolve 0 — e nao um nucleo de mentira que diz ser
/// de GPU. O chamador cai no caminho antigo, e [aurea_render_ultimo_erro]
/// diz por que.
AUREA_API void* aurea_render_abrir(std::uint32_t largura, std::uint32_t altura,
                         std::uint64_t orcamento_de_recursos,
                         std::uint32_t backend, std::int32_t com_thread,
                         std::uint32_t amostras, double escala_interna,
                         double orcamento_ms) {
  try {
    if (backend != 0) {
      // A MENSAGEM DIZ O NOME DO BACKEND, e nao um numero: quem le o
      // relatorio nao tem a tabela de codigos na mao.
      std::string m = "backend nao implementado: ";
      m += nome_do_backend_pedido(backend);
      anotar_motivo(m.c_str());
      return nullptr;
    }
    if (largura == 0 || altura == 0) {
      anotar_motivo("largura ou altura zero");
      return nullptr;
    }
    Configuracao c;
    c.largura = largura;
    c.altura = altura;
    c.orcamento_de_recursos_bytes = orcamento_de_recursos;
    c.backend = Backend::referencia;
    c.com_thread = com_thread != 0;
    c.qualidade.amostras = amostras;
    c.qualidade.escala_interna = escala_interna;
    c.qualidade.orcamento_ms = orcamento_ms;
    auto r = Nucleo::abrir(c);
    if (r.tem_erro()) {
      std::string m = "o nucleo nao abriu: ";
      m += std::string(nome_do_erro(r.erro()));
      anotar_motivo(m.c_str());
      return nullptr;
    }
    anotar_motivo("");
    return static_cast<void*>(std::move(r).valor().release());
  } catch (const std::exception& e) {
    anotar_motivo(e.what());
    return nullptr;
  } catch (...) {
    anotar_motivo("excecao desconhecida ao abrir");
    return nullptr;
  }
}

/// A SONDA DO VULKAN — O QUE O APARELHO TEM, EM TEXTO.
///
/// NAO COMPOE NADA E NAO ABRE NUCLEO NENHUM. Ela sobe o Vulkan, pergunta
/// o nome do dispositivo, o driver, a fila grafica, o teto de textura e a
/// extensao de `AHardwareBuffer`, e desce. Existe para nao se escrever
/// swapchain em cima de um chute — e para o relatorio de bug de um
/// aparelho que nao roda poder dizer o que falta.
///
/// Devolve os bytes escritos (sem contar o terminador), ou negativo:
/// -1 sem buffer, -2 sem Vulkan.
AUREA_API std::int32_t aurea_render_vulkan_sonda(char* saida,
                                                 std::uint32_t capacidade) {
  try {
    if (saida == nullptr || capacidade == 0) return -1;
    const SondaVulkan s = sondar_vulkan();
    const std::string texto = s.resumo();
    const std::uint32_t cabem = capacidade - 1;
    const auto n = static_cast<std::uint32_t>(
        texto.size() < cabem ? texto.size() : cabem);
    for (std::uint32_t i = 0; i < n; ++i) saida[i] = texto[i];
    saida[n] = 0;
    return static_cast<std::int32_t>(n);
  } catch (const std::exception& e) {
    if (saida != nullptr && capacidade > 0) {
      std::string m = std::string("excecao na sonda: ") + e.what();
      const std::uint32_t cabem = capacidade - 1;
      const auto n = static_cast<std::uint32_t>(
          m.size() < cabem ? m.size() : cabem);
      for (std::uint32_t i = 0; i < n; ++i) saida[i] = m[i];
      saida[n] = 0;
    }
    return -3;
  } catch (...) {
    return -4;
  }
}

/// A SONDA RESPONDEU QUE HA VULKAN DE VERDADE?
AUREA_API std::int32_t aurea_render_vulkan_disponivel(void) {
  try {
    return sondar_vulkan().disponivel ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

/// O PORQUE DA ULTIMA ABERTURA TER FALHADO. String vazia = deu certo.
/// Ponteiro para memoria estatica: nao pertence a quem chama.
AUREA_API const char* aurea_render_ultimo_erro(void) {
  try {
    return g_motivo;
  } catch (...) {
    return "";
  }
}

AUREA_API void aurea_render_fechar(void* n) {
  try {
    delete como_nucleo(n);
  } catch (...) {
    // Fechar nunca pode falhar para quem chama: um `delete` que estoura
    // deixa o processo sem a unica oportunidade de soltar a memoria.
  }
}

/// PUBLICA A CENA. As camadas SAO COPIADAS aqui — o Dart pode soltar o
/// buffer assim que a chamada voltar.
///
/// [impressao]: um numero que so muda quando a cena muda. Zero desliga o
/// reaproveitamento (todo quadro e desenhado).
AUREA_API std::int32_t aurea_render_publicar_cena(void* n, const CamadaC* camadas,
                                        std::uint32_t quantas,
                                        std::uint32_t largura,
                                        std::uint32_t altura,
                                        std::uint64_t impressao) {
  try {
    if (n == nullptr) return -1;
    if (camadas == nullptr && quantas > 0) return -2;
    auto cena = std::make_shared<Cena>();
    cena->largura = largura;
    cena->altura = altura;
    cena->impressao = impressao;
    cena->camadas.reserve(quantas);
    for (std::uint32_t i = 0; i < quantas; ++i) {
      const CamadaC& s = camadas[i];
      Camada c;
      c.tipo = static_cast<TipoDeCamada>(s.tipo);
      c.textura = s.textura;
      c.x = s.x;
      c.y = s.y;
      c.largura = s.largura;
      c.altura = s.altura;
      c.ancora_x = s.ancora_x;
      c.ancora_y = s.ancora_y;
      c.escala_x = s.escala_x;
      c.escala_y = s.escala_y;
      c.rotacao_graus = s.rotacao_graus;
      c.opacidade = s.opacidade;
      c.mistura = static_cast<Mistura>(s.mistura);
      c.cor = Cor::de_argb(s.cor);
      cena->camadas.push_back(c);
    }
    como_nucleo(n)->publicar_cena(std::move(cena));
    return 0;
  } catch (...) {
    return -3;
  }
}

/// REGISTRA UMA TEXTURA. Copia os pixels: o buffer do Dart pode morrer
/// depois da chamada. Devolve o ID, ou 0 quando nao coube no orcamento.
AUREA_API std::uint32_t aurea_render_registrar_textura(void* n, const std::uint8_t* rgba,
                                             std::uint32_t largura,
                                             std::uint32_t altura) {
  try {
    if (n == nullptr || rgba == nullptr) return 0;
    const std::size_t bytes =
        static_cast<std::size_t>(largura) * altura * 4;
    std::vector<std::uint8_t> copia(rgba, rgba + bytes);
    auto r = como_nucleo(n)->registrar_textura(largura, altura,
                                               std::move(copia));
    if (r.tem_erro()) return 0;
    return r.valor();
  } catch (...) {
    return 0;
  }
}

AUREA_API void aurea_render_pedir_quadro(void* n) {
  try {
    if (n != nullptr) como_nucleo(n)->pedir_quadro();
  } catch (...) {
  }
}

/// DESENHA AGORA, NA THREAD DE QUEM CHAMOU. Backend de referencia e
/// bancada. Devolve o numero de camadas compostas, ou negativo.
AUREA_API std::int32_t aurea_render_desenhar_agora(void* n) {
  try {
    if (n == nullptr) return -1;
    auto r = como_nucleo(n)->desenhar_agora();
    if (r.tem_erro()) return -2;
    return static_cast<std::int32_t>(r.valor());
  } catch (...) {
    return -3;
  }
}

/// LE O ULTIMO QUADRO EM RGBA8 NAO-PREMULTIPLICADO.
///
/// CAMINHO DE TESTE E DE BANCADA. Em producao o quadro vai para a tela
/// pela superficie do backend; se isto aparecer num play, o zero-copy
/// acabou. Devolve quantos bytes escreveu.
AUREA_API std::uint32_t aurea_render_ler_pixels(void* n, std::uint8_t* destino,
                                      std::uint32_t capacidade) {
  try {
    if (n == nullptr || destino == nullptr) return 0;
    std::vector<std::uint8_t> pixels;
    auto r = como_nucleo(n)->ler_pixels(pixels);
    if (r.tem_erro()) return 0;
    const std::uint32_t n_bytes =
        static_cast<std::uint32_t>(pixels.size());
    const std::uint32_t escrever = n_bytes < capacidade ? n_bytes : capacidade;
    std::memcpy(destino, pixels.data(), escrever);
    return escrever;
  } catch (...) {
    return 0;
  }
}

/// AS ESTATISTICAS NUM VETOR DE DOUBLES, NA ORDEM FIXA ABAIXO.
///
/// UM VETOR, E NAO UMA STRUCT: uma struct com `double` e `uint32_t`
/// misturados tem preenchimento de alinhamento, e o preenchimento e
/// decidido por cada compilador. Um campo a mais de um lado e o Dart le o
/// numero do vizinho — um bug que nao avisa. Aqui nao ha o que errar: e
/// uma sequencia de doubles, e a ordem e o contrato (espelhada em
/// `aurea_render.dart` e conferida pelo teste de estatisticas).
///
///  0 cpu_ultimo_ms        9 quadros_reaproveitados   18 shaders_reaproveitados
///  1 cpu_mediana_ms      10 quadros_falhos           19 shaders_falhas
///  2 cpu_p95_ms          11 bytes_em_uso             20 camadas_desenhadas
///  3 cpu_max_ms          12 bytes_orcamento          21 pixels_escritos
///  4 intervalo_mediana   13 bytes_pico               22 amostras_por_pixel
///  5 fps_efetivo         14 recursos_vivos           23 orcamento_ms
///  6 quadros             15 recursos_despejados      24 escala_interna
///  7 quadros_atrasados   16 recursos_reaproveitados  25 quadros_na_janela
///  8 quadros_pulados     17 recursos_recusados       26 shaders_compilados
///                                                       27 recursos_criados
AUREA_API std::uint32_t aurea_render_estatisticas(void* n, double* saida,
                                        std::uint32_t capacidade) {
  constexpr std::uint32_t kCampos = 28;
  try {
    if (n == nullptr || saida == nullptr) return 0;
    const auto e = como_nucleo(n)->estatisticas();
    const double valores[kCampos] = {
        e.quadro.cpu_ultimo_ms,
        e.quadro.cpu_mediana_ms,
        e.quadro.cpu_p95_ms,
        e.quadro.cpu_max_ms,
        e.quadro.intervalo_mediana_ms,
        e.quadro.fps_efetivo,
        static_cast<double>(e.quadro.quadros),
        static_cast<double>(e.quadro.quadros_atrasados),
        static_cast<double>(e.quadro.quadros_pulados),
        static_cast<double>(e.quadros_reaproveitados),
        static_cast<double>(e.quadros_falhos),
        static_cast<double>(e.recursos.bytes_em_uso),
        static_cast<double>(e.recursos.bytes_orcamento),
        static_cast<double>(e.recursos.bytes_pico),
        static_cast<double>(e.recursos.vivos),
        static_cast<double>(e.recursos.despejados),
        static_cast<double>(e.recursos.reaproveitados),
        static_cast<double>(e.recursos.recusados),
        static_cast<double>(e.shaders.reaproveitados),
        static_cast<double>(e.shaders.falhas),
        static_cast<double>(e.compositor.camadas_desenhadas),
        static_cast<double>(e.compositor.pixels_escritos),
        static_cast<double>(e.compositor.amostras_por_pixel),
        e.qualidade.orcamento_ms,
        e.qualidade.escala_interna,
        static_cast<double>(e.quadro.quadros_na_janela),
        static_cast<double>(e.shaders.compilados),
        static_cast<double>(e.recursos.criados),
    };
    const std::uint32_t escrever =
        kCampos < capacidade ? kCampos : capacidade;
    for (std::uint32_t i = 0; i < escrever; ++i) saida[i] = valores[i];
    return escrever;
  } catch (...) {
    return 0;
  }
}

AUREA_API void aurea_render_definir_qualidade(void* n, std::uint32_t amostras,
                                    double escala_interna,
                                    double orcamento_ms) {
  try {
    if (n == nullptr) return;
    Qualidade q;
    q.amostras = amostras;
    q.escala_interna = escala_interna;
    q.orcamento_ms = orcamento_ms;
    como_nucleo(n)->definir_qualidade(q);
  } catch (...) {
  }
}

AUREA_API void aurea_render_definir_automatica(void* n, std::int32_t ligada) {
  try {
    if (n != nullptr) como_nucleo(n)->definir_automatica(ligada != 0);
  } catch (...) {
  }
}

/// COMPILA SHADERS EM LOTE, FORA DO QUADRO.
///
/// Tres vetores paralelos (nome, fonte, versao) em vez de um vetor de
/// structs: strings de tamanho variavel numa struct obrigariam o Dart a
/// alocar ponteiros, e ponteiro para memoria do Dart e a segunda forma de
/// derrubar o processo por aqui.
AUREA_API std::int32_t aurea_render_pre_aquecer(void* n, const char* const* nomes,
                                      const char* const* fontes,
                                      const std::uint32_t* versoes,
                                      std::uint32_t quantos) {
  try {
    if (n == nullptr) return -1;
    if (quantos > 0 && (nomes == nullptr || fontes == nullptr)) return -2;
    std::vector<DescricaoDoShader> lista;
    lista.reserve(quantos);
    for (std::uint32_t i = 0; i < quantos; ++i) {
      DescricaoDoShader d;
      d.nome = nomes[i] != nullptr ? nomes[i] : "";
      d.fonte = fontes[i] != nullptr ? fontes[i] : "";
      d.versao = versoes != nullptr ? versoes[i] : 1;
      lista.push_back(std::move(d));
    }
    auto r = como_nucleo(n)->pre_aquecer(lista);
    if (r.tem_erro()) return -1;
    return static_cast<std::int32_t>(r.valor());
  } catch (...) {
    return -3;
  }
}

/// AVALIA A PILHA DE KEYFRAMES NUM INSTANTE.
///
/// E a mesma conta do Dart, e o teste compara as duas. Nao depende de
/// nucleo aberto: e uma funcao pura, e por isso pode ser chamada milhares
/// de vezes no teste sem custo de ciclo de vida.
AUREA_API double aurea_render_avaliar(const KeyframeC* quadros, std::uint32_t quantos,
                            double tempo_s, double base,
                            std::int32_t* espelhado) {
  try {
    if (quadros == nullptr || quantos == 0) {
      if (espelhado != nullptr) *espelhado = 0;
      return base;
    }
    std::vector<Keyframe> lista;
    lista.reserve(quantos);
    for (std::uint32_t i = 0; i < quantos; ++i) {
      Keyframe k;
      k.tempo_s = quadros[i].tempo_s;
      k.valor = quadros[i].valor;
      k.curva = converter(quadros[i].curva);
      lista.push_back(k);
    }
    const auto r = avaliar(lista, tempo_s, base);
    if (espelhado != nullptr) *espelhado = r.espelhado ? 1 : 0;
    return r.valor;
  } catch (...) {
    if (espelhado != nullptr) *espelhado = 0;
    return base;
  }
}

/// A CURVA SOZINHA. O teste de divergencia compara curva a curva, e uma
/// curva errada some dentro de uma interpolacao — separada, ela aparece.
AUREA_API double aurea_render_curva(const CurvaC* c, double t) {
  try {
    if (c == nullptr) return t;
    return transformar_curva(converter(*c), t);
  } catch (...) {
    return t;
  }
}

}  // extern "C"
