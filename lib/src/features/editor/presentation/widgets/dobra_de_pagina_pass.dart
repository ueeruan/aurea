import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';
import 'package:flutter/rendering.dart';

import '../../domain/dobra_de_pagina.dart';
import '../../domain/effect.dart';
import 'fx_lote2.dart';

/// A DOBRA DE PAGINA — um shader de filtro, uma passada, sem regiao extra.
///
/// A CAMADA NAO CRESCE, e essa e a diferenca dela para o Motion Tile e para
/// a sombra: a dobra so TIRA. O lado plano fica intacto, o lado que enrola
/// e comprimido para dentro, e o que passou do topo do rolo vira
/// transparente. Nenhum pixel sai da caixa da camada, entao nao ha regiao
/// para expandir nem segunda copia da camada por quadro.
///
/// O QUE O SHADER PRECISA SABER e so onde esta o vinco e para que lado ele
/// enrola — ver [normalDaDobra]. A conta do cilindro esta escrita por
/// extenso no `.frag`, e a mesma conta existe em Dart em [ondeAparece]
/// justamente para poder ser conferida sem GPU.
class DobraDePaginaPass extends StatefulWidget {
  const DobraDePaginaPass({
    super.key,
    required this.effect,
    required this.time,
    required this.child,
  });

  final EffectInstance effect;
  final Duration time;
  final Widget child;

  @override
  State<DobraDePaginaPass> createState() => _DobraDePaginaPassState();
}

class _DobraDePaginaPassState extends State<DobraDePaginaPass> {
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
    (_program ??= ui.FragmentProgram.fromAsset('shaders/dobra_de_pagina.frag'))
        .then((p) {
          if (mounted) setState(() => shader = p.fragmentShader());
        })
        .catchError((Object e) {
          debugPrint('Dobra de página: $e');
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
    final raio = p('raio');

    // IDENTIDADE: sem raio nao ha rolo nenhum. O shader tambem confere,
    // mas sair aqui evita a camada intermediaria inteira.
    if (raio <= 0.5) return widget.child;

    final normal = normalDaDobra(p('angulo'));
    final centro = Offset(
      p('posicao') * medida.width,
      p('posicao_y') * medida.height,
    );
    final cor = widget.effect.color;

    shader!
      ..setFloat(2, medida.width)
      ..setFloat(3, medida.height)
      ..setFloat(4, centro.dx)
      ..setFloat(5, centro.dy)
      ..setFloat(6, normal.dx)
      ..setFloat(7, normal.dy)
      ..setFloat(8, cor.r)
      ..setFloat(9, cor.g)
      ..setFloat(10, cor.b)
      ..setFloat(11, cor.a)
      ..setFloat(12, raio)
      ..setFloat(13, p('luz'))
      ..setFloat(14, (p('verso') / 100).clamp(0.0, 1.0))
      ..setFloat(15, (p('brilho') / 100).clamp(0.0, 3.0));

    if (ui.ImageFilter.isShaderFilterSupported) {
      return ImageFiltered(
        imageFilter: ui.ImageFilter.shader(shader!),
        child: widget.child,
      );
    }
    return FxSnapshot(painter: _Recorte(shader!), child: widget.child);
  }
}

/// O CAMINHO SEM `ImageFilter.shader` (aparelho antigo).
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

/// A ESPESSURA DO VINCO, em pixels logicos: raio pequeno deixa o vinco
/// quase reto, e um raio de menos de um pixel nao e uma dobra.
double raioEfetivo(double raio) {
  if (!raio.isFinite || raio <= 0.5) return 0;
  return math.min(raio, 4000);
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
