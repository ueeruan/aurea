import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../am/borda_e_sombra_sheet.dart' show showBordaESombraSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// BORDA E SOMBRA — contorno, sombra projetada, brilho e os estilos
/// prontos, na folha que ja existe.
class PainelBordaSombra extends ConsumerWidget {
  const PainelBordaSombra({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    if (camadaVisivel(ref, layerId) == null) {
      return const PainelSemCamada(titulo: 'Borda e sombra');
    }
    return PainelDePortas(
      titulo: 'Borda e sombra',
      chave: 'painel-${PainelId.bordaSombra.name}',
      portas: [
        LinhaDePorta(
          rotulo: 'Contorno, sombra e estilos',
          icone: CupertinoIcons.square_on_square,
          aoTocar: () =>
              showBordaESombraSheet(context, ref, layerId, escopo.playback),
        ),
      ],
    );
  }
}
