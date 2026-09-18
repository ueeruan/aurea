import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';
import 'package:flutter/rendering.dart';

import '../../domain/effect.dart';
import 'fx_lote2.dart';

/// A VARREDURA DE LUZ — um shader de filtro, e uma passada so.
///
/// A CONTA INTEIRA CABE NO SHADER porque a luz nao precisa de vizinhanca
/// nenhuma: cada pixel recebe a sua propria luz, calculada da distancia
/// dele ate a reta da faixa. Nao ha desfoque, nao ha amostra em volta, nao
/// ha segundo quadro. Isso a torna a mais barata do catalogo: uma leitura
/// de textura por pixel e uma soma.
///
/// O UNIFORME `uSize` E POSTO PELO PROPRIO `ImageFilter.shader`; os
/// numeros daqui comecam no indice 2. A ordem e a de DECLARACAO no
/// `.frag`, e nao o byte do std140 — trocar a ordem das declaracoes
/// desalinha tudo em silencio.
class LuzNaFaixaPass extends StatefulWidget {
  const LuzNaFaixaPass({
    super.key,
    required this.effect,
    required this.time,
    required this.child,
  });

  final EffectInstance effect;
  final Duration time;
  final Widget child;

  @override
  State<LuzNaFaixaPass> createState() => _LuzNaFaixaPassState();
}

class _LuzNaFaixaPassState extends State<LuzNaFaixaPass> {
  static Future<ui.FragmentProgram>? _program;
  ui.FragmentShader? shader;
  Size? lado;

  void medir(Size size) {
    if (size == lado || size.isEmpty || !size.isFinite) return;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted && lado != size) setState(() => lado = size);
    });
  }

  @override
  void initState() {
    super.initState();
    (_program ??= ui.FragmentProgram.fromAsset('shaders/luz_na_faixa.frag'))
        .then((p) {
          if (mounted) setState(() => shader = p.fragmentShader());
        })
        .catchError((Object e) {
          debugPrint('Varredura de luz: $e');
        });
  }

  @override
  void dispose() {
    shader?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final medida = lado;
    if (shader == null || medida == null) {
      return _Medida(onSize: medir, child: widget.child);
    }
    double p(String k) => widget.effect.paramAt(k, widget.time);
    final intensidade = (p('intensidade') / 100).clamp(0.0, 4.0);

    // IDENTIDADE: sem intensidade nao ha luz. Passar por aqui assim mesmo
    // custaria uma camada inteira por quadro para nao mudar um pixel.
    if (intensidade <= 0.001) return widget.child;

    final angulo = p('direcao') * math.pi / 180;
    final centro = Offset(
      p('centro') * medida.width,
      p('centro_y') * medida.height,
    );

    shader!
      ..setFloat(2, medida.width)
      ..setFloat(3, medida.height)
      ..setFloat(4, centro.dx)
      ..setFloat(5, centro.dy)
      ..setFloat(6, math.cos(angulo))
      ..setFloat(7, math.sin(angulo))
      ..setFloat(8, widget.effect.color.r)
      ..setFloat(9, widget.effect.color.g)
      ..setFloat(10, widget.effect.color.b)
      ..setFloat(11, widget.effect.color.a)
      ..setFloat(12, p('largura'))
      ..setFloat(13, intensidade)
      ..setFloat(14, p('recepcao'));

    if (ui.ImageFilter.isShaderFilterSupported) {
      return ImageFiltered(
        imageFilter: ui.ImageFilter.shader(shader!),
        child: widget.child,
      );
    }
    return FxSnapshot(
      painter: _Recorte(shader!),
      child: widget.child,
    );
  }
}

/// O CAMINHO SEM `ImageFilter.shader` (aparelho antigo): a camada vira uma
/// foto e o shader pinta por cima. Mais caro, e por isso e so a reserva.
class _Recorte extends SnapshotPainter {
  _Recorte(this.shader);
  final ui.FragmentShader shader;

  @override
  bool shouldRepaint(_Recorte old) => true;

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

class _Medida extends SingleChildRenderObjectWidget {
  const _Medida({required this.onSize, required super.child});
  final ValueChanged<Size> onSize;
  @override
  RenderObject createRenderObject(BuildContext context) => _Caixa(onSize);
  @override
  void updateRenderObject(BuildContext context, _Caixa renderObject) {
    renderObject.onSize = onSize;
  }
}

class _Caixa extends RenderProxyBox {
  _Caixa(this.onSize);
  ValueChanged<Size> onSize;
  @override
  void performLayout() {
    super.performLayout();
    onSize(size);
  }
}
