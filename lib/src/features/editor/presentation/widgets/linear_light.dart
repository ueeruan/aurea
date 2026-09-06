import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';

/// A FAMILIA DE LUZ, EM ESPACO LINEAR.
///
/// Borrar, somar e misturar sao contas de LUZ, e luz soma em espaco
/// linear. O valor guardado numa imagem nao e luz: e um numero corrigido
/// para o olho. Borrar esse numero direto e o que faz o glow sair
/// acinzentado e o desfoque deixar halo escuro na borda.
///
/// Aqui o filtro pedido e embrulhado entre desfazer e refazer a curva do
/// sRGB — tres passes na GPU, encadeados pelo proprio motor:
///
///     sRGB -> linear   →   o filtro   →   linear -> sRGB
///
/// Sem suporte (aparelho sem Impeller, ou o shader nao carregou), o
/// filtro volta EXATAMENTE como veio. Um efeito com a matematica antiga
/// e melhor do que nenhum efeito.
class LinearLight {
  LinearLight._();

  static ui.FragmentProgram? _program;
  static bool _tried = false;

  static Future<void> warmUp() async {
    if (_tried) return;
    _tried = true;
    try {
      _program = await ui.FragmentProgram.fromAsset('shaders/gamma.frag');
    } catch (_) {
      _program = null;
    }
  }

  /// Chave de diagnostico: `--dart-define=AUREA_LINEAR=false` desliga o
  /// espaco linear inteiro, para comparar no aparelho.
  static const bool _ligado =
      bool.fromEnvironment('AUREA_LINEAR', defaultValue: true);

  /// Se da para trabalhar em linear neste aparelho.
  static bool get ready =>
      _ligado && _program != null && ui.ImageFilter.isShaderFilterSupported;

  static ui.ImageFilter? _curva(double mode, ui.Size size) {
    final p = _program;
    if (p == null) return null;
    try {
      final shader = p.fragmentShader()
        ..setFloat(0, size.width <= 0 ? 1 : size.width)
        ..setFloat(1, size.height <= 0 ? 1 : size.height)
        ..setFloat(2, mode);
      return ui.ImageFilter.shader(shader);
    } catch (_) {
      // Aparelho sem suporte a shader como filtro.
      return null;
    }
  }

  /// Embrulha [inner] para que ele aconteca em espaco LINEAR.
  ///
  /// [size] e o tamanho em que o filtro vai rodar — o shader precisa
  /// dele para achar o pixel.
  static ui.ImageFilter wrap(ui.ImageFilter inner, ui.Size size) {
    if (!ready) return inner;
    final paraLinear = _curva(0, size);
    final paraSrgb = _curva(1, size);
    if (paraLinear == null || paraSrgb == null) return inner;

    // compose(outer, inner) = outer(inner(x)). Lendo de dentro para
    // fora: primeiro tira a curva, depois filtra, depois devolve a curva.
    return ui.ImageFilter.compose(
      outer: paraSrgb,
      inner: ui.ImageFilter.compose(outer: inner, inner: paraLinear),
    );
  }

  /// Desfoque gaussiano feito em espaco linear.
  ///
  /// O kernel e o do motor (normalizado por construcao — a soma dos
  /// pesos e 1, entao o desfoque nao clareia nem escurece). O que
  /// faltava era o espaco.
  static ui.ImageFilter blur({
    required double sigmaX,
    required double sigmaY,
    required ui.Size size,
    ui.TileMode tileMode = ui.TileMode.decal,
  }) =>
      wrap(
        ui.ImageFilter.blur(
            sigmaX: sigmaX, sigmaY: sigmaY, tileMode: tileMode),
        size,
      );

  /// DESFOQUE EM LINEAR, JA EMBRULHADO NO WIDGET — e com MARGEM.
  ///
  /// O desfoque espalha a imagem para fora da caixa da fonte, e o filtro
  /// de shader que vem depois (linear -> sRGB) recebe uma textura maior
  /// que a fonte. O motor coloca a saida desse filtro no canto da FONTE,
  /// nao no canto da textura: o halo saia deslocado para baixo e para a
  /// direita, exatamente o tanto que o desfoque cresceu. A margem pintada
  /// fora da caixa (um preto de 1/255 de alfa, invisivel) faz a textura
  /// nascer ja do tamanho final: nada cresce, nada desloca.
  static Widget blurred({
    required Widget child,
    required double sigmaX,
    required double sigmaY,
    required ui.Size size,
    ui.TileMode tileMode = ui.TileMode.decal,
  }) {
    final filtro = blur(
        sigmaX: sigmaX, sigmaY: sigmaY, size: size, tileMode: tileMode);
    if (!ready) return ImageFiltered(imageFilter: filtro, child: child);
    final margem = (3 * math.max(sigmaX, sigmaY) + 2).toDouble();
    return ImageFiltered(
      imageFilter: filtro,
      child: CustomPaint(painter: _Margem(margem), child: child),
    );
  }
}

class _Margem extends CustomPainter {
  const _Margem(this.pad);

  final double pad;

  @override
  void paint(ui.Canvas canvas, ui.Size size) {
    canvas.drawRect(
      ui.Rect.fromLTWH(-pad, -pad, size.width + 2 * pad, size.height + 2 * pad),
      ui.Paint()..color = const ui.Color(0x01000000),
    );
  }

  @override
  bool shouldRepaint(_Margem old) => old.pad != pad;
}
