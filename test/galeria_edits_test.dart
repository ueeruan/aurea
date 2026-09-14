// "DEIXE O APP MAIS FACIL DE USAR" (dono, 14/09/2026).
//
// Os efeitos de edit (batida, glitch, tempo e coloring) moram em
// categorias tecnicas diferentes — Luz, Tempo, Distorcer, Glitch, Cor.
// Quem vem do Alight Motion procura por "edit", nao por categoria. O
// atalho "Edits" da galeria junta todos num lugar so, e as pilhas de
// coloring aparecem na aba Presets.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/effect_preset_store.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/context/effects/effect_gallery.dart';
import 'package:aurea/src/features/editor/presentation/context/effects/effect_thumbnail.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  setUpAll(() {
    EffectThumbnailCache.semDisco = true;
    EffectPresetStore.semArquivo = true;
  });

  test('a lista de edits so tem efeitos do catalogo, sem repetir', () {
    expect(efeitosDeEdit.toSet(), hasLength(efeitosDeEdit.length));
    for (final t in efeitosDeEdit) {
      expect(effectSpecs.containsKey(t), isTrue, reason: t.name);
    }
    for (final t in [
      EffectType.flash,
      EffectType.twitch,
      EffectType.timeSlice,
      EffectType.colorBalance,
    ]) {
      expect(efeitosDeEdit, contains(t));
    }
  });

  testWidgets('o atalho Edits filtra a galeria e um toque aplica', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1170, 2532);
    tester.view.devicePixelRatio = 3;
    addTearDown(tester.view.reset);

    final container = ProviderContainer();
    addTearDown(container.dispose);
    final forma = ShapeLayer(
      id: 'forma',
      name: 'Forma',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      contents: [
        ShapePath(primitive: ShapePrimitive.rectangle),
        ShapeFill(color: const Color(0xFF3DDC97)),
      ],
    );
    container
        .read(editorControllerProvider.notifier)
        .openProject(
          VideoProject(
            name: 'galeria',
            createdAt: DateTime(2026, 9, 14),
            aspectRatio: 9 / 16,
            resolutionHeight: 320,
            layers: [forma],
          ),
        );
    final playback = PlaybackController(
      vsync: tester,
      durationOf: () => const Duration(seconds: 5),
    );
    addTearDown(playback.dispose);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: Consumer(
            builder: (context, ref, _) => Scaffold(
              body: Center(
                child: TextButton(
                  key: const ValueKey('abrir-galeria'),
                  onPressed: () =>
                      showEffectGallery(context, ref, 'forma', playback),
                  child: const Text('abrir'),
                ),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.byKey(const ValueKey('abrir-galeria')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));

    expect(find.byKey(const ValueKey('galeria-edits')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('galeria-edits')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.byKey(const ValueKey('efeito-flash')), findsOneWidget);
    expect(find.byKey(const ValueKey('efeito-zoom_punch')), findsOneWidget);
    expect(find.byKey(const ValueKey('efeito-gaussian_blur')), findsNothing);

    await tester.tap(find.byKey(const ValueKey('efeito-flash')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    final camada = container.read(editorControllerProvider).layerById('forma')!;
    expect(camada.effects.map((e) => e.type), [EffectType.flash]);
  });
}
