import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  test(
    'Time Remap stays persisted internally but is absent from the catalog',
    () {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      final controller = c.read(editorControllerProvider.notifier);
      controller.openProject(
        VideoProject(
          name: 'Test',
          createdAt: DateTime(2026),
          layers: [
            VideoLayer(
              id: 'v',
              name: 'Video',
              startTime: Duration.zero,
              duration: const Duration(seconds: 4),
              sourcePath: '/clip.mp4',
            ),
          ],
        ),
      );
      controller.addEffect('v', EffectType.posterizeTime);
      controller.addEffect('v', EffectType.timeRemap);
      VideoLayer video() =>
          c.read(editorControllerProvider).layerById('v') as VideoLayer;
      final effect = video().effects.last;
      expect(effect.type, EffectType.timeRemap);
      expect(
        effectsInCategory('Time'),
        containsAll([EffectType.posterizeTime, EffectType.rgbTimeWarp]),
      );
      expect(effectsInCategory('Time'), isNot(contains(EffectType.timeRemap)));
      expect(efeitosDoCatalogo, isNot(contains(EffectType.timeRemap)));
      expect(
        searchEffects('time remap'),
        isNot(contains(EffectType.timeRemap)),
      );
      controller.editEffectParam(
        'v',
        effect.id,
        'tempo',
        const Duration(milliseconds: 550),
        2,
      );
      expect(
        video().timeRemap!.hasKeyframeAt(const Duration(milliseconds: 550)),
        isTrue,
      );
      controller.reorderEffect('v', effect.id, -1);
      expect(video().effects.first.id, effect.id);
      controller.toggleEffectEnabled('v', effect.id);
      expect(video().timeRemap, isNull);
      final restored = projectFromJson(
        projectToJson(c.read(editorControllerProvider)),
      ).layerById('v') as VideoLayer;
      expect(restored.effects.first.id, effect.id);
      expect(restored.effects.first.enabled, isFalse);
      expect(restored.timeRemap, isNull);
      controller.toggleEffectEnabled('v', effect.id);
      expect(video().timeRemap, isNotNull);
      controller.removeEffect('v', effect.id);
      expect(video().timeRemap, isNull);
    },
  );

  test(
    'Legacy layer track migrates once and remains editable by the same engine',
    () {
      final track = AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(const Duration(seconds: 2), 4);
      final video = VideoLayer(
        name: 'Old',
        startTime: Duration.zero,
        duration: const Duration(seconds: 2),
        sourcePath: '/clip.mp4',
        timeRemap: track,
      );
      expect(video.effects.single.type, EffectType.timeRemap);
      expect(video.copyLayer(name: 'Renamed').timeRemap, same(track));
      expect(video.copyLayer(clearTimeRemap: true).effects, isEmpty);
    },
  );
}
