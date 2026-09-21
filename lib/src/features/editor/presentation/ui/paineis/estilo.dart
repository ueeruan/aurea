import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../am/borda_e_sombra_sheet.dart' show showBordaESombraSheet;
import '../shell/contrato.dart';
import 'comum.dart';
import 'texto.dart' show fileiraDeAlinhamento;

/// ESTILO (texto) — negrito e alinhamento aqui; contorno, sombra e brilho
/// pela folha de borda e sombra que ja existe.
class PainelEstilo extends ConsumerWidget {
  const PainelEstilo({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Estilo';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final c = ref.read(editorControllerProvider.notifier);
    final texto = camada is TextLayer ? camada : null;
    return AureaPanel(
      titulo: _titulo,
      chave: 'painel-${PainelId.estilo.name}',
      aoFechar: escopo.fecharPainel,
      filhos: [
        if (texto != null) ...[
          AureaPropertyRow.personalizada(
            rotulo: 'Negrito',
            filho: AureaToggle(
              valor: texto.bold,
              aoMudar: (v) => c.editTextLayer(layerId, bold: v),
            ),
          ),
          AureaPropertyRow.personalizada(
            rotulo: 'Alinhamento',
            filho: fileiraDeAlinhamento(
              texto.alinhamento,
              (a) => c.editTextLayer(layerId, alinhamento: a),
            ),
          ),
        ],
        LinhaDePorta(
          rotulo: 'Contorno, sombra e brilho',
          icone: CupertinoIcons.square_on_square,
          aoTocar: () =>
              showBordaESombraSheet(context, ref, layerId, escopo.playback),
        ),
      ],
    );
  }
}
