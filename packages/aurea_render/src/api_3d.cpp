// A PORTA DO 3D: DILIGENT + VULKAN + ASSIMP.
//
// ============================ O QUE ESTA FATIA FAZ =====================
// Ela e a UNICA coisa que o Dart ve do motor 3D. Do outro lado dela
// existem o importador (Assimp), o acervo de modelos, o avaliador da cena
// e o renderizador (Diligent). Deste lado existem structs POD e inteiros.
//
// ============================ AS TRES REGRAS ==========================
//
//  1. NENHUMA EXCECAO ATRAVESSA O FFI. Uma excecao C++ que chega do outro
//     lado do `dart:ffi` nao vira erro do Dart: ela atravessa uma fronteira
//     onde nao ha tratamento, e o processo morre com SIGABRT sem mensagem.
//     Por isso TODO simbolo `extern "C"` deste arquivo fecha o corpo num
//     `try { ... } catch (...) }` e devolve um codigo — a mesma regra do
//     `api.cpp`, sem excecao nem nos getters de duas linhas.
//
//  2. NADA DE `dart:ffi` APONTA PARA MEMORIA VIVA DO C++ DEPOIS DA
//     CHAMADA. Duas excecoes declaradas: os pixels do ultimo quadro (que
//     valem ate o proximo desenho) e as strings de motivo e erro (que sao
//     estaticas dentro da biblioteca). A cena que entra e COPIADA.
//
//  3. O ESTADO E DO PROCESSO, E NAO DA CHAMADA. Existe UM acervo e UM
//     renderizador para o processo inteiro, e eles nascem na primeira
//     pergunta. Abrir dois dispositivos Vulkan nao compartilha textura
//     nenhuma entre eles — o resultado seria uma cena desenhada num
//     contexto que ninguem le (§20).
//
// ============================ O DESENHO ===============================
// `aurea_render_3d_desenhar` recebe a cena inteira numa struct e faz, nesta
// ordem: converte para o `Cena3D` do C++, avalia (`avaliar`) para o
// `Quadro3D` — que e onde a timeline vira matriz, material e pose — e
// manda o renderizador desenhar e reler os pixels. O Dart entao pega esses
// pixels e os registra como textura na composicao 2D, que e como o 3D
// vira camada em vez de janela (§14).
#include "api_3d.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

#include "base.h"
#include "cena_3d.h"
#include "importador.h"
#include "modelo.h"
#include "renderizador_3d.h"

#if defined(__ANDROID__)
#include <android/log.h>
#define AUREA_LOG(...) \
  __android_log_print(ANDROID_LOG_INFO, "aurea3d", __VA_ARGS__)
#else
#define AUREA_LOG(...) ((void)0)
#endif

// No Windows, `extern "C"` sozinho nao exporta de uma DLL — o mesmo motivo
// escrito no `api.cpp`.
#if defined(_WIN32)
#define AUREA_API __declspec(dllexport)
#else
#define AUREA_API __attribute__((visibility("default")))
#endif

namespace aurea::render::tresd {

namespace {

// A VERSAO DESTA PORTA. Sobe quando a ABI muda de forma — um campo a mais
// numa struct, um simbolo a menos. O lado Dart confere antes de usar.
//
//   1: preparar/pronto/motivo/backend. Nao desenhava nada.
//   2: a cena inteira, o acervo, a importacao em dois tempos, as
//      estatisticas e os pixels.
constexpr std::int32_t kVersaoDaPorta = 2;

// OS TETOS DE SANIDADE DA ENTRADA. Nao sao limites de projeto: sao a
// defesa contra um contador corrompido, que sem eles faria o motor varrer
// memoria alheia. Uma cena de verdade tem dezenas de camadas e uma duzia
// de luzes.
constexpr std::uint32_t kMaxCamadas = 4096;
constexpr std::uint32_t kMaxLuzes = 64;

/// O ACERVO DO PROCESSO. Ele guarda o dado PURO (vertices, materiais,
/// texturas, ossos, clipes) e nao sabe nada de GPU.
AcervoDeModelos& acervo() {
  static AcervoDeModelos a;
  return a;
}

/// UMA CARGA PENDENTE: o modelo lido e ainda NAO adotado.
///
/// Ela existe pelo §26. Ler um GLB de 200 MB com o Assimp leva segundos, e
/// segundos no thread da interface sao o app travado. Entao a leitura
/// acontece na thread de fundo e devolve isto — um objeto que NAO esta em
/// lugar nenhum compartilhado. A adocao acontece depois, na thread
/// principal, e e um `move`.
struct Carga {
  Modelo modelo;
  RelatoDaImportacao relato;
};

std::unordered_map<void*, std::unique_ptr<Carga>>& cargas() {
  static std::unordered_map<void*, std::unique_ptr<Carga>> c;
  return c;
}

/// O RENDERIZADOR DO PROCESSO. Um so, pela razao escrita no cabecalho: um
/// segundo dispositivo Vulkan nao compartilha textura nenhuma com o
/// primeiro.
std::unique_ptr<Renderizador3D>& renderizador() {
  static std::unique_ptr<Renderizador3D> r;
  return r;
}

/// O RENDERIZADOR, CRIANDO-O NA PRIMEIRA PERGUNTA. Quando o aparelho nao
/// tem Vulkan isto e chamado a cada quadro, e continua barato: o
/// `preparar` guarda a resposta e nao tenta de novo.
Renderizador3D* motor(std::string& erro) {
  auto& r = renderizador();
  if (r == nullptr) r = Renderizador3D::criar(erro);
  return r.get();
}

/// O ULTIMO ERRO DA PROPRIA PORTA — o que nao vem do renderizador (uma
/// importacao recusada, uma cena invalida). A funcao publica prefere o
/// erro do renderizador quando existe, porque ele e mais especifico.
///
/// `thread_local` pela mesma razao do aviso: quem LE o arquivo e a thread
/// de fundo e a mensagem nasce la. Quem a mostra e a interface, e ela
/// recebe o texto junto com o resultado da leitura — nao por uma variavel
/// que as duas escreveriam ao mesmo tempo.
std::string& erro_da_porta() {
  static thread_local std::string e;
  return e;
}

/// A MENSAGEM DO PRIMEIRO AVISO DA ULTIMA LEITURA. Ela nao cabe na struct
/// do relato — um caminho de arquivo de 4 KB num campo fixo obrigaria a
/// cortar, e um aviso cortado nao e o aviso.
///
/// `thread_local`, E NAO `static`, POR CAUSA DOS DOIS TEMPOS DA IMPORTACAO.
/// A leitura acontece na thread de fundo e ela e quem TEM o aviso; a thread
/// da interface so pergunta depois, no `adotar`. Com uma variavel unica as
/// duas threads escreveriam e leriam o mesmo `std::string` sem trava — o
/// aviso de uma importacao podia chegar no meio da outra, com a memoria
/// sendo liberada debaixo de quem le. Cada thread tem a sua copia, e a
/// passagem do aviso de uma para a outra e explicita, no `adotar`.
std::string& ultimo_aviso() {
  static thread_local std::string a;
  return a;
}

/// OS NUMEROS DO ULTIMO QUADRO AVALIADO. Eles vivem aqui, e nao no
/// renderizador, porque quem avalia a cena e esta porta — e a pergunta
/// "por que o modelo sumiu" se responde com eles.
struct NumerosDoQuadro {
  std::uint32_t camadas = 0;
  std::uint32_t desenhadas = 0;
  std::uint32_t fora_do_campo = 0;
  std::uint32_t sem_modelo = 0;
  std::uint32_t triangulos = 0;
  std::uint32_t vertices = 0;
  std::uint32_t ossos = 0;
  std::uint32_t luzes = 0;
  /// SE O ULTIMO QUADRO TINHA GEOMETRIA NENHUMA. Sem isto o Dart nao sabe
  /// distinguir "o 3D desenhou preto" de "nao havia 3D" — e a diferenca
  /// importa: no primeiro caso a camada 3D entra opaca na composicao.
  bool sem_geometria = true;
};

NumerosDoQuadro& numeros() {
  static NumerosDoQuadro n;
  return n;
}

// ------------------------------------------------------------ saneamento

/// UM NUMERO QUE NAO E NUMERO NAO ENTRA NA CONTA. Um `NaN` vindo de uma
/// curva quebrada nao estoura: ele se espalha por toda a matriz e a cena
/// inteira desaparece, sem erro nenhum. Prender na entrada e a diferenca
/// entre "a camada ficou estranha" e "a tela ficou preta" (§43).
[[nodiscard]] float finito(float v, float padrao) noexcept {
  return std::isfinite(v) ? v : padrao;
}

[[nodiscard]] float entre(float v, float lo, float hi, float padrao) noexcept {
  if (!std::isfinite(v)) return padrao;
  return v < lo ? lo : (v > hi ? hi : v);
}

[[nodiscard]] Vec3 de_vetor(const float v[3]) noexcept {
  return Vec3{finito(v[0], 0.0F), finito(v[1], 0.0F), finito(v[2], 0.0F)};
}

[[nodiscard]] Cor de_cor(const std::uint8_t c[4]) noexcept {
  return Cor{c[0], c[1], c[2], c[3]};
}

[[nodiscard]] MaterialDaCamada de_material(const Aurea3DMaterial& m) noexcept {
  MaterialDaCamada s;
  s.ligado = m.ligado != 0;
  s.cor_base = de_cor(m.cor_base);
  s.metalico = finito(m.metalico, -1.0F);
  s.rugosidade = finito(m.rugosidade, -1.0F);
  s.forca_emissiva = finito(m.forca_emissiva, -1.0F);
  s.emissivo = de_cor(m.emissivo);
  // O MODO SO VALE -1 OU 0..2. Um numero fora da faixa viraria um valor de
  // enumeracao invalido, e um `switch` sem `default` sobre ele e um
  // comportamento indefinido — que num celular aparece como cor errada e
  // nao como falha.
  s.modo = (m.modo >= 0 && m.modo <= 2) ? m.modo : -1;
  s.face_dupla = m.face_dupla != 0;
  s.alfa_corte = finito(m.alfa_corte, -1.0F);
  s.sem_textura_de_cor = m.sem_textura_de_cor != 0;
  return s;
}

[[nodiscard]] Camera de_camera(const Aurea3DCamera& c) noexcept {
  Camera d;
  d.posicao = de_vetor(c.posicao);
  d.alvo = de_vetor(c.alvo);
  d.rotacao_graus = de_vetor(c.rotacao);
  d.usar_rotacao = c.usar_rotacao != 0;
  // A CIMA NAO PODE SER DEGENERADA. Um vetor cima paralelo a direcao de
  // olhar faz o produto vetorial do `olhar` dar zero, e a matriz de vista
  // sai com linhas nulas — a cena some inteira.
  const Vec3 cima = de_vetor(c.cima);
  d.cima = (std::fabs(cima.x) + std::fabs(cima.y) + std::fabs(cima.z)) < 1.0e-6F
               ? Vec3{0.0F, 1.0F, 0.0F}
               : cima;
  d.fov_graus = entre(c.fov_graus, 1.0F, 179.0F, 45.0F);
  d.perto = entre(c.perto, 1.0e-4F, 1.0e4F, 0.05F);
  d.longe = entre(c.longe, d.perto + 1.0e-3F, 1.0e6F, 500.0F);
  d.ortografica = c.ortografica != 0;
  d.altura_ortografica = entre(c.altura_ortografica, 1.0e-4F, 1.0e6F, 4.0F);
  return d;
}

[[nodiscard]] Luz de_luz(const Aurea3DLuz& l) noexcept {
  Luz d;
  d.tipo = (l.tipo >= 0 && l.tipo <= 2)
               ? static_cast<TipoDeLuz>(l.tipo)
               : TipoDeLuz::direcional;
  d.posicao = de_vetor(l.posicao);
  d.direcao = de_vetor(l.direcao);
  d.vermelho = entre(l.cor[0], 0.0F, 1024.0F, 1.0F);
  d.verde = entre(l.cor[1], 0.0F, 1024.0F, 1.0F);
  d.azul = entre(l.cor[2], 0.0F, 1024.0F, 1.0F);
  d.intensidade = entre(l.intensidade, 0.0F, 1024.0F, 1.0F);
  d.alcance = entre(l.alcance, 0.0F, 1.0e6F, 0.0F);
  d.angulo_interno_graus = entre(l.angulo_interno_graus, 0.0F, 89.0F, 20.0F);
  d.angulo_externo_graus =
      entre(l.angulo_externo_graus, d.angulo_interno_graus, 89.9F, 35.0F);
  d.ligada = l.ligada != 0;
  return d;
}

/// A CENA DO DART VIRA A CENA DO C++. Devolve falso quando o ponteiro nao
/// serve — e a cena invalida NAO derruba nada, ela so nao desenha (§43).
[[nodiscard]] bool de_cena(const Aurea3DCena* c, Cena3D& saida) {
  if (c == nullptr) return false;
  if (c->quantidade_de_camadas > kMaxCamadas) return false;
  if (c->quantidade_de_luzes > kMaxLuzes) return false;
  if (c->quantidade_de_camadas > 0 && c->camadas == nullptr) return false;
  if (c->quantidade_de_luzes > 0 && c->luzes == nullptr) return false;
  if (c->largura == 0 || c->altura == 0) return false;

  saida.camera = de_camera(c->camera);
  saida.ambiente_vermelho = entre(c->ambiente[0], 0.0F, 64.0F, 0.0F);
  saida.ambiente_verde = entre(c->ambiente[1], 0.0F, 64.0F, 0.0F);
  saida.ambiente_azul = entre(c->ambiente[2], 0.0F, 64.0F, 0.0F);
  saida.sombra = (c->sombra >= 0 && c->sombra <= 3)
                     ? static_cast<QualidadeDaSombra>(c->sombra)
                     : QualidadeDaSombra::desligada;
  // AS AMOSTRAS SO VALEM 1, 2, 4 OU 8. O Diligent recusa a criacao do alvo
  // com uma contagem que a placa nao suporta, e o defeito apareceria como
  // "a 3D nao desenha com antisserrilhado ligado" em vez de um aviso.
  switch (c->amostras) {
    case 2:
    case 4:
    case 8:
      saida.amostras = c->amostras;
      break;
    default:
      saida.amostras = 1;
      break;
  }
  saida.largura = c->largura;
  saida.altura = c->altura;

  saida.camadas.reserve(c->quantidade_de_camadas);
  for (std::uint32_t i = 0; i < c->quantidade_de_camadas; ++i) {
    const Aurea3DCamada& e = c->camadas[i];
    Camada3D d;
    d.alca = e.alca;
    d.modelo = e.modelo;
    d.animacao = e.animacao;
    d.tempo_da_animacao =
        std::isfinite(e.tempo_da_animacao) ? e.tempo_da_animacao : 0.0;
    d.visivel = e.visivel != 0;
    d.posicao = de_vetor(e.posicao);
    d.rotacao_graus = de_vetor(e.rotacao_graus);
    d.escala = de_vetor(e.escala);
    d.ancora = de_vetor(e.ancora);
    d.opacidade = entre(e.opacidade, 0.0F, 1.0F, 1.0F);
    d.cor = de_cor(e.cor);
    d.material = de_material(e.material);
    d.camada_z = finito(e.camada_z, 0.0F);
    saida.camadas.push_back(d);
  }

  saida.luzes.reserve(c->quantidade_de_luzes);
  for (std::uint32_t i = 0; i < c->quantidade_de_luzes; ++i) {
    saida.luzes.push_back(de_luz(c->luzes[i]));
  }
  return true;
}

// ---------------------------------------------------------- a saida POD

void preencher_relato(const RelatoDaImportacao& r, Aurea3DRelato* saida) {
  if (saida == nullptr) return;
  std::memset(saida, 0, sizeof(Aurea3DRelato));
  saida->bytes_do_arquivo = r.bytes_do_arquivo;
  saida->malhas = r.malhas;
  saida->materiais = r.materiais;
  saida->texturas = r.texturas;
  saida->nos = r.nos;
  saida->ossos = r.ossos;
  saida->animacoes = r.animacoes;
  saida->triangulos = r.triangulos;
  saida->vertices = r.vertices;
  saida->bytes_em_memoria = r.bytes_em_memoria;
  saida->avisos = r.avisos;
  ultimo_aviso() = r.primeiro_aviso;
}

/// O OPCOES DO DART VIram AS DO IMPORTADOR. Sem ponteiro, valem os padroes
/// — que sao os mesmos do `OpcoesDeImportacao`.
[[nodiscard]] OpcoesDeImportacao de_opcoes(const Aurea3DOpcoes* o) noexcept {
  OpcoesDeImportacao d;
  if (o == nullptr) return d;
  if (o->teto_de_bytes_do_arquivo > 0) {
    d.teto_de_bytes_do_arquivo = o->teto_de_bytes_do_arquivo;
  }
  d.sem_animacao = o->sem_animacao != 0;
  d.escala = entre(o->escala, 1.0e-4F, 1.0e4F, 1.0F);
  d.textura_neutra_quando_faltar = o->textura_neutra_quando_faltar != 0;
  return d;
}

/// LE E DEIXA A CARGA PRONTA. Nao toca no acervo.
void* ler_com(const Resulta<Modelo>& r, const RelatoDaImportacao& relato) {
  if (r.tem_erro()) {
    erro_da_porta() = std::string("importar: ") +
                      std::string(nome_do_erro(r.erro()));
    return nullptr;
  }
  auto carga = std::make_unique<Carga>();
  carga->modelo = r.valor();
  carga->relato = relato;
  // A CHAVE E O PROPRIO PONTEIRO, e nao um numero nosso: um contador
  // reciclado poderia casar com uma carga ja descartada e adotar memoria
  // alheia. O endereco e unico enquanto o objeto existe, que e exatamente
  // o tempo em que a carga e valida.
  void* chave = carga.get();
  cargas().emplace(chave, std::move(carga));
  return chave;
}

}  // namespace
}  // namespace aurea::render::tresd

// ------------------------------------------------------------ a ABI em C

extern "C" {

AUREA_API std::int32_t aurea_render_3d_versao(void) {
  try {
    return aurea::render::tresd::kVersaoDaPorta;
  } catch (...) {
    return -1;
  }
}

AUREA_API std::uint32_t aurea_render_3d_tamanho(std::uint32_t qual) {
  try {
    // O TAMANHO DE CADA STRUCT, PARA O LADO DART CONFERIR. Uma struct mal
    // declarada no Dart nao avisa: ela le o campo do vizinho e devolve um
    // numero que parece plausivel. O teste compara estes numeros com o
    // `sizeOf` de cada `Struct` e a divergencia aparece como falha.
    switch (qual) {
      case AUREA3D_TAM_CAMERA:
        return static_cast<std::uint32_t>(sizeof(Aurea3DCamera));
      case AUREA3D_TAM_MATERIAL:
        return static_cast<std::uint32_t>(sizeof(Aurea3DMaterial));
      case AUREA3D_TAM_CAMADA:
        return static_cast<std::uint32_t>(sizeof(Aurea3DCamada));
      case AUREA3D_TAM_LUZ:
        return static_cast<std::uint32_t>(sizeof(Aurea3DLuz));
      case AUREA3D_TAM_CENA:
        return static_cast<std::uint32_t>(sizeof(Aurea3DCena));
      case AUREA3D_TAM_RELATO:
        return static_cast<std::uint32_t>(sizeof(Aurea3DRelato));
      case AUREA3D_TAM_FICHA:
        return static_cast<std::uint32_t>(sizeof(Aurea3DFicha));
      case AUREA3D_TAM_MALHA_CRUA:
        return static_cast<std::uint32_t>(sizeof(Aurea3DMalhaCrua));
      case AUREA3D_TAM_OPCOES:
        return static_cast<std::uint32_t>(sizeof(Aurea3DOpcoes));
      case AUREA3D_TAM_QUANTOS:
        return static_cast<std::uint32_t>(AUREA3D_TAM_QUANTOS);
      default:
        return 0;
    }
  } catch (...) {
    return 0;
  }
}

// -------------------------------------------------------- o dispositivo

AUREA_API std::int32_t aurea_render_3d_preparar(void) {
  try {
    const std::int32_t ok = aurea::render::tresd::preparar();
    AUREA_LOG("preparar: ok=%d motivo=%s", ok,
              aurea::render::tresd::motivo());
    return ok;
  } catch (...) {
    return 0;
  }
}

AUREA_API std::int32_t aurea_render_3d_pronto(void) {
  try {
    return aurea::render::tresd::pronto() ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

AUREA_API const char* aurea_render_3d_motivo(void) {
  try {
    return aurea::render::tresd::motivo();
  } catch (...) {
    return "?";
  }
}

AUREA_API const char* aurea_render_3d_backend(void) {
  try {
    return aurea::render::tresd::backend();
  } catch (...) {
    return "?";
  }
}

AUREA_API const char* aurea_render_3d_ultimo_erro(void) {
  try {
    using namespace aurea::render::tresd;
    Renderizador3D* r = renderizador().get();
    if (r != nullptr) {
      const char* e = r->ultimo_erro();
      if (e != nullptr && e[0] != '\0') return e;
    }
    return erro_da_porta().c_str();
  } catch (...) {
    return "?";
  }
}

// ------------------------------------------------------------ a leitura

AUREA_API void* aurea_render_3d_ler_arquivo(const char* caminho,
                                            const Aurea3DOpcoes* opcoes,
                                            Aurea3DRelato* relato) {
  try {
    using namespace aurea::render::tresd;
    if (caminho == nullptr || caminho[0] == '\0') {
      erro_da_porta() = "caminho vazio";
      return nullptr;
    }
    RelatoDaImportacao r;
    const auto resultado = importar(caminho, de_opcoes(opcoes), &r);
    preencher_relato(r, relato);
    return ler_com(resultado, r);
  } catch (...) {
    return nullptr;
  }
}

AUREA_API void* aurea_render_3d_ler_memoria(const std::uint8_t* bytes,
                                            size_t tamanho,
                                            const char* extensao,
                                            const Aurea3DOpcoes* opcoes,
                                            Aurea3DRelato* relato) {
  try {
    using namespace aurea::render::tresd;
    if (bytes == nullptr || tamanho == 0) {
      erro_da_porta() = "sem bytes";
      return nullptr;
    }
    RelatoDaImportacao r;
    const auto resultado =
        importar_memoria(bytes, tamanho, extensao == nullptr ? "" : extensao,
                         de_opcoes(opcoes), &r);
    preencher_relato(r, relato);
    return ler_com(resultado, r);
  } catch (...) {
    return nullptr;
  }
}

AUREA_API std::int64_t aurea_render_3d_adotar(void* carga,
                                              Aurea3DRelato* relato) {
  try {
    using namespace aurea::render::tresd;
    using aurea::render::Erro;
    if (carga == nullptr) return 0;
    auto achado = cargas().find(carga);
    if (achado == cargas().end()) {
      erro_da_porta() = "carga desconhecida";
      return 0;
    }
    // O TETO DO ACERVO VALE AQUI, e nao na leitura: um modelo grande demais
    // para o que ja esta carregado e recusado com um erro que o app sabe
    // explicar, em vez de derrubar o processo (§21, §43). A CARGA NAO E
    // CONSUMIDA quando isto acontece — quem chamou pode soltar um modelo e
    // tentar de novo.
    const std::uint64_t entrando = achado->second->modelo.bytes();
    if (entrando > acervo().espaco_livre()) {
      erro_da_porta() = std::string("acervo: ") +
                        std::string(nome_do_erro(Erro::orcamento_estourado));
      preencher_relato(achado->second->relato, relato);
      return -static_cast<std::int64_t>(Erro::orcamento_estourado);
    }
    preencher_relato(achado->second->relato, relato);
    // O AVISO ATRAVESSA AQUI. Ele nasceu na thread de fundo, que tem a
    // propria copia da variavel; quem vai ler e a thread da interface, e a
    // adocao e o unico instante em que as duas se encontram de forma
    // ordenada (a mensagem do `Isolate` ja as ordenou).
    ultimo_aviso() = achado->second->relato.primeiro_aviso;
    const std::int32_t alca = acervo().guardar(std::move(achado->second->modelo));
    cargas().erase(achado);
    if (alca <= 0) {
      erro_da_porta() = "o acervo nao aceitou o modelo";
      return 0;
    }
    AUREA_LOG("adotar: alca=%d bytes=%llu", alca,
              static_cast<unsigned long long>(entrando));
    return alca;
  } catch (...) {
    return 0;
  }
}

AUREA_API void aurea_render_3d_descartar(void* carga) {
  try {
    if (carga == nullptr) return;
    aurea::render::tresd::cargas().erase(carga);
  } catch (...) {
  }
}

AUREA_API std::uint32_t aurea_render_3d_ultimo_aviso(char* saida,
                                                     std::uint32_t capacidade) {
  try {
    using namespace aurea::render::tresd;
    if (saida == nullptr || capacidade == 0) return 0;
    const std::string& a = ultimo_aviso();
    const std::size_t cabe = static_cast<std::size_t>(capacidade) - 1;
    const std::size_t n = a.size() < cabe ? a.size() : cabe;
    if (n > 0) std::memcpy(saida, a.data(), n);
    saida[n] = '\0';
    return static_cast<std::uint32_t>(n);
  } catch (...) {
    return 0;
  }
}

AUREA_API std::int64_t aurea_render_3d_importar_arquivo(
    const char* caminho, const Aurea3DOpcoes* opcoes, Aurea3DRelato* relato) {
  try {
    using namespace aurea::render::tresd;
    void* carga = aurea_render_3d_ler_arquivo(caminho, opcoes, nullptr);
    if (carga == nullptr) return 0;
    const std::int64_t alca = aurea_render_3d_adotar(carga, relato);
    if (alca <= 0) aurea_render_3d_descartar(carga);
    return alca;
  } catch (...) {
    return 0;
  }
}

AUREA_API std::int64_t aurea_render_3d_importar_memoria(
    const std::uint8_t* bytes, size_t tamanho, const char* extensao,
    const Aurea3DOpcoes* opcoes, Aurea3DRelato* relato) {
  try {
    using namespace aurea::render::tresd;
    void* carga =
        aurea_render_3d_ler_memoria(bytes, tamanho, extensao, opcoes, nullptr);
    if (carga == nullptr) return 0;
    const std::int64_t alca = aurea_render_3d_adotar(carga, relato);
    if (alca <= 0) aurea_render_3d_descartar(carga);
    return alca;
  } catch (...) {
    return 0;
  }
}

// ---------------------------------------------------------- geometria crua

/// QUANTAS MALHAS UMA CHAMADA ACEITA. Um numero grande demais e um erro do
/// chamador, e nao uma cena: o teto existe para recusar antes de percorrer.
constexpr std::uint32_t kMaxMalhasCruas = 4096;

AUREA_API std::int64_t aurea_render_3d_criar_modelo(
    const Aurea3DMalhaCrua* malhas, std::uint32_t quantas,
    Aurea3DRelato* relato) {
  try {
    using namespace aurea::render::tresd;
    using aurea::render::Erro;
    if (relato != nullptr) std::memset(relato, 0, sizeof(Aurea3DRelato));
    if (malhas == nullptr || quantas == 0) {
      return -static_cast<std::int64_t>(Erro::argumento);
    }
    if (quantas > kMaxMalhasCruas) {
      return -static_cast<std::int64_t>(Erro::capacidade);
    }

    // ==================== A CONFERENCIA VEM ANTES DA CONSTRUCAO ==========
    //
    // UM INDICE FORA DA FAIXA NAO DA UM MODELO ERRADO: ele da uma LEITURA
    // de memoria alheia dentro do renderizador — no melhor caso um
    // triangulo com um vertice do vizinho, no pior uma queda que o
    // relatorio de bug nao consegue explicar. A varredura custa uma
    // passada e acontece uma vez por malha, nao por quadro.
    //
    // E a SOMA DE BYTES VEM ANTES TAMBEM: o teto do acervo (§21) so serve
    // se for consultado antes de a memoria ja ter sido pedida ao sistema.
    std::uint64_t bytes_previstos = 0;
    for (std::uint32_t i = 0; i < quantas; ++i) {
      const Aurea3DMalhaCrua& crua = malhas[i];
      if (crua.posicoes == nullptr || crua.indices == nullptr) {
        return -static_cast<std::int64_t>(Erro::argumento);
      }
      if (crua.quantidade_de_vertices == 0 || crua.quantidade_de_indices == 0) {
        return -static_cast<std::int64_t>(Erro::argumento);
      }
      if (crua.quantidade_de_indices % 3 != 0) {
        return -static_cast<std::int64_t>(Erro::argumento);
      }
      for (std::uint32_t k = 0; k < crua.quantidade_de_indices; ++k) {
        if (crua.indices[k] >= crua.quantidade_de_vertices) {
          return -static_cast<std::int64_t>(Erro::argumento);
        }
      }
      bytes_previstos +=
          static_cast<std::uint64_t>(crua.quantidade_de_vertices) *
              sizeof(Vertice) +
          static_cast<std::uint64_t>(crua.quantidade_de_indices) *
              sizeof(std::uint32_t);
    }
    if (bytes_previstos > acervo().espaco_livre()) {
      return -static_cast<std::int64_t>(Erro::orcamento_estourado);
    }

    Modelo modelo;
    modelo.malhas.reserve(quantas);
    modelo.materiais.reserve(quantas);

    for (std::uint32_t i = 0; i < quantas; ++i) {
      const Aurea3DMalhaCrua& crua = malhas[i];
      const std::uint32_t nv = crua.quantidade_de_vertices;
      const std::uint32_t ni = crua.quantidade_de_indices;

      Malha malha;
      malha.vertices.resize(nv);
      for (std::uint32_t j = 0; j < nv; ++j) {
        Vertice& v = malha.vertices[j];
        v.posicao = Vec3{crua.posicoes[j * 3 + 0], crua.posicoes[j * 3 + 1],
                         crua.posicoes[j * 3 + 2]};
        v.normal = crua.normais != nullptr
                       ? Vec3{crua.normais[j * 3 + 0], crua.normais[j * 3 + 1],
                              crua.normais[j * 3 + 2]}
                       : Vec3{0.0F, 0.0F, 0.0F};
        v.uv0 = crua.uvs != nullptr
                    ? Vec2{crua.uvs[j * 2 + 0], crua.uvs[j * 2 + 1]}
                    : Vec2{0.0F, 0.0F};
        v.uv1 = Vec2{0.0F, 0.0F};
        // A COR DO VERTICE E ARGB EMPACOTADO, e nao RGBA.
        //
        // A PORTA fala RGBA (e `cor_base` acima obedece), mas o
        // `Vertice::cor` e o que o `cor_de_assimp` do importador escreve:
        // `a<<24 | r<<16 | g<<8 | b`. Empacotar RGBA aqui trocaria o
        // vermelho pelo alfa — e isso nao da excecao nenhuma, da um modelo
        // com a cor errada que so aparece numa captura de tela.
        v.cor = crua.cores != nullptr
                    ? (static_cast<std::uint32_t>(crua.cores[j * 4 + 3]) << 24 |
                       static_cast<std::uint32_t>(crua.cores[j * 4 + 0]) << 16 |
                       static_cast<std::uint32_t>(crua.cores[j * 4 + 1]) << 8 |
                       static_cast<std::uint32_t>(crua.cores[j * 4 + 2]))
                    : 0xFFFFFFFFU;
        malha.limites.incluir(v.posicao);
      }

      // AS NORMAS AUSENTES SAO CALCULADAS DEPOIS, sobre os TRIANGULOS: uma
      // normal plana precisa dos tres vertices da face, e nao do vertice
      // sozinho. Um cubo sem normais fica certo; uma esfera sem normais
      // fica facetada, que e a resposta honesta para "nao me disseram".
      if (crua.normais == nullptr) {
        for (std::uint32_t t = 0; t < ni; t += 3) {
          const Vec3 a = malha.vertices[crua.indices[t + 0]].posicao;
          const Vec3 b = malha.vertices[crua.indices[t + 1]].posicao;
          const Vec3 c = malha.vertices[crua.indices[t + 2]].posicao;
          const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
          const Vec3 v2{c.x - a.x, c.y - a.y, c.z - a.z};
          Vec3 n{u.y * v2.z - u.z * v2.y, u.z * v2.x - u.x * v2.z,
                 u.x * v2.y - u.y * v2.x};
          const float comprimento2 =
              n.x * n.x + n.y * n.y + n.z * n.z;
          // UM TRIANGULO DEGENERADO NAO TEM NORMAL. Escolher uma direcao
          // qualquer e melhor do que dividir por zero e semear NaN na
          // matriz inteira.
          if (comprimento2 > 1.0e-20F) {
            const float inverso = 1.0F / std::sqrt(comprimento2);
            n = Vec3{n.x * inverso, n.y * inverso, n.z * inverso};
          } else {
            n = Vec3{0.0F, 0.0F, 1.0F};
          }
          malha.vertices[crua.indices[t + 0]].normal = n;
          malha.vertices[crua.indices[t + 1]].normal = n;
          malha.vertices[crua.indices[t + 2]].normal = n;
        }
      }

      malha.indices.assign(crua.indices, crua.indices + ni);

      Material material;
      // `Cor` E QUALIFICADO: a diretiva `using namespace ...::tresd` traz
      // os nomes DAQUELE namespace, e nao os do namespace que o envolve.
      material.cor_base = aurea::render::Cor{crua.cor_base[0], crua.cor_base[1],
                                            crua.cor_base[2], crua.cor_base[3]};
      material.metalico = entre(crua.metalico, 0.0F, 1.0F, 1.0F);
      material.rugosidade = entre(crua.rugosidade, 0.0F, 1.0F, 1.0F);
      material.emissivo = aurea::render::Cor{crua.emissivo[0], crua.emissivo[1],
                                            crua.emissivo[2], crua.emissivo[3]};
      material.forca_emissiva = entre(crua.forca_emissiva, 0.0F, 1.0e4F, 1.0F);
      switch (crua.modo) {
        case 1:
          material.modo = Material::Modo::mascarado;
          break;
        case 2:
          material.modo = Material::Modo::transparente;
          break;
        default:
          material.modo = Material::Modo::opaco;
          break;
      }
      material.alfa_corte = entre(crua.alfa_corte, 0.0F, 1.0F, 0.5F);
      material.face_dupla = crua.face_dupla != 0;

      Faixa faixa;
      faixa.primeiro_indice = 0;
      faixa.quantidade = ni;
      faixa.base_do_vertice = 0;
      faixa.material = static_cast<std::int32_t>(modelo.materiais.size());
      // RESOLVIDO AQUI, e nao por faixa em cada quadro: o renderizador
      // consulta tres bytes para decidir a passada, e nao o material
      // inteiro duas vezes por desenho.
      faixa.transparente =
          material.modo == Material::Modo::transparente ? 1 : 0;
      faixa.mascarado = material.modo == Material::Modo::mascarado ? 1 : 0;
      faixa.face_dupla = material.face_dupla ? 1 : 0;
      malha.faixas.push_back(faixa);

      modelo.bytes_de_malha +=
          static_cast<std::uint64_t>(nv) * sizeof(Vertice) +
          static_cast<std::uint64_t>(ni) * sizeof(std::uint32_t);
      modelo.vertices += nv;
      modelo.triangulos += ni / 3;

      modelo.malhas.push_back(std::move(malha));
      modelo.materiais.push_back(std::move(material));
    }

    // A CAIXA DO MODELO E A UNIAO DAS CAIXAS DAS MALHAS. E ela que responde
    // ao corte de frustum e ao toque (§17, §30) antes de olhar triangulo
    // por triangulo.
    for (const Malha& malha : modelo.malhas) {
      if (!malha.limites.vazia) modelo.limites.incluir(malha.limites);
    }

    const std::int32_t alca = acervo().guardar(std::move(modelo));
    if (relato != nullptr) {
      const Modelo* guardado = acervo().obter(alca);
      if (guardado != nullptr) {
        relato->malhas = static_cast<std::uint32_t>(guardado->malhas.size());
        relato->materiais =
            static_cast<std::uint32_t>(guardado->materiais.size());
        relato->triangulos = guardado->triangulos;
        relato->vertices = guardado->vertices;
        relato->bytes_em_memoria = guardado->bytes();
      }
    }
    return alca;
  } catch (...) {
    // O NOME INTEIRO AQUI: o `using` do `try` nao alcanca o `catch`, e um
    // `Erro` solto neste ponto nao compila.
    return -static_cast<std::int64_t>(aurea::render::Erro::interno);
  }
}

// -------------------------------------------------------------- o acervo

AUREA_API std::uint32_t aurea_render_3d_acervo_quantidade(void) {
  try {
    return aurea::render::tresd::acervo().quantidade();
  } catch (...) {
    return 0;
  }
}

AUREA_API std::uint64_t aurea_render_3d_acervo_bytes(void) {
  try {
    return aurea::render::tresd::acervo().bytes();
  } catch (...) {
    return 0;
  }
}

AUREA_API void aurea_render_3d_acervo_definir_teto(std::uint64_t bytes) {
  try {
    aurea::render::tresd::acervo().teto_de_bytes = bytes;
  } catch (...) {
  }
}

AUREA_API std::uint64_t aurea_render_3d_acervo_teto(void) {
  try {
    return aurea::render::tresd::acervo().teto_de_bytes;
  } catch (...) {
    return 0;
  }
}

AUREA_API void aurea_render_3d_segurar(std::int32_t alca) {
  try {
    aurea::render::tresd::acervo().segurar(alca);
  } catch (...) {
  }
}

AUREA_API void aurea_render_3d_soltar(std::int32_t alca) {
  try {
    aurea::render::tresd::acervo().soltar(alca);
  } catch (...) {
  }
}

AUREA_API std::uint32_t aurea_render_3d_limpar_desocupados(void) {
  try {
    return aurea::render::tresd::acervo().limpar_desocupados();
  } catch (...) {
    return 0;
  }
}

AUREA_API void aurea_render_3d_acervo_limpar(void) {
  try {
    aurea::render::tresd::acervo().limpar();
  } catch (...) {
  }
}

AUREA_API std::int32_t aurea_render_3d_ficha(std::int32_t alca,
                                             Aurea3DFicha* saida) {
  try {
    using namespace aurea::render::tresd;
    if (saida == nullptr) return 0;
    std::memset(saida, 0, sizeof(Aurea3DFicha));
    const Modelo* m = acervo().obter(alca);
    if (m == nullptr) return 0;
    saida->malhas = static_cast<std::uint32_t>(m->malhas.size());
    saida->materiais = static_cast<std::uint32_t>(m->materiais.size());
    saida->texturas = static_cast<std::uint32_t>(m->texturas.size());
    saida->nos = static_cast<std::uint32_t>(m->nos.size());
    saida->ossos = static_cast<std::uint32_t>(m->ossos.size());
    saida->animacoes = static_cast<std::uint32_t>(m->animacoes.size());
    saida->triangulos = m->triangulos;
    saida->vertices = m->vertices;
    saida->bytes_de_malha = m->bytes_de_malha;
    saida->bytes_de_textura = m->bytes_de_textura;
    saida->limites[0] = m->limites.minimo.x;
    saida->limites[1] = m->limites.minimo.y;
    saida->limites[2] = m->limites.minimo.z;
    saida->limites[3] = m->limites.maximo.x;
    saida->limites[4] = m->limites.maximo.y;
    saida->limites[5] = m->limites.maximo.z;
    Renderizador3D* r = renderizador().get();
    saida->na_gpu = (r != nullptr && r->modelo_na_gpu(alca, acervo())) ? 1 : 0;
    return 1;
  } catch (...) {
    return 0;
  }
}

AUREA_API double aurea_render_3d_duracao_da_animacao(std::int32_t alca,
                                                     std::int32_t animacao) {
  try {
    using namespace aurea::render::tresd;
    const Modelo* m = acervo().obter(alca);
    if (m == nullptr) return 0.0;
    return duracao_da_animacao(*m, animacao);
  } catch (...) {
    return 0.0;
  }
}

AUREA_API std::uint64_t aurea_render_3d_memoria_de_gpu(void) {
  try {
    std::string erro;
    aurea::render::tresd::Renderizador3D* r =
        aurea::render::tresd::motor(erro);
    return r == nullptr ? 0 : r->memoria_de_gpu();
  } catch (...) {
    return 0;
  }
}

// -------------------------------------------------------------- o desenho

AUREA_API std::int32_t aurea_render_3d_desenhar(const Aurea3DCena* cena) {
  try {
    using namespace aurea::render::tresd;
    numeros() = NumerosDoQuadro{};
    if (cena == nullptr) {
      erro_da_porta() = "cena nula";
      return 0;
    }
    Cena3D convertida;
    if (!de_cena(cena, convertida)) {
      erro_da_porta() = "cena invalida";
      return 0;
    }

    Quadro3D quadro;
    avaliar(convertida, acervo(), quadro);

    NumerosDoQuadro& n = numeros();
    n.camadas = quadro.camadas;
    n.desenhadas = quadro.camadas_desenhadas;
    n.fora_do_campo = quadro.camadas_fora_do_campo;
    n.sem_modelo = quadro.camadas_sem_modelo;
    n.triangulos = quadro.triangulos;
    n.vertices = quadro.vertices;
    n.ossos = static_cast<std::uint32_t>(quadro.pele.size());
    n.luzes = static_cast<std::uint32_t>(quadro.luzes.size());

    // SEM GEOMETRIA NAO HA QUADRO A DESENHAR — e este atalho nao e so
    // velocidade. O renderizador relê o alvo inteiro da GPU a cada desenho,
    // e esse e o passo mais caro do caminho: paga-lo sessenta vezes por
    // segundo numa cena sem 3D nenhum, que e o caso comum, seria carregar o
    // 3D no bolso de quem nao usa 3D.
    if (quadro.opacos.empty() && quadro.transparentes.empty()) {
      n.sem_geometria = true;
      return 1;
    }
    n.sem_geometria = false;

    std::string erro;
    Renderizador3D* r = motor(erro);
    if (r == nullptr) {
      erro_da_porta() = erro.empty() ? "o motor 3D nao subiu" : erro;
      return 0;
    }
    return r->desenhar(quadro, acervo()) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

AUREA_API const std::uint8_t* aurea_render_3d_pixels(void) {
  try {
    using namespace aurea::render::tresd;
    if (numeros().sem_geometria) return nullptr;
    Renderizador3D* r = renderizador().get();
    if (r == nullptr) return nullptr;
    const std::uint8_t* p = r->pixels();
    return (p == nullptr || r->largura() == 0 || r->altura() == 0) ? nullptr : p;
  } catch (...) {
    return nullptr;
  }
}

AUREA_API std::uint32_t aurea_render_3d_largura(void) {
  try {
    using namespace aurea::render::tresd;
    Renderizador3D* r = renderizador().get();
    return r == nullptr ? 0 : r->largura();
  } catch (...) {
    return 0;
  }
}

AUREA_API std::uint32_t aurea_render_3d_altura(void) {
  try {
    using namespace aurea::render::tresd;
    Renderizador3D* r = renderizador().get();
    return r == nullptr ? 0 : r->altura();
  } catch (...) {
    return 0;
  }
}

AUREA_API std::uint32_t aurea_render_3d_aquecer(void) {
  try {
    using namespace aurea::render::tresd;
    std::string erro;
    Renderizador3D* r = motor(erro);
    if (r == nullptr) {
      erro_da_porta() = erro.empty() ? "o motor 3D nao subiu" : erro;
      return 0;
    }
    return r->aquecer(acervo());
  } catch (...) {
    return 0;
  }
}

AUREA_API std::uint32_t aurea_render_3d_limpar_gpu(void) {
  try {
    using namespace aurea::render::tresd;
    Renderizador3D* r = renderizador().get();
    return r == nullptr ? 0 : r->limpar(acervo());
  } catch (...) {
    return 0;
  }
}

AUREA_API void aurea_render_3d_liberar(void) {
  try {
    using namespace aurea::render::tresd;
    // A ORDEM IMPORTA: o renderizador solta a GPU antes de o acervo soltar o
    // dado puro, porque os buffers de GPU foram criados A PARTIR dele e a
    // ficha do modelo e a identidade que o renderizador usa para saber se o
    // que esta na placa ainda e o mesmo modelo.
    if (renderizador() != nullptr) {
      renderizador()->liberar();
      renderizador().reset();
    }
    acervo().limpar();
    cargas().clear();
    erro_da_porta().clear();
    ultimo_aviso().clear();
    numeros() = NumerosDoQuadro{};
  } catch (...) {
  }
}

// -------------------------------------------------------------- numeros

AUREA_API std::uint32_t aurea_render_3d_estatisticas(double* saida,
                                                     std::uint32_t capacidade) {
  constexpr std::uint32_t kCampos = 13;
  try {
    using namespace aurea::render::tresd;
    if (saida == nullptr) return 0;
    Renderizador3D* r = renderizador().get();
    const Estatisticas3D e = r == nullptr ? Estatisticas3D{} : r->estatisticas();
    const double valores[kCampos] = {
        static_cast<double>(e.desenhos),
        static_cast<double>(e.desenhos_esqueleticos),
        static_cast<double>(e.triangulos),
        static_cast<double>(e.instancias),
        static_cast<double>(e.malhas_na_gpu),
        static_cast<double>(e.texturas_na_gpu),
        static_cast<double>(e.pipelines),
        static_cast<double>(e.quadros),
        e.ultimo_desenho_ms,
        static_cast<double>(e.bytes_de_gpu),
        static_cast<double>(e.lado_da_sombra),
        e.sombra_ligada ? 1.0 : 0.0,
        e.antisserrilhado ? 1.0 : 0.0,
    };
    const std::uint32_t escrever =
        capacidade < kCampos ? capacidade : kCampos;
    for (std::uint32_t i = 0; i < escrever; ++i) saida[i] = valores[i];
    return escrever;
  } catch (...) {
    return 0;
  }
}

AUREA_API std::uint32_t aurea_render_3d_quadro_numeros(std::uint32_t* saida,
                                                       std::uint32_t capacidade) {
  constexpr std::uint32_t kCampos = 8;
  try {
    if (saida == nullptr) return 0;
    const auto& n = aurea::render::tresd::numeros();
    const std::uint32_t valores[kCampos] = {
        n.camadas,      n.desenhadas, n.fora_do_campo, n.sem_modelo,
        n.triangulos,   n.vertices,   n.ossos,         n.luzes,
    };
    const std::uint32_t escrever = capacidade < kCampos ? capacidade : kCampos;
    for (std::uint32_t i = 0; i < escrever; ++i) saida[i] = valores[i];
    return escrever;
  } catch (...) {
    return 0;
  }
}

}  // extern "C"

// -------------------------------------------------- as garantias de layout
//
// UM `static_assert` DE TAMANHO E O QUE IMPEDE A ABI DE MUDAR SEM AVISO. O
// lado Dart confere os mesmos numeros em teste, mas o teste so roda no PC —
// aqui a compilacao para, e para em todas as plataformas.
static_assert(sizeof(Aurea3DCamera) == 72, "Aurea3DCamera mudou de tamanho");
static_assert(sizeof(Aurea3DMaterial) == 40, "Aurea3DMaterial mudou de tamanho");
static_assert(sizeof(Aurea3DCamada) == 128, "Aurea3DCamada mudou de tamanho");
static_assert(sizeof(Aurea3DLuz) == 60, "Aurea3DLuz mudou de tamanho");
static_assert(sizeof(Aurea3DCena) == (sizeof(void*) == 8 ? 128 : 120),
              "Aurea3DCena mudou de tamanho");
static_assert(sizeof(Aurea3DOpcoes) == 24, "Aurea3DOpcoes mudou de tamanho");
static_assert(sizeof(Aurea3DRelato) == 56, "Aurea3DRelato mudou de tamanho");
static_assert(sizeof(Aurea3DFicha) == 80, "Aurea3DFicha mudou de tamanho");
// OS PONTEIROS MUDAM COM A ABI: sao 40 bytes no arm64 e 20 no armv7. O
// restante da struct nao muda. Conferir os dois tamanhos aqui impede o APK
// 32-bit de ser recusado sem enfraquecer a garantia do layout.
static_assert(sizeof(Aurea3DMalhaCrua) == (sizeof(void*) == 8 ? 80 : 60),
              "Aurea3DMalhaCrua mudou de tamanho");
