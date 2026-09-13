import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/tokens.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import '../am/align_sheet.dart';
import '../am/layer_look.dart';
import '../shell/layer_actions.dart';

/// E2 — O CABECALHO DA CAMADA SELECIONADA.
///
/// Icone do tipo na cor do tipo · nome (toque = renomear) · Duplicar ·
/// Excluir · Mais (todas as acoes com rotulo). Mesma posicao sempre.
class LayerHeader extends ConsumerWidget {
  const LayerHeader({super.key, required this.layer, required this.onMore});

  final Layer layer;
  final VoidCallback onMore;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final t = AureaTokens.of(context);
    final controller = ref.read(editorControllerProvider.notifier);
    Widget acao(Key key, IconData icon, String rotulo, VoidCallback onTap) =>
        Tooltip(
          message: rotulo,
          child: GestureDetector(
            key: key,
            behavior: HitTestBehavior.opaque,
            onTap: onTap,
            child: SizedBox(
              width: AureaTokens.minTap,
              height: AureaTokens.minTap,
              child: FittedBox(
                fit: BoxFit.scaleDown,
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Icon(icon, size: 18, color: t.text),
                    const SizedBox(height: 1),
                    AppText(
                      rotulo,
                      style: TextStyle(
                        fontSize: 9,
                        height: 1.1,
                        color: t.muted,
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        );
    return SizedBox(
      height: AureaTokens.minTap,
      child: Row(
        children: [
          const SizedBox(width: 12),
          Container(
            width: 26,
            height: 26,
            decoration: BoxDecoration(
              color: layerTypeColor(layer),
              borderRadius: BorderRadius.circular(7),
            ),
            child: Icon(layerTypeIcon(layer), size: 15, color: Colors.white),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: GestureDetector(
              key: const ValueKey('camada-nome'),
              behavior: HitTestBehavior.opaque,
              onTap: () => renomearCamada(context, ref, layer),
              child: AppText(
                layer.name.isEmpty ? 'Camada' : layer.name,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w600,
                  color: t.text,
                ),
              ),
            ),
          ),
          acao(
            const ValueKey('camada-duplicar'),
            CupertinoIcons.plus_square_on_square,
            'Duplicar',
            () => controller.duplicateLayer(layer.id),
          ),
          acao(
            const ValueKey('camada-excluir'),
            CupertinoIcons.trash,
            'Excluir',
            () => excluirCamadas(context, ref, {layer.id}),
          ),
          acao(
            const ValueKey('camada-mais'),
            CupertinoIcons.square_grid_2x2,
            'Mais',
            onMore,
          ),
          const SizedBox(width: 4),
        ],
      ),
    );
  }
}

/// E2 — SELECAO MULTIPLA: "N camadas" e o que se faz com um conjunto.
class MultiSelectionPanel extends ConsumerWidget {
  const MultiSelectionPanel({
    super.key,
    required this.targets,
    required this.playback,
  });

  final Set<String> targets;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final t = AureaTokens.of(context);
    final n = targets.length;
    Widget acao(
      Key key,
      IconData icon,
      String rotulo,
      VoidCallback onTap, {
      bool perigo = false,
    }) => GestureDetector(
      key: key,
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Container(
        width: 84,
        height: 64,
        decoration: BoxDecoration(
          color: t.chip,
          borderRadius: BorderRadius.circular(AureaTokens.radius),
        ),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(icon, size: 22, color: perigo ? t.danger : t.text),
            const SizedBox(height: 4),
            AppText(
              rotulo,
              style: TextStyle(
                fontSize: 11,
                fontWeight: FontWeight.w600,
                color: perigo ? t.danger : t.text,
              ),
            ),
          ],
        ),
      ),
    );
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        SizedBox(
          height: AureaTokens.minTap,
          child: Row(
            children: [
              const SizedBox(width: 12),
              Container(
                width: 26,
                height: 26,
                decoration: BoxDecoration(
                  color: t.selection,
                  borderRadius: BorderRadius.circular(7),
                ),
                child: const Icon(
                  CupertinoIcons.square_stack_3d_up,
                  size: 15,
                  color: Colors.white,
                ),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: AppText(
                  '$n camadas',
                  key: const ValueKey('selecao-contagem'),
                  style: TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.w600,
                    color: t.text,
                  ),
                ),
              ),
              Tooltip(
                message: 'Limpar selecao',
                child: GestureDetector(
                  key: const ValueKey('selecao-limpar'),
                  behavior: HitTestBehavior.opaque,
                  onTap: () =>
                      ref.read(multiSelectProvider.notifier).state = const {},
                  child: SizedBox(
                    width: AureaTokens.minTap,
                    height: AureaTokens.minTap,
                    child: Icon(CupertinoIcons.xmark, size: 20, color: t.text),
                  ),
                ),
              ),
              const SizedBox(width: 4),
            ],
          ),
        ),
        SizedBox(
          height: 72,
          child: ListView(
            scrollDirection: Axis.horizontal,
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
            children: [
              acao(
                const ValueKey('selecao-agrupar'),
                CupertinoIcons.folder_badge_plus,
                'Agrupar',
                () => agruparSelecao(ref, targets),
              ),
              const SizedBox(width: 8),
              acao(
                const ValueKey('selecao-vincular'),
                CupertinoIcons.link,
                'Vincular',
                () =>
                    vincularSelecao(context, ref, targets, playback.time.value),
              ),
              const SizedBox(width: 8),
              acao(
                const ValueKey('selecao-cascata'),
                Icons.format_line_spacing,
                'Cascata',
                () => abrirCascata(context, ref, targets, playback.time.value),
              ),
              const SizedBox(width: 8),
              acao(
                const ValueKey('selecao-alinhar'),
                CupertinoIcons.square_grid_3x2,
                'Alinhar',
                () => showAlignSheet(
                  context,
                  ref,
                  targets.toList(),
                  playback.time.value,
                ),
              ),
              const SizedBox(width: 8),
              acao(
                const ValueKey('selecao-dividir'),
                CupertinoIcons.scissors,
                'Dividir',
                () {
                  final c = ref.read(editorControllerProvider.notifier);
                  for (final id in targets) {
                    c.splitLayer(id, playback.time.value);
                  }
                },
              ),
              const SizedBox(width: 8),
              acao(
                const ValueKey('selecao-excluir'),
                CupertinoIcons.trash,
                'Excluir',
                () => excluirCamadas(context, ref, targets),
                perigo: true,
              ),
            ],
          ),
        ),
      ],
    );
  }
}
