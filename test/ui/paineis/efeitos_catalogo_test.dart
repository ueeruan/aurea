import 'package:aurea/src/features/editor/application/ui/effect_favorites.dart';
import 'package:aurea/src/features/editor/application/ui/effect_recents.dart';
import 'package:aurea/src/features/editor/domain/animador_de_texto.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/efeitos.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_paineis.dart';

String _idDe(EffectType t) => effectSpecs[t]!.id;

/// Toca num filtro do catalogo (a fileira rola de lado: o filtro pode
/// estar fora da tela).
Future<void> _filtro(WidgetTester tester, String chave) async {
  final f = find.byKey(ValueKey(chave));
  final fileira = find.descendant(
    of: find.byKey(const ValueKey('catalogo-filtros')),
    matching: find.byType(Scrollable),
  );
  if (f.evaluate().isEmpty) {
    // Volta ao comeco da fileira e procura andando para a frente.
    await tester.drag(fileira, const Offset(2000, 0));
    await tester.pumpAndSettle();
    await tester.scrollUntilVisible(f, 80, scrollable: fileira);
    await tester.pumpAndSettle();
  }
  await tester.tap(f);
  await tester.pumpAndSettle();
}

Future<void> _abrirCatalogo(WidgetTester tester) async {
  await tester.tap(find.byKey(const ValueKey('efeitos-adicionar')));
  await tester.pumpAndSettle();
  expect(find.byKey(const ValueKey('catalogo-busca')), findsOneWidget);
}

void main() {
  group('catalogo de efeitos', () {
    testWidgets('a busca acha "Motion Tile"; aplicar poe na pilha e registra '
        'nos recentes (lembrados no aparelho)', (tester) async {
      final (b, id) = await montarPainel(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'Forma');
          return c.projetoCompleto.layers.single.id;
        },
        painel: (id) => PainelEfeitos(layerId: id),
      );
      await _abrirCatalogo(tester);
      final motionTile = _idDe(EffectType.motionTile);
      await tester.enterText(
        find.byKey(const ValueKey('catalogo-busca')),
        'Motion Tile',
      );
      await tester.pumpAndSettle();
      final tile = find.byKey(ValueKey('catalogo-efeito-$motionTile'));
      expect(tile, findsOneWidget);
      // A busca nao traz o resto do catalogo junto.
      expect(
        find.byKey(ValueKey('catalogo-efeito-${_idDe(EffectType.deepGlow)}')),
        findsNothing,
      );

      await tester.tap(tile);
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('catalogo-busca')), findsNothing);
      final efeitos = b.camada(id).effects;
      expect(efeitos.map((e) => e.type), [EffectType.motionTile]);
      // O cartao do recem-aplicado abre sozinho.
      expect(
        find.byKey(ValueKey('efeito-${efeitos.single.id}-corpo')),
        findsOneWidget,
      );

      // RECENTES: registrado e lembrado nas preferencias.
      expect(b.container.read(effectRecentsProvider), [motionTile]);
      expect(b.prefs.getStringList(EffectRecentsNotifier.kChave), [
        motionTile,
      ]);
      await _abrirCatalogo(tester);
      await _filtro(tester, 'catalogo-recentes');
      expect(
        find.byKey(ValueKey('catalogo-efeito-$motionTile')),
        findsOneWidget,
      );
      expect(tester.takeException(), isNull);
    });

    testWidgets('favoritar pela estrela persiste e aparece em Favoritos', (
      tester,
    ) async {
      final (b, _) = await montarPainel(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'Forma');
          return c.projetoCompleto.layers.single.id;
        },
        painel: (id) => PainelEfeitos(layerId: id),
      );
      await _abrirCatalogo(tester);
      final deep = _idDe(EffectType.deepGlow);
      await _filtro(tester, 'catalogo-favoritos');
      expect(find.byKey(ValueKey('catalogo-efeito-$deep')), findsNothing);

      await _filtro(tester, 'catalogo-cat-Light');
      await tester.tap(find.byKey(ValueKey('catalogo-favorito-$deep')));
      await tester.pumpAndSettle();
      expect(b.container.read(effectFavoritesProvider), {deep});
      // PERSISTE: esta nas preferencias, e um provider novo le de la.
      expect(b.prefs.getStringList(EffectFavoritesNotifier.kChave), [deep]);
      b.container.invalidate(effectFavoritesProvider);
      expect(b.container.read(effectFavoritesProvider), {deep});

      await _filtro(tester, 'catalogo-favoritos');
      expect(find.byKey(ValueKey('catalogo-efeito-$deep')), findsOneWidget);
      expect(tester.takeException(), isNull);
    });

    testWidgets('categoria Tempo tem Time Remap, Time Warp e Posterize Time; '
        'aplicar o Time Remap o poe na pilha como efeito normal', (
      tester,
    ) async {
      final (b, id) = await montarPainel(
        tester,
        preparar: (c) {
          final v = videoDeTeste();
          abrirProjetoCom(c, [v]);
          return v.id;
        },
        painel: (id) => PainelEfeitos(layerId: id),
      );
      await _abrirCatalogo(tester);
      await _filtro(tester, 'catalogo-cat-Time');
      for (final t in [
        EffectType.timeRemap,
        EffectType.rgbTimeWarp,
        EffectType.posterizeTime,
      ]) {
        expect(
          find.byKey(ValueKey('catalogo-efeito-${_idDe(t)}')),
          findsOneWidget,
          reason: t.name,
        );
      }
      await tester.tap(
        find.byKey(ValueKey('catalogo-efeito-${_idDe(EffectType.timeRemap)}')),
      );
      await tester.pumpAndSettle();
      final l = b.camada(id) as VideoLayer;
      final remap = l.effects.single;
      expect(remap.type, EffectType.timeRemap);
      expect(l.timeRemap, isNotNull, reason: 'a trilha do motor nasceu');
      // UM CARTAO COMO OS OUTROS, aberto, com as linhas dele.
      final k = 'efeito-${remap.id}';
      expect(find.byKey(ValueKey('$k-cabecalho')), findsOneWidget);
      expect(find.byKey(ValueKey('$k-alca')), findsOneWidget);
      expect(
        find.byKey(const ValueKey('prop-time-remap-speed')),
        findsOneWidget,
      );
      expect(find.byKey(ValueKey('prop-${remap.id}-tempo')), findsOneWidget);
      // Os irmaos da categoria continuam a um toque.
      await _abrirCatalogo(tester);
      await _filtro(tester, 'catalogo-cat-Time');
      await tester.tap(
        find.byKey(
          ValueKey('catalogo-efeito-${_idDe(EffectType.posterizeTime)}'),
        ),
      );
      await tester.pumpAndSettle();
      expect(b.camada(id).effects.map((e) => e.type), [
        EffectType.timeRemap,
        EffectType.posterizeTime,
      ]);
      expect(tester.takeException(), isNull);
    });

    testWidgets('Time Remap nao e oferecido fora de video', (tester) async {
      await montarPainel(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'Forma');
          return c.projetoCompleto.layers.single.id;
        },
        painel: (id) => PainelEfeitos(layerId: id),
      );
      await _abrirCatalogo(tester);
      await _filtro(tester, 'catalogo-cat-Time');
      expect(
        find.byKey(ValueKey('catalogo-efeito-${_idDe(EffectType.timeRemap)}')),
        findsNothing,
      );
      expect(
        find.byKey(
          ValueKey('catalogo-efeito-${_idDe(EffectType.rgbTimeWarp)}'),
        ),
        findsOneWidget,
      );
    });

    testWidgets('Animador de Texto mora na categoria Texto de uma camada de '
        'texto e vira um cartao da pilha com o Range Selector', (tester) async {
      final (b, id) = await montarPainel(
        tester,
        preparar: (c) {
          c.addTextLayer(Duration.zero);
          return c.projetoCompleto.layers.single.id;
        },
        painel: (id) => PainelEfeitos(layerId: id),
      );
      await _abrirCatalogo(tester);
      await _filtro(tester, 'catalogo-cat-Text');
      final entrada = find.byKey(
        const ValueKey('catalogo-efeito-animador_de_texto'),
      );
      expect(entrada, findsOneWidget);
      await tester.tap(entrada);
      await tester.pumpAndSettle();
      final texto = b.camada(id) as TextLayer;
      expect(texto.animators, hasLength(1));
      final a = texto.animators.single;
      final k = 'animador-${a.id}';
      expect(find.byKey(ValueKey('$k-cabecalho')), findsOneWidget);
      expect(find.byKey(ValueKey('$k-corpo')), findsOneWidget);
      for (final linha in ['start', 'end', 'offset']) {
        expect(
          find.byKey(ValueKey('prop-$k-$linha')),
          findsOneWidget,
          reason: linha,
        );
      }
      // O losango do Offset e o mesmo sistema de todo o app.
      await tester.tap(find.byKey(ValueKey('kf-$k-offset')));
      await tester.pumpAndSettle();
      final gravado = (b.camada(id) as TextLayer).animators.single;
      expect(faixaDoAnimador(gravado)!.offset.isAnimated, isTrue);
      expect(tester.takeException(), isNull);
    });

    testWidgets('Camera Tracker mora na categoria Camera do video e abre o '
        'painel Rastrear', (tester) async {
      final (b, _) = await montarPainel(
        tester,
        preparar: (c) {
          final v = videoDeTeste();
          abrirProjetoCom(c, [v]);
          return v.id;
        },
        painel: (id) => PainelEfeitos(layerId: id),
      );
      await _abrirCatalogo(tester);
      await _filtro(tester, 'catalogo-cat-Camera');
      await tester.tap(
        find.byKey(const ValueKey('catalogo-efeito-camera_tracker')),
      );
      await tester.pumpAndSettle();
      expect(b.abertos, [PainelId.rastrear]);
    });
  });
}
