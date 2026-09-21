import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/tempo.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_paineis.dart';

void main() {
  group('painel Tempo', () {
    testWidgets('velocidade de um toque, reverso, e o atalho do Time Remap '
        'aplica o EFEITO e abre a pilha (sem tela propria)', (tester) async {
      final (b, id) = await montarPainel(
        tester,
        preparar: (c) {
          final v = videoDeTeste();
          abrirProjetoCom(c, [v]);
          return v.id;
        },
        painel: (id) => PainelTempo(layerId: id),
      );
      VideoLayer video() => b.camada(id) as VideoLayer;

      await tester.tap(find.byKey(const ValueKey('velocidade-2.0')));
      await tester.pumpAndSettle();
      expect(b.c.clipSpeedOf(id), closeTo(2, 1e-6));
      expect(video().duration, const Duration(seconds: 2));
      b.c.undo();
      expect(b.c.clipSpeedOf(id), closeTo(1, 1e-6));

      await tester.tap(
        find.descendant(
          of: find.byKey(const ValueKey('prop-tempo-reverso')),
          matching: find.byType(CupertinoSwitch),
        ),
      );
      await tester.pumpAndSettle();
      expect(video().reverse, isTrue);

      final remap = find.byKey(
        ValueKey('tempo-efeito-${effectSpecs[EffectType.timeRemap]!.id}'),
      );
      await tester.scrollUntilVisible(
        remap,
        60,
        scrollable: find
            .descendant(
              of: find.byKey(const ValueKey('painel-tempo-corpo')),
              matching: find.byType(Scrollable),
            )
            .first,
      );
      await tester.ensureVisible(remap);
      await tester.pumpAndSettle();
      await tester.tap(remap);
      await tester.pumpAndSettle();
      expect(video().effects.map((e) => e.type), [EffectType.timeRemap]);
      expect(b.abertos, [PainelId.efeitos]);
      // O Time Warp e o Posterize Time estao no mesmo atalho.
      for (final t in [EffectType.rgbTimeWarp, EffectType.posterizeTime]) {
        expect(
          find.byKey(ValueKey('tempo-efeito-${effectSpecs[t]!.id}')),
          findsOneWidget,
        );
      }
      expect(tester.takeException(), isNull);
    });
  });
}
