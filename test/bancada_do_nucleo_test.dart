// A BANCADA DO NUCLEO NO PC: a linha de base do motor ATUAL nas cenas A-E.
//
// Num teste de widget o quadro nao vai para a GPU, mas tudo o que vem antes
// vai: construir a arvore, diagramar e gravar a pintura. E o trabalho que
// hoje mora no fio da interface — o que o nucleo em C++ tem de tirar de la.
// Os numeros daqui sao do PC e so servem para comparar o motor atual com o
// candidato na MESMA maquina; os do aparelho vem da tela de estresse.
//
// DUAS RESSALVAS, medidas em 16/09: (1) efeito de shader no teste cai no
// caminho de reserva que copia a imagem para a CPU — a cena C gasta 14 ms
// so no video com VHS Damage + Hue/Saturation, custo que no aparelho e da
// GPU; (2) a cena 3D no teste e o pintor de CPU. Aqui vale o que e do fio
// da interface: arvore, layout, avaliacao de keyframes e texto.
//
// Rodar:  flutter test test/bancada_do_nucleo_test.dart
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/scene3d_gpu.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/bancada_do_nucleo.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('as contas da bancada', () {
    test('mediana, p95, pior e quadros perdidos', () {
      final m = MedidaDaBancada([
        for (var i = 0; i < 18; i++)
          const QuadroMedido(buildMs: 4, rasterMs: 6, totalMs: 12),
        const QuadroMedido(buildMs: 30, rasterMs: 10, totalMs: 45),
        const QuadroMedido(buildMs: 200, rasterMs: 20, totalMs: 230),
      ], segundos: 1);
      expect(m.quadros, 20);
      expect(m.fps, 20);
      expect(m.build.mediana, 4);
      expect(m.total.pior, 230);
      // 1,5 vsync: 25 ms a 60 fps, 50 ms a 30 fps.
      expect(m.perdidos60, 2);
      expect(m.perdidos30, 1);
      expect(m.travadas, 1);
      expect(m.linha('tocando'), contains('perdidos 60/30: 2/1'));
    });

    test('o arrasto vai e volta pela timeline inteira', () {
      expect(tempoDoArrasto(Duration.zero), Duration.zero);
      expect(
        tempoDoArrasto(const Duration(seconds: 4), velocidade: 2.5),
        duracaoDaBancada,
      );
      expect(
        tempoDoArrasto(const Duration(seconds: 6), velocidade: 2.5),
        const Duration(seconds: 5),
      );
    });

    test('crescimento continuo de memoria e acusado; plato nao', () {
      expect(
        CiclosDeMemoria([300, 420, 421, 419, 422, 420]).pareceVazar,
        isFalse,
      );
      final vaza = CiclosDeMemoria([300, 420, 440, 461, 480, 502]);
      expect(vaza.pareceVazar, isTrue);
      expect(vaza.mbPorCiclo, closeTo(20.4, .5));
      expect(vaza.linha, contains('CRESCIMENTO CONTINUO'));
    });
  });

  group('as cenas', () {
    for (final r in receitasDaBancada) {
      test('${r.letra} monta com o catalogo de hoje', () {
        final p = montarCenaDaBancada(r.id, video: 'video_de_teste.mp4');
        expect(p.layers, isNotEmpty);
        if (r.video) expect(p.layers.whereType<VideoLayer>(), hasLength(1));
        // Sem video, a cena segue sem a camada (e nao quebra).
        expect(
          montarCenaDaBancada(r.id).layers.whereType<VideoLayer>(),
          isEmpty,
        );
      });
    }
  });

  group('linha de base do motor atual (PC)', () {
    final linhas = <String>[];
    tearDownAll(() {
      // ignore: avoid_print
      print(
        '\nBANCADA DO NUCLEO — motor atual, PC, fio da interface\n'
        '${linhas.join('\n')}\n',
      );
    });

    for (final r in receitasDaBancada) {
      testWidgets('cena ${r.letra}: ${r.titulo}', (tester) async {
        await tester.runAsync(Scene3DGpu.preparar);
        final container = ProviderContainer();
        addTearDown(container.dispose);
        container
            .read(editorControllerProvider.notifier)
            .openProject(
              montarCenaDaBancada(r.id, video: 'video_de_teste.mp4'),
            );
        final relogio = ValueNotifier(Duration.zero);
        addTearDown(relogio.dispose);
        final videos = VideoLayerManager();
        addTearDown(videos.dispose);
        await tester.pumpWidget(
          UncontrolledProviderScope(
            container: container,
            child: MaterialApp(
              home: Center(
                child: SizedBox(
                  width: 390,
                  height: 219,
                  child: CompositionView(
                    time: relogio,
                    videos: videos,
                    selectedId: null,
                  ),
                ),
              ),
            ),
          ),
        );
        await tester.pump();

        Future<MedidaDaBancada> fase(Duration Function(int i) tempo) async {
          const n = 90;
          final quadros = <QuadroMedido>[];
          final total = Stopwatch()..start();
          for (var i = 0; i < n; i++) {
            final q = Stopwatch()..start();
            relogio.value = tempo(i);
            await tester.pump(const Duration(milliseconds: 16));
            q.stop();
            final ms = q.elapsedMicroseconds / 1000.0;
            quadros.add(QuadroMedido(buildMs: ms, rasterMs: 0, totalMs: ms));
          }
          total.stop();
          return MedidaDaBancada(
            quadros,
            segundos: total.elapsedMicroseconds / 1e6,
          );
        }

        final tocando = await fase((i) => Duration(milliseconds: i * 33));
        final arrastando = await fase(
          (i) => tempoDoArrasto(Duration(milliseconds: i * 16), velocidade: 12),
        );
        expect(tester.takeException(), isNull);
        expect(tocando.quadros, 90);
        linhas
          ..add('${r.letra} ${r.titulo}')
          ..add('   ${tocando.linha('tocando')}')
          ..add('   ${arrastando.linha('arrastando')}');
      });
    }
  });
}
