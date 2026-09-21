import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/perfil3d.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_editor.dart';
import 'apoio_paineis.dart';

/// Um efeito de cor com numero de faixa (o trilho tem posicao).
final _tipos = effectsInCategory('Color').take(1).toList();

void main() {
  group('painel no editor inteiro', () {
    testWidgets('um slider de parametro arrastado 12 passos = 1 desfazer, e '
        'nao reconstroi editor, casca nem timeline', (tester) async {
      final tipo = _tipos.first;
      final container = await abrirEditorInteiro(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'Forma');
          c.addEffect(c.projetoCompleto.layers.single.id, tipo);
        },
      );
      final ctrl = container.read(editorControllerProvider.notifier);
      final camada = container.read(editorControllerProvider).layers.single;
      final efeito = camada.effects.single;
      container.read(selectedLayerProvider.notifier).state = camada.id;
      await tester.pumpAndSettle();
      container.read(painelAbertoProvider.notifier).state = PainelId.efeitos;
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(ValueKey('efeito-${efeito.id}-seta')));
      await tester.pumpAndSettle();

      // O primeiro numero COM FAIXA da ficha (o trilho tem posicao).
      final chave = efeito.spec.params.entries
          .firstWhere(
            (e) =>
                e.value.kind == ParamKind.number &&
                e.value.max > e.value.min &&
                e.value.initial < e.value.max,
          )
          .key;
      final linha = find.byKey(ValueKey('prop-${efeito.id}-$chave'));
      expect(linha, findsOneWidget);
      await tester.ensureVisible(linha);
      await tester.pumpAndSettle();
      double valor() => container
          .read(editorControllerProvider)
          .layers
          .single
          .effects
          .single
          .paramAt(chave, Duration.zero);
      final antes = valor();

      Perfil3D.zerar();
      Perfil3D.ligado = true;
      try {
        await arrastarEmPassos(tester, linha);
      } finally {
        Perfil3D.ligado = false;
      }
      final r = Perfil3D.relatorio();
      expect(valor(), greaterThan(antes), reason: 'direita aumenta');
      expect(r.contas['build.editor'] ?? 0, 0);
      expect(r.contas['build.casca'] ?? 0, 0);
      expect(r.contas['build.timeline'] ?? 0, 0);
      ctrl.undo();
      expect(valor(), antes, reason: 'um desfazer volta o arrasto inteiro');
      expect(tester.takeException(), isNull);
    });
  });
}
