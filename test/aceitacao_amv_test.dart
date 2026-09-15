// ACEITACAO AMV (spec §43): a montagem inteira — cortes na batida, rampa
// de tempo, transicoes zoom e whip, impacto forte (tremor + glow + flash
// + RGB split), turbulent displace, mascara animada, camada de ajuste
// com correcao e grade de cor, overlay em screen e motion blur — montada
// SO pelas portas publicas do app, sobrevivendo a salvar/abrir e
// desfazendo ate o vazio. Nenhum plugin, nenhuma caixa preta.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/cut.dart';
import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/impacto_amv.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('o edit de AMV sai inteiro pelas portas do app', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    VideoProject projeto() => c.read(editorControllerProvider);

    // ------------------------------------------ musica + clipes + grade
    e.addAudioLayer(
      Duration.zero,
      'musica.mp3',
      'Música',
      const Duration(seconds: 4),
    );
    e.addVideoLayer(
      Duration.zero,
      'anime1.mp4',
      'Cena 1',
      const Duration(seconds: 4),
    );
    e.setBpm(120); // grade de batidas: uma a cada 500 ms
    expect(projeto().beats, isNotEmpty);

    final cena1 =
        projeto().layers.whereType<VideoLayer>().first.id;

    // ------------------------------------------------ cortes na batida
    final cortes = e.cortarNasBatidas(cena1);
    expect(cortes, greaterThanOrEqualTo(6),
        reason: '4 s a 120 bpm tem 7 batidas internas');
    final segmentos = projeto().layers.whereType<VideoLayer>().toList();
    expect(segmentos.length, cortes + 1);
    // Cada segmento comeca numa batida (com a folga do corte).
    final inicio2 = segmentos[segmentos.length - 2].startTime;
    expect(
      projeto().beats.any(
        (b) => (b - inicio2).abs() < const Duration(milliseconds: 12),
      ),
      isTrue,
    );

    // ------------------------------------------------- rampa de tempo
    final alvoRampa = segmentos.first.id;
    e.applySpeedRamp(alvoRampa, SpeedRampPreset.bala);
    expect(
      hasTimeRemap(
        projeto().layerById(alvoRampa)! as VideoLayer,
      ),
      isTrue,
    );

    // -------------------------------------------- transicoes na juncao
    final aSegunda = segmentos[segmentos.length - 2].id;
    final aTerceira = segmentos[segmentos.length - 3].id;
    expect(
      e.applyTransition(
        aSegunda,
        ClipTransitionType.zoomWarp,
        duration: const Duration(milliseconds: 240),
      ),
      isTrue,
    );
    expect(
      e.applyTransition(
        aTerceira,
        ClipTransitionType.whip,
        duration: const Duration(milliseconds: 240),
      ),
      isTrue,
    );

    // ------------------------------------------------- impacto forte
    final alvoImpacto = aSegunda;
    final efeitosAntes =
        projeto().layerById(alvoImpacto)!.effects.length;
    e.aplicarImpactoAmv(
      alvoImpacto,
      projeto().layerById(alvoImpacto)!.startTime,
      ImpactoAmv.forte,
    );
    final comImpacto = projeto().layerById(alvoImpacto)!;
    expect(comImpacto.effects.length, efeitosAntes + 4,
        reason: 'tremor + glow + flash + rgb split');
    final tremor = comImpacto.effects
        .firstWhere((x) => x.type == EffectType.tremor);
    expect(tremor.track('amplitude').keyframes.length, 3,
        reason: 'envelope ataque/segura/decai');
    expect(
      tremor.track('amplitude').valueAt(const Duration(milliseconds: 90)),
      20,
    );
    expect(
      comImpacto.scaleX.valueAt(const Duration(milliseconds: 90)),
      closeTo(1.20, 1e-9),
      reason: 'pulso de escala',
    );

    // ------------------------------------- turbulencia + mascara animada
    e.addEffect(alvoImpacto, EffectType.turbulentDisplace);
    e.addMask(
      alvoImpacto,
      LayerMask(
        path: AnimatedPath(BezierPath.rect(400, 400))
            .withKeyframe(Duration.zero, BezierPath.rect(40, 40))
            .withKeyframe(
              const Duration(milliseconds: 300),
              BezierPath.rect(900, 900),
            ),
        feather: AnimatedDouble(24),
      ),
    );
    final mascarado = projeto().layerById(alvoImpacto)!;
    expect(mascarado.masks.single.path.keyframes.length, 2,
        reason: 'mask wipe por keyframes');

    // --------------------------- camada de ajuste: correcao + grade
    e.addAdjustmentLayer(Duration.zero);
    final ajuste = projeto().layers.whereType<AdjustmentLayer>().first.id;
    e.addEffect(ajuste, EffectType.levels);
    e.addEffect(ajuste, EffectType.colorTune);
    expect(projeto().layerById(ajuste)!.effects.length, 2);

    // ------------------------------------------- overlay em screen
    final overlay = e.addImageLayer(Duration.zero, 'leak.png', 'Light leak');
    e.setBlendMode(overlay, BlendMode.screen);
    expect(projeto().layerById(overlay)!.blendMode, BlendMode.screen);

    // ------------------------------------------------- motion blur
    e.toggleLayerMotionBlur(alvoImpacto);
    expect(projeto().metaOf(alvoImpacto).motionBlur, isTrue);

    // ------------------------------------------- salvar e abrir de novo
    final volta = projectFromJson(projectToJson(projeto()));
    expect(volta.beats.length, projeto().beats.length);
    expect(volta.bpm, 120);
    final impactoVolta = volta.layerById(alvoImpacto)!;
    expect(
      impactoVolta.effects
          .firstWhere((x) => x.type == EffectType.tremor)
          .track('amplitude')
          .keyframes
          .length,
      3,
    );
    expect(volta.layerById(overlay)!.blendMode, BlendMode.screen);
    expect(
      hasTimeRemap(volta.layerById(alvoRampa)! as VideoLayer),
      isTrue,
    );

    // ------------------------------------------------ desfazer tudo
    for (var i = 0; i < 200 && projeto().layers.isNotEmpty; i++) {
      e.undo();
    }
    expect(projeto().layers, isEmpty);
  });
}
