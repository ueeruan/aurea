import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';

import '../../features/editor/presentation/context/parameter_row.dart'
    show KeyframeState;
import '../ui/tocavel.dart';
import 'tokens.dart';

export '../../features/editor/presentation/context/parameter_row.dart'
    show KeyframeState;

/// A TOLERANCIA DE "NESTA MARCA": 8 ms, a mesma do painel antigo e da
/// barra de reproducao. Um quadro a 120 fps tem 8,3 ms — menos que isso e
/// o mesmo instante para quem arrasta.
const int toleranciaDaMarcaUs = 8000;

/// AS MARCAS VIZINHAS de [agoraUs] (tempos no MESMO relogio, em µs): a
/// ultima antes e a primeira depois, ignorando a que esta em cima.
///
/// O controlador nao tem "ir para o keyframe anterior" — e nao precisa:
/// os instantes ja existem (`propKeyframeTimes`, `keyframeTimes` do
/// efeito, os da cena 3D), e pular e so escolher um e fazer `seek`. Esta
/// conta e a UNICA do app para isso, para as setas de todo painel
/// concordarem.
({int? anterior, int? proxima}) marcasVizinhas(
  Iterable<int> marcasUs,
  int agoraUs,
) {
  int? anterior;
  int? proxima;
  for (final us in marcasUs) {
    if ((us - agoraUs).abs() < toleranciaDaMarcaUs) continue;
    if (us < agoraUs) {
      if (anterior == null || us > anterior) anterior = us;
    } else if (proxima == null || us < proxima) {
      proxima = us;
    }
  }
  return (anterior: anterior, proxima: proxima);
}

/// Ha marca em [agoraUs]?
bool temMarcaEm(Iterable<int> marcasUs, int agoraUs) =>
    marcasUs.any((us) => (us - agoraUs).abs() < toleranciaDaMarcaUs);

/// O LOSANGO DA LINHA DE PROPRIEDADE, com as setas.
///
/// Tres estados, lidos do [KeyframeState] — o MESMO objeto do editor
/// antigo, para haver um sistema de keyframe so:
///
///  * ◇ apagado: a propriedade nao e animada;
///  * ◇ aceso: animada, mas sem marca neste quadro — e aparecem ‹ ›;
///  * ◆ cheio: ha marca NESTE quadro.
///
/// A LARGURA E FIXA (64) com ou sem setas: a linha nao pode pular para a
/// esquerda no instante em que a primeira marca nasce, com o dedo em cima.
class AureaKeyframeButton extends StatelessWidget {
  const AureaKeyframeButton({
    super.key,
    required this.estado,
    this.aoAnterior,
    this.aoProximo,
    this.chave = 'kf',
  });

  final KeyframeState estado;

  /// Ir para a marca anterior/proxima. Nulo = nao ha (seta apagada).
  final VoidCallback? aoAnterior;
  final VoidCallback? aoProximo;

  /// Base das chaves de teste: `<chave>`, `<chave>-anterior`,
  /// `<chave>-proximo`.
  final String chave;

  @override
  Widget build(BuildContext context) {
    final cor = estado.animated || estado.here
        ? AureaCores.keyframe
        : AureaCores.textoSecundario.withValues(alpha: .6);
    Widget seta(String sufixo, IconData icone, VoidCallback? acao) {
      if (!estado.animated) {
        return const SizedBox(width: 18);
      }
      return Tocavel(
        key: ValueKey('$chave-$sufixo'),
        onTap: acao,
        child: SizedBox(
          width: 18,
          height: AureaDims.toqueConfortavel,
          child: Icon(
            icone,
            size: 13,
            color: acao == null
                ? AureaCores.textoSecundario.withValues(alpha: .3)
                : AureaCores.keyframe,
          ),
        ),
      );
    }

    return SizedBox(
      width: AureaDims.botaoDeKeyframe,
      height: AureaDims.toqueConfortavel,
      child: Row(
        children: [
          seta('anterior', CupertinoIcons.chevron_left, aoAnterior),
          Tocavel(
            key: ValueKey(chave),
            onTap: () {
              // HAPTICO LEVE SO AO CRIAR: tirar uma marca nao e um
              // acontecimento, por-la e.
              if (!estado.here) HapticFeedback.lightImpact();
              estado.onToggle();
            },
            onLongPress: estado.onCurve,
            child: SizedBox(
              width: 28,
              height: AureaDims.toqueConfortavel,
              child: Icon(
                estado.here
                    ? CupertinoIcons.rhombus_fill
                    : CupertinoIcons.rhombus,
                size: 15,
                color: cor,
              ),
            ),
          ),
          seta('proximo', CupertinoIcons.chevron_right, aoProximo),
        ],
      ),
    );
  }
}
