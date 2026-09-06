import 'dart:convert';
import 'dart:ui';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

const _revealDuration = Duration(milliseconds: 650);

ShapeLayer _shape(
  String name, {
  Duration start = Duration.zero,
  List<LayerMask> masks = const [],
  MatteMode matteMode = MatteMode.none,
  String? matteSourceId,
}) {
  return ShapeLayer(
    name: name,
    startTime: start,
    duration: const Duration(seconds: 5),
    masks: masks,
    matteMode: matteMode,
    matteSourceId: matteSourceId,
  );
}

ProviderContainer _openProject(List<Layer> layers) {
  final container = ProviderContainer();
  container
      .read(editorControllerProvider.notifier)
      .openProject(
        VideoProject(
          name: 'Nivel 4',
          createdAt: DateTime(2026, 9, 2),
          layers: layers,
        ),
      );
  return container;
}

LayerMask _maskOf(ProviderContainer container, String layerId) {
  return container
      .read(editorControllerProvider)
      .layerById(layerId)!
      .masks
      .single;
}

void main() {
  group('Nivel 4 — presets de revelar', () {
    const size = Size(400, 300);
    const localStart = Duration(milliseconds: 250);

    test('os sete presets criam mascara fechada com dois keyframes reais', () {
      expect(MaskRevealPreset.values, hasLength(7));

      for (final preset in MaskRevealPreset.values) {
        final mask = createRevealMask(preset, size, localStart);
        final keyframes = mask.path.keyframes;

        expect(mask.name, isNotEmpty, reason: preset.label);
        expect(mask.path.base.closed, isTrue, reason: preset.label);
        expect(keyframes, hasLength(2), reason: preset.label);
        expect(keyframes.first.time, localStart, reason: preset.label);
        expect(
          keyframes.last.time,
          localStart + _revealDuration,
          reason: preset.label,
        );
        expect(
          keyframes.every((keyframe) => keyframe.value.closed),
          isTrue,
          reason: preset.label,
        );
      }
    });

    test('Revelar Esquerda entra de fora e termina cobrindo a camada', () {
      final mask = createRevealMask(
        MaskRevealPreset.esquerda,
        size,
        localStart,
      );
      final initial = mask.path.valueAt(localStart).build().getBounds();
      final finalBounds = mask.path
          .valueAt(localStart + _revealDuration)
          .build()
          .getBounds();

      expect(initial.center.dx, closeTo(-size.width, 1e-6));
      expect(initial.right, closeTo(-size.width / 2, 1e-6));
      expect(initial.width, closeTo(size.width, 1e-6));
      expect(initial.height, closeTo(size.height, 1e-6));

      expect(finalBounds.center, Offset.zero);
      expect(finalBounds.width, closeTo(size.width, 1e-6));
      expect(finalBounds.height, closeTo(size.height, 1e-6));
    });

    test('o segmento de entrada usa easing de mola, nao linear', () {
      final mask = createRevealMask(
        MaskRevealPreset.esquerda,
        size,
        localStart,
      );
      final easing = mask.path.keyframes.first.ease;

      expect(easing.type, EasingType.elastic);
      expect(easing.isLinear, isFalse);
      expect(easing.transform(0.5), isNot(closeTo(0.5, 1e-3)));

      // Em 20% do segmento, elasticOut passa de 1: a mascara que veio da
      // esquerda ultrapassa geometricamente o centro antes de assentar.
      final overshoot = mask.path
          .valueAt(localStart + const Duration(milliseconds: 130))
          .build()
          .getBounds()
          .center
          .dx;
      expect(overshoot, greaterThan(0));
    });

    test('keyframe criado pelo preset continua editavel pelo controller', () {
      final layer = _shape('Texto', start: const Duration(seconds: 1));
      final container = _openProject([layer]);
      addTearDown(container.dispose);
      final controller = container.read(editorControllerProvider.notifier);
      const globalStart = Duration(milliseconds: 1250);
      const replacementCenter = Offset(-120, 15);

      controller.applyMaskReveal(
        layer.id,
        MaskRevealPreset.esquerda,
        globalStart,
      );
      final created = _maskOf(container, layer.id);
      expect(created.path.keyframes, hasLength(2));

      final replacement = BezierPath.rect(180, 120, center: replacementCenter);
      controller.replaceMaskPath(
        layer.id,
        created.id,
        replacement,
        globalStart,
      );

      final edited = _maskOf(container, layer.id);
      final first = edited.path.keyframes.first;
      expect(edited.path.keyframes, hasLength(2));
      expect(first.time, const Duration(milliseconds: 250));
      expect(first.value.build().getBounds().center, replacementCenter);
      expect(first.value.closed, isTrue);
      expect(first.ease.type, EasingType.elastic);
    });
  });

  group('Nivel 4 — caminho, feather e pilha', () {
    test('caminho aberto preserva closed=false no modelo e no controller', () {
      final open = BezierPath(
        closed: false,
        vertices: const [
          PathVertex(p: Offset(-80, 40)),
          PathVertex(p: Offset(0, -60)),
          PathVertex(p: Offset(80, 40)),
        ],
      );
      final mask = LayerMask(path: AnimatedPath(BezierPath.rect(100, 100)));
      final layer = _shape('Imagem', masks: [mask]);
      final container = _openProject([layer]);
      addTearDown(container.dispose);

      container
          .read(editorControllerProvider.notifier)
          .replaceMaskPath(layer.id, mask.id, open, Duration.zero);

      final stored = _maskOf(container, layer.id).path.base;
      expect(stored.closed, isFalse);
      expect(
        stored.vertices.map((vertex) => vertex.p),
        open.vertices.map((vertex) => vertex.p),
      );
    });

    test('aviso dispara somente quando feather e expansao passam da borda', () {
      const size = Size(400, 400);
      final centered = LayerMask(
        path: AnimatedPath(BezierPath.rect(100, 100)),
        feather: AnimatedDouble(40),
        expansion: AnimatedDouble(10),
      );
      final nearRightEdge = LayerMask(
        path: AnimatedPath(
          BezierPath.rect(100, 100, center: const Offset(125, 0)),
        ),
        feather: AnimatedDouble(40),
        expansion: AnimatedDouble(10),
      );

      expect(maskFeatherExceedsBounds(centered, Duration.zero, size), isFalse);
      expect(
        maskFeatherExceedsBounds(nearRightEdge, Duration.zero, size),
        isTrue,
      );
    });

    test('expansao +10 nao move nenhum vertice do caminho', () {
      final path = BezierPath.ellipse(180, 120, center: const Offset(20, -15));
      final mask = LayerMask(path: AnimatedPath(path));
      final layer = _shape('Imagem', masks: [mask]);
      final container = _openProject([layer]);
      addTearDown(container.dispose);
      final before = [for (final vertex in path.vertices) vertex.p];

      container
          .read(editorControllerProvider.notifier)
          .editMaskParam(layer.id, mask.id, 'expansion', Duration.zero, 10);

      final after = _maskOf(container, layer.id);
      expect(after.expansion.valueAt(Duration.zero), 10);
      expect([for (final vertex in after.path.base.vertices) vertex.p], before);
    });

    test('duas mascaras podem trocar de ordem sem perder identidade', () {
      final outer = LayerMask(name: 'Fora');
      final inner = LayerMask(name: 'Dentro', mode: MaskMode.subtract);
      final layer = _shape('Imagem', masks: [outer, inner]);
      final container = _openProject([layer]);
      addTearDown(container.dispose);
      final controller = container.read(editorControllerProvider.notifier);

      controller.reorderMask(layer.id, inner.id, -1);

      final reordered = container
          .read(editorControllerProvider)
          .layerById(layer.id)!
          .masks;
      expect(reordered.map((mask) => mask.id), [inner.id, outer.id]);
      expect(reordered.first.mode, MaskMode.subtract);
      expect(reordered.last.name, 'Fora');
    });

    test('Da forma converte o caminho para a origem central da mascara', () {
      final geometry = BezierPath.rect(160, 90, center: const Offset(240, -70));
      final mask = LayerMask();
      final layer = ShapeLayer(
        name: 'Forma deslocada',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        contents: [ShapeBezier(path: AnimatedPath(geometry))],
        masks: [mask],
      );
      final container = _openProject([layer]);
      addTearDown(container.dispose);

      final changed = container
          .read(editorControllerProvider.notifier)
          .setMaskFromOwnShape(layer.id, mask.id, Duration.zero);
      final bounds = _maskOf(container, layer.id).path.base.build().getBounds();

      expect(changed, isTrue);
      expect(bounds.center, Offset.zero);
      expect(bounds.size, geometry.build().getBounds().size);
    });
  });

  group('Nivel 4 — matte por camada', () {
    test('encontra a camada acima e a define como matte', () {
      final source = _shape('Retangulo acima');
      final target = _shape('Video alvo');
      final below = _shape('Fundo');
      final container = _openProject([source, target, below]);
      addTearDown(container.dispose);
      final controller = container.read(editorControllerProvider.notifier);

      expect(controller.matteSourceAbove(target.id)?.id, source.id);
      expect(controller.setMatteFromAbove(target.id, MatteMode.alpha), isTrue);

      final updated = container
          .read(editorControllerProvider)
          .layerById(target.id)!;
      expect(updated.matteMode, MatteMode.alpha);
      expect(updated.matteSourceId, source.id);
    });

    test('camada do topo nao inventa fonte acima', () {
      final top = _shape('Topo');
      final below = _shape('Baixo');
      final container = _openProject([top, below]);
      addTearDown(container.dispose);
      final controller = container.read(editorControllerProvider.notifier);

      expect(controller.matteSourceAbove(top.id), isNull);
      expect(controller.setMatteFromAbove(top.id, MatteMode.luma), isFalse);
      expect(
        container.read(editorControllerProvider).layerById(top.id)!.matteMode,
        MatteMode.none,
      );
    });

    test('desligar matte limpa modo, fonte e serializacao antiga', () {
      final source = _shape('Fonte');
      final target = _shape('Alvo');
      final container = _openProject([source, target]);
      addTearDown(container.dispose);
      final controller = container.read(editorControllerProvider.notifier);

      controller.setMatte(target.id, MatteMode.alpha, source.id);
      expect(
        container
            .read(editorControllerProvider)
            .layerById(target.id)!
            .matteSourceId,
        source.id,
      );

      controller.setMatte(target.id, MatteMode.none, null);
      final project = container.read(editorControllerProvider);
      final cleared = project.layerById(target.id)!;
      expect(cleared.matteMode, MatteMode.none);
      expect(cleared.matteSourceId, isNull);

      final json = projectToJson(project);
      final targetJson = (json['layers'] as List)
          .cast<Map<String, dynamic>>()
          .singleWhere((layer) => layer['id'] == target.id);
      expect(targetJson.containsKey('matte'), isFalse);
      expect(targetJson.containsKey('matteSrc'), isFalse);

      final restored = projectFromJson(
        jsonDecode(jsonEncode(json)) as Map<String, dynamic>,
      ).layerById(target.id)!;
      expect(restored.matteMode, MatteMode.none);
      expect(restored.matteSourceId, isNull);
    });
  });

  test('presets, caminho aberto, ordem e matte sobrevivem ao round-trip', () {
    const size = Size(640, 360);
    final source = _shape('Fonte');
    final reveal = createRevealMask(
      MaskRevealPreset.esquerda,
      size,
      const Duration(milliseconds: 100),
    );
    final open = LayerMask(
      name: 'Aberta',
      mode: MaskMode.none,
      path: AnimatedPath(
        BezierPath(
          closed: false,
          vertices: const [
            PathVertex(p: Offset(-100, 0)),
            PathVertex(p: Offset.zero),
            PathVertex(p: Offset(100, 0)),
          ],
        ),
      ),
      expansion: AnimatedDouble(10),
    );
    final target = _shape(
      'Alvo',
      masks: [reveal, open],
      matteMode: MatteMode.luma,
      matteSourceId: source.id,
    );
    final project = VideoProject(
      name: 'Round-trip Nivel 4',
      createdAt: DateTime(2026, 9, 2),
      layers: [source, target],
    );

    final restored = projectFromJson(
      jsonDecode(jsonEncode(projectToJson(project))) as Map<String, dynamic>,
    );
    final restoredTarget = restored.layerById(target.id)!;

    expect(restoredTarget.matteMode, MatteMode.luma);
    expect(restoredTarget.matteSourceId, source.id);
    expect(restoredTarget.masks.map((mask) => mask.id), [reveal.id, open.id]);
    expect(restoredTarget.masks.first.path.keyframes, hasLength(2));
    expect(
      restoredTarget.masks.first.path.keyframes.every(
        (keyframe) => keyframe.value.closed,
      ),
      isTrue,
    );
    expect(restoredTarget.masks.last.path.base.closed, isFalse);
    expect(restoredTarget.masks.last.expansion.base, 10);
  });
}
