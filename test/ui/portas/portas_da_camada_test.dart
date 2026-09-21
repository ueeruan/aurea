import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/menu_da_camada.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import '../paineis/apoio_paineis.dart';

// AS PORTAS DO MENU DA CAMADA que a UI nova tinha perdido: juntar o pedaco
// vizinho (o selo "Juntar" da barra antiga) e escolher a camada vizinha
// (as setas da barra compacta).

/// Um botao que abre o menu da camada escolhida — o "Mais" da barra.
class _AbreMenu extends ConsumerWidget {
  const _AbreMenu({required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) => Center(
    child: TextButton(
      key: const ValueKey('abrir-menu'),
      onPressed: () => mostrarMenuDaCamada(
        context,
        ref,
        layerId,
        playback: EscopoDoEditor.of(context).playback,
      ),
      child: const Text('menu'),
    ),
  );
}

/// Toca no item do menu aberto, rolando a lista do menu ate ele.
Future<void> _item(WidgetTester tester, String chave) async {
  final alvo = find.byKey(ValueKey('menu-camada-$chave'));
  await tester.scrollUntilVisible(
    alvo,
    60,
    scrollable: find.byType(Scrollable).last,
  );
  // Construido nao e visivel: a lista do menu constroi alem da borda.
  await tester.ensureVisible(alvo);
  await tester.pumpAndSettle();
  await tester.tap(alvo);
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('Juntar ao pedaço vizinho: aparece so com vizinho e e UM '
      'desfazer', (tester) async {
    late String inteiro;
    final (b, id) = await montarPainel(
      tester,
      preparar: (c) {
        final v = videoDeTeste();
        abrirProjetoCom(c, [v]);
        inteiro = v.id;
        c.splitLayer(v.id, const Duration(seconds: 2));
        return c.projetoCompleto.layers
            .firstWhere((l) => l.startTime == Duration.zero)
            .id;
      },
      painel: (id) => _AbreMenu(layerId: id),
    );
    expect(b.projeto.layers, hasLength(2));
    expect(b.c.hasJoinableNeighbour(id), isTrue);

    await tester.tap(find.byKey(const ValueKey('abrir-menu')));
    await tester.pumpAndSettle();
    await _item(tester, AcaoDaCamada.juntar);
    expect(b.projeto.layers, hasLength(1));
    expect(b.projeto.layers.single.duration, const Duration(seconds: 4));
    expect(inteiro, isNotEmpty);

    // UM desfazer devolve os dois pedacos.
    b.c.undo();
    await tester.pumpAndSettle();
    expect(b.projeto.layers, hasLength(2));

    // Sem vizinho encostado, o item nao aparece.
    b.c.moveLayer(
      b.projeto.layers.firstWhere((l) => l.id != id).id,
      const Duration(seconds: 3),
    );
    await tester.pumpAndSettle();
    expect(b.c.hasJoinableNeighbour(id), isFalse);
    await tester.tap(find.byKey(const ValueKey('abrir-menu')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('menu-camada-juntar')), findsNothing);
    // O aviso "Pedaços juntados" fecha sozinho.
    await tester.pump(const Duration(seconds: 5));
    expect(tester.takeException(), isNull);
  });

  testWidgets('Mais ações: selecionar a camada de cima e a de baixo', (
    tester,
  ) async {
    final (b, id) = await montarPainel(
      tester,
      preparar: (c) {
        c.addShapeLayer(Duration.zero, name: 'A');
        c.addShapeLayer(Duration.zero, name: 'B');
        c.addShapeLayer(Duration.zero, name: 'C');
        // A do meio.
        return c.projetoCompleto.layers[1].id;
      },
      painel: (id) => _AbreMenu(layerId: id),
    );
    final camadas = b.projeto.layers.map((l) => l.id).toList();
    expect(camadas[1], id);

    await tester.tap(find.byKey(const ValueKey('abrir-menu')));
    await tester.pumpAndSettle();
    await _item(tester, AcaoDaCamada.maisAcoes);
    await _item(tester, AcaoDaCamada.selecionarAbaixo);
    expect(b.container.read(selectedLayerProvider), camadas[2]);

    // Da de baixo, "selecionar a de cima" volta.
    b.container.read(selectedLayerProvider.notifier).state = camadas[1];
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('abrir-menu')));
    await tester.pumpAndSettle();
    await _item(tester, AcaoDaCamada.maisAcoes);
    await _item(tester, AcaoDaCamada.selecionarAcima);
    expect(b.container.read(selectedLayerProvider), camadas[0]);
    expect(tester.takeException(), isNull);
  });

  test('itens puros: juntar so com vizinho; vizinhas nas pontas apagadas', () {
    final a = ShapeLayer(
      name: 'a',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
    );
    final bb = ShapeLayer(
      name: 'b',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
    );
    final projeto = VideoProject.empty('p').copyWith(layers: [a, bb]);
    List<String> chaves(bool juntar) => [
      for (final i in itensDoMenuDaCamada(
        projeto,
        a,
        Duration.zero,
        temCamadaCopiada: false,
        temKeyframesCopiados: false,
        podeColarEstilo: false,
        podeJuntar: juntar,
      ))
        i.chave!,
    ];
    expect(chaves(false), isNot(contains('camada-juntar')));
    expect(chaves(true), contains('camada-juntar'));
    final mais = itensDeMaisAcoes(
      projeto,
      a,
      Duration.zero,
      temEfeitosCopiados: false,
      mudo: false,
      temBaseDeRecorte: false,
    );
    final acima = mais.firstWhere((i) => i.chave == 'camada-selecionar-acima');
    final abaixo = mais.firstWhere(
      (i) => i.chave == 'camada-selecionar-abaixo',
    );
    expect(acima.habilitado, isFalse, reason: 'a primeira nao tem de cima');
    expect(abaixo.habilitado, isTrue);
  });
}
