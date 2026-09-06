import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/effect_preset_store.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/effect_preset.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

/// Presets da pessoa vivem fora do projeto e voltam iguais do JSON; a
/// curva de um efeito vale para todos os parametros do trecho.
void main() {
  setUp(() {
    EffectPresetStore.semArquivo = true;
    EffectPresetStore.instance.reset();
  });

  test('preset vai e volta do JSON com efeito, keyframes e nome', () {
    final efeito = EffectInstance(type: EffectType.glowVol)
        .withKeyframeToggled(Duration.zero)
        .withParamEdited('radius', const Duration(seconds: 1), 0.9);
    final preset = EffectPreset(
      name: 'Meu glow',
      effects: [efeito],
      tags: const ['glow'],
      suggestedDuration: const Duration(seconds: 3),
    );
    final volta = effectPresetFromJson(effectPresetToJson(preset));
    expect(volta.id, preset.id);
    expect(volta.name, 'Meu glow');
    expect(volta.tags, ['glow']);
    expect(volta.suggestedDuration, const Duration(seconds: 3));
    expect(volta.effects.length, 1);
    expect(volta.effects.first.type, EffectType.glowVol);
    expect(volta.effects.first.track('radius').keyframes.length, 2);
    expect(volta.effects.first.paramAt('radius', const Duration(seconds: 1)),
        closeTo(0.9, 1e-9));
  });

  test('a lista guarda, nao duplica e apaga', () async {
    final store = EffectPresetStore.instance;
    final a = EffectPreset(
        name: 'A', effects: [EffectInstance(type: EffectType.tint)]);
    await store.add(a);
    await store.add(a);
    expect(store.presets.length, 1);
    await store.add(EffectPreset(
        name: 'B', effects: [EffectInstance(type: EffectType.tint)]));
    expect(store.presets.map((p) => p.name).toList(), ['B', 'A']);
    await store.remove(a.id);
    expect(store.presets.map((p) => p.name).toList(), ['B']);
  });

  test('a curva do efeito entra em todos os parametros do trecho', () {
    final projeto = VideoProject(
      name: 'p',
      createdAt: DateTime(2026),
      layers: [
        ShapeLayer(
          id: 's',
          name: 's',
          startTime: Duration.zero,
          duration: const Duration(seconds: 5),
          effects: [
            EffectInstance(id: 'fx', type: EffectType.glowVol)
                .withKeyframeToggled(Duration.zero)
                .withParamEdited('radius', const Duration(seconds: 2), 0.9),
          ],
        ),
      ],
    );
    final c = ProviderContainer();
    final ctrl = c.read(editorControllerProvider.notifier);
    ctrl.openProject(projeto);
    const curva = Easing(x1: 0.1, y1: 0.9, x2: 0.9, y2: 0.1);
    ctrl.setEffectSegmentEase('s', 'fx', Duration.zero, curva);
    final fx = (c.read(editorControllerProvider).layerById('s') as ShapeLayer)
        .effects
        .first;
    for (final t in fx.params.values) {
      expect(t.easeAt(Duration.zero), curva, reason: 'todos os parametros');
    }
  });
}
