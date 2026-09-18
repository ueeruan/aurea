import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';
import 'package:flutter/rendering.dart';

import '../../domain/effect.dart';
import 'fx_lote2.dart';

/// Enlarges the actual filter input, so output beyond the original layer is
/// retained. A shader samples repeated tiles in one pass, including live video
/// on Impeller, without thousands of widget copies or CPU pixel readback.
///
/// ==========================================================================
/// A REGIAO DA REPETICAO COBRE A COMPOSICAO, E NAO A CAIXA DA CAMADA.
/// ==========================================================================
///
/// O DEFEITO DO RELATO, e ele nao era a matematica do ladrilho — era ONDE a
/// repeticao acontecia.
///
/// O Motion Tile mora DENTRO do transform da camada (efeito age na fonte, e
/// o transform vem depois — a mesma ordem do After Effects). Entao a regiao
/// ladrilhada era a caixa da camada: com a camada em 50%, a parede de
/// ladrilhos saia junto, encolhida, e a composicao ficava com a moldura
/// vazia em volta. A repeticao estava certa; ela so nao cobria nada.
///
/// O CONSERTO: a regiao que se ladrilha e calculada para cobrir a
/// COMPOSICAO depois da escala. Com a camada em 50%, ela tem o dobro do
/// tamanho em espaco de camada — e, encolhida de volta, cobre o quadro
/// inteiro. A conta e exata, e nao um numero grande chutado: sobra
/// ladrilho o suficiente e nem um pixel a mais, porque cada pixel a mais e
/// area para o shader preencher por quadro.
///
/// A POSICAO ENTRA NA CONTA. Uma camada jogada para a direita nao precisa
/// de uma regiao simetrica: o lado esquerdo dela precisa alcancar a borda
/// esquerda do quadro, e o direito, a direita. Calcular pelos dois lados
/// evita tanto a faixa vazia quanto o desperdicio.
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
  });

  final EffectInstance effect;
  final Duration time;
  final Widget child;

  /// A ESCALA EFETIVA da camada no instante — a propria, mais a que veio
  /// do pai, mais a da camera. E ela que decide o quanto a regiao precisa
  /// crescer para o quadro continuar coberto.
  final double escalaX;
  final double escalaY;

  /// Onde a camada cai na composicao, e o tamanho da composicao. Nulos
  /// significam "nao sei": nesse caso a regiao volta a ser a da camada, e
  /// o comportamento e o de antes — melhor um ladrilho menor do que um
  /// chute errado.
  final Offset? posicao;
  final Size? composicao;

  @override
  State<MotionTilePass> createState() => _MotionTilePassState();
}

class _MotionTilePassState extends State<MotionTilePass> {
  static Future<ui.FragmentProgram>? _program;
  ui.FragmentShader? shader;
  Size? sourceSize;
  void measure(Size size) {
    if (size == sourceSize || size.isEmpty || !size.isFinite) return;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted && sourceSize != size) setState(() => sourceSize = size);
    });
  }

  @override
  void initState() {
    super.initState();
    (_program ??= ui.FragmentProgram.fromAsset('shaders/motion_tile.frag'))
        .then((p) {
          if (mounted) setState(() => shader = p.fragmentShader());
        })
        .catchError((Object e) {
          debugPrint('Motion Tile shader: $e');
        });
  }

  @override
  void dispose() {
    shader?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, constraints) {
      if (shader == null || sourceSize == null) {
        return _Measure(onSize: measure, child: widget.child);
      }
      double p(String key) => widget.effect.paramAt(key, widget.time);
      final size = sourceSize!;
      // A REGIAO DA SAIDA: o que a pessoa pediu, ou o que for preciso para
      // cobrir a composicao — o maior dos dois.
      final fw = fatorQueCobreMotionTile(
        pedido: (p('output_width') / 100).clamp(0.01, 6.0),
        ladoDaCamada: size.width,
        escala: widget.escalaX,
        posicao: widget.posicao?.dx,
        ladoDaComposicao: widget.composicao?.width,
      );
      final fh = fatorQueCobreMotionTile(
        pedido: (p('output_height') / 100).clamp(0.01, 6.0),
        ladoDaCamada: size.height,
        escala: widget.escalaY,
        posicao: widget.posicao?.dy,
        ladoDaComposicao: widget.composicao?.height,
      );
      // A ENTRADA DO FILTRO tem de conter a FONTE INTEIRA, mesmo quando a
      // saida e um recorte (<100%). Por isso ela nunca e menor que 1.
      final gw = fw < 1 ? 1.0 : fw, gh = fh < 1 ? 1.0 : fh;
      final filter = ui.ImageFilter.isShaderFilterSupported;
      // uOutput E O FATOR DA ENTRADA, e nao o da saida: o shader mapeia a
      // entrada ampliada de volta para o espaco da fonte usando este
      // numero. Trocar por `fw` faria a amostragem escorregar sempre que a
      // saida fosse um recorte.
      shader!
        ..setFloat(2, gw)
        ..setFloat(3, gh)
        ..setFloat(4, (p('tile_width') / 100).clamp(0.01, 3.0))
        ..setFloat(5, (p('tile_height') / 100).clamp(0.01, 3.0))
        ..setFloat(6, p('tile_center'))
        ..setFloat(7, p('tile_center_y'))
        ..setFloat(8, p('mirror_edges'))
        ..setFloat(9, p('phase') / 360)
        ..setFloat(10, p('horizontal_phase_shift'))
        ..setFloat(11, filter ? 1 : 0);
      final source = SizedBox(
        width: size.width * gw,
        height: size.height * gh,
        child: Stack(
          children: [
            Positioned.fill(child: CustomPaint(painter: _BoundsPainter())),
            Center(
              child: UnconstrainedBox(
                child: SizedBox(
                  width: size.width,
                  height: size.height,
                  child: widget.child,
                ),
              ),
            ),
          ],
        ),
      );
      final rendered = filter
          ? ImageFiltered(
              imageFilter: ui.ImageFilter.shader(shader!),
              child: source,
            )
          : FxSnapshot(painter: _TileSnapshot(shader!), child: source);
      return SizedBox(
        width: size.width,
        height: size.height,
        child: OverflowBox(
          minWidth: size.width * fw,
          maxWidth: size.width * fw,
          minHeight: size.height * fh,
          maxHeight: size.height * fh,
          child: ClipRect(
            child: OverflowBox(
              minWidth: size.width * gw,
              maxWidth: size.width * gw,
              minHeight: size.height * gh,
              maxHeight: size.height * gh,
              child: rendered,
            ),
          ),
        ),
      );
    },
  );
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
  final preciso = 2 * math.max(posicao, ladoDaComposicao - posicao) /
      (escala * ladoDaCamada);
  if (!preciso.isFinite || preciso <= 0) return pedido;
  // O TETO EXISTE porque cada fator a mais e area por quadro. Seis vezes a
  // camada ja cobre uma composicao com a camada em 1/6 do quadro — abaixo
  // disso nao ha o que salvar sem custo desproporcional.
  return math.max(pedido, preciso).clamp(0.01, 24.0);
}

class _BoundsPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(
      Offset.zero & size,
      Paint()..color = const Color(0x00000000),
    );
  }

  @override
  bool shouldRepaint(_BoundsPainter old) => false;
}

class _TileSnapshot extends SnapshotPainter {
  _TileSnapshot(this.shader);
  final ui.FragmentShader shader;
  @override
  bool shouldRepaint(_TileSnapshot old) => true;
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
    shader
      ..setFloat(0, size.width)
      ..setFloat(1, size.height)
      ..setImageSampler(0, image);
    context.canvas.save();
    context.canvas.translate(offset.dx, offset.dy);
    context.canvas.drawRect(Offset.zero & size, Paint()..shader = shader);
    context.canvas.restore();
  }
}

class _Measure extends SingleChildRenderObjectWidget {
  const _Measure({required this.onSize, required super.child});
  final ValueChanged<Size> onSize;
  @override
  RenderObject createRenderObject(BuildContext context) => _MeasureBox(onSize);
  @override
  void updateRenderObject(BuildContext context, _MeasureBox renderObject) {
    renderObject.onSize = onSize;
  }
}

class _MeasureBox extends RenderProxyBox {
  _MeasureBox(this.onSize);
  ValueChanged<Size> onSize;
  @override
  void performLayout() {
    super.performLayout();
    onSize(size);
  }
}
