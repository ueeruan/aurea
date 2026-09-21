import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../domain/layer.dart';
import '../../am/layer_menu.dart' show showShapeParamsSheet;
import '../shell/contrato.dart';
import 'comum.dart';
import 'pontos.dart';

/// FORMA — tamanho, cantos, lados e traco da forma (folha que ja existe)
/// e a porta do editor de pontos. Com este painel aberto o palco mostra as
/// alcas da forma viva (a casca espelha o painel na sessao).
class PainelForma extends ConsumerWidget {
  const PainelForma({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: 'Forma');
    return PainelDePortas(
      titulo: 'Forma',
      chave: 'painel-${PainelId.forma.name}',
      aviso: camada is ShapeLayer
          ? 'Arraste as alças da forma no palco, ou ajuste os números.'
          : 'Esta camada não é uma forma.',
      portas: [
        if (camada is ShapeLayer) ...[
          LinhaDePorta(
            rotulo: 'Tamanho, cantos e traço',
            icone: CupertinoIcons.slider_horizontal_below_rectangle,
            aoTocar: () {
              escopo.playback.pause();
              showShapeParamsSheet(context, ref, layerId, escopo.playback);
            },
          ),
          LinhaDePorta(
            rotulo: 'Editar pontos',
            icone: CupertinoIcons.scribble,
            aoTocar: () => abrirEditarPontosDaForma(
              context,
              ref,
              escopo.playback,
              layerId,
            ),
          ),
        ],
      ],
    );
  }
}
