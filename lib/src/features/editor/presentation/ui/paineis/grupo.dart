import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../am/precomp_sheet.dart' show showPrecompSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// GRUPO — entrar (o grupo abre como a composicao), desagrupar e o tempo
/// proprio do grupo (precomp). Eram os tres botoes da doca antiga.
class PainelGrupo extends ConsumerWidget {
  const PainelGrupo({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: 'Grupo');
    final c = ref.read(editorControllerProvider.notifier);
    return PainelDePortas(
      titulo: 'Grupo',
      chave: 'painel-${PainelId.grupo.name}',
      aviso: camada is GroupLayer ? null : 'Esta camada não é um grupo.',
      portas: [
        if (camada is GroupLayer) ...[
          LinhaDePorta(
            rotulo: 'Entrar no grupo',
            icone: CupertinoIcons.arrow_down_right_square,
            aoTocar: () {
              HapticFeedback.lightImpact();
              escopo.playback.pause();
              escopo.fecharPainel();
              c.enterGroup(layerId);
            },
          ),
          LinhaDePorta(
            rotulo: 'Desagrupar',
            icone: CupertinoIcons.square_split_2x2,
            aoTocar: () {
              HapticFeedback.lightImpact();
              escopo.playback.pause();
              escopo.fecharPainel();
              final avisos = c.ungroupLayer(layerId);
              if (avisos.isNotEmpty && context.mounted) {
                AureaSnack.show(context, avisos.join('\n'));
              }
            },
          ),
          LinhaDePorta(
            rotulo: 'Tempo do grupo',
            icone: CupertinoIcons.timer,
            aoTocar: () {
              escopo.playback.pause();
              showPrecompSheet(context, ref, layerId, escopo.playback);
            },
          ),
        ],
      ],
    );
  }
}
