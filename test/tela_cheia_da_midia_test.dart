// TELA CHEIA DE ACORDO COM A RESOLUCAO (relato do beta 1.0.5).
//
// O testador importou uma foto, ampliou na mao ate 198,6% para cobrir a
// composicao e, ao escolher uma mesclagem, a imagem "diminuiu o quadrado".
// Dono: "o app tem que importar qualquer coisa e no preview ficar a tela
// cheia de acordo com a resolucao". O que fica preso aqui:
//   * a caixa de cobrir, conter e a antiga pela largura;
//   * midia importada nasce cobrindo; Preencher e Ajustar voltam a 100%;
//   * a proporcao do probe nao vira passo de desfazer;
//   * projeto novo a partir da midia tem a proporcao dela;
//   * o arquivo guarda o ajuste e o projeto antigo abre como antes;
//   * o palco desenha na caixa certa;
//   * a mesclagem nao recorta a camada ampliada.
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/ajuste_da_midia.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/measure.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/blend_mask.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

VideoProject _projeto({double aspecto = 9 / 16, List<Layer> camadas = const []}) =>
    VideoProject(
      name: 'tela cheia',
      createdAt: DateTime(2026, 9, 14),
      aspectRatio: aspecto,
      resolutionHeight: 1080,
      fps: 30,
      backgroundColor: const Color(0xFF000000),
      layers: camadas,
    );

void main() {
  group('caixa da midia', () {
    const vertical = Size(1080, 1920);
    const deitada = Size(1920, 1080);

    test('video deitado cobrindo projeto em pe: altura inteira', () {
      final c = caixaDaMidia(vertical, 16 / 9, AjusteDaMidia.cobrir);
      expect(c.height, 1920);
      expect(c.width, closeTo(3413.33, 0.01));
      expect(c.width >= vertical.width && c.height >= vertical.height, isTrue);
    });

    test('video em pe cobrindo projeto deitado: largura inteira', () {
      final c = caixaDaMidia(deitada, 9 / 16, AjusteDaMidia.cobrir);
      expect(c.width, 1920);
      expect(c.height, closeTo(3413.33, 0.01));
    });

    test('conter cabe inteira e encosta num lado', () {
      final c = caixaDaMidia(vertical, 16 / 9, AjusteDaMidia.conter);
      expect(c.width, 1080);
      expect(c.height, closeTo(607.5, 0.01));
      final d = caixaDaMidia(deitada, 9 / 16, AjusteDaMidia.conter);
      expect(d.height, 1080);
      expect(d.width, closeTo(607.5, 0.01));
    });

    test('pela largura continua o de antes, inclusive sem proporcao', () {
      expect(caixaDaMidia(vertical, 16 / 9, AjusteDaMidia.largura),
          const Size(1080, 607.5));
      expect(caixaDaMidia(vertical, null, AjusteDaMidia.largura),
          const Size(1080, 607.5));
      expect(caixaDaMidia(vertical, null, AjusteDaMidia.cobrir), vertical);
      expect(caixaDaMidia(vertical, double.nan, AjusteDaMidia.conter), vertical);
    });

    test('a proporcao exibida troca com um quarto de volta', () {
      expect(proporcaoExibidaDoVideo(16 / 9, 0), 16 / 9);
      expect(proporcaoExibidaDoVideo(16 / 9, 90), closeTo(9 / 16, 1e-12));
      expect(proporcaoExibidaDoVideo(16 / 9, 180), 16 / 9);
      expect(proporcaoExibidaDoVideo(16 / 9, 270), closeTo(9 / 16, 1e-12));
      expect(proporcaoExibidaDoVideo(0, 90), isNull);
    });

    test('projeto novo tem a proporcao da midia, encostando nas comuns', () {
      expect(proporcaoDoProjeto(1920 / 1088), 16 / 9);
      expect(proporcaoDoProjeto(1080 / 1920), 9 / 16);
      expect(proporcaoDoProjeto(3024 / 4032), 3 / 4);
      expect(proporcaoDoProjeto(null), 9 / 16);
      expect(proporcaoDoProjeto(5), 21 / 9);
      expect(proporcaoDoProjeto(1.9), 1.9);
    });

    test('a medida da camada e a caixa do ajuste', () {
      final v = VideoLayer(
        name: 'v',
        startTime: Duration.zero,
        duration: const Duration(seconds: 2),
        sourcePath: 'x.mp4',
        ajuste: AjusteDaMidia.cobrir,
        proporcaoDaFonte: 16 / 9,
        scaleX: AnimatedDouble(0.5),
        scaleY: AnimatedDouble(0.5),
      );
      final caixa = measureLayerBox(v, Duration.zero,
          fallbackWidth: 1080, compHeight: 1920);
      expect(caixa.height, closeTo(960, 1e-6));
      expect(caixa.width, closeTo(1706.67, 0.01));
    });
  });

  group('editor', () {
    test('midia nasce cobrindo; Preencher e Ajustar voltam a 100% no centro',
        () {
      final container = ProviderContainer();
      addTearDown(container.dispose);
      final e = container.read(editorControllerProvider.notifier);
      e.openProject(_projeto());
      final id = e.addVideoLayer(
        Duration.zero,
        'x.mp4',
        'x',
        const Duration(seconds: 3),
        proporcao: 16 / 9,
      );
      var v = container.read(editorControllerProvider).layerById(id)!
          as VideoLayer;
      expect(v.ajuste, AjusteDaMidia.cobrir);
      expect(v.proporcaoDaFonte, 16 / 9);

      e.editScaleUniform(id, Duration.zero, 1.986);
      e.setAjusteDaMidia(id, AjusteDaMidia.conter);
      v = container.read(editorControllerProvider).layerById(id)! as VideoLayer;
      expect(v.ajuste, AjusteDaMidia.conter);
      expect(v.scaleX.valueAt(Duration.zero), 1);
      expect(v.position.valueAt(Duration.zero), const Offset(540, 960));

      e.undo();
      v = container.read(editorControllerProvider).layerById(id)! as VideoLayer;
      expect(v.ajuste, AjusteDaMidia.cobrir, reason: 'Ajustar e um passo so');
    });

    test('a proporcao que chega do probe nao vira passo de desfazer', () {
      final container = ProviderContainer();
      addTearDown(container.dispose);
      final e = container.read(editorControllerProvider.notifier);
      e.openProject(_projeto());
      final id = e.addImageLayer(Duration.zero, 'nao-existe.png', 'foto');
      e.definirProporcaoDaMidia(id, 4 / 3);
      final foto = container.read(editorControllerProvider).layerById(id)!
          as ImageLayer;
      expect(foto.ajuste, AjusteDaMidia.cobrir);
      expect(foto.proporcaoDaFonte, 4 / 3);
      e.undo();
      expect(container.read(editorControllerProvider).layerById(id), isNull,
          reason: 'o primeiro desfazer tira a foto, nao a medida');
    });
  });

  group('arquivo', () {
    test('ajuste e proporcao vao e voltam; projeto antigo abre pela largura',
        () {
      final p = _projeto(camadas: [
        ImageLayer(
          name: 'foto',
          startTime: Duration.zero,
          duration: const Duration(seconds: 3),
          sourcePath: 'a.jpg',
          ajuste: AjusteDaMidia.conter,
          proporcaoDaFonte: 3 / 4,
        ),
        VideoLayer(
          name: 'video',
          startTime: Duration.zero,
          duration: const Duration(seconds: 3),
          sourcePath: 'b.mp4',
          ajuste: AjusteDaMidia.cobrir,
          proporcaoDaFonte: 16 / 9,
        ),
        VideoLayer(
          name: 'antigo',
          startTime: Duration.zero,
          duration: const Duration(seconds: 3),
          sourcePath: 'c.mp4',
        ),
      ]);
      final json = projectToJson(p);
      final volta = projectFromJson(json);
      final foto = volta.layers[0] as ImageLayer;
      final video = volta.layers[1] as VideoLayer;
      final antigo = volta.layers[2] as VideoLayer;
      expect(foto.ajuste, AjusteDaMidia.conter);
      expect(foto.proporcaoDaFonte, closeTo(0.75, 1e-12));
      expect(video.ajuste, AjusteDaMidia.cobrir);
      expect(antigo.ajuste, AjusteDaMidia.largura);
      expect(antigo.proporcaoDaFonte, isNull);
      final camadas = (json['layers'] as List).cast<Map<String, dynamic>>();
      expect(camadas[2].containsKey('ajuste'), isFalse,
          reason: 'pela largura nao e gravado: arquivo antigo fica igual');
    });
  });

  testWidgets('o palco desenha a foto na caixa de cobrir', (tester) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    container.read(editorControllerProvider.notifier).openProject(_projeto(
          aspecto: 1,
          camadas: [
            ImageLayer(
              id: 'foto',
              name: 'foto',
              startTime: Duration.zero,
              duration: const Duration(seconds: 3),
              sourcePath: 'nao-existe.png',
              ajuste: AjusteDaMidia.cobrir,
              proporcaoDaFonte: 2,
              position: AnimatedOffset(const Offset(540, 540)),
            ),
          ],
        ));
    final tempo = ValueNotifier(Duration.zero);
    addTearDown(tempo.dispose);
    final videos = VideoLayerManager();
    addTearDown(videos.dispose);
    await tester.pumpWidget(UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: Center(
          child: SizedBox(
            width: 200,
            height: 200,
            child: CompositionView(
              time: tempo,
              videos: videos,
              selectedId: null,
            ),
          ),
        ),
      ),
    ));
    await tester.pump();
    final imagem = find.byType(Image);
    expect(imagem, findsOneWidget);
    // Composicao 1080x1080 e foto 2:1: cobrir = 2160 x 1080 (antes, pela
    // largura, era 1080 x 540 — metade da altura vazia).
    expect(tester.getSize(imagem), const Size(2160, 1080));
  });

  testWidgets('a mesclagem nao recorta a camada ampliada', (tester) async {
    final chave = GlobalKey();
    await tester.pumpWidget(MaterialApp(
      home: Center(
        child: RepaintBoundary(
          key: chave,
          child: SizedBox(
            width: 100,
            height: 100,
            child: Stack(children: [
              const Positioned.fill(
                child: ColoredBox(color: Color(0xFF0000FF)),
              ),
              Positioned(
                left: 25,
                top: 25,
                child: BlendMask(
                  blendMode: BlendMode.plus,
                  child: Transform(
                    transform: Matrix4.diagonal3Values(2, 2, 1),
                    alignment: Alignment.center,
                    child: const Opacity(
                      opacity: 0.999,
                      child: SizedBox(
                        width: 50,
                        height: 50,
                        child: ColoredBox(color: Color(0xFFFF0000)),
                      ),
                    ),
                  ),
                ),
              ),
            ]),
          ),
        ),
      ),
    ));
    await tester.pump();
    final bytes = await tester.runAsync(() async {
      final boundary =
          chave.currentContext!.findRenderObject()! as RenderRepaintBoundary;
      final img = await boundary.toImage();
      final data = await img.toByteData(format: ui.ImageByteFormat.rawRgba);
      img.dispose();
      return data!;
    });
    int canal(int x, int y, int c) => bytes!.getUint8((y * 100 + x) * 4 + c);
    // (10,10) fica FORA da caixa de 50x50 (25..75), mas dentro dela
    // ampliada 2x (0..100): tem de receber o vermelho somado ao azul.
    expect(canal(10, 10, 0), greaterThan(200), reason: 'vermelho recortado');
    expect(canal(10, 10, 2), greaterThan(200));
    expect(canal(50, 50, 0), greaterThan(200));
  });
}
