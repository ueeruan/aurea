import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';

import 'fx_lote2.dart' show FxSnapshot;

/// DITHERING DE SAIDA — o que tira a faixa do gradiente escuro.
///
/// Um degrade de `#000` a `#0A1E3C` em tela cheia nao tem degraus
/// suficientes em 8 bits por canal: o olho ve listras, e listra em
/// gradiente e o defeito que denuncia motion amador na hora.
///
/// A correcao e ruido de meio degrau antes de quantizar. Custa quase
/// nada e resolve sozinho.
///
/// COMO ELE RODA importa tanto quanto o que ele faz. Com Impeller o
/// shader entra como FILTRO DE IMAGEM: a GPU aplica o ruido em cima do
/// que ja desenhou, sem foto nenhuma. Sem esse suporte, o caminho antigo
/// — fotografar o filho e passar a foto pelo shader — continua valendo,
/// so que a foto sai na resolucao da tela (ver preview_raster.dart).
/// Fotografar a composicao inteira em resolucao de saida x DPR a cada
/// quadro era o que fechava o app no iPhone.
class DitherLayer extends StatefulWidget {
  const DitherLayer({
    super.key,
    required this.child,
    this.strength = 0.75,
    this.enabled = true,
    this.time = Duration.zero,
    this.pixelRatio = 1.0,
  });

  final Widget child;

  /// Em degraus de 8 bits. 0,75 e o ponto em que a faixa some sem que o
  /// granulado apareca.
  final double strength;

  final bool enabled;

  /// O ruido muda com o tempo — ruido parado vira textura fixa e chama
  /// mais atencao que a faixa que ele veio consertar.
  final Duration time;

  /// Quantos pixels de DISPOSITIVO cabem num pixel logico deste filho —
  /// escala do palco x DPR. E o que diz ao filtro o tamanho real da
  /// textura em que ele roda; errar isso estica o ruido (e a imagem).
  final double pixelRatio;

  /// O programa e carregado uma vez e compartilhado.
  static ui.FragmentProgram? _program;
  static bool _tried = false;

  static Future<void> warmUp() async {
    if (_tried) return;
    _tried = true;
    try {
      _program =
          await ui.FragmentProgram.fromAsset('shaders/dither.frag');
    } catch (_) {
      // Aparelho sem suporte: segue sem dithering, nao quebra.
      _program = null;
    }
  }

  static bool get ready => _program != null;

  /// Se este aparelho aplica o dithering como filtro de GPU (sem foto).
  static bool get comoFiltro => ui.ImageFilter.isShaderFilterSupported;

  @override
  State<DitherLayer> createState() => _DitherLayerState();
}

class _DitherLayerState extends State<DitherLayer> {
  /// UM shader por camada, reaproveitado a cada quadro. Criar um novo a
  /// cada reconstrucao deixava dezenas de shaders (cada um segurando a
  /// imagem que amostrou) esperando o coletor — memoria que sobe ate o
  /// sistema desistir do app.
  ui.FragmentShader? _shader;

  @override
  void initState() {
    super.initState();
    if (!DitherLayer._tried) {
      DitherLayer.warmUp().then((_) {
        if (mounted) setState(() {});
      });
    }
  }

  @override
  void dispose() {
    _shader?.dispose();
    super.dispose();
  }

  ui.FragmentShader? _obterShader() {
    final program = DitherLayer._program;
    if (program == null) return null;
    return _shader ??= program.fragmentShader();
  }

  @override
  Widget build(BuildContext context) {
    final shader = _obterShader();
    if (!widget.enabled || shader == null || widget.strength <= 0.01) {
      return widget.child;
    }
    final seed = (widget.time.inMilliseconds % 4096).toDouble();

    if (DitherLayer.comoFiltro) {
      // O filtro roda na textura da camada, em pixels de dispositivo.
      return LayoutBuilder(builder: (context, c) {
        final w = c.hasBoundedWidth ? c.maxWidth : 1.0;
        final h = c.hasBoundedHeight ? c.maxHeight : 1.0;
        shader
          ..setFloat(0, (w * widget.pixelRatio).ceilToDouble())
          ..setFloat(1, (h * widget.pixelRatio).ceilToDouble())
          ..setFloat(2, widget.strength)
          ..setFloat(3, seed);
        ui.ImageFilter? filtro;
        try {
          filtro = ui.ImageFilter.shader(shader);
        } catch (_) {
          filtro = null;
        }
        if (filtro == null) return widget.child;
        return ImageFiltered(imageFilter: filtro, child: widget.child);
      });
    }

    return FxSnapshot(
      painter: _DitherPainter(
        shader: shader,
        strength: widget.strength,
        seed: seed,
      ),
      child: widget.child,
    );
  }
}

class _DitherPainter extends SnapshotPainter {
  _DitherPainter({
    required this.shader,
    required this.strength,
    required this.seed,
  });

  final ui.FragmentShader shader;
  final double strength;
  final double seed;

  @override
  void paint(PaintingContext context, Offset offset, Size size,
      PaintingContextCallback painter) {
    painter(context, offset);
  }

  @override
  void paintSnapshot(PaintingContext context, Offset offset, Size size,
      ui.Image image, Size sourceSize, double pixelRatio) {
    if (size.isEmpty) return;
    // A foto tem size x pixelRatio pixels; o shader amostra em pixels de
    // tela, entao desenha-se a foto do tamanho dela e o canvas escala.
    shader
      ..setFloat(0, image.width.toDouble())
      ..setFloat(1, image.height.toDouble())
      ..setFloat(2, strength)
      ..setFloat(3, seed)
      ..setImageSampler(0, image);

    context.canvas.save();
    context.canvas.translate(offset.dx, offset.dy);
    context.canvas.scale(1 / pixelRatio);
    context.canvas.drawRect(
      Rect.fromLTWH(0, 0, image.width.toDouble(), image.height.toDouble()),
      Paint()..shader = shader,
    );
    context.canvas.restore();
  }

  @override
  bool shouldRepaint(covariant _DitherPainter old) =>
      old.strength != strength || old.seed != seed;
}
