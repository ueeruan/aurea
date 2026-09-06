import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import 'caption_highlight_painter.dart';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:video_player/video_player.dart';

import '../../application/texture_cache.dart';
import '../../application/blob_track_service.dart';
import '../../application/editor_controller.dart';
import '../../application/freehand_session.dart' show onionSkinProvider;
export '../../application/freehand_session.dart' show onionSkinProvider;
import '../../application/playback_controller.dart';
import '../../application/preview_stats.dart';
import '../../application/video_layer_manager.dart';
import '../../domain/cut.dart';
import '../../domain/cut_ops.dart';
import '../../domain/effect.dart';
import '../../domain/fx.dart';
import '../../domain/gear.dart';
import '../../domain/text_animator.dart' show valueNoise01;
import '../../domain/caption_highlight.dart';
import '../../domain/grid_rig.dart';
import '../../domain/layer.dart';
import '../../domain/layer_meta.dart';
import '../../domain/mask.dart';
import '../../domain/camera3d.dart';
import '../../domain/scene3d.dart';
import '../../domain/shape.dart';
import '../../domain/video_project.dart';
import 'animated_text.dart';
import 'blend_mask.dart';
import 'gradient4_painter.dart';
import 'custom_blend.dart';
import 'linear_light.dart';
import 'pixel_effect_engine.dart';
import '../../domain/pixel_effect.dart';
import '../../domain/bloom.dart';
import '../../domain/color_space.dart';
import 'mask_node_editor.dart';
import 'world3d_painter.dart';
import 'extrude_painter.dart';
import 'vignette_painter.dart';
import 'freehand_overlay.dart';
import '../../application/mesh_cache.dart';
import 'masked_box.dart';
import 'dither_layer.dart';
import 'preview_raster.dart';
import 'fx_lote2.dart';
import 'particles_painter.dart';
import '../../application/scene3d_gpu.dart';
import '../../application/renderer3d/filament_renderer.dart'
    show filamentPreviewEnabled;
import 'scene3d_painter.dart';
import 'scene3d_gpu_view.dart';

// Photos decode asynchronously and videos update their external textures.
// An automatic snapshot of either can retain a placeholder/previous frame.
// Keep imported media on the live compositor, including inside precomps.
bool _containsRasterMedia(List<Layer> layers) => layers.any(
  (layer) =>
      layer is ImageLayer ||
      layer is VideoLayer ||
      (layer is GroupLayer && _containsRasterMedia(layer.children)),
);

/// Palco: composicao renderizada em coordenadas logicas, escalada para
/// caber. Gestos editam a camada selecionada.
class PreviewStage extends ConsumerStatefulWidget {
  const PreviewStage({super.key, required this.playback, required this.videos});

  final PlaybackController playback;
  final VideoLayerManager videos;

  @override
  ConsumerState<PreviewStage> createState() => _PreviewStageState();
}

class _PreviewStageState extends ConsumerState<PreviewStage> {
  @override
  void initState() {
    super.initState();
    widget.playback.playing.addListener(_playbackChanged);
  }

  @override
  void didUpdateWidget(PreviewStage oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.playback != widget.playback) {
      oldWidget.playback.playing.removeListener(_playbackChanged);
      widget.playback.playing.addListener(_playbackChanged);
    }
  }

  void _playbackChanged() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    widget.playback.playing.removeListener(_playbackChanged);
    super.dispose();
  }

  double _startScale = 1;
  double _startRotation = 0;
  double _stageScale = 1;
  Offset _dragStartPos = Offset.zero;
  Offset _dragAccum = Offset.zero;

  void _onScaleStart(ScaleStartDetails d) {
    final id = ref.read(selectedLayerProvider);
    if (id == null) return;
    final layer = ref.read(editorControllerProvider).layerById(id);
    if (layer == null) return;
    final t = layer.localTime(widget.playback.time.value);
    _startScale = layer.scaleX.valueAt(t);
    _startRotation = layer.rotation.valueAt(t);
    _dragStartPos = layer.position.valueAt(t);
    _dragAccum = Offset.zero;
  }

  void _onScaleUpdate(ScaleUpdateDetails d) {
    final id = ref.read(selectedLayerProvider);
    if (id == null) return;
    final controller = ref.read(editorControllerProvider.notifier);
    final project = ref.read(editorControllerProvider);
    final t = widget.playback.time.value;

    if (d.pointerCount >= 2) {
      controller.editScaleUniform(
        id,
        t,
        (_startScale * d.scale).clamp(0.05, 8.0),
      );
      controller.editRotation(
        id,
        t,
        _startRotation + d.rotation * 180 / math.pi,
      );
      return;
    }
    final deltaLogical = d.focalPointDelta / _stageScale;
    if (deltaLogical == Offset.zero) return;
    _dragAccum += deltaLogical;

    // Alinhamento: se o gesto e claramente horizontal/vertical, trava o
    // outro eixo — o arrasto nao "sai torto".
    var target = _dragStartPos + _dragAccum;
    final adx = _dragAccum.dx.abs();
    final ady = _dragAccum.dy.abs();
    if (adx > 24 || ady > 24) {
      if (adx > ady * 2.5) {
        target = Offset(target.dx, _dragStartPos.dy);
      } else if (ady > adx * 2.5) {
        target = Offset(_dragStartPos.dx, target.dy);
      }
    }

    // ENCAIXE (PR-X2): centro e bordas da composicao, centros e bordas
    // das OUTRAS camadas, e as guias. O primeiro alvo dentro da
    // tolerancia vence, por eixo.
    final snap = 16 / _stageScale;
    final self = ref.read(editorControllerProvider.notifier);
    final size = self.layerBoxSize(
      ref.read(editorControllerProvider).layerById(id)!,
      t,
    );
    final half = Offset(size.width / 2, size.height / 2);

    final xs = <double>[
      project.outputWidth / 2,
      half.dx,
      project.outputWidth - half.dx,
      ...project.guides.vertical,
      ...project.guides.vertical.map((g) => g + half.dx),
      ...project.guides.vertical.map((g) => g - half.dx),
    ];
    final ys = <double>[
      project.outputHeight / 2,
      half.dy,
      project.outputHeight - half.dy,
      ...project.guides.horizontal,
      ...project.guides.horizontal.map((g) => g + half.dy),
      ...project.guides.horizontal.map((g) => g - half.dy),
    ];
    for (final other in project.layers) {
      if (other.id == id || !other.activeAt(t)) continue;
      final oc = other.position.valueAt(other.localTime(t));
      final os = self.layerBoxSize(other, t);
      final oh = Offset(os.width / 2, os.height / 2);
      xs
        ..add(oc.dx)
        ..add(oc.dx - oh.dx + half.dx)
        ..add(oc.dx + oh.dx - half.dx)
        ..add(oc.dx - oh.dx - half.dx)
        ..add(oc.dx + oh.dx + half.dx);
      ys
        ..add(oc.dy)
        ..add(oc.dy - oh.dy + half.dy)
        ..add(oc.dy + oh.dy - half.dy)
        ..add(oc.dy - oh.dy - half.dy)
        ..add(oc.dy + oh.dy + half.dy);
    }
    for (final x in xs) {
      if ((target.dx - x).abs() < snap) {
        target = Offset(x, target.dy);
        break;
      }
    }
    for (final y in ys) {
      if ((target.dy - y).abs() < snap) {
        target = Offset(target.dx, y);
        break;
      }
    }

    controller.editPosition(id, t, target);
  }

  @override
  Widget build(BuildContext context) {
    final project = ref.watch(editorControllerProvider);
    final selectedId = ref.watch(selectedLayerProvider);
    final onion = ref.watch(onionSkinProvider);
    final drawing = ref.watch(freehandRequestProvider);
    final compW = project.outputWidth.toDouble();
    final compH = project.outputHeight.toDouble();
    final useDither =
        DitherLayer.comoFiltro && !_containsRasterMedia(project.layers);

    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onScaleStart: drawing ? null : _onScaleStart,
      onScaleUpdate: drawing ? null : _onScaleUpdate,
      child: Stack(
        fit: StackFit.expand,
        children: [
          ColoredBox(
            color: Colors.black,
            child: LayoutBuilder(
              builder: (context, constraints) {
                final scale = math.min(
                  constraints.maxWidth / compW,
                  constraints.maxHeight / compH,
                );
                _stageScale = scale;
                return Center(
                  child: SizedBox(
                    width: compW * scale,
                    height: compH * scale,
                    child: ClipRect(
                      // O filho precisa ter também a área de toque da
                      // composição. Transform + OverflowBox só escalava a
                      // pintura e descartava gestos fora do canto superior.
                      child: FittedBox(
                        fit: BoxFit.contain,
                        alignment: Alignment.topLeft,
                        child: MediaQuery(
                          // FOTOS NA RESOLUCAO DA TELA. Tudo que fotografa a
                          // composicao (efeitos, mescla, dithering) le a razao
                          // de pixels daqui: com a do aparelho, cada foto saia
                          // em 1080x1920 x DPR — 75 MB por efeito por quadro no
                          // iPhone, e o iOS fechava o app na primeira animacao.
                          data: MediaQuery.of(context).copyWith(
                            devicePixelRatio: previewRasterRatio(
                              maxSidePx: widget.playback.playing.value
                                  ? 1080
                                  : 2160,
                              compWidth: compW,
                              compHeight: compH,
                              stageScale: scale,
                              devicePixelRatio: MediaQuery.devicePixelRatioOf(
                                context,
                              ),
                            ),
                          ),
                          child: SizedBox(
                            width: compW,
                            height: compH,
                            child: Stack(
                              clipBehavior: Clip.none,
                              children: [
                                // Automatic dithering is only a live GPU pass
                                // for graphics. Never snapshot the preview:
                                // asynchronous image decoding and nested video
                                // textures must repaint without a clock change.
                                // Export still dithers fully decoded frames.
                                ValueListenableBuilder<Duration>(
                                  valueListenable: widget.playback.time,
                                  builder: (context, t, child) => !useDither
                                      ? child!
                                      : DitherLayer(
                                          time: t,
                                          // Escala do palco x DPR de verdade: e o
                                          // tamanho da textura do filtro.
                                          pixelRatio:
                                              scale *
                                              MediaQuery.devicePixelRatioOf(
                                                context,
                                              ),
                                          child: child!,
                                        ),
                                  child: CompositionView(
                                    time: widget.playback.time,
                                    videos: widget.videos,
                                    selectedId: selectedId,
                                  ),
                                ),
                                // CASCA DE CEBOLA: os quadros vizinhos,
                                // fantasmas, ATRAS do quadro atual. Passado
                                // puxado para o vermelho, futuro para o
                                // verde — e como se sabe de que lado esta.
                                if (onion > 0)
                                  Positioned.fill(
                                    child: IgnorePointer(
                                      child: ValueListenableBuilder<Duration>(
                                        valueListenable: widget.playback.time,
                                        builder: (context, t, _) {
                                          final passo = Duration(
                                            microseconds:
                                                1000000 ~/
                                                (project.fps < 1
                                                    ? 30
                                                    : project.fps),
                                          );
                                          return Stack(
                                            clipBehavior: Clip.none,
                                            children: [
                                              for (var k = onion; k >= 1; k--)
                                                for (final lado in const [
                                                  -1,
                                                  1,
                                                ])
                                                  _Fantasma(
                                                    time:
                                                        t + passo * (k * lado),
                                                    videos: widget.videos,
                                                    opacity: 0.34 / k,
                                                    futuro: lado > 0,
                                                  ),
                                            ],
                                          );
                                        },
                                      ),
                                    ),
                                  ),
                                // GUIAS, GRADE, AREAS SEGURAS e mascara de
                                // enquadramento (PR-X3): vivem ACIMA da
                                // composicao e nunca entram no render final.
                                Positioned.fill(
                                  child: IgnorePointer(
                                    child: CustomPaint(
                                      painter: _GuidesPainter(
                                        guides: project.guides,
                                        compSize: Size(compW, compH),
                                      ),
                                    ),
                                  ),
                                ),
                                // NOS DA MASCARA: quando alguem esta editando
                                // o caminho, o dedo passa a mexer nos nos em
                                // vez de mover a camada. Fora disso o widget
                                // nao existe e nao intercepta nada.
                                Positioned.fill(
                                  child: MaskNodeEditor(
                                    time: widget.playback.time,
                                    stageScale: () => _stageScale,
                                  ),
                                ),
                                // DESENHO LIVRE: por cima de tudo enquanto o
                                // pedido do menu estiver ligado.
                                Positioned.fill(
                                  child: FreehandOverlay(
                                    key: ValueKey(project.id),
                                    playback: widget.playback,
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                      ),
                    ),
                  ),
                );
              },
            ),
          ),
          if (drawing)
            Positioned(
              top: 4,
              left: 8,
              right: 8,
              child: Material(
                color: const Color(0xE620242B),
                borderRadius: BorderRadius.circular(8),
                child: Row(
                  children: [
                    const SizedBox(width: 10),
                    const Expanded(
                      child: Text(
                        'Desenho livre · arraste na prévia',
                        maxLines: 2,
                        style: TextStyle(color: Colors.white, fontSize: 12),
                      ),
                    ),
                    IconButton(
                      tooltip: 'Cancelar desenho livre',
                      onPressed: () =>
                          ref.read(freehandRequestProvider.notifier).state =
                              false,
                      icon: const Icon(
                        Icons.close,
                        color: Colors.white,
                        size: 20,
                      ),
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

/// Recorta uma FAIXA horizontal (dano digital): topo e altura em fracao
/// da caixa.
class _BandClipper extends CustomClipper<Rect> {
  const _BandClipper(this.top, this.height);

  final double top;
  final double height;

  @override
  Rect getClip(Size size) =>
      Rect.fromLTWH(0, size.height * top, size.width, size.height * height);

  @override
  bool shouldReclip(_BandClipper old) => old.top != top || old.height != height;
}

/// GRAO DE FILME: ruido puro por (semente, posicao, tempo) — nada
/// acumula, entao o frame 200 e igual direto ou depois de reproduzir.
class _GrainPainter extends CustomPainter {
  const _GrainPainter({
    required this.amount,
    required this.size,
    required this.seed,
    required this.time,
  });

  final double amount;
  final double size;
  final int seed;
  final Duration time;

  @override
  void paint(Canvas canvas, Size canvasSize) {
    final step = size.clamp(0.5, 6.0) * 3;
    final frame = time.inMilliseconds ~/ 33;
    final paint = Paint();
    for (var y = 0.0; y < canvasSize.height; y += step) {
      for (var x = 0.0; x < canvasSize.width; x += step) {
        final n = fxHash01(seed, frame, (x * 7919 + y * 104729).toInt());
        final v = (n - 0.5) * amount;
        paint.color = Color.fromRGBO(
          128,
          128,
          128,
          (v.abs() * 2).clamp(0.0, 1.0),
        );
        if (v > 0) {
          paint.color = Color.fromRGBO(
            255,
            255,
            255,
            (v * 1.6).clamp(0.0, 1.0),
          );
        } else {
          paint.color = Color.fromRGBO(0, 0, 0, (-v * 1.6).clamp(0.0, 1.0));
        }
        canvas.drawRect(Rect.fromLTWH(x, y, step, step), paint);
      }
    }
  }

  @override
  bool shouldRepaint(_GrainPainter old) =>
      old.amount != amount ||
      old.seed != seed ||
      old.size != size ||
      old.time.inMilliseconds ~/ 33 != time.inMilliseconds ~/ 33;
}

/// RUIDO FRACTAL: soma de oitavas de ruido de valor, com EVOLUCAO —
/// funcao pura de (semente, posicao, tempo), como manda a invariante I1.
class _FractalNoisePainter extends CustomPainter {
  const _FractalNoisePainter({
    required this.scale,
    required this.octaves,
    required this.contrast,
    required this.evolution,
    required this.seed,
    required this.color,
    required this.time,
  });

  final double scale;
  final int octaves;
  final double contrast;
  final double evolution;
  final int seed;
  final Color color;
  final Duration time;

  @override
  void paint(Canvas canvas, Size size) {
    final cell = (18 / scale.clamp(0.02, 1.0)).clamp(6.0, 90.0);
    final z = evolution * time.inMicroseconds / 1e6;
    final paint = Paint();
    for (var y = 0.0; y < size.height; y += cell) {
      for (var x = 0.0; x < size.width; x += cell) {
        var v = 0.0;
        var amp = 1.0;
        var freq = 1.0;
        var norm = 0.0;
        for (var o = 0; o < octaves.clamp(1, 6); o++) {
          v +=
              amp *
              valueNoise01(seed + o, x / cell * freq + z, y / cell * freq + z);
          norm += amp;
          amp *= 0.5;
          freq *= 2;
        }
        v = ((v / norm - 0.5) * contrast + 0.5).clamp(0.0, 1.0);
        paint.color = color.withValues(alpha: v);
        canvas.drawRect(Rect.fromLTWH(x, y, cell + 1, cell + 1), paint);
      }
    }
  }

  @override
  bool shouldRepaint(_FractalNoisePainter old) =>
      old.scale != scale ||
      old.octaves != octaves ||
      old.contrast != contrast ||
      old.evolution != evolution ||
      old.seed != seed ||
      old.color != color ||
      (evolution > 0 && old.time != time);
}

/// GUIAS E GRADE (PR-X3): guias arrastaveis, grade de layout com
/// colunas/medianiz/margem, areas seguras de titulo e acao, e a mascara
/// de enquadramento que mostra como o quadro fica cortado noutra
/// proporcao — sem alterar o projeto.
class _GuidesPainter extends CustomPainter {
  const _GuidesPainter({required this.guides, required this.compSize});

  final GuidesSpec guides;
  final Size compSize;

  @override
  void paint(Canvas canvas, Size size) {
    final w = compSize.width;
    final h = compSize.height;

    // Grade de layout.
    if (guides.columns > 0) {
      final paint = Paint()..color = const Color(0x22B8FF3D);
      final usable = w - guides.margin * 2;
      final colW =
          (usable - guides.gutter * (guides.columns - 1)) / guides.columns;
      for (var i = 0; i < guides.columns; i++) {
        final x = guides.margin + i * (colW + guides.gutter);
        canvas.drawRect(Rect.fromLTWH(x, 0, colW, h), paint);
      }
    }

    // Areas seguras: titulo (80%) e acao (90%).
    if (guides.showSafeAreas) {
      final stroke = Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2
        ..color = const Color(0x66FFFFFF);
      for (final f in const [0.9, 0.8]) {
        canvas.drawRect(
          Rect.fromCenter(
            center: Offset(w / 2, h / 2),
            width: w * f,
            height: h * f,
          ),
          stroke,
        );
      }
    }

    // Guias.
    final guide = Paint()
      ..color = const Color(0xAA35C4E7)
      ..strokeWidth = 2;
    for (final x in guides.vertical) {
      canvas.drawLine(Offset(x, 0), Offset(x, h), guide);
    }
    for (final y in guides.horizontal) {
      canvas.drawLine(Offset(0, y), Offset(w, y), guide);
    }

    // Mascara de enquadramento: escurece o que sai do corte.
    final fp = guides.framePreview;
    if (fp != null && fp > 0) {
      final cropW = fp >= w / h ? w : h * fp;
      final cropH = fp >= w / h ? w / fp : h;
      final crop = Rect.fromCenter(
        center: Offset(w / 2, h / 2),
        width: cropW,
        height: cropH,
      );
      final shade = Paint()..color = const Color(0x99000000);
      canvas.drawPath(
        Path.combine(
          PathOperation.difference,
          Path()..addRect(Rect.fromLTWH(0, 0, w, h)),
          Path()..addRect(crop),
        ),
        shade,
      );
      canvas.drawRect(
        crop,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2
          ..color = const Color(0xCCB8FF3D),
      );
    }
  }

  @override
  bool shouldRepaint(_GuidesPainter old) =>
      old.guides != guides || old.compSize != compSize;
}

/// Estado do portao de recomposicao — um por app (ha um preview). Vive
/// fora do widget porque _CompositionView e recriado a cada build do
/// pai; widgets sao configuracoes imutaveis e reusa-los e valido.
/// A marcha vigente — UMA POR VISTA.
///
/// Ela ja foi global, e isso era um defeito serio: o preview, a tela de
/// exportacao e cada quadro fantasma da casca de cebola sao vistas
/// DIFERENTES, com projeto e instante proprios, e todas liam e escreviam
/// o mesmo estado. Uma via a leitura da outra.
class _CompositionGate {
  VideoProject? project;
  GearDecision? decision;
}

/// Reconstroi por tick do clock; midia isolada em RepaintBoundary.
/// A COMPOSICAO em si — as camadas empilhadas no tempo [time]. E a
/// mesma arvore usada no preview e na EXPORTACAO: exportar renderiza
/// exatamente o que se ve, porque e o mesmo codigo.
/// CASCA DE CEBOLA: quantos quadros fantasma aparecem de cada lado.
/// Zero = desligada.
///
/// Animar a mao sem ver o quadro anterior e desenhar no escuro: o
/// espacamento entre poses e o que da o ritmo, e ele so se enxerga
/// vendo os quadros vizinhos ao mesmo tempo.
/// Um quadro vizinho, esmaecido e tingido.
class _Fantasma extends StatefulWidget {
  const _Fantasma({
    required this.time,
    required this.videos,
    required this.opacity,
    required this.futuro,
  });

  final Duration time;
  final VideoLayerManager videos;
  final double opacity;
  final bool futuro;

  @override
  State<_Fantasma> createState() => _FantasmaState();
}

class _FantasmaState extends State<_Fantasma> {
  late final ValueNotifier<Duration> _t = ValueNotifier(widget.time);

  @override
  void didUpdateWidget(_Fantasma old) {
    super.didUpdateWidget(old);
    _t.value = widget.time;
  }

  @override
  void dispose() {
    _t.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (widget.time < Duration.zero) return const SizedBox.shrink();
    return Positioned.fill(
      child: Opacity(
        opacity: widget.opacity.clamp(0.05, 0.6),
        child: ColorFiltered(
          colorFilter: ColorFilter.mode(
            widget.futuro ? const Color(0x666BFF8A) : const Color(0x66FF6B6B),
            BlendMode.modulate,
          ),
          child: CompositionView(
            time: _t,
            videos: widget.videos,
            selectedId: null,
          ),
        ),
      ),
    );
  }
}

class CompositionView extends ConsumerStatefulWidget {
  const CompositionView({
    super.key,
    required this.time,
    required this.videos,
    required this.selectedId,
    this.exportFrames,
    this.exporting = false,
  });

  final ValueListenable<Duration> time;
  final VideoLayerManager videos;
  final String? selectedId;

  /// Na exportacao, o quadro ja decodificado de cada camada de video —
  /// textura de plataforma nao entra em `toImage`, entao o video chega
  /// aqui como imagem.
  final Map<String, ui.Image>? exportFrames;

  /// Exportando: nunca reusa arvore em cache, porque cada quadro e
  /// diferente mesmo quando a "assinatura" da cena nao muda.
  final bool exporting;

  @override
  ConsumerState<CompositionView> createState() => _CompositionViewState();
}

class _CompositionViewState extends ConsumerState<CompositionView> {
  final _gate = _CompositionGate();

  ValueListenable<Duration> get time => widget.time;
  VideoLayerManager get videos => widget.videos;
  String? get selectedId => widget.selectedId;
  Map<String, ui.Image>? get exportFrames => widget.exportFrames;
  bool get exporting => widget.exporting;

  @override
  Widget build(BuildContext context) {
    final project = ref.watch(editorControllerProvider);

    return ValueListenableBuilder<Duration>(
      valueListenable: time,
      builder: (context, t, _) {
        if (exporting) {
          return Stack(
            clipBehavior: Clip.none,
            children: _buildLayers(
              project,
              project.layers,
              t,
              resolveLinks: true,
            ),
          );
        }
        // MARCHA (PR-G1): o classificador e ESTRUTURAL — roda quando a
        // cena muda (identidade do projeto), nunca por quadro.
        //
        // O QUE SAIU DAQUI, E POR QUE: existia um cache que reusava a
        // arvore composta enquanto "nada parecesse evoluir no tempo".
        // Decidir isso exige manter a lista de tudo que varia com o
        // tempo, e essa lista nunca fica completa — ficaram de fora os
        // efeitos com fase propria, o rastreio, o pulso na batida, o
        // corte de camera. E o preco do erro e o pior que existe: o
        // preview congela, e sem preview vivo nao da para animar, que e
        // para o que o aplicativo serve. Montar a arvore e barato;
        // congelar o preview nao tem preco que pague.
        if (!identical(project, _gate.project)) {
          _gate.project = project;
          _gate.decision = classifyGear(project);
          PreviewStats.setGear(_gate.decision!);
        }
        final kids = _buildLayers(
          project,
          project.layers,
          t,
          resolveLinks: true,
        );
        PreviewStats.tick(kids.length);
        return Stack(clipBehavior: Clip.none, children: kids);
      },
    );
  }

  /// Constroi as camadas em ordem de pintura, com ordenacao 3D por trecho
  /// (D2) e vinculos de propriedade resolvidos (D4).
  List<Widget> _buildLayers(
    VideoProject project,
    List<Layer> layers,
    Duration t, {
    required bool resolveLinks,
  }) {
    // Camadas usadas como MATTE ficam ocultas na composicao (PR-M5).
    final matteSourceIds = <String>{
      for (final l in layers)
        if (l.matteMode != MatteMode.none && l.matteSourceId != null)
          l.matteSourceId!,
    };
    final transitions = transitionContextsAt(layers, t);
    final paintOrder = [
      for (final layer in layers.reversed)
        if (layer is! AudioLayer &&
            visibleForCut(layers, layer, t, contexts: transitions) &&
            !matteSourceIds.contains(layer.id) &&
            // SOLO (PR-X26): havendo solo, so os solos renderizam.
            project.rendersInPreview(layer.id))
          layer,
    ];
    final sorted = depthSortPaintOrder(paintOrder, t);

    // MUNDO 3D: solidos VIZINHOS na pilha viram uma cena so, com a
    // profundidade compartilhada — um entra dentro do outro, passa por
    // tras, o vidro deixa ver o que esta atras. So entra quem nao tem
    // efeito, mascara, blend, estilo ou vinculo de propriedade; esses
    // seguem pelo caminho normal, sozinhos.
    final mundoInicio = <String, List<Element3DLayer>>{};
    final mundoMembro = <String>{};
    {
      var i = 0;
      while (i < sorted.length) {
        final l = sorted[i];
        if (l is Element3DLayer && _mundoElegivel(project, l)) {
          var j = i + 1;
          while (j < sorted.length &&
              sorted[j] is Element3DLayer &&
              _mundoElegivel(project, sorted[j] as Element3DLayer)) {
            j++;
          }
          if (j - i >= 2) {
            final fila = [
              for (var k = i; k < j; k++) sorted[k] as Element3DLayer,
            ];
            mundoInicio[l.id] = fila;
            for (final f in fila) {
              mundoMembro.add(f.id);
            }
          }
          i = j;
        } else {
          i++;
        }
      }
    }

    // Modulo Grid: mapeia assetId -> (nulo, rig, indice, total) neste
    // escopo de camadas (funciona tambem dentro de grupos).
    final rigMembers = <String, (NullLayer, GridRig, int, int)>{};
    for (final l in layers) {
      if (l is NullLayer && l.grid != null && l.grid!.assets.isNotEmpty) {
        final ids = l.grid!.assets;
        for (var i = 0; i < ids.length; i++) {
          rigMembers[ids[i]] = (l, l.grid!, i, ids.length);
        }
      }
    }

    final children = <Widget>[];
    for (final layer in sorted) {
      // CAMADA DE AJUSTE (AUREA-2 §1): aplica a pilha dela ao COMPOSTO
      // de tudo abaixo; mascaras recortam a regiao (na posicao da
      // camada), opacidade dosa a mistura e o blend devolve o resultado.
      // Pilha vazia nao muda um pixel (I2).
      if (layer is AdjustmentLayer) {
        final local = layer.localTime(t);
        final hasWork = layer.effects.any((e) => e.enabled);
        if (!hasWork || children.isEmpty) continue;

        Widget adjusted = Stack(
          clipBehavior: Clip.none,
          children: List<Widget>.of(children),
        );
        adjusted = _applyEffects(layer.effects, adjusted, local);
        if (layer.masks.isNotEmpty) {
          final pos = layer.position.valueAt(local);
          final compCenter = Offset(
            project.outputWidth / 2,
            project.outputHeight / 2,
          );
          final shift = pos - compCenter;
          adjusted = MaskedBox(
            specs: [
              for (final m in layer.masks)
                MaskSpec(
                  path: m.path.valueAt(local).build().shift(shift),
                  closed: m.path.valueAt(local).closed,
                  mode: m.mode,
                  inverted: m.inverted,
                  opacity: m.opacity.valueAt(local).clamp(0.0, 1.0),
                  feather: m.feather.valueAt(local),
                  featherY: m.featherY?.valueAt(local),
                  expansion: m.expansion.valueAt(local),
                ),
            ],
            child: adjusted,
          );
        }
        final op = layer.opacity.valueAt(local).clamp(0.0, 1.0);
        final plain =
            layer.masks.isEmpty &&
            op >= 0.999 &&
            layer.blendMode == BlendMode.srcOver;
        if (plain) {
          // O ajustado SUBSTITUI o acumulado (como no AE) — empilhar por
          // cima duplicava o conteudo no preview.
          children
            ..clear()
            ..add(Positioned.fill(child: IgnorePointer(child: adjusted)));
        } else {
          // Com mascara/opacidade/blend, o ajustado mistura POR CIMA do
          // original (dentro da mascara ele cobre o mesmo conteudo).
          children.add(
            Positioned.fill(
              child: IgnorePointer(
                child: BlendMask(
                  blendMode: layer.blendMode,
                  child: Opacity(opacity: op, child: adjusted),
                ),
              ),
            ),
          );
        }
        continue;
      }

      if (mundoMembro.contains(layer.id)) {
        final fila = mundoInicio[layer.id];
        // Quem nao abre a fila ja foi pintado na cena do primeiro.
        if (fila == null) continue;
        children.add(_buildWorld3D(project, fila, t, resolveLinks));
        continue;
      }

      // Eco/rastro: re-renderiza a camada INTEIRA em tempos anteriores
      // (deterministico — trilha de movimento dos keyframes), atras da
      // copia atual e com opacidade decaindo.
      EffectInstance? echoFx;
      for (final e in layer.effects) {
        if (e.enabled && e.type == EffectType.echo) echoFx = e;
      }
      if (echoFx != null) {
        final local = layer.localTime(t);
        final n = echoFx.paramAt('ecos', local).round().clamp(1, 8);
        final gapUs = (echoFx.paramAt('intervalo', local) * 1e6).round();
        final decay = echoFx.paramAt('decaimento', local).clamp(0.05, 0.95);
        final hueStep = echoFx.paramAt('matiz', local);
        for (var i = n; i >= 1; i--) {
          final et = t - Duration(microseconds: gapUs * i);
          if (!layer.activeAt(et)) continue;
          Widget copy = _buildLayer(
            project,
            layer,
            et,
            resolveLinks,
            rig: rigMembers,
            opacityMul: math.pow(decay, i).toDouble(),
          );
          // Rastro COLORIDO (item 16): cada copia com matiz proprio.
          if (hueStep > 0.5) {
            copy = Positioned.fill(
              child: ColorFiltered(
                colorFilter: ColorFilter.matrix(hueRotateMatrix(hueStep * i)),
                child: Stack(clipBehavior: Clip.none, children: [copy]),
              ),
            );
          }
          children.add(copy);
        }
      }

      // FORCE MOTION BLUR (nivel 2): borra com MAIS amostras do que a
      // composicao permite, e funciona sem keyframe de transform. Como o
      // eco, ele precisa re-renderizar a camada em outros instantes —
      // por isso mora aqui, e nao na pilha de efeitos, que so recebe o
      // widget pronto.
      EffectInstance? forceMb;
      for (final e in layer.effects) {
        if (e.enabled && e.type == EffectType.forceMotionBlur) forceMb = e;
      }

      var w = forceMb == null
          ? _buildLayer(project, layer, t, resolveLinks, rig: rigMembers)
          : _forceMotionBlur(
              project,
              layer,
              t,
              forceMb,
              resolveLinks,
              rigMembers,
            );

      // Uma camada curta pode participar de duas janelas ao mesmo tempo
      // (entra de A e ja sai para C). Cada contexto precisa ser composto;
      // usar o primeiro contexto global deixava uma das juncoes sem efeito.
      for (final transition in transitions) {
        if (transition.isParticipant(layer.id)) {
          w = _transitionVisual(project, transition, layer, t, w);
        }
      }

      // MOTION BLUR DA COMPOSICAO (nivel 1): a camada e desenhada varias
      // vezes ao longo da JANELA DE EXPOSICAO e as copias sao mediadas.
      //
      // A janela vem do angulo e da FASE do obturador. Fase -90 centra o
      // borrao no quadro; fase 0 arrasta para frente — sao imagens
      // visivelmente diferentes, e e por isso que a fase existe.
      if (project.motionBlur.enabled && project.metaOf(layer.id).motionBlur) {
        w = _comMotionBlur(project, layer, t, w, resolveLinks, rigMembers);
      }

      // Matte: a fonte recorta esta camada, num grupo isolado.
      if (layer.matteMode != MatteMode.none) {
        Layer? src;
        for (final l in layers) {
          if (l.id == layer.matteSourceId) src = l;
        }
        // Matte ligado sem fonte valida/ativa equivale a alfa zero. Deixar
        // o alvo inteiro visivel mascara um link quebrado e produz um salto
        // justamente quando a fonte entra ou sai do seu intervalo.
        final sourceActive = src != null && src.activeAt(t);
        final source = sourceActive
            ? _buildLayer(project, src, t, resolveLinks, rig: rigMembers)
            : const SizedBox.shrink();
        // Luma precisa da cor E do alfa original. A matriz de cor trabalha
        // em RGBA nao-premultiplicado, entao uma segunda pintura da mesma
        // fonte preserva sua transparencia depois de extrair a luminancia.
        final alphaForLuma =
            sourceActive &&
                (layer.matteMode == MatteMode.luma ||
                    layer.matteMode == MatteMode.lumaInvert)
            ? _buildLayer(project, src, t, resolveLinks, rig: rigMembers)
            : null;
        final matte = _matteFiltered(
          layer.matteMode,
          source,
          alphaForLuma: alphaForLuma,
        );
        w = Positioned.fill(
          child: BlendMask(
            blendMode: BlendMode.srcOver,
            isolate: true,
            child: Stack(clipBehavior: Clip.none, children: [w, matte]),
          ),
        );
      }
      // MESCLA PROPRIA: os modos que o Flutter nao tem precisam ver o
      // que ja esta embaixo. A pilha se parte aqui — o acumulado vira o
      // andar de baixo, esta camada o de cima — e o compositor devolve
      // um widget so, sobre o qual as proximas continuam empilhando.
      final custom = layer.customBlend;
      if (custom != null && children.isNotEmpty) {
        final base = Stack(
          clipBehavior: Clip.none,
          children: List<Widget>.of(children),
        );
        children
          ..clear()
          ..add(
            Positioned.fill(
              child: CustomBlendBox(
                mode: custom,
                seed: (t.inMilliseconds % 4096).toDouble(),
                base: base,
                top: Stack(clipBehavior: Clip.none, children: [w]),
              ),
            ),
          );
        continue;
      }

      children.add(w);
    }
    return children;
  }

  Widget _transitionVisual(
    VideoProject project,
    ClipTransitionContext context,
    Layer layer,
    Duration globalTime,
    Widget child,
  ) {
    final incoming = layer.id == context.incoming.id;
    final p = context.progress;
    final canvas = Stack(clipBehavior: Clip.none, children: [child]);
    Widget out = Opacity(
      opacity: context.opacityFor(layer.id).clamp(0.0, 1.0),
      child: canvas,
    );
    switch (context.transition.type) {
      case ClipTransitionType.dissolve:
      case ClipTransitionType.black:
        break;
      case ClipTransitionType.wipe:
        if (incoming) {
          out = ClipRect(
            child: Align(
              alignment: Alignment.centerLeft,
              widthFactor: p.clamp(0.001, 1.0),
              child: out,
            ),
          );
        }
      case ClipTransitionType.zoomWarp:
        final scale = incoming ? 0.82 + p * 0.18 : 1 + p * 0.2;
        out = Transform.scale(scale: scale, child: out);
      case ClipTransitionType.whip:
        final distance = project.outputWidth.toDouble();
        out = Transform.translate(
          offset: Offset(incoming ? (1 - p) * distance : -p * distance, 0),
          child: out,
        );
      case ClipTransitionType.glitch:
        final phase = globalTime.inMicroseconds / 1000000.0;
        final envelope = 1 - (2 * p - 1).abs();
        final jump = math.sin(phase * 97.0) * envelope * 24;
        out = Transform.translate(offset: Offset(jump, 0), child: out);
      case ClipTransitionType.effect:
        final effect = context.transition.effect;
        if (effect != null) {
          final amount = context.transition.amountAt(
            globalTime,
            context.incoming.startTime,
          );
          final local = globalTime - context.window.start;
          final effected = _applyEffects([effect], canvas, local);
          out = Opacity(
            opacity: context.opacityFor(layer.id).clamp(0.0, 1.0),
            child: Stack(
              fit: StackFit.expand,
              children: [
                canvas,
                Opacity(opacity: amount, child: effected),
              ],
            ),
          );
        }
    }
    return Positioned.fill(child: out);
  }

  /// FORCE MOTION BLUR: borra a camada com as amostras que o efeito
  /// pedir, independente do que a composicao permite.
  ///
  /// `Native Motion Blur` decide o que fazer com o borrao da composicao:
  /// Off ignora, On soma os dois, Only usa so o da composicao (e ai este
  /// efeito nao faz nada).
  Widget _forceMotionBlur(
    VideoProject project,
    Layer layer,
    Duration t,
    EffectInstance fx,
    bool resolveLinks,
    Map<String, (NullLayer, GridRig, int, int)> rig,
  ) {
    final local = layer.localTime(t);
    final nativo = fx.paramAt('native_motion_blur', local).round();
    if (nativo == 2) {
      // "Only": quem borra e a composicao.
      return _buildLayer(project, layer, t, resolveLinks, rig: rig);
    }

    final n = fx.paramAt('samples', local).round().clamp(2, 64);
    final angulo = fx.paramAt('shutter_angle', local).clamp(0.0, 720.0);
    if (angulo < 0.5) {
      return _buildLayer(project, layer, t, resolveLinks, rig: rig);
    }

    final fps = project.fps < 1 ? 30 : project.fps;
    final quadroUs = 1000000 / fps;
    // Centrado no quadro, como a fase -90 da composicao.
    final metadeUs = angulo / 360 / 2 * quadroUs;

    final copias = <Widget>[];
    for (var i = 0; i < n; i++) {
      final f = n == 1 ? 0.0 : (i / (n - 1)) * 2 - 1;
      final ti = t + Duration(microseconds: (f * metadeUs).round());
      final amostra = ti < Duration.zero
          ? _buildLayer(project, layer, t, resolveLinks, rig: rig)
          : _buildLayer(project, layer, ti, resolveLinks, rig: rig);
      // Media corrente: todas as amostras com o mesmo peso.
      copias.add(Opacity(opacity: 1 / (i + 1), child: amostra));
    }
    return Stack(clipBehavior: Clip.none, children: copias);
  }

  /// Quanto a camada SE MOVE dentro da janela, em pixels aproximados.
  ///
  /// Serve para o limite adaptativo: camada parada nao gasta amostra
  /// nenhuma, e camada que anda tres pixels nao precisa de dezesseis.
  double _movimentoNaJanela(
    VideoProject project,
    Layer layer,
    Duration a,
    Duration b,
  ) {
    final ta = effectiveTransform(project, layer, a);
    final tb = effectiveTransform(project, layer, b);
    final d = (tb.pos - ta.pos).distance;
    final giro = (tb.rot - ta.rot).abs();
    final escala = (tb.scale - ta.scale).abs();
    // Giro e escala viram pixel pelo tamanho aproximado da camada.
    final tamanho = project.outputWidth * 0.5;
    return d + giro / 90 * tamanho * 0.5 + escala * tamanho;
  }

  /// A camada borrada pelo movimento.
  ///
  /// As copias sao mediadas com opacidade 1/(i+1): isso e a MEDIA
  /// CORRENTE, e da o mesmo peso a todas as amostras. Empilhar todas com
  /// 1/N daria peso maior as ultimas, e o borrao sairia puxado para um
  /// lado.
  Widget _comMotionBlur(
    VideoProject project,
    Layer layer,
    Duration t,
    Widget nitida,
    bool resolveLinks,
    Map<String, (NullLayer, GridRig, int, int)> rig,
  ) {
    final mb = project.motionBlur;
    final fps = project.fps < 1 ? 30 : project.fps;
    final quadroUs = 1000000 / fps;
    final (ini, fim) = mb.exposureWindow();
    final janelaUs = (fim - ini) * quadroUs;
    if (janelaUs.abs() < 1) return nitida;

    final inicio = t + Duration(microseconds: (ini * quadroUs).round());
    final termino = t + Duration(microseconds: (fim * quadroUs).round());

    // LIMITE ADAPTATIVO: parada, a camada nao borra; andando pouco,
    // poucas amostras bastam. Dezesseis amostras de uma camada parada
    // seriam dezesseis renderizacoes identicas.
    final movimento = _movimentoNaJanela(project, layer, inicio, termino);
    if (movimento < 0.6) return nitida;

    final pedidas = mb.samples.clamp(2, mb.adaptiveLimit);
    final n = movimento < 3
        ? 2
        : (movimento < 12 ? 4 : pedidas).clamp(2, pedidas);

    final copias = <Widget>[];
    for (var i = 0; i < n; i++) {
      final f = n == 1 ? 0.5 : i / (n - 1);
      final ti =
          inicio +
          Duration(
            microseconds: ((termino - inicio).inMicroseconds * f).round(),
          );
      final amostra = ti < Duration.zero
          ? nitida
          : _buildLayer(project, layer, ti, resolveLinks, rig: rig);
      copias.add(Opacity(opacity: 1 / (i + 1), child: amostra));
    }
    return Stack(clipBehavior: Clip.none, children: copias);
  }

  /// Converte a fonte do matte no canal certo e composita com dstIn.
  Widget _matteFiltered(MatteMode mode, Widget matte, {Widget? alphaForLuma}) {
    const lumaM = <double>[
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      0.299, 0.587, 0.114, 0, 0,
    ];
    const lumaInvM = <double>[
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      -0.299, -0.587, -0.114, 0, 255,
    ];
    const alphaInvM = <double>[
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      0, 0, 0, -1, 255,
    ];
    // O widget do matte e um Positioned: ele precisa ser filho DIRETO de
    // um Stack — o filtro de cor envolve o Stack, nunca o Positioned.
    Widget content = Stack(clipBehavior: Clip.none, children: [matte]);
    switch (mode) {
      case MatteMode.alpha:
      case MatteMode.none:
        break;
      case MatteMode.alphaInvert:
        content = ColorFiltered(
          colorFilter: const ColorFilter.matrix(alphaInvM),
          child: content,
        );
      case MatteMode.luma:
        content = ColorFiltered(
          colorFilter: const ColorFilter.matrix(lumaM),
          child: content,
        );
      case MatteMode.lumaInvert:
        content = ColorFiltered(
          colorFilter: const ColorFilter.matrix(lumaInvM),
          child: content,
        );
    }

    // ColorFilter.matrix substitui o alfa pelo luma em espaco
    // nao-premultiplicado. Intersectar com uma segunda pintura da fonte
    // produz luma * alfa (e (1-luma) * alfa no invertido), sem recuperar
    // pixels originalmente transparentes.
    if (alphaForLuma != null &&
        (mode == MatteMode.luma || mode == MatteMode.lumaInvert)) {
      content = BlendMask(
        blendMode: BlendMode.srcOver,
        isolate: true,
        child: Stack(
          clipBehavior: Clip.none,
          children: [
            Positioned.fill(child: content),
            Positioned.fill(
              child: BlendMask(
                blendMode: BlendMode.dstIn,
                child: Stack(clipBehavior: Clip.none, children: [alphaForLuma]),
              ),
            ),
          ],
        ),
      );
    }
    return Positioned.fill(
      child: IgnorePointer(
        child: BlendMask(blendMode: BlendMode.dstIn, child: content),
      ),
    );
  }

  /// Pode entrar no mundo 3D compartilhado? Sem efeito ligado, mascara,
  /// blend, estilo, matte ou vinculo de propriedade (o vinculo de PAI e
  /// aceito: a cadeia de nulos e resolvida pelo effectiveTransform).
  bool _mundoElegivel(VideoProject project, Element3DLayer l) {
    if (l.effects.any((e) => e.enabled)) return false;
    if (l.masks.isNotEmpty) return false;
    if (l.blendMode != BlendMode.srcOver || l.customBlend != null) {
      return false;
    }
    if (l.matteMode != MatteMode.none) return false;
    if (!project.metaOf(l.id).styles.isEmpty) return false;
    for (final prop in [
      LayerProp.position,
      LayerProp.rotation,
      LayerProp.opacity,
      LayerProp.scale,
    ]) {
      if (project.linkFor(l.id, prop) != null) return false;
    }
    return true;
  }

  /// A cena unica de uma fila de solidos: cada um com seu transform
  /// efetivo (posicao, Z, rotacoes, escala, opacidade), projetados pela
  /// mesma camera no centro da composicao.
  Widget _buildWorld3D(
    VideoProject project,
    List<Element3DLayer> fila,
    Duration t,
    bool resolveLinks,
  ) {
    final items = <World3DItem>[];
    for (final l in fila) {
      final local = l.localTime(t);
      final eff = resolveLinks
          ? effectiveTransform(project, l, t)
          : LayerTransform(
              pos: l.position.valueAt(local),
              rot: l.rotation.valueAt(local),
              rotX: l.rotationX.valueAt(local),
              rotY: l.rotationY.valueAt(local),
              scale: l.scaleX.valueAt(local),
              z: l.positionZ.valueAt(local),
            );
      final rawSx = l.scaleX.valueAt(local);
      final ratio = rawSx.abs() < 1e-6 ? 1.0 : eff.scale / rawSx;
      final proprioZ = l.positionZ.valueAt(local);
      final temZ = l.is3D || (eff.z - proprioZ).abs() > 1e-6;
      items.add(
        World3DItem(
          layer: l,
          center: eff.pos,
          z: temZ ? eff.z.clamp(-1100.0, 100000.0) : 0,
          scaleX: eff.scale,
          scaleY: l.scaleY.valueAt(local) * ratio,
          rotXDeg: eff.rotX,
          rotYDeg: eff.rotY,
          rotZDeg: eff.rot,
          opacity: l.opacity.valueAt(local).clamp(0.0, 1.0),
          selected: l.id == selectedId,
          material: l.material,
          gradient: l.gradient,
          shininess: l.shininess,
        ),
      );
    }
    return Positioned.fill(
      child: IgnorePointer(
        child: ListenableBuilder(
          listenable: Listenable.merge([
            TextureCache.instance.revision,
            MeshCache.instance.revision,
          ]),
          builder: (_, _) => CustomPaint(painter: World3DPainter(items: items)),
        ),
      ),
    );
  }

  Widget _buildLayer(
    VideoProject project,
    Layer layer,
    Duration t,
    bool resolveLinks, {
    double opacityMul = 1,
    Map<String, (NullLayer, GridRig, int, int)>? rig,
  }) {
    final local = layer.localTime(t);

    // REMAPEAR TEMPO (igual ao AE): muda QUAL instante da camada aparece
    // agora, sem tocar nos keyframes de transformacao — que continuam
    // lendo o tempo da composicao. E o que permite congelar, voltar e
    // fazer rampa de velocidade com keyframes de tempo.
    final contentLocal = _remappedTime(layer, local);

    // ---- propriedades efetivas (com pickwhip quando ha vinculo) ----
    var pos = layer.position.valueAt(local);
    var rotationDeg = layer.rotation.valueAt(local);
    var opacityV = layer.opacity.valueAt(local);
    var sx = layer.scaleX.valueAt(local);
    var sy = layer.scaleY.valueAt(local);
    var extraZ = 0.0;
    var extraRotX = 0.0;
    var extraRotY = 0.0;

    if (resolveLinks) {
      Layer? src(PropertyLink? l) =>
          l == null ? null : project.layerById(l.sourceLayerId);

      final pl = project.linkFor(layer.id, LayerProp.position);
      final ps = src(pl);
      if (pl != null && ps != null) {
        final sourceTime = t - pl.delay;
        pos =
            ps.position.valueAt(ps.localTime(sourceTime)) +
            Offset(pl.offsetX, pl.offsetY);
      }
      final rl = project.linkFor(layer.id, LayerProp.rotation);
      final rs = src(rl);
      if (rl != null && rs != null) {
        final sourceTime = t - rl.delay;
        rotationDeg =
            rs.rotation.valueAt(rs.localTime(sourceTime)) * rl.scale +
            rl.offsetX;
      }
      final ol = project.linkFor(layer.id, LayerProp.opacity);
      final os = src(ol);
      if (ol != null && os != null) {
        final sourceTime = t - ol.delay;
        opacityV = (os.opacity.valueAt(os.localTime(sourceTime)) + ol.offsetX)
            .clamp(0, 1);
      }
      final sl = project.linkFor(layer.id, LayerProp.scale);
      final ss = src(sl);
      if (sl != null && ss != null) {
        final sourceTime = t - sl.delay;
        final f = ss.scaleX.valueAt(ss.localTime(sourceTime)) * sl.offsetX;
        sx = f;
        sy = f;
      }

      // Parenting (objeto nulo / camada pai): resolve a CADEIA inteira
      // (objeto -> nulo 1 -> nulo 2 -> ...) recursivamente. O filho segue
      // o delta de posicao/rotacao(3D)/escala acumulado — semantica AE:
      // nada pula ao parear, e girar o nulo em X/Y/Z orbita o filho.
      final par = project.linkFor(layer.id, LayerProp.parent);
      if (par != null) {
        final eff = effectiveTransform(project, layer, t);
        final rawScale = layer.scaleX.valueAt(local);
        final ratio = rawScale.abs() < 1e-6 ? 1.0 : eff.scale / rawScale;
        pos = eff.pos;
        rotationDeg = eff.rot;
        extraRotX = eff.rotX - layer.rotationX.valueAt(local);
        extraRotY = eff.rotY - layer.rotationY.valueAt(local);
        extraZ = eff.z - layer.positionZ.valueAt(local);
        sx *= ratio;
        sy *= ratio;
      }
    }

    // ---- Modulo Grid: a camada e ASSET de uma grade num nulo ----
    // O rig calcula posicao/rotacao/escala base; a transform propria da
    // camada e aplicada POR CIMA como offset — mover uma camada
    // manualmente nunca desloca as outras.
    final rigInfo = rig?[layer.id];
    if (rigInfo != null) {
      final (nullL, g, idx, count) = rigInfo;
      if (nullL.activeAt(t)) {
        // Nulo CONTROLADOR (alem do dono): o transform dele modula os
        // parametros — escala x espacamento/raio, rotZ + rotacao da
        // grade, rotY + twist. Animar o nulo anima a grade.
        var spacingMul = 1.0, rotationAdd = 0.0, twistAdd = 0.0;
        final ctrlId = g.controllerId;
        if (ctrlId != null) {
          final ctrl = project.layerById(ctrlId);
          if (ctrl != null && ctrl.activeAt(t)) {
            final ce = effectiveTransform(project, ctrl, t);
            spacingMul = ce.scale;
            rotationAdd = ce.rot;
            twistAdd = ce.rotY;
          }
        }
        final place = gridPlacementAt(
          g,
          idx,
          count,
          nullL.localTime(t),
          spacingMul: spacingMul,
          rotationAdd: rotationAdd,
          twistAdd: twistAdd,
        );
        final ne = effectiveTransform(project, nullL, t);

        // Layout girado/escalado pelo transform 3D do nulo controlador.
        final vx = place.pos.dx * ne.scale;
        final vy = place.pos.dy * ne.scale;
        final vz = place.z * ne.scale;
        final dRx = ne.rotX * math.pi / 180;
        final dRy = ne.rotY * math.pi / 180;
        final dRz = ne.rot * math.pi / 180;
        final cxr = math.cos(dRx), sxr = math.sin(dRx);
        final y1 = vy * cxr - vz * sxr;
        final z1 = vy * sxr + vz * cxr;
        final cyr = math.cos(dRy), syr = math.sin(dRy);
        final x1 = vx * cyr + z1 * syr;
        final z2 = -vx * syr + z1 * cyr;
        final czr = math.cos(dRz), szr = math.sin(dRz);

        final compCenter = Offset(
          project.outputWidth / 2,
          project.outputHeight / 2,
        );
        final authoredOffset = pos - compCenter;
        // Mesma projecao de posicao do parenting: orbita 3D de verdade.
        final perspPos = 1200 / (1200 + (ne.z + z2).clamp(-1100.0, 100000.0));
        pos =
            ne.pos +
            Offset(x1 * czr - y1 * szr, x1 * szr + y1 * czr) * perspPos +
            authoredOffset;
        extraZ = ne.z + z2 - layer.positionZ.valueAt(local);
        rotationDeg += place.rotationDeg + ne.rot;
        extraRotX += ne.rotX;
        extraRotY += ne.rotY;
        sx *= place.scale * ne.scale;
        sy *= place.scale * ne.scale;
        opacityV *= place.opacity;
      }
    }

    // ---- 3D: perspectiva simples pela profundidade ----
    if (layer.is3D || extraZ != 0) {
      final z = (layer.positionZ.valueAt(local) + extraZ).clamp(
        -1100.0,
        100000.0,
      );
      final persp = 1200 / (1200 + z);
      sx *= persp;
      sy *= persp;
    }

    final rotation = rotationDeg * math.pi / 180;
    final skewX = layer.skewX.valueAt(local) * math.pi / 180;
    final skewY = layer.skewY.valueAt(local) * math.pi / 180;
    final pivot = layer.pivot.valueAt(local);
    final opacity = (opacityV * opacityMul).clamp(0.0, 1.0);

    // Particulas vivem em espaco 3D proprio: a rotacao do sistema (da
    // camada + herdada do nulo pai) e resolvida DENTRO do simulador — a
    // nuvem gira no espaco, nada de inclinar o canvas como um cartao.
    final isParticles = layer is ParticlesLayer || layer is Element3DLayer;
    Widget content = _LayerContent(
      layer: layer,
      project: project,
      exportFrames: exportFrames,
      exporting: exporting,
      compWidth: project.outputWidth.toDouble(),
      videos: videos,
      localTime: contentLocal,
      particlesRotX: isParticles
          ? layer.rotationX.valueAt(local) + extraRotX
          : 0,
      particlesRotY: isParticles
          ? layer.rotationY.valueAt(local) + extraRotY
          : 0,
      buildChildren: (childLayers, childT) =>
          _buildLayers(project, childLayers, childT, resolveLinks: false),
    );

    if (layer is VideoLayer && layer.speedBlur) {
      final rate = videoPlaybackRateAt(layer, local).abs();
      final sigma = ((rate - 1).abs() * 2.4).clamp(0.0, 18.0);
      if (sigma > 0.05) {
        content = ImageFiltered(
          imageFilter: ui.ImageFilter.blur(sigmaX: sigma, sigmaY: sigma * 0.35),
          child: content,
        );
      }
    }

    // Mascaras cortam o alfa da propria camada ANTES dos efeitos (AE).
    if (layer.masks.isNotEmpty) {
      content = MaskedBox(
        specs: [
          for (final m in layer.masks)
            MaskSpec(
              path: m.path.valueAt(local).build(),
              closed: m.path.valueAt(local).closed,
              mode: m.mode,
              inverted: m.inverted,
              opacity: m.opacity.valueAt(local).clamp(0.0, 1.0),
              feather: m.feather.valueAt(local),
              featherY: m.featherY?.valueAt(local),
              expansion: m.expansion.valueAt(local),
            ),
        ],
        child: content,
      );
    }

    content = _applyEffects(layer.effects, content, local);

    // ESTILOS DE CAMADA (PR-X10): aplicam DEPOIS dos efeitos e
    // acompanham a forma da camada — e o que os diferencia de efeito.
    final styles = project.metaOf(layer.id).styles;
    if (!styles.isEmpty) {
      content = _applyLayerStyles(
        styles,
        content,
        local,
        Size(project.outputWidth.toDouble(), project.outputHeight.toDouble()),
      );
    }

    // Selecao desenhada DEPOIS dos efeitos: blur/glow nao pegam a borda.
    // Copias de eco (opacityMul < 1) nao ganham borda de selecao.
    final contentSemSelecao = content;
    if (layer.id == selectedId && opacityMul == 1) {
      content = Stack(
        clipBehavior: Clip.none,
        children: [
          content,
          Positioned.fill(
            child: IgnorePointer(
              child: Container(
                decoration: BoxDecoration(
                  border: Border.all(color: Colors.white, width: 4),
                ),
              ),
            ),
          ),
        ],
      );
    }

    // Rotacao 3D (eixos X/Y) — inclui o delta herdado do pai 3D.
    // Particulas NAO entram aqui: a rotacao delas e 3D real no painter.
    final rx = (layer.rotationX.valueAt(local) + extraRotX) * math.pi / 180;
    final ry = (layer.rotationY.valueAt(local) + extraRotY) * math.pi / 180;
    final tilt3D = (rx != 0 || ry != 0) && !isParticles;

    // ORDEM CONSISTENTE (triagem 3D §5 itens 7/11): o vetor recebe
    // S -> Skew -> Rx -> Ry -> Rz — a MESMA ordem da matematica de
    // orbita (Rz*Ry*Rx). Misturar ordens era o que cisalhava as formas
    // ao girar o nulo. Com tilt 3D, o Rz sobe para a matriz de
    // perspectiva; sem tilt, tudo segue no caminho 2D de sempre.
    final m = Matrix4.identity()..translateByDouble(pivot.dx, pivot.dy, 0, 1);
    if (!tilt3D) m.rotateZ(rotation);
    m
      ..multiply(Matrix4.skew(skewX, skewY))
      ..scaleByDouble(sx, sy, 1, 1)
      ..translateByDouble(-pivot.dx, -pivot.dy, 0, 1);

    Widget composed = Transform(
      transform: m,
      alignment: Alignment.center,
      child: Opacity(opacity: opacity, child: content),
    );

    if (tilt3D) {
      final pm = Matrix4.identity()
        ..setEntry(3, 2, -1 / 1200)
        ..rotateZ(rotation)
        ..rotateY(ry)
        ..rotateX(rx);
      // EXTRUDE 3D: fatias da camada empilhadas em Z atras da frente,
      // escurecidas — a espessura aparece quando a camada inclina. Video
      // e particulas ficam de fora (textura e simulacao nao se repetem).
      final extrude = project.metaOf(layer.id).extrude;
      if (extrude > 0.5 &&
          layer is! VideoLayer &&
          layer is! ParticlesLayer &&
          layer is! Element3DLayer) {
        // Uma FOTO da camada, desenhada N vezes recuando em Z num canvas
        // so (sem camadas do compositor — ver ExtrudeSnapshotPainter).
        // Fatias a ~1,5 px na tela: quanto mais de lado a camada esta,
        // mais fatias, senao a lateral sai listrada.
        final inclinacao = math.max(math.sin(rx).abs(), math.sin(ry).abs());
        final passos = (extrude * math.max(inclinacao, 0.15) / 1.5)
            .ceil()
            .clamp(2, 120);
        composed = FxSnapshot(
          painter: ExtrudeSnapshotPainter(
            perspective: pm,
            transform2d: m,
            opacity: opacity,
            passos: passos,
            passo: extrude / passos,
          ),
          child: contentSemSelecao,
        );
        if (!identical(content, contentSemSelecao)) {
          // A borda de selecao fica so na frente, sem virar caixa 3D.
          composed = Stack(
            clipBehavior: Clip.none,
            children: [
              composed,
              Positioned.fill(
                child: IgnorePointer(
                  child: Transform(
                    transform: pm,
                    alignment: Alignment.center,
                    child: Transform(
                      transform: m,
                      alignment: Alignment.center,
                      child: Container(
                        decoration: BoxDecoration(
                          border: Border.all(color: Colors.white, width: 4),
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ],
          );
        }
      } else {
        composed = Transform(
          transform: pm,
          alignment: Alignment.center,
          child: composed,
        );
      }
    }

    if (layer.blendMode != BlendMode.srcOver) {
      composed = BlendMask(blendMode: layer.blendMode, child: composed);
    }

    return Positioned(
      left: pos.dx,
      top: pos.dy,
      child: FractionalTranslation(
        translation: const Offset(-0.5, -0.5),
        child: composed,
      ),
    );
  }

  /// ESTILOS DE CAMADA (PR-X10). Sombra e brilho usam a SILHUETA da
  /// camada (o alfa), nao uma caixa — por isso o desenho e uma copia
  /// tingida e borrada por baixo do original.
  Widget _applyLayerStyles(
    LayerStyles s,
    Widget child,
    Duration local,
    Size compositionSize,
  ) {
    var out = child;

    // Sobreposicoes pintam POR CIMA, respeitando o alfa.
    if (s.colorOverlay?.enabled ?? false) {
      final o = s.colorOverlay!;
      out = Stack(
        clipBehavior: Clip.none,
        children: [
          out,
          Positioned.fill(
            child: IgnorePointer(
              child: Opacity(
                opacity: o.opacity.valueAt(local).clamp(0.0, 1.0),
                child: BlendMask(
                  blendMode: BlendMode.srcIn,
                  child: ColoredBox(color: o.color),
                ),
              ),
            ),
          ),
        ],
      );
    }
    if (s.gradientOverlay?.enabled ?? false) {
      final g = s.gradientOverlay!;
      final rad = g.angleDeg.valueAt(local) * math.pi / 180;
      out = Stack(
        clipBehavior: Clip.none,
        children: [
          out,
          Positioned.fill(
            child: IgnorePointer(
              child: Opacity(
                opacity: g.opacity.valueAt(local).clamp(0.0, 1.0),
                child: BlendMask(
                  blendMode: BlendMode.srcIn,
                  child: DecoratedBox(
                    decoration: BoxDecoration(
                      gradient: LinearGradient(
                        begin: Alignment(-math.cos(rad), -math.sin(rad)),
                        end: Alignment(math.cos(rad), math.sin(rad)),
                        colors: [g.colorA, g.colorB],
                      ),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ],
      );
    }

    // Contorno: silhueta dilatada por tras.
    if (s.stroke?.enabled ?? false) {
      final st = s.stroke!;
      // TETO DA DILATACAO. O dilate do Impeller e um laco de 2r+1
      // leituras por pixel, por eixo, na resolucao inteira — nao ha a
      // reducao que o desfoque tem. Sem teto, um contorno largo demais
      // (ou um keyframe passando por um valor alto) vira segundos de GPU
      // por quadro, e o iPhone reinicia. Cem pixels e o mesmo teto do
      // espalhamento da sombra, e ja e mais grosso que qualquer contorno
      // legivel.
      final w = st.width.valueAt(local).clamp(0.0, 100.0).toDouble();
      if (w > 0.01) {
        out = Stack(
          clipBehavior: Clip.none,
          children: [
            Positioned.fill(
              child: IgnorePointer(
                child: Opacity(
                  opacity: st.opacity.valueAt(local).clamp(0.0, 1.0),
                  child: ImageFiltered(
                    imageFilter: ui.ImageFilter.dilate(radiusX: w, radiusY: w),
                    child: _tinted(child, st.color),
                  ),
                ),
              ),
            ),
            out,
          ],
        );
      }
    }

    // Brilho externo: silhueta borrada e tingida, por tras.
    if (s.outerGlow?.enabled ?? false) {
      final g = s.outerGlow!;
      final size = g.size.valueAt(local);
      if (size > 0.01) {
        out = Stack(
          clipBehavior: Clip.none,
          children: [
            Positioned.fill(
              child: IgnorePointer(
                child: Opacity(
                  opacity: g.opacity.valueAt(local).clamp(0.0, 1.0),
                  child: ImageFiltered(
                    imageFilter: ui.ImageFilter.blur(
                      sigmaX: size / 2,
                      sigmaY: size / 2,
                      tileMode: TileMode.decal,
                    ),
                    child: _tinted(child, g.color),
                  ),
                ),
              ),
            ),
            out,
          ],
        );
      }
    }

    // Sombra projetada: silhueta deslocada, borrada e tingida, por tras.
    if (s.dropShadow?.enabled ?? false) {
      final d = s.dropShadow!;
      final off = d.offsetAt(local);
      final size = d.size.valueAt(local);
      final spread = d.spread.valueAt(local).clamp(0.0, 100.0);
      final filtered = size > 0.01 || spread > 0.01;
      out = Stack(
        clipBehavior: Clip.none,
        children: [
          Positioned.fill(
            child: IgnorePointer(
              child: Transform.translate(
                offset: off,
                child: Opacity(
                  opacity: d.opacity.valueAt(local).clamp(0.0, 1.0),
                  child: filtered
                      ? ImageFiltered(
                          imageFilter: _shadowImageFilter(
                            size,
                            spread,
                            compositionSize,
                          ),
                          child: _tinted(child, d.color),
                        )
                      : _tinted(child, d.color),
                ),
              ),
            ),
          ),
          out,
        ],
      );
    }

    // Sombra interna: mancha escura recortada pelo proprio alfa.
    if (s.innerShadow?.enabled ?? false) {
      final d = s.innerShadow!;
      final off = d.offsetAt(local);
      final size = d.size.valueAt(local);
      final spread = d.spread.valueAt(local).clamp(0.0, 100.0);
      out = Stack(
        clipBehavior: Clip.none,
        children: [
          out,
          Positioned.fill(
            child: IgnorePointer(
              child: Opacity(
                opacity: d.opacity.valueAt(local).clamp(0.0, 1.0),
                child: BlendMask(
                  blendMode: BlendMode.srcATop,
                  child: ImageFiltered(
                    imageFilter: _shadowImageFilter(
                      size,
                      spread,
                      compositionSize,
                    ),
                    child: Transform.translate(
                      offset: off,
                      child: _invertedSilhouette(child, d.color),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ],
      );
    }
    return out;
  }

  /// A silhueta cresce antes do blur (espalhamento), e o conjunto roda
  /// em espaco linear para nao criar faixas/cinza nas bordas suaves.
  static ui.ImageFilter _shadowImageFilter(
    double blurSize,
    double spread,
    Size compositionSize,
  ) {
    final blur = ui.ImageFilter.blur(
      sigmaX: math.max(0.1, blurSize / 2),
      sigmaY: math.max(0.1, blurSize / 2),
      tileMode: TileMode.decal,
    );
    final filter = spread <= 0.01
        ? blur
        : ui.ImageFilter.compose(
            outer: blur,
            inner: ui.ImageFilter.dilate(radiusX: spread, radiusY: spread),
          );
    return LinearLight.wrap(filter, compositionSize);
  }

  /// Silhueta da camada pintada de uma cor so (usa o alfa como forma).
  static Widget _tinted(Widget child, Color color) => ColorFiltered(
    colorFilter: ColorFilter.mode(color, BlendMode.srcIn),
    child: child,
  );

  /// Negativo do alfa: onde a camada NAO esta, na cor dada — e o que
  /// forma a mancha da sombra interna.
  static Widget _invertedSilhouette(Widget child, Color color) => ColorFiltered(
    colorFilter: ColorFilter.mode(color, BlendMode.srcOut),
    child: child,
  );

  /// Tempo de CONTEUDO da camada depois do remapeamento (se houver).
  static Duration _remappedTime(Layer layer, Duration local) {
    for (final e in layer.effects) {
      if (!e.enabled || e.type != EffectType.timeRemap) continue;
      final secs = e.paramAt('tempo', local);
      final us = (secs * 1000000).round();
      return Duration(microseconds: us < 0 ? 0 : us);
    }
    return local;
  }

  /// Tamanho da composicao — o raio dos efeitos de luz e uma FRACAO do
  /// menor lado, e o shader de gama precisa do tamanho para achar o
  /// pixel.
  int get fxWidth => ref.read(editorControllerProvider).outputWidth;
  int get fxHeight => ref.read(editorControllerProvider).outputHeight;

  Size get fxSize => Size(fxWidth.toDouble(), fxHeight.toDouble());

  Widget _applyEffects(
    List<EffectInstance> effects,
    Widget child,
    Duration local,
  ) {
    var out = child;
    for (final effect in effects) {
      if (!effect.enabled) continue;
      if (PixelEffectEngine.ready && pixelKernels.containsKey(effect.type)) {
        out = PixelEffectPass(
          key: ValueKey('pixel-effect-${effect.id}'),
          frame: PixelEffectFrame.of(
            effect,
            local,
            pixelScale: math.min(fxWidth, fxHeight) / 1080.0,
          ),
          child: out,
        );
        continue;
      }
      switch (effect.type) {
        case EffectType.gaussianBlur:
          // NIVEL 3: o raio e pixel (pensado em 1080p), a borda decide o
          // que existe fora da camada, e a qualidade escolhe entre a
          // conta em espaco linear (certa) e o desfoque direto (barato).
          final sigma = pxAt1080(
            effect.paramAt('raio', local).clamp(0.0, 500.0),
            fxWidth,
            fxHeight,
          );
          if (sigma > 0.01) {
            final tile = switch (effect
                .paramAt('borda', local)
                .round()
                .clamp(0, 2)) {
              1 => ui.TileMode.repeated,
              2 => ui.TileMode.mirror,
              _ => ui.TileMode.decal,
            };
            out = effect.paramAt('qualidade', local) > 0.5
                ? LinearLight.blurred(
                    sigmaX: sigma,
                    sigmaY: sigma,
                    size: fxSize,
                    tileMode: tile,
                    child: out,
                  )
                : ImageFiltered(
                    imageFilter: ui.ImageFilter.blur(
                      sigmaX: sigma,
                      sigmaY: sigma,
                      tileMode: tile,
                    ),
                    child: out,
                  );
          }

        case EffectType.lightGlow:
          // NIVEL 3. Tres numeros no montar: limite (%), raio (px) e
          // intensidade (%, ate 400 — estourar e uma escolha). No
          // avancado entram a mesclagem, a piramide e o multiplicador
          // por canal.
          //
          // A PIRAMIDE e o que faz halo grande sem pagar o raio inteiro:
          // cada nivel dobra o sigma e vale metade, somando um halo
          // largo e barato por cima do nucleo apertado.
          final intensidade = (effect.paramAt('intensity', local) / 100).clamp(
            0.0,
            4.0,
          );
          if (intensidade > 0.004) {
            final raio = pxAt1080(
              effect.paramAt('raio', local).clamp(0.0, 500.0),
              fxWidth,
              fxHeight,
            );
            final sigma = math.max(0.6, raio);
            final th = (effect.paramAt('threshold', local) / 100).clamp(
              0.0,
              0.98,
            );
            final niveis = effect
                .paramAt('piramide', local)
                .round()
                .clamp(1, 5);
            final multR = effect.paramAt('mult_r', local).clamp(0.0, 2.0);
            final multG = effect.paramAt('mult_g', local).clamp(0.0, 2.0);
            final multB = effect.paramAt('mult_b', local).clamp(0.0, 2.0);
            final modo = switch (effect
                .paramAt('mesclagem', local)
                .round()
                .clamp(0, 2)) {
              1 => BlendMode.screen,
              2 => BlendMode.lighten,
              _ => BlendMode.plus,
            };

            // O LIMITE: o que esta abaixo vai a zero ANTES do desfoque —
            // brilho e o que passa de um ponto, nao a imagem inteira
            // borrada. O ganho da intensidade entra aqui junto, para o
            // halo nascer forte e nao ser multiplicado depois de somado.
            final e = 1 / (1 - th);
            final g = e * intensidade;
            final o = -th * 255 * e * intensidade;
            Widget fonte = ColorFiltered(
              colorFilter: ColorFilter.matrix(<double>[
                g * multR,
                0,
                0,
                0,
                o,
                0,
                g * multG,
                0,
                0,
                o,
                0,
                0,
                g * multB,
                0,
                o,
                0,
                0,
                0,
                1,
                0,
              ]),
              child: out,
            );
            if (PixelEffectEngine.ready) {
              fonte = PixelEffectPass(
                frame: PixelEffectFrame(21, [
                  th,
                  .25,
                  0,
                  0,
                  multR,
                  multG,
                  multB,
                  0,
                  0,
                  1,
                ]),
                child: out,
              );
              fonte = PixelEffectPass(
                frame: PixelEffectFrame(22, [
                  math.log(intensidade) / math.ln2,
                  3,
                  0,
                ]),
                child: fonte,
              );
            }
            final tingido = ColorFiltered(
              // srcATop substituia o RGB extraido pela cor solida e
              // ressuscitava pixels abaixo do threshold. Multiplicar
              // preserva o preto (sem luz) e o ganho da intensidade.
              colorFilter: ColorFilter.mode(effect.color, BlendMode.modulate),
              child: fonte,
            );

            final pesos = <double>[
              for (var k = 0; k < niveis; k++) 1 / (1 << k),
            ];
            final soma = pesos.fold<double>(0, (a, b) => a + b);
            // O TETO. O preset Neon (raio 60, piramide 4) pedia sigma
            // 480 no ultimo nivel; o Sonho, 2400. Cada um desses e uma
            // textura de centenas de megabytes na GPU — e o app fechava
            // antes de desenhar o quadro. Ver [sigmaTeto].
            final piramide = piramideAteOTeto(
              [for (var k = 0; k < niveis; k++) sigma * (1 << k)],
              [for (var k = 0; k < niveis; k++) pesos[k] / soma],
              sigmaTeto(fxWidth, fxHeight),
            );
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                for (final nivel in piramide)
                  BlendMask(
                    blendMode: modo,
                    margem: 3 * nivel.sigma + 4,
                    child: Opacity(
                      opacity: nivel.peso.clamp(0.0, 1.0),
                      // EM ESPACO LINEAR: glow SOMA luz, e soma de luz em
                      // sRGB da o halo lavado com borda escura de sempre.
                      child: LinearLight.blurred(
                        sigmaX: nivel.sigma,
                        sigmaY: nivel.sigma,
                        size: fxSize,
                        child: tingido,
                      ),
                    ),
                  ),
              ],
            );
          }

        case EffectType.flicker:
          // FLICKER: a camada pisca. Aleatorio e a lampada ruim; strobe e
          // a balada; senoide e a respiracao. Age na opacidade ou no
          // brilho — no brilho a camada nao some, so escurece.
          final amt = effect.paramAt('amount', local).clamp(0.0, 1.0);
          if (amt > 0.004) {
            final freq = effect.paramAt('frequency', local).clamp(0.5, 60.0);
            final estilo = effect.paramAt('style', local).round().clamp(0, 2);
            final alvo = effect.paramAt('target', local).round().clamp(0, 1);
            final seedF = effect.paramAt('seed', local).round();
            final x = local.inMicroseconds / 1e6 * freq;
            final onda = switch (estilo) {
              1 => (x - x.floor()) < 0.5 ? 1.0 : -1.0,
              2 => math.sin(x * 2 * math.pi),
              _ => fxNoiseSigned(seedF + 7, 3, x),
            };
            // 0..1: quanto da camada FICA neste instante.
            final k = (1 - amt * (0.5 - 0.5 * onda)).clamp(0.0, 1.0);
            if (alvo == 0) {
              out = Opacity(opacity: k, child: out);
            } else {
              out = ColorFiltered(
                colorFilter: ColorFilter.matrix(<double>[
                  k, 0, 0, 0, 0, //
                  0, k, 0, 0, 0,
                  0, 0, k, 0, 0,
                  0, 0, 0, 1, 0,
                ]),
                child: out,
              );
            }
          }

        case EffectType.gradient4:
          // GRADIENTE DE QUATRO CORES sobre a camada, preso ao alfa dela
          // (srcATop): cada canto uma cor. Girar troca os cantos de lugar.
          final opG = effect.paramAt('opacity', local).clamp(0.0, 1.0);
          if (opG > 0.004) {
            final mescla = effect.paramAt('blend', local).round().clamp(0, 3);
            final giro = effect.paramAt('angle', local) * math.pi / 180;
            final cores = [
              effect.color,
              effect.extraColor(0),
              effect.extraColor(2),
              effect.extraColor(1),
            ];
            final modoG = switch (mescla) {
              1 => BlendMode.multiply,
              2 => BlendMode.screen,
              3 => BlendMode.overlay,
              _ => BlendMode.srcATop,
            };
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                Positioned.fill(
                  child: IgnorePointer(
                    child: BlendMask(
                      blendMode: mescla == 0 ? BlendMode.srcATop : modoG,
                      child: mescla == 0
                          ? Transform.rotate(
                              angle: giro,
                              child: CustomPaint(
                                painter: Gradient4Painter(
                                  topLeft: cores[0],
                                  topRight: cores[1],
                                  bottomLeft: cores[2],
                                  bottomRight: cores[3],
                                  opacity: opG,
                                ),
                              ),
                            )
                          // Com mescla, o gradiente ainda fica preso ao alfa
                          // da camada: srcATop por dentro, mescla por fora.
                          : BlendMask(
                              blendMode: BlendMode.srcATop,
                              child: Transform.rotate(
                                angle: giro,
                                child: CustomPaint(
                                  painter: Gradient4Painter(
                                    topLeft: cores[0],
                                    topRight: cores[1],
                                    bottomLeft: cores[2],
                                    bottomRight: cores[3],
                                    opacity: opG,
                                  ),
                                ),
                              ),
                            ),
                    ),
                  ),
                ),
              ],
            );
          }

        case EffectType.liquidGlass:
          // LIQUID GLASS: a camada vira uma placa de vidro sobre o que
          // esta atras — desfoque do fundo, leve LENTE (o fundo cresce
          // um pouco por baixo do vidro), tingimento, brilho especular
          // correndo pela borda de cima e sombra por baixo. A camada em
          // si (texto, icone) fica por cima, nitida.
          final blurLG = effect.paramAt('blur', local).clamp(0.0, 40.0);
          final saturacaoLG =
              effect.paramAt('saturation', local).clamp(0.0, 200.0) / 100;
          final brilhoLG =
              effect.paramAt('brightness', local).clamp(0.0, 200.0) / 100;
          final grainLG = effect.paramAt('grain', local).clamp(0.0, 0.12);
          final refr = effect.paramAt('refraction', local).clamp(0.0, 1.0);
          final rimLG = effect.paramAt('rim', local).clamp(0.0, 1.0);
          final tintLG = effect.paramAt('tint', local).clamp(0.0, 1.0);
          final raioLG = effect.paramAt('radius', local).clamp(0.0, 200.0);
          final sombraLG = effect.paramAt('shadow', local).clamp(0.0, 1.0);
          final folgaLG = effect.paramAt('padding', local).clamp(0.0, 120.0);
          final bordaLG = BorderRadius.circular(raioLG);
          // O caminho rapido so vale quando TODO parametro que pinta pixels
          // esta neutro. Blur/cor neutros nao podem desligar grao, tinta,
          // borda, refracao ou sombra configurados no painel avancado.
          final neutroLG =
              blurLG <= 0.001 &&
              (saturacaoLG - 1).abs() <= 0.001 &&
              (brilhoLG - 1).abs() <= 0.001 &&
              grainLG <= 0.0001 &&
              refr <= 0.001 &&
              rimLG <= 0.001 &&
              tintLG <= 0.001 &&
              sombraLG <= 0.001;
          if (!neutroLG) {
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                Positioned(
                  left: -folgaLG,
                  top: -folgaLG,
                  right: -folgaLG,
                  bottom: -folgaLG,
                  child: IgnorePointer(
                    child: LayoutBuilder(
                      builder: (context, c) {
                        final w = c.maxWidth, h = c.maxHeight;
                        final k = 1 + refr * 0.12;
                        // Lente: escala o fundo em torno do centro da placa.
                        final lente = Matrix4.identity()
                          ..translateByDouble(w / 2, h / 2, 0, 1)
                          ..scaleByDouble(k, k, 1, 1)
                          ..translateByDouble(-w / 2, -h / 2, 0, 1);
                        final optico = ui.ImageFilter.compose(
                          outer: ui.ImageFilter.blur(
                            sigmaX: blurLG,
                            sigmaY: blurLG,
                            tileMode: TileMode.mirror,
                          ),
                          inner: ui.ImageFilter.matrix(
                            lente.storage,
                            filterQuality: FilterQuality.medium,
                          ),
                        );
                        return Stack(
                          children: [
                            if (sombraLG > 0.01)
                              Positioned.fill(
                                child: DecoratedBox(
                                  decoration: BoxDecoration(
                                    borderRadius: bordaLG,
                                    boxShadow: [
                                      BoxShadow(
                                        color: Colors.black.withValues(
                                          alpha: 0.45 * sombraLG,
                                        ),
                                        blurRadius: 28,
                                        offset: const Offset(0, 12),
                                      ),
                                    ],
                                  ),
                                ),
                              ),
                            Positioned.fill(
                              child: ClipRRect(
                                borderRadius: bordaLG,
                                child: ColorFiltered(
                                  colorFilter: ColorFilter.matrix(
                                    _saturationBrightnessMatrix(
                                      saturacaoLG,
                                      brilhoLG,
                                    ),
                                  ),
                                  child: BackdropFilter(
                                    // O blur acontece entre as curvas sRGB/linear;
                                    // o ajuste de cor e aplicado ao passe pronto.
                                    filter: LinearLight.wrap(
                                      optico,
                                      Size(w, h),
                                    ),
                                    child: DecoratedBox(
                                      decoration: BoxDecoration(
                                        borderRadius: bordaLG,
                                        color: effect.color.withValues(
                                          alpha: tintLG,
                                        ),
                                        border: Border.all(
                                          color: Colors.white.withValues(
                                            alpha: 0.55 * rimLG,
                                          ),
                                          width: 1.2,
                                        ),
                                      ),
                                    ),
                                  ),
                                ),
                              ),
                            ),
                            // O reflexo especular: claro em cima e a esquerda,
                            // um fio claro embaixo — a luz passando pela curva.
                            Positioned.fill(
                              child: DecoratedBox(
                                decoration: BoxDecoration(
                                  borderRadius: bordaLG,
                                  gradient: LinearGradient(
                                    begin: Alignment.topLeft,
                                    end: Alignment.bottomRight,
                                    colors: [
                                      Colors.white.withValues(
                                        alpha: 0.38 * rimLG,
                                      ),
                                      Colors.white.withValues(
                                        alpha: 0.06 * rimLG,
                                      ),
                                      Colors.transparent,
                                      Colors.white.withValues(
                                        alpha: 0.14 * rimLG,
                                      ),
                                    ],
                                    stops: const [0, 0.3, 0.7, 1],
                                  ),
                                ),
                              ),
                            ),
                            if (grainLG > 0.0001)
                              Positioned.fill(
                                child: ClipRRect(
                                  borderRadius: bordaLG,
                                  child: IgnorePointer(
                                    child: BlendMask(
                                      blendMode: BlendMode.overlay,
                                      child: CustomPaint(
                                        painter: _GrainPainter(
                                          amount: grainLG,
                                          size: 0.7,
                                          seed: 8606,
                                          time: local,
                                        ),
                                      ),
                                    ),
                                  ),
                                ),
                              ),
                          ],
                        );
                      },
                    ),
                  ),
                ),
                out,
              ],
            );
          }

        case EffectType.tint:
          final forcaTint = effect.paramAt('strength', local).clamp(0.0, 1.0);
          // Em forca zero o srcATop ja devolvia o destino intacto — mas
          // pagava uma camada de composicao para isso.
          if (forcaTint > 0.004) {
            out = ColorFiltered(
              colorFilter: ColorFilter.mode(
                effect.color.withValues(alpha: forcaTint),
                BlendMode.srcATop,
              ),
              child: out,
            );
          }

        case EffectType.glowVol:
          // DEEP GLOW: piramide de bloom com pesos NORMALIZADOS, em
          // espaco linear. "Conservacao de energia" quer dizer isto: a
          // soma dos pesos e 1, entao acrescentar nivel deixa o glow
          // mais suave sem deixa-lo mais claro.
          final exposure = effect.paramAt('exposure', local);
          final ganho = exposureGain(exposure).clamp(0.0, 8.0);
          if (ganho > 0.01) {
            final r = effect.paramAt('radius', local).clamp(0.0, 1.0);
            final raioPx = radiusToPixels(
              r,
              fxWidth,
              fxHeight,
            ).clamp(1.0, 2000.0);
            final quality = effect
                .paramAt('quality', local)
                .round()
                .clamp(0, 2);
            final niveis = bloomLevels(quality);
            final pesos = bloomWeights(niveis);
            final sigmas = bloomSigmas(raioPx, niveis);

            final limiar = effect.paramAt('threshold', local);
            final suavidade = effect.paramAt('threshold_softness', local);
            final aspecto = effect
                .paramAt('aspect_ratio', local)
                .clamp(0.1, 10.0);
            final satur = effect.paramAt('glow_saturation', local) / 100.0;
            final tintAmt = effect.paramAt('tint_amount', local);
            final tintMode = effect
                .paramAt('tint_mode', local)
                .round()
                .clamp(0, 3);
            final multR = effect.paramAt('red_radius_multiplier', local);
            final multG = effect.paramAt('green_radius_multiplier', local);
            final multB = effect.paramAt('blue_radius_multiplier', local);
            final porCanal =
                (multR - multG).abs() > 0.01 || (multG - multB).abs() > 0.01;
            final soGlow = effect.paramAt('glow_only', local) >= 0.5;
            final blend = effect
                .paramAt('blend_mode', local)
                .round()
                .clamp(0, 2);
            final anguloOn = effect.paramAt('enable_angle', local) >= 0.5;
            final anguloRad = effect.paramAt('angle', local) * math.pi / 180;
            // 0 = Luminance, 1 = Chrominance.
            final modoLimiar = effect
                .paramAt('threshold_mode', local)
                .round()
                .clamp(0, 1);
            // 0 = Exponential, 1 = Iris.
            final modoGlow = effect
                .paramAt('glow_mode', local)
                .round()
                .clamp(0, 1);
            final reducaoRuido = effect
                .paramAt('noise_reduction', local)
                .clamp(0.0, 100.0);
            final reducao = effect.paramAt('downsample', local).clamp(1.0, 8.0);
            final tonemapping = effect
                .paramAt('tonemapping', local)
                .round()
                .clamp(0, 3);
            final lensDirt = effect
                .paramAt('lens_dirt_amount', local)
                .clamp(0.0, 200.0);

            // LIMIAR: so o que passa do valor vira glow. A rampa suave
            // evita a linha reta onde o brilho cruza o limiar — com
            // limiar duro, o glow "liga" de repente no meio do degrade.
            Widget fonte = out;

            // REDUCAO DE RUIDO, antes do limiar.
            //
            // Granulacao de sensor tem pixels isolados acima do limiar, e
            // cada um vira uma estrelinha cintilando de quadro em quadro.
            // Um desfoque minimo antes do corte tira o pixel solto e
            // deixa passar o que e area clara de verdade.
            if (reducaoRuido > 0.5) {
              final sr = reducaoRuido / 100 * 3.0;
              fonte = LinearLight.blurred(
                sigmaX: sr,
                sigmaY: sr,
                size: fxSize,
                child: fonte,
              );
            }

            if (PixelEffectEngine.ready) {
              // Per-pixel luminance/chroma selection preserves alpha. Tone
              // response is applied to the source before the SDR blur pyramid;
              // this is deliberately not described as an HDR intermediate.
              fonte = PixelEffectPass(
                frame: PixelEffectFrame(
                  21,
                  [
                    limiar,
                    suavidade,
                    0,
                    modoLimiar.toDouble(),
                    1,
                    1,
                    1,
                    tintMode.toDouble(),
                    tintAmt,
                    satur,
                  ],
                  color: [
                    effect.color.r,
                    effect.color.g,
                    effect.color.b,
                    effect.color.a,
                  ],
                  extraColors: [
                    for (final c in effect.extraColors) ...[c.r, c.g, c.b, c.a],
                  ],
                ),
                child: fonte,
              );
              fonte = PixelEffectPass(
                frame: PixelEffectFrame(22, [
                  exposure.clamp(-8.0, 3.0),
                  tonemapping.toDouble(),
                  lensDirt,
                ]),
                child: fonte,
              );
            } else if (limiar > 0.01) {
              // O limiar REMAPEIA (limiar -> 0, branco -> 1); a conta
              // mora em bloom.dart, onde da para testa-la.
              final (escala, desl) = glowThresholdMatrix(
                limiar,
                suavidade,
                ganho,
              );
              // O QUE O LIMIAR MEDE.
              //
              // Luminancia: passa o que e CLARO — o caso comum, e o que
              // faz o glow morar nos realces.
              // Crominancia: passa o que e COLORIDO, subtraindo o cinza
              // de cada canal. Um neon saturado sobre fundo claro nao
              // ganha glow por luminancia (o fundo e tao claro quanto);
              // por crominancia, so o neon brilha.
              fonte = ColorFiltered(
                colorFilter: ColorFilter.matrix(
                  modoLimiar == 1
                      ? <double>[
                          escala * (1 - 0.2126), -escala * 0.7152,
                          -escala * 0.0722, 0, desl, //
                          -escala * 0.2126, escala * (1 - 0.7152),
                          -escala * 0.0722, 0, desl,
                          -escala * 0.2126, -escala * 0.7152,
                          escala * (1 - 0.0722), 0, desl,
                          0, 0, 0, 1, 0,
                        ]
                      : <double>[
                          escala, 0, 0, 0, desl, //
                          0, escala, 0, 0, desl,
                          0, 0, escala, 0, desl,
                          0, 0, 0, 1, 0,
                        ],
                ),
                child: fonte,
              );
            }

            // DOWNSAMPLE: quanto detalhe o halo guarda.
            //
            // Encolher e devolver ao tamanho apaga o detalhe fino do
            // halo — que e o mesmo resultado de calcular o glow numa
            // resolucao menor, que e o que o nome promete. Em 1 nao
            // acontece nada.
            if (reducao > 1.01) {
              fonte = ImageFiltered(
                imageFilter: ui.ImageFilter.compose(
                  outer: ui.ImageFilter.matrix(
                    Matrix4.diagonal3Values(reducao, reducao, 1).storage,
                    filterQuality: FilterQuality.low,
                  ),
                  inner: ui.ImageFilter.matrix(
                    Matrix4.diagonal3Values(
                      1 / reducao,
                      1 / reducao,
                      1,
                    ).storage,
                    filterQuality: FilterQuality.low,
                  ),
                ),
                child: fonte,
              );
            }
            if (!PixelEffectEngine.ready && (satur - 1).abs() > 0.01) {
              fonte = ColorFiltered(
                colorFilter: ColorFilter.matrix(_saturationMatrix(satur)),
                child: fonte,
              );
            }
            if (!PixelEffectEngine.ready && tintMode != 0 && tintAmt > 0.01) {
              fonte = ColorFiltered(
                colorFilter: ColorFilter.mode(
                  effect.color.withValues(alpha: tintAmt),
                  BlendMode.srcATop,
                ),
                child: fonte,
              );
            }

            Widget borra(Widget c, double sigma, double mult) {
              // ASPECTO e ANGULO: um glow anamorfico se espalha mais num
              // eixo. O angulo gira a fonte, borra e desgira.
              final sx = sigma * mult * aspecto;
              final sy = sigma * mult / aspecto;
              Widget alvo = c;
              if (anguloOn && anguloRad.abs() > 0.001) {
                alvo = Transform.rotate(angle: -anguloRad, child: alvo);
              }
              if (modoGlow == 1) {
                // IRIS: o halo ganha as PONTAS da abertura da lente.
                //
                // Bloom exponencial e redondo por construcao — e o que
                // uma gaussiana faz. A estrela que se ve em foto vem das
                // laminas do diafragma, e se reproduz somando desfoques
                // muito alongados em direcoes diferentes. Tres eixos ja
                // dao a leitura de seis pontas.
                Widget lamina(double giro) {
                  final r = LinearLight.blurred(
                    sigmaX: math.max(0.1, sx * 2.2),
                    sigmaY: math.max(0.1, sy * 0.18),
                    size: fxSize,
                    child: Transform.rotate(angle: -giro, child: alvo),
                  );
                  return Transform.rotate(angle: giro, child: r);
                }

                final alcanceLamina = 3 * sx * 2.2 + 4;
                alvo = Stack(
                  clipBehavior: Clip.none,
                  children: [
                    lamina(0),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: alcanceLamina,
                      child: lamina(math.pi / 3),
                    ),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: alcanceLamina,
                      child: lamina(2 * math.pi / 3),
                    ),
                  ],
                );
              } else {
                alvo = LinearLight.blurred(
                  sigmaX: math.max(0.1, sx),
                  sigmaY: math.max(0.1, sy),
                  size: fxSize,
                  child: alvo,
                );
              }
              if (anguloOn && anguloRad.abs() > 0.001) {
                alvo = Transform.rotate(angle: anguloRad, child: alvo);
              }
              return alvo;
            }

            // ALCANCE do halo de um nivel: ate onde o desfoque chega fora
            // da caixa. E a margem que a foto da mescla precisa ter.
            double alcance(double sigma) {
              final mult = math.max(
                math.max(multR, multG),
                math.max(multB, 1.0),
              );
              final eixo = math.max(aspecto, 1 / aspecto);
              final bruto =
                  3 * sigma * mult * eixo * (modoGlow == 1 ? 2.2 : 1.0) + 4;
              // A margem multiplicava o sigma pelo canal (ate 2x), pela
              // proporcao (ate 10x) e pelo modo (2,2x): um raio grande
              // pedia noventa mil pixels de margem de cada lado. Nada do
              // que passa da composicao inteira aparece — o resto e so
              // textura que o aparelho nao tem.
              return math.min(bruto, math.max(fxWidth, fxHeight) * 1.0);
            }

            Widget nivel(double sigma, double peso) {
              final w = porCanal
                  ? Stack(
                      clipBehavior: Clip.none,
                      children: [
                        borra(_channelIso(fonte, 0), sigma, multR),
                        BlendMask(
                          blendMode: BlendMode.plus,
                          margem: alcance(sigma),
                          child: borra(_channelIso(fonte, 1), sigma, multG),
                        ),
                        BlendMask(
                          blendMode: BlendMode.plus,
                          margem: alcance(sigma),
                          child: borra(_channelIso(fonte, 2), sigma, multB),
                        ),
                      ],
                    )
                  : borra(fonte, sigma, 1);
              // O ganho ja entrou na FONTE, junto do limiar; aqui so
              // o peso do nivel. Multiplicar de novo seria contar a
              // exposicao duas vezes.
              return Opacity(opacity: peso.clamp(0.0, 1.0), child: w);
            }

            // O TETO, igual ao do Glow: com qualidade Alta a piramide
            // vai a cinco niveis e o ultimo sigma e dezesseis vezes o
            // raio. Ver [sigmaTeto] e [piramideAteOTeto].
            final piramide = piramideAteOTeto(
              sigmas,
              pesos,
              sigmaTeto(fxWidth, fxHeight),
            );
            final camadas = <(Widget, double)>[
              for (final n in piramide)
                (nivel(n.sigma, n.peso), alcance(n.sigma)),
            ];

            // BLEND: Add e o padrao — luz soma. Screen e mais suave nas
            // altas; Normal cobre.
            final modo = switch (blend) {
              1 => BlendMode.screen,
              2 => BlendMode.srcOver,
              _ => BlendMode.plus,
            };

            out = soGlow
                // GLOW ONLY: so o brilho, sem a fonte. Serve para mandar
                // o glow para outra camada e mesclar la. Os niveis se
                // somam entre si.
                ? Stack(
                    clipBehavior: Clip.none,
                    children: [
                      camadas.first.$1,
                      for (final (c, a) in camadas.skip(1))
                        BlendMask(
                          blendMode: BlendMode.plus,
                          margem: a,
                          child: c,
                        ),
                    ],
                  )
                // A FONTE EMBAIXO, O GLOW POR CIMA. Com a fonte por
                // ultimo, o solido cobria o brilho e o glow so aparecia
                // pela borda de fora — um contorno, nao um glow. O Deep
                // Glow soma luz em cima de tudo: o miolo claro tambem
                // acende, e e isso que faz um texto branco "queimar".
                : Stack(
                    clipBehavior: Clip.none,
                    children: [
                      out,
                      for (final (c, a) in camadas)
                        BlendMask(blendMode: modo, margem: a, child: c),
                    ],
                  );
          }

        case EffectType.tremor:
          // SHAKE: fase INTEGRADA no tempo, componentes aleatoria e de
          // onda separadas por eixo, e canais RGB com fase propria.
          // A AMPLITUDE ERA PIXEL CRU, e a ficha ja dizia que era
          // relativa. Um tremor de "1" sacudia 60 px em qualquer
          // resolucao: em 4K isso e metade do tremor aparente de 1080p,
          // e o mesmo projeto exportado em duas resolucoes tremia
          // diferente. Em 1080p o resultado continua identico.
          final amp = pxAt1080(
            effect.paramAt('amplitude', local) * 60,
            fxWidth,
            fxHeight,
          );
          final style = effect.paramAt('style', local).round().clamp(0, 2);
          final seed = effect.paramAt('seed', local).round();
          final phase =
              integratedPhase(effect.track('frequency'), local) +
              effect.paramAt('phase', local) / 360.0;

          ShakeAxis eixo(String pre) => ShakeAxis(
            randomAmplitude: effect.paramAt('${pre}_random_amplitude', local),
            randomFrequency: effect.paramAt('${pre}_random_frequency', local),
            waveAmplitude: effect.paramAt('${pre}_wave_amplitude', local),
            waveFrequency: effect.paramAt('${pre}_wave_frequency', local),
            phaseDeg: effect.paramAt('${pre}_phase', local),
          );

          final ex = eixo('x');
          final ey = eixo('y');
          final ez = eixo('z');
          final et = eixo('tilt');

          TremorSample sampleAt(double shift) => tremorSample(
            amplitudePx: amp,
            phase: phase + shift,
            style: style,
            seed: seed,
            x: ex,
            y: ey,
            z: ez,
            tilt: et,
            stillness: effect.paramAt('stillness', local),
            twitchFrequency: effect.paramAt('twitch_frequency', local),
            drift: effect.paramAt('drift', local),
            centerBias: effect.paramAt('center_bias', local),
            zDistance: effect.paramAt('z_distance', local),
          );

          // BORDAS: refletir e a escolha certa por padrao — a imagem
          // sacode e a borda continua parecendo imagem, em vez de virar
          // faixa preta.
          final bordas = effect.paramAt('edges', local).round().clamp(0, 2);
          // As copias ficam FORA da caixa, coladas a cada lado: e o que
          // preenche o vao que o tremor abre na borda. Espelhar sobre o
          // proprio centro (como era) punha a copia EM CIMA da original —
          // texto saia com um gemeo invertido por cima.
          Widget comBorda(Widget c) => switch (bordas) {
            0 => Stack(
              clipBehavior: Clip.none,
              children: [
                Transform.scale(
                  scaleX: -1,
                  alignment: Alignment.centerLeft,
                  child: c,
                ),
                Transform.scale(
                  scaleX: -1,
                  alignment: Alignment.centerRight,
                  child: c,
                ),
                Transform.scale(
                  scaleY: -1,
                  alignment: Alignment.topCenter,
                  child: c,
                ),
                Transform.scale(
                  scaleY: -1,
                  alignment: Alignment.bottomCenter,
                  child: c,
                ),
                c,
              ],
            ),
            1 => Stack(
              clipBehavior: Clip.none,
              children: [
                Transform.translate(
                  offset: Offset(-fxWidth.toDouble(), 0),
                  child: c,
                ),
                Transform.translate(
                  offset: Offset(fxWidth.toDouble(), 0),
                  child: c,
                ),
                Transform.translate(
                  offset: Offset(0, -fxHeight.toDouble()),
                  child: c,
                ),
                Transform.translate(
                  offset: Offset(0, fxHeight.toDouble()),
                  child: c,
                ),
                c,
              ],
            ),
            _ => c,
          };

          Widget shaken(TremorSample s, Widget c) => Transform(
            transform: Matrix4.identity()
              ..translateByDouble(s.dx, s.dy, 0, 1)
              ..rotateZ(s.rotationDeg * math.pi / 180)
              ..scaleByDouble(s.scale, s.scale, 1, 1),
            alignment: Alignment.center,
            child: c,
          );

          final s0 = sampleAt(0);
          final rgbAleatorio = effect
              .paramAt('rgb_randomness', local)
              .clamp(0.0, 1.0);
          final rgbFreq = effect.paramAt('rgb_frequency', local);
          final ampR = effect.paramAt('red_amplitude', local);
          final ampG = effect.paramAt('green_amplitude', local);
          final ampB = effect.paramAt('blue_amplitude', local);
          final separaCanais =
              rgbAleatorio > 0.001 ||
              (ampR - ampG).abs() > 0.001 ||
              (ampG - ampB).abs() > 0.001 ||
              effect.paramAt('red_phase', local).abs() > 0.5 ||
              effect.paramAt('green_phase', local).abs() > 0.5 ||
              effect.paramAt('blue_phase', local).abs() > 0.5;

          if (!s0.isNeutral || separaCanais) {
            if (separaCanais) {
              // FASE POR CANAL: desloca o canal NO TEMPO. O vermelho se
              // move antes, os outros seguem — franja organica, que um
              // deslocamento estatico nao consegue imitar.
              double faseDe(String p, double amplitude) =>
                  effect.paramAt(p, local) / 360.0 +
                  (rgbAleatorio <= 0
                      ? 0.0
                      : fxNoiseSigned(seed + 31, 9, phase * rgbFreq) *
                            rgbAleatorio *
                            0.15);

              TremorSample canal(String p, double a) {
                final base = sampleAt(faseDe(p, a));
                return TremorSample(
                  base.dx * a,
                  base.dy * a,
                  base.scale,
                  base.rotationDeg,
                );
              }

              // A margem da foto cobre ate onde o tremor pode levar o
              // canal: um quinto do quadro e mais do que qualquer tremor.
              final alcanceTremor = fxSize.longestSide * 0.2;
              out = comBorda(
                Stack(
                  clipBehavior: Clip.none,
                  children: [
                    shaken(canal('red_phase', ampR), _channelIso(out, 0)),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: alcanceTremor,
                      child: shaken(
                        canal('green_phase', ampG),
                        _channelIso(out, 1),
                      ),
                    ),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: alcanceTremor,
                      child: shaken(
                        canal('blue_phase', ampB),
                        _channelIso(out, 2),
                      ),
                    ),
                  ],
                ),
              );
            } else {
              out = comBorda(shaken(s0, out));
            }

            // MOTION BLUR do proprio Shake: amostras ao longo do rastro
            // do tremor. Sem ele, um tremor forte vira imagem picotada.
            if (effect.paramAt('motion_blur', local) >= 0.5) {
              final comprimento = effect
                  .paramAt('blur_length', local)
                  .clamp(0.0, 10.0);
              if (comprimento > 0.01) {
                const n = 5;
                final copias = <Widget>[];
                for (var k = 0; k < n; k++) {
                  final f = (k / (n - 1) - 0.5) * comprimento * 0.02;
                  copias.add(
                    Opacity(
                      opacity: 1 / (k + 1),
                      child: shaken(sampleAt(f), out),
                    ),
                  );
                }
                out = Stack(clipBehavior: Clip.none, children: copias);
              }
            }
          }

        case EffectType.glitch:
          // Modulador mestre + operadores com tiques puros (PR-FX4).
          final master = effect.paramAt('quantidade', local).clamp(0.0, 2.0);
          if (master > 0.001) {
            final tau = integratedPhase(effect.track('velocidade'), local);
            final st = glitchState(
              master: master,
              tau: tau,
              intervalSec: effect.paramAt('intervalo', local),
              seed: effect.paramAt('semente', local).round(),
              slide: effect.paramAt('deslize', local),
              scaleAmt: effect.paramAt('escala', local),
              colorAmt: effect.paramAt('cor', local),
              lightAmt: effect.paramAt('luz', local),
              blurAmt: effect.paramAt('desfoque', local),
              rgbAmt: effect.paramAt('rgb', local),
            );
            if (!st.isNeutral) {
              var g = out;
              if (st.hueDeg.abs() > 0.5) {
                g = ColorFiltered(
                  colorFilter: ColorFilter.matrix(hueRotateMatrix(st.hueDeg)),
                  child: g,
                );
              }
              if (st.brightness > 0.01) {
                final b = 1 + st.brightness;
                g = ColorFiltered(
                  colorFilter: ColorFilter.matrix(<double>[
                    b, 0, 0, 0, 0, //
                    0, b, 0, 0, 0, //
                    0, 0, b, 0, 0, //
                    0, 0, 0, 1, 0,
                  ]),
                  child: g,
                );
              }
              if (st.blurSigma > 0.2) {
                g = ImageFiltered(
                  imageFilter: ui.ImageFilter.blur(
                    sigmaX: st.blurSigma,
                    sigmaY: st.blurSigma * 0.4,
                    tileMode: TileMode.decal,
                  ),
                  child: g,
                );
              }
              Widget moved(double extraDx, Widget c) => Transform(
                transform: Matrix4.identity()
                  ..translateByDouble(st.dx + extraDx, st.dy, 0, 1)
                  ..scaleByDouble(st.scale, st.scale, 1, 1),
                alignment: Alignment.center,
                child: c,
              );
              if (st.rgbSep > 0.2) {
                out = Stack(
                  clipBehavior: Clip.none,
                  children: [
                    moved(-st.rgbSep, _channelIso(g, 0)),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: st.rgbSep.abs() + 4,
                      child: moved(0, _channelIso(g, 1)),
                    ),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: st.rgbSep.abs() + 4,
                      child: moved(st.rgbSep, _channelIso(g, 2)),
                    ),
                  ],
                );
              } else {
                out = moved(0, g);
              }
            }
          }

        case EffectType.rgbSplit:
          // Mesma historia do tremor: deslocamento em pixel cru, com a
          // ficha dizendo relativo. Em 1080p nada muda.
          final d = pxAt1080(
            effect.paramAt('deslocamento', local).clamp(0.0, 100.0),
            fxWidth,
            fxHeight,
          );
          if (d > 0.2) {
            final ang = effect.paramAt('angulo', local) * math.pi / 180;
            final off = Offset(math.cos(ang) * d, math.sin(ang) * d);
            // QUAIS CANAIS se afastam (avancado): o par decide a cor das
            // franjas. O terceiro fica parado, no lugar da imagem.
            final (antes, meio, depois) = switch (effect
                .paramAt('canais', local)
                .round()
                .clamp(0, 2)) {
              1 => (0, 2, 1),
              2 => (1, 0, 2),
              _ => (0, 1, 2),
            };
            final suave = effect.paramAt('suavizar', local).clamp(0.0, 1.0);
            final sigma = suave * d * 0.35;
            Widget canal(int i, Offset deslocamento) {
              Widget w = _channelIso(out, i);
              if (sigma > 0.05) {
                w = LinearLight.blurred(
                  sigmaX: sigma,
                  sigmaY: sigma,
                  size: fxSize,
                  child: w,
                );
              }
              return deslocamento == Offset.zero
                  ? w
                  : Transform.translate(offset: deslocamento, child: w);
            }

            final margem = off.distance + 3 * sigma + 4;
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                canal(antes, -off),
                BlendMask(
                  blendMode: BlendMode.plus,
                  margem: margem,
                  child: canal(meio, Offset.zero),
                ),
                BlendMask(
                  blendMode: BlendMode.plus,
                  margem: margem,
                  child: canal(depois, off),
                ),
              ],
            );
          }

        case EffectType.echo:
          // Tratado no nivel da camada (_buildLayers): aqui e neutro.
          break;

        case EffectType.spatialEcho:
          // Repeticao no ESPACO com transformacao progressiva (item 28).
          final n = effect.paramAt('copias', local).round().clamp(1, 12);
          if (n > 1) {
            final dx = effect.paramAt('dx', local);
            final dy = effect.paramAt('dy', local);
            final scaleStep = effect.paramAt('escala', local) / 100.0;
            final rotStep = effect.paramAt('rotacao', local) * math.pi / 180;
            final decay = effect.paramAt('decaimento', local).clamp(0.05, 1.0);
            final hueStep = effect.paramAt('matiz', local);
            final copies = <Widget>[];
            for (var c = n - 1; c >= 0; c--) {
              Widget w = out;
              if (hueStep > 0.5 && c > 0) {
                w = ColorFiltered(
                  colorFilter: ColorFilter.matrix(hueRotateMatrix(hueStep * c)),
                  child: w,
                );
              }
              copies.add(
                Opacity(
                  opacity: math.pow(decay, c).toDouble().clamp(0.0, 1.0),
                  child: Transform(
                    transform: Matrix4.identity()
                      ..translateByDouble(dx * c, dy * c, 0, 1)
                      ..rotateZ(rotStep * c)
                      ..scaleByDouble(
                        math.pow(scaleStep, c).toDouble(),
                        math.pow(scaleStep, c).toDouble(),
                        1,
                        1,
                      ),
                    alignment: Alignment.center,
                    child: w,
                  ),
                ),
              );
            }
            out = Stack(clipBehavior: Clip.none, children: copies);
          }

        case EffectType.radialAberration:
          // Cresce do centro para a borda, como lente real (item 14):
          // cada canal amostrado com uma ESCALA levemente diferente.
          final amt = effect.paramAt('quantidade', local).clamp(0.0, 1.0);
          if (amt > 0.01) {
            final spread = amt * 0.06;
            Widget scaled(double s, int ch) => Transform.scale(
              scale: s,
              alignment: Alignment.center,
              child: _channelIso(out, ch),
            );
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                scaled(1 - spread, 0),
                BlendMask(
                  blendMode: BlendMode.plus,
                  margem: fxSize.longestSide * spread + 4,
                  child: scaled(1.0, 1),
                ),
                BlendMask(
                  blendMode: BlendMode.plus,
                  margem: fxSize.longestSide * spread + 4,
                  child: scaled(1 + spread, 2),
                ),
              ],
            );
          }

        // ------------------- catalogo, lote 1 -------------------

        case EffectType.levels:
          // Entrada -> gama -> saida, por canal, em matriz.
          final inMin = effect.paramAt('entradaMin', local);
          final inMax = effect.paramAt('entradaMax', local);
          final gamma = effect.paramAt('gama', local);
          final outMin = effect.paramAt('saidaMin', local);
          final outMax = effect.paramAt('saidaMax', local);
          final span = (inMax - inMin).abs() < 1e-4 ? 1e-4 : inMax - inMin;
          final scale = (outMax - outMin) / span;
          final shift = outMin - inMin * scale;
          // CANAL (avancado): a mesma curva num canal so.
          final canal = effect.paramAt('canal', local).round().clamp(0, 3);
          if ((scale - 1).abs() > 1e-4 || shift.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix(scale, shift, canal: canal),
              ),
              child: out,
            );
          }
          // Gama por aproximacao: uma segunda passada de ganho.
          if ((gamma - 1).abs() > 0.01) {
            final g = 1 / gamma;
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix(g, (1 - g) * 0.18, canal: canal),
              ),
              child: out,
            );
          }

        case EffectType.corrections:
          // CORRECOES (o "basico" do Lumetri): exposicao e contraste numa
          // matriz, sombras/altas como pe e topo da curva, temperatura e
          // verde/magenta como ganho por canal, saturacao e gama.
          final ev = effect.paramAt('exposicao', local);
          final ctr = effect.paramAt('contraste', local).clamp(-1.0, 1.0);
          final altas = effect.paramAt('altas', local).clamp(-1.0, 1.0);
          final sombras = effect.paramAt('sombras', local).clamp(-1.0, 1.0);
          final temp = effect.paramAt('temperatura', local).clamp(-1.0, 1.0);
          final verdeMag = effect.paramAt('matiz', local).clamp(-1.0, 1.0);
          final satC = effect.paramAt('saturacao', local).clamp(-1.0, 1.0);
          final gamaC = effect.paramAt('gama', local).clamp(0.3, 3.0);
          final ganho = math.pow(2.0, ev).toDouble();
          final cC = 1 + ctr * 0.9;
          final escala = ganho * cC;
          final desloc = (1 - cC) * 0.5 * ganho;
          if ((escala - 1).abs() > 1e-4 || desloc.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix(escala, desloc),
              ),
              child: out,
            );
          }
          // Sombras levantam o preto (branco fica); altas esticam ou
          // comprimem o topo (preto fica).
          final pe = sombras * 0.22;
          final topo = altas * 0.22;
          if (pe.abs() > 1e-4 || topo.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix((1 - pe) * (1 + topo), pe),
              ),
              child: out,
            );
          }
          if (temp.abs() > 1e-4 || verdeMag.abs() > 1e-4) {
            final rG = (1 + temp * 0.18) * (1 + verdeMag * 0.05);
            final gG = 1 - verdeMag * 0.14;
            final bG = (1 - temp * 0.18) * (1 + verdeMag * 0.05);
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(<double>[
                rG, 0, 0, 0, 0, //
                0, gG, 0, 0, 0,
                0, 0, bG, 0, 0,
                0, 0, 0, 1, 0,
              ]),
              child: out,
            );
          }
          if (satC.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(_saturationMatrix(1 + satC)),
              child: out,
            );
          }
          if ((gamaC - 1).abs() > 0.01) {
            final g = 1 / gamaC;
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix(g, (1 - g) * 0.18),
              ),
              child: out,
            );
          }

        case EffectType.curves:
          final contrast = effect.paramAt('contraste', local);
          final bright = effect.paramAt('brilho', local);
          final lift = effect.paramAt('sombras', local);
          final pull = effect.paramAt('altas', local);
          final c = 1 + contrast;
          final b = bright * 0.5 + lift * 0.25 - pull * 0.25;
          if ((c - 1).abs() > 1e-4 || b.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix(c, b + (1 - c) * 0.5),
              ),
              child: out,
            );
          }

        case EffectType.vibrance:
          final vib = effect.paramAt('vibracao', local);
          final sat = effect.paramAt('saturacao', local);
          final skin = effect.paramAt('protecaoPele', local).clamp(0.0, 1.0);
          // Vibracao sobe mais o que esta POUCO saturado; a protecao de
          // pele segura o ganho no canal vermelho, que e onde o tom de
          // pele vive — sem isso o rosto fica laranja.
          final amount = sat + vib * 0.6 * (1 - skin * 0.7);
          if (amount.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _saturationMatrix(1 + amount, redGuard: skin * vib),
              ),
              child: out,
            );
          }

        case EffectType.whiteBalance:
          final temp = effect.paramAt('temperatura', local);
          final tintV = effect.paramAt('matiz', local);
          if (temp.abs() > 1e-4 || tintV.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(<double>[
                1 + temp * 0.3,
                0,
                0,
                0,
                0,
                0,
                1 + tintV * 0.2,
                0,
                0,
                0,
                0,
                0,
                1 - temp * 0.3,
                0,
                0,
                0,
                0,
                0,
                1,
                0,
              ]),
              child: out,
            );
          }

        case EffectType.colorWheels:
          // Sombras = deslocamento (lift); altas = ganho (gain).
          final sr = effect.paramAt('sombrasR', local);
          final sg = effect.paramAt('sombrasG', local);
          final sb = effect.paramAt('sombrasB', local);
          final hr = effect.paramAt('altasR', local);
          final hg = effect.paramAt('altasG', local);
          final hb = effect.paramAt('altasB', local);
          if ([sr, sg, sb, hr, hg, hb].any((v) => v.abs() > 1e-4)) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(<double>[
                1 + hr,
                0,
                0,
                0,
                sr * 255,
                0,
                1 + hg,
                0,
                0,
                sg * 255,
                0,
                0,
                1 + hb,
                0,
                sb * 255,
                0,
                0,
                0,
                1,
                0,
              ]),
              child: out,
            );
          }

        case EffectType.unmult:
          // O preto vira TRANSPARENTE: a luminancia entra no alfa. E o
          // que faz overlay de fogo/fumaca/faisca funcionar direto.
          final soft = effect.paramAt('suavidade', local).clamp(0.0, 1.0);
          final k = 0.7 + soft * 0.6;
          // O LIMIAR EXISTIA NA TELA E NAO EXISTIA NA CONTA.
          //
          // O controle estava exposto, com nome e faixa, e o codigo nunca
          // o lia: mexer nele nao mudava um pixel. Agora ele entra como
          // deslocamento constante da linha de alfa — que e como um
          // limiar se escreve numa matriz de cor, onde nao cabe
          // comparacao. Cinza abaixo do limiar vai a zero; acima, sobra
          // o que passou dele.
          final limiar = effect.paramAt('limiar', local).clamp(0.0, 1.0);
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(<double>[
              1,
              0,
              0,
              0,
              0,
              0,
              1,
              0,
              0,
              0,
              0,
              0,
              1,
              0,
              0,
              0.2126 * k,
              0.7152 * k,
              0.0722 * k,
              0,
              -limiar * 255,
            ]),
            child: out,
          );

        case EffectType.vignette:
          final amt = effect.paramAt('quantidade', local).clamp(0.0, 1.0);
          if (amt > 0.01) {
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                Positioned.fill(
                  child: IgnorePointer(
                    child: CustomPaint(
                      painter: VignettePainter(
                        amount: amt,
                        radius: effect.paramAt('raio', local),
                        softness: effect
                            .paramAt('suavidade', local)
                            .clamp(0.0, 1.0),
                        color: effect.color,
                        retangular: effect.paramAt('forma', local) > 0.5,
                        center: Offset(
                          effect.paramAt('centroX', local).clamp(-1.0, 2.0),
                          effect.paramAt('centroY', local).clamp(-1.0, 2.0),
                        ),
                      ),
                    ),
                  ),
                ),
              ],
            );
          }

        case EffectType.directionalBlur:
          final len = effect.paramAt('comprimento', local);
          if (len > 0.5) {
            final ang = effect.paramAt('angulo', local) * math.pi / 180;
            // Blur anisotropico girado: sigma no eixo do movimento.
            out = Transform.rotate(
              angle: -ang,
              child: LinearLight.blurred(
                // Tambem em linear: e desfoque, e desfoque em sRGB
                // escurece a media entre claro e escuro — a franja
                // suja na borda do movimento vem daí.
                sigmaX: len / 3,
                sigmaY: 0.01,
                size: fxSize,
                child: Transform.rotate(angle: ang, child: out),
              ),
            );
          }

        case EffectType.radialBlur:
          final amt = effect.paramAt('quantidade', local).clamp(0.0, 1.0);
          if (amt > 0.01) {
            final zoom = effect.paramAt('modo', local) < 0.5;
            final n = effect.paramAt('amostras', local).round().clamp(2, 16);
            final layers = <Widget>[];
            for (var i = 0; i < n; i++) {
              final f = i / (n - 1);
              final o = 1.0 / n;
              layers.add(
                Opacity(
                  opacity: o * 1.6,
                  child: zoom
                      ? Transform.scale(scale: 1 + amt * 0.25 * f, child: out)
                      : Transform.rotate(angle: amt * 0.4 * f, child: out),
                ),
              );
            }
            out = Stack(clipBehavior: Clip.none, children: layers);
          }

        case EffectType.lightRays:
          final len = effect.paramAt('comprimento', local);
          if (len > 0.01) {
            final n = effect.paramAt('amostras', local).round().clamp(2, 20);
            final gain = effect.paramAt('intensidade', local);
            final cx = effect.paramAt('centroX', local);
            final cy = effect.paramAt('centroY', local);
            final origin = Alignment(cx * 2 - 1, cy * 2 - 1);
            final rays = <Widget>[];
            for (var i = 1; i <= n; i++) {
              final s = 1 + len * 0.6 * i / n;
              rays.add(
                Opacity(
                  opacity: (gain / n).clamp(0.0, 1.0),
                  child: Transform.scale(
                    scale: s,
                    alignment: origin,
                    child: ColorFiltered(
                      colorFilter: ColorFilter.mode(
                        effect.color,
                        BlendMode.srcATop,
                      ),
                      child: out,
                    ),
                  ),
                ),
              );
            }
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                BlendMask(
                  blendMode: BlendMode.plus,
                  child: Stack(clipBehavior: Clip.none, children: rays),
                ),
                out,
              ],
            );
          }

        case EffectType.mosaic:
          final blocks = effect.paramAt('blocos', local).clamp(3.0, 160.0);
          // Reduz e amplia SEM interpolacao: e o pixelate de verdade.
          out = ImageFiltered(
            imageFilter: ui.ImageFilter.compose(
              outer: ui.ImageFilter.matrix(
                Matrix4.diagonal3Values(blocks / 3, blocks / 3, 1).storage,
                filterQuality: FilterQuality.none,
              ),
              inner: ui.ImageFilter.matrix(
                Matrix4.diagonal3Values(3 / blocks, 3 / blocks, 1).storage,
                filterQuality: FilterQuality.none,
              ),
            ),
            child: out,
          );

        case EffectType.posterize:
          final levels = effect.paramAt('niveis', local).round().clamp(2, 32);
          // Aproximacao por quantizacao de contraste em degraus.
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(
              _posterizeMatrix(levels.toDouble()),
            ),
            child: out,
          );

        case EffectType.filmGrain:
          final amt = effect.paramAt('intensidade', local).clamp(0.0, 1.0);
          if (amt > 0.01) {
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                Positioned.fill(
                  child: IgnorePointer(
                    child: BlendMask(
                      blendMode: BlendMode.overlay,
                      child: CustomPaint(
                        painter: _GrainPainter(
                          amount: amt,
                          size: effect.paramAt('tamanho', local),
                          seed: effect.paramAt('semente', local).round(),
                          time: local,
                        ),
                      ),
                    ),
                  ),
                ),
              ],
            );
          }

        case EffectType.fractalNoise:
          final op = effect.paramAt('opacidade', local).clamp(0.0, 1.0);
          if (op > 0.01) {
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                Positioned.fill(
                  child: IgnorePointer(
                    child: Opacity(
                      opacity: op,
                      child: BlendMask(
                        blendMode: BlendMode.screen,
                        child: CustomPaint(
                          painter: _FractalNoisePainter(
                            scale: effect.paramAt('escala', local),
                            octaves: effect
                                .paramAt('complexidade', local)
                                .round(),
                            contrast: effect.paramAt('contraste', local),
                            evolution: effect.paramAt('evolucao', local),
                            seed: effect.paramAt('semente', local).round(),
                            color: effect.color,
                            time: local,
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
              ],
            );
          }

        case EffectType.digitalDamage:
          final n = effect.paramAt('blocos', local).round().clamp(1, 24);
          final interval = effect.paramAt('intervalo', local);
          final seed = effect.paramAt('semente', local).round();
          final tick = interval <= 0
              ? 0
              : (local.inMicroseconds / 1e6 / interval).floor();
          final shift = effect.paramAt('deslocamento', local);
          final colorAmt = effect.paramAt('cor', local);
          final h = effect.paramAt('altura', local);
          final slices = <Widget>[];
          for (var i = 0; i < n; i++) {
            final r = fxHash01(seed, tick, i * 7 + 3);
            final r2 = fxHash01(seed, tick, i * 7 + 11);
            if (r > 0.55) continue;
            final top = r2.clamp(0.0, 1 - h);
            slices.add(
              Positioned.fill(
                child: ClipRect(
                  clipper: _BandClipper(top, h),
                  child: Transform.translate(
                    offset: Offset((r - 0.275) * 4 * shift * 200, 0),
                    child: colorAmt > 0.05 ? _channelIso(out, (i % 3)) : out,
                  ),
                ),
              ),
            );
          }
          if (slices.isNotEmpty) {
            out = Stack(clipBehavior: Clip.none, children: [out, ...slices]);
          }

        case EffectType.zoomWarp:
          final amt = effect.paramAt('quantidade', local);
          if (amt.abs() > 0.005) {
            final trail = effect.paramAt('rastro', local).clamp(0.0, 1.0);
            final n = effect.paramAt('amostras', local).round().clamp(2, 12);
            if (trail < 0.02) {
              out = Transform.scale(scale: 1 + amt, child: out);
            } else {
              final layers = <Widget>[];
              for (var i = 0; i < n; i++) {
                final f = i / (n - 1);
                layers.add(
                  Opacity(
                    opacity: 1.0 / n * 1.8,
                    child: Transform.scale(
                      scale: 1 + amt * (1 - trail * f),
                      child: out,
                    ),
                  ),
                );
              }
              out = Stack(clipBehavior: Clip.none, children: layers);
            }
          }

        // O remapeamento de tempo nao pinta nada: ele ja mudou QUAL
        // instante da camada foi montado, la em cima.
        case EffectType.timeRemap:
        // FORCE MOTION BLUR nao acontece aqui: ele precisa re-renderizar
        // a camada em outros instantes, e a pilha de efeitos so recebe o
        // widget ja pronto. Quem o aplica e o compositor.
        case EffectType.forceMotionBlur:
          break;

        case EffectType.turbulentDisplace:
          final amt = effect.paramAt('quantidade', local);
          if (amt.abs() > 0.5) {
            out = FxSnapshot(
              painter: TurbulentDisplacePainter(
                amount: amt,
                scale: effect.paramAt('tamanho', local),
                complexity: effect.paramAt('complexidade', local),
                evolution: effect.paramAt('evolucao', local),
                seed: effect.paramAt('semente', local).round(),
              ),
              child: out,
            );
          }

        case EffectType.bend:
          final amt = effect.paramAt('quantidade', local);
          if (amt.abs() > 0.5) {
            out = FxSnapshot(
              painter: BendPainter(
                amount: amt,
                vertical: effect.paramAt('eixo', local).round() == 1,
                curvature: effect.paramAt('curvatura', local),
                anchor: effect.paramAt('ancora', local).clamp(0.0, 1.0),
              ),
              child: out,
            );
          }

        case EffectType.pixelSort:
          // BLEND WITH ORIGINAL em 1 e o "desligado" da ficha: a saida
          // tem de ser identica a entrada.
          final mistura = effect.paramAt('blend_with_original', local);
          final compr = effect.paramAt('radius_length', local);
          if (compr > 0.001 && mistura < 0.999) {
            out = FxSnapshot(
              painter: PixelSortPainter(
                // A instancia do efeito e a chave do cache do resultado.
                cacheKey: effect.id,
                mode: effect.paramAt('mode', local).round().clamp(0, 2),
                sortAngle: effect.paramAt('sort_angle', local),
                threshold: effect.paramAt('threshold', local),
                aboveThreshold: effect.paramAt('direction', local) >= 0.5,
                reverse: effect.paramAt('reverse_sort', local) >= 0.5,
                sortBy: effect.paramAt('sort_by', local).round().clamp(0, 2),
                length: compr,
                randomRestart: effect.paramAt('random_restart', local),
                seed: effect.paramAt('seed', local).round(),
                sortResolution: effect.paramAt('sort_resolution', local),
                downsample: effect.paramAt('downsample', local),
                matteBlur: effect.paramAt('blur_threshold_matte', local),
                blendWithOriginal: mistura,
                show: effect.paramAt('show', local).round().clamp(0, 3),
                softEdges: effect.paramAt('soft_edges', local) >= 0.5,
                centerX: effect.paramAt('center_x', local),
                centerY: effect.paramAt('center_y', local),
                startAngle: effect.paramAt('start_angle', local),
                degreesSorted: effect.paramAt('degrees_sorted', local),
                innerRadius: effect.paramAt('inner_radius', local),
                radiusVariation: effect.paramAt('radius_variation', local),
                startVariation: effect.paramAt('start_variation', local),
                thickness: effect.paramAt('thickness', local),
              ),
              child: out,
            );
          }

        case EffectType.ccScatterize:
          final sp = effect.paramAt('dispersao', local);
          if (sp > 0.5) {
            out = FxSnapshot(
              painter: ScatterizePainter(
                spread: sp,
                grain: effect.paramAt('grao', local),
                rotation: effect.paramAt('rotacao', local),
                transfer: effect
                    .paramAt('transferencia', local)
                    .clamp(0.0, 1.0),
                gravity: effect.paramAt('gravidade', local),
                seed: effect.paramAt('semente', local).round(),
              ),
              child: out,
            );
          }

        case EffectType.motionTile:
          final ladoW = effect.paramAt('tile_width', local);
          final ladoH = effect.paramAt('tile_height', local);
          final saidaW = effect.paramAt('output_width', local);
          final saidaH = effect.paramAt('output_height', local);
          final fase = effect.paramAt('phase', local);
          final espelha = effect.paramAt('mirror_edges', local) >= 0.5;
          // IDENTIDADE do After Effects: ladrilho de 100% num quadro de
          // 100%, sem fase e sem espelho, e a propria camada. Passar por
          // aqui assim mesmo custava uma FOTO da camada inteira por
          // quadro — e foto e justamente o que congelava a camada.
          final identidade =
              (ladoW - 100).abs() < 0.01 &&
              (ladoH - 100).abs() < 0.01 &&
              (saidaW - 100).abs() < 0.01 &&
              (saidaH - 100).abs() < 0.01 &&
              fase.abs() < 0.01 &&
              !espelha;
          if (!identidade) {
            out = FxSnapshot(
              painter: MotionTilePainter(
                tileW: ladoW,
                tileH: ladoH,
                outW: saidaW,
                outH: saidaH,
                centerX: effect.paramAt('tile_center', local),
                centerY: effect.paramAt('tile_center_y', local),
                mirror: espelha,
                phase: fase,
                horizontalPhase:
                    effect.paramAt('horizontal_phase_shift', local) >= 0.5,
              ),
              child: out,
            );
          }

        case EffectType.ccSplit:
          final sp = effect.paramAt('divisao', local);
          if (sp.abs() > 0.5) {
            out = FxSnapshot(
              painter: SplitPainter(
                split: sp,
                angleDeg: effect.paramAt('angulo', local),
                center: effect.paramAt('centro', local).clamp(0.0, 1.0),
                softness: effect.paramAt('suavidade', local).clamp(0.0, 1.0),
              ),
              child: out,
            );
          }

        case EffectType.unsharpMask:
          final amt = effect.paramAt('quantidade', local);
          if (amt > 0.01) {
            out = FxSnapshot(
              painter: UnsharpMaskPainter(
                amount: amt,
                radius: effect.paramAt('raio', local).clamp(0.5, 40.0),
                threshold: effect.paramAt('limiar', local).clamp(0.0, 0.95),
              ),
              child: out,
            );
          }

        case EffectType.glitchify:
          final amt = effect.paramAt('intensidade', local);
          if (amt > 0.01) {
            out = FxSnapshot(
              painter: GlitchifyPainter(
                intensity: amt,
                blocks: effect.paramAt('blocos', local),
                shift: effect.paramAt('deslocamento', local),
                colorSplit: effect.paramAt('cor', local),
                lineNoise: effect.paramAt('ruidoLinha', local),
                speed: effect.paramAt('velocidade', local),
                time: local,
                seed: effect.paramAt('semente', local).round(),
              ),
              child: out,
            );
          }

        case EffectType.vhs:
          final amt = effect.paramAt('intensidade', local).clamp(0.0, 1.0);
          if (amt > 0.01) {
            final bleed = effect.paramAt('sangramento', local);
            final jitter = effect.paramAt('tremor', local);
            // Tremor horizontal por linha: e o que denuncia a fita.
            final shake = jitter <= 0.01
                ? 0.0
                : (fxNoise(
                            (local.inMilliseconds / 40).floorToDouble(),
                            0,
                            effect.paramAt('semente', local).round(),
                          ) -
                          0.5) *
                      2 *
                      jitter *
                      14;
            var body = out;
            if (bleed > 0.02) {
              body = Stack(
                clipBehavior: Clip.none,
                children: [
                  Transform.translate(
                    offset: Offset(-bleed * 6, 0),
                    child: _channelIso(body, 0),
                  ),
                  Transform.translate(
                    offset: Offset(bleed * 6, 0),
                    child: _channelIso(body, 2),
                  ),
                  body,
                ],
              );
            }
            final fade = effect.paramAt('desbotar', local).clamp(0.0, 1.0);
            if (fade > 0.02) {
              body = ColorFiltered(
                colorFilter: ColorFilter.matrix(
                  _saturationMatrix(1 - fade * 0.55),
                ),
                child: body,
              );
            }
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                Transform.translate(offset: Offset(shake, 0), child: body),
                Positioned.fill(
                  child: IgnorePointer(
                    child: CustomPaint(
                      painter: VhsPainter(
                        intensity: amt,
                        lines: effect.paramAt('linhas', local),
                        noise: effect.paramAt('ruido', local),
                        time: local,
                        seed: effect.paramAt('semente', local).round(),
                      ),
                    ),
                  ),
                ),
              ],
            );
          }

        case EffectType.filmDamage:
          final flick = effect.paramAt('cintilacao', local).clamp(0.0, 1.0);
          final jump = effect.paramAt('salto', local).clamp(0.0, 1.0);
          final seed = effect.paramAt('semente', local).round();
          final frame = (local.inMilliseconds / 1000.0 * 16).floor();
          // Cintilacao e salto de quadro andam no relogio do projetor.
          final lum = flick <= 0.01
              ? 1.0
              : 1 + (fxNoise(frame.toDouble(), 0, seed) - 0.5) * flick * 0.4;
          final dy = jump <= 0.01
              ? 0.0
              : (fxNoise(frame.toDouble(), 1, seed + 5) - 0.5) * jump * 10;
          var body = out;
          if ((lum - 1).abs() > 0.005) {
            body = ColorFiltered(
              colorFilter: ColorFilter.matrix(_scaleShiftMatrix(lum, 0)),
              child: body,
            );
          }
          out = Stack(
            clipBehavior: Clip.none,
            children: [
              Transform.translate(offset: Offset(0, dy), child: body),
              Positioned.fill(
                child: IgnorePointer(
                  child: CustomPaint(
                    painter: FilmDamagePainter(
                      dust: effect.paramAt('poeira', local),
                      scratches: effect.paramAt('riscos', local),
                      burn: effect.paramAt('queimado', local),
                      time: local,
                      seed: seed,
                    ),
                  ),
                ),
              ),
              if (effect.paramAt('granulacao', local) > 0.01)
                Positioned.fill(
                  child: IgnorePointer(
                    child: BlendMask(
                      blendMode: BlendMode.overlay,
                      child: CustomPaint(
                        painter: _GrainPainter(
                          amount: effect.paramAt('granulacao', local),
                          size: 1,
                          seed: seed,
                          time: local,
                        ),
                      ),
                    ),
                  ),
                ),
            ],
          );

        case EffectType.blobTracker:
          // As caixas vem da ANALISE ja gravada. Sem analise, o pintor
          // simula — para a pessoa ajustar a aparencia antes de gastar
          // o processamento.
          // Opacidade zero e o desligado do rastreio: sem isto ele
          // desenhava tudo para pintar com alfa zero em cima.
          if (effect.paramAt('opacity', local) <= 0.4) break;
          final rastreio = BlobTrackService.instance.dataFor(effect.id);
          final sobreposicao = Positioned.fill(
            child: IgnorePointer(
              child: CustomPaint(
                painter: BlobTrackerPainter(
                  track: rastreio,
                  time: local,
                  color: effect.color,
                  style: effect.paramAt('style', local).round().clamp(0, 4),
                  showCenter:
                      effect.paramAt('show_center_marker', local) >= 0.5,
                  showLines:
                      effect.paramAt('show_connecting_lines', local) >= 0.5,
                  lineType: effect
                      .paramAt('line_type', local)
                      .round()
                      .clamp(0, 2),
                  lineStyle: effect
                      .paramAt('line_style', local)
                      .round()
                      .clamp(0, 2),
                  palette: effect.paramAt('palette', local).round().clamp(0, 2),
                  thickness: effect.paramAt('thickness', local),
                  opacity: effect.paramAt('opacity', local),
                  fill: effect.paramAt('fill', local),
                  cornerLength: effect.paramAt('corner_length', local),
                  showCaption: effect.paramAt('show_caption', local) >= 0.5,
                  captionContent: effect
                      .paramAt('caption_content', local)
                      .round()
                      .clamp(0, 2),
                  captionPosition: effect
                      .paramAt('caption_position', local)
                      .round()
                      .clamp(0, 3),
                  fontSize: effect.paramAt('font_size', local),
                  seed: effect.paramAt('seed', local).round(),
                  simulatedCount: effect
                      .paramAt('max_blobs', local)
                      .round()
                      .clamp(1, 16),
                ),
              ),
            ),
          );

          // OVERLAY ONLY: so as sobreposicoes, fundo transparente —
          // serve para levar o rastreio para outra camada.
          final soSobreposicao = effect.paramAt('overlay_only', local) >= 0.5;
          final modoBlob = effect
              .paramAt('blend_mode', local)
              .round()
              .clamp(0, 2);
          final camadaBlob = switch (modoBlob) {
            1 => BlendMask(blendMode: BlendMode.plus, child: sobreposicao),
            2 => BlendMask(blendMode: BlendMode.screen, child: sobreposicao),
            _ => sobreposicao,
          };

          out = soSobreposicao
              ? Stack(clipBehavior: Clip.none, children: [camadaBlob])
              : Stack(clipBehavior: Clip.none, children: [out, camadaBlob]);
      }
    }
    return out;
  }

  /// Matriz de ganho+deslocamento igual nos tres canais.
  /// Escala e desloca. Com [canal] 1..3 mexe so em R, G ou B — e o
  /// "por canal" do Levels avancado.
  static List<double> _scaleShiftMatrix(
    double s,
    double shift, {
    int canal = 0,
  }) {
    final b = shift * 255;
    if (canal != 0) {
      final r = canal == 1, g = canal == 2, bl = canal == 3;
      return <double>[
        r ? s : 1, 0, 0, 0, r ? b : 0, //
        0, g ? s : 1, 0, 0, g ? b : 0,
        0, 0, bl ? s : 1, 0, bl ? b : 0,
        0, 0, 0, 1, 0,
      ];
    }
    return <double>[s, 0, 0, 0, b, 0, s, 0, 0, b, 0, 0, s, 0, b, 0, 0, 0, 1, 0];
  }

  /// Saturacao com guarda no vermelho (protecao de tom de pele).
  static List<double> _saturationMatrix(double sat, {double redGuard = 0}) {
    const lr = 0.2126, lg = 0.7152, lb = 0.0722;
    final s = sat;
    final rs = s - (s - 1) * redGuard.clamp(0.0, 1.0);
    return <double>[
      lr * (1 - rs) + rs,
      lg * (1 - rs),
      lb * (1 - rs),
      0,
      0,
      lr * (1 - s),
      lg * (1 - s) + s,
      lb * (1 - s),
      0,
      0,
      lr * (1 - s),
      lg * (1 - s),
      lb * (1 - s) + s,
      0,
      0,
      0,
      0,
      0,
      1,
      0,
    ];
  }

  /// Saturacao seguida de ganho de brilho, numa unica matriz. Quando
  /// ambos valem 1 a matriz e identidade; o alfa nunca e alterado.
  static List<double> _saturationBrightnessMatrix(
    double saturation,
    double brightness,
  ) {
    final m = _saturationMatrix(saturation);
    return <double>[
      m[0] * brightness,
      m[1] * brightness,
      m[2] * brightness,
      0,
      0,
      m[5] * brightness,
      m[6] * brightness,
      m[7] * brightness,
      0,
      0,
      m[10] * brightness,
      m[11] * brightness,
      m[12] * brightness,
      0,
      0,
      0,
      0,
      0,
      1,
      0,
    ];
  }

  /// Aproximacao de posterizacao: contraste alto centrado, que agrupa os
  /// tons em patamares visiveis.
  static List<double> _posterizeMatrix(double levels) {
    final c = 1 + (32 - levels) / 12;
    final b = (1 - c) * 0.5 * 255;
    return <double>[c, 0, 0, 0, b, 0, c, 0, 0, b, 0, 0, c, 0, b, 0, 0, 0, 1, 0];
  }

  /// Isola um canal (0=R, 1=G, 2=B) preservando o alfa — base da franja
  /// cromatica, da separacao RGB e da aberracao do glow.
  Widget _channelIso(Widget child, int channel) {
    const zeros = [0.0, 0.0, 0.0, 0.0, 0.0];
    final rows = [
      channel == 0 ? const [1.0, 0.0, 0.0, 0.0, 0.0] : zeros,
      channel == 1 ? const [0.0, 1.0, 0.0, 0.0, 0.0] : zeros,
      channel == 2 ? const [0.0, 0.0, 1.0, 0.0, 0.0] : zeros,
      const [0.0, 0.0, 0.0, 1.0, 0.0],
    ];
    return ColorFiltered(
      colorFilter: ColorFilter.matrix([for (final r in rows) ...r]),
      child: child,
    );
  }
}

/// A camera de uma Cena 3D com o nulo da COMPOSICAO ja aplicado.
///
/// A ponte entre as duas hierarquias: a arvore de camadas da composicao
/// e o grafo interno da cena. Quem conhece a cadeia de parenting de fora
/// e o compositor, entao o transform do nulo chega pronto aqui.
///
/// Devolve null quando nao ha nada a resolver — assim o pintor segue
/// pelo caminho barato de sempre.
RenderCamera? cameraDaCena(
  VideoProject project,
  Scene3DLayer l,
  Duration local,
  Duration global,
) {
  final paiId = l.cameraParentLayerId;
  if (paiId == null) {
    return l.shots.isEmpty && l.scene.cameraParentId == null
        ? null
        : l.cameraAt(local);
  }
  final pai = project.layerById(paiId);
  if (pai == null) return l.cameraAt(local);

  // O transform EFETIVO do nulo (com a cadeia dele ja resolvida).
  final eff = effectiveTransform(project, pai, global);
  final centro = Offset(project.outputWidth / 2, project.outputHeight / 2);

  // Posicao da composicao (canto superior esquerdo) para a cena (origem
  // no centro). A ESCALA nao entra: camera nao tem escala, e herdar e o
  // bug que faz o enquadramento explodir.
  return l.cameraAt(
    local,
    external: NodeTransform(
      position: Vec3(eff.pos.dx - centro.dx, eff.pos.dy - centro.dy, eff.z),
      rotX: eff.rotX,
      rotY: eff.rotY,
      rotZ: eff.rot,
    ),
  );
}

class _LayerContent extends StatelessWidget {
  const _LayerContent({
    this.exportFrames,
    required this.exporting,
    required this.layer,
    required this.project,
    required this.compWidth,
    required this.videos,
    required this.localTime,
    required this.buildChildren,
    this.particlesRotX = 0,
    this.particlesRotY = 0,
  });

  final Layer layer;
  final VideoProject project;
  final double compWidth;
  final VideoLayerManager videos;
  final Duration localTime;

  /// Rotacao 3D do sistema de particulas (graus), ja com o delta do pai.
  final double particlesRotX;
  final double particlesRotY;

  /// Quadro ja decodificado por camada de video (so na exportacao).
  final Map<String, ui.Image>? exportFrames;
  final bool exporting;

  /// Recursao do precomp: constroi as camadas filhas no tempo local.
  final List<Widget> Function(List<Layer> layers, Duration t) buildChildren;

  /// Caminho da camada de forma [id], ja avaliado no tempo — para o
  /// texto que segue uma forma desenhada no proprio projeto.
  static ui.Path? _pathOfShapeLayer(
    VideoProject project,
    String? id,
    Duration t,
  ) {
    if (id == null) return null;
    final l = project.layerById(id);
    if (l is! ShapeLayer) return null;
    final draws = evaluateShape(l.contents, l.localTime(t));
    if (draws.isEmpty) return null;
    final out = ui.Path();
    for (final d in draws) {
      out.addPath(d.path, Offset.zero);
    }
    return out;
  }

  @override
  Widget build(BuildContext context) {
    Widget child = switch (layer) {
      // Caminho rapido sem animador ativo (I2: linha inteira, com kerning).
      TextLayer l when l.hasTextAnimation => AnimatedTextView(
        layer: l,
        localTime: localTime,
        // Texto seguindo OUTRA camada: o widget nao sabe resolver id,
        // entao o caminho chega pronto de quem monta a composicao.
        pathOverride: _pathOfShapeLayer(
          project,
          l.textPath.shapeLayerId,
          localTime,
        ),
      ),
      TextLayer l => Text(
        l.text,
        textAlign: TextAlign.center,
        style: AnimatedTextView.styleFor(l),
      ),
      // Forma vetorial: arvore avaliada no tempo local, pintada por Path.
      ShapeLayer l => _ShapeView(layer: l, localTime: localTime),
      // CONTEINER CENA 3D: por fora e uma camada; por dentro roda o
      // proprio renderizador, com passe opaco e passe transparente
      // ordenados POR TRIANGULO.
      Scene3DLayer l => SizedBox(
        width: compWidth,
        height: project.outputHeight.toDouble(),
        child: ValueListenableBuilder<int>(
          valueListenable: TextureCache.instance.revision,
          builder: (_, _, _) {
            // A PONTE ENTRE AS DUAS HIERARQUIAS: o nulo da composicao
            // vira pai externo da camera da cena. Quem conhece a
            // cadeia de parenting de fora e o compositor, entao o
            // transform chega pronto aqui.
            final resolvida = cameraDaCena(
              project,
              l,
              localTime,
              layer.startTime + localTime,
            );
            // Ajudas NUNCA entram na exportacao — so no preview.
            final ajudas = !exporting && l.showHelpers;
            // RASCUNHO ENQUANTO TOCA. Em 33 ms nao cabe reflexo no
            // chao, sombra de contato e profundidade de campo de uma
            // cena inteira; num celular a conta passa de 300 ms por
            // quadro, e uma thread bloqueada por 300 ms e o que faz o
            // iOS matar o app. Quem aperta o play quer ver o
            // MOVIMENTO — a qualidade cheia volta na pausa e na
            // exportacao, que e onde ela e olhada de perto.
            return ValueListenableBuilder<bool>(
              valueListenable: PlaybackController.tocandoAgora,
              builder: (_, tocando, _) {
                final rascunho = tocando && !exporting;
                // O MOTOR EM GPU desenha a cena quando existe; sem ele
                // (ou com as ajudas de cena ligadas, que so o pintor
                // sabe desenhar) fica o pintor em CPU de sempre.
                if ((filamentPreviewEnabled || !Scene3DGpu.indisponivel) &&
                    !ajudas) {
                  return Scene3DGpuView(
                    exporting: exporting,
                    scene: l.scene,
                    camera: l.camera,
                    renderCamera: l.view == SceneView.camera
                        ? (resolvida ?? l.camera.renderAt(localTime))
                        : orthoViewCamera(l.view),
                    view: l.view,
                    time: localTime,
                    rascunho: rascunho,
                  );
                }
                return CustomPaint(
                  painter: Scene3DPainter(
                    scene: rascunho
                        ? l.scene.copyWith(draftMode: true)
                        : l.scene,
                    camera: l.camera,
                    resolvedCamera: resolvida,
                    view: l.view,
                    time: localTime,
                    showHelpers: ajudas,
                  ),
                );
              },
            );
          },
        ),
      ),
      // Precomp: filhos compostos no tempo local do grupo.
      // PRECOMP: tempo proprio (com remapeamento), quadro proprio e a
      // opcao de colapsar — que e o que evita a forma vetorial pixelar
      // quando a precomp e ampliada.
      GroupLayer l => SizedBox(
        width: compWidth,
        height: project.outputHeight.toDouble(),
        child: ClipRect(
          clipBehavior: l.clipToComp && !l.collapse ? Clip.hardEdge : Clip.none,
          child: Stack(
            clipBehavior: Clip.none,
            children: buildChildren(l.children, l.contentTimeAt(localTime)),
          ),
        ),
      ),
      ImageLayer l => RepaintBoundary(
        child: Image.file(
          File(l.sourcePath),
          width: compWidth,
          fit: BoxFit.contain,
          errorBuilder: (_, _, _) => _brokenMedia(),
        ),
      ),
      // EXPORTANDO: o quadro vem decodificado do disco. A textura do
      // player nunca entra num `toImage`, entao o video sairia preto.
      VideoLayer l when exportFrames != null && exportFrames![l.id] != null =>
        SizedBox(
          width: compWidth,
          height:
              compWidth *
              exportFrames![l.id]!.height /
              exportFrames![l.id]!.width,
          child: RawImage(
            image: exportFrames![l.id],
            fit: BoxFit.contain,
            filterQuality: FilterQuality.medium,
          ),
        ),
      VideoLayer l => RepaintBoundary(
        child: ValueListenableBuilder<int>(
          valueListenable: videos.revision,
          builder: (context, _, _) {
            final controller = videos.controllerFor(l.id);
            if (controller == null || !controller.value.isInitialized) {
              return SizedBox(
                width: compWidth,
                height: compWidth * 9 / 16,
                child: const Center(
                  child: Icon(
                    CupertinoIcons.film,
                    size: 60,
                    color: Colors.white24,
                  ),
                ),
              );
            }
            return SizedBox(
              width: compWidth,
              height: compWidth / controller.value.aspectRatio,
              child: VideoPlayer(controller),
            );
          },
        ),
      ),
      AudioLayer _ => const SizedBox.shrink(),
      // Objeto nulo: wireframe so no editor (nao sai na exportacao).
      NullLayer _ => const IgnorePointer(
        child: CustomPaint(size: Size(220, 220), painter: NullGizmoPainter()),
      ),
      // Ajuste nao tem conteudo proprio: age no composto (interceptado
      // em _buildLayers); aqui rende so o gizmo de selecao.
      AdjustmentLayer _ => const SizedBox(width: 220, height: 220),
      // Particulas em espaco 3D: simulacao + projecao por particula.
      ParticlesLayer l => CustomPaint(
        size: const Size(420, 420),
        painter: ParticlesPainter(
          layer: l,
          time: localTime,
          rotXDeg: particlesRotX,
          rotYDeg: particlesRotY,
        ),
      ),
      // Elemento 3D: vertices girados no espaco dentro do pintor (como
      // as particulas) — nada de inclinar o canvas como um cartao.
      Element3DLayer l => ListenableBuilder(
        listenable: Listenable.merge([
          TextureCache.instance.revision,
          MeshCache.instance.revision,
        ]),
        builder: (_, _) => CustomPaint(
          size: const Size(620, 620),
          painter: World3DPainter(
            items: [
              World3DItem(
                layer: l,
                center: const Offset(310, 310),
                rotXDeg: particlesRotX,
                rotYDeg: particlesRotY,
                material: l.material,
                gradient: l.gradient,
                shininess: l.shininess,
              ),
            ],
          ),
        ),
      ),
      CaptionLayer l => Builder(
        builder: (context) {
          // ESTILO DESTAQUE: a frase inteira na tela, com a palavra dita
          // inflando no lugar dela. Precisa de tempo POR PALAVRA — sem
          // ele, cai para a legenda comum, que e falhar com dignidade.
          if (l.highlight.ativo && !l.highlight.isNeutro) {
            final frases = agruparEmFrases(l.cues);
            final frase = fraseEm(frases, localTime);
            if (frase != null) {
              return SizedBox(
                width: compWidth,
                height: project.outputHeight.toDouble(),
                child: CustomPaint(
                  painter: CaptionHighlightPainter(
                    frase: frase,
                    tempo: localTime,
                    estilo: l.highlight,
                    corpo: l.style.fontSize,
                  ),
                ),
              );
            }
            return const SizedBox.shrink();
          }
          final cue = l.cueAt(localTime);
          if (cue == null) return const SizedBox.shrink();
          return Container(
            constraints: BoxConstraints(maxWidth: compWidth * 0.86),
            padding: const EdgeInsets.symmetric(horizontal: 28, vertical: 14),
            decoration: BoxDecoration(
              color: l.style.backgroundColor.withValues(
                alpha: l.style.backgroundOpacity,
              ),
              borderRadius: BorderRadius.circular(14),
            ),
            child: Text(
              cue.text,
              textAlign: TextAlign.center,
              style: TextStyle(
                color: l.style.color,
                fontSize: l.style.fontSize,
                fontWeight: l.style.bold ? FontWeight.w700 : FontWeight.w400,
                height: 1.2,
              ),
            ),
          );
        },
      ),
    };

    return child;
  }

  Widget _brokenMedia() => Container(
    width: 400,
    height: 300,
    color: Colors.white10,
    child: const Icon(
      CupertinoIcons.exclamationmark_triangle,
      color: Colors.white38,
    ),
  );
}

/// Pinta a arvore da forma (vetorial: nitida em qualquer escala).
class _ShapeView extends StatelessWidget {
  const _ShapeView({required this.layer, required this.localTime});

  final ShapeLayer layer;
  final Duration localTime;

  @override
  Widget build(BuildContext context) {
    final draws = evaluateShape(layer.contents, localTime);
    final bounds = shapeBounds(draws);
    return CustomPaint(
      size: bounds.size,
      painter: _ShapePainter(draws: draws, bounds: bounds),
    );
  }
}

class _ShapePainter extends CustomPainter {
  const _ShapePainter({required this.draws, required this.bounds});

  final List<ShapeDraw> draws;
  final Rect bounds;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.translate(-bounds.left, -bounds.top);
    for (final d in draws) {
      canvas.drawPath(d.path, d.paint);
    }
  }

  @override
  bool shouldRepaint(_ShapePainter old) => true;
}
