// O INSPECTOR DA CENA 3D (painel Cena) E OS PAINEIS DE MATERIAL, LUZ,
// AMBIENTE E ANIMACAO, na UI nova.
//
//   * posicao X com keyframe: o losango crava no cabecote, e editar sobre
//     a marca atualiza a marca (nao cria outra);
//   * escala uniforme: muda so a uniforme, e o arrasto e um desfazer;
//   * Material muda o material do objeto; a luz anima pelo losango.
import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/cena3d.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/luz.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/material.dart';
import 'package:aurea/src/features/editor/presentation/widgets/gizmo_da_cena_overlay.dart'
    show noDaCenaSelecionadoProvider;
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'banco.dart';

(ProviderContainer, String) _cenaComCubo() {
  final c = containerNovo();
  final ctl = controladorDe(c);
  ctl.addScene3DLayer(Duration.zero);
  final id = c.read(editorControllerProvider).layers.single.id;
  ctl.addSceneNode(id, Element3DKind.cube);
  return (c, id);
}

SceneNode _no(ProviderContainer c, String id) =>
    cenaDe(c, id).scene.nodes.where((n) => !n.isNull).first;

void main() {
  testWidgets('Transformar: posicao X ganha keyframe no cabecote, e editar '
      'sobre a marca atualiza a marca', (tester) async {
    final (c, id) = _cenaComCubo();
    final ctl = controladorDe(c);
    await montar(tester, c, (_) => PainelCena3D(layerId: id));

    // As tres familias do Transformar, cada uma com XYZ.
    for (final p in [
      'x',
      'y',
      'z',
      'giroX',
      'giroY',
      'giroZ',
      'escala',
      'escalaX',
      'escalaY',
      'escalaZ',
    ]) {
      expect(
        find.byKey(ValueKey('prop-cena3d-$p'), skipOffstage: false),
        findsOneWidget,
        reason: p,
      );
    }

    BancoDoPainel.relogio!.seek(const Duration(seconds: 1));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('kf-cena3d-x')));
    await tester.pumpAndSettle();
    var no = _no(c, id);
    expect(ctl.sceneNodeKeyframeTimes(no, PropDoNo.x), [
      const Duration(seconds: 1),
    ]);
    expect(ctl.sceneNodeKeyframeTimes(no, PropDoNo.y), isEmpty);

    final antes = no.x.valueAt(const Duration(seconds: 1));
    await arrastarLinha(tester, 'cena3d-x', 50);
    await tester.pumpAndSettle();
    no = _no(c, id);
    expect(no.x.valueAt(const Duration(seconds: 1)), isNot(antes));
    expect(
      ctl.sceneNodeKeyframeTimes(no, PropDoNo.x),
      hasLength(1),
      reason: 'editar sobre a marca nao cria outra',
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('Transformar: a escala UNIFORME muda so a uniforme, e o '
      'arrasto e um desfazer', (tester) async {
    final (c, id) = _cenaComCubo();
    await montar(tester, c, (_) => PainelCena3D(layerId: id));

    final antes = _no(c, id);
    final linha = find.byKey(
      const ValueKey('prop-cena3d-escala'),
      skipOffstage: false,
    );
    await tester.ensureVisible(linha);
    await tester.pumpAndSettle();
    await arrastarLinha(tester, 'cena3d-escala', 40);
    await tester.pumpAndSettle();

    final depois = _no(c, id);
    expect(
      depois.scale.valueAt(Duration.zero),
      greaterThan(antes.scale.valueAt(Duration.zero)),
    );
    expect(
      depois.scaleX.valueAt(Duration.zero),
      antes.scaleX.valueAt(Duration.zero),
    );
    expect(
      depois.scaleY.valueAt(Duration.zero),
      antes.scaleY.valueAt(Duration.zero),
    );
    expect(
      depois.scaleZ.valueAt(Duration.zero),
      antes.scaleZ.valueAt(Duration.zero),
    );

    controladorDe(c).undo();
    expect(
      _no(c, id).scale.valueAt(Duration.zero),
      antes.scale.valueAt(Duration.zero),
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('as seis abas do inspector abrem sem quebrar', (tester) async {
    final (c, id) = _cenaComCubo();
    await montar(tester, c, (_) => PainelCena3D(layerId: id));
    for (var i = 0; i < 6; i++) {
      await tocarNaAba(tester, 'cena3d', i);
      expect(find.byKey(const ValueKey('painel-cena3d')), findsOneWidget);
      expect(tester.takeException(), isNull, reason: 'aba $i');
    }
  });

  testWidgets('trocar o objeto nas fichas troca o que o painel mostra '
      '(e o que o gizmo segura)', (tester) async {
    final (c, id) = _cenaComCubo();
    controladorDe(c).addSceneNode(id, Element3DKind.sphere);
    await montar(tester, c, (_) => PainelCena3D(layerId: id));
    final primeiro = cenaDe(c, id).scene.nodes.first;
    final segundo = cenaDe(c, id).scene.nodes.last;
    final ficha = find.byKey(ValueKey('cena3d-objeto-${segundo.id}'));
    await tester.ensureVisible(ficha);
    await tester.tap(ficha);
    await tester.pumpAndSettle();
    expect(c.read(noDaCenaSelecionadoProvider), segundo.id);

    await arrastarLinha(tester, 'cena3d-x', 40);
    await tester.pumpAndSettle();
    final nos = cenaDe(c, id).scene.nodes;
    expect(
      nos.last.x.valueAt(Duration.zero),
      isNot(segundo.x.valueAt(Duration.zero)),
    );
    expect(
      nos.first.x.valueAt(Duration.zero),
      primeiro.x.valueAt(Duration.zero),
      reason: 'o outro objeto nao se mexe',
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('Material: metal e predefinicao mudam o material do objeto', (
    tester,
  ) async {
    final (c, id) = _cenaComCubo();
    await montar(tester, c, (_) => PainelMaterial(layerId: id));
    expect(find.byKey(const ValueKey('painel-material')), findsOneWidget);

    final antes = _no(c, id).material.metallic;
    await arrastarLinha(tester, 'cena3d-metal', 60);
    await tester.pumpAndSettle();
    expect(_no(c, id).material.metallic, greaterThan(antes));

    final cromo = find.byKey(const ValueKey('cena3d-preset-chrome'));
    await tester.ensureVisible(cromo);
    await tester.tap(cromo);
    await tester.pumpAndSettle();
    final m = _no(c, id).material;
    expect(m.metallic, materialFromPreset(MaterialPreset3D.chrome).metallic);
    expect(predefinicaoDoMaterial(m), MaterialPreset3D.chrome);
    expect(tester.takeException(), isNull);
  });

  testWidgets('Luz: a intensidade anima pelo losango', (tester) async {
    final (c, id) = _cenaComCubo();
    final ctl = controladorDe(c);
    if (cenaDe(c, id).scene.lights.isEmpty) {
      ctl.addSceneLight(id, Light3DKind.point);
    }
    await montar(tester, c, (_) => PainelLuz(layerId: id));
    expect(find.byKey(const ValueKey('painel-luz')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('kf-luz-intensidade')));
    await tester.pumpAndSettle();
    final luz = cenaDe(c, id).scene.lights.first;
    expect(ctl.sceneLightKeyframeTimes(luz, PropDaLuz.intensidade), [
      Duration.zero,
    ]);
    expect(tester.takeException(), isNull);
  });

  testWidgets('os paineis do 3D numa camada que nao e 3D dizem isso', (
    tester,
  ) async {
    final c = containerNovo();
    controladorDe(c).addTextLayer(Duration.zero);
    final texto = c.read(editorControllerProvider).layers.single.id;
    for (final p in [
      (String id) => PainelCena3D(layerId: id),
      (String id) => PainelMaterial(layerId: id),
      (String id) => PainelLuz(layerId: id),
    ]) {
      await montar(tester, c, (_) => p(texto));
      // Nenhuma linha de propriedade num painel de 3D aberto num texto.
      expect(find.byType(AureaPropertyRow), findsNothing);
      expect(tester.takeException(), isNull);
    }
  });
}
