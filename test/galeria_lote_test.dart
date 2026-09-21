// VARIAS MIDIAS DE UMA VEZ (mapa v1.1.1): marcar na galeria e adicionar
// juntas ou em sequencia, com a duracao de imagem escolhida na barra.
import 'dart:typed_data';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/galeria.dart';
import 'package:aurea/src/features/media/application/gallery_service.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _GaleriaFalsa extends GalleryService {
  @override
  Future<GalleryAccess> requestAccess() async => GalleryAccess.full;

  @override
  Future<List<GalleryAlbum>> albums() async => const [
    GalleryAlbum('r', 'Recentes'),
  ];

  @override
  Future<List<GalleryAsset>> page(String album, int page, int size) async =>
      page == 0
      ? const [
          GalleryAsset('a0', video: false),
          GalleryAsset('a1', video: false),
          GalleryAsset('a2', video: true, duration: Duration(seconds: 7)),
        ]
      : const [];

  @override
  Future<Uint8List?> thumbnail(GalleryAsset asset) async => null;
}

void main() {
  test('a imagem entra com a duração pedida, presa no chão de 100 ms', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addImageLayer(
      Duration.zero,
      '/tmp/a.png',
      'a.png',
      duracao: const Duration(milliseconds: 1500),
    );
    e.addImageLayer(
      Duration.zero,
      '/tmp/b.png',
      'b.png',
      duracao: Duration.zero,
    );
    final camadas = c.read(editorControllerProvider).layers;
    expect(camadas[1].duration, const Duration(milliseconds: 1500));
    expect(
      camadas[0].duration,
      const Duration(milliseconds: 100),
      reason: 'zero nao pode virar camada invisivel',
    );
  });

  test('sem pedir nada, a imagem continua com os 3 s de sempre', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addImageLayer(Duration.zero, '/tmp/a.png', 'a.png');
    expect(
      c.read(editorControllerProvider).layers.single.duration,
      const Duration(seconds: 3),
    );
  });

  test('em sequência: cada uma começa onde a anterior acaba', () {
    // O mesmo laco do onImportLote da folha de adicionar, sem widgets:
    // e a conta que importa.
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    var t = Duration.zero;
    final midias = [
      (video: false, duration: const Duration(seconds: 2), path: '/tmp/1.png'),
      (video: false, duration: const Duration(seconds: 2), path: '/tmp/2.png'),
      (video: true, duration: const Duration(seconds: 5), path: '/tmp/3.mp4'),
    ];
    for (final m in midias) {
      if (m.video) {
        e.addVideoLayer(t, m.path, 'v', m.duration);
      } else {
        e.addImageLayer(t, m.path, 'i', duracao: m.duration);
      }
      t += m.duration;
    }
    final camadas = c.read(editorControllerProvider).layers.reversed.toList();
    expect(camadas[0].startTime, Duration.zero);
    expect(camadas[1].startTime, const Duration(seconds: 2));
    expect(camadas[2].startTime, const Duration(seconds: 4));
    expect(camadas[2], isA<VideoLayer>());
    expect(
      c.read(editorControllerProvider).duration,
      greaterThanOrEqualTo(const Duration(seconds: 9)),
    );
  });

  testWidgets('o número da ordem pinta POR CIMA da miniatura', (tester) async {
    // REGRESSAO (16/09): o cracha da ordem era o primeiro filho do
    // Stack — a foto pintava depois e cobria o numero. Ele tem de ser
    // o ULTIMO filho.
    await tester.pumpWidget(
      ProviderScope(
        overrides: [
          galleryServiceProvider.overrideWithValue(_GaleriaFalsa()),
        ],
        child: MaterialApp(
          theme: ThemeData(brightness: Brightness.dark),
          home: Scaffold(
            body: SizedBox(
              height: 470,
              child: GalleryPanel(
                onImport: (file, video, duration) async {},
                onImportLote: (midias, {required emSequencia}) async {},
              ),
            ),
          ),
        ),
      ),
    );
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 300));
    await tester.longPress(find.byKey(const ValueKey('a0')));
    await tester.pump();
    await tester.tap(find.byKey(const ValueKey('a1')));
    await tester.pump();

    expect(find.byKey(const ValueKey('galeria-ordem-a0')), findsOneWidget);
    expect(find.byKey(const ValueKey('galeria-ordem-a1')), findsOneWidget);
    final stack = tester.widget<Stack>(
      find
          .ancestor(
            of: find.byKey(const ValueKey('galeria-ordem-a0')),
            matching: find.byType(Stack),
          )
          .first,
    );
    final ultimo = stack.children.last;
    expect(ultimo, isA<Positioned>());
    expect(
      ((ultimo as Positioned).child as Container).key,
      const ValueKey('galeria-ordem-a0'),
      reason: 'o cracha precisa ser o ultimo filho para aparecer',
    );
  });

  test('juntas: todas no mesmo instante', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    const cabecote = Duration(seconds: 1);
    for (final p in ['/tmp/1.png', '/tmp/2.png', '/tmp/3.png']) {
      e.addImageLayer(cabecote, p, 'i', duracao: const Duration(seconds: 4));
    }
    for (final l in c.read(editorControllerProvider).layers) {
      expect(l.startTime, cabecote);
    }
  });
}
