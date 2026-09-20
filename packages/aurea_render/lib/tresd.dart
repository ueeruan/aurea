/// A PORTA DART DO MOTOR 3D NATIVO.
///
/// ============================ O QUE ATRAVESSA ==========================
/// UMA CENA POR QUADRO, E NAO UMA CHAMADA POR OBJETO. O Dart diz o que
/// aconteceu na timeline — esta camada, deste modelo, neste instante, com
/// esta posicao — e o C++ monta a matriz, a pose, o material, o corte de
/// frustum e o desenho. Nao ha `moverVertice`, `porOsso` nem
/// `definirMatriz` aqui, e nao pode haver: mil vertices sao mil vertices
/// DENTRO de uma chamada.
///
/// ============================ O QUE VOLTA =============================
/// OS PIXELS DO QUADRO, ja compostos com profundidade, sombra e
/// transparencia, em RGBA8 premultiplicado. Eles NAO viram uma janela: o
/// caminho e registra-los como textura na composicao 2D que ja existe, e
/// e assim que o 3D e uma CAMADA e nao um lugar separado (§1, §14).
///
/// ============================ A IMPORTACAO ============================
/// EM DOIS TEMPOS, por causa do §26. [Motor3D.lerArquivo] so LE e devolve
/// uma carga pendente — ele roda na thread de fundo, e e ali que o Assimp
/// gasta os segundos dele. [Motor3D.adotar] move a carga para o acervo, na
/// thread principal, e e instantaneo. Uma carga pertence a UMA thread de
/// cada vez, entao nao ha trava nenhuma em lugar nenhum.
// PARTE DE `aurea_render.dart`, E NAO UMA BIBLIOTECA PROPRIA: os `@Native`
// so resolvem dentro do arquivo que o build hook declarou, e um segundo
// arquivo com `@Native` cai em "No asset with id ... found". Como `part`,
// os dois sao a MESMA biblioteca.
part of 'aurea_render.dart';

// ============================================================== a ABI

/// A VERSAO DESTA PORTA. Separada da versao do 2D: o 3D entra e sai sem
/// mexer no que ja funciona, e um Dart antigo simplesmente nao chama.
const int versaoEsperadaDaPorta3D = 2;

/// OS INDICES DE [Motor3D.tamanhos], na mesma ordem do enum do C++.
abstract final class Tamanho3D {
  static const int camera = 0;
  static const int material = 1;
  static const int camada = 2;
  static const int luz = 3;
  static const int cena = 4;
  static const int relato = 5;
  static const int ficha = 6;
  static const int opcoes = 7;

  /// NO FIM DA LISTA, e nao no meio: os oito primeiros indices ja estavam
  /// compilados do outro lado.
  static const int malhaCrua = 8;
  static const int quantos = 9;
}

// AS STRUCTS, ACHATADAS.
//
// Um `Struct` aninhado dentro de outro funciona no `dart:ffi`, mas o
// arquivo ja resolveu isso uma vez: achatado o resultado e o MESMO byte a
// byte e nao ha alinhamento de dois `Struct` para dar errado. O que
// garante que o achatamento continua certo e o teste de tamanho, que
// compara cada `sizeOf` com o que o C++ responde.
//
// TODA COR E RGBA NA ORDEM DOS BYTES. O `Color` do Flutter e ARGB: quem
// converte e [Ponte3D], num lugar so.

final class _Camera3DC extends Struct {
  @Float()
  external double posicaoX;
  @Float()
  external double posicaoY;
  @Float()
  external double posicaoZ;

  @Float()
  external double alvoX;
  @Float()
  external double alvoY;
  @Float()
  external double alvoZ;

  @Float()
  external double rotacaoX;
  @Float()
  external double rotacaoY;
  @Float()
  external double rotacaoZ;

  @Int32()
  external int usarRotacao;

  @Float()
  external double cimaX;
  @Float()
  external double cimaY;
  @Float()
  external double cimaZ;

  @Float()
  external double fovGraus;
  @Float()
  external double perto;
  @Float()
  external double longe;

  @Int32()
  external int ortografica;

  @Float()
  external double alturaOrtografica;
}

final class _Material3DC extends Struct {
  @Int32()
  external int ligado;

  @Uint8()
  external int corBaseR;
  @Uint8()
  external int corBaseG;
  @Uint8()
  external int corBaseB;
  @Uint8()
  external int corBaseA;

  @Float()
  external double metalico;
  @Float()
  external double rugosidade;
  @Float()
  external double forcaEmissiva;

  @Uint8()
  external int emissivoR;
  @Uint8()
  external int emissivoG;
  @Uint8()
  external int emissivoB;
  @Uint8()
  external int emissivoA;

  @Int32()
  external int modo;
  @Int32()
  external int faceDupla;

  @Float()
  external double alfaCorte;

  @Int32()
  external int semTexturaDeCor;
}

/// A CAMADA COM O MATERIAL ACHATADO DENTRO — a ordem dos campos e a ordem
/// da memoria do `Aurea3DCamada` do C++, e nao uma escolha.
final class _Camada3DC extends Struct {
  @Uint64()
  external int alca;
  @Int32()
  external int modelo;
  @Int32()
  external int animacao;
  @Double()
  external double tempoDaAnimacao;
  @Int32()
  external int visivel;

  @Float()
  external double posicaoX;
  @Float()
  external double posicaoY;
  @Float()
  external double posicaoZ;

  @Float()
  external double rotacaoX;
  @Float()
  external double rotacaoY;
  @Float()
  external double rotacaoZ;

  @Float()
  external double escalaX;
  @Float()
  external double escalaY;
  @Float()
  external double escalaZ;

  @Float()
  external double ancoraX;
  @Float()
  external double ancoraY;
  @Float()
  external double ancoraZ;

  @Float()
  external double opacidade;

  @Uint8()
  external int corR;
  @Uint8()
  external int corG;
  @Uint8()
  external int corB;
  @Uint8()
  external int corA;

  // ---------------------------------------------- o material, achatado
  @Int32()
  external int materialLigado;

  @Uint8()
  external int materialCorBaseR;
  @Uint8()
  external int materialCorBaseG;
  @Uint8()
  external int materialCorBaseB;
  @Uint8()
  external int materialCorBaseA;

  @Float()
  external double materialMetalico;
  @Float()
  external double materialRugosidade;
  @Float()
  external double materialForcaEmissiva;

  @Uint8()
  external int materialEmissivoR;
  @Uint8()
  external int materialEmissivoG;
  @Uint8()
  external int materialEmissivoB;
  @Uint8()
  external int materialEmissivoA;

  @Int32()
  external int materialModo;
  @Int32()
  external int materialFaceDupla;

  @Float()
  external double materialAlfaCorte;

  @Int32()
  external int materialSemTexturaDeCor;

  @Float()
  external double camadaZ;
}

final class _Luz3DC extends Struct {
  @Int32()
  external int tipo;

  @Float()
  external double posicaoX;
  @Float()
  external double posicaoY;
  @Float()
  external double posicaoZ;

  @Float()
  external double direcaoX;
  @Float()
  external double direcaoY;
  @Float()
  external double direcaoZ;

  @Float()
  external double corR;
  @Float()
  external double corG;
  @Float()
  external double corB;

  @Float()
  external double intensidade;
  @Float()
  external double alcance;
  @Float()
  external double anguloInternoGraus;
  @Float()
  external double anguloExternoGraus;

  @Int32()
  external int ligada;
}

/// A CENA COM A CAMERA ACHATADA DENTRO.
final class _Cena3DC extends Struct {
  external Pointer<_Camada3DC> camadas;
  @Uint32()
  external int quantidadeDeCamadas;
  @Uint32()
  external int quantidadeDeLuzes;
  external Pointer<_Luz3DC> luzes;

  /// 0 desligada, 1 baixa, 2 media, 3 alta.
  @Int32()
  external int sombra;
  @Uint32()
  external int amostras;
  @Uint32()
  external int largura;
  @Uint32()
  external int altura;

  @Float()
  external double ambienteR;
  @Float()
  external double ambienteG;
  @Float()
  external double ambienteB;

  // ------------------------------------------------ a camera, achatada
  @Float()
  external double cameraPosicaoX;
  @Float()
  external double cameraPosicaoY;
  @Float()
  external double cameraPosicaoZ;

  @Float()
  external double cameraAlvoX;
  @Float()
  external double cameraAlvoY;
  @Float()
  external double cameraAlvoZ;

  @Float()
  external double cameraRotacaoX;
  @Float()
  external double cameraRotacaoY;
  @Float()
  external double cameraRotacaoZ;

  @Int32()
  external int cameraUsarRotacao;

  @Float()
  external double cameraCimaX;
  @Float()
  external double cameraCimaY;
  @Float()
  external double cameraCimaZ;

  @Float()
  external double cameraFovGraus;
  @Float()
  external double cameraPerto;
  @Float()
  external double cameraLonge;

  @Int32()
  external int cameraOrtografica;

  @Float()
  external double cameraAlturaOrtografica;

  @Uint32()
  external int reserva;

  // ------------------------------------------------- o ambiente com direcao
  //
  // O AMBIENTE PLANO NAO FAZ METAL. Um metal nao tem difusa: ele responde
  // inteiro pelo que reflete, e refletindo uma cor unica ele vira uma cor
  // chapada — o ouro sai como um bronze fosco. O ceu e o chao fazem o que a
  // foto faz: o que aponta para cima pega o ceu, o que aponta para baixo
  // pega o chao, e o olho le volume e brilho onde antes havia um adesivo.
  //
  // As cores vao em sRGB (0..1) e o motor as leva ao linear, que e o mesmo
  // caminho da cor do painel.
  @Float()
  external double ceuR;
  @Float()
  external double ceuG;
  @Float()
  external double ceuB;

  /// Quanto do ambiente a superficie devolve no reflexo espelhado.
  @Float()
  external double reflexoDoAmbiente;

  @Float()
  external double chaoR;
  @Float()
  external double chaoG;
  @Float()
  external double chaoB;
  @Float()
  external double chaoReserva;
}

/// A MALHA CRUA. CINCO PONTEIROS E OS CAMPOS DO MATERIAL.
///
/// Os ponteiros apontam para buffers que valem ate o retorno da chamada —
/// a copia acontece dentro do motor. Eles sao preenchidos por
/// [Motor3D.criarModelo], e nao a mao.
final class _MalhaCrua3DC extends Struct {
  external Pointer<Float> posicoes;
  external Pointer<Float> normais;
  external Pointer<Float> uvs;
  external Pointer<Uint8> cores;
  external Pointer<Uint32> indices;

  @Uint32()
  external int quantidadeDeVertices;
  @Uint32()
  external int quantidadeDeIndices;

  @Int32()
  external int modo;
  @Float()
  external double alfaCorte;
  @Int32()
  external int faceDupla;

  @Uint8()
  external int corBaseR;
  @Uint8()
  external int corBaseG;
  @Uint8()
  external int corBaseB;
  @Uint8()
  external int corBaseA;

  @Float()
  external double metalico;
  @Float()
  external double rugosidade;
  @Float()
  external double forcaEmissiva;

  @Uint8()
  external int emissivoR;
  @Uint8()
  external int emissivoG;
  @Uint8()
  external int emissivoB;
  @Uint8()
  external int emissivoA;
}

final class _Opcoes3DC extends Struct {
  @Uint64()
  external int tetoDeBytesDoArquivo;
  @Int32()
  external int semAnimacao;
  @Float()
  external double escala;
  @Int32()
  external int texturaNeutraQuandoFaltar;
}

final class _Relato3DC extends Struct {
  @Uint64()
  external int bytesDoArquivo;
  @Uint32()
  external int malhas;
  @Uint32()
  external int materiais;
  @Uint32()
  external int texturas;
  @Uint32()
  external int nos;
  @Uint32()
  external int ossos;
  @Uint32()
  external int animacoes;
  @Uint32()
  external int triangulos;
  @Uint32()
  external int vertices;
  @Uint64()
  external int bytesEmMemoria;
  @Uint32()
  external int avisos;
  @Uint32()
  external int reserva;
}

final class _Ficha3DC extends Struct {
  @Uint32()
  external int malhas;
  @Uint32()
  external int materiais;
  @Uint32()
  external int texturas;
  @Uint32()
  external int nos;
  @Uint32()
  external int ossos;
  @Uint32()
  external int animacoes;
  @Uint32()
  external int triangulos;
  @Uint32()
  external int vertices;
  @Uint64()
  external int bytesDeMalha;
  @Uint64()
  external int bytesDeTextura;

  @Float()
  external double limiteMinX;
  @Float()
  external double limiteMinY;
  @Float()
  external double limiteMinZ;
  @Float()
  external double limiteMaxX;
  @Float()
  external double limiteMaxY;
  @Float()
  external double limiteMaxZ;

  @Int32()
  external int naGpu;
  @Uint32()
  external int reserva;
}

// ---------------------------------------------------------- as ligacoes

@Native<Int32 Function()>(symbol: 'aurea_render_3d_versao', isLeaf: true)
external int _tresdVersao();

@Native<Uint32 Function(Uint32)>(symbol: 'aurea_render_3d_tamanho', isLeaf: true)
external int _tresdTamanho(int qual);

@Native<Int32 Function()>(symbol: 'aurea_render_3d_preparar')
external int _tresdPreparar();

@Native<Int32 Function()>(symbol: 'aurea_render_3d_pronto', isLeaf: true)
external int _tresdPronto();

@Native<Pointer<Utf8> Function()>(symbol: 'aurea_render_3d_motivo', isLeaf: true)
external Pointer<Utf8> _tresdMotivo();

@Native<Pointer<Utf8> Function()>(
  symbol: 'aurea_render_3d_backend',
  isLeaf: true,
)
external Pointer<Utf8> _tresdBackend();

@Native<Pointer<Utf8> Function()>(
  symbol: 'aurea_render_3d_ultimo_erro',
  isLeaf: true,
)
external Pointer<Utf8> _tresdUltimoErro();

@Native<
  Pointer<Void> Function(
    Pointer<Utf8>,
    Pointer<_Opcoes3DC>,
    Pointer<_Relato3DC>
  )
>(symbol: 'aurea_render_3d_ler_arquivo')
external Pointer<Void> _tresdLerArquivo(
  Pointer<Utf8> caminho,
  Pointer<_Opcoes3DC> opcoes,
  Pointer<_Relato3DC> relato,
);

@Native<
  Pointer<Void> Function(
    Pointer<Uint8>,
    UintPtr,
    Pointer<Utf8>,
    Pointer<_Opcoes3DC>,
    Pointer<_Relato3DC>
  )
>(symbol: 'aurea_render_3d_ler_memoria')
external Pointer<Void> _tresdLerMemoria(
  Pointer<Uint8> bytes,
  int tamanho,
  Pointer<Utf8> extensao,
  Pointer<_Opcoes3DC> opcoes,
  Pointer<_Relato3DC> relato,
);

@Native<Int64 Function(Pointer<Void>, Pointer<_Relato3DC>)>(
  symbol: 'aurea_render_3d_adotar',
)
external int _tresdAdotar(Pointer<Void> carga, Pointer<_Relato3DC> relato);

@Native<Void Function(Pointer<Void>)>(symbol: 'aurea_render_3d_descartar')
external void _tresdDescartar(Pointer<Void> carga);

@Native<Uint32 Function(Pointer<Utf8>, Uint32)>(
  symbol: 'aurea_render_3d_ultimo_aviso',
  isLeaf: true,
)
external int _tresdUltimoAviso(Pointer<Utf8> saida, int capacidade);

@Native<Int64 Function(Pointer<Utf8>, Pointer<_Opcoes3DC>, Pointer<_Relato3DC>)>(
  symbol: 'aurea_render_3d_importar_arquivo',
)
external int _tresdImportarArquivo(
  Pointer<Utf8> caminho,
  Pointer<_Opcoes3DC> opcoes,
  Pointer<_Relato3DC> relato,
);

@Native<
  Int64 Function(
    Pointer<Uint8>,
    UintPtr,
    Pointer<Utf8>,
    Pointer<_Opcoes3DC>,
    Pointer<_Relato3DC>
  )
>(symbol: 'aurea_render_3d_importar_memoria')
external int _tresdImportarMemoria(
  Pointer<Uint8> bytes,
  int tamanho,
  Pointer<Utf8> extensao,
  Pointer<_Opcoes3DC> opcoes,
  Pointer<_Relato3DC> relato,
);

@Native<Int64 Function(Pointer<_MalhaCrua3DC>, Uint32, Pointer<_Relato3DC>)>(
  symbol: 'aurea_render_3d_criar_modelo',
)
external int _tresdCriarModelo(
  Pointer<_MalhaCrua3DC> malhas,
  int quantas,
  Pointer<_Relato3DC> relato,
);

@Native<Uint32 Function()>(
  symbol: 'aurea_render_3d_acervo_quantidade',
  isLeaf: true,
)
external int _tresdAcervoQuantidade();

@Native<Uint64 Function()>(symbol: 'aurea_render_3d_acervo_bytes', isLeaf: true)
external int _tresdAcervoBytes();

@Native<Void Function(Uint64)>(symbol: 'aurea_render_3d_acervo_definir_teto')
external void _tresdAcervoDefinirTeto(int bytes);

@Native<Uint64 Function()>(symbol: 'aurea_render_3d_acervo_teto', isLeaf: true)
external int _tresdAcervoTeto();

@Native<Void Function(Int32)>(symbol: 'aurea_render_3d_segurar', isLeaf: true)
external void _tresdSegurar(int alca);

@Native<Void Function(Int32)>(symbol: 'aurea_render_3d_soltar', isLeaf: true)
external void _tresdSoltar(int alca);

@Native<Uint32 Function()>(
  symbol: 'aurea_render_3d_limpar_desocupados',
  isLeaf: true,
)
external int _tresdLimparDesocupados();

@Native<Void Function()>(symbol: 'aurea_render_3d_acervo_limpar')
external void _tresdAcervoLimpar();

@Native<Int32 Function(Int32, Pointer<_Ficha3DC>)>(
  symbol: 'aurea_render_3d_ficha',
  isLeaf: true,
)
external int _tresdFicha(int alca, Pointer<_Ficha3DC> saida);

@Native<Double Function(Int32, Int32)>(
  symbol: 'aurea_render_3d_duracao_da_animacao',
  isLeaf: true,
)
external double _tresdDuracaoDaAnimacao(int alca, int animacao);

@Native<Uint64 Function()>(
  symbol: 'aurea_render_3d_memoria_de_gpu',
  isLeaf: true,
)
external int _tresdMemoriaDeGpu();

@Native<Int32 Function(Pointer<_Cena3DC>)>(symbol: 'aurea_render_3d_desenhar')
external int _tresdDesenhar(Pointer<_Cena3DC> cena);

@Native<Pointer<Uint8> Function()>(symbol: 'aurea_render_3d_pixels', isLeaf: true)
external Pointer<Uint8> _tresdPixels();

@Native<Uint32 Function()>(symbol: 'aurea_render_3d_largura', isLeaf: true)
external int _tresdLargura();

@Native<Uint32 Function()>(symbol: 'aurea_render_3d_altura', isLeaf: true)
external int _tresdAltura();

@Native<Uint32 Function()>(symbol: 'aurea_render_3d_aquecer')
external int _tresdAquecer();

@Native<Uint32 Function()>(symbol: 'aurea_render_3d_limpar_gpu')
external int _tresdLimparGpu();

@Native<Void Function()>(symbol: 'aurea_render_3d_liberar')
external void _tresdLiberar();

@Native<Uint32 Function(Pointer<Double>, Uint32)>(
  symbol: 'aurea_render_3d_estatisticas',
)
external int _tresdEstatisticas(Pointer<Double> saida, int capacidade);

@Native<Uint32 Function(Pointer<Uint32>, Uint32)>(
  symbol: 'aurea_render_3d_quadro_numeros',
  isLeaf: true,
)
external int _tresdQuadroNumeros(Pointer<Uint32> saida, int capacidade);

// ====================================================== o que voltou

/// O QUE A IMPORTACAO PRODUZIU. E a ficha que a camada mostra, e a prova
/// de que o arquivo foi lido de verdade — e nao de que o desenho saiu.
class RelatoDeImportacao3D {
  const RelatoDeImportacao3D({
    this.bytesDoArquivo = 0,
    this.malhas = 0,
    this.materiais = 0,
    this.texturas = 0,
    this.nos = 0,
    this.ossos = 0,
    this.animacoes = 0,
    this.triangulos = 0,
    this.vertices = 0,
    this.bytesEmMemoria = 0,
    this.avisos = 0,
    this.aviso = '',
  });

  final int bytesDoArquivo;
  final int malhas;
  final int materiais;
  final int texturas;
  final int nos;
  final int ossos;
  final int animacoes;
  final int triangulos;
  final int vertices;
  final int bytesEmMemoria;
  final int avisos;

  /// A MENSAGEM DO PRIMEIRO AVISO, e nao os 256 primeiros bytes de uma
  /// lista: ela nao cabe na struct e vem por uma chamada propria.
  final String aviso;

  @override
  String toString() =>
      'RelatoDeImportacao3D(malhas: $malhas, materiais: $materiais, '
      'texturas: $texturas, ossos: $ossos, animacoes: $animacoes, '
      'triangulos: $triangulos, vertices: $vertices, '
      'bytes: $bytesEmMemoria, avisos: $avisos)';
}

/// A FICHA DE UM MODELO NO ACERVO. [naGpu] e o cracha: ele responde se a
/// geometria ja subiu, e e o que explica a engasgada do primeiro quadro.
class FichaDeModelo3D {
  const FichaDeModelo3D({
    required this.alca,
    this.malhas = 0,
    this.materiais = 0,
    this.texturas = 0,
    this.nos = 0,
    this.ossos = 0,
    this.animacoes = 0,
    this.triangulos = 0,
    this.vertices = 0,
    this.bytesDeMalha = 0,
    this.bytesDeTextura = 0,
    this.limites = const <double>[0, 0, 0, 0, 0, 0],
    this.naGpu = false,
  });

  final int alca;
  final int malhas;
  final int materiais;
  final int texturas;
  final int nos;
  final int ossos;
  final int animacoes;
  final int triangulos;
  final int vertices;
  final int bytesDeMalha;
  final int bytesDeTextura;

  /// minimo xyz e maximo xyz, na caixa do MODELO (antes de qualquer
  /// transformacao de camada).
  final List<double> limites;
  final bool naGpu;

  bool get vazio => malhas == 0;
  int get bytes => bytesDeMalha + bytesDeTextura;

  @override
  String toString() =>
      'FichaDeModelo3D(alca: $alca, malhas: $malhas, materiais: $materiais, '
      'texturas: $texturas, ossos: $ossos, animacoes: $animacoes, '
      'triangulos: $triangulos, vertices: $vertices, naGpu: $naGpu)';
}

/// OS NUMEROS DO ULTIMO QUADRO 3D.
class Estatisticas3D {
  const Estatisticas3D({
    this.desenhos = 0,
    this.desenhosEsqueleticos = 0,
    this.triangulos = 0,
    this.instancias = 0,
    this.malhasNaGpu = 0,
    this.texturasNaGpu = 0,
    this.pipelines = 0,
    this.quadros = 0,
    this.ultimoDesenhoMs = 0,
    this.bytesDeGpu = 0,
    this.ladoDaSombra = 0,
    this.sombraLigada = false,
    this.antisserrilhado = false,
  });

  final int desenhos;
  final int desenhosEsqueleticos;
  final int triangulos;
  final int instancias;
  final int malhasNaGpu;
  final int texturasNaGpu;
  final int pipelines;
  final int quadros;
  final double ultimoDesenhoMs;
  final int bytesDeGpu;
  final int ladoDaSombra;
  final bool sombraLigada;
  final bool antisserrilhado;

  @override
  String toString() =>
      'Estatisticas3D(desenhos: $desenhos, triangulos: $triangulos, '
      'malhasNaGpu: $malhasNaGpu, texturasNaGpu: $texturasNaGpu, '
      'ultimoDesenhoMs: ${ultimoDesenhoMs.toStringAsFixed(2)}, '
      'bytesDeGpu: $bytesDeGpu, sombra: $sombraLigada)';
}

/// OS NUMEROS DO ULTIMO QUADRO AVALIADO. Eles respondem "por que o modelo
/// sumiu": tres camadas, uma desenhada, uma fora do campo, uma sem modelo.
class NumerosDoQuadro3D {
  const NumerosDoQuadro3D({
    this.camadas = 0,
    this.desenhadas = 0,
    this.foraDoCampo = 0,
    this.semModelo = 0,
    this.triangulos = 0,
    this.vertices = 0,
    this.ossos = 0,
    this.luzes = 0,
  });

  final int camadas;
  final int desenhadas;
  final int foraDoCampo;
  final int semModelo;
  final int triangulos;
  final int vertices;
  final int ossos;
  final int luzes;

  @override
  String toString() =>
      'NumerosDoQuadro3D(camadas: $camadas, desenhadas: $desenhadas, '
      'foraDoCampo: $foraDoCampo, semModelo: $semModelo, '
      'triangulos: $triangulos, ossos: $ossos, luzes: $luzes)';
}

// ============================================== o que se manda desenhar

/// UMA COR EM RGBA, NA ORDEM DOS BYTES.
///
/// O `Color` do Flutter e ARGB e o motor guarda RGBA. A conversao acontece
/// no [Ponte3D], num lugar so — uma troca de canais e o tipo de erro que
/// so aparece numa captura de tela.
class Cor3D {
  const Cor3D(this.r, this.g, this.b, [this.a = 255]);
  final int r;
  final int g;
  final int b;
  final int a;

  @override
  String toString() => 'Cor3D($r, $g, $b, $a)';
}

/// A SOBRESCRITA DE MATERIAL DA CAMADA.
///
/// `-1` em [metalico], [rugosidade], [forcaEmissiva], [alfaCorte] e [modo]
/// quer dizer "o que o modelo traz" — e nao zero, que e uma escolha
/// artistica legitima (metalico 0 e um plastico). Sem essa distincao,
/// mexer num controle apagaria o material do arquivo.
class MaterialDaCamada3D {
  bool ligado = false;
  Cor3D corBase = Cor3D(255, 255, 255, 255);
  double metalico = -1;
  double rugosidade = -1;
  double forcaEmissiva = -1;
  Cor3D emissivo = Cor3D(0, 0, 0, 255);

  /// -1 = do modelo; 0 opaco, 1 mascarado, 2 transparente.
  int modo = -1;
  bool faceDupla = false;
  double alfaCorte = -1;

  /// Apaga a textura de cor sem apagar o material.
  bool semTexturaDeCor = false;

  void copiarDe(MaterialDaCamada3D outro) {
    ligado = outro.ligado;
    corBase = Cor3D(outro.corBase.r, outro.corBase.g, outro.corBase.b,
        outro.corBase.a);
    metalico = outro.metalico;
    rugosidade = outro.rugosidade;
    forcaEmissiva = outro.forcaEmissiva;
    emissivo =
        Cor3D(outro.emissivo.r, outro.emissivo.g, outro.emissivo.b, outro.emissivo.a);
    modo = outro.modo;
    faceDupla = outro.faceDupla;
    alfaCorte = outro.alfaCorte;
    semTexturaDeCor = outro.semTexturaDeCor;
  }
}

/// UMA MALHA QUE NAO VEIO DE ARQUIVO.
///
/// CINCO VETORES, e todos no formato que a GPU ja quer. [normais] em nulo
/// faz o motor calcular a normal PLANA — que e o certo para um cubo e o
/// errado para uma esfera, entao quem tem a normal manda.
///
/// O MATERIAL E POR MALHA, e nao por objeto: uma peca com dois materiais
/// sao duas entradas nesta lista, cada uma com o seu. E o que um
/// `DrawIndexed` desenha.
class MalhaCrua3D {
  MalhaCrua3D({
    required this.posicoes,
    required this.indices,
    this.normais,
    this.uvs,
    this.cores,
    this.corBase = const Cor3D(255, 255, 255, 255),
    this.metalico = 1,
    this.rugosidade = 1,
    this.forcaEmissiva = 1,
    this.emissivo = const Cor3D(0, 0, 0, 255),
    this.modo = 0,
    this.alfaCorte = 0.5,
    this.faceDupla = false,
    int? quantidadeDeVertices,
    int? quantidadeDeIndices,
  }) : quantidadeDeVertices = quantidadeDeVertices ?? posicoes.length ~/ 3,
       quantidadeDeIndices = quantidadeDeIndices ?? indices.length;

  /// 3 floats por vertice.
  final Float32List posicoes;
  /// 3 indices por triangulo.
  final Uint32List indices;
  /// 3 floats por vertice, ou nulo para a normal plana.
  final Float32List? normais;
  /// 2 floats por vertice.
  final Float32List? uvs;
  /// 4 bytes por vertice, RGBA.
  final Uint8List? cores;

  final int quantidadeDeVertices;
  final int quantidadeDeIndices;

  final Cor3D corBase;
  final double metalico;
  final double rugosidade;
  final double forcaEmissiva;
  final Cor3D emissivo;

  /// 0 opaco, 1 mascarado, 2 transparente.
  final int modo;
  final double alfaCorte;
  final bool faceDupla;
}

/// UMA CAMADA 3D DA TIMELINE, JA RESOLVIDA PELO AVALIADOR DO DART.
///
/// O QUE VEM DAQUI E O ESTADO, E NAO A ANIMACAO: [tempoDaAnimacao] ja
/// chegou resolvido — uma camada com Time Remap a 50% nao muda nada aqui,
/// porque quem desacelera e a timeline (§12).
class CamadaDeCena3D {
  /// A identificacao que o Dart usa. E o que liga esta camada a camada
  /// homonima da timeline — sem isso a selecao no palco nao teria como
  /// saber que objeto esta debaixo do dedo.
  int alca = 0;

  /// O indice do modelo no acervo, ou -1 quando ainda nao ha geometria. UMA
  /// CAMADA SEM MODELO NAO E UM ERRO: ela existe, aparece na timeline, e
  /// nao desenha nada enquanto o modelo nao chega (§26).
  int modelo = -1;

  int animacao = -1;
  double tempoDaAnimacao = 0;
  bool visivel = true;

  double posicaoX = 0, posicaoY = 0, posicaoZ = 0;
  double rotacaoX = 0, rotacaoY = 0, rotacaoZ = 0;
  double escalaX = 1, escalaY = 1, escalaZ = 1;

  /// O PIVO DENTRO DA CAIXA DO MODELO, em 0..1 por eixo. (0,5;0,5;0,5) e o
  /// centro.
  double ancoraX = 0.5, ancoraY = 0.5, ancoraZ = 0.5;

  double opacidade = 1;
  Cor3D cor = Cor3D(255, 255, 255, 255);
  MaterialDaCamada3D material = MaterialDaCamada3D();

  /// A ORDEM NA TIMELINE. Para os solidos quase nao importa (o teste de
  /// profundidade decide); para os transparentes e o que separa "o vidro
  /// na frente" de "o vidro atras".
  double camadaZ = 0;

  void copiarDe(CamadaDeCena3D outro) {
    alca = outro.alca;
    modelo = outro.modelo;
    animacao = outro.animacao;
    tempoDaAnimacao = outro.tempoDaAnimacao;
    visivel = outro.visivel;
    posicaoX = outro.posicaoX;
    posicaoY = outro.posicaoY;
    posicaoZ = outro.posicaoZ;
    rotacaoX = outro.rotacaoX;
    rotacaoY = outro.rotacaoY;
    rotacaoZ = outro.rotacaoZ;
    escalaX = outro.escalaX;
    escalaY = outro.escalaY;
    escalaZ = outro.escalaZ;
    ancoraX = outro.ancoraX;
    ancoraY = outro.ancoraY;
    ancoraZ = outro.ancoraZ;
    opacidade = outro.opacidade;
    cor = Cor3D(outro.cor.r, outro.cor.g, outro.cor.b, outro.cor.a);
    material.copiarDe(outro.material);
    camadaZ = outro.camadaZ;
  }
}

/// UMA LUZ. Os tres tipos que existem: direcional, pontual e holofote.
abstract final class TipoDeLuz3D {
  static const int direcional = 0;
  static const int pontual = 1;
  static const int holofote = 2;
}

class LuzDeCena3D {
  int tipo = TipoDeLuz3D.direcional;

  double posicaoX = 0, posicaoY = 3, posicaoZ = 0;

  /// Para a direcional e a holofote: para ONDE a luz aponta.
  double direcaoX = 0, direcaoY = -1, direcaoZ = 0;

  double corR = 1, corG = 1, corB = 1, intensidade = 1;

  /// Ate onde a pontual e a holofote alcancam. Zero desliga o limite.
  double alcance = 0;
  double anguloInternoGraus = 20;
  double anguloExternoGraus = 35;
  bool ligada = true;

  void copiarDe(LuzDeCena3D outro) {
    tipo = outro.tipo;
    posicaoX = outro.posicaoX;
    posicaoY = outro.posicaoY;
    posicaoZ = outro.posicaoZ;
    direcaoX = outro.direcaoX;
    direcaoY = outro.direcaoY;
    direcaoZ = outro.direcaoZ;
    corR = outro.corR;
    corG = outro.corG;
    corB = outro.corB;
    intensidade = outro.intensidade;
    alcance = outro.alcance;
    anguloInternoGraus = outro.anguloInternoGraus;
    anguloExternoGraus = outro.anguloExternoGraus;
    ligada = outro.ligada;
  }
}

/// A CAMERA DE COMPOSICAO. Nao e a camera do editor: a do editor orbita e
/// nao vai para a exportacao; esta vai, e por isso e uma camada da
/// timeline como qualquer outra (§8).
class CameraDeCena3D {
  double posicaoX = 0, posicaoY = 0, posicaoZ = 5;
  double alvoX = 0, alvoY = 0, alvoZ = 0;
  double rotacaoX = 0, rotacaoY = 0, rotacaoZ = 0;
  bool usarRotacao = false;
  double cimaX = 0, cimaY = 1, cimaZ = 0;
  double fovGraus = 45;
  double perto = 0.05;
  double longe = 500;
  bool ortografica = false;
  double alturaOrtografica = 4;

  void copiarDe(CameraDeCena3D outra) {
    posicaoX = outra.posicaoX;
    posicaoY = outra.posicaoY;
    posicaoZ = outra.posicaoZ;
    alvoX = outra.alvoX;
    alvoY = outra.alvoY;
    alvoZ = outra.alvoZ;
    rotacaoX = outra.rotacaoX;
    rotacaoY = outra.rotacaoY;
    rotacaoZ = outra.rotacaoZ;
    usarRotacao = outra.usarRotacao;
    cimaX = outra.cimaX;
    cimaY = outra.cimaY;
    cimaZ = outra.cimaZ;
    fovGraus = outra.fovGraus;
    perto = outra.perto;
    longe = outra.longe;
    ortografica = outra.ortografica;
    alturaOrtografica = outra.alturaOrtografica;
  }
}

/// A CENA DE UM QUADRO. Ela e o que se manda desenhar: as camadas, as
/// luzes, a camera, a luz ambiente, a sombra e o tamanho do alvo.
class Cena3D {
  final List<CamadaDeCena3D> camadas = <CamadaDeCena3D>[];
  final List<LuzDeCena3D> luzes = <LuzDeCena3D>[];
  CameraDeCena3D camera = CameraDeCena3D();

  /// A LUZ QUE VEM DE TODO LADO. Sem ela o lado escuro de um objeto fica
  /// preto absoluto, e o modelo parece recortado em papel.
  double ambienteR = 0.18, ambienteG = 0.18, ambienteB = 0.20;

  /// O AMBIENTE COM DIRECAO: a cor de cima e a cor de baixo, em sRGB.
  /// Neutro — ceu e chao brancos — e o mesmo ambiente plano de antes.
  double ceuR = 1.0, ceuG = 1.0, ceuB = 1.0;
  double chaoR = 1.0, chaoG = 1.0, chaoB = 1.0;

  /// Quanto do ambiente volta no reflexo espelhado. E o que separa um metal
  /// de um plastico: em 1 o metal espelha o ambiente inteiro.
  double reflexoDoAmbiente = 0.0;

  /// 0 desligada, 1 baixa, 2 media, 3 alta.
  int sombra = 0;

  /// Amostras por eixo do antisserrilhado. 1 = sem. So 2, 4 e 8 valem.
  int amostras = 1;

  /// O TAMANHO DO ALVO, em pixels — o MESMO da composicao. A 3D nao tem
  /// resolucao propria, porque ela e uma camada e nao uma janela.
  int largura = 0;
  int altura = 0;

  void limpar() {
    camadas.clear();
    luzes.clear();
  }

  @override
  String toString() =>
      'Cena3D(${camadas.length} camadas, ${luzes.length} luzes, '
      '${largura}x$altura)';
}

// ============================================================ o motor

/// A PORTA DO MOTOR 3D. Tudo o que e estado do processo mora do outro
/// lado; aqui so ha perguntas.
abstract final class Motor3D {
  /// A PORTA 3D EXISTE NESTE BINARIO?
  ///
  /// O MOTOR 3D NAO E COMPILADO NO PC, e isso e uma decisao e nao um
  /// esquecimento: o `hook/build.dart` so acrescenta o Diligent e o Assimp
  /// quando o alvo e Android ou Apple (`final tresD = android || apple`),
  /// porque o Diligent deste repositorio esta configurado para Vulkan e
  /// Metal. No PC o simbolo nao existe, e o `@Native` levanta na PRIMEIRA
  /// chamada. Por isso toda pergunta daqui passa por esta sonda: um teste
  /// no PC nao pode explodir por causa de um motor que ali nao roda.
  static bool get disponivel {
    try {
      return _tresdVersao() == versaoEsperadaDaPorta3D;
    } catch (e) {
      debugPrint('AUREA: motor 3D indisponivel ($e).');
      return false;
    }
  }

  /// A VERSAO QUE A BIBLIOTECA COMPILADA RESPONDE, ou -1 quando nao ha
  /// biblioteca.
  static int get versao {
    try {
      return _tresdVersao();
    } catch (_) {
      return -1;
    }
  }

  /// A BIBLIOTECA E A DESTE ARQUIVO SAO A MESMA? Um descompasso entre elas
  /// e a causa mais comum de "leu o campo errado" depois de uma
  /// atualizacao parcial.
  static bool get portaConfere => disponivel;

  /// O TAMANHO DE CADA STRUCT DA ABI, para o teste conferir contra o
  /// `sizeOf` do lado Dart.
  static List<int> get tamanhos {
    if (!disponivel) return const <int>[];
    return List<int>.generate(Tamanho3D.quantos, _tresdTamanho);
  }

  /// O TAMANHO QUE O LADO DART ACHA QUE CADA STRUCT TEM, na mesma ordem de
  /// [tamanhos]. Os dois tem de bater exatamente: uma struct mal declarada
  /// nao avisa, ela le o campo do vizinho e devolve um numero plausivel.
  static List<int> get tamanhosDoDart => <int>[
    sizeOf<_Camera3DC>(),
    sizeOf<_Material3DC>(),
    sizeOf<_Camada3DC>(),
    sizeOf<_Luz3DC>(),
    sizeOf<_Cena3DC>(),
    sizeOf<_Relato3DC>(),
    sizeOf<_Ficha3DC>(),
    sizeOf<_Opcoes3DC>(),
    sizeOf<_MalhaCrua3DC>(),
  ];

  /// ABRE O DISPOSITIVO. 1 subiu, 0 nao subiu — e [motivo] diz por que.
  static bool preparar() => _tresdPreparar() != 0;

  static bool get pronto => _tresdPronto() != 0;

  static String get motivo => _tresdMotivo().toDartString();
  static String get backend => _tresdBackend().toDartString();
  static String get ultimoErro => _tresdUltimoErro().toDartString();

  /// A MENSAGEM DO PRIMEIRO AVISO DA ULTIMA LEITURA.
  static String get ultimoAviso {
    final p = calloc<Uint8>(512);
    try {
      final n = _tresdUltimoAviso(p.cast<Utf8>(), 512);
      if (n == 0) return '';
      return p.cast<Utf8>().toDartString();
    } finally {
      calloc.free(p);
    }
  }

  // ------------------------------------------------------- a leitura

  /// LE UM ARQUIVO E DEVOLVE A CARGA PENDENTE. E ESTE o passo que roda na
  /// thread de fundo: o Assimp gasta segundos aqui, e aqui nao ha nada
  /// compartilhado para travar.
  static Carga3D? lerArquivo(
    String caminho, {
    bool semAnimacao = false,
    double escala = 1,
    int? tetoDeBytesDoArquivo,
    bool texturaNeutraQuandoFaltar = true,
  }) {
    final pCaminho = caminho.toNativeUtf8();
    final pOpcoes = calloc<_Opcoes3DC>();
    final pRelato = calloc<_Relato3DC>();
    try {
      _escreverOpcoes(
        pOpcoes.ref,
        semAnimacao: semAnimacao,
        escala: escala,
        tetoDeBytesDoArquivo: tetoDeBytesDoArquivo,
        texturaNeutraQuandoFaltar: texturaNeutraQuandoFaltar,
      );
      final ponteiro = _tresdLerArquivo(pCaminho, pOpcoes, pRelato);
      if (ponteiro == nullptr) return null;
      return Carga3D._(ponteiro, _lerRelato(pRelato.ref));
    } finally {
      calloc.free(pCaminho);
      calloc.free(pOpcoes);
      calloc.free(pRelato);
    }
  }

  /// A MESMA COISA com os bytes ja na mao. [extensao] decide o importador
  /// (".glb", ".gltf", ".fbx", ".obj") e pode vir com ou sem o ponto.
  static Carga3D? lerMemoria(
    Uint8List bytes,
    String extensao, {
    bool semAnimacao = false,
    double escala = 1,
    bool texturaNeutraQuandoFaltar = true,
  }) {
    if (bytes.isEmpty) return null;
    final pBytes = calloc<Uint8>(bytes.length);
    final pExtensao = extensao.toNativeUtf8();
    final pOpcoes = calloc<_Opcoes3DC>();
    final pRelato = calloc<_Relato3DC>();
    try {
      pBytes.asTypedList(bytes.length).setAll(0, bytes);
      _escreverOpcoes(
        pOpcoes.ref,
        semAnimacao: semAnimacao,
        escala: escala,
        texturaNeutraQuandoFaltar: texturaNeutraQuandoFaltar,
      );
      final ponteiro = _tresdLerMemoria(
        pBytes,
        bytes.length,
        pExtensao,
        pOpcoes,
        pRelato,
      );
      if (ponteiro == nullptr) return null;
      return Carga3D._(ponteiro, _lerRelato(pRelato.ref));
    } finally {
      calloc.free(pBytes);
      calloc.free(pExtensao);
      calloc.free(pOpcoes);
      calloc.free(pRelato);
    }
  }

  static void _escreverOpcoes(
    _Opcoes3DC o, {
    required bool semAnimacao,
    required double escala,
    required bool texturaNeutraQuandoFaltar,
    int? tetoDeBytesDoArquivo,
  }) {
    o.tetoDeBytesDoArquivo = tetoDeBytesDoArquivo ?? 0;
    o.semAnimacao = semAnimacao ? 1 : 0;
    o.escala = escala;
    o.texturaNeutraQuandoFaltar = texturaNeutraQuandoFaltar ? 1 : 0;
  }

  /// MOVE A CARGA PARA O ACERVO E DEVOLVE A ALCA. A carga deixa de
  /// existir — nao se adota duas vezes.
  ///
  /// DEVOLVE O NEGATIVO DO ERRO quando o acervo recusou por orcamento, e
  /// nesse caso A CARGA NAO E CONSUMIDA: quem chamou pode soltar um modelo
  /// e tentar de novo.
  static ResultadoDeAdocao3D adotar(Carga3D carga) {
    final pRelato = calloc<_Relato3DC>();
    try {
      final r = _tresdAdotar(carga._ponteiro, pRelato);
      return ResultadoDeAdocao3D(
        alca: r > 0 ? r : 0,
        erro: r < 0 ? -r : (r == 0 ? 1 : 0),
        relato: _lerRelato(pRelato.ref),
      );
    } finally {
      calloc.free(pRelato);
      carga._esquecer();
    }
  }

  /// O ATALHO: LER E ADOTAR NA MESMA CHAMADA. Ele TRAVA pelo tempo da
  /// leitura, e por isso nao serve para a interface — serve para a bancada
  /// e para o teste.
  static ResultadoDeAdocao3D importarArquivo(
    String caminho, {
    bool semAnimacao = false,
    double escala = 1,
    bool texturaNeutraQuandoFaltar = true,
  }) {
    final pCaminho = caminho.toNativeUtf8();
    final pOpcoes = calloc<_Opcoes3DC>();
    final pRelato = calloc<_Relato3DC>();
    try {
      _escreverOpcoes(
        pOpcoes.ref,
        semAnimacao: semAnimacao,
        escala: escala,
        texturaNeutraQuandoFaltar: texturaNeutraQuandoFaltar,
      );
      final r = _tresdImportarArquivo(pCaminho, pOpcoes, pRelato);
      return ResultadoDeAdocao3D(
        alca: r > 0 ? r : 0,
        erro: r < 0 ? -r : (r == 0 ? 1 : 0),
        relato: _lerRelato(pRelato.ref),
      );
    } finally {
      calloc.free(pCaminho);
      calloc.free(pOpcoes);
      calloc.free(pRelato);
    }
  }

  static ResultadoDeAdocao3D importarMemoria(
    Uint8List bytes,
    String extensao, {
    bool semAnimacao = false,
    double escala = 1,
    bool texturaNeutraQuandoFaltar = true,
  }) {
    if (bytes.isEmpty) {
      return const ResultadoDeAdocao3D(alca: 0, erro: 6, relato: RelatoDeImportacao3D());
    }
    final pBytes = calloc<Uint8>(bytes.length);
    final pExtensao = extensao.toNativeUtf8();
    final pOpcoes = calloc<_Opcoes3DC>();
    final pRelato = calloc<_Relato3DC>();
    try {
      pBytes.asTypedList(bytes.length).setAll(0, bytes);
      _escreverOpcoes(
        pOpcoes.ref,
        semAnimacao: semAnimacao,
        escala: escala,
        texturaNeutraQuandoFaltar: texturaNeutraQuandoFaltar,
      );
      final r = _tresdImportarMemoria(
        pBytes,
        bytes.length,
        pExtensao,
        pOpcoes,
        pRelato,
      );
      return ResultadoDeAdocao3D(
        alca: r > 0 ? r : 0,
        erro: r < 0 ? -r : (r == 0 ? 1 : 0),
        relato: _lerRelato(pRelato.ref),
      );
    } finally {
      calloc.free(pBytes);
      calloc.free(pExtensao);
      calloc.free(pOpcoes);
      calloc.free(pRelato);
    }
  }

  /// GUARDA GEOMETRIA QUE NAO VEIO DE ARQUIVO.
  ///
  /// ESTE E O CAMINHO DOS SOLIDOS NATIVOS — cubo, esfera, toro, capsula,
  /// cilindro —, do texto 3D extrudado, das formas parametricas e de tudo
  /// que o importador Dart ja le. Sem ele, o motor novo so desenharia o que
  /// o Assimp leu, e o 3D que ja existe no app continuaria preso ao pintor
  /// de CPU: duas metades de motor, que e exatamente o que nao pode haver.
  ///
  /// UMA CHAMADA POR OBJETO, E NAO POR VERTICE: uma malha com dois materiais
  /// e duas entradas na lista. Os buffers daqui podem morrer quando a
  /// funcao retorna — a copia acontece dentro do motor.
  static ResultadoDeAdocao3D criarModelo(List<MalhaCrua3D> malhas) {
    if (malhas.isEmpty) {
      return const ResultadoDeAdocao3D(
        alca: 0,
        erro: 6,
        relato: RelatoDeImportacao3D(),
      );
    }
    final pMalhas = calloc<_MalhaCrua3DC>(malhas.length);
    final pRelato = calloc<_Relato3DC>();
    // O QUE FOI ALOCADO PRECISA SER LIVRE NA SAIDA, INCLUSIVE NO ERRO: sao
    // cinco vetores por malha, e um `return` no meio sem passar pelo
    // `finally` vazaria todos eles.
    final alocados = <Pointer<Uint8>>[];
    try {
      for (var i = 0; i < malhas.length; i++) {
        final m = malhas[i];
        final d = (pMalhas + i).ref;

        if (m.posicoes.length < m.quantidadeDeVertices * 3 ||
            m.indices.length < m.quantidadeDeIndices) {
          return const ResultadoDeAdocao3D(
            alca: 0,
            erro: 6,
            relato: RelatoDeImportacao3D(),
          );
        }

        d.posicoes = _emprestar(alocados, m.posicoes).cast<Float>();
        d.normais = m.normais == null
            ? nullptr
            : _emprestar(alocados, m.normais!).cast<Float>();
        d.uvs = m.uvs == null
            ? nullptr
            : _emprestar(alocados, m.uvs!).cast<Float>();
        d.cores = m.cores == null
            ? nullptr
            : _emprestar(alocados, m.cores!).cast<Uint8>();
        d.indices = _emprestar(alocados, m.indices).cast<Uint32>();

        d.quantidadeDeVertices = m.quantidadeDeVertices;
        d.quantidadeDeIndices = m.quantidadeDeIndices;
        d.modo = m.modo;
        d.alfaCorte = m.alfaCorte;
        d.faceDupla = m.faceDupla ? 1 : 0;
        d.corBaseR = m.corBase.r;
        d.corBaseG = m.corBase.g;
        d.corBaseB = m.corBase.b;
        d.corBaseA = m.corBase.a;
        d.metalico = m.metalico;
        d.rugosidade = m.rugosidade;
        d.forcaEmissiva = m.forcaEmissiva;
        d.emissivoR = m.emissivo.r;
        d.emissivoG = m.emissivo.g;
        d.emissivoB = m.emissivo.b;
        d.emissivoA = m.emissivo.a;
      }

      final r = _tresdCriarModelo(pMalhas, malhas.length, pRelato);
      return ResultadoDeAdocao3D(
        alca: r > 0 ? r : 0,
        erro: r < 0 ? -r : (r == 0 ? 1 : 0),
        relato: _lerRelato(pRelato.ref),
      );
    } finally {
      for (final p in alocados) {
        calloc.free(p);
      }
      calloc.free(pMalhas);
      calloc.free(pRelato);
    }
  }

  /// COPIA UM VETOR DO DART PARA A MEMORIA DO NATIVO E O ANOTA PARA LIBERAR.
  ///
  /// `calloc<Uint8>` E NAO O TIPO DO VETOR: um `Float32List` tem quatro
  /// bytes por elemento e `Pointer<Float>` puro so existe de quatro em
  /// quatro — alocar pelo tipo certo exigiria uma sobrecarga de `calloc`
  /// por tipo. Um bloco de bytes, com a conversao de ponteiro na hora de
  /// escrever, serve para os cinco formatos.
  static Pointer<Uint8> _emprestar(
    List<Pointer<Uint8>> alocados,
    TypedData dados,
  ) {
    final bytes = dados.buffer.asUint8List(
      dados.offsetInBytes,
      dados.lengthInBytes,
    );
    final p = calloc<Uint8>(bytes.length);
    p.asTypedList(bytes.length).setAll(0, bytes);
    alocados.add(p);
    return p;
  }

  static RelatoDeImportacao3D _lerRelato(_Relato3DC r) => RelatoDeImportacao3D(
    bytesDoArquivo: r.bytesDoArquivo,
    malhas: r.malhas,
    materiais: r.materiais,
    texturas: r.texturas,
    nos: r.nos,
    ossos: r.ossos,
    animacoes: r.animacoes,
    triangulos: r.triangulos,
    vertices: r.vertices,
    bytesEmMemoria: r.bytesEmMemoria,
    avisos: r.avisos,
    aviso: r.avisos > 0 ? ultimoAviso : '',
  );

  // -------------------------------------------------------- o acervo

  static int get modelosNoAcervo => _tresdAcervoQuantidade();
  static int get bytesNoAcervo => _tresdAcervoBytes();

  /// O TETO DE MEMORIA DO ACERVO. Estourar o teto nao recusa a importacao
  /// de um modelo SO — ele existe para impedir o segundo (§21).
  static int get tetoDoAcervo => _tresdAcervoTeto();
  static set tetoDoAcervo(int bytes) => _tresdAcervoDefinirTeto(bytes);

  /// SEGURA E SOLTA. Quem cria uma camada 3D chama [segurar]; quem a apaga
  /// chama [soltar]. A contagem e o que impede [limparDesocupados] de
  /// apagar um modelo em uso.
  static void segurar(int alca) => _tresdSegurar(alca);
  static void soltar(int alca) => _tresdSoltar(alca);
  static int limparDesocupados() => _tresdLimparDesocupados();
  static void limparAcervo() => _tresdAcervoLimpar();

  /// A FICHA DE UM MODELO, ou nulo quando a alca nao existe.
  static FichaDeModelo3D? ficha(int alca) {
    final p = calloc<_Ficha3DC>();
    try {
      if (_tresdFicha(alca, p) == 0) return null;
      final f = p.ref;
      return FichaDeModelo3D(
        alca: alca,
        malhas: f.malhas,
        materiais: f.materiais,
        texturas: f.texturas,
        nos: f.nos,
        ossos: f.ossos,
        animacoes: f.animacoes,
        triangulos: f.triangulos,
        vertices: f.vertices,
        bytesDeMalha: f.bytesDeMalha,
        bytesDeTextura: f.bytesDeTextura,
        limites: <double>[
          f.limiteMinX,
          f.limiteMinY,
          f.limiteMinZ,
          f.limiteMaxX,
          f.limiteMaxY,
          f.limiteMaxZ,
        ],
        naGpu: f.naGpu != 0,
      );
    } finally {
      calloc.free(p);
    }
  }

  /// A DURACAO DE UM CLIPE, em segundos. Zero quando o indice nao existe.
  static double duracaoDaAnimacao(int alca, int animacao) =>
      _tresdDuracaoDaAnimacao(alca, animacao);

  /// QUANTO DE MEMORIA DE GPU O APARELHO TEM, em bytes. Zero quando o
  /// aparelho nao informa — e ai quem decide e o teto do acervo.
  static int get memoriaDeGpu => _tresdMemoriaDeGpu();

  // -------------------------------------------------------- o desenho

  /// SOBE PARA A PLACA O QUE AINDA NAO SUBIU, sem desenhar. Chamado logo
  /// depois de adotar um modelo, para o primeiro quadro com ele nao pagar
  /// a subida.
  static int aquecer() => _tresdAquecer();

  /// ESQUECE O QUE NAO ESTA MAIS NO ACERVO, na GPU.
  static int limparGpu() => _tresdLimparGpu();

  /// ESQUECE TUDO. Chamado quando o app vai fechar ou trocar de projeto.
  static void liberar() => _tresdLiberar();

  static Estatisticas3D get estatisticas {
    final p = calloc<Double>(13);
    try {
      final n = _tresdEstatisticas(p, 13);
      if (n < 13) return const Estatisticas3D();
      final v = p.asTypedList(13);
      return Estatisticas3D(
        desenhos: v[0].toInt(),
        desenhosEsqueleticos: v[1].toInt(),
        triangulos: v[2].toInt(),
        instancias: v[3].toInt(),
        malhasNaGpu: v[4].toInt(),
        texturasNaGpu: v[5].toInt(),
        pipelines: v[6].toInt(),
        quadros: v[7].toInt(),
        ultimoDesenhoMs: v[8],
        bytesDeGpu: v[9].toInt(),
        ladoDaSombra: v[10].toInt(),
        sombraLigada: v[11] != 0,
        antisserrilhado: v[12] != 0,
      );
    } finally {
      calloc.free(p);
    }
  }

  static NumerosDoQuadro3D get numerosDoQuadro {
    final p = calloc<Uint32>(8);
    try {
      final n = _tresdQuadroNumeros(p, 8);
      if (n < 8) return const NumerosDoQuadro3D();
      final v = p.asTypedList(8);
      return NumerosDoQuadro3D(
        camadas: v[0],
        desenhadas: v[1],
        foraDoCampo: v[2],
        semModelo: v[3],
        triangulos: v[4],
        vertices: v[5],
        ossos: v[6],
        luzes: v[7],
      );
    } finally {
      calloc.free(p);
    }
  }
}

/// UMA CARGA PENDENTE. Ela pertence a UMA thread de cada vez — nasce na de
/// fundo e e consumida na principal — e por isso nao ha trava nenhuma.
class Carga3D {
  Carga3D._(this._ponteiro, this.relato);

  final Pointer<Void> _ponteiro;
  final RelatoDeImportacao3D relato;
  bool _consumida = false;

  bool get valida => !_consumida && _ponteiro != nullptr;

  /// O ENDERECO DA CARGA, COMO UM NUMERO, PARA ATRAVESSAR UM `Isolate`.
  ///
  /// Um `Pointer` NAO e enviado de um isolate para outro — a mensagem e
  /// recusada. Um `int` e. Esta e a unica forma de uma carga nascer na
  /// thread de fundo (onde o Assimp gasta segundos) e ser adotada na
  /// thread da interface (onde o acervo mora), que e o desenho dos dois
  /// tempos da importacao.
  ///
  /// E SEGURO porque a carga pertence a uma thread de cada vez: quem a
  /// cria para de usar antes de enviar o numero, e quem a recebe so a usa
  /// depois de a mensagem chegar. A mensagem do `Isolate` e a ordem.
  int get endereco => _ponteiro.address;

  /// A CARGA A PARTIR DO ENDERECO que veio de outro isolate. [relato] e o
  /// mesmo que a leitura devolveu — ele viaja como dado, nao como ponteiro.
  factory Carga3D.deEndereco(int endereco, RelatoDeImportacao3D relato) =>
      Carga3D._(Pointer<Void>.fromAddress(endereco), relato);

  /// JOGA FORA UMA CARGA QUE NAO VAI SER ADOTADA.
  void descartar() {
    if (_consumida) return;
    _consumida = true;
    _tresdDescartar(_ponteiro);
  }

  void _esquecer() => _consumida = true;
}

/// O RESULTADO DE ADOTAR. [erro] e o codigo do `Erro` do C++ (0 nenhum),
/// e um `orcamento_estourado` (3) quer dizer que a carga CONTINUA viva e
/// pode ser adotada depois de soltar alguma coisa.
class ResultadoDeAdocao3D {
  const ResultadoDeAdocao3D({
    required this.alca,
    required this.erro,
    required this.relato,
  });

  final int alca;
  final int erro;
  final RelatoDeImportacao3D relato;

  bool get deuCerto => alca > 0;

  /// O NOME DO ERRO, como o C++ o escreve.
  static const Map<int, String> nomes = <int, String>{
    0: 'nenhum',
    1: 'sem_memoria',
    2: 'sem_recurso',
    3: 'orcamento_estourado',
    4: 'capacidade',
    5: 'estado_invalido',
    6: 'argumento',
    7: 'ja_existe',
    8: 'nao_existe',
    9: 'nao_suportado',
    10: 'interno',
  };

  String get nomeDoErro => nomes[erro] ?? 'desconhecido';

  @override
  String toString() =>
      deuCerto ? 'ResultadoDeAdocao3D(alca: $alca)' : 'ResultadoDeAdocao3D($nomeDoErro)';
}

// ==================================================== a ponte do quadro

/// A PONTE DO QUADRO 3D: os buffers da cena, reservados uma vez e reusados.
///
/// POR QUE NAO ALOCAR POR QUADRO: um `calloc` de oitenta camadas por quadro
/// e memoria indo e vindo sessenta vezes por segundo no caminho do dedo.
/// Aqui os buffers crescem quando precisa e ficam — o mesmo desenho do
/// [LoteDeParticulas], pelo mesmo motivo.
class Ponte3D {
  Pointer<_Cena3DC>? _cena;
  Pointer<_Camada3DC>? _camadas;
  int _camadasReservadas = 0;
  Pointer<_Luz3DC>? _luzes;
  int _luzesReservadas = 0;

  bool _desenhou = false;
  bool _semGeometria = true;
  int _largura = 0;
  int _altura = 0;

  /// DESENHA A CENA. Falso quando o motor nao desenhou — e o resto do app
  /// segue, porque um quadro 3D que falha nao derruba o editor (§43).
  bool desenhar(Cena3D cena) {
    final pCena = _cena ??= calloc<_Cena3DC>();
    final n = cena.camadas.length;
    final m = cena.luzes.length;

    // O `== null` NO CRITÉRIO, E NAO SO O CRESCIMENTO: uma cena sem camada
    // nenhuma — que e o caso normal de um projeto sem 3D — precisa mandar
    // um ponteiro VALIDO e nao o nulo, porque o C++ recusa o nulo.
    if (_camadas == null || n > _camadasReservadas) {
      if (_camadas != null) calloc.free(_camadas!);
      _camadasReservadas = n < 32 ? 32 : n;
      _camadas = calloc<_Camada3DC>(_camadasReservadas);
    }
    if (_luzes == null || m > _luzesReservadas) {
      if (_luzes != null) calloc.free(_luzes!);
      _luzesReservadas = m < 8 ? 8 : m;
      _luzes = calloc<_Luz3DC>(_luzesReservadas);
    }

    // UM A UM PELO PONTEIRO, E NAO POR `asTypedList`: o `dart:ffi` so
    // define `asTypedList` para os tipos numericos — nao ha vista de
    // `Struct`, e `Pointer<Uint8>` sobre a memoria lida como byte nao
    // deixaria escrever campo nenhum.
    final pCamadas = _camadas!;
    final pLuzes = _luzes!;
    for (var i = 0; i < n; i++) {
      _escreverCamada((pCamadas + i).ref, cena.camadas[i]);
    }
    for (var i = 0; i < m; i++) {
      _escreverLuz((pLuzes + i).ref, cena.luzes[i]);
    }

    final c = pCena.ref;
    c.camadas = pCamadas;
    c.quantidadeDeCamadas = n;
    c.luzes = pLuzes;
    c.quantidadeDeLuzes = m;
    c.sombra = cena.sombra;
    c.amostras = cena.amostras;
    c.largura = cena.largura;
    c.altura = cena.altura;
    c.ambienteR = cena.ambienteR;
    c.ambienteG = cena.ambienteG;
    c.ambienteB = cena.ambienteB;
    c.ceuR = cena.ceuR;
    c.ceuG = cena.ceuG;
    c.ceuB = cena.ceuB;
    c.reflexoDoAmbiente = cena.reflexoDoAmbiente;
    c.chaoR = cena.chaoR;
    c.chaoG = cena.chaoG;
    c.chaoB = cena.chaoB;
    c.chaoReserva = 1.0;
    final cam = cena.camera;
    c.cameraPosicaoX = cam.posicaoX;
    c.cameraPosicaoY = cam.posicaoY;
    c.cameraPosicaoZ = cam.posicaoZ;
    c.cameraAlvoX = cam.alvoX;
    c.cameraAlvoY = cam.alvoY;
    c.cameraAlvoZ = cam.alvoZ;
    c.cameraRotacaoX = cam.rotacaoX;
    c.cameraRotacaoY = cam.rotacaoY;
    c.cameraRotacaoZ = cam.rotacaoZ;
    c.cameraUsarRotacao = cam.usarRotacao ? 1 : 0;
    c.cameraCimaX = cam.cimaX;
    c.cameraCimaY = cam.cimaY;
    c.cameraCimaZ = cam.cimaZ;
    c.cameraFovGraus = cam.fovGraus;
    c.cameraPerto = cam.perto;
    c.cameraLonge = cam.longe;
    c.cameraOrtografica = cam.ortografica ? 1 : 0;
    c.cameraAlturaOrtografica = cam.alturaOrtografica;

    _desenhou = _tresdDesenhar(pCena) != 0;
    if (!_desenhou) {
      _semGeometria = true;
      return false;
    }
    _largura = _tresdLargura();
    _altura = _tresdAltura();
    // UM QUADRO SEM GEOMETRIA NENHUMA NAO TEM PIXEL NOVO — e ele NAO e a
    // mesma coisa que um quadro preto. O primeiro nao entra na composicao;
    // o segundo entraria como uma camada opaca tapando tudo.
    _semGeometria = _tresdPixels() == nullptr;
    return true;
  }

  /// OS PIXELS DO ULTIMO QUADRO, em RGBA8 PREMULTIPLICADO, como uma vista
  /// SEM COPIA sobre a memoria do motor. Nulo quando nao houve 3D.
  ///
  /// A VISTA VALE ATE O PROXIMO DESENHO. Quem for guardar, copia.
  Uint8List? pixels() {
    if (!_desenhou || _semGeometria) return null;
    final p = _tresdPixels();
    if (p == nullptr || _largura == 0 || _altura == 0) return null;
    return p.asTypedList(_largura * _altura * 4);
  }

  bool get semGeometria => !_desenhou || _semGeometria;
  int get largura => _largura;
  int get altura => _altura;

  /// SOLTA OS BUFFERS DA CENA. Chamado quando o app troca de projeto ou
  /// fecha o editor.
  void esquecer() {
    if (_camadas != null) {
      calloc.free(_camadas!);
      _camadas = null;
    }
    if (_luzes != null) {
      calloc.free(_luzes!);
      _luzes = null;
    }
    if (_cena != null) {
      calloc.free(_cena!);
      _cena = null;
    }
    _camadasReservadas = 0;
    _luzesReservadas = 0;
    _desenhou = false;
    _semGeometria = true;
    _largura = 0;
    _altura = 0;
  }

  // --------------------------------------------------------- os campos

  static void _escreverCamada(_Camada3DC d, CamadaDeCena3D c) {
    d.alca = c.alca;
    d.modelo = c.modelo;
    d.animacao = c.animacao;
    d.tempoDaAnimacao = c.tempoDaAnimacao;
    d.visivel = c.visivel ? 1 : 0;
    d.posicaoX = c.posicaoX;
    d.posicaoY = c.posicaoY;
    d.posicaoZ = c.posicaoZ;
    d.rotacaoX = c.rotacaoX;
    d.rotacaoY = c.rotacaoY;
    d.rotacaoZ = c.rotacaoZ;
    d.escalaX = c.escalaX;
    d.escalaY = c.escalaY;
    d.escalaZ = c.escalaZ;
    d.ancoraX = c.ancoraX;
    d.ancoraY = c.ancoraY;
    d.ancoraZ = c.ancoraZ;
    d.opacidade = c.opacidade;
    d.corR = c.cor.r;
    d.corG = c.cor.g;
    d.corB = c.cor.b;
    d.corA = c.cor.a;
    d.camadaZ = c.camadaZ;

    final m = c.material;
    d.materialLigado = m.ligado ? 1 : 0;
    d.materialCorBaseR = m.corBase.r;
    d.materialCorBaseG = m.corBase.g;
    d.materialCorBaseB = m.corBase.b;
    d.materialCorBaseA = m.corBase.a;
    d.materialMetalico = m.metalico;
    d.materialRugosidade = m.rugosidade;
    d.materialForcaEmissiva = m.forcaEmissiva;
    d.materialEmissivoR = m.emissivo.r;
    d.materialEmissivoG = m.emissivo.g;
    d.materialEmissivoB = m.emissivo.b;
    d.materialEmissivoA = m.emissivo.a;
    d.materialModo = m.modo;
    d.materialFaceDupla = m.faceDupla ? 1 : 0;
    d.materialAlfaCorte = m.alfaCorte;
    d.materialSemTexturaDeCor = m.semTexturaDeCor ? 1 : 0;
  }

  static void _escreverLuz(_Luz3DC d, LuzDeCena3D l) {
    d.tipo = l.tipo;
    d.posicaoX = l.posicaoX;
    d.posicaoY = l.posicaoY;
    d.posicaoZ = l.posicaoZ;
    d.direcaoX = l.direcaoX;
    d.direcaoY = l.direcaoY;
    d.direcaoZ = l.direcaoZ;
    d.corR = l.corR;
    d.corG = l.corG;
    d.corB = l.corB;
    d.intensidade = l.intensidade;
    d.alcance = l.alcance;
    d.anguloInternoGraus = l.anguloInternoGraus;
    d.anguloExternoGraus = l.anguloExternoGraus;
    d.ligada = l.ligada ? 1 : 0;
  }
}
