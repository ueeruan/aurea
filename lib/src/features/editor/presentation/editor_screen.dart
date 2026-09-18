import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/tokens.dart';
import '../../../core/ui/snack.dart';
import '../../projects/application/projects_controller.dart';
import '../../projects/application/thumbnail_service.dart';
import '../application/editor_controller.dart';
import '../application/media_preview_service.dart';
import '../application/freehand_session.dart';
import '../application/playback_controller.dart';
import '../application/preview_stats.dart';
import '../application/qualidade3d_controller.dart';
import '../application/ui/editor_layout.dart';
import '../application/ui/editor_session.dart';
import '../application/video_layer_manager.dart';
import '../domain/effect.dart';
import '../domain/gear.dart';
import '../domain/grupo_ops.dart';
import '../domain/layer.dart';
import '../domain/orcamento_render.dart';
import 'am/am_colors.dart';
import 'am/am_timeline.dart';
import 'am/am_widgets.dart';
import 'am/beats_sheet.dart';
import 'am/curve_panel.dart';
import 'am/effects_panel.dart';
import 'am/layer_menu.dart';
import 'am/points_panel.dart';
import 'am/property_keyframe_context.dart';
import 'am/shape_panel.dart';
import 'am/transform_panel.dart';
import '../domain/estilizar_lote2.dart';
import 'context/add_toolbar.dart';
import 'shell/onboarding.dart';
import '../../help/presentation/quick_guide_screen.dart';
import 'context/categories/text_panel.dart';
import 'context/context_sheet.dart';
import 'context/layer_header.dart';
import 'shell/layer_actions.dart';
import 'shell/cromo_editor.dart';
import '../application/ui/opcoes_de_visualizacao.dart';
import 'shell/barra_de_tempo_tela_cheia.dart';
import 'widgets/add_layer_sheet.dart';
import 'widgets/mask_node_editor.dart';
import 'widgets/preview_stage.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

/// O EDITOR — cinco zonas fixas (secao 4 do prompt):
///
///   A  barra de cima     um estado so: voltar, nome, desfazer, projeto,
///                        Exportar, Simples/Pro
///   B  preview           com alca de altura
///   C  transporte        play, timecode tocavel, loop, ◆, marca
///   D  timeline          playhead central; nunca abaixo de 88 px
///   E  painel contextual muda de CONTEUDO com a selecao, nunca de lugar
///
/// O estado de sessao (o que esta aberto) mora em [editorSessionProvider];
/// esta tela orquestra: le a sessao, resolve as alturas e monta as zonas.
class EditorScreen extends ConsumerStatefulWidget {
  const EditorScreen({super.key, this.playback});

  final PlaybackController? playback;

  @override
  ConsumerState<EditorScreen> createState() => _EditorScreenState();
}

class _EditorScreenState extends ConsumerState<EditorScreen>
    with SingleTickerProviderStateMixin {
  late final PlaybackController _playback;
  final VideoLayerManager _videos = VideoLayerManager();

  /// Dicas de primeiro uso ja vistas neste aparelho (Fase 6).
  late bool _dicasVistas = OnboardingPrefs.vistas(ref);
  final GlobalKey<PointsPanelState> _pointsKey = GlobalKey<PointsPanelState>();
  AddTab? _addTab;

  EditorSessionNotifier get _session =>
      ref.read(editorSessionProvider.notifier);
  EditorSession get _s => ref.read(editorSessionProvider);

  @override
  void initState() {
    super.initState();
    // Ondas de todos os audios e videos do projeto, ja ao abrir.
    void coletar(List<Layer> ls, List<String> out) {
      for (final l in ls) {
        if (l is VideoLayer) out.add(l.sourcePath);
        if (l is AudioLayer) out.add(l.sourcePath);
        if (l is GroupLayer) coletar(l.children, out);
      }
    }

    final midias = <String>[];
    coletar(ref.read(editorControllerProvider).layers, midias);
    MediaPreviewService.instance.preparar(midias);
    RecentSheets.instance.clear();
    _playback =
        widget.playback ??
        PlaybackController(
          vsync: this,
          durationOf: () => ref.read(editorControllerProvider).duration,
        );
    _playback.time.addListener(_syncVideos);
    _playback.playing.addListener(_syncVideos);
    // ENTRAR E SAIR DE GRUPO movem o cabecote junto: la dentro o tempo
    // conta do inicio do grupo.
    _controladorDoNivel = ref.read(editorControllerProvider.notifier);
    _controladorDoNivel!.aoMudarDeNivel = (d) {
      final alvo = _playback.time.value + d;
      _playback.seek(alvo < Duration.zero ? Duration.zero : alvo);
    };
    // ABRIR UM PROJETO precisa montar os tocadores AGORA: o relogio esta
    // parado no zero e o sync so aconteceria quando ele andasse.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) return;
      _syncVideos();
      // O zoom do palco e da sessao de edicao: entra sempre ajustado.
      ref.read(zoomDoPalcoProvider.notifier).state = 1.0;
    });
  }

  EditorController? _controladorDoNivel;

  /// Midias com os grupos abertos, recalculadas so quando a pilha muda:
  /// o gerenciador de video compara a lista por identidade.
  List<Layer>? _pilhaDasMidias;
  List<Layer> _midias = const [];

  void _syncVideos() {
    final project = ref.read(editorControllerProvider);
    if (!identical(project.layers, _pilhaDasMidias)) {
      _pilhaDasMidias = project.layers;
      _midias = midiasAchatadas(project.layers);
    }
    final master = _videos.sync(
      _midias,
      _playback.time.value,
      _playback.playing.value,
      seekRevision: _playback.seekRevision,
    );
    if (master != null) _playback.anchorToMedia(master);
  }

  @override
  void dispose() {
    // Saiu do editor em tela cheia: devolve as barras do sistema.
    if (_telaCheiaAtiva) _modoDeSistema(false);
    RecentSheets.instance.clear();
    // Sem ref no dispose: o controlador foi guardado na montagem.
    _controladorDoNivel?.aoMudarDeNivel = null;
    _playback.time.removeListener(_syncVideos);
    _playback.playing.removeListener(_syncVideos);
    if (widget.playback == null) {
      _playback.dispose();
    }
    _videos.dispose();
    super.dispose();
  }

  bool _telaCheiaAtiva = false;

  void _modoDeSistema(bool telaCheia) {
    _telaCheiaAtiva = telaCheia;
    try {
      SystemChrome.setEnabledSystemUIMode(
        telaCheia ? SystemUiMode.immersiveSticky : SystemUiMode.edgeToEdge,
      );
    } catch (_) {
      // Plataforma sem controle de barras (testes, desktop): segue.
    }
  }

  // ------------------------------------------------------- navegacao

  /// Fecha a coisa mais interna que estiver aberta; sem nada aberto,
  /// sai do editor.
  void _back() {
    if (MediaQuery.viewInsetsOf(context).bottom > 0) {
      FocusManager.instance.primaryFocus?.unfocus();
      return;
    }
    final s = _s;
    if (s.adding) {
      _session.closeAdd();
      return;
    }
    if (ref.read(freehandRequestProvider)) {
      ref.read(freehandRequestProvider.notifier).state = false;
      return;
    }
    // Folhas persistentes tem uma entrada local de historico. Consumi-la
    // primeiro nao remove o editor nem muda sua selecao.
    if (ModalRoute.of(context)?.willHandlePopInternally ?? false) {
      Navigator.of(context).pop();
      return;
    }
    if (s.previewExpanded) {
      _session.setPreviewExpanded(false);
      return;
    }
    if (s.timelineExpanded) {
      _session.toggleTimelineExpanded();
      return;
    }
    switch (s.panel) {
      case EditorPanel.editPoints:
        _fecharEditPoints();
        _session.backFromEditPoints();
        return;
      case EditorPanel.curve:
        _session.backFromCurve();
        return;
      case EditorPanel.none:
      case EditorPanel.add:
        if (ref.read(multiSelectProvider).isNotEmpty) {
          ref.read(multiSelectProvider.notifier).state = const {};
          return;
        }
        if (ref.read(selectedLayerProvider) != null) {
          ref.read(selectedLayerProvider.notifier).state = null;
          return;
        }
        // Dentro de um grupo, Voltar sai do grupo (um nivel).
        if (ref.read(editorControllerProvider.notifier).dentroDeGrupo) {
          ref.read(editorControllerProvider.notifier).exitGroup();
          return;
        }
        // A miniatura do projeto para a tela inicial: capturada AGORA,
        // com o palco ainda vivo; a escrita segue em segundo plano. Com
        // um quadro escolhido no menu da timeline, fica o escolhido.
        if (ref.read(editorControllerProvider).thumbTime == null) {
          ThumbnailService.instance.capture(
            previewStageKey,
            ref.read(editorControllerProvider).id,
          );
        }
        Navigator.of(context).maybePop();
      default:
        _session.closePanel();
    }
  }

  /// USAR ESTE QUADRO COMO MINIATURA: sem selecao (a borda branca da
  /// camada escolhida entraria na foto), o palco e fotografado no quadro
  /// seguinte e o instante fica gravado no projeto.
  Future<void> _usarQuadroComoMiniatura() async {
    final controller = ref.read(editorControllerProvider.notifier);
    final id = ref.read(editorControllerProvider).id;
    controller.definirQuadroDaMiniatura(_playback.time.value);
    ref.read(multiSelectProvider.notifier).state = const {};
    ref.read(selectedLayerProvider.notifier).state = null;
    await WidgetsBinding.instance.endOfFrame;
    if (!mounted) return;
    await ThumbnailService.instance.capture(previewStageKey, id);
    if (!mounted) return;
    AureaSnack.show(context, 'Este quadro virou a miniatura do projeto');
  }

  /// Titulo da categoria aberta (cabecalho da zona E).
  String? _tituloDoPainel(EditorSession s) => switch (s.panel) {
    EditorPanel.none || EditorPanel.add => null,
    EditorPanel.transform => 'Transformar · ${labelOfProp(propOfTool(s.tool))}',
    EditorPanel.blending => 'Mesclagem e opacidade',
    EditorPanel.colorFill => 'Cor e preenchimento',
    EditorPanel.effects => 'Efeitos',
    EditorPanel.curve => 'Curva de gradação',
    EditorPanel.editText => switch (s.textSection) {
      TextSection.edit => 'Editar texto',
      TextSection.animation => 'Animação de texto',
      TextSection.presets => 'Presets de texto',
    },
    EditorPanel.editShape => 'Editar forma',
    EditorPanel.editPoints => 'Editar pontos',
  };

  Set<int> _timesForProp(Layer layer, LayerProp prop) =>
      keyframeTimesForProp(layer, prop);

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

  /// TOCARAM NUM DIAMANTE ACESO (Fase 5): sem painel aberto, o toque
  /// abre o easing do keyframe (E5) — selecionar, tocar o losango,
  /// escolher o preset: tres toques. Com painel aberto, so navega.
  void _onKeyframeTap(Layer layer, Duration t) {
    if (_s.panel != EditorPanel.none) return;
    if (ref.read(selectedLayerProvider) != layer.id) {
      ref.read(selectedLayerProvider.notifier).state = layer.id;
    }
    final dono = _donoDoKeyframe(layer, t);
    if (dono == null) return;
    final (prop, nome) = dono;
    if (nome == 'Efeitos') {
      _session.openPanel(EditorPanel.effects);
    } else if (nome == 'Mascaras') {
      selectMaskInBlendingPanel(ref);
      _session.openPanel(EditorPanel.blending);
    } else {
      _session.openCurve(prop);
    }
  }

  /// TOCARAM NUM DIAMANTE APAGADO: dizer de quem e, e levar ate la.
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
              if (tool != null) {
                _session.openTransform(tool);
              } else if (nome == 'Efeitos') {
                _session.openPanel(EditorPanel.effects);
              } else {
                selectMaskInBlendingPanel(ref);
                _session.openPanel(EditorPanel.blending);
              }
            }
          : null,
    );
  }

  void _openCurve(LayerProp prop) => _session.openCurve(prop);

  /// Tocar na barra JA selecionada: o painel ja esta aberto e sempre da
  /// mesma altura, entao so resta parar a reproducao.
  void _onTapLayer(Layer layer) => _playback.pause();

  void _openLayerAction(Layer layer, LayerMenuAction action) {
    if (ref.read(selectedLayerProvider) != layer.id ||
        ref.read(editorControllerProvider).layerById(layer.id) == null) {
      return;
    }
    _playback.pause();
    switch (action) {
      case LayerMenuAction.transform:
        _session.openTransform();
      case LayerMenuAction.blending:
        _session.openPanel(EditorPanel.blending);
      case LayerMenuAction.colorFill:
        _session.openPanel(EditorPanel.colorFill);
      case LayerMenuAction.effects:
        _session.openPanel(EditorPanel.effects);
      case LayerMenuAction.editText:
        // A FICHA DIZ "EDITAR TEXTO", entao abre no que ela promete: a
        // aba de conteudo. A animacao tem porta propria (a acao rapida
        // "Animar"), e a ultima aba visitada nao decide por ninguem.
        _session.openText(TextSection.edit);
      case LayerMenuAction.editShape:
        _session.openShape(ShapeTool.size);
      case LayerMenuAction.stroke:
        _session.openShape(ShapeTool.stroke);
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
    _session.openEditPoints(itemId, returnTo: EditorPanel.editShape);
  }

  /// A mascara usa o MESMO Edit Points com trackpad das formas.
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
    _session.openEditPoints(maskId, returnTo: EditorPanel.blending);
  }

  void _fecharEditPoints() {
    ref.read(pathEditTargetProvider.notifier).state = null;
    ref.read(pathEditSelectedProvider.notifier).state = null;
    ref.read(pathEditCursorProvider.notifier).state = null;
  }

  // ------------------------------------------------------- adicionar

  void _openAdd([AddTab? tab]) {
    _playback.pause();
    _addTab = tab ?? AddTab.forma;
    _session.openAdd();
  }

  /// O E1 pediu algo.
  Future<void> _onAddTarget(AddTarget alvo) async {
    final controller = ref.read(editorControllerProvider.notifier);
    final t = _playback.time.value;
    switch (alvo) {
      case AddTarget.midia:
        _openAdd(AddTab.midia);
      case AddTarget.audio:
        _openAdd(AddTab.audio);
      case AddTarget.forma:
        _openAdd(AddTab.forma);
      case AddTarget.objeto:
      case AddTarget.icone:
        _openAdd(AddTab.objeto);
      case AddTarget.ajuda:
        _playback.pause();
        Navigator.of(context).push(
          MaterialPageRoute<void>(
            builder: (_) => const QuickGuideScreen(initialQuery: ''),
          ),
        );
      case AddTarget.texto:
        _playback.pause();
        controller.addTextLayer(t);
      case AddTarget.efeito:
        // Um efeito sobre tudo: camada de ajuste, ja com o painel de
        // efeitos aberto.
        _playback.pause();
        controller.addAdjustmentLayer(t);
        _session.openPanel(EditorPanel.effects);
      case AddTarget.grupo:
        _agruparPorEscolha();
      case AddTarget.legendas:
        _playback.pause();
        await showCaptionCreationSheet(context, ref);
      case AddTarget.marcas:
        await menuDasMarcas(context, ref, _playback);
      case AddTarget.batidas:
        final som = ref
            .read(editorControllerProvider)
            .layers
            .where((l) => l is AudioLayer || l is VideoLayer)
            .firstOrNull;
        if (som == null) {
          showReasonToast(context, 'Adicione um audio ou um video primeiro');
          return;
        }
        _playback.pause();
        await showBeatsSheet(context, ref, som.id);
      case AddTarget.autoEdit:
        // Fora do app por enquanto: sem entrada na interface.
        break;
    }
  }

  /// O MENU DA LINHA DO TEMPO, agora aberto pelo ⋮ da barra do projeto.
  void _abrirMenuDaTimeline(BuildContext context, WidgetRef ref) {
    menuDaTimeline(
      context,
      ref,
      _playback,
      onDefinirMiniatura: _usarQuadroComoMiniatura,
      onAgrupar: _agruparPorEscolha,
      onGuia: () {
        _playback.pause();
        Navigator.of(context).push(
          MaterialPageRoute<void>(
            builder: (_) => const QuickGuideScreen(initialQuery: ''),
          ),
        );
      },
    );
  }

  /// GRUPO sem selecao: escolher as camadas numa lista.
  Future<void> _agruparPorEscolha() async {
    final layers = ref.read(editorControllerProvider).layers;
    if (layers.length < 2) {
      showReasonToast(context, 'Um grupo precisa de duas ou mais camadas');
      return;
    }
    final escolhidas = <String>{};
    final ok = await showModalBottomSheet<bool>(
      context: context,
      backgroundColor: AmColors.panel,
      isScrollControlled: true,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setSheet) => SafeArea(
          child: ConstrainedBox(
            constraints: BoxConstraints(
              maxHeight: MediaQuery.of(ctx).size.height * .7,
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                const Padding(
                  padding: EdgeInsets.fromLTRB(18, 14, 18, 6),
                  child: AppText(
                    'Agrupar quais camadas?',
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                ),
                Flexible(
                  child: ListView(
                    shrinkWrap: true,
                    children: [
                      for (final l in layers)
                        CheckboxListTile(
                          key: ValueKey('agrupar-${l.id}'),
                          value: escolhidas.contains(l.id),
                          title: AppText(
                            l.name,
                            style: const TextStyle(color: AmColors.text),
                          ),
                          activeColor: AmColors.action,
                          onChanged: (v) => setSheet(() {
                            if (v == true) {
                              escolhidas.add(l.id);
                            } else {
                              escolhidas.remove(l.id);
                            }
                          }),
                        ),
                    ],
                  ),
                ),
                Padding(
                  padding: const EdgeInsets.all(12),
                  child: FilledButton(
                    key: const ValueKey('agrupar-confirmar'),
                    onPressed: escolhidas.length >= 2
                        ? () => Navigator.pop(ctx, true)
                        : null,
                    child: const AppText('Agrupar'),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
    if (ok == true && escolhidas.length >= 2) {
      agruparSelecao(ref, escolhidas);
    }
  }

  // ------------------------------------------------------- build

  @override
  Widget build(BuildContext context) {
    final selectedId = ref.watch(selectedLayerProvider);
    final multi = ref.watch(multiSelectProvider);
    final s = ref.watch(editorSessionProvider);

    // TELA CHEIA DE VERDADE: some a barra de status e a de navegacao
    // enquanto a previa ocupa a tela; voltam ao sair.
    ref.listen<bool>(editorSessionProvider.select((s) => s.previewExpanded), (
      antes,
      agora,
    ) {
      if (antes == agora) return;
      _modoDeSistema(agora);
    });

    ref.listen<String?>(selectedLayerProvider, (previous, next) {
      if (previous == next) return;
      // Um painel/atalho capturado para A nao pode editar A depois de
      // selecionar B.
      RecentSheets.instance.clear();
      closeActiveParamSheet(context);
      _fecharEditPoints();
      if (_s.panel != EditorPanel.none) _session.closePanel();
    });

    ref.listen(editorControllerProvider, (previous, updated) {
      if (previous?.id != updated.id) {
        RecentSheets.instance.clear();
        closeActiveParamSheet(context);
      }
      // Dentro de um grupo o estado e o grupo; o que se salva e o todo.
      ref
          .read(projectsControllerProvider.notifier)
          .upsert(ref.read(editorControllerProvider.notifier).projetoCompleto);
      _syncVideos();
    });

    // Desenho vetorial pelo menu de adicionar: abre o Edit Points na
    // camada recem-criada.
    ref.listen<String?>(editPointsRequestProvider, (_, id) {
      if (id == null) return;
      ref.read(editPointsRequestProvider.notifier).state = null;
      _abrirEditPoints(id);
    });

    // O clock compoe na taxa da COMPOSICAO, nao na da tela.
    _playback.compositionFps = ref.watch(
      editorControllerProvider.select((p) => p.fps),
    );

    final layer = selectedId == null
        ? null
        : ref.watch(editorControllerProvider).layerById(selectedId);
    final targets = <String>{...multi, ?selectedId};
    final semCamadas = ref.watch(
      editorControllerProvider.select((p) => p.layers.isEmpty),
    );

    // O painel da categoria aberta (null sem categoria).
    final Widget? panel = switch (s.panel) {
      EditorPanel.none || EditorPanel.add => null,
      EditorPanel.transform => TransformPanel(
        playback: _playback,
        tool: s.tool,
        onToolChanged: _session.setTool,
        onBack: _back,
        onOpenCurve: _openCurve,
      ),
      EditorPanel.blending => BlendingPanel(
        playback: _playback,
        onBack: _back,
        onOpenCurve: _openCurve,
        onEditMaskPoints: _abrirMaskEditPoints,
      ),
      EditorPanel.colorFill => ColorFillPanel(
        onBack: _back,
        playback: _playback,
      ),
      EditorPanel.effects => EffectsPanel(playback: _playback, onBack: _back),
      EditorPanel.curve => CurvePanel(
        playback: _playback,
        prop: s.curveProp,
        onBack: _back,
      ),
      EditorPanel.editText => TextPanel(playback: _playback),
      EditorPanel.editShape => ShapePanel(
        playback: _playback,
        tool: s.shapeTool,
        onToolChanged: _session.setShapeTool,
        onBack: _back,
        onEditPoints: _abrirEditPoints,
      ),
      EditorPanel.editPoints => PointsPanel(
        key: _pointsKey,
        playback: _playback,
        layerId: selectedId ?? '',
        itemId: s.pointsItemId ?? '',
        onBack: _back,
      ),
    };

    final pinkPlayhead =
        s.panel == EditorPanel.effects ||
        s.panel == EditorPanel.curve ||
        s.animandoTexto;

    // Diamantes da propriedade ativa acendem; os demais ficam apagados.
    final Set<int>? activeTimesUs = layer == null
        ? null
        : switch (s.panel) {
            EditorPanel.none || EditorPanel.add => null,
            EditorPanel.transform => _timesForProp(layer, propOfTool(s.tool)),
            EditorPanel.curve => _timesForProp(layer, s.curveProp),
            EditorPanel.blending => {
              ...layer.opacityTimesUs,
              ...layer.maskTimesUs,
            },
            EditorPanel.effects => layer.effectTimesUs,
            EditorPanel.colorFill => const <int>{},
            EditorPanel.editText => null,
            EditorPanel.editShape => layer.moduleTimesUs,
            EditorPanel.editPoints =>
              (ref.watch(pathEditTargetProvider)?.forma ?? true)
                  ? layer.moduleTimesUs
                  : layer.maskTimesUs,
          };

    final temContexto =
        s.panel != EditorPanel.none ||
        ref.watch(freehandRequestProvider) ||
        s.previewExpanded ||
        s.timelineExpanded ||
        selectedId != null ||
        multi.isNotEmpty ||
        ref.read(editorControllerProvider.notifier).dentroDeGrupo;

    // O CONTEUDO DA ZONA E, pela selecao.
    //
    // SEM SELECAO E SEM O "+", O PAINEL NAO EXISTE: a barra de adicionar
    // ficava aberta o tempo todo e comia meia tela de timeline sem
    // ninguem ter pedido. Ela e o segundo passo do "+", e o toque na
    // timeline fecha (ver AmTimeline: tocar no vazio fecha o adicionar).
    Widget? conteudo;
    String? titulo;
    String? trilha;
    if (panel != null) {
      conteudo = panel;
      titulo = _tituloDoPainel(s);
      trilha = layer == null
          ? ref.watch(editorControllerProvider).name
          : '${ref.watch(editorControllerProvider).name} › ${layer.name}';
    } else if (s.adding) {
      conteudo = AddLayerPanel(
        key: const ValueKey('adicionar-camada'),
        playhead: _playback.time.value,
        initialTab: _addTab,
        onClose: _session.closeAdd,
        onProjectAction: _onAddTarget,
      );
    } else if (targets.length >= 2) {
      conteudo = MultiSelectionPanel(targets: targets, playback: _playback);
    } else if (layer != null) {
      conteudo = LayerToolsDock(
        layer: layer,
        playback: _playback,
        onAction: (action) => _openLayerAction(layer, action),
      );
    } else {
      conteudo = null;
    }
    // AS DICAS DE PRIMEIRO USO MORAM NA FOLHA, e nao por cima do palco.
    //
    // Como cartao flutuante elas cobriam o alto do preview — e o alto do
    // preview e onde fica a alca de GIRAR. Enquanto as quatro dicas
    // estavam na tela, girar era impossivel: o toque batia nos botoes do
    // cartao. Ensinar a mexer no palco tapando o palco e o pior lugar
    // possivel; embaixo, no lugar onde a ajuda ja vive, elas nao tapam
    // nada.
    //
    // Elas ocupam o lugar da DICA DO PALCO — a linha "toque num objeto"
    // que ja aparece quando nada esta selecionado. Assim que a pessoa
    // seleciona alguma coisa, as ferramentas da camada voltam a mandar:
    // ensinar e util ate o momento em que atrapalha.
    final mostrandoDicas =
        !_dicasVistas && !s.previewExpanded && conteudo is DicaDoPalco;
    if (mostrandoDicas) {
      conteudo = OnboardingCoach(
        onFechar: () {
          setState(() => _dicasVistas = true);
          OnboardingPrefs.marcar(ref, true);
        },
      );
    }
    // O estado vazio e a dica ocupam UMA linha; o resto e painel de verdade.
    final folhaFina =
        !mostrandoDicas &&
        panel == null &&
        !s.adding &&
        layer == null &&
        targets.length < 2;

    return AureaTheme(
      tokens: AureaTokens.motion,
      child: PopScope(
        canPop: !temContexto,
        onPopInvokedWithResult: (didPop, _) {
          if (!didPop) _back();
        },
        child: Scaffold(
          key: paramSheetHostKey,
          resizeToAvoidBottomInset: ModalRoute.of(context)?.isCurrent ?? true,
          backgroundColor: AmColors.bg,
          body: SafeArea(
            child: LayoutBuilder(
              builder: (context, constraints) {
                final ws = EditorLayoutMetrics.workspace(constraints.maxHeight);
                // A ALTURA DO PREVIEW SAI DA PROPORCAO DA COMPOSICAO.
                //
                // Antes era uma fracao fixa da tela (40%), o que dava um
                // retangulo com a proporcao do APARELHO. Um projeto 16:9
                // dentro dele encostava so nas laterais e deixava duas
                // tarjas pretas — e como o fundo do palco tambem e preto,
                // o que se via era um vazio enorme e nenhuma pista de
                // onde a composicao comeca. Deduzindo a altura da
                // proporcao, o quadro preenche a area inteira e o que
                // sobra vai para a timeline, que estava vazia.
                final proporcao = ref
                    .watch(editorControllerProvider)
                    .aspectRatio;
                // O PALCO TEM TAMANHO PROPRIO; a composicao encaixa
                // DENTRO dele.
                //
                // Ate 16/09 o palco encolhia ate colar na composicao.
                // A intencao era boa — tirava as tarjas pretas de um
                // projeto largo — mas o preco era um editor apertado:
                // um 16:9 num celular de 390 dava 219 px de composicao,
                // e com o palco colado nela o preview virava 26% da
                // tela, contra 40% de um 9:16. A mesma tela mudava de
                // cara conforme o formato do projeto.
                //
                // O dono mandou a planta com os dois formatos lado a
                // lado: la o palco tem SEMPRE a mesma altura, e o que
                // muda e onde sobra folga — em cima e embaixo no 16:9,
                // nas laterais no 9:16. A folga nao e preta: e a cor do
                // painel, entao le como moldura de palco e nao como
                // video quebrado (ver a cor do fundo em preview_stage).
                //
                // A proporcao do projeto nao entra mais nesta conta.
                final fracaoDaComposicao =
                    constraints.maxHeight <= 0 || proporcao <= 0
                    ? EditorSession.alturaDoPreview
                    : math.min(
                        EditorSession.alturaDoPreview,
                        // A RESERVA DO PAINEL: 250 px, e nao 230.
                        //
                        // Num celular de 667 o palco novo tomava 16 px
                        // a mais, e 16 px bastaram para o trilho do
                        // painel de transformacao perder um botao. Numa
                        // tela alta a conta nem encosta neste piso (a
                        // fracao de 42% e maior), entao o palco
                        // generoso continua igual onde ha espaco.
                        math.max(96, ws - 90 - (ws * .42).clamp(250, 320)) /
                            constraints.maxHeight,
                      );
                final m = EditorLayoutMetrics.solve(
                  totalHeight: constraints.maxHeight,
                  previewFraction: fracaoDaComposicao,
                  // A dica e o estado vazio ocupam UMA linha; painel de
                  // verdade ocupa o nivel que a alca deixou.
                  // A FOLHA FINA E A FAIXA DO CABECALHO MAIS UMA LINHA.
                  //
                  // Era 48 px fixos, o que dava certo enquanto o
                  // cabecalho tinha 18. Com o cabecalho em 34 (o Voltar
                  // ganhou tamanho de alvo), sobravam 14 px para o texto
                  // e a dica saia cortada pela metade.
                  // O PAINEL DO LOTE ("N camadas") e um cabecalho e uma
                  // fileira de botoes: com a altura de painel inteiro, ele
                  // comia a timeline e as outras camadas nao apareciam.
                  sheetFraction: conteudo is MultiSelectionPanel && ws > 0
                      ? (ContextSheet.handleHeight + 124) / ws
                      : s.animandoTexto
                      // A ABA DE ANIMACAO E A FERRAMENTA: ela pede o teto
                      // que o layout permite, e quem cede e a timeline.
                      ? EditorSession.alturaDaAnimacaoDeTexto
                      : folhaFina && ws > 0
                      ? (ContextSheet.handleHeight + (semCamadas ? 86 : 30)) /
                            ws
                      : (mostrandoDicas && ws > 0
                            ? (ContextSheet.handleHeight + 108) / ws
                            : (s.adding
                                  ? 0.48
                                  : (s.panel != EditorPanel.none
                                        ? 0.46
                                        : EditorSession.alturaDaFolha))),
                  previewExpanded: s.previewExpanded,
                  timelineExpanded: s.timelineExpanded,
                  sheetVisible: conteudo != null,
                  timelineFloor: s.animandoTexto
                      ? EditorSession.pisoDaTimelineAoAnimar
                      : layer != null || s.panel != EditorPanel.none
                      ? 90
                      : 120,
                  sheetMayCoverTimeline: s.adding,
                  focusedLayer:
                      layer != null && s.panel != EditorPanel.none && !s.adding,
                );
                // AS PECAS, montadas uma vez; o arranjo depende da largura
                // (Fase 7: acima de 700 pt, timeline e painel lado a lado —
                // tablet e paisagem).
                // O PALCO COM A COLUNA DE VISUALIZACAO: pixels, grade,
                // solo, camera e zoom, colados a direita quando o olho da
                // barra de reproducao a abre.
                Widget preview(double? altura) => SizedBox(
                  height: altura,
                  child: Stack(
                    children: [
                      Positioned.fill(
                        child: RepaintBoundary(
                          key: previewStageKey,
                          child: PreviewStage(
                            playback: _playback,
                            videos: _videos,
                          ),
                        ),
                      ),
                      // ABAIXO do seletor de resolucao (right/top 4):
                      // no topo o trilho cobria o botao e o toque nunca
                      // chegava nele.
                      if (!s.previewExpanded &&
                          ref.watch(
                            opcoesDeVisualizacaoProvider.select(
                              (o) => o.aberta,
                            ),
                          ))
                        Positioned(
                          right: 0,
                          top: 40,
                          bottom: 6,
                          child: Align(
                            alignment: Alignment.topRight,
                            child: ColunaDeVisualizacao(playback: _playback),
                          ),
                        ),
                      const Positioned(
                        left: 8,
                        top: 8,
                        child: IndicadorDeZoomDoPalco(),
                      ),
                    ],
                  ),
                );
                final alca = PreviewResizeHandle(
                  expanded: false,
                  onExpand: _session.togglePreviewExpanded,
                );
                final transporte = BarraDeReproducao(playback: _playback);
                // A BARRA DE CIMA SEGUE A SELECAO: lote, camada ou projeto.
                final barraDoTopo = multi.isNotEmpty
                    ? BarraDoLote(playback: _playback) as Widget
                    : (layer != null
                          ? BarraDaCamada(
                              layerId: layer.id,
                              onBack: _back,
                              playback: _playback,
                            )
                          : BarraDoProjeto(
                              onBack: _back,
                              playback: _playback,
                              onMenu: () => _abrirMenuDaTimeline(context, ref),
                            ));
                Widget timeline(double alturaTimeline) => RepaintBoundary(
                  child: AmTimeline(
                    playback: _playback,
                    height: alturaTimeline,
                    singleLayerId:
                        targets.length < 2 &&
                            s.panel != EditorPanel.none &&
                            !s.adding
                        ? selectedId
                        : null,
                    playheadColor: pinkPlayhead ? AmColors.pink : Colors.white,
                    onTapLayer: (l) {
                      if (panel != null) {
                        _back();
                        return;
                      }
                      _onTapLayer(l);
                    },
                    onTapBackground: panel != null ? _back : null,
                    onScrub: _videos.scrub,
                    onExpand: _session.toggleTimelineExpanded,
                    expanded: s.timelineExpanded,
                    activeTimesUs: activeTimesUs,
                    onForeignKeyframe: panel == null
                        ? null
                        : _onForeignKeyframe,
                    onKeyframeTap: _onKeyframeTap,
                  ),
                );
                Widget folha(double alturaFolha) => ContextSheet(
                  height: alturaFolha,
                  title: titulo,
                  subtitle: trilha,
                  onBack: titulo == null ? null : _back,
                  child: RepaintBoundary(child: conteudo),
                );
                final largo =
                    constraints.maxWidth >= 600 &&
                        constraints.maxWidth > constraints.maxHeight &&
                        !s.previewExpanded ||
                    constraints.maxWidth >= 900 && !s.previewExpanded;
                final larguraFolha = (constraints.maxWidth * .4).clamp(
                  280.0,
                  380.0,
                );
                final alturaTimelineLarga =
                    ((constraints.maxHeight -
                                AureaTokens.topBar -
                                AureaTokens.transport -
                                EditorLayoutMetrics.handleHeight) *
                            0.34)
                        .clamp(88.0, 280.0)
                        .toDouble();
                return Stack(
                  children: [
                    if (largo)
                      Column(
                        key: const ValueKey('editor-largo'),
                        children: [
                          barraDoTopo,
                          Expanded(
                            child: Row(
                              crossAxisAlignment: CrossAxisAlignment.stretch,
                              children: [
                                Expanded(
                                  child: Column(
                                    children: [
                                      Expanded(child: preview(null)),
                                      alca,
                                      transporte,
                                      timeline(alturaTimelineLarga),
                                    ],
                                  ),
                                ),
                                SizedBox(
                                  width: larguraFolha,
                                  child: folha(
                                    constraints.maxHeight - AureaTokens.topBar,
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ],
                      )
                    else
                      Column(
                        children: [
                          if (!s.previewExpanded) barraDoTopo,
                          preview(
                            s.previewExpanded
                                ? (m.preview - BarraDeTempoTelaCheia.altura)
                                      .clamp(0.0, double.infinity)
                                      .toDouble()
                                : m.preview,
                          ),
                          if (!s.previewExpanded) alca,
                          if (s.previewExpanded)
                            BarraDeTempoTelaCheia(playback: _playback),
                          transporte,
                          if (!s.previewExpanded) ...[
                            timeline(m.timeline),
                            if (conteudo != null) folha(m.sheet),
                          ],
                        ],
                      ),
                    // O "+": circulo escuro com anel lima no canto de
                    // baixo a direita da timeline; o ⋮ fica no esquerdo.
                    if (!s.previewExpanded &&
                        !s.adding &&
                        s.panel == EditorPanel.none)
                      Positioned(
                        right: 18 + (largo ? larguraFolha : 0),
                        bottom: 18 + (largo || conteudo == null ? 0 : m.sheet),
                        child: Tooltip(
                          message: 'Adicionar camada',
                          child: GestureDetector(
                            key: const ValueKey('editor-fab'),
                            onTap: _openAdd,
                            child: Container(
                              width: 52,
                              height: 52,
                              decoration: BoxDecoration(
                                shape: BoxShape.circle,
                                color: const Color(0xFF1E2130),
                                border: Border.all(
                                  color: CromoEditor.acao,
                                  width: 2.2,
                                ),
                                boxShadow: const [
                                  BoxShadow(
                                    color: Colors.black45,
                                    blurRadius: 8,
                                    offset: Offset(0, 3),
                                  ),
                                ],
                              ),
                              child: const Icon(
                                Icons.add,
                                size: 32,
                                color: CromoEditor.acao,
                              ),
                            ),
                          ),
                        ),
                      ),
                    // A BARRA FLUTUANTE DO LOTE (aparar, dividir, alinhar no
                    // tempo) saiu da selecao multipla (beta 89): ficava em
                    // cima das trilhas e escondia as camadas selecionadas.
                    if (s.previewExpanded)
                      Positioned(
                        right: 10,
                        top: 10,
                        child: Tooltip(
                          message: 'Voltar ao editor',
                          child: GestureDetector(
                            key: const ValueKey('preview-collapse'),
                            onTap: _session.togglePreviewExpanded,
                            child: Container(
                              width: 40,
                              height: 40,
                              decoration: const BoxDecoration(
                                color: Color(0xCC12151A),
                                shape: BoxShape.circle,
                              ),
                              child: const Icon(
                                Icons.fullscreen_exit,
                                color: AmColors.text,
                              ),
                            ),
                          ),
                        ),
                      ),
                    if (ref.watch(debugOverlayProvider))
                      Positioned(
                        top: s.previewExpanded ? 6 : AureaTokens.topBar + 6,
                        left: 8,
                        child: IgnorePointer(
                          child: _DiagOverlay(playback: _playback),
                        ),
                      ),
                    // RASCUNHO ENQUANTO TOCA: a cena 3D e o glow desenham
                    // simplificados durante a reproducao.
                    Positioned(
                      top: s.previewExpanded ? 6 : AureaTokens.topBar + 6,
                      right: 8,
                      child: const _RascunhoBadge(),
                    ),
                  ],
                );
              },
            ),
          ),
        ),
      ),
    );
  }
}

/// Overlay de diagnostico: MARCHA com o motivo, composicoes por segundo,
/// variancia entre ticks e % de tempo em marcha baixa.
class _DiagOverlay extends ConsumerWidget {
  const _DiagOverlay({required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final fps = ref.watch(editorControllerProvider.select((p) => p.fps));
    final total = ref.watch(
      editorControllerProvider.select((p) => p.layers.length),
    );
    PreviewStats.hookTimings();
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        ValueListenableBuilder<GearDecision?>(
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
                                builder: (context, r, _) => AppText(
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
                                    fontFeatures: [
                                      FontFeature.tabularFigures(),
                                    ],
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
        ),
        const SizedBox(height: 4),
        const _Diag3D(),
      ],
    );
  }
}

/// O MOTOR 3D NO OVERLAY: nivel, pressao, a estimativa de GPU contra o
/// orcamento, memoria do processo e o que o ultimo quadro desenhou.
class _Diag3D extends StatelessWidget {
  const _Diag3D();

  @override
  Widget build(BuildContext context) {
    final c = ControladorDeQualidade3D.instancia;
    return ValueListenableBuilder<Estatisticas3D?>(
      valueListenable: PreviewStats.cena3d,
      builder: (context, e, _) => ValueListenableBuilder<Qualidade3D>(
        valueListenable: c.nivel,
        builder: (context, nivel, _) => ValueListenableBuilder<NivelDePressao>(
          valueListenable: c.pressao,
          builder: (context, pressao, _) => ValueListenableBuilder<int>(
            valueListenable: PreviewStats.rssMb,
            builder: (context, rss, _) {
              if (e == null && c.cenasNaTela == 0) {
                return const SizedBox.shrink();
              }
              final est = c.estimativa.value;
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
                child: AppText(
                  '── 3D ──\n'
                  'nivel ${qualidade3dRotulo(nivel)} · pressao '
                  '${nivelDePressaoRotulo(pressao)} · ${c.motivo.value}\n'
                  'GPU estimada ${bytesLegiveis(est.total)} de '
                  '${bytesLegiveis(c.orcamentoBytes)} '
                  '(alvos ${bytesLegiveis(est.alvosDeRender)} · sombras '
                  '${bytesLegiveis(est.sombras)} · tex ${bytesLegiveis(est.texturas)} · '
                  'geo ${bytesLegiveis(est.geometria)})\n'
                  'RSS $rss MB · disponivel '
                  '${c.disponivelBytes >= 0 ? bytesLegiveis(c.disponivelBytes) : '?'} · '
                  'termico ${c.termico}\n'
                  '${e ?? 'sem quadro em GPU'}',
                  style: const TextStyle(
                    fontSize: 11,
                    color: AmColors.accent,
                    height: 1.4,
                    fontFeatures: [FontFeature.tabularFigures()],
                  ),
                ),
              );
            },
          ),
        ),
      ),
    );
  }
}

/// "Rascunho" sobre o preview enquanto toca — so quando ha algo que o
/// rascunho simplifica (cena 3D, brilho, nitidez), para nao virar ruido.
///
/// A LISTA SAI DO PROPRIO MOTOR. Ela era escrita a mao e listava
/// `lightGlow` e `glowVol` — dois efeitos que sairam do catalogo em 16/09.
/// O aviso passou a NUNCA aparecer, inclusive nos efeitos de brilho que
/// HOJE sao simplificados ao tocar: quem dava play via um halo mais cru e
/// nao tinha como saber que a qualidade final era outra. Derivar de
/// `receitasSapphire` e `passadasDeNitidez` faz o aviso acompanhar o
/// motor sozinho.
class _RascunhoBadge extends ConsumerWidget {
  const _RascunhoBadge();

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final simplifica = ref.watch(
      editorControllerProvider.select(
        (p) => p.layers.any(
          (l) =>
              l is Scene3DLayer ||
              l.effects.any(
                (e) =>
                    e.enabled &&
                    ((receitasSapphire[e.type]?.usaOrcamentoDeAmostras ??
                            false) ||
                        e.type == EffectType.unsharpMask),
              ),
        ),
      ),
    );
    if (!simplifica) return const SizedBox.shrink();
    return ValueListenableBuilder<bool>(
      valueListenable: PlaybackController.tocandoAgora,
      builder: (context, tocando, _) {
        if (!tocando) return const SizedBox.shrink();
        return IgnorePointer(
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
            decoration: BoxDecoration(
              color: const Color(0xCC12151A),
              borderRadius: BorderRadius.circular(999),
            ),
            child: const AppText(
              'Rascunho · pause para ver a qualidade final',
              style: TextStyle(fontSize: 10.5, color: AmColors.muted),
            ),
          ),
        );
      },
    );
  }
}
