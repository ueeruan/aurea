import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/effect_preset_store.dart';
import 'package:aurea/src/features/editor/application/estilo_preset_store.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/effect_preset.dart';
import 'package:aurea/src/features/editor/domain/estilo_preset.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/presentation/am/presets_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  setUp(() {
    EffectPresetStore.semArquivo = true;
    EstiloPresetStore.semArquivo = true;
    EffectPresetStore.instance.reset();
    EstiloPresetStore.instance.reset();
  });

  test('o estilo vai e volta do arquivo, com todo o acabamento', () {
    final estilo = EstiloPreset(
      nome: 'Meu contorno',
      estilos: LayerStyles(
        stroke: StrokeStyle(
          color: const Color(0xFFFF0066),
          width: AnimatedDouble(9),
        ),
        dropShadow: ShadowStyle(
          opacity: AnimatedDouble(.3),
          distance: AnimatedDouble(7),
        ),
      ),
    );
    final volta = estiloPresetFromJson(estiloPresetToJson(estilo));
    expect(volta.nome, 'Meu contorno');
    expect(volta.id, estilo.id);
    expect(volta.estilos.stroke!.color, const Color(0xFFFF0066));
    expect(volta.estilos.stroke!.width.base, 9);
    expect(volta.estilos.dropShadow!.distance.base, 7);
    expect(volta.partes, contains('borda'));
    expect(volta.partes, contains('sombra'));
  });

  test('os estilos de fábrica são todos diferentes e têm acabamento', () {
    final fabrica = estilosDeFabrica();
    expect(fabrica.length, greaterThanOrEqualTo(5));
    expect(fabrica.map((e) => e.nome).toSet().length, fabrica.length);
    for (final e in fabrica) {
      expect(e.deFabrica, isTrue);
      expect(e.estilos.isEmpty, isFalse);
      expect(e.partes, isNotEmpty);
    }
  });

  test('a lista guarda, renomeia e apaga o estilo', () async {
    final loja = EstiloPresetStore.instance;
    final e = EstiloPreset(
      nome: 'Neon meu',
      estilos: LayerStyles(outerGlow: GlowStyle()),
    );
    await loja.add(e);
    expect(loja.estilos.single.nome, 'Neon meu');
    await loja.renomear(e.id, 'Neon novo');
    expect(loja.estilos.single.nome, 'Neon novo');
    await loja.remove(e.id);
    expect(loja.estilos, isEmpty);
  });

  test('aplicar um estilo TROCA o acabamento da camada', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.single.id;
    e.updateLayerStyles(
      id,
      (s) => s.copyWith(
        stroke: StrokeStyle(color: const Color(0xFF00FF00)),
        innerShadow: ShadowStyle(),
      ),
    );
    e.aplicarEstilo(id, estilosDeFabrica().first);
    final agora = e.estiloDaCamada(id);
    expect(agora.stroke!.color, const Color(0xFFFFFFFF));
    expect(
      agora.innerShadow,
      isNull,
      reason: 'aplicar troca o acabamento, nao empilha',
    );
  });

  testWidgets('a tela lista os dois tipos, busca e aplica', (tester) async {
    tester.view.physicalSize = const Size(820, 1400);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.single.id;
    await EffectPresetStore.instance.add(
      EffectPreset(
        name: 'Meu glitch',
        effects: [
          EffectInstance(
            type: EffectType.glitch,
            params: {'quantidade': AnimatedDouble(1)},
          ),
        ],
      ),
    );

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: TelaDePresets(layerId: id, at: Duration.zero),
        ),
      ),
    );
    await tester.pumpAndSettle();
    expect(find.text('Meu glitch'), findsOneWidget);
    // Os de fábrica entram na mesma lista.
    expect(find.text('Cor de filme'), findsOneWidget);

    // A busca filtra.
    await tester.enterText(
      find.byKey(const ValueKey('presets-busca')),
      'glitch',
    );
    await tester.pumpAndSettle();
    expect(find.text('Cor de filme'), findsNothing);
    expect(find.text('Meu glitch'), findsOneWidget);
    await tester.enterText(find.byKey(const ValueKey('presets-busca')), '');
    await tester.pumpAndSettle();

    // A aba de estilos mostra os de fábrica.
    await tester.tap(find.byKey(const ValueKey('presets-aba-estilos')));
    await tester.pumpAndSettle();
    expect(find.text('Contorno branco'), findsOneWidget);
    expect(find.text('Meu glitch'), findsNothing);

    // Tocar num estilo aplica na camada e volta.
    await tester.ensureVisible(find.text('Néon'));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('preset-estilo-fab-neon')));
    await tester.pumpAndSettle();
    expect(e.estiloDaCamada(id).outerGlow, isNotNull);
    expect(tester.takeException(), isNull);
  });

  testWidgets('o toque longo oferece aplicar, exportar, renomear e excluir', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(820, 1400);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.single.id;
    final meu = EstiloPreset(
      nome: 'Só meu',
      estilos: LayerStyles(outerGlow: GlowStyle()),
    );
    await EstiloPresetStore.instance.add(meu);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: TelaDePresets(
            layerId: id,
            at: Duration.zero,
            aba: AbaDosPresets.estilos,
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('Só meu'));
    await tester.pumpAndSettle();
    await tester.longPress(find.text('Só meu'));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('preset-aplicar')), findsOneWidget);
    expect(find.byKey(const ValueKey('preset-exportar')), findsOneWidget);
    expect(find.byKey(const ValueKey('preset-renomear')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('preset-excluir')));
    await tester.pumpAndSettle();
    expect(EstiloPresetStore.instance.estilos, isEmpty);
    expect(find.text('Só meu'), findsNothing);

    // O de fábrica não oferece excluir nem renomear.
    await tester.ensureVisible(find.text('Adesivo'));
    await tester.pumpAndSettle();
    await tester.longPress(
      find.byKey(const ValueKey('preset-estilo-fab-adesivo')),
    );
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('preset-excluir')), findsNothing);
    expect(find.byKey(const ValueKey('preset-renomear')), findsNothing);
    expect(find.byKey(const ValueKey('preset-exportar')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
