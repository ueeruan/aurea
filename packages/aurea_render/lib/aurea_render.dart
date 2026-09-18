/// A PORTA DART DO RENDERCORE.
///
/// O QUE ESTA CAMADA E, E O QUE ELA NAO E: ela e FINA de proposito. Ela
/// nao compoe, nao avalia animacao, nao decide qualidade — ela COPIA
/// estado para o C++ e LE numeros de volta. Toda a conta acontece do
/// outro lado; se algum dia aparecer aqui um `for` sobre pixels, o
/// RenderCore deixou de existir e virou enfeite.
///
/// NAO HA FRAME ATRAVESSANDO A PONTE. O caminho de producao e
/// `estado -> comando -> C++ -> GPU -> superficie`. O unico metodo que
/// traz pixels para o Dart e [NucleoRender.lerPixels], e ele existe para
/// teste e bancada — esta escrito la, e nao ha como confundir.
library;

import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';
import 'package:flutter/foundation.dart' show debugPrint;

// --------------------------------------------------------------- a ABI

/// A VERSAO DA PORTA. O C++ responde [versaoDaPorta] e o Dart confere: um
/// descompasso entre a biblioteca compilada e este arquivo e a causa mais
/// comum de "leu o campo errado" depois de uma atualizacao parcial.
const int versaoEsperadaDaPorta = 4;

final class _CamadaC extends Struct {
  @Uint32()
  external int tipo;

  @Uint32()
  external int textura;

  @Float()
  external double x;
  @Float()
  external double y;
  @Float()
  external double largura;
  @Float()
  external double altura;

  @Float()
  external double ancoraX;
  @Float()
  external double ancoraY;

  @Float()
  external double escalaX;
  @Float()
  external double escalaY;
  @Float()
  external double rotacaoGraus;
  @Float()
  external double opacidade;

  @Uint32()
  external int mistura;

  @Uint32()
  external int cor;
}

/// OS DOIS INTEIROS NO FIM, na mesma ordem do C++: um `Int32` no meio de
/// `Double` abre preenchimento, e preenchimento e o que faz os dois lados
/// discordarem sem avisar. O teste `nucleo_abi_test.dart` confere o
/// `sizeOf` das tres structs contra o que o C++ responde.
final class _CurvaC extends Struct {
  @Double()
  external double x1;
  @Double()
  external double y1;
  @Double()
  external double x2;
  @Double()
  external double y2;
  @Double()
  external double suavidade;
  @Double()
  external double intensidade;
  @Double()
  external double resposta;
  @Double()
  external double amortecimento;
  @Double()
  external double velocidadeInicial;

  @Int32()
  external int tipo;
  @Int32()
  external int contagem;
}

/// A CURVA ACHATADA DENTRO DO KEYFRAME.
///
/// Um `Struct` aninhado do `dart:ffi` nao tem como ser reinterpretado no
/// lugar (`Array<Uint8>` nao tem `cast`), e alinhar dois `Struct`
/// aninhados e a forma mais facil de errar o layout. Achatado, o
/// resultado e o MESMO byte a byte — 8 + 8 + 80 = 96 — e o teste de
/// tamanho confere contra o que o C++ responde.
final class _KeyframeC extends Struct {
  @Double()
  external double tempoS;
  @Double()
  external double valor;

  @Double()
  external double curvaX1;
  @Double()
  external double curvaY1;
  @Double()
  external double curvaX2;
  @Double()
  external double curvaY2;
  @Double()
  external double curvaSuavidade;
  @Double()
  external double curvaIntensidade;
  @Double()
  external double curvaResposta;
  @Double()
  external double curvaAmortecimento;
  @Double()
  external double curvaVelocidadeInicial;

  @Int32()
  external int curvaTipo;
  @Int32()
  external int curvaContagem;
}

// ------------------------------------------------------------- os tipos

/// OS MODOS DE MISTURA, NA NUMERACAO DO C++.
///
/// A TRADUCAO VEM POR NOME, e nunca pelo indice do `BlendMode` do
/// `dart:ui`: um membro novo no enum do Flutter deslocaria todos os
/// seguintes, e o projeto que abrisse com "multiplicar" viraria "tela"
/// sem ninguem mexer em nada.
enum MisturaDeRender {
  normal(0),
  multiplicar(1),
  tela(2),
  sobrepor(3),
  somar(4),
  escurecer(5),
  clarear(6),
  diferenca(7);

  const MisturaDeRender(this.valorNoNucleo);
  final int valorNoNucleo;
}

enum TipoDeCamadaDeRender { vazia(0), cor(1), textura(2);

  const TipoDeCamadaDeRender(this.valorNoNucleo);
  final int valorNoNucleo;
}

/// UMA CAMADA, DO JEITO QUE O NUCLEO ENTENDE.
///
/// (x, y) e onde cai a ANCORA na composicao; [largura] e [altura] sao a
/// caixa local, antes de escalar. A ancora e uma fracao 0..1 dentro da
/// caixa — (0,5, 0,5) e o centro.
class CamadaDeRender {
  const CamadaDeRender({
    this.tipo = TipoDeCamadaDeRender.cor,
    this.textura = 0,
    required this.x,
    required this.y,
    required this.largura,
    required this.altura,
    this.ancoraX = 0.5,
    this.ancoraY = 0.5,
    this.escalaX = 1,
    this.escalaY = 1,
    this.rotacaoGraus = 0,
    this.opacidade = 1,
    this.mistura = MisturaDeRender.normal,
    this.cor = 0xFFFFFFFF,
  });

  final TipoDeCamadaDeRender tipo;
  final int textura;
  final double x;
  final double y;
  final double largura;
  final double altura;
  final double ancoraX;
  final double ancoraY;
  final double escalaX;
  final double escalaY;
  final double rotacaoGraus;
  final double opacidade;
  final MisturaDeRender mistura;

  /// ARGB EMPACOTADO (0xAARRGGBB) — o mesmo `Color.value` do Flutter.
  /// A ponte manda o numero cru: trocar a ordem aqui faz vermelho virar
  /// azul sem erro nenhum.
  final int cor;
}

/// O QUE O NUCLEO RESPONDE. Os indices sao o contrato com `api.cpp`.
class EstatisticasDeRender {
  const EstatisticasDeRender(this._campos);

  final Float64List _campos;

  double _em(int i) => i < _campos.length ? _campos[i] : 0;

  double get cpuUltimoMs => _em(0);
  double get cpuMedianaMs => _em(1);
  double get cpuP95Ms => _em(2);
  double get cpuMaxMs => _em(3);
  double get intervaloMedianaMs => _em(4);
  double get fpsEfetivo => _em(5);
  int get quadros => _em(6).toInt();
  int get quadrosAtrasados => _em(7).toInt();
  int get quadrosPulados => _em(8).toInt();
  int get quadrosReaproveitados => _em(9).toInt();
  int get quadrosFalhos => _em(10).toInt();
  int get bytesEmUso => _em(11).toInt();
  int get bytesOrcamento => _em(12).toInt();
  int get bytesPico => _em(13).toInt();
  int get recursosVivos => _em(14).toInt();
  int get recursosDespejados => _em(15).toInt();
  int get recursosReaproveitados => _em(16).toInt();
  int get recursosRecusados => _em(17).toInt();
  int get shadersReaproveitados => _em(18).toInt();
  int get shadersFalhas => _em(19).toInt();
  int get camadasDesenhadas => _em(20).toInt();
  int get pixelsEscritos => _em(21).toInt();
  int get amostrasPorPixel => _em(22).toInt();
  double get orcamentoMs => _em(23);
  double get escalaInterna => _em(24);
  int get quadrosNaJanela => _em(25).toInt();
  int get shadersCompilados => _em(26).toInt();
  int get recursosCriados => _em(27).toInt();

  @override
  String toString() =>
      'quadros=$quadros cpu p50=${cpuMedianaMs.toStringAsFixed(2)}ms '
      'p95=${cpuP95Ms.toStringAsFixed(2)}ms '
      'fps=${fpsEfetivo.toStringAsFixed(1)} '
      'recursos=${bytesEmUso ~/ 1024}KB/${bytesOrcamento ~/ 1024}KB '
      'shaders=${shadersCompilados}c/${shadersReaproveitados}r';
}

// ---------------------------------------------------------- as ligacoes

@Native<Int32 Function()>(symbol: 'aurea_render_versao', isLeaf: true)
external int _versao();

@Native<Uint32 Function()>(symbol: 'aurea_render_tamanho_camada', isLeaf: true)
external int _tamanhoCamada();

@Native<Uint32 Function()>(symbol: 'aurea_render_tamanho_curva', isLeaf: true)
external int _tamanhoCurva();

@Native<Uint32 Function()>(symbol: 'aurea_render_tamanho_keyframe', isLeaf: true)
external int _tamanhoKeyframe();

@Native<
  Pointer<Void> Function(
    Uint32,
    Uint32,
    Uint64,
    Uint32,
    Int32,
    Uint32,
    Double,
    Double
  )
>(symbol: 'aurea_render_abrir')
external Pointer<Void> _abrir(
  int largura,
  int altura,
  int orcamentoDeRecursos,
  int backend,
  int comThread,
  int amostras,
  double escalaInterna,
  double orcamentoMs,
);

/// O PORQUE DA ULTIMA ABERTURA TER FALHADO. Vazio = deu certo.
@Native<Pointer<Utf8> Function()>(symbol: 'aurea_render_ultimo_erro', isLeaf: true)
external Pointer<Utf8> _ultimoErro();

@Native<Void Function(Pointer<Void>)>(symbol: 'aurea_render_fechar')
external void _fechar(Pointer<Void> n);

@Native<
  Int32 Function(Pointer<Void>, Pointer<_CamadaC>, Uint32, Uint32, Uint32, Uint64)
>(symbol: 'aurea_render_publicar_cena')
external int _publicarCena(
  Pointer<Void> n,
  Pointer<_CamadaC> camadas,
  int quantas,
  int largura,
  int altura,
  int impressao,
);

@Native<
  Uint32 Function(Pointer<Void>, Pointer<Uint8>, Uint32, Uint32)
>(symbol: 'aurea_render_registrar_textura')
external int _registrarTextura(
  Pointer<Void> n,
  Pointer<Uint8> rgba,
  int largura,
  int altura,
);

@Native<Void Function(Pointer<Void>)>(symbol: 'aurea_render_pedir_quadro')
external void _pedirQuadro(Pointer<Void> n);

@Native<Int32 Function(Pointer<Void>)>(symbol: 'aurea_render_desenhar_agora')
external int _desenharAgora(Pointer<Void> n);

@Native<
  Uint32 Function(Pointer<Void>, Pointer<Uint8>, Uint32)
>(symbol: 'aurea_render_ler_pixels')
external int _lerPixels(Pointer<Void> n, Pointer<Uint8> destino, int capacidade);

@Native<
  Uint32 Function(Pointer<Void>, Pointer<Double>, Uint32)
>(symbol: 'aurea_render_estatisticas')
external int _estatisticas(Pointer<Void> n, Pointer<Double> saida, int capacidade);

@Native<
  Void Function(Pointer<Void>, Uint32, Double, Double)
>(symbol: 'aurea_render_definir_qualidade')
external void _definirQualidade(
  Pointer<Void> n,
  int amostras,
  double escalaInterna,
  double orcamentoMs,
);

@Native<Void Function(Pointer<Void>, Int32)>(
  symbol: 'aurea_render_definir_automatica',
)
external void _definirAutomatica(Pointer<Void> n, int ligada);

@Native<
  Int32 Function(Pointer<Void>, Pointer<Pointer<Utf8>>, Pointer<Pointer<Utf8>>, Pointer<Uint32>, Uint32)
>(symbol: 'aurea_render_pre_aquecer')
external int _preAquecer(
  Pointer<Void> n,
  Pointer<Pointer<Utf8>> nomes,
  Pointer<Pointer<Utf8>> fontes,
  Pointer<Uint32> versoes,
  int quantos,
);

@Native<
  Double Function(Pointer<_KeyframeC>, Uint32, Double, Double, Pointer<Int32>)
>(symbol: 'aurea_render_avaliar', isLeaf: true)
external double _avaliar(
  Pointer<_KeyframeC> quadros,
  int quantos,
  double tempoS,
  double base,
  Pointer<Int32> espelhado,
);

@Native<Double Function(Pointer<_CurvaC>, Double)>(
  symbol: 'aurea_render_curva',
  isLeaf: true,
)
external double _curva(Pointer<_CurvaC> c, double t);

// ------------------------------------------------------------- o nucleo

/// O NUCLEO DO LADO DART. Segura o handle e nada mais.
class NucleoRender {
  NucleoRender._(this._nucleo);

  final Pointer<Void> _nucleo;
  bool _fechado = false;

  /// SE A BIBLIOTECA CARREGOU E A PORTA FALA A MESMA LINGUA.
  ///
  /// Uma versao diferente NAO e usada: um descompasso de ABI le o campo
  /// do vizinho e devolve um numero plausivel. Melhor cair no caminho
  /// antigo do que desenhar errado.
  static bool get disponivel {
    try {
      return _versao() == versaoEsperadaDaPorta;
    } catch (e) {
      debugPrint('AUREA: RenderCore indisponivel ($e). Caminho antigo.');
      return false;
    }
  }

  /// POR QUE A ULTIMA ABERTURA FALHOU, EM TEXTO.
  ///
  /// Um `null` nao e um diagnostico: sem isto, "a GPU nao subiu" chega na
  /// tela como "nao abriu", e o relatorio de bug nao tem o que dizer.
  static String get ultimoErro {
    try {
      final p = _ultimoErro();
      return p == nullptr ? '' : p.toDartString();
    } catch (_) {
      return '';
    }
  }

  /// O TAMANHO DAS STRUCTS, do lado do C++, para o teste comparar.
  static int get tamanhoDaCamada => _tamanhoCamada();
  static int get tamanhoDaCurva => _tamanhoCurva();
  static int get tamanhoDoKeyframe => _tamanhoKeyframe();

  /// ABRE O NUCLEO. [backend] so aceita 0 (referencia, CPU) hoje: pedir
  /// GPU devolve `null` em vez de um nucleo que diz ser o que nao e.
  static NucleoRender? abrir({
    required int largura,
    required int altura,
    int orcamentoDeRecursos = 0,
    int backend = 0,
    bool comThread = true,
    int amostras = 2,
    double escalaInterna = 1.0,
    double orcamentoMs = 1000 / 60,
  }) {
    if (!disponivel) return null;
    final p = _abrir(
      largura,
      altura,
      orcamentoDeRecursos,
      backend,
      comThread ? 1 : 0,
      amostras,
      escalaInterna,
      orcamentoMs,
    );
    if (p == nullptr) return null;
    return NucleoRender._(p);
  }

  /// PUBLICA A CENA. As camadas sao copiadas para dentro do C++ nesta
  /// chamada: o buffer morre aqui e o C++ nao guarda ponteiro nenhum.
  bool publicarCena(
    List<CamadaDeRender> camadas, {
    required int largura,
    required int altura,
    int impressao = 0,
  }) {
    if (_fechado) return false;
    final quantas = camadas.length;
    final buffer = quantas == 0
        ? nullptr
        : calloc<_CamadaC>(quantas);
    try {
      for (var i = 0; i < quantas; i++) {
        final c = camadas[i];
        final alvo = (buffer + i).ref;
        alvo.tipo = c.tipo.valorNoNucleo;
        alvo.textura = c.textura;
        alvo.x = c.x;
        alvo.y = c.y;
        alvo.largura = c.largura;
        alvo.altura = c.altura;
        alvo.ancoraX = c.ancoraX;
        alvo.ancoraY = c.ancoraY;
        alvo.escalaX = c.escalaX;
        alvo.escalaY = c.escalaY;
        alvo.rotacaoGraus = c.rotacaoGraus;
        alvo.opacidade = c.opacidade;
        alvo.mistura = c.mistura.valorNoNucleo;
        alvo.cor = c.cor;
      }
      return _publicarCena(_nucleo, buffer, quantas, largura, altura, impressao) ==
          0;
    } finally {
      if (buffer != nullptr) calloc.free(buffer);
    }
  }

  /// REGISTRA UMA TEXTURA (RGBA8 nao-premultiplicado). Devolve 0 quando o
  /// orcamento nao comporta.
  int registrarTextura(Uint8List rgba, int largura, int altura) {
    if (_fechado) return 0;
    final ptr = calloc<Uint8>(rgba.length);
    try {
      ptr.asTypedList(rgba.length).setAll(0, rgba);
      return _registrarTextura(_nucleo, ptr, largura, altura);
    } finally {
      calloc.free(ptr);
    }
  }

  void pedirQuadro() {
    if (!_fechado) _pedirQuadro(_nucleo);
  }

  /// DESENHA AGORA, sem passar pela thread. E o caminho da bancada e do
  /// teste: deterministico e sem corrida.
  int desenharAgora() => _fechado ? -1 : _desenharAgora(_nucleo);

  /// LE O ULTIMO QUADRO. TESTE E BANCADA — ver a regra no `api.cpp`.
  Uint8List? lerPixels() {
    if (_fechado) return null;
    final capacidade = 4096 * 4096 * 4;
    final ptr = calloc<Uint8>(capacidade);
    try {
      final escritos = _lerPixels(_nucleo, ptr, capacidade);
      if (escritos == 0) return null;
      return Uint8List.fromList(ptr.asTypedList(escritos));
    } finally {
      calloc.free(ptr);
    }
  }

  EstatisticasDeRender estatisticas() {
    const campos = 28;
    final ptr = calloc<Double>(campos);
    try {
      final escritos = _estatisticas(_nucleo, ptr, campos);
      return EstatisticasDeRender(ptr.asTypedList(campos).sublist(0, escritos));
    } finally {
      calloc.free(ptr);
    }
  }

  void definirQualidade({
    required int amostras,
    required double escalaInterna,
    required double orcamentoMs,
  }) {
    if (!_fechado) {
      _definirQualidade(_nucleo, amostras, escalaInterna, orcamentoMs);
    }
  }

  void definirAutomatica(bool ligada) {
    if (!_fechado) _definirAutomatica(_nucleo, ligada ? 1 : 0);
  }

  /// COMPILA SHADERS EM LOTE, fora do quadro. Devolve quantos ficaram
  /// prontos.
  int preAquecer(List<({String nome, String fonte, int versao})> shaders) {
    if (_fechado || shaders.isEmpty) return 0;
    final n = shaders.length;
    final nomes = calloc<Pointer<Utf8>>(n);
    final fontes = calloc<Pointer<Utf8>>(n);
    final versoes = calloc<Uint32>(n);
    final alocados = <Pointer<Utf8>>[];
    try {
      for (var i = 0; i < n; i++) {
        final a = shaders[i].nome.toNativeUtf8();
        final b = shaders[i].fonte.toNativeUtf8();
        alocados
          ..add(a)
          ..add(b);
        nomes[i] = a;
        fontes[i] = b;
        versoes[i] = shaders[i].versao;
      }
      return _preAquecer(_nucleo, nomes, fontes, versoes, n);
    } finally {
      for (final p in alocados) {
        malloc.free(p);
      }
      calloc
        ..free(nomes)
        ..free(fontes)
        ..free(versoes);
    }
  }

  void fechar() {
    if (_fechado) return;
    _fechado = true;
    _fechar(_nucleo);
  }
}

// --------------------------------------------------- o avaliador puro

/// UMA CURVA, do lado Dart. Os campos sao os mesmos do `Easing` do
/// projeto, e a ordem dos argumentos segue o construtor de la.
class CurvaDeRender {
  const CurvaDeRender({
    this.tipo = 0,
    this.x1 = 0,
    this.y1 = 0,
    this.x2 = 1,
    this.y2 = 1,
    this.contagem = 4,
    this.suavidade = 1,
    this.intensidade = 0.5,
    this.resposta = 0.55,
    this.amortecimento = 0.825,
    this.velocidadeInicial = 0,
  });

  final int tipo;
  final double x1, y1, x2, y2;
  final int contagem;
  final double suavidade, intensidade, resposta, amortecimento;
  final double velocidadeInicial;
}

class KeyframeDeRender {
  const KeyframeDeRender({
    required this.tempoS,
    required this.valor,
    this.curva = const CurvaDeRender(),
  });

  final double tempoS;
  final double valor;
  final CurvaDeRender curva;
}

class ResultadoDaAvaliacaoDeRender {
  const ResultadoDaAvaliacaoDeRender(this.valor, this.espelhado);

  final double valor;
  final bool espelhado;
}

/// AVALIA A PILHA NO C++ — a mesma conta do Dart, para o teste comparar.
///
/// A memoria e alocada e liberada dentro da chamada: nada aqui sobrevive
/// a ela, e por isso nao ha ponteiro guardado do lado do C++.
ResultadoDaAvaliacaoDeRender avaliarNoNucleo(
  List<KeyframeDeRender> quadros,
  double tempoS, {
  double base = 0,
}) {
  if (quadros.isEmpty) return ResultadoDaAvaliacaoDeRender(base, true);
  final ptr = calloc<_KeyframeC>(quadros.length);
  final espelhado = calloc<Int32>();
  try {
    for (var i = 0; i < quadros.length; i++) {
      final k = quadros[i];
      final alvo = (ptr + i).ref;
      alvo
        ..tempoS = k.tempoS
        ..valor = k.valor
        ..curvaX1 = k.curva.x1
        ..curvaY1 = k.curva.y1
        ..curvaX2 = k.curva.x2
        ..curvaY2 = k.curva.y2
        ..curvaSuavidade = k.curva.suavidade
        ..curvaIntensidade = k.curva.intensidade
        ..curvaResposta = k.curva.resposta
        ..curvaAmortecimento = k.curva.amortecimento
        ..curvaVelocidadeInicial = k.curva.velocidadeInicial
        ..curvaTipo = k.curva.tipo
        ..curvaContagem = k.curva.contagem;
    }
    final v = _avaliar(ptr, quadros.length, tempoS, base, espelhado);
    return ResultadoDaAvaliacaoDeRender(v, espelhado.value != 0);
  } finally {
    calloc
      ..free(ptr)
      ..free(espelhado);
  }
}

/// TRANSFORMA UMA CURVA SOZINHA. O teste de divergencia usa isto para
/// comparar curva a curva: uma curva errada some dentro de uma
/// interpolacao, e separada ela aparece.
double transformarCurvaNoNucleo(CurvaDeRender c, double t) {
  final ptr = calloc<_CurvaC>();
  try {
    final curva = ptr.ref;
    curva
      ..x1 = c.x1
      ..y1 = c.y1
      ..x2 = c.x2
      ..y2 = c.y2
      ..suavidade = c.suavidade
      ..intensidade = c.intensidade
      ..resposta = c.resposta
      ..amortecimento = c.amortecimento
      ..velocidadeInicial = c.velocidadeInicial
      ..tipo = c.tipo
      ..contagem = c.contagem;
    return _curva(ptr, t);
  } finally {
    calloc.free(ptr);
  }
}
