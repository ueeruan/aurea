// O PAINEL TEXTO 3D DA UI NOVA.
//
// O que estes testes prendem:
//
//   * o painel so existe para camada que E Texto 3D — texto comum, modelo
//     3D e video nao ganham a ferramenta nem os controles;
//   * Profundidade refaz a MALHA, e o arrasto inteiro e um desfazer so;
//   * Metal muda o material da letra (o numero e o do modelo que vai para
//     o motor, e nao so o do painel);
//   * Caracteres: escolher o "C" de "ABCDE" e empurrar em Z move SO o "C".
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d_animado.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/texto3d.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'banco.dart';

void main() {
  testWidgets('o painel Texto 3D so existe para camada de Texto 3D '
      '(texto, texto 3D, modelo 3D, video)', (tester) async {
    final c = containerNovo();
    final ctl = controladorDe(c);
    final (texto3d, _) = await criarTexto3D(tester, c);
    ctl.addTextLayer(Duration.zero);
    ctl.addScene3DLayer(Duration.zero);
    final camadas = c.read(editorControllerProvider).layers;
    final texto = camadas.whereType<TextLayer>().single.id;
    final modelo = camadas
        .whereType<Scene3DLayer>()
        .firstWhere((l) => l.id != texto3d)
        .id;
    ctl.addSceneNode(modelo, Element3DKind.cube);
    final video = ctl.addVideoLayer(
      Duration.zero,
      '/nao/existe.mp4',
      'Clipe',
      const Duration(seconds: 3),
    );

    final casos = {texto: false, texto3d: true, modelo: false, video: false};
    for (final caso in casos.entries) {
      final camada = c.read(editorControllerProvider).layerById(caso.key);
      final ferramentas = ferramentasDa(camada).map((f) => f.id).toList();
      expect(
        ferramentas.contains(PainelId.texto3d.name),
        caso.value,
        reason: 'barra de ${camada.runtimeType}',
      );

      await montar(
        tester,
        c,
        (_) => PainelTexto3D(key: ValueKey(caso.key), layerId: caso.key),
      );
      expect(find.byKey(const ValueKey('painel-texto3d')), findsOneWidget);
      // Abre na Extrusao: a Profundidade e a primeira linha que se ve.
      expect(
        find.byKey(const ValueKey('prop-texto3d-profundidade')),
        caso.value ? findsOneWidget : findsNothing,
        reason: 'controles em ${camada.runtimeType}',
      );
      expect(
        find.text(
          'Esta camada não é um Texto 3D. Num texto, use a ferramenta '
          'Texto 3D da barra para convertê-lo.',
        ),
        caso.value ? findsNothing : findsOneWidget,
      );
      expect(tester.takeException(), isNull);
    }
  });

  testWidgets('Profundidade refaz a malha, e o arrasto e UM desfazer', (
    tester,
  ) async {
    final c = containerNovo();
    final (cena, no) = await criarTexto3D(tester, c);
    await montar(tester, c, (_) => PainelTexto3D(layerId: cena));

    final antes = cenaDe(c, cena).scene.nodeById(no)!;
    await arrastarLinha(tester, 'texto3d-profundidade', 60);
    await esperarAMalha(tester);

    final depois = cenaDe(c, cena).scene.nodeById(no)!;
    expect(depois.texto3d!.espessura, greaterThan(antes.texto3d!.espessura));
    expect(
      identical(depois.modelAsset, antes.modelAsset),
      isFalse,
      reason: 'a profundidade muda a malha que vai para o motor',
    );
    // O painel mostra o numero gravado (a fila esvaziou).
    expect(find.byKey(const ValueKey('texto3d-montando')), findsNothing);

    controladorDe(c).undo();
    expect(
      cenaDe(c, cena).scene.nodeById(no)!.texto3d!.espessura,
      antes.texto3d!.espessura,
      reason: 'o arrasto inteiro volta num toque de desfazer',
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('Metal muda o material da letra que vai para o motor', (
    tester,
  ) async {
    final c = containerNovo();
    final (cena, no) = await criarTexto3D(tester, c);
    await montar(tester, c, (_) => PainelTexto3D(layerId: cena));

    await tocarNaAba(tester, 'texto3d', 2);
    expect(find.byKey(const ValueKey('prop-texto3d-metal')), findsOneWidget);

    await arrastarLinha(tester, 'texto3d-metal', -80);
    await esperarAMalha(tester);

    final t = cenaDe(c, cena).scene.nodeById(no)!;
    final metal = t.texto3d!.metalico;
    expect(metal, isNotNull);
    expect(metal!, lessThan(1));
    final materiais = t.modelAsset!.data['materials'] as List;
    expect(
      ((materiais.first as Map)['metallic'] as num).toDouble(),
      closeTo(metal, 1e-9),
      reason: 'o numero do painel chega ao material do modelo',
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('Caracteres: ajustar o "C" de ABCDE move so o "C"', (
    tester,
  ) async {
    final c = containerNovo();
    final (cena, no) = await criarTexto3D(tester, c, 'ABCDE');
    await montar(tester, c, (_) => PainelTexto3D(layerId: cena));

    await tocarNaAba(tester, 'texto3d', 4);
    // As nove medidas, cada uma com o losango da casa.
    for (final m in MedidaDoCaractere.values) {
      expect(
        find.byKey(ValueKey('kf-texto3d-ajuste-${m.name}')),
        findsOneWidget,
        reason: m.name,
      );
    }

    final uma = find.byKey(const ValueKey('texto3d-selecao-uma'));
    await tester.ensureVisible(uma);
    await tester.tap(uma);
    await tester.pumpAndSettle();
    final letraC = find.byKey(const ValueKey('texto3d-letra-2'));
    await tester.ensureVisible(letraC);
    await tester.tap(letraC);
    await tester.pumpAndSettle();

    await arrastarLinha(tester, 'texto3d-ajuste-z', 60);
    await tester.pumpAndSettle();

    final n = cenaDe(c, cena).scene.nodeById(no)!;
    final ajustes = n.texto3d!.ajustes;
    expect(ajustes, hasLength(1));
    expect(ajustes.single.inicio, 2);
    expect(ajustes.single.fim, 2);
    final z = ajustes.single.valorEm(MedidaDoCaractere.z, Duration.zero);
    expect(z, greaterThan(0));

    // A PROVA E A MATRIZ DAS CINCO LETRAS: so a terceira saiu do lugar.
    final dados = n.modelAsset!.data;
    final matrizes = matrizesDoTextoAnimado(dados, Duration.zero, null)!;
    final nodes = dados['nodes'] as List;
    for (var i = 1; i < nodes.length; i++) {
      final tr = (nodes[i] as Map)['translation'] as List;
      final m = matrizes[i];
      final dz = m == null ? 0.0 : m.getTranslation().z - (tr[2] as num);
      if (i == 3) {
        expect(dz, closeTo(z, 1e-6), reason: 'o C anda em Z');
      } else {
        expect(dz.abs(), lessThan(1e-9), reason: 'letra $i ficou');
      }
    }

    // Um toque em desfazer tira o ajuste inteiro.
    controladorDe(c).undo();
    expect(cenaDe(c, cena).scene.nodeById(no)!.texto3d!.ajustes, isEmpty);
    expect(tester.takeException(), isNull);
  });

  testWidgets('Caracteres: o losango crava keyframe na letra, no cabecote', (
    tester,
  ) async {
    final c = containerNovo();
    final (cena, no) = await criarTexto3D(tester, c, 'ABC');
    await montar(tester, c, (_) => PainelTexto3D(layerId: cena));
    await tocarNaAba(tester, 'texto3d', 4);

    BancoDoPainel.relogio!.seek(const Duration(seconds: 1));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('kf-texto3d-ajuste-escala')));
    await tester.pumpAndSettle();

    final aj = cenaDe(c, cena).scene.nodeById(no)!.texto3d!.ajustes.single;
    final trilha = aj.trilha(MedidaDoCaractere.escala);
    expect(trilha.keyframes, hasLength(1));
    expect(trilha.keyframes.single.time, const Duration(seconds: 1));
    expect(aj.todas, isTrue, reason: 'sem escolha, e a faixa aberta');
    expect(tester.takeException(), isNull);
  });
}
