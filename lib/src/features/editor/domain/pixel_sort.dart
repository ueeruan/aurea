import 'dart:math' as math;
import 'dart:typed_data';

/// PIXEL SORTING DE VERDADE — a conta, sem Flutter.
///
/// O efeito classico (Kim Asendorf): em cada linha, os trechos onde o
/// brilho passa de um limiar sao ORDENADOS por luminancia. E isso que
/// faz um rosto escorrer em faixas horizontais e o cabelo de uma
/// estatua derreter para baixo — o escorrido nasce da propria imagem,
/// onde ela e clara, e nao de ruido sorteado.
///
/// Tudo aqui trabalha num buffer RGBA pequeno, em CPU. Ordenar exige
/// ler os pixels, e ler pixel na GPU nao existe em passe unico; por isso
/// a composicao entrega o quadro reduzido, esta conta ordena, e o
/// resultado volta como imagem. Em 360 px de lado sao poucos
/// milissegundos.
enum PixelSortMode { linear, radial, circular }

/// O que decide a ordem dentro do trecho.
enum PixelSortKey { luminance, hue, saturation }

class PixelSortSpec {
  const PixelSortSpec({
    this.mode = PixelSortMode.linear,
    this.threshold = 0.3,
    this.above = true,
    this.reverse = false,
    this.key = PixelSortKey.luminance,
    this.maxRun = 0.8,
    this.restart = 0,
    this.seed = 0,
    this.matteBlur = 0,
    this.centerX = 0.5,
    this.centerY = 0.5,
    this.startAngle = 0,
    this.degreesSorted = 360,
    this.innerRadius = 0.1,
    this.radiusVariation = 0.1,
    this.startVariation = 0.15,
    this.thickness = 1.1,
  });

  final PixelSortMode mode;

  /// Limiar em 0..1 sobre a chave.
  final double threshold;

  /// Verdadeiro: ordena o que esta ACIMA do limiar (o claro escorre).
  /// Falso: o que esta abaixo (o escuro escorre). Sao imagens
  /// diferentes, e e por isso que a direcao existe.
  final bool above;
  final bool reverse;
  final PixelSortKey key;

  /// Comprimento maximo de um trecho, em fracao do comprimento da linha.
  final double maxRun;

  /// Reinicios aleatorios: 0..100, virando probabilidade por pixel
  /// (restart / 1000). Quebra trechos longos em pedacos de tamanho
  /// variado — a textura de "chuva" das referencias vem daqui.
  final double restart;
  final int seed;

  /// Raio (em pixels do buffer) do desfoque 1D sobre a chave ANTES do
  /// limiar. Suaviza a borda dos trechos: sem ele, ruido de compressao
  /// abre e fecha trechos a cada pixel.
  final int matteBlur;

  // Modos polares.
  final double centerX;
  final double centerY;
  final double startAngle;
  final double degreesSorted;
  final double innerRadius;
  final double radiusVariation;
  final double startVariation;
  final double thickness;
}

/// A chave de um pixel, em 0..1.
double pixelSortKeyOf(int r, int g, int b, PixelSortKey key) {
  switch (key) {
    case PixelSortKey.luminance:
      return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0;
    case PixelSortKey.saturation:
      final mx = math.max(r, math.max(g, b));
      if (mx == 0) return 0;
      final mn = math.min(r, math.min(g, b));
      return (mx - mn) / mx;
    case PixelSortKey.hue:
      final mx = math.max(r, math.max(g, b));
      final mn = math.min(r, math.min(g, b));
      final d = (mx - mn).toDouble();
      if (d == 0) return 0;
      double h;
      if (mx == r) {
        h = ((g - b) / d) % 6;
      } else if (mx == g) {
        h = (b - r) / d + 2;
      } else {
        h = (r - g) / d + 4;
      }
      if (h < 0) h += 6;
      return h / 6;
  }
}

/// Ruido inteiro deterministico em 0..1: funcao pura de (semente, a, b).
double _ruido(int seed, int a, int b) {
  var h = seed * 374761393;
  h += a * 668265263;
  h ^= b * 2246822519;
  h = (h ^ (h >> 13)) * 1274126177;
  return ((h ^ (h >> 16)) & 0x7fffffff) / 0x7fffffff;
}

/// ORDENA UMA LINHA de [n] pixels dentro de [rgba], a partir de [inicio]
/// (indice do primeiro byte), com passo [passo] bytes entre pixels.
///
/// Escrito assim para servir tanto a uma linha horizontal (passo 4)
/// quanto a uma coluna do buffer polar (passo = largura * 4). O buffer
/// de trabalho [tmp] e [idx] sao reaproveitados entre chamadas: alocar
/// por linha seria lixo por quadro.
void _ordenaLinha(
  Uint8List rgba,
  int inicio,
  int passo,
  int n,
  int linhaId,
  PixelSortSpec spec,
  Float32List chave,
  Float32List chaveMatte,
  Uint8List tmp,
  List<int> idx, {
  int primeiroValido = 0,
}) {
  if (n <= 1) return;

  // 1. A chave de cada pixel. Alfa zero = fora da imagem (borda de uma
  //    rotacao, fora do circulo): nunca entra num trecho.
  for (var i = 0; i < n; i++) {
    final p = inicio + i * passo;
    final a = rgba[p + 3];
    chave[i] = a < 8
        ? -1
        : pixelSortKeyOf(rgba[p], rgba[p + 1], rgba[p + 2], spec.key);
  }

  // 2. A chave que decide o LIMIAR pode ser suavizada; a que decide a
  //    ORDEM continua crua, senao a ordenacao vira um degrade morto.
  final raio = spec.matteBlur;
  if (raio > 0) {
    for (var i = 0; i < n; i++) {
      var soma = 0.0;
      var cont = 0;
      for (var j = i - raio; j <= i + raio; j++) {
        if (j < 0 || j >= n || chave[j] < 0) continue;
        soma += chave[j];
        cont++;
      }
      chaveMatte[i] = cont == 0 ? -1 : soma / cont;
    }
  } else {
    for (var i = 0; i < n; i++) {
      chaveMatte[i] = chave[i];
    }
  }

  final limite = math.max(1, (spec.maxRun.clamp(0.0, 1.0) * n).round());
  final pReinicio = (spec.restart.clamp(0.0, 100.0) / 1000.0);

  bool dentro(int i) {
    final k = chaveMatte[i];
    if (k < 0 || i < primeiroValido) return false;
    return spec.above ? k >= spec.threshold : k <= spec.threshold;
  }

  var i = 0;
  while (i < n) {
    if (!dentro(i)) {
      i++;
      continue;
    }
    // O trecho vai ate sair do limiar, bater no comprimento maximo ou
    // cair num reinicio aleatorio.
    var fim = i;
    while (fim < n && dentro(fim) && fim - i < limite) {
      if (pReinicio > 0 &&
          fim > i &&
          _ruido(spec.seed, linhaId, fim) < pReinicio) {
        break;
      }
      fim++;
    }
    final len = fim - i;
    if (len > 1) {
      // Ordena os indices pela chave (estavel: empate mantem a ordem).
      for (var k = 0; k < len; k++) {
        idx[k] = i + k;
      }
      final lista = idx.sublist(0, len);
      lista.sort((p, q) {
        final c = chave[p].compareTo(chave[q]);
        return c != 0 ? c : p.compareTo(q);
      });
      if (spec.reverse) {
        for (var k = 0; k < len ~/ 2; k++) {
          final t = lista[k];
          lista[k] = lista[len - 1 - k];
          lista[len - 1 - k] = t;
        }
      }
      // Copia os pixels na nova ordem por cima do trecho.
      for (var k = 0; k < len; k++) {
        final src = inicio + lista[k] * passo;
        tmp[k * 4] = rgba[src];
        tmp[k * 4 + 1] = rgba[src + 1];
        tmp[k * 4 + 2] = rgba[src + 2];
        tmp[k * 4 + 3] = rgba[src + 3];
      }
      for (var k = 0; k < len; k++) {
        final dst = inicio + (i + k) * passo;
        rgba[dst] = tmp[k * 4];
        rgba[dst + 1] = tmp[k * 4 + 1];
        rgba[dst + 2] = tmp[k * 4 + 2];
        rgba[dst + 3] = tmp[k * 4 + 3];
      }
    }
    i = math.max(fim, i + 1);
  }
}

/// MODO LINEAR: ordena cada LINHA de um buffer RGBA [w] x [h], no lugar.
///
/// O angulo nao entra aqui: quem quer ordenar na vertical ou na diagonal
/// gira a imagem antes de entregar o buffer e desgira ao desenhar — a
/// GPU faz isso de graca, e esta conta continua uma so.
Uint8List pixelSortRows(Uint8List rgba, int w, int h, PixelSortSpec spec) {
  if (w <= 0 || h <= 0 || rgba.length < w * h * 4) return rgba;
  final chave = Float32List(w);
  final chaveMatte = Float32List(w);
  final tmp = Uint8List(w * 4);
  final idx = List<int>.filled(w, 0);
  for (var y = 0; y < h; y++) {
    _ordenaLinha(rgba, y * w * 4, 4, w, y, spec, chave, chaveMatte, tmp, idx);
  }
  return rgba;
}

/// MODOS POLARES: remapeia para (angulo, raio), ordena, e devolve.
///
/// Radial ordena ao longo de cada RAIO (o escorrido sai do centro);
/// circular ordena ao longo de cada ANEL (gira em volta dele). O que
/// esta fora da janela angular ou dentro do raio interno fica intacto —
/// copiado do original, nunca reamostrado duas vezes.
Uint8List pixelSortPolar(Uint8List rgba, int w, int h, PixelSortSpec spec) {
  if (w <= 0 || h <= 0 || rgba.length < w * h * 4) return rgba;
  final cx = spec.centerX * w;
  final cy = spec.centerY * h;
  // Raio maximo: ate o canto mais longe, para cobrir a imagem inteira.
  double distCanto(double x, double y) {
    final dx = x - cx, dy = y - cy;
    return math.sqrt(dx * dx + dy * dy);
  }

  final maxR = [
    distCanto(0, 0),
    distCanto(w.toDouble(), 0),
    distCanto(0, h.toDouble()),
    distCanto(w.toDouble(), h.toDouble()),
  ].reduce(math.max);
  if (maxR < 2) return rgba;

  final passoR = spec.mode == PixelSortMode.circular
      ? math.max(1.0, spec.thickness)
      : 1.0;
  final nr = (maxR / passoR).ceil().clamp(2, 4096);
  final na = (2 * math.pi * maxR).ceil().clamp(16, 2048);

  // 1. Amostra o buffer polar (nearest): linha = angulo, coluna = raio.
  final polar = Uint8List(na * nr * 4);
  for (var ia = 0; ia < na; ia++) {
    final ang = ia / na * 2 * math.pi;
    final ca = math.cos(ang), sa = math.sin(ang);
    for (var ir = 0; ir < nr; ir++) {
      final r = ir * passoR;
      final x = (cx + ca * r).floor();
      final y = (cy + sa * r).floor();
      final o = (ia * nr + ir) * 4;
      if (x < 0 || y < 0 || x >= w || y >= h) {
        polar[o + 3] = 0; // fora da imagem: alfa zero, nunca entra
        continue;
      }
      final s = (y * w + x) * 4;
      polar[o] = rgba[s];
      polar[o + 1] = rgba[s + 1];
      polar[o + 2] = rgba[s + 2];
      polar[o + 3] = rgba[s + 3];
    }
  }

  // 2. A janela angular e o raio interno.
  final ini = spec.startAngle * math.pi / 180;
  final abertura = spec.degreesSorted.clamp(0.0, 360.0) * math.pi / 180;
  bool noArco(double ang) {
    var d = (ang - ini) % (2 * math.pi);
    if (d < 0) d += 2 * math.pi;
    return d <= abertura;
  }

  final rInterno = spec.innerRadius.clamp(0.0, 1.0) * math.min(w, h) / 2;

  if (spec.mode == PixelSortMode.radial) {
    final chave = Float32List(nr);
    final chaveMatte = Float32List(nr);
    final tmp = Uint8List(nr * 4);
    final idx = List<int>.filled(nr, 0);
    for (var ia = 0; ia < na; ia++) {
      if (!noArco(ia / na * 2 * math.pi)) continue;
      // O raio interno treme por raio: e o "radius variation" da ficha.
      final jitter = 1 +
          spec.radiusVariation.clamp(0.0, 1.0) * (_ruido(spec.seed, ia, 7) - 0.5) * 2;
      final primeiro = ((rInterno * jitter) / passoR).round().clamp(0, nr);
      _ordenaLinha(polar, ia * nr * 4, 4, nr, ia, spec, chave, chaveMatte,
          tmp, idx,
          primeiroValido: primeiro);
    }
  } else {
    final chave = Float32List(na);
    final chaveMatte = Float32List(na);
    final tmp = Uint8List(na * 4);
    final idx = List<int>.filled(na, 0);
    for (var ir = 0; ir < nr; ir++) {
      if (ir * passoR < rInterno) continue;
      // Cada anel comeca num angulo proprio quando ha "start variation":
      // aneis todos alinhados denunciam a grade.
      final desloc = spec.startVariation.clamp(0.0, 1.0) *
          _ruido(spec.seed, ir, 11) *
          na;
      final inicioLinha = ((ini / (2 * math.pi)) * na + desloc).round() % na;
      final quantos = (abertura / (2 * math.pi) * na).round().clamp(0, na);
      if (quantos < 2) continue;
      // Copia o arco para uma linha continua, ordena, devolve: assim a
      // coluna circular (que da a volta) vira uma linha comum.
      final arco = Uint8List(quantos * 4);
      for (var k = 0; k < quantos; k++) {
        final ia = (inicioLinha + k) % na;
        final o = (ia * nr + ir) * 4;
        arco[k * 4] = polar[o];
        arco[k * 4 + 1] = polar[o + 1];
        arco[k * 4 + 2] = polar[o + 2];
        arco[k * 4 + 3] = polar[o + 3];
      }
      _ordenaLinha(arco, 0, 4, quantos, ir, spec, chave, chaveMatte, tmp, idx);
      for (var k = 0; k < quantos; k++) {
        final ia = (inicioLinha + k) % na;
        final o = (ia * nr + ir) * 4;
        polar[o] = arco[k * 4];
        polar[o + 1] = arco[k * 4 + 1];
        polar[o + 2] = arco[k * 4 + 2];
        polar[o + 3] = arco[k * 4 + 3];
      }
    }
  }

  // 3. De volta ao cartesiano, SO onde houve ordenacao.
  final out = Uint8List.fromList(rgba);
  for (var y = 0; y < h; y++) {
    for (var x = 0; x < w; x++) {
      final dx = x + 0.5 - cx, dy = y + 0.5 - cy;
      final r = math.sqrt(dx * dx + dy * dy);
      if (r < rInterno) continue;
      var ang = math.atan2(dy, dx);
      if (ang < 0) ang += 2 * math.pi;
      if (!noArco(ang)) continue;
      final ir = (r / passoR).round();
      final ia = ((ang / (2 * math.pi)) * na).round() % na;
      if (ir < 0 || ir >= nr) continue;
      final o = (ia * nr + ir) * 4;
      if (polar[o + 3] < 8) continue;
      final d = (y * w + x) * 4;
      out[d] = polar[o];
      out[d + 1] = polar[o + 1];
      out[d + 2] = polar[o + 2];
      out[d + 3] = polar[o + 3];
    }
  }
  return out;
}

/// O MATTE DO LIMIAR, para o modo de diagnostico: branco onde o efeito
/// age, preto onde nao age. E como se descobre por que nao pegou.
Uint8List pixelSortMatte(Uint8List rgba, int w, int h, PixelSortSpec spec) {
  final out = Uint8List(w * h * 4);
  for (var p = 0; p < w * h; p++) {
    final o = p * 4;
    final a = rgba[o + 3];
    final k = a < 8
        ? -1.0
        : pixelSortKeyOf(rgba[o], rgba[o + 1], rgba[o + 2], spec.key);
    final dentro =
        k >= 0 && (spec.above ? k >= spec.threshold : k <= spec.threshold);
    final v = dentro ? 255 : 0;
    out[o] = v;
    out[o + 1] = v;
    out[o + 2] = v;
    out[o + 3] = 255;
  }
  return out;
}

/// A entrada unica, pelo modo.
Uint8List pixelSort(Uint8List rgba, int w, int h, PixelSortSpec spec) =>
    spec.mode == PixelSortMode.linear
        ? pixelSortRows(rgba, w, h, spec)
        : pixelSortPolar(rgba, w, h, spec);
