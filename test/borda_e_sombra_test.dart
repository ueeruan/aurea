// BORDA E SOMBRA (v1.1.1): o traco da forma ganha pontas nos contornos
// abertos; qualquer camada empilha ate quatro bordas (fora, dentro ou
// centro) e tem sombra, sombra interna e brilho numa folha so.
import 'dart:ui' show Color, Offset, Size;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/borda_e_sombra_sheet.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart' hide Color, Offset, Size;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/print_da_ui.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

ProviderContainer _container() {
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  return c;
}

ShapeLayer _forma(ProviderContainer c) =>
    c.read(editorControllerProvider).layers.first as ShapeLayer;

ShapeStroke _traco(TerminacaoDoTraco inicio, TerminacaoDoTraco fim) =>
    ShapeStroke(
      color: const Color(0xFFFFFFFF),
      width: AnimatedDouble(4),
      inicio: inicio,
      fim: fim,
    );

/// Rola a folha ate [alvo] existir (a lista e preguicosa) e o mostra.
Future<void> _ver(WidgetTester tester, Finder alvo) async {
  if (alvo.evaluate().isEmpty) {
    await tester.scrollUntilVisible(
      alvo,
      160,
      scrollable: find
          .descendant(
            of: find.byKey(const ValueKey('borda-e-sombra')),
            matching: find.byType(Scrollable),
          )
          .first,
    );
  }
  await tester.ensureVisible(alvo);
  await tester.pumpAndSettle();
}

void main() {
  setUpAll(carregarFontesReais);

  test('a lista grava a primeira borda no contorno e as outras nas extras', () {
    final a = StrokeStyle(color: const Color(0xFFFFFFFF));
    final b = StrokeStyle(
      color: const Color(0xFF000000),
      posicao: PosicaoDaBorda.dentro,
    );
    var s = comBordas(const LayerStyles(), [a, b]);
    expect(s.stroke, same(a));
    expect(s.bordasExtras, [b]);
    expect(s.bordas, [a, b]);
    expect(s.isEmpty, isFalse);
    s = comBordas(s, const []);
    expect(s.bordas, isEmpty);
    expect(s.isEmpty, isTrue);
  });

  test('a borda nova nasce por fora da ultima', () {
    final primeira = novaBorda(const [], Duration.zero);
    expect(primeira.width.base, 6);
    final segunda = novaBorda([primeira], Duration.zero);
    expect(segunda.width.base, 12);
    expect(segunda.color, isNot(primeira.color));
    final larga = novaBorda([
      StrokeStyle(width: AnimatedDouble(98), posicao: PosicaoDaBorda.centro),
    ], Duration.zero);
    expect(larga.width.base, 100, reason: 'o teto da dilatacao');
    expect(larga.posicao, PosicaoDaBorda.centro);
  });

  test('bordas, posicoes, pontas e sombra interna voltam do arquivo', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'F');
    final id = _forma(c).id;
    e.ensureShapeStroke(id);
    e.updateShapeStroke(
      id,
      (s) => s.copyWith(
        inicio: TerminacaoDoTraco.setaCheia,
        fim: TerminacaoDoTraco.circuloVazado,
        tamanhoDaTerminacao: 4.5,
      ),
    );
    e.updateLayerStyles(
      id,
      (s) => comBordas(s, [
        StrokeStyle(
          color: const Color(0xFFFF0000),
          width: AnimatedDouble(5),
          posicao: PosicaoDaBorda.dentro,
        ),
        StrokeStyle(
          color: const Color(0xFF00FF00),
          width: AnimatedDouble(9),
          posicao: PosicaoDaBorda.centro,
        ),
        StrokeStyle(color: const Color(0xFF0000FF), width: AnimatedDouble(14)),
      ]).copyWith(innerShadow: ShadowStyle(spread: AnimatedDouble(3))),
    );

    final volta = projectFromJson(
      projectToJson(c.read(editorControllerProvider)),
    );
    final forma = volta.layers.whereType<ShapeLayer>().single;
    final estilos = volta.metaOf(forma.id).styles;
    expect(estilos.bordas.map((b) => b.posicao), [
      PosicaoDaBorda.dentro,
      PosicaoDaBorda.centro,
      PosicaoDaBorda.fora,
    ]);
    expect(estilos.bordas.map((b) => b.width.base), [5, 9, 14]);
    expect(estilos.bordas[1].color, const Color(0xFF00FF00));
    expect(estilos.innerShadow?.spread.base, 3);
    final traco = forma.contents.whereType<ShapeStroke>().single;
    expect(traco.inicio, TerminacaoDoTraco.setaCheia);
    expect(traco.fim, TerminacaoDoTraco.circuloVazado);
    expect(traco.tamanhoDaTerminacao, 4.5);
  });

  test('pontas so nos contornos abertos, cada uma no seu lado', () {
    final linha = ShapeBezier(
      path: AnimatedPath(
        BezierPath(
          closed: false,
          vertices: const [
            PathVertex(p: Offset(-100, 0)),
            PathVertex(p: Offset(100, 0)),
          ],
        ),
      ),
    );
    final sem = evaluateShape([
      linha,
      _traco(TerminacaoDoTraco.nenhuma, TerminacaoDoTraco.nenhuma),
    ], Duration.zero);
    final com = evaluateShape([
      linha,
      _traco(TerminacaoDoTraco.setaCheia, TerminacaoDoTraco.circuloVazado),
    ], Duration.zero);
    expect(com, hasLength(sem.length + 2));
    final inicio = com[sem.length];
    final fim = com.last;
    expect(inicio.paint.style, PaintingStyle.fill, reason: 'seta cheia');
    expect(fim.paint.style, PaintingStyle.stroke, reason: 'circulo vazado');
    expect(inicio.path.getBounds().center.dx, lessThan(-50));
    expect(fim.path.getBounds().center.dx, greaterThan(50));

    final fechado = evaluateShape([
      ...ShapePresets.paramRect().where((i) => i is! ShapeFill),
      _traco(TerminacaoDoTraco.seta, TerminacaoDoTraco.seta),
    ], Duration.zero);
    expect(fechado, hasLength(1), reason: 'retangulo nao tem ponta');

    for (final tipo in TerminacaoDoTraco.values) {
      final d = caminhoDaTerminacao(tipo, Offset.zero, const Offset(1, 0), 12);
      expect(d == null, tipo == TerminacaoDoTraco.nenhuma, reason: tipo.name);
      if (d != null) {
        expect(d.$1.getBounds().longestSide, greaterThan(0), reason: tipo.name);
      }
      expect(nomesDasTerminacoes[tipo], isNotNull, reason: tipo.name);
    }
  });

  testWidgets(
    'o menu da camada abre a folha; traco, ponta e bordas mudam o projeto',
    (tester) async {
      tester.view.physicalSize = const Size(390, 844);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final c = _container();
      c
          .read(editorControllerProvider.notifier)
          .addShapeLayer(Duration.zero, name: 'F');
      final id = _forma(c).id;
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: c,
          child: const MaterialApp(home: EditorScreen()),
        ),
      );
      await tester.pumpAndSettle();
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();

      final tile = find.text('Borda e sombra');
      await tester.ensureVisible(tile);
      await tester.pumpAndSettle();
      await tester.tap(tile);
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('borda-e-sombra')), findsOneWidget);

      ShapeStroke? traco() =>
          _forma(c).contents.whereType<ShapeStroke>().firstOrNull;
      LayerStyles estilos() =>
          c.read(editorControllerProvider).metaOf(id).styles;

      if (traco() == null) {
        await tester.tap(find.byKey(const ValueKey('borda-traco-ligado')));
        await tester.pumpAndSettle();
      }
      expect(traco(), isNotNull);

      await tester.tap(find.byKey(const ValueKey('borda-traco-juncao-Redonda')));
      await tester.pumpAndSettle();
      expect(traco()!.join, StrokeJoin.round);

      await tester.tap(find.byKey(const ValueKey('borda-terminacao-fim')));
      await tester.pumpAndSettle();
      final seta = find.byKey(const ValueKey('borda-terminacao-fim-setaCheia'));
      await tester.ensureVisible(seta);
      await tester.tap(seta);
      await tester.pumpAndSettle();
      expect(traco()!.fim, TerminacaoDoTraco.setaCheia);

      final adicionar = find.byKey(const ValueKey('borda-adicionar'));
      await tester.ensureVisible(adicionar);
      await tester.pumpAndSettle();
      await tester.tap(adicionar);
      await tester.pumpAndSettle();
      await tester.ensureVisible(adicionar);
      await tester.pumpAndSettle();
      await tester.tap(adicionar);
      await tester.pumpAndSettle();
      expect(estilos().bordas, hasLength(2));
      expect(estilos().bordas.last.width.base, 12);

      final dentro = find.byKey(const ValueKey('borda-posicao-1-Dentro'));
      await _ver(tester, dentro);
      await tester.tap(dentro);
      await tester.pumpAndSettle();
      expect(estilos().bordas.last.posicao, PosicaoDaBorda.dentro);

      final subir = find.byKey(const ValueKey('borda-subir-1'));
      await _ver(tester, subir);
      await tester.tap(subir);
      await tester.pumpAndSettle();
      expect(estilos().bordas.first.posicao, PosicaoDaBorda.dentro);

      final excluir = find.byKey(const ValueKey('borda-excluir-0'));
      await _ver(tester, excluir);
      await tester.tap(excluir);
      await tester.pumpAndSettle();
      expect(estilos().bordas, hasLength(1));
      expect(estilos().bordas.single.posicao, PosicaoDaBorda.fora);

      final sombra = find.byKey(const ValueKey('sombra-ligada'));
      await _ver(tester, sombra);
      await tester.tap(sombra);
      await tester.pumpAndSettle();
      expect(estilos().dropShadow?.enabled, isTrue);
      final dura = find.byKey(const ValueKey('sombra-pronta-Dura'));
      await _ver(tester, dura);
      await tester.tap(dura);
      await tester.pumpAndSettle();
      expect(estilos().dropShadow!.size.base, 0);

      final interna = find.byKey(const ValueKey('sombra-interna-ligada'));
      await _ver(tester, interna);
      await tester.tap(interna);
      await tester.pumpAndSettle();
      expect(estilos().innerShadow?.enabled, isTrue);

      final brilho = find.byKey(const ValueKey('brilho-ligado'));
      await _ver(tester, brilho);
      await tester.tap(brilho);
      await tester.pumpAndSettle();
      expect(estilos().outerGlow?.enabled, isTrue);

      expect(tester.takeException(), isNull);
      await tester.pump(const Duration(seconds: 1));
    },
  );

  testWidgets('texto: a folha nao mostra traco, so bordas e sombras', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = _container();
    c.read(editorControllerProvider.notifier).addTextLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.first.id;
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: BordaESombra(layerId: id, tempo: Duration.zero),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('borda-traco-ligado')), findsNothing);
    await tester.tap(find.byKey(const ValueKey('borda-adicionar')));
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).metaOf(id).styles.stroke?.width.base,
      6,
    );
    for (var i = 0; i < 3; i++) {
      final adicionar = find.byKey(const ValueKey('borda-adicionar'));
      await tester.ensureVisible(adicionar);
      await tester.pumpAndSettle();
      await tester.tap(adicionar);
      await tester.pumpAndSettle();
    }
    expect(c.read(editorControllerProvider).metaOf(id).styles.bordas, hasLength(4));
    expect(
      find.byKey(const ValueKey('borda-adicionar')),
      findsNothing,
      reason: 'quatro e o teto',
    );
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });
  testWidgets('print: tres bordas, sombra longa e uma seta no palco', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'Caixa');
    final caixa = _forma(c).id;
    e.updateLayerStyles(
      caixa,
      (s) => comBordas(s, [
        StrokeStyle(
          color: const Color(0xFFFFFFFF),
          width: AnimatedDouble(12),
          posicao: PosicaoDaBorda.dentro,
        ),
        StrokeStyle(color: const Color(0xFFFF3B52), width: AnimatedDouble(16)),
        StrokeStyle(color: const Color(0xFFFFB020), width: AnimatedDouble(32)),
      ]).copyWith(dropShadow: sombrasProntas['Longa']!()),
    );
    e.addShapeLayer(
      Duration.zero,
      name: 'Seta',
      contents: [
        ShapeBezier(
          path: AnimatedPath(
            BezierPath(
              closed: false,
              vertices: const [
                PathVertex(p: Offset(-320, 380)),
                PathVertex(p: Offset(300, 520)),
              ],
            ),
          ),
        ),
        ShapeStroke(
          color: const Color(0xFF35C4E7),
          width: AnimatedDouble(14),
          inicio: TerminacaoDoTraco.circuloCheio,
          fim: TerminacaoDoTraco.setaCheia,
        ),
      ],
    );
    final chave = GlobalKey();
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: RepaintBoundary(
          key: chave,
          child: MaterialApp(
            theme: ThemeData(
              platform: TargetPlatform.iOS,
              fontFamily: 'Aurea Motion Sans',
              brightness: Brightness.dark,
            ),
            home: const EditorScreen(),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    await gravarPrint(tester, chave, 'borda-e-sombra-palco');

    c.read(selectedLayerProvider.notifier).state = caixa;
    await tester.pumpAndSettle();
    final tile = find.text('Borda e sombra');
    await tester.ensureVisible(tile);
    await tester.pumpAndSettle();
    await tester.tap(tile);
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    await gravarPrint(tester, chave, 'borda-e-sombra-folha');
    await tester.pump(const Duration(seconds: 1));
  });
}
