import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/domain/grid_rig.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/shape_ops.dart';
import 'package:aurea/src/features/editor/presentation/ui/curva/curva.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/clonar.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/forma.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_test/flutter_test.dart';

import '../paineis/apoio_paineis.dart';

// AS PORTAS DO PAINEL FORMA: a aba Operadores (Trim Paths, Repeater,
// Morph, operadores de caminho, Combinar, geometria composta, remover) e a
// curva das trilhas da forma — parametro da primitiva, traco e trim — e da
// grade do Clonar, pelo editor de curva novo.

const _aba = 'painel-forma-aba';

/// Toca em [chave]: se a peca saiu da lista (a lista do painel e
/// preguicosa), volta ao topo e rola ate ela.
Future<void> _tocar(WidgetTester tester, String chave) async {
  final alvo = find.byKey(ValueKey(chave));
  if (alvo.evaluate().isEmpty) {
    final lista = find
        .descendant(
          of: find.byKey(const ValueKey('painel-forma-corpo')),
          matching: find.byType(Scrollable),
        )
        .first;
    await tester.drag(lista, const Offset(0, 3000));
    await tester.pumpAndSettle();
    await tester.scrollUntilVisible(alvo, 80, scrollable: lista);
  }
  await tester.ensureVisible(alvo);
  await tester.pumpAndSettle();
  await tester.tap(alvo);
  await tester.pumpAndSettle();
}

Future<(BancadaDoPainel, String)> _forma(WidgetTester tester) => montarPainel(
  tester,
  altura: 700,
  tamanho: const Size(390, 900),
  preparar: (c) {
    c.addShapeLayer(Duration.zero, name: 'Forma');
    final id = c.projetoCompleto.layers.single.id;
    // A forma parametrica (a do painel "Forma" com numeros por tipo).
    c.convertShapeToParametric(id);
    return id;
  },
  painel: (id) => PainelForma(layerId: id),
);

List<ShapeItem> _itens(BancadaDoPainel b, String id) =>
    (b.camada(id) as ShapeLayer).contents;

void main() {
  testWidgets('Operadores: adicionar, editar e remover — um desfazer por '
      'acao', (tester) async {
    final (b, id) = await _forma(tester);
    await _tocar(tester, '$_aba-4');

    // + REPEATER e as copias.
    await _tocar(tester, 'operador-add-repeater');
    final rep = _itens(b, id).whereType<RepeaterOperator>().single;
    expect(rep.copies, 3);
    b.c.editRepeater(id, rep.id, Duration.zero, copies: 5);
    await tester.pumpAndSettle();
    expect(find.byKey(ValueKey('prop-operador-${rep.id}-copias')), findsOneWidget);

    // + TRIM PATHS e o modo.
    await _tocar(tester, 'operador-add-trim');
    final trim = _itens(b, id).whereType<TrimOperator>().single;
    expect(trim.individually, isTrue);
    await _tocar(tester, 'operador-${trim.id}-modo-continuo');
    expect(
      _itens(b, id).whereType<TrimOperator>().single.individually,
      isFalse,
    );

    // + OPERADOR DE CAMINHO (torcer) e o numero dele.
    await _tocar(tester, 'operador-add-twist');
    final torcer = _itens(b, id).whereType<TwistOperator>().single;
    expect(
      find.byKey(ValueKey('prop-operador-${torcer.id}-valor')),
      findsOneWidget,
    );

    // + COMBINAR e o modo (o controlador gira; a porta anda ate o escolhido).
    await _tocar(tester, 'operador-add-merge');
    final merge = _itens(b, id).whereType<MergePathsOperator>().single;
    expect(merge.mode, MergeMode.union);
    await tester.ensureVisible(
      find.byKey(ValueKey('prop-operador-${merge.id}-merge')),
    );
    await tester.pumpAndSettle();
    await tester.tap(
      find.descendant(
        of: find.byKey(ValueKey('prop-operador-${merge.id}-merge')),
        matching: find.byType(AureaDropdown<MergeMode>),
      ),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('menu-2')));
    await tester.pumpAndSettle();
    expect(
      _itens(b, id).whereType<MergePathsOperator>().single.mode,
      MergeMode.intersect,
    );
    b.c.undo();
    expect(
      _itens(b, id).whereType<MergePathsOperator>().single.mode,
      MergeMode.union,
      reason: 'girar duas vezes foi UM passo',
    );

    // GEOMETRIA COMPOSTA: mais uma primitiva na mesma forma.
    await tester.pumpAndSettle();
    final antes = _itens(b, id).whereType<ShapeParametric>().length;
    await _tocar(tester, 'operador-geometria-ellipse');
    expect(_itens(b, id).whereType<ShapeParametric>().length, antes + 1);

    // REMOVER o repeater.
    await _tocar(tester, 'operador-${rep.id}-remover');
    expect(_itens(b, id).whereType<RepeaterOperator>(), isEmpty);
    b.c.undo();
    expect(_itens(b, id).whereType<RepeaterOperator>(), hasLength(1));
    expect(tester.takeException(), isNull);
  });

  testWidgets('Morph: morfar para uma forma, progresso com losango, desfazer '
      'o morph', (tester) async {
    final (b, id) = await montarPainel(
      tester,
      altura: 700,
      tamanho: const Size(390, 900),
      preparar: (c) {
        c.addShapeLayer(
          Duration.zero,
          name: 'Caminho',
          contents: [
            ShapePath(primitive: ShapePrimitive.star),
            ShapeFill(color: const Color(0xFFFFFFFF)),
          ],
        );
        return c.projetoCompleto.layers.single.id;
      },
      painel: (id) => PainelForma(layerId: id),
    );
    await _tocar(tester, '$_aba-4');
    await _tocar(tester, 'operador-add-morph');
    await _tocar(tester, 'menu-morfar-0');
    final morph = _itens(b, id).whereType<ShapeMorph>().single;
    expect(morph.to.primitive, ShapePrimitive.ellipse);
    // O losango do progresso crava a marca.
    await _tocar(tester, 'kf-operador-${morph.id}-progresso');
    expect(
      _itens(b, id).whereType<ShapeMorph>().single.progress.hasKeyframeAt(
        Duration.zero,
      ),
      isTrue,
    );
    b.c.editMorphProgress(id, morph.id, Duration.zero, .5);
    await tester.pumpAndSettle();
    // DESFAZER O MORPH volta a forma de origem.
    await _tocar(tester, 'operador-${morph.id}-remover');
    expect(_itens(b, id).whereType<ShapeMorph>(), isEmpty);
    expect(
      _itens(b, id).whereType<ShapePath>().single.primitive,
      ShapePrimitive.star,
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('curva: toque longo no losango do parametro abre o editor e '
      '"aplicar em todos" grava na trilha da forma', (tester) async {
    final (b, id) = await _forma(tester);
    final sp = _itens(b, id).whereType<ShapeParametric>().single;
    final chave = parametrosDaForma(sp.kind).first;
    // Duas marcas: so entao ha trecho.
    b.c.toggleShapeParamKeyframe(id, chave, Duration.zero);
    b.c.toggleShapeParamKeyframe(id, chave, const Duration(seconds: 1));
    await tester.pumpAndSettle();
    final trilha = shapeParamTrackOf(
      _itens(b, id).whereType<ShapeParametric>().single,
      chave,
    )!;
    expect(trilha.keyframes, hasLength(2));

    await tester.longPress(find.byKey(ValueKey('kf-forma-$chave')));
    await tester.pumpAndSettle();
    expect(find.byType(EditorDeCurva), findsOneWidget);
    final editor = tester.widget<EditorDeCurva>(find.byType(EditorDeCurva));
    // O editor recebeu a porta que grava na forma, e ela grava.
    editor.trilha.gravarEmTodos!(b.c, b.camada(id), Easing.easeIn);
    final depois = shapeParamTrackOf(
      _itens(b, id).whereType<ShapeParametric>().single,
      chave,
    )!;
    expect(depois.easeAt(Duration.zero).mesmoPresetQue(Easing.easeIn), isTrue);
    editor.trilha.gravar(b.c, b.camada(id), Duration.zero, Easing.easeOut);
    expect(
      shapeParamTrackOf(
        _itens(b, id).whereType<ShapeParametric>().single,
        chave,
      )!.easeAt(Duration.zero).mesmoPresetQue(Easing.easeOut),
      isTrue,
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('curva do traco (item da forma) pelo losango da aba Traço', (
    tester,
  ) async {
    final (b, id) = await _forma(tester);
    final traco = b.c.ensureShapeStroke(id)!;
    b.c.toggleShapeItemTrackKeyframe(id, traco, 'width', Duration.zero);
    b.c.toggleShapeItemTrackKeyframe(
      id,
      traco,
      'width',
      const Duration(seconds: 1),
    );
    await tester.pumpAndSettle();
    await _tocar(tester, '$_aba-2');
    await tester.longPress(find.byKey(const ValueKey('kf-forma-traco-width')));
    await tester.pumpAndSettle();
    final editor = tester.widget<EditorDeCurva>(find.byType(EditorDeCurva));
    editor.trilha.gravarEmTodos!(b.c, b.camada(id), Easing.easeIn);
    final s = _itens(b, id).whereType<ShapeStroke>().single;
    expect(s.width.easeAt(Duration.zero).mesmoPresetQue(Easing.easeIn), isTrue);
    // E o deslocamento do tracejado tem linha de novo.
    expect(
      find.byKey(const ValueKey('prop-forma-traco-dashOffset')),
      findsOneWidget,
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('Clonar: curva da trilha da grade pelo losango', (tester) async {
    final (b, id) = await montarPainel(
      tester,
      altura: 700,
      tamanho: const Size(390, 900),
      preparar: (c) {
        c.addShapeLayer(Duration.zero);
        final forma = c.projetoCompleto.layers.first.id;
        c.addNullLayer(Duration.zero);
        final nulo = c.projetoCompleto.layers.first.id;
        c.setGridAssets(nulo, [forma]);
        return nulo;
      },
      painel: (id) => PainelClonar(layerId: id),
    );
    final l = b.camada(id) as NullLayer;
    expect(l.grid, isNotNull);
    b.c.toggleGridTransitionKeyframe(id, Duration.zero);
    b.c.toggleGridTransitionKeyframe(id, const Duration(seconds: 1));
    await tester.pumpAndSettle();
    await tester.longPress(find.byKey(const ValueKey('kf-clonar-morph')));
    await tester.pumpAndSettle();
    final editor = tester.widget<EditorDeCurva>(find.byType(EditorDeCurva));
    editor.trilha.gravarEmTodos!(b.c, b.camada(id), Easing.easeIn);
    final grade = (b.camada(id) as NullLayer).grid!;
    expect(
      gridTrackOf(grade, 'transition')!
          .easeAt(Duration.zero)
          .mesmoPresetQue(Easing.easeIn),
      isTrue,
    );
    editor.trilha.gravar(b.c, b.camada(id), Duration.zero, Easing.easeOut);
    expect(
      gridTrackOf((b.camada(id) as NullLayer).grid!, 'transition')!
          .easeAt(Duration.zero)
          .mesmoPresetQue(Easing.easeOut),
      isTrue,
    );
    expect(tester.takeException(), isNull);
  });
}
