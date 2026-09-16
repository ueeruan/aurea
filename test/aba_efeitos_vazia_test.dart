// A ABA DE EFEITOS FICA (ordem do dono, 16/09: "deixa so a aba add
// efeito por enquanto").
//
// Com o catalogo vazio ela nao tem o que listar — e e exatamente por
// isso que precisa de teste. Uma galeria que estoura ao abrir seria
// pior do que uma galeria sem nada dentro.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/context/effects/effect_gallery.dart';
import 'package:aurea/src/features/editor/presentation/context/effects/effect_thumbnail.dart';
import 'package:aurea/src/features/editor/application/effect_preset_store.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  setUpAll(() {
    EffectThumbnailCache.semDisco = true;
    EffectPresetStore.semArquivo = true;
  });

  testWidgets('a galeria abre vazia, sem estourar', (tester) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'Forma');
    final id = c.read(editorControllerProvider).layers.first.id;
    final playback = PlaybackController(
      vsync: tester,
      durationOf: () => const Duration(seconds: 5),
    );
    addTearDown(playback.dispose);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Consumer(
            builder: (context, ref, _) => Scaffold(
              body: Center(
                child: TextButton(
                  key: const ValueKey('abrir'),
                  onPressed: () =>
                      showEffectGallery(context, ref, id, playback),
                  child: const Text('abrir'),
                ),
              ),
            ),
          ),
        ),
      ),
    );

    await tester.tap(find.byKey(const ValueKey('abrir')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));

    expect(
      tester.takeException(),
      isNull,
      reason: 'a aba de efeitos estourou com o catalogo vazio',
    );
    expect(effectSpecs, isEmpty);
    // Deixa a folha fechar antes do fim do teste.
    await tester.pump(const Duration(seconds: 1));
  });
}
