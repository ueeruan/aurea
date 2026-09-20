// A AVALIACAO DA CENA 3D: DO ESTADO DA TIMELINE AOS DESENHOS DO QUADRO.
//
// Nao ha nada de GPU neste arquivo, e isso e de proposito. Tudo o que
// decide O QUE aparece — qual clipe, em que instante, com que matriz, com
// que material — acontece aqui, em C++ puro, e pode ser conferido sem
// placa de video nenhuma. O renderizador so recebe o resultado pronto.
//
// A CONSEQUENCIA PRATICA E A GARANTIA QUE INTERESSA (§33, §34): o mesmo
// instante da timeline da o mesmo `Quadro3D`, no preview e na exportacao,
// num celular e no PC, hoje e amanha. Se a conta estivesse la no
// renderizador, ela dependeria de quando o quadro saiu, e o export nao
// bateria com o que se viu.
#include "cena_3d.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace aurea::render::tresd {

namespace {

/// UM NUMERO QUE MUDA QUANDO A IMAGEM MUDA. Mistura de 64 bits (o
/// "splitmix" final do murmur) — barata e sem surpresas.
///
/// Nao e criptografia e nao precisa ser: o que se quer e que dois quadros
/// diferentes quase nunca caiam no mesmo numero. Uma colisao aqui custa
/// reaproveitar um quadro antigo por um instante — e a mesma conta que o
/// `impressao` da cena 2D ja faz.
inline void misturar_impressao(std::uint64_t& s, std::uint64_t v) noexcept {
  s ^= v + 0x9E3779B97F4A7C15ULL + (s << 6) + (s >> 2);
}

inline void misturar_float(std::uint64_t& s, float v) noexcept {
  // UM FLOAT NUNCA E COMPARADO POR `==` ESTA NOITE. O mesmo numero vindo de
  // um caminho diferente de conta (a posicao que veio de uma curva, a que
  // veio de uma chave) pode diferir no ultimo bit; arredondar para 1/1024
  // antes de misturar evita redesenhar por causa de um bit.
  const auto q = static_cast<std::int64_t>(std::lround(v * 1024.0F));
  misturar_impressao(s, static_cast<std::uint64_t>(q));
}

inline void misturar_vec(std::uint64_t& s, Vec3 v) noexcept {
  misturar_float(s, v.x);
  misturar_float(s, v.y);
  misturar_float(s, v.z);
}

inline void misturar_cor(std::uint64_t& s, Cor c) noexcept {
  misturar_impressao(s, c.r);
  misturar_impressao(s, c.g);
  misturar_impressao(s, c.b);
  misturar_impressao(s, c.a);
}

inline void misturar_indice(std::uint64_t& s, std::int32_t i) noexcept {
  // O DESLOCAMENTO DE DOIS e o mesmo truque do acervo: -1 ("nao tem") e 0
  // ("nenhum") viram numeros distintos logo no primeiro passo, em vez de
  // colidirem depois de passar por complemento de dois.
  misturar_impressao(s, static_cast<std::uint64_t>(
                           static_cast<std::int64_t>(i) + 2));
}

/// TODO CAMPO DO MATERIAL QUE CHEGA AO SHADER. Um material e uma coisa so:
/// trocar a cor de base ou a textura muda a imagem, e a impressao tem de
/// mudar junto — senao o dono arrasta o controle de metalico e a tela nao
/// se mexe, porque o motor conclui que a cena e a mesma de antes.
inline void misturar_material(std::uint64_t& s, const Material& m) noexcept {
  misturar_cor(s, m.cor_base);
  misturar_float(s, m.metalico);
  misturar_float(s, m.rugosidade);
  misturar_cor(s, m.emissivo);
  misturar_float(s, m.forca_emissiva);
  misturar_indice(s, m.textura_cor);
  misturar_indice(s, m.textura_normal);
  misturar_indice(s, m.textura_metalico_rugosidade);
  misturar_indice(s, m.textura_emissiva);
  misturar_indice(s, m.textura_oclusao);
  misturar_float(s, m.forca_da_oclusao);
  misturar_impressao(s, static_cast<std::uint32_t>(m.modo));
  misturar_float(s, m.alfa_corte);
  misturar_impressao(s, m.face_dupla ? 1U : 0U);
  misturar_float(s, m.indice_de_refracao);
}

/// TODA CAMADA CONTA, INCLUSIVE A QUE NAO DESENHA. A que esta invisivel
/// nao entra no quadro, e por isso mesmo ela tem de entrar na impressao: e
/// a diferenca entre esconder a ultima camada 3D e a tela continuar
/// mostrando o modelo — o defeito que aparece como "o botao de olho nao
/// funciona" e nao como erro nenhum.
inline void misturar_camada(std::uint64_t& s, const Camada3D& c) noexcept {
  misturar_impressao(s, c.alca);
  misturar_indice(s, c.modelo);
  misturar_indice(s, c.animacao);
  misturar_float(s, static_cast<float>(c.tempo_da_animacao));
  misturar_impressao(s, c.visivel ? 1U : 0U);
  misturar_vec(s, c.posicao);
  misturar_vec(s, c.rotacao_graus);
  misturar_vec(s, c.escala);
  misturar_vec(s, c.ancora);
  misturar_float(s, c.opacidade);
  misturar_cor(s, c.cor);
  misturar_float(s, c.camada_z);

  const MaterialDaCamada& m = c.material;
  misturar_impressao(s, m.ligado ? 1U : 0U);
  misturar_cor(s, m.cor_base);
  misturar_float(s, m.metalico);
  misturar_float(s, m.rugosidade);
  misturar_float(s, m.forca_emissiva);
  misturar_cor(s, m.emissivo);
  misturar_impressao(s, static_cast<std::uint64_t>(
                           static_cast<std::int64_t>(m.modo) + 2));
  misturar_impressao(s, m.face_dupla ? 1U : 0U);
  misturar_float(s, m.alfa_corte);
  misturar_impressao(s, m.sem_textura_de_cor ? 1U : 0U);
}

/// A CAMERA INTEIRA, E NAO SO A POSICAO E O ALVO. O FOV, o corte perto, o
/// fundo e a ortografica mudam o enquadramento tanto quanto andar com a
/// camera — e trocar para ortografica sem redesenhar deixaria na tela um
/// quadro em perspectiva que ja nao corresponde a cena.
inline void misturar_camera(std::uint64_t& s, const Camera& c) noexcept {
  misturar_vec(s, c.posicao);
  misturar_vec(s, c.alvo);
  misturar_vec(s, c.rotacao_graus);
  misturar_impressao(s, c.usar_rotacao ? 1U : 0U);
  misturar_vec(s, c.cima);
  misturar_float(s, c.fov_graus);
  misturar_float(s, c.perto);
  misturar_float(s, c.longe);
  misturar_impressao(s, c.ortografica ? 1U : 0U);
  misturar_float(s, c.altura_ortografica);
}

inline void misturar_luz(std::uint64_t& s, const Luz& l) noexcept {
  misturar_impressao(s, static_cast<std::uint32_t>(l.tipo));
  misturar_vec(s, l.posicao);
  misturar_vec(s, l.direcao);
  misturar_float(s, l.vermelho);
  misturar_float(s, l.verde);
  misturar_float(s, l.azul);
  misturar_float(s, l.intensidade);
  misturar_float(s, l.alcance);
  misturar_float(s, l.angulo_interno_graus);
  misturar_float(s, l.angulo_externo_graus);
  misturar_impressao(s, l.ligada ? 1U : 0U);
}

/// O PONTO DE ANCORA NO ESPACO LOCAL DO MODELO. A ancora e 0..1 dentro da
/// CAIXA, e a caixa nem sempre esta centrada na origem — um modelo
/// exportado com o zero no canto esquerdo tem caixa de 0 a 2, e tratar a
/// ancora como se fosse em torno do zero giraria o modelo em volta de um
/// ponto que nao existe.
[[nodiscard]] Vec3 ponto_da_ancora(const geo::Caixa& caixa, Vec3 ancora) noexcept {
  const Vec3 c = caixa.centro();
  const Vec3 t = caixa.tamanho();
  return {caixa.minimo.x + t.x * ancora.x,
          caixa.minimo.y + t.y * ancora.y,
          caixa.minimo.z + t.z * ancora.z};
}

/// ACAIXA DE UM DESENHO NO MUNDO, para o corte de frustum (§17).
[[nodiscard]] geo::Caixa caixa_no_mundo(const geo::Caixa& local,
                                        const Mat4& mundo) noexcept {
  return geo::transformar_caixa(local, mundo);
}

/// O TESTE DE FRUSTUM PELOS SEIS PLANOS SERIA O CORRETO E E O QUE A GPU
/// FAZ. Aqui, com dezenas de objetos, o que decide e mais barato e quase
/// tao bom: o raio da esfera que envolve a caixa contra o angulo do cone
/// de visao. Uma esfera e uma aproximacao generosa — ela deixa passar
/// objeto que nao aparece — e generosa e o lado certo de errar: cortar o
/// que apareceria e um buraco na imagem, deixar passar e um desenho que a
/// GPU descarta sozinha.
[[nodiscard]] bool fora_do_campo(const geo::Caixa& caixa, const Mat4& vista,
                                 float tangente_meia_altura,
                                 float aspecto, float perto, float longe) noexcept {
  // SEM CAIXA NAO HA O QUE CORTAR. Uma malha sem vertice nao chega aqui (o
  // importador a descarta), mas um modelo degenerado nao pode ser cortado
  // por engano: quem decide e a GPU, que nao desenha nada.
  if (caixa.vazia) return false;

  const Vec3 c = geo::transformar_ponto(vista, caixa.centro());
  const float raio = geo::comprimento(caixa.tamanho()) * 0.5F;

  // A CAMERA OLHA PARA O -Z, entao a distancia para a frente e `-z`. A
  // caixa ocupa [d - raio, d + raio] ao longo do eixo.
  const float d = -c.z;

  // INTEIRA PARA TRAS DO OLHO, ou inteira alem do fundo: nao ha imagem.
  // A folga do raio e o que impede cortar um objeto que ainda encosta na
  // borda do plano — cortar o que apareceria e um buraco na imagem.
  if (d + raio < perto) return true;
  if (d - raio > longe) return true;

  // O CONE DE VISAO. O teto lateral cresce com a distancia: na altura do
  // centro, `tan(meia_abertura) * d`. Comparar contra ele e o mesmo que
  // testar contra os quatro planos laterais, so que mais barato.
  const float frente = d < perto ? perto : d;
  const float meio_x = (tangente_meia_altura * aspecto) * frente + raio;
  const float meio_y = tangente_meia_altura * frente + raio;
  return std::fabs(c.x) > meio_x || std::fabs(c.y) > meio_y;
}

}  // namespace

// ------------------------------------------------------------------ camera

void montar_camera(const Camera& camera, std::uint32_t largura,
                   std::uint32_t altura, Mat4& vista, Mat4& projecao,
                   Vec3& olho) noexcept {
  olho = camera.posicao;
  Vec3 para = camera.alvo;
  if (camera.usar_rotacao) {
    // A ROTACAO VIRA UM ALVO. Olhar para a frente (-Z girado) e o que a
    // rotacao de uma camera quer dizer no Aurea — igual a uma camada
    // plana: zero graus olha para o eixo -Z.
    const Quat q = Quat::de_euler(camera.rotacao_graus);
    para = camera.posicao + geo::girar(q, Vec3{0.0F, 0.0F, -1.0F});
  }
  vista = geo::olhar(camera.posicao, para, camera.cima);

  const float aspecto = altura == 0 ? 1.0F
                                    : static_cast<float>(largura) /
                                          static_cast<float>(altura);
  if (camera.ortografica) {
    // A ALTURA E O QUE SE DECLARA. A largura sai do aspecto, e nao o
    // contrario: e assim que a composicao mantem o enquadramento quando o
    // formato muda de retrato para paisagem.
    const float meia = camera.altura_ortografica * 0.5F;
    projecao = geo::ortografica(-meia * aspecto, meia * aspecto, -meia, meia,
                               camera.perto, camera.longe);
  } else {
    projecao = geo::perspectiva(camera.fov_graus, aspecto, camera.perto,
                               camera.longe);
  }
}

// ---------------------------------------------------------------- camada

Mat4 mundo_da_camada(const Camada3D& camada, const Modelo& modelo) noexcept {
  const Vec3 pivo = ponto_da_ancora(modelo.limites, camada.ancora);
  const Quat giro = Quat::de_euler(camada.rotacao_graus);

  // A CONTA, EM ORDEM: tira o pivo, escala, gira, poe de volta e translada.
  // Escalar ANTES de girar e o que faz uma escala nao-uniforme achatar o
  // modelo no eixo dele, e nao no eixo do mundo — a diferenca aparece como
  // um modelo que entorta quando gira.
  Mat4 m = Mat4::de_translacao(camada.posicao);
  m = m * Mat4::de_translacao(pivo);
  m = m * Mat4::de_trs({0.0F, 0.0F, 0.0F}, giro, camada.escala);
  m = m * Mat4::de_translacao({-pivo.x, -pivo.y, -pivo.z});
  return m;
}

Material material_da_camada(const Material& original,
                            const MaterialDaCamada& sobre) noexcept {
  if (!sobre.ligado) return original;
  Material m = original;
  m.cor_base = sobre.cor_base;
  if (sobre.metalico >= 0.0F) m.metalico = sobre.metalico;
  if (sobre.rugosidade >= 0.0F) m.rugosidade = sobre.rugosidade;
  if (sobre.forca_emissiva >= 0.0F) m.forca_emissiva = sobre.forca_emissiva;
  if (sobre.emissivo.a != 0 || sobre.emissivo.r != 0 || sobre.emissivo.g != 0 ||
      sobre.emissivo.b != 0) {
    m.emissivo = sobre.emissivo;
  }
  if (sobre.modo >= 0) m.modo = static_cast<Material::Modo>(sobre.modo);
  m.face_dupla = sobre.face_dupla;
  if (sobre.alfa_corte >= 0.0F) m.alfa_corte = sobre.alfa_corte;
  if (sobre.sem_textura_de_cor) m.textura_cor = -1;
  return m;
}

// ---------------------------------------------------------------- acervo

std::int32_t AcervoDeModelos::guardar(Modelo&& modelo) {
  std::int32_t alca = -1;
  if (!livres_.empty()) {
    alca = livres_.back();
    livres_.pop_back();
    fichas_[static_cast<std::size_t>(alca)] = Ficha{std::move(modelo), 0, {}};
  } else {
    alca = static_cast<std::int32_t>(fichas_.size());
    fichas_.push_back(Ficha{std::move(modelo), 0, {}});
  }
  // UMA ALCA NUNCA E ZERO, e o deslocamento de um garante isso sem gastar
  // um valor de teste em cada consulta: zero e o "nenhum" do Dart, e um
  // modelo valido respondendo zero seria uma camada que existe e nao
  // desenha.
  return alca + 1;
}

const Modelo* AcervoDeModelos::obter(std::int32_t alca) const noexcept {
  if (alca <= 0) return nullptr;
  const auto i = static_cast<std::size_t>(alca - 1);
  if (i >= fichas_.size()) return nullptr;
  return &fichas_[i].modelo;
}

void AcervoDeModelos::segurar(std::int32_t alca) noexcept {
  if (alca <= 0) return;
  const auto i = static_cast<std::size_t>(alca - 1);
  if (i < fichas_.size()) ++fichas_[i].usos;
}

void AcervoDeModelos::soltar(std::int32_t alca) noexcept {
  if (alca <= 0) return;
  const auto i = static_cast<std::size_t>(alca - 1);
  if (i < fichas_.size() && fichas_[i].usos > 0) --fichas_[i].usos;
}

std::uint32_t AcervoDeModelos::limpar_desocupados() noexcept {
  std::uint32_t quantos = 0;
  for (std::size_t i = 0; i < fichas_.size(); ++i) {
    if (fichas_[i].usos != 0 || fichas_[i].modelo.malhas.empty()) continue;
    // UMA FICHA VAZIA NAO SE REPETE NA LISTA DE LIVRES: sem a checagem, o
    // mesmo indice voltaria a circular e a alca mudaria de dono sozinha.
    const auto alca = static_cast<std::int32_t>(i + 1);
    if (std::find(livres_.begin(), livres_.end(), alca) == livres_.end()) {
      livres_.push_back(alca);
      ++quantos;
    }
    fichas_[i].modelo = Modelo{};
    fichas_[i].origem.clear();
  }
  return quantos;
}

void AcervoDeModelos::limpar() noexcept {
  fichas_.clear();
  livres_.clear();
}

std::uint64_t AcervoDeModelos::bytes() const noexcept {
  std::uint64_t total = 0;
  for (const Ficha& f : fichas_) total += f.modelo.bytes();
  return total;
}

Resulta<std::int32_t> AcervoDeModelos::importar_do_arquivo(
    const std::string& caminho, const OpcoesDeImportacao& opcoes,
    RelatoDaImportacao* relato) {
  // O MESMO ARQUIVO, A MESMA ALCA. Sem isso, arrastar dez vezes o mesmo
  // modelo para a timeline carregaria dez copias do mesmo modelo — 40 MB
  // de geometria identica na memoria, e o mesmo envio para a GPU dez vezes.
  const std::string chave = "f:" + caminho;
  for (const Ficha& f : fichas_) {
    if (f.origem == chave && !f.modelo.malhas.empty()) {
      return static_cast<std::int32_t>(&f - fichas_.data()) + 1;
    }
  }

  auto r = importar(caminho, opcoes, relato);
  if (r.tem_erro()) return r.erro();
  // O TETO DO ACERVO VALE AQUI, e nao no importador: um modelo grande
  // demais para o que ja esta carregado e recusado com um erro que o app
  // sabe explicar, em vez de derrubar o processo (§21, §43).
  const std::uint64_t entrando = r.valor().bytes();
  if (entrando > espaco_livre()) return Erro::orcamento_estourado;

  const std::int32_t alca = guardar(std::move(r).valor());
  fichas_[static_cast<std::size_t>(alca - 1)].origem = chave;
  return alca;
}

Resulta<std::int32_t> AcervoDeModelos::importar_da_memoria(
    const std::uint8_t* bytes, std::size_t tamanho, const std::string& extensao,
    const OpcoesDeImportacao& opcoes, RelatoDaImportacao* relato) {
  if (bytes == nullptr || tamanho == 0) return Erro::argumento;
  auto r = importar_memoria(bytes, tamanho, extensao, opcoes, relato);
  if (r.tem_erro()) return r.erro();
  const std::uint64_t entrando = r.valor().bytes();
  if (entrando > espaco_livre()) return Erro::orcamento_estourado;
  return guardar(std::move(r).valor());
}

// -------------------------------------------------------------- avaliacao

void avaliar(const Cena3D& cena, const AcervoDeModelos& acervo,
             Quadro3D& saida) {
  saida.camera = cena.camera;
  saida.luzes.clear();
  saida.opacos.clear();
  saida.transparentes.clear();
  saida.pele.clear();
  saida.ossos_por_pele.clear();
  saida.limites = geo::Caixa{};
  saida.triangulos = 0;
  saida.vertices = 0;
  saida.camadas = static_cast<std::uint32_t>(cena.camadas.size());
  saida.camadas_desenhadas = 0;
  saida.camadas_fora_do_campo = 0;
  saida.camadas_sem_modelo = 0;
  saida.sombra = cena.sombra;
  saida.amostras = cena.amostras;
  saida.largura = cena.largura;
  saida.altura = cena.altura;
  saida.ambiente[0] = cena.ambiente_vermelho;
  saida.ambiente[1] = cena.ambiente_verde;
  saida.ambiente[2] = cena.ambiente_azul;
  saida.ceu[0] = cena.ceu_vermelho;
  saida.ceu[1] = cena.ceu_verde;
  saida.ceu[2] = cena.ceu_azul;
  saida.chao[0] = cena.chao_vermelho;
  saida.chao[1] = cena.chao_verde;
  saida.chao[2] = cena.chao_azul;
  saida.reflexo_do_ambiente = cena.reflexo_do_ambiente;
  saida.mapa_de_ambiente = cena.mapa_de_ambiente;
  saida.mapa_de_ambiente_largura = cena.mapa_de_ambiente_largura;
  saida.mapa_de_ambiente_niveis = cena.mapa_de_ambiente_niveis;

  montar_camera(cena.camera, cena.largura, cena.altura, saida.vista,
                saida.projecao, saida.olho);

  for (const Luz& l : cena.luzes) {
    if (l.ligada) saida.luzes.push_back(l);
  }

  const float tangente =
      std::tan(cena.camera.fov_graus * (0.5F * geo::kGrau));

  std::uint64_t impressao = 0xCBF29CE484222325ULL;
  misturar_impressao(impressao, cena.largura);
  misturar_impressao(impressao, cena.altura);
  misturar_impressao(impressao, cena.luzes.size());
  misturar_impressao(impressao, static_cast<std::uint32_t>(cena.sombra));
  misturar_impressao(impressao, cena.amostras);
  misturar_float(impressao, cena.ambiente_vermelho);
  misturar_float(impressao, cena.ambiente_verde);
  misturar_float(impressao, cena.ambiente_azul);
  // O CEU E O CHAO ENTRAM NA IMPRESSAO. E a impressao que diz ao compositor
  // se o quadro pode ser reaproveitado: sem eles aqui, pintar a cena de
  // dourado nao redesenharia nada.
  misturar_float(impressao, cena.ceu_vermelho);
  misturar_float(impressao, cena.ceu_verde);
  misturar_float(impressao, cena.ceu_azul);
  misturar_float(impressao, cena.chao_vermelho);
  misturar_float(impressao, cena.chao_verde);
  misturar_float(impressao, cena.chao_azul);
  misturar_float(impressao, cena.reflexo_do_ambiente);
  // O MAPA DE AMBIENTE ENTRA PELO ENDERECO, E NAO PELO CONTEUDO.
  //
  // O aplicativo assa um mapa por estudio e guarda cada um num buffer
  // proprio; trocar de estudio troca o endereco, e o mesmo estudio reusa o
  // mesmo. Hashear os 700 KB a cada quadro diria a mesma coisa custando uma
  // varredura inteira por quadro — e a impressao existe justamente para
  // evitar trabalho repetido.
  misturar_impressao(
      impressao,
      static_cast<std::uint64_t>(
          reinterpret_cast<std::uintptr_t>(cena.mapa_de_ambiente)));
  misturar_impressao(impressao, cena.mapa_de_ambiente_largura);
  misturar_impressao(impressao, cena.mapa_de_ambiente_niveis);
  misturar_camera(impressao, cena.camera);
  for (const Luz& l : cena.luzes) misturar_luz(impressao, l);
  // A QUANTIDADE DE CAMADAS ENTRA AQUI, e nao so o conteudo de cada uma:
  // apagar a ultima camada da lista e uma cena sem camada nenhuma, e sem
  // este numero o laco abaixo nao mistura nada e a impressao ficaria igual
  // a de antes — a tela guardaria o modelo apagado.
  misturar_impressao(impressao, cena.camadas.size());

  // A POSE E AMOSTRADA UMA VEZ POR MODELO, E NAO UMA VEZ POR CAMADA. Tres
  // camadas do mesmo modelo animado, cada uma no seu instante, pedem tres
  // amostragens — mas cada uma delas e uma so, e o resultado e compartilhado
  // com todos os desenhos daquela camada. Amostrar por MALHA refaria a
  // arvore de ossos inteira para cada pedaco do mesmo modelo.
  Pose pose;
  std::int32_t pose_de = -2;   // -2 = nenhuma amostrada ainda
  double pose_tempo = 0.0;

  for (const Camada3D& camada : cena.camadas) {
    // A IMPRESSAO VEM ANTES DO CORTE. Uma camada invisivel nao desenha, mas
    // ela e parte do que o dono pediu — e uma impressao que ignora o que
    // foi escondido congela a ultima imagem em vez de apagar.
    misturar_camada(impressao, camada);
    if (!camada.visivel) continue;
    const Modelo* modelo = acervo.obter(camada.modelo);
    if (modelo == nullptr || modelo->malhas.empty()) {
      ++saida.camadas_sem_modelo;
      continue;
    }

    // A POSE E RECALCULADA QUANDO MUDA O MODELO OU O INSTANTE. Comparar
    // por identidade seria mais rapido e estaria ERRADO: dois objetos
    // `Modelo` diferentes podem ter o mesmo conteudo e a mesma alca reciclada.
    if (pose_de != camada.modelo || pose_tempo != camada.tempo_da_animacao ||
        pose_de == -2) {
      amostrar_pose(*modelo, camada.animacao, camada.tempo_da_animacao, pose);
      pose_de = camada.modelo;
      pose_tempo = camada.tempo_da_animacao;
    }

    const Mat4 mundo = mundo_da_camada(camada, *modelo);
    const geo::Caixa no_mundo = caixa_no_mundo(modelo->limites, mundo);
    saida.limites.incluir(no_mundo);

    if (fora_do_campo(no_mundo, saida.vista, tangente,
                      cena.altura == 0 ? 1.0F
                                       : static_cast<float>(cena.largura) /
                                             static_cast<float>(cena.altura),
                      cena.camera.perto, cena.camera.longe)) {
      ++saida.camadas_fora_do_campo;
      continue;
    }

    // A FATIA DE OSSOS DESTA CAMADA. Cada camada que anima tem a propria
    // copia das matrizes — duas camadas do mesmo modelo em instantes
    // diferentes NAO podem dividir o mesmo bloco, ou a segunda pose
    // sobrescreveria a primeira.
    std::int32_t primeira_pele = -1;
    if (modelo->tem_esqueleto() && !pose.ossos.empty()) {
      primeira_pele = static_cast<std::int32_t>(saida.pele.size());
      saida.pele.insert(saida.pele.end(), pose.ossos.begin(), pose.ossos.end());
      saida.ossos_por_pele.push_back(
          static_cast<std::uint32_t>(pose.ossos.size()));
    }

    const float distancia =
        geo::comprimento(no_mundo.centro() - saida.olho);

    for (const Malha& malha : modelo->malhas) {
      if (malha.faixas.empty()) continue;
      const std::int32_t indice_da_malha =
          static_cast<std::int32_t>(&malha - modelo->malhas.data());
      saida.triangulos += static_cast<std::uint32_t>(malha.indices.size() / 3);
      saida.vertices += static_cast<std::uint32_t>(malha.vertices.size());

      // UMA FAIXA, UM DESENHO. Uma malha de GLB costuma trazer varias
      // primitivas, cada uma com o seu material; desenhar a malha inteira
      // de uma vez pintaria tudo com o material da primeira.
      for (const Faixa& faixa : malha.faixas) {
        if (faixa.quantidade == 0) continue;
        const Material original =
            (faixa.material >= 0 &&
             static_cast<std::size_t>(faixa.material) <
                 modelo->materiais.size())
                ? modelo->materiais[static_cast<std::size_t>(faixa.material)]
                : Material{};
        Material material = material_da_camada(original, camada.material);

        Desenho d;
        d.modelo = camada.modelo;
        d.malha = indice_da_malha;
        d.primeiro_indice = faixa.primeiro_indice;
        d.quantidade_indices = faixa.quantidade;
        d.base_do_vertice = faixa.base_do_vertice;
        d.mundo = mundo;
        d.pele = malha.esqueletica ? primeira_pele : -1;
        d.opacidade = camada.opacidade;
        d.cor = camada.cor;
        d.distancia = distancia;
        d.alca = camada.alca;

        // A OPACIDADE DA CAMADA E A DO MATERIAL SAO A MESMA COISA: uma
        // camada a 50% sobre um material que ja era translucido tem de sair
        // mais fraca, e nao igual. Multiplicar aqui e o que faz os dois
        // controles somarem em vez de um anular o outro.
        const float alfa_do_material =
            static_cast<float>(material.cor_base.a) * (1.0F / 255.0F);
        const float alfa = alfa_do_material * camada.opacidade;
        material.cor_base.a = static_cast<std::uint8_t>(
            alfa <= 0.0F ? 0.0F : (alfa >= 1.0F ? 255.0F : alfa * 255.0F + 0.5F));

        d.material = material;

        // O QUE E TRANSPARENTE VAI PARA A LISTA DE TRAS, E ORDENADO. Um
        // material em modo transparente com alfa cheio ainda e transparente:
        // quem decidiu foi o arquivo, e o arquivo costuma estar certo sobre
        // a propria intencao.
        if (material.modo == Material::Modo::transparente && alfa < 1.0F) {
          saida.transparentes.push_back(d);
        } else {
          saida.opacos.push_back(d);
        }
      }
    }
    // CONTA-SE A CAMADA, E NAO A MALHA: a barra de estado responde "quantas
    // camadas 3D desenharam", e nao "quantos pedacos de geometria" — a
    // segunda pergunta nao interessa a quem esta usando o app.
    ++saida.camadas_desenhadas;
  }

  // --------------------------------------------------- ordem dos desenhos
  //
  // OS OPACOS SAO ORDENADOS POR MATERIAL, e nao por distancia. Trocar de
  // material e o que quebra o lote na GPU: desenhar todos os pedacos do
  // mesmo material em sequencia reduz as trocas de estado a uma por
  // material. A profundidade quem resolve e o teste de profundidade, que
  // da o mesmo resultado em qualquer ordem.
  std::stable_sort(saida.opacos.begin(), saida.opacos.end(),
                   [](const Desenho& a, const Desenho& b) {
                     // O MODELO VEM PRIMEIRO nao por gosto: dois modelos
                     // diferentes tem buffers de vertice diferentes, e
                     // agrupar por modelo primeiro e o que faz a troca de
                     // buffer acontecer uma vez por modelo em vez de uma vez
                     // por peca. O `pele` vem depois porque ele so reordena
                     // dentro do mesmo modelo, onde a geometria ja e a mesma.
                     if (a.modelo != b.modelo) return a.modelo < b.modelo;
                     if (a.pele != b.pele) return a.pele < b.pele;
                     if (a.malha != b.malha) return a.malha < b.malha;
                     return a.primeiro_indice < b.primeiro_indice;
                   });

  // OS TRANSPARENTES VÃO DO MAIS LONGE PARA O MAIS PERTO. Aqui a ordem
  // decide o resultado, porque nao ha teste de profundidade que resolva a
  // mistura: e exatamente por isso que eles sao desenhados depois e
  // ordenados.
  std::stable_sort(saida.transparentes.begin(), saida.transparentes.end(),
                   [](const Desenho& a, const Desenho& b) {
                     return a.distancia > b.distancia;
                   });

  // O DESENHO INTEIRO, E NAO SO A MALHA. O que sai daqui e o que a GPU
  // recebe: se um campo deste nao estiver na impressao, mexer nele nao
  // redesenha — e o sintoma e o controle que se arrasta sem a tela mudar.
  const auto misturar_desenho = [&impressao](const Desenho& d) {
    misturar_impressao(impressao, d.alca);
    misturar_indice(impressao, d.modelo);
    misturar_indice(impressao, d.malha);
    misturar_indice(impressao, d.pele);
    misturar_impressao(impressao, d.primeiro_indice);
    misturar_impressao(impressao, d.quantidade_indices);
    misturar_impressao(impressao, d.base_do_vertice);
    misturar_float(impressao, d.opacidade);
    misturar_cor(impressao, d.cor);
    misturar_float(impressao, d.distancia);
    misturar_material(impressao, d.material);
    for (int i = 0; i < 16; ++i) misturar_float(impressao, d.mundo.m[i]);
  };
  for (const Desenho& d : saida.opacos) misturar_desenho(d);
  for (const Desenho& d : saida.transparentes) misturar_desenho(d);

  saida.impressao = impressao;
}

}  // namespace aurea::render::tresd
