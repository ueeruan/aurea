import 'dart:math' as math;
import 'dart:typed_data';

/// O BASICO DE PROCESSAMENTO DE SINAL, num lugar so.
///
/// Filtro e transformada sao as duas pecas que a sonoridade, a limpeza e
/// a melhoria de voz usam. Ter tres copias de um biquad e ter tres
/// lugares para o mesmo erro.

/// Um biquad direto forma I. Os coeficientes ja vem normalizados por a0.
class Biquad {
  const Biquad(this.b0, this.b1, this.b2, this.a1, this.a2);

  /// Passa tudo — o filtro que nao faz nada.
  static const neutro = Biquad(1, 0, 0, 0, 0);

  final double b0, b1, b2, a1, a2;

  Float32List apply(Float32List x) {
    final y = Float32List(x.length);
    var x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (var i = 0; i < x.length; i++) {
      final v = x[i];
      final o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1;
      x1 = v;
      y2 = y1;
      y1 = o;
      y[i] = o;
    }
    return y;
  }

  static Biquad _norm(double b0, double b1, double b2, double a0, double a1,
          double a2) =>
      Biquad(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0);
}

/// As formulas do livro do Robert Bristow-Johnson, que e de onde vem o
/// biquad de praticamente todo mundo.
Biquad highPass(int rate, double fc, {double q = math.sqrt1_2}) {
  final w0 = 2 * math.pi * fc / rate;
  final cos = math.cos(w0);
  final alpha = math.sin(w0) / (2 * q);
  return Biquad._norm(
      (1 + cos) / 2, -(1 + cos), (1 + cos) / 2, 1 + alpha, -2 * cos, 1 - alpha);
}

Biquad lowPass(int rate, double fc, {double q = math.sqrt1_2}) {
  final w0 = 2 * math.pi * fc / rate;
  final cos = math.cos(w0);
  final alpha = math.sin(w0) / (2 * q);
  return Biquad._norm(
      (1 - cos) / 2, 1 - cos, (1 - cos) / 2, 1 + alpha, -2 * cos, 1 - alpha);
}

Biquad highShelf(int rate, double fc, double gainDb, {double q = 0.707}) {
  if (gainDb == 0) return Biquad.neutro;
  final a = math.pow(10, gainDb / 40).toDouble();
  final w0 = 2 * math.pi * fc / rate;
  final cos = math.cos(w0);
  final alpha = math.sin(w0) / (2 * q);
  final raiz = 2 * math.sqrt(a) * alpha;
  return Biquad._norm(
    a * ((a + 1) + (a - 1) * cos + raiz),
    -2 * a * ((a - 1) + (a + 1) * cos),
    a * ((a + 1) + (a - 1) * cos - raiz),
    (a + 1) - (a - 1) * cos + raiz,
    2 * ((a - 1) - (a + 1) * cos),
    (a + 1) - (a - 1) * cos - raiz,
  );
}

Biquad lowShelf(int rate, double fc, double gainDb, {double q = 0.707}) {
  if (gainDb == 0) return Biquad.neutro;
  final a = math.pow(10, gainDb / 40).toDouble();
  final w0 = 2 * math.pi * fc / rate;
  final cos = math.cos(w0);
  final alpha = math.sin(w0) / (2 * q);
  final raiz = 2 * math.sqrt(a) * alpha;
  return Biquad._norm(
    a * ((a + 1) - (a - 1) * cos + raiz),
    2 * a * ((a - 1) - (a + 1) * cos),
    a * ((a + 1) - (a - 1) * cos - raiz),
    (a + 1) + (a - 1) * cos + raiz,
    -2 * ((a - 1) + (a + 1) * cos),
    (a + 1) + (a - 1) * cos - raiz,
  );
}

Biquad peaking(int rate, double fc, double gainDb, {double q = 1.0}) {
  if (gainDb == 0) return Biquad.neutro;
  final a = math.pow(10, gainDb / 40).toDouble();
  final w0 = 2 * math.pi * fc / rate;
  final cos = math.cos(w0);
  final alpha = math.sin(w0) / (2 * q);
  return Biquad._norm(
    1 + alpha * a,
    -2 * cos,
    1 - alpha * a,
    1 + alpha / a,
    -2 * cos,
    1 - alpha / a,
  );
}

// --------------------------------------------------------- transformada

/// FFT no lugar, radix 2. [re] e [im] tem de ter tamanho potencia de 2.
///
/// E a peca que falta para tirar ruido de verdade: sem espectro, "tirar
/// chiado" vira um portao de ruido que corta o fim das palavras.
void fft(Float64List re, Float64List im, {bool inverse = false}) {
  final n = re.length;
  if (n <= 1 || im.length != n || (n & (n - 1)) != 0) return;

  // Reordenacao por inversao de bits.
  for (var i = 1, j = 0; i < n; i++) {
    var bit = n >> 1;
    for (; j & bit != 0; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      var t = re[i];
      re[i] = re[j];
      re[j] = t;
      t = im[i];
      im[i] = im[j];
      im[j] = t;
    }
  }

  for (var len = 2; len <= n; len <<= 1) {
    final ang = 2 * math.pi / len * (inverse ? 1 : -1);
    final wRe = math.cos(ang);
    final wIm = math.sin(ang);
    for (var i = 0; i < n; i += len) {
      var curRe = 1.0, curIm = 0.0;
      for (var j = 0; j < len ~/ 2; j++) {
        final uRe = re[i + j];
        final uIm = im[i + j];
        final vRe = re[i + j + len ~/ 2] * curRe - im[i + j + len ~/ 2] * curIm;
        final vIm = re[i + j + len ~/ 2] * curIm + im[i + j + len ~/ 2] * curRe;
        re[i + j] = uRe + vRe;
        im[i + j] = uIm + vIm;
        re[i + j + len ~/ 2] = uRe - vRe;
        im[i + j + len ~/ 2] = uIm - vIm;
        final novoRe = curRe * wRe - curIm * wIm;
        curIm = curRe * wIm + curIm * wRe;
        curRe = novoRe;
      }
    }
  }

  if (inverse) {
    for (var i = 0; i < n; i++) {
      re[i] /= n;
      im[i] /= n;
    }
  }
}

/// Janela de Hann. Com salto de metade da janela ela SOMA EXATAMENTE 1 —
/// e por isso que da para desmontar o sinal em pedacos, mexer e remontar
/// sem deixar costura audivel.
Float64List hannWindow(int n) {
  final w = Float64List(n);
  for (var i = 0; i < n; i++) {
    w[i] = 0.5 - 0.5 * math.cos(2 * math.pi * i / n);
  }
  return w;
}
