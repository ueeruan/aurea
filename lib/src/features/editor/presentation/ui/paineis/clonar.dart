import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../am/layer_menu.dart' show showGridSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// CLONAR — a grade de clones do nulo (retangular, radial, esferica,
/// caminho), na folha que ja existe.
class PainelClonar extends ConsumerWidget {
  const PainelClonar({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    if (camadaVisivel(ref, layerId) == null) {
      return const PainelSemCamada(titulo: 'Clonar');
    }
    return PainelDePortas(
      titulo: 'Clonar',
      chave: 'painel-${PainelId.clonar.name}',
      portas: [
        LinhaDePorta(
          rotulo: 'Grade de clones',
          icone: CupertinoIcons.circle_grid_3x3,
          aoTocar: () {
            escopo.playback.pause();
            showGridSheet(context, ref, layerId, escopo.playback);
          },
        ),
      ],
    );
  }
}
