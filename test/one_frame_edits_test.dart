// "ADD TIMESLICE, TWITCH, MELHORE O SHAKE... ADD EFEITOS PARA ONE FRAME
// EDITS (PESQUISE)" (dono, 14/09/2026).
//
// Os efeitos de batida contam QUADROS. Aqui ficam presas as contas
// (gatilho, envelope, soco, strobe, pulso do Twitch, impacto do Shake) e
// o desenho de verdade de Flash, Strobe e Zoom Punch sobre uma forma.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/fx.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/one_frame.dart';
import 'package:aurea/src/features/editor/domain/pixel_effect.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/pixel_effect_engine.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

Duration _quadro(int f, [int fps = 30]) =>
    Duration(microseconds: (f * 1000000 / fps).ceil());

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  group('contas de quadro', () {
    test('o quadro local encaixa no instante que o relogio publica', () {
      for (var f = 0; f < 200; f++) {
        expect(quadroLocal(_quadro(f), 30), f);
        expect(quadroLocal(_quadro(f, 24), 24), f);
      }
      expect(quadroLocal(const Duration(microseconds: -5), 30), -1);
    });

    test('gatilhos: inicio, a cada N, aleatorio e sempre', () {
      expect(quadrosDesdeODisparo(f: 7, gatilho: Gatilho.inicioDaCamada), 7);
      expect(quadrosDesdeODisparo(f: 17, gatilho: Gatilho.aCadaN, periodo: 8), 1);
      expect(quadrosDesdeODisparo(f: 16, gatilho: Gatilho.aCadaN, periodo: 8), 0);
      expect(quadrosDesdeODisparo(f: 50, gatilho: Gatilho.sempre), 0);
      expect(quadrosDesdeODisparo(f: -1, gatilho: Gatilho.sempre), isNull);

      // Probabilidade zero nunca dispara; um dispara em toda celula.
      for (var f = 0; f < 120; f++) {
        expect(
          quadrosDesdeODisparo(
            f: f,
            gatilho: Gatilho.aleatorio,
            periodo: 6,
            probabilidade: 0,
          ),
          isNull,
        );
        final tau = quadrosDesdeODisparo(
          f: f,
          gatilho: Gatilho.aleatorio,
          periodo: 6,
          probabilidade: 1,
          semente: 3,
        );
        // O disparo cai dentro da celula: nunca mais que duas celulas atras.
        if (f >= 12) expect(tau, isNotNull);
        if (tau != null) expect(tau, lessThan(12));
      }
      // Deterministico.
      final a = [
        for (var f = 0; f < 90; f++)
          quadrosDesdeODisparo(
            f: f,
            gatilho: Gatilho.aleatorio,
            periodo: 5,
            probabilidade: .4,
            semente: 11,
          ),
      ];
      final b = [
        for (var f = 0; f < 90; f++)
          quadrosDesdeODisparo(
            f: f,
            gatilho: Gatilho.aleatorio,
            periodo: 5,
            probabilidade: .4,
            semente: 11,
          ),
      ];
      expect(a, b);
    });

    test('envelope segura N quadros e cai em M', () {
      expect(envelopeHoldDecay(0, hold: 1, decay: 0), 1);
      expect(envelopeHoldDecay(1, hold: 1, decay: 0), 0);
      expect(envelopeHoldDecay(1, hold: 2, decay: 4), 1);
      final queda = [
        for (var t = 2; t <= 6; t++) envelopeHoldDecay(t, hold: 2, decay: 4, gama: 2),
      ];
      for (var i = 1; i < queda.length; i++) {
        expect(queda[i], lessThan(queda[i - 1]));
      }
      expect(queda.last, 0);
    });

    test('zoom de soco: pico, hold e volta a 100%', () {
      expect(escalaDoSoco(0, pico: 120, hold: 1, soltura: 0), closeTo(1.2, 1e-9));
      expect(escalaDoSoco(1, pico: 120, hold: 1, soltura: 0), 1);
      expect(escalaDoSoco(0, pico: 140, hold: 1, soltura: 6), closeTo(1.4, 1e-9));
      final volta = [for (var t = 1; t <= 7; t++) escalaDoSoco(t, pico: 140, hold: 1, soltura: 6)];
      for (var i = 1; i < volta.length; i++) {
        expect(volta[i], lessThanOrEqualTo(volta[i - 1]));
      }
      expect(volta.last, 1);
      // Com ataque, o primeiro quadro ainda nao chegou ao pico.
      expect(escalaDoSoco(0, pico: 140, ataque: 2), lessThan(1.4));
      // Quique termina parado.
      expect(escalaDoSoco(40, pico: 140, soltura: 6, curva: 2), 1);
    });

    test('strobe periodico e aleatorio', () {
      final periodico = [
        for (var f = 0; f < 8; f++) strobeAceso(f: f, aleatorio: false, periodo: 4, duracao: 2),
      ];
      expect(periodico, [true, true, false, false, true, true, false, false]);
      final nunca = [
        for (var f = 0; f < 40; f++)
          strobeAceso(f: f, aleatorio: true, probabilidade: 0),
      ];
      expect(nunca, everyElement(isFalse));
      // Aleatorio segura blocos de [duracao] quadros.
      for (var f = 0; f < 60; f += 3) {
        final bloco = [
          for (var k = 0; k < 3; k++)
            strobeAceso(f: f + k, aleatorio: true, duracao: 3, probabilidade: .5, semente: 4),
        ];
        expect(bloco.toSet(), hasLength(1));
      }
    });

    test('impacto do Shake cai pela metade a cada meia-vida', () {
      expect(envelopeDeImpacto(0, meiaVida: 5), 1);
      expect(envelopeDeImpacto(5, meiaVida: 5), closeTo(.5, 1e-9));
      expect(envelopeDeImpacto(10, meiaVida: 5), closeTo(.25, 1e-9));
      expect(envelopeDeImpacto(0, ataque: 3, meiaVida: 5), lessThan(1));
      expect(socoDoShake(0, 6), 1);
      expect(socoDoShake(6, 6), 0);
    });

    test('Shake: uma oitava e sem serrilhado da exatamente o de antes', () {
      const antigo = ShakeAxis(randomAmplitude: .7, randomFrequency: 1.3);
      const novo = ShakeAxis(
        randomAmplitude: .7,
        randomFrequency: 1.3,
        octaves: 1,
        jaggedness: 0,
      );
      for (var i = 0; i < 50; i++) {
        final fase = i * .173;
        expect(
          novo.valueAt(9, 1, fase),
          closeTo(fxNoiseSigned(9, 1, fase * 1.3) * .7, 1e-12),
        );
        expect(novo.valueAt(9, 1, fase), antigo.valueAt(9, 1, fase));
      }
      // Serrilhado 1: o valor fica parado dentro de um ciclo.
      const seco = ShakeAxis(randomAmplitude: 1, jaggedness: 1);
      expect(seco.valueAt(2, 1, 3.1), seco.valueAt(2, 1, 3.9));
      expect(seco.valueAt(2, 1, 3.9), isNot(seco.valueAt(2, 1, 4.1)));
    });

    test('Twitch: quieto nunca pulsa; pulso e deterministico e limitado', () {
      PulsoTwitch em(double t, {double quietude = .5, int semente = 7}) =>
          pulsoTwitch(
            semente: semente,
            fluxo: 2000,
            t: t,
            velocidade: 4,
            quietude: quietude,
            minimo: .3,
            duracaoSeg: 2 / 30,
          );
      for (var f = 0; f < 120; f++) {
        expect(em(f / 30, quietude: 1).v, 0);
      }
      var pulsos = 0;
      for (var f = 0; f < 240; f++) {
        final p = em(f / 30, quietude: 0);
        expect(p.v, inInclusiveRange(0, 1));
        expect(p.v, em(f / 30, quietude: 0).v);
        if (p.v > 0) pulsos++;
      }
      // Quatro celulas por segundo, dois quadros cada: ~16 quadros em 8 s.
      expect(pulsos, greaterThan(20));
      expect(pulsos, lessThan(120), reason: 'o pulso dura dois quadros');
    });

    test('matrizes do flash', () {
      List<double> aplicar(List<double> m, List<double> c) => [
        for (var i = 0; i < 3; i++)
          (m[i * 5] * c[0] + m[i * 5 + 1] * c[1] + m[i * 5 + 2] * c[2] + m[i * 5 + 4] / 255)
              .clamp(0.0, 1.0),
      ];
      final branco = matrizDoFlash(0, 1, r: 1, g: 1, b: 1);
      expect(aplicar(branco, [.2, .5, .7]), [1, 1, 1]);
      final metade = matrizDoFlash(4, .5, r: 1, g: 1, b: 1);
      for (final v in aplicar(metade, [.2, .5, .9])) {
        expect(v, closeTo(.5, 1e-9));
      }
      final exposicao = matrizDoFlash(3, 1, r: 1, g: 1, b: 1, stops: 1);
      expect(aplicar(exposicao, [.2, .3, .4]), [
        closeTo(.4, 1e-9),
        closeTo(.6, 1e-9),
        closeTo(.8, 1e-9),
      ]);
    });
  });

  group('desenho', () {
    ShapeLayer alvo(List<EffectInstance> efeitos) => ShapeLayer(
      id: 'alvo',
      name: 'Alvo',
      startTime: Duration.zero,
      duration: const Duration(seconds: 10),
      position: AnimatedOffset(const Offset(64, 64)),
      contents: [
        ShapePath(primitive: ShapePrimitive.rectangle, width: 40, height: 40),
        ShapeFill(color: const Color(0xFF2050C0)),
      ],
      effects: efeitos,
    );

    Future<Uint8List> pixels(
      WidgetTester tester,
      List<EffectInstance> efeitos,
      Duration t,
    ) async {
      final projeto = VideoProject(
        name: 'one-frame',
        createdAt: DateTime(2026, 9, 14),
        aspectRatio: 1,
        resolutionHeight: 128,
        backgroundColor: const Color(0xFF000000),
        layers: [alvo(efeitos)],
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

    List<int> px(Uint8List b, int x, int y) =>
        b.sublist((y * 128 + x) * 4, (y * 128 + x) * 4 + 3);

    testWidgets('Flash branco no quadro da batida e some depois', (tester) async {
      tester.view.physicalSize = const Size(256, 256);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final flash = EffectInstance(
        type: EffectType.flash,
        params: {
          'hold': AnimatedDouble(1),
          'decay': AnimatedDouble(0),
          'trigger': AnimatedDouble(1),
          'period': AnimatedDouble(10),
        },
      );
      final naBatida = await pixels(tester, [flash], _quadro(10));
      expect(px(naBatida, 64, 64), [255, 255, 255], reason: 'quadro 10 e batida');
      final depois = await pixels(tester, [flash], _quadro(11));
      expect(px(depois, 64, 64), [0x20, 0x50, 0xC0], reason: 'quadro seguinte limpo');
    });

    testWidgets('Strobe transparente apaga a camada nos quadros acesos', (
      tester,
    ) async {
      tester.view.physicalSize = const Size(256, 256);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final strobe = EffectInstance(
        type: EffectType.strobe,
        params: {
          'period': AnimatedDouble(2),
          'duration': AnimatedDouble(1),
          'operation': AnimatedDouble(0),
        },
      );
      final aceso = await pixels(tester, [strobe], _quadro(4));
      expect(px(aceso, 64, 64), [0, 0, 0]);
      final apagado = await pixels(tester, [strobe], _quadro(5));
      expect(px(apagado, 64, 64), [0x20, 0x50, 0xC0]);
    });

    testWidgets('Zoom Punch cresce a camada no quadro da batida', (tester) async {
      tester.view.physicalSize = const Size(256, 256);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final soco = EffectInstance(
        type: EffectType.zoomPunch,
        params: {
          'peak': AnimatedDouble(180),
          'hold': AnimatedDouble(1),
          'release': AnimatedDouble(0),
          'zoom_blur': AnimatedDouble(0),
        },
      );
      // O retangulo de 40 vai de x = 44 a 84; a 180% chega a x = 28.
      final semSoco = await pixels(tester, const [], Duration.zero);
      expect(px(semSoco, 34, 64), [0, 0, 0]);
      final comSoco = await pixels(tester, [soco], Duration.zero);
      expect(px(comSoco, 34, 64), [0x20, 0x50, 0xC0]);
      final depois = await pixels(tester, [soco], _quadro(1));
      expect(px(depois, 34, 64), [0, 0, 0]);
    });
  });

  group('Slice Glitch no shader', () {
    setUpAll(() async {
      await PixelEffectEngine.warmUp();
    });

    Future<Uint8List> render(PixelEffectFrame frame) async {
      const w = 64, h = 64;
      final bytes = Uint8List(w * h * 4);
      for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
          final i = (y * w + x) * 4;
          bytes[i] = x * 4;
          bytes[i + 1] = y * 4;
          bytes[i + 2] = 90;
          bytes[i + 3] = 255;
        }
      }
      final buffer = await ui.ImmutableBuffer.fromUint8List(bytes);
      final descriptor = ui.ImageDescriptor.raw(
        buffer,
        width: w,
        height: h,
        pixelFormat: ui.PixelFormat.rgba8888,
      );
      final codec = await descriptor.instantiateCodec();
      final input = (await codec.getNextFrame()).image;
      final recorder = ui.PictureRecorder();
      final shader = PixelEffectEngine.createShader(
        frame,
        width: w.toDouble(),
        height: h.toDouble(),
        image: input,
      );
      ui.Canvas(recorder).drawRect(
        const ui.Rect.fromLTWH(0, 0, 64, 64),
        ui.Paint()..shader = shader,
      );
      final picture = recorder.endRecording();
      final out = await picture.toImage(w, h);
      final data = await out.toByteData(format: ui.ImageByteFormat.rawStraightRgba);
      for (final d in [picture, shader, out, input, codec, descriptor, buffer]) {
        (d as dynamic).dispose();
      }
      return data!.buffer.asUint8List();
    }

    PixelEffectFrame quadro(Map<String, double> v) {
      final k = pixelKernels[EffectType.sliceGlitch]!;
      final spec = effectSpecs[EffectType.sliceGlitch]!;
      return PixelEffectFrame(
        k.mode,
        [for (final key in k.keys) v[key] ?? spec.params[key]!.initial],
        time: .5,
      );
    }

    test('sem fatias, sem blocos e sem cor: a imagem passa intacta', () async {
      final saida = await render(quadro({'probability': 0, 'block_size': 0}));
      for (final (x, y) in [(3, 3), (30, 40), (60, 12)]) {
        final i = (y * 64 + x) * 4;
        expect(saida[i], closeTo(x * 4, 1.5));
        expect(saida[i + 1], closeTo(y * 4, 1.5));
      }
    });

    test('com todas as fatias ativas, as linhas deslizam', () async {
      final saida = await render(
        quadro({'probability': 1, 'offset': 25, 'rgb_offset': 0, 'slices': 8}),
      );
      var deslocados = 0;
      for (var y = 2; y < 64; y += 8) {
        final i = (y * 64 + 32) * 4;
        if ((saida[i] - 32 * 4).abs() > 6) deslocados++;
      }
      expect(deslocados, greaterThan(4));
    });
  });
}
