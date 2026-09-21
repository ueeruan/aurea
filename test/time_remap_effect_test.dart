import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  // 20/09, pedido do dono: o Time Remap VOLTOU ao catalogo como efeito
  // comum, em EFEITOS -> TEMPO, ao lado de Time Warp (rgbTimeWarp) e
  // Posterize Time. Ate 17/09 ele era interno de proposito (a UI dele
  // fora apagada em 14/09) e este teste cobrava a ausencia; agora cobra a
  // presenca. O que ele sempre protegeu continua aqui: a trilha de tempo
  // da camada nasce e morre com a instancia do efeito, e atravessa o
  // salvar/abrir.
  test(
    'Time Remap is a normal catalog effect under Time and stays persisted',
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
        containsAll([
          EffectType.posterizeTime,
          EffectType.rgbTimeWarp,
          EffectType.timeRemap,
        ]),
      );
      expect(efeitosDoCatalogo, contains(EffectType.timeRemap));
      expect(efeitosForaDoCatalogo, isNot(contains(EffectType.timeRemap)));
      expect(specDe(EffectType.timeRemap)!.category, 'Time');
      // Achavel pelo nome e pelo que a pessoa realmente digita.
      expect(searchEffects('time remap'), contains(EffectType.timeRemap));
      expect(searchEffects('congelar'), contains(EffectType.timeRemap));
      // O losango dele e o do rail do painel: a barra da camada continua
      // sem mostrar a trilha de tempo.
      expect(efeitosInternos, contains(EffectType.timeRemap));
      // A trilha de tempo nasce ANIMADA (curva identidade), entao ela cai
      // na regra da casa: editar um valor fora de uma marca nao crava
      // marca sozinho (docs/keyframe-explicito.md; `autoKeyframeProvider`
      // nasce desligado). A edicao fica PENDENTE — o projeto de verdade
      // nao muda.
      const em550 = Duration(milliseconds: 550);
      controller.editEffectParam('v', effect.id, 'tempo', em550, 2);
      expect(video().timeRemap!.hasKeyframeAt(em550), isFalse);
      // Com a marca automatica ligada de proposito, a edicao grava — e o
      // que ela grava e a trilha de tempo DA CAMADA: o parametro `tempo`
      // do efeito e a mesma coisa que `VideoLayer.timeRemap`.
      c.read(autoKeyframeProvider.notifier).state = true;
      controller.editEffectParam('v', effect.id, 'tempo', em550, 2);
      expect(video().timeRemap!.hasKeyframeAt(em550), isTrue);
      expect(video().timeRemap!.valueAt(em550), closeTo(2, 1e-6));
      c.read(autoKeyframeProvider.notifier).state = false;
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
