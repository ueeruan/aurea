import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import '../../domain/effect.dart';
import 'fx_lote2.dart';

const tiposDeRepeticao = [
  EffectType.repetirEmLinha,
  EffectType.repetirEmGrade,
  EffectType.repetirEmCirculo,
  EffectType.espalharCopias,
];

/// REPETIÇÃO: a camada aparece várias vezes, num passe só.
///
/// O alvo de render cresce o quanto as cópias precisam, e o shader
/// procura, para cada pixel, de qual cópia ele veio. É por isso que
/// funciona com vídeo ao vivo — uma cópia por widget custaria uma
/// árvore inteira por cópia.
class RepeticaoPass extends StatefulWidget {
  const RepeticaoPass({
    super.key,
    required this.effect,
    required this.time,
    required this.child,
  });

  final EffectInstance effect;
  final Duration time;
  final Widget child;

  @override
  State<RepeticaoPass> createState() => _RepeticaoPassState();
}

/// O quanto o alvo precisa crescer para as cópias caberem, em vezes o
/// tamanho da camada (e o teto que evita um alvo gigante).
({double largura, double altura}) folgaDaRepeticao(
  EffectInstance effect,
  Duration t,
) {
  double p(String k) => effect.paramAt(k, t);
  final copias = p('copias').clamp(1.0, 64.0);
  var x = 1.0, y = 1.0;
  switch (effect.type) {
    case EffectType.repetirEmLinha:
      x = 1 + (p('passo_x') / 100).abs() * (copias - 1) * 2;
      y = 1 + (p('passo_y') / 100).abs() * (copias - 1) * 2;
    case EffectType.repetirEmGrade:
      final colunas = p('colunas').clamp(1.0, 8.0);
      final linhas = p('linhas').clamp(1.0, 8.0);
      x = 1 + (p('passo_x') / 100).abs() * (colunas - 1);
      y = 1 + (p('passo_y') / 100).abs() * (linhas - 1);
    case EffectType.repetirEmCirculo:
    case EffectType.espalharCopias:
      final raio = (p('raio') / 100).abs() * 2;
      x = 1 + raio;
      y = 1 + raio;
    default:
      break;
  }
  return (largura: x.clamp(1.0, 6.0), altura: y.clamp(1.0, 6.0));
}

class _RepeticaoPassState extends State<RepeticaoPass> {
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
    (_program ??= ui.FragmentProgram.fromAsset('shaders/repeticao.frag'))
        .then((p) {
          if (mounted) setState(() => shader = p.fragmentShader());
        })
        .catchError((Object e) {
          debugPrint('Repeticao shader: $e');
        });
  }

  @override
  void dispose() {
    shader?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (shader == null || sourceSize == null) {
      return _Medida(onSize: measure, child: widget.child);
    }
    final size = sourceSize!;
    double p(String k) => widget.effect.paramAt(k, widget.time);
    final folga = folgaDaRepeticao(widget.effect, widget.time);
    final modo = switch (widget.effect.type) {
      EffectType.repetirEmLinha => 0.0,
      EffectType.repetirEmGrade => 1.0,
      EffectType.repetirEmCirculo => 2.0,
      _ => 3.0,
    };
    final grade = widget.effect.type == EffectType.repetirEmGrade;
    final colunas = grade ? p('colunas').clamp(1.0, 8.0) : 1.0;
    final linhas = grade ? p('linhas').clamp(1.0, 8.0) : 1.0;
    final copias = grade
        ? (colunas * linhas)
        : p('copias').clamp(1.0, 64.0).toDouble();
    final filter = ui.ImageFilter.isShaderFilterSupported;
    shader!
      ..setFloat(2, folga.largura)
      ..setFloat(3, folga.altura)
      ..setFloat(4, modo)
      ..setFloat(5, copias)
      ..setFloat(
        6,
        grade || widget.effect.type == EffectType.repetirEmLinha
            ? p('passo_x') / 100
            : 0,
      )
      ..setFloat(
        7,
        grade || widget.effect.type == EffectType.repetirEmLinha
            ? p('passo_y') / 100
            : 0,
      )
      ..setFloat(8, p('giro'))
      ..setFloat(9, p('escala') / 100)
      ..setFloat(10, p('opacidade') / 100)
      ..setFloat(11, modo >= 2 ? p('raio') / 100 : 0)
      ..setFloat(12, modo == 2 ? p('abertura') : 360)
      ..setFloat(13, modo == 2 ? p('orientacao') : 0)
      ..setFloat(14, modo == 3 ? p('semente') : 0)
      ..setFloat(15, colunas)
      ..setFloat(16, linhas)
      ..setFloat(
        17,
        size.height <= 0 ? 1 : math.max(.01, size.width / size.height),
      )
      ..setFloat(18, filter ? 1 : 0);

    final fonte = SizedBox(
      width: size.width * folga.largura,
      height: size.height * folga.altura,
      child: Stack(
        children: [
          Positioned.fill(child: CustomPaint(painter: _LimitePainter())),
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
    final desenhado = filter
        ? ImageFiltered(
            imageFilter: ui.ImageFilter.shader(shader!),
            child: fonte,
          )
        : FxSnapshot(painter: _RepeticaoSnapshot(shader!), child: fonte);
    return SizedBox(
      width: size.width,
      height: size.height,
      child: OverflowBox(
        minWidth: size.width * folga.largura,
        maxWidth: size.width * folga.largura,
        minHeight: size.height * folga.altura,
        maxHeight: size.height * folga.altura,
        child: desenhado,
      ),
    );
  }
}

class _LimitePainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) => canvas.drawRect(
    Offset.zero & size,
    Paint()..color = const Color(0x00000000),
  );

  @override
  bool shouldRepaint(_LimitePainter old) => false;
}

class _RepeticaoSnapshot extends SnapshotPainter {
  _RepeticaoSnapshot(this.shader);

  final ui.FragmentShader shader;

  @override
  bool shouldRepaint(_RepeticaoSnapshot old) => true;

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
    context.canvas
      ..save()
      ..translate(offset.dx, offset.dy)
      ..drawRect(Offset.zero & size, Paint()..shader = shader)
      ..restore();
  }
}

class _Medida extends SingleChildRenderObjectWidget {
  const _Medida({required this.onSize, required super.child});

  final ValueChanged<Size> onSize;

  @override
  RenderObject createRenderObject(BuildContext context) => _CaixaMedida(onSize);

  @override
  void updateRenderObject(BuildContext context, _CaixaMedida renderObject) {
    renderObject.onSize = onSize;
  }
}

class _CaixaMedida extends RenderProxyBox {
  _CaixaMedida(this.onSize);

  ValueChanged<Size> onSize;

  @override
  void performLayout() {
    super.performLayout();
    onSize(size);
  }
}
