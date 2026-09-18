import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/tokens.dart';
import '../../../../core/ui/snack.dart';
import '../../application/editor_controller.dart';
import '../../domain/cut_ops.dart' show hasTimeRemap;
import '../am/decupar_sheet.dart' show showDecuparSheet;
import '../../application/playback_controller.dart';
import '../../application/ui/editor_session.dart';
import '../../domain/layer.dart';
import '../am/align_sheet.dart';
import '../am/audio_sheet.dart';
import '../am/beat_pulse_sheet.dart';
import '../am/beats_sheet.dart';
import '../am/cameras_sheet.dart';
import '../am/font_sheet.dart';
import '../am/freeze_sheet.dart';
import '../am/layer_menu.dart';
import '../am/oficio_sheets.dart';
import '../am/precomp_sheet.dart';
import '../am/speed_sheet.dart';
import '../am/text_path_sheet.dart';
import '../widgets/add_layer_sheet.dart' show showCaptionCreationSheet;

/// Uma acao rapida da camada: icone, rotulo, o que faz, e se e Pro.
class QuickAction {
  const QuickAction({
    required this.key,
    required this.icon,
    required this.label,
    required this.onTap,
    this.enabled = true,
    this.reason = '',
    this.pro = false,
    this.aceso = false,
  });

  final String key;
  final IconData icon;
  final String label;
  final VoidCallback onTap;
  final bool enabled;
  final String reason;
  final bool pro;
  final bool aceso;
}

/// O QUE A CAMADA SELECIONADA PODE FAZER DE IMEDIATO (E2, linha de
/// acoes rapidas — Blurrr). So o que faz sentido para o tipo; o que e
/// Pro entra so no modo Pro. Tudo com rotulo.
List<QuickAction> quickActionsFor(
  BuildContext context,
  WidgetRef ref,
  Layer layer,
  PlaybackController playback, {
  required bool pro,
  required VoidCallback onAnimarTexto,
}) {
  final controller = ref.read(editorControllerProvider.notifier);
  final project = ref.read(editorControllerProvider);
  final id = layer.id;
  final temSom = layer is AudioLayer || layer is VideoLayer;
  final mudo = controller.audioSpecOf(id)?.muted ?? false;
  final temPai =
      project.linkFor(id, LayerProp.parent) != null ||
      (layer is Scene3DLayer && layer.cameraParentLayerId != null);
  final t = playback.time.value;

  void pausa(void Function() abrir) {
    playback.pause();
    abrir();
  }

  final acoes = <QuickAction>[
    QuickAction(
      key: 'dividir',
      icon: CupertinoIcons.scissors,
      label: 'Dividir',
      onTap: () => controller.splitLayer(id, t),
    ),
    // DECUPAR: o detector de cena corta o clipe inteiro sozinho — a
    // porta do servico que existia sem entrada nenhuma.
    if (layer is VideoLayer)
      QuickAction(
        key: 'decupar',
        icon: CupertinoIcons.rectangle_split_3x1,
        label: 'Decupar',
        enabled: !layer.reverse && !hasTimeRemap(layer),
        reason: 'Reverso e Time Remap entortam o tempo da fonte: '
            'decupe antes de aplicar.',
        onTap: () => pausa(() => showDecuparSheet(context, ref, id)),
      ),
    QuickAction(
      key: 'copiar-efeitos',
      icon: CupertinoIcons.doc_on_doc,
      label: 'Copiar efeitos',
      enabled: layer.effects.isNotEmpty,
      reason: 'Esta camada ainda nao tem efeitos',
      onTap: () {
        final n = controller.copyEffects(id);
        ScaffoldMessenger.maybeOf(context)?.showSnackBar(
          SnackBar(content: AppText(n == 0 ? 'Nada para copiar' : 'Efeitos copiados')),
        );
      },
    ),
    QuickAction(
      key: 'colar-efeitos',
      icon: CupertinoIcons.doc_on_clipboard,
      label: 'Colar efeitos',
      onTap: () {
        final n = controller.pasteEffects(id);
        ScaffoldMessenger.maybeOf(context)?.showSnackBar(
          SnackBar(content: AppText(n == 0 ? 'Copie os efeitos de outra camada primeiro' : 'Efeitos colados')),
        );
      },
    ),
    QuickAction(
      key: 'subir',
      icon: CupertinoIcons.arrow_up_to_line,
      label: 'Subir',
      onTap: () => controller.reorderLayer(id, -1),
    ),
    QuickAction(
      key: 'descer',
      icon: CupertinoIcons.arrow_down_to_line,
      label: 'Descer',
      onTap: () => controller.reorderLayer(id, 1),
    ),
    if (temSom) ...[
      QuickAction(
        key: 'velocidade',
        icon: CupertinoIcons.speedometer,
        label: 'Velocidade',
        onTap: () =>
            pausa(() => showSpeedSheet(context, ref, id, playback: playback)),
      ),
      QuickAction(
        key: 'volume',
        icon: CupertinoIcons.speaker_2,
        label: 'Volume',
        onTap: () => pausa(() => showAudioSheet(context, ref, id)),
      ),
      QuickAction(
        key: 'mudo',
        icon: mudo
            ? CupertinoIcons.speaker_slash_fill
            : CupertinoIcons.speaker_slash,
        label: mudo ? 'Ativar som' : 'Mudo',
        aceso: mudo,
        onTap: () =>
            controller.updateAudioSpec(id, (s) => s.copyWith(muted: !mudo)),
      ),
    ],
    if (layer is VideoLayer)
      QuickAction(
        key: 'congelar',
        icon: CupertinoIcons.pause_fill,
        label: 'Congelar',
        pro: true,
        onTap: () => pausa(() => showFreezeSheet(context, ref, id, t)),
      ),
    QuickAction(
      key: 'alinhar',
      icon: CupertinoIcons.square_grid_3x2,
      label: 'Alinhar',
      pro: true,
      onTap: () => showAlignSheet(context, ref, [id], t),
    ),
    // EDICAO DE 3 PONTOS (Pro, Fase 8): inserir e sobrescrever poem uma
    // copia deste clipe no cabecote; levantar e extrair usam o trecho
    // Entrada→Saida marcado na regua.
    if (temSom || layer is VideoLayer) ...[
      QuickAction(
        key: 'inserir',
        icon: CupertinoIcons.arrow_right_to_line,
        label: 'Inserir aqui',
        pro: true,
        onTap: () {
          controller.insertLayerAt(layer.duplicated(), t);
          AureaSnack.show(
            context,
            'Inserido no cabecote; o que vinha depois foi empurrado',
            actionLabel: 'Desfazer',
            onAction: controller.undo,
          );
        },
      ),
      QuickAction(
        key: 'sobrescrever',
        icon: CupertinoIcons.rectangle_on_rectangle,
        label: 'Sobrescrever',
        pro: true,
        onTap: () {
          controller.overwriteLayerAt(layer.duplicated(), t);
          AureaSnack.show(
            context,
            'Sobrescrito no cabecote',
            actionLabel: 'Desfazer',
            onAction: controller.undo,
          );
        },
      ),
    ],
    QuickAction(
      key: 'levantar',
      icon: CupertinoIcons.arrow_up_to_line,
      label: 'Levantar',
      pro: true,
      onTap: () {
        final trecho = ref.read(editorSessionProvider).inOut;
        if (trecho == null) {
          showReasonToast(context, 'Marque Entrada (I) e Saida (O) na regua');
          return;
        }
        controller.liftTimeRange(trecho.$1, trecho.$2, only: {id});
        AureaSnack.show(
          context,
          'Trecho levantado (ficou o buraco)',
          actionLabel: 'Desfazer',
          onAction: controller.undo,
        );
      },
    ),
    QuickAction(
      key: 'extrair',
      icon: CupertinoIcons.scissors_alt,
      label: 'Extrair',
      pro: true,
      onTap: () {
        final trecho = ref.read(editorSessionProvider).inOut;
        if (trecho == null) {
          showReasonToast(context, 'Marque Entrada (I) e Saida (O) na regua');
          return;
        }
        controller.extractTimeRange(trecho.$1, trecho.$2, only: {id});
        AureaSnack.show(
          context,
          'Trecho extraido (o resto encostou)',
          actionLabel: 'Desfazer',
          onAction: controller.undo,
        );
      },
    ),
    QuickAction(
      key: 'vincular',
      icon: !temPai ? CupertinoIcons.link : CupertinoIcons.link_circle_fill,
      label: !temPai ? 'Vincular' : 'Soltar',
      pro: false,
      aceso: temPai,
      onTap: () {
        if (temPai) {
          controller.unlinkProperty(id, LayerProp.parent, time: t);
        } else {
          pausa(() => showParentSheet(context, ref, layer, t));
        }
      },
    ),
    if (layer is GroupLayer) ...[
      // ENTRAR NO GRUPO: os filhos viram a timeline, em tempo local, com
      // o caminho Projeto › Grupo na regua (Fase 2).
      QuickAction(
        key: 'entrar',
        icon: CupertinoIcons.folder_open,
        label: 'Entrar',
        onTap: () => controller.enterGroup(id),
      ),
      QuickAction(
        key: 'precomp',
        icon: CupertinoIcons.timer,
        label: 'Tempo',
        pro: true,
        onTap: () => pausa(() => showPrecompSheet(context, ref, id, playback)),
      ),
      QuickAction(
        key: 'desagrupar',
        icon: CupertinoIcons.folder_badge_minus,
        label: 'Desagrupar',
        onTap: () => controller.ungroupLayer(id),
      ),
    ],
    if (layer is TextLayer) ...[
      QuickAction(
        key: 'fonte',
        icon: CupertinoIcons.textformat_abc,
        label: 'Fonte',
        onTap: () => pausa(() => showFontSheet(context, ref, id)),
      ),
      QuickAction(
        key: 'animar',
        icon: CupertinoIcons.play_rectangle,
        label: 'Animar',
        onTap: onAnimarTexto,
      ),
      QuickAction(
        key: 'caminho',
        icon: CupertinoIcons.arrow_turn_up_right,
        label: 'Caminho',
        pro: true,
        onTap: () => pausa(() => showTextPathSheet(context, ref, id)),
      ),
    ],
    if (temSom) ...[
      QuickAction(
        key: 'batidas',
        icon: CupertinoIcons.metronome,
        label: 'Batidas',
        pro: true,
        onTap: () => pausa(() => showBeatsSheet(context, ref, id)),
      ),
    ],
    if (layer is VideoLayer) ...[
      QuickAction(
        key: 'legendar',
        icon: CupertinoIcons.captions_bubble,
        label: 'Legendar',
        onTap: () => pausa(() => showCaptionCreationSheet(context, ref)),
      ),
      QuickAction(
        key: 'reenquadrar',
        icon: CupertinoIcons.crop,
        label: 'Reenquadrar',
        pro: true,
        onTap: () async {
          AureaSnack.show(context, 'Achando o assunto...');
          final n = await controller.autoReframeLayer(id);
          if (!context.mounted) return;
          AureaSnack.show(
            context,
            n == 0
                ? 'Nao achei um assunto claro'
                : 'Reenquadrado com $n keyframes',
            actionLabel: n == 0 ? null : 'Desfazer',
            onAction: controller.undo,
          );
        },
      ),
      QuickAction(
        key: 'estabilizar',
        icon: CupertinoIcons.camera_viewfinder,
        label: 'Estabilizar',
        pro: true,
        onTap: () async {
          AureaSnack.show(context, 'Lendo o video para estabilizar...');
          final n = await controller.stabilizeLayer(id);
          if (!context.mounted) return;
          AureaSnack.show(
            context,
            'Estabilizado com $n quadros de referencia',
            actionLabel: 'Desfazer',
            onAction: controller.undo,
          );
        },
      ),
    ],
    if (layer is Scene3DLayer)
      QuickAction(
        key: 'cameras',
        icon: CupertinoIcons.videocam,
        label: 'Câmeras',
        pro: true,
        onTap: () => pausa(() => showCamerasSheet(context, ref, id, playback)),
      ),
    QuickAction(
      key: '3d',
      icon: CupertinoIcons.cube,
      label: layer.is3D ? '3D ligado' : 'Ligar 3D',
      pro: true,
      aceso: layer.is3D,
      onTap: () => controller.toggle3D(id),
    ),
    QuickAction(
      key: 'motionblur',
      icon: CupertinoIcons.wind,
      label: 'Motion blur',
      pro: true,
      aceso: project.metaOf(id).motionBlur,
      onTap: () => controller.toggleLayerMotionBlurReal(id),
    ),
    if (layer is! NullLayer &&
        layer is! VideoLayer &&
        layer is! ParticulasLayer &&
        layer is! Element3DLayer &&
        layer is! AudioLayer)
      QuickAction(
        key: 'extrude',
        icon: CupertinoIcons.cube_box,
        label: 'Extrude 3D',
        pro: true,
        onTap: () => pausa(() => showExtrudeSheet(context, ref, id)),
      ),
    if (layer is! AudioLayer)
      QuickAction(
        key: 'pulsar',
        icon: CupertinoIcons.waveform,
        label: 'Pulsar',
        pro: true,
        onTap: () => pausa(() => showBeatPulseSheet(context, ref, id)),
      ),
    QuickAction(
      key: 'organizar',
      icon: CupertinoIcons.tag,
      label: 'Organizar',
      onTap: () => pausa(() => showOrganizeSheet(context, ref, id)),
    ),
    QuickAction(
      key: 'loop',
      icon: CupertinoIcons.repeat,
      label: 'Loop de keyframes',
      pro: true,
      onTap: () => pausa(() => showLoopSheet(context, ref, id)),
    ),
    QuickAction(
      key: 'excluir-fechar',
      icon: CupertinoIcons.delete_left,
      label: 'Excluir e fechar',
      pro: true,
      // Sem aviso de "excluida" (pedido dos testadores): o Desfazer fica
      // na barra de reproducao.
      onTap: () => controller.rippleDeleteLayer(id),
    ),
    QuickAction(
      key: 'fechar-buracos',
      icon: CupertinoIcons.arrow_left_right,
      label: 'Fechar buracos',
      pro: true,
      onTap: () {
        if (controller.gapCount() == 0) {
          showReasonToast(context, 'Nao ha buraco para fechar');
          return;
        }
        controller.closeTimelineGaps();
      },
    ),
  ];
  return [
    for (final a in acoes)
      if (pro || !a.pro) a,
  ];
}

/// A LINHA DE ACOES RAPIDAS: botoes de 56 pt com icone e rotulo,
/// rolaveis. Desabilitado esmaece e explica no toque; nunca some.
class QuickActionsRow extends StatelessWidget {
  // 52 e nao 60: "os de baixo um pouco menores, para o acesso ser mais
  // facil" — o pedido dos testadores. O alvo continua acima do minimo.
  const QuickActionsRow({super.key, required this.actions, this.height = 52});

  final List<QuickAction> actions;
  final double height;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    // Row dentro de rolagem, nao ListView: sao poucas acoes, e todas
    // montadas de uma vez sao achaveis (por teste e por leitor de tela).
    return SizedBox(
      height: height,
      child: SingleChildScrollView(
        key: const ValueKey('quick-actions'),
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: 10),
        child: Row(
          children: [
            for (var i = 0; i < actions.length; i++) ...[
              if (i > 0) const SizedBox(width: 4),
              _acao(context, t, actions[i]),
            ],
          ],
        ),
      ),
    );
  }

  Widget _acao(BuildContext context, AureaTokens t, QuickAction a) {
    {
      return Tooltip(
        message: a.label,
        child: GestureDetector(
          key: ValueKey('acao-${a.key}'),
          behavior: HitTestBehavior.opaque,
          onTap: () {
            HapticFeedback.lightImpact();
            if (!a.enabled) {
              showReasonToast(context, a.reason);
              return;
            }
            a.onTap();
          },
          child: Opacity(
            opacity: a.enabled ? 1 : .35,
            child: SizedBox(
              width: 58,
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(a.icon, size: 19, color: a.aceso ? t.accent : t.text),
                  const SizedBox(height: 2),
                  AppText(
                    a.label,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      fontSize: 10,
                      color: a.aceso ? t.accent : t.muted,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      );
    }
  }
}

/// O botao "MAIS" do cabecalho abre TODAS as acoes com rotulo, em lista
/// — nao e menu escondido: cada uma tambem esta na linha rolavel.
Future<void> showAllActionsSheet(
  BuildContext context,
  List<QuickAction> actions,
) {
  return showModalBottomSheet<void>(
    context: context,
    backgroundColor: AureaTokens.of(context).surface,
    isScrollControlled: true,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
    ),
    builder: (ctx) {
      final t = AureaTokens.of(ctx);
      return SafeArea(
        child: ConstrainedBox(
          constraints: BoxConstraints(
            maxHeight: MediaQuery.of(ctx).size.height * .7,
          ),
          child: ListView(
            shrinkWrap: true,
            children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(18, 14, 18, 6),
                child: AppText('Acoes da camada',
                  style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: t.text,
                  ),
                ),
              ),
              for (final a in actions)
                ListTile(
                  key: ValueKey('mais-${a.key}'),
                  leading: Icon(a.icon, color: a.aceso ? t.accent : t.text),
                  title: AppText(a.label, style: TextStyle(color: t.text)),
                  enabled: a.enabled,
                  onTap: () {
                    HapticFeedback.lightImpact();
                    Navigator.pop(ctx);
                    a.onTap();
                  },
                ),
              const SizedBox(height: 8),
            ],
          ),
        ),
      );
    },
  );
}
