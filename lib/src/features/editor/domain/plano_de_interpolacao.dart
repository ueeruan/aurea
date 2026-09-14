import 'dart:math' as math;

import 'layer.dart';
import 'temporal_interpolation.dart';

/// Um quadro de saida da camera lenta: copia do quadro real [a] ([t] = 0)
/// ou o instante [t] entre os quadros reais [a] e [b] = a + 1.
class PassoDeInterpolacao {
  const PassoDeInterpolacao(this.saida, this.a, this.b, this.t);

  final int saida;
  final int a;
  final int b;
  final double t;

  bool get copia => t == 0;

  @override
  bool operator ==(Object other) =>
      other is PassoDeInterpolacao &&
      other.saida == saida &&
      other.a == a &&
      other.b == b &&
      other.t == t;

  @override
  int get hashCode => Object.hash(saida, a, b, t);

  @override
  String toString() => 'Passo($saida: $a..$b @ $t)';
}

/// PERTO DAS PONTAS O QUADRO REAL FICA. Medido na bancada do host (RIFE
/// v4.6, janela andando 48 px): com t ate 0,1 ou a partir de 0,9 a rede
/// fica presa perto do vizinho e nao ganha de repetir o quadro real — so
/// gasta GPU. Isso aparece quando as taxas nao se dividem (25 fps para
/// 120 da t = 1/24); de 30 para 60 ou 120 nunca acontece.
const pontaDoRife = 0.1;

/// O PLANO DA CAMERA LENTA COM IA.
///
/// A fonte e lida numa taxa BASE (a do proprio arquivo, sem quadro
/// repetido) e a exportacao escolhe quadros numa taxa de SAIDA (composicao
/// vezes o fator de interpolacao). O quadro de saida i mora no instante
/// i/saida; na grade base isso e p = i*base/saida. Caiu num quadro real,
/// copia; senao o RIFE gera o instante t = p - floor(p) entre os dois
/// vizinhos. A conta e inteira: de 30 para 120 os t sao exatamente 1/4,
/// 1/2 e 3/4, sem deriva ao longo do clipe.
///
/// Sao floor(quadrosBase*saida/base) quadros (a mesma duracao dos quadros
/// base). Depois do ultimo quadro real nao ha vizinho: copia-se ele.
///
/// Perto das pontas o quadro real fica: ver [pontaDoRife].
List<PassoDeInterpolacao> planoDeInterpolacao({
  required int quadrosBase,
  required int taxaBase,
  required int taxaSaida,
}) {
  if (quadrosBase <= 0 || taxaBase <= 0 || taxaSaida <= 0) return const [];
  final total = math.max(1, quadrosBase * taxaSaida ~/ taxaBase);
  final ultimo = quadrosBase - 1;
  return [
    for (var i = 0; i < total; i++)
      () {
        final numerador = i * taxaBase;
        final a = numerador ~/ taxaSaida;
        final resto = numerador % taxaSaida;
        if (a >= ultimo) return PassoDeInterpolacao(i, ultimo, ultimo, 0);
        final t = resto / taxaSaida;
        if (resto == 0 || t <= pontaDoRife) {
          return PassoDeInterpolacao(i, a, a, 0);
        }
        if (t >= 1 - pontaDoRife) return PassoDeInterpolacao(i, a + 1, a + 1, 0);
        return PassoDeInterpolacao(i, a, a + 1, t);
      }(),
  ];
}

/// Como os quadros a mais de um clipe lento sao feitos na exportacao.
enum ComoInterpolar {
  /// Clipe sem camera lenta (ou interpolacao desligada): extracao normal.
  nada,

  /// A fonte ja tem quadros suficientes (video de 60 ou 120 fps): so os
  /// quadros REAIS, sem inventar nenhum.
  quadrosReais,

  /// RIFE (IA, fluxo optico aprendido) sobre os quadros reais.
  rife,

  /// FFmpeg minterpolate (mci ou blend), como sempre foi.
  ffmpeg,
}

/// A decisao, com a taxa em que a fonte deve ser lida.
///
/// [fpsDaFonte] vem do ffprobe (nulo quando nao se sabe: vale a da
/// composicao). O RIFE so entra no modo "movimento" — "mesclar" continua
/// sendo a mistura barata do FFmpeg — e so quando o aparelho tem o motor.
({ComoInterpolar como, int taxaBase}) estrategiaDeInterpolacao(
  VideoLayer layer, {
  required int fps,
  required double? fpsDaFonte,
  required bool rifeDisponivel,
}) {
  final fator = fatorDeInterpolacao(layer);
  final taxa = fps * fator;
  if (fator <= 1) return (como: ComoInterpolar.nada, taxaBase: taxa);
  final fonte = fpsDaFonte;
  final base = fonte != null && fonte.isFinite && fonte >= 1 && fonte <= 480
      ? fonte.round()
      : fps;
  if (base >= taxa) return (como: ComoInterpolar.quadrosReais, taxaBase: taxa);
  if (rifeDisponivel &&
      interpolacaoEfetiva(layer) == InterpolacaoDeQuadros.movimento) {
    return (como: ComoInterpolar.rife, taxaBase: base);
  }
  return (como: ComoInterpolar.ffmpeg, taxaBase: taxa);
}
