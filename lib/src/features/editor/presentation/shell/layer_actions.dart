import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import '../am/am_colors.dart';
import '../am/am_timeline.dart' show magneticProvider;
import '../am/apple_cascade_sheet.dart';
import '../am/layer_look.dart';
import '../am/scene3d_studio_ux.dart' show pedirNome;
import 'package:aurea/src/core/l10n/app_language.dart';

/// AS ACOES ESTRUTURAIS SOBRE CAMADAS, num lugar so.
///
/// O cabecalho da selecao, a linha de acoes rapidas e a timeline
/// chamam estas funcoes: dois botoes, uma regra so (magnetico, limpar a
/// selecao multipla).

/// Exclui as camadas de [targets], respeitando o magnetico.
///
/// SEM AVISO DE "EXCLUIDA". Os testadores do beta 1.0.5 pediram para tirar:
/// o aviso cobria a timeline logo depois de cada exclusao, e a camada
/// sumindo ja diz o que aconteceu. O Desfazer continua na barra de
/// reproducao.
void excluirCamadas(BuildContext context, WidgetRef ref, Set<String> targets) {
  if (targets.isEmpty) return;
  final controller = ref.read(editorControllerProvider.notifier);
  // MAGNETICO: excluir FECHA o buraco e puxa o que vinha depois.
  final magnetico = ref.read(magneticProvider);
  if (magnetico) {
    for (final id in targets) {
      controller.rippleDeleteLayer(id);
    }
    ref.read(multiSelectProvider.notifier).state = const {};
  } else {
    controller.removeLayers(targets);
  }
}

/// Agrupa a selecao e sai da selecao multipla.
void agruparSelecao(WidgetRef ref, Set<String> targets) {
  ref.read(editorControllerProvider.notifier).groupLayers(targets.toList());
  ref.read(multiSelectProvider.notifier).state = const {};
}

/// Escalonamento Apple da selecao multipla.
void abrirCascata(
  BuildContext context,
  WidgetRef ref,
  Set<String> targets,
  Duration time,
) {
  final controller = ref.read(editorControllerProvider.notifier);
  showAppleCascadeSheet(
    context,
    selectionCount: targets.length,
    onApply: (interval, order, ease) {
      controller.cascadeSelection(
        targets,
        interval: interval,
        order: order,
        ease: ease,
      );
      AureaSnack.show(
        context,
        'Cascata aplicada',
        actionLabel: 'Desfazer',
        onAction: controller.undo,
      );
    },
    onLinkProperty: (interval, order, ease, property) {
      controller.linkCascadeSelection(
        targets,
        time,
        interval: interval,
        order: order,
        ease: ease,
        property: property,
      );
      AureaSnack.show(
        context,
        'Vinculo em cascata aplicado',
        actionLabel: 'Desfazer',
        onAction: controller.undo,
      );
    },
  );
}

/// VINCULAR A SELECAO INTEIRA a um objeto.
Future<void> vincularSelecao(
  BuildContext context,
  WidgetRef ref,
  Set<String> targets,
  Duration t,
) async {
  final project = ref.read(editorControllerProvider);
  final candidatos = [
    for (final l in project.layers)
      if (!targets.contains(l.id)) l,
  ];
  if (candidatos.isEmpty) {
    AureaSnack.show(context, 'Nao ha outra camada para seguir');
    return;
  }
  final controller = ref.read(editorControllerProvider.notifier);
  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: AmColors.panel,
    builder: (sheetContext) => SafeArea(
      child: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(18, 14, 18, 6),
              child: AppText(
                '${targets.length} camadas seguirem...',
                style: const TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text,
                ),
              ),
            ),
            for (final other in candidatos)
              Material(
                color: Colors.transparent,
                child: ListTile(
                  leading: Icon(
                    layerTypeIcon(other),
                    size: 20,
                    color: layerTypeStripe(other),
                  ),
                  title: AppText(
                    other.name,
                    style: const TextStyle(color: AmColors.text),
                  ),
                  onTap: () {
                    for (final id in targets) {
                      controller.linkProperty(
                        id,
                        LayerProp.parent,
                        other.id,
                        t,
                      );
                    }
                    Navigator.of(sheetContext).pop();
                    ref.read(multiSelectProvider.notifier).state = const {};
                    AureaSnack.show(
                      context,
                      '${targets.length} camadas seguindo ${other.name}',
                      actionLabel: 'Desfazer',
                      onAction: controller.undo,
                    );
                  },
                ),
              ),
            const SizedBox(height: 8),
          ],
        ),
      ),
    ),
  );
}

/// O QUE AS MARCAS DESTRAVAM: ir para a proxima, cortar em todas,
/// distribuir as camadas nelas, limpar.
Future<void> menuDasMarcas(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  final project = ref.read(editorControllerProvider);
  final quantas = project.markers.length;

  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: AmColors.panel,
    builder: (sheetContext) => SafeArea(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 14, 16, 6),
            child: Row(
              children: [
                AppText(
                  '$quantas marca${quantas == 1 ? '' : 's'}',
                  style: const TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const Spacer(),
                if (project.bpm != null)
                  AppText(
                    '${project.bpm!.toStringAsFixed(0)} bpm',
                    style: const TextStyle(fontSize: 12, color: AmColors.muted),
                  ),
              ],
            ),
          ),
          ListTile(
            leading: const Icon(
              CupertinoIcons.bookmark,
              size: 19,
              color: AmColors.text,
            ),
            title: const AppText('Marcar aqui',
              style: TextStyle(color: AmColors.text, fontSize: 15),
            ),
            onTap: () {
              controller.toggleMarker(playback.time.value);
              Navigator.of(sheetContext).pop();
            },
          ),
          ListTile(
            leading: const Icon(
              CupertinoIcons.chevron_right_2,
              size: 19,
              color: AmColors.text,
            ),
            title: const AppText('Ir para a proxima marca',
              style: TextStyle(color: AmColors.text, fontSize: 15),
            ),
            enabled: quantas > 0,
            onTap: () {
              final t = playback.time.value;
              final proximo =
                  controller.markerAfter(t) ??
                  (project.markers.isEmpty ? null : project.markers.first.time);
              if (proximo != null) playback.seek(proximo);
              Navigator.of(sheetContext).pop();
            },
          ),
          ListTile(
            leading: const Icon(
              CupertinoIcons.scissors,
              size: 19,
              color: AmColors.text,
            ),
            title: const AppText('Cortar em todas as marcas',
              style: TextStyle(color: AmColors.text, fontSize: 15),
            ),
            enabled: quantas > 0,
            onTap: () {
              final n = controller.cutAtMarkers();
              Navigator.of(sheetContext).pop();
              AureaSnack.show(context, '$n corte${n == 1 ? '' : 's'}');
            },
          ),
          ListTile(
            leading: const Icon(
              CupertinoIcons.square_grid_2x2,
              size: 19,
              color: AmColors.text,
            ),
            title: const AppText('Distribuir as camadas nas marcas',
              style: TextStyle(color: AmColors.text, fontSize: 15),
            ),
            subtitle: const AppText('Uma camada por marca, na ordem em que estao',
              style: TextStyle(color: AmColors.muted, fontSize: 11.5),
            ),
            enabled: quantas > 1,
            onTap: () {
              final n = controller.distributeAtMarkers();
              Navigator.of(sheetContext).pop();
              AureaSnack.show(context, '$n camadas distribuidas');
            },
          ),
          ListTile(
            leading: const Icon(
              CupertinoIcons.delete,
              size: 19,
              color: AmColors.pink,
            ),
            title: const AppText('Limpar as marcas',
              style: TextStyle(color: AmColors.pink, fontSize: 15),
            ),
            enabled: quantas > 0,
            onTap: () {
              controller.clearMarkers();
              Navigator.of(sheetContext).pop();
            },
          ),
          const SizedBox(height: 6),
        ],
      ),
    ),
  );
}

/// RENOMEAR UMA CAMADA (toque no nome, no cabecalho da selecao).
Future<void> renomearCamada(
  BuildContext context,
  WidgetRef ref,
  Layer layer,
) async {
  final nome = await pedirNome(
    context,
    titulo: 'Nome da camada',
    atual: layer.name,
  );
  if (nome == null) return;
  ref.read(editorControllerProvider.notifier).renameLayer(layer.id, nome);
}

/// RENOMEAR O PROJETO (toque no nome, na barra de cima).
Future<void> renomearProjeto(BuildContext context, WidgetRef ref) async {
  final atual = ref.read(editorControllerProvider).name;
  final nome = await pedirNome(
    context,
    titulo: 'Nome do projeto',
    atual: atual,
  );
  if (nome == null) return;
  ref.read(editorControllerProvider.notifier).renameProject(nome);
}
