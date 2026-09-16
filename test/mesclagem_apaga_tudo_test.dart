// "TUDO SIMPLESMENTE SOME... FICA ESCURO" (relato do dono, 16/09).
//
// Medindo a luz media do quadro com cada mesclagem, uma salta: uma
// camada em EXCLUIR (dstOut) no nivel de cima nao recorta so quem esta
// perto — ela apaga a composicao INTEIRA, fundo e tudo, e o quadro vai
// a zero. Quem esta editando ve o projeto sumir sem entender por que.
//
// Dentro de um grupo isso ja nao acontece: o palco isola o knockout
// entre os irmaos (ver o BlendMask com isolate no ramo de GroupLayer).
// E por isso que AGRUPAR "conserta" e desagrupar "quebra" — o relato
// do dono batia, so que ao contrario do que parecia.
//
// Este teste fixa os dois lados para o conserto nao passar em branco.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

const double _L = 300;

ShapeLayer _quad(String id, Color cor, {BlendMode bm = BlendMode.srcOver}) =>
    ShapeLayer(
      id: id,
      name: id,
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      position: AnimatedOffset(const Offset(_L / 2, _L / 2)),
      blendMode: bm,
      contents: [
        ShapePath(primitive: ShapePrimitive.rectangle, width: 140, height: 140),
        ShapeFill(color: cor),
      ],
    );

VideoProject _cena(BlendMode bm) => VideoProject(
  name: 'g',
  createdAt: DateTime(2026, 9, 16),
  aspectRatio: 1,
  resolutionHeight: _L.round(),
  backgroundColor: const Color(0xFF202030),
  layers: [
    _quad('cima', const Color(0xFFEE5555), bm: bm),
    _quad('baixo', const Color(0xFF55AAEE)),
  ],
);

Future<double> _luzMedia(
  WidgetTester tester,
  BlendMode bm, {
  required bool agrupar,
}) async {
  final tempo = ValueNotifier(Duration.zero);
  final videos = VideoLayerManager();
  final chave = GlobalKey();
  final c = ProviderContainer();
  addTearDown(c.dispose);
  addTearDown(videos.dispose);
  addTearDown(tempo.dispose);
  final e = c.read(editorControllerProvider.notifier);
  e.openProject(_cena(bm));
  if (agrupar) e.groupLayer('cima');
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        debugShowCheckedModeBanner: false,
        home: Material(
          color: const Color(0xFF202030),
          child: Align(
            alignment: Alignment.topLeft,
            child: RepaintBoundary(
              key: chave,
              child: SizedBox(
                width: _L,
                height: _L,
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
  await tester.pump();
  await tester.runAsync(
    () => Future<void>.delayed(const Duration(milliseconds: 60)),
  );
  await tester.pump();
  final b = chave.currentContext!.findRenderObject()! as RenderRepaintBoundary;
  final img = b.toImageSync(pixelRatio: 1);
  final bd = await tester.runAsync(
    () => img.toByteData(format: ui.ImageByteFormat.rawRgba),
  );
  img.dispose();
  final Uint8List px = bd!.buffer.asUint8List();
  var soma = 0.0;
  var n = 0;
  for (var i = 0; i < px.length; i += 4) {
    soma += (px[i] + px[i + 1] + px[i + 2]) / 3;
    n++;
  }
  await tester.pumpWidget(const SizedBox.shrink());
  return soma / n;
}

void main() {
  testWidgets('as mesclagens comuns nao dependem de estar num grupo', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(_L, _L);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    for (final bm in [
      BlendMode.srcOver,
      BlendMode.multiply,
      BlendMode.screen,
      BlendMode.plus,
      BlendMode.difference,
    ]) {
      final solta = await _luzMedia(tester, bm, agrupar: false);
      final agrupada = await _luzMedia(tester, bm, agrupar: true);
      expect(
        agrupada,
        closeTo(solta, 6),
        reason: 'agrupar mudou o desenho em ${bm.name}',
      );
    }
  });

  testWidgets('EXCLUIR no nivel de cima NAO apaga mais a composicao', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(_L, _L);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final solta = await _luzMedia(tester, BlendMode.dstOut, agrupar: false);
    final agrupada = await _luzMedia(tester, BlendMode.dstOut, agrupar: true);

    // CONSERTADO (16/09, escolha do dono): recortar so corta quem esta
    // na mesma pilha. O fundo da composicao sobrevive, e o quadro nunca
    // mais vai a zero.
    expect(
      solta,
      greaterThan(20),
      reason: 'a camada que recorta voltou a apagar a composicao inteira',
    );
    // E estar num grupo deixou de fazer diferenca — era o sintoma que
    // fazia "agrupar" parecer o conserto.
    expect(
      agrupada,
      closeTo(solta, 6),
      reason: 'recortar nao pode depender de estar num grupo',
    );
  });
}
