// A ABA DE EFEITOS FICOU (ordem do dono, 16/09: "deixa so a aba add
// efeito por enquanto") e voltou a ter o que mostrar: o lote de correcao
// de cor, recomecado do zero.
//
// O teste nasceu para provar que a galeria vazia nao estourava; agora
// prova que ela abre com os cinco efeitos e que um toque aplica.
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

  testWidgets('a galeria abre com a correcao de cor e um toque aplica', (
    tester,
  ) async {
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
      reason: 'a aba de efeitos estourou ao abrir',
    );
    for (final spec in effectSpecs.values.where((s) => s.category == 'Color')) {
      expect(
        find.byKey(ValueKey('efeito-${spec.id}')),
        findsOneWidget,
        reason: '${spec.name} tem de aparecer na galeria',
      );
    }
    await tester.tap(find.byKey(const ValueKey('efeito-unsharp_mask')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));
    final efeitos = c.read(editorControllerProvider).layerById(id)!.effects;
    expect(efeitos.map((e) => e.type), [EffectType.unsharpMask]);
    // Deixa a folha fechar antes do fim do teste.
    await tester.pump(const Duration(seconds: 1));
  });
}
