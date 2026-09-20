import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import '../../domain/effect.dart';

/// One source-sized capture, then UV wrapping over the expanded output.
/// The output is drawn directly: no giant transparent input surface whose
/// bounds an ImageFilter can collapse back to the original video texture.
class MotionTilePass extends StatefulWidget {
  const MotionTilePass({
    super.key,
    required this.effect,
    required this.time,
    required this.child,
    this.escalaX = 1,
    this.escalaY = 1,
    this.posicao,
    this.composicao,
    this.rotacaoGraus = 0,
  });
  final EffectInstance effect;
  final Duration time;
  final Widget child;
  final double escalaX, escalaY, rotacaoGraus;
  final Offset? posicao;
  final Size? composicao;
  static Future<ui.FragmentProgram>? _program;
  static ui.FragmentProgram? _loaded;
  static Future<ui.FragmentProgram> warmUp() =>
      _program ??= ui.FragmentProgram.fromAsset('shaders/motion_tile.frag')
          .then((p) => _loaded = p);
  @override
  State<MotionTilePass> createState() => _MotionTilePassState();
}

class _MotionTilePassState extends State<MotionTilePass> {
  ui.FragmentShader? _shader;
  @override
  void initState() {
    super.initState();
    final loaded = MotionTilePass._loaded;
    if (loaded != null) {
      _shader = loaded.fragmentShader();
      return;
    }
    MotionTilePass.warmUp().then((p) {
      if (mounted) setState(() => _shader = p.fragmentShader());
    });
  }

  @override
  void dispose() {
    _shader?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => _TileOutput(
    key: const ValueKey('motion-tile-viewport'),
    config: widget,
    shader: _shader,
    pixelRatio: MediaQuery.maybeDevicePixelRatioOf(context) ?? 1,
    child: widget.child,
  );
}

class _TileOutput extends SingleChildRenderObjectWidget {
  const _TileOutput({
    super.key,
    required this.config,
    required this.shader,
    required this.pixelRatio,
    required super.child,
  });
  final MotionTilePass config;
  final ui.FragmentShader? shader;
  final double pixelRatio;
  @override
  RenderObject createRenderObject(BuildContext context) =>
      _TileRender(config, shader, pixelRatio);
  @override
  void updateRenderObject(BuildContext context, _TileRender renderObject) {
    renderObject.config = config;
    renderObject.shader = shader;
    renderObject.pixelRatio = pixelRatio;
    renderObject.markNeedsLayout();
    renderObject.markNeedsPaint();
  }
}

class _TileRender extends RenderProxyBox {
  _TileRender(this.config, this.shader, this.pixelRatio);
  MotionTilePass config;
  ui.FragmentShader? shader;
  double pixelRatio;
  double _p(String key) => config.effect.paramAt(key, config.time);
  @override
  void performLayout() {
    child?.layout(const BoxConstraints(), parentUsesSize: true);
    final source = child?.size ?? Size.zero;
    if (source.isEmpty) {
      size = constraints.smallest;
      return;
    }
    final coverage = fatoresQueCobremMotionTile(
      pedidoX: _p('output_width') / 100,
      pedidoY: _p('output_height') / 100,
      ladoDaCamada: source,
      escalaX: config.escalaX,
      escalaY: config.escalaY,
      posicao: config.posicao,
      composicao: config.composicao,
      rotacaoGraus: config.rotacaoGraus,
    );
    size = constraints.constrain(
      Size(source.width * coverage.x, source.height * coverage.y),
    );
  }

  @override
  void paint(PaintingContext context, Offset offset) {
    final input = child;
    final fx = shader;
    if (input == null || input.size.isEmpty || size.isEmpty) return;
    if (fx == null) {
      context.paintChild(
        input,
        offset +
            Offset(
              (size.width - input.size.width) / 2,
              (size.height - input.size.height) / 2,
            ),
      );
      return;
    }
    final bounds = Offset.zero & input.size;
    // Only the source is rasterized. Scaling down adds tiles without
    // increasing source texture dimensions or multiplying CPU bitmaps.
    final ratio = math
        .min(
          pixelRatio,
          math.sqrt(2e6 / (input.size.width * input.size.height)),
        )
        .clamp(.05, 3.0);
    final layer = OffsetLayer();
    final capture = PaintingContext(layer, bounds);
    capture.paintChild(input, Offset.zero);
    // ignore: invalid_use_of_protected_member
    capture.stopRecordingIfNeeded();
    final image = layer.toImageSync(bounds, pixelRatio: ratio);
    layer.dispose();
    fx
      ..setFloat(0, size.width)
      ..setFloat(1, size.height)
      ..setFloat(2, size.width / input.size.width)
      ..setFloat(3, size.height / input.size.height)
      ..setFloat(4, (_p('tile_width') / 100).clamp(.01, 3))
      ..setFloat(5, (_p('tile_height') / 100).clamp(.01, 3))
      ..setFloat(6, _p('tile_center'))
      ..setFloat(7, _p('tile_center_y'))
      ..setFloat(8, _p('mirror_edges'))
      ..setFloat(9, _p('phase') / 360)
      ..setFloat(10, _p('horizontal_phase_shift'))
      ..setFloat(11, 0)
      ..setFloat(12, _p('clamp_edges'))
      ..setImageSampler(0, image);
    context.canvas.save();
    context.canvas.translate(offset.dx, offset.dy);
    context.canvas.drawRect(Offset.zero & size, Paint()..shader = fx);
    context.canvas.restore();
    image.dispose();
  }

  @override
  void applyPaintTransform(RenderBox child, Matrix4 transform) {}
}

/// O FATOR PELA LARGURA (ou altura) QUE FAZ A REPETICAO COBRIR A COMPOSICAO.
///
/// A CONTA, em uma linha: depois da escala, a camada ocupa
/// `[posicao - H*escala, posicao + H*escala]` na composicao, onde `H` e a
/// meia-extensao da regiao ladrilhada em espaco de camada. Para cobrir
/// `[0, lado]` do quadro, `H` tem de alcancar os DOIS lados:
///
///   H >= max(posicao, lado - posicao) / escala
///
/// e o fator e `2*H / ladoDaCamada`. O maior entre isso e o que a pessoa
/// pediu em "Largura da saida".
///
/// OS DOIS LADOS, e nao a distancia ao centro: uma camada encostada na
/// direita precisa de quase nada a mais do lado esquerdo, e de muito do
/// lado direito. Calcular pelo centro daria uma regiao simetrica cheia de
/// ladrilho que ninguem ve — e cada pixel a mais e area que o shader
/// preenche por quadro.
///
/// SEM POSICAO OU SEM COMPOSICAO, devolve o pedido: nao saber e motivo
/// para nao crescer, e nao para chutar.
double fatorQueCobreMotionTile({
  required double pedido,
  required double ladoDaCamada,
  required double escala,
  required double? posicao,
  required double? ladoDaComposicao,
}) {
  if (posicao == null || ladoDaComposicao == null) return pedido;
  if (!ladoDaCamada.isFinite || ladoDaCamada <= 0) return pedido;
  if (!escala.isFinite || escala <= 0.001) return pedido;
  if (!ladoDaComposicao.isFinite || ladoDaComposicao <= 0) return pedido;
  final preciso =
      2 *
      math.max(posicao, ladoDaComposicao - posicao) /
      (escala * ladoDaCamada);
  if (!preciso.isFinite || preciso <= 0) return pedido;
  // O TETO EXISTE porque cada fator a mais e area por quadro. Seis vezes a
  // camada ja cobre uma composicao com a camada em 1/6 do quadro — abaixo
  // disso nao ha o que salvar sem custo desproporcional.
  return math.max(pedido, preciso).clamp(0.01, 24.0);
}

/// ==========================================================================
/// A REGIAO QUE COBRE O QUADRO QUANDO A CAMADA ESTA GIRADA.
/// ==========================================================================
///
/// O defeito que este calculo fecha: girar a camada num efeito de ladrilho
/// deixava os QUATRO CANTOS com buraco. A conta anterior olhava so a escala
/// e a posicao, e por isso tratava a area coberta como um retangulo alinhado
/// aos eixos do quadro. A camada girada cobre um losango — e o losango nao
/// alcanca os cantos do retangulo.
///
/// A CONTA E A INVERSA DA TRANSFORMACAO, e nao uma aproximacao.
///
/// A camada vai ao quadro por `q = R(theta) * S(escala) * d + posicao`, onde
/// `d` e o deslocamento em espaco de camada. Para a regiao ladrilhada cobrir
/// TODO o quadro, basta que os QUATRO CANTOS do quadro caibam dentro dela.
/// Entao inverte-se a conta: para cada canto, `d = S^-1 * R(-theta) * (canto
/// - posicao)`, e a regiao tem de alcancar o maior `|d|` que aparecer.
///
/// SAO OS CANTOS, E NAO O CENTRO. Um canto e sempre o pior caso de uma
/// transformacao linear: a distancia maxima a origem de um retangulo esta
/// num vertice. Medir pelo centro subestimaria justamente a quina onde o
/// buraco aparece.
///
/// COM ROTACAO ZERO ISTO E A CONTA ANTIGA. `max(|0-p|, |w-p|)` e o mesmo que
/// `max(p, w-p)` — as duas formulas concordam na diagonal, e por isso a
/// funcao de um eixo so ([fatorQueCobreMotionTile]) continua valendo e
/// continua testada.
({double x, double y}) fatoresQueCobremMotionTile({
  required double pedidoX,
  required double pedidoY,
  required Size ladoDaCamada,
  required double escalaX,
  required double escalaY,
  required Offset? posicao,
  required Size? composicao,
  double rotacaoGraus = 0,
}) {
  final pedido = (x: pedidoX.clamp(0.01, 6.0), y: pedidoY.clamp(0.01, 6.0));
  if (!ladoDaCamada.width.isFinite || ladoDaCamada.width <= 0) return pedido;
  if (!ladoDaCamada.height.isFinite || ladoDaCamada.height <= 0) return pedido;
  if (!escalaX.isFinite || escalaX <= 0.001) return pedido;
  if (!escalaY.isFinite || escalaY <= 0.001) return pedido;

  double fator(double meio, double lado) {
    if (lado <= 0) return 0;
    final f = 2 * meio / lado;
    return f.isFinite && f > 0 ? f : 0;
  }

  double junto(double a, double b) => (a > b ? a : b).clamp(0.01, 24.0);

  // O PISO QUE NAO DEPENDE DE SABER ONDE A CAMADA CAI.
  //
  // Sem a posicao, a conta dos cantos nao existe — e o efeito ficava
  // ladrilhando SO A CAIXA DA CAMADA. Numa camada menor que o quadro
  // isso e a imagem pequena no meio do preto, que e o relato. Com o
  // tamanho do quadro conhecido, da para cobri-lo mesmo sem saber onde a
  // camada esta: a regiao tem de ser, no minimo, o quadro inteiro
  // dividido pela escala. E o caso do centro, que e o pior que a conta
  // dos cantos tambem cobre.
  if (composicao == null ||
      !composicao.width.isFinite ||
      !composicao.height.isFinite ||
      composicao.width <= 0 ||
      composicao.height <= 0) {
    return pedido;
  }
  final pisoX = composicao.width / (ladoDaCamada.width * escalaX);
  final pisoY = composicao.height / (ladoDaCamada.height * escalaY);
  if (posicao == null) {
    return (x: junto(pedido.x, pisoX), y: junto(pedido.y, pisoY));
  }
  final theta = (rotacaoGraus.isFinite ? rotacaoGraus : 0) * math.pi / 180;
  final cos = math.cos(theta), sen = math.sin(theta);
  var maxX = 0.0, maxY = 0.0;
  for (final canto in [
    Offset.zero,
    Offset(composicao.width, 0),
    Offset(0, composicao.height),
    Offset(composicao.width, composicao.height),
  ]) {
    final v = canto - posicao;
    // R(-theta) * v, e depois desfaz a escala: e a volta completa, em
    // espaco de camada.
    final dx = (v.dx * cos + v.dy * sen) / escalaX;
    final dy = (-v.dx * sen + v.dy * cos) / escalaY;
    if (dx.abs() > maxX) maxX = dx.abs();
    if (dy.abs() > maxY) maxY = dy.abs();
  }
  // O TETO E O MESMO DO CASO SEM GIRO, pelo mesmo motivo: cada fator a mais
  // e area que o shader preenche por quadro.
  return (
    x: junto(junto(pedido.x, pisoX), fator(maxX, ladoDaCamada.width)),
    y: junto(junto(pedido.y, pisoY), fator(maxY, ladoDaCamada.height)),
  );
}
