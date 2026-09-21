import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/audio.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_paineis.dart';

void main() {
  group('painel Audio', () {
    testWidgets('volume com keyframe: o losango crava a marca, o arrasto na '
        'marca muda o valor dela (um desfazer), fora dela nao grava', (
      tester,
    ) async {
      final (b, id) = await montarPainel(
        tester,
        preparar: (c) {
          final a = audioDeTeste();
          abrirProjetoCom(c, [a]);
          return a.id;
        },
        painel: (id) => PainelAudio(layerId: id),
      );
      AudioSpec som() => (b.camada(id) as AudioLayer).audio;
      expect(find.byKey(const ValueKey('prop-volume')), findsOneWidget);
      expect(som().volumeAnimado, isNull);

      // O LOSANGO: marca no cabecote (0) com o valor de agora (100%).
      await tester.tap(find.byKey(const ValueKey('kf-volume')));
      await tester.pumpAndSettle();
      expect(som().volumeAnimado, isNotNull);
      expect(som().volumeAnimado!.hasKeyframeAt(Duration.zero), isTrue);
      expect(som().volumeEm(Duration.zero), closeTo(1, 1e-9));

      // ARRASTAR NA MARCA para a esquerda baixa o valor dela.
      await arrastarEmPassos(
        tester,
        find.byKey(const ValueKey('prop-volume')),
        cada: const Offset(-8, 0),
      );
      final baixo = som().volumeEm(Duration.zero);
      expect(baixo, lessThan(1));
      expect(som().volumeAnimado!.keyframes, hasLength(1));
      // UM desfazer devolve os 100% (a marca fica: ela veio antes).
      b.c.undo();
      expect(som().volumeEm(Duration.zero), closeTo(1, 1e-9));
      expect(som().volumeAnimado!.hasKeyframeAt(Duration.zero), isTrue);
      b.c.redo();
      await tester.pumpAndSettle();

      // FORA DA MARCA a linha avisa e nao grava.
      final trilha = som().volumeAnimado!;
      // O cabecote vai a 2 s pelo relogio da casca.
      final playback = tester
          .element(find.byKey(const ValueKey('prop-volume')))
          .findAncestorWidgetOfExactType<EscopoDoEditor>()!
          .playback;
      playback.seek(const Duration(seconds: 2));
      await tester.pumpAndSettle();
      expect(
        find.text('O volume tem keyframes: toque no losango para marcar este '
            'instante.'),
        findsOneWidget,
      );
      await arrastarEmPassos(
        tester,
        find.byKey(const ValueKey('prop-volume')),
        cada: const Offset(-8, 0),
      );
      expect(identical(som().volumeAnimado, trilha), isTrue);
      expect(tester.takeException(), isNull);
    });

    testWidgets('mudo, fade e efeito de audio pelo DS', (tester) async {
      final (b, id) = await montarPainel(
        tester,
        preparar: (c) {
          final a = audioDeTeste();
          abrirProjetoCom(c, [a]);
          return a.id;
        },
        painel: (id) => PainelAudio(layerId: id),
      );
      AudioSpec som() => (b.camada(id) as AudioLayer).audio;
      await tester.tap(
        find.descendant(
          of: find.byKey(const ValueKey('prop-mudo')),
          matching: find.byType(CupertinoSwitch),
        ),
      );
      await tester.pumpAndSettle();
      expect(som().muted, isTrue);

      await arrastarEmPassos(
        tester,
        find.byKey(const ValueKey('prop-fade-de-entrada')),
      );
      expect(som().fadeIn, greaterThan(Duration.zero));

      await tester.scrollUntilVisible(
        find.byKey(const ValueKey('adicionar-efeito-audio')),
        60,
        scrollable: find
            .descendant(
              of: find.byKey(const ValueKey('painel-audio-corpo')),
              matching: find.byType(Scrollable),
            )
            .first,
      );
      await tester.ensureVisible(
        find.byKey(const ValueKey('adicionar-efeito-audio')),
      );
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('adicionar-efeito-audio')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('menu-reverb')));
      await tester.pumpAndSettle();
      expect(som().processing.effects, hasLength(1));
      expect(tester.takeException(), isNull);
    });
  });
}
