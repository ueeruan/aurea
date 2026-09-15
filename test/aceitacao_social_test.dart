// ACEITACAO SOCIAL (spec §44): o talking-head de Reels/Shorts montado
// pelas portas do app — pausas removidas com ripple, audio separado em
// sincronia pela FONTE, punch in, zoom suave com ease, fundo desfocado,
// texto animado, degrade no rodape, revelacao por mascara animada,
// linha que se desenha (trim), overlay na frente, grupo, limpeza de voz
// e ducking marcados na trilha, tudo sobrevivendo a salvar/abrir e
// desfazendo ate o vazio.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/presets_de_movimento.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('o Reels de talking head sai inteiro pelas portas do app', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    VideoProject projeto() => c.read(editorControllerProvider);

    // ------------------------------------------------ o clipe falado
    e.addVideoLayer(
      Duration.zero,
      'fala.mp4',
      'Fala',
      const Duration(seconds: 8),
    );
    final fala = projeto().layers.whereType<VideoLayer>().first.id;

    // ---------------------------------- pausas fora, com ripple (§26)
    // O mesmo motor do removeSilence, com as faixas ja em tempo da linha
    // (a forma de onda real nao existe num teste; o corte e o que conta).
    final pedacos = e.cutRangesOf(fala, [
      (const Duration(seconds: 2), const Duration(milliseconds: 2600)),
      (const Duration(seconds: 5), const Duration(milliseconds: 5700)),
    ]);
    expect(pedacos, 3);
    final falados = projeto().layers.whereType<VideoLayer>().toList();
    expect(falados.length, 3);
    // Ripple: o total encolheu exatamente o tamanho das pausas.
    final fim = falados
        .map((l) => l.endTime)
        .reduce((a, b) => a > b ? a : b);
    expect(fim, const Duration(microseconds: 6700000));

    // ------------------------------------------ audio separado (§2)
    final primeiro = falados.first.id;
    final audioId = e.separarAudio(primeiro)!;
    final audio = projeto().layerById(audioId)! as AudioLayer;
    final video = projeto().layerById(primeiro)! as VideoLayer;
    expect(audio.sourcePath, video.sourcePath);
    expect(audio.sourceOffset, video.sourceOffset,
        reason: 'sincronia pela FONTE, nunca por indice de quadro');
    expect(audio.duration, video.duration);
    expect(video.audio.muted, isTrue);

    // ------------------- musica com ducking pela voz (§35) e voz limpa
    e.addAudioLayer(
      Duration.zero,
      'trilha.mp3',
      'Música',
      const Duration(seconds: 6),
    );
    final musica = projeto()
        .layers
        .whereType<AudioLayer>()
        .firstWhere((l) => l.name == 'Música')
        .id;
    // Quem abaixa e a MUSICA, ao ouvir a voz separada.
    e.updateAudioSpec(
      musica,
      (s) => s.copyWith(
        duckAgainstId: audioId,
        duckThreshold: .12,
        duckAttack: const Duration(milliseconds: 80),
        duckRelease: const Duration(milliseconds: 320),
      ),
    );
    expect(
      (projeto().layerById(musica)! as AudioLayer).audio.duckAgainstId,
      audioId,
    );
    // Limpeza de voz marcada na propria trilha separada (§36).
    e.updateAudioSpec(
      audioId,
      (s) => s.copyWith(
        processing: const AudioProcessing(denoise: .5, voice: .6),
      ),
    );

    // ------------------------------------------------ punch in (§28)
    final segmento = e.punchIn(primeiro, const Duration(seconds: 1))!;
    expect(segmento, isNot(primeiro));
    expect(
      (projeto().layerById(segmento)! as VideoLayer).scaleX.base,
      closeTo(1.18, 1e-9),
    );

    // ------------------------------------- zoom suave com ease (§27)
    e.aplicarPresetDeMovimento(
      segmento,
      projeto().layerById(segmento)!.startTime,
      PresetDeMovimento.zoomSuave,
    );
    final zoom = projeto().layerById(segmento)!;
    expect(zoom.scaleX.keyframes.length, 2);
    expect(
      zoom.scaleX.keyframes.last.value,
      closeTo(1.18 * 1.08, 1e-9),
    );

    // -------------------------------------------- fundo desfocado (§8)
    final fundo = e.fundoDesfocado(primeiro)!;
    final f = projeto().layerById(fundo)! as VideoLayer;
    expect(f.effects.single.type, EffectType.gaussianBlur);
    expect(f.volume, 0);
    // Fica ATRAS do clipe (indice maior = mais fundo na pilha).
    expect(
      projeto().layers.indexWhere((l) => l.id == fundo),
      projeto().layers.indexWhere((l) => l.id == primeiro) + 1,
    );

    // ------------------------------ texto dinamico + degrade (§11-13)
    e.addTextLayer(Duration.zero, text: 'FICA ATÉ O FIM');
    final texto = projeto()
        .layers
        .firstWhere((l) => l is TextLayer)
        .id;
    e.aplicarPresetDeMovimento(texto, Duration.zero, PresetDeMovimento.subir);
    expect(projeto().layerById(texto)!.opacity.keyframes, isNotEmpty);
    e.addAdjustmentLayer(Duration.zero);
    final degrade = projeto().layers.whereType<AdjustmentLayer>().first.id;
    e.addEffect(degrade, EffectType.gradient4);
    expect(projeto().layerById(degrade)!.effects.single.type,
        EffectType.gradient4);

    // -------------------------- revelacao por mascara animada (§14-17)
    e.addMask(
      texto,
      LayerMask(
        path: AnimatedPath(BezierPath.rect(600, 200))
            .withKeyframe(Duration.zero, BezierPath.rect(1, 200))
            .withKeyframe(
              const Duration(milliseconds: 400),
              BezierPath.rect(900, 200),
            ),
        feather: AnimatedDouble(30),
      ),
    );
    expect(
      projeto().layerById(texto)!.masks.single.path.keyframes.length,
      2,
    );

    // ------------------------------- linha que se desenha (trim, §18)
    e.addShapeLayer(Duration.zero, name: 'Linha');
    final linha = projeto().layers.whereType<ShapeLayer>().first.id;
    e.addShapeOperator(linha, repeater: false);
    expect(
      (projeto().layerById(linha)! as ShapeLayer)
          .contents
          .whereType<TrimOperator>(),
      isNotEmpty,
    );

    // ------------------------- overlay na frente do texto + grupo (§29)
    final overlay = e.addImageLayer(Duration.zero, 'sticker.png', 'Sticker');
    e.setBlendMode(overlay, BlendMode.screen);
    e.groupLayers([texto, overlay, linha]);
    expect(projeto().layers.whereType<GroupLayer>().length, 1);

    // --------------------------------------- salvar, abrir, desfazer
    final volta = projectFromJson(projectToJson(projeto()));
    final musicaVolta = (volta.layerById(musica)! as AudioLayer).audio;
    expect(musicaVolta.duckAgainstId, audioId);
    expect(musicaVolta.duckThreshold, closeTo(.12, 1e-9));
    expect(
      (volta.layerById(fundo)! as VideoLayer).effects.single.type,
      EffectType.gaussianBlur,
    );
    // O texto agora vive DENTRO do grupo: a mascara atravessa o arquivo
    // junto com ele.
    final grupoVolta = volta.layers.whereType<GroupLayer>().single;
    final textoVolta =
        grupoVolta.children.firstWhere((l) => l.id == texto);
    expect(textoVolta.masks.single.path.keyframes.length, 2);

    for (var i = 0; i < 300 && projeto().layers.isNotEmpty; i++) {
      e.undo();
    }
    expect(projeto().layers, isEmpty);
  });
}
