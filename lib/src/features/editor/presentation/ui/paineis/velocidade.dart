import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../domain/layer.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'tempo.dart' show SecaoDaVelocidade;

/// VELOCIDADE (audio) — o clipe de som mais rapido ou mais lento: a MESMA
/// secao do painel Tempo ([SecaoDaVelocidade]), sem o que so o video tem.
class PainelVelocidade extends ConsumerWidget {
  const PainelVelocidade({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Velocidade';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final temVelocidade = camada is AudioLayer || camada is VideoLayer;
    return AureaPanel(
      titulo: _titulo,
      chave: 'painel-${PainelId.velocidade.name}',
      aoFechar: escopo.fecharPainel,
      filhos: [
        if (temVelocidade)
          SecaoDaVelocidade(layerId: layerId)
        else
          const AureaAvisoDoPainel(texto: 'Esta camada não tem velocidade.'),
      ],
    );
  }
}
