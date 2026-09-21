import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../am/layer_menu.dart' show showParticulasSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// PARTICULAS — emissor, forma, cor e movimento, na folha que ja existe.
class PainelParticulas extends ConsumerWidget {
  const PainelParticulas({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    if (camadaVisivel(ref, layerId) == null) {
      return const PainelSemCamada(titulo: 'Partículas');
    }
    return PainelDePortas(
      titulo: 'Partículas',
      chave: 'painel-${PainelId.particulas.name}',
      portas: [
        LinhaDePorta(
          rotulo: 'Ajustar partículas',
          icone: CupertinoIcons.sparkles,
          aoTocar: () => showParticulasSheet(context, ref, layerId),
        ),
      ],
    );
  }
}
