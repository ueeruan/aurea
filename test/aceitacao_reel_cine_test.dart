// ACEITACAO REEL CINEMATOGRAFICO (spec §45): o Reel 9:16 inteiro pelas
// portas do app — push e deriva de camera com keyframes reais, rampa de
// velocidade, whip na juncao, motion blur, titulo animado, o LOOK de
// cinema (grade + grao + bloom + halation + vinheta numa camada de
// ajuste), light leak em screen, whoosh ancorado em marcador e musica
// com ducking pela voz. Salva, reabre e desfaz ate o vazio.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/cut.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/look_de_cinema.dart';
import 'package:aurea/src/features/editor/domain/presets_de_movimento.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('o Reel de cinema sai inteiro pelas portas do app', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    VideoProject projeto() => c.read(editorControllerProvider);

    // ------------------------------------------------ o canvas 9:16
    e.setComposition(aspectRatio: 9 / 16);
    expect(projeto().aspectRatio, closeTo(9 / 16, 1e-9));

    // ------------------------------------------- dois clipes seguidos
    e.addVideoLayer(
      Duration.zero,
      'a.mp4',
      'Cena A',
      const Duration(seconds: 3),
    );
    e.addVideoLayer(
      const Duration(seconds: 3),
      'b.mp4',
      'Cena B',
      const Duration(seconds: 3),
    );
    final cenaA = projeto()
        .layers
        .whereType<VideoLayer>()
        .firstWhere((l) => l.name == 'Cena A')
        .id;
    final cenaB = projeto()
        .layers
        .whereType<VideoLayer>()
        .firstWhere((l) => l.name == 'Cena B')
        .id;

    // --------------------- movimentos de camera com keyframes reais
    e.aplicarPresetDeMovimento(cenaA, Duration.zero, PresetDeMovimento.zoomSuave);
    e.aplicarPresetDeMovimento(
      cenaB,
      const Duration(seconds: 3),
      PresetDeMovimento.deriva,
    );
    final a = projeto().layerById(cenaA)!;
    expect(a.scaleX.keyframes.last.value, closeTo(1.08, 1e-9));
    final b = projeto().layerById(cenaB)!;
    expect(b.position.keyframes.length, 2, reason: 'deriva move a posicao');
    expect(b.scaleX.keyframes.last.value, closeTo(1.05, 1e-9));

    // -------------------------------- rampa de velocidade e whip (§6/8)
    e.applySpeedRamp(cenaA, SpeedRampPreset.heroi);
    expect(
      (projeto().layerById(cenaA)! as VideoLayer).effects.any(
        (x) => x.type == EffectType.timeRemap,
      ),
      isTrue,
    );
    expect(
      e.applyTransition(
        cenaA,
        ClipTransitionType.whip,
        duration: const Duration(milliseconds: 240),
        // Os clipes comecam no zero da fonte (sem sobra): a borda
        // congela, como a folha de transicao oferece.
        fallback: TransitionEdgeFallback.freeze,
      ),
      isTrue,
    );

    // ------------------------------------------------- motion blur
    e.toggleLayerMotionBlur(cenaB);
    expect(projeto().metaOf(cenaB).motionBlur, isTrue);

    // -------------------------------------------- titulo minimalista
    e.addTextLayer(Duration.zero, text: 'LISBOA, 2026');
    final titulo = projeto().layers.firstWhere((l) => l is TextLayer).id;
    e.aplicarPresetDeMovimento(titulo, Duration.zero, PresetDeMovimento.aparecer);
    expect(projeto().layerById(titulo)!.opacity.keyframes.length, 2);

    // ------------------------------------ O LOOK DE CINEMA no topo (§16)
    final look = e.adicionarLook(LookDeCinema.cine)!;
    final ajuste = projeto().layerById(look)! as AdjustmentLayer;
    expect(ajuste.effects.length, 5);
    final tipos = ajuste.effects.map((x) => x.type).toList();
    expect(tipos.where((t) => t == EffectType.lightGlow).length, 2,
        reason: 'bloom + halation sao dois glows');
    expect(tipos, contains(EffectType.colorTune));
    expect(tipos, contains(EffectType.filmGrain));
    expect(tipos, contains(EffectType.vignette));
    // A halation e o glow QUENTE: multiplicador de vermelho acima de 1.
    final halation = ajuste.effects
        .where((x) => x.type == EffectType.lightGlow)
        .last;
    expect(halation.paramAt('mult_r', Duration.zero), greaterThan(1));
    expect(halation.paramAt('mult_b', Duration.zero), lessThan(1));
    expect(projeto().layers.first.id, look, reason: 'look no TOPO da pilha');

    // ----------------------------------- light leak + whoosh + ducking
    final leak = e.addImageLayer(Duration.zero, 'leak.png', 'Light leak');
    e.setBlendMode(leak, BlendMode.screen);
    e.addMarkers([const Duration(seconds: 3)], label: 'corte');
    expect(projeto().markers.single.time, const Duration(seconds: 3));
    e.addAudioLayer(
      const Duration(milliseconds: 2800),
      'whoosh.wav',
      'Whoosh',
      const Duration(milliseconds: 600),
    );
    final voz = e.separarAudio(cenaB)!;
    e.addAudioLayer(
      Duration.zero,
      'musica.mp3',
      'Música',
      const Duration(seconds: 6),
    );
    final musica = projeto()
        .layers
        .whereType<AudioLayer>()
        .firstWhere((l) => l.name == 'Música')
        .id;
    e.updateAudioSpec(
      musica,
      (s) => s.copyWith(duckAgainstId: voz, duckAmount: .7),
    );

    // --------------------------------------- salvar, abrir, desfazer
    final volta = projectFromJson(projectToJson(projeto()));
    expect(volta.aspectRatio, closeTo(9 / 16, 1e-9));
    final lookVolta = volta.layerById(look)! as AdjustmentLayer;
    expect(lookVolta.effects.length, 5);
    expect(
      (volta.layerById(musica)! as AudioLayer).audio.duckAgainstId,
      voz,
    );
    expect(volta.markers.single.label, 'corte');

    for (var i = 0; i < 300 && projeto().layers.isNotEmpty; i++) {
      e.undo();
    }
    expect(projeto().layers, isEmpty);
  });
}
