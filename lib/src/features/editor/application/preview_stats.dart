import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../domain/gear.dart';

/// Overlay de diagnostico do preview (specs motor-de-preview §7 e
/// arquitetura-de-marchas §9): sem numeros, todo "travou" vira
/// adivinhacao.
final debugOverlayProvider = StateProvider<bool>((ref) => false);

/// Medidores do preview. O compositor chama [tick] a cada frame que ele
/// REALMENTE compoe — com o portao de marchas, cena estatica compoe ~0/s
/// (o analogo Flutter de "o player nao compoe, so toca").
abstract final class PreviewStats {
  /// TRAVADAS DE INTERFACE: quadros que passaram de 34 ms do inicio da
  /// construcao ao fim da rasterizacao, contados pelo proprio motor.
  /// E o numero que mede "arrastar trava a tela" — o registrador de
  /// frames so ve a reproducao, e a travada de arrasto acontece parado.
  static final ValueNotifier<int> jankFrames = ValueNotifier(0);
  static final ValueNotifier<double> worstFrameMs = ValueNotifier(0);
  static bool _timingsHooked = false;

  static void hookTimings() {
    if (_timingsHooked) return;
    _timingsHooked = true;
    SchedulerBinding.instance.addTimingsCallback((timings) {
      var travadas = 0;
      var pior = 0.0;
      for (final t in timings) {
        final ms = t.totalSpan.inMicroseconds / 1000.0;
        if (ms > pior) pior = ms;
        if (ms > 34) travadas++;
      }
      if (travadas > 0) jankFrames.value += travadas;
      if (pior > worstFrameMs.value) worstFrameMs.value = pior;
    });
  }

  static void resetJank() {
    jankFrames.value = 0;
    worstFrameMs.value = 0;
  }

  /// Composicoes por segundo (janela de 1 s).
  static final ValueNotifier<int> compsPerSec = ValueNotifier(0);

  /// Camadas compostas no ultimo frame.
  static final ValueNotifier<int> layersInFrame = ValueNotifier(0);

  /// Marcha ativa + motivo (classificador PR-G1).
  static final ValueNotifier<GearDecision?> gear = ValueNotifier(null);

  /// Variancia do intervalo entre ticks do clock, em ms (§6: suavidade e
  /// cadencia, nao media de fps). Janela de ~1 s.
  static final ValueNotifier<double> tickVarianceMs = ValueNotifier(0);

  /// Percentual do tempo de uso em M1+M2 (instrumentacao do PR-G1: se
  /// passar de 60%, o ganho da fase nativa esta provado).
  static final ValueNotifier<int> lowGearPercent = ValueNotifier(0);

  static int _count = 0;
  static int _windowStartMs = 0;

  static void tick(int layers) {
    layersInFrame.value = layers;
    final now = DateTime.now().millisecondsSinceEpoch;
    if (_windowStartMs == 0) _windowStartMs = now;
    _count++;
    final span = now - _windowStartMs;
    if (span >= 1000) {
      compsPerSec.value = (_count * 1000 / span).round();
      _count = 0;
      _windowStartMs = now;
    }
  }

  /// Janela deslizante de comps/s zera sozinha quando o portao segura as
  /// recomposicoes (nenhum tick chega para fechar a janela).
  static void idle() {
    final now = DateTime.now().millisecondsSinceEpoch;
    if (_windowStartMs != 0 && now - _windowStartMs >= 1000) {
      compsPerSec.value =
          (_count * 1000 / (now - _windowStartMs)).round();
      _count = 0;
      _windowStartMs = now;
    }
  }

  // ---- marcha: tempo acumulado por classe (M1+M2 vs resto) ----
  static int _gearSinceMs = 0;
  static int _lowMs = 0;
  static int _highMs = 0;

  static void setGear(GearDecision decision) {
    final now = DateTime.now().millisecondsSinceEpoch;
    final prev = gear.value;
    if (_gearSinceMs != 0 && prev != null) {
      final span = now - _gearSinceMs;
      if (prev.gear == PreviewGear.m1 || prev.gear == PreviewGear.m2) {
        _lowMs += span;
      } else {
        _highMs += span;
      }
      final total = _lowMs + _highMs;
      if (total > 0) {
        lowGearPercent.value = (_lowMs * 100 / total).round();
      }
    }
    _gearSinceMs = now;
    if (prev?.gear != decision.gear || prev?.reason != decision.reason) {
      gear.value = decision;
    }
  }

  // ---- cadencia: variancia do intervalo entre ticks ----
  static final List<double> _intervals = <double>[];
  static int _lastTickUs = 0;

  static void clockTick() {
    final now = DateTime.now().microsecondsSinceEpoch;
    if (_lastTickUs != 0) {
      _intervals.add((now - _lastTickUs) / 1000.0);
      if (_intervals.length > 32) _intervals.removeAt(0);
      if (_intervals.length >= 8) {
        final mean =
            _intervals.reduce((a, b) => a + b) / _intervals.length;
        var acc = 0.0;
        for (final v in _intervals) {
          acc += (v - mean) * (v - mean);
        }
        final std = math.sqrt(acc / _intervals.length);
        // Atualiza com moderacao para nao virar ruido visual.
        if ((std - tickVarianceMs.value).abs() > 0.1) {
          tickVarianceMs.value = double.parse(std.toStringAsFixed(1));
        }
      }
    }
    _lastTickUs = now;
  }

  /// Pausou/retomou: intervalo atravessando a pausa nao e jitter.
  static void clockReset() {
    _lastTickUs = 0;
    _intervals.clear();
  }
}

/// Relatorio do registrador de frames (PR-J0).
typedef FrameReport = ({
  double medianMs,
  int stutters,
  double gapS,
  double gapSdS,
  double peakMs,
  double driftMs,
  int seconds,
});

/// Analise PURA da serie de intervalos entre frames (ms), sem relogio:
/// mediana, travadas (> 2x a mediana), intervalo medio entre travadas e
/// o DESVIO desse intervalo — desvio baixo significa ciclo regular, e
/// ciclo regular aponta causa mecanica.
FrameReport analyzeIntervals(List<double> intervalsMs,
    {double driftMs = 0}) {
  if (intervalsMs.isEmpty) {
    return (
      medianMs: 0,
      stutters: 0,
      gapS: 0,
      gapSdS: 0,
      peakMs: 0,
      driftMs: driftMs,
      seconds: 0
    );
  }
  final sorted = List<double>.of(intervalsMs)..sort();
  final n = sorted.length;
  final median = n.isOdd
      ? sorted[n >> 1]
      : (sorted[(n >> 1) - 1] + sorted[n >> 1]) / 2;
  final peak = sorted.last;

  // Instante de cada travada vem da soma cumulativa: nada de relogio.
  final at = <double>[];
  var elapsedMs = 0.0;
  for (final v in intervalsMs) {
    elapsedMs += v;
    if (median > 0 && v > median * 2) at.add(elapsedMs / 1000.0);
  }

  var gap = 0.0;
  var sd = 0.0;
  if (at.length >= 2) {
    var sum = 0.0;
    for (var i = 1; i < at.length; i++) {
      sum += at[i] - at[i - 1];
    }
    gap = sum / (at.length - 1);
    var acc = 0.0;
    for (var i = 1; i < at.length; i++) {
      final d = (at[i] - at[i - 1]) - gap;
      acc += d * d;
    }
    sd = math.sqrt(acc / (at.length - 1));
  }

  double r1(double v) => double.parse(v.toStringAsFixed(1));
  return (
    medianMs: r1(median),
    stutters: at.length,
    gapS: r1(gap),
    gapSdS: double.parse(sd.toStringAsFixed(2)),
    peakMs: double.parse(peak.toStringAsFixed(0)),
    driftMs: r1(driftMs),
    seconds: (elapsedMs / 1000).round(),
  );
}

/// REGISTRADOR DE FRAMES (spec travada-periodica, PR-J0): a travada
/// periodica nao se diagnostica pela media de fps — se diagnostica pelo
/// INTERVALO ENTRE AS TRAVADAS. Intervalo regular = causa mecanica.
///
/// Mede no ponto exato de apresentacao: mediana do intervalo, travadas
/// (> 2x a mediana), intervalo entre elas e o desvio desse intervalo,
/// pico, e a DERIVA entre o relogio da composicao e o da midia — a
/// deriva subindo e zerando de repente e a assinatura exata de C1.
abstract final class FrameLog {
  /// ~60 s a 30 fps.
  static const int _cap = 1800;

  static final List<double> _intervals = <double>[];
  static double _driftMs = 0;
  static int _lastUs = 0;

  /// Snapshot para a UI (recalculado no maximo 1x/s).
  static final ValueNotifier<FrameReport?> report =
      ValueNotifier<FrameReport?>(null);
  static int _lastReportMs = 0;

  static void reset() {
    _intervals.clear();
    _lastUs = 0;
    _driftMs = 0;
    report.value = null;
  }

  /// Deriva medida entre o relogio da composicao e a posicao real da
  /// midia (ms). Positivo = composicao adiantada.
  static void reportDrift(double ms) => _driftMs = ms;

  /// Ponto de APRESENTACAO do frame.
  static void present() {
    final now = DateTime.now().microsecondsSinceEpoch;
    if (_lastUs != 0) {
      _intervals.add((now - _lastUs) / 1000.0);
      if (_intervals.length > _cap) _intervals.removeAt(0);
    }
    _lastUs = now;

    final nowMs = now ~/ 1000;
    if (nowMs - _lastReportMs >= 1000) {
      _lastReportMs = nowMs;
      report.value = analyzeIntervals(_intervals, driftMs: _driftMs);
    }
  }
}
