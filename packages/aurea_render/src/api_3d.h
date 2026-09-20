// A PORTA DO 3D: DILIGENT + VULKAN + ASSIMP.
//
// ============================ O QUE ESTA PORTA E =======================
// A ponte FINA entre o Dart e o motor 3D nativo (§39). Fina quer dizer
// tres coisas, e as tres sao regras de projeto e nao gosto:
//
//  1. UMA CHAMADA POR QUADRO, E NAO UMA POR OBJETO. A cena inteira viaja
//     numa struct de tamanho fixo com dois vetores apontados: as camadas e
//     as luzes. Nao ha `colocar_vertice`, `por_osso` nem `mover_objeto`.
//     Dez mil vertices sao dez mil vertices DENTRO de uma chamada.
//
//  2. O C++ MONTA A CENA, E O DART SO CONTA O QUE ACONTECEU NA TIMELINE.
//     O Dart nao sabe o que e uma matriz de osso, um mapa de sombra ou um
//     pipeline. Ele diz "esta camada, deste modelo, neste instante, com
//     esta posicao" — e o `avaliar` da cena resolve o resto.
//
//  3. NADA DE MEMORIA VIVA DO C++ APONTA PARA O DART DEPOIS DA CHAMADA.
//     As duas excecoes sao declaradas: os pixels do ultimo quadro (validos
//     ate o proximo desenho) e as strings de motivo/erro (estaticas dentro
//     da biblioteca). A cena de entrada e COPIADA antes do retorno.
//
// ============================ AS ALCAS =================================
// O acervo de modelos numera o que ja foi importado. A alca NUNCA E ZERO
// (zero e o "nenhum" do Dart) e ela e o que liga uma camada ao modelo.
// Quem cria uma camada CHAMA `segurar`; quem a apaga chama `soltar`. Sem
// isso, `limpar_desocupados` apaga um modelo que ainda esta em uso — que e
// o defeito classico de contagem de referencia, e o sintoma e a cena
// perder a geometria depois de alguns minutos de uso.
//
// ============================ A IMPORTACAO =============================
// A IMPORTACAO TEM DOIS TEMPOS, e a razao e o §26: ler um GLB de 200 MB
// com o Assimp leva segundos, e segundos no thread da interface sao o app
// travado. Entao:
//
//    1. `aurea_render_3d_ler_arquivo` — LE e devolve uma carga pendente
//       (`void*`). NAO toca no acervo. Feita na thread de fundo.
//    2. `aurea_render_3d_adotar` — MOVE a carga para o acervo e devolve a
//       alca. Feita na thread principal, e instantanea (um move).
//
// Uma carga pendente pertence a UMA thread de cada vez: quem a cria e a
// de fundo, quem a adota e a principal, e a passagem pelo `Isolate` do
// Dart ja estabelece a ordem. NAO ha trava nenhuma aqui, e nao precisa
// haver — o que nao e compartilhado nao precisa de mutex.
#ifndef AUREA_RENDER_API_3D_H
#define AUREA_RENDER_API_3D_H

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------- a ABI

/// AS STRUCTS DA CENA. Todas POD, todas de tamanho fixo, todas conferidas
/// contra o `sizeOf` do lado Dart por `aurea_render_3d_tamanho`.
///
/// POR QUE STRUCT E NAO UM VETOR DE DOUBLES: a cena tem campos de tipos
/// diferentes (indices, floats, cores de 8 bits, um tempo em segundos) e
/// um vetor de doubles obrigaria a inventar uma posicao para cada um. As
/// structs de SAIDA das estatisticas continuam sendo vetores de doubles,
/// pela razao escrita no `api.cpp`: alinhamento de struct mista e decisao
/// do compilador, e um campo a mais vira um numero plausivel e errado.
///
/// TODAS AS CORES SAO RGBA NA ORDEM DOS BYTES (r, g, b, a), e nao o ARGB
/// do `Color` do Flutter: quem converte e a ponte Dart, em um lugar so.

typedef struct Aurea3DCamera {
  float posicao[3];
  float alvo[3];
  float rotacao[3];
  int32_t usar_rotacao;
  float cima[3];
  float fov_graus;
  float perto;
  float longe;
  int32_t ortografica;
  float altura_ortografica;
} Aurea3DCamera;

/// A SOBRESCRITA DE MATERIAL DA CAMADA. `-1` em metalico, rugosidade,
/// forca_emissiva, alfa_corte e modo quer dizer "o que o modelo traz" — e
/// nao zero, que e uma escolha artistica legitima.
typedef struct Aurea3DMaterial {
  int32_t ligado;
  uint8_t cor_base[4];
  float metalico;
  float rugosidade;
  float forca_emissiva;
  uint8_t emissivo[4];
  int32_t modo;
  int32_t face_dupla;
  float alfa_corte;
  int32_t sem_textura_de_cor;
} Aurea3DMaterial;

typedef struct Aurea3DCamada {
  uint64_t alca;
  int32_t modelo;
  int32_t animacao;
  double tempo_da_animacao;
  int32_t visivel;
  float posicao[3];
  float rotacao_graus[3];
  float escala[3];
  float ancora[3];
  float opacidade;
  uint8_t cor[4];
  Aurea3DMaterial material;
  float camada_z;
} Aurea3DCamada;

typedef struct Aurea3DLuz {
  /// 0 direcional, 1 pontual, 2 holofote (o `TipoDeLuz` do C++).
  int32_t tipo;
  float posicao[3];
  float direcao[3];
  float cor[3];
  float intensidade;
  float alcance;
  float angulo_interno_graus;
  float angulo_externo_graus;
  int32_t ligada;
} Aurea3DLuz;

/// A CENA DE UM QUADRO. `camadas` e `luzes` sao vetores do chamador, e
/// eles precisam valer ate o retorno de `aurea_render_3d_desenhar` — a
/// cena e copiada para dentro do motor antes de desenhar.
typedef struct Aurea3DCena {
  const Aurea3DCamada* camadas;
  uint32_t quantidade_de_camadas;
  uint32_t quantidade_de_luzes;
  const Aurea3DLuz* luzes;
  /// 0 desligada, 1 baixa, 2 media, 3 alta (`QualidadeDaSombra`).
  int32_t sombra;
  uint32_t amostras;
  uint32_t largura;
  uint32_t altura;
  float ambiente[3];
  Aurea3DCamera camera;
  uint32_t reserva;
  /// O AMBIENTE COM DIRECAO: a cor de cima e a cor de baixo, em sRGB.
  /// Neutro (branco em cima e embaixo) e o ambiente plano de antes.
  float ceu[3];
  /// Quanto do ambiente volta no reflexo espelhado (0..1). E o que faz um
  /// metal parecer metal: sem ele, um metal so mostra o realce da luz.
  float reflexo_do_ambiente;
  float chao[3];
  float chao_reserva;

  /// O MAPA DE AMBIENTE (o estudio que o metal reflete), em radiancia
  /// LINEAR e ja normalizado — a media dele vale 1, e quem cuida disso e o
  /// aplicativo. Nulo = "nao ha mapa": o reflexo volta a sair do ceu e do
  /// chao, que e o comportamento de antes.
  ///
  /// OS NIVEIS VEM CONCATENADOS, do maior para o menor: o nivel `k` tem
  /// `largura >> k` de largura por metade disso de altura, quatro floats
  /// por pixel. O motor copia tudo antes de desenhar — o ponteiro so
  /// precisa valer ate o retorno.
  const float* ambiente_mapa;
  uint32_t ambiente_mapa_largura;
  uint32_t ambiente_mapa_niveis;
} Aurea3DCena;

typedef struct Aurea3DOpcoes {
  uint64_t teto_de_bytes_do_arquivo;
  int32_t sem_animacao;
  float escala;
  int32_t textura_neutra_quando_faltar;
} Aurea3DOpcoes;

/// O QUE A IMPORTACAO PRODUZIU. Serve para a ficha da camada e para o
/// teste que confere que o arquivo foi lido de verdade (§48).
///
/// A MENSAGEM DO PRIMEIRO AVISO NAO CABE AQUI. Um caminho de arquivo de
/// 4 KB num campo de tamanho fixo obrigaria a cortar — e o campo, cortado,
/// deixa de ser a mensagem. Ela sai por `aurea_render_3d_ultimo_aviso`,
/// que e o mesmo caminho que o `aurea_render_preview_motivo` ja usa.
typedef struct Aurea3DRelato {
  uint64_t bytes_do_arquivo;
  uint32_t malhas;
  uint32_t materiais;
  uint32_t texturas;
  uint32_t nos;
  uint32_t ossos;
  uint32_t animacoes;
  uint32_t triangulos;
  uint32_t vertices;
  uint64_t bytes_em_memoria;
  uint32_t avisos;
  uint32_t reserva;
} Aurea3DRelato;

/// A FICHA DE UM MODELO NO ACERVO. `na_gpu` responde "ja subiu?" — e o que
/// a barra de estado mostra quando o dono pergunta por que o primeiro
/// quadro com o modelo deu uma engasgada.
typedef struct Aurea3DFicha {
  uint32_t malhas;
  uint32_t materiais;
  uint32_t texturas;
  uint32_t nos;
  uint32_t ossos;
  uint32_t animacoes;
  uint32_t triangulos;
  uint32_t vertices;
  uint64_t bytes_de_malha;
  uint64_t bytes_de_textura;
  float limites[6];
  int32_t na_gpu;
  uint32_t reserva;
} Aurea3DFicha;

/// UMA MALHA CRUA, DO JEITO QUE O DART JA A TEM.
///
/// ============================ POR QUE ISTO EXISTE =====================
/// O ASSIMP NAO E O UNICO CAMINHO PARA A GEOMETRIA. O Aurea ja sabe
/// construir, em Dart, tudo o que nao vem de arquivo: os solidos nativos
/// (cubo, esfera, toro, capsula, cilindro), o texto 3D extrudado, as formas
/// parametricas, o terreno, e o que o importador Dart leu de um OBJ. Sem
/// esta porta, essas geometrias continuariam presas ao pintor de CPU para
/// sempre — e "o motor novo so desenha GLB" seria uma promessa pela metade.
///
/// UMA CHAMADA POR MALHA, E NAO POR VERTICE. Os vetores sao do chamador e
/// a copia acontece dentro: uma malha de dez mil vertices atravessa isto
/// uma vez, e nao dez mil.
///
/// O MATERIAL E POR MALHA, e nao por modelo: uma malha com dois materiais
/// e duas entradas nesta lista, cada uma com o seu. E o que o
/// `DrawIndexed` desenha, e nao ha o que juntar.
typedef struct Aurea3DMalhaCrua {
  /// 3 floats por vertice, OBRIGATORIO.
  const float* posicoes;
  /// 3 floats por vertice. NULO faz o motor calcular a normal PLANA —
  /// que e o que um cubo quer e o que uma esfera nao quer.
  const float* normais;
  /// 2 floats por vertice. NULO deixa tudo em (0,0).
  const float* uvs;
  /// 4 bytes por vertice, RGBA na ordem dos bytes. NULO deixa branco.
  const uint8_t* cores;
  /// 3 indices por triangulo, OBRIGATORIO.
  const uint32_t* indices;
  uint32_t quantidade_de_vertices;
  uint32_t quantidade_de_indices;

  /// 0 opaco, 1 mascarado, 2 transparente — o `Material::Modo` do C++.
  int32_t modo;
  float alfa_corte;
  int32_t face_dupla;

  uint8_t cor_base[4];
  float metalico;
  float rugosidade;
  float forca_emissiva;
  uint8_t emissivo[4];

  // ===================== OS CINCO MAPAS DO PBR =========================
  //
  // RGBA8 CRU, `largura * altura * 4` bytes, sem cadeia de desfoque. NULO
  // quer dizer "esta malha nao tem este mapa" — e o shader cai na neutra
  // daquele canal (branca para a cor, plana para a normal), que e o mesmo
  // caminho de um material feito so de numeros.
  //
  // POR QUE RGBA8 CRU, E NAO O PNG DE DENTRO DO ARQUIVO. Quem ja decodificou
  // foi o Dart: o `TextureCache` do aplicativo precisa da mesma imagem para
  // o pintor de CPU, e decodificar de novo aqui seria a segunda copia do
  // mesmo PNG na memoria. O caminho do Assimp (`importar`) continua
  // decodificando por conta propria, porque la ninguem decodificou antes.
  //
  // O PONTEIRO REPETIDO E UMA TEXTURA SO. Um modelo com quatro malhas que
  // dividem o mesmo mapa de cor sobe UMA vez: o `criar_modelo` reconhece o
  // ponteiro e reaproveita o indice. Sem isso, um atlas de 2048 quadrados
  // viraria 64 MB na placa por causa de quatro chamadas de desenho.
  //
  // O ESPACO DE COR E DECIDIDO AQUI, e nao pelo chamador: cor e emissiva
  // sao sRGB (sao cor), normal, metalico-rugosidade e oclusao sao lineares
  // (sao dado). Deixar isso para quem chama daria a mesma textura em dois
  // tons conforme o caminho (§6).
  const uint8_t* textura_cor;
  uint32_t textura_cor_largura;
  uint32_t textura_cor_altura;

  const uint8_t* textura_normal;
  uint32_t textura_normal_largura;
  uint32_t textura_normal_altura;

  /// O VERDE E A RUGOSIDADE E O AZUL E O METALICO — a ordem do glTF.
  const uint8_t* textura_metalico_rugosidade;
  uint32_t textura_metalico_rugosidade_largura;
  uint32_t textura_metalico_rugosidade_altura;

  const uint8_t* textura_emissiva;
  uint32_t textura_emissiva_largura;
  uint32_t textura_emissiva_altura;

  /// O VERMELHO E O VALOR — tambem a ordem do glTF.
  const uint8_t* textura_oclusao;
  uint32_t textura_oclusao_largura;
  uint32_t textura_oclusao_altura;

  /// Quanto da oclusao do mapa vale. 1 e o padrao do glTF.
  float forca_da_oclusao;
} Aurea3DMalhaCrua;

/// QUAL STRUCT SE QUER MEDIR. O teste do lado Dart compara cada resposta
/// com o `sizeOf` do `Struct` correspondente: uma struct mal declarada nao
/// avisa, ela le o campo do vizinho e devolve um numero plausivel.
typedef enum Aurea3DTamanho {
  AUREA3D_TAM_CAMERA = 0,
  AUREA3D_TAM_MATERIAL = 1,
  AUREA3D_TAM_CAMADA = 2,
  AUREA3D_TAM_LUZ = 3,
  AUREA3D_TAM_CENA = 4,
  AUREA3D_TAM_RELATO = 5,
  AUREA3D_TAM_FICHA = 6,
  AUREA3D_TAM_OPCOES = 7,
  /// NO FIM DA LISTA DE PROPOSITO: a ordem dos primeiros oito e a que o
  /// Dart ja conhece, e acrescentar no meio mudaria o significado de um
  /// numero que ja esta compilado do outro lado.
  AUREA3D_TAM_MALHA_CRUA = 8,
  AUREA3D_TAM_QUANTOS = 9
} Aurea3DTamanho;

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------ a versao

/// A VERSAO DESTA PORTA. Separada da versao do 2D (`aurea_render_versao`)
/// para que o 3D entre e saia sem mexer no que ja funciona.
int32_t aurea_render_3d_versao(void);

/// O TAMANHO DE UMA DAS STRUCTS ACIMA. `AUREA3D_TAM_QUANTOS` pede a
/// contagem de tipos, e um indice fora da faixa devolve zero.
uint32_t aurea_render_3d_tamanho(uint32_t qual);

// -------------------------------------------------------- o dispositivo

/// ABRE O DISPOSITIVO 3D. 1 subiu, 0 nao subiu. UMA VEZ POR PROCESSO.
int32_t aurea_render_3d_preparar(void);
int32_t aurea_render_3d_pronto(void);
/// POR QUE NAO SUBIU. String ESTATICA dentro da biblioteca.
const char* aurea_render_3d_motivo(void);
const char* aurea_render_3d_backend(void);
/// O ULTIMO ERRO DO RENDERIZADOR. String ESTATICA dentro da biblioteca.
const char* aurea_render_3d_ultimo_erro(void);

// ------------------------------------------------------------ a leitura

/// LE UM ARQUIVO E DEVOLVE UMA CARGA PENDENTE. NAO toca no acervo: e este
/// o passo que pode rodar na thread de fundo sem trava nenhuma (§26).
/// Devolve nulo quando falhou, e o motivo sai em `relato` (quando pedido)
/// e em `aurea_render_3d_ultimo_erro`.
void* aurea_render_3d_ler_arquivo(const char* caminho,
                                  const Aurea3DOpcoes* opcoes,
                                  Aurea3DRelato* relato);

/// A MESMA COISA com os bytes ja na mao. `extensao` decide o importador
/// (".glb", ".gltf", ".fbx", ".obj") e pode vir com ou sem o ponto.
void* aurea_render_3d_ler_memoria(const uint8_t* bytes, size_t tamanho,
                                  const char* extensao,
                                  const Aurea3DOpcoes* opcoes,
                                  Aurea3DRelato* relato);

/// ADOTA A CARGA: move o modelo para o acervo e devolve a ALCA (> 0). A
/// carga deixa de existir — nao se adota duas vezes. Devolve 0 (ou o
/// negativo do `Erro`) quando nao deu.
int64_t aurea_render_3d_adotar(void* carga, Aurea3DRelato* relato);

/// JOGA FORA UMA CARGA que nao vai ser adotada. Nulo e aceito e ignorado.
void aurea_render_3d_descartar(void* carga);

/// A MENSAGEM DO PRIMEIRO AVISO DA ULTIMA LEITURA. Escreve ate
/// `capacidade` bytes, com terminador, e devolve quantos escreveu.
uint32_t aurea_render_3d_ultimo_aviso(char* saida, uint32_t capacidade);

/// O ATALHO SINCRONO: ler e adotar na mesma chamada. Existe para a
/// bancada e para o arquivo pequeno — a interface chama os dois tempos
/// separados.
int64_t aurea_render_3d_importar_arquivo(const char* caminho,
                                         const Aurea3DOpcoes* opcoes,
                                         Aurea3DRelato* relato);

int64_t aurea_render_3d_importar_memoria(const uint8_t* bytes, size_t tamanho,
                                         const char* extensao,
                                         const Aurea3DOpcoes* opcoes,
                                         Aurea3DRelato* relato);

/// GUARDA GEOMETRIA QUE NAO VEIO DE ARQUIVO. Devolve a alca, ou o negativo
/// do `Erro` — `argumento` para um vetor nulo, um indice fora da faixa ou um
/// numero de indices que nao e multiplo de tres, e `orcamento_estourado`
/// quando a malha nao cabe no que resta do teto do acervo (§21).
///
/// E O MESMO ACERVO, AS MESMAS ALCAS e o mesmo `segurar`/`soltar` do
/// caminho do Assimp: para o renderizador, um cubo feito de numeros e um
/// cubo lido de um GLB sao a mesma coisa.
int64_t aurea_render_3d_criar_modelo(const Aurea3DMalhaCrua* malhas,
                                     uint32_t quantas, Aurea3DRelato* relato);

// -------------------------------------------------------------- o acervo

uint32_t aurea_render_3d_acervo_quantidade(void);
uint64_t aurea_render_3d_acervo_bytes(void);
/// O TETO DE MEMORIA DO ACERVO, em bytes. Estourar o teto nao recusa a
/// importacao de um modelo SO — ele existe para impedir o segundo (§21).
void aurea_render_3d_acervo_definir_teto(uint64_t bytes);
uint64_t aurea_render_3d_acervo_teto(void);

/// SEGURA E SOLTA. Quem cria uma camada 3D chama `segurar`; quem a apaga
/// chama `soltar`. A contagem e o que impede `limpar_desocupados` de
/// apagar um modelo em uso.
void aurea_render_3d_segurar(int32_t alca);
void aurea_render_3d_soltar(int32_t alca);
/// APAGA O QUE NINGUEM SEGURA. Devolve quantos sairam.
uint32_t aurea_render_3d_limpar_desocupados(void);
void aurea_render_3d_acervo_limpar(void);

/// A FICHA DE UM MODELO. 1 quando a alca existe.
int32_t aurea_render_3d_ficha(int32_t alca, Aurea3DFicha* saida);

/// A DURACAO DE UM CLIPE, em segundos. Zero quando o indice nao existe.
double aurea_render_3d_duracao_da_animacao(int32_t alca, int32_t animacao);

/// QUANTO DE MEMORIA DE GPU O APARELHO TEM, em bytes. Zero quando o
/// aparelho nao informa (§21).
uint64_t aurea_render_3d_memoria_de_gpu(void);

// -------------------------------------------------------------- o desenho

/// DESENHA A CENA. 1 desenhou, 0 nao desenhou (e `ultimo_erro` diz o que
/// foi). Um quadro que nao desenha NAO derruba o app: o alvo fica com o
/// que tinha (§43).
int32_t aurea_render_3d_desenhar(const Aurea3DCena* cena);

/// OS PIXELS DO ULTIMO QUADRO, em RGBA8 PREMULTIPLICADO, linhas de cima
/// para baixo, `largura * altura * 4` bytes. O PONTEIRO PERTENCE AO MOTOR
/// E VALE ATE O PROXIMO DESENHO: copiar antes de desenhar de novo.
const uint8_t* aurea_render_3d_pixels(void);
uint32_t aurea_render_3d_largura(void);
uint32_t aurea_render_3d_altura(void);

/// SOBE PARA A PLACA O QUE AINDA NAO SUBIU, sem desenhar. Chamado logo
/// depois de adotar um modelo, para o primeiro quadro com ele nao pagar a
/// subida (§26). Devolve quantas malhas subiram.
uint32_t aurea_render_3d_aquecer(void);

/// ESQUECE O QUE NAO ESTA MAIS NO ACERVO, na GPU. Devolve quantos sairam.
uint32_t aurea_render_3d_limpar_gpu(void);

/// ESQUECE TUDO. Chamado quando o app vai fechar ou trocar de projeto.
void aurea_render_3d_liberar(void);

// -------------------------------------------------------------- numeros

/// AS ESTATISTICAS DO ULTIMO QUADRO, num vetor de doubles na ordem fixa
/// abaixo (a mesma razao do `aurea_render_estatisticas`).
///
///   0 desenhos                5 texturas_na_gpu      10 lado_da_sombra
///   1 desenhos_esqueleticos   6 pipelines            11 sombra_ligada
///   2 triangulos              7 quadros             12 antisserrilhado
///   3 instancias              8 ultimo_desenho_ms
///   4 malhas_na_gpu           9 bytes_de_gpu
///
/// Devolve quantos campos escreveu.
uint32_t aurea_render_3d_estatisticas(double* saida, uint32_t capacidade);

/// OS NUMEROS DO ULTIMO QUADRO AVALIADO, na ordem fixa abaixo. Sao a
/// resposta para "por que o modelo sumiu": camadas 3 e desenhadas 1 e uma
/// conta que o dono entende.
///
///   0 camadas   1 camadas_desenhadas   2 camadas_fora_do_campo
///   3 camadas_sem_modelo   4 triangulos   5 vertices
///   6 ossos_no_quadro      7 luzes
///
/// Devolve quantos campos escreveu.
uint32_t aurea_render_3d_quadro_numeros(uint32_t* saida, uint32_t capacidade);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AUREA_RENDER_API_3D_H
