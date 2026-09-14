// "ADD TIMESLICE" (dono, 14/09/2026).
//
// Time Slice e Posterize Time montam a camada inteira (ou o composto
// abaixo de uma camada de ajuste) em OUTROS instantes. Aqui ficam presas
// as contas das faixas e dos degraus, os instantes que a exportacao tem
// de decodificar, e o desenho de verdade: cada faixa mostra a forma no
// instante dela.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/time_slice.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('contas', () {
    test('escada: simetrica, um quadro por faixa com o maximo classico', () {
      final a = atrasosDasFaixas(faixas: 12, distribuicao: 0, maximo: 5.5);
      expect(a, hasLength(12));
      expect(a.first, -a.last);
      for (var i = 1; i < a.length; i++) {
        expect(a[i], greaterThanOrEqualTo(a[i - 1]));
      }
      expect(atrasosDasFaixas(faixas: 1, distribuicao: 0, maximo: 9), [0]);
    });

    test('linear, centro, aleatoria e onda', () {
      final linear = atrasosDasFaixas(faixas: 16, distribuicao: 1, maximo: -15);
      expect(linear.first, 0);
      expect(linear.last, -15);
      final centro = atrasosDasFaixas(faixas: 9, distribuicao: 2, maximo: 8);
      expect(centro[4], 0);
      expect(centro.first, 8);
      expect(centro.last, 8);
      final sorteio = atrasosDasFaixas(
        faixas: 24,
        distribuicao: 3,
        maximo: 8,
        semente: 7,
      );
      expect(sorteio, everyElement(inInclusiveRange(-8, 8)));
      expect(
        sorteio,
        atrasosDasFaixas(faixas: 24, distribuicao: 3, maximo: 8, semente: 7),
      );
      expect(sorteio.toSet().length, greaterThan(4));
      final onda = atrasosDasFaixas(
        faixas: 5,
        distribuicao: 4,
        maximo: 10,
        ciclos: .25,
      );
      expect(onda.first, 0);
      expect(onda.last, 10);
      // Deslocamento global soma a todas.
      final soma = atrasosDasFaixas(
        faixas: 3,
        distribuicao: 1,
        maximo: 0,
        deslocamento: -4,
      );
      expect(soma, [-4, -4, -4]);
    });

    test('as faixas cobrem o quadro sem sobrar nem sobrepor', () {
      const tamanho = Size(160, 90);
      for (final angulo in [0.0, 90.0, 33.0, 211.0]) {
        final faixas = [
          for (var k = 0; k < 7; k++)
            faixaDoTimeSlice(tamanho, anguloGraus: angulo, k: k, n: 7),
        ];
        for (var y = 1.5; y < 90; y += 7.3) {
          for (var x = 1.5; x < 160; x += 9.1) {
            final dentro = faixas.where((f) => f.contains(Offset(x, y))).length;
            expect(dentro, 1, reason: 'angulo $angulo em ($x, $y)');
          }
        }
      }
      // 90 graus: faixas horizontais, a primeira em cima.
      final topo = faixaDoTimeSlice(tamanho, anguloGraus: 90, k: 0, n: 2);
      expect(topo.contains(const Offset(80, 10)), isTrue);
      expect(topo.contains(const Offset(80, 80)), isFalse);
      // Vao: a borda entre duas faixas fica vazia.
      final comVao = [
        faixaDoTimeSlice(tamanho, anguloGraus: 90, k: 0, n: 2, vao: 6),
        faixaDoTimeSlice(tamanho, anguloGraus: 90, k: 1, n: 2, vao: 6),
      ];
      expect(comVao.any((f) => f.contains(const Offset(80, 45))), isFalse);
    });

    test('posterize: degraus da taxa pedida, com fase', () {
      Duration ms(int v) => Duration(milliseconds: v);
      expect(localPosterizado(ms(0), 12, 0), Duration.zero);
      expect(localPosterizado(ms(80), 12, 0), Duration.zero);
      expect(localPosterizado(ms(90), 12, 0), const Duration(microseconds: 83333));
      expect(localPosterizado(ms(990), 2, 0), ms(500));
      expect(localPosterizado(ms(990), 2, .5), ms(750));
    });

    test('deslocar prende o instante no intervalo da camada', () {
      final camada = ShapeLayer(
        id: 's',
        name: 's',
        startTime: const Duration(seconds: 1),
        duration: const Duration(seconds: 2),
        contents: const [],
      );
      expect(
        localDeslocado(camada, const Duration(milliseconds: 100), -30, 30),
        Duration.zero,
      );
      expect(
        localDeslocado(camada, const Duration(milliseconds: 1900), 30, 30),
        const Duration(seconds: 2) - const Duration(microseconds: 1),
      );
      expect(
        localDeslocado(camada, const Duration(seconds: 1), 15, 30),
        const Duration(milliseconds: 1500),
      );
    });

    test('a exportacao sabe quais outros instantes decodificar', () {
      final camada = ShapeLayer(
        id: 's',
        name: 's',
        startTime: Duration.zero,
        duration: const Duration(seconds: 10),
        contents: const [],
        effects: [
          EffectInstance(
            type: EffectType.timeSlice,
            params: {
              'slices': AnimatedDouble(3),
              'distribution': AnimatedDouble(1),
              'max_offset': AnimatedDouble(-30),
            },
          ),
        ],
      );
      final instantes = instantesDeOutroTempo(
        [camada],
        const Duration(seconds: 2),
        30,
      );
      expect(instantes, {
        const Duration(milliseconds: 1500),
        const Duration(seconds: 1),
      });
      final parada = instantesDeOutroTempo([camada], const Duration(seconds: 20), 30);
      expect(parada, isEmpty, reason: 'camada fora do ar nao pede nada');
    });
  });

  group('desenho', () {
    // Barra vertical de 20 x 100 que anda de x = 20 (0 s) a x = 108 (1 s).
    ShapeLayer barra({List<EffectInstance> efeitos = const []}) => ShapeLayer(
      id: 'barra',
      name: 'Barra',
      startTime: Duration.zero,
      duration: const Duration(seconds: 10),
      position: AnimatedOffset(const Offset(20, 64), const [
        Keyframe(time: Duration.zero, value: Offset(20, 64)),
        Keyframe(time: Duration(seconds: 1), value: Offset(108, 64)),
      ]),
      contents: [
        ShapePath(primitive: ShapePrimitive.rectangle, width: 20, height: 100),
        ShapeFill(color: const Color(0xFFFFFFFF)),
      ],
      effects: efeitos,
    );

    Future<Uint8List> pixels(
      WidgetTester tester,
      List<Layer> camadas,
      Duration t,
    ) async {
      final projeto = VideoProject(
        name: 'time-slice',
        createdAt: DateTime(2026, 9, 14),
        aspectRatio: 1,
        resolutionHeight: 128,
        fps: 30,
        backgroundColor: const Color(0xFF000000),
        layers: camadas,
      );
      final container = ProviderContainer();
      addTearDown(container.dispose);
      container.read(editorControllerProvider.notifier).openProject(projeto);
      final tempo = ValueNotifier(t);
      addTearDown(tempo.dispose);
      final videos = VideoLayerManager();
      addTearDown(videos.dispose);
      final chave = GlobalKey();
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            home: Center(
              child: RepaintBoundary(
                key: chave,
                child: SizedBox(
                  width: 128,
                  height: 128,
                  child: CompositionView(
                    time: tempo,
                    videos: videos,
                    selectedId: null,
                    exporting: true,
                  ),
                ),
              ),
            ),
          ),
        ),
      );
      await tester.pump();
      final boundary =
          chave.currentContext!.findRenderObject() as RenderRepaintBoundary;
      final imagem = boundary.toImageSync(pixelRatio: 1);
      final dados = await tester.runAsync(
        () => imagem.toByteData(format: ui.ImageByteFormat.rawStraightRgba),
      );
      imagem.dispose();
      return dados!.buffer.asUint8List();
    }

    bool branco(Uint8List b, int x, int y) => b[(y * 128 + x) * 4] > 200;

    void preparar(WidgetTester tester) {
      tester.view.physicalSize = const Size(256, 256);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
    }

    // Duas faixas horizontais; a de baixo atrasada 15 quadros (0,5 s).
    EffectInstance fatias() => EffectInstance(
      type: EffectType.timeSlice,
      params: {
        'slices': AnimatedDouble(2),
        'angle': AnimatedDouble(90),
        'distribution': AnimatedDouble(1),
        'max_offset': AnimatedDouble(-15),
      },
    );

    testWidgets('cada faixa mostra a camada no instante dela', (tester) async {
      preparar(tester);
      final b = await pixels(tester, [
        barra(efeitos: [fatias()]),
      ], const Duration(seconds: 1));
      // Em cima: agora (x = 108). Embaixo: meio segundo atras (x = 64).
      expect(branco(b, 108, 30), isTrue);
      expect(branco(b, 64, 30), isFalse);
      expect(branco(b, 64, 100), isTrue);
      expect(branco(b, 108, 100), isFalse);
    });

    testWidgets('numa camada de ajuste, fatia o que esta embaixo', (tester) async {
      preparar(tester);
      final ajuste = AdjustmentLayer(
        id: 'ajuste',
        name: 'Ajuste',
        startTime: Duration.zero,
        duration: const Duration(seconds: 10),
        effects: [fatias()],
      );
      final b = await pixels(tester, [ajuste, barra()], const Duration(seconds: 1));
      expect(branco(b, 108, 30), isTrue);
      expect(branco(b, 64, 100), isTrue);
      expect(branco(b, 108, 100), isFalse);
    });

    testWidgets('Posterize Time segura a camada no degrau', (tester) async {
      preparar(tester);
      final poster = EffectInstance(
        type: EffectType.posterizeTime,
        params: {'rate': AnimatedDouble(2)},
      );
      // 0,9 s a 2 fps mostra o instante 0,5 s (x = 64).
      final b = await pixels(tester, [
        barra(efeitos: [poster]),
      ], const Duration(milliseconds: 900));
      expect(branco(b, 64, 64), isTrue);
      final sem = await pixels(tester, [barra()], const Duration(milliseconds: 900));
      expect(branco(sem, 64, 64), isFalse);
    });
  });
}
