import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../../domain/velocidade.dart';
import '../../am/speed_sheet.dart' show showSpeedSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// VELOCIDADE (audio) — o clipe de som mais rapido ou mais lento.
class PainelVelocidade extends ConsumerWidget {
  const PainelVelocidade({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Velocidade';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final c = ref.read(editorControllerProvider.notifier);
    final atual = switch (camada) {
      AudioLayer l => l.speed,
      VideoLayer l => l.speed,
      _ => null,
    };
    return AureaPanel(
      titulo: _titulo,
      chave: 'painel-${PainelId.velocidade.name}',
      aoFechar: escopo.fecharPainel,
      filhos: [
        if (atual != null)
          AureaPropertyRow(
            rotulo: 'Velocidade',
            valor: atual,
            aoMudar: (v) => c.setClipSpeed(layerId, v),
            min: velocidadeMinima,
            max: velocidadeMaxima,
            unidade: '×',
            casas: 2,
            aoResetar: () => c.setClipSpeed(layerId, 1),
            aoComecarGesto: c.beginGesture,
            aoTerminarGesto: c.endGesture,
          )
        else
          const AureaAvisoDoPainel(texto: 'Esta camada não tem velocidade.'),
        LinhaDePorta(
          rotulo: 'Mais opções de velocidade',
          icone: CupertinoIcons.speedometer,
          aoTocar: () {
            escopo.playback.pause();
            showSpeedSheet(context, ref, layerId, playback: escopo.playback);
          },
        ),
      ],
    );
  }
}
