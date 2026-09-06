import 'package:flutter/cupertino.dart';
import 'package:file_picker/file_picker.dart';

import '../../application/mesh_cache.dart';
import '../../domain/mesh_import.dart';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/utils/time_format.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/blend_extra.dart';
import '../../domain/caption.dart';
import '../../domain/effect_preset.dart';
import '../../application/effect_preset_store.dart';
import '../../domain/element3d.dart';
import '../../domain/grid_rig.dart';
import '../../domain/keyframe.dart';
import '../../domain/am_sections.dart';
import '../../domain/layer.dart';
import '../../domain/mask.dart';
import '../../domain/shape.dart';
import '../../domain/shape_ops.dart';
import 'am_colors.dart';
import 'audio_sheet.dart';
import 'beats_sheet.dart';
import 'beat_pulse_sheet.dart';
import '../../../../core/ui/snack.dart';
import 'am_widgets.dart';
import 'layer_look.dart';
import 'cameras_sheet.dart';
import 'caption_style_sheet.dart';
import 'color_picker_sheet.dart';
import 'curve_panel.dart';
import 'decupagem_screen.dart';
import 'font_sheet.dart';
import 'gradient_fill_sheet.dart';
import 'oficio_sheets.dart';
import 'panel_chrome.dart';
import 'path_edit_sheet.dart';
import 'precomp_sheet.dart';
import 'scene3d_sheet.dart';
import 'scene3d_studio.dart';
import 'speed_sheet.dart';
import 'text_path_sheet.dart';
import '../widgets/add_layer_sheet.dart' show showCaptionCreationSheet;

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

/// Menu que abre ao tocar na barra da camada selecionada: fileira de
/// utilidades (icones pequenos) + grade fixa de 7 secoes em 2 fileiras.
Future<LayerMenuAction?> showLayerMenu(
  BuildContext context,
  WidgetRef ref,
  Layer layer,
  PlaybackController playback,
) {
  // Quem decide o que aparece na grade e o contrato das secoes, nao este
  // widget: e assim que "no maximo sete" e "nada inerte" viram teste.
  final secoes = secoesDe(layer);
  final controller = ref.read(editorControllerProvider.notifier);
  var menuFechado = false;

  return showModalBottomSheet<LayerMenuAction>(
    context: context,
    backgroundColor: AmColors.panel,
    // Sem isScrollControlled o modal para em 9/16 da tela; com a fileira
    // de utilidades + duas fileiras de 68 px o conteudo passa disso em
    // aparelho baixo e cortaria a segunda fileira. A Column com
    // mainAxisSize.min continua abracando o conteudo (nao vira tela cheia).
    isScrollControlled: true,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
    // StatefulBuilder porque Mudo alterna SEM fechar o sheet e o icone
    // precisa redesenhar no lugar.
    builder: (_) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        // So este menu pode ser fechado, uma unica vez. Um segundo pop
        // consumiria a rota do editor (ou outro painel aberto depois).
        bool fecharMenu([LayerMenuAction? action]) {
          if (menuFechado || !sheetContext.mounted) return false;
          final route = ModalRoute.of(sheetContext);
          if (route is! PopupRoute || !route.isCurrent) return false;
          menuFechado = true;
          Navigator.of(sheetContext).pop(action);
          return true;
        }

        // Fecha o modal e so DEPOIS abre o sheet: showParamSheet mora no
        // Scaffold hospedeiro (paramSheetHostKey); aberto com o modal em
        // pe ficaria atras da barreira.
        void abrirDepois(void Function() abrir) {
          if (!fecharMenu()) return;
          Future.microtask(() {
            if (context.mounted) abrir();
          });
        }

        // `layer` e um snapshot: o estado de mudo e lido do controller a
        // cada redesenho, senao o icone nao acompanha o toggle.
        final mudo = controller.audioSpecOf(layer.id)?.muted ?? false;
        final temSom = layer is AudioLayer || layer is VideoLayer;
        final pai = ref
            .read(editorControllerProvider)
            .linkFor(layer.id, LayerProp.parent);

        return SafeArea(
          child: Padding(
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 14),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Center(
                  child: Container(
                    width: 34,
                    height: 4,
                    decoration: BoxDecoration(
                      color: Colors.white24,
                      borderRadius: BorderRadius.circular(2),
                    ),
                  ),
                ),
                const SizedBox(height: 12),
                Text(
                  layer.name,
                  style: const TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 10),
                // INSPETOR (spec barra-de-acoes §4, forma do Alight
                // Motion): UMA fileira de UTILIDADES (icones pequenos:
                // acoes rapidas e sheets que nao trocam a pagina do
                // editor; so as que se aplicam ao tipo, com "Mais" fixo
                // no fim) + no maximo 7 editores em 2 fileiras (3
                // largos + 4). A grade conserva os lugares, mas um editor
                // que nao se aplica ao tipo fica vazio — nenhum controle
                // visivel pode ser inerte. Comandos estruturais
                // (dividir/duplicar/agrupar/excluir) vivem na barra de
                // acoes, nao aqui.
                SizedBox(
                  height: 40,
                  child: Row(
                    children: [
                      Expanded(
                        child: SingleChildScrollView(
                          scrollDirection: Axis.horizontal,
                          child: Row(
                            children: [
                              if (temSom) ...[
                                _UtilIcon(
                                  icon: CupertinoIcons.speedometer,
                                  label: 'Velocidade',
                                  onTap: () => abrirDepois(
                                    () =>
                                        showSpeedSheet(context, ref, layer.id),
                                  ),
                                ),
                                _UtilIcon(
                                  icon: CupertinoIcons.scissors,
                                  label: 'Cortes',
                                  onTap: () => abrirDepois(
                                    () => openDecupagem(context, ref, layer.id),
                                  ),
                                ),
                                // Mudo e toggle no lugar (nao abre nada):
                                // nao ha metodo de mudo no controller, e
                                // composto com updateAudioSpec + copyWith.
                                _UtilIcon(
                                  icon: mudo
                                      ? CupertinoIcons.speaker_slash_fill
                                      : CupertinoIcons.speaker_slash,
                                  label: mudo ? 'Ativar som' : 'Mudo',
                                  aceso: mudo,
                                  onTap: () {
                                    controller.updateAudioSpec(
                                      layer.id,
                                      (a) => a.copyWith(muted: !a.muted),
                                    );
                                    setSheetState(() {});
                                  },
                                ),
                                _UtilIcon(
                                  icon: CupertinoIcons.waveform,
                                  label: 'Som',
                                  onTap: () => abrirDepois(
                                    () =>
                                        showAudioSheet(context, ref, layer.id),
                                  ),
                                ),
                                _UtilIcon(
                                  icon: CupertinoIcons.metronome,
                                  label: 'Batidas',
                                  onTap: () => abrirDepois(
                                    () =>
                                        showBeatsSheet(context, ref, layer.id),
                                  ),
                                ),
                              ],
                              if (layer is VideoLayer)
                                _UtilIcon(
                                  icon: CupertinoIcons.captions_bubble,
                                  label: 'Legendar',
                                  onTap: () {
                                    Navigator.of(sheetContext).pop();
                                    Future.microtask(() {
                                      if (context.mounted) {
                                        showCaptionCreationSheet(context, ref);
                                      }
                                    });
                                  },
                                ),
                              if (layer is VideoLayer) ...[
                                _UtilIcon(
                                  icon: CupertinoIcons.crop,
                                  label: 'Reenquadrar sozinho',
                                  onTap: () async {
                                    Navigator.of(sheetContext).pop();
                                    if (!context.mounted) return;
                                    AureaSnack.show(
                                      context,
                                      'Achando o assunto...',
                                    );
                                    final n = await controller.autoReframeLayer(
                                      layer.id,
                                    );
                                    if (!context.mounted) return;
                                    if (n == null) {
                                      AureaSnack.show(
                                        context,
                                        'Nao consegui ler esse video',
                                      );
                                      return;
                                    }
                                    AureaSnack.show(
                                      context,
                                      'Reenquadrado seguindo o assunto',
                                      actionLabel: 'Desfazer',
                                      onAction: controller.undo,
                                    );
                                  },
                                ),
                                _UtilIcon(
                                  icon: CupertinoIcons.hand_raised,
                                  label: 'Estabilizar',
                                  onTap: () async {
                                    Navigator.of(sheetContext).pop();
                                    if (!context.mounted) return;
                                    AureaSnack.show(
                                      context,
                                      'Lendo o video para estabilizar...',
                                    );
                                    final n = await controller.stabilizeLayer(
                                      layer.id,
                                    );
                                    if (!context.mounted) return;
                                    if (n == null) {
                                      AureaSnack.show(
                                        context,
                                        'Nao consegui ler esse video',
                                      );
                                      return;
                                    }
                                    AureaSnack.show(
                                      context,
                                      'Estabilizado com $n quadros de referencia',
                                      actionLabel: 'Desfazer',
                                      onAction: controller.undo,
                                    );
                                  },
                                ),
                              ],
                              // 3D DA CAMADA e MOTION BLUR nesta fileira.
                              // Mascara agora mora dentro de Mesclagem e
                              // opacidade, sem criar uma oitava secao.
                              // Tudo daqui em diante e ESTUDIO (fora do
                              // nucleo).
                              _UtilIcon(
                                icon: layer.is3D
                                    ? CupertinoIcons.cube_fill
                                    : CupertinoIcons.cube,
                                label: layer.is3D ? '3D ligado' : 'Ligar 3D',
                                aceso: layer.is3D,
                                onTap: () {
                                  controller.toggle3D(layer.id);
                                  setSheetState(() {});
                                },
                              ),
                              _UtilIcon(
                                icon: CupertinoIcons.speedometer,
                                label: 'Motion blur',
                                aceso: ref
                                    .read(editorControllerProvider)
                                    .metaOf(layer.id)
                                    .motionBlur,
                                onTap: () {
                                  controller.toggleLayerMotionBlurReal(
                                    layer.id,
                                  );
                                  setSheetState(() {});
                                },
                              ),
                              if (layer is! NullLayer &&
                                  layer is! VideoLayer &&
                                  layer is! ParticlesLayer &&
                                  layer is! Element3DLayer)
                                _UtilIcon(
                                  icon: CupertinoIcons.cube,
                                  label: 'Extrude 3D',
                                  aceso:
                                      ref
                                          .read(editorControllerProvider)
                                          .metaOf(layer.id)
                                          .extrude >
                                      0,
                                  onTap: () => abrirDepois(
                                    () => showExtrudeSheet(
                                      context,
                                      ref,
                                      layer.id,
                                    ),
                                  ),
                                ),
                              _UtilIcon(
                                icon: CupertinoIcons.music_note_2,
                                label: 'Pulsar na batida',
                                onTap: () => abrirDepois(
                                  () => showBeatPulseSheet(
                                    context,
                                    ref,
                                    layer.id,
                                  ),
                                ),
                              ),
                              _UtilIcon(
                                icon: CupertinoIcons.repeat,
                                label: 'Loop de keyframes',
                                onTap: () => abrirDepois(
                                  () => showLoopSheet(context, ref, layer.id),
                                ),
                              ),
                              _UtilIcon(
                                icon: CupertinoIcons.tag,
                                label: 'Organizar (rotulo, solo, timida)',
                                onTap: () => abrirDepois(
                                  () =>
                                      showOrganizeSheet(context, ref, layer.id),
                                ),
                              ),
                              _UtilIcon(
                                icon: pai != null
                                    ? CupertinoIcons.link_circle_fill
                                    : CupertinoIcons.link,
                                label: pai != null
                                    ? 'Soltar do pai'
                                    : 'Vincular ao pai',
                                aceso: pai != null,
                                onTap: () {
                                  if (pai != null) {
                                    Navigator.of(sheetContext).pop();
                                    controller.unlinkProperty(
                                      layer.id,
                                      LayerProp.parent,
                                    );
                                    return;
                                  }
                                  abrirDepois(
                                    () => showParentSheet(
                                      context,
                                      ref,
                                      layer,
                                      playback.time.value,
                                    ),
                                  );
                                },
                              ),
                            ],
                          ),
                        ),
                      ),
                      // OS COMANDOS QUE SOBRARAM DO MENU ESCONDIDO.
                      // Cada um aqui e um icone visivel, nao um item de
                      // lista dentro de um "Mais".
                      if (layer is TextLayer) ...[
                        _UtilIcon(
                          icon: CupertinoIcons.textformat,
                          label: 'Fonte',
                          onTap: () => abrirDepois(
                            () => showFontSheet(context, ref, layer.id),
                          ),
                        ),
                        _UtilIcon(
                          icon: CupertinoIcons.circle_grid_hex,
                          label: 'Caminho',
                          onTap: () => abrirDepois(
                            () => showTextPathSheet(context, ref, layer.id),
                          ),
                        ),
                        _UtilIcon(
                          icon: CupertinoIcons.textformat_abc_dottedunderline,
                          label: 'Animar',
                          onTap: () =>
                              Navigator.of(sheetContext)
                                  .pop(LayerMenuAction.textAnimators),
                        ),
                      ],
                      if (layer is Scene3DLayer) ...[
                        _UtilIcon(
                          icon: CupertinoIcons.videocam,
                          label: 'Cameras',
                          onTap: () => abrirDepois(
                            () => showCamerasSheet(
                              context,
                              ref,
                              layer.id,
                              playback,
                            ),
                          ),
                        ),
                      ],
                      if (layer is GroupLayer) ...[
                        _UtilIcon(
                          icon: CupertinoIcons.timer,
                          label: 'Tempo',
                          onTap: () => abrirDepois(
                            () => showPrecompSheet(
                              context,
                              ref,
                              layer.id,
                              playback,
                            ),
                          ),
                        ),
                        _UtilIcon(
                          icon: CupertinoIcons.square_stack_3d_down_right,
                          label: 'Desagrupar',
                          onTap: () {
                            Navigator.of(sheetContext).pop();
                            controller.ungroupLayer(layer.id);
                          },
                        ),
                      ],
                      _UtilIcon(
                        icon: CupertinoIcons.delete_left,
                        label: 'Excluir e\nfechar',
                        onTap: () {
                          Navigator.of(sheetContext).pop();
                          controller.rippleDeleteLayer(layer.id);
                        },
                      ),
                      _UtilIcon(
                        icon: CupertinoIcons.arrow_left_to_line,
                        label: 'Fechar\nburacos',
                        onTap: () {
                          Navigator.of(sheetContext).pop();
                          if (controller.gapCount() == 0) {
                            showReasonToast(
                              context,
                              'Nao ha buraco para fechar',
                            );
                            return;
                          }
                          controller.closeTimelineGaps();
                        },
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 12),
                // A GRADE, MONTADA A PARTIR DO CONTRATO.
                //
                // Antes cada tile carregava a propria condicao de
                // aparecer, e contar as secoes era ler duzentas linhas de
                // widget. Agora o contrato diz quais secoes o tipo tem, e
                // a grade so desenha — nao ha como as duas listas
                // discordarem, porque so existe uma.
                ..._fileiras(
                  secoes,
                  (secao) => _tileDaSecao(
                    secao,
                    context: context,
                    fecharCom: fecharMenu,
                    ref: ref,
                    layer: layer,
                    playback: playback,
                    abrirDepois: abrirDepois,
                  ),
                ),
              ],
            ),
          ),
        );
      },
    ),
  );
}

/// Selecionar revela ferramentas sem abrir um modal. O contrato e os comandos
/// são os mesmos do menu completo; as utilidades continuam em Mais ações.
class LayerToolsDock extends ConsumerWidget {
  const LayerToolsDock({
    super.key,
    required this.layer,
    required this.playback,
    required this.onAction,
    required this.onMore,
  });

  final Layer layer;
  final PlaybackController playback;
  final ValueChanged<LayerMenuAction> onAction;
  final VoidCallback onMore;

  @override
  Widget build(BuildContext context, WidgetRef ref) => ColoredBox(
    color: AmColors.panel,
    child: LayoutBuilder(
      builder: (context, constraints) {
        final tileHeight = ((constraints.maxHeight - 48) / 2).clamp(48.0, 68.0);
        return SingleChildScrollView(
          padding: const EdgeInsets.fromLTRB(10, 0, 10, 8),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Row(
                children: [
                  const Expanded(
                    child: Text(
                      'Ferramentas da camada',
                      style: TextStyle(color: AmColors.muted, fontSize: 12),
                    ),
                  ),
                  TextButton(
                    key: const ValueKey('layer-more-actions'),
                    style: TextButton.styleFrom(
                      minimumSize: const Size(48, 32),
                      padding: const EdgeInsets.symmetric(horizontal: 8),
                      tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                    ),
                    onPressed: onMore,
                    child: const Text(
                      'Mais ações',
                      style: TextStyle(color: AmColors.accent, fontSize: 12),
                    ),
                  ),
                ],
              ),
              ..._fileiras(
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
            ],
          ),
        );
      },
    ),
  );
}

/// UM TILE DA GRADE: icone, rotulo e o que ele abre.
typedef _Tile = ({IconData icone, String rotulo, VoidCallback onTap});

/// AS FILEIRAS: tres tiles largos em cima, o resto embaixo.
///
/// A grade nunca reorganiza porque a ordem vem do enum, e o enum nao
/// muda. O que muda e QUAIS secoes o tipo tem.
List<Widget> _fileiras(
  Set<AmSecao> secoes,
  _Tile? Function(AmSecao) tile, {
  double tileHeight = 68,
}) {
  final visiveis = [
    for (final s in AmSecao.values)
      if (secoes.contains(s)) ?tile(s),
  ];
  assert(
    visiveis.length <= kAmMaximoSecoes,
    'a grade estourou: ${visiveis.length} secoes',
  );
  if (visiveis.isEmpty) return const [];

  final primeira = visiveis.take(3).toList();
  final segunda = visiveis.skip(3).toList();

  Widget linha(List<_Tile> tiles, int vagas) => Row(
    children: [
      for (var i = 0; i < vagas; i++) ...[
        if (i > 0) const SizedBox(width: 8),
        if (i < tiles.length)
          _MenuTile(
            height: tileHeight,
            icon: tiles[i].icone,
            label: tiles[i].rotulo,
            onTap: tiles[i].onTap,
          )
        else
          // O lugar continua reservado para a grade nao se
          // reorganizar quando o tipo muda.
          const Spacer(),
      ],
    ],
  );

  return [
    linha(primeira, 3),
    if (segunda.isNotEmpty) ...[
      const SizedBox(height: 8),
      linha(segunda, segunda.length < 4 ? segunda.length : 4),
    ],
  ];
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
      icone: CupertinoIcons.move,
      rotulo: 'Mover e\ntransf.',
      onTap: () => fecharCom(LayerMenuAction.transform),
    ),
    AmSecao.corPreenchimento => (
      icone: CupertinoIcons.paintbrush,
      rotulo: 'Cor e\npreench.',
      onTap: () {
        // Cena 3D: a cor mora no material de cada objeto. Elemento 3D
        // edita cor e forma no sheet proprio.
        if (layer is Scene3DLayer) {
          abrirDepois(() => showScene3DSheet(context, ref, layer.id));
        } else if (layer is Element3DLayer) {
          abrirDepois(() => showElement3DSheet(context, ref, layer.id));
        } else {
          fecharCom(LayerMenuAction.colorFill);
        }
      },
    ),
    AmSecao.bordaSombra => (
      icone: CupertinoIcons.square_on_square,
      rotulo: 'Borda e\nsombra',
      onTap: () {
        // Forma: o traco vetorial (espessura, cor, tracejado animavel)
        // mora no painel da forma; a sombra fica nos estilos.
        if (layer is ShapeLayer) {
          fecharCom(LayerMenuAction.stroke);
        } else {
          abrirDepois(
            () => showLayerStylesSheet(context, ref, layer.id, playback),
          );
        }
      },
    ),
    AmSecao.mesclarOpacidade => (
      icone: CupertinoIcons.circle_lefthalf_fill,
      rotulo: 'Mesclar e\nopacidade',
      onTap: () => fecharCom(LayerMenuAction.blending),
    ),
    AmSecao.volume => (
      icone: CupertinoIcons.speaker_2,
      rotulo: 'Volume',
      onTap: () => abrirDepois(() => showAudioSheet(context, ref, layer.id)),
    ),
    AmSecao.fade => (
      icone: CupertinoIcons.slider_horizontal_below_rectangle,
      rotulo: 'Fade',
      onTap: () => abrirDepois(() => showAudioSheet(context, ref, layer.id)),
    ),
    AmSecao.editarForma => (
      icone: CupertinoIcons.slider_horizontal_below_rectangle,
      rotulo: 'Editar\nforma',
      onTap: () => fecharCom(LayerMenuAction.editShape),
    ),
    AmSecao.clonar => (
      icone: CupertinoIcons.circle_grid_3x3,
      rotulo: 'Clonar',
      onTap: () =>
          abrirDepois(() => showGridSheet(context, ref, layer.id, playback)),
    ),
    AmSecao.editarTexto => (
      icone: CupertinoIcons.textformat,
      rotulo: 'Editar\ntexto',
      onTap: () => fecharCom(LayerMenuAction.editText),
    ),
    AmSecao.editarLegendas => (
      icone: CupertinoIcons.captions_bubble,
      rotulo: 'Editar\nlegendas',
      onTap: () => abrirDepois(
        () => showCaptionCuesSheet(context, ref, layer.id, playback),
      ),
    ),
    AmSecao.particulas => (
      icone: CupertinoIcons.sparkles,
      rotulo: 'Particulas',
      onTap: () =>
          abrirDepois(() => showParticlesSheet(context, ref, layer.id)),
    ),
    AmSecao.cena3d => (
      icone: CupertinoIcons.cube_box,
      rotulo: layer is Scene3DLayer ? 'Cena 3D' : 'Elemento\n3D',
      // UMA CENA 3D SE EDITA NO ESTUDIO: e la que se orbita, se escolhe
      // objeto e se alinha camera. Esta secao abria a ficha de
      // parametros, e o Estudio ficava atras de um botao de texto no
      // cabecalho dela — tres niveis ate o lugar onde o trabalho
      // acontece. Agora e o contrario: o Estudio abre direto, e a ficha
      // continua a um toque de dentro dele.
      onTap: () => abrirDepois(
        () => layer is Scene3DLayer
            ? openScene3DStudio(context, ref, layer.id)
            : showElement3DSheet(context, ref, layer.id),
      ),
    ),
    AmSecao.presets => (
      icone: CupertinoIcons.square_stack_3d_down_right,
      rotulo: 'Presets',
      onTap: () => abrirDepois(
        () => showEffectPresetsSheet(context, ref, layer.id, playback),
      ),
    ),
    AmSecao.efeitos => (
      icone: CupertinoIcons.wand_stars,
      rotulo: 'Efeitos',
      onTap: () => fecharCom(LayerMenuAction.effects),
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
                child: Text(
                  'Camadas da grade (ordem = indice)',
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
                            title: Text(
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
                    child: Text(
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
            return Padding(
              padding: const EdgeInsets.only(bottom: 6),
              child: Row(
                children: [
                  SizedBox(
                    width: 92,
                    child: Text(
                      label,
                      style: const TextStyle(
                        fontSize: 13,
                        color: AmColors.muted,
                      ),
                    ),
                  ),
                  Expanded(
                    child: AmTickRuler(
                      value: value,
                      min: min,
                      max: max,
                      unitsPerPixel: (max - min) / 420,
                      height: 42,
                      onChanged: (v) {
                        onChanged(v);
                        setSheetState(() {});
                      },
                    ),
                  ),
                  SizedBox(
                    width: 56,
                    child: Text(
                      display,
                      textAlign: TextAlign.center,
                      style: const TextStyle(
                        fontSize: 13,
                        color: AmColors.accent,
                      ),
                    ),
                  ),
                ],
              ),
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
            return Padding(
              padding: const EdgeInsets.only(bottom: 6),
              child: Row(
                children: [
                  SizedBox(
                    width: 92,
                    child: Text(
                      label,
                      style: const TextStyle(
                        fontSize: 13,
                        color: AmColors.muted,
                      ),
                    ),
                  ),
                  Expanded(
                    child: AmTickRuler(
                      value: track.valueAt(local) * scale,
                      min: min,
                      max: max,
                      unitsPerPixel: (max - min) / 420,
                      height: 42,
                      onChanged: (v) {
                        controller.editGridParam(nullId, key, t, v / scale);
                        setSheetState(() {});
                      },
                    ),
                  ),
                  SizedBox(
                    width: 48,
                    child: Text(
                      display,
                      textAlign: TextAlign.center,
                      style: const TextStyle(
                        fontSize: 13,
                        color: AmColors.accent,
                      ),
                    ),
                  ),
                  CupertinoButton(
                    padding: const EdgeInsets.only(left: 2),
                    onPressed: () {
                      controller.toggleGridParamKeyframe(nullId, key, t);
                      setSheetState(() {});
                    },
                    child: Icon(
                      track.hasKeyframeAt(local)
                          ? CupertinoIcons.rhombus_fill
                          : CupertinoIcons.rhombus,
                      size: 17,
                      color: track.isAnimated
                          ? AmColors.accent
                          : AmColors.muted,
                    ),
                  ),
                  // Curve editor POR PARAMETRO: cada trilha da grade tem
                  // sua propria curva de easing por segmento.
                  CupertinoButton(
                    padding: EdgeInsets.zero,
                    onPressed: () {
                      if (track.keyframes.length < 2) {
                        showReasonToast(
                          context,
                          'Crie 2+ keyframes em "$label" para editar a curva',
                        );
                        return;
                      }
                      showGridCurveSheet(
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
                      );
                    },
                    child: Icon(
                      CupertinoIcons.graph_square,
                      size: 17,
                      color: track.keyframes.length >= 2
                          ? AmColors.accent
                          : AmColors.muted,
                    ),
                  ),
                ],
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
                  Row(
                    children: [
                      const Expanded(
                        child: Text(
                          'Modulo Grade',
                          style: TextStyle(
                            fontSize: 17,
                            fontWeight: FontWeight.w700,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      if (rig != null)
                        GestureDetector(
                          onTap: () {
                            controller.removeGrid(nullId);
                            setSheetState(() {});
                          },
                          child: const Text(
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
                      child: Text(
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
                            child: GestureDetector(
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
                                child: Text(
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
                        const Text(
                          'Embaralhar',
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
                        const Text(
                          'Proximidade',
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
                        const Text(
                          'Nulo controlador',
                          style: TextStyle(fontSize: 13, color: AmColors.muted),
                        ),
                        const SizedBox(width: 10),
                        Expanded(
                          child: GestureDetector(
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
                              child: Text(
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
                        child: Text(
                          'Escala do nulo -> espacamento/raio · Rotacao Z '
                          '-> rotacao da grade · Rotacao Y -> twist.',
                          style: TextStyle(fontSize: 11, color: AmColors.muted),
                        ),
                      ),
                    if (rig.proximity?.enabled ?? false) ...[
                      const SizedBox(height: 6),
                      const Text(
                        'O effector e uma ESFERA 3D: raio 200 tambem '
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
            return Padding(
              padding: const EdgeInsets.only(bottom: 6),
              child: Row(
                children: [
                  SizedBox(
                    width: 86,
                    child: Text(
                      label,
                      style: const TextStyle(
                        fontSize: 13,
                        color: AmColors.muted,
                      ),
                    ),
                  ),
                  Expanded(
                    child: AmTickRuler(
                      value: value,
                      min: min,
                      max: max,
                      unitsPerPixel: (max - min) / 420,
                      height: 44,
                      onChanged: (v) {
                        onChanged(v);
                        setSheetState(() {});
                      },
                    ),
                  ),
                  SizedBox(
                    width: 56,
                    child: Text(
                      display,
                      textAlign: TextAlign.center,
                      style: const TextStyle(
                        fontSize: 13,
                        color: AmColors.accent,
                      ),
                    ),
                  ),
                ],
              ),
            );
          }

          Widget preset(String label, BezierPath path) {
            return GestureDetector(
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
                child: Text(
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
                  const Text(
                    'Máscaras',
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
                  const Text(
                    'A primeira corta o alfa da camada; as seguintes '
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
                                child: Text(
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
                              GestureDetector(
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
                                GestureDetector(
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
                                    child: Text(
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
                                child: Text(
                                  'Inverter',
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
                            const Text(
                              'Caminho aberto nao corta; pode servir de entrada de efeito.',
                              style: TextStyle(
                                fontSize: 11,
                                color: AmColors.accent,
                              ),
                            ),
                          if (maskFeatherExceedsBounds(m, local, maskSize))
                            const Text(
                              'Aviso: caminho + feather/2 + expansao passa do limite.',
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
                              GestureDetector(
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
  final candidates = [
    ...project.layers.whereType<NullLayer>(),
    ...project.layers.where((l) => l is! NullLayer),
  ];

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
            child: Text(
              'Seguir a camada (pai)...',
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
              title: const Text(
                'Nenhum',
                style: TextStyle(color: AmColors.text),
              ),
              subtitle: const Text(
                'Solta a camada do pai',
                style: TextStyle(fontSize: 11, color: AmColors.muted),
              ),
              onTap: () {
                controller.unlinkProperty(child.id, LayerProp.parent);
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
                opacity: other.id == child.id ? 0.35 : 1,
                child: ListTile(
                  leading: Container(
                    width: 20,
                    height: 20,
                    decoration: BoxDecoration(
                      color: layerTypeColor(other),
                      borderRadius: BorderRadius.circular(6),
                    ),
                  ),
                  title: Text(
                    other.name,
                    style: const TextStyle(color: AmColors.text),
                  ),
                  subtitle: other.id == child.id
                      ? const Text(
                          'E a propria camada',
                          style: TextStyle(fontSize: 11, color: AmColors.muted),
                        )
                      : other is NullLayer
                      ? const Text(
                          'Objeto nulo',
                          style: TextStyle(fontSize: 11, color: AmColors.muted),
                        )
                      : null,
                  onTap: other.id == child.id
                      ? null
                      : () {
                          controller.linkProperty(
                            child.id,
                            LayerProp.parent,
                            other.id,
                            t,
                          );
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

        Widget row(
          String label,
          double value,
          double min,
          double max,
          double upp,
          String display,
          ValueChanged<double> onChanged,
        ) {
          return Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Row(
              children: [
                SizedBox(
                  width: 86,
                  child: Text(
                    label,
                    style: const TextStyle(fontSize: 13, color: AmColors.muted),
                  ),
                ),
                Expanded(
                  child: AmTickRuler(
                    value: value,
                    min: min,
                    max: max,
                    unitsPerPixel: upp,
                    height: 46,
                    onChanged: (v) {
                      onChanged(v);
                      setSheetState(() {});
                    },
                  ),
                ),
                SizedBox(
                  width: 64,
                  child: Text(
                    display,
                    textAlign: TextAlign.center,
                    style: const TextStyle(
                      fontSize: 14,
                      color: AmColors.accent,
                    ),
                  ),
                ),
              ],
            ),
          );
        }

        Widget chips(
          String label,
          List<String> nomes,
          int atual,
          ValueChanged<int> onPick,
        ) {
          return Padding(
            padding: const EdgeInsets.only(bottom: 10),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SizedBox(
                  width: 86,
                  child: Padding(
                    padding: const EdgeInsets.only(top: 8),
                    child: Text(
                      label,
                      style: const TextStyle(
                        fontSize: 13,
                        color: AmColors.muted,
                      ),
                    ),
                  ),
                ),
                Expanded(
                  child: Wrap(
                    spacing: 6,
                    runSpacing: 6,
                    children: [
                      for (var i = 0; i < nomes.length; i++)
                        GestureDetector(
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
                            child: Text(
                              nomes[i],
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
          );
        }

        Widget titulo(String t) => Padding(
          padding: const EdgeInsets.only(top: 6, bottom: 8),
          child: Text(
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
                const Text(
                  'Particulas 3D',
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
                        child: Text(
                          'Cor final',
                          style: TextStyle(fontSize: 13, color: AmColors.muted),
                        ),
                      ),
                      GestureDetector(
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
                          child: const Text(
                            'Nenhuma',
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
                        GestureDetector(
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
                    const Text(
                      'Cintilar',
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
                      GestureDetector(
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
                const Text(
                  'Elemento 3D',
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
                      GestureDetector(
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
                          child: Text(
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
                Row(
                  children: [
                    const SizedBox(
                      width: 86,
                      child: Text(
                        'Tamanho',
                        style: TextStyle(fontSize: 13, color: AmColors.muted),
                      ),
                    ),
                    Expanded(
                      child: AmTickRuler(
                        value: layer.size,
                        min: 20,
                        max: 600,
                        unitsPerPixel: 1.4,
                        height: 46,
                        onChanged: (v) {
                          controller.updateElement3D(
                            layerId,
                            (e) => e.copyElement3D(size: v),
                          );
                          setSheetState(() {});
                        },
                      ),
                    ),
                    SizedBox(
                      width: 56,
                      child: Text(
                        amNumber(layer.size, 0),
                        textAlign: TextAlign.center,
                        style: const TextStyle(
                          fontSize: 14,
                          color: AmColors.accent,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                Row(
                  children: [
                    const Text(
                      'Arestas',
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
                      GestureDetector(
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
                Row(
                  children: [
                    const SizedBox(
                      width: 86,
                      child: Text(
                        'Reflexo',
                        style: TextStyle(fontSize: 13, color: AmColors.muted),
                      ),
                    ),
                    Expanded(
                      child: AmTickRuler(
                        value: layer.reflect,
                        min: 0,
                        max: 1,
                        unitsPerPixel: 1 / 420,
                        height: 46,
                        onChanged: (v) {
                          controller.updateElement3D(
                            layerId,
                            (e) => e.copyElement3D(reflect: v),
                          );
                          setSheetState(() {});
                        },
                      ),
                    ),
                    SizedBox(
                      width: 56,
                      child: Text(
                        amNumber(layer.reflect * 100, 0),
                        textAlign: TextAlign.center,
                        style: const TextStyle(
                          fontSize: 14,
                          color: AmColors.accent,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: [
                    for (final k in EnvironmentKind.values)
                      GestureDetector(
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
                          child: Text(
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
                        child: Text(
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
                            GestureDetector(
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
                                child: Text(
                                  nome,
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
                          child: Text(
                            'Degrade',
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
                              GestureDetector(
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
                Row(
                  children: [
                    const SizedBox(
                      width: 86,
                      child: Text(
                        'Brilho',
                        style: TextStyle(fontSize: 13, color: AmColors.muted),
                      ),
                    ),
                    Expanded(
                      child: AmTickRuler(
                        value: layer.shininess,
                        min: 0,
                        max: 1,
                        unitsPerPixel: 0.003,
                        height: 46,
                        onChanged: (v) {
                          controller.updateElement3D(
                            layerId,
                            (e) => e.copyElement3D(shininess: v),
                          );
                          setSheetState(() {});
                        },
                      ),
                    ),
                    SizedBox(
                      width: 56,
                      child: Text(
                        amNumber(layer.shininess * 100, 0),
                        textAlign: TextAlign.center,
                        style: const TextStyle(
                          fontSize: 14,
                          color: AmColors.accent,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                // MODELO IMPORTADO: OBJ ou FBX (ASCII) no lugar do solido.
                Row(
                  children: [
                    const SizedBox(
                      width: 86,
                      child: Text(
                        'Modelo',
                        style: TextStyle(fontSize: 13, color: AmColors.muted),
                      ),
                    ),
                    Expanded(
                      child: Text(
                        _descricaoDoModelo(layer),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    GestureDetector(
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
                            Text(
                              'OBJ / FBX',
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
                      GestureDetector(
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
                      child: Text(
                        'Imagem',
                        style: TextStyle(fontSize: 13, color: AmColors.muted),
                      ),
                    ),
                    Expanded(
                      child: Text(
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
                    GestureDetector(
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
                            Text(
                              'Escolher',
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
                      GestureDetector(
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
                const Text(
                  'Gire com a rotacao X/Y/Z normal da camada — ou '
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
            return Padding(
              padding: const EdgeInsets.only(bottom: 6),
              child: Row(
                children: [
                  SizedBox(
                    width: 88,
                    child: Text(
                      label,
                      style: const TextStyle(
                        fontSize: 13,
                        color: AmColors.muted,
                      ),
                    ),
                  ),
                  Expanded(
                    child: AmTickRuler(
                      value: track.valueAt(local) * scale,
                      min: min,
                      max: max,
                      unitsPerPixel: (max - min) / 420,
                      height: 42,
                      onChanged: (v) {
                        controller.editShapeParam(layerId, key, t, v / scale);
                        setSheetState(() {});
                      },
                    ),
                  ),
                  SizedBox(
                    width: 48,
                    child: Text(
                      display,
                      textAlign: TextAlign.center,
                      style: const TextStyle(
                        fontSize: 13,
                        color: AmColors.accent,
                      ),
                    ),
                  ),
                  CupertinoButton(
                    padding: const EdgeInsets.only(left: 2),
                    onPressed: () {
                      controller.toggleShapeParamKeyframe(layerId, key, t);
                      setSheetState(() {});
                    },
                    child: Icon(
                      track.hasKeyframeAt(local)
                          ? CupertinoIcons.rhombus_fill
                          : CupertinoIcons.rhombus,
                      size: 17,
                      color: track.isAnimated
                          ? AmColors.accent
                          : AmColors.muted,
                    ),
                  ),
                  CupertinoButton(
                    padding: EdgeInsets.zero,
                    onPressed: () {
                      if (track.keyframes.length < 2) {
                        showReasonToast(
                          context,
                          'Crie 2+ keyframes em "$label" para editar a curva',
                        );
                        return;
                      }
                      showTrackCurveSheet(
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
                      );
                    },
                    child: Icon(
                      CupertinoIcons.graph_square,
                      size: 17,
                      color: track.keyframes.length >= 2
                          ? AmColors.accent
                          : AmColors.muted,
                    ),
                  ),
                ],
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
                  const Text(
                    'Forma — geometria',
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
                      child: const Text('Gradiente: cores, posicoes e alcance'),
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
                          Text(
                            'Editar nos do caminho',
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
                    const Text(
                      'Esta forma e um caminho desenhado (sem '
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
                        child: const Text(
                          'Converter para parametrica',
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
                          GestureDetector(
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
                              child: Text(
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
                          const Text(
                            'Unidade do canto',
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
                              child: GestureDetector(
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
                                  child: Text(
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
                    const Text(
                      'Tamanho muda a GEOMETRIA (traco constante). '
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
                      child: Text(
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
                    GestureDetector(
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
                        child: Text(
                          'Estilo',
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
                const Text(
                  'Toque no tempo para ouvir o trecho; corrija o texto '
                  'direto. Editar trava o cue.',
                  style: TextStyle(fontSize: 11, color: AmColors.muted),
                ),
                const SizedBox(height: 6),
                Expanded(
                  child: cues.isEmpty
                      ? const Center(
                          child: Text(
                            'Sem cues nesta camada.',
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
                                  GestureDetector(
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
                                      child: Text(
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

/// Folha de PRESETS de efeito: lista os presets de fabrica e aplica no
/// playhead. Existe porque a grade tem um botao "Presets" proprio e os
/// presets do painel de efeitos vivem dentro de um metodo privado do
/// EffectsPanel (sem parametro para abrir ja neles); abrir por sheet
/// evita mexer no painel e no switch exaustivo de LayerMenuAction.
Future<void> showEffectPresetsSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  final fabrica = factoryPresets();
  // Os presets DA PESSOA vem de fora do projeto: a mesma lista em todo
  // projeto, e o que se salvou num aparece nos outros.
  final store = EffectPresetStore.instance;
  await store.load();
  if (!context.mounted) return;

  await showParamSheet(
    context,
    title: 'Presets de efeito',
    heightFactor: 0.5,
    builder: (sheetContext) => ValueListenableBuilder<int>(
      valueListenable: store.revision,
      builder: (sheetContext, _, _) {
        final meus = store.presets;
        final todos = [...meus, ...fabrica];
        return SafeArea(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Padding(
                padding: EdgeInsets.fromLTRB(18, 12, 18, 6),
                child: Row(
                  children: [
                    Icon(
                      CupertinoIcons.square_stack_3d_down_right,
                      size: 18,
                      color: AmColors.accent,
                    ),
                    SizedBox(width: 8),
                    Text(
                      'Presets de efeito',
                      style: TextStyle(
                        fontSize: 17,
                        fontWeight: FontWeight.w700,
                        color: AmColors.text,
                      ),
                    ),
                  ],
                ),
              ),
              if (meus.isEmpty)
                const Padding(
                  padding: EdgeInsets.fromLTRB(18, 0, 18, 6),
                  child: Text(
                    'Para guardar um efeito seu: no cartao do efeito, '
                    'menu (...) > "Salvar como preset".',
                    style: TextStyle(fontSize: 11, color: AmColors.muted),
                  ),
                ),
              Expanded(
                // PREGUICOSO (builder): so o visivel existe; a lista e
                // longa e meia duzia cabe na tela.
                child: ListView.builder(
                  itemCount: todos.length,
                  itemBuilder: (_, i) {
                    final p = todos[i];
                    final meu = i < meus.length;
                    return Material(
                      color: Colors.transparent,
                      child: ListTile(
                        leading: Icon(
                          meu
                              ? CupertinoIcons.person_crop_circle
                              : CupertinoIcons.square_stack_3d_down_right,
                          size: 20,
                          color: AmColors.accent,
                        ),
                        title: Text(
                          p.name,
                          style: const TextStyle(
                            fontSize: 14,
                            color: AmColors.text,
                          ),
                        ),
                        subtitle: Text(
                          '${meu ? 'Meu preset' : p.category} · '
                          '${p.effects.length} efeito(s)',
                          style: const TextStyle(
                            fontSize: 11,
                            color: AmColors.muted,
                          ),
                        ),
                        trailing: meu
                            ? GestureDetector(
                                onTap: () => store.remove(p.id),
                                child: const Padding(
                                  padding: EdgeInsets.all(6),
                                  child: Icon(
                                    CupertinoIcons.trash,
                                    size: 18,
                                    color: AmColors.muted,
                                  ),
                                ),
                              )
                            : null,
                        onTap: () {
                          controller.applyPreset(
                            layerId,
                            p,
                            at: playback.time.value,
                          );
                          AureaSnack.show(
                            context,
                            'Preset "${p.name}" aplicado',
                            actionLabel: 'Desfazer',
                            onAction: controller.undo,
                          );
                        },
                      ),
                    );
                  },
                ),
              ),
            ],
          ),
        );
      },
    ),
  );
}

/// Icone PEQUENO de utilidade (fileira de cima do menu da camada): acao
/// rapida ou sheet que nao troca a pagina do editor. Nao entra na grade
/// porque nao e editor de propriedade. Sem bolha, sem ripple: so o icone
/// num alvo de 40 px; `aceso` sinaliza estado ligado (ex.: mudo ativo).
class _UtilIcon extends StatelessWidget {
  const _UtilIcon({
    required this.icon,
    required this.label,
    required this.onTap,
    this.aceso = false,
  });

  final IconData icon;
  final String label;
  final VoidCallback onTap;
  final bool aceso;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      label: label,
      button: true,
      child: Tooltip(
        message: label,
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTap: onTap,
          child: SizedBox(
            width: 42,
            height: 40,
            child: Icon(
              icon,
              size: 20,
              color: aceso ? AmColors.accent : AmColors.text,
            ),
          ),
        ),
      ),
    );
  }
}

/// Botao GRANDE da grade de secoes: superficie chip com cantos
/// arredondados e SEM borda, icone em cima e rotulo embaixo, 68 px de
/// altura para o dedo acertar sem mirar.
class _MenuTile extends StatelessWidget {
  const _MenuTile({
    required this.icon,
    required this.label,
    required this.onTap,
    this.height = 68,
  });

  final IconData icon;
  final String label;
  final VoidCallback onTap;
  final double height;

  @override
  Widget build(BuildContext context) {
    return Expanded(
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Container(
          height: height,
          padding: const EdgeInsets.symmetric(horizontal: 6),
          decoration: BoxDecoration(
            color: AmColors.chip,
            borderRadius: BorderRadius.circular(3),
          ),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon, size: height < 60 ? 18 : 22, color: AmColors.text),
              SizedBox(height: height < 60 ? 2 : 6),
              Text(
                label,
                textAlign: TextAlign.center,
                maxLines: 2,
                style: const TextStyle(
                  fontSize: 11,
                  height: 1.1,
                  fontWeight: FontWeight.w600,
                  color: AmColors.text,
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
  ('Screen', BlendMode.screen),
  ('Color Dodge', BlendMode.colorDodge),
  ('Adicionar', BlendMode.plus),
  ('Overlay', BlendMode.overlay),
  ('Soft Light', BlendMode.softLight),
  ('Hard Light', BlendMode.hardLight),
  ('Diferenca', BlendMode.difference),
  ('Exclusao', BlendMode.exclusion),
  ('Matiz', BlendMode.hue),
  ('Saturacao', BlendMode.saturation),
  ('Cor', BlendMode.color),
  ('Luminosidade', BlendMode.luminosity),
];

/// Uma opcao de mescla na fileira.
class _BlendChip extends StatelessWidget {
  const _BlendChip({
    required this.label,
    required this.aceso,
    required this.onTap,
  });

  final String label;
  final bool aceso;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
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
          Icon(
            CupertinoIcons.circle_lefthalf_fill,
            size: 18,
            color: aceso ? AmColors.accent : AmColors.muted,
          ),
          const SizedBox(height: 4),
          Text(
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
    final project = ref.watch(editorControllerProvider);
    final id = ref.watch(selectedLayerProvider);
    final layer = id == null ? null : project.layerById(id);
    if (layer == null || id == null) {
      return const ColoredBox(color: AmColors.panel);
    }
    final controller = ref.read(editorControllerProvider.notifier);

    // Escuta o relogio: `t` sempre atual (keyframe cai no playhead real).
    return ValueListenableBuilder<Duration>(
      valueListenable: widget.playback.time,
      builder: (context, t, _) {
        final local = layer.localTime(t);
        final opacity = layer.opacity.valueAt(local);

        final tab = ref.watch(_blendTabProvider);
        final mask = layer.masks.isEmpty ? null : layer.masks.last;
        final pathAnimated = mask?.path.isAnimated ?? false;
        final pathKf = mask?.path.hasKeyframeAt(local) ?? false;

        return ColoredBox(
          color: AmColors.panel,
          child: Row(
            children: [
              Column(
                children: [
                  AmRailButton(
                    onTap: widget.onBack,
                    child: const Icon(
                      CupertinoIcons.chevron_back,
                      size: 24,
                      color: AmColors.text,
                    ),
                  ),
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
                          ? layer.opacity.isAnimated
                          : tab == _BlendTab.mask && pathAnimated,
                      filled: tab == _BlendTab.opacity
                          ? layer.opacity.hasKeyframeAt(local)
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
                          opacity,
                          t,
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

  Widget _opacity(EditorController c, String id, double value, Duration t) =>
      Padding(
        padding: const EdgeInsets.fromLTRB(10, 12, 16, 10),
        child: Column(
          children: [
            AmValueChip(
              text: amNumber(value * 100, 0),
              label: 'Opacidade',
              width: 150,
            ),
            const SizedBox(height: 8),
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

  Widget _blending(EditorController c, String id, Layer layer) => Padding(
    padding: const EdgeInsets.fromLTRB(10, 10, 16, 10),
    child: SizedBox(
      height: 68,
      child: ListView(
        scrollDirection: Axis.horizontal,
        children: [
          for (final (label, mode) in amBlendModes)
            _BlendChip(
              label: label,
              aceso: layer.customBlend == null && layer.blendMode == mode,
              onTap: () => c.setBlendMode(id, mode),
            ),
          for (final extra in AureaBlend.values)
            _BlendChip(
              label: aureaBlendLabel(extra),
              aceso: layer.customBlend == extra,
              onTap: () => c.setCustomBlend(id, extra),
            ),
        ],
      ),
    ),
  );

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
      GestureDetector(
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
          child: Text(
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
            const Text(
              'Sete modos, pilha, feather X/Y, opacidade e caminho.',
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
          const Text(
            'Pronto · Revelar',
            style: TextStyle(
              fontSize: 14,
              fontWeight: FontWeight.w700,
              color: AmColors.text,
            ),
          ),
          const SizedBox(height: 4),
          const Text(
            'Um toque cria mascara e keyframes reais com mola.',
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
        Text(
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
              child: Text(
                'Inverter',
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
          const Text(
            'Caminho aberto nao recorta. Feche no Edit Points.',
            style: TextStyle(fontSize: 11, color: AmColors.accent),
          ),
        if (maskFeatherExceedsBounds(mask, local, size))
          const Text(
            'Aviso: feather e expansao passam do limite.',
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
      SizedBox(
        width: 70,
        child: Text(
          label,
          style: const TextStyle(fontSize: 12, color: AmColors.muted),
        ),
      ),
      Expanded(
        child: AmTickRuler(
          value: value,
          min: min,
          max: max,
          unitsPerPixel: (max - min) / 360,
          height: 38,
          onChanged: changed,
        ),
      ),
      SizedBox(
        width: 38,
        child: Text(
          amNumber(value, 0),
          textAlign: TextAlign.center,
          style: const TextStyle(fontSize: 12, color: AmColors.accent),
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
            const Text(
              'Pronto · Recortar',
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
            const SizedBox(height: 6),
            Text(
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
        Text(
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
          const Text(
            'A fonte some enquanto o recorte estiver ligado, volta ao '
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
            child: Text(
              'Nulo controlador da grade',
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
              child: Text(
                'Crie outro Nulo 3D para usar como controlador '
                '(o proprio nulo da grade nao conta).',
                style: TextStyle(fontSize: 13, color: AmColors.muted),
              ),
            ),
          Material(
            color: Colors.transparent,
            child: ListTile(
              title: const Text(
                'Nenhum',
                style: TextStyle(color: AmColors.muted),
              ),
              onTap: () => Navigator.of(sheetContext).pop(''),
            ),
          ),
          for (final other in nulls)
            Material(
              color: Colors.transparent,
              child: ListTile(
                title: Text(
                  other.name,
                  style: const TextStyle(color: AmColors.text),
                ),
                subtitle: const Text(
                  'Escala/rotacao dele passam a modular a grade',
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
            child: Text(
              'Usar como matte...',
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
                  title: Text(
                    other.name,
                    style: const TextStyle(color: AmColors.text),
                  ),
                  subtitle: const Text(
                    'A fonte fica oculta na cena',
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
    if (layer == null || id == null) {
      return const ColoredBox(color: AmColors.panel);
    }
    final controller = ref.read(editorControllerProvider.notifier);
    final current = switch (layer) {
      ShapeLayer l => l.primaryColor,
      TextLayer l => l.color,
      _ => null,
    };

    return ColoredBox(
      color: AmColors.panel,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Column(
            children: [
              AmRailButton(
                onTap: onBack,
                child: const Icon(
                  CupertinoIcons.chevron_back,
                  size: 24,
                  color: AmColors.text,
                ),
              ),
            ],
          ),
          Expanded(
            child: current == null
                ? const Center(
                    child: Text(
                      'Esta camada nao tem cor editavel.',
                      style: TextStyle(color: AmColors.muted, fontSize: 13),
                    ),
                  )
                : ValueListenableBuilder<Duration>(
                    valueListenable: playback.time,
                    builder: (context, t, _) => ListView(
                      padding: const EdgeInsets.fromLTRB(4, 16, 16, 16),
                      children: [
                        // QUALQUER COR: espectro completo, hex e alfa.
                        // Os atalhos abaixo continuam para o caso comum.
                        GestureDetector(
                          onTap: () async {
                            void set(Color c) {
                              if (layer is ShapeLayer) {
                                controller.setShapePrimaryColor(id, c);
                              } else if (layer is TextLayer) {
                                controller.editTextLayer(id, color: c);
                              }
                            }

                            final picked = await showColorPicker(
                              context,
                              initial: current,
                              onChanged: set,
                            );
                            if (picked != null) set(picked);
                          },
                          child: Container(
                            margin: const EdgeInsets.only(bottom: 16),
                            padding: const EdgeInsets.symmetric(
                              horizontal: 12,
                              vertical: 11,
                            ),
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
                                    color: current,
                                    borderRadius: BorderRadius.circular(9),
                                    border: Border.all(color: Colors.white24),
                                  ),
                                ),
                                const SizedBox(width: 12),
                                const Expanded(
                                  child: Text(
                                    'Escolher qualquer cor',
                                    style: TextStyle(
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
                        Wrap(
                          spacing: 14,
                          runSpacing: 14,
                          children: [
                            for (final c in _swatches)
                              GestureDetector(
                                onTap: () {
                                  if (layer is ShapeLayer) {
                                    controller.setShapePrimaryColor(id, c);
                                  } else if (layer is TextLayer) {
                                    controller.editTextLayer(id, color: c);
                                  }
                                },
                                child: Container(
                                  width: 48,
                                  height: 48,
                                  decoration: BoxDecoration(
                                    color: c,
                                    shape: BoxShape.circle,
                                    border: Border.all(
                                      color: current.toARGB32() == c.toARGB32()
                                          ? AmColors.accent
                                          : Colors.white24,
                                      width: current.toARGB32() == c.toARGB32()
                                          ? 3
                                          : 1,
                                    ),
                                  ),
                                ),
                              ),
                          ],
                        ),
                        if (layer is ShapeLayer)
                          _ShapeOperators(
                            layer: layer,
                            layerId: id,
                            globalTime: t,
                          ),
                      ],
                    ),
                  ),
          ),
        ],
      ),
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
            child: Text(
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
            child: Text(
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
        const Text(
          'OPERADORES',
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
                        child: Text(
                          'Trim Paths',
                          style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      // PR-M8: Individually (cascata) x Simultaneously.
                      GestureDetector(
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
                          child: Text(
                            item.individually ? 'Individual' : 'Continuo',
                            style: const TextStyle(
                              fontSize: 11,
                              color: AmColors.accent,
                            ),
                          ),
                        ),
                      ),
                      GestureDetector(
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
                        child: Text(
                          'Repeater',
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
                      Text(
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
                      GestureDetector(
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
                        child: Text(
                          'Morph  '
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
                      GestureDetector(
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
                      Text(
                        _opName(item),
                        style: const TextStyle(
                          fontSize: 12,
                          fontWeight: FontWeight.w600,
                          color: AmColors.text,
                        ),
                      ),
                      const Spacer(),
                      if (item is MergePathsOperator)
                        GestureDetector(
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
                            child: Text(
                              mergeModeLabel(item.mode),
                              style: const TextStyle(
                                fontSize: 11,
                                color: AmColors.accent,
                              ),
                            ),
                          ),
                        ),
                      const SizedBox(width: 8),
                      GestureDetector(
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
              GestureDetector(
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
                  child: Text(
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
        Row(
          children: [
            CupertinoButton(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
              onPressed: () =>
                  controller.addShapeOperator(layerId, repeater: false),
              child: const Text(
                '+ Trim Paths',
                style: TextStyle(fontSize: 12, color: AmColors.accent),
              ),
            ),
            CupertinoButton(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
              onPressed: () =>
                  controller.addShapeOperator(layerId, repeater: true),
              child: const Text(
                '+ Repeater',
                style: TextStyle(fontSize: 12, color: AmColors.accent),
              ),
            ),
            CupertinoButton(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
              onPressed: () => _pickMorphTarget(context, ref),
              child: const Text(
                '+ Morfar',
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
              const Text(
                'Morfar para...',
                style: TextStyle(
                  fontSize: 17,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text,
                ),
              ),
              const SizedBox(height: 6),
              const Text(
                'A forma atual vira a origem; anime o Progresso com '
                'keyframes para ver a transformacao.',
                style: TextStyle(fontSize: 12, color: AmColors.muted),
              ),
              const SizedBox(height: 12),
              Wrap(
                spacing: 10,
                runSpacing: 10,
                children: [
                  for (final (label, path) in options)
                    GestureDetector(
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
                        child: Text(
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
                const Text(
                  'Extrude 3D',
                  style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 6),
                Text(
                  inclinada
                      ? 'Espessura da camada. Gire em X ou Y para ver a lateral.'
                      : 'A espessura so aparece com a camada girada em X ou Y (Mover e transf. > Rotacao 3D).',
                  style: const TextStyle(fontSize: 12.5, color: AmColors.muted),
                ),
                const SizedBox(height: 14),
                Row(
                  children: [
                    const SizedBox(
                      width: 86,
                      child: Text(
                        'Espessura',
                        style: TextStyle(fontSize: 13, color: AmColors.muted),
                      ),
                    ),
                    Expanded(
                      child: AmTickRuler(
                        value: atual,
                        min: 0,
                        max: 400,
                        unitsPerPixel: 1,
                        height: 46,
                        onChanged: (v) {
                          controller.setLayerExtrude(layerId, v);
                          setSheetState(() {});
                        },
                      ),
                    ),
                    SizedBox(
                      width: 56,
                      child: Text(
                        amNumber(atual, 0),
                        textAlign: TextAlign.center,
                        style: const TextStyle(
                          fontSize: 14,
                          color: AmColors.accent,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                Wrap(
                  spacing: 8,
                  children: [
                    for (final v in const [0.0, 20.0, 40.0, 80.0, 160.0])
                      GestureDetector(
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
                          child: Text(
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
        title: const Text('Modelo pesado'),
        content: Padding(
          padding: const EdgeInsets.only(top: 8),
          child: Text(
            '${resultado.faceCount} faces'
            '${resultado.truncated ? ' (o app usa as primeiras $kMeshFacesMax)' : ''}. '
            'Modelos assim podem travar em celulares fracos; em aparelhos '
            'potentes rodam bem. Importar mesmo assim?',
          ),
        ),
        actions: [
          CupertinoDialogAction(
            onPressed: () => Navigator.of(c).pop(false),
            child: const Text('Cancelar'),
          ),
          CupertinoDialogAction(
            isDefaultAction: true,
            onPressed: () => Navigator.of(c).pop(true),
            child: const Text('Importar'),
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
      title: Text(titulo),
      content: Padding(
        padding: const EdgeInsets.only(top: 8),
        child: Text(texto),
      ),
      actions: [
        CupertinoDialogAction(
          isDefaultAction: true,
          onPressed: () => Navigator.of(c).pop(),
          child: const Text('OK'),
        ),
      ],
    ),
  );
}
