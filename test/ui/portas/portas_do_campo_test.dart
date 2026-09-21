import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/ajuste_da_midia.dart';
import 'package:aurea/src/features/editor/domain/animadores.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/texto.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/transformar.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import '../paineis/apoio_paineis.dart';

// O MENU DO CAMPO (toque longo no valor, e o ⋯ do Transformar): expressao,
// "Animar sozinho" e expor no projeto — os recursos que penduravam no
// painel de transformacao antigo. E o encaixe da midia (Preencher /
// Ajustar) na aba Escala, e o vinculo de DADOS do painel Texto.

Future<void> _tocar(WidgetTester tester, String chave) async {
  final alvo = find.byKey(ValueKey(chave));
  await tester.ensureVisible(alvo);
  await tester.pumpAndSettle();
  await tester.tap(alvo);
  await tester.pumpAndSettle();
}

Future<(BancadaDoPainel, String)> _transformar(
  WidgetTester tester, {
  Layer Function()? camada,
}) => montarPainel(
  tester,
  altura: 500,
  preparar: (c) {
    if (camada != null) {
      final l = camada();
      abrirProjetoCom(c, [l]);
      return l.id;
    }
    c.addShapeLayer(Duration.zero, name: 'Forma');
    return c.projetoCompleto.layers.single.id;
  },
  painel: (id) => PainelTransformar(layerId: id),
);

void main() {
  testWidgets('toque longo no valor da opacidade: expressao, animar sozinho '
      'e expor no projeto', (tester) async {
    final (b, id) = await _transformar(tester);
    // A aba Opacidade.
    await _tocar(tester, 'painel-transformar-aba-3');

    // EXPRESSAO.
    await tester.longPress(find.byKey(const ValueKey('valor-opacidade')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('menu-campo-expressao')));
    await tester.pumpAndSettle();
    await tester.enterText(
      find.byKey(const ValueKey('expressao-campo')),
      '0.5',
    );
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(
      b.c.propExpression(b.camada(id), LayerProp.opacity),
      '0.5',
    );
    b.c.undo();
    expect(b.c.propExpression(b.camada(id), LayerProp.opacity), isNull);

    // ANIMAR SOZINHO: a folha liga o animador e o tipo escolhido fica.
    await tester.pumpAndSettle();
    await tester.longPress(find.byKey(const ValueKey('valor-opacidade')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('menu-campo-animar')));
    await tester.pumpAndSettle();
    await _tocar(tester, 'animador-tipo-triangulo');
    final a = b.c.propAnimador(b.camada(id), LayerProp.opacity);
    expect(a?.tipo, TipoDoAnimador.triangulo);
    // Tirar.
    await _tocar(tester, 'animador-tirar');
    expect(b.c.propAnimador(b.camada(id), LayerProp.opacity), isNull);

    // EXPOR NO PROJETO (e tirar de novo).
    await tester.longPress(find.byKey(const ValueKey('valor-opacidade')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('menu-campo-expor')));
    await tester.pumpAndSettle();
    final exposta = b.projeto.exposed.single;
    expect(exposta.layerId, id);
    expect(exposta.property, 'opacity');
    await tester.longPress(find.byKey(const ValueKey('valor-opacidade')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('menu-campo-expor')));
    await tester.pumpAndSettle();
    expect(b.projeto.exposed, isEmpty);
    // O aviso "exposta em ⚙ Propriedades" fecha sozinho.
    await tester.pump(const Duration(seconds: 5));
    expect(tester.takeException(), isNull);
  });

  testWidgets('o ⋯ do Transformar abre o menu da aba (posicao anima '
      'sozinha)', (tester) async {
    final (b, id) = await _transformar(tester);
    await _tocar(tester, 'transformar-campo');
    // Posicao nao tem expressao nem exposicao: so animar.
    expect(find.byKey(const ValueKey('menu-campo-expressao')), findsNothing);
    expect(find.byKey(const ValueKey('menu-campo-expor')), findsNothing);
    await tester.tap(find.byKey(const ValueKey('menu-campo-animar')));
    await tester.pumpAndSettle();
    await _tocar(tester, 'animador-tipo-seno');
    expect(
      b.c.propAnimador(b.camada(id), LayerProp.position)?.tipo,
      TipoDoAnimador.seno,
    );
    // O ponto tem forca X e Y.
    expect(find.byKey(const ValueKey('prop-animador-forca-y')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('Escala de um video: Preencher e Ajustar (setAjusteDaMidia)', (
    tester,
  ) async {
    final (b, id) = await _transformar(tester, camada: videoDeTeste);
    await _tocar(tester, 'painel-transformar-aba-1');
    await _tocar(tester, 'midia-preencher');
    expect((b.camada(id) as VideoLayer).ajuste, AjusteDaMidia.cobrir);
    await _tocar(tester, 'midia-ajustar');
    expect((b.camada(id) as VideoLayer).ajuste, AjusteDaMidia.conter);
    b.c.undo();
    expect((b.camada(id) as VideoLayer).ajuste, AjusteDaMidia.cobrir);
    expect(tester.takeException(), isNull);
  });

  testWidgets('forma nao tem encaixe de midia', (tester) async {
    await _transformar(tester);
    await _tocar(tester, 'painel-transformar-aba-1');
    expect(find.byKey(const ValueKey('prop-midia-encaixe')), findsNothing);
  });

  testWidgets('Texto › Dados: vincula a uma coluna do CSV e desvincula', (
    tester,
  ) async {
    final (b, id) = await montarPainel(
      tester,
      altura: 500,
      preparar: (c) {
        c.addTextLayer(Duration.zero);
        c.setDataSource(
          const DataSource(
            name: 'placar',
            columns: ['time', 'gols'],
            rows: [
              ['Azul', '3'],
            ],
          ),
        );
        return c.projetoCompleto.layers.single.id;
      },
      painel: (id) => PainelTexto(layerId: id),
    );
    await _tocar(tester, 'prop-texto-dados');
    // As opcoes: sem vinculo (0) e as colunas.
    await tester.tap(find.byKey(const ValueKey('menu-2')));
    await tester.pumpAndSettle();
    final vinculo = b.projeto.bindings.single;
    expect(vinculo.layerId, id);
    expect(vinculo.column, 'gols');
    expect((b.camada(id) as TextLayer).text, '3');
    // Um desfazer tira o vinculo e o texto que ele escreveu.
    b.c.undo();
    expect(b.projeto.bindings, isEmpty);
    await tester.pumpAndSettle();
    b.c.addDataBinding(DataBinding(layerId: id, column: 'time'));
    await tester.pumpAndSettle();
    await _tocar(tester, 'prop-texto-dados');
    await tester.tap(find.byKey(const ValueKey('menu-0')));
    await tester.pumpAndSettle();
    expect(b.projeto.bindings, isEmpty);
    expect(tester.takeException(), isNull);
  });

  test('o id da exposicao e o do editor antigo', () {
    expect(EditorController.propEhPonto(LayerProp.position), isTrue);
  });
}
