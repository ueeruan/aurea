import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/export/application/export_engine.dart';

VideoProject _p({
  int fps = 30,
  int w = 1080,
  int h = 1920,
  Duration duration = const Duration(seconds: 5),
  List<Layer> layers = const [],
}) =>
    VideoProject(
      name: 'Teste',
      createdAt: DateTime(2026),
      aspectRatio: w / h,
      // O projeto guarda a proporcao e o LADO CURTO; a largura e a
      // altura de saida saem dai.
      resolutionHeight: w < h ? w : h,
      fps: fps,
      layers: layers.isEmpty
          ? [
              TextLayer(
                name: 'T',
                startTime: Duration.zero,
                duration: duration,
                text: 'oi',
              )
            ]
          : layers,
    );

void main() {
  group('Contagem de quadros', () {
    test('duracao x fps da a contagem exata', () {
      final e = ExportEngine(_p());
      expect(e.frameCount, 150);
      expect(e.fps, 30);
    });

    test('60 fps dobra os quadros', () {
      expect(ExportEngine(_p(fps: 60)).frameCount, 300);
    });

    test('fps invalido cai para 30 em vez de dividir por zero', () {
      final e = ExportEngine(_p(fps: 0));
      expect(e.fps, 30);
      expect(e.frameCount, 150);
    });

    test('o instante de cada quadro anda no passo do fps', () {
      final e = ExportEngine(_p());
      expect(e.timeOfFrame(0), Duration.zero);
      expect(e.timeOfFrame(30), const Duration(seconds: 1));
      expect(e.timeOfFrame(149).inMilliseconds, closeTo(4966, 2));
      // O ultimo quadro cai DENTRO da composicao, nunca depois.
      expect(e.timeOfFrame(e.frameCount - 1),
          lessThan(const Duration(seconds: 5)));
    });
  });

  group('Dimensao de saida', () {
    // O H.264 nao aceita largura ou altura impar: sem isto o encode
    // falha com uma mensagem que nao diz nada.
    test('dimensao impar vira par', () {
      final e = ExportEngine(_p(w: 1081, h: 1921));
      expect(e.width.isEven, isTrue);
      expect(e.height.isEven, isTrue);
    });

    test('dimensao par fica como esta', () {
      final e = ExportEngine(_p(w: 1080, h: 1920));
      expect(e.width, 1080);
      expect(e.height, 1920);
    });
  });

  group('Audio', () {
    test('sem faixa de som, nao monta grafo nenhum', () {
      final g = ExportEngine(_p()).audioGraph(1);
      expect(g.inputs, isEmpty);
      expect(g.filter, isNull);
      expect(g.outLabel, isNull);
    });

    test('uma faixa entra com corte, volume e atraso', () {
      final e = ExportEngine(_p(layers: [
        AudioLayer(
          name: 'Trilha',
          startTime: const Duration(milliseconds: 1500),
          duration: const Duration(seconds: 3),
          sourcePath: '/tmp/a.wav',
          volume: 0.5,
        ),
      ]));
      final g = e.audioGraph(1);
      expect(g.inputs, contains('/tmp/a.wav'));
      expect(g.inputs, contains('-t'));
      expect(g.filter, contains('volume=0.500'));
      // Atraso = posicao na linha do tempo, nos dois canais.
      expect(g.filter, contains('adelay=1500|1500'));
      expect(g.outLabel, '[aout]');
    });

    test('varias faixas viram uma mixagem so', () {
      final e = ExportEngine(_p(layers: [
        AudioLayer(
          name: 'A',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          sourcePath: '/tmp/a.wav',
        ),
        AudioLayer(
          name: 'B',
          startTime: const Duration(seconds: 1),
          duration: const Duration(seconds: 2),
          sourcePath: '/tmp/b.wav',
        ),
      ]));
      final g = e.audioGraph(1);
      expect(g.filter, contains('amix=inputs=2'));
      expect(g.inputs.where((s) => s == '-i').length, 2);
    });

    test('video mudo nao entra na mixagem', () {
      final e = ExportEngine(_p(layers: [
        VideoLayer(
          name: 'V',
          startTime: Duration.zero,
          duration: const Duration(seconds: 3),
          sourcePath: '/tmp/v.mp4',
          volume: 0,
        ),
      ]));
      expect(e.audioSources, isEmpty);
      expect(e.audioGraph(1).filter, isNull);
    });

    test('video com som entra, respeitando o corte da fonte', () {
      final e = ExportEngine(_p(layers: [
        VideoLayer(
          name: 'V',
          startTime: const Duration(seconds: 1),
          duration: const Duration(seconds: 3),
          sourcePath: '/tmp/v.mp4',
          sourceOffset: const Duration(milliseconds: 2500),
          volume: 0.8,
        ),
      ]));
      final g = e.audioGraph(1);
      expect(e.audioSources.length, 1);
      // O corte da fonte vira -ss; a posicao na timeline vira adelay.
      // Seis casas: o corte precisa cair no quadro certo, nao no milesimo.
      expect(g.inputs, contains('2.500000'));
      expect(g.filter, contains('adelay=1000|1000'));
      expect(g.filter, contains('volume=0.800'));
    });

    test('o indice da entrada comeca depois da sequencia de quadros', () {
      final e = ExportEngine(_p(layers: [
        AudioLayer(
          name: 'A',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          sourcePath: '/tmp/a.wav',
        ),
      ]));
      // A entrada 0 e a sequencia de PNG; o audio comeca na 1.
      expect(e.audioGraph(1).filter, contains('[1:a]'));
      expect(e.audioGraph(3).filter, contains('[3:a]'));
    });
  });
}
