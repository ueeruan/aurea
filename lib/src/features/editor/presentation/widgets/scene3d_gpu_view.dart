import 'package:flutter/widgets.dart';

import '../../application/motor3d_modo.dart';
import '../../application/scene3d_gpu.dart';
import '../../domain/camera3d.dart';
import '../../domain/scene3d.dart';
import 'scene3d_painter.dart';
import '../../application/renderer3d/filament_renderer.dart';
import 'filament_viewport.dart';

/// A CENA 3D DESENHADA PELA GPU — e, ate a GPU estar pronta, pelo pintor
/// de sempre, para nunca haver quadro vazio.
///
/// Na timeline a cena e um quadro 2D como qualquer camada: este widget
/// renderiza o instante [time] pela camera [renderCamera] e entrega o
/// resultado no canvas da composicao (o que a exportacao captura). O
/// Estudio usa o mesmo widget com a camera livre dele.
class Scene3DGpuView extends StatefulWidget {
  const Scene3DGpuView({
    super.key,
    required this.scene,
    required this.camera,
    required this.renderCamera,
    required this.time,
    this.view = SceneView.camera,
    this.rascunho = false,
    this.showHelpers = false,
    this.selectedNodeId,
    this.exporting = false,
  });

  final Scene3D scene;
  final Camera3D camera;
  final RenderCamera renderCamera;
  final Duration time;
  final SceneView view;

  /// Durante um gesto no Estudio: sem profundidade de campo, para navegar
  /// liso.
  final bool rascunho;
  final bool showHelpers;
  final String? selectedNodeId;
  final bool exporting;

  @override
  State<Scene3DGpuView> createState() => _Scene3DGpuViewState();
}

class _Scene3DGpuViewState extends State<Scene3DGpuView> {
  Scene3DGpu? _gpu;
  var _pronto = Scene3DGpu.pronto;

  /// Esta view ja contou como cena em GPU na tela? (Ver [MarcaGpuViva].)
  var _marcado = false;
  bool get _wantsFilament =>
      filamentPreviewEnabled &&
      (Motor3DPreferencia.instancia?.permiteGpu ?? true) &&
      !widget.exporting &&
      !widget.camera.dof.enabled &&
      !widget.renderCamera.orthographic &&
      FilamentRenderer.supports(widget.scene);
  bool _preparingLegacy = false;

  @override
  void initState() {
    super.initState();
    if (!_wantsFilament) _prepareLegacy();
  }

  void _prepareLegacy() {
    if (_pronto) {
      _marcar();
    } else if (!Scene3DGpu.indisponivel && !_preparingLegacy) {
      _preparingLegacy = true;
      Scene3DGpu.preparar().then((_) {
        _preparingLegacy = false;
        if (!mounted || _wantsFilament) return;
        setState(() => _pronto = Scene3DGpu.pronto);
        if (_pronto) _marcar();
      });
    }
  }

  @override
  void didUpdateWidget(covariant Scene3DGpuView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (_wantsFilament) {
      _gpu?.descartar();
      _gpu = null;
      if (_marcado) {
        MarcaGpuViva.saiu();
        _marcado = false;
      }
    } else {
      _prepareLegacy();
    }
  }

  void _marcar() {
    if (_marcado) return;
    _marcado = true;
    MarcaGpuViva.entrou();
  }

  @override
  void dispose() {
    if (_marcado) MarcaGpuViva.saiu();
    _gpu?.descartar();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_wantsFilament) {
      final nativePreview = FilamentViewport(
        scene: widget.rascunho
            ? widget.scene.copyWith(draftMode: true)
            : widget.scene,
        camera: widget.renderCamera,
        time: widget.time,
        fallback: CustomPaint(
          painter: Scene3DPainter(
            scene: widget.scene,
            camera: widget.camera,
            view: widget.view,
            time: widget.time,
            overrideCamera: widget.renderCamera,
          ),
          size: Size.infinite,
        ),
      );
      if (!widget.showHelpers) return nativePreview;
      return Stack(
        fit: StackFit.expand,
        children: [
          nativePreview,
          IgnorePointer(
            child: CustomPaint(
              painter: Scene3DPainter(
                scene: widget.scene,
                camera: widget.camera,
                view: widget.view,
                time: widget.time,
                overrideCamera: widget.renderCamera,
                showHelpers: true,
                helpersOnly: true,
                selectedNodeId: widget.selectedNodeId,
              ),
            ),
          ),
        ],
      );
    }
    if (!_pronto || widget.renderCamera.orthographic) {
      return CustomPaint(
        painter: Scene3DPainter(
          scene: widget.rascunho
              ? widget.scene.copyWith(draftMode: true)
              : widget.scene,
          camera: widget.camera,
          view: widget.view,
          time: widget.time,
          overrideCamera: widget.renderCamera,
          showHelpers: widget.showHelpers,
          selectedNodeId: widget.selectedNodeId,
        ),
        size: Size.infinite,
      );
    }
    final gpu = _gpu ??= Scene3DGpu();
    gpu.sincronizar(
      widget.scene,
      widget.time,
      rascunho: widget.rascunho,
      onMudou: () {
        if (mounted) setState(() {});
      },
    );
    gpu.configurarProfundidadeDeCampo(
      widget.camera,
      widget.time,
      rascunho: widget.rascunho || widget.view != SceneView.camera,
    );
    return CustomPaint(
      painter: _PintorGpu(
        gpu,
        widget.renderCamera,
        widget.scene.background,
        widget.rascunho,
        widget.exporting,
      ),
      size: Size.infinite,
    );
  }
}

class _PintorGpu extends CustomPainter {
  _PintorGpu(this.gpu, this.camera, this.fundo, this.rascunho, this.exporting);
  final bool rascunho, exporting;

  final Scene3DGpu gpu;
  final RenderCamera camera;

  /// O FUNDO da cena. O motor em GPU limpa para transparente e desenha o
  /// ceu so quando ha panorama; uma cena com cor de fundo (o preto do
  /// espaco) precisa dela pintada aqui, senao o que aparece atras e a
  /// composicao.
  final Color? fundo;

  @override
  void paint(Canvas canvas, Size size) {
    if (size.isEmpty) return;
    final area = Offset.zero & size;
    canvas.save();
    canvas.clipRect(area);
    if (fundo != null) canvas.drawRect(area, Paint()..color = fundo!);
    gpu.desenhar(
      canvas,
      area,
      gpu.camera(camera, size),
      rascunho: rascunho,
      exporting: exporting,
    );
    canvas.restore();
  }

  // O adaptador ja decide o que mudou; o quadro e sempre redesenhado
  // quando o widget e reconstruido (tempo ou cena novos).
  @override
  bool shouldRepaint(covariant _PintorGpu old) => true;
}
