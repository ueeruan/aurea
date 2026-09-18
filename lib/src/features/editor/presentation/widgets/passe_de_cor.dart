import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import '../../domain/correcao_de_cor.dart';
import '../../domain/estilizar.dart';
import 'fx_lote2.dart';

/// O MOTOR DA CORRECAO DE COR: os dois shaders do lote novo.
///
/// Separado do `PixelEffectEngine` de proposito. O shader antigo tem 62
/// modos de efeitos que ja nao existem, e cada filtro que usa um programa
/// paga o pipeline inteiro dele; estes dois sao pequenos e so fazem o que
/// o catalogo novo pede.
class MotorDeCorrecao {
  MotorDeCorrecao._();

  static ui.FragmentProgram? _cor;
  static ui.FragmentProgram? _nitidez;
  static ui.FragmentProgram? _estilo;
  static Future<void>? _carregando;

  /// O erro de carga, para o teste e o log dizerem o que houve.
  static String? falha;

  static bool get corPronta => _cor != null;
  static bool get nitidezPronta => _nitidez != null;
  static bool get estiloPronto => _estilo != null;

  static Future<void> warmUp() => _carregando ??= _carregar();

  static Future<void> _carregar() async {
    try {
      _cor = await ui.FragmentProgram.fromAsset('shaders/correcao_de_cor.frag');
    } catch (erro) {
      falha = '$erro';
      debugPrint('AUREA correcao de cor indisponivel: $erro');
    }
    try {
      _nitidez = await ui.FragmentProgram.fromAsset(
        'shaders/unsharp_mask.frag',
      );
    } catch (erro) {
      falha = '$erro';
      debugPrint('AUREA unsharp mask indisponivel: $erro');
    }
    try {
      _estilo = await ui.FragmentProgram.fromAsset('shaders/estilizar.frag');
    } catch (erro) {
      falha = '$erro';
      debugPrint('AUREA estilizar indisponivel: $erro');
    }
  }

  /// Floats 2..43 do shader de Estilizar.
  static void configurarEstilo(
    ui.FragmentShader shader,
    QuadroDeEstilo q, {
    required bool filtro,
    required Size logico,
    required double escalaRef,
    required double tempo,
  }) {
    shader
      ..setFloat(2, filtro ? 1 : 0)
      ..setFloat(3, logico.width)
      ..setFloat(4, logico.height)
      ..setFloat(5, escalaRef)
      ..setFloat(6, q.modo.toDouble())
      ..setFloat(7, tempo);
    for (var i = 0; i < 16; i++) {
      shader.setFloat(8 + i, i < q.valores.length ? q.valores[i] : 0);
    }
    for (var k = 0; k < 5; k++) {
      final c = k < q.cores.length ? q.cores[k] : const Color(0xFF000000);
      shader
        ..setFloat(24 + 4 * k, c.r)
        ..setFloat(25 + 4 * k, c.g)
        ..setFloat(26 + 4 * k, c.b)
        ..setFloat(27 + 4 * k, c.a);
    }
  }

  @visibleForTesting
  static ui.FragmentShader shaderDeEstilo(
    QuadroDeEstilo q, {
    required ui.Image imagem,
    double escalaRef = 1,
    double tempo = 0,
  }) {
    final shader = _estilo!.fragmentShader();
    configurarEstilo(
      shader,
      q,
      filtro: false,
      logico: Size(imagem.width.toDouble(), imagem.height.toDouble()),
      escalaRef: escalaRef,
      tempo: tempo,
    );
    shader
      ..setFloat(0, imagem.width.toDouble())
      ..setFloat(1, imagem.height.toDouble())
      ..setImageSampler(0, imagem);
    return shader;
  }

  /// Floats 2..35 do shader de cor.
  static void configurarCor(
    ui.FragmentShader shader,
    List<OperacaoDeCor> operacoes, {
    required bool filtro,
  }) {
    final n = operacoes.length.clamp(0, operacoesPorPassada);
    shader
      ..setFloat(2, filtro ? 1 : 0)
      ..setFloat(3, n.toDouble());
    for (var k = 0; k < operacoesPorPassada; k++) {
      final u = k < n ? operacoes[k].uniformes : const <double>[];
      for (var i = 0; i < 8; i++) {
        shader.setFloat(4 + 8 * k + i, i < u.length ? u[i] : 0);
      }
    }
  }

  /// Floats 2..10 do Unsharp Mask.
  static void configurarNitidez(
    ui.FragmentShader shader,
    ParametrosDeNitidez p, {
    required bool filtro,
    required Size logico,
    required double escalaRef,
    required int amostras,
  }) {
    shader
      ..setFloat(2, filtro ? 1 : 0)
      ..setFloat(3, logico.width)
      ..setFloat(4, logico.height)
      ..setFloat(5, escalaRef)
      ..setFloat(6, p.quantidade)
      ..setFloat(7, p.raio)
      ..setFloat(8, p.limiar)
      ..setFloat(9, p.soLuminancia ? 1 : 0)
      ..setFloat(10, amostras.toDouble());
  }

  @visibleForTesting
  static ui.FragmentShader shaderDeCor(
    List<OperacaoDeCor> operacoes, {
    required ui.Image imagem,
  }) {
    final shader = _cor!.fragmentShader();
    configurarCor(shader, operacoes, filtro: false);
    shader
      ..setFloat(0, imagem.width.toDouble())
      ..setFloat(1, imagem.height.toDouble())
      ..setImageSampler(0, imagem);
    return shader;
  }

  @visibleForTesting
  static ui.FragmentShader shaderDeNitidez(
    ParametrosDeNitidez p, {
    required ui.Image imagem,
    double escalaRef = 1,
    int amostras = AmostrasDeNitidez.exportacao,
  }) {
    final shader = _nitidez!.fragmentShader();
    configurarNitidez(
      shader,
      p,
      filtro: false,
      logico: Size(imagem.width.toDouble(), imagem.height.toDouble()),
      escalaRef: escalaRef,
      amostras: amostras,
    );
    shader
      ..setFloat(0, imagem.width.toDouble())
      ..setFloat(1, imagem.height.toDouble())
      ..setImageSampler(0, imagem);
    return shader;
  }
}

/// UMA PASSADA DE COR: ate [operacoesPorPassada] operacoes numa leitura
/// so da camada. Lista vazia = passa o filho direto, sem textura extra —
/// mas a arvore continua a mesma, para o video embaixo nao ser remontado
/// quando um numero cruza o neutro.
class PassadaDeCor extends StatefulWidget {
  const PassadaDeCor({
    super.key,
    required this.operacoes,
    required this.child,
  });

  final List<OperacaoDeCor> operacoes;
  final Widget child;

  @override
  State<PassadaDeCor> createState() => _PassadaDeCorState();
}

class _PassadaDeCorState extends State<PassadaDeCor> {
  ui.FragmentShader? _shader;

  @override
  void dispose() {
    _shader?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final programa = MotorDeCorrecao._cor;
    if (programa == null) return widget.child;
    final operacoes = widget.operacoes.length > operacoesPorPassada
        ? widget.operacoes.sublist(0, operacoesPorPassada)
        : widget.operacoes;
    final shader = _shader ??= programa.fragmentShader();
    final uniformes = [for (final op in operacoes) ...op.uniformes];
    if (ui.ImageFilter.isShaderFilterSupported) {
      return FiltroDeShader(
        shader: shader,
        ativo: operacoes.isNotEmpty,
        assinatura: uniformes,
        configurar: (s, _) =>
            MotorDeCorrecao.configurarCor(s, operacoes, filtro: true),
        child: widget.child,
      );
    }
    if (operacoes.isEmpty) return widget.child;
    return FxSnapshot(
      painter: _PintorDeShader(
        shader,
        uniformes,
        (s, _) => MotorDeCorrecao.configurarCor(s, operacoes, filtro: false),
      ),
      child: widget.child,
    );
  }
}

/// A PASSADA DO UNSHARP MASK. [parametros] nulo (quantidade zero) deixa
/// a camada intacta sem trocar a arvore.
class PassadaDeNitidez extends StatefulWidget {
  const PassadaDeNitidez({
    super.key,
    required this.parametros,
    required this.escalaRef,
    required this.amostras,
    required this.child,
  });

  final ParametrosDeNitidez? parametros;

  /// Pixels da composicao por pixel de 1080p (menor lado / 1080).
  final double escalaRef;

  /// Teto de amostras no raio grande ([AmostrasDeNitidez]).
  final int amostras;

  final Widget child;

  @override
  State<PassadaDeNitidez> createState() => _PassadaDeNitidezState();
}

class _PassadaDeNitidezState extends State<PassadaDeNitidez> {
  ui.FragmentShader? _shader;

  @override
  void dispose() {
    _shader?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final programa = MotorDeCorrecao._nitidez;
    if (programa == null) return widget.child;
    final p = widget.parametros;
    final shader = _shader ??= programa.fragmentShader();
    void configurar(ui.FragmentShader s, Size logico, bool filtro) {
      if (p == null) return;
      MotorDeCorrecao.configurarNitidez(
        s,
        p,
        filtro: filtro,
        logico: logico,
        escalaRef: widget.escalaRef,
        amostras: widget.amostras,
      );
    }

    final assinatura = p == null
        ? const <double>[]
        : [
            p.quantidade,
            p.raio,
            p.limiar,
            p.soLuminancia ? 1.0 : 0.0,
            widget.escalaRef,
            widget.amostras.toDouble(),
          ];
    if (ui.ImageFilter.isShaderFilterSupported) {
      return FiltroDeShader(
        shader: shader,
        ativo: p != null,
        assinatura: assinatura,
        configurar: (s, logico) => configurar(s, logico, true),
        child: widget.child,
      );
    }
    if (p == null) return widget.child;
    return FxSnapshot(
      painter: _PintorDeShader(
        shader,
        assinatura,
        (s, logico) => configurar(s, logico, false),
      ),
      child: widget.child,
    );
  }
}

/// A PASSADA DE UM EFEITO DE ESTILIZAR (CC e Sapphire, lote 1).
class PassadaDeEstilo extends StatefulWidget {
  const PassadaDeEstilo({
    super.key,
    required this.quadro,
    required this.escalaRef,
    required this.tempo,
    required this.child,
  });

  final QuadroDeEstilo quadro;
  final double escalaRef;

  /// Segundos no tempo da camada (o ruido do ScanLines troca por quadro).
  final double tempo;
  final Widget child;

  @override
  State<PassadaDeEstilo> createState() => _PassadaDeEstiloState();
}

class _PassadaDeEstiloState extends State<PassadaDeEstilo> {
  ui.FragmentShader? _shader;

  @override
  void dispose() {
    _shader?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final programa = MotorDeCorrecao._estilo;
    if (programa == null) return widget.child;
    final shader = _shader ??= programa.fragmentShader();
    final q = widget.quadro;
    void configurar(ui.FragmentShader s, Size logico, bool filtro) =>
        MotorDeCorrecao.configurarEstilo(
          s,
          q,
          filtro: filtro,
          logico: logico,
          escalaRef: widget.escalaRef,
          tempo: widget.tempo,
        );
    final assinatura = [
      q.modo.toDouble(),
      ...q.valores,
      for (final c in q.cores) ...[c.r, c.g, c.b, c.a],
      widget.escalaRef,
      // O ruido do ScanLines anda por quadro; os outros ignoram o tempo.
      if (q.modo == 5) widget.tempo,
    ];
    if (ui.ImageFilter.isShaderFilterSupported) {
      return FiltroDeShader(
        shader: shader,
        ativo: true,
        assinatura: assinatura,
        configurar: (s, logico) => configurar(s, logico, true),
        child: widget.child,
      );
    }
    return FxSnapshot(
      painter: _PintorDeShader(
        shader,
        assinatura,
        (s, logico) => configurar(s, logico, false),
      ),
      child: widget.child,
    );
  }
}

/// OS SHADERS DO LOTE 2 DE ESTILIZAR (Sapphire), carregados por asset.
///
/// ABI comum: 0,1 tamanho; 2 filtro; 3,4 logico; 5 escalaRef; 6 tempo;
/// 7 modo (passada); 8.. valores; cores em 72.. quando houver.
class MotorSapphire {
  MotorSapphire._();

  static final Map<String, ui.FragmentProgram> _programas = {};
  static final Map<String, Future<void>> _carregando = {};
  static String? falha;

  static ui.FragmentProgram? programa(String asset) => _programas[asset];

  static Future<void> carregar(String asset) =>
      _carregando[asset] ??= () async {
        try {
          _programas[asset] = await ui.FragmentProgram.fromAsset(asset);
        } catch (erro) {
          falha = '$erro';
          debugPrint('AUREA $asset indisponivel: $erro');
        }
      }();

  static Future<void> warmUp(Iterable<String> assets) =>
      Future.wait([for (final a in assets) carregar(a)]);

  static void configurar(
    ui.FragmentShader shader, {
    required int modo,
    required List<double> valores,
    required List<Color> cores,
    required bool filtro,
    required Size logico,
    required double escalaRef,
    required double tempo,
    double? orcamentoDeAmostras,
  }) {
    shader
      ..setFloat(2, filtro ? 1 : 0)
      ..setFloat(3, logico.width)
      ..setFloat(4, logico.height)
      ..setFloat(5, escalaRef)
      ..setFloat(6, tempo)
      ..setFloat(7, modo.toDouble());
    for (var i = 0; i < valores.length; i++) {
      shader.setFloat(8 + i, valores[i]);
    }
    for (var k = 0; k < cores.length; k++) {
      final c = cores[k];
      shader
        ..setFloat(72 + 4 * k, c.r)
        ..setFloat(73 + 4 * k, c.g)
        ..setFloat(74 + 4 * k, c.b)
        ..setFloat(75 + 4 * k, c.a);
    }
    // O ORCAMENTO E O ULTIMO FLOAT DO BLOCO (80), e so o shader que o
    // declara pode recebe-lo — ver `ReceitaSapphire.usaOrcamentoDeAmostras`.
    // A posicao foi conferida com o `impellerc --reflection-json`: os
    // uniforms de runtime effect sao indexados por ORDEM DE DECLARACAO,
    // sem contar o preenchimento de alinhamento do std140.
    if (orcamentoDeAmostras != null) {
      shader.setFloat(80, orcamentoDeAmostras);
    }
  }
}

/// A PASSADA DE UM EFEITO SAPPHIRE DO LOTE 2. [passadas] > 1 roda o mesmo
/// programa com modo 0, 1, ... encadeados (JpegDamage: codificar e
/// decodificar).
class PassadaSapphire extends StatefulWidget {
  const PassadaSapphire({
    super.key,
    required this.asset,
    required this.valores,
    required this.cores,
    required this.escalaRef,
    required this.tempo,
    required this.child,
    this.passadas = 1,
    this.usaTempo = true,
    this.orcamentoDeAmostras,
  });

  final String asset;
  final List<double> valores;
  final List<Color> cores;
  final double escalaRef;
  final double tempo;
  final int passadas;

  /// Fracao das amostras do kernel que este quadro paga (0..1). Nulo nos
  /// shaders que nao tem o uniforme. Ver [AmostrasDoBrilho].
  final double? orcamentoDeAmostras;

  /// Efeito que anda sozinho no tempo (ruido, rolagem): refaz a camada a
  /// cada quadro. Falso = so quando os numeros mudam.
  final bool usaTempo;
  final Widget child;

  @override
  State<PassadaSapphire> createState() => _PassadaSapphireState();
}

class _PassadaSapphireState extends State<PassadaSapphire> {
  final List<ui.FragmentShader> _shaders = [];

  @override
  void dispose() {
    for (final s in _shaders) {
      s.dispose();
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final programa = MotorSapphire.programa(widget.asset);
    if (programa == null) {
      MotorSapphire.carregar(widget.asset).then((_) {
        if (mounted) setState(() {});
      });
      return widget.child;
    }
    while (_shaders.length < widget.passadas) {
      _shaders.add(programa.fragmentShader());
    }
    void configurar(int modo, ui.FragmentShader s, Size logico, bool filtro) =>
        MotorSapphire.configurar(
          s,
          modo: modo,
          valores: widget.valores,
          cores: widget.cores,
          filtro: filtro,
          logico: logico,
          escalaRef: widget.escalaRef,
          tempo: widget.tempo,
          orcamentoDeAmostras: widget.orcamentoDeAmostras,
        );
    final ultimo = widget.passadas - 1;
    final assinatura = [
      ...widget.valores,
      for (final c in widget.cores) ...[c.r, c.g, c.b, c.a],
      widget.escalaRef,
      if (widget.usaTempo) widget.tempo,
      // O orcamento muda o desenho: trocar de qualidade tem de refazer a
      // camada, senao o preview ficava com o numero de amostras da
      // exportacao (ou o contrario) depois de dar play/pause.
      if (widget.orcamentoDeAmostras != null) widget.orcamentoDeAmostras!,
    ];
    if (ui.ImageFilter.isShaderFilterSupported) {
      return FiltroDeShader(
        shader: _shaders[ultimo],
        antes: _shaders.sublist(0, ultimo),
        configurarAntes: (i, s, logico) => configurar(i, s, logico, true),
        ativo: true,
        assinatura: assinatura,
        configurar: (s, logico) => configurar(ultimo, s, logico, true),
        child: widget.child,
      );
    }
    return FxSnapshot(
      painter: _PintorDePassadas(
        _shaders.sublist(0, widget.passadas),
        (i, s, logico) => configurar(i, s, logico, false),
      ),
      child: widget.child,
    );
  }
}

/// Sem filtro de shader: cada passada desenha numa imagem e a proxima le.
class _PintorDePassadas extends SnapshotPainter {
  _PintorDePassadas(this.shaders, this.configurar);

  final List<ui.FragmentShader> shaders;
  final void Function(int modo, ui.FragmentShader s, Size logico) configurar;

  @override
  void paint(
    PaintingContext context,
    Offset offset,
    Size size,
    PaintingContextCallback painter,
  ) => painter(context, offset);

  @override
  void paintSnapshot(
    PaintingContext context,
    Offset offset,
    Size size,
    ui.Image image,
    Size sourceSize,
    double pixelRatio,
  ) {
    if (size.isEmpty || image.width == 0 || image.height == 0) return;
    final w = image.width.toDouble(), h = image.height.toDouble();
    var entrada = image;
    final temporarias = <ui.Image>[];
    for (var i = 0; i < shaders.length; i++) {
      final s = shaders[i];
      configurar(i, s, size);
      s
        ..setFloat(0, w)
        ..setFloat(1, h)
        ..setImageSampler(0, entrada);
      if (i == shaders.length - 1) {
        final canvas = context.canvas;
        canvas.save();
        canvas.translate(offset.dx, offset.dy);
        canvas.scale(size.width / w, size.height / h);
        canvas.drawRect(Rect.fromLTWH(0, 0, w, h), Paint()..shader = s);
        canvas.restore();
      } else {
        final rec = ui.PictureRecorder();
        Canvas(rec).drawRect(
          Rect.fromLTWH(0, 0, w, h),
          Paint()
            ..blendMode = BlendMode.src
            ..shader = s,
        );
        final foto = rec.endRecording();
        entrada = foto.toImageSync(image.width, image.height);
        foto.dispose();
        temporarias.add(entrada);
      }
    }
    for (final t in temporarias) {
      t.dispose();
    }
  }

  @override
  bool shouldRepaint(covariant _PintorDePassadas old) => true;
}

typedef ConfigurarShader =
    void Function(ui.FragmentShader shader, Size tamanhoLogico);

/// FILTRO DE SHADER QUE CONHECE O PROPRIO TAMANHO.
///
/// E o `ImageFiltered` do Flutter com uma diferenca: os uniformes sao
/// escritos na hora de montar a camada, quando o tamanho da caixa ja e
/// conhecido. O Unsharp Mask precisa dele para saber quantos texels a GPU
/// usou por pixel; sem isso o raio mudaria com o zoom da previa.
class FiltroDeShader extends SingleChildRenderObjectWidget {
  const FiltroDeShader({
    super.key,
    required this.shader,
    required this.configurar,
    required this.assinatura,
    required this.ativo,
    this.antes = const [],
    this.configurarAntes,
    super.child,
  });

  final ui.FragmentShader shader;

  /// PASSADAS ANTERIORES, na ordem em que rodam (o JpegDamage codifica e
  /// so depois decodifica). Compostas num filtro so.
  final List<ui.FragmentShader> antes;
  final void Function(int indice, ui.FragmentShader shader, Size logico)?
  configurarAntes;
  final ConfigurarShader configurar;

  /// Os numeros que mudam o desenho: so quando mudam a camada e refeita.
  final List<double> assinatura;

  final bool ativo;

  @override
  RenderFiltroDeShader createRenderObject(BuildContext context) =>
      RenderFiltroDeShader(
          shader: shader,
          configurar: configurar,
          assinatura: assinatura,
          ativo: ativo,
        )
        ..antes = antes
        ..configurarAntes = configurarAntes;

  @override
  void updateRenderObject(
    BuildContext context,
    RenderFiltroDeShader renderObject,
  ) {
    renderObject
      ..shader = shader
      ..configurar = configurar
      ..antes = antes
      ..configurarAntes = configurarAntes
      ..assinatura = assinatura
      ..ativo = ativo;
  }
}

class RenderFiltroDeShader extends RenderProxyBox {
  RenderFiltroDeShader({
    required this._shader,
    required this.configurar,
    required this._assinatura,
    required this._ativo,
  });

  ConfigurarShader configurar;
  List<ui.FragmentShader> antes = const [];
  void Function(int indice, ui.FragmentShader shader, Size logico)?
  configurarAntes;

  ui.FragmentShader get shader => _shader;
  ui.FragmentShader _shader;
  set shader(ui.FragmentShader valor) {
    if (identical(valor, _shader)) return;
    _shader = valor;
    markNeedsCompositedLayerUpdate();
  }

  List<double> _assinatura;
  set assinatura(List<double> valor) {
    if (listEquals(valor, _assinatura)) return;
    _assinatura = valor;
    markNeedsCompositedLayerUpdate();
  }

  bool get ativo => _ativo;
  bool _ativo;
  set ativo(bool valor) {
    if (valor == _ativo) return;
    final eraFronteira = isRepaintBoundary;
    _ativo = valor;
    if (isRepaintBoundary != eraFronteira) markNeedsCompositingBitsUpdate();
    markNeedsPaint();
  }

  @override
  bool get alwaysNeedsCompositing => child != null && _ativo;

  @override
  bool get isRepaintBoundary => alwaysNeedsCompositing;

  @override
  OffsetLayer updateCompositedLayer({
    required covariant ImageFilterLayer? oldLayer,
  }) {
    configurar(_shader, size);
    final camada = oldLayer ?? ImageFilterLayer();
    // Um filtro novo a cada montagem: o nativo copia os uniformes na hora
    // em que nasce, entao reaproveitar o velho desenharia numeros velhos.
    ui.ImageFilter? interno;
    for (var i = 0; i < antes.length; i++) {
      configurarAntes?.call(i, antes[i], size);
      final f = ui.ImageFilter.shader(antes[i]);
      interno = interno == null
          ? f
          : ui.ImageFilter.compose(outer: f, inner: interno);
    }
    final externo = ui.ImageFilter.shader(_shader);
    camada.imageFilter = interno == null
        ? externo
        : ui.ImageFilter.compose(outer: externo, inner: interno);
    return camada;
  }
}

/// Sem filtro de shader (Skia): fotografa a camada e desenha com o shader.
class _PintorDeShader extends SnapshotPainter {
  _PintorDeShader(this.shader, this.assinatura, this.configurar);

  final ui.FragmentShader shader;
  final List<double> assinatura;
  final ConfigurarShader configurar;

  @override
  void paint(
    PaintingContext context,
    Offset offset,
    Size size,
    PaintingContextCallback painter,
  ) => painter(context, offset);

  @override
  void paintSnapshot(
    PaintingContext context,
    Offset offset,
    Size size,
    ui.Image image,
    Size sourceSize,
    double pixelRatio,
  ) {
    if (size.isEmpty || image.width == 0 || image.height == 0) return;
    configurar(shader, size);
    shader
      ..setFloat(0, image.width.toDouble())
      ..setFloat(1, image.height.toDouble())
      ..setImageSampler(0, image);
    final canvas = context.canvas;
    canvas.save();
    canvas.translate(offset.dx, offset.dy);
    canvas.scale(size.width / image.width, size.height / image.height);
    canvas.drawRect(
      Rect.fromLTWH(0, 0, image.width.toDouble(), image.height.toDouble()),
      Paint()..shader = shader,
    );
    canvas.restore();
  }

  /// Sempre: a foto e refeita a cada reconstrucao (o conteudo pode ter
  /// mudado mesmo com os mesmos numeros).
  @override
  bool shouldRepaint(covariant _PintorDeShader old) => true;
}
