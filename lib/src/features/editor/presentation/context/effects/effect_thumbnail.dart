import 'dart:io';
import 'dart:ui' as ui;
import 'package:aurea/src/core/theme/aurea_colors.dart';

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';

import '../../../application/editor_controller.dart';
import '../../../application/video_layer_manager.dart';
import '../../../domain/effect.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/shape.dart';
import '../../../domain/video_project.dart';
import '../../widgets/preview_stage.dart';

/// MINIATURAS DOS EFEITOS (Fase 4): cada efeito e renderizado UMA vez
/// pelo motor real sobre uma cartela padrao (degrade, disco saturado,
/// traco fino) e guardado — em memoria e em disco (PNG por versao).
///
/// A primeira vez que a galeria abre, cada tile visivel monta uma
/// composicao pequena de verdade (o mesmo `CompositionView` do preview)
/// e a captura no quadro seguinte; dali em diante e uma imagem.
class EffectThumbnailCache {
  EffectThumbnailCache._();

  static final EffectThumbnailCache instance = EffectThumbnailCache._();

  /// Sobe quando a cartela ou o motor mudam: invalida o disco.
  static const int versao = 1;

  /// Testes: nada de disco.
  static bool semDisco = false;

  final Map<String, ui.Image> _memoria = {};
  final Map<String, Future<ui.Image?>> _lendo = {};

  ui.Image? get(String effectId) => _memoria[effectId];

  Future<File?> _arquivo(String effectId) async {
    if (semDisco) return null;
    try {
      final base = await getApplicationSupportDirectory();
      final dir = Directory('${base.path}/efeitos-miniaturas');
      if (!dir.existsSync()) dir.createSync(recursive: true);
      return File('${dir.path}/$effectId-v$versao.png');
    } catch (_) {
      return null;
    }
  }

  /// Le do disco, se ja foi renderizada antes.
  Future<ui.Image?> carregar(String effectId) {
    final pronta = _memoria[effectId];
    if (pronta != null) return Future.value(pronta);
    return _lendo.putIfAbsent(effectId, () async {
      try {
        final f = await _arquivo(effectId);
        if (f == null || !f.existsSync()) return null;
        final codec = await ui.instantiateImageCodec(await f.readAsBytes());
        final frame = await codec.getNextFrame();
        _memoria[effectId] = frame.image;
        return frame.image;
      } catch (_) {
        return null;
      } finally {
        _lendo.remove(effectId);
      }
    });
  }

  Future<void> guardar(String effectId, ui.Image imagem) async {
    _memoria[effectId] = imagem;
    try {
      final f = await _arquivo(effectId);
      if (f == null) return;
      final bytes = await imagem.toByteData(format: ui.ImageByteFormat.png);
      if (bytes != null) await f.writeAsBytes(bytes.buffer.asUint8List());
    } catch (_) {}
  }

  @visibleForTesting
  void limparMemoria() => _memoria.clear();
}

/// A CARTELA: o que denuncia o efeito — degrade escuro→claro, um disco
/// saturado e um traco fino. O efeito vai nas duas camadas de cima.
VideoProject cartelaDoEfeito(EffectType type, {int lado = 240}) {
  final fx = EffectInstance(type: type);
  final d = const Duration(seconds: 2);
  final c = lado / 2;
  return VideoProject(
    name: 'miniatura',
    createdAt: DateTime(2026, 1, 1),
    aspectRatio: 1,
    resolutionHeight: lado,
    layers: [
      ShapeLayer(
        name: 'traco',
        startTime: Duration.zero,
        duration: d,
        contents: [
          ShapeParametric(
            kind: ParamShapeKind.rect,
            sizeX: AnimatedDouble(lado * 0.6),
            sizeY: AnimatedDouble(lado * 0.012),
          ),
          ShapeFill(color: const Color(0xFFFFFFFF)),
        ],
        position: AnimatedOffset(Offset(c, lado * 0.2)),
        effects: [fx],
      ),
      ShapeLayer(
        name: 'disco',
        startTime: Duration.zero,
        duration: d,
        contents: [
          ShapeParametric(
            kind: ParamShapeKind.ellipse,
            sizeX: AnimatedDouble(lado * 0.42),
            sizeY: AnimatedDouble(lado * 0.42),
          ),
          ShapeFill(color: const Color(0xFFFF4D2D)),
        ],
        position: AnimatedOffset(Offset(lado * 0.36, lado * 0.55)),
        effects: [fx],
      ),
      ShapeLayer(
        name: 'degrade',
        startTime: Duration.zero,
        duration: d,
        contents: [
          ShapeParametric(
            kind: ParamShapeKind.rect,
            sizeX: AnimatedDouble(lado.toDouble()),
            sizeY: AnimatedDouble(lado.toDouble()),
          ),
          ShapeGradientFill(
            colorA: AureaColors.bg,
            colorB: const Color(0xFF6A7BA8),
            angleDeg: 90,
          ),
        ],
        position: AnimatedOffset(Offset(c, c)),
      ),
    ],
  );
}

/// O TILE: a imagem guardada, ou a composicao viva ate a captura.
class EffectThumbnail extends StatefulWidget {
  const EffectThumbnail({super.key, required this.type, this.size = 84});

  final EffectType type;
  final double size;

  @override
  State<EffectThumbnail> createState() => _EffectThumbnailState();
}

class _EffectThumbnailState extends State<EffectThumbnail> {
  final _chave = GlobalKey();
  ui.Image? _imagem;
  ProviderContainer? _container;
  ValueNotifier<Duration>? _tempo;
  VideoLayerManager? _videos;
  bool _capturando = false;

  String get _id => effectSpecs[widget.type]!.id;

  @override
  void initState() {
    super.initState();
    _imagem = EffectThumbnailCache.instance.get(_id);
    if (_imagem == null) {
      EffectThumbnailCache.instance.carregar(_id).then((img) {
        if (!mounted) return;
        if (img != null) {
          setState(() => _imagem = img);
        } else {
          _montarViva();
        }
      });
    }
  }

  /// Monta a composicao de verdade num container proprio: o motor le o
  /// projeto do provider, e a cartela nao pode vazar para o editor.
  void _montarViva() {
    final container = ProviderContainer();
    container
        .read(editorControllerProvider.notifier)
        .openProject(cartelaDoEfeito(widget.type));
    setState(() {
      _container = container;
      _tempo = ValueNotifier(const Duration(milliseconds: 400));
      _videos = VideoLayerManager();
    });
    WidgetsBinding.instance.addPostFrameCallback((_) => _capturar());
  }

  Future<void> _capturar() async {
    if (_capturando || !mounted) return;
    _capturando = true;
    // Dois quadros: o primeiro monta, o segundo pinta.
    await Future<void>.delayed(const Duration(milliseconds: 32));
    if (!mounted) return;
    final rb = _chave.currentContext?.findRenderObject();
    if (rb is! RenderRepaintBoundary) {
      _capturando = false;
      return;
    }
    try {
      final img = await rb.toImage(pixelRatio: 2);
      if (!mounted) {
        img.dispose();
        return;
      }
      await EffectThumbnailCache.instance.guardar(_id, img);
      if (!mounted) return;
      setState(() {
        _imagem = img;
        _desmontarViva();
      });
    } catch (_) {
      _capturando = false;
    }
  }

  void _desmontarViva() {
    _container?.dispose();
    _container = null;
    _tempo?.dispose();
    _tempo = null;
    _videos = null;
  }

  @override
  void dispose() {
    _desmontarViva();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final lado = widget.size;
    final img = _imagem;
    if (img != null) {
      return ClipRRect(
        borderRadius: BorderRadius.circular(10),
        child: RawImage(
          key: ValueKey('miniatura-$_id'),
          image: img,
          width: lado,
          height: lado,
          fit: BoxFit.cover,
        ),
      );
    }
    final container = _container;
    if (container == null) {
      return SizedBox(
        width: lado,
        height: lado,
        child: DecoratedBox(
          decoration: BoxDecoration(
            color: AureaColors.chip,
            borderRadius: BorderRadius.circular(10),
          ),
        ),
      );
    }
    return ClipRRect(
      borderRadius: BorderRadius.circular(10),
      child: UncontrolledProviderScope(
        container: container,
        child: RepaintBoundary(
          key: _chave,
          child: SizedBox(
            width: lado,
            height: lado,
            child: ColoredBox(
              color: AureaColors.bg,
              // A composicao tem o tamanho da cartela (240 px); o FittedBox
              // encolhe para o tile sem cortar.
              child: FittedBox(
                fit: BoxFit.contain,
                child: SizedBox(
                  width: 240,
                  height: 240,
                  child: CompositionView(
                    time: _tempo!,
                    videos: _videos!,
                    selectedId: null,
                    exporting: true,
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
