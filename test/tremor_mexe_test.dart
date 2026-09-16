// O TREMOR TEM DE TREMER (relato do dono, 16/09: "o atual nao
// funciona").
//
// Ele nunca esteve com a conta errada: os EIXOS e que nasciam em 0,2 e
// 0,1, entao "Amplitude 1" entregava 12 px no X e 6 px no Y em 1080p —
// invisivel, e o efeito passava por quebrado. O eixo agora nasce cheio
// e quem dosa e a Amplitude.
//
// Este teste mede o CENTRO DE MASSA do desenho quadro a quadro: se o
// tremor mexe, ele anda.
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

const double _lado = 400;

EffectInstance _tremor([Map<String, double> sobre = const {}]) {
  final spec = effectSpecs[EffectType.tremor]!;
  return EffectInstance(
    type: EffectType.tremor,
    params: {
      for (final e in spec.params.entries)
        e.key: AnimatedDouble(sobre[e.key] ?? e.value.initial),
    },
  );
}

VideoProject _cena(EffectInstance? fx) => VideoProject(
  name: 'tremor',
  createdAt: DateTime(2026, 9, 16),
  aspectRatio: 1,
  resolutionHeight: _lado.round(),
  backgroundColor: const Color(0xFF000000),
  layers: [
    ShapeLayer(
      id: 'alvo',
      name: 'Alvo',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      position: AnimatedOffset(const Offset(_lado / 2, _lado / 2)),
      contents: [
        ShapePath(primitive: ShapePrimitive.rectangle, width: 90, height: 90),
        ShapeFill(color: const Color(0xFFFFFFFF)),
      ],
      effects: [?fx],
    ),
  ],
);

void main() {
  /// O quanto o desenho passeia no eixo X ao longo de seis quadros.
  Future<double> passeio(WidgetTester tester, EffectInstance? fx) async {
    final tempo = ValueNotifier(Duration.zero);
    final videos = VideoLayerManager();
    final chave = GlobalKey();
    final c = ProviderContainer();
    addTearDown(c.dispose);
    addTearDown(videos.dispose);
    addTearDown(tempo.dispose);
    c.read(editorControllerProvider.notifier).openProject(_cena(fx));
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          debugShowCheckedModeBanner: false,
          home: Material(
            color: const Color(0xFF000000),
            child: Align(
              alignment: Alignment.topLeft,
              child: RepaintBoundary(
                key: chave,
                child: SizedBox(
                  width: _lado,
                  height: _lado,
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
      ),
    );
    final centros = <double>[];
    for (var i = 0; i < 6; i++) {
      tempo.value = Duration(milliseconds: 120 * i);
      await tester.pump();
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 40)),
      );
      await tester.pump();
      final b =
          chave.currentContext!.findRenderObject()! as RenderRepaintBoundary;
      final img = b.toImageSync(pixelRatio: 1);
      final bd = await tester.runAsync(
        () => img.toByteData(format: ui.ImageByteFormat.rawRgba),
      );
      img.dispose();
      final px = bd!.buffer.asUint8List();
      var soma = 0.0;
      var n = 0;
      for (var y = 0; y < _lado.toInt(); y++) {
        for (var x = 0; x < _lado.toInt(); x++) {
          if (px[(y * _lado.toInt() + x) * 4] > 128) {
            soma += x;
            n++;
          }
        }
      }
      if (n > 0) centros.add(soma / n);
    }
    expect(centros, hasLength(6), reason: 'o desenho sumiu de algum quadro');
    await tester.pumpWidget(const SizedBox.shrink());
    final menor = centros.reduce((a, b) => a < b ? a : b);
    final maior = centros.reduce((a, b) => a > b ? a : b);
    return maior - menor;
  }

  testWidgets('sem tremor a camada fica parada', (tester) async {
    tester.view.physicalSize = const Size(_lado, _lado);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    expect(await passeio(tester, null), lessThan(0.5));
  });

  testWidgets('com os valores da ficha, o tremor e VISIVEL', (tester) async {
    tester.view.physicalSize = const Size(_lado, _lado);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    // 12 px num quadro de 400 = ~32 px em 1080p. Antes de 16/09 dava 6.
    expect(
      await passeio(tester, _tremor()),
      greaterThan(12),
      reason: 'a Amplitude tem de valer o que diz',
    );
  });

  testWidgets('cada pronto do tremor sacode', (tester) async {
    tester.view.physicalSize = const Size(_lado, _lado);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    for (final p in effectSpecs[EffectType.tremor]!.presets) {
      final d = await passeio(tester, _tremor(p.valores));
      expect(d, greaterThan(6), reason: 'o pronto "${p.nome}" mal se mexe');
    }
  });
}
