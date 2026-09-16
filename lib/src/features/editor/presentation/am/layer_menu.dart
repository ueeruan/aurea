import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/services.dart';

import '../../application/mesh_cache.dart';
import '../../domain/mesh_import.dart';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/utils/time_format.dart';
import '../../../media/application/media_import_service.dart';
import '../../application/editor_controller.dart';
import 'camera_sheet.dart' show showCameraSheet;
import 'precomp_sheet.dart';
import 'rastreio_sheet.dart' show showRastreioSheet;
import '../../../../core/ui/tocavel.dart';
import '../../application/playback_controller.dart';
import '../../domain/blend_extra.dart';
import '../../domain/caption.dart';
import '../../domain/element3d.dart';
import '../../domain/grid_rig.dart';
import '../../domain/keyframe.dart';
import '../../domain/am_sections.dart';
import '../../domain/layer.dart';
import '../../domain/mask.dart';
import '../../domain/shape.dart';
import '../../domain/shape_ops.dart';
import '../../domain/video_project.dart' show descendentesPorParentesco;
import 'am_colors.dart';
import '../context/parameter_row.dart';
import 'audio_sheet.dart';
import 'borda_e_sombra_sheet.dart';
import '../../../../core/ui/snack.dart';
import '../../domain/layer_meta.dart';
import 'am_widgets.dart';
import 'layer_look.dart';
import 'caption_style_sheet.dart';
import 'color_picker_sheet.dart';
import 'curve_panel.dart';
import 'gradient_fill_sheet.dart';
import 'panel_chrome.dart';
import 'path_edit_sheet.dart';
import 'scene3d_sheet.dart';
import 'speed_sheet.dart';
import '../estudio/estudio_da_cena.dart';

/// Acao escolhida no menu da camada.
enum LayerMenuAction {
  transform,
  blending,
  colorFill,
  effects,
  editText,
  textAnimators,

  /// Nivel 1: o painel "Editar forma" (numeros da forma, traco,
  /// desenhar, pontos) e o mesmo painel aberto na aba do traco.
  editShape,
  stroke,
}

class LayerToolsDock extends ConsumerWidget {
  const LayerToolsDock({
    super.key,
    required this.layer,
    required this.playback,
    required this.onAction,
    this.onAnimarTexto,
  });

  final Layer layer;
  final PlaybackController playback;
  final ValueChanged<LayerMenuAction> onAction;
  final VoidCallback? onAnimarTexto;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return ColoredBox(
      color: AmColors.panel,
      child: LayoutBuilder(
        builder: (context, constraints) {
          final tileHeight = ((constraints.maxHeight - 70) / 2).clamp(
            58.0,
            82.0,
          );
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Linha superior com 5 botões de ação rápida conforme Screenshot 3
              Container(
                height: 44,
                margin: const EdgeInsets.fromLTRB(10, 8, 10, 8),
                decoration: BoxDecoration(
                  color: const Color(0xFF1E222D),
                  borderRadius: BorderRadius.circular(10),
                ),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                  children: [
                    // AS PORTAS DO GRUPO. Entrar, Desagrupar e Tempo viraram
                    // codigo sem chamador quando o menu foi refeito: grupo so
                    // entrava por toque duplo, e desagrupar nao existia na
                    // tela. Velocidade e volume nao fazem nada num grupo.
                    if (layer is GroupLayer) ...[
                      Expanded(
                        child: IconButton(
                          key: const ValueKey('grupo-entrar'),
                          tooltip: 'Entrar no grupo',
                          onPressed: () {
                            HapticFeedback.lightImpact();
                            playback.pause();
                            ref
                                .read(editorControllerProvider.notifier)
                                .enterGroup(layer.id);
                          },
                          icon: const Icon(
                            CupertinoIcons.arrow_down_right_square,
                            size: 20,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      Expanded(
                        child: IconButton(
                          key: const ValueKey('grupo-desagrupar'),
                          tooltip: 'Desagrupar',
                          onPressed: () {
                            HapticFeedback.lightImpact();
                            playback.pause();
                            final avisos = ref
                                .read(editorControllerProvider.notifier)
                                .ungroupLayer(layer.id);
                            if (avisos.isNotEmpty) {
                              ScaffoldMessenger.maybeOf(context)?.showSnackBar(
                                SnackBar(
                                  content: AppText(avisos.join('\n')),
                                  duration: const Duration(seconds: 3),
                                ),
                              );
                            }
                          },
                          icon: const Icon(
                            CupertinoIcons.square_split_2x2,
                            size: 20,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      Expanded(
                        child: IconButton(
                          key: const ValueKey('grupo-tempo'),
                          tooltip: 'Tempo',
                          onPressed: () {
                            HapticFeedback.lightImpact();
                            playback.pause();
                            showPrecompSheet(context, ref, layer.id, playback);
                          },
                          icon: const Icon(
                            CupertinoIcons.timer,
                            size: 20,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                    ],
                    if (layer is! GroupLayer)
                    Expanded(
                      child: IconButton(
                        tooltip: 'Velocidade',
                        onPressed: () {
                          HapticFeedback.lightImpact();
                          playback.pause();
                          showSpeedSheet(
                            context,
                            ref,
                            layer.id,
                            playback: playback,
                          );
                        },
                        icon: const Icon(
                          CupertinoIcons.speedometer,
                          size: 20,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    Expanded(
                      child: IconButton(
                        tooltip: 'Mover início para o cabeçote',
                        onPressed: () {
                          HapticFeedback.lightImpact();
                          playback.pause();
                          ref
                              .read(editorControllerProvider.notifier)
                              .moveLayer(layer.id, playback.time.value);
                        },
                        icon: const Icon(
                          CupertinoIcons.arrow_right_to_line,
                          size: 19,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    Expanded(
                      child: IconButton(
                        key: const ValueKey('camada-dividir'),
                        tooltip: 'Dividir camada',
                        onPressed: () {
                          HapticFeedback.lightImpact();
                          ref
                              .read(editorControllerProvider.notifier)
                              .splitLayer(layer.id, playback.time.value);
                        },
                        icon: const Icon(
                          CupertinoIcons.scissors,
                          size: 19,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    Expanded(
                      child: IconButton(
                        tooltip: 'Mover fim para o cabeçote',
                        onPressed: () {
                          HapticFeedback.lightImpact();
                          playback.pause();
                          final start = playback.time.value - layer.duration;
                          ref
                              .read(editorControllerProvider.notifier)
                              .moveLayer(
                                layer.id,
                                start < Duration.zero ? Duration.zero : start,
                              );
                        },
                        icon: const Icon(
                          CupertinoIcons.arrow_left_to_line,
                          size: 19,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    if (layer is! GroupLayer)
                    Expanded(
                      child: IconButton(
                        tooltip: 'Volume / Áudio',
                        onPressed: () {
                          HapticFeedback.lightImpact();
                          playback.pause();
                          showAudioSheet(
                            context,
                            ref,
                            layer.id,
                            playback: playback,
                          );
                        },
                        icon: const Icon(
                          CupertinoIcons.speaker_2,
                          size: 20,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    Expanded(
                      child: IconButton(
                        tooltip: 'Vincular (Parentear)',
                        onPressed: () {
                          HapticFeedback.lightImpact();
                          playback.pause();
                          showParentSheet(
                            context,
                            ref,
                            layer,
                            playback.time.value,
                          );
                        },
                        icon: Icon(
                          (ref
                                          .watch(editorControllerProvider)
                                          .linkFor(
                                            layer.id,
                                            LayerProp.parent,
                                          ) !=
                                      null ||
                                  ((layer is Scene3DLayer) &&
                                      (layer as Scene3DLayer)
                                              .cameraParentLayerId !=
                                          null))
                              ? CupertinoIcons.link_circle_fill
                              : CupertinoIcons.link,
                          size: 20,
                          color:
                              (ref
                                          .watch(editorControllerProvider)
                                          .linkFor(
                                            layer.id,
                                            LayerProp.parent,
                                          ) !=
                                      null ||
                                  ((layer is Scene3DLayer) &&
                                      (layer as Scene3DLayer)
                                              .cameraParentLayerId !=
                                          null))
                              ? AmColors.accent
                              : AmColors.text,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
              Expanded(
                child: SingleChildScrollView(
                  padding: const EdgeInsets.fromLTRB(10, 0, 10, 8),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: _fileiras(
                      secoesDe(layer),
                      (section) => _tileDaSecao(
                        section,
                        context: context,
                        ref: ref,
                        layer: layer,
                        playback: playback,
                        fecharCom: onAction,
                        abrirDepois: (open) {
                          playback.pause();
                          open();
                        },
                      ),
                      tileHeight: tileHeight,
                    ),
                  ),
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

/// UM TILE DA GRADE: icone, rotulo e o que ele abre.
typedef _Tile = ({
  IconData icone,
  String rotulo,
  VoidCallback onTap,
  String? badge,
});

/// AS FILEIRAS DE FICHAS.
///
/// Eram DUAS por fileira, e o dono apontou a planta: sao TRES. Com duas
/// a ficha ficava larga e baixa (uma tarja), a lista descia por quatro
/// fileiras e o painel virava rolagem. Com tres, o mesmo conjunto cabe
/// em tres fileiras e cada ficha tem a proporcao da planta.
const int _colunasDeFichas = 3;

List<Widget> _fileiras(
  Set<AmSecao> secoes,
  _Tile? Function(AmSecao) tile, {
  double tileHeight = 68,
}) {
  final visiveis = [
    for (final s in AmSecao.values)
      if (secoes.contains(s)) ?tile(s),
  ];
  if (visiveis.isEmpty) return const [];

  final List<Widget> linhas = [];
  for (var i = 0; i < visiveis.length; i += _colunasDeFichas) {
    final daLinha = visiveis.skip(i).take(_colunasDeFichas).toList();
    linhas.add(
      Padding(
        padding: const EdgeInsets.only(bottom: 8),
        child: Row(
          children: [
            for (var k = 0; k < _colunasDeFichas; k++) ...[
              if (k > 0) const SizedBox(width: 8),
              if (k < daLinha.length)
                _MenuTile(
                  height: tileHeight,
                  icon: daLinha[k].icone,
                  label: daLinha[k].rotulo,
                  onTap: daLinha[k].onTap,
                  badge: daLinha[k].badge,
                )
              // A COLUNA VAZIA guarda o lugar: sem ela a ultima ficha
              // de uma fileira incompleta esticava e ficava do tamanho
              // de tres.
              else
                const Spacer(),
            ],
          ],
        ),
      ),
    );
  }
  return linhas;
}

/// O QUE CADA SECAO ABRE, para esta camada.
_Tile? _tileDaSecao(
  AmSecao secao, {
  required BuildContext context,
  required ValueChanged<LayerMenuAction> fecharCom,
  required WidgetRef ref,
  required Layer layer,
  required PlaybackController playback,
  required void Function(VoidCallback) abrirDepois,
}) {
  return switch (secao) {
    AmSecao.moverTransformar => (
      icone: Icons.open_with_rounded,
      rotulo: 'Movimentação e transformação',
      onTap: () => fecharCom(LayerMenuAction.transform),
      badge: null,
    ),
    AmSecao.corPreenchimento => (
      icone: CupertinoIcons.paintbrush,
      rotulo: 'Cor e preenchimento',
      onTap: () {
        if (layer is Scene3DLayer) {
          abrirDepois(() => showScene3DSheet(context, ref, layer.id));
        } else if (layer is Element3DLayer) {
          abrirDepois(() => showElement3DSheet(context, ref, layer.id));
        } else {
          fecharCom(LayerMenuAction.colorFill);
        }
      },
      badge: null,
    ),
    AmSecao.bordaSombra => (
      icone: CupertinoIcons.square_on_square,
      rotulo: 'Borda e sombra',
      onTap: () => abrirDepois(
        () => showBordaESombraSheet(context, ref, layer.id, playback),
      ),
      badge: null,
    ),
    AmSecao.mesclarOpacidade => (
      icone: CupertinoIcons.circle_lefthalf_fill,
      rotulo: 'Mistura e opacidade',
      onTap: () => fecharCom(LayerMenuAction.blending),
      badge: null,
    ),
    AmSecao.volume => (
      icone: CupertinoIcons.speaker_2,
      rotulo: 'Volume',
      onTap: () => abrirDepois(
        () => showAudioSheet(context, ref, layer.id, playback: playback),
      ),
      badge: null,
    ),
    AmSecao.fade => (
      icone: CupertinoIcons.slider_horizontal_below_rectangle,
      rotulo: 'Fade',
      onTap: () => abrirDepois(
        () => showAudioSheet(context, ref, layer.id, playback: playback),
      ),
      badge: null,
    ),
    AmSecao.editarForma => (
      icone: CupertinoIcons.slider_horizontal_below_rectangle,
      rotulo: 'Editar forma',
      onTap: () => fecharCom(LayerMenuAction.editShape),
      badge: null,
    ),
    AmSecao.clonar => (
      icone: CupertinoIcons.circle_grid_3x3,
      rotulo: 'Clonar',
      onTap: () =>
          abrirDepois(() => showGridSheet(context, ref, layer.id, playback)),
      badge: null,
    ),
    AmSecao.editarTexto => (
      icone: CupertinoIcons.textformat,
      rotulo: 'Editar texto',
      onTap: () => fecharCom(LayerMenuAction.editText),
      badge: null,
    ),
    AmSecao.editarLegendas => (
      icone: CupertinoIcons.captions_bubble,
      rotulo: 'Editar legendas',
      onTap: () => abrirDepois(
        () => showCaptionCuesSheet(context, ref, layer.id, playback),
      ),
      badge: null,
    ),
    AmSecao.particulas => (
      icone: CupertinoIcons.sparkles,
      rotulo: 'Partículas',
      onTap: () =>
          abrirDepois(() => showParticlesSheet(context, ref, layer.id)),
      badge: null,
    ),
    AmSecao.cena3d => (
      icone: CupertinoIcons.videocam,
      rotulo: layer is Scene3DLayer ? 'Cena 3D' : 'Elemento 3D',
      onTap: () => abrirDepois(
        () => layer is Scene3DLayer
            ? abrirEstudioDaCena(context, layerId: layer.id, playback: playback)
            : showElement3DSheet(context, ref, layer.id),
      ),
      badge: null,
    ),
    AmSecao.rastrear => (
      icone: CupertinoIcons.viewfinder,
      rotulo: 'Cena 3D',
      onTap: () => abrirDepois(() => showRastreioSheet(context, ref, layer.id)),
      badge: 'NEW',
    ),
    AmSecao.camera => (
      icone: CupertinoIcons.videocam,
      rotulo: 'Câmera',
      onTap: () =>
          abrirDepois(() => showCameraSheet(context, ref, layer.id, playback)),
      badge: 'NEW',
    ),
    AmSecao.efeitos => (
      icone: CupertinoIcons.sparkles,
      rotulo: 'Efeitos',
      onTap: () => fecharCom(LayerMenuAction.effects),
      badge: null,
    ),
  };
}

/// Toast curto com a razao de um controle desabilitado ou de um comando
/// sem alvo (contrato: nada visivel pode ser inerte em silencio).
void showReasonToast(BuildContext context, String msg) {
  AureaSnack.show(context, msg, duration: const Duration(milliseconds: 1500));
}

/// Modulo GRADE do objeto nulo (spec AM2-modulo-grid): o rig posiciona os
/// assets; a transform de cada camada e um offset por cima (mover uma
/// camada nao quebra a grade). Modos Retangular/Radial/Esferico + Morph
/// animavel (caminho mais curto) + Proximidade com effector esferico 3D.
Future<void> showGridSheet(
  BuildContext context,
  WidgetRef ref,
  String nullId,
  PlaybackController playback,
) async {
  final controller = ref.read(editorControllerProvider.notifier);

  Future<void> pickAssets(BuildContext ctx, StateSetter setSheetState) async {
    final project = ref.read(editorControllerProvider);
    final current = (project.layerById(nullId) as NullLayer?)?.grid;
    final picked = <String>{...(current?.assets ?? const [])};
    await showModalBottomSheet<void>(
      context: ctx,
      backgroundColor: AmColors.panelHigh,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
      ),
      builder: (c2) => StatefulBuilder(
        builder: (c2, setInner) => SafeArea(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Padding(
                padding: EdgeInsets.all(14),
                child: AppText('Camadas da grade (ordem = indice)',
                  style: TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
              ),
              Flexible(
                child: ListView(
                  shrinkWrap: true,
                  children: [
                    for (final l in project.layers)
                      if (l.id != nullId && l is! NullLayer)
                        Material(
                          color: Colors.transparent,
                          child: CheckboxListTile(
                            dense: true,
                            value: picked.contains(l.id),
                            activeColor: AmColors.accent,
                            checkColor: const Color(0xFF0B0E12),
                            controlAffinity: ListTileControlAffinity.leading,
                            title: AppText(
                              l.name,
                              style: const TextStyle(
                                fontSize: 14,
                                color: AmColors.text,
                              ),
                            ),
                            onChanged: (v) => setInner(() {
                              if (v == true) {
                                picked.add(l.id);
                              } else {
                                picked.remove(l.id);
                              }
                            }),
                          ),
                        ),
                  ],
                ),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(20, 8, 20, 12),
                child: SizedBox(
                  width: double.infinity,
                  child: CupertinoButton(
                    color: AmColors.accent,
                    borderRadius: BorderRadius.circular(12),
                    onPressed: () {
                      controller.setGridAssets(nullId, picked.toList());
                      Navigator.of(c2).pop();
                    },
                    child: AppText(
                      'Usar ${picked.length} camada(s)',
                      style: const TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w700,
                        color: Color(0xFF0B0E12),
                      ),
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
    setSheetState(() {});
  }

  await showParamSheet(
    context,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) => ValueListenableBuilder<Duration>(
        valueListenable: playback.time,
        builder: (sheetContext, t, _) {
          final layer = ref.read(editorControllerProvider).layerById(nullId);
          if (layer is! NullLayer) return const SizedBox.shrink();
          final rig = layer.grid;
          final local = layer.localTime(t);

          Widget ruler(
            String label,
            double value,
            double min,
            double max,
            String display,
            ValueChanged<double> onChanged,
          ) {
            final numero = RegExp(r'^-?[\d.,]+').firstMatch(display);
            final casas =
                RegExp(r'[.,](\d+)$').firstMatch(numero?.group(0) ?? '');
            return ParameterRow(
              label: label,
              value: value.clamp(min, max),
              min: min,
              max: max,
              unitsPerPixel: (max - min) / 420,
              decimals: casas?.group(1)?.length ?? 0,
              unit: numero == null ? '' : display.substring(numero.end),
              onChanged: (v) {
                onChanged(v.clamp(min, max));
                setSheetState(() {});
              },
            );
          }

          // Linha ANIMAVEL: cada parametro da grade tem sua trilha de
          // keyframes propria, com diamante no playhead atual.
          Widget animRow(
            String label,
            String key,
            AnimatedDouble track,
            double min,
            double max,
            String display, {
            double scale = 1,
          }) {
            // Na lingua nova, o diamante mora na propria linha: toque
            // poe/tira o keyframe; toque LONGO abre a curva (2+ kfs).
            final numero = RegExp(r'^-?[\d.,]+').firstMatch(display);
            final casas =
                RegExp(r'[.,](\d+)$').firstMatch(numero?.group(0) ?? '');
            return ParameterRow(
              label: label,
              value: (track.valueAt(local) * scale).clamp(min, max),
              min: min,
              max: max,
              unitsPerPixel: (max - min) / 420,
              decimals: casas?.group(1)?.length ?? 0,
              unit: numero == null ? '' : display.substring(numero.end),
              keyframe: KeyframeState(
                animated: track.isAnimated,
                here: track.hasKeyframeAt(local),
                onToggle: () {
                  controller.toggleGridParamKeyframe(nullId, key, t);
                  setSheetState(() {});
                },
                onCurve: track.keyframes.length < 2
                    ? null
                    : () => showGridCurveSheet(
                        context,
                        ref,
                        playback,
                        nullId,
                        key,
                        label,
                        onClosed: () {
                          if (context.mounted) {
                            showGridSheet(context, ref, nullId, playback);
                          }
                        },
                      ),
              ),
              onChanged: (v) {
                controller.editGridParam(nullId, key, t, v / scale);
                setSheetState(() {});
              },
            );
          }

          return SafeArea(
            child: SingleChildScrollView(
              padding: EdgeInsets.fromLTRB(
                18,
                14,
                18,
                14 + MediaQuery.of(sheetContext).viewInsets.bottom,
              ),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const Expanded(
                        child: AppText('Modulo Grade',
                          style: TextStyle(
                            fontSize: 17,
                            fontWeight: FontWeight.w700,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      if (rig != null)
                        Tocavel(
                          onTap: () {
                            controller.removeGrid(nullId);
                            setSheetState(() {});
                          },
                          child: const AppText(
                            'Remover',
                            style: TextStyle(
                              fontSize: 13,
                              color: AmColors.muted,
                            ),
                          ),
                        ),
                    ],
                  ),
                  // Mini-transporte: anime keyframes SEM fechar o painel.
                  SheetTransport(
                    playback: playback,
                    duration: ref.read(editorControllerProvider).duration,
                    fps: ref.read(editorControllerProvider).fps,
                  ),
                  const SizedBox(height: 6),
                  SizedBox(
                    width: double.infinity,
                    child: CupertinoButton(
                      color: rig == null ? AmColors.accent : AmColors.chip,
                      borderRadius: BorderRadius.circular(12),
                      onPressed: () => pickAssets(sheetContext, setSheetState),
                      child: AppText(
                        rig == null
                            ? 'Escolher camadas da grade...'
                            : 'Camadas: ${rig.assets.length}  (editar)',
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w700,
                          color: rig == null
                              ? const Color(0xFF0B0E12)
                              : AmColors.accent,
                        ),
                      ),
                    ),
                  ),
                  if (rig != null) ...[
                    const SizedBox(height: 12),
                    // Modo / morph: 1 Retangular, 2 Radial, 3 Esferico.
                    Row(
                      children: [
                        for (final (label, mode) in const [
                          ('Retangular', 1.0),
                          ('Radial', 2.0),
                          ('Esferico', 3.0),
                        ])
                          Padding(
                            padding: const EdgeInsets.only(right: 8),
                            child: Tocavel(
                              onTap: () {
                                // Com o morph ANIMADO, escolher um modo
                                // cria keyframe no playhead (nao apaga a
                                // animacao); sem animacao, so troca o modo.
                                if (rig.transition.isAnimated) {
                                  controller.editGridTransition(
                                    nullId,
                                    playback.time.value,
                                    mode,
                                  );
                                } else {
                                  controller.updateGrid(
                                    nullId,
                                    (g) => g.copyWith(
                                      transition: AnimatedDouble(mode),
                                    ),
                                  );
                                }
                                setSheetState(() {});
                              },
                              child: Container(
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 12,
                                  vertical: 8,
                                ),
                                decoration: BoxDecoration(
                                  color:
                                      (!rig.transition.isAnimated &&
                                          rig.transition
                                                  .valueAt(local)
                                                  .round() ==
                                              mode.round())
                                      ? AmColors.accentDim
                                      : AmColors.chip,
                                  borderRadius: BorderRadius.circular(9),
                                ),
                                child: AppText(
                                  label,
                                  style: const TextStyle(
                                    fontSize: 12,
                                    color: AmColors.accent,
                                  ),
                                ),
                              ),
                            ),
                          ),
                        const Spacer(),
                        // Diamante do MORPH (transition animavel).
                        CupertinoButton(
                          padding: const EdgeInsets.all(4),
                          onPressed: () {
                            controller.toggleGridTransitionKeyframe(nullId, t);
                            setSheetState(() {});
                          },
                          child: Icon(
                            rig.transition.hasKeyframeAt(local)
                                ? CupertinoIcons.rhombus_fill
                                : CupertinoIcons.rhombus,
                            size: 18,
                            color: rig.transition.isAnimated
                                ? AmColors.accent
                                : AmColors.muted,
                          ),
                        ),
                        // Curva do MORPH.
                        CupertinoButton(
                          padding: const EdgeInsets.all(4),
                          onPressed: () {
                            if (rig.transition.keyframes.length < 2) {
                              showReasonToast(
                                context,
                                'Crie 2+ keyframes no morph para editar a curva',
                              );
                              return;
                            }
                            showGridCurveSheet(
                              context,
                              ref,
                              playback,
                              nullId,
                              'transition',
                              'Morph',
                              onClosed: () {
                                if (context.mounted) {
                                  showGridSheet(context, ref, nullId, playback);
                                }
                              },
                            );
                          },
                          child: Icon(
                            CupertinoIcons.graph_square,
                            size: 18,
                            color: rig.transition.keyframes.length >= 2
                                ? AmColors.accent
                                : AmColors.muted,
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 6),
                    ruler(
                      'Morph',
                      rig.transition.valueAt(local),
                      1,
                      3,
                      amNumber(rig.transition.valueAt(local), 1),
                      (v) => controller.editGridTransition(nullId, t, v),
                    ),
                    ruler(
                      'Colunas',
                      rig.columns.toDouble(),
                      1,
                      12,
                      '${rig.columns}',
                      (v) => controller.updateGrid(
                        nullId,
                        (g) => g.copyWith(columns: v.round()),
                      ),
                    ),
                    animRow(
                      'Espaco X',
                      'spacingX',
                      rig.spacingX,
                      20,
                      800,
                      amNumber(rig.spacingX.valueAt(local), 0),
                    ),
                    animRow(
                      'Espaco Y',
                      'spacingY',
                      rig.spacingY,
                      20,
                      800,
                      amNumber(rig.spacingY.valueAt(local), 0),
                    ),
                    animRow(
                      'Raio',
                      'radius',
                      rig.radius,
                      40,
                      1200,
                      amNumber(rig.radius.valueAt(local), 0),
                    ),
                    animRow(
                      'Rotacao',
                      'rotation',
                      rig.gridRotationDeg,
                      -180,
                      180,
                      '${amNumber(rig.gridRotationDeg.valueAt(local), 0)}°',
                    ),
                    animRow(
                      'Twist',
                      'twist',
                      rig.twistDeg,
                      -180,
                      180,
                      '${amNumber(rig.twistDeg.valueAt(local), 0)}°',
                    ),
                    animRow(
                      'Stagger',
                      'stagger',
                      rig.staggerDeg,
                      -360,
                      360,
                      '${amNumber(rig.staggerDeg.valueAt(local), 0)}°',
                    ),
                    animRow(
                      'Prof. Z',
                      'zDepth',
                      rig.zDepth,
                      -400,
                      400,
                      amNumber(rig.zDepth.valueAt(local), 0),
                    ),
                    animRow(
                      'Esc. frente',
                      'scaleFront',
                      rig.scaleFront,
                      10,
                      300,
                      amNumber(rig.scaleFront.valueAt(local) * 100, 0),
                      scale: 100,
                    ),
                    animRow(
                      'Esc. tras',
                      'scaleBack',
                      rig.scaleBack,
                      10,
                      300,
                      amNumber(rig.scaleBack.valueAt(local) * 100, 0),
                      scale: 100,
                    ),
                    animRow(
                      'Aleatorio',
                      'randomOffset',
                      rig.randomOffset,
                      0,
                      300,
                      amNumber(rig.randomOffset.valueAt(local), 0),
                    ),
                    ruler(
                      'Semente',
                      rig.seed.toDouble(),
                      0,
                      100,
                      '${rig.seed}',
                      (v) => controller.updateGrid(
                        nullId,
                        (g) => g.copyWith(seed: v.round()),
                      ),
                    ),
                    Row(
                      children: [
                        const AppText('Embaralhar',
                          style: TextStyle(fontSize: 13, color: AmColors.muted),
                        ),
                        Transform.scale(
                          scale: 0.68,
                          child: CupertinoSwitch(
                            value: rig.shuffle,
                            activeTrackColor: AmColors.accent,
                            onChanged: (v) {
                              controller.updateGrid(
                                nullId,
                                (g) => g.copyWith(shuffle: v),
                              );
                              setSheetState(() {});
                            },
                          ),
                        ),
                        const SizedBox(width: 12),
                        const AppText('Proximidade',
                          style: TextStyle(fontSize: 13, color: AmColors.muted),
                        ),
                        Transform.scale(
                          scale: 0.68,
                          child: CupertinoSwitch(
                            value: rig.proximity?.enabled ?? false,
                            activeTrackColor: AmColors.accent,
                            onChanged: (v) {
                              controller.updateGrid(nullId, (g) {
                                if (v) {
                                  return g.copyWith(
                                    proximity: (g.proximity ?? ProximityGroup())
                                        .copyWith(enabled: true),
                                  );
                                }
                                return g.copyWith(
                                  proximity: g.proximity?.copyWith(
                                    enabled: false,
                                  ),
                                );
                              });
                              setSheetState(() {});
                            },
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 8),
                    // Nulo CONTROLADOR: um SEGUNDO nulo cujo transform
                    // modula a grade — animar/curvar o nulo anima a grade.
                    Row(
                      children: [
                        const AppText('Nulo controlador',
                          style: TextStyle(fontSize: 13, color: AmColors.muted),
                        ),
                        const SizedBox(width: 10),
                        Expanded(
                          child: Tocavel(
                            onTap: () async {
                              final pickedId = await _pickControllerNull(
                                sheetContext,
                                ref,
                                nullId,
                              );
                              if (pickedId == '') {
                                controller.setGridController(nullId, null);
                              } else if (pickedId != null) {
                                controller.setGridController(nullId, pickedId);
                              }
                              setSheetState(() {});
                            },
                            child: Container(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 12,
                                vertical: 8,
                              ),
                              decoration: BoxDecoration(
                                color: rig.controllerId != null
                                    ? AmColors.accentDim
                                    : AmColors.chip,
                                borderRadius: BorderRadius.circular(9),
                              ),
                              child: AppText(
                                rig.controllerId == null
                                    ? 'Nenhum'
                                    : (ref
                                              .read(editorControllerProvider)
                                              .layerById(rig.controllerId!)
                                              ?.name ??
                                          'Nulo removido'),
                                overflow: TextOverflow.ellipsis,
                                style: const TextStyle(
                                  fontSize: 12,
                                  color: AmColors.accent,
                                ),
                              ),
                            ),
                          ),
                        ),
                      ],
                    ),
                    if (rig.controllerId != null)
                      const Padding(
                        padding: EdgeInsets.only(top: 4),
                        child: AppText('Escala do nulo -> espacamento/raio · Rotacao Z '
                          '-> rotacao da grade · Rotacao Y -> twist.',
                          style: TextStyle(fontSize: 11, color: AmColors.muted),
                        ),
                      ),
                    if (rig.proximity?.enabled ?? false) ...[
                      const SizedBox(height: 6),
                      const AppText('O effector e uma ESFERA 3D: raio 200 tambem '
                        'alcanca 200 de profundidade.',
                        style: TextStyle(fontSize: 11, color: AmColors.muted),
                      ),
                      const SizedBox(height: 6),
                      ruler(
                        'Effector X',
                        rig.proximity!.effector.valueAt(local).dx,
                        -800,
                        800,
                        amNumber(rig.proximity!.effector.valueAt(local).dx, 0),
                        (v) => controller.updateGrid(nullId, (g) {
                          final p = g.proximity!;
                          final cur = p.effector.valueAt(local);
                          return g.copyWith(
                            proximity: p.copyWith(
                              effector: p.effector.edited(
                                local,
                                Offset(v, cur.dy),
                              ),
                            ),
                          );
                        }),
                      ),
                      ruler(
                        'Effector Y',
                        rig.proximity!.effector.valueAt(local).dy,
                        -800,
                        800,
                        amNumber(rig.proximity!.effector.valueAt(local).dy, 0),
                        (v) => controller.updateGrid(nullId, (g) {
                          final p = g.proximity!;
                          final cur = p.effector.valueAt(local);
                          return g.copyWith(
                            proximity: p.copyWith(
                              effector: p.effector.edited(
                                local,
                                Offset(cur.dx, v),
                              ),
                            ),
                          );
                        }),
                      ),
                      ruler(
                        'Raio prox.',
                        rig.proximity!.radius.valueAt(local),
                        20,
                        800,
                        amNumber(rig.proximity!.radius.valueAt(local), 0),
                        (v) => controller.updateGrid(nullId, (g) {
                          final p = g.proximity!;
                          return g.copyWith(
                            proximity: p.copyWith(
                              radius: p.radius.edited(local, v),
                            ),
                          );
                        }),
                      ),
                      ruler(
                        'Escala max',
                        rig.proximity!.scaleMax * 100,
                        20,
                        400,
                        amNumber(rig.proximity!.scaleMax * 100, 0),
                        (v) => controller.updateGrid(nullId, (g) {
                          return g.copyWith(
                            proximity: g.proximity!.copyWith(scaleMax: v / 100),
                          );
                        }),
                      ),
                      ruler(
                        'Atrair',
                        rig.proximity!.attract.valueAt(local),
                        -300,
                        300,
                        amNumber(rig.proximity!.attract.valueAt(local), 0),
                        (v) => controller.updateGrid(nullId, (g) {
                          final p = g.proximity!;
                          return g.copyWith(
                            proximity: p.copyWith(
                              attract: p.attract.edited(local, v),
                            ),
                          );
                        }),
                      ),
                    ],
                  ],
                ],
              ),
            ),
          );
        },
      ),
    ),
  );
}

/// Painel de MASCARAS da camada (spec AM2-mascaras-e-formas, PR-M2):
/// pilha de mascaras com modo, inverter, feather, expansao e opacidade —
/// tudo animavel; o caminho tambem aceita keyframe (diamante).
Future<void> showMasksSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback, {
  required void Function(String maskId) onEditMaskPoints,
}) async {
  final controller = ref.read(editorControllerProvider.notifier);

  String modeLabel(MaskMode m) => switch (m) {
    MaskMode.none => 'Nenhum',
    MaskMode.add => 'Somar',
    MaskMode.subtract => 'Subtrair',
    MaskMode.intersect => 'Intersecao',
    MaskMode.lighten => 'Clarear',
    MaskMode.darken => 'Escurecer',
    MaskMode.difference => 'Diferenca',
  };

  await showParamSheet(
    context,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) => ValueListenableBuilder<Duration>(
        valueListenable: playback.time,
        builder: (sheetContext, t, _) {
          final layer = ref.read(editorControllerProvider).layerById(layerId);
          if (layer == null) return const SizedBox.shrink();
          final local = layer.localTime(t);
          final maskSize = controller.layerBoxSize(
            layer.copyLayer(
              scaleX: AnimatedDouble(1),
              scaleY: AnimatedDouble(1),
            ),
            t,
          );

          Widget ruler(
            String label,
            double value,
            double min,
            double max,
            String display,
            ValueChanged<double> onChanged,
          ) {
            final numero = RegExp(r'^-?[\d.,]+').firstMatch(display);
            final casas =
                RegExp(r'[.,](\d+)$').firstMatch(numero?.group(0) ?? '');
            return ParameterRow(
              label: label,
              value: value.clamp(min, max),
              min: min,
              max: max,
              unitsPerPixel: (max - min) / 420,
              decimals: casas?.group(1)?.length ?? 0,
              unit: numero == null ? '' : display.substring(numero.end),
              onChanged: (v) {
                onChanged(v.clamp(min, max));
                setSheetState(() {});
              },
            );
          }

          Widget preset(String label, BezierPath path) {
            return Tocavel(
              onTap: () {
                controller.addMask(
                  layerId,
                  LayerMask(
                    name: label,
                    path: AnimatedPath(path),
                    feather: AnimatedDouble(0),
                  ),
                );
                setSheetState(() {});
              },
              child: Container(
                padding: const EdgeInsets.symmetric(
                  horizontal: 14,
                  vertical: 10,
                ),
                decoration: BoxDecoration(
                  color: AmColors.chip,
                  borderRadius: BorderRadius.circular(10),
                ),
                child: AppText(
                  '+ $label',
                  style: const TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: AmColors.accent,
                  ),
                ),
              ),
            );
          }

          return SafeArea(
            child: SingleChildScrollView(
              padding: EdgeInsets.fromLTRB(
                18,
                14,
                18,
                14 + MediaQuery.of(sheetContext).viewInsets.bottom,
              ),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const AppText('Máscaras',
                    style: TextStyle(
                      fontSize: 17,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                  SheetTransport(
                    playback: playback,
                    duration: ref.read(editorControllerProvider).duration,
                    fps: ref.read(editorControllerProvider).fps,
                  ),
                  const SizedBox(height: 4),
                  const AppText('A primeira corta o alfa da camada; as seguintes '
                    'operam sobre as de cima.',
                    style: TextStyle(fontSize: 12, color: AmColors.muted),
                  ),
                  const SizedBox(height: 12),
                  for (final (i, m) in layer.masks.indexed)
                    Container(
                      margin: const EdgeInsets.only(bottom: 8),
                      padding: const EdgeInsets.fromLTRB(12, 10, 12, 6),
                      decoration: BoxDecoration(
                        color: AmColors.bg.withValues(alpha: 0.5),
                        borderRadius: BorderRadius.circular(12),
                      ),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Row(
                            children: [
                              Expanded(
                                child: AppText(
                                  '${i + 1}. ${m.name}',
                                  overflow: TextOverflow.ellipsis,
                                  style: const TextStyle(
                                    fontSize: 14,
                                    fontWeight: FontWeight.w700,
                                    color: AmColors.text,
                                  ),
                                ),
                              ),
                              CupertinoButton(
                                padding: const EdgeInsets.all(4),
                                onPressed: i == 0
                                    ? null
                                    : () {
                                        controller.reorderMask(
                                          layerId,
                                          m.id,
                                          -1,
                                        );
                                        setSheetState(() {});
                                      },
                                child: const Icon(
                                  CupertinoIcons.arrow_up,
                                  size: 17,
                                  color: AmColors.muted,
                                ),
                              ),
                              CupertinoButton(
                                padding: const EdgeInsets.all(4),
                                onPressed: i == layer.masks.length - 1
                                    ? null
                                    : () {
                                        controller.reorderMask(
                                          layerId,
                                          m.id,
                                          1,
                                        );
                                        setSheetState(() {});
                                      },
                                child: const Icon(
                                  CupertinoIcons.arrow_down,
                                  size: 17,
                                  color: AmColors.muted,
                                ),
                              ),
                              // O mesmo Edit Points com trackpad do nivel 1.
                              CupertinoButton(
                                padding: const EdgeInsets.all(4),
                                onPressed: () {
                                  closeParamSheet(sheetContext);
                                  if (context.mounted) onEditMaskPoints(m.id);
                                },
                                child: const Icon(
                                  CupertinoIcons.pencil_outline,
                                  size: 18,
                                  color: AmColors.muted,
                                ),
                              ),
                              // Diamante: keyframe do CAMINHO.
                              CupertinoButton(
                                padding: const EdgeInsets.all(4),
                                onPressed: () {
                                  controller.toggleMaskPathKeyframe(
                                    layerId,
                                    m.id,
                                    t,
                                  );
                                  setSheetState(() {});
                                },
                                child: Icon(
                                  m.path.hasKeyframeAt(local)
                                      ? CupertinoIcons.rhombus_fill
                                      : CupertinoIcons.rhombus,
                                  size: 18,
                                  color: m.path.isAnimated
                                      ? AmColors.accent
                                      : AmColors.muted,
                                ),
                              ),
                              Tocavel(
                                onTap: () {
                                  controller.removeMask(layerId, m.id);
                                  setSheetState(() {});
                                },
                                child: const Icon(
                                  CupertinoIcons.xmark,
                                  size: 15,
                                  color: AmColors.muted,
                                ),
                              ),
                            ],
                          ),
                          Wrap(
                            spacing: 5,
                            runSpacing: 5,
                            children: [
                              for (final mode in MaskMode.values)
                                Tocavel(
                                  onTap: () {
                                    controller.updateMask(
                                      layerId,
                                      m.id,
                                      (x) => x.copyWith(mode: mode),
                                    );
                                    setSheetState(() {});
                                  },
                                  child: Container(
                                    padding: const EdgeInsets.symmetric(
                                      horizontal: 8,
                                      vertical: 5,
                                    ),
                                    decoration: BoxDecoration(
                                      color: m.mode == mode
                                          ? AmColors.accentDim
                                          : AmColors.chip,
                                      borderRadius: BorderRadius.circular(7),
                                    ),
                                    child: AppText(
                                      modeLabel(mode),
                                      style: TextStyle(
                                        fontSize: 11,
                                        color: m.mode == mode
                                            ? AmColors.accent
                                            : AmColors.text,
                                      ),
                                    ),
                                  ),
                                ),
                            ],
                          ),
                          Row(
                            children: [
                              const Expanded(
                                child: AppText('Inverter',
                                  style: TextStyle(
                                    fontSize: 12,
                                    color: AmColors.muted,
                                  ),
                                ),
                              ),
                              Transform.scale(
                                scale: 0.68,
                                child: CupertinoSwitch(
                                  value: m.inverted,
                                  activeTrackColor: AmColors.accent,
                                  onChanged: (_) {
                                    controller.toggleMaskInverted(
                                      layerId,
                                      m.id,
                                    );
                                    setSheetState(() {});
                                  },
                                ),
                              ),
                            ],
                          ),
                          if (!m.path.valueAt(local).closed)
                            const AppText('Caminho aberto nao corta; pode servir de entrada de efeito.',
                              style: TextStyle(
                                fontSize: 11,
                                color: AmColors.accent,
                              ),
                            ),
                          if (maskFeatherExceedsBounds(m, local, maskSize))
                            const AppText('Aviso: caminho + feather/2 + expansao passa do limite.',
                              style: TextStyle(
                                fontSize: 11,
                                color: AmColors.accent,
                              ),
                            ),
                          const SizedBox(height: 6),
                          Row(
                            children: [
                              Expanded(
                                child: ruler(
                                  m.featherLinked ? 'Feather' : 'Feather X',
                                  m.feather.valueAt(local),
                                  0,
                                  200,
                                  amNumber(m.feather.valueAt(local), 0),
                                  (v) => controller.editMaskParam(
                                    layerId,
                                    m.id,
                                    'feather',
                                    t,
                                    v,
                                  ),
                                ),
                              ),
                              // Soltar os eixos: borda dura dos lados e
                              // macia em cima e embaixo — o degrade de
                              // horizonte que o feather redondo nao faz.
                              Tocavel(
                                onTap: () => controller.toggleMaskFeatherAxes(
                                  layerId,
                                  m.id,
                                ),
                                child: Padding(
                                  padding: const EdgeInsets.only(left: 6),
                                  child: Icon(
                                    m.featherLinked
                                        ? CupertinoIcons.link
                                        : CupertinoIcons.link_circle,
                                    size: 16,
                                    color: m.featherLinked
                                        ? AmColors.muted
                                        : AmColors.accent,
                                  ),
                                ),
                              ),
                            ],
                          ),
                          if (!m.featherLinked)
                            ruler(
                              'Feather Y',
                              m.featherVertical.valueAt(local),
                              0,
                              200,
                              amNumber(m.featherVertical.valueAt(local), 0),
                              (v) => controller.editMaskParam(
                                layerId,
                                m.id,
                                'featherY',
                                t,
                                v,
                              ),
                            ),
                          ruler(
                            'Expansao',
                            m.expansion.valueAt(local),
                            -200,
                            200,
                            amNumber(m.expansion.valueAt(local), 0),
                            (v) => controller.editMaskParam(
                              layerId,
                              m.id,
                              'expansion',
                              t,
                              v,
                            ),
                          ),
                          ruler(
                            'Opacidade',
                            m.opacity.valueAt(local) * 100,
                            0,
                            100,
                            amNumber(m.opacity.valueAt(local) * 100, 0),
                            (v) => controller.editMaskParam(
                              layerId,
                              m.id,
                              'opacity',
                              t,
                              v / 100,
                            ),
                          ),
                        ],
                      ),
                    ),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      preset('Retangulo', BezierPath.rect(460, 460)),
                      preset('Circulo', BezierPath.ellipse(480, 480)),
                      preset('Estrela', BezierPath.star(5, 250, 125)),
                      preset('Coracao', BezierPath.heart(440, 420)),
                    ],
                  ),
                ],
              ),
            ),
          );
        },
      ),
    ),
  );
}

/// Escolher o PAI (objeto nulo ou qualquer camada): o filho segue o delta
/// de posicao/rotacao/escala do pai a partir de agora.
Future<void> showParentSheet(
  BuildContext context,
  WidgetRef ref,
  Layer child,
  Duration t,
) async {
  final project = ref.read(editorControllerProvider);
  final controller = ref.read(editorControllerProvider.notifier);
  final currentParentLink = project.linkFor(child.id, LayerProp.parent);
  final currentParentId =
      currentParentLink?.sourceLayerId ??
      (child is Scene3DLayer ? child.cameraParentLayerId : null);

  final candidates = [
    ...project.layers.whereType<NullLayer>(),
    ...project.layers.where((l) => l is! NullLayer),
  ];
  // QUEM JA SEGUE ESTA CAMADA nao pode ser o pai dela: o dedo ficaria
  // num ciclo sem fim. Aparecem esmaecidas, com o porque.
  final descendentes = descendentesPorParentesco(project, child.id);

  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: AmColors.panel,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
    builder: (sheetContext) => SafeArea(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Padding(
            padding: EdgeInsets.all(14),
            child: AppText('Seguir a camada (pai)...',
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
          ),
          // NENHUM vem primeiro: desvincular tem de custar o mesmo que
          // vincular, e ate aqui so dava para soltar por outro caminho.
          Material(
            color: Colors.transparent,
            child: ListTile(
              leading: const Icon(
                CupertinoIcons.clear_circled,
                size: 20,
                color: AmColors.muted,
              ),
              title: const AppText('Nenhum',
                style: TextStyle(color: AmColors.text),
              ),
              subtitle: const AppText('Solta a camada do pai',
                style: TextStyle(fontSize: 11, color: AmColors.muted),
              ),
              trailing: currentParentId == null
                  ? const Icon(
                      CupertinoIcons.checkmark_alt,
                      color: AmColors.accent,
                      size: 20,
                    )
                  : null,
              onTap: () {
                controller.unlinkProperty(child.id, LayerProp.parent, time: t);
                if (child is Scene3DLayer) {
                  controller.setSceneCameraCompParent(child.id, null);
                }
                Navigator.of(sheetContext).pop();
              },
            ),
          ),
          for (final other in candidates)
            Material(
              color: Colors.transparent,
              child: Opacity(
                // A PROPRIA CAMADA aparece esmaecida em vez de sumir da
                // lista: sumir faz procurar o que nao existe. Parentear em
                // si mesma nao da, e a lista diz isso.
                opacity:
                    other.id == child.id || descendentes.contains(other.id)
                    ? 0.35
                    : 1,
                child: ListTile(
                  key: ValueKey('pai-${other.id}'),
                  leading: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      // A ETIQUETA DE COR, a mesma do cabecalho da linha.
                      Container(
                        width: 4,
                        height: 26,
                        margin: const EdgeInsets.only(right: 8),
                        decoration: BoxDecoration(
                          color:
                              project.metaOf(other.id).label?.color ??
                              Colors.transparent,
                          borderRadius: BorderRadius.circular(2),
                        ),
                      ),
                      Container(
                        width: 24,
                        height: 24,
                        decoration: BoxDecoration(
                          color: layerTypeColor(other),
                          borderRadius: BorderRadius.circular(6),
                        ),
                        child: Icon(
                          layerTypeIcon(other),
                          size: 14,
                          color: Colors.white,
                        ),
                      ),
                    ],
                  ),
                  title: AppText(
                    other.name,
                    style: const TextStyle(color: AmColors.text),
                  ),
                  subtitle: other.id == child.id
                      ? const AppText('É a própria camada',
                          style: TextStyle(fontSize: 11, color: AmColors.muted),
                        )
                      : descendentes.contains(other.id)
                      ? const AppText('Já segue esta camada',
                          style: TextStyle(fontSize: 11, color: AmColors.muted),
                        )
                      : other is NullLayer
                      ? AppText(
                          other.is3D ? 'Objeto Nulo 3D' : 'Objeto Nulo',
                          style: const TextStyle(
                            fontSize: 11,
                            color: AmColors.muted,
                          ),
                        )
                      : other is Scene3DLayer
                      ? const AppText(
                          'Scene 3D',
                          style: TextStyle(fontSize: 11, color: AmColors.muted),
                        )
                      : null,
                  trailing: other.id == currentParentId
                      ? const Icon(
                          CupertinoIcons.checkmark_alt,
                          color: AmColors.accent,
                          size: 20,
                        )
                      : null,
                  onTap:
                      other.id == child.id || descendentes.contains(other.id)
                      ? null
                      : () {
                          controller.linkProperty(
                            child.id,
                            LayerProp.parent,
                            other.id,
                            t,
                          );
                          if (child is Scene3DLayer) {
                            controller.setSceneCameraCompParent(
                              child.id,
                              other.id,
                            );
                          }
                          Navigator.of(sheetContext).pop();
                        },
                ),
              ),
            ),
          const SizedBox(height: 8),
        ],
      ),
    ),
  );
}

/// Painel de parametros do sistema de particulas.
Future<void> showParticlesSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  final controller = ref.read(editorControllerProvider.notifier);

  await showParamSheet(
    context,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final layer = ref.read(editorControllerProvider).layerById(layerId);
        if (layer is! ParticlesLayer) return const SizedBox.shrink();

        // A LINGUA NOVA DO PAINEL (ParameterRow): a linha inteira
        // arrasta e o numero digita o valor exato. A unidade e as casas
        // saem do display de sempre — as chamadas nao mudam.
        Widget row(
          String label,
          double value,
          double min,
          double max,
          double upp,
          String display,
          ValueChanged<double> onChanged,
        ) {
          final numero = RegExp(r'^-?[\d.,]+').firstMatch(display);
          final casas =
              RegExp(r'[.,](\d+)\$').firstMatch(numero?.group(0) ?? '');
          return ParameterRow(
            label: label,
            value: value.clamp(min, max),
            min: min,
            max: max,
            unitsPerPixel: upp,
            decimals: casas?.group(1)?.length ?? 0,
            unit: numero == null ? '' : display.substring(numero.end),
            onChanged: (v) {
              onChanged(v.clamp(min, max));
              setSheetState(() {});
            },
          );
        }

        Widget chips(
          String label,
          List<String> nomes,
          int atual,
          ValueChanged<int> onPick,
        ) {
          return Padding(
            padding: const EdgeInsets.only(bottom: 4),
            child: ParameterCustomRow(
              label: label,
              height: 44,
              child: SingleChildScrollView(
                scrollDirection: Axis.horizontal,
                child: Row(
                  children: [
                    for (var i = 0; i < nomes.length; i++)
                      Padding(
                        padding: const EdgeInsets.only(right: 6),
                        child: Tocavel(
                          onTap: () {
                            onPick(i);
                            setSheetState(() {});
                          },
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 11,
                              vertical: 7,
                            ),
                            decoration: BoxDecoration(
                              color: atual == i
                                  ? AmColors.accentDim
                                  : AmColors.chip,
                              borderRadius: BorderRadius.circular(9),
                            ),
                            child: AppText(
                              nomes[i],
                              style: const TextStyle(
                                fontSize: 12,
                                color: AmColors.accent,
                              ),
                            ),
                          ),
                        ),
                      ),
                  ],
                ),
              ),
            ),
          );
        }

        Widget titulo(String t) => Padding(
          padding: const EdgeInsets.only(top: 6, bottom: 8),
          child: AppText(
            t,
            style: const TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w700,
              letterSpacing: 0.6,
              color: AmColors.muted,
            ),
          ),
        );

        void up(ParticlesLayer Function(ParticlesLayer) f) =>
            controller.updateParticles(layerId, f);

        return SafeArea(
          child: SingleChildScrollView(
            padding: EdgeInsets.fromLTRB(
              18,
              14,
              18,
              14 + MediaQuery.of(sheetContext).viewInsets.bottom,
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const AppText('Particulas 3D',
                  style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 14),
                row(
                  'Quantidade',
                  layer.count.toDouble(),
                  1,
                  2000,
                  3,
                  '${layer.count}',
                  (v) => controller.updateParticles(
                    layerId,
                    (p) => p.copyParticles(count: v.round()),
                  ),
                ),
                row(
                  'Velocidade',
                  layer.speed,
                  0,
                  2000,
                  3,
                  amNumber(layer.speed, 0),
                  (v) => controller.updateParticles(
                    layerId,
                    (p) => p.copyParticles(speed: v),
                  ),
                ),
                row(
                  'Abertura',
                  layer.spreadDeg,
                  0,
                  360,
                  0.9,
                  '${amNumber(layer.spreadDeg, 0)}°',
                  (v) => controller.updateParticles(
                    layerId,
                    (p) => p.copyParticles(spreadDeg: v),
                  ),
                ),
                row(
                  'Direcao',
                  layer.directionDeg,
                  -180,
                  180,
                  0.9,
                  '${amNumber(layer.directionDeg, 0)}°',
                  (v) => controller.updateParticles(
                    layerId,
                    (p) => p.copyParticles(directionDeg: v),
                  ),
                ),
                row(
                  'Gravidade',
                  layer.gravity,
                  -2000,
                  2000,
                  4,
                  amNumber(layer.gravity, 0),
                  (v) => controller.updateParticles(
                    layerId,
                    (p) => p.copyParticles(gravity: v),
                  ),
                ),
                row(
                  'Tamanho',
                  layer.size,
                  1,
                  120,
                  0.3,
                  amNumber(layer.size, 0),
                  (v) => controller.updateParticles(
                    layerId,
                    (p) => p.copyParticles(size: v),
                  ),
                ),
                row(
                  'Vida (s)',
                  layer.lifetimeMs / 1000,
                  0.3,
                  10,
                  0.02,
                  amNumber(layer.lifetimeMs / 1000, 1),
                  (v) => controller.updateParticles(
                    layerId,
                    (p) => p.copyParticles(lifetimeMs: (v * 1000).round()),
                  ),
                ),
                titulo('EMISSOR'),
                chips(
                  'Emissor',
                  const ['Caixa', 'Ponto', 'Esfera', 'Anel'],
                  layer.emitter,
                  (i) => up((p) => p.copyParticles(emitter: i)),
                ),
                chips(
                  'Saida',
                  const ['Cone', 'Todas', 'Para fora'],
                  layer.emitMode,
                  (i) => up((p) => p.copyParticles(emitMode: i)),
                ),
                row(
                  '3D (Z)',
                  layer.depth,
                  0,
                  3000,
                  5,
                  amNumber(layer.depth, 0),
                  (v) => controller.updateParticles(
                    layerId,
                    (p) => p.copyParticles(depth: v),
                  ),
                ),
                row(
                  'Area X',
                  layer.emitW,
                  0,
                  2400,
                  4,
                  amNumber(layer.emitW, 0),
                  (v) => controller.updateParticles(
                    layerId,
                    (p) => p.copyParticles(emitW: v),
                  ),
                ),
                row(
                  'Area Y',
                  layer.emitH,
                  0,
                  2400,
                  4,
                  amNumber(layer.emitH, 0),
                  (v) => controller.updateParticles(
                    layerId,
                    (p) => p.copyParticles(emitH: v),
                  ),
                ),
                titulo('FISICA'),
                row(
                  'Vento X',
                  layer.windX,
                  -1500,
                  1500,
                  3,
                  amNumber(layer.windX, 0),
                  (v) => up((p) => p.copyParticles(windX: v)),
                ),
                row(
                  'Vento Y',
                  layer.windY,
                  -1500,
                  1500,
                  3,
                  amNumber(layer.windY, 0),
                  (v) => up((p) => p.copyParticles(windY: v)),
                ),
                row(
                  'Ar',
                  layer.drag,
                  0,
                  8,
                  0.02,
                  amNumber(layer.drag, 2),
                  (v) => up((p) => p.copyParticles(drag: v)),
                ),
                row(
                  'Turbulencia',
                  layer.turbulence,
                  0,
                  600,
                  1.2,
                  amNumber(layer.turbulence, 0),
                  (v) => up((p) => p.copyParticles(turbulence: v)),
                ),
                row(
                  'Detalhe',
                  layer.turbulenceScale,
                  20,
                  1200,
                  2,
                  amNumber(layer.turbulenceScale, 0),
                  (v) => up((p) => p.copyParticles(turbulenceScale: v)),
                ),
                row(
                  'Evolucao',
                  layer.turbulenceSpeed,
                  0,
                  5,
                  0.01,
                  amNumber(layer.turbulenceSpeed, 2),
                  (v) => up((p) => p.copyParticles(turbulenceSpeed: v)),
                ),
                row(
                  'Giro',
                  layer.spin,
                  -720,
                  720,
                  2,
                  '${amNumber(layer.spin, 0)}°/s',
                  (v) => up((p) => p.copyParticles(spin: v)),
                ),
                titulo('VIDA'),
                chips(
                  'Tamanho',
                  const ['Fixo', 'Cresce', 'Encolhe', 'Sobe e desce'],
                  layer.sizeOverLife,
                  (i) => up((p) => p.copyParticles(sizeOverLife: i)),
                ),
                chips(
                  'Opacidade',
                  const ['Entra e sai', 'Some', 'Aparece', 'Fixa'],
                  layer.opacityOverLife,
                  (i) => up((p) => p.copyParticles(opacityOverLife: i)),
                ),
                row(
                  'Vida aleat.',
                  layer.lifeRandom,
                  0,
                  1,
                  0.003,
                  amNumber(layer.lifeRandom * 100, 0),
                  (v) => up((p) => p.copyParticles(lifeRandom: v)),
                ),
                row(
                  'Tam. aleat.',
                  layer.sizeRandom,
                  0,
                  1,
                  0.003,
                  amNumber(layer.sizeRandom * 100, 0),
                  (v) => up((p) => p.copyParticles(sizeRandom: v)),
                ),
                row(
                  'Opac. aleat.',
                  layer.opacityRandom,
                  0,
                  1,
                  0.003,
                  amNumber(layer.opacityRandom * 100, 0),
                  (v) => up((p) => p.copyParticles(opacityRandom: v)),
                ),
                titulo('APARENCIA'),
                chips(
                  'Forma',
                  const [
                    'Esfera',
                    'Estrela',
                    'Risco',
                    'Nuvem',
                    'Quadrado',
                    'Anel',
                  ],
                  layer.shape,
                  (i) => up((p) => p.copyParticles(shape: i)),
                ),
                row(
                  'Brilho',
                  layer.glow,
                  0,
                  1,
                  0.003,
                  amNumber(layer.glow * 100, 0),
                  (v) => up((p) => p.copyParticles(glow: v)),
                ),
                row(
                  'Rastro',
                  layer.trail,
                  0,
                  1,
                  0.003,
                  amNumber(layer.trail * 100, 0),
                  (v) => up((p) => p.copyParticles(trail: v)),
                ),
                // COR FINAL: a particula muda de cor ao longo da vida.
                Padding(
                  padding: const EdgeInsets.only(bottom: 8),
                  child: Row(
                    children: [
                      const SizedBox(
                        width: 86,
                        child: AppText('Cor final',
                          style: TextStyle(fontSize: 13, color: AmColors.muted),
                        ),
                      ),
                      Tocavel(
                        onTap: () {
                          up((p) => p.copyParticles(clearColorEnd: true));
                          setSheetState(() {});
                        },
                        child: Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 11,
                            vertical: 7,
                          ),
                          decoration: BoxDecoration(
                            color: layer.colorEnd == null
                                ? AmColors.accentDim
                                : AmColors.chip,
                            borderRadius: BorderRadius.circular(9),
                          ),
                          child: const AppText('Nenhuma',
                            style: TextStyle(
                              fontSize: 12,
                              color: AmColors.accent,
                            ),
                          ),
                        ),
                      ),
                      const Spacer(),
                      for (final c in const [
                        Color(0xFFFF3B52),
                        Color(0xFFB8FF3D),
                        Color(0xFF7C62FF),
                        Color(0xFFFFFFFF),
                        Color(0xFFFFB020),
                        Color(0xFF35C4E7),
                      ])
                        Tocavel(
                          onTap: () {
                            up((p) => p.copyParticles(colorEnd: c));
                            setSheetState(() {});
                          },
                          child: Container(
                            width: 26,
                            height: 26,
                            margin: const EdgeInsets.only(left: 7),
                            decoration: BoxDecoration(
                              color: c,
                              shape: BoxShape.circle,
                              border: layer.colorEnd == c
                                  ? Border.all(color: Colors.white, width: 2.5)
                                  : null,
                            ),
                          ),
                        ),
                    ],
                  ),
                ),
                const SizedBox(height: 4),
                Row(
                  children: [
                    const AppText('Cintilar',
                      style: TextStyle(fontSize: 13, color: AmColors.muted),
                    ),
                    Transform.scale(
                      scale: 0.72,
                      child: CupertinoSwitch(
                        value: layer.twinkle,
                        activeTrackColor: AmColors.accent,
                        onChanged: (v) {
                          controller.updateParticles(
                            layerId,
                            (p) => p.copyParticles(twinkle: v),
                          );
                          setSheetState(() {});
                        },
                      ),
                    ),
                    const Spacer(),
                    for (final c in const [
                      Color(0xFFFF3B52),
                      Color(0xFFB8FF3D),
                      Color(0xFF7C62FF),
                      Color(0xFFFFFFFF),
                      Color(0xFFFFB020),
                      Color(0xFF35C4E7),
                    ])
                      Tocavel(
                        onTap: () {
                          controller.updateParticles(
                            layerId,
                            (p) => p.copyParticles(color: c),
                          );
                          setSheetState(() {});
                        },
                        child: Container(
                          width: 30,
                          height: 30,
                          margin: const EdgeInsets.only(left: 8),
                          decoration: BoxDecoration(
                            color: c,
                            shape: BoxShape.circle,
                            border: layer.color == c
                                ? Border.all(color: Colors.white, width: 2.5)
                                : null,
                          ),
                        ),
                      ),
                  ],
                ),
              ],
            ),
          ),
        );
      },
    ),
  );
}

/// Sheet do ELEMENTO 3D: tipo do solido, tamanho, cor e arestas. A
/// rotacao vem do transform normal da camada (X/Y/Z, keyframes e curvas
/// de sempre) — e da cadeia de nulos quando vinculado a um pai.
Future<void> showElement3DSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  final controller = ref.read(editorControllerProvider.notifier);

  await showParamSheet(
    context,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final layer = ref.read(editorControllerProvider).layerById(layerId);
        if (layer is! Element3DLayer) return const SizedBox.shrink();

        return SafeArea(
          child: SingleChildScrollView(
            padding: EdgeInsets.fromLTRB(
              18,
              14,
              18,
              14 + MediaQuery.of(sheetContext).viewInsets.bottom,
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const AppText('Elemento 3D',
                  style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 10),
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: [
                    for (final kind in Element3DKind.values)
                      Tocavel(
                        onTap: () {
                          controller.updateElement3D(
                            layerId,
                            (e) => e.copyElement3D(kind: kind),
                          );
                          setSheetState(() {});
                        },
                        child: Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 12,
                            vertical: 8,
                          ),
                          decoration: BoxDecoration(
                            color: layer.kind == kind
                                ? AmColors.accentDim
                                : AmColors.chip,
                            borderRadius: BorderRadius.circular(9),
                          ),
                          child: AppText(
                            element3DLabel(kind),
                            style: const TextStyle(
                              fontSize: 12,
                              color: AmColors.accent,
                            ),
                          ),
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 12),
                ParameterRow(
                  label: 'Tamanho',
                  value: layer.size,
                  min: 20,
                  max: 600,
                  unitsPerPixel: 1.4,
                  decimals: 0,
                  onChanged: (v) {
                    controller.updateElement3D(
                      layerId,
                      (e) => e.copyElement3D(size: v),
                    );
                    setSheetState(() {});
                  },
                ),
                const SizedBox(height: 8),
                Row(
                  children: [
                    const AppText('Arestas',
                      style: TextStyle(fontSize: 13, color: AmColors.muted),
                    ),
                    Transform.scale(
                      scale: 0.72,
                      child: CupertinoSwitch(
                        value: layer.edges,
                        activeTrackColor: AmColors.accent,
                        onChanged: (v) {
                          controller.updateElement3D(
                            layerId,
                            (e) => e.copyElement3D(edges: v),
                          );
                          setSheetState(() {});
                        },
                      ),
                    ),
                    const Spacer(),
                    // Qualquer cor: espectro, hex e alfa.
                    ColorWell(
                      color: layer.color,
                      size: 30,
                      onChanged: (c) {
                        controller.updateElement3D(
                          layerId,
                          (e) => e.copyElement3D(color: c),
                        );
                        setSheetState(() {});
                      },
                    ),
                    for (final c in const [
                      Color(0xFF7C62FF),
                      Color(0xFFB8FF3D),
                      Color(0xFFFF3B52),
                      Color(0xFFFFFFFF),
                    ])
                      Tocavel(
                        onTap: () {
                          controller.updateElement3D(
                            layerId,
                            (e) => e.copyElement3D(color: c),
                          );
                          setSheetState(() {});
                        },
                        child: Container(
                          width: 30,
                          height: 30,
                          margin: const EdgeInsets.only(left: 8),
                          decoration: BoxDecoration(
                            color: c,
                            shape: BoxShape.circle,
                            border: layer.color == c
                                ? Border.all(color: Colors.white, width: 2.5)
                                : null,
                          ),
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 12),
                // REFLEXO DO AMBIENTE + qual ambiente. E o que faz o
                // solido deixar de parecer plastico fosco.
                ParameterRow(
                  label: 'Reflexo',
                  value: layer.reflect * 100,
                  min: 0,
                  max: 100,
                  unitsPerPixel: 100 / 420,
                  decimals: 0,
                  unit: '%',
                  onChanged: (v) {
                    controller.updateElement3D(
                      layerId,
                      (e) => e.copyElement3D(reflect: v / 100),
                    );
                    setSheetState(() {});
                  },
                ),
                const SizedBox(height: 8),
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: [
                    for (final k in EnvironmentKind.values)
                      Tocavel(
                        onTap: () {
                          controller.updateElement3D(
                            layerId,
                            (e) => e.copyElement3D(environment: k),
                          );
                          setSheetState(() {});
                        },
                        child: Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 12,
                            vertical: 8,
                          ),
                          decoration: BoxDecoration(
                            color: layer.environment == k
                                ? AmColors.accentDim
                                : AmColors.chip,
                            borderRadius: BorderRadius.circular(9),
                          ),
                          child: AppText(
                            environmentLabel(k),
                            style: const TextStyle(
                              fontSize: 12,
                              color: AmColors.accent,
                            ),
                          ),
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 12),
                // MATERIAL: como a luz toca o solido.
                Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const SizedBox(
                      width: 86,
                      child: Padding(
                        padding: EdgeInsets.only(top: 8),
                        child: AppText(
                          'Material',
                          style: TextStyle(fontSize: 13, color: AmColors.muted),
                        ),
                      ),
                    ),
                    Expanded(
                      child: Wrap(
                        spacing: 6,
                        runSpacing: 6,
                        children: [
                          for (final (i, nome) in const [
                            'Solido',
                            'Brilhante',
                            'Vidro',
                            'Metal',
                            'Fosco',
                          ].indexed)
                            Tocavel(
                              onTap: () {
                                controller.updateElement3D(
                                  layerId,
                                  (e) => e.copyElement3D(material: i),
                                );
                                setSheetState(() {});
                              },
                              child: Container(
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 11,
                                  vertical: 7,
                                ),
                                decoration: BoxDecoration(
                                  color: layer.material == i
                                      ? AmColors.accentDim
                                      : AmColors.chip,
                                  borderRadius: BorderRadius.circular(9),
                                ),
                                child: AppText(nome,
                                  style: const TextStyle(
                                    fontSize: 12,
                                    color: AmColors.accent,
                                  ),
                                ),
                              ),
                            ),
                        ],
                      ),
                    ),
                  ],
                ),
                if (layer.material == 1) ...[
                  const SizedBox(height: 10),
                  // DEGRADES do material brilhante.
                  Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const SizedBox(
                        width: 86,
                        child: Padding(
                          padding: EdgeInsets.only(top: 6),
                          child: AppText('Degrade',
                            style: TextStyle(
                              fontSize: 13,
                              color: AmColors.muted,
                            ),
                          ),
                        ),
                      ),
                      Expanded(
                        child: Wrap(
                          spacing: 8,
                          runSpacing: 8,
                          children: [
                            for (final cores in kGlossyGradients)
                              Tocavel(
                                onTap: () {
                                  controller.updateElement3D(
                                    layerId,
                                    (e) => e.copyElement3D(gradient: cores),
                                  );
                                  setSheetState(() {});
                                },
                                child: Container(
                                  width: 54,
                                  height: 30,
                                  decoration: BoxDecoration(
                                    gradient: LinearGradient(colors: cores),
                                    borderRadius: BorderRadius.circular(8),
                                    border: _mesmasCores(layer.gradient, cores)
                                        ? Border.all(
                                            color: Colors.white,
                                            width: 2.5,
                                          )
                                        : null,
                                  ),
                                ),
                              ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ],
                const SizedBox(height: 10),
                ParameterRow(
                  label: 'Brilho',
                  value: layer.shininess * 100,
                  min: 0,
                  max: 100,
                  unitsPerPixel: 0.3,
                  decimals: 0,
                  unit: '%',
                  onChanged: (v) {
                    controller.updateElement3D(
                      layerId,
                      (e) => e.copyElement3D(shininess: v / 100),
                    );
                    setSheetState(() {});
                  },
                ),
                const SizedBox(height: 12),
                // MODELO IMPORTADO: OBJ ou FBX (ASCII) no lugar do solido.
                Row(
                  children: [
                    const SizedBox(
                      width: 86,
                      child: AppText('Modelo',
                        style: TextStyle(fontSize: 13, color: AmColors.muted),
                      ),
                    ),
                    Expanded(
                      child: AppText(
                        _descricaoDoModelo(layer),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    Tocavel(
                      onTap: () async {
                        await _escolherModelo3D(sheetContext, ref, layerId);
                        setSheetState(() {});
                      },
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 12,
                          vertical: 8,
                        ),
                        decoration: BoxDecoration(
                          color: AmColors.chip,
                          borderRadius: BorderRadius.circular(9),
                        ),
                        child: const Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            Icon(
                              CupertinoIcons.cube_box,
                              size: 16,
                              color: AmColors.accent,
                            ),
                            SizedBox(width: 6),
                            AppText('OBJ / FBX',
                              style: TextStyle(
                                fontSize: 12,
                                color: AmColors.accent,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                    if (layer.meshPath != null)
                      Tocavel(
                        onTap: () {
                          controller.updateElement3D(
                            layerId,
                            (e) => e.copyElement3D(clearMesh: true),
                          );
                          setSheetState(() {});
                        },
                        child: const Padding(
                          padding: EdgeInsets.only(left: 8),
                          child: Icon(
                            CupertinoIcons.xmark_circle_fill,
                            size: 20,
                            color: AmColors.muted,
                          ),
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 12),
                // IMAGEM NO SOLIDO: uma foto ou logo vestindo as faces.
                Row(
                  children: [
                    const SizedBox(
                      width: 86,
                      child: AppText(
                        'Imagem',
                        style: TextStyle(fontSize: 13, color: AmColors.muted),
                      ),
                    ),
                    Expanded(
                      child: AppText(
                        layer.imagePath == null
                            ? 'Nenhuma'
                            : layer.imagePath!.split(RegExp(r'[\\/]')).last,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    Tocavel(
                      onTap: () async {
                        final r = await FilePicker.platform.pickFiles(
                          type: FileType.image,
                        );
                        final caminho = r?.files.single.path;
                        if (caminho == null) return;
                        controller.updateElement3D(
                          layerId,
                          (e) => e.copyElement3D(imagePath: caminho),
                        );
                        setSheetState(() {});
                      },
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 12,
                          vertical: 8,
                        ),
                        decoration: BoxDecoration(
                          color: AmColors.chip,
                          borderRadius: BorderRadius.circular(9),
                        ),
                        child: const Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            Icon(
                              CupertinoIcons.photo,
                              size: 16,
                              color: AmColors.accent,
                            ),
                            SizedBox(width: 6),
                            AppText('Escolher',
                              style: TextStyle(
                                fontSize: 12,
                                color: AmColors.accent,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                    if (layer.imagePath != null)
                      Tocavel(
                        onTap: () {
                          controller.updateElement3D(
                            layerId,
                            (e) => e.copyElement3D(clearImage: true),
                          );
                          setSheetState(() {});
                        },
                        child: const Padding(
                          padding: EdgeInsets.only(left: 8),
                          child: Icon(
                            CupertinoIcons.xmark_circle,
                            size: 20,
                            color: AmColors.muted,
                          ),
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 6),
                const AppText('Gire com a rotacao X/Y/Z normal da camada — ou '
                  'vincule a um nulo 3D e gire o nulo.',
                  style: TextStyle(fontSize: 11, color: AmColors.muted),
                ),
              ],
            ),
          ),
        );
      },
    ),
  );
}

/// Sheet FORMA (spec AUREA-parametros-de-forma §4): parametros da
/// GEOMETRIA — Tamanho e parametro do caminho e nao engorda o traco;
/// Escala (em Mover) engorda tudo junto. Todo numero e animavel com
/// diamante e curva.
Future<void> showShapeParamsSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  final controller = ref.read(editorControllerProvider.notifier);

  await showParamSheet(
    context,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) => ValueListenableBuilder<Duration>(
        valueListenable: playback.time,
        builder: (sheetContext, t, _) {
          final layer = ref.read(editorControllerProvider).layerById(layerId);
          if (layer is! ShapeLayer) return const SizedBox.shrink();
          final local = layer.localTime(t);
          ShapeParametric? sp;
          for (final item in layer.contents) {
            if (item is ShapeParametric) {
              sp = item;
              break;
            }
          }

          Widget animRow(
            String label,
            String key,
            double min,
            double max,
            String display, {
            double scale = 1,
          }) {
            final track = shapeParamTrackOf(sp!, key)!;
            // Na lingua nova o diamante mora na linha: toque poe/tira o
            // keyframe; toque LONGO abre a curva (2+ kfs).
            final numero = RegExp(r'^-?[\d.,]+').firstMatch(display);
            final casas =
                RegExp(r'[.,](\d+)$').firstMatch(numero?.group(0) ?? '');
            return ParameterRow(
              label: label,
              value: (track.valueAt(local) * scale).clamp(min, max),
              min: min,
              max: max,
              unitsPerPixel: (max - min) / 420,
              decimals: casas?.group(1)?.length ?? 0,
              unit: numero == null ? '' : display.substring(numero.end),
              keyframe: KeyframeState(
                animated: track.isAnimated,
                here: track.hasKeyframeAt(local),
                onToggle: () {
                  controller.toggleShapeParamKeyframe(layerId, key, t);
                  setSheetState(() {});
                },
                onCurve: track.keyframes.length < 2
                    ? null
                    : () => showTrackCurveSheet(
                        context,
                        ref,
                        playback,
                        label: label,
                        layerId: layerId,
                        trackOf: (l) {
                          if (l is! ShapeLayer) return null;
                          for (final item in l.contents) {
                            if (item is ShapeParametric) {
                              return shapeParamTrackOf(item, key);
                            }
                          }
                          return null;
                        },
                        onSetEase: (segStart, ease) =>
                            controller.setShapeParamSegmentEase(
                              layerId,
                              key,
                              segStart,
                              ease,
                            ),
                        onSetEaseAll: (ease) =>
                            controller.applyEaseToAllShapeParamSegments(
                              layerId,
                              key,
                              ease,
                            ),
                        onClosed: () {
                          if (context.mounted) {
                            showShapeParamsSheet(
                              context,
                              ref,
                              layerId,
                              playback,
                            );
                          }
                        },
                      ),
              ),
              onChanged: (v) {
                controller.editShapeParam(layerId, key, t, v / scale);
                setSheetState(() {});
              },
            );
          }

          return SafeArea(
            child: SingleChildScrollView(
              padding: EdgeInsets.fromLTRB(
                18,
                14,
                18,
                14 + MediaQuery.of(sheetContext).viewInsets.bottom,
              ),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const AppText('Forma — geometria',
                    style: TextStyle(
                      fontSize: 17,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                  SheetTransport(
                    playback: playback,
                    duration: ref.read(editorControllerProvider).duration,
                    fps: ref.read(editorControllerProvider).fps,
                  ),
                  const SizedBox(height: 6),
                  if (layer.contents.any((item) => item is ShapeGradientFill))
                    CupertinoButton(
                      onPressed: () => showGradientFillSheet(
                        context,
                        layerId,
                        playback: playback,
                      ),
                      child: const AppText('Gradiente: cores, posicoes e alcance'),
                    ),
                  // EDITAR NOS: a forma vira caminho bezier (se ainda nao
                  // e) e os nos aparecem sobre o preview. E daqui que sai
                  // o retangulo que vira card: dois keyframes do caminho.
                  SizedBox(
                    width: double.infinity,
                    child: CupertinoButton(
                      color: AmColors.chip,
                      borderRadius: BorderRadius.circular(12),
                      onPressed: () {
                        ShapeItem? geo;
                        for (final i in layer.contents) {
                          if (i is ShapeBezier ||
                              i is ShapePath ||
                              i is ShapeParametric ||
                              i is ShapeSvgPath ||
                              i is ShapeMorph) {
                            geo = i;
                            break;
                          }
                        }
                        if (geo == null) {
                          showReasonToast(
                            context,
                            'Esta forma nao tem geometria',
                          );
                          return;
                        }
                        if (geo is! ShapeBezier &&
                            !controller.convertShapeItemToBezier(
                              layerId,
                              geo.id,
                              t,
                            )) {
                          showReasonToast(
                            context,
                            'Nao consegui converter esta geometria',
                          );
                          return;
                        }
                        final idGeo = geo.id;
                        closeParamSheet(sheetContext);
                        Future.microtask(() {
                          if (context.mounted) {
                            showPathEditSheet(
                              context,
                              ref,
                              layerId,
                              idGeo,
                              playback,
                              forma: true,
                            );
                          }
                        });
                      },
                      child: const Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(
                            CupertinoIcons.pencil_outline,
                            size: 17,
                            color: AmColors.accent,
                          ),
                          SizedBox(width: 8),
                          AppText('Editar nos do caminho',
                            style: TextStyle(
                              fontSize: 14,
                              fontWeight: FontWeight.w600,
                              color: AmColors.accent,
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(height: 10),
                  if (sp == null) ...[
                    const AppText('Esta forma e um caminho desenhado (sem '
                      'parametros). Converta para editar Tamanho, '
                      'Arredondamento, Pontas e afins — animaveis.',
                      style: TextStyle(fontSize: 13, color: AmColors.muted),
                    ),
                    const SizedBox(height: 10),
                    SizedBox(
                      width: double.infinity,
                      child: CupertinoButton(
                        color: AmColors.accent,
                        borderRadius: BorderRadius.circular(12),
                        onPressed: () {
                          controller.convertShapeToParametric(layerId);
                          if (controller.shapeParametricOf(layerId) == null) {
                            showReasonToast(
                              context,
                              'Esta forma nao tem equivalente parametrico',
                            );
                          }
                          setSheetState(() {});
                        },
                        child: const AppText('Converter para parametrica',
                          style: TextStyle(
                            fontSize: 15,
                            fontWeight: FontWeight.w700,
                            color: Color(0xFF0B0E12),
                          ),
                        ),
                      ),
                    ),
                  ] else ...[
                    Wrap(
                      spacing: 8,
                      runSpacing: 8,
                      children: [
                        for (final (label, kind) in const [
                          ('Retangulo', ParamShapeKind.rect),
                          ('Elipse', ParamShapeKind.ellipse),
                          ('Poligono', ParamShapeKind.polygon),
                          ('Estrela', ParamShapeKind.star),
                          ('Setor', ParamShapeKind.sector),
                        ])
                          Tocavel(
                            onTap: () {
                              controller.setShapeParamKind(layerId, kind);
                              setSheetState(() {});
                            },
                            child: Container(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 12,
                                vertical: 8,
                              ),
                              decoration: BoxDecoration(
                                color: sp.kind == kind
                                    ? AmColors.accentDim
                                    : AmColors.chip,
                                borderRadius: BorderRadius.circular(9),
                              ),
                              child: AppText(
                                label,
                                style: const TextStyle(
                                  fontSize: 12,
                                  color: AmColors.accent,
                                ),
                              ),
                            ),
                          ),
                      ],
                    ),
                    const SizedBox(height: 10),
                    if (sp.kind == ParamShapeKind.rect ||
                        sp.kind == ParamShapeKind.ellipse) ...[
                      animRow(
                        'Tamanho X',
                        'sizeX',
                        4,
                        1000,
                        amNumber(sp.sizeX.valueAt(local), 0),
                      ),
                      animRow(
                        'Tamanho Y',
                        'sizeY',
                        4,
                        1000,
                        amNumber(sp.sizeY.valueAt(local), 0),
                      ),
                    ],
                    if (sp.kind == ParamShapeKind.rect) ...[
                      animRow(
                        'Arredond.',
                        'roundness',
                        0,
                        sp.roundnessPercent ? 100 : 300,
                        amNumber(sp.roundness.valueAt(local), 0),
                      ),
                      Row(
                        children: [
                          const AppText('Unidade do canto',
                            style: TextStyle(
                              fontSize: 12,
                              color: AmColors.muted,
                            ),
                          ),
                          const SizedBox(width: 10),
                          for (final (label, pct) in const [
                            ('% do lado', true),
                            ('px fixo', false),
                          ])
                            Padding(
                              padding: const EdgeInsets.only(right: 8),
                              child: Tocavel(
                                onTap: () {
                                  controller.setShapeRoundnessUnit(
                                    layerId,
                                    percent: pct,
                                  );
                                  setSheetState(() {});
                                },
                                child: Container(
                                  padding: const EdgeInsets.symmetric(
                                    horizontal: 10,
                                    vertical: 6,
                                  ),
                                  decoration: BoxDecoration(
                                    color: sp.roundnessPercent == pct
                                        ? AmColors.accentDim
                                        : AmColors.chip,
                                    borderRadius: BorderRadius.circular(8),
                                  ),
                                  child: AppText(
                                    label,
                                    style: const TextStyle(
                                      fontSize: 11,
                                      color: AmColors.accent,
                                    ),
                                  ),
                                ),
                              ),
                            ),
                        ],
                      ),
                      const SizedBox(height: 6),
                    ],
                    if (sp.kind == ParamShapeKind.polygon ||
                        sp.kind == ParamShapeKind.star) ...[
                      animRow(
                        'Pontas',
                        'points',
                        2,
                        16,
                        amNumber(sp.points.valueAt(local), 1),
                      ),
                      animRow(
                        'Raio',
                        'outerRadius',
                        10,
                        600,
                        amNumber(sp.outerRadius.valueAt(local), 0),
                      ),
                      animRow(
                        'Arred. ext',
                        'outerRoundness',
                        -100,
                        200,
                        amNumber(sp.outerRoundness.valueAt(local), 0),
                      ),
                      animRow(
                        'Rotacao',
                        'shapeRotation',
                        -180,
                        180,
                        '${amNumber(sp.shapeRotation.valueAt(local), 0)}°',
                      ),
                    ],
                    if (sp.kind == ParamShapeKind.star) ...[
                      animRow(
                        'Raio int',
                        'innerRadius',
                        0,
                        600,
                        amNumber(sp.innerRadius.valueAt(local), 0),
                      ),
                      animRow(
                        'Arred. int',
                        'innerRoundness',
                        -100,
                        200,
                        amNumber(sp.innerRoundness.valueAt(local), 0),
                      ),
                    ],
                    if (sp.kind == ParamShapeKind.sector) ...[
                      animRow(
                        'Raio',
                        'outerRadius',
                        10,
                        600,
                        amNumber(sp.outerRadius.valueAt(local), 0),
                      ),
                      animRow(
                        'Raio int',
                        'sectorInner',
                        0,
                        600,
                        amNumber(sp.sectorInner.valueAt(local), 0),
                      ),
                      animRow(
                        'Ang. inicial',
                        'startAngle',
                        -180,
                        360,
                        '${amNumber(sp.startAngle.valueAt(local), 0)}°',
                      ),
                      animRow(
                        'Varredura',
                        'sweep',
                        0,
                        360,
                        '${amNumber(sp.sweep.valueAt(local), 0)}°',
                      ),
                    ],
                    const AppText('Tamanho muda a GEOMETRIA (traco constante). '
                      'Escala, em Mover, engorda tudo junto.',
                      style: TextStyle(fontSize: 11, color: AmColors.muted),
                    ),
                  ],
                ],
              ),
            ),
          );
        },
      ),
    ),
  );
}

/// Editor de LEGENDAS: lista de cues com texto corrigivel em linha
/// (editar trava o cue — retranscrever nao sobrescreve), toque no tempo
/// para dar seek e ouvir, e X para descartar um cue errado. Sheet
/// persistente: o preview segue visivel enquanto voce revisa.
Future<void> showCaptionCuesSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  // Um TextEditingController ESTAVEL por cue: o sheet pode reconstruir
  // (setSheetState) sem perder cursor nem texto digitado.
  final editors = <String, TextEditingController>{};

  await showParamSheet(
    context,
    heightFactor: 0.55,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final layer = project.layerById(layerId);
        if (layer is! CaptionLayer) return const SizedBox.shrink();
        final cues = layer.cues;

        TextEditingController editorOf(Cue c) => editors.putIfAbsent(c.id, () {
          final e = TextEditingController(text: c.text.replaceAll('\n', ' '));
          // So grava quando o TEXTO muda (o listener tambem dispara
          // por cursor/selecao — isso nao pode travar o cue).
          var last = e.text;
          e.addListener(() {
            if (e.text == last) return;
            last = e.text;
            controller.updateCueText(layerId, c.id, e.text);
          });
          return e;
        });

        return SafeArea(
          child: Padding(
            padding: EdgeInsets.fromLTRB(
              18,
              12,
              18,
              10 + MediaQuery.of(sheetContext).viewInsets.bottom,
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Expanded(
                      child: AppText(
                        'Legendas — ${cues.length} cues',
                        style: const TextStyle(
                          fontSize: 16,
                          fontWeight: FontWeight.w700,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    // ESTILO, visivel no cabecalho do editor de legendas.
                    // O estilo Destaque (nivel 12.1) mora aqui, com as
                    // tres profundidades.
                    Tocavel(
                      behavior: HitTestBehavior.opaque,
                      onTap: () {
                        closeParamSheet(sheetContext);
                        Future.microtask(() {
                          if (context.mounted) {
                            showCaptionStyleSheet(context, ref, layerId);
                          }
                        });
                      },
                      child: Container(
                        height: 30,
                        padding: const EdgeInsets.symmetric(horizontal: 11),
                        alignment: Alignment.center,
                        decoration: BoxDecoration(
                          color: layer.highlight.ativo
                              ? AmColors.accentDim
                              : AmColors.chip,
                          borderRadius: BorderRadius.circular(9),
                        ),
                        child: AppText('Estilo',
                          style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: layer.highlight.ativo
                                ? AmColors.accent
                                : AmColors.text,
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
                SheetTransport(
                  playback: playback,
                  duration: project.duration,
                  fps: project.fps,
                ),
                const SizedBox(height: 4),
                const AppText('Toque no tempo para ouvir o trecho; corrija o texto '
                  'direto. Editar trava o cue.',
                  style: TextStyle(fontSize: 11, color: AmColors.muted),
                ),
                const SizedBox(height: 6),
                Expanded(
                  child: cues.isEmpty
                      ? const Center(
                          child: AppText('Sem cues nesta camada.',
                            style: TextStyle(
                              fontSize: 13,
                              color: AmColors.muted,
                            ),
                          ),
                        )
                      : ListView.builder(
                          itemCount: cues.length,
                          itemBuilder: (context, i) {
                            final c = cues[i];
                            return Padding(
                              padding: const EdgeInsets.only(bottom: 6),
                              child: Row(
                                crossAxisAlignment: CrossAxisAlignment.center,
                                children: [
                                  Tocavel(
                                    onTap: () => playback.seek(
                                      layer.startTime + c.start,
                                    ),
                                    child: Container(
                                      width: 74,
                                      padding: const EdgeInsets.symmetric(
                                        vertical: 8,
                                      ),
                                      alignment: Alignment.center,
                                      decoration: BoxDecoration(
                                        color: AmColors.chip,
                                        borderRadius: BorderRadius.circular(8),
                                      ),
                                      child: AppText(
                                        formatTimecode(c.start, project.fps),
                                        style: TextStyle(
                                          fontSize: 11,
                                          color: c.locked
                                              ? AmColors.accent
                                              : AmColors.muted,
                                        ),
                                      ),
                                    ),
                                  ),
                                  const SizedBox(width: 8),
                                  Expanded(
                                    child: CupertinoTextField(
                                      controller: editorOf(c),
                                      maxLines: 1,
                                      style: const TextStyle(
                                        fontSize: 14,
                                        color: AmColors.text,
                                      ),
                                      padding: const EdgeInsets.symmetric(
                                        horizontal: 10,
                                        vertical: 8,
                                      ),
                                      decoration: BoxDecoration(
                                        color: AmColors.chip,
                                        borderRadius: BorderRadius.circular(8),
                                      ),
                                    ),
                                  ),
                                  CupertinoButton(
                                    padding: const EdgeInsets.only(left: 6),
                                    onPressed: () {
                                      controller.removeCue(layerId, c.id);
                                      editors.remove(c.id)?.dispose();
                                      setSheetState(() {});
                                    },
                                    child: const Icon(
                                      CupertinoIcons.xmark_circle,
                                      size: 20,
                                      color: AmColors.muted,
                                    ),
                                  ),
                                ],
                              ),
                            );
                          },
                        ),
                ),
              ],
            ),
          ),
        );
      },
    ),
  );
  for (final e in editors.values) {
    e.dispose();
  }
}

/// Icone PEQUENO de utilidade (fileira de cima do menu da camada): acao
/// rapida ou sheet que nao troca a pagina do editor. Nao entra na grade
/// porque nao e editor de propriedade. Sem bolha, sem ripple: so o icone
/// num alvo de 40 px; `aceso` sinaliza estado ligado (ex.: mudo ativo).
class _MenuTile extends StatelessWidget {
  const _MenuTile({
    required this.icon,
    required this.label,
    required this.onTap,
    this.height = 68,
    this.badge,
  });

  final IconData icon;
  final String label;
  final VoidCallback onTap;
  final double height;
  final String? badge;

  @override
  Widget build(BuildContext context) {
    return Expanded(
      child: Tocavel(
        onTap: () {
          HapticFeedback.lightImpact();
          onTap();
        },
        child: Container(
          height: height,
          padding: EdgeInsets.symmetric(
            horizontal: 4,
            vertical: height < 65 ? 2 : 4,
          ),
          decoration: BoxDecoration(
            color: const Color(0xFF222634),
            borderRadius: BorderRadius.circular(10),
          ),
          child: Stack(
            children: [
              Center(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Icon(
                      icon,
                      size: height < 65 ? 18 : 21,
                      color: const Color(0xFFD4D8E2),
                    ),
                    SizedBox(height: height < 65 ? 2 : 4),
                    // TRES LINHAS: em tres colunas a ficha e estreita, e
                    // "Movimentacao e transformacao" nao cabe em duas —
                    // saia cortado com reticencias.
                    AppText(
                      label,
                      textAlign: TextAlign.center,
                      maxLines: 3,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        fontSize: height < 65 ? 9.5 : 10.5,
                        height: 1.12,
                        fontWeight: FontWeight.w500,
                        color: const Color(0xFFD4D8E2),
                      ),
                    ),
                  ],
                ),
              ),
              if (badge != null)
                Positioned(
                  top: 0,
                  right: 0,
                  child: Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 5,
                      vertical: 1.5,
                    ),
                    decoration: BoxDecoration(
                      color: const Color(0xFFFFD600),
                      borderRadius: BorderRadius.circular(4),
                    ),
                    child: AppText(
                      badge!,
                      style: const TextStyle(
                        color: Colors.black,
                        fontSize: 8.5,
                        fontWeight: FontWeight.w900,
                      ),
                    ),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Blend modes oferecidos (familias estilo compositor classico).
const amBlendModes = <(String, BlendMode)>[
  ('Normal', BlendMode.srcOver),
  ('Escurecer', BlendMode.darken),
  ('Multiplicar', BlendMode.multiply),
  ('Color Burn', BlendMode.colorBurn),
  ('Clarear', BlendMode.lighten),
  ('Tela', BlendMode.screen),
  ('Color Dodge', BlendMode.colorDodge),
  ('Adicionar', BlendMode.plus),
  ('Sobrepor', BlendMode.overlay),
  ('Luz suave', BlendMode.softLight),
  ('Luz forte', BlendMode.hardLight),
  ('Diferenca', BlendMode.difference),
  ('Exclusao', BlendMode.exclusion),
  ('Matiz', BlendMode.hue),
  ('Saturacao', BlendMode.saturation),
  ('Cor', BlendMode.color),
  ('Luminosidade', BlendMode.luminosity),
];

/// UM MODO DE MESCLA: nativo do Flutter ou proprio da Aurea.
class ModoDeMescla {
  const ModoDeMescla(this.rotulo, {this.nativo, this.aurea});

  final String rotulo;
  final BlendMode? nativo;
  final AureaBlend? aurea;
}

/// AS SETE CATEGORIAS DA MESCLAGEM (v1.1.1), com todos os modos que o
/// motor sabe fazer — os nativos e os da Aurea lado a lado.
const categoriasDeMescla = <({String nome, List<ModoDeMescla> modos})>[
  (
    nome: 'Normal',
    modos: [
      ModoDeMescla('Normal', nativo: BlendMode.srcOver),
      ModoDeMescla('Dissolver', aurea: AureaBlend.dissolve),
    ],
  ),
  (
    nome: 'Escurecer',
    modos: [
      ModoDeMescla('Escurecer', nativo: BlendMode.darken),
      ModoDeMescla('Multiplicar', nativo: BlendMode.multiply),
      ModoDeMescla('Queimar cor', nativo: BlendMode.colorBurn),
      ModoDeMescla('Queimar linear', aurea: AureaBlend.linearBurn),
      ModoDeMescla('Cor mais escura', aurea: AureaBlend.darkerColor),
    ],
  ),
  (
    nome: 'Clarear',
    modos: [
      ModoDeMescla('Clarear', nativo: BlendMode.lighten),
      ModoDeMescla('Tela', nativo: BlendMode.screen),
      ModoDeMescla('Subexpor cor', nativo: BlendMode.colorDodge),
      ModoDeMescla('Adicionar', nativo: BlendMode.plus),
      ModoDeMescla('Cor mais clara', aurea: AureaBlend.lighterColor),
    ],
  ),
  (
    nome: 'Contraste',
    modos: [
      ModoDeMescla('Sobrepor', nativo: BlendMode.overlay),
      ModoDeMescla('Luz suave', nativo: BlendMode.softLight),
      ModoDeMescla('Luz forte', nativo: BlendMode.hardLight),
      ModoDeMescla('Luz viva', aurea: AureaBlend.vividLight),
      ModoDeMescla('Luz linear', aurea: AureaBlend.linearLight),
      ModoDeMescla('Luz pontual', aurea: AureaBlend.pinLight),
      ModoDeMescla('Mistura dura', aurea: AureaBlend.hardMix),
    ],
  ),
  (
    nome: 'Diferença',
    modos: [
      ModoDeMescla('Diferença', nativo: BlendMode.difference),
      ModoDeMescla('Exclusão', nativo: BlendMode.exclusion),
      ModoDeMescla('Subtrair', aurea: AureaBlend.subtract),
      ModoDeMescla('Dividir', aurea: AureaBlend.divide),
    ],
  ),
  (
    nome: 'Cor',
    modos: [
      ModoDeMescla('Matiz', nativo: BlendMode.hue),
      ModoDeMescla('Saturação', nativo: BlendMode.saturation),
      ModoDeMescla('Cor', nativo: BlendMode.color),
      ModoDeMescla('Luminosidade', nativo: BlendMode.luminosity),
    ],
  ),
  (
    nome: 'Máscara',
    modos: [
      ModoDeMescla('Máscara', nativo: BlendMode.dstIn),
      ModoDeMescla('Recortar', nativo: BlendMode.dstOut),
    ],
  ),
];

/// As categorias abertas na mesclagem. Nulo = a do modo atual.
final _mesclaAbertasProvider = StateProvider.autoDispose<Set<String>?>(
  (ref) => null,
);

/// OS SEIS MODOS DO SIMPLES (secao 2 do plano): o resto e Pro.
const kBlendModesSimples = <BlendMode>{
  BlendMode.srcOver,
  BlendMode.multiply,
  BlendMode.screen,
  BlendMode.overlay,
  BlendMode.plus,
  BlendMode.softLight,
};

/// Uma opcao de mescla na fileira, com MINIATURA: dois discos, o de
/// cima composto com o modo — ve-se o que o modo faz antes de tocar.
class _BlendChip extends StatelessWidget {
  const _BlendChip({
    super.key,
    required this.label,
    required this.aceso,
    required this.onTap,
    this.mode,
  });

  final String label;
  final bool aceso;
  final VoidCallback onTap;

  /// Modo nativo para a miniatura; null = modo Aurea (desenha o padrao).
  final BlendMode? mode;

  @override
  Widget build(BuildContext context) => Tocavel(
    onTap: onTap,
    child: Container(
      width: 74,
      margin: const EdgeInsets.only(right: 8),
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(10),
        border: aceso ? Border.all(color: AmColors.accent, width: 2) : null,
      ),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          CustomPaint(
            size: const Size(34, 22),
            painter: _MesclaMiniatura(mode ?? BlendMode.srcOver),
          ),
          const SizedBox(height: 4),
          AppText(
            label,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(
              fontSize: 9.5,
              color: aceso ? AmColors.accent : AmColors.text,
            ),
          ),
        ],
      ),
    ),
  );
}

/// A miniatura de um modo de mescla: disco laranja embaixo, disco azul
/// em cima composto com o modo.
class _MesclaMiniatura extends CustomPainter {
  const _MesclaMiniatura(this.mode);

  final BlendMode mode;

  @override
  void paint(Canvas canvas, Size size) {
    final r = size.height * 0.46;
    canvas.saveLayer(Offset.zero & size, Paint());
    canvas.drawCircle(
      Offset(size.width * 0.38, size.height / 2),
      r,
      Paint()..color = const Color(0xFFFF8A3D),
    );
    canvas.drawCircle(
      Offset(size.width * 0.62, size.height / 2),
      r,
      Paint()
        ..color = const Color(0xFF3D9BFF)
        ..blendMode = mode,
    );
    canvas.restore();
  }

  @override
  bool shouldRepaint(_MesclaMiniatura old) => old.mode != mode;
}

enum _BlendTab { opacity, blending, mask, matte }

enum _N4Depth { pronto, montar, avancado }

final _blendTabProvider = StateProvider<_BlendTab>((_) => _BlendTab.opacity);
final _maskDepthProvider = StateProvider<_N4Depth>((_) => _N4Depth.pronto);
final _matteDepthProvider = StateProvider<_N4Depth>((_) => _N4Depth.pronto);

/// Leva a navegacao por um keyframe de mascara para a aba correspondente.
void selectMaskInBlendingPanel(WidgetRef ref) {
  ref.read(_blendTabProvider.notifier).state = _BlendTab.mask;
}

/// Opacidade, mesclagem, mascara e recorte dentro da mesma secao da grade.
class BlendingPanel extends ConsumerStatefulWidget {
  const BlendingPanel({
    super.key,
    required this.playback,
    required this.onBack,
    required this.onOpenCurve,
    required this.onEditMaskPoints,
  });

  final PlaybackController playback;
  final VoidCallback onBack;
  final void Function(LayerProp prop) onOpenCurve;
  final void Function(String maskId) onEditMaskPoints;

  @override
  ConsumerState<BlendingPanel> createState() => _BlendingPanelState();
}

class _BlendingPanelState extends ConsumerState<BlendingPanel> {
  @override
  Widget build(BuildContext context) {
    final project = ref.watch(projetoVisivelProvider);
    final id = ref.watch(selectedLayerProvider);
    final layer = id == null ? null : project.layerById(id);
    if (layer == null || id == null) {
      return const ColoredBox(color: AmColors.panel);
    }
    final realLayer =
        ref.watch(editorControllerProvider).layerById(id) ?? layer;
    final controller = ref.read(editorControllerProvider.notifier);

    // Escuta o relogio: `t` sempre atual (keyframe cai no playhead real).
    return ValueListenableBuilder<Duration>(
      valueListenable: widget.playback.time,
      builder: (context, t, _) {
        final local = layer.localTime(t);
        final opacity = layer.opacity.valueAt(local);

        final tab = ref.watch(_blendTabProvider);
        final mask = layer.masks.isEmpty ? null : layer.masks.last;
        final realMask = realLayer.masks.isEmpty ? null : realLayer.masks.last;
        final pathAnimated = realMask?.path.isAnimated ?? false;
        final pathKf = realMask?.path.hasKeyframeAt(local) ?? false;

        return ColoredBox(
          color: AmColors.panel,
          child: Row(
            children: [
              Column(
                children: [
                  AmRailButton(
                    onTap: () {
                      if (tab == _BlendTab.opacity) {
                        controller.toggleKeyframe(id, t, LayerProp.opacity);
                      } else if (tab == _BlendTab.mask && mask != null) {
                        controller.toggleMaskPathKeyframe(id, mask.id, t);
                      } else {
                        showReasonToast(
                          context,
                          tab == _BlendTab.mask
                              ? 'Crie uma mascara primeiro'
                              : 'Este modo nao e animavel',
                        );
                      }
                    },
                    child: AmDiamondAdd(
                      active: tab == _BlendTab.opacity
                          ? realLayer.opacity.isAnimated
                          : tab == _BlendTab.mask && pathAnimated,
                      filled: tab == _BlendTab.opacity
                          ? realLayer.opacity.hasKeyframeAt(local)
                          : tab == _BlendTab.mask && pathKf,
                    ),
                  ),
                  AmRailButton(
                    onTap: tab == _BlendTab.opacity && layer.opacity.isAnimated
                        ? () => widget.onOpenCurve(LayerProp.opacity)
                        : null,
                    child: AmCurveIcon(
                      color:
                          tab == _BlendTab.opacity && layer.opacity.isAnimated
                          ? AmColors.text
                          : AmColors.muted,
                    ),
                  ),
                ],
              ),
              Expanded(
                child: Column(
                  children: [
                    AmParamTabs(
                      abas: [
                        ParamTab(
                          id: _BlendTab.opacity.name,
                          label: 'Opacidade',
                          animated: layer.opacity.isAnimated,
                        ),
                        ParamTab(
                          id: _BlendTab.blending.name,
                          label: 'Mesclagem',
                        ),
                        ParamTab(
                          id: _BlendTab.mask.name,
                          label: 'Máscara',
                          animated: layer.masks.any((m) => m.hasAnimation),
                        ),
                        ParamTab(
                          id: _BlendTab.matte.name,
                          label: 'Recorte por camada',
                        ),
                      ],
                      ativa: tab.name,
                      onAba: (v) => ref.read(_blendTabProvider.notifier).state =
                          _BlendTab.values.firstWhere((x) => x.name == v),
                    ),
                    Expanded(
                      child: switch (tab) {
                        _BlendTab.opacity => _opacity(
                          controller,
                          id,
                          layer,
                          opacity,
                          t,
                          local,
                        ),
                        _BlendTab.blending => _blending(controller, id, layer),
                        _BlendTab.mask => _mask(
                          context,
                          controller,
                          id,
                          layer,
                          t,
                          local,
                        ),
                        _BlendTab.matte => _matte(
                          context,
                          controller,
                          id,
                          layer,
                        ),
                      },
                    ),
                  ],
                ),
              ),
            ],
          ),
        );
      },
    );
  }

  Widget _opacity(
    EditorController c,
    String id,
    Layer layer,
    double value,
    Duration t,
    Duration local,
  ) => Padding(
    padding: const EdgeInsets.fromLTRB(2, 6, 10, 10),
    child: Column(
      children: [
        ParameterRow(
          label: 'Opacidade',
          value: value * 100,
          min: 0,
          max: 100,
          unitsPerPixel: 0.35,
          decimals: 0,
          unit: '%',
          valueKey: const ValueKey('opacidade-valor'),
          keyframe: KeyframeState(
            animated: layer.opacity.isAnimated,
            here: layer.opacity.hasKeyframeAt(local),
            onToggle: () => c.toggleKeyframe(id, t, LayerProp.opacity),
            onCurve: layer.opacity.isAnimated
                ? () => widget.onOpenCurve(LayerProp.opacity)
                : null,
          ),
          onReset: () => c.resetProp(id, LayerProp.opacity),
          // EXPOR NO PROJETO (item que faltava do plano de redesign): a
          // lista de ⚙ Propriedades expostas so removia; agora o toque
          // longo no nome poe.
          onExpose: () {
            c.exposeProperty(
              ExposedProperty(
                id: 'expor-$id-opacity',
                layerId: id,
                property: 'opacity',
                label: '${layer.name} · Opacidade',
                min: 0,
                max: 1,
              ),
            );
            AureaSnack.show(
              context,
              'Opacidade exposta em ⚙ Propriedades do projeto',
            );
          },
          onChanged: (v) => c.editOpacity(id, t, v / 100),
        ),
        const SizedBox(height: 6),
        Expanded(
          child: AmTickRuler(
            value: value * 100,
            min: 0,
            max: 100,
            unitsPerPixel: 0.35,
            height: double.infinity,
            onChanged: (v) => c.editOpacity(id, t, v / 100),
          ),
        ),
      ],
    ),
  );

  /// A MESCLAGEM EM CATEGORIAS: normal, escurecer, clarear, contraste,
  /// diferenca, cor e mascara. Cada cabecalho diz qual modo esta ligado
  /// ali dentro; a categoria do modo atual nasce aberta.
  Widget _blending(EditorController c, String id, Layer layer) {
    bool ligado(ModoDeMescla m) => m.nativo != null
        ? layer.customBlend == null && layer.blendMode == m.nativo
        : layer.customBlend == m.aurea;
    final atual = [
      for (final cat in categoriasDeMescla)
        for (final m in cat.modos)
          if (ligado(m)) (cat, m),
    ].firstOrNull;
    final abertas = ref.watch(_mesclaAbertasProvider) ??
        {if (atual != null) atual.$1.nome};
    return ListView(
      key: const ValueKey('mescla-categorias'),
      padding: const EdgeInsets.fromLTRB(8, 4, 12, 12),
      children: [
        for (final cat in categoriasDeMescla) ...[
          Tocavel(
            key: ValueKey('categoria-mescla-${cat.nome}'),
            onTap: () {
              final novo = {...abertas};
              if (!novo.remove(cat.nome)) novo.add(cat.nome);
              ref.read(_mesclaAbertasProvider.notifier).state = novo;
            },
            child: SizedBox(
              height: 40,
              child: Row(
                children: [
                  Icon(
                    abertas.contains(cat.nome)
                        ? CupertinoIcons.chevron_down
                        : CupertinoIcons.chevron_right,
                    size: 13,
                    color: AmColors.muted,
                  ),
                  const SizedBox(width: 8),
                  AppText(
                    cat.nome,
                    style: const TextStyle(
                      fontSize: 13.5,
                      fontWeight: FontWeight.w600,
                      color: AmColors.text,
                    ),
                  ),
                  const Spacer(),
                  if (atual != null && atual.$1 == cat) ...[
                    AppText(
                      atual.$2.rotulo,
                      style: const TextStyle(
                        fontSize: 12,
                        color: AmColors.accent,
                      ),
                    ),
                    const SizedBox(width: 6),
                    const Icon(
                      CupertinoIcons.checkmark_circle_fill,
                      size: 16,
                      color: AmColors.accent,
                    ),
                  ],
                ],
              ),
            ),
          ),
          if (abertas.contains(cat.nome))
            Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  for (final m in cat.modos)
                    _BlendChip(
                      key: ValueKey(
                        'mescla-${m.nativo?.name ?? m.aurea!.name}',
                      ),
                      label: m.rotulo,
                      mode: m.nativo,
                      aceso: ligado(m),
                      onTap: () => m.nativo != null
                          ? c.setBlendMode(id, m.nativo!)
                          : c.setCustomBlend(id, m.aurea),
                    ),
                ],
              ),
            ),
        ],
        const SizedBox(height: 4),
        // A MESCLA AGE SOBRE O QUE ESTA POR BAIXO. Sem isto escrito, uma
        // camada sozinha em Multiplicar (que some no preto) ou em Tela
        // (que nao muda) vira "a mesclagem nao funciona" — o relato do
        // beta. A conta esta certa; faltava dizer com o que ela conta.
        const AppText('A mescla combina esta camada com as camadas abaixo. Branco em '
          'Clarear cobre a imagem; em Escurecer deixa a imagem aparecer. '
          'Ajuste também a opacidade para reduzir a intensidade.',
          maxLines: 3,
          overflow: TextOverflow.ellipsis,
          style: TextStyle(fontSize: 11, height: 1.3, color: AmColors.muted),
        ),
      ],
    );
  }

  Widget _depth(_N4Depth current, ValueChanged<_N4Depth> set) => Row(
    children: [
      Expanded(
        child: _button(
          'Montar',
          () => set(
            current == _N4Depth.montar ? _N4Depth.pronto : _N4Depth.montar,
          ),
          selected: current == _N4Depth.montar,
        ),
      ),
      const SizedBox(width: 8),
      Expanded(
        child: _button(
          'Avançado',
          () => set(
            current == _N4Depth.avancado ? _N4Depth.pronto : _N4Depth.avancado,
          ),
          selected: current == _N4Depth.avancado,
        ),
      ),
    ],
  );

  Widget _button(String text, VoidCallback onTap, {bool selected = false}) =>
      Tocavel(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 9),
          decoration: BoxDecoration(
            color: selected ? AmColors.accentDim : AmColors.chip,
            borderRadius: BorderRadius.circular(9),
            border: selected ? Border.all(color: AmColors.accent) : null,
          ),
          alignment: Alignment.center,
          child: AppText(
            text,
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: selected ? AmColors.accent : AmColors.text,
            ),
          ),
        ),
      );

  Size _maskSize(EditorController c, Layer layer, Duration t) {
    final neutral = layer.copyLayer(
      scaleX: AnimatedDouble(1),
      scaleY: AnimatedDouble(1),
    );
    final size = c.layerBoxSize(neutral, t);
    return Size(
      size.width > 1 ? size.width : 460,
      size.height > 1 ? size.height : 460,
    );
  }

  LayerMask _addRect(EditorController c, String id, Layer layer, Duration t) {
    final size = _maskSize(c, layer, t);
    final mask = LayerMask(
      name: 'Retangulo',
      path: AnimatedPath(BezierPath.rect(size.width, size.height)),
    );
    c.addMask(id, mask);
    return mask;
  }

  void _setMaskDepth(
    _N4Depth next,
    EditorController c,
    String id,
    Layer layer,
    Duration t,
  ) {
    if (next == _N4Depth.montar && layer.masks.isEmpty) {
      _addRect(c, id, layer, t);
    }
    ref.read(_maskDepthProvider.notifier).state = next;
  }

  Widget _mask(
    BuildContext context,
    EditorController c,
    String id,
    Layer layer,
    Duration t,
    Duration local,
  ) {
    final depth = ref.watch(_maskDepthProvider);
    if (depth == _N4Depth.avancado) {
      return Padding(
        padding: const EdgeInsets.fromLTRB(10, 8, 14, 12),
        child: Column(
          children: [
            _depth(depth, (d) {
              _setMaskDepth(d, c, id, layer, t);
              if (d == _N4Depth.avancado) {
                showMasksSheet(
                  context,
                  ref,
                  id,
                  widget.playback,
                  onEditMaskPoints: widget.onEditMaskPoints,
                );
              }
            }),
            const SizedBox(height: 14),
            const AppText('Sete modos, pilha, feather X/Y, opacidade e caminho.',
              style: TextStyle(fontSize: 12, color: AmColors.muted),
            ),
            const SizedBox(height: 12),
            _button(
              'Abrir pilha de máscaras',
              () => showMasksSheet(
                context,
                ref,
                id,
                widget.playback,
                onEditMaskPoints: widget.onEditMaskPoints,
              ),
            ),
          ],
        ),
      );
    }
    if (depth == _N4Depth.pronto) {
      return ListView(
        padding: const EdgeInsets.fromLTRB(10, 7, 14, 12),
        children: [
          const AppText('Pronto · Revelar',
            style: TextStyle(
              fontSize: 14,
              fontWeight: FontWeight.w700,
              color: AmColors.text,
            ),
          ),
          const SizedBox(height: 4),
          const AppText('Um toque cria mascara e keyframes reais com mola.',
            style: TextStyle(fontSize: 12, color: AmColors.muted),
          ),
          const SizedBox(height: 9),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (final preset in MaskRevealPreset.values)
                _button(
                  preset == MaskRevealPreset.iris ? 'Íris' : preset.label,
                  () => c.applyMaskReveal(id, preset, t),
                ),
            ],
          ),
          const SizedBox(height: 10),
          _depth(depth, (d) {
            _setMaskDepth(d, c, id, layer, t);
            if (d == _N4Depth.avancado) {
              showMasksSheet(
                context,
                ref,
                id,
                widget.playback,
                onEditMaskPoints: widget.onEditMaskPoints,
              );
            }
          }),
        ],
      );
    }
    final mask = layer.masks.isEmpty ? null : layer.masks.last;
    if (mask == null) return const SizedBox.shrink();
    final size = _maskSize(c, layer, t);
    return ListView(
      padding: const EdgeInsets.fromLTRB(10, 7, 14, 12),
      children: [
        _depth(depth, (d) {
          _setMaskDepth(d, c, id, layer, t);
          if (d == _N4Depth.avancado) {
            showMasksSheet(
              context,
              ref,
              id,
              widget.playback,
              onEditMaskPoints: widget.onEditMaskPoints,
            );
          }
        }),
        const SizedBox(height: 8),
        AppText(
          'Montar · ${mask.name}',
          style: const TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w700,
            color: AmColors.text,
          ),
        ),
        const SizedBox(height: 7),
        Wrap(
          spacing: 6,
          runSpacing: 6,
          children: [
            _button(
              'Retângulo',
              () => c.replaceMaskPath(
                id,
                mask.id,
                BezierPath.rect(size.width, size.height),
                t,
              ),
            ),
            _button(
              'Elipse',
              () => c.replaceMaskPath(
                id,
                mask.id,
                BezierPath.ellipse(size.width, size.height),
                t,
              ),
            ),
            _button('Da forma', () {
              if (!c.setMaskFromOwnShape(id, mask.id, t)) {
                showReasonToast(
                  context,
                  'Esta camada nao tem forma para copiar',
                );
              }
            }),
            _button('Desenhar', () {
              c.replaceMaskPath(
                id,
                mask.id,
                BezierPath(vertices: const [], closed: false),
                t,
              );
              widget.onEditMaskPoints(mask.id);
            }),
          ],
        ),
        Row(
          children: [
            const Expanded(
              child: AppText('Inverter',
                style: TextStyle(fontSize: 12, color: AmColors.muted),
              ),
            ),
            CupertinoSwitch(
              value: mask.inverted,
              activeTrackColor: AmColors.accent,
              onChanged: (_) => c.toggleMaskInverted(id, mask.id),
            ),
          ],
        ),
        _maskRuler(
          'Feather',
          mask.feather.valueAt(local),
          0,
          200,
          (v) => c.editMaskParam(id, mask.id, 'feather', t, v),
        ),
        _maskRuler(
          'Expansão',
          mask.expansion.valueAt(local),
          -200,
          200,
          (v) => c.editMaskParam(id, mask.id, 'expansion', t, v),
        ),
        if (!mask.path.valueAt(local).closed)
          const AppText('Caminho aberto nao recorta. Feche no Edit Points.',
            style: TextStyle(fontSize: 11, color: AmColors.accent),
          ),
        if (maskFeatherExceedsBounds(mask, local, size))
          const AppText('Aviso: feather e expansao passam do limite.',
            style: TextStyle(fontSize: 11, color: AmColors.accent),
          ),
      ],
    );
  }

  Widget _maskRuler(
    String label,
    double value,
    double min,
    double max,
    ValueChanged<double> changed,
  ) => Row(
    children: [
      Expanded(
        child: ParameterRow(
          label: label,
          value: value.clamp(min, max),
          min: min,
          max: max,
          unitsPerPixel: (max - min) / 360,
          decimals: 0,
          height: 40,
          onChanged: (v) => changed(v.clamp(min, max)),
        ),
      ),
    ],
  );

  static const _matteModes = <(String, MatteMode)>[
    ('Nenhum', MatteMode.none),
    ('Alfa', MatteMode.alpha),
    ('Alfa inv.', MatteMode.alphaInvert),
    ('Luma', MatteMode.luma),
    ('Luma inv.', MatteMode.lumaInvert),
  ];

  Widget _matte(
    BuildContext context,
    EditorController c,
    String id,
    Layer layer,
  ) {
    final depth = ref.watch(_matteDepthProvider);
    void setDepth(_N4Depth d) =>
        ref.read(_matteDepthProvider.notifier).state = d;
    if (depth == _N4Depth.pronto) {
      final above = c.matteSourceAbove(id);
      return Padding(
        padding: const EdgeInsets.fromLTRB(10, 8, 14, 12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            const AppText('Pronto · Recortar',
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
            const SizedBox(height: 6),
            AppText(
              above == null
                  ? 'Coloque uma camada acima desta.'
                  : 'Fonte acima: ${above.name}. Ela sera ocultada.',
              style: const TextStyle(fontSize: 12, color: AmColors.muted),
            ),
            const SizedBox(height: 12),
            _button('Recortar pela camada acima', () {
              if (!c.setMatteFromAbove(id, MatteMode.alpha)) {
                showReasonToast(context, 'Nao ha camada valida acima');
              }
            }, selected: true),
            const SizedBox(height: 12),
            _depth(depth, setDepth),
          ],
        ),
      );
    }
    final advanced = depth == _N4Depth.avancado;
    final source = ref
        .read(editorControllerProvider)
        .layerById(layer.matteSourceId ?? '');
    return ListView(
      padding: const EdgeInsets.fromLTRB(10, 8, 14, 12),
      children: [
        _depth(depth, setDepth),
        const SizedBox(height: 10),
        AppText(
          advanced ? 'Avançado · Fonte e canal' : 'Montar · Canal',
          style: const TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w700,
            color: AmColors.text,
          ),
        ),
        const SizedBox(height: 8),
        Wrap(
          spacing: 6,
          runSpacing: 6,
          children: [
            for (final (label, mode) in _matteModes)
              _button(label, () async {
                if (mode == MatteMode.none) {
                  c.setMatte(id, mode, null);
                } else if (!advanced) {
                  if (!c.setMatteFromAbove(id, mode)) {
                    showReasonToast(context, 'Nao ha camada valida acima');
                  }
                } else {
                  var sourceId = layer.matteSourceId;
                  sourceId ??= await _pickMatteSource(context, ref, layer);
                  if (sourceId != null) {
                    c.setMatte(id, mode, sourceId);
                  }
                }
              }, selected: layer.matteMode == mode),
          ],
        ),
        if (advanced) ...[
          const SizedBox(height: 12),
          _button(
            source == null
                ? 'Escolher qualquer camada'
                : 'Fonte: ${source.name}',
            () async {
              final sourceId = await _pickMatteSource(context, ref, layer);
              if (sourceId != null) {
                c.setMatte(
                  id,
                  layer.matteMode == MatteMode.none
                      ? MatteMode.alpha
                      : layer.matteMode,
                  sourceId,
                );
              }
            },
          ),
          const SizedBox(height: 7),
          const AppText('A fonte some enquanto o recorte estiver ligado, volta ao '
            'desligar e a transparencia abaixo e preservada.',
            style: TextStyle(fontSize: 11, color: AmColors.muted),
          ),
        ],
      ],
    );
  }
}

/// Escolher a camada FONTE do matte (qualquer camada da cena).
/// Picker do nulo CONTROLADOR da grade: null = cancelou, '' = nenhum.
Future<String?> _pickControllerNull(
  BuildContext context,
  WidgetRef ref,
  String ownerId,
) async {
  final project = ref.read(editorControllerProvider);
  final nulls = [
    for (final l in project.layers)
      if (l is NullLayer && l.id != ownerId) l,
  ];
  return showModalBottomSheet<String>(
    context: context,
    backgroundColor: AmColors.panel,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
    builder: (sheetContext) => SafeArea(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Padding(
            padding: EdgeInsets.all(14),
            child: AppText('Nulo controlador da grade',
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
          ),
          if (nulls.isEmpty)
            const Padding(
              padding: EdgeInsets.fromLTRB(20, 0, 20, 12),
              child: AppText('Crie outro Nulo 3D para usar como controlador '
                '(o proprio nulo da grade nao conta).',
                style: TextStyle(fontSize: 13, color: AmColors.muted),
              ),
            ),
          Material(
            color: Colors.transparent,
            child: ListTile(
              title: const AppText('Nenhum',
                style: TextStyle(color: AmColors.muted),
              ),
              onTap: () => Navigator.of(sheetContext).pop(''),
            ),
          ),
          for (final other in nulls)
            Material(
              color: Colors.transparent,
              child: ListTile(
                title: AppText(
                  other.name,
                  style: const TextStyle(color: AmColors.text),
                ),
                subtitle: const AppText('Escala/rotacao dele passam a modular a grade',
                  style: TextStyle(fontSize: 11, color: AmColors.muted),
                ),
                onTap: () => Navigator.of(sheetContext).pop(other.id),
              ),
            ),
          const SizedBox(height: 8),
        ],
      ),
    ),
  );
}

Future<String?> _pickMatteSource(
  BuildContext context,
  WidgetRef ref,
  Layer target,
) async {
  final project = ref.read(editorControllerProvider);
  return showModalBottomSheet<String>(
    context: context,
    backgroundColor: AmColors.panel,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
    builder: (sheetContext) => SafeArea(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Padding(
            padding: EdgeInsets.all(14),
            child: AppText('Usar como matte...',
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
          ),
          for (final other in project.layers)
            if (other.id != target.id &&
                other is! AudioLayer &&
                other is! NullLayer &&
                other is! AdjustmentLayer)
              Material(
                color: Colors.transparent,
                child: ListTile(
                  title: AppText(
                    other.name,
                    style: const TextStyle(color: AmColors.text),
                  ),
                  subtitle: const AppText('A fonte fica oculta na cena',
                    style: TextStyle(fontSize: 11, color: AmColors.muted),
                  ),
                  onTap: () => Navigator.of(sheetContext).pop(other.id),
                ),
              ),
          const SizedBox(height: 8),
        ],
      ),
    ),
  );
}

/// Painel "Cor e preenchimento" (forma/texto) + operadores vetoriais
/// (Trim Paths e Repeater) quando a camada e uma forma.
class ColorFillPanel extends ConsumerWidget {
  const ColorFillPanel({
    super.key,
    required this.onBack,
    required this.playback,
  });

  final VoidCallback onBack;
  final PlaybackController playback;

  static const _swatches = [
    Color(0xFFB97A5E),
    Color(0xFF4A7BA6),
    Color(0xFFFF5566),
    Color(0xFFFFB020),
    Color(0xFF2BE3A0),
    Color(0xFF35C4E7),
    Color(0xFF7C62FF),
    Color(0xFFFFFFFF),
    Color(0xFF10151D),
  ];

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final project = ref.watch(editorControllerProvider);
    final id = ref.watch(selectedLayerProvider);
    final layer = id == null ? null : project.layerById(id);
    if (layer == null || id == null || layer is AudioLayer) {
      return const ColoredBox(color: AmColors.panel);
    }
    return ColoredBox(
      color: AmColors.panel,
      child: ValueListenableBuilder<Duration>(
        valueListenable: playback.time,
        builder: (context, t, _) => switch (layer) {
          ShapeLayer l => _PreenchimentoDaForma(
            layer: l,
            layerId: id,
            tempo: t,
            swatches: _swatches,
          ),
          _ => _SobreposicaoDeCor(
            layer: layer,
            layerId: id,
            swatches: _swatches,
          ),
        },
      ),
    );
  }
}

/// A FILEIRA DE ABAS do painel de cor: icone e rotulo, a ativa acesa.
class _AbasDeCor<T> extends StatelessWidget {
  const _AbasDeCor({
    required this.abas,
    required this.ativa,
    required this.onAba,
  });

  final List<(T, String, IconData, String)> abas;
  final T ativa;
  final ValueChanged<T> onAba;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(8, 8, 8, 4),
    child: Row(
      children: [
        for (final (valor, rotulo, icone, chave) in abas)
          Expanded(
            child: Tocavel(
              key: ValueKey(chave),
              onTap: () => onAba(valor),
              child: Container(
                height: 50,
                margin: const EdgeInsets.symmetric(horizontal: 3),
                decoration: BoxDecoration(
                  color: valor == ativa ? AmColors.accentDim : AmColors.chip,
                  borderRadius: BorderRadius.circular(10),
                ),
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Icon(
                      icone,
                      size: 17,
                      color: valor == ativa ? AmColors.accent : AmColors.text,
                    ),
                    const SizedBox(height: 3),
                    AppText(
                      rotulo,
                      maxLines: 1,
                      style: TextStyle(
                        fontSize: 10.5,
                        color: valor == ativa
                            ? AmColors.accent
                            : AmColors.muted,
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
      ],
    ),
  );
}

/// Linha "escolher qualquer cor" + atalhos de cor.
Widget _escolhaDeCor(
  BuildContext context, {
  required Color atual,
  required List<Color> swatches,
  required ValueChanged<Color> onCor,
  String rotulo = 'Escolher qualquer cor',
  String chave = 'cor-qualquer',
}) {
  return Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    children: [
      Tocavel(
        key: ValueKey(chave),
        onTap: () async {
          final picked = await showColorPicker(
            context,
            initial: atual,
            onChanged: onCor,
          );
          if (picked != null) onCor(picked);
        },
        child: Container(
          margin: const EdgeInsets.only(bottom: 14),
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 11),
          decoration: BoxDecoration(
            color: AmColors.chip,
            borderRadius: BorderRadius.circular(12),
          ),
          child: Row(
            children: [
              Container(
                width: 34,
                height: 34,
                decoration: BoxDecoration(
                  color: atual,
                  borderRadius: BorderRadius.circular(9),
                  border: Border.all(color: Colors.white24),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: AppText(
                  rotulo,
                  style: const TextStyle(fontSize: 14, color: AmColors.text),
                ),
              ),
              const Icon(
                CupertinoIcons.chevron_right,
                size: 15,
                color: AmColors.muted,
              ),
            ],
          ),
        ),
      ),
      Wrap(
        spacing: 12,
        runSpacing: 12,
        children: [
          for (final c in swatches)
            Tocavel(
              onTap: () => onCor(c),
              child: Container(
                width: 42,
                height: 42,
                decoration: BoxDecoration(
                  color: c,
                  shape: BoxShape.circle,
                  border: Border.all(
                    color: atual.toARGB32() == c.toARGB32()
                        ? AmColors.accent
                        : Colors.white24,
                    width: atual.toARGB32() == c.toARGB32() ? 3 : 1,
                  ),
                ),
              ),
            ),
        ],
      ),
    ],
  );
}

/// FORMA: nenhum, cor solida, degrade (linear, radial, varredura) e midia.
class _PreenchimentoDaForma extends ConsumerWidget {
  const _PreenchimentoDaForma({
    required this.layer,
    required this.layerId,
    required this.tempo,
    required this.swatches,
  });

  final ShapeLayer layer;
  final String layerId;
  final Duration tempo;
  final List<Color> swatches;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final c = ref.read(editorControllerProvider.notifier);
    final tipo = tipoDePreenchimentoDe(layer.contents);
    final degrade = layer.contents.whereType<ShapeGradientFill>().firstOrNull;
    final midia = layer.contents.whereType<ShapeMediaFill>().firstOrNull;

    Future<void> escolherMidia() async {
      final foto = await ref
          .read(mediaImportServiceProvider)
          .pickImageFromGallery();
      if (foto == null) return;
      c.definirTipoDePreenchimento(
        layerId,
        TipoDePreenchimento.midia,
        midia: foto.path,
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _AbasDeCor<TipoDePreenchimento>(
          abas: const [
            (TipoDePreenchimento.nenhum, 'Nenhum', CupertinoIcons.nosign, 'cor-aba-nenhum'),
            (TipoDePreenchimento.cor, 'Cor', CupertinoIcons.drop_fill, 'cor-aba-cor'),
            (TipoDePreenchimento.degrade, 'Degradê', CupertinoIcons.color_filter, 'cor-aba-degrade'),
            (TipoDePreenchimento.midia, 'Mídia', CupertinoIcons.photo, 'cor-aba-midia'),
          ],
          ativa: tipo,
          onAba: (novo) {
            if (novo == TipoDePreenchimento.midia && midia == null) {
              escolherMidia();
              return;
            }
            c.definirTipoDePreenchimento(layerId, novo);
          },
        ),
        Expanded(
          child: ListView(
            padding: const EdgeInsets.fromLTRB(12, 10, 16, 16),
            children: [
              switch (tipo) {
                TipoDePreenchimento.nenhum => const Padding(
                  padding: EdgeInsets.symmetric(vertical: 18),
                  child: AppText(
                    'Sem preenchimento: só o traço (se houver) aparece.',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: AmColors.muted, fontSize: 13),
                  ),
                ),
                TipoDePreenchimento.cor => _escolhaDeCor(
                  context,
                  atual: layer.primaryColor,
                  swatches: swatches,
                  onCor: (cor) => c.setShapePrimaryColor(layerId, cor),
                ),
                TipoDePreenchimento.degrade when degrade != null =>
                  _EdicaoDoDegrade(layerId: layerId, degrade: degrade),
                TipoDePreenchimento.degrade => const SizedBox.shrink(),
                TipoDePreenchimento.midia => Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Tocavel(
                      key: const ValueKey('cor-midia-escolher'),
                      onTap: escolherMidia,
                      child: Container(
                        padding: const EdgeInsets.all(12),
                        decoration: BoxDecoration(
                          color: AmColors.chip,
                          borderRadius: BorderRadius.circular(12),
                        ),
                        child: Row(
                          children: [
                            const Icon(
                              CupertinoIcons.photo_on_rectangle,
                              color: AmColors.text,
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: AppText(
                                midia == null
                                    ? 'Escolher uma foto'
                                    : 'Trocar a foto',
                                style: const TextStyle(
                                  fontSize: 14,
                                  color: AmColors.text,
                                ),
                              ),
                            ),
                            const Icon(
                              CupertinoIcons.chevron_right,
                              size: 15,
                              color: AmColors.muted,
                            ),
                          ],
                        ),
                      ),
                    ),
                    const SizedBox(height: 12),
                    if (midia != null)
                      CupertinoSlidingSegmentedControl<EncaixeNaForma>(
                        key: const ValueKey('cor-midia-encaixe'),
                        groupValue: midia.encaixe,
                        thumbColor: AmColors.accentDim,
                        backgroundColor: AmColors.chip,
                        children: const {
                          EncaixeNaForma.preencher: Padding(
                            padding: EdgeInsets.symmetric(vertical: 8),
                            child: AppText('Preencher',
                              style: TextStyle(color: AmColors.text, fontSize: 13),
                            ),
                          ),
                          EncaixeNaForma.caber: AppText('Caber',
                            style: TextStyle(color: AmColors.text, fontSize: 13),
                          ),
                          EncaixeNaForma.esticar: AppText('Esticar',
                            style: TextStyle(color: AmColors.text, fontSize: 13),
                          ),
                        },
                        onValueChanged: (e) {
                          if (e != null) {
                            c.definirEncaixeDaMidiaNaForma(layerId, e);
                          }
                        },
                      ),
                  ],
                ),
              },
              _ShapeOperators(
                layer: layer,
                layerId: layerId,
                globalTime: tempo,
              ),
            ],
          ),
        ),
      ],
    );
  }
}

/// O DEGRADE ALI MESMO: tipo (linear, radial, varredura), as duas cores
/// e o angulo.
class _EdicaoDoDegrade extends ConsumerWidget {
  const _EdicaoDoDegrade({required this.layerId, required this.degrade});

  final String layerId;
  final ShapeGradientFill degrade;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final c = ref.read(editorControllerProvider.notifier);
    final g = degrade;
    final tipo = g.varredura ? 2 : (g.radial ? 1 : 0);
    void editar(ShapeGradientFill Function(ShapeGradientFill) f) =>
        c.updateShapeGradient(layerId, g.id, f);

    Widget amostra(String chave, String rotulo, Color cor, bool inicio) =>
        Expanded(
          child: Tocavel(
            key: ValueKey(chave),
            onTap: () async {
              void aplicar(Color nova) => editar(
                (x) => inicio ? x.copyWith(colorA: nova) : x.copyWith(colorB: nova),
              );
              final escolhida = await showColorPicker(
                context,
                initial: cor,
                onChanged: aplicar,
              );
              if (escolhida != null) aplicar(escolhida);
            },
            child: Container(
              height: 46,
              margin: const EdgeInsets.symmetric(horizontal: 4),
              padding: const EdgeInsets.symmetric(horizontal: 10),
              decoration: BoxDecoration(
                color: AmColors.chip,
                borderRadius: BorderRadius.circular(10),
              ),
              child: Row(
                children: [
                  Container(
                    width: 24,
                    height: 24,
                    decoration: BoxDecoration(
                      color: cor,
                      shape: BoxShape.circle,
                      border: Border.all(color: Colors.white24),
                    ),
                  ),
                  const SizedBox(width: 8),
                  AppText(
                    rotulo,
                    style: const TextStyle(fontSize: 13, color: AmColors.text),
                  ),
                ],
              ),
            ),
          ),
        );

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        CupertinoSlidingSegmentedControl<int>(
          key: const ValueKey('cor-degrade-tipo'),
          groupValue: tipo,
          thumbColor: AmColors.accentDim,
          backgroundColor: AmColors.chip,
          children: const {
            0: Padding(
              padding: EdgeInsets.symmetric(vertical: 8),
              child: AppText('Linear',
                style: TextStyle(color: AmColors.text, fontSize: 13),
              ),
            ),
            1: AppText('Radial',
              style: TextStyle(color: AmColors.text, fontSize: 13),
            ),
            2: AppText('Varredura',
              style: TextStyle(color: AmColors.text, fontSize: 13),
            ),
          },
          onValueChanged: (v) {
            if (v == null) return;
            editar((x) => x.copyWith(radial: v == 1, varredura: v == 2));
          },
        ),
        const SizedBox(height: 12),
        // A PREVIA DO DEGRADE: a mesma conta de cores e paradas da forma.
        Container(
          height: 26,
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(8),
            gradient: LinearGradient(
              colors: g.paradas,
              stops: g.resolvedStops,
            ),
          ),
        ),
        const SizedBox(height: 12),
        Row(
          children: [
            amostra('cor-degrade-inicio', 'Início', g.colorA, true),
            amostra('cor-degrade-fim', 'Fim', g.colorB, false),
          ],
        ),
        const SizedBox(height: 10),
        ParameterRow(
          label: 'Ângulo',
          value: g.angleDeg,
          min: -360,
          max: 360,
          unitsPerPixel: 1,
          decimals: 0,
          unit: '°',
          onChanged: (v) => editar((x) => x.copyWith(angleDeg: v)),
        ),
      ],
    );
  }
}

/// FOTO, VIDEO, GRUPO E TEXTO: cor intrinseca (a da propria midia), uma
/// cor por cima ou um degrade por cima — os estilos de camada.
class _SobreposicaoDeCor extends ConsumerWidget {
  const _SobreposicaoDeCor({
    required this.layer,
    required this.layerId,
    required this.swatches,
  });

  final Layer layer;
  final String layerId;
  final List<Color> swatches;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final c = ref.read(editorControllerProvider.notifier);
    final estilos = ref.watch(
      editorControllerProvider.select((p) => p.metaOf(layerId).styles),
    );
    final texto = layer is TextLayer;
    final aba = estilos.gradientOverlay?.enabled == true
        ? 2
        : (estilos.colorOverlay?.enabled == true ? 1 : 0);

    void escolherAba(int nova) {
      c.updateLayerStyles(layerId, (s) {
        switch (nova) {
          case 0:
            return s.copyWith(
              clearColorOverlay: true,
              clearGradientOverlay: true,
            );
          case 1:
            return s.copyWith(
              colorOverlay: (s.colorOverlay ?? OverlayStyle()).copyWith(
                enabled: true,
              ),
              clearGradientOverlay: true,
            );
          default:
            return s.copyWith(
              gradientOverlay: (s.gradientOverlay ?? GradientOverlayStyle())
                  .copyWith(enabled: true),
              clearColorOverlay: true,
            );
        }
      });
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _AbasDeCor<int>(
          abas: [
            (0, texto ? 'Cor do texto' : 'Intrínseca', texto ? CupertinoIcons.textformat : CupertinoIcons.photo, 'cor-aba-intrinseca'),
            (1, 'Cor', CupertinoIcons.drop_fill, 'cor-aba-cor'),
            (2, 'Degradê', CupertinoIcons.color_filter, 'cor-aba-degrade'),
          ],
          ativa: aba,
          onAba: escolherAba,
        ),
        Expanded(
          child: ListView(
            padding: const EdgeInsets.fromLTRB(12, 10, 16, 16),
            children: [
              if (aba == 0 && layer is TextLayer)
                _escolhaDeCor(
                  context,
                  atual: (layer as TextLayer).color,
                  swatches: swatches,
                  onCor: (cor) => c.editTextLayer(layerId, color: cor),
                )
              else if (aba == 0)
                const Padding(
                  padding: EdgeInsets.symmetric(vertical: 18),
                  child: AppText(
                    'As cores da própria camada, sem nada por cima.',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: AmColors.muted, fontSize: 13),
                  ),
                ),
              if (aba == 1)
                _escolhaDeCor(
                  context,
                  atual: estilos.colorOverlay?.color ?? const Color(0xFFB8FF3D),
                  swatches: swatches,
                  rotulo: 'Cor por cima da camada',
                  onCor: (cor) => c.updateLayerStyles(
                    layerId,
                    (s) => s.copyWith(
                      colorOverlay: (s.colorOverlay ?? OverlayStyle())
                          .copyWith(color: cor, enabled: true),
                    ),
                  ),
                ),
              if (aba == 2) ...[
                Row(
                  children: [
                    for (final (chave, rotulo, inicio) in const [
                      ('cor-sobreposicao-inicio', 'Início', true),
                      ('cor-sobreposicao-fim', 'Fim', false),
                    ])
                      Expanded(
                        child: Tocavel(
                          key: ValueKey(chave),
                          onTap: () async {
                            final atual = estilos.gradientOverlay ??
                                GradientOverlayStyle();
                            void aplicar(Color nova) => c.updateLayerStyles(
                              layerId,
                              (s) => s.copyWith(
                                gradientOverlay:
                                    (s.gradientOverlay ?? GradientOverlayStyle())
                                        .copyWith(
                                          colorA: inicio ? nova : null,
                                          colorB: inicio ? null : nova,
                                          enabled: true,
                                        ),
                              ),
                            );
                            final escolhida = await showColorPicker(
                              context,
                              initial: inicio ? atual.colorA : atual.colorB,
                              onChanged: aplicar,
                            );
                            if (escolhida != null) aplicar(escolhida);
                          },
                          child: Container(
                            height: 46,
                            margin: const EdgeInsets.symmetric(horizontal: 4),
                            decoration: BoxDecoration(
                              color: AmColors.chip,
                              borderRadius: BorderRadius.circular(10),
                            ),
                            child: Row(
                              mainAxisAlignment: MainAxisAlignment.center,
                              children: [
                                Container(
                                  width: 22,
                                  height: 22,
                                  decoration: BoxDecoration(
                                    shape: BoxShape.circle,
                                    color: inicio
                                        ? (estilos.gradientOverlay?.colorA ??
                                              GradientOverlayStyle().colorA)
                                        : (estilos.gradientOverlay?.colorB ??
                                              GradientOverlayStyle().colorB),
                                  ),
                                ),
                                const SizedBox(width: 8),
                                AppText(
                                  rotulo,
                                  style: const TextStyle(
                                    color: AmColors.text,
                                    fontSize: 13,
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                      ),
                  ],
                ),
              ],
            ],
          ),
        ),
      ],
    );
  }
}

/// Operadores vetoriais da forma: Trim Paths e Repeater, animaveis.
class _ShapeOperators extends ConsumerWidget {
  const _ShapeOperators({
    required this.layer,
    required this.layerId,
    required this.globalTime,
  });

  final ShapeLayer layer;
  final String layerId;
  final Duration globalTime;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final controller = ref.read(editorControllerProvider.notifier);
    final local = layer.localTime(globalTime);

    Widget ruler(
      String label,
      double value,
      double min,
      double max,
      ValueChanged<double> onChanged,
    ) {
      return Row(
        children: [
          SizedBox(
            width: 58,
            child: AppText(
              label,
              style: const TextStyle(fontSize: 11, color: AmColors.muted),
            ),
          ),
          Expanded(
            child: AmTickRuler(
              value: value,
              min: min,
              max: max,
              unitsPerPixel: (max - min) / 450,
              height: 30,
              onChanged: onChanged,
            ),
          ),
          SizedBox(
            width: 40,
            child: AppText(
              amNumber(value, 0),
              textAlign: TextAlign.right,
              style: const TextStyle(fontSize: 11, color: AmColors.text),
            ),
          ),
        ],
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SizedBox(height: 16),
        const AppText('OPERADORES',
          style: TextStyle(
            fontSize: 11,
            letterSpacing: 1,
            fontWeight: FontWeight.w600,
            color: AmColors.muted,
          ),
        ),
        const SizedBox(height: 8),
        for (final item in layer.contents)
          if (item is TrimOperator)
            Container(
              margin: const EdgeInsets.only(bottom: 6),
              padding: const EdgeInsets.fromLTRB(10, 8, 10, 8),
              decoration: BoxDecoration(
                color: AmColors.bg.withValues(alpha: 0.5),
                borderRadius: BorderRadius.circular(10),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const Expanded(
                        child: AppText('Trim Paths',
                          style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      // PR-M8: Individually (cascata) x Simultaneously.
                      Tocavel(
                        onTap: () => controller.setTrimMode(
                          layerId,
                          item.id,
                          !item.individually,
                        ),
                        child: Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 9,
                            vertical: 4,
                          ),
                          margin: const EdgeInsets.only(right: 10),
                          decoration: BoxDecoration(
                            color: AmColors.chip,
                            borderRadius: BorderRadius.circular(7),
                          ),
                          child: AppText(
                            item.individually ? 'Individual' : 'Continuo',
                            style: const TextStyle(
                              fontSize: 11,
                              color: AmColors.accent,
                            ),
                          ),
                        ),
                      ),
                      Tocavel(
                        onTap: () =>
                            controller.removeShapeItem(layerId, item.id),
                        child: const Icon(
                          CupertinoIcons.xmark,
                          size: 14,
                          color: AmColors.muted,
                        ),
                      ),
                    ],
                  ),
                  ruler(
                    'Inicio',
                    item.start.valueAt(local) * 100,
                    0,
                    100,
                    (v) => controller.editTrim(
                      layerId,
                      item.id,
                      'start',
                      globalTime,
                      v / 100,
                    ),
                  ),
                  ruler(
                    'Fim',
                    item.end.valueAt(local) * 100,
                    0,
                    100,
                    (v) => controller.editTrim(
                      layerId,
                      item.id,
                      'end',
                      globalTime,
                      v / 100,
                    ),
                  ),
                  ruler(
                    'Offset',
                    item.offset.valueAt(local) * 100,
                    -100,
                    100,
                    (v) => controller.editTrim(
                      layerId,
                      item.id,
                      'offset',
                      globalTime,
                      v / 100,
                    ),
                  ),
                ],
              ),
            )
          else if (item is RepeaterOperator)
            Container(
              margin: const EdgeInsets.only(bottom: 6),
              padding: const EdgeInsets.fromLTRB(10, 8, 10, 8),
              decoration: BoxDecoration(
                color: AmColors.bg.withValues(alpha: 0.5),
                borderRadius: BorderRadius.circular(10),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const Expanded(
                        child: AppText('Repeater',
                          style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      CupertinoButton(
                        padding: const EdgeInsets.all(4),
                        onPressed: item.copies > 1
                            ? () => controller.editRepeater(
                                layerId,
                                item.id,
                                globalTime,
                                copies: item.copies - 1,
                              )
                            : null,
                        child: const Icon(
                          CupertinoIcons.minus_circle,
                          size: 18,
                          color: AmColors.muted,
                        ),
                      ),
                      AppText(
                        '${item.copies}',
                        style: const TextStyle(
                          fontSize: 13,
                          color: AmColors.text,
                        ),
                      ),
                      CupertinoButton(
                        padding: const EdgeInsets.all(4),
                        onPressed: () => controller.editRepeater(
                          layerId,
                          item.id,
                          globalTime,
                          copies: item.copies + 1,
                        ),
                        child: const Icon(
                          CupertinoIcons.plus_circle,
                          size: 18,
                          color: AmColors.accent,
                        ),
                      ),
                      Tocavel(
                        onTap: () =>
                            controller.removeShapeItem(layerId, item.id),
                        child: const Icon(
                          CupertinoIcons.xmark,
                          size: 14,
                          color: AmColors.muted,
                        ),
                      ),
                    ],
                  ),
                  ruler(
                    'Desloc X',
                    item.dx,
                    -400,
                    400,
                    (v) => controller.editRepeater(
                      layerId,
                      item.id,
                      globalTime,
                      dx: v,
                    ),
                  ),
                  ruler(
                    'Desloc Y',
                    item.dy,
                    -400,
                    400,
                    (v) => controller.editRepeater(
                      layerId,
                      item.id,
                      globalTime,
                      dy: v,
                    ),
                  ),
                  ruler(
                    'Rotacao',
                    item.rotation.valueAt(local),
                    -180,
                    180,
                    (v) => controller.editRepeater(
                      layerId,
                      item.id,
                      globalTime,
                      rotationDeg: v,
                    ),
                  ),
                ],
              ),
            )
          else if (item is ShapeMorph)
            Container(
              margin: const EdgeInsets.only(bottom: 6),
              padding: const EdgeInsets.fromLTRB(10, 8, 10, 8),
              decoration: BoxDecoration(
                color: AmColors.bg.withValues(alpha: 0.5),
                borderRadius: BorderRadius.circular(10),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      Expanded(
                        child: AppText('Morph  '
                          '${_primName(item.from.primitive)} -> '
                          '${_primName(item.to.primitive)}',
                          style: const TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      // Diamante: keyframe do progresso do morph.
                      CupertinoButton(
                        padding: const EdgeInsets.all(4),
                        onPressed: () => controller.toggleMorphKeyframe(
                          layerId,
                          item.id,
                          globalTime,
                        ),
                        child: Icon(
                          item.progress.hasKeyframeAt(local)
                              ? CupertinoIcons.rhombus_fill
                              : CupertinoIcons.rhombus,
                          size: 18,
                          color: item.progress.isAnimated
                              ? AmColors.accent
                              : AmColors.muted,
                        ),
                      ),
                      Tocavel(
                        onTap: () => controller.removeMorph(layerId, item.id),
                        child: const Icon(
                          CupertinoIcons.xmark,
                          size: 14,
                          color: AmColors.muted,
                        ),
                      ),
                    ],
                  ),
                  ruler(
                    'Progresso',
                    item.progress.valueAt(local) * 100,
                    0,
                    100,
                    (v) => controller.editMorphProgress(
                      layerId,
                      item.id,
                      globalTime,
                      v / 100,
                    ),
                  ),
                ],
              ),
            ),
        // Operadores de caminho: cada um com o seu numero principal
        // animavel e um botao para tirar.
        for (final item in layer.contents)
          if (item is OffsetPathOperator ||
              item is RoundCornersOperator ||
              item is ZigZagOperator ||
              item is PuckerBloatOperator ||
              item is TwistOperator ||
              item is WigglePathOperator ||
              item is MergePathsOperator)
            Container(
              margin: const EdgeInsets.only(bottom: 6),
              padding: const EdgeInsets.fromLTRB(10, 8, 10, 8),
              decoration: BoxDecoration(
                color: AmColors.bg.withValues(alpha: 0.5),
                borderRadius: BorderRadius.circular(10),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      AppText(
                        _opName(item),
                        style: const TextStyle(
                          fontSize: 12,
                          fontWeight: FontWeight.w600,
                          color: AmColors.text,
                        ),
                      ),
                      const Spacer(),
                      if (item is MergePathsOperator)
                        Tocavel(
                          onTap: () =>
                              controller.cycleMergeMode(layerId, item.id),
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 9,
                              vertical: 4,
                            ),
                            decoration: BoxDecoration(
                              color: AmColors.chip,
                              borderRadius: BorderRadius.circular(7),
                            ),
                            child: AppText(
                              mergeModeLabel(item.mode),
                              style: const TextStyle(
                                fontSize: 11,
                                color: AmColors.accent,
                              ),
                            ),
                          ),
                        ),
                      const SizedBox(width: 8),
                      Tocavel(
                        onTap: () =>
                            controller.removeShapeItem(layerId, item.id),
                        child: const Icon(
                          CupertinoIcons.trash,
                          size: 14,
                          color: AmColors.muted,
                        ),
                      ),
                    ],
                  ),
                  if (item is! MergePathsOperator)
                    ruler(
                      _opUnit(item),
                      _opValue(item, local),
                      _opMin(item),
                      _opMax(item),
                      (v) => controller.editPathOperator(
                        layerId,
                        item.id,
                        local,
                        v,
                      ),
                    ),
                ],
              ),
            ),
        // Menu de operadores: cabe em duas linhas e nao esconde nada
        // atras de um submenu.
        Wrap(
          spacing: 6,
          runSpacing: 6,
          children: [
            for (final op in ShapePathOp.values)
              Tocavel(
                onTap: () => controller.addPathOperator(layerId, op),
                child: Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 10,
                    vertical: 6,
                  ),
                  decoration: BoxDecoration(
                    color: AmColors.chip,
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: AppText(
                    '+ ${shapePathOpLabel(op)}',
                    style: const TextStyle(
                      fontSize: 11,
                      color: AmColors.accent,
                    ),
                  ),
                ),
              ),
          ],
        ),
        const SizedBox(height: 6),
        Wrap(
          children: [
            CupertinoButton(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
              onPressed: () =>
                  controller.addShapeOperator(layerId, repeater: false),
              child: const AppText('+ Trim Paths',
                style: TextStyle(fontSize: 12, color: AmColors.accent),
              ),
            ),
            CupertinoButton(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
              onPressed: () =>
                  controller.addShapeOperator(layerId, repeater: true),
              child: const AppText('+ Repeater',
                style: TextStyle(fontSize: 12, color: AmColors.accent),
              ),
            ),
            CupertinoButton(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
              onPressed: () => _pickMorphTarget(context, ref),
              child: const AppText('+ Morfar',
                style: TextStyle(fontSize: 12, color: AmColors.accent),
              ),
            ),
          ],
        ),
      ],
    );
  }

  static String _opName(ShapeItem i) => switch (i) {
    OffsetPathOperator _ => 'Deslocar caminho',
    RoundCornersOperator _ => 'Arredondar cantos',
    ZigZagOperator _ => 'Zig zag',
    PuckerBloatOperator _ => 'Inchar e encolher',
    TwistOperator _ => 'Torcer',
    WigglePathOperator _ => 'Baguncar caminho',
    MergePathsOperator _ => 'Combinar caminhos',
    _ => 'Operador',
  };

  static String _opUnit(ShapeItem i) => switch (i) {
    OffsetPathOperator _ => 'px',
    RoundCornersOperator _ => 'raio',
    ZigZagOperator _ => 'altura',
    PuckerBloatOperator _ => 'forca',
    TwistOperator _ => 'graus',
    WigglePathOperator _ => 'px',
    _ => 'valor',
  };

  static double _opValue(ShapeItem i, Duration t) => switch (i) {
    OffsetPathOperator o => o.amount.valueAt(t),
    RoundCornersOperator r => r.radius.valueAt(t),
    ZigZagOperator z => z.amplitude.valueAt(t),
    PuckerBloatOperator pb => pb.amount.valueAt(t) * 100,
    TwistOperator tw => tw.angle.valueAt(t),
    WigglePathOperator w => w.amount.valueAt(t),
    _ => 0,
  };

  static double _opMin(ShapeItem i) => switch (i) {
    RoundCornersOperator _ => 0,
    ZigZagOperator _ => 0,
    WigglePathOperator _ => 0,
    TwistOperator _ => -720,
    PuckerBloatOperator _ => -100,
    _ => -300,
  };

  static double _opMax(ShapeItem i) => switch (i) {
    TwistOperator _ => 720,
    PuckerBloatOperator _ => 100,
    _ => 300,
  };

  static String _primName(ShapePrimitive p) => switch (p) {
    ShapePrimitive.rectangle => 'Retangulo',
    ShapePrimitive.roundedRectangle => 'Retangulo',
    ShapePrimitive.ellipse => 'Circulo',
    ShapePrimitive.polygon => 'Poligono',
    ShapePrimitive.star => 'Estrela',
    ShapePrimitive.ring => 'Anel',
    ShapePrimitive.arc => 'Arco',
    ShapePrimitive.wave => 'Onda',
    ShapePrimitive.heart => 'Coracao',
    ShapePrimitive.gear => 'Engrenagem',
    ShapePrimitive.arrow => 'Seta',
    ShapePrimitive.check => 'Check',
    ShapePrimitive.plus => 'Mais',
    ShapePrimitive.drop => 'Gota',
    ShapePrimitive.flower => 'Flor',
    ShapePrimitive.sparkle => 'Faisca',
  };

  /// Escolhe a forma DESTINO do morph.
  Future<void> _pickMorphTarget(BuildContext context, WidgetRef ref) async {
    final controller = ref.read(editorControllerProvider.notifier);
    final options = <(String, ShapePath)>[
      ('Circulo', ShapePath(primitive: ShapePrimitive.ellipse)),
      ('Retangulo', ShapePath(primitive: ShapePrimitive.roundedRectangle)),
      ('Estrela', ShapePath(primitive: ShapePrimitive.star)),
      ('Poligono', ShapePath(primitive: ShapePrimitive.polygon, points: 6)),
      ('Coracao', ShapePath(primitive: ShapePrimitive.heart)),
      ('Arco', ShapePath(primitive: ShapePrimitive.arc)),
    ];
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: AmColors.panel,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
      ),
      builder: (sheetContext) => SafeArea(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(20, 14, 20, 16),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const AppText('Morfar para...',
                style: TextStyle(
                  fontSize: 17,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text,
                ),
              ),
              const SizedBox(height: 6),
              const AppText('A forma atual vira a origem; anime o Progresso com '
                'keyframes para ver a transformacao.',
                style: TextStyle(fontSize: 12, color: AmColors.muted),
              ),
              const SizedBox(height: 12),
              Wrap(
                spacing: 10,
                runSpacing: 10,
                children: [
                  for (final (label, path) in options)
                    Tocavel(
                      onTap: () {
                        controller.convertShapeToMorph(layerId, path);
                        Navigator.of(sheetContext).pop();
                      },
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 18,
                          vertical: 12,
                        ),
                        decoration: BoxDecoration(
                          color: AmColors.chip,
                          borderRadius: BorderRadius.circular(12),
                        ),
                        child: AppText(
                          label,
                          style: const TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.w600,
                            color: AmColors.accent,
                          ),
                        ),
                      ),
                    ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Degrades prontos do material brilhante (roxo/azul/rosa das
/// referencias, por do sol, oceano, ouro, prata, neon).
const kGlossyGradients = <List<Color>>[
  [Color(0xFF7A3FF2), Color(0xFF2F7BFF), Color(0xFFFF4FD8)],
  [Color(0xFFFF7A18), Color(0xFFFF2D95), Color(0xFF7A3FF2)],
  [Color(0xFF00E5A8), Color(0xFF2F7BFF), Color(0xFF7A3FF2)],
  [Color(0xFF7A4A00), Color(0xFFFFD36A), Color(0xFFFFF4C2)],
  [Color(0xFF3A3F4A), Color(0xFFC9D1DC), Color(0xFFFFFFFF)],
  [Color(0xFFB8FF3D), Color(0xFF35C4E7), Color(0xFFFF4FD8)],
];

bool _mesmasCores(List<Color> a, List<Color> b) {
  if (a.length != b.length) return false;
  for (var i = 0; i < a.length; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

/// Sheet do EXTRUDE 3D: a espessura da camada. Precisa de rotacao X ou Y
/// para aparecer (de frente, a espessura fica escondida atras).
Future<void> showExtrudeSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  await showParamSheet(
    context,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final projeto = ref.read(editorControllerProvider);
        final layer = projeto.layerById(layerId);
        if (layer == null) return const SizedBox.shrink();
        final atual = projeto.metaOf(layerId).extrude;
        final inclinada =
            layer.rotationX.isAnimated ||
            layer.rotationY.isAnimated ||
            layer.rotationX.base != 0 ||
            layer.rotationY.base != 0;
        return SafeArea(
          child: Padding(
            padding: const EdgeInsets.fromLTRB(18, 14, 18, 14),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const AppText('Extrude 3D',
                  style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 6),
                AppText(
                  inclinada
                      ? 'Espessura da camada. Gire em X ou Y para ver a lateral.'
                      : 'A espessura so aparece com a camada girada em X ou Y (Mover e transf. > Rotacao 3D).',
                  style: const TextStyle(fontSize: 12.5, color: AmColors.muted),
                ),
                const SizedBox(height: 14),
                ParameterRow(
                  label: 'Espessura',
                  value: atual,
                  min: 0,
                  max: 400,
                  unitsPerPixel: 1,
                  decimals: 0,
                  onChanged: (v) {
                    controller.setLayerExtrude(layerId, v);
                    setSheetState(() {});
                  },
                  onReset: () {
                    controller.setLayerExtrude(layerId, 0);
                    setSheetState(() {});
                  },
                ),
                const SizedBox(height: 8),
                Wrap(
                  spacing: 8,
                  children: [
                    for (final v in const [0.0, 20.0, 40.0, 80.0, 160.0])
                      Tocavel(
                        onTap: () {
                          controller.setLayerExtrude(layerId, v);
                          setSheetState(() {});
                        },
                        child: Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 12,
                            vertical: 8,
                          ),
                          decoration: BoxDecoration(
                            color: (atual - v).abs() < 0.5
                                ? AmColors.accentDim
                                : AmColors.chip,
                            borderRadius: BorderRadius.circular(9),
                          ),
                          child: AppText(
                            v == 0 ? 'Desligado' : amNumber(v, 0),
                            style: const TextStyle(
                              fontSize: 12,
                              color: AmColors.accent,
                            ),
                          ),
                        ),
                      ),
                  ],
                ),
              ],
            ),
          ),
        );
      },
    ),
  );
}

String _descricaoDoModelo(Element3DLayer layer) {
  final caminho = layer.meshPath;
  if (caminho == null) return 'Solido nativo';
  final nome = caminho.split(RegExp(r'[\\/]')).last;
  final r = MeshCache.instance.resultFor(caminho);
  if (r != null) return '$nome · ${r.faceCount} faces';
  final erro = MeshCache.instance.errorFor(caminho);
  if (erro != null) return '$nome · nao carregou';
  return '$nome · carregando...';
}

/// Escolhe um OBJ/FBX, le fora da UI, avisa se for pesado e aplica.
Future<void> _escolherModelo3D(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  final r = await FilePicker.platform.pickFiles(type: FileType.any);
  final caminho = r?.files.single.path;
  if (caminho == null) return;
  final ext = caminho.split('.').last.toLowerCase();
  if (!context.mounted) return;
  if (ext != 'obj' && ext != 'fbx') {
    await _avisoModelo(
      context,
      'Formato nao suportado',
      'Escolha um arquivo .obj ou .fbx (ASCII).',
    );
    return;
  }
  MeshImportResult resultado;
  try {
    resultado = await MeshCache.instance.load(caminho);
  } on MeshImportException catch (e) {
    if (!context.mounted) return;
    await _avisoModelo(context, 'Nao deu para importar', e.message);
    return;
  }
  if (!context.mounted) return;
  if (resultado.heavy) {
    final segue = await showCupertinoDialog<bool>(
      context: context,
      builder: (c) => CupertinoAlertDialog(
        title: const AppText('Modelo pesado'),
        content: Padding(
          padding: const EdgeInsets.only(top: 8),
          child: AppText(
            '${resultado.faceCount} faces'
            '${resultado.truncated ? ' (o app usa as primeiras $kMeshFacesMax)' : ''}. '
            'Modelos assim podem travar em celulares fracos; em aparelhos '
            'potentes rodam bem. Importar mesmo assim?',
          ),
        ),
        actions: [
          CupertinoDialogAction(
            onPressed: () => Navigator.of(c).pop(false),
            child: const AppText('Cancelar'),
          ),
          CupertinoDialogAction(
            isDefaultAction: true,
            onPressed: () => Navigator.of(c).pop(true),
            child: const AppText('Importar'),
          ),
        ],
      ),
    );
    if (segue != true) return;
  }
  controller.updateElement3D(
    layerId,
    (e) => e.copyElement3D(meshPath: caminho),
  );
}

Future<void> _avisoModelo(
  BuildContext context,
  String titulo,
  String texto,
) async {
  await showCupertinoDialog<void>(
    context: context,
    builder: (c) => CupertinoAlertDialog(
      title: AppText(titulo),
      content: Padding(
        padding: const EdgeInsets.only(top: 8),
        child: AppText(texto),
      ),
      actions: [
        CupertinoDialogAction(
          isDefaultAction: true,
          onPressed: () => Navigator.of(c).pop(),
          child: const AppText('OK'),
        ),
      ],
    ),
  );
}
