import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/texture_cache.dart';
import '../../application/playback_controller.dart';
import '../../application/editor_controller.dart';
import '../../domain/camera3d.dart';
import '../../domain/layer.dart';
import '../../domain/scene3d.dart';
import '../../domain/scene_motion.dart';
import '../../domain/keyframe.dart';
import '../../domain/camera_cuts.dart';
import '../../application/scene3d_gpu.dart';
import '../../application/renderer3d/filament_renderer.dart' show filamentPreviewEnabled;
import '../widgets/scene3d_painter.dart';
import '../widgets/scene3d_gpu_view.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'scene3d_sheet.dart';

/// ESTUDIO DA CENA 3D: onde a navegacao por toque acontece sem brigar
/// com os gestos do compositor.
///
/// O conflito que a spec manda resolver (camera §2.1): um dedo pode
/// significar mover o objeto ou girar a camera. A regra e:
///
///   dedo sobre o objeto selecionado  -> move o objeto
///   dedo sobre outro objeto          -> seleciona ele
///   dedo em area vazia               -> ORBITA a camera
///
/// mais o botao persistente de modo navegacao, para quando a cena esta
/// cheia e nao sobra area vazia.
Future<void> openScene3DStudio(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  await Navigator.of(context).push(
    MaterialPageRoute<void>(
      fullscreenDialog: true,
      builder: (_) => Scene3DStudio(layerId: layerId),
    ),
  );
}

class Scene3DStudio extends ConsumerStatefulWidget {
  const Scene3DStudio({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<Scene3DStudio> createState() => _Scene3DStudioState();
}

class _Scene3DStudioState extends ConsumerState<Scene3DStudio>
    with SingleTickerProviderStateMixin {
  late final PlaybackController _playback;
  Duration get _time => _playback.time.value;
  bool _autoKey = true;
  int _transformTool = 0;

  @override
  void initState() {
    super.initState();
    _playback = PlaybackController(
      vsync: this,
      durationOf: () => _layer?.duration ?? Duration.zero,
    );
    _playback.compositionFps = ref.read(editorControllerProvider).fps;
    _playback.loop.value = true;
    _playback.time.addListener(_clockChanged);
    _playback.playing.addListener(_clockChanged);
  }

  void _clockChanged() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    _playback.time.removeListener(_clockChanged);
    _playback.playing.removeListener(_clockChanged);
    _playback.pause();
    _playback.dispose();
    super.dispose();
  }

  Camera3D _activeCamera(Scene3DLayer layer) {
    final shots = sortedShots(layer.shots);
    final shot = shotAt(shots, _time) ?? shots.firstOrNull;
    return layer.allCameras.where((c) => c.id == shot?.cameraId).firstOrNull ??
        layer.camera;
  }

  void _setCamera(Camera3D camera) {
    _controller.updateSceneCameraById(widget.layerId, camera);
  }

  void _editCamera(Camera3D Function(Camera3D) edit) {
    final layer = _layer;
    if (layer == null) return;
    _setCamera(
      editCameraMotion(_activeCamera(layer), _time, edit, autoKey: _autoKey),
    );
  }

  SceneView _view = SceneView.camera;
  String? _selected;
  bool _navigationMode = false;
  bool _showMiniView = true;

  /// Vista LIVRE navegada aqui dentro: e ela que o comando "alinhar
  /// camera a vista" compromete com a camera de verdade.
  Vec3 _freePos = const Vec3(700, 500, 900);
  Vec3 _freeTarget = Vec3.zero;

  /// Deslocamento e escala das vistas ortograficas.
  Vec3 _orthoCenter = Vec3.zero;
  double _orthoScale = 0.35;

  // Estado do gesto.
  Vec3? _pivot;
  Offset _lastFocal = Offset.zero;
  double _lastScale = 1;
  int _pointers = 0;
  bool _gestureActive = false;
  TouchIntent? _gestureIntent;

  // Mini-vista arrastavel e redimensionavel.
  Offset _miniPos = const Offset(-1, -1);
  double _miniSize = 130;
  SceneView _miniView = SceneView.top;

  Scene3DLayer? get _layer {
    final l = ref.read(editorControllerProvider).layerById(widget.layerId);
    return l is Scene3DLayer ? l : null;
  }

  EditorController get _controller =>
      ref.read(editorControllerProvider.notifier);

  bool get _freeView =>
      _view == SceneView.custom1 || _view == SceneView.custom2;

  RenderCamera _renderCamera(Scene3DLayer layer) {
    if (_view == SceneView.camera) return layer.cameraAt(_time);
    if (_freeView) {
      return RenderCamera(
        position: _freePos,
        target: _freeTarget,
        focalLength: _activeCamera(layer).focalLength.base,
        filmWidth: _activeCamera(layer).filmWidth,
      );
    }
    return orthoViewCamera(_view, scale: _orthoScale, center: _orthoCenter);
  }

  // ------------------------------------------------------- gestos

  /// MODO RASCUNHO durante o gesto (camera §8): navega liso, e o
  /// resultado bom volta ao soltar.
  void _beginGesture() {
    if (_gestureActive) return;
    _playback.pause();
    setState(() => _gestureActive = true);
  }

  void _endGesture() {
    if (!_gestureActive) return;
    setState(() {
      _gestureActive = false;
      _gestureIntent = null;
      _pivot = null;
    });
  }

  /// PIVO fixado no INICIO do gesto — trocar no meio e o que mais
  /// atrapalha.
  Vec3 _resolvePivot(Scene3DLayer layer) {
    if (_pivot != null) return _pivot!;
    final sel = layer.scene.nodes.where((n) => n.id == _selected).firstOrNull;
    if (sel != null) {
      return _pivot = resolveNodeTransform(layer.scene, sel, _time).position;
    }
    if (_view == SceneView.camera &&
        _activeCamera(layer).kind == CameraKind.twoNode) {
      return _pivot = _activeCamera(layer).pointOfInterestAt(_time);
    }
    if (_freeView) return _pivot = _freeTarget;
    return _pivot = sceneBounds(layer.scene, _time).center;
  }

  void _orbit(Scene3DLayer layer, Offset delta) {
    final pivot = _resolvePivot(layer);
    final yaw = -delta.dx * 0.35;
    final pitch = delta.dy * 0.28;
    if (_view == SceneView.camera) {
      _editCamera((c) => orbitCamera(c, pivot, yaw, pitch, _time));
      return;
    }
    if (_freeView) {
      setState(() {
        final rel = _freePos - pivot;
        final radius = rel.length;
        var a = math.atan2(rel.x, rel.z) + yaw * math.pi / 180;
        var p =
            math.asin((rel.y / math.max(1e-6, radius)).clamp(-1.0, 1.0)) +
            pitch * math.pi / 180;
        p = p.clamp(-math.pi / 2 + 0.02, math.pi / 2 - 0.02);
        _freePos = Vec3(
          pivot.x + radius * math.cos(p) * math.sin(a),
          pivot.y + radius * math.sin(p),
          pivot.z + radius * math.cos(p) * math.cos(a),
        );
        _freeTarget = pivot;
      });
      return;
    }
    // Nas vistas ortograficas um dedo desloca, nao gira: girar
    // destruiria justamente o que elas servem para mostrar.
    _panOrtho(delta);
  }

  void _pan(Scene3DLayer layer, Offset delta) {
    if (_view == SceneView.camera) {
      _editCamera((c) => panCamera(c, delta, _time));
      return;
    }
    if (_freeView) {
      final rc = _renderCamera(layer);
      final basis = cameraBasis(rc);
      final shift = basis.right * (-delta.dx) + basis.up * delta.dy;
      setState(() {
        _freePos = _freePos + shift;
        _freeTarget = _freeTarget + shift;
      });
      return;
    }
    _panOrtho(delta);
  }

  void _panOrtho(Offset delta) {
    final rc = orthoViewCamera(_view, scale: _orthoScale, center: _orthoCenter);
    final basis = cameraBasis(rc);
    final shift =
        basis.right * (-delta.dx / _orthoScale) +
        basis.up * (delta.dy / _orthoScale);
    setState(() => _orthoCenter = _orthoCenter + shift);
  }

  /// PINCA move a camera no Z e NAO altera a distancia focal —
  /// aproximar muda a perspectiva, o zoom muda a lente.
  void _dolly(Scene3DLayer layer, double factor) {
    if (factor <= 0) return;
    if (_view == SceneView.camera) {
      _editCamera((c) => dollyCamera(c, factor, _time));
      return;
    }
    if (_freeView) {
      setState(() {
        final rel = _freePos - _freeTarget;
        _freePos = _freeTarget + rel * (1 / factor.clamp(0.2, 5.0));
      });
      return;
    }
    setState(() => _orthoScale = (_orthoScale * factor).clamp(0.02, 6.0));
  }

  void _handleTap(Scene3DLayer layer, Offset local, Size size) {
    final frame = renderScene(layer.scene, _renderCamera(layer), size, _time);
    final hit = pickNodeAt(frame, local);
    if (_navigationMode) return;
    setState(() => _selected = hit);
  }

  void _handleDoubleTap(Scene3DLayer layer, Offset local, Size size) {
    final frame = renderScene(layer.scene, _renderCamera(layer), size, _time);
    final hit = pickNodeAt(frame, local);
    if (hit == null) {
      // Toque duplo em area vazia: volta ao enquadramento geral.
      _frameAll(layer);
      setState(() => _selected = null);
      return;
    }
    setState(() => _selected = hit);
    final node = layer.scene.nodes.firstWhere((n) => n.id == hit);
    final center = resolveNodeTransform(layer.scene, node, _time).position;
    final r = node.size * node.scale.valueAt(_time) * 1.8;
    if (_view == SceneView.camera) {
      _editCamera((c) => frameBounds(c, Bounds3D(center, r), _time));
    } else if (_freeView) {
      setState(() {
        final dir = (_freePos - center).normalized;
        _freeTarget = center;
        _freePos = center + dir * (r * 3.2);
      });
    } else {
      setState(() => _orthoCenter = center);
    }
    // O ponto tocado vira o pivo da orbita.
    _pivot = center;
  }

  void _frameAll(Scene3DLayer layer) {
    final b = sceneBounds(layer.scene, _time);
    if (_view == SceneView.camera) {
      _editCamera((c) => frameBounds(c, b, _time));
    } else if (_freeView) {
      setState(() {
        _freeTarget = b.center;
        final dir = (_freePos - b.center).normalized;
        _freePos =
            b.center +
            (dir.length < 1e-6 ? const Vec3(0.6, 0.5, 0.7) : dir) *
                math.max(600, b.radius * 3);
      });
    } else {
      setState(() {
        _orthoCenter = b.center;
        _orthoScale = b.radius <= 0 ? 0.35 : (300 / b.radius).clamp(0.02, 3.0);
      });
    }
  }

  // ------------------------------------------------------ interface

  @override
  Widget build(BuildContext context) {
    // Reconstroi quando o projeto muda (a camera e do modelo).
    ref.watch(editorControllerProvider);
    final layer = _layer;
    if (layer == null) {
      return const Scaffold(
        backgroundColor: AmColors.bg,
        body: Center(
          child: Text(
            'Cena nao encontrada',
            style: TextStyle(color: AmColors.muted),
          ),
        ),
      );
    }

    return Scaffold(
      backgroundColor: AmColors.bg,
      body: SafeArea(
        child: Column(
          children: [
            _topBar(layer),
            Expanded(
              child: LayoutBuilder(
                builder: (context, constraints) {
                  final size = Size(
                    constraints.maxWidth,
                    constraints.maxHeight,
                  );
                  if (_miniPos.dx < 0) {
                    _miniPos = Offset(
                      size.width - _miniSize - 12,
                      size.height - _miniSize - 12,
                    );
                  }
                  return Stack(
                    children: [
                      _viewport(layer, size),
                      if (_showMiniView) _miniViewWidget(layer, size),
                      Positioned(
                        left: 12,
                        top: 12,
                        child: AxisGizmo(
                          camera: _activeCamera(layer),
                          time: _time,
                          onView: (v) => setState(() {
                            _view = v;
                            _pivot = null;
                          }),
                        ),
                      ),
                      Positioned(right: 12, top: 12, child: _navButton()),
                    ],
                  );
                },
              ),
            ),
            _motionControls(layer),
            _viewSelector(layer),
            _commandBar(layer),
          ],
        ),
      ),
    );
  }

  Widget _topBar(Scene3DLayer layer) {
    return Container(
      height: 48,
      padding: const EdgeInsets.symmetric(horizontal: 8),
      color: AmColors.topBar,
      child: Row(
        children: [
          CupertinoButton(
            padding: EdgeInsets.zero,
            onPressed: () => Navigator.of(context).maybePop(),
            child: const Icon(
              CupertinoIcons.chevron_down,
              size: 20,
              color: AmColors.text,
            ),
          ),
          const SizedBox(width: 4),
          Expanded(
            child: Text(
              layer.name,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(
                fontSize: 15,
                fontWeight: FontWeight.w600,
                color: AmColors.text,
              ),
            ),
          ),
          CupertinoButton(
            padding: EdgeInsets.zero,
            onPressed: () => showScene3DSheet(context, ref, widget.layerId),
            child: const Icon(
              CupertinoIcons.slider_horizontal_3,
              size: 20,
              color: AmColors.accent,
            ),
          ),
        ],
      ),
    );
  }

  Widget _viewport(Scene3DLayer layer, Size size) {
    final cam = _renderCamera(layer);
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTapUp: (d) => _handleTap(layer, d.localPosition, size),
      onDoubleTapDown: (d) => _handleDoubleTap(layer, d.localPosition, size),
      onDoubleTap: () {},
      onScaleStart: (d) {
        final frame = renderScene(layer.scene, cam, size, _time);
        final hit = pickNodeAt(frame, d.localFocalPoint);
        final startIntent = resolveTouch(
          onSelectedLayer: hit != null && hit == _selected,
          onOtherLayer: hit != null && hit != _selected,
          navigationMode: _navigationMode,
        );
        if (startIntent == TouchIntent.selectLayer) {
          setState(() => _selected = hit);
          // A selecao acontece no inicio: o restante do mesmo gesto ja
          // arrasta o objeto, sem exigir um segundo toque.
          _gestureIntent = TouchIntent.moveLayer;
        } else {
          _gestureIntent = startIntent;
        }
        _pivot = null;
        _beginGesture();
        _lastFocal = d.localFocalPoint;
        _lastScale = 1;
        _pointers = d.pointerCount;
        _resolvePivot(layer);
      },
      onScaleUpdate: (d) {
        final delta = d.localFocalPoint - _lastFocal;
        _lastFocal = d.localFocalPoint;
        _pointers = math.max(_pointers, d.pointerCount);

        // PINCA: move no Z. Nunca mexe na lente.
        if ((d.scale - _lastScale).abs() > 0.004) {
          _dolly(layer, d.scale / _lastScale);
          _lastScale = d.scale;
        }
        if (delta == Offset.zero) return;

        if (d.pointerCount >= 2) {
          _pan(layer, delta);
          return;
        }
        // A intencao e decidida onde o gesto comecou e fica estavel ate
        // soltar; atravessar outro objeto nao troca arraste por orbita.
        final intent = _gestureIntent ?? TouchIntent.orbitCamera;
        switch (intent) {
          case TouchIntent.moveLayer:
            _moveSelected(layer, delta, cam);
          case TouchIntent.orbitCamera:
          case TouchIntent.selectLayer:
            _orbit(layer, delta);
        }
      },
      onScaleEnd: (_) {
        _pointers = 0;
        _endGesture();
      },
      child: ValueListenableBuilder<int>(
        valueListenable: TextureCache.instance.revision,
        builder: (_, _, _) => Stack(
          fit: StackFit.expand,
          children: [
            // O MOTOR EM GPU desenha a cena; sem ele, o pintor em CPU
            // (que tambem e quem desenha as ajudas: grade, frustum,
            // caixa do selecionado).
            if (filamentPreviewEnabled || !Scene3DGpu.indisponivel)
              SizedBox(
                width: size.width,
                height: size.height,
                child: Scene3DGpuView(
                  scene: layer.scene,
                  camera: _activeCamera(layer),
                  renderCamera: cam,
                  view: _view,
                  time: _time,
                  rascunho: _gestureActive || _playback.playing.value,
                  showHelpers: true,
                  selectedNodeId: _selected,
                ),
              )
            else
              CustomPaint(
                size: size,
                painter: Scene3DPainter(
                  scene: (_gestureActive || _playback.playing.value)
                      ? layer.scene.copyWith(draftMode: true)
                      : layer.scene,
                  camera: _activeCamera(layer),
                  view: _view,
                  time: _time,
                  showHelpers: true,
                  selectedNodeId: _selected,
                  overrideCamera: cam,
                ),
              ),
          ],
        ),
      ),
    );
  }

  /// Arrastar o objeto selecionado no plano da tela.
  void _moveSelected(Scene3DLayer layer, Offset delta, RenderCamera cam) {
    final id = _selected;
    if (id == null) return;
    final basis = cameraBasis(cam);
    final node = layer.scene.nodes.firstWhere((n) => n.id == id);
    if (node.locked) return;
    if (_transformTool == 1) {
      _controller.updateSceneNode(
        widget.layerId,
        id,
        (n) => n.copyWith(
          rotX: editMotionValue(
            n.rotX,
            _time,
            n.rotX.valueAt(_time) + delta.dy * .5,
            autoKey: _autoKey,
          ),
          rotY: editMotionValue(
            n.rotY,
            _time,
            n.rotY.valueAt(_time) + delta.dx * .5,
            autoKey: _autoKey,
          ),
        ),
      );
      return;
    }
    if (_transformTool == 2) {
      _controller.updateSceneNode(
        widget.layerId,
        id,
        (n) => n.copyWith(
          scale: editMotionValue(
            n.scale,
            _time,
            (n.scale.valueAt(_time) * math.exp((delta.dx - delta.dy) * .008))
                .clamp(.001, 1000),
            autoKey: _autoKey,
          ),
        ),
      );
      return;
    }
    final p = resolveNodeTransform(layer.scene, node, _time).position;
    final dist = (p - cam.position).dot(basis.forward).abs();
    final k = cam.orthographic ? 1 / cam.orthoScale : dist / math.max(1, 600);
    final worldShift = basis.right * (delta.dx * k) - basis.up * (delta.dy * k);
    final shift = sceneLocalDelta(layer.scene, node, _time, worldShift);
    _controller.updateSceneNode(
      widget.layerId,
      id,
      (n) => n.copyWith(
        x: editMotionValue(
          n.x,
          _time,
          n.x.valueAt(_time) + shift.x,
          autoKey: _autoKey,
        ),
        y: editMotionValue(
          n.y,
          _time,
          n.y.valueAt(_time) + shift.y,
          autoKey: _autoKey,
        ),
        z: editMotionValue(
          n.z,
          _time,
          n.z.valueAt(_time) + shift.z,
          autoKey: _autoKey,
        ),
      ),
    );
  }

  Widget _navButton() {
    return GestureDetector(
      onTap: () => setState(() => _navigationMode = !_navigationMode),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
        decoration: BoxDecoration(
          color: _navigationMode ? AmColors.accentDim : AmColors.chip,
          borderRadius: BorderRadius.circular(9),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              CupertinoIcons.move,
              size: 14,
              color: _navigationMode ? AmColors.accent : AmColors.muted,
            ),
            const SizedBox(width: 5),
            Text(
              'Navegar',
              style: TextStyle(
                fontSize: 11,
                color: _navigationMode ? AmColors.accent : AmColors.muted,
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// MINI-VISTA: arrastavel, redimensionavel, com a vista trocavel e
  /// sumivel. Resolve 90% do que quatro janelas resolvem, em 25% do
  /// espaco.
  Widget _miniViewWidget(Scene3DLayer layer, Size size) {
    return Positioned(
      left: _miniPos.dx.clamp(0.0, math.max(0.0, size.width - _miniSize)),
      top: _miniPos.dy.clamp(0.0, math.max(0.0, size.height - _miniSize)),
      child: GestureDetector(
        onPanUpdate: (d) => setState(() => _miniPos += d.delta),
        onTap: () => setState(() {
          _miniView = _miniView == SceneView.top
              ? SceneView.right
              : SceneView.top;
        }),
        child: SizedBox(
          width: _miniSize,
          height: _miniSize,
          child: Stack(
            children: [
              CustomPaint(
                size: Size(_miniSize, _miniSize),
                painter: MiniViewPainter(
                  scene: layer.scene,
                  camera: _activeCamera(layer),
                  time: _time,
                  view: _miniView,
                ),
              ),
              Positioned(
                left: 5,
                top: 3,
                child: Text(
                  sceneViewLabel(_miniView),
                  style: const TextStyle(fontSize: 9, color: AmColors.muted),
                ),
              ),
              Positioned(
                right: 2,
                top: 0,
                child: GestureDetector(
                  onTap: () => setState(() => _showMiniView = false),
                  child: const Padding(
                    padding: EdgeInsets.all(5),
                    child: Icon(
                      CupertinoIcons.xmark,
                      size: 11,
                      color: AmColors.muted,
                    ),
                  ),
                ),
              ),
              // Canto de redimensionar.
              Positioned(
                right: 0,
                bottom: 0,
                child: GestureDetector(
                  onPanUpdate: (d) => setState(() {
                    _miniSize = (_miniSize + d.delta.dx).clamp(
                      90.0,
                      math.min(260.0, size.width - 24),
                    );
                  }),
                  child: const Padding(
                    padding: EdgeInsets.all(6),
                    child: Icon(
                      CupertinoIcons.arrow_up_left_arrow_down_right,
                      size: 11,
                      color: AmColors.muted,
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

  Widget _viewSelector(Scene3DLayer layer) {
    const views = SceneView.values;
    return SizedBox(
      height: 40,
      child: ListView.separated(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
        itemCount: views.length + (_showMiniView ? 0 : 1),
        separatorBuilder: (_, _) => const SizedBox(width: 6),
        itemBuilder: (_, i) {
          if (i == views.length) {
            return _chip(
              'Mini-vista',
              false,
              () => setState(() => _showMiniView = true),
            );
          }
          final v = views[i];
          return _chip(sceneViewLabel(v), v == _view, () {
            setState(() {
              _view = v;
              _pivot = null;
              if (v == SceneView.custom1 || v == SceneView.custom2) {
                // A vista livre nasce onde a camera esta — assim nada
                // pula quando se troca para ela.
                _freePos = _activeCamera(layer).positionAt(_time);
                _freeTarget = _activeCamera(layer).kind == CameraKind.twoNode
                    ? _activeCamera(layer).pointOfInterestAt(_time)
                    : _freePos + _activeCamera(layer).forwardAt(_time) * 800;
              }
            });
          });
        },
      ),
    );
  }

  List<AnimatedDouble> _tracks(Scene3DLayer layer) {
    final node = layer.scene.nodeById(_selected ?? '');
    return node == null
        ? cameraMotionTracks(_activeCamera(layer))
        : nodeMotionTracks(node);
  }

  void _mapTracks(AnimatedDouble Function(AnimatedDouble) edit) {
    _playback.pause();
    final layer = _layer;
    if (layer == null) return;
    final node = layer.scene.nodeById(_selected ?? '');
    if (node == null) {
      _setCamera(mapCameraMotion(_activeCamera(layer), edit));
    } else {
      _controller.updateSceneNode(
        widget.layerId,
        node.id,
        (n) => mapNodeMotion(n, edit),
      );
    }
  }

  Future<void> _chooseTarget(Scene3DLayer layer) async {
    _playback.pause();
    final result = await showModalBottomSheet<String>(
      context: context,
      backgroundColor: AmColors.panel,
      builder: (context) => SafeArea(
        child: ListView.builder(
          itemCount: layer.scene.nodes.length + 1,
          itemBuilder: (context, i) {
            final node = i == 0 ? null : layer.scene.nodes[i - 1];
            return ListTile(
              title: Text(
                node?.name ?? 'Camera',
                style: const TextStyle(color: AmColors.text),
              ),
              leading: Icon(
                node == null
                    ? Icons.videocam
                    : node.locked
                    ? Icons.lock
                    : Icons.view_in_ar,
                color: AmColors.accent,
              ),
              onTap: () => Navigator.pop(context, node?.id ?? ''),
            );
          },
        ),
      ),
    );
    if (mounted && result != null) {
      setState(() => _selected = result.isEmpty ? null : result);
    }
  }

  Widget _motionControls(Scene3DLayer layer) {
    final node = layer.scene.nodeById(_selected ?? '');
    final tracks = _tracks(layer);
    final keys = {
      for (final track in tracks)
        for (final key in track.keyframes) key.time.inMicroseconds,
    }.toList()..sort();
    final here = tracks.any((track) => track.hasKeyframeAt(_time));
    final previous = keys
        .where((t) => t < _time.inMicroseconds - 8000)
        .lastOrNull;
    final next = keys.where((t) => t > _time.inMicroseconds + 8000).firstOrNull;
    final duration = math.max(1, layer.duration.inMicroseconds).toDouble();
    void seek(int value) {
      _playback.pause();
      _playback.seek(Duration(microseconds: value));
    }

    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        Row(
          children: [
            IconButton(
              tooltip: _playback.playing.value ? 'Pausar' : 'Reproduzir cena',
              onPressed: _playback.toggle,
              icon: Icon(
                _playback.playing.value ? Icons.pause : Icons.play_arrow,
                color: AmColors.accent,
              ),
            ),
            Expanded(
              child: AmTickRuler(
                key: const ValueKey('scene-motion-time'),
                height: 48,
                unitsPerPixel: duration / 1e6 / 300,
                min: 0,
                max: duration / 1e6,
                value: _time.inMicroseconds.toDouble().clamp(0, duration) / 1e6,
                onChanged: (v) => seek((v * 1e6).round()),
              ),
            ),
            Padding(
              padding: const EdgeInsets.only(right: 10),
              child: Text(
                '${(_time.inMicroseconds / 1e6).toStringAsFixed(2)} s',
                style: const TextStyle(color: AmColors.text),
              ),
            ),
          ],
        ),
        Row(
          children: [
            Expanded(
              child: TextButton(
                onPressed: () => _chooseTarget(layer),
                child: Text(
                  node?.name ?? _activeCamera(layer).name,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ),
            IconButton(
              tooltip: 'Keyframe anterior',
              onPressed: previous == null ? null : () => seek(previous),
              icon: const Icon(Icons.skip_previous, color: AmColors.text),
            ),
            IconButton(
              tooltip: here ? 'Remover keyframe' : 'Adicionar keyframe',
              onPressed: node?.locked == true
                  ? null
                  : () => _mapTracks(
                      (track) => here
                          ? track.withoutKeyframe(_time)
                          : editMotionValue(track, _time, track.valueAt(_time)),
                    ),
              icon: Icon(
                here ? Icons.diamond : Icons.diamond_outlined,
                color: AmColors.accent,
              ),
            ),
            IconButton(
              tooltip: 'Proximo keyframe',
              onPressed: next == null ? null : () => seek(next),
              icon: const Icon(Icons.skip_next, color: AmColors.text),
            ),
            PopupMenuButton<Easing>(
              tooltip: 'Curva do movimento',
              icon: const Icon(Icons.show_chart, color: AmColors.accent),
              itemBuilder: (_) => [
                const PopupMenuItem(
                  value: Easing.linear,
                  child: Text('Linear'),
                ),
                const PopupMenuItem(
                  value: Easing.easeInOut,
                  child: Text('Suave'),
                ),
                const PopupMenuItem(
                  value: Easing.easeOut,
                  child: Text('Desacelerar'),
                ),
                const PopupMenuItem(
                  value: Easing.overshoot,
                  child: Text('Antecipacao e retorno'),
                ),
              ],
              onSelected: (ease) => _mapTracks((track) {
                final start = track.keyframes
                    .where((k) => k.time <= _time)
                    .lastOrNull;
                return start == null ? track : track.withEase(start.time, ease);
              }),
            ),
          ],
        ),
        SizedBox(
          height: 36,
          child: ListView(
            scrollDirection: Axis.horizontal,
            padding: const EdgeInsets.symmetric(horizontal: 10),
            children: [
              for (var i = 0; i < 3; i++) ...[
                _chip(
                  ['Mover', 'Girar', 'Escalar'][i],
                  _transformTool == i,
                  () => setState(() => _transformTool = i),
                ),
                const SizedBox(width: 6),
              ],
              _chip(
                _autoKey ? 'Auto-key ligado' : 'Auto-key desligado',
                _autoKey,
                () => setState(() => _autoKey = !_autoKey),
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _chip(String label, bool on, VoidCallback onTap) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        alignment: Alignment.center,
        padding: const EdgeInsets.symmetric(horizontal: 12),
        decoration: BoxDecoration(
          color: on ? AmColors.accentDim : AmColors.chip,
          borderRadius: BorderRadius.circular(9),
        ),
        child: Text(
          label,
          style: TextStyle(
            fontSize: 12,
            color: on ? AmColors.accent : AmColors.muted,
          ),
        ),
      ),
    );
  }

  Widget _commandBar(Scene3DLayer layer) {
    return Container(
      padding: const EdgeInsets.fromLTRB(10, 4, 10, 10),
      color: AmColors.panel,
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        child: Row(
          children: [
            _cmd(
              CupertinoIcons.fullscreen,
              'Enquadrar tudo',
              () => _frameAll(layer),
            ),
            const SizedBox(width: 8),
            _cmd(
              CupertinoIcons.viewfinder,
              'Enquadrar selecionado',
              _selected == null
                  ? null
                  : () {
                      final node = layer.scene.nodeById(_selected!);
                      if (node == null) return;
                      final xf = resolveNodeTransform(layer.scene, node, _time);
                      _editCamera(
                        (c) => frameBounds(
                          c,
                          Bounds3D(
                            xf.position,
                            node.size * xf.scale.abs() * 1.8,
                          ),
                          _time,
                        ),
                      );
                    },
            ),
            const SizedBox(width: 8),
            // O COMANDO MAIS USADO: navegar livre ate achar o
            // enquadramento, e so entao a camera assumir ele.
            _cmd(
              CupertinoIcons.camera_viewfinder,
              'Alinhar camera a vista',
              _view == SceneView.camera
                  ? null
                  : () {
                      _editCamera((c) => alignToView(c, _renderCamera(layer)));
                      setState(() => _view = SceneView.camera);
                    },
            ),
            const SizedBox(width: 8),
            _cmd(CupertinoIcons.bookmark, 'Salvar vista', () {
              final cam = _renderCamera(layer);
              _controller.saveSceneView(
                widget.layerId,
                'Vista ${layer.scene.savedViews.length + 1}',
                cam,
              );
              setState(() {});
            }),
            const SizedBox(width: 8),
            _cmd(
              CupertinoIcons.circle_lefthalf_fill,
              'Focar no selecionado',
              _selected == null
                  ? null
                  : () {
                      final node = layer.scene.nodeById(_selected!);
                      if (node == null) return;
                      final camera = _activeCamera(layer);
                      final position = resolveNodeTransform(
                        layer.scene,
                        node,
                        _time,
                      ).position;
                      final distance =
                          (position - layer.cameraAt(_time).position).length;
                      _setCamera(
                        camera.copyWith(
                          dof: camera.dof.copyWith(
                            enabled: true,
                            focusDistance: editMotionValue(
                              camera.dof.focusDistance,
                              _time,
                              distance,
                              autoKey: _autoKey,
                            ),
                          ),
                        ),
                      );
                    },
            ),
          ],
        ),
      ),
    );
  }

  Widget _cmd(IconData icon, String label, VoidCallback? onTap) {
    final on = onTap != null;
    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 8),
        decoration: BoxDecoration(
          color: AmColors.chip,
          borderRadius: BorderRadius.circular(9),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 14, color: on ? AmColors.accent : AmColors.muted),
            const SizedBox(width: 6),
            Text(
              label,
              style: TextStyle(
                fontSize: 12,
                color: on ? AmColors.accent : AmColors.muted,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
