// A PILHA DE EFEITOS EM ACORDEAO: com todos os cartoes abertos, o
// terceiro efeito ficava a sete arrastos do topo. So o recem-aplicado (ou
// o ultimo tocado) fica aberto; os outros recolhem ao cabecalho de 37.
import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/efeitos.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

import '../paineis/apoio_paineis.dart';

void main() {
  testWidgets('aplicar 3 efeitos: so o ultimo fica aberto; tocar o '
      'cabecalho de outro abre ele e fecha o anterior', (tester) async {
    final (b, id) = await montarPainel(
      tester,
      preparar: (c) {
        c.addShapeLayer(Duration.zero, name: 'Forma');
        return c.projetoCompleto.layers.single.id;
      },
      painel: (id) => PainelEfeitos(layerId: id),
    );
    final tipos = effectsInCategory('Color').take(3).toList();
    expect(tipos, hasLength(3));

    int abertos() => [
      for (final e in b.camada(id).effects)
        if (find.byKey(ValueKey('efeito-${e.id}-corpo')).evaluate().isNotEmpty)
          e.id,
    ].length;

    for (var i = 0; i < 3; i++) {
      b.c.addEffect(id, tipos[i]);
      await tester.pumpAndSettle();
      final ultimo = b.camada(id).effects.last.id;
      // O RECEM-APLICADO abre; os de antes recolhem.
      expect(
        find.byKey(ValueKey('efeito-$ultimo-corpo')),
        findsOneWidget,
        reason: 'efeito ${i + 1} nao abriu',
      );
      expect(abertos(), 1, reason: 'com ${i + 1} efeito(s)');
      expect(
        b.container.read(propriedadeAtivaProvider),
        PropriedadeAtiva.efeito(ultimo),
      );
    }

    // Os recolhidos sao so o cabecalho de 37.
    final efeitos = b.camada(id).effects;
    for (final e in efeitos.take(2)) {
      expect(
        tester.getSize(find.byKey(ValueKey('efeito-${e.id}-cabecalho'))).height,
        AureaDims.cabecalhoDoCartao,
      );
    }
    // O terceiro, aberto, esta a vista sem rolar: o cabecalho dele cabe
    // no painel de 326.
    final painel = tester.getRect(find.byType(PainelEfeitos));
    final terceiro = tester.getRect(
      find.byKey(ValueKey('efeito-${efeitos.last.id}-cabecalho')),
    );
    expect(terceiro.bottom, lessThanOrEqualTo(painel.bottom));

    // TOCAR O CABECALHO DO PRIMEIRO: ele abre e o terceiro fecha.
    final primeiro = efeitos.first.id;
    await tester.tap(find.byKey(ValueKey('efeito-$primeiro-seta')));
    await tester.pumpAndSettle();
    expect(find.byKey(ValueKey('efeito-$primeiro-corpo')), findsOneWidget);
    expect(
      find.byKey(ValueKey('efeito-${efeitos.last.id}-corpo')),
      findsNothing,
    );
    expect(abertos(), 1);
    expect(
      b.container.read(propriedadeAtivaProvider),
      PropriedadeAtiva.efeito(primeiro),
    );

    // Tocar de novo o aberto recolhe: todos fechados.
    await tester.tap(find.byKey(ValueKey('efeito-$primeiro-seta')));
    await tester.pumpAndSettle();
    expect(abertos(), 0);
    expect(tester.takeException(), isNull);
  });
}
