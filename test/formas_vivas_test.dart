// FORMAS VIVAS (v1.1.1): seta, linha larga, lua, multifolio, mais, selo,
// gota e balao nascem com a ficha propria de numeros animaveis, voltam
// do arquivo, entram na biblioteca do "+", viram pontos editaveis e tem
// alcas no palco com o Editar forma aberto.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/shape_library.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

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

/// Pontos espalhados pelo contorno: muda se o desenho muda de verdade.
List<String> _assinatura(Path p) => [
  for (final m in p.computeMetrics())
    for (var i = 0; i < 24; i++)
      if (m.getTangentForOffset(m.length * i / 24) case final tg?)
        '${tg.position.dx.toStringAsFixed(1)},${tg.position.dy.toStringAsFixed(1)}',
];

ShapeParametric _forma(List<ShapeItem> itens) =>
    itens.whereType<ShapeParametric>().single;

const _vivas = <ParamShapeKind, List<ShapeItem> Function()>{
  ParamShapeKind.seta: ShapePresets.paramSeta,
  ParamShapeKind.linhaLarga: ShapePresets.paramLinhaLarga,
  ParamShapeKind.lua: ShapePresets.paramLua,
  ParamShapeKind.multifolio: ShapePresets.paramMultifolio,
  ParamShapeKind.mais: ShapePresets.paramMais,
  ParamShapeKind.selo: ShapePresets.paramSelo,
  ParamShapeKind.gota: ShapePresets.paramGota,
  ParamShapeKind.balao: ShapePresets.paramBalao,
};

void main() {
  test('toda forma viva desenha, tem ficha e cada numero da ficha muda o desenho', () {
    for (final e in _vivas.entries) {
      final s = _forma(e.value());
      expect(s.kind, e.key);
      final antes = s.buildAt(Duration.zero).getBounds();
      expect(antes.isEmpty, isFalse, reason: e.key.name);
      for (final chave in parametrosDaForma(e.key)) {
        final trilha = shapeParamTrackOf(s, chave);
        expect(trilha, isNotNull, reason: '${e.key.name}.$chave');
        expect(fichaDoParametroDaForma(chave, e.key).rotulo, isNot(chave));
        if (chave == 'shapeRotation') continue;
        final v = trilha!.valueAt(Duration.zero);
        final mudada = shapeParamWithTrack(
          s,
          chave,
          AnimatedDouble(chave == 'aperto' ? v + 3 : v * .5 + 7),
        );
        expect(
          _assinatura(mudada.buildAt(Duration.zero)),
          isNot(_assinatura(s.buildAt(Duration.zero))),
          reason: '${e.key.name}.$chave nao mudou nada',
        );
      }
    }
  });

  test('a seta: haste, ponta e comprimento na conta certa', () {
    final s = ShapeParametric(
      kind: ParamShapeKind.seta,
      sizeX: AnimatedDouble(400),
      extras: {
        'larguraDaCauda': AnimatedDouble(40),
        'larguraDaCabeca': AnimatedDouble(120),
        'comprimentoDaCabeca': AnimatedDouble(100),
      },
    );
    final b = s.buildAt(Duration.zero).getBounds();
    expect(b.width, closeTo(400, 1e-6));
    expect(b.height, closeTo(120, 1e-6));
    final caminho = s.buildAt(Duration.zero);
    expect(caminho.contains(const Offset(-150, 0)), isTrue, reason: 'haste');
    expect(caminho.contains(const Offset(-150, 30)), isFalse, reason: 'fora da haste');
    expect(caminho.contains(const Offset(120, 40)), isTrue, reason: 'ponta larga');
  });

  test('selo tem furos na borda e lua e um circulo mordido', () {
    final selo = _forma(ShapePresets.paramSelo()).buildAt(Duration.zero);
    final b = selo.getBounds();
    // O meio de uma borda cai num furo ou entre dois: algum ponto da borda
    // de cima fica de fora.
    final foraNaBorda = [
      for (var x = b.left + 5; x < b.right - 5; x += 3)
        if (!selo.contains(Offset(x, b.top + 2))) x,
    ];
    expect(foraNaBorda, isNotEmpty);
    expect(selo.contains(b.center), isTrue);

    final lua = _forma(ShapePresets.paramLua()).buildAt(Duration.zero);
    expect(lua.contains(const Offset(-120, 0)), isTrue);
    expect(lua.contains(const Offset(60, 0)), isFalse, reason: 'mordida');
  });

  test('as formas vivas voltam do arquivo com os extras', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'B', contents: ShapePresets.paramBalao());
    final id = c.read(editorControllerProvider).layers.first.id;
    e.editShapeParam(id, 'caudaX', Duration.zero, 80);
    final volta = projectFromJson(projectToJson(c.read(editorControllerProvider)));
    final s = _forma((volta.layers.single as ShapeLayer).contents);
    expect(s.kind, ParamShapeKind.balao);
    expect(shapeParamTrackOf(s, 'caudaX')!.valueAt(Duration.zero), 80);
    expect(shapeParamTrackOf(s, 'caudaY')!.valueAt(Duration.zero), 110, reason: 'padrao');
  });

  test('a biblioteca do "+" usa as formas vivas e todas desenham', () {
    final nomes = {for (final e in shapeLibrary) e.nome: e};
    for (final nome in ['Seta', 'Mais', 'Lua', 'Balao', 'Gota', 'Linha larga', 'Multifolio', 'Selo', 'Arco', 'Pizza']) {
      expect(nomes.containsKey(nome), isTrue, reason: nome);
      expect(nomes[nome]!.build().whereType<ShapeParametric>(), hasLength(1), reason: nome);
    }
  });

  test('forma viva vira pontos editaveis', () {
    for (final e in _vivas.entries) {
      final b = bezierOfShapeItem(_forma(e.value()), Duration.zero);
      expect(b, isNotNull, reason: e.key.name);
      expect(b!.vertices.length, greaterThanOrEqualTo(3), reason: e.key.name);
    }
  });

  test('alcas: puxar a borda muda o tamanho; a cauda do balao vai com o dedo', () {
    final balao = _forma(ShapePresets.paramBalao());
    final alcas = {for (final a in alcasDaForma(balao, Duration.zero)) a.chave: a.ponto};
    expect(alcas['sizeX'], const Offset(190, 0));
    expect(
      valoresDaAlcaDaForma(balao, 'sizeX', const Offset(250, 10), Duration.zero),
      {'sizeX': 500},
    );
    final cauda = valoresDaAlcaDaForma(balao, 'cauda', const Offset(40, 200), Duration.zero);
    expect(cauda['caudaX'], 40);
    expect(cauda['caudaY'], 80);
    // Com o desenho girado, a alca gira junto e a conta desfaz o giro.
    final seta = shapeParamWithTrack(
      _forma(ShapePresets.paramSeta()),
      'shapeRotation',
      AnimatedDouble(90),
    );
    final ponta = alcasDaForma(seta, Duration.zero).firstWhere((a) => a.chave == 'sizeX').ponto;
    expect(ponta.dx, closeTo(0, 1e-6));
    expect(ponta.dy, closeTo(210, 1e-6));
    expect(
      valoresDaAlcaDaForma(seta, 'sizeX', const Offset(0, 300), Duration.zero)['sizeX'],
      closeTo(600, 1e-6),
    );
  });

  testWidgets('no palco: com o Editar forma aberto aparecem as alcas e o arrasto muda a forma', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = _container();
    c
        .read(editorControllerProvider.notifier)
        .addShapeLayer(Duration.zero, name: 'M', contents: ShapePresets.paramMais());
    final id = c.read(editorControllerProvider).layers.first.id;
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: const MaterialApp(home: EditorScreen()),
      ),
    );
    await tester.pumpAndSettle();
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('alca-forma-sizeX')), findsNothing, reason: 'so no Editar forma');
    c.read(editorSessionProvider.notifier).openShape(ShapeTool.size);
    await tester.pumpAndSettle();
    final alca = find.byKey(const ValueKey('alca-forma-sizeX'));
    expect(alca, findsOneWidget);
    expect(find.byKey(const ValueKey('alca-forma-largura')), findsOneWidget);

    double tamanho() => shapeParamTrackOf(
      _forma((c.read(editorControllerProvider).layerById(id)! as ShapeLayer).contents),
      'sizeX',
    )!.valueAt(Duration.zero);
    final antes = tamanho();
    final inicio = tester.getCenter(alca);
    final gesto = await tester.startGesture(inicio);
    await tester.pump();
    await gesto.moveBy(const Offset(20, 0));
    await tester.pump();
    await gesto.moveBy(const Offset(20, 0));
    await tester.pump();
    await gesto.up();
    await tester.pumpAndSettle();
    expect(tamanho(), greaterThan(antes + 10));
    expect(find.byType(PreviewStage), findsOneWidget);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });
}
