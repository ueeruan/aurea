import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';
import 'package:flutter/rendering.dart';

import '../../domain/effect.dart';
import '../../domain/sombra_longa.dart';
import '../../domain/sombra_projetada.dart';
import 'fx_lote2.dart';

/// A SOMBRA LONGA — um shader de filtro que marcha, e a camada por cima.
///
/// ==========================================================================
/// POR QUE AQUI E SHADER, E A SOMBRA PROJETADA NAO E
/// ==========================================================================
///
/// As duas parecem o mesmo problema e nao sao. A sombra projetada e UMA
/// copia deslocada: cabe em `ColorFiltered` + `ImageFiltered` + `Transform`,
/// com o desfoque do proprio motor, e sair da arvore de widgets para um
/// shader so teria custo.
///
/// A sombra longa e a UNIAO de centenas de copias, uma por passo da
/// marcha. Em arvore de widgets isso seria um `saveLayer` por copia por
/// quadro — o caminho que ja congelou o palco neste projeto. No shader e
/// uma leitura de textura por passo, e o passo e o unico numero que o
/// custo obedece ([kTetoDePassosDaSombraLonga]).
///
/// ==========================================================================
/// A CAIXA
/// ==========================================================================
///
/// A sombra sai da camada, entao a caixa abre — e abre por LADO, com a
/// MESMA conta da sombra projetada (`margemDaSombraLonga`). A camada fica
/// posicionada dentro da caixa aberta, e o filtro recebe a caixa inteira:
/// por isso o shader nao precisa saber onde a camada comeca, o alfa em
/// volta ja e transparente.
///
/// A ORDEM E: shader (a sombra), depois o desfoque, e a camada por cima.
/// Assim o desfoque acontece na sombra e nunca borra a camada.
class SombraLongaPass extends StatefulWidget {
  const SombraLongaPass({
    super.key,
    required this.effect,
    required this.time,
    required this.child,
  });

  final EffectInstance effect;
  final Duration time;
  final Widget child;

  @override
  State<SombraLongaPass> createState() => _SombraLongaPassState();
}

class _SombraLongaPassState extends State<SombraLongaPass> {
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
    (_program ??= ui.FragmentProgram.fromAsset('shaders/sombra_longa.frag'))
        .then((p) {
          if (mounted) setState(() => shader = p.fragmentShader());
        })
        .catchError((Object e) {
          debugPrint('Sombra longa: $e');
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

    final distancia = p('distancia');
    final direcao = p('direcao');
    final suavidade = p('suavidade');
    final opacidade = (p('opacidade') / 100).clamp(0.0, 1.0);
    final queda = (p('queda') / 100).clamp(0.0, 1.0);

    final cor = widget.effect.color;
    final alfa = (cor.a * opacidade).clamp(0.0, 1.0);

    // IDENTIDADE: sem comprimento nao ha sombra, e sem alfa nao ha o que
    // desenhar. Nos dois casos passar por aqui custaria a camada desenhada
    // duas vezes por quadro para nao mudar um pixel.
    if (distancia <= 0 || alfa <= 0.0001) return widget.child;

    final marcha = marchaDaSombraLonga(distancia: distancia);
    // A DIRECAO E A MESMA DA SOMBRA PROJETADA: o vetor unitario e o
    // deslocamento de 1 px, e cada passo da marcha e esse vetor vezes o
    // passo. Assim 135 graus continua sendo "a luz em cima a esquerda".
    final unidade = deslocamentoDaSombra(distancia: 1, direcao: direcao);
    final margem = margemDaSombraLonga(
      distancia: distancia,
      direcao: direcao,
      suavidade: suavidade,
    );
    if (margem.vazia) return widget.child;

    final caixa = Size(
      medida.width + margem.largura,
      medida.height + margem.altura,
    );

    shader!
      ..setFloat(2, caixa.width)
      ..setFloat(3, caixa.height)
      ..setFloat(4, unidade.dx * marcha.passo)
      ..setFloat(5, unidade.dy * marcha.passo)
      ..setFloat(6, marcha.passos.toDouble())
      ..setFloat(7, cor.r)
      ..setFloat(8, cor.g)
      ..setFloat(9, cor.b)
      ..setFloat(10, alfa)
      ..setFloat(11, queda);

    Widget camada = Stack(
      fit: StackFit.expand,
      children: [
        Positioned(
          left: margem.esquerda,
          top: margem.topo,
          width: medida.width,
          height: medida.height,
          child: widget.child,
        ),
      ],
    );

    final sigma = sigmaDaSombra(suavidade);

    if (ui.ImageFilter.isShaderFilterSupported) {
      Widget sombra = ImageFiltered(
        imageFilter: ui.ImageFilter.shader(shader!),
        child: camada,
      );
      if (sigma > 0) {
        sombra = ImageFiltered(
          imageFilter: ui.ImageFilter.blur(sigmaX: sigma, sigmaY: sigma),
          child: sombra,
        );
      }
      return SizedBox(
        width: medida.width,
        height: medida.height,
        child: OverflowBox(
          minWidth: caixa.width,
          maxWidth: caixa.width,
          minHeight: caixa.height,
          maxHeight: caixa.height,
          alignment: _alinhamento(margem, medida),
          child: Stack(
            clipBehavior: Clip.none,
            children: [
              Positioned.fill(child: sombra),
              Positioned(
                left: margem.esquerda,
                top: margem.topo,
                width: medida.width,
                height: medida.height,
                child: widget.child,
              ),
            ],
          ),
        ),
      );
    }

    // RESERVA (aparelho sem `ImageFilter.shader`): a camada vira foto e o
    // shader pinta a sombra por baixo.
    return SizedBox(
      width: medida.width,
      height: medida.height,
      child: OverflowBox(
        minWidth: caixa.width,
        maxWidth: caixa.width,
        minHeight: caixa.height,
        maxHeight: caixa.height,
        alignment: _alinhamento(margem, medida),
        child: Stack(
          clipBehavior: Clip.none,
          children: [
            Positioned.fill(
              child: FxSnapshot(painter: _Marcha(shader!), child: camada),
            ),
            Positioned(
              left: margem.esquerda,
              top: margem.topo,
              width: medida.width,
              height: medida.height,
              child: widget.child,
            ),
          ],
        ),
      ),
    );
  }

}

Alignment _alinhamento(MargemDaSombra margem, Size lado) {
  final a = alinhamentoDaSombra(
    margem: margem,
    larguraDaCamada: lado.width,
    alturaDaCamada: lado.height,
  );
  return Alignment(a.x, a.y);
}

/// A RESERVA PINTA A MARCHA por cima da foto da camada, e so a sombra:
/// quem desenha a camada na reserva e o segundo filho do `Stack`.
class _Marcha extends SnapshotPainter {
  _Marcha(this.shader);
  final ui.FragmentShader shader;

  @override
  bool shouldRepaint(_Marcha old) => true;

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
    // O `uSize` AQUI E O LOGICO: desenhando por `canvas.drawRect`, o
    // `FlutterFragCoord` vem no espaco local do canvas, e nao em pixel de
    // aparelho — a mesma convencao das outras reservas do projeto.
    //
    // E O `offset` NAO E ENFEITE: sem o translate, o passe pinta a sombra
    // na ORIGEM do quadro em vez de na posicao da camada. O teste de
    // pixel pegou exatamente isso — a silhueta certa, na esquina errada.
    final shader = this.shader
      ..setFloat(0, size.width)
      ..setFloat(1, size.height)
      ..setImageSampler(0, image);
    context.canvas.save();
    context.canvas.translate(offset.dx, offset.dy);
    context.canvas.drawRect(
      ui.Rect.fromLTWH(0, 0, size.width, size.height),
      ui.Paint()..shader = shader,
    );
    context.canvas.restore();
  }
}

class _Medida extends SingleChildRenderObjectWidget {
  const _Medida({required this.onSize, required super.child});
  final ValueChanged<Size> onSize;
  @override
  RenderObject createRenderObject(BuildContext context) => _CaixaDeMedida(onSize);
  @override
  void updateRenderObject(BuildContext context, _CaixaDeMedida renderObject) {
    renderObject.onSize = onSize;
  }
}

class _CaixaDeMedida extends RenderProxyBox {
  _CaixaDeMedida(this.onSize);
  ValueChanged<Size> onSize;
  @override
  void performLayout() {
    super.performLayout();
    onSize(size);
  }
}
