import 'package:aurea/src/features/editor/application/blob_track_service.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/blob_track.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// RASTREAR, do lado de quem usa.
///
/// O solver e o detector tem os proprios testes. O que se prova aqui e a
/// outra metade: que existe uma porta de entrada, e que o resultado do
/// rastreio realmente MOVE alguma coisa. Ate esta versao o Blob Tracker
/// so desenhava caixas — via-se o rastreio e nao dava para usar.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  /// Uma analise falsa, mas com a forma exata da de verdade: um blob que
  /// atravessa o quadro da esquerda para a direita.
  BlobTrackData analiseFalsa({int fps = 10, int quadros = 20}) => BlobTrackData(
    fps: fps,
    width: 240,
    height: 135,
    frames: [
      for (var i = 0; i < quadros; i++)
        [
          Blob(
            id: 7,
            // Anda so na horizontal e engorda so na largura: assim o
            // teste separa "seguiu" de "cresceu junto".
            rect: Rect.fromLTWH(20.0 + i * 8, 50, 24 + i * .5, 24),
            area: 400,
          ),
          // Um segundo blob, curto: entra na deteccao e nao deve ser
          // oferecido como alvo.
          if (i < 2)
            Blob(id: 9, rect: const Rect.fromLTWH(4, 4, 10, 10), area: 100),
        ],
    ],
  );

  test('a lista de blobs vem do mais duradouro para o mais fugaz', () {
    final d = analiseFalsa();
    expect(d.idsPorDuracao.first, 7);
    expect(d.duracaoDe(7), 20);
    expect(d.duracaoDe(9), 2);
    expect(d.caminhoDe(7).length, 20);
    expect(d.caminhoDe(404), isEmpty);
  });

  testWidgets('grudar uma camada num blob escreve o caminho em keyframes', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);

    // Um video de dez segundos e um texto para pendurar nele.
    e.addVideoLayer(
      Duration.zero,
      'clipe.mp4',
      'Clipe',
      const Duration(seconds: 10),
    );
    final video = c.read(editorControllerProvider).layers
        .whereType<VideoLayer>()
        .first;
    e.addTextLayer(Duration.zero, text: 'Segue');
    final texto = c.read(editorControllerProvider).layers
        .whereType<TextLayer>()
        .first;
    e.trimLayerEnd(texto.id, const Duration(seconds: 8));

    // Sem analise, nao gruda nada — e diz isso em vez de quebrar.
    expect(
      e.grudarNoBlob(
        alvoId: texto.id,
        videoId: video.id,
        effectId: 'nao-existe',
        blobId: 7,
      ),
      isNull,
    );

    // Com analise, o texto passa a seguir o blob.
    final dados = analiseFalsa();
    BlobTrackService.instance.injetar('fx-teste', dados);
    addTearDown(() => BlobTrackService.instance.clear('fx-teste'));

    final n = e.grudarNoBlob(
      alvoId: texto.id,
      videoId: video.id,
      effectId: 'fx-teste',
      blobId: 7,
    );
    expect(n, greaterThan(10));

    final movido = c.read(editorControllerProvider).layerById(texto.id)!;
    final inicio = movido.position.valueAt(Duration.zero);
    final depois = movido.position.valueAt(const Duration(seconds: 1));
    // O blob anda para a direita: o texto tambem.
    expect(depois.dx, greaterThan(inicio.dx + 10));
    // E na vertical o blob nao anda, entao o texto tambem nao.
    expect(depois.dy, closeTo(inicio.dy, 12));
  });

  testWidgets('com escala, a camada cresce junto com a caixa', (tester) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    e.addVideoLayer(
      Duration.zero,
      'clipe.mp4',
      'Clipe',
      const Duration(seconds: 10),
    );
    final video = c.read(editorControllerProvider).layers
        .whereType<VideoLayer>()
        .first;
    e.addTextLayer(Duration.zero, text: 'Segue');
    final texto = c.read(editorControllerProvider).layers
        .whereType<TextLayer>()
        .first;

    BlobTrackService.instance.injetar('fx-escala', analiseFalsa());
    addTearDown(() => BlobTrackService.instance.clear('fx-escala'));
    e.grudarNoBlob(
      alvoId: texto.id,
      videoId: video.id,
      effectId: 'fx-escala',
      blobId: 7,
      comEscala: true,
    );

    final movido = c.read(editorControllerProvider).layerById(texto.id)!;
    // A caixa cresce de 24 para 33,5: a camada acompanha.
    expect(
      movido.scaleX.valueAt(const Duration(milliseconds: 1900)),
      greaterThan(movido.scaleX.valueAt(Duration.zero) * 1.2),
    );
  });
}
