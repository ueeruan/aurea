// O CADEADO PRECISA FUNCIONAR DE VERDADE.
//
// Os testadores relataram que o botao de bloquear nao fazia nada. Fazia
// metade: a timeline ja recusava mover, aparar e reordenar, mas o painel
// de transformacao e o arrasto no palco continuavam editando a camada
// travada — e o toque longo na etiqueta de cor, unico caminho para o
// cadeado, ninguem descobre.
//
// Estes testes prendem o contrato inteiro: o que a camada bloqueada NAO
// aceita, o que ela continua aceitando (senao nao haveria como sair do
// bloqueio), e a ida e volta pelo arquivo.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

void main() {
  late ProviderContainer container;
  late EditorController c;

  setUp(() {
    container = ProviderContainer();
    c = container.read(editorControllerProvider.notifier);
    c.openProject(
      VideoProject(
        name: 'cadeado',
        createdAt: DateTime(2026, 9, 17),
        layers: [
          ShapeLayer(
            id: 'a',
            name: 'Forma A',
            startTime: Duration.zero,
            duration: const Duration(seconds: 3),
            position: AnimatedOffset(const Offset(10, 20)),
            contents: [ShapePath(primitive: ShapePrimitive.rectangle)],
          ),
          ShapeLayer(
            id: 'b',
            name: 'Forma B',
            startTime: const Duration(seconds: 3),
            duration: const Duration(seconds: 3),
            position: AnimatedOffset(const Offset(40, 60)),
            contents: [ShapePath(primitive: ShapePrimitive.ellipse)],
          ),
        ],
      ),
    );
  });

  tearDown(() => container.dispose());

  Offset posicao(String id) => c.state
      .layerById(id)!
      .position
      .valueAt(c.state.layerById(id)!.localTime(Duration.zero));

  group('bloqueada nao aceita', () {
    setUp(() => c.toggleLocked('a'));

    test('mover no tempo', () {
      c.moveLayer('a', const Duration(seconds: 1));
      expect(c.state.layerById('a')!.startTime, Duration.zero);
    });

    test('aparar as duas pontas', () {
      c.trimLayerStart('a', const Duration(seconds: 1));
      c.trimLayerEnd('a', const Duration(seconds: 1));
      final l = c.state.layerById('a')!;
      expect(l.startTime, Duration.zero);
      expect(l.duration, const Duration(seconds: 3));
    });

    test('mover a posicao', () {
      final antes = posicao('a');
      c.editPosition('a', Duration.zero, const Offset(300, 300));
      expect(posicao('a'), antes);
    });

    test('reordenar na pilha', () {
      final antes = c.state.layers.map((l) => l.id).toList();
      c.reorderLayer('a', 1);
      expect(c.state.layers.map((l) => l.id).toList(), antes);
    });

    test('reordenar em lote — e o lote das outras passa', () {
      c.reorderLayers(['a', 'b'], 1);
      // 'a' esta no fim; quem podia andar era 'b'.
      expect(c.state.layers.map((l) => l.id).toList(), ['a', 'b']);
    });

    test('apagar', () {
      expect(c.removeLayer('a'), isFalse);
      expect(c.state.layerById('a'), isNotNull);
      expect(c.removeLayers(['a']), 1, reason: 'uma ficou de fora');
      expect(c.state.layerById('a'), isNotNull);
    });

    test('apagar por ripple', () {
      c.rippleDeleteLayer('a');
      expect(c.state.layerById('a'), isNotNull);
    });

    test('cravar keyframe', () {
      c.toggleKeyframe('a', Duration.zero, LayerProp.position);
      expect(c.state.layerById('a')!.position.isAnimated, isFalse);
    });
  });

  group('bloqueada continua aceitando', () {
    setUp(() => c.toggleLocked('a'));

    test('o proprio desbloqueio', () {
      expect(c.isLocked('a'), isTrue);
      c.toggleLocked('a');
      expect(c.isLocked('a'), isFalse);
    });

    test('as fichas de leitura da timeline: olho, solo, timida', () {
      c.toggleHidden('a');
      c.toggleSolo('a');
      c.toggleShy('a');
      final m = c.state.metaOf('a');
      expect([m.hidden, m.solo, m.shy], [true, true, true]);
    });

    test('duplicar e copiar', () {
      final quantas = c.state.layers.length;
      c.duplicateLayer('a');
      expect(c.state.layers.length, quantas + 1);
      expect(
        c.state.layers.firstWhere((l) => l.id != 'a' && l.id != 'b').name,
        contains('Forma A'),
      );
    });

    test('desfazer continua funcionando com a camada bloqueada', () {
      // O portao do cadeado mora no caminho da EDICAO. O desfazer troca o
      // projeto inteiro e nao passa por ele — se passasse, a pessoa
      // ficaria presa: bloqueada e sem poder voltar atras.
      c.toggleLocked('a'); // destrava o que o setUp travou
      c.moveLayer('a', const Duration(seconds: 1));
      c.toggleLocked('a');
      expect(c.isLocked('a'), isTrue);
      c.undo();
      expect(c.state.layerById('a')!.startTime, Duration.zero);
      // E o estado do cadeado vem junto no desfazer, como qualquer ficha.
      expect(c.isLocked('a'), isFalse);
    });
  });

  test('depois de desbloquear, a edicao volta a passar', () {
    c.toggleLocked('a');
    c.moveLayer('a', const Duration(seconds: 1));
    expect(c.state.layerById('a')!.startTime, Duration.zero);
    c.toggleLocked('a');
    c.moveLayer('a', const Duration(seconds: 1));
    expect(c.state.layerById('a')!.startTime, const Duration(seconds: 1));
  });

  test('a camada VIZINHA nao sofre com o cadeado da outra', () {
    c.toggleLocked('a');
    c.moveLayer('b', const Duration(seconds: 5));
    expect(c.state.layerById('b')!.startTime, const Duration(seconds: 5));
  });

  test('o bloqueio sobrevive a ida e volta pelo arquivo', () {
    c.toggleLocked('a');
    c.toggleHidden('b');
    final volta = projectFromJson(projectToJson(c.state));
    final outro = ProviderContainer();
    addTearDown(outro.dispose);
    final c2 = outro.read(editorControllerProvider.notifier);
    c2.openProject(volta);
    expect(c2.isLocked('a'), isTrue);
    expect(c2.state.metaOf('b').hidden, isTrue);
    expect(c2.state.metaOf('b').locked, isFalse);
  });

  _naTela();
}

// ------------------------------------------------------------- na tela
//
// O CONTRATO QUE OS TESTADORES COBRARAM: da para VER que a camada esta
// bloqueada, da para DESBLOQUEAR sem procurar, e as alcas que prometiam um
// gesto recusado nao aparecem.
void _naTela() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  testWidgets('a camada bloqueada mostra a faixa e perde as alcas', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    final id = c.read(editorControllerProvider).layers.first.id;

    // Desbloqueada: a alca existe e a faixa nao.
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('alca-escala')), findsOneWidget);
    expect(find.byKey(ValueKey('faixa-bloqueio-$id')), findsNothing);

    e.toggleLocked(id);
    await tester.pumpAndSettle();
    expect(
      find.byKey(const ValueKey('alca-escala')),
      findsNothing,
      reason: 'a alca prometia um gesto que o cadeado recusa',
    );
    expect(
      find.byKey(ValueKey('faixa-bloqueio-$id')),
      findsOneWidget,
      reason: 'a camada bloqueada precisa se anunciar',
    );
  });

  testWidgets('o botao da faixa desbloqueia a camada', (tester) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    e.toggleLocked(id);
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(ValueKey('desbloquear-$id')));
    await tester.pumpAndSettle();

    expect(e.isLocked(id), isFalse);
    expect(find.byKey(const ValueKey('alca-escala')), findsOneWidget);
  });
}
