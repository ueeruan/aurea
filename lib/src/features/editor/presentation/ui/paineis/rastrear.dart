import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../am/rastreio_sheet.dart' show showRastreioSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// RASTREAR — a Cena 3D rastreada (camera do clipe, motor 2.0) e o
/// rastreio de blobs, na folha do rastreio que ja existe.
class PainelRastrear extends ConsumerWidget {
  const PainelRastrear({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    if (camadaVisivel(ref, layerId) == null) {
      return const PainelSemCamada(titulo: 'Rastrear');
    }
    return PainelDePortas(
      titulo: 'Rastrear',
      chave: 'painel-${PainelId.rastrear.name}',
      aviso: 'Rastreie a câmera do vídeo e povoe a cena com objetos 3D.',
      portas: [
        LinhaDePorta(
          rotulo: 'Rastrear câmera e blobs',
          icone: CupertinoIcons.viewfinder,
          aoTocar: () {
            escopo.playback.pause();
            showRastreioSheet(context, ref, layerId);
          },
        ),
      ],
    );
  }
}
