import 'package:flutter/services.dart';

import '../../../domain/video_project.dart';
import 'keyframes_da_timeline.dart';

// ===========================================================================
// O IMA DA TIMELINE
// ===========================================================================
//
// Arrastar um clipe, uma alca ou um losango GRUDA no que importa: o
// cabecote, o zero, as bordas dos outros clipes, as marcas, as batidas e os
// keyframes das outras camadas. E o que faz o corte cair NO tempo, e nao
// perto dele. O alvo e ABSOLUTO (origem do gesto + deslocamento do dedo),
// entao o ima nunca "prende" o dedo: basta passar da tolerancia para soltar.

/// A distancia, em dp, em que um arrasto gruda num alvo.
const double toleranciaDoImaDp = 12;

/// A distancia, em dp, em que o losango arrastado gruda no cabecote.
const double toleranciaDoCabecoteDp = 8;

/// Os alvos de um gesto, reunidos UMA vez no comeco dele (a lista das
/// outras camadas nao muda enquanto o dedo arrasta uma so).
class Ima {
  Ima._(this._alvos);

  /// Os alvos para arrastar a camada [excluir] (ela nao gruda nela mesma).
  factory Ima.para(VideoProject p, {String? excluir}) {
    final alvos = <int>{0};
    for (final l in p.layers) {
      if (l.id == excluir) continue;
      final ini = l.startTime.inMicroseconds;
      alvos
        ..add(ini)
        ..add(l.endTime.inMicroseconds);
      // Os KEYFRAMES das outras camadas: a animacao desta cai junto com a
      // daquela.
      for (final us in instantesDaCamada(l)) {
        alvos.add(ini + us);
      }
    }
    for (final m in p.markers) {
      alvos.add(m.time.inMicroseconds);
    }
    for (final b in p.beats) {
      alvos.add(b.inMicroseconds);
    }
    return Ima._(alvos.toList()..sort());
  }

  /// Ordenados, para a busca binaria: um projeto de musica tem milhares de
  /// batidas, e isto roda a cada quadro do arrasto.
  final List<int> _alvos;

  /// O alvo mais perto de [us] a menos de [tolUs], com o CABECOTE
  /// ([cabecoteUs]) disputando junto. Nulo = nada perto.
  int? alvoPerto(int us, {required int cabecoteUs, required double tolUs}) {
    int? melhor;
    var melhorD = tolUs;
    void considerar(int alvo) {
      final d = (alvo - us).abs().toDouble();
      if (d <= melhorD) {
        melhorD = d;
        melhor = alvo;
      }
    }

    considerar(cabecoteUs);
    var lo = 0;
    var hi = _alvos.length;
    while (lo < hi) {
      final mid = (lo + hi) >> 1;
      if (_alvos[mid] < us) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    for (var i = lo - 1; i <= lo; i++) {
      if (i >= 0 && i < _alvos.length) considerar(_alvos[i]);
    }
    return melhor;
  }

  /// MOVER UM INTERVALO de [duracaoUs] com o comeco desejado em
  /// [inicioUs]: gruda o comeco OU o fim, o que estiver mais perto.
  /// Devolve o comeco final e o instante da guia (nulo = solto).
  ({int inicioUs, int? guiaUs}) encaixarIntervalo(
    int inicioUs,
    int duracaoUs, {
    required int cabecoteUs,
    required double tolUs,
  }) {
    final a = alvoPerto(inicioUs, cabecoteUs: cabecoteUs, tolUs: tolUs);
    final b = alvoPerto(
      inicioUs + duracaoUs,
      cabecoteUs: cabecoteUs,
      tolUs: tolUs,
    );
    final da = a == null ? double.infinity : (a - inicioUs).abs();
    final db = b == null ? double.infinity : (b - inicioUs - duracaoUs).abs();
    if (a != null && da <= db) return (inicioUs: a, guiaUs: a);
    if (b != null) return (inicioUs: b - duracaoUs, guiaUs: b);
    return (inicioUs: inicioUs, guiaUs: null);
  }
}

/// O TOQUE DO IMA: um clique leve UMA vez por encaixe novo, e nao a cada
/// pixel que o dedo anda com o alvo preso.
class HapticoDoIma {
  int? _ultimo;

  void avisar(int? guiaUs) {
    if (guiaUs != null && guiaUs != _ultimo) {
      HapticFeedback.selectionClick();
    }
    _ultimo = guiaUs;
  }
}

/// Arredonda [us] para o quadro mais perto (na grade do relogio).
int naGradeDeQuadros(double us, int fps) {
  final f = fps < 1 ? 30 : fps;
  return instanteDoQuadroUs((us * f / 1e6).round(), f);
}
