import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../application/ui/editor_session.dart';
import '../../am/aviso_de_bloqueio.dart';
import '../../am/layer_menu.dart' show showReasonToast;
import '../../am/points_panel.dart' show PointsPanel;
import '../../widgets/mask_node_editor.dart';
import '../shell/contrato.dart';
import 'comum.dart';

// EDITAR PONTOS — os nos do caminho da FORMA ou da MASCARA.
//
// A logica veio intacta do `EditorScreen` antigo (`_abrirEditPoints`,
// `_abrirMaskEditPoints`, `_fecharEditPoints`): o palco desenha os nos
// quando `pathEditTargetProvider` aponta um caminho E a sessao diz que o
// painel de pontos esta aberto (`editorDeNosAtivo`) — por isso abrir e
// fechar aqui tambem espelham a sessao.

/// ABRE O EDITOR DE PONTOS DA FORMA [layerId]: a geometria vira caminho
/// (se ainda nao e), o palco passa a mirar nela e o painel abre. Devolve
/// se abriu.
bool abrirEditarPontosDaForma(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback,
  String layerId,
) {
  final controller = ref.read(editorControllerProvider.notifier);
  // BLOQUEADA NAO ABRE, e diz por que: o cadeado recusa criar o contorno,
  // e o erro generico ("sem caminho editavel") mentiria.
  if (controller.isLocked(layerId)) {
    avisarCamadaBloqueada(
      context,
      ref,
      fraseDeBloqueio('editar os pontos'),
      layerId,
    );
    return false;
  }
  final itemId = controller.ensureShapeBezierGeometry(
    layerId,
    playback.time.value,
  );
  if (itemId == null) {
    showReasonToast(context, 'Esta camada nao tem caminho editavel');
    return false;
  }
  playback.pause();
  ref.read(selectedLayerProvider.notifier).state = layerId;
  _mirar(ref, PathEditTarget(layerId, itemId, forma: true));
  ref
      .read(editorSessionProvider.notifier)
      .openEditPoints(itemId, returnTo: EditorPanel.editShape);
  ref.read(painelAbertoProvider.notifier).state = PainelId.pontos;
  return true;
}

/// ABRE O EDITOR DE PONTOS DA MASCARA [maskId] da camada selecionada.
bool abrirEditarPontosDaMascara(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback,
  String maskId,
) {
  final id = ref.read(selectedLayerProvider);
  if (id == null) return false;
  final layer = ref.read(editorControllerProvider).layerById(id);
  if (layer == null || !layer.masks.any((m) => m.id == maskId)) {
    showReasonToast(context, 'Esta mascara nao existe mais');
    return false;
  }
  playback.pause();
  _mirar(ref, PathEditTarget(id, maskId, forma: false));
  ref
      .read(editorSessionProvider.notifier)
      .openEditPoints(maskId, returnTo: EditorPanel.blending);
  ref.read(painelAbertoProvider.notifier).state = PainelId.pontos;
  return true;
}

void _mirar(WidgetRef ref, PathEditTarget alvo) {
  ref.read(pathEditTargetProvider.notifier).state = alvo;
  ref.read(pathEditSelectedProvider.notifier).state = null;
  ref.read(pathEditCursorProvider.notifier).state = null;
  ref.read(pathEditModeProvider.notifier).state = PointsMode.move;
}

/// Solta o alvo do editor de nos (o palco para de desenhar os pontos).
void fecharEditarPontos(WidgetRef ref) {
  ref.read(pathEditTargetProvider.notifier).state = null;
  ref.read(pathEditSelectedProvider.notifier).state = null;
  ref.read(pathEditCursorProvider.notifier).state = null;
}

/// O PAINEL: o trackpad de pontos que ja existe, dentro da casca nova.
class PainelPontos extends ConsumerWidget {
  const PainelPontos({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final alvo = ref.watch(pathEditTargetProvider);
    if (alvo == null || alvo.layerId != layerId) {
      return PainelDePortas(
        titulo: 'Editar pontos',
        chave: 'painel-${PainelId.pontos.name}',
        aviso: 'Escolha uma forma ou máscara para editar os pontos.',
        portas: [
          LinhaDePorta(
            rotulo: 'Editar pontos desta forma',
            aoTocar: () => abrirEditarPontosDaForma(
              context,
              ref,
              escopo.playback,
              layerId,
            ),
          ),
        ],
      );
    }
    return KeyedSubtree(
      key: ValueKey('painel-${PainelId.pontos.name}'),
      child: PointsPanel(
        playback: escopo.playback,
        layerId: layerId,
        itemId: alvo.maskId,
        onBack: () {
          fecharEditarPontos(ref);
          escopo.fecharPainel();
        },
      ),
    );
  }
}
