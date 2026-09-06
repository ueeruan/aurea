import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../projects/application/projects_controller.dart';
import '../application/editor_controller.dart';
import '../application/freehand_session.dart';
import '../application/playback_controller.dart';
import '../application/preview_stats.dart';
import '../application/video_layer_manager.dart';
import '../domain/gear.dart';
import '../domain/layer.dart';
import 'am/align_sheet.dart';
import '../domain/shape.dart';
import 'am/points_panel.dart';
import 'am/shape_panel.dart';
import 'widgets/mask_node_editor.dart';
import '../../../core/ui/snack.dart';
import '../../projects/application/thumbnail_service.dart';
import 'am/am_colors.dart';
import 'am/layer_look.dart';
import 'am/export_sheet.dart';
import 'am/am_widgets.dart';
import 'am/am_timeline.dart';
import 'am/apple_cascade_sheet.dart';
import 'am/curve_panel.dart';
import 'am/effects_panel.dart';
import 'am/layer_menu.dart';
import 'am/text_animators_panel.dart';
import 'am/transform_panel.dart';
import 'am/property_keyframe_context.dart';
import 'widgets/add_layer_sheet.dart';
import 'widgets/preview_stage.dart';

enum _Mode {
  main,
  transform,
  blending,
  colorFill,
  effects,
  curve,
  animators,
  // Nivel 1 (shapes): o painel da forma e o Edit Points com trackpad.
  editShape,
  editPoints,
}

/// Os quatro estados do cabecalho (observados no Alight Motion). A mesma
/// faixa, na mesma altura, muda o que mostra conforme o contexto: nome do
/// projeto, camada selecionada, selecao multipla (faixa verde) ou painel.
enum _HeaderKind { projeto, camada, multipla, painel }

/// Editor: preview, transporte, timeline com playhead central e paginas
/// de ferramenta em tela cheia.
class EditorScreen extends ConsumerStatefulWidget {
  const EditorScreen({super.key});

  @override
  ConsumerState<EditorScreen> createState() => _EditorScreenState();
}

class _EditorScreenState extends ConsumerState<EditorScreen>
    with SingleTickerProviderStateMixin {
  late final PlaybackController _playback;
  final VideoLayerManager _videos = VideoLayerManager();

  _Mode _mode = _Mode.main;
  ShapeTool _shapeTool = ShapeTool.size;
  String? _pointsItemId;
  _Mode _pointsReturn = _Mode.editShape;
  final GlobalKey<PointsPanelState> _pointsKey = GlobalKey<PointsPanelState>();
  LayerProp _curveProp = LayerProp.position;
  _Mode _curveReturn = _Mode.transform;
  TransformTool _tool = TransformTool.position;
  bool _previewExpanded = false;
  bool _adding = false;

  void _openAdd() {
    _playback.pause();
    setState(() => _adding = true);
  }

  @override
  void initState() {
    super.initState();
    RecentSheets.instance.clear();
    _playback = PlaybackController(
      vsync: this,
      durationOf: () => ref.read(editorControllerProvider).duration,
    );
    _playback.time.addListener(_syncVideos);
    _playback.playing.addListener(_syncVideos);

    // ABRIR UM PROJETO precisa montar os tocadores AGORA.
    //
    // O sync so acontecia quando o relogio andava ou quando o projeto
    // mudava — e abrir um projeto salvo nao e nenhum dos dois: o
    // openProject roda ANTES desta tela existir, e o relogio fica parado
    // no zero. Resultado: preview preto com o icone de filme ate a
    // pessoa apertar play. Uma linha de sync no primeiro quadro resolve.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) _syncVideos();
    });
  }

  void _syncVideos() {
    final project = ref.read(editorControllerProvider);
    // A midia devolve o RELOGIO MESTRE (PR-J1): o clock da composicao e
    // ancorado nela continuamente, em vez de corrigir a deriva em bloco
    // com um seek — que era a travada periodica.
    final master = _videos.sync(
      project.layers,
      _playback.time.value,
      _playback.playing.value,
    );
    if (master != null) _playback.anchorToMedia(master);
  }

  @override
  void dispose() {
    RecentSheets.instance.clear();
    _playback.dispose();
    _videos.dispose();
    super.dispose();
  }

  String get _title => switch (_mode) {
    _Mode.main => ref.read(editorControllerProvider).name,
    _Mode.transform => 'Transformar · ${labelOfProp(propOfTool(_tool))}',
    _Mode.blending => 'Mesclagem e opacidade',
    _Mode.colorFill => 'Cor e preenchimento',
    _Mode.effects => 'Efeitos',
    _Mode.curve => 'Curva · ${labelOfProp(_curveProp)}',
    _Mode.animators => 'Animacao de texto',
    _Mode.editShape => 'Editar forma',
    _Mode.editPoints => 'Editar pontos',
  };

  void _back() {
    if (_adding) {
      setState(() => _adding = false);
      return;
    }
    if (ref.read(freehandRequestProvider)) {
      ref.read(freehandRequestProvider.notifier).state = false;
      return;
    }
    // Folhas persistentes têm uma entrada local de histórico. Consumi-la
    // primeiro não remove o editor nem muda sua seleção.
    if (ModalRoute.of(context)?.willHandlePopInternally ?? false) {
      Navigator.of(context).pop();
      return;
    }
    if (_previewExpanded) {
      setState(() => _previewExpanded = false);
      return;
    }
    switch (_mode) {
      case _Mode.editPoints:
        _fecharEditPoints();
        setState(() => _mode = _pointsReturn);
        return;
      case _Mode.main:
        if (ref.read(multiSelectProvider).isNotEmpty) {
          ref.read(multiSelectProvider.notifier).state = const {};
          return;
        }
        if (ref.read(selectedLayerProvider) != null) {
          ref.read(selectedLayerProvider.notifier).state = null;
          return;
        }
        // A miniatura do projeto para a tela inicial: capturada AGORA,
        // com o palco ainda vivo; a escrita segue em segundo plano.
        ThumbnailService.instance.capture(
          previewStageKey,
          ref.read(editorControllerProvider).id,
        );
        Navigator.of(context).maybePop();
      case _Mode.curve:
        setState(() => _mode = _curveReturn);
      default:
        setState(() => _mode = _Mode.main);
    }
  }

  Set<int> _timesForProp(Layer layer, LayerProp prop) => switch (prop) {
    LayerProp.position => layer.positionTimesUs,
    LayerProp.scale => layer.scaleTimesUs,
    LayerProp.rotation => layer.rotationTimesUs,
    LayerProp.opacity => layer.opacityTimesUs,
    LayerProp.skew => layer.skewTimesUs,
    LayerProp.pivot => layer.pivotTimesUs,
    LayerProp.parent => const <int>{},
  };

  /// De qual propriedade e o keyframe que esta em [t], e como se chama.
  (LayerProp, String)? _donoDoKeyframe(Layer layer, Duration t) {
    final us = t.inMicroseconds;
    const nomes = {
      LayerProp.position: 'Posicao',
      LayerProp.rotation: 'Rotacao',
      LayerProp.scale: 'Escala',
      LayerProp.skew: 'Inclinar',
      LayerProp.pivot: 'Pivo',
      LayerProp.opacity: 'Opacidade',
    };
    for (final entrada in nomes.entries) {
      if (_timesForProp(layer, entrada.key).contains(us)) {
        return (entrada.key, entrada.value);
      }
    }
    if (layer.effectTimesUs.contains(us)) return (LayerProp.parent, 'Efeitos');
    if (layer.maskTimesUs.contains(us)) return (LayerProp.parent, 'Mascaras');
    return null;
  }

  /// TOCARAM NUM DIAMANTE APAGADO.
  ///
  /// A timeline mostra os keyframes da propriedade em EDICAO acesos e os
  /// outros apagados — e ate aqui a AM chega. O que falta la e dizer de
  /// quem sao os apagados: a pessoa ve a marca, sabe que fez alguma coisa
  /// naquele instante, e nao tem como descobrir o que. Aqui o toque
  /// responde, e leva.
  void _onForeignKeyframe(Duration t) {
    final id = ref.read(selectedLayerProvider);
    final layer = id == null
        ? null
        : ref.read(editorControllerProvider).layerById(id);
    if (layer == null) return;
    final dono = _donoDoKeyframe(layer, t);
    if (dono == null) return;
    final (prop, nome) = dono;

    final tool = switch (prop) {
      LayerProp.position => TransformTool.position,
      LayerProp.rotation => TransformTool.rotation,
      LayerProp.scale => TransformTool.scale,
      LayerProp.skew => TransformTool.skew,
      LayerProp.pivot => TransformTool.pivot,
      LayerProp.opacity => TransformTool.opacity,
      _ => null,
    };
    final podeIr = tool != null || nome == 'Efeitos' || nome == 'Mascaras';

    AureaSnack.show(
      context,
      'Este keyframe e de $nome.',
      actionLabel: podeIr ? 'Ir' : null,
      onAction: podeIr
          ? () {
              _playback.pause();
              _playback.seek(layer.startTime + t);
              setState(() {
                if (tool != null) {
                  _tool = tool;
                  _mode = _Mode.transform;
                } else if (nome == 'Efeitos') {
                  _mode = _Mode.effects;
                } else {
                  selectMaskInBlendingPanel(ref);
                  _mode = _Mode.blending;
                }
              });
            }
          : null,
    );
  }

  void _openCurve(LayerProp prop) {
    setState(() {
      _curveProp = prop;
      _curveReturn = _mode == _Mode.curve ? _curveReturn : _mode;
      _mode = _Mode.curve;
    });
  }

  Future<void> _onTapLayer(Layer layer) async {
    _playback.pause();
    final action = await showLayerMenu(context, ref, layer, _playback);
    if (!mounted || action == null) return;
    _openLayerAction(layer, action);
  }

  void _openLayerAction(Layer layer, LayerMenuAction action) {
    if (ref.read(selectedLayerProvider) != layer.id ||
        ref.read(editorControllerProvider).layerById(layer.id) == null) {
      return;
    }
    _playback.pause();
    switch (action) {
      case LayerMenuAction.transform:
        setState(() => _mode = _Mode.transform);
      case LayerMenuAction.blending:
        setState(() => _mode = _Mode.blending);
      case LayerMenuAction.colorFill:
        setState(() => _mode = _Mode.colorFill);
      case LayerMenuAction.effects:
        setState(() => _mode = _Mode.effects);
      case LayerMenuAction.editText:
        _editText(layer);
      case LayerMenuAction.textAnimators:
        setState(() => _mode = _Mode.animators);
      case LayerMenuAction.editShape:
        setState(() {
          _shapeTool = ShapeTool.size;
          _mode = _Mode.editShape;
        });
      case LayerMenuAction.stroke:
        setState(() {
          _shapeTool = ShapeTool.stroke;
          _mode = _Mode.editShape;
        });
    }
  }

  /// EDIT POINTS: a geometria vira caminho (se ainda nao e), o editor
  /// de nos passa a mirar nela e o painel do trackpad abre.
  void _abrirEditPoints([String? layerId]) {
    final id = layerId ?? ref.read(selectedLayerProvider);
    if (id == null) return;
    final controller = ref.read(editorControllerProvider.notifier);
    final itemId = controller.ensureShapeBezierGeometry(
      id,
      _playback.time.value,
    );
    if (itemId == null) {
      showReasonToast(context, 'Esta camada nao tem caminho editavel');
      return;
    }
    _playback.pause();
    ref.read(selectedLayerProvider.notifier).state = id;
    ref.read(pathEditTargetProvider.notifier).state = PathEditTarget(
      id,
      itemId,
      forma: true,
    );
    ref.read(pathEditSelectedProvider.notifier).state = null;
    ref.read(pathEditCursorProvider.notifier).state = null;
    ref.read(pathEditModeProvider.notifier).state = PointsMode.move;
    setState(() {
      _pointsItemId = itemId;
      _pointsReturn = _Mode.editShape;
      _mode = _Mode.editPoints;
    });
  }

  /// A mascara usa o MESMO Edit Points com trackpad das formas. O alvo
  /// compartilhado diz ao painel e ao overlay qual AnimatedPath editar;
  /// ao voltar, a pessoa retorna para Blending & Opacity.
  void _abrirMaskEditPoints(String maskId) {
    final id = ref.read(selectedLayerProvider);
    if (id == null) return;
    final layer = ref.read(editorControllerProvider).layerById(id);
    if (layer == null || !layer.masks.any((m) => m.id == maskId)) {
      showReasonToast(context, 'Esta mascara nao existe mais');
      return;
    }
    _playback.pause();
    ref.read(pathEditTargetProvider.notifier).state = PathEditTarget(
      id,
      maskId,
      forma: false,
    );
    ref.read(pathEditSelectedProvider.notifier).state = null;
    ref.read(pathEditCursorProvider.notifier).state = null;
    ref.read(pathEditModeProvider.notifier).state = PointsMode.move;
    setState(() {
      _pointsItemId = maskId;
      _pointsReturn = _Mode.blending;
      _mode = _Mode.editPoints;
    });
  }

  void _fecharEditPoints() {
    ref.read(pathEditTargetProvider.notifier).state = null;
    ref.read(pathEditSelectedProvider.notifier).state = null;
    ref.read(pathEditCursorProvider.notifier).state = null;
  }

  Future<void> _editText(Layer layer) async {
    if (layer is! TextLayer) return;
    final controller = ref.read(editorControllerProvider.notifier);
    final textController = TextEditingController(text: layer.text);
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: AmColors.panel,
      isScrollControlled: true,
      builder: (sheetContext) => Padding(
        padding: EdgeInsets.fromLTRB(
          20,
          16,
          20,
          16 + MediaQuery.of(sheetContext).viewInsets.bottom,
        ),
        child: CupertinoTextField(
          controller: textController,
          autofocus: true,
          maxLines: 3,
          minLines: 1,
          style: const TextStyle(fontSize: 17, color: AmColors.text),
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
          decoration: BoxDecoration(
            color: AmColors.chip,
            borderRadius: BorderRadius.circular(12),
          ),
          onChanged: (v) => controller.editTextLayer(layer.id, text: v),
          onSubmitted: (_) => Navigator.of(sheetContext).pop(),
        ),
      ),
    );
    textController.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final selectedId = ref.watch(selectedLayerProvider);
    final multi = ref.watch(multiSelectProvider);

    ref.listen<String?>(selectedLayerProvider, (previous, next) {
      if (previous == next) return;
      // Um painel/atalho capturado para A não pode editar A após selecionar B.
      RecentSheets.instance.clear();
      closeActiveParamSheet(context);
      _fecharEditPoints();
      setState(() => _mode = _Mode.main);
    });

    ref.listen(editorControllerProvider, (previous, updated) {
      if (previous?.id != updated.id) {
        RecentSheets.instance.clear();
        closeActiveParamSheet(context);
      }
      ref.read(projectsControllerProvider.notifier).upsert(updated);
      _syncVideos();
    });

    // Motor de preview: o clock compoe na taxa da COMPOSICAO, nao na da
    // tela — ticks sao quantizados no fps do projeto.
    _playback.compositionFps = ref.watch(
      editorControllerProvider.select((p) => p.fps),
    );

    // Painel de ferramenta atual (null no modo principal).
    final Widget? panel = switch (_mode) {
      _Mode.main => null,
      _Mode.transform => TransformPanel(
        playback: _playback,
        tool: _tool,
        onToolChanged: (t) => setState(() => _tool = t),
        onBack: _back,
        onOpenCurve: _openCurve,
      ),
      _Mode.blending => BlendingPanel(
        playback: _playback,
        onBack: _back,
        onOpenCurve: _openCurve,
        onEditMaskPoints: _abrirMaskEditPoints,
      ),
      _Mode.colorFill => ColorFillPanel(onBack: _back, playback: _playback),
      _Mode.effects => EffectsPanel(playback: _playback, onBack: _back),
      _Mode.curve => CurvePanel(
        playback: _playback,
        prop: _curveProp,
        onBack: _back,
      ),
      _Mode.animators => TextAnimatorsPanel(playback: _playback, onBack: _back),
      _Mode.editShape => ShapePanel(
        playback: _playback,
        tool: _shapeTool,
        onToolChanged: (t) => setState(() => _shapeTool = t),
        onBack: _back,
        onEditPoints: _abrirEditPoints,
      ),
      _Mode.editPoints => PointsPanel(
        key: _pointsKey,
        playback: _playback,
        layerId: selectedId ?? '',
        itemId: _pointsItemId ?? '',
        onBack: _back,
      ),
    };

    // Desenho vetorial pelo menu de adicionar: abre o Edit Points na
    // camada recem-criada.
    ref.listen<String?>(editPointsRequestProvider, (_, id) {
      if (id == null) return;
      ref.read(editPointsRequestProvider.notifier).state = null;
      _abrirEditPoints(id);
    });

    final pinkPlayhead =
        _mode == _Mode.effects ||
        _mode == _Mode.curve ||
        _mode == _Mode.animators;

    // Diamantes da propriedade ativa acendem; os demais ficam apagados.
    final layer = selectedId == null
        ? null
        : ref.watch(editorControllerProvider).layerById(selectedId);
    final Set<int>? activeTimesUs = layer == null
        ? null
        : switch (_mode) {
            _Mode.main => null,
            _Mode.transform => _timesForProp(layer, propOfTool(_tool)),
            _Mode.curve => _timesForProp(layer, _curveProp),
            // Blending abriga opacidade e mascara; os keyframes reais dos
            // dois continuam visiveis ao alternar entre as abas.
            _Mode.blending => {...layer.opacityTimesUs, ...layer.maskTimesUs},
            _Mode.effects => layer.effectTimesUs,
            _Mode.colorFill => const <int>{},
            _Mode.animators => null,
            // Todo numero da forma, o tracejado, o Desenhar e os pontos.
            _Mode.editShape => layer.moduleTimesUs,
            // O mesmo Edit Points atende forma e mascara.
            _Mode.editPoints =>
              (ref.watch(pathEditTargetProvider)?.forma ?? true)
                  ? layer.moduleTimesUs
                  : layer.maskTimesUs,
          };

    final showTools =
        !_adding && _mode == _Mode.main && layer != null && multi.isEmpty;
    final hasContext =
        _adding ||
        ref.watch(freehandRequestProvider) ||
        _previewExpanded ||
        _mode != _Mode.main ||
        selectedId != null ||
        multi.isNotEmpty;
    return PopScope(
      canPop: !hasContext,
      onPopInvokedWithResult: (didPop, _) {
        if (!didPop) _back();
      },
      child: Scaffold(
        key: paramSheetHostKey,
        backgroundColor: AmColors.bg,
        body: SafeArea(
          child: LayoutBuilder(
            builder: (context, constraints) {
              // The preview owns the same space in overview, selection and
              // property editing. Tools share the lower half of the workspace.
              final workspaceHeight = (constraints.maxHeight - 100).clamp(
                0.0,
                double.infinity,
              );
              final lowerHeight = workspaceHeight / 2;
              final dockHeight = (lowerHeight - 44 - 88).clamp(0.0, 202.0);
              final alturaDoPainel = (lowerHeight - 88).clamp(
                0.0,
                double.infinity,
              );
              final timelineHeight = _mode == _Mode.main
                  ? (lowerHeight -
                            (!_adding && (layer != null || multi.isNotEmpty)
                                ? 44
                                : 0) -
                            (showTools ? dockHeight : 0))
                        .clamp(88.0, double.infinity)
                  : 88.0;
              return Stack(
                children: [
                  Column(
                    children: [
                      if (!_previewExpanded)
                        _TopBar(
                          title: _title,
                          isMain: _mode == _Mode.main,
                          onBack: _back,
                          onLayerMenu: _onTapLayer,
                          playback: _playback,
                          trailing: _mode == _Mode.editPoints
                              ? _PointsHeaderActions(
                                  playback: _playback,
                                  onKeyframe: () =>
                                      _pointsKey.currentState?.toggleKeyframe(),
                                  onAdd: () =>
                                      _pointsKey.currentState?.addPoint(),
                                )
                              : null,
                        ),
                      // RepaintBoundary: palco, timeline e painel pintam em
                      // camadas separadas — repintar um nao repinta os outros.
                      Expanded(
                        child: RepaintBoundary(
                          key: previewStageKey,
                          child: PreviewStage(
                            playback: _playback,
                            videos: _videos,
                          ),
                        ),
                      ),
                      _TransportBar(
                        playback: _playback,
                        previewExpanded: _previewExpanded,
                        onTogglePreview: () => setState(
                          () => _previewExpanded = !_previewExpanded,
                        ),
                      ),
                      // Barra de ACOES fixa (spec barra-de-acoes): comandos
                      // estruturais sempre no mesmo lugar; desabilitado fica
                      // esmaecido, nunca some.
                      if (!_previewExpanded) ...[
                        // A BARRA DE ACOES SO NO MODO PRINCIPAL. Ela guarda
                        // comandos de estrutura — somar camada, cortar,
                        // duplicar, precompor, vincular — e nenhum deles se
                        // usa no meio de um ajuste de parametro. Escondida
                        // durante um painel, os 44 px dela vao para o
                        // painel, que era o que faltava para as abas de
                        // transformacao caberem sem rolagem.
                        if (!_adding &&
                            _mode == _Mode.main &&
                            (layer != null || multi.isNotEmpty))
                          _ActionBar(playback: _playback, onAdd: _openAdd),
                        RepaintBoundary(
                          child: AmTimeline(
                            playback: _playback,
                            height: timelineHeight,
                            singleLayerId: _mode == _Mode.main
                                ? null
                                : selectedId,
                            playheadColor: pinkPlayhead
                                ? AmColors.pink
                                : Colors.white,
                            onTapLayer: _onTapLayer,
                            onScrub: _videos.scrub,
                            activeTimesUs: activeTimesUs,
                            onForeignKeyframe: _mode == _Mode.main
                                ? null
                                : _onForeignKeyframe,
                          ),
                        ),
                        if (showTools)
                          SizedBox(
                            height: dockHeight,
                            child: LayerToolsDock(
                              layer: layer,
                              playback: _playback,
                              onAction: (action) =>
                                  _openLayerAction(layer, action),
                              onMore: () => _onTapLayer(layer),
                            ),
                          ),
                        // Todos os painéis usam a mesma área inferior;
                        // trocar de ferramenta preserva o enquadramento.
                        if (panel != null)
                          SizedBox(
                            height: alturaDoPainel,
                            child: RepaintBoundary(child: panel),
                          ),
                      ],
                    ],
                  ),
                  if (_mode == _Mode.main &&
                      !_previewExpanded &&
                      !_adding &&
                      layer == null &&
                      multi.isEmpty)
                    Positioned(
                      right: 16,
                      bottom: 16,
                      child: Tooltip(
                        message: 'Adicionar camada',
                        child: Material(
                          color: AmColors.bg,
                          shape: const CircleBorder(
                            side: BorderSide(
                              color: AmColors.accent,
                              width: 1.5,
                            ),
                          ),
                          child: InkWell(
                            customBorder: const CircleBorder(),
                            onTap: _openAdd,
                            child: const SizedBox(
                              width: 48,
                              height: 48,
                              child: Icon(
                                CupertinoIcons.plus,
                                color: AmColors.accent,
                                size: 26,
                              ),
                            ),
                          ),
                        ),
                      ),
                    ),
                  if (_adding && !_previewExpanded)
                    Positioned(
                      left: 0,
                      right: 0,
                      bottom: 0,
                      height: (lowerHeight - 38).clamp(0.0, double.infinity),
                      child: AddLayerPanel(
                        playhead: _playback.time.value,
                        onClose: () => setState(() => _adding = false),
                      ),
                    ),
                  if (ref.watch(debugOverlayProvider))
                    Positioned(
                      top: 6,
                      left: 8,
                      child: IgnorePointer(
                        child: _DiagOverlay(playback: _playback),
                      ),
                    ),
                ],
              );
            },
          ),
        ),
      ),
    );
  }
}

/// O QUE AS MARCAS DESTRAVAM.
///
/// Marcar e barato; o valor esta no que se faz com as marcas depois.
/// Cortar em todas de uma vez e distribuir as camadas nelas sao as duas
/// coisas que, feitas a mao, consomem a tarde inteira.
Future<void> _menuDasMarcas(
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
                Text(
                  '$quantas marca${quantas == 1 ? '' : 's'}',
                  style: const TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const Spacer(),
                if (project.bpm != null)
                  Text(
                    '${project.bpm!.toStringAsFixed(0)} bpm',
                    style: const TextStyle(fontSize: 12, color: AmColors.muted),
                  ),
              ],
            ),
          ),
          ListTile(
            leading: const Icon(
              CupertinoIcons.chevron_right_2,
              size: 19,
              color: AmColors.text,
            ),
            title: const Text(
              'Ir para a proxima marca',
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
            title: const Text(
              'Cortar em todas as marcas',
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
            title: const Text(
              'Distribuir as camadas nas marcas',
              style: TextStyle(color: AmColors.text, fontSize: 15),
            ),
            subtitle: const Text(
              'Uma camada por marca, na ordem em que estao',
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
            title: const Text(
              'Limpar as marcas',
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

/// Exclui as camadas de [targets] pelo MESMO caminho da barra de acoes:
/// respeita o magnetico e oferece "Desfazer". Vive fora da barra porque
/// o cabecalho verde tambem exclui — dois botoes, uma regra so.
void _excluirCamadas(BuildContext context, WidgetRef ref, Set<String> targets) {
  if (targets.isEmpty) return;
  final controller = ref.read(editorControllerProvider.notifier);
  final count = targets.length;
  // MAGNETICO: excluir FECHA o buraco e puxa o que vinha depois. Era a
  // reclamacao "corta, apaga e fica um buraco". Desligado, o buraco
  // fica — e o que se quer quando outra trilha precisa continuar no
  // mesmo lugar.
  final magnetico = ref.read(magneticProvider);
  if (magnetico) {
    for (final id in targets) {
      controller.rippleDeleteLayer(id);
    }
    // rippleDeleteLayer limpa so a selecao simples. Sem limpar a multipla
    // aqui, ids mortos mantem a contagem e o cabecalho verde acesos
    // depois de nao sobrar camada nenhuma.
    ref.read(multiSelectProvider.notifier).state = const {};
  } else {
    controller.removeLayers(targets);
  }
  AureaSnack.show(
    context,
    count == 1
        ? (magnetico ? 'Camada excluida e o buraco fechado' : 'Camada excluida')
        : '$count camadas excluidas',
    actionLabel: 'Desfazer',
    onAction: controller.undo,
  );
}

/// Agrupa a selecao e sai da selecao multipla. groupLayers ja seleciona o
/// grupo novo, mas nao limpa a multipla — sem isso os ids antigos ficam
/// contando e o cabecalho nao cairia para o estado de camada.
void _agruparSelecao(WidgetRef ref, Set<String> targets) {
  ref.read(editorControllerProvider.notifier).groupLayers(targets.toList());
  ref.read(multiSelectProvider.notifier).state = const {};
}

/// Escalonamento Apple da selecao multipla. Tanto keyframes reais quanto
/// o vinculo avancado entram no controller como uma unica mutacao/undo.
void _abrirCascata(
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

/// VINCULAR A SELECAO INTEIRA a um objeto: cada camada selecionada passa
/// a seguir o alvo, de uma vez.
Future<void> _vincularSelecao(
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
              child: Text(
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
                    color: AmColors.muted,
                  ),
                  title: Text(
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

/// Cabecalho contextual SOBRE o preview: uma faixa chapada, sempre na
/// mesma altura, que troca de conteudo (e de cor) conforme o contexto.
/// Os watches de selecao ficam AQUI, e nao no build da tela, para um
/// toque longo nao rebuildar preview, timeline e painel.
class _TopBar extends ConsumerWidget {
  const _TopBar({
    required this.title,
    required this.isMain,
    this.trailing,
    required this.onBack,
    required this.onLayerMenu,
    required this.playback,
  });

  final String title;
  final bool isMain;

  /// Acoes a direita no cabecalho de painel (ex.: ◈ e ⊕ do Edit Points).
  final Widget? trailing;
  final VoidCallback onBack;
  final ValueChanged<Layer> onLayerMenu;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final selectedId = isMain ? ref.watch(selectedLayerProvider) : null;
    final multi = isMain ? ref.watch(multiSelectProvider) : const <String>{};
    // Observa so o NOME da camada, nao o projeto inteiro: o cabecalho nao
    // precisa repintar a cada keyframe movido.
    final nome = ref.watch(
      editorControllerProvider.select(
        (p) => selectedId == null ? null : p.layerById(selectedId)?.name,
      ),
    );
    // Mesma formula da barra de acoes: o toque longo pode deixar a
    // camada primaria fora do conjunto multiplo.
    final targets = <String>{...multi, ?selectedId};
    final n = targets.length;

    // Precedencia: painel aberto ganha de tudo; depois a selecao multipla;
    // uma camada cujo id ja nao existe (undo) cai no projeto sem quebrar.
    //
    // Verde so com DUAS ou mais: a faixa verde diz "voce esta agindo
    // sobre um conjunto". Um toque longo que deixa uma camada sozinha no
    // conjunto pintava tudo de verde com "1 camada" — modo de conjunto
    // sem conjunto nenhum.
    final kind = !isMain
        ? _HeaderKind.painel
        : n >= 2
        ? _HeaderKind.multipla
        : (selectedId != null && nome != null)
        ? _HeaderKind.camada
        : _HeaderKind.projeto;
    final multipla = kind == _HeaderKind.multipla;

    // A faixa INTEIRA muda de cor na selecao multipla: o modo se
    // reconhece sem ler. Texto e icones invertem para o fundo escuro.
    final tinta = multipla ? AmColors.bg : AmColors.text;

    final titulo = switch (kind) {
      _HeaderKind.projeto || _HeaderKind.painel => title,
      _HeaderKind.camada => nome!.isEmpty ? 'Camada' : nome,
      _HeaderKind.multipla => '$n camada${n == 1 ? '' : 's'}',
    };

    // Botao da esquerda. No projeto e no painel ele VOLTA de nivel. Com
    // uma camada, ele so LIMPA a selecao (nao sai do editor). Na selecao
    // multipla, sair da selecao nao e voltar de nivel — por isso e um X,
    // nao um chevron; a camada primaria fica e o cabecalho cai para ela.
    final project = ref.watch(editorControllerProvider);
    final activeId = ref.watch(selectedLayerProvider);
    final activeName = activeId == null
        ? null
        : project.layerById(activeId)?.name;
    final contextLabel = kind == _HeaderKind.painel && activeName != null
        ? '${project.name} › $activeName'
        : kind == _HeaderKind.camada
        ? project.name
        : null;

    return Container(
      height: 52,
      color: multipla ? AmColors.accent : AmColors.topBar,
      child: Row(
        children: [
          CupertinoButton(
            padding: const EdgeInsets.symmetric(horizontal: 14),
            key: const ValueKey('editor-back'),
            onPressed: onBack,
            child: Tooltip(
              message: 'Voltar um nível',
              child: Icon(
                multipla ? CupertinoIcons.xmark : CupertinoIcons.chevron_back,
                size: 24,
                color: tinta,
              ),
            ),
          ),
          Expanded(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                if (contextLabel != null)
                  Text(
                    contextLabel,
                    key: const ValueKey('editor-context'),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(fontSize: 10, color: AmColors.muted),
                  ),
                Text(
                  titulo,
                  textAlign: kind == _HeaderKind.projeto || multipla
                      ? TextAlign.left
                      : TextAlign.center,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: contextLabel == null ? 14 : 13,
                    fontWeight: FontWeight.w500,
                    color: tinta,
                  ),
                ),
              ],
            ),
          ),
          ...switch (kind) {
            _HeaderKind.projeto => [
              // Engrenagem: liga o overlay de diagnostico do preview
              // (composicoes/s, camadas, marcha — motor-de-preview §7).
              Consumer(
                builder: (context, ref, _) => CupertinoButton(
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                  onPressed: () =>
                      ref.read(debugOverlayProvider.notifier).state = !ref.read(
                        debugOverlayProvider,
                      ),
                  child: Icon(
                    CupertinoIcons.gear,
                    size: 23,
                    color: ref.watch(debugOverlayProvider)
                        ? AmColors.accent
                        : AmColors.text,
                  ),
                ),
              ),
              Padding(
                padding: const EdgeInsets.only(right: 12),
                child: GestureDetector(
                  onTap: () => showExportSheet(context, ref),
                  child: Container(
                    width: 42,
                    height: 38,
                    decoration: const BoxDecoration(color: AmColors.topBar),
                    child: const Icon(
                      CupertinoIcons.square_arrow_up,
                      size: 20,
                      color: AmColors.text,
                    ),
                  ),
                ),
              ),
            ],
            // "..." abre o MESMO menu que tocar na barra da timeline abre:
            // um so lugar para as acoes da camada, alcancavel sem mirar
            // na barra.
            _HeaderKind.camada => [
              // VINCULAR AO PAI mora no cabecalho, nao na grade de secoes:
              // e relacao estrutural entre camadas, nao editor de
              // aparencia. E e o que faz o nulo funcionar.
              CupertinoButton(
                padding: const EdgeInsets.symmetric(horizontal: 10),
                onPressed: () {
                  final layer = ref
                      .read(editorControllerProvider)
                      .layerById(selectedId);
                  if (layer != null) {
                    showParentSheet(context, ref, layer, playback.time.value);
                  }
                },
                child: Icon(
                  ref
                              .watch(editorControllerProvider)
                              .linkFor(selectedId!, LayerProp.parent) ==
                          null
                      ? CupertinoIcons.link
                      : CupertinoIcons.link_circle_fill,
                  size: 22,
                  color: tinta,
                ),
              ),
              CupertinoButton(
                padding: const EdgeInsets.symmetric(horizontal: 14),
                onPressed: () {
                  final layer = ref
                      .read(editorControllerProvider)
                      .layerById(selectedId);
                  if (layer != null) onLayerMenu(layer);
                },
                // Grade de secoes, nao "mais opcoes": o icone diz o que
                // abre. Tres pontinhos prometem menu escondido.
                child: Icon(
                  CupertinoIcons.square_grid_2x2,
                  size: 22,
                  color: tinta,
                ),
              ),
            ],
            // Agrupar e excluir com os mesmos icones da barra de acoes,
            // para a pessoa reconhecer sem aprender de novo.
            _HeaderKind.multipla => [
              CupertinoButton(
                padding: const EdgeInsets.symmetric(horizontal: 10),
                onPressed: () =>
                    _abrirCascata(context, ref, targets, playback.time.value),
                child: Icon(Icons.format_line_spacing, size: 22, color: tinta),
              ),
              CupertinoButton(
                padding: const EdgeInsets.symmetric(horizontal: 12),
                onPressed: () => _agruparSelecao(ref, targets),
                child: Icon(
                  CupertinoIcons.square_stack_3d_up,
                  size: 22,
                  color: tinta,
                ),
              ),
              // VINCULAR TUDO DE UMA VEZ: a selecao inteira passa a
              // seguir um objeto, em vez de abrir camada por camada.
              CupertinoButton(
                padding: const EdgeInsets.symmetric(horizontal: 12),
                onPressed: () => _vincularSelecao(
                  context,
                  ref,
                  targets,
                  playback.time.value,
                ),
                child: Icon(CupertinoIcons.link, size: 22, color: tinta),
              ),
              CupertinoButton(
                padding: const EdgeInsets.symmetric(horizontal: 12),
                onPressed: () => _excluirCamadas(context, ref, targets),
                child: Icon(CupertinoIcons.trash, size: 22, color: tinta),
              ),
            ],
            // Espacador da largura do botao de voltar: o titulo
            // centralizado fica de fato no centro.
            _HeaderKind.painel => [trailing ?? const SizedBox(width: 52)],
          },
        ],
      ),
    );
  }
}

/// Overlay de diagnostico (motor-de-preview §7 + marchas §9): a MARCHA
/// em destaque com o motivo, composicoes por segundo, variancia entre
/// ticks e % de tempo em marcha baixa — sem numeros, todo relato de
/// travamento vira adivinhacao.
class _DiagOverlay extends ConsumerWidget {
  const _DiagOverlay({required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final fps = ref.watch(editorControllerProvider.select((p) => p.fps));
    final total = ref.watch(
      editorControllerProvider.select((p) => p.layers.length),
    );
    // Liga o contador de travadas de interface junto com o overlay.
    PreviewStats.hookTimings();
    return ValueListenableBuilder<GearDecision?>(
      valueListenable: PreviewStats.gear,
      builder: (context, gear, _) => ValueListenableBuilder<int>(
        valueListenable: PreviewStats.compsPerSec,
        builder: (context, comps, _) => ValueListenableBuilder<double>(
          valueListenable: PreviewStats.tickVarianceMs,
          builder: (context, variance, _) => ValueListenableBuilder<int>(
            valueListenable: PreviewStats.layersInFrame,
            builder: (context, inFrame, _) => ValueListenableBuilder<int>(
              valueListenable: PreviewStats.lowGearPercent,
              builder: (context, lowPct, _) {
                return Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 10,
                    vertical: 6,
                  ),
                  decoration: BoxDecoration(
                    color: const Color(0xCC12151A),
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: AmColors.hairline),
                  ),
                  child: ValueListenableBuilder<int>(
                    valueListenable: PreviewStats.jankFrames,
                    builder: (context, jank, _) => ValueListenableBuilder<double>(
                      valueListenable: PreviewStats.worstFrameMs,
                      builder: (context, pior, _) =>
                          ValueListenableBuilder<FrameReport?>(
                            valueListenable: FrameLog.report,
                            builder: (context, r, _) => Text(
                              'UI: $jank travadas · pior ${pior.toStringAsFixed(0)} ms\n'
                              'MARCHA: ${gear == null ? '—' : gearLabel(gear.gear)}\n'
                              'motivo: ${gear?.reason ?? '—'}\n'
                              'compoe $comps/s · projeto ${fps}fps\n'
                              'variancia entre ticks: $variance ms\n'
                              'camadas no frame: $inFrame / $total · '
                              'M1+M2: $lowPct%\n'
                              '── registrador (${r?.seconds ?? 0}s) ──\n'
                              'mediana ${r?.medianMs ?? 0} ms · '
                              'pico ${r?.peakMs ?? 0} ms\n'
                              'travadas ${r?.stutters ?? 0} · '
                              'intervalo ${r?.gapS ?? 0}s (±${r?.gapSdS ?? 0})\n'
                              'deriva video-audio ${r?.driftMs ?? 0} ms',
                              style: const TextStyle(
                                fontSize: 11,
                                color: AmColors.accent,
                                height: 1.4,
                                fontFeatures: [FontFeature.tabularFigures()],
                              ),
                            ),
                          ),
                    ),
                  ),
                );
              },
            ),
          ),
        ),
      ),
    );
  }
}

/// Barra de ACOES fixa (spec AUREA-barra-de-acoes §2): [+] isolado a
/// esquerda; dividir/duplicar/agrupar/vincular no centro; excluir isolado
/// a direita. Posicoes IMUTAVEIS: sem alvo, o botao esmaece e o toque
/// explica a razao. Excluir nao pede confirmacao — snackbar "Desfazer".
class _ActionBar extends ConsumerWidget {
  const _ActionBar({required this.playback, required this.onAdd});

  final PlaybackController playback;
  final VoidCallback onAdd;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final controller = ref.read(editorControllerProvider.notifier);
    final project = ref.watch(editorControllerProvider);
    final selId = ref.watch(selectedLayerProvider);
    final multi = ref.watch(multiSelectProvider);
    final targets = <String>{...multi, ?selId};
    final n = targets.length;
    const completo = true;
    const parenting = true;

    Widget btn({
      required IconData icon,
      required bool enabled,
      required VoidCallback onTap,
      required String reason,
      Color color = AmColors.text,
    }) {
      return CupertinoButton(
        padding: const EdgeInsets.symmetric(horizontal: 13, vertical: 6),
        onPressed: () {
          if (enabled) {
            onTap();
          } else {
            showReasonToast(context, reason);
          }
        },
        child: Opacity(
          opacity: enabled ? 1 : 0.32,
          child: Icon(icon, size: 21, color: color),
        ),
      );
    }

    Widget divider() =>
        Container(width: 1, height: 22, color: AmColors.hairline);

    return Container(
      height: 44,
      decoration: const BoxDecoration(
        color: AmColors.bg,
        border: Border(
          top: BorderSide(color: AmColors.hairline),
          bottom: BorderSide(color: AmColors.hairline),
        ),
      ),
      child: Row(
        children: [
          // A FILEIRA DE FERRAMENTAS ROLA. Ela cresce a cada recurso
          // novo, e uma Row fixa estoura em tela estreita — que e
          // exatamente o que aconteceu ao entrar o magnetico e o
          // marcador. Rolando, cabe sempre, e o que mais se usa
          // continua na esquerda.
          Expanded(
            child: ListView(
              scrollDirection: Axis.horizontal,
              children: [
                // [+] nunca depende de selecao.
                btn(
                  icon: CupertinoIcons.plus,
                  enabled: true,
                  reason: '',
                  color: AmColors.accent,
                  onTap: () {
                    playback.pause();
                    onAdd();
                  },
                ),
                divider(),
                btn(
                  icon: CupertinoIcons.scissors,
                  enabled: n >= 1,
                  reason: 'Selecione uma camada',
                  onTap: () {
                    for (final id in targets) {
                      controller.splitLayer(id, playback.time.value);
                    }
                  },
                ),
                btn(
                  icon: CupertinoIcons.plus_square_on_square,
                  enabled: n >= 1,
                  reason: 'Selecione uma camada',
                  onTap: () {
                    for (final id in targets) {
                      controller.duplicateLayer(id);
                    }
                  },
                ),
                if (parenting)
                  btn(
                    icon: CupertinoIcons.square_stack_3d_up,
                    enabled: n >= 2,
                    reason: 'Selecione duas ou mais camadas (toque longo nas barras)',
                    // Pelo helper: limpa a selecao multipla depois de agrupar,
                    // senao o "N camadas" ficava aceso com ids mortos.
                    onTap: () => _agruparSelecao(ref, targets),
                  ),
                if (parenting)
                  btn(
                    icon: CupertinoIcons.link,
                    enabled: n == 1,
                    reason: 'Selecione UMA camada para vincular',
                    onTap: () {
                      final layer = project.layerById(targets.first);
                      if (layer != null) {
                        showParentSheet(
                          context,
                          ref,
                          layer,
                          playback.time.value,
                        );
                      }
                    },
                  ),
                // MAGNETICO: visivel, porque muda o que EXCLUIR faz. Um modo
                // escondido que muda o resultado de um botao e pior que nao
                // ter o modo.
                Builder(
                  builder: (context) {
                    final magnetico = ref.watch(magneticProvider);
                    return btn(
                      icon: magnetico
                          ? CupertinoIcons.arrow_left_right_square_fill
                          : CupertinoIcons.arrow_left_right_square,
                      enabled: true,
                      reason: '',
                      color: magnetico ? AmColors.accent : AmColors.text,
                      onTap: () {
                        ref.read(magneticProvider.notifier).state = !magnetico;
                        AureaSnack.show(
                          context,
                          magnetico
                              ? 'Magnetico desligado: excluir deixa o buraco'
                              : 'Magnetico ligado: excluir fecha o buraco',
                        );
                      },
                    );
                  },
                ),
                // MARCADOR: o mesmo botao poe e tira, e funciona TOCANDO NO
                // RITMO com a reproducao andando — e assim que se marca musica,
                // e por isso ele nunca pausa nada. Toque longo abre o que as
                // marcas destravam.
                // O TEMPO E LIDO NO TOQUE, e o icone segue o relogio. A barra
                // nao reconstroi quando o cabecote anda; ler o tempo na
                // construcao deixava um valor velho no toque — toda marca caia
                // no mesmo instante antigo, e o segundo toque apagava a
                // primeira. "So da para por uma marca" era isso.
                ValueListenableBuilder<Duration>(
                  valueListenable: playback.time,
                  builder: (context, t, _) {
                    final tem =
                        project.markerNear(
                          t,
                          const Duration(milliseconds: 120),
                        ) !=
                        null;
                    return GestureDetector(
                      onLongPress: () => _menuDasMarcas(context, ref, playback),
                      child: btn(
                        icon: tem
                            ? CupertinoIcons.bookmark_fill
                            : CupertinoIcons.bookmark,
                        enabled: true,
                        reason: '',
                        color: tem ? AmColors.accent : AmColors.text,
                        onTap: () =>
                            controller.toggleMarker(playback.time.value),
                      ),
                    );
                  },
                ),
                // ALINHAR E DISTRIBUIR (PR-X1): exato ao pixel, o que no dedo
                // nunca fica.
                if (completo)
                  btn(
                    icon: CupertinoIcons.square_grid_3x2,
                    enabled: n >= 1,
                    reason: 'Selecione uma camada',
                    onTap: () => showAlignSheet(
                      context,
                      ref,
                      targets.toList(),
                      playback.time.value,
                    ),
                  ),
              ],
            ),
          ),
          if (n > 1)
            Padding(
              padding: const EdgeInsets.only(right: 8),
              child: Text(
                '$n camadas',
                style: const TextStyle(fontSize: 12, color: AmColors.accent),
              ),
            ),
          divider(),
          btn(
            icon: CupertinoIcons.trash,
            enabled: n >= 1,
            reason: 'Selecione uma camada',
            // Mesma regra do cabecalho verde: magnetico + "Desfazer".
            onTap: () => _excluirCamadas(context, ref, targets),
          ),
        ],
      ),
    );
  }
}

class _TransportBar extends ConsumerWidget {
  const _TransportBar({
    required this.playback,
    required this.previewExpanded,
    required this.onTogglePreview,
  });

  final PlaybackController playback;
  final bool previewExpanded;
  final VoidCallback onTogglePreview;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final duration = ref.watch(editorControllerProvider).duration;
    final selectedId = ref.watch(selectedLayerProvider);
    const shapes = true;
    const nullAndClone = true;

    final controller = ref.read(editorControllerProvider.notifier);
    return SizedBox(
      height: 48,
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceEvenly,
        children: [
          GestureDetector(
            onTap: controller.canUndo ? controller.undo : null,
            child: Icon(
              CupertinoIcons.arrow_uturn_left,
              size: 21,
              color: controller.canUndo ? AmColors.text : AmColors.muted,
            ),
          ),
          GestureDetector(
            onTap: controller.canRedo ? controller.redo : null,
            child: Icon(
              CupertinoIcons.arrow_uturn_right,
              size: 21,
              color: controller.canRedo ? AmColors.text : AmColors.muted,
            ),
          ),
          // COM MARCAS, os botoes de ponta andam DE MARCA EM MARCA: e o
          // jeito de navegar uma musica marcada. Sem marca, inicio/fim.
          GestureDetector(
            onTap: () {
              final t = playback.time.value;
              final temMarcas = ref
                  .read(editorControllerProvider)
                  .markers
                  .isNotEmpty;
              playback.seek(
                temMarcas
                    ? (controller.markerBefore(t) ?? Duration.zero)
                    : Duration.zero,
              );
            },
            child: const Icon(
              CupertinoIcons.backward_end,
              size: 22,
              color: AmColors.text,
            ),
          ),
          ValueListenableBuilder<bool>(
            valueListenable: playback.playing,
            builder: (context, playing, _) => GestureDetector(
              onTap: playback.toggle,
              child: Icon(
                playing ? CupertinoIcons.pause_fill : CupertinoIcons.play_fill,
                size: 26,
                color: Colors.white,
              ),
            ),
          ),
          GestureDetector(
            onTap: () {
              final t = playback.time.value;
              final temMarcas = ref
                  .read(editorControllerProvider)
                  .markers
                  .isNotEmpty;
              playback.seek(
                temMarcas ? (controller.markerAfter(t) ?? duration) : duration,
              );
            },
            child: const Icon(
              CupertinoIcons.forward_end,
              size: 22,
              color: AmColors.text,
            ),
          ),
          // LOOP: a reproducao volta ao inicio ao chegar no fim — e assim
          // que se marca batida e se confere um trecho sem parar.
          ValueListenableBuilder<bool>(
            valueListenable: playback.loop,
            builder: (context, loop, _) => GestureDetector(
              onTap: () => playback.loop.value = !loop,
              child: Icon(
                CupertinoIcons.repeat,
                size: 22,
                color: loop ? AmColors.accent : AmColors.text,
              ),
            ),
          ),
          if (nullAndClone)
            GestureDetector(
              onTap: selectedId == null
                  ? null
                  : () => ref
                        .read(editorControllerProvider.notifier)
                        .duplicateLayer(selectedId),
              child: Icon(
                CupertinoIcons.plus_square_on_square,
                size: 21,
                color: selectedId == null ? AmColors.muted : AmColors.text,
              ),
            ),
          // CASCA DE CEBOLA: toque cicla 0 -> 1 -> 2 -> 0. Animar a mao
          // sem ver o quadro anterior e desenhar no escuro.
          if (shapes)
            Builder(
              builder: (context) {
                final onion = ref.watch(onionSkinProvider);
                return GestureDetector(
                  onTap: () => ref.read(onionSkinProvider.notifier).state =
                      (onion + 1) % 3,
                  child: Icon(
                    CupertinoIcons.square_stack_3d_down_dottedline,
                    size: 22,
                    color: onion > 0 ? AmColors.accent : AmColors.text,
                  ),
                );
              },
            ),
          Tooltip(
            message: previewExpanded ? 'Voltar ao editor' : 'Expandir preview',
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: onTogglePreview,
              child: SizedBox(
                width: 40,
                height: 40,
                child: Icon(
                  previewExpanded ? Icons.fullscreen_exit : Icons.fullscreen,
                  size: 24,
                  color: previewExpanded ? AmColors.accent : AmColors.text,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// `◈` (keyframe dos pontos) e `⊕` (adicionar no cursor) do Edit
/// Points, no cabecalho — como na AM. O diamante acende quando o
/// caminho e animado e enche quando ha keyframe no tempo de agora.
class _PointsHeaderActions extends ConsumerWidget {
  const _PointsHeaderActions({
    required this.playback,
    required this.onKeyframe,
    required this.onAdd,
  });

  final PlaybackController playback;
  final VoidCallback onKeyframe;
  final VoidCallback onAdd;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final project = ref.watch(editorControllerProvider);
    final alvo = ref.watch(pathEditTargetProvider);
    return ValueListenableBuilder<Duration>(
      valueListenable: playback.time,
      builder: (context, t, _) {
        var animado = false;
        var temKf = false;
        if (alvo != null) {
          final layer = project.layerById(alvo.layerId);
          if (alvo.forma && layer is ShapeLayer) {
            for (final i in layer.contents) {
              if (i.id == alvo.maskId && i is ShapeBezier) {
                animado = i.path.isAnimated;
                temKf = i.path.hasKeyframeAt(layer.localTime(t));
              }
            }
          } else if (layer != null) {
            for (final m in layer.masks) {
              if (m.id == alvo.maskId) {
                animado = m.path.isAnimated;
                temKf = m.path.hasKeyframeAt(layer.localTime(t));
              }
            }
          }
        }
        return Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            CupertinoButton(
              padding: const EdgeInsets.symmetric(horizontal: 10),
              onPressed: onKeyframe,
              child: Icon(
                temKf ? CupertinoIcons.rhombus_fill : CupertinoIcons.rhombus,
                size: 22,
                color: animado ? AmColors.accent : AmColors.text,
              ),
            ),
            CupertinoButton(
              padding: const EdgeInsets.only(left: 6, right: 14),
              onPressed: onAdd,
              child: const Icon(
                CupertinoIcons.plus_circle,
                size: 24,
                color: AmColors.text,
              ),
            ),
          ],
        );
      },
    );
  }
}
