import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../../domain/velocidade.dart';
import '../../am/freeze_sheet.dart' show showFreezeSheet;
import '../../am/speed_sheet.dart' show showSpeedSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// TEMPO (video) — velocidade constante e reverso aqui mesmo; rampas,
/// blur temporal, interpolacao e congelar pelas portas dos editores que ja
/// existem. O Time Remap e efeito (Efeitos -> Tempo), nao mora aqui.
class PainelTempo extends ConsumerWidget {
  const PainelTempo({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Tempo';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final c = ref.read(editorControllerProvider.notifier);
    final video = camada is VideoLayer ? camada : null;
    return AureaPanel(
      titulo: _titulo,
      chave: 'painel-${PainelId.tempo.name}',
      aoFechar: escopo.fecharPainel,
      filhos: [
        if (video != null) ...[
          AureaPropertyRow(
            rotulo: 'Velocidade',
            valor: video.speed,
            aoMudar: (v) => c.setClipSpeed(layerId, v),
            min: velocidadeMinima,
            max: velocidadeMaxima,
            unidade: '×',
            casas: 2,
            aoResetar: () => c.setClipSpeed(layerId, 1),
            aoComecarGesto: c.beginGesture,
            aoTerminarGesto: c.endGesture,
          ),
          AureaPropertyRow.personalizada(
            rotulo: 'Reverso',
            filho: AureaToggle(
              valor: video.reverse,
              aoMudar: (v) => c.setClipReverse(layerId, v),
            ),
          ),
        ] else
          const AureaAvisoDoPainel(texto: 'Esta camada não tem tempo próprio.'),
        LinhaDePorta(
          rotulo: 'Rampas, blur e interpolação',
          icone: CupertinoIcons.speedometer,
          aoTocar: () {
            escopo.playback.pause();
            showSpeedSheet(context, ref, layerId, playback: escopo.playback);
          },
        ),
        LinhaDePorta(
          rotulo: 'Congelar quadro',
          icone: CupertinoIcons.snow,
          aoTocar: () {
            escopo.playback.pause();
            showFreezeSheet(context, ref, layerId, escopo.playback.time.value);
          },
        ),
      ],
    );
  }
}
