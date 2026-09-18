#include "nucleo.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace aurea::render {

namespace {
/// OS TRES DEGRAUS DE QUALIDADE.
///
/// O DEGRAU MEXE NA RESOLUCAO E NAS AMOSTRAS — NUNCA NO ORCAMENTO. O
/// orcamento e a POLITICA (16,667 ms para 60 Hz, 33,3 ms para quem esta
/// com a bateria no fim) e vem de fora; o degrau e o MEIO de caber nele.
/// Deixar o degrau reescrever o orcamento era o defeito da primeira
/// versao: quem pedia um alvo apertado recebia 16,667 ms de volta na
/// primeira reacao, e o controle passava a perseguir um alvo que ninguem
/// tinha pedido — oscilando entre dois degraus para sempre.
struct Degrau {
  std::uint32_t amostras;
  double escala_interna;
};

constexpr Degrau kDegraus[] = {
    // 0 — EMERGENCIA: o aparelho esta quente ou o projeto estourou.
    {.amostras = 1, .escala_interna = 0.5},
    // 1 — ESTAVEL: o meio-termo que segura 30 fps sem esquentar.
    {.amostras = 1, .escala_interna = 0.75},
    // 2 — CHEIO: o alvo do Aurea enquanto o aparelho aguenta.
    {.amostras = 2, .escala_interna = 1.0},
};
constexpr std::uint32_t kDegrauMaximo =
    static_cast<std::uint32_t>(std::size(kDegraus)) - 1;
}  // namespace

Resulta<std::unique_ptr<CargaDeShader>> CompiladorDeReferencia::compilar(
    const DescricaoDoShader& d, Backend b) {
  (void)b;
  // A FONTE VAZIA NAO COMPILA em backend nenhum. O de referencia confere
  // isso e mais nada — e e justamente esse o unico erro que ele precisa
  // saber recusar, porque e o unico que acontece sem placa de video.
  if (d.fonte.empty()) return Erro::argumento;
  return std::unique_ptr<CargaDeShader>(new CargaDeShader());
}

Nucleo::Nucleo(const Configuracao& c) noexcept
    : cfg_(c),
      relogio_(c.qualidade.orcamento_ms),
      recursos_(c.orcamento_de_recursos_bytes),
      shaders_(compilador_, c.backend),
      compositor_(recursos_),
      fila_(256) {
  qualidade_ = c.qualidade;
  relogio_.definir_orcamento(qualidade_.orcamento_ms);
}

Resulta<std::unique_ptr<Nucleo>> Nucleo::abrir(const Configuracao& c) {
  if (c.largura == 0 || c.altura == 0) return Erro::argumento;
  auto n = std::unique_ptr<Nucleo>(new Nucleo(c));
  auto r = n->abrir_recursos();
  if (r.tem_erro()) return r.erro();
  if (c.com_thread) {
    n->thread_ = std::jthread([p = n.get()](std::stop_token) {
      p->laco_da_thread();
    });
  }
  return n;
}

Nucleo::~Nucleo() { fechar(); }

Resulta<AlcaDeRecurso> Nucleo::pegar_alvo(std::uint32_t largura,
                                          std::uint32_t altura) {
  auto r = recursos_.alvo(largura, altura);
  if (r.tem_erro()) return r.erro();
  AlcaDeRecurso alca = std::move(r).valor();
  Recurso* recurso = alca.ponteiro().get();
  if (recurso != nullptr && recurso->carga == nullptr) {
    recurso->carga = std::make_unique<CargaDeAlvo>(largura, altura);
  }
  return alca;
}

Resulta<std::uint32_t> Nucleo::abrir_recursos() {
  // O ALVO DO QUADRO NASCE UMA VEZ. Compor num alvo reaproveitado e o que
  // evita uma alocacao de GPU por quadro — e um alvo de 1080p RGBA sao
  // 8 MB que, alocados a 60 Hz, aparecem no perfil de memoria como um
  // serrote.
  const auto l = static_cast<std::uint32_t>(
      std::max(1.0, cfg_.largura * qualidade_.escala_interna));
  const auto a = static_cast<std::uint32_t>(
      std::max(1.0, cfg_.altura * qualidade_.escala_interna));
  auto alvo = pegar_alvo(l, a);
  if (alvo.tem_erro()) return alvo.erro();
  alvo_ = std::move(alvo).valor();
  return 0u;
}

void Nucleo::publicar_cena(std::shared_ptr<const Cena> cena) noexcept {
  {
    std::scoped_lock trava(trava_);
    cena_ = std::move(cena);
  }
  tempo_publicado_ = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

void Nucleo::pedir_quadro() noexcept {
  {
    std::scoped_lock trava(trava_);
    pedido_ = true;
  }
  sinal_.notify_one();
}

Resulta<std::uint32_t> Nucleo::desenhar_agora() {
  relogio_.comecar_quadro();
  recursos_.avancar_quadro();

  std::shared_ptr<const Cena> cena;
  {
    std::scoped_lock trava(trava_);
    cena = cena_;
  }
  if (!cena) {
    relogio_.registrar_pulo();
    return Erro::estado_invalido;
  }
  if (!alvo_.viva()) return Erro::estado_invalido;

  // CENA IGUAL, QUADRO IGUAL. A impressao digital vem de quem publica
  // (o nucleo Dart), e vale enquanto a cena for a mesma — um projeto
  // parado nao gasta GPU.
  if (cena_desenhada_ &&
      cena->impressao != 0 && cena->impressao == impressao_desenhada_) {
    ++quadros_reaproveitados_;
    relogio_.registrar_pulo();
    relogio_.terminar_quadro();
    return 0u;
  }

  auto* alvo = dynamic_cast<CargaDeAlvo*>(alvo_.carga());
  if (alvo == nullptr) return Erro::estado_invalido;

  // A ESCALA INTERNA: as camadas vem na resolucao DA COMPOSICAO e o alvo
  // pode estar menor. Repassar as coordenadas cruas desenharia tudo
  // maior do que devia — o erro classico de quem reduz resolucao "so"
  // trocando o tamanho do alvo.
  const double fator =
      cfg_.largura == 0
          ? 1.0
          : static_cast<double>(alvo->largura) / static_cast<double>(cfg_.largura);

  std::vector<Camada> camadas;
  camadas.reserve(cena->camadas.size());
  for (const Camada& c : cena->camadas) {
    Camada s = c;
    if (fator != 1.0) {
      const auto f = static_cast<float>(fator);
      s.x *= f;
      s.y *= f;
      s.largura *= f;
      s.altura *= f;
    }
    camadas.push_back(s);
  }

  alvo->limpar();
  auto pintou = compositor_.desenhar(*alvo, camadas, qualidade_.amostras);

  cena_desenhada_ = cena;
  impressao_desenhada_ = cena->impressao;
  relogio_.terminar_quadro();

  if (pintou.tem_erro()) {
    ++quadros_falhos_;
    return pintou.erro();
  }
  ajustar_qualidade();
  return pintou.valor();
}

void Nucleo::ajustar_qualidade() noexcept {
  if (!automatica_.load(std::memory_order_relaxed)) return;
  const EstatisticasDoQuadro e = relogio_.estatisticas();
  // ESPERA A JANELA TER DADOS. Decidir com tres quadros na mao faria o
  // nivel oscilar na largada, antes de o projeto carregar.
  if (e.quadros_na_janela < 12) return;

  const double orcamento = qualidade_.orcamento_ms;
  if (e.cpu_p95_ms > orcamento && nivel_de_qualidade_ > 0) {
    --nivel_de_qualidade_;
    aplicar_degrau();
    return;
  }
  // SUBIR E MAIS DIFICIL DO QUE DESCER. Pede folga larga (60% do
  // orcamento) e um nivel abaixo do teto — sem isso o controle fica
  // batendo entre dois niveis, e a oscilacao incomoda mais do que o
  // nivel baixo.
  if (nivel_de_qualidade_ < kDegrauMaximo &&
      e.cpu_p95_ms < orcamento * 0.6 && e.fps_efetivo >= 50.0) {
    ++nivel_de_qualidade_;
    aplicar_degrau();
  }
}

void Nucleo::aplicar_degrau() noexcept {
  Qualidade q = qualidade_;
  q.amostras = kDegraus[nivel_de_qualidade_].amostras;
  q.escala_interna = kDegraus[nivel_de_qualidade_].escala_interna;
  definir_qualidade(q);
}

void Nucleo::laco_da_thread() noexcept {
  // ESPERA SEM GASTAR BATERIA. Um `sleep_for(1ms)` em laco acorda a CPU
  // sessenta mil vezes por minuto para descobrir que nao havia nada a
  // fazer; num celular isso e aquecimento puro. A variavel de condicao
  // acorda so quando alguem pede um quadro.
  for (;;) {
    {
      // A TRAVA EM CHAVES, E NAO EM PARENTESES. `unique_lock<mutex>
      // espera(mutex);` e ambiguo o bastante para o MSVC ler como
      // declaracao de funcao em alguns contextos — e o erro que aparece
      // depois ("wait nao recebe 2 argumentos") nao aponta para a linha
      // culpada. Com chaves nao ha o que interpretar.
      std::unique_lock<std::mutex> espera{trava_};
      sinal_.wait(espera, [this] { return pedido_ || parar_; });
      if (parar_) return;
      pedido_ = false;
    }
    (void)desenhar_agora();
  }
}

void Nucleo::definir_qualidade(const Qualidade& q) noexcept {
  qualidade_ = q;
  qualidade_.amostras = std::clamp<std::uint32_t>(q.amostras, 1, 4);
  qualidade_.escala_interna = prender(q.escala_interna, 0.25, 1.0);
  // SO UM ORCAMENTO POSITIVO E ACEITO. Zero ou negativo nao e um alvo
  // apertado: e um pedido sem sentido, e o padrao e a resposta certa.
  qualidade_.orcamento_ms =
      q.orcamento_ms > 0.0 ? q.orcamento_ms : kOrcamentoPadraoMs;
  relogio_.definir_orcamento(qualidade_.orcamento_ms);

  // O ALVO MUDA DE TAMANHO: um alvo novo, e o antigo volta para o cache
  // em vez de ser destruido — trocar de nivel nao pode custar uma
  // alocacao de 8 MB.
  const auto l = static_cast<std::uint32_t>(
      std::max(1.0, cfg_.largura * qualidade_.escala_interna));
  const auto a = static_cast<std::uint32_t>(
      std::max(1.0, cfg_.altura * qualidade_.escala_interna));
  auto alvo = pegar_alvo(l, a);
  if (alvo.tem_valor()) {
    alvo_ = std::move(alvo).valor();
    // A CENA DESENHADA NAO VALE MAIS: o alvo e outro, e a impressao
    // digital apontaria para um quadro que nao esta ali.
    cena_desenhada_.reset();
    impressao_desenhada_ = 0;
  }
}

Qualidade Nucleo::qualidade() const noexcept { return qualidade_; }

void Nucleo::definir_automatica(bool ligada) noexcept {
  automatica_.store(ligada, std::memory_order_relaxed);
}

bool Nucleo::automatica() const noexcept {
  return automatica_.load(std::memory_order_relaxed);
}

Resulta<IdDeRecurso> Nucleo::registrar_textura(
    std::uint32_t largura, std::uint32_t altura,
    std::vector<std::uint8_t> rgba) {
  if (largura == 0 || altura == 0 ||
      rgba.size() < static_cast<std::size_t>(largura) * altura * 4) {
    return Erro::argumento;
  }
  DescricaoDoRecurso d;
  d.tipo = TipoDeRecurso::textura;
  d.largura = largura;
  d.altura = altura;
  d.bytes = static_cast<std::uint64_t>(largura) * altura * 4;
  auto r = recursos_.criar(d);
  if (r.tem_erro()) return r.erro();
  AlcaDeRecurso alca = std::move(r).valor();
  // A CARGA E COLOCADA UMA VEZ, aqui. Depois disto o recurso e somente
  // leitura para quem desenha.
  Recurso* recurso = alca.ponteiro().get();
  recurso->carga = std::make_unique<CargaDeTextura>(largura, altura,
                                                    std::move(rgba));
  const IdDeRecurso id = alca.id();
  // A ALCA MORRE AQUI E O RECURSO FICA: o mapa do gerenciador e o dono
  // enquanto ele existir, e quem desenha o alcanca pelo id.
  return id;
}

Resulta<std::uint32_t> Nucleo::pre_aquecer(
    std::span<const DescricaoDoShader> descricoes) {
  return shaders_.pre_aquecer(descricoes);
}

Resulta<std::uint32_t> Nucleo::ler_pixels(
    std::vector<std::uint8_t>& destino) {
  // ======================= A REGRA DO ZERO-COPY, EM CODIGO ==============
  //
  // ESTA FUNCAO TRAZ O QUADRO INTEIRO DA MEMORIA DE VOLTA PARA A CPU. Ela
  // existe para o teste e para a bancada — e, se algum dia entrar num
  // play, o caminho GPU→CPU→GPU que o motor inteiro foi feito para
  // evitar esta de volta, a 60 Hz, com oito megabytes por quadro.
  //
  // POR ISSO ELA E RECUSADA NO NUCLEO DE PRODUCAO. Um nucleo com thread
  // propria e o nucleo de producao; um nucleo sem thread e o da bancada,
  // onde o resultado tem de ser deterministico. A distincao ja existia e
  // agora ela PROTEGE: nao ha como ler pixels de um nucleo que esta
  // desenhando para a tela sem antes ter escolhido o modo de bancada. Um
  // aviso em comentario nao impede nada; esta linha impede.
  if (cfg_.com_thread) return Erro::nao_suportado;

  if (!alvo_.viva()) return Erro::estado_invalido;
  auto* alvo = dynamic_cast<CargaDeAlvo*>(alvo_.carga());
  if (alvo == nullptr) return Erro::estado_invalido;

  const std::size_t n = static_cast<std::size_t>(alvo->largura) * alvo->altura;
  destino.assign(n * 4, 0);
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint8_t r = alvo->pixels[i * 4 + 0];
    const std::uint8_t g = alvo->pixels[i * 4 + 1];
    const std::uint8_t b = alvo->pixels[i * 4 + 2];
    const std::uint8_t a = alvo->pixels[i * 4 + 3];
    const Cor c = desmultiplicar(r, g, b, a);
    destino[i * 4 + 0] = c.r;
    destino[i * 4 + 1] = c.g;
    destino[i * 4 + 2] = c.b;
    destino[i * 4 + 3] = c.a;
  }
  return static_cast<std::uint32_t>(n * 4);
}

EstatisticasDoNucleo Nucleo::estatisticas() const noexcept {
  EstatisticasDoNucleo e;
  e.quadro = relogio_.estatisticas();
  e.recursos = recursos_.estatisticas();
  e.shaders = shaders_.estatisticas();
  e.compositor = compositor_.estatisticas();
  e.qualidade = qualidade_;
  e.fila_pendente = static_cast<std::uint32_t>(fila_.uso());
  e.quadros_reaproveitados = quadros_reaproveitados_;
  e.quadros_falhos = quadros_falhos_;
  e.tempo_publicado = tempo_publicado_;
  return e;
}

void Nucleo::fechar() noexcept {
  {
    std::scoped_lock trava(trava_);
    if (parar_) return;
    parar_ = true;
  }
  sinal_.notify_all();
  if (thread_.joinable()) thread_.join();
  // A ORDEM IMPORTA: os shaders e as texturas apontam para objetos que o
  // backend criou. Soltar o alvo antes deles deixaria uma alca viva sobre
  // memoria ja liberada.
  alvo_ = AlcaDeRecurso{};
  shaders_.soltar_tudo();
  recursos_.soltar_tudo();
}

}  // namespace aurea::render
