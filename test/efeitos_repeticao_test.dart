import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/editor/presentation/widgets/repeticao_pass.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

EffectInstance _com(EffectType tipo, Map<String, double> valores) {
  final e = EffectInstance(type: tipo);
  return e.copyWith(
    params: {
      ...e.params,
      for (final v in valores.entries) v.key: AnimatedDouble(v.value),
    },
  );
}

void main() {
  group('a folga do alvo', () {
    test('sem repetição nenhuma, o alvo não cresce', () {
      final f = folgaDaRepeticao(
        _com(EffectType.repetirEmLinha, {'copias': 1, 'passo_x': 60}),
        Duration.zero,
      );
      expect(f.largura, 1);
      expect(f.altura, 1);
    });

    test('a linha cresce só no eixo em que anda', () {
      final f = folgaDaRepeticao(
        _com(EffectType.repetirEmLinha, {
          'copias': 5,
          'passo_x': 50,
          'passo_y': 0,
        }),
        Duration.zero,
      );
      expect(f.largura, greaterThan(3));
      expect(f.altura, 1, reason: 'nao anda em Y, nao cresce em Y');
    });

    test('a grade cresce pelo número de colunas e linhas', () {
      final f = folgaDaRepeticao(
        _com(EffectType.repetirEmGrade, {
          'colunas': 4,
          'linhas': 2,
          'passo_x': 100,
          'passo_y': 100,
        }),
        Duration.zero,
      );
      expect(f.largura, 4);
      expect(f.altura, 2);
    });

    test('círculo e espalhar crescem pelo raio, nos dois eixos', () {
      for (final tipo in [
        EffectType.repetirEmCirculo,
        EffectType.espalharCopias,
      ]) {
        final f = folgaDaRepeticao(_com(tipo, {'raio': 100}), Duration.zero);
        expect(f.largura, 3, reason: tipo.name);
        expect(f.altura, 3, reason: tipo.name);
      }
    });

    test('o alvo tem teto: um raio enorme não vira uma textura gigante', () {
      final f = folgaDaRepeticao(
        _com(EffectType.espalharCopias, {'raio': 300}),
        Duration.zero,
      );
      expect(f.largura, 6);
      expect(f.altura, 6);
    });
  });

  group('as fichas da repetição', () {
    test('as quatro entraram, com id próprio e prontos', () {
      for (final t in tiposDeRepeticao) {
        final ficha = effectSpecs[t]!;
        expect(effectTypeFromId(ficha.id), t);
        expect(ficha.presets, isNotEmpty, reason: t.name);
        // Toda repeticao tem as tres por copia: e o que faz o rastro.
        for (final k in ['giro', 'escala', 'opacidade']) {
          expect(ficha.params.containsKey(k), isTrue, reason: '${t.name}.$k');
        }
      }
      expect(effectSpecs[EffectType.repetirEmLinha]!.id, 'repeat_line');
      expect(effectSpecs[EffectType.repetirEmGrade]!.id, 'repeat_grid');
      expect(effectSpecs[EffectType.repetirEmCirculo]!.id, 'repeat_radial');
      expect(effectSpecs[EffectType.espalharCopias]!.id, 'repeat_scatter');
    });

    test('a busca acha pelos nomes de casa', () {
      expect(searchEffects('mandala').first, EffectType.repetirEmCirculo);
      expect(searchEffects('confete').first, EffectType.espalharCopias);
      expect(searchEffects('grade'), contains(EffectType.repetirEmGrade));
    });
  });

  testWidgets('o palco monta o passe de repetição na camada', (tester) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final videos = VideoLayerManager();
    addTearDown(videos.dispose);
    final playback = PlaybackController(
      vsync: const TestVSync(),
      durationOf: () => container.read(editorControllerProvider).duration,
    );
    addTearDown(playback.dispose);
    final editor = container.read(editorControllerProvider.notifier);
    editor.openProject(VideoProject.empty('Repetir'));
    editor.addShapeLayer(Duration.zero);
    final id = container.read(editorControllerProvider).layers.single.id;
    editor.addEffect(id, EffectType.repetirEmCirculo);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: Scaffold(
            body: Center(
              child: SizedBox(
                width: 320,
                height: 320,
                child: PreviewStage(playback: playback, videos: videos),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    expect(find.byType(RepeticaoPass), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
