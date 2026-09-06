import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import '../../domain/blend_extra.dart';

/// COMPOSITOR DE DOIS ANDARES.
///
/// Os modos de mescla que o Flutter nao tem precisam das DUAS imagens ao
/// mesmo tempo — o que ja estava embaixo e a camada. `saveLayer` com
/// `BlendMode` resolve os 17 nativos porque a conta acontece dentro do
/// motor; para Linear Burn ou Vivid Light a conta e nossa, e um shader
/// so ve o que a gente entrega.
///
/// Entao aqui a pilha se parte: tudo o que vinha antes desta camada vira
/// [base], a camada vira [top], e as duas sao desenhadas em imagens do
/// tamanho da composicao antes de ir para a GPU. Custa duas passagens
/// fora da tela por camada com mescla propria — por isso so acontece
/// quando a pessoa escolhe um desses modos.
class CustomBlendBox extends MultiChildRenderObjectWidget {
  CustomBlendBox({
    super.key,
    required this.mode,
    required this.seed,
    required Widget base,
    required Widget top,
  }) : super(children: [base, top]);

  final AureaBlend mode;

  /// O Dissolve sorteia por pixel; sem semente por quadro o granulado
  /// congela e vira textura fixa.
  final double seed;

  /// O programa e carregado uma vez e compartilhado.
  static ui.FragmentProgram? _program;
  static bool _tried = false;

  static Future<void> warmUp() async {
    if (_tried) return;
    _tried = true;
    try {
      _program = await ui.FragmentProgram.fromAsset('shaders/blend.frag');
    } catch (_) {
      // Aparelho sem suporte: a camada continua aparecendo, so que
      // empilhada normal. Nunca some por causa do modo.
      _program = null;
    }
  }

  static bool get ready => _program != null;

  @override
  RenderObject createRenderObject(BuildContext context) =>
      RenderCustomBlend(
        mode: mode,
        seed: seed,
        pixelRatio: MediaQuery.maybeDevicePixelRatioOf(context) ?? 1.0,
        program: _program,
      );

  @override
  void updateRenderObject(
      BuildContext context, covariant RenderCustomBlend renderObject) {
    renderObject
      ..mode = mode
      ..seed = seed
      ..pixelRatio = MediaQuery.maybeDevicePixelRatioOf(context) ?? 1.0
      ..program = _program;
  }
}

class _BlendParentData extends ContainerBoxParentData<RenderBox> {}

// ignore_for_file: prefer_initializing_formals

class RenderCustomBlend extends RenderBox
    with
        ContainerRenderObjectMixin<RenderBox, _BlendParentData>,
        RenderBoxContainerDefaultsMixin<RenderBox, _BlendParentData> {
  RenderCustomBlend({
    required AureaBlend mode,
    required double seed,
    required double pixelRatio,
    required ui.FragmentProgram? program,
  })  : _mode = mode,
        _seed = seed,
        _pixelRatio = pixelRatio,
        _program = program;

  AureaBlend _mode;
  set mode(AureaBlend v) {
    if (_mode == v) return;
    _mode = v;
    markNeedsPaint();
  }

  double _seed;
  set seed(double v) {
    if (_seed == v) return;
    _seed = v;
    if (_mode == AureaBlend.dissolve) markNeedsPaint();
  }

  double _pixelRatio;
  set pixelRatio(double v) {
    if (_pixelRatio == v) return;
    _pixelRatio = v;
    markNeedsPaint();
  }

  ui.FragmentProgram? _program;
  set program(ui.FragmentProgram? v) {
    if (identical(_program, v)) return;
    _program = v;
    markNeedsPaint();
  }

  @override
  void setupParentData(RenderObject child) {
    if (child.parentData is! _BlendParentData) {
      child.parentData = _BlendParentData();
    }
  }

  @override
  void performLayout() {
    // Os dois andares ocupam a composicao inteira: e o que garante que
    // as duas imagens tenham pixel a pixel a mesma origem.
    final vao = BoxConstraints.tight(constraints.biggest.isFinite
        ? constraints.biggest
        : const Size(1, 1));
    var child = firstChild;
    while (child != null) {
      child.layout(vao);
      child = childAfter(child);
    }
    size = constraints.biggest.isFinite
        ? constraints.biggest
        : const Size(1, 1);
  }

  @override
  bool hitTestChildren(BoxHitTestResult result, {required Offset position}) =>
      defaultHitTestChildren(result, position: position);

  /// Desenha [child] fora da tela e devolve o resultado como imagem.
  ui.Image _paraImagem(RenderBox child, Size tamanho) {
    final camada = OffsetLayer();
    final ctx = PaintingContext(camada, Offset.zero & tamanho);
    ctx.paintChild(child, Offset.zero);
    // Fechar a gravacao e o que coloca a imagem na arvore da camada. O
    // proprio SnapshotWidget do Flutter faz exatamente isto aqui.
    // ignore: invalid_use_of_protected_member
    ctx.stopRecordingIfNeeded();
    final img = camada.toImageSync(Offset.zero & tamanho,
        pixelRatio: _pixelRatio);
    camada.dispose();
    return img;
  }

  @override
  void paint(PaintingContext context, Offset offset) {
    final base = firstChild;
    final top = base == null ? null : childAfter(base);
    final program = _program;

    if (base == null || top == null) return;

    // Sem shader (aparelho sem suporte, ou ainda carregando): empilha
    // normal. Melhor ver a camada com mescla errada do que nao ver.
    if (program == null || size.isEmpty) {
      context.paintChild(base, offset);
      context.paintChild(top, offset);
      return;
    }

    final imgBase = _paraImagem(base, size);
    final imgTop = _paraImagem(top, size);

    final shader = program.fragmentShader()
      ..setFloat(0, size.width)
      ..setFloat(1, size.height)
      ..setFloat(2, _mode.index.toDouble())
      ..setFloat(3, _seed)
      ..setImageSampler(0, imgBase)
      ..setImageSampler(1, imgTop);

    final canvas = context.canvas;
    canvas.save();
    canvas.translate(offset.dx, offset.dy);
    canvas.drawRect(Offset.zero & size, Paint()..shader = shader);
    canvas.restore();

    shader.dispose();
    imgBase.dispose();
    imgTop.dispose();
  }
}
