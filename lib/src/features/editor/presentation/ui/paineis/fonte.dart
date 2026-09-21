import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../application/font_service.dart';
import '../../../domain/layer.dart';
import '../../am/font_sheet.dart' show showFontSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// FONTE — as familias instaladas, a atual marcada; um toque troca.
/// Importar fonte e buscar ficam na folha de fontes que ja existe.
///
/// O NOME DA FONTE e conteudo, nao rotulo: vai em `Text`, sem catalogo.
class PainelFonte extends ConsumerWidget {
  const PainelFonte({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Fonte';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.fonte.name}';
    if (camada is! TextLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é de texto.',
        portas: const [],
      );
    }
    final c = ref.read(editorControllerProvider.notifier);
    final familias = FontService.instance.families;
    final atual = camada.fontFamily;
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      aoFechar: escopo.fecharPainel,
      filhos: [
        LinhaDePorta(
          rotulo: 'Importar ou buscar fonte',
          icone: CupertinoIcons.search,
          aoTocar: () => showFontSheet(context, ref, layerId),
        ),
        for (final f in familias)
          AureaLayerRow(
            key: ValueKey('fonte-$f'),
            nome: f,
            icone: CupertinoIcons.textformat,
            selecionada: f == atual,
            aoTocar: () => c.editTextLayer(layerId, fontFamily: f),
          ),
      ],
    );
  }
}
