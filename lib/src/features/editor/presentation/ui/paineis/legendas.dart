import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../am/caption_style_sheet.dart' show showCaptionStyleSheet;
import '../../am/layer_menu.dart' show showCaptionCuesSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// LEGENDAS — o texto de cada fala e o estilo com destaque, nas folhas
/// que ja existem.
class PainelLegendas extends ConsumerWidget {
  const PainelLegendas({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    if (camadaVisivel(ref, layerId) == null) {
      return const PainelSemCamada(titulo: 'Legendas');
    }
    return PainelDePortas(
      titulo: 'Legendas',
      chave: 'painel-${PainelId.legendas.name}',
      portas: [
        LinhaDePorta(
          rotulo: 'Editar falas',
          icone: CupertinoIcons.captions_bubble,
          aoTocar: () {
            escopo.playback.pause();
            showCaptionCuesSheet(context, ref, layerId, escopo.playback);
          },
        ),
        LinhaDePorta(
          rotulo: 'Estilo da legenda',
          icone: CupertinoIcons.textformat,
          aoTocar: () => showCaptionStyleSheet(context, ref, layerId),
        ),
      ],
    );
  }
}
