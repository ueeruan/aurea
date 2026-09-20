import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../../../core/utils/time_format.dart';
import '../../application/editor_controller.dart';
import '../../application/ui/editor_session.dart';
import '../../application/ui/pro_mode.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import '../am/am_colors.dart';
import '../am/am_timeline.dart' show magneticProvider;
import '../am/apple_cascade_sheet.dart';
import '../am/aviso_de_bloqueio.dart';
import '../am/beats_sheet.dart' show showBeatsSheet;
import '../am/layer_look.dart';
import 'package:aurea/src/core/l10n/app_language.dart';
import '../../../../core/ui/pedir_nome.dart';

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
  // A BLOQUEADA NAO VAI. Antes de qualquer coisa: quantas do alvo estao
  // travadas. Elas ficam, o resto some, e a tela diz quantas ficaram.
  final travadas = [
    for (final id in targets)
      if (controller.isLocked(id)) id,
  ];
  // MAGNETICO: excluir FECHA o buraco e puxa o que vinha depois.
  final magnetico = ref.read(magneticProvider);
  if (magnetico) {
    for (final id in targets) {
      if (controller.isLocked(id)) continue;
      controller.rippleDeleteLayer(id);
    }
    ref.read(multiSelectProvider.notifier).state = const {};
  } else {
    controller.removeLayers(targets);
  }
  if (travadas.isNotEmpty) {
    avisarCamadaBloqueada(
      context,
      ref,
      travadas.length == 1
          ? 'Camada bloqueada: desbloqueie para apagar'
          : '${travadas.length} camadas bloqueadas: desbloqueie para apagar',
      travadas.first,
    );
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
              child: AppTextMoldado(
                '{0} camadas seguirem...', [targets.length],
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
              controller.toggleMarker(playback.timeForInput());
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
          // --------------------------------------- Entrada e Saida (Pro)
          //
          // AS DUAS PONTAS DA EDICAO DE 3 PONTOS. Elas so existiam como
          // DESENHO: a regua ja pintava as marcas `I` e `O` quando a
          // sessao tinha os pontos, e as acoes rapidas da camada ja
          // ofereciam Levantar e Extrair — mas nao havia NENHUM jeito de
          // marcar os pontos. Os dois botoes diziam "Marque Entrada (I) e
          // Saida (O) na regua" e a regua nao tinha onde.
          //
          // ELAS MORAM AQUI, e nao na regua: o dono mandou tirar da
          // regua tudo o que a atravancava, e ha teste guardando isso.
          // Este e o menu das marcas — o lugar do assunto.
          if (ref.read(proModeProvider))
            for (final (chave, rotulo, dica, tempo, marcar) in [
              (
                'timeline-entrada',
                'Marcar Entrada (I)',
                'O comeco do trecho que Levantar e Extrair usam',
                ref.read(editorSessionProvider).inPoint,
                () => ref
                    .read(editorSessionProvider.notifier)
                    .setInPoint(playback.time.value),
              ),
              (
                'timeline-saida',
                'Marcar Saida (O)',
                'O fim do trecho que Levantar e Extrair usam',
                ref.read(editorSessionProvider).outPoint,
                () => ref
                    .read(editorSessionProvider.notifier)
                    .setOutPoint(playback.time.value),
              ),
            ])
              ListTile(
                key: ValueKey(chave),
                leading: Icon(
                  tempo == null
                      ? CupertinoIcons.arrow_right_to_line
                      : CupertinoIcons.checkmark_circle_fill,
                  size: 19,
                  color: tempo == null ? AmColors.text : AmColors.accent,
                ),
                title: AppText(
                  rotulo,
                  style: const TextStyle(color: AmColors.text, fontSize: 15),
                ),
                subtitle: AppText(
                  tempo == null
                      ? dica
                      : 'ja marcado em ${formatTimecode(tempo, 30)}',
                  style: const TextStyle(
                    color: AmColors.muted,
                    fontSize: 11.5,
                  ),
                ),
                onTap: () {
                  marcar();
                  Navigator.of(sheetContext).pop();
                },
              ),
          // -------------------------------------------------- batidas
          //
          // A GRADE DO RITMO mora aqui porque e daqui que se navega e se
          // corta por marca — e batida e a marca que a musica poe.
          ListTile(
            key: const ValueKey('marcas-batidas-detectar'),
            leading: const Icon(
              CupertinoIcons.music_note_2,
              size: 19,
              color: AmColors.text,
            ),
            title: const AppText('Batidas da música…',
              style: TextStyle(color: AmColors.text, fontSize: 15),
            ),
            subtitle: const AppText('Detecta o ritmo e risca a régua',
              style: TextStyle(color: AmColors.muted, fontSize: 11.5),
            ),
            onTap: () {
              Navigator.of(sheetContext).pop();
              String? comSom;
              for (final l in project.layers) {
                if (l is AudioLayer ||
                    (l is VideoLayer && l.volume > 0.001)) {
                  comSom = l.id;
                  break;
                }
              }
              if (comSom == null) {
                AureaSnack.show(context, 'Adicione uma música primeiro.');
                return;
              }
              showBeatsSheet(context, ref, comSom);
            },
          ),
          ListTile(
            key: const ValueKey('marcas-batidas-virar'),
            leading: const Icon(
              CupertinoIcons.flag,
              size: 19,
              color: AmColors.text,
            ),
            title: const AppText('Batidas viram marcas',
              style: TextStyle(color: AmColors.text, fontSize: 15),
            ),
            subtitle: const AppText(
              'Cada batida vira uma marca de verdade na régua',
              style: TextStyle(color: AmColors.muted, fontSize: 11.5),
            ),
            enabled: project.beats.isNotEmpty,
            onTap: () {
              final n = controller.batidasViramMarcadores();
              Navigator.of(sheetContext).pop();
              AureaSnack.show(
                context,
                n == 0 ? 'As batidas já têm marcas.' : '$n marcas no ritmo',
              );
            },
          ),
          ListTile(
            leading: Icon(
              CupertinoIcons.delete,
              size: 19,
              color: AmColors.pink,
            ),
            title: AppText('Limpar as marcas',
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
