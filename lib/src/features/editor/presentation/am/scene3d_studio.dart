import 'package:aurea/src/core/l10n/app_language.dart';
import '../widgets/composition_frame.dart';
import '../widgets/motion_keyframe_track.dart';
import '../context/parameter_row.dart' show showNumberInput;

import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/texture_cache.dart';
import '../../application/playback_controller.dart';
import '../../application/editor_controller.dart';
import '../../application/estudio_preferencia.dart';
import '../../domain/camera3d.dart';
import '../../domain/estudio_ux.dart';
import '../../domain/layer.dart';
import '../../domain/scene3d.dart';
import '../../domain/scene_motion.dart';
import '../../domain/keyframe.dart';
import '../../domain/camera_cuts.dart';
import '../../application/scene3d_gpu.dart';
import '../widgets/scene3d_painter.dart';
import '../widgets/scene3d_gpu_view.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'color_picker_sheet.dart';
import 'scene3d_sheet.dart';
import 'scene3d_studio_ux.dart';
import '../../../tutoriais/presentation/tutorial_screen.dart';
import '../estudio/estudio_da_cena.dart';
import 'hud_desempenho.dart';

/// ESTUDIO DA CENA 3D: a vista no centro, e o resto em volta dela.
///
/// A reestruturacao (a missao "redesign completo"): SIMPLES POR PADRAO.
/// Na tela ficam so a barra de cima (Voltar, a camera, os tres pontos),
/// a vista com o gizmo e as acoes rapidas, a barra de contexto do que
/// esta selecionado, a linha do tempo e as quatro ferramentas. O modo
/// avancado devolve as vistas, a mini-vista, a grade, o eixo travado e
/// os comandos de camera — nada do motor foi tirado; mudou o caminho.
///
/// O conflito de gestos continua resolvido como antes (camera §2.1):
///
///   dedo sobre o objeto selecionado  -> move o objeto (com uma
///                                       ferramenta de transformar)
///   dedo sobre outro objeto          -> seleciona ele e ja arrasta
///   dedo em area vazia               -> ORBITA a camera
///   dois dedos                       -> deslizam; a pinca aproxima
///
/// e a ferramenta SELECIONAR e o modo de navegacao: um dedo sempre gira,
/// o toque sempre escolhe.
/// Um Estudio de cada vez: o toque duplo no tile (ou dois chamadores
/// no mesmo quadro) nao pode empilhar dois Estudios.
bool _estudioAberto = false;

Future<void> openScene3DStudio(
  BuildContext context,
  WidgetRef ref,
  String layerId, {
  PlaybackController? playback,
}) async {
  if (_estudioAberto) return;
  _estudioAberto = true;
  try {
    if (playback != null) {
      await abrirEstudioDaCena(context, layerId: layerId, playback: playback);
    } else {
      await Navigator.of(context).push(
        MaterialPageRoute<void>(
          fullscreenDialog: true,
          builder: (_) => _TravaDoEstudio(child: Scene3DStudio(layerId: layerId)),
        ),
      );
    }
  } finally {
    _estudioAberto = false;
  }
}

class _TravaDoEstudio extends StatefulWidget {
  const _TravaDoEstudio({required this.child});

  final Widget child;

  @override
  State<_TravaDoEstudio> createState() => _TravaDoEstudioState();
}

class _TravaDoEstudioState extends State<_TravaDoEstudio> {
  @override
  void dispose() {
    _estudioAberto = false;
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => widget.child;
}

class Scene3DStudio extends ConsumerStatefulWidget {
  const Scene3DStudio({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<Scene3DStudio> createState() => _Scene3DStudioState();
}

/// O que cada alvo tinha quando o gesto comecou — a base do encaixe na
/// grade, que precisa do valor bruto acumulado e nao do ja encaixado.
class _BaseDoGesto {
  const _BaseDoGesto(this.pos, this.rotX, this.rotY, this.rotZ, this.escala);
  final Vec3 pos;
  final double rotX;
  final double rotY;
  final double rotZ;
  final double escala;
}

class _Scene3DStudioState extends ConsumerState<Scene3DStudio>
    with SingleTickerProviderStateMixin {
  late final PlaybackController _playback;
  late final EstudioPreferencia _prefs;
  Duration get _time => _playback.time.value;
  bool _autoKey = true;

  /// A ferramenta da barra de baixo.
  FerramentaDoEstudio _tool = FerramentaDoEstudio.selecionar;

  /// MODO AVANCADO: vistas, mini-vista, grade, eixo, comandos.
  final bool _avancado = true;

  /// Encaixar na grade e eixo travado (modo avancado).
  bool _snap = false;
  EixoTravado _eixo = EixoTravado.livre;

  /// A dica visivel (-1 = nenhuma).
  int _dica = -1;

  @override
  void initState() {
    super.initState();
    _prefs = EstudioPreferencia.de(ref);
    _dica = _prefs.dicasVistas ? -1 : 0;
    _playback = PlaybackController(
      vsync: this,
      durationOf: () => _layer?.duration ?? Duration.zero,
    );
    _playback.compositionFps = ref.read(editorControllerProvider).fps;
    _playback.loop.value = true;
  }

  @override
  void dispose() {
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
    if (_gestureActive) _abrirGestoNoControlador();
    _setCamera(
      editCameraMotion(_activeCamera(layer), _time, edit),
    );
  }

  SceneView _view = SceneView.camera;

  /// O selecionado principal, os demais da selecao multipla, e a luz
  /// escolhida no painel (luz nao se toca na vista).
  String? _selected;
  final Set<String> _multi = {};
  String? _selectedLight;
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
  bool _gestoNoControlador = false;
  TouchIntent? _gestureIntent;
  final Map<String, _BaseDoGesto> _base = {};
  Vec3 _accMundo = Vec3.zero;
  double _accDx = 0;
  double _accDy = 0;

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

  bool get _navigationMode => _tool == FerramentaDoEstudio.selecionar;

  /// Todos os nos selecionados (o principal e os da selecao multipla).
  Set<String> get _alvos => {?_selected, ..._multi};

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

  // ------------------------------------------------------- selecao

  void _selecionar(String? id, {bool acumular = false}) {
    setState(() {
      _selectedLight = null;
      if (!acumular || id == null) {
        _multi.clear();
        _selected = id;
        return;
      }
      // SELECAO MULTIPLA (toque longo): entra ou sai do conjunto.
      if (_alvos.contains(id)) {
        _multi.remove(id);
        if (_selected == id) {
          _selected = _multi.isEmpty ? null : _multi.first;
          _multi.remove(_selected);
        }
      } else {
        if (_selected != null) _multi.add(_selected!);
        _selected = id;
      }
    });
  }

  void _selecionarLuz(String? id) {
    setState(() {
      _multi.clear();
      _selected = null;
      _selectedLight = id;
    });
  }

  // ------------------------------------------------------- gestos

  /// MODO RASCUNHO durante o gesto (camera §8): navega liso, e o
  /// resultado bom volta ao soltar.
  void _beginGesture() {
    if (_gestureActive) return;
    _playback.pause();
    setState(() => _gestureActive = true);
  }

  /// Um gesto inteiro e UM passo de desfazer — e so vira passo se
  /// mexeu no projeto (orbitar a vista livre nao mexe).
  void _abrirGestoNoControlador() {
    if (_gestoNoControlador) return;
    _gestoNoControlador = true;
    _controller.beginGesture();
  }

  void _endGesture() {
    if (_gestoNoControlador) {
      _gestoNoControlador = false;
      _controller.endGesture();
    }
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

  String? _pick(Scene3DLayer layer, Offset local, Size size) {
    final frame = renderScene(layer.scene, _renderCamera(layer), size, _time);
    return pickNodeAt(frame, local);
  }

  /// O toque SEMPRE escolhe — em qualquer ferramenta. E o que faz o
  /// modo Selecionar ser de verdade o modo de selecionar.
  void _handleTap(Scene3DLayer layer, Offset local, Size size) {
    _selecionar(_pick(layer, local, size));
  }

  /// TOQUE LONGO: selecao multipla. Cada toque longo entra ou sai.
  void _handleLongPress(Scene3DLayer layer, Offset local, Size size) {
    final hit = _pick(layer, local, size);
    if (hit == null) return;
    HapticFeedback.selectionClick();
    _selecionar(hit, acumular: true);
  }

  void _handleDoubleTap(Scene3DLayer layer, Offset local, Size size) {
    final hit = _pick(layer, local, size);
    if (hit == null) {
      // Toque duplo em area vazia: volta ao enquadramento geral.
      _frameAll(layer);
      _selecionar(null);
      return;
    }
    _selecionar(hit);
    _frameSelected(layer);
  }

  /// ENQUADRAR um volume na vista que esta em uso (camera, livre ou
  /// ortografica).
  void _enquadrar(Scene3DLayer layer, Bounds3D b) {
    if (b.radius <= 0) return;
    if (_view == SceneView.camera) {
      _editCamera((c) => frameBounds(c, b, _time));
    } else if (_freeView) {
      setState(() {
        final dir = (_freePos - b.center).normalized;
        _freeTarget = b.center;
        _freePos =
            b.center +
            (dir.length < 1e-6 ? const Vec3(0.6, 0.5, 0.7) : dir) *
                math.max(b.radius * 3.2, 1);
      });
    } else {
      setState(() {
        _orthoCenter = b.center;
        _orthoScale = (300 / b.radius).clamp(0.02, 3.0);
      });
    }
    _pivot = b.center;
  }

  Bounds3D? _boundsDaSelecao(Scene3DLayer layer) {
    Bounds3D? total;
    for (final id in _alvos) {
      final node = layer.scene.nodeById(id);
      if (node == null) continue;
      final xf = resolveNodeTransform(layer.scene, node, _time);
      final b = Bounds3D(xf.position, node.size * xf.scale.abs() * 1.8);
      if (total == null) {
        total = b;
      } else {
        final centro = (total.center + b.center) * 0.5;
        final r = math.max(
          (total.center - centro).length + total.radius,
          (b.center - centro).length + b.radius,
        );
        total = Bounds3D(centro, r);
      }
    }
    return total;
  }

  /// FOCAR: enquadra o selecionado; sem selecao, a cena inteira.
  void _frameSelected(Scene3DLayer layer) {
    final b = _boundsDaSelecao(layer);
    if (b == null) {
      _frameAll(layer);
      return;
    }
    _enquadrar(layer, b);
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

  // ------------------------------------------------------- cameras

  /// USAR UMA CAMERA: ela entra no ar a partir do instante atual (o
  /// modelo de corte do app), e a vista volta a ser pela camera.
  void _usarCamera(String cameraId) {
    _controller.setCameraShot(widget.layerId, _time, cameraId);
    setState(() {
      _view = SceneView.camera;
      _pivot = null;
    });
  }

  /// NOVA CAMERA: nasce olhando exatamente para onde a vista esta
  /// agora (a livre, a ortografica ou a camera no ar), com a lente da
  /// camera no ar, e ja entra no ar.
  void _novaCamera() {
    final layer = _layer;
    if (layer == null) return;
    final ativa = _activeCamera(layer);
    final rc = _renderCamera(layer);
    final id = _controller.addScene3DCamera(widget.layerId);
    if (id.isEmpty) return;
    final nova = _layer?.allCameras.where((c) => c.id == id).firstOrNull;
    if (nova == null) return;
    _controller.updateSceneCameraById(
      widget.layerId,
      alignToView(nova, rc).copyWith(
        focalLength: AnimatedDouble(ativa.focalLength.valueAt(_time)),
        filmWidth: ativa.filmWidth,
        orthographic: ativa.orthographic,
      ),
    );
    _usarCamera(id);
  }

  void _verVista(SceneView v) {
    final layer = _layer;
    setState(() {
      _view = v;
      _pivot = null;
      if ((v == SceneView.custom1 || v == SceneView.custom2) && layer != null) {
        // A vista livre nasce onde a camera esta — assim nada pula
        // quando se troca para ela.
        final cam = _activeCamera(layer);
        _freePos = cam.positionAt(_time);
        _freeTarget = cam.kind == CameraKind.twoNode
            ? cam.pointOfInterestAt(_time)
            : _freePos + cam.forwardAt(_time) * 800;
      }
    });
  }

  void _alinharCameraAVista(Scene3DLayer layer) {
    if (_view == SceneView.camera) return;
    _editCamera((c) => alignToView(c, _renderCamera(layer)));
    setState(() => _view = SceneView.camera);
  }

  void _salvarVista(Scene3DLayer layer) {
    _controller.saveSceneView(
      widget.layerId,
      'Vista ${layer.scene.savedViews.length + 1}',
      _renderCamera(layer),
    );
    setState(() {});
  }

  /// FOCO DA LENTE (profundidade de campo) no selecionado.
  void _focarLente(Scene3DLayer layer) {
    final id = _selected;
    if (id == null) return;
    final node = layer.scene.nodeById(id);
    if (node == null) return;
    final camera = _activeCamera(layer);
    final position = resolveNodeTransform(layer.scene, node, _time).position;
    final distance = (position - layer.cameraAt(_time).position).length;
    _setCamera(
      camera.copyWith(
        dof: camera.dof.copyWith(
          enabled: true,
          focusDistance: editMotionValue(
            camera.dof.focusDistance,
            _time,
            distance,
          ),
        ),
      ),
    );
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
          child: AppText('Cena nao encontrada',
            style: TextStyle(color: AmColors.muted),
          ),
        ),
      );
    }
    // Uma selecao que ficou para tras (desfazer, excluir) nao vale.
    final node = layer.scene.nodeById(_selected ?? '');
    final luz = layer.scene.lights
        .where((l) => l.id == _selectedLight)
        .firstOrNull;

    // Clock ticks update the viewport and motion controls, not the whole
    // studio or its tool menus. Camera cuts still update the camera title.
    Widget live(Widget Function() builder) => ListenableBuilder(
      listenable: Listenable.merge([_playback.time, _playback.playing]),
      builder: (_, _) => builder(),
    );
    final project = ref.read(editorControllerProvider);
    Widget viewport() => LayoutBuilder(
      builder: (context, constraints) {
        final size = constraints.biggest;
        final frame = compositionRect(
          size,
          Size(project.outputWidth.toDouble(), project.outputHeight.toDouble()),
        );
        if (_miniPos.dx < 0) {
          _miniPos = Offset(
            math.max(0, size.width - _miniSize - 12),
            math.max(0, size.height - _miniSize - 12),
          );
        }
        return Stack(
          key: const ValueKey('studio-viewport'),
          children: [
            // Camera projection and picking use precisely the same output
            // aspect ratio as the main composition and exported frame.
            Positioned.fromRect(
              rect: frame,
              child: CompositionFrame(
                key: const ValueKey('studio-composition-frame'),
                safeAreas: project.guides.showSafeAreas,
                child: RepaintBoundary(child: _viewport(layer, frame.size)),
              ),
            ),
            if (_avancado && _showMiniView) _miniViewWidget(layer, size),
            Positioned(
              left: 10,
              top: 10,
              child: AxisGizmo(
                camera: _activeCamera(layer),
                time: _time,
                onView: _verVista,
              ),
            ),
            if (layer.allCameras.length > 1)
              Positioned(
                left: 80,
                right: 58,
                top: 10,
                child: _cameraStrip(layer),
              ),
          ],
        );
      },
    );
    Widget controls() => ColoredBox(
      color: AmColors.panel,
      child: Column(
        children: [
          SizedBox(height: 44, child: _quickActions(layer)),
          live(() => _timeRow(layer, node)),
          live(() => _contextBar(layer, node, luz)),
          live(() => _transformValues(layer, node)),
          Expanded(
            child: SingleChildScrollView(
              child: Column(
                children: [
                  _toolStrip(),
                  if (_avancado) _viewSelector(layer),
                  if (_avancado) _commandBar(layer),
                  if (_dica >= 0) _hintCard(),
                ],
              ),
            ),
          ),
        ],
      ),
    );
    return Scaffold(
      backgroundColor: AmColors.bg,
      resizeToAvoidBottomInset: ModalRoute.of(context)?.isCurrent ?? true,
      body: SafeArea(
        child: Column(
          children: [
            live(() => _topBar(layer)),
            Expanded(
              child: LayoutBuilder(
                builder: (context, constraints) {
                  final wide =
                      constraints.maxWidth >= 600 &&
                      constraints.maxWidth > constraints.maxHeight;
                  if (wide) {
                    return Row(
                      children: [
                        Expanded(child: live(viewport)),
                        SizedBox(
                          width: (constraints.maxWidth * .4).clamp(
                            280.0,
                            380.0,
                          ),
                          child: controls(),
                        ),
                      ],
                    );
                  }
                  final previewHeight = math.min(
                    (constraints.maxWidth - 16) / project.aspectRatio + 16,
                    math.min(
                      constraints.maxHeight * .57,
                      math.max(80.0, constraints.maxHeight - 308),
                    ),
                  );
                  return Column(
                    children: [
                      SizedBox(height: previewHeight, child: live(viewport)),
                      Expanded(child: controls()),
                    ],
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _transformValues(Scene3DLayer layer, SceneNode? node) {
    final camera = _activeCamera(layer);
    final tool = _tool;
    if (tool == FerramentaDoEstudio.selecionar || _alvos.length > 1) {
      return const SizedBox(
        height: 44,
        child: Center(
          child: AppText('Escolha Mover, Girar ou Escalar para ajustar',
            style: TextStyle(fontSize: 11, color: AmColors.muted),
          ),
        ),
      );
    }
    final tracks = switch (tool) {
      FerramentaDoEstudio.girar =>
        node == null
            ? [camera.rotX, camera.rotY, camera.rotZ]
            : [node.rotX, node.rotY, node.rotZ],
      FerramentaDoEstudio.escalar => [node?.scale ?? camera.focalLength],
      _ =>
        node == null
            ? [camera.posX, camera.posY, camera.posZ]
            : [node.x, node.y, node.z],
    };
    return SizedBox(
      height: 44,
      child: Row(
        children: [
          for (var axis = 0; axis < tracks.length; axis++)
            Expanded(
              child: TextButton(
                key: ValueKey('scene-transform-$axis'),
                onPressed: node?.locked == true
                    ? null
                    : () async {
                        _playback.pause();
                        final isScale =
                            tool == FerramentaDoEstudio.escalar && node != null;
                        final value = await showNumberInput(
                          context,
                          title: tracks.length == 1
                              ? (isScale ? 'Escala (%)' : 'Lente (mm)')
                              : '${ferramentaLabel(tool)} · ${['X', 'Y', 'Z'][axis]}',
                          value:
                              tracks[axis].valueAt(_time) * (isScale ? 100 : 1),
                          min: tracks.length == 1
                              ? .1
                              : double.negativeInfinity,
                        );
                        if (value == null || !mounted || _layer == null) return;
                        final v = value / (isScale ? 100 : 1);
                        if (node != null) {
                          _controller.updateSceneNode(
                            widget.layerId,
                            node.id,
                            (n) => switch (tool) {
                              FerramentaDoEstudio.escalar => n.copyWith(
                                scale: _valor(n.scale, v),
                              ),
                              FerramentaDoEstudio.girar => n.copyWith(
                                rotX: axis == 0 ? _valor(n.rotX, v) : null,
                                rotY: axis == 1 ? _valor(n.rotY, v) : null,
                                rotZ: axis == 2 ? _valor(n.rotZ, v) : null,
                              ),
                              _ => n.copyWith(
                                x: axis == 0 ? _valor(n.x, v) : null,
                                y: axis == 1 ? _valor(n.y, v) : null,
                                z: axis == 2 ? _valor(n.z, v) : null,
                              ),
                            },
                          );
                        } else {
                          final c = _activeCamera(_layer!);
                          _setCamera(switch (tool) {
                            FerramentaDoEstudio.escalar => c.copyWith(
                              focalLength: _valor(c.focalLength, v),
                            ),
                            FerramentaDoEstudio.girar => c.copyWith(
                              rotX: axis == 0 ? _valor(c.rotX, v) : null,
                              rotY: axis == 1 ? _valor(c.rotY, v) : null,
                              rotZ: axis == 2 ? _valor(c.rotZ, v) : null,
                            ),
                            _ => c.copyWith(
                              posX: axis == 0 ? _valor(c.posX, v) : null,
                              posY: axis == 1 ? _valor(c.posY, v) : null,
                              posZ: axis == 2 ? _valor(c.posZ, v) : null,
                            ),
                          });
                        }
                      },
                child: FittedBox(
                  fit: BoxFit.scaleDown,
                  child: AppText(
                    '${tracks.length == 1 ? (node == null ? 'mm' : '%') : ['X', 'Y', 'Z'][axis]}  '
                    '${(tracks[axis].valueAt(_time) * (tool == FerramentaDoEstudio.escalar && node != null ? 100 : 1)).toStringAsFixed(1)}',
                    style: const TextStyle(
                      color: AmColors.accent,
                      fontSize: 13,
                    ),
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }

  String _tituloDaVista(Scene3DLayer layer) {
    final cam = _activeCamera(layer).name;
    if (_view == SceneView.camera) return cam;
    return '${_freeView ? 'Livre' : sceneViewLabel(_view)} · $cam';
  }

  Widget _topBar(Scene3DLayer layer) {
    return Container(
      height: 48,
      padding: const EdgeInsets.symmetric(horizontal: 4),
      color: AmColors.topBar,
      child: Row(
        children: [
          Tooltip(
            message: layer.name,
            child: CupertinoButton(
              key: const ValueKey('estudio-voltar'),
              padding: const EdgeInsets.symmetric(horizontal: 8),
              onPressed: () => Navigator.of(context).maybePop(),
              child: const Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(
                    CupertinoIcons.chevron_back,
                    size: 20,
                    color: AmColors.text,
                  ),
                  SizedBox(width: 2),
                  AppText(
                    'Cena',
                    style: TextStyle(fontSize: 14, color: AmColors.text),
                  ),
                ],
              ),
            ),
          ),
          Expanded(
            child: Center(
              child: CupertinoButton(
                key: const ValueKey('estudio-camera'),
                padding: const EdgeInsets.symmetric(horizontal: 10),
                onPressed: () => _abrirMenuDaCamera(layer),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    const Icon(
                      CupertinoIcons.videocam_fill,
                      size: 16,
                      color: AmColors.accent,
                    ),
                    const SizedBox(width: 6),
                    Flexible(
                      child: AppText(
                        _tituloDaVista(layer),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                          fontSize: 14.5,
                          fontWeight: FontWeight.w600,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    const SizedBox(width: 4),
                    const Icon(
                      CupertinoIcons.chevron_down,
                      size: 13,
                      color: AmColors.muted,
                    ),
                  ],
                ),
              ),
            ),
          ),
          // O menu carrega o estado (correcao 10.1.1): aceso e com um
          // ponto quando ha um modo ligado la dentro — avancado, grade,
          // auto-key desligado.
          CupertinoButton(
            key: const ValueKey('estudio-mais'),
            padding: const EdgeInsets.symmetric(horizontal: 10),
            onPressed: () => _abrirMenuMais(layer),
            child: AmMenuIcon(ativo: _avancado || _snap || !_autoKey),
          ),
        ],
      ),
    );
  }

  Future<void> _abrirMenuDaCamera(Scene3DLayer layer) {
    _playback.pause();
    return showMenuDaCamera(
      context,
      ref,
      widget.layerId,
      raiz: context,
      tempo: _time,
      cameraAtiva: _activeCamera(layer).id,
      vista: _view,
      temSelecao: _alvos.isNotEmpty,
      aoUsarCamera: _usarCamera,
      aoNovaCamera: _novaCamera,
      aoVerVista: _verVista,
      aoEnquadrarTudo: () => _frameAll(_layer ?? layer),
      aoEnquadrarSelecionado: () => _frameSelected(_layer ?? layer),
      aoAlterar: () => setState(() {}),
    );
  }

  /// O MENU DE TRES PONTOS: o que nao cabe na tela, mas existe.
  Future<void> _abrirMenuMais(Scene3DLayer layer) {
    _playback.pause();
    return folhaDoEstudio<void>(
      context,
      alturaFator: 0.85,
      builder: (ctx, setSheet) {
        final l = _layer ?? layer;
        return ListView(
          shrinkWrap: true,
          children: [
            const SecaoDoEstudio('Cena'),
            LinhaDoEstudio(
              key: const ValueKey('mais-cena'),
              icone: CupertinoIcons.list_bullet_indent,
              titulo: 'Objetos, luzes e cameras',
              subtitulo:
                  '${l.scene.nodes.length} objetos · '
                  '${l.scene.lights.length} luzes · '
                  '${l.allCameras.length} cameras',
              chevron: true,
              onTap: () {
                Navigator.pop(ctx);
                _abrirHierarquia();
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('mais-buscar'),
              icone: CupertinoIcons.search,
              titulo: 'Buscar na cena',
              chevron: true,
              onTap: () {
                Navigator.pop(ctx);
                _abrirHierarquia(buscar: true);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('mais-ajustes'),
              icone: CupertinoIcons.slider_horizontal_3,
              titulo: 'Ajustes avancados da cena',
              subtitulo:
                  'A ficha completa: objetos, luzes, ambiente, '
                  'camera, foco e ajudas.',
              chevron: true,
              onTap: () {
                Navigator.pop(ctx);
                showScene3DSheet(context, ref, widget.layerId);
              },
            ),
            const SecaoDoEstudio('Camera e vista'),
            LinhaDoEstudio(
              key: const ValueKey('mais-enquadrar'),
              icone: CupertinoIcons.fullscreen,
              titulo: 'Enquadrar tudo',
              onTap: () {
                Navigator.pop(ctx);
                _frameAll(_layer ?? layer);
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('mais-alinhar'),
              icone: CupertinoIcons.camera_viewfinder,
              titulo: 'Alinhar camera a vista',
              subtitulo: _view == SceneView.camera
                  ? 'Primeiro escolha uma vista fixa ou livre no menu da '
                        'camera.'
                  : 'A camera no ar assume o enquadramento da vista atual.',
              onTap: _view == SceneView.camera
                  ? null
                  : () {
                      Navigator.pop(ctx);
                      _alinharCameraAVista(_layer ?? layer);
                    },
            ),
            LinhaDoEstudio(
              key: const ValueKey('mais-salvar-vista'),
              icone: CupertinoIcons.bookmark,
              titulo: 'Salvar vista',
              subtitulo: 'Guarda o enquadramento atual com nome.',
              onTap: () {
                Navigator.pop(ctx);
                _salvarVista(_layer ?? layer);
              },
            ),
            const SecaoDoEstudio('Edicao'),
            LinhaDoEstudio(
              key: const ValueKey('mais-autokey'),
              icone: CupertinoIcons.circle_fill,
              titulo: 'Auto-key',
              subtitulo: 'Cada mudanca no tempo vira um keyframe.',
              ligado: _autoKey,
              onTap: () {
                setState(() => _autoKey = !_autoKey);
                setSheet(() {});
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('mais-grade'),
              icone: CupertinoIcons.grid,
              titulo: 'Encaixar na grade',
              subtitulo:
                  'Mover de ${passoDeMover.round()} em ${passoDeMover.round()}, '
                  'girar de ${passoDeGirar.round()} em ${passoDeGirar.round()} '
                  'graus, escalar de $passoDeEscalar em $passoDeEscalar.',
              ligado: _snap,
              onTap: () {
                setState(() => _snap = !_snap);
                setSheet(() {});
              },
            ),
            const SecaoDoEstudio('Tela'),
            if (_avancado)
              LinhaDoEstudio(
                key: const ValueKey('mais-minivista'),
                icone: CupertinoIcons.rectangle_on_rectangle,
                titulo: 'Mini-vista',
                ligado: _showMiniView,
                onTap: () {
                  setState(() => _showMiniView = !_showMiniView);
                  setSheet(() {});
                },
              ),
            LinhaDoEstudio(
              key: const ValueKey('mais-dicas'),
              icone: CupertinoIcons.lightbulb,
              titulo: 'Ver as dicas de novo',
              onTap: () {
                Navigator.pop(ctx);
                setState(() => _dica = 0);
              },
            ),
            // OS TUTORIAIS EM VIDEO: gravacoes do proprio app montando uma
            // cena do zero. O primeiro com um cubo; o segundo com os
            // modelos, a animacao e os cortes de camera.
            LinhaDoEstudio(
              key: const ValueKey('mais-tutorial'),
              icone: CupertinoIcons.play_rectangle,
              titulo: 'Tutorial: primeira cena 3D (1 min)',
              onTap: () {
                Navigator.pop(ctx);
                _playback.pause();
                Navigator.of(context).push(
                  MaterialPageRoute<void>(
                    builder: (_) => const TutorialScreen(id: 'cena3d'),
                  ),
                );
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('mais-tutorial-completo'),
              icone: CupertinoIcons.cube_box,
              titulo: 'Tutorial: modelos, animação e câmeras (1min24)',
              onTap: () {
                Navigator.pop(ctx);
                _playback.pause();
                Navigator.of(context).push(
                  MaterialPageRoute<void>(
                    builder: (_) => const TutorialScreen(id: 'cena-completa'),
                  ),
                );
              },
            ),
            const SizedBox(height: 12),
          ],
        );
      },
    );
  }

  Future<void> _abrirHierarquia({bool buscar = false}) {
    _playback.pause();
    final layer = _layer;
    if (layer == null) return Future.value();
    return showHierarquia(
      context,
      ref,
      widget.layerId,
      raiz: context,
      tempo: _time,
      selecionado: _selectedLight ?? _selected,
      cameraAtiva: _activeCamera(layer).id,
      buscar: buscar,
      aoEscolher: _escolherItem,
      aoAlterar: () {
        if (mounted) setState(() {});
      },
    );
  }

  void _escolherItem(String id, TipoDeItem tipo) {
    switch (tipo) {
      case TipoDeItem.no:
        _selecionar(id);
      case TipoDeItem.luz:
        _selecionarLuz(id);
      case TipoDeItem.camera:
        _selecionar(null);
        _usarCamera(id);
    }
  }

  /// A FAIXA DE CAMERAS: com duas ou mais, a troca e um toque.
  Widget _cameraStrip(Scene3DLayer layer) {
    final ativa = _activeCamera(layer).id;
    return SizedBox(
      height: 30,
      child: ListView(
        scrollDirection: Axis.horizontal,
        children: [
          for (final c in layer.allCameras) ...[
            ChipDoEstudio(
              key: ValueKey('faixa-camera-${c.id}'),
              label: c.name,
              compacto: true,
              aceso: c.id == ativa && _view == SceneView.camera,
              onTap: () => _usarCamera(c.id),
            ),
            const SizedBox(width: 5),
          ],
        ],
      ),
    );
  }

  /// AS ACOES RAPIDAS, flutuando no canto da vista.
  Widget _quickActions(Scene3DLayer layer) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceEvenly,
      children: [
        _acaoRapida(
          key: const ValueKey('estudio-adicionar'),
          icone: CupertinoIcons.add,
          tooltip: 'Adicionar',
          destaque: true,
          onTap: _abrirAdicionar,
        ),
        const SizedBox(width: 4),
        _acaoRapida(
          key: const ValueKey('estudio-focar'),
          icone: CupertinoIcons.viewfinder,
          tooltip: 'Focar (enquadrar o selecionado)',
          onTap: () => _frameSelected(layer),
        ),
        const SizedBox(width: 4),
        _acaoRapida(
          key: const ValueKey('estudio-cena'),
          icone: CupertinoIcons.list_bullet,
          tooltip: 'Cena: objetos, luzes e cameras',
          onTap: _abrirHierarquia,
        ),
        const SizedBox(width: 4),
        _acaoRapida(
          key: const ValueKey('estudio-desfazer'),
          icone: CupertinoIcons.arrow_uturn_left,
          tooltip: 'Desfazer',
          onTap: _controller.canUndo ? _controller.undo : null,
        ),
        const SizedBox(width: 4),
        _acaoRapida(
          key: const ValueKey('estudio-refazer'),
          icone: CupertinoIcons.arrow_uturn_right,
          tooltip: 'Refazer',
          onTap: _controller.canRedo ? _controller.redo : null,
        ),
      ],
    );
  }

  Widget _acaoRapida({
    required Key key,
    required IconData icone,
    required String tooltip,
    required VoidCallback? onTap,
    bool destaque = false,
  }) {
    final ligado = onTap != null;
    return Tooltip(
      message: tooltip,
      child: GestureDetector(
        key: key,
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Container(
          width: 40,
          height: 40,
          decoration: BoxDecoration(
            color: destaque
                ? AmColors.action
                : AmColors.panel.withValues(alpha: .82),
            shape: BoxShape.circle,
          ),
          child: Icon(
            icone,
            size: 20,
            color: destaque
                ? AmColors.bg
                : ligado
                ? AmColors.text
                : AmColors.muted.withValues(alpha: .45),
          ),
        ),
      ),
    );
  }

  Future<void> _abrirAdicionar() {
    _playback.pause();
    return showAdicionar(
      context,
      ref,
      widget.layerId,
      aoCriarNo: _selecionar,
      aoCriarLuz: _selecionarLuz,
      aoNovaCamera: _novaCamera,
      aoAmbiente: () =>
          showScene3DSheet(context, ref, widget.layerId, abaInicial: 2),
    );
  }

  /// AS DICAS: um cartao pequeno, quatro passos, "Entendi" fecha para
  /// sempre.
  Widget _hintCard() {
    final i = _dica.clamp(0, dicasDoEstudio.length - 1);
    final ultima = i == dicasDoEstudio.length - 1;
    void fechar() {
      setState(() => _dica = -1);
      _prefs.marcarDicasVistas(true);
    }

    return Container(
      key: const ValueKey('estudio-dica'),
      padding: const EdgeInsets.fromLTRB(14, 10, 10, 8),
      decoration: BoxDecoration(
        color: AmColors.panel.withValues(alpha: .94),
        borderRadius: BorderRadius.circular(14),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          AppText(
            dicasDoEstudio[i],
            style: const TextStyle(
              fontSize: 12.5,
              height: 1.35,
              color: AmColors.text,
            ),
          ),
          const SizedBox(height: 4),
          Row(
            children: [
              AppText(
                '${i + 1}/${dicasDoEstudio.length}',
                style: const TextStyle(fontSize: 11, color: AmColors.muted),
              ),
              const Spacer(),
              if (!ultima)
                CupertinoButton(
                  key: const ValueKey('estudio-dica-proxima'),
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                  minimumSize: const Size(0, 30),
                  onPressed: () => setState(() => _dica = i + 1),
                  child: const AppText('Proxima',
                    style: TextStyle(fontSize: 13, color: AmColors.text),
                  ),
                ),
              CupertinoButton(
                key: const ValueKey('estudio-dica-entendi'),
                padding: const EdgeInsets.symmetric(horizontal: 10),
                minimumSize: const Size(0, 30),
                onPressed: fechar,
                child: const AppText('Entendi',
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: AmColors.accent,
                  ),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  // ------------------------------------------------------ contexto

  /// A BARRA DE CONTEXTO: muda com o que esta selecionado. Nada: a
  /// camera no ar e o que se pode criar. Objeto: focar, material,
  /// keyframe e as acoes. Varios: agrupar e excluir. Luz: os quatro
  /// ajustes.
  Widget _contextBar(Scene3DLayer layer, SceneNode? node, Light3D? luz) {
    final chips = <Widget>[];
    if (luz != null) {
      chips.addAll(_contextoDaLuz(layer, luz));
    } else if (_alvos.length > 1) {
      chips.addAll(_contextoDeVarios(layer));
    } else if (node != null) {
      chips.addAll(_contextoDoObjeto(layer, node));
    } else {
      chips.addAll(_contextoDaCamera(layer));
    }
    // Uma linha que rola, com TODOS os chips construidos: sao poucos, e
    // quem procura um pelo nome (ou pela chave) tem de acha-lo.
    return SizedBox(
      key: const ValueKey('estudio-contexto'),
      height: 38,
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: 10),
        child: Row(
          children: [
            for (var i = 0; i < chips.length; i++) ...[
              if (i > 0) const SizedBox(width: 6),
              chips[i],
            ],
          ],
        ),
      ),
    );
  }

  bool _keyframeAqui(Scene3DLayer layer) =>
      _tracks(layer).any((track) => track.hasKeyframeAt(_time));

  Widget _chipKeyframe(Scene3DLayer layer, {bool travado = false}) {
    final aqui = _keyframeAqui(layer);
    return ChipDoEstudio(
      key: const ValueKey('contexto-keyframe'),
      label: 'Keyframe',
      icone: aqui ? Icons.diamond : Icons.diamond_outlined,
      aceso: aqui,
      onTap: travado ? null : () => _alternarKeyframe(layer),
    );
  }

  List<Widget> _contextoDaCamera(Scene3DLayer layer) {
    final cam = _activeCamera(layer);
    return [
      ChipDoEstudio(
        key: const ValueKey('contexto-adicionar'),
        label: 'Adicionar',
        icone: CupertinoIcons.add,
        onTap: _abrirAdicionar,
      ),
      // O nome da camera ja esta na barra de cima; aqui, as acoes dela.
      ChipDoEstudio(
        key: const ValueKey('contexto-camera'),
        label: 'Mais',
        icone: CupertinoIcons.chevron_down,
        onTap: () => _acoesDaCamera(layer, cam),
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-lente'),
        label: 'Lente',
        icone: CupertinoIcons.circle_lefthalf_fill,
        onTap: () {
          _playback.pause();
          showLente(
            context,
            ref,
            widget.layerId,
            cam.id,
            tempo: _time,
            autoKey: _autoKey,
          );
        },
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-olhar'),
        label: cam.lookAtNodeId == null ? 'Olhar para' : 'Olhando',
        icone: CupertinoIcons.scope,
        aceso: cam.lookAtNodeId != null,
        onTap: () {
          _playback.pause();
          showOlharPara(
            context,
            ref,
            widget.layerId,
            cam.id,
            aoAlterar: () {
              if (mounted) setState(() {});
            },
          );
        },
      ),
      _chipKeyframe(layer),
      ChipDoEstudio(
        key: const ValueKey('contexto-luz'),
        label: 'Luz',
        icone: CupertinoIcons.lightbulb,
        onTap: () {
          _playback.pause();
          showLuzes(
            context,
            ref,
            widget.layerId,
            aoEscolher: _selecionarLuz,
            aoNova: _abrirAdicionar,
          );
        },
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-ambiente'),
        label: 'Ambiente',
        icone: CupertinoIcons.sun_haze,
        onTap: () =>
            showScene3DSheet(context, ref, widget.layerId, abaInicial: 2),
      ),
    ];
  }

  Future<void> _acoesDaCamera(Scene3DLayer layer, Camera3D cam) {
    _playback.pause();
    return showAcoesDoItem(
      context,
      ref,
      widget.layerId,
      raiz: context,
      item: ItemDaCena(
        id: cam.id,
        nome: cam.name,
        tipo: TipoDeItem.camera,
        ativo: true,
      ),
      tempo: _time,
      aoEscolher: _escolherItem,
      aoUsarCamera: () => _usarCamera(cam.id),
      aoAlterar: () {
        if (mounted) setState(() {});
      },
      aoExcluir: () {
        if (mounted) setState(() {});
      },
    );
  }

  List<Widget> _contextoDoObjeto(Scene3DLayer layer, SceneNode node) {
    return [
      ChipDoEstudio(
        key: const ValueKey('contexto-objeto'),
        label: node.name,
        icone: node.locked
            ? CupertinoIcons.lock_fill
            : node.isNull
            ? CupertinoIcons.folder
            : CupertinoIcons.chevron_down,
        onTap: () => _acoesDoObjeto(node),
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-linkar-nulo'),
        label: node.parentId == null ? 'Linkar nulo' : 'Vinculado',
        icone: CupertinoIcons.link,
        onTap: node.locked ? null : () => _linkarObjeto(layer, node),
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-focar'),
        label: 'Focar',
        icone: CupertinoIcons.viewfinder,
        onTap: () => _frameSelected(layer),
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-material'),
        label: 'Material',
        icone: CupertinoIcons.paintbrush,
        onTap: node.isNull
            ? null
            : () {
                _playback.pause();
                showMaterialSimples(context, ref, widget.layerId, node.id);
              },
      ),
      _chipKeyframe(layer, travado: node.locked),
      ChipDoEstudio(
        key: const ValueKey('contexto-propriedades'),
        label: 'Propriedades',
        icone: CupertinoIcons.slider_horizontal_3,
        onTap: () => showScene3DSheet(
          context,
          ref,
          widget.layerId,
          abaInicial: 0,
          noInicial: node.id,
        ),
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-limpar'),
        label: 'Fechar',
        icone: CupertinoIcons.xmark,
        onTap: () => _selecionar(null),
      ),
    ];
  }

  Future<void> _linkarObjeto(Scene3DLayer layer, SceneNode node) async {
    _playback.pause();
    final nodes = {for (final n in layer.scene.nodes) n.id: n};
    bool eligible(SceneNode candidate) {
      final seen = <String>{};
      SceneNode? current = candidate;
      while (current != null && seen.add(current.id)) {
        if (current.id == node.id) return false;
        current = nodes[current.parentId];
      }
      return current == null;
    }

    final candidates = [
      ...layer.scene.nodes.where((n) => n.isNull && eligible(n)),
      ...layer.scene.nodes.where((n) => !n.isNull && eligible(n)),
    ];
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: AmColors.panel,
      builder: (ctx) => SafeArea(
        child: ListView(
          shrinkWrap: true,
          children: [
            const ListTile(title: AppText('Linkar a objeto nulo ou objeto')),
            ListTile(
              leading: const Icon(CupertinoIcons.add),
              title: const AppText('Criar nulo e linkar'),
              onTap: () {
                final parent = _controller.addSceneNull(widget.layerId);
                _controller.setSceneNodeParent(widget.layerId, node.id, parent);
                Navigator.pop(ctx);
              },
            ),
            ListTile(
              leading: const Icon(CupertinoIcons.link),
              title: const AppText('Sem vinculo'),
              onTap: () {
                _controller.setSceneNodeParent(widget.layerId, node.id, null);
                Navigator.pop(ctx);
              },
            ),
            for (final parent in candidates)
              ListTile(
                title: AppText(parent.name),
                leading: Icon(
                  parent.isNull ? CupertinoIcons.folder : CupertinoIcons.cube,
                ),
                selected: node.parentId == parent.id,
                onTap: () {
                  _controller.setSceneNodeParent(
                    widget.layerId,
                    node.id,
                    parent.id,
                  );
                  Navigator.pop(ctx);
                },
              ),
          ],
        ),
      ),
    );
  }

  Future<void> _acoesDoObjeto(SceneNode node) {
    _playback.pause();
    return showAcoesDoItem(
      context,
      ref,
      widget.layerId,
      raiz: context,
      item: ItemDaCena(
        id: node.id,
        nome: node.name,
        tipo: TipoDeItem.no,
        visivel: node.visible,
        travado: node.locked,
        grupo: node.isNull,
        ativo: true,
      ),
      tempo: _time,
      aoEscolher: _escolherItem,
      aoAlterar: () {
        if (mounted) setState(() {});
      },
      aoExcluir: () => _selecionar(null),
    );
  }

  List<Widget> _contextoDeVarios(Scene3DLayer layer) {
    final n = _alvos.length;
    return [
      ChipDoEstudio(
        key: const ValueKey('contexto-varios'),
        label: '$n objetos',
        icone: CupertinoIcons.square_stack_3d_up,
        aceso: true,
        onTap: _abrirHierarquia,
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-agrupar'),
        label: 'Agrupar',
        icone: CupertinoIcons.folder_badge_plus,
        onTap: () {
          final grupo = _controller.groupSceneNodes(widget.layerId, _alvos);
          if (grupo.isNotEmpty) _selecionar(grupo);
        },
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-focar'),
        label: 'Focar',
        icone: CupertinoIcons.viewfinder,
        onTap: () => _frameSelected(layer),
      ),
      _chipKeyframe(layer),
      ChipDoEstudio(
        key: const ValueKey('contexto-excluir'),
        label: 'Excluir',
        icone: CupertinoIcons.trash,
        onTap: () {
          for (final id in _alvos.toList()) {
            if (layer.scene.nodeById(id)?.locked ?? true) continue;
            _controller.removeSceneNode(widget.layerId, id);
          }
          _selecionar(null);
        },
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-limpar'),
        label: 'Limpar selecao',
        icone: CupertinoIcons.xmark,
        onTap: () => _selecionar(null),
      ),
    ];
  }

  List<Widget> _contextoDaLuz(Scene3DLayer layer, Light3D luz) {
    void editar(Light3D Function(Light3D) fn) =>
        _controller.updateSceneLight(widget.layerId, luz.id, fn);
    Future<void> ajustar() {
      _playback.pause();
      return showLuzSimples(context, ref, widget.layerId, luz.id);
    }

    return [
      ChipDoEstudio(
        key: const ValueKey('contexto-luz-nome'),
        label: luzLabel(luz.kind),
        icone: CupertinoIcons.chevron_down,
        onTap: () {
          _playback.pause();
          showAcoesDoItem(
            context,
            ref,
            widget.layerId,
            raiz: context,
            item: ItemDaCena(
              id: luz.id,
              nome: luzLabel(luz.kind),
              tipo: TipoDeItem.luz,
              ativo: true,
            ),
            tempo: _time,
            aoEscolher: _escolherItem,
            aoAlterar: () {
              if (mounted) setState(() {});
            },
            aoExcluir: () => _selecionarLuz(null),
          );
        },
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-intensidade'),
        label: 'Intensidade',
        icone: CupertinoIcons.sun_max,
        onTap: ajustar,
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-cor'),
        label: 'Cor',
        icone: CupertinoIcons.drop,
        onTap: () {
          _playback.pause();
          showColorPicker(
            context,
            initial: luz.color,
            withAlpha: false,
            onChanged: (c) => editar((l) => l.copyWith(color: c)),
          );
        },
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-tipo'),
        label: 'Tipo',
        icone: CupertinoIcons.lightbulb,
        onTap: ajustar,
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-sombras'),
        label: 'Sombras',
        icone: CupertinoIcons.moon,
        aceso: luz.castsShadow,
        onTap: () => editar((l) => l.copyWith(castsShadow: !l.castsShadow)),
      ),
      ChipDoEstudio(
        key: const ValueKey('contexto-limpar'),
        label: 'Fechar',
        icone: CupertinoIcons.xmark,
        onTap: () => _selecionarLuz(null),
      ),
    ];
  }

  // ------------------------------------------------------ vista

  Widget _viewport(Scene3DLayer layer, Size size) {
    final cam = _renderCamera(layer);
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTapUp: (d) => _handleTap(layer, d.localPosition, size),
      onDoubleTapDown: (d) => _handleDoubleTap(layer, d.localPosition, size),
      onDoubleTap: () {},
      onLongPressStart: (d) => _handleLongPress(layer, d.localPosition, size),
      onScaleStart: (d) {
        final frame = renderScene(layer.scene, cam, size, _time);
        final hit = pickNodeAt(frame, d.localFocalPoint);
        final startIntent = resolveTouch(
          onSelectedLayer: hit != null && _alvos.contains(hit),
          onOtherLayer: hit != null && !_alvos.contains(hit),
          navigationMode: _navigationMode,
        );
        if (startIntent == TouchIntent.selectLayer) {
          _selecionar(hit);
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
        _guardarBases(layer);
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
            if (!Scene3DGpu.indisponivel)
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
            // O PAINEL DE DESEMPENHO, so em build de desenvolvimento: e
            // aqui que se ve o quadro cair, e por qual fase.
            if (mostrarHudDesempenho)
              const Positioned(right: 6, top: 6, child: HudDesempenho()),
          ],
        ),
      ),
    );
  }

  /// Guarda o ponto de partida de cada alvo: o encaixe na grade e o
  /// arrasto de varios precisam do valor bruto acumulado desde o
  /// inicio, nao do ultimo valor ja encaixado.
  void _guardarBases(Scene3DLayer layer) {
    _base.clear();
    _accMundo = Vec3.zero;
    _accDx = 0;
    _accDy = 0;
    for (final id in _alvos) {
      final node = layer.scene.nodeById(id);
      if (node == null) continue;
      _base[id] = _BaseDoGesto(
        resolveNodeTransform(layer.scene, node, _time).position,
        node.rotX.valueAt(_time),
        node.rotY.valueAt(_time),
        node.rotZ.valueAt(_time),
        node.scale.valueAt(_time),
      );
    }
  }

  _BaseDoGesto _baseDe(Scene3DLayer layer, SceneNode node) =>
      _base[node.id] ??= _BaseDoGesto(
        resolveNodeTransform(layer.scene, node, _time).position,
        node.rotX.valueAt(_time),
        node.rotY.valueAt(_time),
        node.rotZ.valueAt(_time),
        node.scale.valueAt(_time),
      );

  AnimatedDouble _valor(AnimatedDouble track, double v) =>
      editMotionValue(track, _time, v);

  /// Arrastar o(s) selecionado(s) no plano da tela, com a ferramenta
  /// que estiver na barra: mover, girar ou escalar. Com a grade ligada,
  /// o valor bruto acumulado e que se encaixa — e o eixo travado corta
  /// o que nao e dele.
  void _moveSelected(Scene3DLayer layer, Offset delta, RenderCamera cam) {
    final alvos = [
      for (final id in _alvos)
        if (layer.scene.nodeById(id) case final n? when !n.locked) n,
    ];
    if (alvos.isEmpty) return;
    _abrirGestoNoControlador();
    _accDx += delta.dx;
    _accDy += delta.dy;

    if (_tool == FerramentaDoEstudio.girar) {
      for (final node in alvos) {
        final b = _baseDe(layer, node);
        var rx = b.rotX, ry = b.rotY, rz = b.rotZ;
        switch (_eixo) {
          case EixoTravado.livre:
            rx = b.rotX + _accDy * .5;
            ry = b.rotY + _accDx * .5;
          case EixoTravado.x:
            rx = b.rotX + _accDx * .5;
          case EixoTravado.y:
            ry = b.rotY + _accDx * .5;
          case EixoTravado.z:
            rz = b.rotZ + _accDx * .5;
        }
        if (_snap) {
          rx = encaixar(rx, passoDeGirar);
          ry = encaixar(ry, passoDeGirar);
          rz = encaixar(rz, passoDeGirar);
        }
        _controller.updateSceneNode(
          widget.layerId,
          node.id,
          (n) => n.copyWith(
            rotX: rx == b.rotX ? null : _valor(n.rotX, rx),
            rotY: ry == b.rotY ? null : _valor(n.rotY, ry),
            rotZ: rz == b.rotZ ? null : _valor(n.rotZ, rz),
          ),
        );
      }
      return;
    }
    if (_tool == FerramentaDoEstudio.escalar) {
      final fator = math.exp((_accDx - _accDy) * .008);
      for (final node in alvos) {
        final b = _baseDe(layer, node);
        var s = (b.escala * fator).clamp(.001, 1000.0);
        if (_snap) s = math.max(passoDeEscalar, encaixar(s, passoDeEscalar));
        _controller.updateSceneNode(
          widget.layerId,
          node.id,
          (n) => n.copyWith(scale: _valor(n.scale, s)),
        );
      }
      return;
    }
    // MOVER: no plano da tela, na profundidade do objeto principal.
    final basis = cameraBasis(cam);
    final principal = layer.scene.nodeById(_selected ?? '') ?? alvos.first;
    final p = resolveNodeTransform(layer.scene, principal, _time).position;
    final dist = (p - cam.position).dot(basis.forward).abs();
    final k = cam.orthographic ? 1 / cam.orthoScale : dist / math.max(1, 600);
    final worldShift = basis.right * (delta.dx * k) - basis.up * (delta.dy * k);
    _accMundo = _accMundo + travarEixo(worldShift, _eixo);
    for (final node in alvos) {
      final b = _baseDe(layer, node);
      var desejado = b.pos + _accMundo;
      if (_snap) desejado = encaixarVec3(desejado, passoDeMover);
      final atual = resolveNodeTransform(layer.scene, node, _time).position;
      final falta = desejado - atual;
      if (falta.length < 1e-9) continue;
      final shift = sceneLocalDelta(layer.scene, node, _time, falta);
      _controller.updateSceneNode(
        widget.layerId,
        node.id,
        (n) => n.copyWith(
          x: _valor(n.x, n.x.valueAt(_time) + shift.x),
          y: _valor(n.y, n.y.valueAt(_time) + shift.y),
          z: _valor(n.z, n.z.valueAt(_time) + shift.z),
        ),
      );
    }
  }

  /// MINI-VISTA: arrastavel, redimensionavel, com a vista trocavel e
  /// sumivel. Resolve 90% do que quatro janelas resolvem, em 25% do
  /// espaco. So no modo avancado.
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
                child: AppText(
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
      height: 36,
      child: ListView.separated(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 3),
        itemCount: views.length + (_showMiniView ? 0 : 1),
        separatorBuilder: (_, _) => const SizedBox(width: 6),
        itemBuilder: (_, i) {
          if (i == views.length) {
            return ChipDoEstudio(
              label: 'Mini-vista',
              compacto: true,
              onTap: () => setState(() => _showMiniView = true),
            );
          }
          final v = views[i];
          return ChipDoEstudio(
            label: sceneViewLabel(v),
            compacto: true,
            aceso: v == _view,
            onTap: () => _verVista(v),
          );
        },
      ),
    );
  }

  // ------------------------------------------------------ tempo

  List<AnimatedDouble> _tracks(Scene3DLayer layer) {
    final node = layer.scene.nodeById(_selected ?? '');
    return node == null
        ? cameraMotionTracks(_activeCamera(layer))
        : nodeMotionTracks(node);
  }

  /// Aplica uma edicao de trilhas a TODOS os alvos (ou a camera).
  void _mapTracks(AnimatedDouble Function(AnimatedDouble) edit) {
    _playback.pause();
    final layer = _layer;
    if (layer == null) return;
    final alvos = [
      for (final id in _alvos)
        if (layer.scene.nodeById(id) case final n? when !n.locked) n,
    ];
    if (alvos.isEmpty) {
      _setCamera(mapCameraMotion(_activeCamera(layer), edit));
      return;
    }
    _controller.beginGesture();
    for (final node in alvos) {
      _controller.updateSceneNode(
        widget.layerId,
        node.id,
        (n) => mapNodeMotion(n, edit),
      );
    }
    _controller.endGesture();
  }

  void _alternarKeyframe(Scene3DLayer layer) {
    final aqui = _keyframeAqui(layer);
    _mapTracks(
      (track) => aqui
          ? track.withoutKeyframe(_time)
          : editMotionValue(track, _time, track.valueAt(_time)),
    );
  }

  Widget _timeRow(Scene3DLayer layer, SceneNode? node) {
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

    Widget miudo(
      IconData icone,
      String tooltip,
      VoidCallback? onTap, {
      Color? cor,
      Key? key,
    }) => IconButton(
      key: key,
      tooltip: tooltip,
      visualDensity: VisualDensity.compact,
      padding: EdgeInsets.zero,
      constraints: const BoxConstraints(minWidth: 34, minHeight: 34),
      onPressed: onTap,
      icon: Icon(icone, size: 20, color: cor ?? AmColors.text),
    );

    return Container(
      color: AmColors.panel,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          SizedBox(
            height: 46,
            child: Row(
              children: [
                miudo(
                  _playback.playing.value ? Icons.pause : Icons.play_arrow,
                  _playback.playing.value ? 'Pausar' : 'Reproduzir cena',
                  _playback.toggle,
                  cor: AmColors.accent,
                ),
                Expanded(
                  child: AmTickRuler(
                    key: const ValueKey('scene-motion-time'),
                    height: 44,
                    unitsPerPixel: duration / 1e6 / 300,
                    min: 0,
                    max: duration / 1e6,
                    value:
                        _time.inMicroseconds.toDouble().clamp(0, duration) /
                        1e6,
                    onChanged: (v) => seek((v * 1e6).round()),
                  ),
                ),
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 8),
                  child: AppText(
                    '${(_time.inMicroseconds / 1e6).toStringAsFixed(2)} s',
                    style: const TextStyle(fontSize: 12, color: AmColors.text),
                  ),
                ),
              ],
            ),
          ),
          MotionKeyframeTrack(
            key: const ValueKey('scene-motion-keyframes'),
            keysUs: keys,
            duration: layer.duration,
            time: _time,
            label: node == null ? 'Camera' : node.name,
            onSeek: (time) => seek(time.inMicroseconds),
          ),
          SizedBox(
            height: 36,
            child: Row(
              children: [
                // O ALVO DOS KEYFRAMES: o objeto selecionado, ou a camera.
                miudo(
                  node == null ? CupertinoIcons.videocam : CupertinoIcons.cube,
                  node == null
                      ? 'Keyframes da camera (toque para escolher um objeto)'
                      : 'Keyframes do objeto (toque para voltar a camera)',
                  node == null ? _abrirHierarquia : () => _selecionar(null),
                  cor: AmColors.muted,
                  key: const ValueKey('tempo-alvo'),
                ),
                const Spacer(),
                miudo(
                  Icons.skip_previous,
                  'Keyframe anterior',
                  previous == null ? null : () => seek(previous),
                ),
                miudo(
                  here ? Icons.diamond : Icons.diamond_outlined,
                  here ? 'Remover keyframe' : 'Adicionar keyframe',
                  node?.locked == true ? null : () => _alternarKeyframe(layer),
                  cor: AmColors.accent,
                  key: const ValueKey('tempo-keyframe'),
                ),
                miudo(
                  Icons.skip_next,
                  'Proximo keyframe',
                  next == null ? null : () => seek(next),
                ),
                SizedBox(
                  width: 34,
                  child: PopupMenuButton<Easing>(
                    tooltip: 'Curva do movimento',
                    padding: EdgeInsets.zero,
                    icon: const Icon(
                      Icons.show_chart,
                      size: 20,
                      color: AmColors.accent,
                    ),
                    itemBuilder: (_) => [
                      const PopupMenuItem(
                        value: Easing.linear,
                        child: AppText('Linear'),
                      ),
                      const PopupMenuItem(
                        value: Easing.easeInOut,
                        child: AppText('Suave'),
                      ),
                      const PopupMenuItem(
                        value: Easing.easeOut,
                        child: AppText('Desacelerar'),
                      ),
                      const PopupMenuItem(
                        value: Easing.overshoot,
                        child: AppText('Antecipacao e retorno'),
                      ),
                    ],
                    onSelected: (ease) => _mapTracks((track) {
                      final start = track.keyframes
                          .where((k) => k.time <= _time)
                          .lastOrNull;
                      return start == null
                          ? track
                          : track.withEase(start.time, ease);
                    }),
                  ),
                ),
                const Spacer(),
                if (_avancado) ...[
                  ChipDoEstudio(
                    key: const ValueKey('tempo-autokey'),
                    label: 'Auto-key',
                    compacto: true,
                    aceso: _autoKey,
                    onTap: () => setState(() => _autoKey = !_autoKey),
                  ),
                  const SizedBox(width: 8),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }

  /// AS QUATRO FERRAMENTAS, sempre no mesmo lugar. No modo avancado, a
  /// grade e o eixo travado ficam ao lado.
  Widget _toolStrip() {
    return Container(
      color: AmColors.panel,
      padding: const EdgeInsets.fromLTRB(10, 2, 10, 8),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            children: [
              for (final f in FerramentaDoEstudio.values) ...[
                Expanded(
                  child: GestureDetector(
                    key: ValueKey('ferramenta-${f.name}'),
                    behavior: HitTestBehavior.opaque,
                    onTap: () => setState(() => _tool = f),
                    child: Container(
                      height: 40,
                      alignment: Alignment.center,
                      decoration: BoxDecoration(
                        color: _tool == f ? AmColors.accentDim : AmColors.chip,
                        borderRadius: BorderRadius.circular(11),
                      ),
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(
                            switch (f) {
                              FerramentaDoEstudio.selecionar =>
                                CupertinoIcons.hand_point_left,
                              FerramentaDoEstudio.mover => CupertinoIcons.move,
                              FerramentaDoEstudio.girar =>
                                CupertinoIcons.rotate_right,
                              FerramentaDoEstudio.escalar =>
                                CupertinoIcons.arrow_up_left_arrow_down_right,
                            },
                            size: 15,
                            color: _tool == f
                                ? AmColors.accent
                                : AmColors.muted,
                          ),
                          const SizedBox(height: 2),
                          // Uma linha sempre: em tela estreita o nome
                          // encolhe, nao quebra.
                          FittedBox(
                            fit: BoxFit.scaleDown,
                            child: AppText(
                              ferramentaLabel(f),
                              maxLines: 1,
                              style: TextStyle(
                                fontSize: 10.5,
                                fontWeight: _tool == f
                                    ? FontWeight.w600
                                    : FontWeight.w500,
                                color: _tool == f
                                    ? AmColors.accent
                                    : AmColors.muted,
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
                if (f != FerramentaDoEstudio.values.last)
                  const SizedBox(width: 6),
              ],
            ],
          ),
          if (_avancado)
            Padding(
              padding: const EdgeInsets.only(top: 6),
              child: SizedBox(
                height: 30,
                child: ListView(
                  scrollDirection: Axis.horizontal,
                  children: [
                    ChipDoEstudio(
                      key: const ValueKey('ferramenta-grade'),
                      label: 'Grade',
                      icone: CupertinoIcons.grid,
                      compacto: true,
                      aceso: _snap,
                      onTap: () => setState(() => _snap = !_snap),
                    ),
                    const SizedBox(width: 10),
                    const Padding(
                      padding: EdgeInsets.only(right: 6),
                      child: Center(
                        child: AppText('Eixo',
                          style: TextStyle(fontSize: 11, color: AmColors.muted),
                        ),
                      ),
                    ),
                    for (final e in EixoTravado.values) ...[
                      ChipDoEstudio(
                        key: ValueKey('eixo-${e.name}'),
                        label: eixoLabel(e),
                        compacto: true,
                        aceso: _eixo == e,
                        onTap: () => setState(() => _eixo = e),
                      ),
                      const SizedBox(width: 5),
                    ],
                  ],
                ),
              ),
            ),
        ],
      ),
    );
  }

  /// OS COMANDOS DE CAMERA do modo avancado — os mesmos de sempre.
  Widget _commandBar(Scene3DLayer layer) {
    return Container(
      padding: const EdgeInsets.fromLTRB(10, 0, 10, 8),
      color: AmColors.panel,
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        child: Row(
          children: [
            ChipDoEstudio(
              icone: CupertinoIcons.fullscreen,
              label: 'Enquadrar tudo',
              compacto: true,
              onTap: () => _frameAll(layer),
            ),
            const SizedBox(width: 6),
            ChipDoEstudio(
              icone: CupertinoIcons.viewfinder,
              label: 'Enquadrar selecionado',
              compacto: true,
              onTap: _alvos.isEmpty ? null : () => _frameSelected(layer),
            ),
            const SizedBox(width: 6),
            // O COMANDO MAIS USADO: navegar livre ate achar o
            // enquadramento, e so entao a camera assumir ele.
            ChipDoEstudio(
              icone: CupertinoIcons.camera_viewfinder,
              label: 'Alinhar camera a vista',
              compacto: true,
              onTap: _view == SceneView.camera
                  ? null
                  : () => _alinharCameraAVista(layer),
            ),
            const SizedBox(width: 6),
            ChipDoEstudio(
              icone: CupertinoIcons.bookmark,
              label: 'Salvar vista',
              compacto: true,
              onTap: () => _salvarVista(layer),
            ),
            const SizedBox(width: 6),
            ChipDoEstudio(
              icone: CupertinoIcons.circle_lefthalf_fill,
              label: 'Foco da lente no selecionado',
              compacto: true,
              onTap: _selected == null ? null : () => _focarLente(layer),
            ),
          ],
        ),
      ),
    );
  }
}
