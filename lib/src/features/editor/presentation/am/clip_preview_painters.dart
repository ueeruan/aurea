import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../../domain/peak_pyramid.dart';

/// FORMA DE ONDA na barra do clipe.
///
/// Desenhada como envelope espelhado no meio da barra: e a forma que
/// deixa a respiracao entre duas falas visivel de relance, que e para
/// isso que ela serve.
///
/// Os picos cobrem o arquivo INTEIRO; a barra mostra so o trecho usado,
/// entao a janela [start, end] recorta a leitura. Assim arrastar a alca
/// de corte revela o audio que estava fora, em vez de esticar o que ja
/// estava dentro.
/// FORMA DE ONDA COM CORPO: contorno de PICO e miolo de RMS.
///
/// So o pico da uma mancha cheia que nao diz o quao alto esta; so o RMS
/// da uma forma sem ataque, que esconde a batida seca. Os dois juntos
/// sao a forma de onda que se reconhece — e e a mesma leitura que
/// qualquer editor de audio mostra.
///
/// O nivel da piramide e escolhido pelo ZOOM: ampliar troca de nivel e
/// nunca recalcula nada.
class PyramidWaveformPainter extends CustomPainter {
  const PyramidWaveformPainter({
    required this.pyramid,
    required this.start,
    required this.end,
    required this.color,
    this.silences = const [],
  });

  final PeakPyramid pyramid;

  /// Trecho do ARQUIVO que esta barra mostra.
  final Duration start;
  final Duration end;
  final Color color;

  /// Regioes de silencio, em tempo do arquivo — marcadas para a
  /// decupagem: e onde o corte vai cair.
  final List<(Duration, Duration)> silences;

  @override
  void paint(Canvas canvas, Size size) {
    if (pyramid.isEmpty || size.width < 2 || size.height < 4) return;
    final janela = (end - start).inMicroseconds / 1000000.0;
    if (janela <= 0) return;

    final segundosPorPixel = janela / size.width;
    final nivel = pyramid.levelFor(segundosPorPixel);
    if (nivel.length == 0) return;

    final mid = size.height / 2;
    final half = size.height / 2 - 1.5;

    // SILENCIO primeiro, por tras da onda.
    if (silences.isNotEmpty) {
      final fundo = Paint()..color = color.withValues(alpha: 0.12);
      for (final sil in silences) {
        final a = (sil.$1 - start).inMicroseconds / 1000000.0 / janela;
        final b = (sil.$2 - start).inMicroseconds / 1000000.0 / janela;
        final x0 = (a * size.width).clamp(0.0, size.width);
        final x1 = (b * size.width).clamp(0.0, size.width);
        if (x1 > x0) {
          canvas.drawRect(Rect.fromLTRB(x0, 0, x1, size.height), fundo);
        }
      }
    }

    final contorno = Paint()
      ..color = color
      ..strokeWidth = 1
      ..strokeCap = StrokeCap.round;
    final corpo = Paint()
      ..color = color.withValues(alpha: 0.55)
      ..strokeWidth = 1
      ..strokeCap = StrokeCap.round;

    final de = nivel.bucketAt(start);
    final ate = nivel.bucketAt(end);
    final vao = ate - de;
    if (vao <= 0) return;

    final colunas = size.width.floor();
    final passo = vao / colunas;

    for (var x = 0; x < colunas; x++) {
      final a = de + (x * passo).floor();
      final b = de + ((x + 1) * passo).ceil();
      var alto = 0.0, baixo = 0.0, rms = 0.0;
      var contou = 0;
      for (var i = a; i < b; i++) {
        if (i < 0 || i >= nivel.length) continue;
        if (nivel.max[i] > alto) alto = nivel.max[i];
        if (nivel.min[i] < baixo) baixo = nivel.min[i];
        if (nivel.rms[i] > rms) rms = nivel.rms[i];
        contou++;
      }
      if (contou == 0) continue;
      final dx = x + 0.5;

      // Raiz comprime o alto e abre o baixo: som fraco continua visivel.
      final hAlto = math.sqrt(alto.abs()) * half;
      final hBaixo = math.sqrt(baixo.abs()) * half;
      if (hAlto > 0 || hBaixo > 0) {
        canvas.drawLine(
          Offset(dx, mid - hAlto),
          Offset(dx, mid + hBaixo),
          contorno,
        );
      }
      final hRms = math.sqrt(rms) * half;
      if (hRms > 0.5) {
        canvas.drawLine(Offset(dx, mid - hRms), Offset(dx, mid + hRms), corpo);
      }
    }
  }

  @override
  bool shouldRepaint(PyramidWaveformPainter old) =>
      old.pyramid != pyramid ||
      old.start != start ||
      old.end != end ||
      old.color != color ||
      old.silences.length != silences.length;
}

class WaveformPainter extends CustomPainter {
  const WaveformPainter({
    required this.peaks,
    required this.start,
    required this.end,
    required this.color,
  });

  final Float32List peaks;
  final Duration start;
  final Duration end;
  final Color color;

  /// Picos por segundo — tem de bater com quem gerou.
  static const perSecond = 100;

  @override
  void paint(Canvas canvas, Size size) {
    if (peaks.isEmpty || size.width < 2 || size.height < 4) return;

    final from = (start.inMilliseconds / 1000.0 * perSecond).floor();
    final to = (end.inMilliseconds / 1000.0 * perSecond).ceil();
    final span = to - from;
    if (span <= 0) return;

    final mid = size.height / 2;
    final half = size.height / 2 - 1.5;
    final paint = Paint()
      ..color = color
      ..strokeWidth = 1
      ..strokeCap = StrokeCap.round;

    // Uma coluna por pixel: mais que isso nao aparece, menos que isso
    // esconde transiente.
    final columns = size.width.floor();
    final step = span / columns;
    for (var x = 0; x < columns; x++) {
      final a = from + (x * step).floor();
      final b = from + ((x + 1) * step).ceil();
      var peak = 0.0;
      for (var i = a; i < b; i++) {
        if (i < 0 || i >= peaks.length) continue;
        if (peaks[i] > peak) peak = peaks[i];
      }
      if (peak <= 0) continue;
      // Raiz comprime o alto e abre o baixo: som fraco continua visivel.
      final h = math.sqrt(peak) * half;
      final dx = x + 0.5;
      canvas.drawLine(Offset(dx, mid - h), Offset(dx, mid + h), paint);
    }
  }

  @override
  bool shouldRepaint(WaveformPainter old) =>
      old.peaks != peaks ||
      old.start != start ||
      old.end != end ||
      old.color != color;
}

/// TIRA DE MINIATURAS na barra do clipe de video.
///
/// Sem ela, achar o corte e tatear: a barra e um retangulo liso e a
/// unica pista e o playhead. Com ela, da para ver a cena mudar.
class FilmstripPainter extends CustomPainter {
  const FilmstripPainter({
    required this.frames,
    required this.start,
    required this.end,
    required this.sourceDuration,
  });

  final List<ui.Image> frames;

  /// Trecho usado do arquivo.
  final Duration start;
  final Duration end;

  /// Duracao total do arquivo, que e o que as miniaturas cobrem.
  final Duration sourceDuration;

  @override
  void paint(Canvas canvas, Size size) {
    if (frames.isEmpty || size.width < 2 || size.height < 4) return;

    final total = sourceDuration.inMicroseconds;
    final a = total <= 0 ? 0.0 : start.inMicroseconds / total;
    final b = total <= 0 ? 1.0 : end.inMicroseconds / total;
    final span = (b - a).clamp(0.0001, 1.0);

    final paint = Paint()
      ..filterQuality = FilterQuality.low
      ..isAntiAlias = false;

    // Largura de cada miniatura na barra, mantendo a proporcao.
    final first = frames.first;
    final tileW = size.height * first.width / first.height;
    if (tileW <= 0) return;

    final n = (size.width / tileW).ceil() + 1;
    for (var i = 0; i < n; i++) {
      final x = i * tileW;
      // Qual instante do ARQUIVO esta neste ponto da barra.
      final f = a + (x / size.width) * span;
      final idx = (f * frames.length).floor().clamp(0, frames.length - 1);
      final img = frames[idx];
      canvas.drawImageRect(
        img,
        Rect.fromLTWH(0, 0, img.width.toDouble(), img.height.toDouble()),
        Rect.fromLTWH(x, 0, tileW, size.height),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(FilmstripPainter old) =>
      old.frames != frames ||
      old.start != start ||
      old.end != end ||
      old.sourceDuration != sourceDuration;
}

/// A ONDA DO CLIPE, legivel para decupar.
///
/// Diferencas para o [PyramidWaveformPainter]:
///   * segue o instante REAL do arquivo em cada coluna ([fonte]), entao
///     acompanha corte, velocidade, reverso e Time Remap;
///   * espelho de verdade: o maximo para cima e o minimo para baixo;
///   * altura em DECIBEIS (piso de -45 dB) com ganho de exibicao — fala
///     baixa continua visivel e um estalo nao achata o resto;
///   * le a piramide pela janela fracionaria, sem arredondar o comeco
///     para o balde (antes a onda podia ficar ate 640 ms fora do lugar).
class ClipWaveformPainter extends CustomPainter {
  const ClipWaveformPainter({
    required this.pyramid,
    required this.fonte,
    required this.color,
    this.contorno,
    this.gain = 1,
    this.muted = false,
  });

  final PeakPyramid pyramid;

  /// n+1 instantes (segundos, absolutos no arquivo) uniformes na largura.
  final Float64List fonte;
  final Color color;
  final Color? contorno;
  final double gain;
  final bool muted;

  static const double pisoDb = 45;

  double _em(double u) {
    final n = fonte.length - 1;
    if (n <= 0) return fonte.isEmpty ? 0 : fonte.first;
    final p = (u * n).clamp(0.0, n.toDouble());
    final i = p.floor().clamp(0, n - 1);
    final f = p - i;
    return fonte[i] * (1 - f) + fonte[i + 1] * f;
  }

  double _altura(double amplitude, double half) {
    final a = amplitude.abs() * gain;
    if (a <= 1e-5) return 0;
    final db = 20 * math.log(a) / math.ln10;
    return ((db + pisoDb) / pisoDb).clamp(0.0, 1.0) * half;
  }

  @override
  void paint(Canvas canvas, Size size) {
    if (pyramid.isEmpty || fonte.length < 2 || size.width < 2 || size.height < 6) {
      return;
    }
    final colunas = size.width.floor();
    final mid = size.height / 2;
    final half = size.height / 2 - 1;
    final cima = Float32List(colunas), baixo = Float32List(colunas);
    final corpo = Float32List(colunas);
    for (var x = 0; x < colunas; x++) {
      final t0 = _em(x / colunas), t1 = _em((x + 1) / colunas);
      final lo = math.min(t0, t1), hi = math.max(t0, t1);
      final nivel = pyramid.levelFor(math.max(hi - lo, 1e-6));
      final b = nivel.bucketSeconds;
      if (nivel.length == 0 || b <= 0) continue;
      var a = (lo / b).floor();
      var z = (hi / b).ceil();
      if (z <= a) z = a + 1;
      if (a < 0) a = 0;
      if (z > nivel.length) z = nivel.length;
      var mx = 0.0, mn = 0.0, rms = 0.0;
      for (var i = a; i < z; i++) {
        if (nivel.max[i] > mx) mx = nivel.max[i];
        if (nivel.min[i] < mn) mn = nivel.min[i];
        if (nivel.rms[i] > rms) rms = nivel.rms[i];
      }
      cima[x] = _altura(mx, half);
      baixo[x] = _altura(mn, half);
      corpo[x] = _altura(rms, half);
    }
    final alfa = muted ? 0.35 : 1.0;
    Path forma(Float32List up, Float32List down) {
      final p = Path()..moveTo(0, mid - up[0]);
      for (var x = 1; x < colunas; x++) {
        p.lineTo(x + 0.5, mid - up[x]);
      }
      for (var x = colunas - 1; x >= 0; x--) {
        p.lineTo(x + 0.5, mid + down[x]);
      }
      return p..close();
    }

    canvas.drawLine(
      Offset(0, mid),
      Offset(size.width, mid),
      Paint()
        ..color = Colors.white.withValues(alpha: 0.10 * alfa)
        ..strokeWidth = 1,
    );
    final pico = forma(cima, baixo);
    canvas.drawPath(
      pico,
      Paint()..color = color.withValues(alpha: color.a * 0.55 * alfa),
    );
    if (contorno != null) {
      canvas.drawPath(
        pico,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1
          ..color = contorno!.withValues(alpha: contorno!.a * alfa),
      );
    }
    canvas.drawPath(
      forma(corpo, corpo),
      Paint()..color = color.withValues(alpha: color.a * alfa),
    );
  }

  @override
  bool shouldRepaint(ClipWaveformPainter old) =>
      old.pyramid != pyramid ||
      old.color != color ||
      old.gain != gain ||
      old.muted != muted ||
      old.fonte.length != fonte.length ||
      (fonte.isNotEmpty &&
          (old.fonte.first != fonte.first || old.fonte.last != fonte.last)) ||
      !_iguais(old.fonte, fonte);

  static bool _iguais(Float64List a, Float64List b) {
    if (identical(a, b)) return true;
    if (a.length != b.length) return false;
    for (var i = 0; i < a.length; i += math.max(1, a.length ~/ 16)) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }
}
