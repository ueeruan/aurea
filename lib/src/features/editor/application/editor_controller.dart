import 'dart:io';
import 'dart:ui';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:video_player/video_player.dart';

import '../../media/application/media_import_service.dart';
import '../domain/blend_extra.dart';
import 'blob_track_service.dart';
import 'freehand_session.dart';
import '../domain/blob_track.dart';
import '../domain/caption.dart';
import '../domain/caption_highlight.dart';
import '../domain/camera3d.dart';
import '../domain/camera_cuts.dart';
import '../domain/cut.dart';
import '../domain/cut_ops.dart';
import '../domain/scene3d.dart';
import '../domain/effect.dart';
import '../domain/effect_preset.dart';
import '../domain/element3d.dart';
import '../domain/extrude3d.dart';
import '../domain/glb_import.dart';
import '../domain/model_asset3d.dart';
import 'tracking_service.dart';
import '../domain/tracker2d.dart';
import '../domain/fx.dart';
import '../domain/grid_rig.dart';
import '../domain/keyframe.dart';
import '../domain/scene_motion.dart';
import '../domain/loudness.dart';
import '../domain/layer.dart';
import '../domain/layer_meta.dart';
import '../domain/layout_ops.dart';
import '../domain/measure.dart';
import 'media_preview_service.dart';
import 'proxy_service.dart';
import '../domain/audio_ops.dart';
import '../domain/apple_motion.dart';
import '../domain/mask.dart';
import '../domain/nle_ops.dart';
import '../domain/shape.dart';
import '../domain/shape_ops.dart';
import '../domain/text_anim.dart';
import '../domain/text_path.dart';
import '../domain/text_animator.dart';
import '../domain/text_presets.dart';
import '../domain/video_project.dart';

export '../domain/video_project.dart' show LayerProp, PropertyLink;

/// Camada selecionada no editor (null = nada).
final selectedLayerProvider = StateProvider<String?>((ref) => null);
final autoKeyframeProvider = StateProvider<bool>((ref) => false);

/// Selecao MULTIPLA (toque longo nas barras): a barra de acoes opera no
/// conjunto — agrupar, duplicar e excluir em lote.
final multiSelectProvider = StateProvider<Set<String>>((ref) => const {});

/// Estado central do editor: a composicao aberta e as operacoes sobre ela.
/// Toda mutacao passa por [_mutate], que alimenta o undo/redo.
class EditorController extends Notifier<VideoProject> {
  final List<VideoProject> _undoStack = [];
  final List<VideoProject> _redoStack = [];
  final Map<String, Map<String, bool>> _visibilityBeforeIsolation = {};
  final Map<String, String> _isolatedSceneNode = {};
  DateTime _lastPush = DateTime.fromMillisecondsSinceEpoch(0);

  @override
  VideoProject build() => VideoProject.empty('Novo projeto');

  bool get canUndo => _undoStack.isNotEmpty;
  bool get canRedo => _redoStack.isNotEmpty;

  /// Mutacoes continuas (arrasto de slider/regua) dentro desta janela sao
  /// coalescidas num unico passo de undo.
  void _mutate(VideoProject next) {
    final now = DateTime.now();
    // Dentro de um grupo, ninguem empilha: o snapshot foi tirado uma vez
    // no comeco, e desfazer volta o passo inteiro.
    if (!_agrupando &&
        now.difference(_lastPush) > const Duration(milliseconds: 450)) {
      _undoStack.add(state);
      if (_undoStack.length > 100) _undoStack.removeAt(0);
    }
    _lastPush = now;
    _redoStack.clear();
    state = next;
  }

  bool _agrupando = false;

  /// N EDICOES, UM UNDO SO.
  ///
  /// O AutoEdit corta trinta silencios num passo. Sem isto, desfazer
  /// "os cortes" seria tocar trinta vezes em desfazer — e a pessoa
  /// desistiria no quinto. A janela de 450 ms nao resolve porque o
  /// trabalho leva segundos.
  void runAsOneUndo(void Function() body) {
    if (_agrupando) {
      body();
      return;
    }
    _undoStack.add(state);
    if (_undoStack.length > 100) _undoStack.removeAt(0);
    _redoStack.clear();
    _agrupando = true;
    try {
      body();
    } finally {
      _agrupando = false;
      _lastPush = DateTime.now();
    }
  }

  void undo() {
    if (_undoStack.isEmpty) return;
    _redoStack.add(state);
    state = _undoStack.removeLast();
    _lastPush = DateTime.fromMillisecondsSinceEpoch(0);
  }

  void redo() {
    if (_redoStack.isEmpty) return;
    _undoStack.add(state);
    state = _redoStack.removeLast();
    _lastPush = DateTime.fromMillisecondsSinceEpoch(0);
  }

  void openProject(VideoProject project) {
    // Exportação e miniaturas também abrem projetos, sem sessão de desenho.
    if (ref.exists(freehandRequestProvider)) {
      ref.read(freehandRequestProvider.notifier).state = false;
    }
    if (ref.exists(onionSkinProvider)) {
      ref.read(onionSkinProvider.notifier).state = 0;
    }
    state = project;
    _undoStack.clear();
    _redoStack.clear();
    _visibilityBeforeIsolation.clear();
    _isolatedSceneNode.clear();
    ref.read(selectedLayerProvider.notifier).state = null;
    // A selecao multipla tambem carrega ids do projeto anterior: sem
    // limpar, o cabecalho abre verde ("2 camadas") sobre camadas que nao
    // existem, e agrupar/excluir agem sobre ids mortos.
    ref.read(multiSelectProvider.notifier).state = const {};
  }

  void renameProject(String name) => _mutate(state.copyWith(name: name));

  Offset get _center => Offset(state.outputWidth / 2, state.outputHeight / 2);

  // ---------------------------------------------------------------- camadas

  void _push(Layer layer) {
    _mutate(state.copyWith(layers: [layer, ...state.layers]));
    ref.read(selectedLayerProvider.notifier).state = layer.id;
  }

  void _replace(Layer layer) {
    _mutate(
      state.copyWith(
        layers: [for (final l in state.layers) l.id == layer.id ? layer : l],
      ),
    );
  }

  Layer? _layer(String id) => state.layerById(id);

  void addTextLayer(Duration at, {String text = 'Seu texto'}) {
    _push(
      TextLayer(
        name: text,
        startTime: at,
        duration: const Duration(seconds: 3),
        text: text,
        position: AnimatedOffset(_center),
      ),
    );
  }

  void addShapeLayer(
    Duration at, {
    List<ShapeItem>? contents,
    String name = 'Forma',
  }) {
    final n = state.layers.whereType<ShapeLayer>().length + 1;
    _push(
      ShapeLayer(
        name: '$name $n',
        startTime: at,
        duration: const Duration(seconds: 3),
        contents: contents,
        position: AnimatedOffset(_center),
      ),
    );
  }

  /// Insere um icone do Iconify como FORMA vetorial editavel (nunca
  /// imagem): Trim, Repeater, morph, gradiente e mascaras funcionam.
  void addIconLayer(Duration at, String pathData, String name) {
    _push(
      ShapeLayer(
        name: name,
        startTime: at,
        duration: const Duration(seconds: 3),
        contents: [
          ShapeSvgPath(pathData: pathData),
          ShapeFill(color: const Color(0xFFFFFFFF)),
        ],
        position: AnimatedOffset(_center),
      ),
    );
  }

  void addImageLayer(Duration at, String path, String name) {
    _push(
      ImageLayer(
        name: name,
        startTime: at,
        duration: const Duration(seconds: 3),
        sourcePath: path,
        position: AnimatedOffset(_center),
      ),
    );
  }

  String addVideoLayer(
    Duration at,
    String path,
    String name,
    Duration duration,
  ) {
    final layer = VideoLayer(
      name: name,
      startTime: at,
      duration: duration,
      sourcePath: path,
      position: AnimatedOffset(_center),
    );
    _push(layer);
    return layer.id;
  }

  Future<void> importVideoFromGallery(Duration at) async {
    final file = await ref
        .read(mediaImportServiceProvider)
        .pickVideoFromGallery();
    if (file == null) return;
    // A camada entra NA HORA com duracao provisoria; a duracao real chega
    // do probe em background (inicializar um decoder travava o import).
    final id = addVideoLayer(
      at,
      file.path,
      file.name,
      const Duration(seconds: 4),
    );
    _probeDuration(file.path).then((d) {
      final layer = _layer(id);
      if (layer is VideoLayer && d > Duration.zero) {
        _replace(layer.copyLayer(duration: d));
      }
    });
  }

  /// IMPORTA UM VIDEO JA ESCOLHIDO e so devolve quando a duracao real
  /// chegou do probe.
  ///
  /// O caminho de sempre poe a camada na hora com duracao provisoria e
  /// deixa o probe corrigir depois — otimo para o import a mao, ruim para
  /// o AutoEdit: ele calcula corte em cima da duracao, e cortar um video
  /// de "4 segundos" que na verdade tem tres minutos nao da nada certo.
  Future<String> importVideoAwaitingDuration(
    Duration at,
    String path,
    String name,
  ) async {
    final id = addVideoLayer(at, path, name, const Duration(seconds: 4));
    final d = await _probeDuration(path);
    final layer = _layer(id);
    if (layer is VideoLayer && d > Duration.zero) {
      _replace(layer.copyLayer(duration: d));
    }
    return id;
  }

  String addAudioLayer(
    Duration at,
    String path,
    String name,
    Duration duration,
  ) {
    final layer = AudioLayer(
      name: name,
      startTime: at,
      duration: duration,
      sourcePath: path,
      position: AnimatedOffset(_center),
    );
    _push(layer);
    return layer.id;
  }

  /// Importa AUDIO pelo seletor de arquivos; duracao real chega do probe
  /// em background (mesmo fluxo do video).
  Future<void> importAudioFile(Duration at) async {
    final file = await ref.read(mediaImportServiceProvider).pickAudioFile();
    if (file == null) return;
    final id = addAudioLayer(
      at,
      file.path,
      file.name,
      const Duration(seconds: 4),
    );
    _probeDuration(file.path).then((d) {
      final layer = _layer(id);
      if (layer is AudioLayer && d > Duration.zero) {
        _replace(layer.copyLayer(duration: d));
      }
    });
  }

  // ------------------------------------------ oficio: meta da camada

  void _updateMeta(String id, LayerMeta Function(LayerMeta) fn) {
    final next = fn(state.metaOf(id));
    _mutate(state.copyWith(meta: {...state.meta, id: next}));
  }

  /// Rotulo colorido (PR-X26).
  void setLayerLabel(String id, LayerLabel? label) => _updateMeta(
    id,
    (m) =>
        label == null ? m.copyWith(clearLabel: true) : m.copyWith(label: label),
  );

  /// SOLO: havendo qualquer solo, so os solos renderizam.
  void toggleSolo(String id) =>
      _updateMeta(id, (m) => m.copyWith(solo: !m.solo));

  /// TIMIDA: some da timeline, continua no render.
  void toggleShy(String id) => _updateMeta(id, (m) => m.copyWith(shy: !m.shy));

  void toggleLocked(String id) =>
      _updateMeta(id, (m) => m.copyWith(locked: !m.locked));

  void setLayerFolder(String id, String? folder) =>
      _updateMeta(id, (m) => m.copyWith(folder: folder));

  /// BUSCA na timeline (PR-X26): nome, tipo, rotulo e predicados.
  List<Layer> searchLayers(String query) {
    final q = query.trim().toLowerCase();
    if (q.isEmpty) return state.layers;
    return [
      for (final l in state.layers)
        if (_matchesSearch(l, q)) l,
    ];
  }

  bool _matchesSearch(Layer l, String q) {
    if (l.name.toLowerCase().contains(q)) return true;
    final meta = state.metaOf(l.id);
    if (meta.label?.name.toLowerCase().contains(q) ?? false) return true;
    final type = switch (l) {
      VideoLayer _ => 'video',
      ImageLayer _ => 'imagem',
      TextLayer _ => 'texto',
      ShapeLayer _ => 'forma',
      GroupLayer _ => 'grupo',
      NullLayer _ => 'nulo',
      AudioLayer _ => 'audio',
      CaptionLayer _ => 'legenda',
      ParticlesLayer _ => 'particulas',
      AdjustmentLayer _ => 'ajuste',
      Element3DLayer _ => '3d',
      Scene3DLayer _ => 'cena 3d',
    };
    if (type.contains(q)) return true;
    if (q == 'keyframe' || q == 'animado') return l.hasAnimation;
    if (q == 'efeito') return l.effects.isNotEmpty;
    if (q == 'loop') {
      return l.position.loop.active ||
          l.scaleX.loop.active ||
          l.rotation.loop.active ||
          l.opacity.loop.active;
    }
    if (q == 'solo') return meta.solo;
    if (q == 'timida' || q == 'shy') return meta.shy;
    return false;
  }

  /// RENOMEAR EM LOTE com numeracao automatica (PR-X26).
  void renameLayers(Iterable<String> ids, String pattern) {
    var n = 1;
    final byId = {for (final id in ids) id};
    _mutate(
      state.copyWith(
        layers: [
          for (final l in state.layers)
            if (byId.contains(l.id))
              l.copyLayer(
                name: pattern.contains('#')
                    ? pattern.replaceAll('#', '${n++}')
                    : '$pattern ${n++}',
              )
            else
              l,
        ],
      ),
    );
  }

  // ---------------------------------------------- presets de efeito

  /// Salva a pilha (ou parte dela) como preset, com keyframes relativos
  /// e parametros de distancia normalizados (PR-C2).
  EffectPreset? saveEffectPresetFrom(
    String layerId,
    String name, {
    Set<String>? onlyEffectIds,
  }) {
    final layer = _layer(layerId);
    if (layer == null || layer.effects.isEmpty) return null;
    final chosen = onlyEffectIds == null
        ? layer.effects
        : [
            for (final e in layer.effects)
              if (onlyEffectIds.contains(e.id)) e,
          ];
    if (chosen.isEmpty) return null;
    return saveEffectPreset(
      name: name,
      effects: chosen,
      layerStart: Duration.zero,
      layerDuration: layer.duration,
      layerSize: layerBoxSize(layer, layer.startTime),
    );
  }

  /// Aplica um preset no cabecote. [replace] troca a pilha em vez de
  /// somar; [stretchTo] estica o preset para a duracao pedida.
  List<String> applyPreset(
    String layerId,
    EffectPreset preset, {
    required Duration at,
    bool replace = false,
    Duration? stretchTo,
  }) {
    final layer = _layer(layerId);
    if (layer == null) return const [];
    final compat = reconcilePreset(preset);
    final applied = applyEffectPreset(
      EffectPreset(
        name: preset.name,
        effects: compat.effects,
        suggestedDuration: preset.suggestedDuration,
      ),
      at: at - layer.startTime,
      targetSize: layerBoxSize(layer, at),
      stretchTo: stretchTo,
    );
    _replace(
      layer.copyLayer(
        effects: replace ? applied : [...layer.effects, ...applied],
      ),
    );
    return compat.warnings;
  }

  /// ASSAR EM KEYFRAMES (PR-C3): o movimento procedural do Tremor vira
  /// keyframes reais na camada, e o efeito sai da pilha.
  void bakeEffectToKeyframes(String layerId, String effectId, int fps) {
    final layer = _layer(layerId);
    if (layer == null) return;
    EffectInstance? effect;
    for (final e in layer.effects) {
      if (e.id == effectId) effect = e;
    }
    if (effect == null || !effect.spec.procedural) return;

    // Amostra o MESMO calculo que o compositor faz, frame a frame — por
    // isso o assado bate com o procedural.
    final fx = effect;
    TremorSample sampleAt(Duration t) => tremorSample(
      amplitudePx: fx.paramAt('amplitude', t),
      phase: integratedPhase(fx.track('frequencia'), t),
      style: fx.paramAt('estilo', t).round().clamp(0, 2),
      seed: fx.paramAt('semente', t).round(),
      zoom: fx.paramAt('zoom', t).clamp(0.0, 1.0),
      tiltDeg: fx.paramAt('inclinacao', t),
    );

    final baked = bakeProceduralMotion(
      effect: effect,
      duration: layer.duration,
      fps: fps,
      basePosition: layer.position.valueAt(Duration.zero),
      baseRotation: layer.rotation.valueAt(Duration.zero),
      baseScale: layer.scaleX.valueAt(Duration.zero),
      sampleOffset: (t) {
        final s = sampleAt(t);
        return Offset(s.dx, s.dy);
      },
      sampleRotation: (t) => sampleAt(t).rotationDeg,
      sampleScale: (t) => sampleAt(t).scale,
    );

    _replace(
      layer.copyLayer(
        position: baked.position,
        rotation: baked.rotation,
        scaleX: baked.scale,
        scaleY: baked.scale,
        effects: [
          for (final e in layer.effects)
            if (e.id != effectId) e,
        ],
      ),
    );
  }

  // ------------------------------------------------------ aparencia

  /// Estilos de camada (PR-X10).
  void setLayerStyles(String id, LayerStyles styles) =>
      _updateMeta(id, (m) => m.copyWith(styles: styles));

  void updateLayerStyles(String id, LayerStyles Function(LayerStyles) fn) =>
      _updateMeta(id, (m) => m.copyWith(styles: fn(m.styles)));

  /// PALETA (PR-X11): trocar uma entrada muda TODAS as camadas
  /// vinculadas a ela, e nenhuma outra.
  void setPaletteColor(String name, Color color) =>
      _mutate(state.copyWith(palette: state.palette.withColor(name, color)));

  void removePaletteColor(String name) =>
      _mutate(state.copyWith(palette: state.palette.without(name)));

  /// Vincula a cor da camada a uma entrada da paleta.
  void linkLayerColor(String id, String? paletteName) => _updateMeta(
    id,
    (m) => paletteName == null
        ? m.copyWith(clearColorRef: true)
        : m.copyWith(colorRef: paletteName),
  );

  /// Estilos de texto nomeados (PR-X12).
  void upsertTextStyle(TextStyleDef style) {
    final rest = [
      for (final s in state.textStyles)
        if (s.name != style.name) s,
    ];
    _mutate(state.copyWith(textStyles: [...rest, style]));
  }

  void linkTextStyle(String id, String? styleName) =>
      _updateMeta(id, (m) => m.copyWith(textStyleRef: styleName));

  // ------------------------------------------------------ responsivo

  /// Caixa de texto (PR-X13).
  void setTextBox(String id, TextBoxSpec spec) =>
      _updateMeta(id, (m) => m.copyWith(textBox: spec));

  /// Forma CONTEINER que abraca um texto (PR-X14).
  void setContainer(String id, ContainerSpec? spec) {
    _updateMeta(
      id,
      (m) => spec == null
          ? m.copyWith(clearContainer: true)
          : m.copyWith(container: spec),
    );
    if (spec != null) applyContainer(id);
  }

  /// Redimensiona a forma para abracar o texto alvo. O ponto de
  /// ancoragem decide QUAL lado fica parado quando ela cresce — sem
  /// isso, um nome mais longo desloca o layout inteiro.
  void applyContainer(String shapeId) {
    final shape = _layer(shapeId);
    final spec = state.metaOf(shapeId).container;
    if (shape is! ShapeLayer || spec == null) return;
    final target = _layer(spec.targetLayerId);
    if (target == null) return;

    final textSize = measureLayerBox(target, Duration.zero);
    final wanted = spec.sizeFor(textSize);
    final before = measureLayerBox(shape, Duration.zero);

    // Reescreve a geometria parametrica para o tamanho pedido.
    var found = false;
    final contents = [
      for (final item in shape.contents)
        if (!found && item is ShapeParametric)
          (() {
            found = true;
            return item.copyWith(
              sizeX: AnimatedDouble(wanted.width),
              sizeY: AnimatedDouble(wanted.height),
            );
          })()
        else
          item,
    ];
    if (!found) return;

    // A ancora mantem o lado escolhido parado.
    final shift = anchorShift(before, wanted, spec.anchor);
    final basePos = spec.follow
        ? target.position.valueAt(Duration.zero)
        : shape.position.valueAt(Duration.zero);
    _replace(
      shape.copyLayer(
        contents: contents,
        position: shape.position.withBase(basePos + shift),
      ),
    );
  }

  /// Empilhamento automatico num grupo (PR-X15).
  void setStack(String id, StackSpec? spec) {
    _updateMeta(
      id,
      (m) =>
          spec == null ? m.copyWith(clearStack: true) : m.copyWith(stack: spec),
    );
    if (spec != null) applyStack(id);
  }

  /// Reposiciona os filhos do grupo conforme o empilhamento. Remover o
  /// filho do meio reposiciona os outros mantendo o espaco.
  void applyStack(String groupId) {
    final group = _layer(groupId);
    final spec = state.metaOf(groupId).stack;
    if (group is! GroupLayer || spec == null) return;
    final children = <({String id, Size size})>[
      for (final c in group.children)
        (id: c.id, size: measureLayerBox(c, Duration.zero)),
    ];
    final places = stackLayout(spec, children);
    _replace(
      group.copyLayer(
        children: [
          for (final c in group.children)
            if (places[c.id] case final p?)
              c.copyLayer(position: c.position.withBase(p))
            else
              c,
        ],
      ),
    );
  }

  /// Reaplica o layout responsivo de tudo que depende de [layerId] —
  /// chamado quando o texto muda, para a forma acompanhar SOZINHA.
  void _refreshResponsive(String layerId) {
    for (final e in state.meta.entries) {
      if (e.value.container?.targetLayerId == layerId) {
        applyContainer(e.key);
      }
    }
    for (final l in state.layers) {
      if (l is GroupLayer &&
          state.metaOf(l.id).stack != null &&
          l.children.any((c) => c.id == layerId)) {
        applyStack(l.id);
      }
    }
  }

  // -------------------------------------------------------- template

  /// Expor uma propriedade da precomp (PR-X16).
  void exposeProperty(ExposedProperty prop) {
    final rest = [
      for (final e in state.exposed)
        if (e.id != prop.id) e,
    ];
    _mutate(state.copyWith(exposed: [...rest, prop]));
  }

  void unexposeProperty(String id) => _mutate(
    state.copyWith(
      exposed: [
        for (final e in state.exposed)
          if (e.id != id) e,
      ],
    ),
  );

  /// Mexer no controle do PAI altera a precomp sem abri-la.
  void setExposedValue(String exposedId, double value, Duration t) {
    ExposedProperty? prop;
    for (final e in state.exposed) {
      if (e.id == exposedId) prop = e;
    }
    if (prop == null) return;
    final v = prop.clampValue(value);
    switch (prop.property) {
      case 'opacity':
        editOpacity(prop.layerId, t, v);
      case 'rotation':
        editRotation(prop.layerId, t, v);
      case 'scale':
        editScaleUniform(prop.layerId, t, v);
      default:
        break;
    }
  }

  // ----------------------------------------------------- dados/Lottie

  /// CSV/JSON dirigindo a animacao (PR-X21).
  void setDataSource(DataSource? source) =>
      _mutate(state.copyWith(data: source));

  void addDataBinding(DataBinding binding) =>
      _mutate(state.copyWith(bindings: [...state.bindings, binding]));

  void removeDataBinding(String layerId) => _mutate(
    state.copyWith(
      bindings: [
        for (final b in state.bindings)
          if (b.layerId != layerId) b,
      ],
    ),
  );

  /// Aplica os vinculos: cada campo escreve no texto da sua camada.
  void applyDataBindings() {
    final data = state.data;
    if (data == null) return;
    var layers = state.layers;
    for (final b in state.bindings) {
      final raw = data.cell(b.row, b.column);
      if (raw == null) continue;
      final num = data.number(b.row, b.column);
      final text = num == null ? raw : b.format.format(num);
      layers = [
        for (final l in layers)
          if (l.id == b.layerId && l is TextLayer)
            l.copyLayer(text: text)
          else
            l,
      ];
    }
    _mutate(state.copyWith(layers: layers));
  }

  /// REPETIR POR LINHA (PR-X21): N copias de uma camada, uma por linha,
  /// com escalonamento de tempo automatico.
  void repeatForEachRow(
    String layerId,
    String column, {
    Duration stagger = const Duration(milliseconds: 120),
  }) {
    final data = state.data;
    final src = _layer(layerId);
    if (data == null || src is! TextLayer) return;
    final copies = <Layer>[
      for (var i = 0; i < data.rowCount; i++)
        src.duplicated().copyLayer(
          name: '${src.name} ${i + 1}',
          startTime: src.startTime + stagger * i,
          text: data.cell(i, column) ?? '',
        ),
    ];
    _mutate(state.copyWith(layers: [...copies, ...state.layers]));
  }

  /// Modo "compativel com Lottie" (PR-X23): recursos nao suportados
  /// aparecem esmaecidos desde o comeco.
  void setLottieMode(bool on) => _mutate(state.copyWith(lottieMode: on));

  // --------------------------------------------------------- guias

  void setGuides(GuidesSpec spec) => _mutate(state.copyWith(guides: spec));

  void addGuide({double? x, double? y}) => _mutate(
    state.copyWith(
      guides: state.guides.copyWith(
        vertical: x == null ? null : [...state.guides.vertical, x],
        horizontal: y == null ? null : [...state.guides.horizontal, y],
      ),
    ),
  );

  /// Motion blur da composicao (PR-X9).
  void setMotionBlur(MotionBlurSpec spec) =>
      _mutate(state.copyWith(motionBlur: spec));

  void toggleLayerMotionBlur(String id) =>
      _updateMeta(id, (m) => m.copyWith(motionBlur: !m.motionBlur));

  // ------------------------------------------------ precisao e layout

  /// Caixa renderizada da camada (px logicos).
  Size layerBoxSize(Layer layer, Duration t) => measureLayerBox(
    layer,
    layer.localTime(t),
    fallbackWidth: state.outputWidth.toDouble(),
  );

  List<LayoutBox> _layoutBoxes(Iterable<String> ids, Duration t) => [
    for (final id in ids)
      if (_layer(id) case final l?)
        (
          id: l.id,
          center: l.position.valueAt(l.localTime(t)),
          size: layerBoxSize(l, t),
        ),
  ];

  void _applyCenters(Map<String, Offset> centers, Duration t) {
    if (centers.isEmpty) return;
    var layers = state.layers;
    for (final e in centers.entries) {
      layers = [
        for (final l in layers)
          if (l.id == e.key)
            l.copyLayer(position: l.position.edited(l.localTime(t), e.value))
          else
            l,
      ];
    }
    _mutate(state.copyWith(layers: layers));
  }

  /// ALINHAR a selecao (PR-X1). Exato ao pixel: usa a caixa real de
  /// cada camada, entao tamanhos diferentes encostam no mesmo lugar.
  void alignSelection(
    Iterable<String> ids,
    AlignEdge edge,
    Duration t, {
    AlignTo to = AlignTo.composition,
    String? anchorId,
  }) {
    final boxes = _layoutBoxes(ids, t);
    if (boxes.isEmpty) return;
    _applyCenters(
      alignLayers(
        boxes,
        edge,
        to: to,
        anchorId: anchorId,
        compSize: Size(
          state.outputWidth.toDouble(),
          state.outputHeight.toDouble(),
        ),
      ),
      t,
    );
  }

  /// DISTRIBUIR (PR-X1): por centro OU por vao igual — sao operacoes
  /// diferentes quando as camadas tem tamanhos distintos.
  void distributeSelection(
    Iterable<String> ids,
    DistributeAxis axis,
    DistributeMode mode,
    Duration t,
  ) {
    _applyCenters(distributeLayers(_layoutBoxes(ids, t), axis, mode), t);
  }

  /// Espacamento exato em px entre as camadas da selecao.
  void spaceSelection(
    Iterable<String> ids,
    DistributeAxis axis,
    double gap,
    Duration t,
  ) {
    _applyCenters(spaceLayers(_layoutBoxes(ids, t), axis, gap), t);
  }

  // ------------------------------------------------------------ loop

  /// Liga/desliga o LOOP de keyframes de uma propriedade (PR-X6).
  void setPropertyLoop(String id, LayerProp prop, LoopSpec spec) {
    final layer = _layer(id);
    if (layer == null) return;
    switch (prop) {
      case LayerProp.position:
        _replace(layer.copyLayer(position: layer.position.withLoop(spec)));
      case LayerProp.scale:
        _replace(
          layer.copyLayer(
            scaleX: layer.scaleX.withLoop(spec),
            scaleY: layer.scaleY.withLoop(spec),
          ),
        );
      case LayerProp.rotation:
        _replace(layer.copyLayer(rotation: layer.rotation.withLoop(spec)));
      case LayerProp.opacity:
        _replace(layer.copyLayer(opacity: layer.opacity.withLoop(spec)));
      case LayerProp.skew:
        _replace(layer.copyLayer(skewX: layer.skewX.withLoop(spec)));
      case LayerProp.pivot:
        _replace(layer.copyLayer(pivot: layer.pivot.withLoop(spec)));
      case LayerProp.parent:
        break;
    }
  }

  /// Assistente "inverter no tempo" (PR-X7) na propriedade dada.
  void reversePropertyInTime(String id, LayerProp prop) {
    final layer = _layer(id);
    if (layer == null) return;
    switch (prop) {
      case LayerProp.position:
        _replace(layer.copyLayer(position: layer.position.reversedInTime()));
      case LayerProp.scale:
        _replace(
          layer.copyLayer(
            scaleX: layer.scaleX.reversedInTime(),
            scaleY: layer.scaleY.reversedInTime(),
          ),
        );
      case LayerProp.rotation:
        _replace(layer.copyLayer(rotation: layer.rotation.reversedInTime()));
      case LayerProp.opacity:
        _replace(layer.copyLayer(opacity: layer.opacity.reversedInTime()));
      case LayerProp.skew:
        _replace(layer.copyLayer(skewX: layer.skewX.reversedInTime()));
      case LayerProp.pivot:
        _replace(layer.copyLayer(pivot: layer.pivot.reversedInTime()));
      case LayerProp.parent:
        break;
    }
  }

  /// Camada de ajuste: efeitos aplicados ao composto de tudo abaixo.
  void addAdjustmentLayer(Duration at) {
    final n = state.layers.whereType<AdjustmentLayer>().length + 1;
    _push(
      AdjustmentLayer(
        name: 'Ajuste $n',
        startTime: at,
        duration: const Duration(seconds: 5),
        position: AnimatedOffset(_center),
      ),
    );
  }

  void addNullLayer(Duration at) {
    final n = state.layers.whereType<NullLayer>().length + 1;
    _push(
      NullLayer(
        name: 'Nulo $n',
        startTime: at,
        duration: const Duration(seconds: 5),
        is3D: true,
        position: AnimatedOffset(_center),
      ),
    );
  }

  void addParticlesLayer(Duration at) {
    final n = state.layers.whereType<ParticlesLayer>().length + 1;
    _push(
      ParticlesLayer(
        name: 'Particulas $n',
        startTime: at,
        duration: const Duration(seconds: 5),
        is3D: true,
        position: AnimatedOffset(_center),
      ),
    );
  }

  /// Edita parametros do sistema de particulas.
  void updateParticles(String id, ParticlesLayer Function(ParticlesLayer) fn) {
    final layer = _layer(id);
    if (layer is! ParticlesLayer) return;
    _replace(fn(layer));
  }

  /// Elemento 3D nativo (cubo, esfera, diamante...): solido girado de
  /// verdade no espaco, vinculavel a um nulo como qualquer camada.
  void addElement3DLayer(Duration at, Element3DKind kind) {
    final n = state.layers.whereType<Element3DLayer>().length + 1;
    _push(
      Element3DLayer(
        name: '${element3DLabel(kind)} $n',
        startTime: at,
        duration: const Duration(seconds: 5),
        kind: kind,
        is3D: true,
        position: AnimatedOffset(_center),
      ),
    );
  }

  void updateElement3D(String id, Element3DLayer Function(Element3DLayer) fn) {
    final layer = _layer(id);
    if (layer is! Element3DLayer) return;
    _replace(fn(layer));
  }

  /// CONTEINER CENA 3D: uma camada para o compositor, um renderizador
  /// por dentro.
  void addScene3DLayer(Duration at) {
    final n = state.layers.whereType<Scene3DLayer>().length + 1;
    _push(
      Scene3DLayer(
        name: 'Cena 3D $n',
        startTime: at,
        duration: const Duration(seconds: 5),
        // Nasce vazia, mas ja com luz de tres pontos, ambiente Estudio e
        // grade. O primeiro objeto nunca aparece cinza no escuro.
        scene: Scene3D(lights: Scene3D.tresPontos),
        position: AnimatedOffset(_center),
      ),
    );
  }

  void updateScene3D(String id, Scene3D Function(Scene3D) fn) {
    final layer = _layer(id);
    if (layer is! Scene3DLayer) return;
    _replace(layer.withScene(fn(layer.scene)));
  }

  void updateScene3DCamera(String id, Camera3D Function(Camera3D) fn) {
    final layer = _layer(id);
    if (layer is! Scene3DLayer) return;
    _replace(layer.withCamera(fn(layer.camera)));
  }

  void updateSceneCameraById(String id, Camera3D camera) {
    final layer = _layer(id);
    if (layer is! Scene3DLayer) return;
    if (layer.camera.id != camera.id &&
        !layer.extraCameras.any((c) => c.id == camera.id)) {
      return;
    }
    _replace(
      layer.copyScene(
        camera: layer.camera.id == camera.id ? camera : layer.camera,
        extraCameras: [
          for (final c in layer.extraCameras) c.id == camera.id ? camera : c,
        ],
      ),
    );
  }

  /// Acrescenta uma camera com o enquadramento ATUAL. Nascer olhando
  /// para outro lugar obrigaria a reenquadrar do zero toda vez.
  String addScene3DCamera(String id) {
    final layer = _layer(id);
    if (layer is! Scene3DLayer) return '';
    final base = layer.camera;
    final nova = Camera3D(
      name: 'Camera ${layer.allCameras.length + 1}',
      kind: base.kind,
      posX: AnimatedDouble(base.posX.base),
      posY: AnimatedDouble(base.posY.base),
      posZ: AnimatedDouble(base.posZ.base),
      poiX: AnimatedDouble(base.poiX.base),
      poiY: AnimatedDouble(base.poiY.base),
      poiZ: AnimatedDouble(base.poiZ.base),
      focalLength: AnimatedDouble(base.focalLength.base),
      filmWidth: base.filmWidth,
      orthographic: base.orthographic,
    );
    _replace(layer.copyScene(extraCameras: [...layer.extraCameras, nova]));
    return nova.id;
  }

  void removeScene3DCamera(String id, String cameraId) {
    final layer = _layer(id);
    if (layer is! Scene3DLayer) return;
    _replace(
      layer.copyScene(
        extraCameras: [
          for (final c in layer.extraCameras)
            if (c.id != cameraId) c,
        ],
        // Tomada que aponta para camera apagada viraria cena sem camera.
        shots: [
          for (final t in layer.shots)
            if (t.cameraId != cameraId) t,
        ],
      ),
    );
  }

  /// CORTA para [cameraId] em [local]. Marcar de novo no mesmo instante
  /// troca a camera daquela tomada, em vez de empilhar duas.
  void setCameraShot(
    String id,
    Duration local,
    String cameraId, {
    Duration transition = Duration.zero,
  }) {
    final layer = _layer(id);
    if (layer is! Scene3DLayer) return;
    const tol = Duration(milliseconds: 60);
    final resto = [
      for (final t in layer.shots)
        if ((t.time - local).inMicroseconds.abs() > tol.inMicroseconds) t,
    ];
    _replace(
      layer.copyScene(
        shots: [
          ...resto,
          CameraShot(time: local, cameraId: cameraId, transition: transition),
        ],
      ),
    );
  }

  void removeCameraShot(String id, Duration local) {
    final layer = _layer(id);
    if (layer is! Scene3DLayer) return;
    const tol = Duration(milliseconds: 60);
    _replace(
      layer.copyScene(
        shots: [
          for (final t in layer.shots)
            if ((t.time - local).inMicroseconds.abs() > tol.inMicroseconds) t,
        ],
      ),
    );
  }

  void clearCameraShots(String id) {
    final layer = _layer(id);
    if (layer is! Scene3DLayer) return;
    _replace(layer.copyScene(shots: const []));
  }

  /// EXTRUDAR uma forma do projeto para dentro da cena 3D.
  ///
  /// Pega o contorno que a camada de forma ja desenha, vira volume, e
  /// entra como um no da cena. E o caminho de logo plano para logo
  /// girando — sem obrigar ninguem a modelar nada.
  String? extrudeShapeIntoScene(
    String sceneId,
    String shapeLayerId, {
    double depth = 40,
  }) {
    final cena = _layer(sceneId);
    final forma = _layer(shapeLayerId);
    if (cena is! Scene3DLayer || forma is! ShapeLayer) return null;

    final desenhos = evaluateShape(forma.contents, Duration.zero);
    if (desenhos.isEmpty) return null;
    // O maior desenho e a forma; os outros sao contorno e enfeite.
    var maior = desenhos.first.path;
    var maiorArea = 0.0;
    for (final d in desenhos) {
      final b = d.path.getBounds();
      final a = b.width * b.height;
      if (a > maiorArea) {
        maiorArea = a;
        maior = d.path;
      }
    }
    final contorno = dedupeOutline(outlineOfPath(maior));
    if (contorno.length < 3) return null;

    final malha = extrudeOutline(contorno, depth: depth);
    if (malha.verts.isEmpty) return null;

    final no = SceneNode(
      name: forma.name,
      mesh: malha,
      outline: contorno,
      extrudeDepth: depth,
      size: 120,
    );
    _replace(
      cena.withScene(cena.scene.copyWith(nodes: [...cena.scene.nodes, no])),
    );
    return no.id;
  }

  /// NULO 3D: um no que so transforma, nao desenha. E o pivo dos rigs.
  String addSceneNull(String sceneId) {
    final cena = _layer(sceneId);
    if (cena is! Scene3DLayer) return '';
    final no = SceneNode(
      name: 'Nulo ${cena.scene.nodes.where((n) => n.isNull).length + 1}',
      isNull: true,
    );
    _replace(
      cena.withScene(cena.scene.copyWith(nodes: [...cena.scene.nodes, no])),
    );
    return no.id;
  }

  /// Parenteia um no da cena a outro. Passar null solta.
  ///
  /// Recusa o ciclo: A pai de B e B pai de A travaria o quadro.
  void setSceneNodeParent(String sceneId, String nodeId, String? parentId) {
    final cena = _layer(sceneId);
    if (cena is! Scene3DLayer) return;
    if (cena.scene.nodeById(nodeId)?.locked ?? false) return;
    if (parentId == nodeId) return;
    if (parentId != null && _criaCiclo(cena.scene, nodeId, parentId)) {
      return;
    }
    _replace(
      cena.withScene(
        cena.scene.copyWith(
          nodes: [
            for (final n in cena.scene.nodes)
              if (n.id == nodeId)
                n.copyWith(parentId: parentId, clearParent: parentId == null)
              else
                n,
          ],
        ),
      ),
    );
  }

  bool _criaCiclo(Scene3D cena, String nodeId, String parentId) {
    var atual = cena.nodeById(parentId);
    var passos = 0;
    while (atual != null && passos++ < 32) {
      if (atual.id == nodeId) return true;
      final p = atual.parentId;
      atual = p == null ? null : cena.nodeById(p);
    }
    return false;
  }

  /// De qual NO INTERNO a camera da cena e filha. Null solta.
  void setSceneCameraParent(String sceneId, String? nodeId) {
    final cena = _layer(sceneId);
    if (cena is! Scene3DLayer) return;
    _replace(
      cena.withScene(
        cena.scene.copyWith(
          cameraParentId: nodeId,
          clearCameraParent: nodeId == null,
        ),
      ),
    );
  }

  /// De qual NULO DA COMPOSICAO a camera da cena e filha. Null solta.
  void setSceneCameraCompParent(String sceneId, String? layerId) {
    final cena = _layer(sceneId);
    if (cena is! Scene3DLayer) return;
    _replace(
      cena.copyScene(
        cameraParentLayerId: layerId,
        clearCameraParent: layerId == null,
      ),
    );
  }

  /// RIG DE ORBITA EM UM TOQUE.
  ///
  /// Cria o nulo, parenteia a camera nele e anima uma volta inteira —
  /// com keyframes de verdade, editaveis. Rig que nao vira keyframe e
  /// caixa-preta: quando a pessoa quer mudar o ritmo, nao tem onde
  /// mexer.
  void addOrbitRig(String sceneId, {Duration? duration}) {
    final cena = _layer(sceneId);
    if (cena is! Scene3DLayer) return;
    final dur = duration ?? cena.duration;
    final no = SceneNode(
      name: 'Orbita',
      isNull: true,
      rotY: AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(dur, 360),
    );
    _replace(
      cena.withScene(
        cena.scene.copyWith(
          nodes: [...cena.scene.nodes, no],
          cameraParentId: no.id,
        ),
      ),
    );
  }

  /// TRAZ UM MODELO .glb para dentro da cena.
  ///
  /// Devolve o id do no criado. Erro de leitura sobe como [GlbException]
  /// para a interface poder dizer o que houve — engolir e mostrar cena
  /// vazia seria pior.
  String addGlbNode(
    String sceneId,
    GlbResult modelo, {
    String sourcePath = '',
  }) {
    final cena = _layer(sceneId);
    if (cena is! Scene3DLayer) return '';
    final no = SceneNode(
      name: modelo.name,
      mesh: modelo.mesh,
      mediumMesh: modelo.mediumMesh,
      lowMesh: modelo.lowMesh,
      size: 120,
      credit: ModelCredit3D(
        author: modelo.author,
        license: modelo.license,
        url: modelo.sourceUrl,
      ),
      modelSource: ModelSource3D(
        path: sourcePath,
        triangles: modelo.triangles,
        bytes: modelo.report.bytes,
        meshes: modelo.report.meshes,
        materials: modelo.report.materials,
        textures: modelo.report.textures,
        animations: modelo.report.animations,
        nodeNames: modelo.nodeNames,
        animationNames: modelo.animationNames,
        overBudget: modelo.report.overBudget,
        lodCount: modelo.report.lodCount,
        warning: modelo.warning,
      ),
    );
    _replace(
      cena.withScene(cena.scene.copyWith(nodes: [...cena.scene.nodes, no])),
    );
    return no.id;
  }

  String addModel3D(String sceneId, ModelAsset3D model) {
    final layer = _layer(sceneId);
    if (layer is! Scene3DLayer) return '';
    final node = SceneNode(
      name: model.name,
      size: 120,
      modelAsset: model,
      modelSource: ModelSource3D(
        path: '',
        triangles: model.triangleCount,
        meshes: model.primitives.length,
        animations: model.clips.length,
        materials: (model.data['materials'] as List).length,
        nodeNames: [for (final n in model.nodes) n['name'] as String],
        animationNames: model.clipNames,
        lodCount: 1,
        warning: model.warnings.isEmpty ? null : model.warnings.join(' '),
      ),
    );
    _replace(
      layer.withScene(
        layer.scene.copyWith(nodes: [...layer.scene.nodes, node]),
      ),
    );
    return node.id;
  }

  /// Muda a ESPESSURA de um no extrudado, reaproveitando o contorno —
  /// pedir a forma de novo perderia qualquer ajuste ja feito no no.
  void setExtrudeDepth(String sceneId, String nodeId, double depth) {
    final cena = _layer(sceneId);
    if (cena is! Scene3DLayer) return;
    if (cena.scene.nodeById(nodeId)?.locked ?? false) return;
    _replace(
      cena.withScene(
        cena.scene.copyWith(
          nodes: [
            for (final n in cena.scene.nodes)
              if (n.id == nodeId && n.outline != null)
                n.copyWith(
                  mesh: extrudeOutline(n.outline!, depth: depth),
                  extrudeDepth: depth,
                )
              else
                n,
          ],
        ),
      ),
    );
  }

  void setScene3DHelpers(String id, bool show) {
    final layer = _layer(id);
    if (layer is! Scene3DLayer) return;
    _replace(layer.copyScene(showHelpers: show));
  }

  void setScene3DView(String id, SceneView view) {
    final layer = _layer(id);
    if (layer is! Scene3DLayer) return;
    _replace(layer.copyScene(view: view));
  }

  /// Adiciona um objeto ao grafo da cena.
  void addSceneNode(String layerId, Element3DKind kind) {
    updateScene3D(layerId, (s) {
      final n = s.nodes.length + 1;
      return s.copyWith(
        nodes: [
          ...s.nodes,
          SceneNode(
            name: '${element3DLabel(kind)} $n',
            kind: kind,
            x: AnimatedDouble((n.isEven ? 1 : -1) * 60.0 * (n ~/ 2 + 1)),
          ),
        ],
      );
    });
  }

  void updateSceneNode(
    String layerId,
    String nodeId,
    SceneNode Function(SceneNode) fn,
  ) {
    updateScene3D(layerId, (s) {
      final target = s.nodeById(nodeId);
      if (target == null || target.locked) return s;
      return s.copyWith(
        nodes: [for (final n in s.nodes) n.id == nodeId ? fn(n) : n],
      );
    });
  }

  /// O cadeado precisa conseguir destravar a si proprio; todas as outras
  /// mutacoes passam por [updateSceneNode] e respeitam o bloqueio.
  void setSceneNodeLocked(String layerId, String nodeId, bool locked) {
    updateScene3D(
      layerId,
      (s) => s.copyWith(
        nodes: [
          for (final n in s.nodes)
            if (n.id == nodeId) n.copyWith(locked: locked) else n,
        ],
      ),
    );
  }

  void removeSceneNode(String layerId, String nodeId) {
    updateScene3D(layerId, (s) {
      if (s.nodeById(nodeId)?.locked ?? false) return s;
      return s.copyWith(
        nodes: [
          for (final n in s.nodes)
            if (n.id != nodeId)
              n.parentId == nodeId ? n.copyWith(clearParent: true) : n,
        ],
        clearCameraParent: s.cameraParentId == nodeId,
      );
    });
  }

  /// RIG DE CAMERA em um toque — gera keyframes REAIS, editaveis.
  void applyRigToScene(String layerId, CameraRig rig) {
    final layer = _layer(layerId);
    if (layer is! Scene3DLayer) return;
    final bounds = sceneBounds(layer.scene, Duration.zero);
    _replace(
      layer.withCamera(
        applyCameraRig(
          layer.camera,
          rig,
          duration: layer.duration,
          target: bounds.center,
          radius: bounds.radius <= 0 ? 600 : bounds.radius * 2.2,
        ),
      ),
    );
  }

  /// Enquadrar tudo / alinhar camera a vista.
  void frameSceneAll(String layerId) {
    final layer = _layer(layerId);
    if (layer is! Scene3DLayer) return;
    _replace(
      layer.withCamera(
        frameBounds(
          layer.camera,
          sceneBounds(layer.scene, Duration.zero),
          Duration.zero,
        ),
      ),
    );
  }

  void addSceneLight(String layerId, Light3DKind kind) {
    updateScene3D(
      layerId,
      (s) => s.copyWith(
        lights: [
          ...s.lights,
          Light3D(kind: kind, castsShadow: s.lights.isEmpty),
        ],
      ),
    );
  }

  void updateSceneLight(
    String layerId,
    String lightId,
    Light3D Function(Light3D) fn,
  ) {
    updateScene3D(
      layerId,
      (s) => s.copyWith(
        lights: [for (final l in s.lights) l.id == lightId ? fn(l) : l],
      ),
    );
  }

  void removeSceneLight(String layerId, String lightId) {
    updateScene3D(
      layerId,
      (s) => s.copyWith(
        lights: [
          for (final l in s.lights)
            if (l.id != lightId) l,
        ],
      ),
    );
  }

  /// DUPLICAR EM ARRAY — o modulo Grade direto em 3D: 200 objetos numa
  /// grade e um slider, e tudo entra em UMA chamada de desenho porque
  /// vira instancia da mesma malha.
  void arrayNodeInstances(
    String layerId,
    String nodeId, {
    required int countX,
    required int countY,
    required int countZ,
    required double spacing,
  }) {
    final cx = countX.clamp(1, 40);
    final cy = countY.clamp(1, 40);
    final cz = countZ.clamp(1, 40);
    updateSceneNode(layerId, nodeId, (n) {
      if (cx * cy * cz <= 1) return n.copyWith(instances: const []);
      final out = <Vec3>[];
      for (var ix = 0; ix < cx; ix++) {
        for (var iy = 0; iy < cy; iy++) {
          for (var iz = 0; iz < cz; iz++) {
            out.add(
              Vec3(
                (ix - (cx - 1) / 2) * spacing,
                (iy - (cy - 1) / 2) * spacing,
                (iz - (cz - 1) / 2) * spacing,
              ),
            );
          }
        }
      }
      return n.copyWith(instances: out);
    });
  }

  /// MODO DE ISOLAMENTO: esconde tudo menos o selecionado. Chamar de
  /// novo com o mesmo no mostra todos outra vez.
  void isolateSceneNode(String layerId, String nodeId) {
    updateScene3D(layerId, (s) {
      final previous = _visibilityBeforeIsolation[layerId];
      if (_isolatedSceneNode[layerId] == nodeId && previous != null) {
        _visibilityBeforeIsolation.remove(layerId);
        _isolatedSceneNode.remove(layerId);
        return s.copyWith(
          nodes: [
            for (final n in s.nodes)
              n.copyWith(visible: previous[n.id] ?? n.visible),
          ],
        );
      }
      _visibilityBeforeIsolation.putIfAbsent(
        layerId,
        () => {for (final n in s.nodes) n.id: n.visible},
      );
      _isolatedSceneNode[layerId] = nodeId;
      return s.copyWith(
        nodes: [for (final n in s.nodes) n.copyWith(visible: n.id == nodeId)],
      );
    });
  }

  /// FOCAR NO SELECIONADO: a distancia de foco vem do objeto, nao de um
  /// numero chutado.
  void focusCameraOnNode(String layerId, String nodeId) {
    final layer = _layer(layerId);
    if (layer is! Scene3DLayer) return;
    final node = layer.scene.nodes.where((n) => n.id == nodeId).firstOrNull;
    if (node == null) return;
    final worldPosition = resolveNodeTransform(
      layer.scene,
      node,
      Duration.zero,
    ).position;
    final d = (worldPosition - layer.camera.positionAt(Duration.zero)).length;
    _replace(
      layer.withCamera(
        layer.camera.copyWith(
          dof: layer.camera.dof.copyWith(
            enabled: true,
            focusDistance: layer.camera.dof.focusDistance.withBase(d),
          ),
        ),
      ),
    );
  }

  /// ENQUADRAR SELECIONADO.
  void frameSceneNode(String layerId, String nodeId) {
    final layer = _layer(layerId);
    if (layer is! Scene3DLayer) return;
    final node = layer.scene.nodes.where((n) => n.id == nodeId).firstOrNull;
    if (node == null) return;
    final transform = resolveNodeTransform(layer.scene, node, Duration.zero);
    final r = node.size * transform.scale.abs() * 1.8;
    _replace(
      layer.withCamera(
        frameBounds(
          layer.camera,
          Bounds3D(transform.position, r),
          Duration.zero,
        ),
      ),
    );
  }

  /// SALVAR VISTA: guarda o enquadramento atual com nome.
  void saveSceneView(String layerId, String name, RenderCamera cam) {
    updateScene3D(
      layerId,
      (s) => s.copyWith(
        savedViews: [
          ...s.savedViews,
          SavedView(name: name, position: cam.position, target: cam.target),
        ],
      ),
    );
  }

  void removeSceneView(String layerId, int index) {
    updateScene3D(layerId, (s) {
      if (index < 0 || index >= s.savedViews.length) return s;
      return s.copyWith(
        savedViews: [
          for (var i = 0; i < s.savedViews.length; i++)
            if (i != index) s.savedViews[i],
        ],
      );
    });
  }

  /// Coloca a camera exatamente num enquadramento salvo.
  void applySavedView(String layerId, SavedView view) {
    final layer = _layer(layerId);
    if (layer is! Scene3DLayer) return;
    _replace(
      layer.withCamera(
        alignToView(
          layer.camera,
          RenderCamera(position: view.position, target: view.target),
        ),
      ),
    );
  }

  /// ALINHAR CAMERA A VISTA a partir de uma camera de render arbitraria
  /// (a vista livre navegada no estudio). E o comando mais usado: navega
  /// livre ate achar o plano, e so entao a camera assume ele.
  void alignCameraToRender(String layerId, RenderCamera cam) {
    final layer = _layer(layerId);
    if (layer is! Scene3DLayer) return;
    _replace(
      layer
          .withCamera(alignToView(layer.camera, cam))
          .copyScene(view: SceneView.camera),
    );
  }

  void alignCameraToCurrentView(String layerId) {
    final layer = _layer(layerId);
    if (layer is! Scene3DLayer || layer.view == SceneView.camera) return;
    _replace(
      layer
          .withCamera(alignToView(layer.camera, orthoViewCamera(layer.view)))
          .copyScene(view: SceneView.camera),
    );
  }

  Future<void> importImageFromGallery(Duration at) async {
    final file = await ref
        .read(mediaImportServiceProvider)
        .pickImageFromGallery();
    if (file == null) return;
    addImageLayer(at, file.path, file.name);
  }

  void removeLayer(String id) {
    _mutate(
      state.copyWith(
        layers: [
          for (final l in state.layers)
            if (l.id != id)
              if (l is VideoLayer && l.transitionIn?.outgoingLayerId == id)
                l.copyLayer(clearTransitionIn: true)
              else
                l,
        ],
      ),
    );
    if (ref.read(selectedLayerProvider) == id) {
      ref.read(selectedLayerProvider.notifier).state = null;
    }
  }

  /// Exclui VARIAS camadas numa unica mutacao (um "Desfazer" restaura
  /// tudo — efeitos, keyframes e vinculos intactos).
  void removeLayers(Iterable<String> ids) {
    final set = ids.toSet();
    if (set.isEmpty) return;
    _mutate(
      state.copyWith(
        layers: [
          for (final l in state.layers)
            if (!set.contains(l.id))
              if (l is VideoLayer &&
                  set.contains(l.transitionIn?.outgoingLayerId))
                l.copyLayer(clearTransitionIn: true)
              else
                l,
        ],
      ),
    );
    if (set.contains(ref.read(selectedLayerProvider))) {
      ref.read(selectedLayerProvider.notifier).state = null;
    }
    ref.read(multiSelectProvider.notifier).state = const {};
  }

  // ------------------------------------------------------- audio

  AudioSpec? audioSpecOf(String id) => switch (_layer(id)) {
    AudioLayer a => a.audio,
    VideoLayer v => v.audio,
    _ => null,
  };

  void updateAudioSpec(String id, AudioSpec Function(AudioSpec) fn) {
    final layer = _layer(id);
    if (layer is AudioLayer) {
      _replace(layer.copyLayer(audio: fn(layer.audio)));
    } else if (layer is VideoLayer) {
      _replace(layer.copyLayer(audio: fn(layer.audio)));
    }
  }

  /// NORMALIZAR POR SONORIDADE.
  ///
  /// Pico normalizado ainda soa desigual: uma locucao seca e um trecho de
  /// bateria podem ter o mesmo pico e 10 dB de diferenca na percepcao. O
  /// alvo e -14 LUFS, que e o que as plataformas de video usam — entregar
  /// mais alto so faz elas abaixarem depois.
  ///
  /// Cai para o pico quando a sonoridade ainda nao foi medida (faixa
  /// recem-importada) — melhor um ganho aproximado que nenhum.
  ///
  /// Devolve o ganho aplicado, ou null se a forma de onda ainda nao esta
  /// pronta — quem chama avisa em vez de fingir que fez.
  double? normalizeAudio(String id, {double alvoLufs = lufsAlvoPadrao}) {
    final path = _audioPathOf(id);
    if (path == null) return null;
    final lufs = MediaPreviewService.instance.loudnessOf(path);
    if (lufs != null && lufs.isFinite) {
      final g = normalizeGainForLufs(lufs, target: alvoLufs);
      updateAudioSpec(
        id,
        (a) => a.copyWith(gain: g, normalizeTargetLufs: alvoLufs),
      );
      return g;
    }
    final peaks = MediaPreviewService.instance.peaksOf(path);
    if (peaks == null || peaks.isEmpty) return null;
    final g = normalizeGain(peaks);
    updateAudioSpec(id, (a) => a.copyWith(gain: g));
    return g;
  }

  /// ONDE ESTAO AS PAUSAS desta camada, ja em tempo da LINHA e aparadas
  /// no pedaco que a camada usa.
  ///
  /// Devolver a lista (em vez de ja cortar) e o que deixa a decupagem
  /// MOSTRAR o corte antes de fazer: a pessoa mexe no limiar e ve as
  /// faixas vermelhas aparecerem e sumirem.
  List<(Duration, Duration)>? silenceRangesOf(
    String id, {
    double threshold = 0.035,
    Duration minSilence = const Duration(milliseconds: 350),
    Duration padding = const Duration(milliseconds: 120),
  }) {
    final layer = _layer(id);
    if (layer == null) return null;
    final path = _audioPathOf(id);
    if (path == null) return null;
    final peaks = MediaPreviewService.instance.peaksOf(path);
    if (peaks == null || peaks.isEmpty) return null;

    final offset = _sourceOffsetOf(layer);
    final pausas = detectSilence(
      peaks,
      threshold: threshold,
      minSilence: minSilence,
      padding: padding,
    );

    // Tempo do ARQUIVO -> tempo da LINHA, aparado na camada.
    final out = <(Duration, Duration)>[];
    for (final p in pausas) {
      var de = layer.startTime + (p.$1 - offset);
      var ate = layer.startTime + (p.$2 - offset);
      if (de < layer.startTime) de = layer.startTime;
      if (ate > layer.endTime) ate = layer.endTime;
      if (ate > de) out.add((de, ate));
    }
    return out;
  }

  /// Tira os trechos marcados de UMA camada. Devolve quantos pedacos
  /// sobraram (0 se a camada inteira saiu).
  int cutRangesOf(
    String id,
    List<(Duration, Duration)> ranges, {
    bool ripple = true,
  }) {
    if (ranges.isEmpty) return 1;
    final antes = state.layers.length;
    final novas = removeRangesFrom(state.layers, id, ranges, ripple: ripple);
    final pedacos = novas.length - antes + 1;
    _mutate(state.copyWith(layers: novas));
    if (!novas.any((l) => l.id == id)) {
      ref.read(selectedLayerProvider.notifier).state = null;
    }
    return pedacos < 0 ? 0 : pedacos;
  }

  /// REMOVER SILENCIO: joga fora as pausas e encosta o que sobrou.
  ///
  /// Devolve quantos pedacos ficaram, ou null se ainda nao ha forma de
  /// onda.
  int? removeSilence(
    String id, {
    double threshold = 0.035,
    Duration minSilence = const Duration(milliseconds: 350),
  }) {
    final pausas = silenceRangesOf(
      id,
      threshold: threshold,
      minSilence: minSilence,
    );
    if (pausas == null) return null;
    if (pausas.isEmpty) return 1;
    final antes = state.layers.length;
    final novas = removeRangesFrom(state.layers, id, pausas, ripple: true);
    _mutate(state.copyWith(layers: novas));
    final pedacos = novas.length - antes + 1;
    final primeiro = novas.where((l) => l.id == id).firstOrNull;
    if (primeiro != null) {
      ref.read(selectedLayerProvider.notifier).state = primeiro.id;
    }
    return pedacos < 1 ? 1 : pedacos;
  }

  /// ANALISA AS BATIDAS da trilha e poe a GRADE no projeto.
  ///
  /// O que fica guardado nao sao os ataques detectados: e a grade regular
  /// que sai do andamento. Ataque treme alguns milissegundos, e corte
  /// encaixado em ataque herda o tremor — soa fora do tempo mesmo estando
  /// "no lugar certo". A grade e regular por construcao.
  ///
  /// Devolve quantas marcas entraram, ou null se nao deu para ler o
  /// arquivo ou se nao havia ritmo discernivel.
  Future<int?> detectBeatsInto(
    String id, {
    BeatBand band = BeatBand.grave,
    double sensitivity = 50,
    int denominador = 4,
  }) async {
    final path = _audioPathOf(id);
    if (path == null) return null;
    final env = await MediaPreviewService.instance.bandEnvelopeOf(path, band);
    if (env.isEmpty) return null;

    // 0..100 na tela vira o multiplicador do detector, INVERTIDO: mais
    // sensibilidade e limiar mais baixo.
    final limiar = 1.05 + (100 - sensitivity.clamp(0, 100)) / 100 * 1.1;
    final ataques = detectBeats(env, sensitivity: limiar);
    if (ataques.length < 3) return null;

    final bpm = estimateBpm(ataques);
    if (bpm == null) return null;

    final camada = _layer(id);
    final ate = camada?.endTime ?? state.duration;
    final grade = beatGrid(
      first: camada == null ? ataques.first : camada.startTime + ataques.first,
      bpm: bpm,
      denominador: denominador,
      until: ate,
    );
    if (grade.isEmpty) return null;
    _mutate(state.copyWith(beats: grade, bpm: bpm));
    return grade.length;
  }

  /// Corrige o andamento a mao e refaz a grade a partir da primeira
  /// batida — quem edita musica muitas vezes SABE o BPM, e digitar e
  /// mais rapido e mais certo que reanalisar.
  void setBpm(double bpm, {int denominador = 4}) {
    if (bpm <= 0) return;
    final primeira = state.beats.isEmpty ? Duration.zero : state.beats.first;
    final grade = beatGrid(
      first: primeira,
      bpm: bpm,
      denominador: denominador,
      until: state.duration,
    );
    _mutate(state.copyWith(beats: grade, bpm: bpm));
  }

  void clearBeats() {
    if (state.beats.isEmpty) return;
    _mutate(state.copyWith(beats: const []));
  }

  /// CORTA TODAS AS CAMADAS em cada marcador. Devolve quantos cortes
  /// aconteceram.
  int cutAtMarkers({bool usarBatidas = false}) {
    final tempos = usarBatidas
        ? state.beats
        : [for (final m in state.markers) m.time];
    if (tempos.isEmpty) return 0;
    var cortes = 0;
    // De TRAS para a frente: cortar cedo desloca as bordas do que vem
    // depois, e a lista de tempos ficaria falando de outro clipe.
    for (final t in tempos.reversed) {
      for (final l in [...state.layers]) {
        if (!l.activeAt(t)) continue;
        final antes = state.layers.length;
        splitLayer(l.id, t);
        if (state.layers.length > antes) cortes++;
      }
    }
    return cortes;
  }

  /// DISTRIBUI as camadas selecionadas (ou todas) nos marcadores: a
  /// primeira comeca na primeira marca, a segunda na segunda, e assim
  /// por diante. E o atalho de "uma foto por batida".
  int distributeAtMarkers({List<String>? only, bool usarBatidas = false}) {
    final tempos = usarBatidas
        ? state.beats
        : [for (final m in state.markers) m.time];
    if (tempos.length < 2) return 0;
    final alvos = [
      for (final l in state.layers)
        if (only == null || only.contains(l.id)) l,
    ];
    if (alvos.isEmpty) return 0;

    final novas = <Layer>[];
    var i = 0;
    for (final l in state.layers) {
      final k = alvos.indexWhere((a) => a.id == l.id);
      if (k < 0) {
        novas.add(l);
        continue;
      }
      final idx = i.clamp(0, tempos.length - 1);
      final inicio = tempos[idx];
      final fim = idx + 1 < tempos.length ? tempos[idx + 1] : null;
      novas.add(
        l.copyLayer(
          startTime: inicio,
          duration: fim == null ? l.duration : fim - inicio,
        ),
      );
      i++;
    }
    _mutate(state.copyWith(layers: novas));
    return alvos.length;
  }

  /// Marca as BATIDAS da faixa como tempos, para encaixar corte no
  /// ritmo. Devolve null se a forma de onda ainda nao esta pronta.
  List<Duration>? beatsOf(String id) {
    final path = _audioPathOf(id);
    if (path == null) return null;
    final peaks = MediaPreviewService.instance.peaksOf(path);
    if (peaks == null || peaks.isEmpty) return null;
    return detectBeats(peaks);
  }

  /// Ponto de entrada na midia, para video e audio.
  static Duration _sourceOffsetOf(Layer l) => switch (l) {
    VideoLayer v => v.sourceOffset,
    AudioLayer a => a.sourceOffset,
    _ => Duration.zero,
  };

  String? _audioPathOf(String id) => switch (_layer(id)) {
    AudioLayer a => a.sourcePath,
    VideoLayer v => v.sourcePath,
    _ => null,
  };

  // -------------------------------------------------- rastrear regioes

  /// ANALISA as regioes de um clipe para o Blob Tracker.
  ///
  /// Roda uma vez e guarda as caixas por quadro. Rastreio depende do
  /// quadro anterior, e detectar no desenho quebraria o seek: pular para
  /// o segundo 40 exigiria processar os 1200 quadros anteriores.
  ///
  /// Devolve quantos quadros foram analisados, ou null se nao deu.
  Future<int?> analyzeBlobsFor(String layerId, String effectId) async {
    final layer = _layer(layerId);
    if (layer is! VideoLayer) return null;
    EffectInstance? fx;
    for (final e in layer.effects) {
      if (e.id == effectId) fx = e;
    }
    if (fx == null) return null;

    const t0 = Duration.zero;
    return BlobTrackService.instance.analyze(
      effectId: effectId,
      sourcePath: layer.sourcePath,
      start: layer.sourceOffset,
      duration: layer.sourceSpan,
      by: BlobDetectBy.values[fx.paramAt('detect_by', t0).round().clamp(0, 3)],
      threshold: fx.paramAt('threshold', t0),
      sensitivity: fx.paramAt('sensitivity', t0),
      minBlobSize: fx.paramAt('min_blob_size', t0),
      maxBlobSize: fx.paramAt('max_blob_size', t0),
      maxBlobs: fx.paramAt('max_blobs', t0).round(),
      mergeDistance: fx.paramAt('merge_distance', t0),
      persistence: fx.paramAt('persistence', t0).round(),
      smoothing: fx.paramAt('smoothing', t0) / 100,
    );
  }

  // ------------------------------------------------------- estabilizar

  /// ESTABILIZAR: tira o tremor da mao.
  ///
  /// Rastreia um pedaco do centro do quadro, acha o caminho SUAVE que a
  /// camera "queria" fazer, e escreve a diferenca como keyframe de
  /// posicao — mais uma ampliacao, porque estabilizar sem ampliar mostra
  /// o vazio nas bordas, que e o defeito que denuncia estabilizacao
  /// caseira na hora.
  ///
  /// Devolve quantos quadros foram usados, ou null se nao deu para ler o
  /// video.
  Future<int?> stabilizeLayer(
    String id, {
    int janela = 15,
    double forca = 1.0,
  }) async {
    final layer = _layer(id);
    if (layer is! VideoLayer) return null;

    final frames = await TrackingService.instance.grayFrames(
      layer.sourcePath,
      start: layer.sourceOffset,
      duration: layer.sourceSpan,
    );
    if (frames.length < 4) return null;

    final centro = Offset(frames.first.width / 2, frames.first.height / 2);
    final track = trackSequence(frames, centro);
    final offs = stabilizeOffsets(track, janela: janela);
    if (offs.isEmpty) return 0;

    // O rastreio roda em resolucao baixa: o deslocamento volta para a
    // escala da composicao antes de virar keyframe.
    final escala = state.outputWidth / frames.first.width;
    final zoom = stabilizeZoom(offs, frames.first.width, frames.first.height);

    final base = layer.position.valueAt(Duration.zero);
    final passoUs = layer.duration.inMicroseconds / offs.length;

    var pos = AnimatedOffset(base);
    for (var i = 0; i < offs.length; i++) {
      final t = Duration(microseconds: (i * passoUs).round());
      final d = offs[i] * escala * forca.clamp(0.0, 1.0);
      pos = pos.withKeyframe(t, base + d);
    }

    final zx = layer.scaleX.valueAt(Duration.zero) * zoom;
    final zy = layer.scaleY.valueAt(Duration.zero) * zoom;
    _replace(
      layer.copyLayer(
        position: pos,
        scaleX: AnimatedDouble(zx),
        scaleY: AnimatedDouble(zy),
      ),
    );
    return offs.length;
  }

  /// REENQUADRAR SOZINHO: o clipe horizontal vira vertical seguindo o
  /// que se move.
  ///
  /// Cortar 16:9 para 9:16 no centro corta a cabeca de quem esta na
  /// lateral. Aqui o mesmo rastreador acha o que se mexe, o caminho e
  /// suavizado (senao o enquadramento treme junto com o assunto) e vira
  /// keyframe de posicao — com a escala que preenche o novo quadro.
  ///
  /// Devolve quantos quadros foram usados, ou null se nao deu para ler.
  Future<int?> autoReframeLayer(String id) async {
    final layer = _layer(id);
    if (layer is! VideoLayer) return null;

    final frames = await TrackingService.instance.grayFrames(
      layer.sourcePath,
      start: layer.sourceOffset,
      duration: layer.sourceSpan,
      fps: 6,
    );
    if (frames.length < 4) return null;

    final centro = Offset(frames.first.width / 2, frames.first.height / 2);
    final track = trackSequence(frames, centro, patch: 20, busca: 40);

    // Suaviza MUITO mais que a estabilizacao: aqui nao se quer copiar o
    // movimento do assunto, e sim acompanhar de longe. Enquadramento que
    // treme junto com o assunto e pior que enquadramento parado.
    final caminho = smoothPath([for (final p in track) p.position], janela: 31);

    // Escala que preenche o quadro do projeto com o video da fonte.
    final larguraFonte = frames.first.width.toDouble();
    final alturaFonte = frames.first.height.toDouble();
    final proporcaoFonte = larguraFonte / alturaFonte;
    final proporcaoAlvo = state.outputWidth / state.outputHeight;
    final preenche = proporcaoFonte > proporcaoAlvo
        ? state.outputHeight / alturaFonte
        : state.outputWidth / larguraFonte;

    final centroComp = Offset(state.outputWidth / 2, state.outputHeight / 2);
    final passoUs = layer.duration.inMicroseconds / caminho.length;

    var pos = AnimatedOffset(centroComp);
    for (var i = 0; i < caminho.length; i++) {
      final t = Duration(microseconds: (i * passoUs).round());
      // O assunto no centro: a camada anda o contrario de onde ele esta.
      final desvio = (caminho[i] - centro) * preenche;
      pos = pos.withKeyframe(t, centroComp - desvio);
    }

    final escala = preenche * larguraFonte / state.outputWidth;
    _replace(
      layer.copyLayer(
        position: pos,
        scaleX: AnimatedDouble(escala),
        scaleY: AnimatedDouble(escala),
      ),
    );
    return caminho.length;
  }

  /// Desfaz a estabilizacao: posicao fixa de volta no valor do comeco.
  void clearStabilization(String id) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        position: AnimatedOffset(layer.position.valueAt(Duration.zero)),
      ),
    );
  }

  // ------------------------------------------------------ pulso na batida

  /// PULSAR NA BATIDA: escala a camada em cada ataque da musica.
  ///
  /// Feito na mao, isso e um keyframe a cada meio segundo por tres
  /// minutos — ninguem faz, e o video fica parado. A conta e a mesma que
  /// o detector de batida ja faz; o que faltava era virar keyframe.
  ///
  /// Cada batida vira TRES keyframes: o repouso logo antes, o pico, e a
  /// volta. Sem o repouso antes, a escala viria subindo desde a batida
  /// anterior e o pulso viraria uma onda mole.
  ///
  /// Devolve quantas batidas entraram, ou null se a forma de onda ainda
  /// nao esta pronta.
  int? applyBeatPulse(
    String targetId,
    String audioId, {
    double amount = 0.12,
    Duration attack = const Duration(milliseconds: 60),
    Duration release = const Duration(milliseconds: 180),
  }) {
    final alvo = _layer(targetId);
    if (alvo == null) return null;
    final batidas = beatsOf(audioId);
    if (batidas == null) return null;

    final fonte = _layer(audioId);
    if (fonte == null) return null;

    final base = alvo.scaleX.valueAt(Duration.zero);
    final pico = base * (1 + amount.clamp(0.0, 2.0));

    var sx = alvo.scaleX;
    var sy = alvo.scaleY;
    var n = 0;
    for (final b in batidas) {
      // As batidas vem em tempo do ARQUIVO de audio; a camada alvo tem
      // o relogio dela.
      final naLinha = fonte.startTime + b;
      if (naLinha < alvo.startTime || naLinha > alvo.endTime) continue;
      final t = alvo.localTime(naLinha);

      final antes = t - attack;
      final depois = t + release;
      if (antes >= Duration.zero) {
        sx = sx.withKeyframe(antes, base, Easing.easeOut);
        sy = sy.withKeyframe(antes, base, Easing.easeOut);
      }
      sx = sx.withKeyframe(t, pico, Easing.easeOut);
      sy = sy.withKeyframe(t, pico, Easing.easeOut);
      if (depois <= alvo.duration) {
        sx = sx.withKeyframe(depois, base, Easing.easeInOut);
        sy = sy.withKeyframe(depois, base, Easing.easeInOut);
      }
      n++;
    }
    if (n == 0) return 0;
    _replace(alvo.copyLayer(scaleX: sx, scaleY: sy));
    return n;
  }

  /// Tira todos os keyframes de escala — o "desfazer" do pulso quando a
  /// pessoa ja mexeu em outras coisas depois.
  void clearScaleKeyframes(String id) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        scaleX: AnimatedDouble(layer.scaleX.valueAt(Duration.zero)),
        scaleY: AnimatedDouble(layer.scaleY.valueAt(Duration.zero)),
      ),
    );
  }

  // -------------------------------------------------------- velocidade

  /// Muda a VELOCIDADE do clipe, encolhendo ou esticando a barra para
  /// que a mesma fonte continue cabendo.
  ///
  /// Sem mexer na barra, acelerar cortaria o fim do clipe (a fonte
  /// acabaria antes) — e a pessoa veria um pedaco congelado.
  void setClipSpeed(String id, double speed) {
    final layer = _layer(id);
    if (layer == null) return;
    final v = speed.clamp(0.1, 10.0);

    final atual = switch (layer) {
      VideoLayer l =>
        videoSourceSpan(l).inMicroseconds / l.duration.inMicroseconds,
      AudioLayer l => l.speed,
      _ => 1.0,
    };
    if (atual <= 0) return;
    final novaDur = Duration(
      microseconds: (layer.duration.inMicroseconds * atual / v).round(),
    );
    if (novaDur.inMilliseconds < 50) return;

    final novo = switch (layer) {
      VideoLayer l => l.copyLayer(
        speed: v,
        duration: novaDur,
        effects: replaceTimeRemap(l, null),
      ),
      AudioLayer l => l.copyLayer(speed: v, duration: novaDur),
      _ => null,
    };
    if (novo == null) return;

    // O que vinha depois anda junto: acelerar um clipe no meio nao pode
    // deixar buraco nem sobreposicao.
    final delta = novaDur - layer.duration;
    _mutate(
      state.copyWith(
        layers: [
          for (final l in state.layers)
            if (l.id == id)
              novo
            else if (l.startTime >= layer.endTime)
              l.copyLayer(startTime: l.startTime + delta)
            else
              l,
        ],
      ),
    );
  }

  double clipSpeedOf(String id) => switch (_layer(id)) {
    VideoLayer l =>
      videoSourceSpan(l).inMicroseconds / l.duration.inMicroseconds,
    AudioLayer l => l.speed,
    _ => 1.0,
  };

  bool clipHasTimeRemap(String id) => switch (_layer(id)) {
    VideoLayer l => hasTimeRemap(l),
    _ => false,
  };

  AnimatedDouble? clipTimeRemapTrack(String id) => switch (_layer(id)) {
    VideoLayer l => timeRemapTrackOf(l),
    _ => null,
  };

  void setClipPreservePitch(String id, bool preserve) {
    final layer = _layer(id);
    switch (layer) {
      case VideoLayer l:
        _replace(l.copyLayer(audio: l.audio.copyWith(preservePitch: preserve)));
      case AudioLayer l:
        _replace(l.copyLayer(audio: l.audio.copyWith(preservePitch: preserve)));
      case _:
        return;
    }
  }

  void applySpeedRamp(String id, SpeedRampPreset preset) {
    final layer = _layer(id);
    if (layer is! VideoLayer) return;
    final span = videoSourceSpan(layer);
    final track = speedRampTrack(preset, layer.duration, span);
    _replace(
      layer.copyLayer(speed: 1, effects: replaceTimeRemap(layer, track)),
    );
  }

  /// Liga o modo avancado com identidade linear ou volta a velocidade
  /// constante equivalente, sem alterar o quadro nem a duracao.
  void setClipTimeRemapEnabled(String id, bool enabled) {
    final layer = _layer(id);
    if (layer is! VideoLayer) return;
    if (enabled) {
      if (hasTimeRemap(layer)) return;
      final span = videoSourceSpan(layer);
      final track = AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(layer.duration, span.inMicroseconds / 1000000.0);
      _replace(
        layer.copyLayer(speed: 1, effects: replaceTimeRemap(layer, track)),
      );
      return;
    }
    final average = clipSpeedOf(id).clamp(0.1, 10.0);
    _replace(
      layer.copyLayer(speed: average, effects: replaceTimeRemap(layer, null)),
    );
  }

  void setClipTimeRemapKeyframe(
    String id,
    Duration localTime,
    double sourceSeconds,
  ) {
    final layer = _layer(id);
    if (layer is! VideoLayer) return;
    var track = timeRemapTrackOf(layer);
    if (track == null) {
      setClipTimeRemapEnabled(id, true);
      final updated = _layer(id);
      if (updated is! VideoLayer) return;
      track = timeRemapTrackOf(updated);
      if (track == null) return;
      _replace(
        updated.copyLayer(
          effects: replaceTimeRemap(
            updated,
            track.edited(localTime, sourceSeconds),
          ),
        ),
      );
      return;
    }
    _replace(
      layer.copyLayer(
        effects: replaceTimeRemap(
          layer,
          track.edited(localTime, sourceSeconds),
        ),
      ),
    );
  }

  void setClipSpeedBlur(String id, bool enabled) {
    final layer = _layer(id);
    if (layer is VideoLayer) {
      _replace(layer.copyLayer(speedBlur: enabled));
    }
  }

  bool reverseNeedsProxy(String id) {
    final layer = _layer(id);
    return layer is VideoLayer &&
        layer.duration >= const Duration(seconds: 10) &&
        ProxyService.instance.proxyOf(layer.sourcePath) == null;
  }

  /// Devolve false quando um video longo ainda precisa do proxy curto.
  bool setClipReverse(
    String id,
    bool reverse, {
    bool allowWithoutProxy = false,
  }) {
    final layer = _layer(id);
    if (layer is! VideoLayer) return false;
    if (reverse && !allowWithoutProxy && reverseNeedsProxy(id)) return false;
    _replace(layer.copyLayer(reverse: reverse));
    return true;
  }

  // --------------------------------------------------------- transicoes

  VideoLayer? clipAfter(String outgoingId) =>
      videoAfter(state.layers, outgoingId);

  ClipTransition? transitionAfter(String outgoingId) =>
      clipAfter(outgoingId)?.transitionIn;

  TransitionHandleReport? transitionHandleReport(
    String outgoingId, {
    ClipTransitionType type = ClipTransitionType.dissolve,
    Duration duration = const Duration(milliseconds: 300),
    TransitionAlignment alignment = TransitionAlignment.center,
  }) => transitionHandles(
    state.layers,
    outgoingId,
    ClipTransition(
      outgoingLayerId: outgoingId,
      type: type,
      duration: duration,
      alignment: alignment,
    ),
  );

  Duration _endATrim(ClipTransition? transition) =>
      transition != null &&
          transition.enabled &&
          transition.alignment == TransitionAlignment.endA
      ? transition.duration
      : Duration.zero;

  ClipTransition _fitEndATrim(
    VideoLayer outgoing,
    ClipTransition requested,
    ClipTransition? previous,
  ) {
    if (requested.alignment != TransitionAlignment.endA) return requested;
    final restoredDuration = outgoing.duration + _endATrim(previous);
    final maximum = restoredDuration - const Duration(milliseconds: 50);
    if (maximum <= Duration.zero) {
      return requested.copyWith(duration: Duration.zero);
    }
    if (requested.duration <= maximum) {
      return requested;
    }
    return requested.copyWith(duration: maximum);
  }

  /// Grava a transicao e o ajuste magnetico numa unica mutacao. Em
  /// "Fim de A", a janela inteira ocupa o fim do primeiro clipe: A e
  /// encurtado e tudo a direita acompanha. Trocar alinhamento, duracao ou
  /// remover a transicao restaura exatamente o ajuste anterior antes de
  /// aplicar o novo, portanto a operacao nunca acumula trims ocultos.
  void _commitTransition(
    VideoLayer outgoing,
    VideoLayer incoming,
    ClipTransition? previous,
    ClipTransition? next,
  ) {
    final durationDelta = _endATrim(previous) - _endATrim(next);
    final junction = incoming.startTime;
    final previousWasEndA = previous?.alignment == TransitionAlignment.endA;
    final nextIsEndA = next?.alignment == TransitionAlignment.endA;
    final rippleIds =
        previousWasEndA && (previous?.rippleLayerIds.isNotEmpty ?? false)
        ? previous!.rippleLayerIds.toSet()
        : nextIsEndA
        ? {
            incoming.id,
            for (final layer in state.layers)
              if (layer.startTime >= junction) layer.id,
          }
        : <String>{};
    final storedNext = nextIsEndA
        ? next!.copyWith(rippleLayerIds: rippleIds.toList(growable: false))
        : next;
    final shiftedOutgoing = outgoing.copyLayer(
      duration: outgoing.duration + durationDelta,
    );
    // A tolerancia serve para encontrar a juncao, mas ao grava-la o
    // magnetico faz snap exato. Assim preview e export nao herdam um gap
    // ou overlap de poucos milissegundos.
    final snappedJunction = shiftedOutgoing.endTime;
    final timelineDelta = snappedJunction - incoming.startTime;
    final shiftedIncoming = incoming.copyLayer(
      startTime: snappedJunction,
      transitionIn: storedNext,
      clearTransitionIn: storedNext == null,
    );
    _mutate(
      state.copyWith(
        layers: [
          for (final layer in state.layers)
            if (layer.id == outgoing.id)
              shiftedOutgoing
            else if (layer.id == incoming.id)
              shiftedIncoming
            else if (timelineDelta != Duration.zero &&
                rippleIds.contains(layer.id))
              layer.copyLayer(startTime: layer.startTime + timelineDelta)
            else
              layer,
        ],
      ),
    );
  }

  /// Cria/atualiza a transicao na juncao depois de [outgoingId]. Quando
  /// faltam handles, nao altera nada ate a interface escolher encurtar ou
  /// congelar as pontas.
  bool applyTransition(
    String outgoingId,
    ClipTransitionType type, {
    Duration duration = const Duration(milliseconds: 300),
    TransitionAlignment alignment = TransitionAlignment.center,
    Easing curve = Easing.easeInOut,
    TransitionEdgeFallback fallback = TransitionEdgeFallback.none,
    EffectType? effectType,
  }) {
    var requested = ClipTransition(
      outgoingLayerId: outgoingId,
      type: type,
      duration: duration < Duration.zero ? Duration.zero : duration,
      alignment: alignment,
      curve: curve,
      effect: type == ClipTransitionType.effect && effectType != null
          ? EffectInstance(type: effectType)
          : null,
    );
    final report = transitionHandles(state.layers, outgoingId, requested);
    if (report == null) return false;
    if (!report.hasEnough) {
      switch (fallback) {
        case TransitionEdgeFallback.none:
          return false;
        case TransitionEdgeFallback.shorten:
          final shortened = report.maximumDuration;
          if (shortened < const Duration(milliseconds: 50)) return false;
          requested = requested.copyWith(duration: shortened);
        case TransitionEdgeFallback.freeze:
          requested = requested.copyWith(freezeEdges: true);
      }
    }
    requested = _fitEndATrim(
      report.outgoing,
      requested,
      report.incoming.transitionIn,
    );
    if (duration > Duration.zero && requested.duration == Duration.zero) {
      return false;
    }
    if (requested.duration < const Duration(milliseconds: 50) &&
        requested.duration > Duration.zero) {
      return false;
    }
    _commitTransition(
      report.outgoing,
      report.incoming,
      report.incoming.transitionIn,
      requested,
    );
    return true;
  }

  void removeTransition(String outgoingId) {
    final incoming = clipAfter(outgoingId);
    if (incoming == null || incoming.transitionIn == null) return;
    final outgoing = _layer(outgoingId);
    if (outgoing is! VideoLayer) return;
    _commitTransition(outgoing, incoming, incoming.transitionIn, null);
  }

  void updateTransition(
    String outgoingId,
    ClipTransition Function(ClipTransition) update,
  ) {
    final incoming = clipAfter(outgoingId);
    final current = incoming?.transitionIn;
    if (incoming == null || current == null) return;
    _replace(incoming.copyLayer(transitionIn: update(current)));
  }

  void setTransitionDuration(String outgoingId, Duration duration) {
    final incoming = clipAfter(outgoingId);
    final current = incoming?.transitionIn;
    if (incoming == null || current == null) return;
    var next = current.copyWith(
      duration: duration < Duration.zero ? Duration.zero : duration,
    );
    final report = transitionHandles(state.layers, outgoingId, next);
    if (report != null && !report.hasEnough && !next.freezeEdges) {
      next = next.copyWith(duration: report.maximumDuration);
    }
    final outgoing = _layer(outgoingId);
    if (outgoing is! VideoLayer) return;
    next = _fitEndATrim(outgoing, next, current);
    _commitTransition(outgoing, incoming, current, next);
  }

  void setTransitionAlignment(
    String outgoingId,
    TransitionAlignment alignment,
  ) {
    final incoming = clipAfter(outgoingId);
    final current = incoming?.transitionIn;
    if (incoming == null || current == null) return;
    var next = current.copyWith(alignment: alignment);
    final report = transitionHandles(state.layers, outgoingId, next);
    if (report != null && !report.hasEnough && !next.freezeEdges) {
      next = next.copyWith(duration: report.maximumDuration);
    }
    final outgoing = _layer(outgoingId);
    if (outgoing is! VideoLayer) return;
    next = _fitEndATrim(outgoing, next, current);
    _commitTransition(outgoing, incoming, current, next);
  }

  void setTransitionCurve(String outgoingId, Easing curve) {
    updateTransition(outgoingId, (t) => t.copyWith(curve: curve));
  }

  void setTransitionAudioCrossfade(String outgoingId, bool enabled) {
    updateTransition(outgoingId, (t) => t.copyWith(crossfadeAudio: enabled));
  }

  void setTransitionEffect(String outgoingId, EffectType effectType) {
    updateTransition(
      outgoingId,
      (t) => t.copyWith(
        type: ClipTransitionType.effect,
        effect: EffectInstance(type: effectType),
      ),
    );
  }

  void setTransitionEffectParam(String outgoingId, String key, double value) {
    updateTransition(outgoingId, (t) {
      final effect = t.effect;
      if (effect == null) return t;
      return t.copyWith(
        effect: effect.copyWith(
          params: {...effect.params, key: effect.track(key).withBase(value)},
        ),
      );
    });
  }

  // ------------------------------------------------------------ congelar

  ({Duration sourceOffset, AnimatedDouble track}) _sliceVideoTrack(
    VideoLayer layer,
    Duration from,
    Duration to,
  ) {
    final originalTrack = timeRemapTrackOf(layer);
    final times = <Duration>{from, to};
    if (originalTrack != null) {
      for (final k in originalTrack.keyframes) {
        if (k.time > from && k.time < to) times.add(k.time);
      }
    }
    final sorted = times.toList()..sort();
    final values = <double>[
      for (final t in sorted)
        videoSourceTimeAt(layer, t).inMicroseconds / 1000000.0,
    ];
    var minimum = values.first;
    for (final value in values) {
      if (value < minimum) minimum = value;
    }
    var track = AnimatedDouble(values.first - minimum);
    for (var i = 0; i < sorted.length; i++) {
      track = track.withKeyframe(
        sorted[i] - from,
        values[i] - minimum,
        originalTrack?.easeAt(sorted[i]) ?? Easing.linear,
      );
    }
    return (
      sourceOffset:
          layer.sourceOffset +
          Duration(microseconds: (minimum * 1000000).round()),
      track: track,
    );
  }

  /// Congela o quadro do cabecote e ripla tudo que comeca dali para a
  /// direita. [insideClip] grava o hold no proprio Time Remap;
  /// [separateClip] divide e insere uma camada de quadro parado.
  bool freezeFrame(
    String id,
    Duration globalTime, {
    Duration duration = const Duration(seconds: 1),
    FreezePlacement placement = FreezePlacement.separateClip,
  }) {
    final layer = _layer(id);
    if (layer is! VideoLayer ||
        duration <= Duration.zero ||
        !layer.activeAt(globalTime)) {
      return false;
    }
    final at = layer.localTime(globalTime);
    if (at < const Duration(milliseconds: 50) ||
        layer.duration - at < const Duration(milliseconds: 50)) {
      return false;
    }

    if (placement == FreezePlacement.insideClip) {
      final before = _sliceVideoTrack(layer, Duration.zero, at);
      final after = _sliceVideoTrack(layer, at, layer.duration);
      final frozenValue =
          videoSourceTimeAt(layer, at).inMicroseconds / 1000000.0;
      var track = AnimatedDouble(
        videoSourceTimeAt(layer, Duration.zero).inMicroseconds / 1000000.0,
      );
      for (final k in before.track.keyframes) {
        final absolute =
            before.sourceOffset.inMicroseconds / 1000000.0 +
            k.value -
            layer.sourceOffset.inMicroseconds / 1000000.0;
        track = track.withKeyframe(k.time, absolute, k.ease);
      }
      track = track
          .withKeyframe(at, frozenValue)
          .withKeyframe(at + duration, frozenValue);
      for (final k in after.track.keyframes.skip(1)) {
        final absolute =
            after.sourceOffset.inMicroseconds / 1000000.0 +
            k.value -
            layer.sourceOffset.inMicroseconds / 1000000.0;
        track = track.withKeyframe(at + duration + k.time, absolute, k.ease);
      }
      final extended = layer.copyLayer(
        duration: layer.duration + duration,
        speed: 1,
        reverse: false,
        effects: replaceTimeRemap(layer, track),
      );
      _mutate(
        state.copyWith(
          layers: [
            for (final l in state.layers)
              if (l.id == id)
                extended
              else if (l.startTime >= globalTime)
                l.copyLayer(startTime: l.startTime + duration)
              else
                l,
          ],
        ),
      );
      return true;
    }

    final firstSlice = _sliceVideoTrack(layer, Duration.zero, at);
    final secondSlice = _sliceVideoTrack(layer, at, layer.duration);
    final first = layer.copyLayer(
      duration: at,
      sourceOffset: firstSlice.sourceOffset,
      speed: 1,
      reverse: false,
      effects: replaceTimeRemap(layer, firstSlice.track),
    );
    final second = layer.duplicated().copyLayer(
      name: layer.name,
      startTime: globalTime + duration,
      duration: layer.duration - at,
      sourceOffset: secondSlice.sourceOffset,
      speed: 1,
      reverse: false,
      effects: replaceTimeRemap(layer, secondSlice.track),
      clearTransitionIn: true,
    );
    final frozenSource = videoAbsoluteSourceTimeAt(layer, at);
    final hold = AnimatedDouble(0)
        .withKeyframe(Duration.zero, 0)
        .withKeyframe(duration, 0);
    final frozen = layer.duplicated().copyLayer(
      name: '${layer.name} · quadro parado',
      startTime: globalTime,
      duration: duration,
      sourceOffset: frozenSource,
      speed: 1,
      reverse: false,
      volume: 0,
      audio: layer.audio.copyWith(muted: true),
      effects: replaceTimeRemap(layer, hold),
      clearTransitionIn: true,
    );

    _mutate(
      state.copyWith(
        layers: [
          for (final l in state.layers)
            if (l.id == id) ...[
              second,
              frozen,
              first,
            ] else if (l is VideoLayer && l.transitionIn?.outgoingLayerId == id)
              l.copyLayer(
                startTime: l.startTime >= globalTime
                    ? l.startTime + duration
                    : l.startTime,
                transitionIn: l.transitionIn!.copyWith(
                  outgoingLayerId: second.id,
                ),
              )
            else if (l.startTime >= globalTime)
              l.copyLayer(startTime: l.startTime + duration)
            else
              l,
        ],
      ),
    );
    ref.read(selectedLayerProvider.notifier).state = frozen.id;
    return true;
  }

  // ------------------------------------------------------- marcadores

  /// Marca o instante atual. Marcar duas vezes no mesmo lugar TIRA a
  /// marca — e o mesmo gesto, e ninguem precisa procurar como apagar.
  void toggleMarker(Duration t, {String label = ''}) {
    const tol = Duration(milliseconds: 120);
    final existente = state.markerNear(t, tol);
    if (existente != null) {
      _mutate(
        state.copyWith(
          markers: [
            for (final m in state.markers)
              if (m.time != existente.time) m,
          ],
        ),
      );
      return;
    }
    _mutate(
      state.copyWith(
        markers: [
          ...state.markers,
          Marker(time: t, label: label),
        ],
      ),
    );
  }

  /// Renomeia a marca em [t] (a mais proxima).
  void renameMarker(Duration t, String label) {
    const tol = Duration(milliseconds: 120);
    final alvo = state.markerNear(t, tol);
    if (alvo == null) return;
    _mutate(
      state.copyWith(
        markers: [
          for (final m in state.markers)
            if (m.time == alvo.time) m.copyWith(label: label) else m,
        ],
      ),
    );
  }

  /// Arrasta uma marca de [de] para [para].
  void moveMarker(Duration de, Duration para) {
    const tol = Duration(milliseconds: 120);
    final alvo = state.markerNear(de, tol);
    if (alvo == null) return;
    final t = para < Duration.zero ? Duration.zero : para;
    _mutate(
      state.copyWith(
        markers: [
          for (final m in state.markers)
            if (m.time == alvo.time) m.copyWith(time: t) else m,
        ],
      ),
    );
  }

  /// Pinta a marca. Cor de marcador nao e enfeite: e como se separa
  /// "corte" de "letra da musica" numa regua cheia.
  void setMarkerColor(Duration t, Color cor) {
    const tol = Duration(milliseconds: 120);
    final alvo = state.markerNear(t, tol);
    if (alvo == null) return;
    _mutate(
      state.copyWith(
        markers: [
          for (final m in state.markers)
            if (m.time == alvo.time) m.copyWith(color: cor) else m,
        ],
      ),
    );
  }

  void removeMarker(Duration t) {
    const tol = Duration(milliseconds: 120);
    final alvo = state.markerNear(t, tol);
    if (alvo == null) return;
    _mutate(
      state.copyWith(
        markers: [
          for (final m in state.markers)
            if (m.time != alvo.time) m,
        ],
      ),
    );
  }

  void clearMarkers() {
    if (state.markers.isEmpty) return;
    _mutate(state.copyWith(markers: const []));
  }

  /// A marca seguinte (ou anterior) a [t] — para pular de marca em
  /// marca em vez de arrastar o playhead no olho.
  Duration? markerAfter(Duration t) {
    for (final m in state.markers) {
      if (m.time > t + const Duration(milliseconds: 20)) return m.time;
    }
    return null;
  }

  Duration? markerBefore(Duration t) {
    Duration? achado;
    for (final m in state.markers) {
      if (m.time < t - const Duration(milliseconds: 20)) achado = m.time;
    }
    return achado;
  }

  // ------------------------------------------ montagem (NLE)

  /// JUNTA dois pedacos do mesmo arquivo de volta num clipe so.
  ///
  /// Devolve false quando nao da (arquivos diferentes, separados na
  /// linha, ou fora de sequencia na fonte) — a interface avisa em vez de
  /// nao fazer nada em silencio.
  bool joinWithNeighbour(String id) {
    final vizinho = joinableNeighbour(state.layers, id);
    if (vizinho == null) return false;
    _mutate(state.copyWith(layers: joinAdjacent(state.layers, id, vizinho.id)));
    return true;
  }

  /// Se este clipe tem um vizinho para juntar (a marca na juncao).
  bool hasJoinableNeighbour(String id) =>
      joinableNeighbour(state.layers, id) != null;

  /// EXCLUSAO COM ARRASTO: tira a camada e puxa para tras o que vinha
  /// depois. E a diferenca entre "apaguei um trecho" e "apaguei um
  /// trecho e agora tenho um silencio no meio".
  void rippleDeleteLayer(String id) {
    if (_layer(id) == null) return;
    final deleted = rippleDelete(state.layers, id);
    _mutate(
      state.copyWith(
        layers: [
          for (final l in deleted)
            if (l is VideoLayer && l.transitionIn?.outgoingLayerId == id)
              l.copyLayer(clearTransitionIn: true)
            else
              l,
        ],
      ),
    );
    if (ref.read(selectedLayerProvider) == id) {
      ref.read(selectedLayerProvider.notifier).state = null;
    }
  }

  /// FECHAR BURACOS: encosta tudo, sem mudar ordem nem duracao.
  void closeTimelineGaps({Duration from = Duration.zero}) {
    final antes = gapsIn(state.layers, from: from);
    if (antes.isEmpty) return;
    _mutate(state.copyWith(layers: closeGaps(state.layers, from: from)));
  }

  /// Quantos vazios existem hoje — para o comando saber se tem o que
  /// fazer, em vez de piscar sem efeito.
  int gapCount({Duration from = Duration.zero}) =>
      gapsIn(state.layers, from: from).length;

  /// INSERIR: abre espaco e empurra para frente o que vem depois.
  void insertLayerAt(Layer novo, Duration at) {
    final r = insertAt(state.layers, novo, at);
    _mutate(state.copyWith(layers: r.layers));
    ref.read(selectedLayerProvider.notifier).state = r.inserted.id;
  }

  /// SOBRESCREVER: poe por cima apagando o que estava embaixo, sem
  /// esticar a linha do tempo.
  void overwriteLayerAt(Layer novo, Duration at) {
    final r = overwriteAt(state.layers, novo, at);
    _mutate(state.copyWith(layers: r.layers));
    ref.read(selectedLayerProvider.notifier).state = r.inserted.id;
  }

  /// LEVANTAR: tira o trecho e deixa o buraco (mantem a sincronia).
  void liftTimeRange(Duration from, Duration to, {Set<String>? only}) {
    if (to <= from) return;
    _mutate(
      state.copyWith(layers: liftRange(state.layers, from, to, only: only)),
    );
  }

  /// EXTRAIR: tira o trecho e fecha o buraco.
  void extractTimeRange(Duration from, Duration to, {Set<String>? only}) {
    if (to <= from) return;
    _mutate(
      state.copyWith(layers: extractRange(state.layers, from, to, only: only)),
    );
  }

  void duplicateLayer(String id) {
    final layer = _layer(id);
    if (layer == null) return;
    final copy = layer.duplicated();
    final idx = state.layers.indexWhere((l) => l.id == id);
    final layers = [...state.layers]..insert(idx, copy);
    _mutate(state.copyWith(layers: layers));
    ref.read(selectedLayerProvider.notifier).state = copy.id;
  }

  void reorderLayer(String id, int delta) {
    final layers = [...state.layers];
    final idx = layers.indexWhere((l) => l.id == id);
    if (idx < 0) return;
    final to = (idx + delta).clamp(0, layers.length - 1);
    if (to == idx) return;
    final layer = layers.removeAt(idx);
    layers.insert(to, layer);
    _mutate(state.copyWith(layers: layers));
  }

  void selectNeighbor(int delta) {
    final id = ref.read(selectedLayerProvider);
    if (id == null || state.layers.isEmpty) return;
    final idx = state.layers.indexWhere((l) => l.id == id);
    final to = (idx + delta).clamp(0, state.layers.length - 1);
    ref.read(selectedLayerProvider.notifier).state = state.layers[to].id;
  }

  // ------------------------------------------------------------- tempo/trim

  void moveLayer(String id, Duration newStart) {
    final layer = _layer(id);
    if (layer == null) return;
    final start = newStart < Duration.zero ? Duration.zero : newStart;
    _replace(layer.copyLayer(startTime: start));
  }

  void trimLayerStart(String id, Duration newStart) {
    final layer = _layer(id);
    if (layer == null) return;
    var start = newStart < Duration.zero ? Duration.zero : newStart;
    final maxStart = layer.endTime - const Duration(milliseconds: 100);
    if (start > maxStart) start = maxStart;
    final delta = start - layer.startTime;
    if (layer is VideoLayer) {
      if (hasTimeRemap(layer) || layer.reverse) {
        final sliced = _sliceVideoTrack(layer, delta, layer.duration);
        _replace(
          layer.copyLayer(
            startTime: start,
            duration: layer.endTime - start,
            sourceOffset: sliced.sourceOffset,
            speed: 1,
            reverse: false,
            effects: replaceTimeRemap(layer, sliced.track),
          ),
        );
      } else {
        var offset =
            layer.sourceOffset +
            Duration(
              microseconds: (delta.inMicroseconds * layer.speed).round(),
            );
        if (offset < Duration.zero) offset = Duration.zero;
        _replace(
          layer.copyLayer(
            startTime: start,
            duration: layer.endTime - start,
            sourceOffset: offset,
          ),
        );
      }
    } else if (layer is AudioLayer) {
      var offset =
          layer.sourceOffset +
          Duration(microseconds: (delta.inMicroseconds * layer.speed).round());
      if (offset < Duration.zero) offset = Duration.zero;
      _replace(
        layer.copyLayer(
          startTime: start,
          duration: layer.endTime - start,
          sourceOffset: offset,
        ),
      );
    } else {
      _replace(
        layer.copyLayer(startTime: start, duration: layer.endTime - start),
      );
    }
  }

  void trimLayerEnd(String id, Duration newEnd) {
    final layer = _layer(id);
    if (layer == null) return;
    var duration = newEnd - layer.startTime;
    if (duration < const Duration(milliseconds: 100)) {
      duration = const Duration(milliseconds: 100);
    }
    if (layer is VideoLayer &&
        duration < layer.duration &&
        (layer.reverse || hasTimeRemap(layer))) {
      final slice = _sliceVideoTrack(layer, Duration.zero, duration);
      _replace(
        layer.copyLayer(
          duration: duration,
          sourceOffset: slice.sourceOffset,
          speed: 1,
          reverse: false,
          effects: replaceTimeRemap(layer, slice.track),
        ),
      );
      return;
    }
    _replace(layer.copyLayer(duration: duration));
  }

  void splitLayer(String id, Duration at) {
    final layer = _layer(id);
    if (layer == null || !layer.activeAt(at)) return;
    final firstDur = at - layer.startTime;
    final secondDur = layer.endTime - at;
    if (firstDur < const Duration(milliseconds: 100) ||
        secondDur < const Duration(milliseconds: 100)) {
      return;
    }

    Layer first = layer.copyLayer(duration: firstDur);
    Layer second = layer.duplicated().copyLayer(
      startTime: at,
      duration: secondDur,
    );
    if (second is VideoLayer && layer is VideoLayer) {
      if (hasTimeRemap(layer) || layer.reverse) {
        final a = _sliceVideoTrack(layer, Duration.zero, firstDur);
        final b = _sliceVideoTrack(layer, firstDur, layer.duration);
        first = layer.copyLayer(
          duration: firstDur,
          sourceOffset: a.sourceOffset,
          speed: 1,
          reverse: false,
          effects: replaceTimeRemap(layer, a.track),
        );
        second = second.copyLayer(
          sourceOffset: b.sourceOffset,
          speed: 1,
          reverse: false,
          effects: replaceTimeRemap(layer, b.track),
          clearTransitionIn: true,
        );
      } else {
        second = second.copyLayer(
          sourceOffset:
              layer.sourceOffset +
              Duration(
                microseconds: (firstDur.inMicroseconds * layer.speed).round(),
              ),
          clearTransitionIn: true,
        );
      }
    } else if (second is AudioLayer && layer is AudioLayer) {
      second = second.copyLayer(
        sourceOffset:
            layer.sourceOffset +
            Duration(
              microseconds: (firstDur.inMicroseconds * layer.speed).round(),
            ),
      );
    }

    final layers = <Layer>[];
    for (final l in state.layers) {
      if (l.id == id) {
        layers.add(second);
        layers.add(first);
      } else if (l is VideoLayer &&
          second is VideoLayer &&
          l.transitionIn?.outgoingLayerId == id) {
        layers.add(
          l.copyLayer(
            transitionIn: l.transitionIn!.copyWith(outgoingLayerId: second.id),
          ),
        );
      } else {
        layers.add(l);
      }
    }
    _mutate(state.copyWith(layers: layers));
    ref.read(selectedLayerProvider.notifier).state = second.id;
  }

  /// DIVIDE a camada em todos os [times] (globais), de uma vez.
  ///
  /// E o "decupar sozinho": os cortes que a deteccao de cena achou viram
  /// pedacos na linha do tempo. Cada pedaco novo e a segunda metade do
  /// anterior, entao a divisao segue em cadeia; tudo cai num passo so
  /// de desfazer. Devolve os ids dos pedacos, na ordem do tempo.
  List<String> splitLayerAtTimes(String id, Iterable<Duration> times) {
    final ordenados = times.toList()..sort();
    var atual = id;
    final ids = <String>[id];
    for (final t in ordenados) {
      final layer = _layer(atual);
      if (layer == null) break;
      if (!layer.activeAt(t)) continue;
      ref.read(selectedLayerProvider.notifier).state = atual;
      splitLayer(atual, t);
      final novo = ref.read(selectedLayerProvider);
      if (novo != null && novo != atual) {
        atual = novo;
        ids.add(novo);
      }
    }
    return ids;
  }

  /// MARCAS em varios instantes de uma vez, sem duplicar as que ja
  /// existem perto. E o outro destino dos cortes de cena: marcar em vez
  /// de cortar, para decidir depois.
  void addMarkers(Iterable<Duration> times, {String label = ''}) {
    const tol = Duration(milliseconds: 120);
    final novos = <Marker>[];
    for (final t in times) {
      if (t < Duration.zero) continue;
      if (state.markerNear(t, tol) != null) continue;
      if (novos.any((m) => (m.time - t).abs() < tol)) continue;
      novos.add(Marker(time: t, label: label));
    }
    if (novos.isEmpty) return;
    final todos = [...state.markers, ...novos]
      ..sort((a, b) => a.time.compareTo(b.time));
    _mutate(state.copyWith(markers: todos));
  }

  // -------------------------------------------------- transform + keyframes

  void editPosition(String id, Duration globalTime, Offset value) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        position: _editOffset(
          layer.position,
          layer.localTime(globalTime),
          value,
        ),
      ),
    );
  }

  AnimatedDouble _editDouble(
    AnimatedDouble track,
    Duration time,
    double value,
  ) => editMotionValue(
    track,
    time,
    value,
    autoKey: ref.read(autoKeyframeProvider),
  );

  AnimatedOffset _editOffset(
    AnimatedOffset track,
    Duration time,
    Offset value,
  ) {
    if (!ref.read(autoKeyframeProvider)) return track.edited(time, value);
    final anchored = !track.isAnimated && time > Duration.zero
        ? track.withKeyframe(Duration.zero, track.base)
        : track;
    return anchored.withKeyframe(time, value, track.easeAt(time));
  }

  /// CRAVA UMA TRILHA DE ESCALA INTEIRA de uma vez.
  ///
  /// O AutoEdit calcula dezenas de keyframes de zoom antes de tocar no
  /// projeto; aplicar um por um passaria pela coalescencia de undo e pelo
  /// recalculo do preview a cada um. Aqui entra a trilha pronta — e ela
  /// e uma trilha COMUM, que abre no editor e se arrasta como qualquer
  /// outra.
  void setScaleKeyframes(String id, List<Keyframe<double>> keyframes) {
    final layer = _layer(id);
    if (layer == null) return;
    final ordenados = [...keyframes]..sort((a, b) => a.time.compareTo(b.time));
    _replace(
      layer.copyLayer(
        scaleX: AnimatedDouble(layer.scaleX.base, ordenados),
        scaleY: AnimatedDouble(layer.scaleY.base, ordenados),
      ),
    );
    _push(layer);
  }

  void editScaleUniform(String id, Duration globalTime, double value) {
    final layer = _layer(id);
    if (layer == null) return;
    final t = layer.localTime(globalTime);
    _replace(
      layer.copyLayer(
        scaleX: _editDouble(layer.scaleX, t, value),
        scaleY: _editDouble(layer.scaleY, t, value),
      ),
    );
  }

  void editScaleX(String id, Duration globalTime, double value) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        scaleX: _editDouble(layer.scaleX, layer.localTime(globalTime), value),
      ),
    );
  }

  void editScaleY(String id, Duration globalTime, double value) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        scaleY: _editDouble(layer.scaleY, layer.localTime(globalTime), value),
      ),
    );
  }

  /// Rotacao e uma propriedade GLOBAL de 3 eixos: com animacao ligada,
  /// editar qualquer eixo (X, Y ou Z) marca keyframe nos TRES ao mesmo
  /// tempo — os eixos ficam sempre sincronizados na timeline.
  void _editRotationAxis(
    String id,
    Duration globalTime, {
    double? z,
    double? x,
    double? y,
  }) {
    final layer = _layer(id);
    if (layer == null) return;
    final t = layer.localTime(globalTime);
    final anyAnimated =
        layer.rotation.isAnimated ||
        layer.rotationX.isAnimated ||
        layer.rotationY.isAnimated;
    if (!anyAnimated && !ref.read(autoKeyframeProvider)) {
      _replace(
        layer.copyLayer(
          rotation: z == null ? null : layer.rotation.withBase(z),
          rotationX: x == null ? null : layer.rotationX.withBase(x),
          rotationY: y == null ? null : layer.rotationY.withBase(y),
        ),
      );
      return;
    }
    AnimatedDouble key(AnimatedDouble track, double? v) =>
        ref.read(autoKeyframeProvider)
        ? _editDouble(track, t, v ?? track.valueAt(t))
        : track.withKeyframe(t, v ?? track.valueAt(t), track.easeAt(t));
    _replace(
      layer.copyLayer(
        rotation: key(layer.rotation, z),
        rotationX: key(layer.rotationX, x),
        rotationY: key(layer.rotationY, y),
      ),
    );
  }

  void editRotation(String id, Duration globalTime, double value) =>
      _editRotationAxis(id, globalTime, z: value);

  void editRotationX(String id, Duration globalTime, double value) =>
      _editRotationAxis(id, globalTime, x: value);

  void editRotationY(String id, Duration globalTime, double value) =>
      _editRotationAxis(id, globalTime, y: value);

  void editOpacity(String id, Duration globalTime, double value) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        opacity: _editDouble(
          layer.opacity,
          layer.localTime(globalTime),
          value.clamp(0, 1),
        ),
      ),
    );
  }

  void editSkewX(String id, Duration globalTime, double value) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        skewX: _editDouble(layer.skewX, layer.localTime(globalTime), value),
      ),
    );
  }

  void editSkewY(String id, Duration globalTime, double value) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        skewY: _editDouble(layer.skewY, layer.localTime(globalTime), value),
      ),
    );
  }

  void editPivot(String id, Duration globalTime, Offset value) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        pivot: _editOffset(layer.pivot, layer.localTime(globalTime), value),
      ),
    );
  }

  /// Escolhe um dos modos PROPRIOS (Linear Burn, Vivid Light...), que
  /// nao existem no Flutter e passam pelo compositor de dois andares.
  void setCustomBlend(String id, AureaBlend? mode) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        blendMode: BlendMode.srcOver,
        customBlend: mode,
        clearCustomBlend: mode == null,
      ),
    );
  }

  void setBlendMode(String id, BlendMode mode) {
    final layer = _layer(id);
    if (layer == null) return;
    // Escolher um modo nativo desliga o proprio: so um manda.
    _replace(layer.copyLayer(blendMode: mode, clearCustomBlend: true));
  }

  /// Reseta a propriedade: limpa keyframes e volta ao valor padrao.
  void resetProp(String id, LayerProp prop) {
    final layer = _layer(id);
    if (layer == null) return;
    switch (prop) {
      case LayerProp.position:
        _replace(layer.copyLayer(position: AnimatedOffset(_center)));
      case LayerProp.scale:
        _replace(
          layer.copyLayer(scaleX: AnimatedDouble(1), scaleY: AnimatedDouble(1)),
        );
      case LayerProp.rotation:
        _replace(
          layer.copyLayer(
            rotation: AnimatedDouble(0),
            rotationX: AnimatedDouble(0),
            rotationY: AnimatedDouble(0),
          ),
        );
      case LayerProp.opacity:
        _replace(layer.copyLayer(opacity: AnimatedDouble(1)));
      case LayerProp.skew:
        _replace(
          layer.copyLayer(skewX: AnimatedDouble(0), skewY: AnimatedDouble(0)),
        );
      case LayerProp.pivot:
        _replace(layer.copyLayer(pivot: AnimatedOffset(Offset.zero)));
      case LayerProp.parent:
        break;
    }
  }

  /// Tempos locais de keyframe da propriedade (para ‹◆›).
  List<Duration> propKeyframeTimes(Layer layer, LayerProp prop) {
    final us = switch (prop) {
      LayerProp.position => layer.positionTimesUs,
      LayerProp.scale => layer.scaleTimesUs,
      LayerProp.rotation => layer.rotationTimesUs,
      LayerProp.opacity => layer.opacityTimesUs,
      LayerProp.skew => layer.skewTimesUs,
      LayerProp.pivot => layer.pivotTimesUs,
      LayerProp.parent => const <int>{},
    };
    final list = us.toList()..sort();
    return [for (final u in list) Duration(microseconds: u)];
  }

  void toggleKeyframe(String id, Duration globalTime, LayerProp prop) {
    final layer = _layer(id);
    if (layer == null) return;
    final t = layer.localTime(globalTime);

    AnimatedDouble tog(AnimatedDouble track) => track.hasKeyframeAt(t)
        ? track.withoutKeyframe(t)
        : track.withKeyframe(t, track.valueAt(t));
    AnimatedOffset togO(AnimatedOffset track) => track.hasKeyframeAt(t)
        ? track.withoutKeyframe(t)
        : track.withKeyframe(t, track.valueAt(t));

    switch (prop) {
      case LayerProp.position:
        _replace(layer.copyLayer(position: togO(layer.position)));
      case LayerProp.scale:
        _replace(
          layer.copyLayer(scaleX: tog(layer.scaleX), scaleY: tog(layer.scaleY)),
        );
      case LayerProp.rotation:
        // Keyframe de rotacao e GLOBAL: marca/desmarca X, Y e Z juntos,
        // seja qual for o eixo que o usuario esta usando.
        _replace(
          layer.copyLayer(
            rotation: tog(layer.rotation),
            rotationX: tog(layer.rotationX),
            rotationY: tog(layer.rotationY),
          ),
        );
      case LayerProp.opacity:
        _replace(layer.copyLayer(opacity: tog(layer.opacity)));
      case LayerProp.skew:
        _replace(
          layer.copyLayer(skewX: tog(layer.skewX), skewY: tog(layer.skewY)),
        );
      case LayerProp.pivot:
        _replace(layer.copyLayer(pivot: togO(layer.pivot)));
      case LayerProp.parent:
        break;
    }
  }

  void setSegmentEase(
    String id,
    LayerProp prop,
    Duration segStartLocal,
    Easing ease,
  ) {
    final layer = _layer(id);
    if (layer == null) return;
    switch (prop) {
      case LayerProp.position:
        _replace(
          layer.copyLayer(
            position: layer.position.withEase(segStartLocal, ease),
          ),
        );
      case LayerProp.scale:
        _replace(
          layer.copyLayer(
            scaleX: layer.scaleX.withEase(segStartLocal, ease),
            scaleY: layer.scaleY.withEase(segStartLocal, ease),
          ),
        );
      case LayerProp.rotation:
        _replace(
          layer.copyLayer(
            rotation: layer.rotation.withEase(segStartLocal, ease),
            rotationX: layer.rotationX.withEase(segStartLocal, ease),
            rotationY: layer.rotationY.withEase(segStartLocal, ease),
          ),
        );
      case LayerProp.opacity:
        _replace(
          layer.copyLayer(opacity: layer.opacity.withEase(segStartLocal, ease)),
        );
      case LayerProp.skew:
        _replace(
          layer.copyLayer(
            skewX: layer.skewX.withEase(segStartLocal, ease),
            skewY: layer.skewY.withEase(segStartLocal, ease),
          ),
        );
      case LayerProp.pivot:
        _replace(
          layer.copyLayer(pivot: layer.pivot.withEase(segStartLocal, ease)),
        );
      case LayerProp.parent:
        break;
    }
  }

  void applyEaseToAllSegments(String id, LayerProp prop, Easing ease) {
    final layer = _layer(id);
    if (layer == null) return;
    switch (prop) {
      case LayerProp.position:
        _replace(layer.copyLayer(position: layer.position.withEaseAll(ease)));
      case LayerProp.scale:
        _replace(
          layer.copyLayer(
            scaleX: layer.scaleX.withEaseAll(ease),
            scaleY: layer.scaleY.withEaseAll(ease),
          ),
        );
      case LayerProp.rotation:
        _replace(
          layer.copyLayer(
            rotation: layer.rotation.withEaseAll(ease),
            rotationX: layer.rotationX.withEaseAll(ease),
            rotationY: layer.rotationY.withEaseAll(ease),
          ),
        );
      case LayerProp.opacity:
        _replace(layer.copyLayer(opacity: layer.opacity.withEaseAll(ease)));
      case LayerProp.skew:
        _replace(
          layer.copyLayer(
            skewX: layer.skewX.withEaseAll(ease),
            skewY: layer.skewY.withEaseAll(ease),
          ),
        );
      case LayerProp.pivot:
        _replace(layer.copyLayer(pivot: layer.pivot.withEaseAll(ease)));
      case LayerProp.parent:
        break;
    }
  }

  // --------------------------------------------------------------- efeitos

  void addEffect(String layerId, EffectType type) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        effects: [
          ...layer.effects,
          EffectInstance(type: type),
        ],
      ),
    );
  }

  /// AS TRES PROFUNDIDADES (constituicao, regra 2): descer e subir nao
  /// mexe em numero nenhum — muda so quanto se ve.
  void setEffectDepth(String layerId, String effectId, EffectDepth depth) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        effects: [
          for (final e in layer.effects)
            e.id == effectId ? e.withDepth(depth) : e,
        ],
      ),
    );
  }

  /// PRONTO: o preset crava os numeros dele (e a cor, quando o preset
  /// manda nela) e deixa o resto como estava.
  void applyEffectPronto(String layerId, String effectId, EffectPronto pronto) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        effects: [
          for (final e in layer.effects)
            e.id == effectId ? e.withPreset(pronto) : e,
        ],
      ),
    );
  }

  /// DUPLICAR UM EFEITO, com os valores e os keyframes que ele tem.
  ///
  /// Entra logo abaixo do original: dois Glows um sobre o outro e a
  /// receita de brilho mais usada que existe, e ate agora so dava para
  /// aplicar de novo do zero e reajustar tudo na mao.
  void duplicateEffect(String layerId, String effectId) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final idx = layer.effects.indexWhere((e) => e.id == effectId);
    if (idx < 0) return;
    final original = layer.effects[idx];
    // Sem id: a instancia nova sorteia o proprio. Os keyframes vao junto
    // porque AnimatedDouble e imutavel — compartilhar a trilha aqui e
    // seguro, e e o que faz o duplicado nascer identico.
    final copia = EffectInstance(
      type: original.type,
      params: {...original.params},
      color: original.color,
      enabled: original.enabled,
      depth: original.depth,
      extraColors: [...original.extraColors],
    );
    final novos = [...layer.effects]..insert(idx + 1, copia);
    _replace(layer.copyLayer(effects: novos));
  }

  void removeEffect(String layerId, String effectId) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        effects: layer.effects.where((e) => e.id != effectId).toList(),
      ),
    );
  }

  void reorderEffect(String layerId, String effectId, int delta) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final effects = [...layer.effects];
    final idx = effects.indexWhere((e) => e.id == effectId);
    if (idx < 0) return;
    final to = (idx + delta).clamp(0, effects.length - 1);
    if (to == idx) return;
    final e = effects.removeAt(idx);
    effects.insert(to, e);
    _replace(layer.copyLayer(effects: effects));
  }

  void toggleEffectEnabled(String layerId, String effectId) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        effects: [
          for (final e in layer.effects)
            e.id == effectId ? e.copyWith(enabled: !e.enabled) : e,
        ],
      ),
    );
  }

  /// Edita valor do parametro no tempo global (auto-keyframe se anima).
  void editEffectParam(
    String layerId,
    String effectId,
    String key,
    Duration globalTime,
    double value,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _replace(
      layer.copyLayer(
        effects: [
          for (final e in layer.effects)
            e.id == effectId ? e.withParamEdited(key, local, value) : e,
        ],
      ),
    );
  }

  /// Diamante do EFEITO: keyframe universal neste instante.
  void toggleEffectKeyframe(
    String layerId,
    String effectId,
    Duration globalTime,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _replace(
      layer.copyLayer(
        effects: [
          for (final e in layer.effects)
            e.id == effectId ? e.withKeyframeToggled(local) : e,
        ],
      ),
    );
  }

  /// CURVA DO EFEITO: o easing do trecho que comeca em [segStartLocal],
  /// em TODOS os parametros. O keyframe e universal; a curva tambem.
  void setEffectSegmentEase(
    String layerId,
    String effectId,
    Duration segStartLocal,
    Easing ease,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        effects: [
          for (final e in layer.effects)
            e.id == effectId
                ? e.copyWith(
                    params: {
                      for (final p in e.params.entries)
                        p.key: p.value.hasKeyframeAt(segStartLocal)
                            ? p.value.withEase(segStartLocal, ease)
                            : p.value,
                    },
                  )
                : e,
        ],
      ),
    );
  }

  /// A mesma curva em todos os trechos de todos os parametros do efeito.
  void applyEaseToAllEffectSegments(
    String layerId,
    String effectId,
    Easing ease,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        effects: [
          for (final e in layer.effects)
            e.id == effectId
                ? e.copyWith(
                    params: {
                      for (final p in e.params.entries)
                        p.key: p.value.withEaseAll(ease),
                    },
                  )
                : e,
        ],
      ),
    );
  }

  /// Diamante do parametro do efeito.
  void toggleEffectParamKeyframe(
    String layerId,
    String effectId,
    String key,
    Duration globalTime,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _replace(
      layer.copyLayer(
        effects: [
          for (final e in layer.effects)
            e.id == effectId ? e.withParamKeyframeToggled(key, local) : e,
        ],
      ),
    );
  }

  void setEffectColor(String layerId, String effectId, Color color) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        effects: [
          for (final e in layer.effects)
            e.id == effectId ? e.copyWith(color: color) : e,
        ],
      ),
    );
  }

  // ---------------------------------------------------------- modulo grid

  /// Define os assets da grade do nulo (cria o rig se preciso).
  void setGridAssets(String nullId, List<String> assetIds) {
    final layer = _layer(nullId);
    if (layer is! NullLayer) return;
    final rig = (layer.grid ?? GridRig()).copyWith(assets: assetIds);
    _replace(layer.withGrid(rig));
  }

  void removeGrid(String nullId) {
    final layer = _layer(nullId);
    if (layer is! NullLayer) return;
    _replace(layer.withGrid(null));
  }

  void updateGrid(String nullId, GridRig Function(GridRig) fn) {
    final layer = _layer(nullId);
    if (layer is! NullLayer || layer.grid == null) return;
    _replace(layer.withGrid(fn(layer.grid!)));
  }

  /// Easing do SEGMENTO de uma trilha da grade (curve editor por
  /// parametro — inclui 'transition', o morph).
  void setGridSegmentEase(
    String nullId,
    String key,
    Duration segStartLocal,
    Easing ease,
  ) {
    final layer = _layer(nullId);
    if (layer is! NullLayer || layer.grid == null) return;
    final track = gridTrackOf(layer.grid!, key);
    if (track == null) return;
    _replace(
      layer.withGrid(
        gridWithTrack(layer.grid!, key, track.withEase(segStartLocal, ease)),
      ),
    );
  }

  void applyEaseToAllGridSegments(String nullId, String key, Easing ease) {
    final layer = _layer(nullId);
    if (layer is! NullLayer || layer.grid == null) return;
    final track = gridTrackOf(layer.grid!, key);
    if (track == null) return;
    _replace(
      layer.withGrid(gridWithTrack(layer.grid!, key, track.withEaseAll(ease))),
    );
  }

  /// Nulo CONTROLADOR da grade: o transform dele modula os parametros
  /// (escala -> espacamento/raio, rotZ -> rotacao, rotY -> twist).
  void setGridController(String nullId, String? controllerId) {
    final layer = _layer(nullId);
    if (layer is! NullLayer || layer.grid == null) return;
    _replace(
      layer.withGrid(
        controllerId == null
            ? layer.grid!.copyWith(clearController: true)
            : layer.grid!.copyWith(controllerId: controllerId),
      ),
    );
  }

  AnimatedDouble? _gridTrack(GridRig g, String key) => switch (key) {
    'spacingX' => g.spacingX,
    'spacingY' => g.spacingY,
    'radius' => g.radius,
    'rotation' => g.gridRotationDeg,
    'twist' => g.twistDeg,
    'stagger' => g.staggerDeg,
    'zDepth' => g.zDepth,
    'scaleFront' => g.scaleFront,
    'scaleBack' => g.scaleBack,
    'randomOffset' => g.randomOffset,
    _ => null,
  };

  GridRig _gridWith(GridRig g, String key, AnimatedDouble v) => switch (key) {
    'spacingX' => g.copyWith(spacingX: v),
    'spacingY' => g.copyWith(spacingY: v),
    'radius' => g.copyWith(radius: v),
    'rotation' => g.copyWith(gridRotationDeg: v),
    'twist' => g.copyWith(twistDeg: v),
    'stagger' => g.copyWith(staggerDeg: v),
    'zDepth' => g.copyWith(zDepth: v),
    'scaleFront' => g.copyWith(scaleFront: v),
    'scaleBack' => g.copyWith(scaleBack: v),
    'randomOffset' => g.copyWith(randomOffset: v),
    _ => g,
  };

  /// Edita um parametro da grade com AUTO-KEYFRAME quando ja anima —
  /// cada parametro tem sua propria trilha.
  void editGridParam(
    String nullId,
    String key,
    Duration globalTime,
    double value,
  ) {
    final layer = _layer(nullId);
    if (layer is! NullLayer || layer.grid == null) return;
    final local = layer.localTime(globalTime);
    final track = _gridTrack(layer.grid!, key);
    if (track == null) return;
    _replace(
      layer.withGrid(_gridWith(layer.grid!, key, track.edited(local, value))),
    );
  }

  /// Diamante do parametro da grade: liga/desliga keyframe no playhead.
  void toggleGridParamKeyframe(String nullId, String key, Duration globalTime) {
    final layer = _layer(nullId);
    if (layer is! NullLayer || layer.grid == null) return;
    final local = layer.localTime(globalTime);
    final track = _gridTrack(layer.grid!, key);
    if (track == null) return;
    _replace(
      layer.withGrid(
        _gridWith(
          layer.grid!,
          key,
          track.hasKeyframeAt(local)
              ? track.withoutKeyframe(local)
              : track.withKeyframe(local, track.valueAt(local)),
        ),
      ),
    );
  }

  /// Morph da grade: [transition] anima entre layouts pelo SEGMENTO de
  /// keyframe (1 -> 3 vai direto, sem passar pelo 2).
  void editGridTransition(String nullId, Duration globalTime, double v) {
    final layer = _layer(nullId);
    if (layer is! NullLayer || layer.grid == null) return;
    final local = layer.localTime(globalTime);
    _replace(
      layer.withGrid(
        layer.grid!.copyWith(
          transition: layer.grid!.transition.edited(local, v),
        ),
      ),
    );
  }

  void toggleGridTransitionKeyframe(String nullId, Duration globalTime) {
    final layer = _layer(nullId);
    if (layer is! NullLayer || layer.grid == null) return;
    final local = layer.localTime(globalTime);
    final track = layer.grid!.transition;
    _replace(
      layer.withGrid(
        layer.grid!.copyWith(
          transition: track.hasKeyframeAt(local)
              ? track.withoutKeyframe(local)
              : track.withKeyframe(local, track.valueAt(local)),
        ),
      ),
    );
  }

  // -------------------------------------------------------------- mascaras

  void addMask(String layerId, LayerMask mask) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(layer.copyLayer(masks: [...layer.masks, mask]));
  }

  /// Aplica uma revelacao pronta, mas grava uma mascara comum com dois
  /// keyframes reais para que Montar/Avancado possam continuar editando-a.
  void applyMaskReveal(
    String layerId,
    MaskRevealPreset preset,
    Duration globalTime,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    // A mascara vive antes do transform da camada. Neutralizar a escala
    // evita aplicar scale duas vezes ao tamanho usado pelo caminho local.
    final localLayer = layer.copyLayer(
      scaleX: AnimatedDouble(1),
      scaleY: AnimatedDouble(1),
    );
    final size = measureLayerBox(
      localLayer,
      local,
      fallbackWidth: state.outputWidth.toDouble(),
    );
    addMask(layerId, createRevealMask(preset, size, local));
  }

  void removeMask(String layerId, String maskId) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(masks: layer.masks.where((m) => m.id != maskId).toList()),
    );
  }

  void reorderMask(String layerId, String maskId, int delta) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final masks = [...layer.masks];
    final idx = masks.indexWhere((m) => m.id == maskId);
    if (idx < 0) return;
    final to = (idx + delta).clamp(0, masks.length - 1);
    if (to == idx) return;
    final mask = masks.removeAt(idx);
    masks.insert(to, mask);
    _replace(layer.copyLayer(masks: masks));
  }

  void updateMask(
    String layerId,
    String maskId,
    LayerMask Function(LayerMask) fn,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        masks: [for (final m in layer.masks) m.id == maskId ? fn(m) : m],
      ),
    );
  }

  void cycleMaskMode(String layerId, String maskId) {
    updateMask(layerId, maskId, (m) {
      final next = MaskMode.values[(m.mode.index + 1) % MaskMode.values.length];
      return m.copyWith(mode: next);
    });
  }

  void toggleMaskInverted(String layerId, String maskId) {
    updateMask(layerId, maskId, (m) => m.copyWith(inverted: !m.inverted));
  }

  /// Edita feather/expansao/opacidade da mascara com auto-keyframe.
  void editMaskParam(
    String layerId,
    String maskId,
    String param,
    Duration globalTime,
    double value,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    updateMask(layerId, maskId, (m) {
      return switch (param) {
        'feather' => m.copyWith(feather: m.feather.edited(local, value)),
        'featherY' => m.copyWith(
          featherY: m.featherVertical.edited(local, value),
        ),
        'expansion' => m.copyWith(expansion: m.expansion.edited(local, value)),
        'opacity' => m.copyWith(opacity: m.opacity.edited(local, value)),
        _ => m,
      };
    });
  }

  /// Liga/solta os eixos do feather. Ao soltar, o eixo Y comeca no
  /// valor que ja estava valendo — soltar nao pode mudar a imagem.
  void toggleMaskFeatherAxes(String layerId, String maskId) {
    updateMask(layerId, maskId, (m) {
      if (m.featherLinked) return m.copyWith(featherY: m.feather);
      return m.copyWith(linkFeather: true);
    });
  }

  /// EDITAR OS NOS da mascara. Com o caminho ja animado, vira keyframe
  /// no tempo atual; sem, muda a forma base.
  void editMaskPath(
    String layerId,
    String maskId,
    Duration globalTime,
    BezierPath Function(BezierPath) fn,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    updateMask(
      layerId,
      maskId,
      (m) => m.copyWith(path: m.path.edited(local, fn(m.path.valueAt(local)))),
    );
  }

  /// Substitui a geometria no tempo atual, respeitando o contrato de
  /// auto-keyframe quando o caminho da mascara ja esta animado.
  void replaceMaskPath(
    String layerId,
    String maskId,
    BezierPath path,
    Duration globalTime,
  ) {
    editMaskPath(layerId, maskId, globalTime, (_) => path);
  }

  /// Usa a primeira geometria convertivel da propria ShapeLayer como
  /// caminho da mascara. Fill, stroke e operadores sao ignorados.
  bool setMaskFromOwnShape(String layerId, String maskId, Duration globalTime) {
    final layer = _layer(layerId);
    if (layer is! ShapeLayer || !layer.masks.any((m) => m.id == maskId)) {
      return false;
    }
    final local = layer.localTime(globalTime);
    for (final item in layer.contents) {
      final path = bezierOfShapeItem(item, local);
      if (path != null && path.vertices.isNotEmpty) {
        // A ShapeView normaliza o bounds para (0,0) e centraliza o widget.
        // A mascara, por sua vez, usa a origem no centro da camada; trazer
        // o mesmo caminho para essa origem evita deslocar formas desenhadas
        // cujos vertices nao nasceram ao redor de Offset.zero.
        final center = path.build().getBounds().center;
        final centered = center == Offset.zero
            ? path
            : BezierPath(
                closed: path.closed,
                vertices: [
                  for (final v in path.vertices)
                    PathVertex(
                      p: v.p - center,
                      inT: v.inT,
                      outT: v.outT,
                      corner: v.corner,
                    ),
                ],
              );
        replaceMaskPath(layerId, maskId, centered, globalTime);
        return true;
      }
    }
    return false;
  }

  /// Keyframe do CAMINHO da mascara no tempo atual (PR-M1 aplicado).
  void toggleMaskPathKeyframe(
    String layerId,
    String maskId,
    Duration globalTime,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    updateMask(layerId, maskId, (m) {
      return m.copyWith(
        path: m.path.hasKeyframeAt(local)
            ? m.path.withoutKeyframe(local)
            : m.path.withKeyframe(local, m.path.valueAt(local)),
      );
    });
  }

  // ----------------------------------------------------------------- matte

  /// A ordem do projeto e de cima para baixo; track matte usa exatamente
  /// a vizinha anterior e nao atravessa uma camada sem saida visual.
  Layer? matteSourceAbove(String layerId) {
    final idx = state.layers.indexWhere((l) => l.id == layerId);
    if (idx <= 0) return null;
    final source = state.layers[idx - 1];
    if (source is AudioLayer ||
        source is NullLayer ||
        source is AdjustmentLayer) {
      return null;
    }
    return source;
  }

  bool setMatteFromAbove(String layerId, MatteMode mode) {
    if (_layer(layerId) == null) return false;
    if (mode == MatteMode.none) {
      setMatte(layerId, mode, null);
      return true;
    }
    final source = matteSourceAbove(layerId);
    if (source == null) return false;
    setMatte(layerId, mode, source.id);
    return true;
  }

  /// Define o matte da camada (PR-M5): [sourceId] pode ser QUALQUER
  /// camada da cena; a fonte fica oculta automaticamente no render.
  void setMatte(String layerId, MatteMode mode, String? sourceId) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        matteMode: mode,
        matteSourceId: sourceId,
        clearMatteSource: mode == MatteMode.none,
      ),
    );
  }

  /// Modo do Trim Paths: individual (cascata) ou continuo (PR-M8).
  void setTrimMode(String layerId, String itemId, bool individually) {
    _updateShape(
      layerId,
      (items) => [
        for (final i in items)
          if (i.id == itemId && i is TrimOperator)
            i.copyWith(individually: individually)
          else
            i,
      ],
    );
  }

  // -------------------------------------------------------------- legendas

  /// Cria uma camada de legendas: UMA camada, muitos cues.
  /// TROCA O ESTILO DESTAQUE da camada de legenda.
  ///
  /// O estilo e da CAMADA, nao da fala: um toque muda as 47 falas de uma
  /// vez, sem percorrer cue por cue.
  void updateCaptionHighlight(
    String id,
    CaptionHighlightStyle Function(CaptionHighlightStyle) fn,
  ) {
    final layer = _layer(id);
    if (layer is! CaptionLayer) return;
    _replace(layer.copyLayer(highlight: fn(layer.highlight)));
    _push(layer);
  }

  int addCaptionLayer(List<Cue> cues) {
    if (cues.isEmpty) return 0;
    final end = cues.last.end + const Duration(milliseconds: 300);
    _push(
      CaptionLayer(
        name: 'Legendas',
        startTime: Duration.zero,
        duration: end,
        cues: cues,
        position: AnimatedOffset(
          Offset(state.outputWidth / 2, state.outputHeight * 0.88),
        ),
      ),
    );
    return cues.length;
  }

  /// Camada de legendas a partir de um SRT colado/importado.
  void addCaptionLayerFromSrt(String srt) {
    addCaptionLayer(normalizeCues(parseSrt(srt)));
  }

  /// Primeira midia com audio do projeto (fonte da transcricao).
  String? firstTranscribableMediaPath() {
    for (final l in state.layers) {
      if (l is VideoLayer) return l.sourcePath;
      if (l is AudioLayer) return l.sourcePath;
    }
    return null;
  }

  /// Corrige o TEXTO de um cue (edicao manual trava o cue: uma nova
  /// transcricao nao sobrescreve o que voce corrigiu).
  void updateCueText(String layerId, String cueId, String text) {
    final layer = _layer(layerId);
    if (layer is! CaptionLayer) return;
    _replace(
      layer.copyLayer(
        cues: [
          for (final c in layer.cues)
            c.id == cueId
                ? c.copyWith(text: wrapCaptionText(text), locked: true)
                : c,
        ],
      ),
    );
  }

  void removeCue(String layerId, String cueId) {
    final layer = _layer(layerId);
    if (layer is! CaptionLayer) return;
    _replace(
      layer.copyLayer(cues: layer.cues.where((c) => c.id != cueId).toList()),
    );
  }

  String? exportCaptionsSrt(String layerId) {
    final layer = _layer(layerId);
    if (layer is! CaptionLayer) return null;
    return serializeSrt(layer.cues);
  }

  // ------------------------------------------------ animadores de texto

  void _updateTextLayer(String id, TextLayer Function(TextLayer) fn) {
    final layer = _layer(id);
    if (layer is! TextLayer) return;
    _replace(fn(layer));
  }

  void _updateAnimator(
    String id,
    String animatorId,
    TextAnimator Function(TextAnimator) fn,
  ) {
    _updateTextLayer(id, (l) {
      return l.copyLayer(
        animators: [
          for (final a in l.animators) a.id == animatorId ? fn(a) : a,
        ],
      );
    });
  }

  /// Aplica um preset substituindo a pilha de animadores.
  void applyTextPreset(String id, TextPreset preset) {
    _updateTextLayer(id, (l) => l.copyLayer(animators: preset.build()));
  }

  /// PRECOMP: duracao interna, remapeamento de tempo, colapsar e
  /// recortar.
  void updatePrecomp(
    String id, {
    Duration? sourceDuration,
    bool clearSourceDuration = false,
    AnimatedDouble? timeRemap,
    bool clearRemap = false,
    bool? collapse,
    bool? clipToComp,
  }) {
    final layer = _layer(id);
    if (layer is! GroupLayer) return;
    _replace(
      GroupLayer(
        id: layer.id,
        name: layer.name,
        startTime: layer.startTime,
        duration: layer.duration,
        children: layer.children,
        sourceDuration: clearSourceDuration
            ? null
            : (sourceDuration ?? layer.sourceDuration),
        timeRemap: clearRemap ? null : (timeRemap ?? layer.timeRemap),
        collapse: collapse ?? layer.collapse,
        clipToComp: clipToComp ?? layer.clipToComp,
        position: layer.position,
        scaleX: layer.scaleX,
        scaleY: layer.scaleY,
        rotation: layer.rotation,
        rotationX: layer.rotationX,
        rotationY: layer.rotationY,
        opacity: layer.opacity,
        skewX: layer.skewX,
        skewY: layer.skewY,
        pivot: layer.pivot,
        blendMode: layer.blendMode,
        is3D: layer.is3D,
        positionZ: layer.positionZ,
        effects: layer.effects,
        masks: layer.masks,
        matteMode: layer.matteMode,
        matteSourceId: layer.matteSourceId,
      ),
    );
  }

  /// Liga o remapeamento com dois keyframes que reproduzem normal — a
  /// pessoa ajusta dali, em vez de comecar com a precomp congelada.
  void enablePrecompTimeRemap(String id) {
    final layer = _layer(id);
    if (layer is! GroupLayer || layer.timeRemap != null) return;
    final dur = layer.innerDuration.inMicroseconds / 1000000.0;
    updatePrecomp(
      id,
      timeRemap: AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(layer.duration, dur),
    );
  }

  /// Qual instante do conteudo aparece AGORA. Com a trilha animada,
  /// vira keyframe; sem, muda o valor fixo (congelado).
  void setPrecompContentTime(String id, Duration globalTime, double seconds) {
    final layer = _layer(id);
    if (layer is! GroupLayer) return;
    final r = layer.timeRemap ?? AnimatedDouble(0);
    final v = seconds < 0 ? 0.0 : seconds;
    updatePrecomp(id, timeRemap: r.edited(layer.localTime(globalTime), v));
  }

  /// Congela a precomp no instante que esta aparecendo agora.
  void freezePrecompAt(String id, Duration globalTime) {
    final layer = _layer(id);
    if (layer is! GroupLayer) return;
    final agora = layer.contentTimeAt(layer.localTime(globalTime));
    updatePrecomp(
      id,
      timeRemap: AnimatedDouble(agora.inMicroseconds / 1000000.0),
    );
  }

  /// Roda a precomp de tras para frente, do fim ao comeco.
  void reversePrecomp(String id) {
    final layer = _layer(id);
    if (layer is! GroupLayer) return;
    final dur = layer.innerDuration.inMicroseconds / 1000000.0;
    updatePrecomp(
      id,
      timeRemap: AnimatedDouble(0)
          .withKeyframe(Duration.zero, dur)
          .withKeyframe(layer.duration, 0),
    );
  }

  /// TEXTO EM CAMINHO: selo circular, arco, ou seguindo outra forma.
  void updateTextPath(String id, TextPathSpec Function(TextPathSpec) fn) {
    _updateTextLayer(id, (l) => l.copyLayer(textPath: fn(l.textPath)));
  }

  // ------------------------------- animacoes de texto (catalogo AM)

  /// Quantas unidades o texto tem na base desta animacao.
  int textAnimUnitCount(String id, TextAnimUnit unit) {
    final layer = _layer(id);
    if (layer is! TextLayer) return 1;
    final u = TextUnits.of(layer.text);
    return switch (unit) {
      TextAnimUnit.character => u.charCount,
      TextAnimUnit.charactersNoSpaces => u.charNoSpaceCount,
      TextAnimUnit.word => u.wordCount,
      TextAnimUnit.line => u.lineCount,
      TextAnimUnit.all => 1,
    };
  }

  /// Poe uma animacao do catalogo numa posicao. Cada posicao —
  /// entrada, enfase, saida — aceita UMA animacao, como no Alight
  /// Motion: escolher outra troca, nao empilha.
  void setTextAnim(String id, TextAnimSlot slot, String? specId) {
    _updateTextLayer(id, (l) {
      final rest = [
        for (final a in l.anims)
          if (a.slot != slot) a,
      ];
      if (specId == null) return l.copyLayer(anims: rest);
      return l.copyLayer(
        anims: [
          ...rest,
          TextAnim(specId: specId, slot: slot),
        ],
      );
    });
  }

  void updateTextAnim(
    String id,
    String animId,
    TextAnim Function(TextAnim) fn,
  ) {
    _updateTextLayer(
      id,
      (l) => l.copyLayer(
        anims: [for (final a in l.anims) a.id == animId ? fn(a) : a],
      ),
    );
  }

  void setTextAnimParam(String id, String animId, String key, double value) {
    updateTextAnim(
      id,
      animId,
      (a) => a.copyWith(params: {...a.params, key: value}),
    );
  }

  void addTextAnimator(String id) {
    _updateTextLayer(id, (l) {
      final n = l.animators.length + 1;
      return l.copyLayer(
        animators: [
          ...l.animators,
          TextAnimator(name: 'Animador $n'),
        ],
      );
    });
  }

  void removeTextAnimator(String id, String animatorId) {
    _updateTextLayer(id, (l) {
      return l.copyLayer(
        animators: [
          for (final a in l.animators)
            if (a.id != animatorId) a,
        ],
      );
    });
  }

  void toggleTextAnimator(String id, String animatorId) {
    _updateAnimator(id, animatorId, (a) => a.copyWith(enabled: !a.enabled));
  }

  /// Chip de propriedade: adiciona se falta, remove se presente.
  void toggleAnimatorPropType(String id, String animatorId, TextAnimProp type) {
    _updateAnimator(id, animatorId, (a) {
      final has = a.properties.any((p) => p.type == type);
      return a.copyWith(
        properties: has
            ? [
                for (final p in a.properties)
                  if (p.type != type) p,
              ]
            : [...a.properties, AnimatorProperty(type: type)],
      );
    });
  }

  void editAnimatorPropValue(
    String id,
    String animatorId,
    String propId,
    Duration globalTime,
    double value,
  ) {
    final layer = _layer(id);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateAnimator(id, animatorId, (a) {
      return a.copyWith(
        properties: [
          for (final p in a.properties)
            p.id == propId
                ? p.copyWith(value: p.value.edited(local, value))
                : p,
        ],
      );
    });
  }

  void toggleAnimatorPropKeyframe(
    String id,
    String animatorId,
    String propId,
    Duration globalTime,
  ) {
    final layer = _layer(id);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateAnimator(id, animatorId, (a) {
      return a.copyWith(
        properties: [
          for (final p in a.properties)
            p.id == propId
                ? p.copyWith(
                    value: p.value.hasKeyframeAt(local)
                        ? p.value.withoutKeyframe(local)
                        : p.value.withKeyframe(local, p.value.valueAt(local)),
                  )
                : p,
        ],
      );
    });
  }

  void addTextSelector(String id, String animatorId, {bool wiggly = false}) {
    _updateAnimator(id, animatorId, (a) {
      return a.copyWith(
        selectors: [
          ...a.selectors,
          if (wiggly) WigglySelector() else RangeSelector(),
        ],
      );
    });
  }

  void removeTextSelector(String id, String animatorId, String selectorId) {
    _updateAnimator(id, animatorId, (a) {
      final rest = [
        for (final s in a.selectors)
          if (s.id != selectorId) s,
      ];
      // Animador sem seletor nao seleciona nada; mantem ao menos um.
      return a.copyWith(selectors: rest.isEmpty ? [RangeSelector()] : rest);
    });
  }

  void _updateSelector(
    String id,
    String animatorId,
    String selectorId,
    TextSelector Function(TextSelector) fn,
  ) {
    _updateAnimator(id, animatorId, (a) {
      return a.copyWith(
        selectors: [
          for (final s in a.selectors) s.id == selectorId ? fn(s) : s,
        ],
      );
    });
  }

  void cycleSelectorMode(String id, String animatorId, String selectorId) {
    _updateSelector(id, animatorId, selectorId, (s) {
      final next =
          SelectorMode.values[(s.mode.index + 1) % SelectorMode.values.length];
      return switch (s) {
        RangeSelector r => r.copyWith(mode: next),
        WigglySelector w => w.copyWith(mode: next),
        // O seletor escalonado vem compilado do catalogo: seu modo nao
        // e editado a mao.
        StaggerSelector _ => s,
      };
    });
  }

  void setRangeSelectorShape(
    String id,
    String animatorId,
    String selectorId,
    SelectorShape shape,
  ) {
    _updateSelector(id, animatorId, selectorId, (s) {
      return s is RangeSelector ? s.copyWith(shape: shape) : s;
    });
  }

  /// Edita um parametro do seletor (auto-key se ja anima).
  /// Range: start, end, offset, amount. Wiggly: freq, correlation, min, max.
  void editSelectorParam(
    String id,
    String animatorId,
    String selectorId,
    String param,
    Duration globalTime,
    double value,
  ) {
    final layer = _layer(id);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateSelector(id, animatorId, selectorId, (s) {
      if (s is RangeSelector) {
        return switch (param) {
          'start' => s.copyWith(start: s.start.edited(local, value)),
          'end' => s.copyWith(end: s.end.edited(local, value)),
          'offset' => s.copyWith(offset: s.offset.edited(local, value)),
          'amount' => s.copyWith(amount: s.amount.edited(local, value)),
          _ => s,
        };
      }
      if (s is WigglySelector) {
        return switch (param) {
          'freq' => s.copyWith(
            wigglesPerSecond: s.wigglesPerSecond.edited(local, value),
          ),
          'correlation' => s.copyWith(
            correlation: s.correlation.edited(local, value),
          ),
          'min' => s.copyWith(minAmount: s.minAmount.edited(local, value)),
          'max' => s.copyWith(maxAmount: s.maxAmount.edited(local, value)),
          _ => s,
        };
      }
      return s;
    });
  }

  // ------------------------------------------------------------- conteudo

  void editTextLayer(
    String id, {
    String? text,
    double? fontSize,
    Color? color,
    String? fontFamily,
    bool clearFont = false,
  }) {
    final layer = _layer(id);
    if (layer is! TextLayer) return;
    _replace(
      layer.copyLayer(
        text: text,
        name: text ?? layer.name,
        fontSize: fontSize,
        color: color,
        fontFamily: fontFamily,
        clearFont: clearFont,
      ),
    );
    // A forma-conteiner acompanha o texto SOZINHA (PR-X14).
    _refreshResponsive(id);
  }

  // ------------------------------------------------------ forma vetorial

  void _updateShape(String id, List<ShapeItem> Function(List<ShapeItem>) fn) {
    final layer = _layer(id);
    if (layer is! ShapeLayer) return;
    _replace(layer.copyLayer(contents: fn(layer.contents)));
  }

  /// Edita somente o preenchimento escolhido, preservando a geometria.
  void updateShapeGradient(
    String id,
    String itemId,
    ShapeGradientFill Function(ShapeGradientFill) update,
  ) {
    _updateShape(
      id,
      (items) => [
        for (final item in items)
          if (item is ShapeGradientFill && item.id == itemId)
            update(item)
          else
            item,
      ],
    );
  }

  /// Primeira geometria PARAMETRICA da forma (painel de parametros).
  ShapeParametric? shapeParametricOf(String id) {
    final layer = _layer(id);
    if (layer is! ShapeLayer) return null;
    for (final item in layer.contents) {
      if (item is ShapeParametric) return item;
    }
    return null;
  }

  void _updateParametric(
    String id,
    ShapeParametric Function(ShapeParametric) fn,
  ) {
    _updateShape(id, (items) {
      var done = false;
      return [
        for (final item in items)
          if (!done && item is ShapeParametric)
            (() {
              done = true;
              return fn(item);
            })()
          else
            item,
      ];
    });
  }

  /// Edita um parametro da geometria (auto-key quando a trilha ja anima).
  void editShapeParam(
    String id,
    String key,
    Duration globalTime,
    double value,
  ) {
    final layer = _layer(id);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateParametric(id, (s) {
      final track = shapeParamTrackOf(s, key);
      if (track == null) return s;
      return shapeParamWithTrack(s, key, track.edited(local, value));
    });
  }

  void toggleShapeParamKeyframe(String id, String key, Duration globalTime) {
    final layer = _layer(id);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateParametric(id, (s) {
      final track = shapeParamTrackOf(s, key);
      if (track == null) return s;
      return shapeParamWithTrack(
        s,
        key,
        track.hasKeyframeAt(local)
            ? track.withoutKeyframe(local)
            : track.withKeyframe(local, track.valueAt(local)),
      );
    });
  }

  void setShapeParamSegmentEase(
    String id,
    String key,
    Duration segStartLocal,
    Easing ease,
  ) {
    _updateParametric(id, (s) {
      final track = shapeParamTrackOf(s, key);
      if (track == null) return s;
      return shapeParamWithTrack(s, key, track.withEase(segStartLocal, ease));
    });
  }

  void applyEaseToAllShapeParamSegments(String id, String key, Easing ease) {
    _updateParametric(id, (s) {
      final track = shapeParamTrackOf(s, key);
      if (track == null) return s;
      return shapeParamWithTrack(s, key, track.withEaseAll(ease));
    });
  }

  /// Troca o TIPO da primitiva mantendo as trilhas (rect -> star etc.).
  void setShapeParamKind(String id, ParamShapeKind kind) {
    _updateParametric(id, (s) => s.copyWith(kind: kind));
  }

  /// Alternador de unidade do arredondamento (px ou % do menor lado).
  void setShapeRoundnessUnit(String id, {required bool percent}) {
    _updateParametric(id, (s) => s.copyWith(roundnessPercent: percent));
  }

  /// Converte o primeiro caminho COZIDO em geometria parametrica
  /// equivalente (formas antigas ganham os parametros novos).
  void convertShapeToParametric(String id) {
    _updateShape(id, (items) {
      var done = false;
      return [
        for (final item in items)
          if (!done && item is ShapePath && _paramFromLegacy(item) != null)
            (() {
              done = true;
              return _paramFromLegacy(item)!;
            })()
          else
            item,
      ];
    });
  }

  ShapeParametric? _paramFromLegacy(ShapePath p) => switch (p.primitive) {
    ShapePrimitive.rectangle => ShapeParametric(
      kind: ParamShapeKind.rect,
      sizeX: AnimatedDouble(p.width),
      sizeY: AnimatedDouble(p.height),
      roundness: AnimatedDouble(0),
    ),
    ShapePrimitive.roundedRectangle => ShapeParametric(
      kind: ParamShapeKind.rect,
      sizeX: AnimatedDouble(p.width),
      sizeY: AnimatedDouble(p.height),
      roundnessPercent: false,
      roundness: AnimatedDouble(p.cornerRadius),
    ),
    ShapePrimitive.ellipse => ShapeParametric(
      kind: ParamShapeKind.ellipse,
      sizeX: AnimatedDouble(p.width),
      sizeY: AnimatedDouble(p.height),
    ),
    ShapePrimitive.polygon => ShapeParametric(
      kind: ParamShapeKind.polygon,
      points: AnimatedDouble(p.points.toDouble()),
      outerRadius: AnimatedDouble(p.width / 2),
    ),
    ShapePrimitive.star => ShapeParametric(
      kind: ParamShapeKind.star,
      points: AnimatedDouble(p.points.toDouble()),
      outerRadius: AnimatedDouble(p.width / 2),
      innerRadius: AnimatedDouble(p.width / 2 * p.innerRadiusRatio),
    ),
    ShapePrimitive.ring => ShapeParametric(
      kind: ParamShapeKind.sector,
      outerRadius: AnimatedDouble(p.width / 2),
      sectorInner: AnimatedDouble((p.width / 2 - p.thickness).clamp(0, 1e9)),
      sweep: AnimatedDouble(360),
    ),
    ShapePrimitive.arc => ShapeParametric(
      kind: ParamShapeKind.sector,
      outerRadius: AnimatedDouble(p.width / 2),
      sectorInner: AnimatedDouble((p.width / 2 - p.thickness).clamp(0, 1e9)),
      startAngle: AnimatedDouble(p.startAngle),
      sweep: AnimatedDouble(p.sweepAngle),
    ),
    _ => null,
  };

  /// Troca a cor do primeiro fill/stroke (painel Cor e preenchimento).
  void setShapePrimaryColor(String id, Color color) {
    _updateShape(id, (items) {
      var done = false;
      return [
        for (final item in items)
          if (!done && item is ShapeFill)
            (() {
              done = true;
              return item.copyWith(color: color);
            })()
          else if (!done && item is ShapeStroke)
            (() {
              done = true;
              return item.copyWith(color: color);
            })()
          else
            item,
      ];
    });
  }

  /// Adiciona um operador ao FIM da lista (afeta tudo que veio antes).
  void addShapeOperator(String id, {required bool repeater}) {
    _updateShape(id, (items) {
      // Operador entra antes das pinturas para afetar os caminhos.
      final paintIdx = items.indexWhere(
        (i) => i is ShapeFill || i is ShapeStroke || i is ShapeGradientFill,
      );
      final op = repeater
          ? RepeaterOperator()
          : TrimOperator(end: AnimatedDouble(0.6));
      final out = [...items];
      out.insert(paintIdx < 0 ? out.length : paintIdx, op);
      return out;
    });
  }

  /// Os operadores de caminho que entram pelo menu.
  ///
  /// Entram ANTES das pinturas, como no AE: operador mexe no caminho, e
  /// o traco pintado depois sai com a espessura certa.
  void addPathOperator(String id, ShapePathOp kind) {
    _updateShape(id, (items) {
      final paintIdx = items.indexWhere(
        (i) => i is ShapeFill || i is ShapeStroke || i is ShapeGradientFill,
      );
      final op = switch (kind) {
        ShapePathOp.offset => OffsetPathOperator(),
        ShapePathOp.roundCorners => RoundCornersOperator(),
        ShapePathOp.zigZag => ZigZagOperator(),
        ShapePathOp.puckerBloat => PuckerBloatOperator(
          amount: AnimatedDouble(0.4),
        ),
        ShapePathOp.twist => TwistOperator(),
        ShapePathOp.wiggle => WigglePathOperator(),
        ShapePathOp.merge => MergePathsOperator(),
      };
      final out = [...items];
      out.insert(paintIdx < 0 ? out.length : paintIdx, op);
      return out;
    });
  }

  /// Acrescenta uma segunda geometria na mesma ShapeLayer, antes do
  /// Merge Paths e das pinturas. Assim uniao/subtracao/interseccao sao
  /// utilizaveis no fluxo comum sem precisar importar um SVG composto.
  void addCompoundShapeGeometry(String id, ParamShapeKind kind) {
    final geometry = switch (kind) {
      ParamShapeKind.rect => ShapeParametric(
        kind: ParamShapeKind.rect,
        sizeX: AnimatedDouble(190),
        sizeY: AnimatedDouble(190),
        roundness: AnimatedDouble(12),
      ),
      ParamShapeKind.ellipse => ShapeParametric(
        kind: ParamShapeKind.ellipse,
        sizeX: AnimatedDouble(230),
        sizeY: AnimatedDouble(230),
      ),
      ParamShapeKind.polygon => ShapeParametric(
        kind: ParamShapeKind.polygon,
        points: AnimatedDouble(6),
        outerRadius: AnimatedDouble(120),
      ),
      ParamShapeKind.star => ShapeParametric(
        kind: ParamShapeKind.star,
        points: AnimatedDouble(5),
        outerRadius: AnimatedDouble(125),
        innerRadius: AnimatedDouble(62),
      ),
      ParamShapeKind.sector => ShapeParametric(
        kind: ParamShapeKind.sector,
        outerRadius: AnimatedDouble(125),
        sweep: AnimatedDouble(270),
      ),
    };
    _updateShape(id, (items) {
      final insertAt = items.indexWhere(
        (item) =>
            item is MergePathsOperator ||
            item is ShapeFill ||
            item is ShapeStroke ||
            item is ShapeGradientFill,
      );
      final out = [...items];
      out.insert(insertAt < 0 ? out.length : insertAt, geometry);
      return out;
    });
  }

  /// Edita o valor principal de um operador de caminho, com keyframe
  /// automatico quando a propriedade ja anima.
  void editPathOperator(
    String id,
    String itemId,
    Duration local,
    double value,
  ) {
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i.id != itemId)
            i
          else
            switch (i) {
              OffsetPathOperator o => o.copyWith(
                amount: o.amount.edited(local, value),
              ),
              RoundCornersOperator r => r.copyWith(
                radius: r.radius.edited(local, value),
              ),
              ZigZagOperator z => z.copyWith(
                amplitude: z.amplitude.edited(local, value),
              ),
              PuckerBloatOperator pb => pb.copyWith(
                amount: pb.amount.edited(local, value),
              ),
              TwistOperator tw => tw.copyWith(
                angle: tw.angle.edited(local, value),
              ),
              WigglePathOperator w => w.copyWith(
                amount: w.amount.edited(local, value),
              ),
              _ => i,
            },
      ],
    );
  }

  void cycleMergeMode(String id, String itemId) {
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i is MergePathsOperator && i.id == itemId)
            i.copyWith(
              mode: MergeMode
                  .values[(i.mode.index + 1) % MergeMode.values.length],
            )
          else
            i,
      ],
    );
  }

  /// Transforma a forma num MORPH: a primeira ShapePath (ou o destino do
  /// morph atual) vira a origem, e [target] o destino. O progresso (0..1)
  /// anima por keyframe como qualquer propriedade.
  void convertShapeToMorph(String id, ShapePath target) {
    _updateShape(id, (items) {
      final idx = items.indexWhere((i) => i is ShapePath || i is ShapeMorph);
      if (idx < 0) return items;
      final out = [...items];
      final current = out[idx];
      final fromPath = switch (current) {
        ShapePath p => p,
        ShapeMorph m => m.to,
        _ => null,
      };
      if (fromPath == null) return items;
      out[idx] = ShapeMorph(from: fromPath, to: target);
      return out;
    });
  }

  // ------------------------------------------------- caminho editavel

  /// CONVERTE um item de geometria em caminho bezier editavel, mantendo
  /// o id (a selecao continua valendo). Primitivas com formula fechada
  /// viram nos exatos; o resto e amostrado. Devolve false se o item nao
  /// e geometria.
  bool convertShapeItemToBezier(
    String layerId,
    String itemId,
    Duration globalTime,
  ) {
    final layer = _layer(layerId);
    if (layer is! ShapeLayer) return false;
    final local = layer.localTime(globalTime);
    var ok = false;
    _updateShape(
      layerId,
      (items) => [
        for (final i in items)
          if (i.id != itemId || i is ShapeBezier)
            i
          else
            switch (bezierOfShapeItem(i, local)) {
              null => i,
              final BezierPath b => () {
                ok = true;
                return ShapeBezier(id: i.id, path: AnimatedPath(b));
              }(),
            },
      ],
    );
    return ok;
  }

  ShapeBezier? shapeBezierOf(String layerId, String itemId) {
    final layer = _layer(layerId);
    if (layer is! ShapeLayer) return null;
    for (final i in layer.contents) {
      if (i.id == itemId && i is ShapeBezier) return i;
    }
    return null;
  }

  /// O primeiro caminho bezier da forma, ou nulo.
  ShapeBezier? firstShapeBezier(String layerId) {
    final layer = _layer(layerId);
    if (layer is! ShapeLayer) return null;
    for (final i in layer.contents) {
      if (i is ShapeBezier) return i;
    }
    return null;
  }

  /// EDITAR OS NOS do caminho da forma. Com o caminho ja animado, vira
  /// keyframe no tempo atual; sem, muda a forma base. E o mesmo
  /// contrato de [editMaskPath] — o editor de nos nao sabe a diferenca.
  void editShapeBezier(
    String layerId,
    String itemId,
    Duration globalTime,
    BezierPath Function(BezierPath) fn,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateShape(
      layerId,
      (items) => [
        for (final i in items)
          if (i.id == itemId && i is ShapeBezier)
            i.copyWith(path: i.path.edited(local, fn(i.path.valueAt(local))))
          else
            i,
      ],
    );
  }

  /// Keyframe do CAMINHO da forma no tempo atual.
  void toggleShapeBezierKeyframe(
    String layerId,
    String itemId,
    Duration globalTime,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateShape(
      layerId,
      (items) => [
        for (final i in items)
          if (i.id == itemId && i is ShapeBezier)
            i.copyWith(
              path: i.path.hasKeyframeAt(local)
                  ? i.path.withoutKeyframe(local)
                  : i.path.withKeyframe(local, i.path.valueAt(local)),
            )
          else
            i,
      ],
    );
  }

  /// Desfaz o morph mantendo a forma de ORIGEM.
  void removeMorph(String id, String itemId) {
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i.id == itemId && i is ShapeMorph) i.from else i,
      ],
    );
  }

  void editMorphProgress(
    String id,
    String itemId,
    Duration globalTime,
    double value,
  ) {
    final layer = _layer(id);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i.id == itemId && i is ShapeMorph)
            i.copyWith(progress: i.progress.edited(local, value))
          else
            i,
      ],
    );
  }

  void toggleMorphKeyframe(String id, String itemId, Duration globalTime) {
    final layer = _layer(id);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i.id == itemId && i is ShapeMorph)
            i.copyWith(
              progress: i.progress.hasKeyframeAt(local)
                  ? i.progress.withoutKeyframe(local)
                  : i.progress.withKeyframe(local, i.progress.valueAt(local)),
            )
          else
            i,
      ],
    );
  }

  // ------------------------------------------------------------------
  // NIVEL 1 (shapes no modelo AM): trilhas dos ITENS da forma — o
  // Drawing Progress (Trim: inicio/fim/deslocamento) e o deslocamento do
  // tracejado do traco — com keyframe, curva e edicao no tempo, como
  // qualquer propriedade.

  /// Trilha animavel de um item da forma pelo nome.
  static AnimatedDouble? shapeItemTrack(ShapeItem item, String key) =>
      switch (item) {
        TrimOperator t => switch (key) {
          'start' => t.start,
          'end' => t.end,
          'offset' => t.offset,
          _ => null,
        },
        ShapeStroke s => switch (key) {
          'width' => s.width,
          'opacity' => s.opacity,
          'dashLength' => s.dashLength,
          'gapLength' => s.gapLength,
          'dashOffset' => s.dashOffset,
          _ => null,
        },
        _ => null,
      };

  static ShapeItem _shapeItemWithTrack(
    ShapeItem item,
    String key,
    AnimatedDouble v,
  ) => switch (item) {
    TrimOperator t => switch (key) {
      'start' => t.copyWith(start: v),
      'end' => t.copyWith(end: v),
      'offset' => t.copyWith(offset: v),
      _ => t,
    },
    ShapeStroke s => switch (key) {
      'width' => s.copyWith(width: v),
      'opacity' => s.copyWith(opacity: v),
      'dashLength' => s.copyWith(dashLength: v),
      'gapLength' => s.copyWith(gapLength: v),
      'dashOffset' => s.copyWith(dashOffset: v),
      _ => s,
    },
    _ => item,
  };

  void _updateShapeItemTrack(
    String id,
    String itemId,
    String key,
    AnimatedDouble Function(AnimatedDouble) fn,
  ) {
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i.id == itemId && shapeItemTrack(i, key) != null)
            _shapeItemWithTrack(i, key, fn(shapeItemTrack(i, key)!))
          else
            i,
      ],
    );
  }

  void editShapeItemTrack(
    String id,
    String itemId,
    String key,
    Duration globalTime,
    double value,
  ) {
    final layer = _layer(id);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateShapeItemTrack(id, itemId, key, (t) => t.edited(local, value));
  }

  void toggleShapeItemTrackKeyframe(
    String id,
    String itemId,
    String key,
    Duration globalTime,
  ) {
    final layer = _layer(id);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateShapeItemTrack(
      id,
      itemId,
      key,
      (t) => t.hasKeyframeAt(local)
          ? t.withoutKeyframe(local)
          : t.withKeyframe(local, t.valueAt(local)),
    );
  }

  void setShapeItemTrackSegmentEase(
    String id,
    String itemId,
    String key,
    Duration segStartLocal,
    Easing ease,
  ) => _updateShapeItemTrack(
    id,
    itemId,
    key,
    (t) => t.withEase(segStartLocal, ease),
  );

  void applyEaseToAllShapeItemTrackSegments(
    String id,
    String itemId,
    String key,
    Easing ease,
  ) => _updateShapeItemTrack(id, itemId, key, (t) => t.withEaseAll(ease));

  /// O traco da forma (Border & Shadow): garante um e devolve o id.
  String? ensureShapeStroke(
    String id, {
    double width = 10,
    Color color = const Color(0xFFFFFFFF),
  }) {
    final layer = _layer(id);
    if (layer is! ShapeLayer) return null;
    for (final i in layer.contents) {
      if (i is ShapeStroke) return i.id;
    }
    final s = ShapeStroke(color: color, width: AnimatedDouble(width));
    _updateShape(id, (items) => [...items, s]);
    return s.id;
  }

  void updateShapeStroke(String id, ShapeStroke Function(ShapeStroke) fn) {
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i is ShapeStroke) fn(i) else i,
      ],
    );
  }

  void removeShapeStroke(String id) {
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i is! ShapeStroke) i,
      ],
    );
  }

  /// DRAWING PROGRESS (o Trim Paths da AM): garante um na forma, antes
  /// da pintura, e devolve o id.
  String? ensureShapeTrim(String id) {
    final layer = _layer(id);
    if (layer is! ShapeLayer) return null;
    for (final i in layer.contents) {
      if (i is TrimOperator) return i.id;
    }
    final op = TrimOperator();
    _updateShape(id, (items) {
      final paintIdx = items.indexWhere(
        (i) => i is ShapeFill || i is ShapeStroke || i is ShapeGradientFill,
      );
      final out = [...items];
      out.insert(paintIdx < 0 ? out.length : paintIdx, op);
      return out;
    });
    return op.id;
  }

  void removeShapeTrim(String id) {
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i is! TrimOperator) i,
      ],
    );
  }

  /// A geometria da forma como caminho bezier (converte a parametrica
  /// ou primitiva se preciso) e o id do item — a porta do Edit Points.
  String? ensureShapeBezierGeometry(String id, Duration globalTime) {
    final layer = _layer(id);
    if (layer is! ShapeLayer) return null;
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
      // Forma vazia (desenho vetorial recem-criado): nasce um caminho.
      final b = ShapeBezier(
        path: AnimatedPath(BezierPath(vertices: const [], closed: false)),
      );
      _updateShape(id, (items) => [b, ...items]);
      return b.id;
    }
    if (geo is ShapeBezier) return geo.id;
    return convertShapeItemToBezier(id, geo.id, globalTime) ? geo.id : null;
  }

  void removeShapeItem(String id, String itemId) {
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i.id != itemId) i,
      ],
    );
  }

  /// Edita parametro de Trim (start/end/offset) com auto-keyframe.
  void editTrim(
    String id,
    String itemId,
    String param,
    Duration globalTime,
    double value,
  ) {
    final layer = _layer(id);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i.id == itemId && i is TrimOperator)
            switch (param) {
              'start' => i.copyWith(start: i.start.edited(local, value)),
              'end' => i.copyWith(end: i.end.edited(local, value)),
              'offset' => i.copyWith(offset: i.offset.edited(local, value)),
              _ => i,
            }
          else
            i,
      ],
    );
  }

  void editRepeater(
    String id,
    String itemId,
    Duration globalTime, {
    int? copies,
    double? dx,
    double? dy,
    double? rotationDeg,
  }) {
    final layer = _layer(id);
    if (layer == null) return;
    final local = layer.localTime(globalTime);
    _updateShape(
      id,
      (items) => [
        for (final i in items)
          if (i.id == itemId && i is RepeaterOperator)
            i.copyWith(
              copies: copies,
              dx: dx,
              dy: dy,
              rotation: rotationDeg == null
                  ? null
                  : i.rotation.edited(local, rotationDeg),
            )
          else
            i,
      ],
    );
  }

  // ------------------------------------------------------------ precomp/3D

  /// Agrupa VARIAS camadas num precomp: o grupo cobre do inicio mais cedo
  /// ao fim mais tarde; os filhos entram com tempo local ao grupo, na
  /// mesma ordem de empilhamento da composicao.
  void groupLayers(List<String> ids) {
    final picked = [
      for (final l in state.layers)
        if (ids.contains(l.id)) l,
    ];
    if (picked.isEmpty) return;
    if (picked.length == 1) {
      groupLayer(picked.first.id);
      return;
    }
    var start = picked.first.startTime;
    var end = picked.first.endTime;
    for (final l in picked) {
      if (l.startTime < start) start = l.startTime;
      if (l.endTime > end) end = l.endTime;
    }
    final group = GroupLayer(
      name: 'Grupo',
      startTime: start,
      duration: end - start,
      position: AnimatedOffset(_center),
      children: [
        for (final l in picked) l.copyLayer(startTime: l.startTime - start),
      ],
    );
    final layers = <Layer>[];
    var placed = false;
    for (final l in state.layers) {
      if (ids.contains(l.id)) {
        if (!placed) {
          layers.add(group);
          placed = true;
        }
      } else {
        layers.add(l);
      }
    }
    _mutate(state.copyWith(layers: layers));
    ref.read(selectedLayerProvider.notifier).state = group.id;
  }

  /// Agrupa a camada num precomp (tempo dos filhos vira local ao grupo).
  void groupLayer(String id) {
    final layer = _layer(id);
    if (layer == null || layer is GroupLayer) return;
    final group = GroupLayer(
      name: 'Grupo',
      startTime: layer.startTime,
      duration: layer.duration,
      position: AnimatedOffset(_center),
      children: [layer.copyLayer(startTime: Duration.zero)],
    );
    _mutate(
      state.copyWith(
        layers: [
          for (final l in state.layers)
            if (l.id == id) group else l,
        ],
      ),
    );
    ref.read(selectedLayerProvider.notifier).state = group.id;
  }

  /// Desfaz o grupo devolvendo os filhos com tempo absoluto.
  void ungroupLayer(String id) {
    final layer = _layer(id);
    if (layer is! GroupLayer) return;
    final children = [
      for (final c in layer.children)
        c.copyLayer(startTime: layer.startTime + c.startTime),
    ];
    final layers = <Layer>[];
    for (final l in state.layers) {
      if (l.id == id) {
        layers.addAll(children);
      } else {
        layers.add(l);
      }
    }
    _mutate(state.copyWith(layers: layers));
    ref.read(selectedLayerProvider.notifier).state = children.isEmpty
        ? null
        : children.first.id;
  }

  /// MOTION BLUR REAL na camada: liga a amostragem por quadro desta
  /// camada E o interruptor da composicao — sem os dois juntos o botao
  /// parecia nao fazer nada.
  void toggleLayerMotionBlurReal(String id) {
    final ligado = state.metaOf(id).motionBlur;
    _updateMeta(id, (m) => m.copyWith(motionBlur: !ligado));
    if (!ligado && !state.motionBlur.enabled) {
      _mutate(
        state.copyWith(motionBlur: state.motionBlur.copyWith(enabled: true)),
      );
    }
  }

  /// EXTRUDE 3D da camada: espessura em px (0 desliga).
  void setLayerExtrude(String id, double depth) =>
      _updateMeta(id, (m) => m.copyWith(extrude: depth.clamp(0.0, 400.0)));

  /// Cor EXTRA de um efeito (gradiente de quatro cores e afins).
  void setEffectExtraColor(
    String layerId,
    String effectId,
    int index,
    Color color,
  ) {
    final layer = _layer(layerId);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        effects: [
          for (final e in layer.effects)
            e.id == effectId ? e.withExtraColor(index, color) : e,
        ],
      ),
    );
  }

  void toggle3D(String id) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(layer.copyLayer(is3D: !layer.is3D));
  }

  void editPositionZ(String id, Duration globalTime, double value) {
    final layer = _layer(id);
    if (layer == null) return;
    _replace(
      layer.copyLayer(
        positionZ: layer.positionZ.edited(layer.localTime(globalTime), value),
      ),
    );
  }

  // ------------------------------------------------------------- pickwhip

  /// Escalona a selecao inteira em um unico passo de undo. Os keyframes
  /// continuam pertencendo a cada camada e, por isso, seguem editaveis.
  void cascadeSelection(
    Iterable<String> ids, {
    Duration interval = const Duration(milliseconds: 40),
    CascadeOrder order = CascadeOrder.start,
    Easing? ease,
  }) {
    final selected = ids.toSet();
    if (selected.length < 2 || interval <= Duration.zero) return;
    _mutate(
      state.copyWith(
        layers: cascadeLayerKeyframes(
          state.layers,
          selected,
          interval: interval,
          order: order,
          ease: ease,
        ),
      ),
    );
  }

  /// Variante avancada da cascata: a primeira camada vira a fonte e as
  /// demais recebem o mesmo vinculo com atraso incremental. Os offsets
  /// sao capturados no instante atrasado para a composicao nao saltar.
  void linkCascadeSelection(
    Iterable<String> ids,
    Duration globalTime, {
    Duration interval = const Duration(milliseconds: 40),
    CascadeOrder order = CascadeOrder.start,
    required Easing ease,
    required LayerProp property,
  }) {
    if (property != LayerProp.position &&
        property != LayerProp.scale &&
        property != LayerProp.rotation &&
        property != LayerProp.opacity) {
      return;
    }
    final ordered = orderedCascadeLayers(
      state.layers,
      ids.toSet(),
      order: order,
    );
    if (ordered.length < 2) return;

    final source = ordered.first;
    final orderedIds = {for (final layer in ordered) layer.id};

    // O vinculo amostra a trilha real da fonte; gravar a curva escolhida
    // nessas trilhas faz todos herdarem o mesmo easing sem um efeito
    // procedural escondido. Mantemos tambem a curva nas trilhas dos alvos
    // para que ela continue editavel (e sobreviva a um futuro desvinculo).
    Layer withSharedEase(Layer layer) => switch (property) {
      LayerProp.position => layer.copyLayer(
        position: layer.position.withEaseAll(ease),
      ),
      LayerProp.scale => layer.copyLayer(
        scaleX: layer.scaleX.withEaseAll(ease),
        scaleY: layer.scaleY.withEaseAll(ease),
      ),
      LayerProp.rotation => layer.copyLayer(
        rotation: layer.rotation.withEaseAll(ease),
      ),
      LayerProp.opacity => layer.copyLayer(
        opacity: layer.opacity.withEaseAll(ease),
      ),
      LayerProp.skew || LayerProp.pivot || LayerProp.parent => layer,
    };

    // O offset e calculado contra a fonte JA com a curva nova. Assim a
    // propriedade visivel do alvo nao salta no instante em que o usuario
    // confirma a cascata.
    final sourceWithEase = withSharedEase(source);
    final links = <PropertyLink>[
      for (final link in state.links)
        if (!(ordered.skip(1).any((layer) => layer.id == link.targetLayerId) &&
            link.targetProp == property))
          link,
    ];

    for (var i = 1; i < ordered.length; i++) {
      final target = ordered[i];
      final delay = interval * i;
      final sourceTime = globalTime - delay;
      final targetLocal = target.localTime(globalTime);
      final sourceLocal = sourceWithEase.localTime(sourceTime);
      var offsetX = 0.0;
      var offsetY = 0.0;
      switch (property) {
        case LayerProp.position:
          final delta =
              target.position.valueAt(targetLocal) -
              sourceWithEase.position.valueAt(sourceLocal);
          offsetX = delta.dx;
          offsetY = delta.dy;
        case LayerProp.rotation:
          offsetX =
              target.rotation.valueAt(targetLocal) -
              sourceWithEase.rotation.valueAt(sourceLocal);
        case LayerProp.opacity:
          offsetX =
              target.opacity.valueAt(targetLocal) -
              sourceWithEase.opacity.valueAt(sourceLocal);
        case LayerProp.scale:
          final sourceScale = sourceWithEase.scaleX.valueAt(sourceLocal);
          offsetX = sourceScale.abs() < 1e-9
              ? 1
              : target.scaleX.valueAt(targetLocal) / sourceScale;
        case LayerProp.skew:
        case LayerProp.pivot:
        case LayerProp.parent:
          return;
      }
      links.add(
        PropertyLink(
          targetLayerId: target.id,
          targetProp: property,
          sourceLayerId: source.id,
          offsetX: offsetX,
          offsetY: offsetY,
          delay: delay,
        ),
      );
    }

    _mutate(
      state.copyWith(
        layers: [
          for (final layer in state.layers)
            if (orderedIds.contains(layer.id)) withSharedEase(layer) else layer,
        ],
        links: links,
      ),
    );
  }

  /// Vincula a propriedade da camada alvo a MESMA propriedade da fonte,
  /// capturando o offset do instante (a relacao espacial nao pula).
  void linkProperty(
    String targetId,
    LayerProp prop,
    String sourceId,
    Duration globalTime,
  ) {
    final target = _layer(targetId);
    final source = _layer(sourceId);
    if (target == null || source == null || targetId == sourceId) return;
    final tLocal = target.localTime(globalTime);
    final sLocal = source.localTime(globalTime);

    double ox = 0, oy = 0;
    switch (prop) {
      case LayerProp.position:
        final d =
            target.position.valueAt(tLocal) - source.position.valueAt(sLocal);
        ox = d.dx;
        oy = d.dy;
      case LayerProp.rotation:
        ox = target.rotation.valueAt(tLocal) - source.rotation.valueAt(sLocal);
      case LayerProp.opacity:
        ox = target.opacity.valueAt(tLocal) - source.opacity.valueAt(sLocal);
      case LayerProp.scale:
        final s = source.scaleX.valueAt(sLocal);
        ox = s == 0 ? 1 : target.scaleX.valueAt(tLocal) / s;
      case LayerProp.parent:
        // Parenting: captura o transform EFETIVO do pai (a cadeia dele ja
        // resolvida — o pai pode estar linkado a outro nulo) no instante
        // do vinculo; o filho segue o delta (nada pula ao parear).
        final pe = effectiveTransform(state, source, globalTime);
        final links = [
          for (final l in state.links)
            if (!(l.targetLayerId == targetId &&
                l.targetProp == LayerProp.parent))
              l,
          PropertyLink(
            targetLayerId: targetId,
            targetProp: LayerProp.parent,
            sourceLayerId: sourceId,
            offsetX: pe.pos.dx,
            offsetY: pe.pos.dy,
            baseRotation: pe.rot,
            baseScale: pe.scale,
            baseRotationX: pe.rotX,
            baseRotationY: pe.rotY,
            baseZ: pe.z,
          ),
        ];
        _mutate(state.copyWith(links: links));
        return;
      case LayerProp.skew:
      case LayerProp.pivot:
        return; // sem vinculo para estes por enquanto
    }

    final links = [
      for (final l in state.links)
        if (!(l.targetLayerId == targetId && l.targetProp == prop)) l,
      PropertyLink(
        targetLayerId: targetId,
        targetProp: prop,
        sourceLayerId: sourceId,
        offsetX: ox,
        offsetY: oy,
      ),
    ];
    _mutate(state.copyWith(links: links));
  }

  void unlinkProperty(String targetId, LayerProp prop) {
    _mutate(
      state.copyWith(
        links: [
          for (final l in state.links)
            if (!(l.targetLayerId == targetId && l.targetProp == prop)) l,
        ],
      ),
    );
  }

  void editVideoVolume(String id, double volume) {
    final layer = _layer(id);
    if (layer is! VideoLayer) return;
    _replace(layer.copyLayer(volume: volume.clamp(0, 1)));
  }

  Future<Duration> _probeDuration(String path) async {
    final probe = VideoPlayerController.file(File(path));
    try {
      await probe.initialize();
      return probe.value.duration;
    } catch (_) {
      return const Duration(seconds: 5);
    } finally {
      await probe.dispose();
    }
  }
}

final editorControllerProvider =
    NotifierProvider<EditorController, VideoProject>(EditorController.new);
