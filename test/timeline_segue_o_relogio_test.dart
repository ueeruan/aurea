import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:flutter_test/flutter_test.dart';

import 'ui/timeline/montagem.dart';

/// A TIMELINE SEGUE O RELOGIO — inclusive depois de um arrasto que
/// terminou de um jeito torto.
///
/// O cabecote e desenhado FIXO no centro: quem se move e o conteudo.
/// Enquanto se arrasta um clipe a timeline para de seguir o relogio, de
/// proposito, para nao brigar com o dedo (`segurarVista`). So que os
/// tratadores de FIM do arrasto so existem enquanto a camada esta
/// escolhida — desselecionar no meio do arrasto (ou o painel trocar)
/// fazia o fim nunca chegar, e a timeline ficava presa.
///
/// Presa assim, o keyframe nasce no tempo certo e APARECE longe do
/// cabecote. Foi o primeiro bug que os testadores acharam: "olha onde eu
/// coloquei keyframe e olha onde ele aparece".
///
/// O conteudo andando com o relogio e a virtualizacao das linhas estao em
/// test/ui/timeline/timeline_test.dart.
void main() {
  testWidgets(
    'desselecionar no meio do arrasto nao trava a timeline no tempo',
    (tester) async {
      final b = await montarTimeline(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero),
      );
      final id = b.projeto.layers.single.id;
      await tester.tapAt(pontoNoTempo(tester, b, id, 0.5));
      await tester.pumpAndSettle();
      expect(b.container.read(selectedLayerProvider), id);

      // Comeca a arrastar o CLIPE escolhido.
      final gesto = await tester.startGesture(pontoNoTempo(tester, b, id, 0.5));
      for (var i = 0; i < 3; i++) {
        await gesto.moveBy(const Offset(20, 0));
        await tester.pump(const Duration(milliseconds: 16));
      }
      // Sem um arrasto RECONHECIDO este teste nao prova nada: com o clipe
      // na mao a timeline para de seguir o relogio de proposito.
      expect(
        b.estado(tester).vistaPresa,
        isTrue,
        reason: 'o gesto nao pegou o clipe',
      );

      // O painel troca / a selecao cai NO MEIO do arrasto: os tratadores
      // de fim deixam de existir.
      b.container.read(selectedLayerProvider.notifier).state = null;
      await tester.pump();
      await gesto.up();
      await tester.pumpAndSettle();

      // A prova: a timeline voltou a seguir o relogio.
      b.playback.seek(const Duration(seconds: 2));
      await tester.pumpAndSettle();
      expect(
        b.estado(tester).vistaUs.value,
        2e6,
        reason: 'a timeline ficou presa em "arrastando clipe"',
      );
    },
  );
}
