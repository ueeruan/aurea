import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import '../../domain/correcao_de_cor.dart';
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
  static Future<void>? _carregando;

  /// O erro de carga, para o teste e o log dizerem o que houve.
  static String? falha;

  static bool get corPronta => _cor != null;
  static bool get nitidezPronta => _nitidez != null;

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
    super.child,
  });

  final ui.FragmentShader shader;
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
      );

  @override
  void updateRenderObject(
    BuildContext context,
    RenderFiltroDeShader renderObject,
  ) {
    renderObject
      ..shader = shader
      ..configurar = configurar
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
    camada.imageFilter = ui.ImageFilter.shader(_shader);
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
