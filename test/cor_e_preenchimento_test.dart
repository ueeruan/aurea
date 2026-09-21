// COR E PREENCHIMENTO (v1.1.1): a forma troca entre nenhum, cor, degrade
// (linear, radial, varredura) e foto sem perder a cor nem o traco; as
// outras camadas ganham cor ou degrade por cima, ou ficam intrinsecas.
import 'dart:ui' show Color, Rect, Size;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart' hide Color, Rect, Size;
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

ShapeLayer _forma(ProviderContainer c) =>
    c.read(editorControllerProvider).layers.first as ShapeLayer;

void main() {
  test('trocar de tipo guarda a cor e deixa o traco por cima', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'F');
    final id = _forma(c).id;
    e.setShapePrimaryColor(id, const Color(0xFF3366FF));
    e.ensureShapeStroke(id);
    expect(tipoDePreenchimentoDe(_forma(c).contents), TipoDePreenchimento.cor);

    e.definirTipoDePreenchimento(id, TipoDePreenchimento.degrade);
    final g = _forma(c).contents.whereType<ShapeGradientFill>().single;
    expect(g.colorA, const Color(0xFF3366FF));
    expect(_forma(c).contents.whereType<ShapeFill>(), isEmpty);

    e.definirTipoDePreenchimento(id, TipoDePreenchimento.nenhum);
    expect(tipoDePreenchimentoDe(_forma(c).contents), TipoDePreenchimento.nenhum);
    expect(_forma(c).contents.whereType<ShapeStroke>(), hasLength(1));

    // Voltar para Cor devolve a cor de antes, e o preenchimento entra
    // ANTES do traco.
    e.definirTipoDePreenchimento(id, TipoDePreenchimento.cor);
    final itens = _forma(c).contents;
    final fill = itens.whereType<ShapeFill>().single;
    expect(fill.color, const Color(0xFF3366FF));
    expect(itens.indexOf(fill), lessThan(itens.indexWhere((i) => i is ShapeStroke)));

    e.definirTipoDePreenchimento(id, TipoDePreenchimento.midia, midia: 'foto.png');
    final m = _forma(c).contents.whereType<ShapeMediaFill>().single;
    expect(m.sourcePath, 'foto.png');
    e.definirEncaixeDaMidiaNaForma(id, EncaixeNaForma.esticar);
    expect(
      _forma(c).contents.whereType<ShapeMediaFill>().single.encaixe,
      EncaixeNaForma.esticar,
    );
    // Midia sem foto nao faz nada.
    e.definirTipoDePreenchimento(id, TipoDePreenchimento.nenhum);
    e.definirTipoDePreenchimento(id, TipoDePreenchimento.midia);
    expect(_forma(c).contents.whereType<ShapeMediaFill>(), hasLength(1),
        reason: 'a foto guardada volta');
  });

  test('a foto pousa na forma: preencher cobre, caber cabe, esticar deforma', () {
    const caixa = Rect.fromLTWH(0, 0, 200, 100);
    const foto = Size(100, 100);
    expect(destinoDaMidiaNaForma(caixa, foto, EncaixeNaForma.preencher).width, 200);
    expect(destinoDaMidiaNaForma(caixa, foto, EncaixeNaForma.caber).width, 100);
    expect(destinoDaMidiaNaForma(caixa, foto, EncaixeNaForma.esticar), caixa);
  });

  test('avaliar a forma: foto vira desenho com imagem; varredura ganha shader', () {
    final draws = evaluateShape([
      ...ShapePresets.paramRect().where((i) => i is! ShapeFill),
      ShapeMediaFill(sourcePath: 'x.png', encaixe: EncaixeNaForma.caber),
    ], Duration.zero);
    expect(draws.single.imagem, 'x.png');
    expect(draws.single.encaixe, EncaixeNaForma.caber);
    final varredura = evaluateShape([
      ...ShapePresets.paramRect().where((i) => i is! ShapeFill),
      ShapeGradientFill(varredura: true),
    ], Duration.zero);
    expect(varredura.single.paint.shader, isNotNull);
  });

  test('foto e varredura voltam do arquivo', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'F');
    final id = _forma(c).id;
    e.definirTipoDePreenchimento(id, TipoDePreenchimento.midia, midia: '/tmp/a.png');
    e.addShapeLayer(Duration.zero, name: 'G');
    final g = c.read(editorControllerProvider).layers.first.id;
    e.definirTipoDePreenchimento(g, TipoDePreenchimento.degrade);
    final gid = (c.read(editorControllerProvider).layers.first as ShapeLayer)
        .contents
        .whereType<ShapeGradientFill>()
        .single
        .id;
    e.updateShapeGradient(g, gid, (x) => x.copyWith(varredura: true));
    final volta = projectFromJson(projectToJson(c.read(editorControllerProvider)));
    final formas = volta.layers.whereType<ShapeLayer>().toList();
    expect(
      formas.expand((f) => f.contents).whereType<ShapeMediaFill>().single.sourcePath,
      '/tmp/a.png',
    );
    expect(
      formas.expand((f) => f.contents).whereType<ShapeGradientFill>().single.varredura,
      isTrue,
    );
  });

  // O PAINEL COR da casca nova: o tipo de preenchimento e uma ESCOLHA
  // (AureaDropdown na linha "Preenchimento"); o menu dela numera os itens
  // na ordem das opcoes (`menu-<indice>`).
  Future<void> escolher(WidgetTester tester, int indice) async {
    await tester.tap(
      find.descendant(
        of: find.byKey(const ValueKey('prop-cor-preenchimento')),
        matching: find.byWidgetPredicate((w) => w is AureaDropdown),
      ),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(ValueKey('menu-$indice')).last);
    await tester.pumpAndSettle();
  }

  Future<void> abrirCor(WidgetTester tester, ProviderContainer c, String id) async {
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: const MaterialApp(home: EditorScreen()),
      ),
    );
    await tester.pumpAndSettle();
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    c.read(painelAbertoProvider.notifier).state = PainelId.cor;
    await tester.pumpAndSettle();
  }

  testWidgets('painel da forma: a escolha troca o tipo', (tester) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = _container();
    c.read(editorControllerProvider.notifier).addShapeLayer(Duration.zero, name: 'F');
    final id = _forma(c).id;
    await abrirCor(tester, c, id);
    // Opcoes: nenhum, cor, degrade, midia.
    await escolher(tester, 2);
    expect(tipoDePreenchimentoDe(_forma(c).contents), TipoDePreenchimento.degrade);
    await escolher(tester, 0);
    expect(tipoDePreenchimentoDe(_forma(c).contents), TipoDePreenchimento.nenhum);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });

  testWidgets('texto: cor por cima e de volta ao intrinseco', (tester) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = _container();
    c.read(editorControllerProvider.notifier).addTextLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.first.id;
    await abrirCor(tester, c, id);
    // Opcoes: a propria cor do texto, cor por cima, degrade.
    await escolher(tester, 1);
    expect(
      c.read(editorControllerProvider).metaOf(id).styles.colorOverlay?.enabled,
      isTrue,
    );
    await escolher(tester, 2);
    final estilos = c.read(editorControllerProvider).metaOf(id).styles;
    expect(estilos.gradientOverlay?.enabled, isTrue);
    expect(estilos.colorOverlay, isNull);
    await escolher(tester, 0);
    expect(c.read(editorControllerProvider).metaOf(id).styles.isEmpty, isTrue);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });
}
