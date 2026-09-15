// VECTOR MOTION DESIGN: a cor do preenchimento animavel, o Auto Morph
// com progresso keyframado, os morphs rapidos do retangulo, os presets
// de movimento e as ordens posicionais da cascata — tudo keyframe REAL.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/apple_motion.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/presets_de_movimento.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

({ProviderContainer c, EditorController e}) _motor() {
  final c = ProviderContainer();
  addTearDown(c.dispose);
  return (c: c, e: c.read(editorControllerProvider.notifier));
}

ShapeLayer _forma(ProviderContainer c) =>
    c.read(editorControllerProvider).layers.whereType<ShapeLayer>().first;

void main() {
  test('a cor do preenchimento anima por canal e faz ida e volta no JSON', () {
    final fill = ShapeFill(
      color: const Color(0xFFFF0000),
      corR: AnimatedDouble(255)
          .withKeyframe(Duration.zero, 255)
          .withKeyframe(const Duration(seconds: 1), 0),
      corB: AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(const Duration(seconds: 1), 255),
      opacity: AnimatedDouble(1)
          .withKeyframe(Duration.zero, 1)
          .withKeyframe(const Duration(seconds: 1), .25),
    );
    // No meio do caminho: vermelho e azul trocando, verde parado.
    final meio = fill.colorAt(const Duration(milliseconds: 500));
    expect((meio.r * 255).round(), closeTo(128, 2));
    expect((meio.b * 255).round(), closeTo(128, 2));
    expect((meio.g * 255).round(), 0);
    expect(
      fill.opacity.valueAt(const Duration(milliseconds: 500)),
      closeTo(.625, 1e-9),
    );

    final layer = ShapeLayer(
      name: 'f',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      contents: [
        ShapePath(primitive: ShapePrimitive.ellipse),
        fill,
      ],
    );
    final volta = projectFromJson(
      projectToJson(
        VideoProject(name: 'p', createdAt: DateTime(2026), layers: [layer]),
      ),
    ).layers.single as ShapeLayer;
    final fillVolta = volta.contents.whereType<ShapeFill>().single;
    expect(
      fillVolta
          .colorAt(const Duration(milliseconds: 500))
          .r,
      closeTo(meio.r, 1e-6),
    );
    expect(fillVolta.corG, isNull, reason: 'canal sem trilha nao vai ao arquivo');
  });

  test('arquivo ANTIGO de fill (opacidade como numero) continua abrindo', () {
    final layer = ShapeLayer(
      name: 'f',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      contents: [
        ShapePath(primitive: ShapePrimitive.ellipse),
        ShapeFill(color: const Color(0xFF00FF00)),
      ],
    );
    final json = projectToJson(
      VideoProject(name: 'p', createdAt: DateTime(2026), layers: [layer]),
    );
    // Reescreve o fill no formato velho: 'op' vira numero puro.
    final camadas = json['layers'] as List;
    for (final item in ((camadas[0] as Map)['contents'] as List)) {
      if ((item as Map)['kind'] == 'fill') item['op'] = 0.5;
    }
    final volta = projectFromJson(json).layers.single as ShapeLayer;
    final fill = volta.contents.whereType<ShapeFill>().single;
    expect(fill.opacity.valueAt(Duration.zero), 0.5);
    expect(fill.corAnimada, isFalse);
  });

  test('o losango de cor crava os tres canais e a edicao respeita a regra', () {
    final m = _motor();
    m.e.addShapeLayer(Duration.zero);
    final id = _forma(m.c).id;
    final fillId =
        _forma(m.c).contents.whereType<ShapeFill>().first.id;
    m.e.toggleShapeFillColorKeyframe(id, fillId, Duration.zero);
    var fill = _forma(m.c).contents.whereType<ShapeFill>().first;
    expect(fill.corAnimada, isTrue);
    expect(fill.corR!.hasKeyframeAt(Duration.zero), isTrue);
    // Editar SOBRE o keyframe muda o keyframe...
    m.e.setShapeFillColorAt(
      id,
      fillId,
      Duration.zero,
      const Color(0xFF102030),
    );
    fill = _forma(m.c).contents.whereType<ShapeFill>().first;
    expect((fill.colorAt(Duration.zero).b * 255).round(), 0x30);
    // ...e editar fora de keyframe nao inventa um (regra do app).
    final antes = fill.corR!.keyframes.length;
    m.e.setShapeFillColorAt(
      id,
      fillId,
      const Duration(seconds: 1),
      const Color(0xFFFFFFFF),
    );
    fill = _forma(m.c).contents.whereType<ShapeFill>().first;
    expect(fill.corR!.keyframes.length, antes);
  });

  test('auto morph vira ShapeMorph com progresso 0 -> 1 keyframado', () {
    final m = _motor();
    m.e.addShapeLayer(Duration.zero);
    final id = _forma(m.c).id;
    m.e.autoMorphPara(id, ShapePrimitive.star, Duration.zero);
    final morph = _forma(m.c).contents.whereType<ShapeMorph>().single;
    expect(morph.to.primitive, ShapePrimitive.star);
    expect(morph.progress.valueAt(Duration.zero), 0);
    expect(morph.progress.valueAt(const Duration(milliseconds: 500)), 1);
    // O registro de trilhas conhece o progresso (losango e curva).
    expect(
      EditorController.shapeItemTrack(morph, 'progress'),
      isNotNull,
    );
    // No meio, o caminho existe e nao e nenhum dos extremos.
    final meio = morph.build(const Duration(milliseconds: 250));
    expect(meio.getBounds().isEmpty, isFalse);
    // Um undo desfaz o morph inteiro (conversao + keyframes juntos).
    m.e.undo();
    expect(_forma(m.c).contents.whereType<ShapeMorph>(), isEmpty);
  });

  test('morph rapido: quadrado vira circulo sem deformar o canto', () {
    final m = _motor();
    m.e.addShapeLayer(Duration.zero);
    final id = _forma(m.c).id;
    // Garante um retangulo parametrico na forma.
    m.e.addCompoundShapeGeometry(id, ParamShapeKind.rect);
    m.e.morphRapidoDeForma(id, Duration.zero, FormaRapida.circulo);
    final rect = _forma(m.c)
        .contents
        .whereType<ShapeParametric>()
        .firstWhere((i) => i.kind == ParamShapeKind.rect);
    final fim = const Duration(milliseconds: 500);
    expect(rect.sizeX.valueAt(fim), rect.sizeY.valueAt(fim));
    expect(rect.roundness.valueAt(fim), 100);
    expect(rect.sizeX.keyframes.length, 2);
    expect(rect.sizeX.keyframes.first.ease.y1, isNot(0),
        reason: 'chegada com overshoot');
  });

  test('presets de movimento cravam keyframes editaveis', () {
    final m = _motor();
    m.e.addTextLayer(Duration.zero, text: 'Titulo');
    final id = m.c.read(editorControllerProvider).layers.first.id;
    m.e.aplicarPresetDeMovimento(id, Duration.zero, PresetDeMovimento.pop);
    final l = m.c.read(editorControllerProvider).layerById(id)!;
    expect(l.opacity.valueAt(Duration.zero), 0);
    expect(l.scaleX.valueAt(Duration.zero), closeTo(0.6, 1e-9));
    expect(
      l.scaleX.valueAt(const Duration(milliseconds: 450)),
      closeTo(1, 1e-9),
    );
    // Soco: incha 12% no pico e volta.
    m.e.undo();
    m.e.aplicarPresetDeMovimento(id, Duration.zero, PresetDeMovimento.soco);
    final soco = m.c.read(editorControllerProvider).layerById(id)!;
    expect(
      soco.scaleX.valueAt(const Duration(milliseconds: 130)),
      closeTo(1.12, 1e-9),
    );
    expect(
      soco.scaleX.valueAt(const Duration(milliseconds: 340)),
      closeTo(1, 1e-9),
    );
  });

  test('a cascata ganha ordens posicionais de verdade', () {
    Layer texto(String id, Offset pos) => TextLayer(
      id: id,
      name: id,
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      text: id,
      position: AnimatedOffset(
        pos,
        [Keyframe(time: Duration.zero, value: pos)],
      ),
    );
    final camadas = [
      texto('meio', const Offset(200, 300)),
      texto('esq', const Offset(40, 500)),
      texto('dir', const Offset(400, 100)),
    ];
    final esqDir = orderedCascadeLayers(
      camadas,
      {'meio', 'esq', 'dir'},
      order: CascadeOrder.esquerdaDireita,
    ).map((l) => l.id).toList();
    expect(esqDir, ['esq', 'meio', 'dir']);
    final cimaBaixo = orderedCascadeLayers(
      camadas,
      {'meio', 'esq', 'dir'},
      order: CascadeOrder.cimaBaixo,
    ).map((l) => l.id).toList();
    expect(cimaBaixo, ['dir', 'meio', 'esq']);
    // E o atraso aplicado segue essa ordem.
    final out = cascadeLayerKeyframes(
      camadas,
      {'meio', 'esq', 'dir'},
      interval: const Duration(milliseconds: 100),
      order: CascadeOrder.esquerdaDireita,
    );
    final dir = out.firstWhere((l) => l.id == 'dir');
    expect(
      dir.position.keyframes.first.time,
      const Duration(milliseconds: 200),
    );
  });
}
