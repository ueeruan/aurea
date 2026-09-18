import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';

import '../../domain/effect.dart';
import '../../domain/time_warp_rgb.dart';
import 'blend_mask.dart';

/// O RGB NO TEMPO — TRES MONTAGENS DA CAMADA, UMA POR CANAL.
///
/// Cada canal mostra a camada num instante diferente e os tres sao
/// somados. A montagem de cada instante vem de fora ([emTempo]): quem
/// sabe montar a camada noutro tempo e o palco, com `_emOutroTempo`, e
/// repetir essa conta aqui seria criar um segundo caminho de composicao.
///
/// ==========================================================================
/// POR QUE A SOMA PRECISA DO `BlendMask`, E NAO DE UM `Stack` COMUM
/// ==========================================================================
///
/// Um `Stack` desenha um filho por cima do outro em `srcOver`: o ultimo
/// canal taparia os dois anteriores. O que se quer e a SOMA, e somar um
/// filho com o que ja esta embaixo e o que o `BlendMask` faz — com o
/// caminho da foto, porque `ColorFiltered` cria camada do motor e o
/// `saveLayer` do canvas nao alcanca essas (esta escrito no proprio
/// `BlendMask`, e foi assim que o glow perdia a fonte).
///
/// O PRECO E CONHECIDO: tres montagens da camada e duas fotos por quadro.
/// Nao ha caminho mais barato para este efeito — cada canal e uma imagem
/// de verdade, e um shader so ve uma entrada.
class TimeWarpRgbPass extends StatelessWidget {
  const TimeWarpRgbPass({
    super.key,
    required this.effect,
    required this.time,
    required this.child,
    required this.emTempo,
    required this.tempoDeslocado,
  });

  final EffectInstance effect;
  final Duration time;

  /// A camada no instante atual, ja montada pelo palco.
  final Widget child;

  /// Monta a MESMA camada noutro instante da composicao.
  final Widget Function(Duration tempo) emTempo;

  /// O tempo que a camada mostra com [quadros] de deslocamento.
  final Duration Function(int quadros) tempoDeslocado;

  @override
  Widget build(BuildContext context) {
    final d = deslocamentosDoTimeWarp(effect, time);
    final mistura = (effect.paramAt('mix', time) / 100).clamp(0.0, 1.0);
    if (mistura <= .001 || timeWarpEhIdentidade(d)) return child;

    Widget canal(int quadros, int canal) => ColorFiltered(
      colorFilter: ui.ColorFilter.matrix(matrizDoCanal(canal)),
      child: quadros == 0 ? child : emTempo(tempoDeslocado(quadros)),
    );

    // Cada canal com a SUA montagem, e so o canal dele na imagem.
    final vermelho = canal(d.r, 0);
    final verde = canal(d.g, 1);
    final azul = canal(d.b, 2);

    final somado = Stack(
      clipBehavior: Clip.none,
      children: [
        vermelho,
        BlendMask(blendMode: BlendMode.plus, child: verde),
        BlendMask(blendMode: BlendMode.plus, child: azul),
      ],
    );
    if (mistura >= .999) return somado;
    return Stack(
      clipBehavior: Clip.none,
      children: [
        child,
        Positioned.fill(child: Opacity(opacity: mistura, child: somado)),
      ],
    );
  }

}
