import 'dart:math' as math;

/// DESLOCAMENTO DE MATIZ, SATURACAO E BRILHO DE UMA UNIDADE DE TEXTO.
///
/// Isto era um `TextStyle` novo por letra. Passou a ser MATRIZ DE COR por
/// causa da escrita cursiva: o paragrafo do texto animado virou a PALAVRA
/// (uma letra arabe desenhada sozinha perde a ligacao), e trocar a cor do
/// estilo exigiria um paragrafo por (palavra, cor) — refazer o paragrafo a
/// cada quadro desfaz a juncao. A matriz desenha a MESMA palavra ja
/// moldada, com outra cor.
///
/// Devolve nulo quando nao ha nada a fazer. Esse e o caso comum — texto sem
/// animacao de cor — e e o unico que nao paga uma camada a mais por letra.
///
/// As tres contas sao matrizes 3x3 puras compostas numa so; o alfa entra na
/// ultima linha. Os pesos de luminancia sao os do resto do app
/// (0,2126 / 0,7152 / 0,0722).
List<double>? matrizDaUnidade(
  double hueDeg,
  double satPct,
  double brightPct,
  double alfa,
) {
  final neutro =
      hueDeg == 0 && satPct == 100 && brightPct == 100 && alfa >= 0.999;
  if (neutro) return null;

  var m = _identidade();

  if (hueDeg != 0) {
    final a = hueDeg * math.pi / 180;
    final c = math.cos(a), s = math.sin(a);
    const lr = 0.2126, lg = 0.7152, lb = 0.0722;
    m = _compor(<double>[
      lr + c * (1 - lr) - s * lr, lg - c * lg - s * lg,
      lb - c * lb + s * (1 - lb), 0, 0, //
      lr - c * lr + s * 0.143, lg + c * (1 - lg) + s * 0.140,
      lb - c * lb - s * 0.283, 0, 0, //
      lr - c * lr - s * (1 - lr), lg - c * lg + s * lg,
      lb + c * (1 - lb) + s * lb, 0, 0, //
      0, 0, 0, 1, 0,
    ], m);
  }

  final s = (satPct / 100).clamp(0.0, 4.0);
  if ((s - 1).abs() > 1e-6) {
    const lr = 0.2126, lg = 0.7152, lb = 0.0722;
    final ir = (1 - s) * lr, ig = (1 - s) * lg, ib = (1 - s) * lb;
    m = _compor(<double>[
      ir + s, ig, ib, 0, 0, //
      ir, ig + s, ib, 0, 0, //
      ir, ig, ib + s, 0, 0, //
      0, 0, 0, 1, 0,
    ], m);
  }

  final b = brightPct / 100;
  if ((b - 1).abs() > 1e-6) {
    m = _compor(<double>[
      b, 0, 0, 0, 0, //
      0, b, 0, 0, 0, //
      0, 0, b, 0, 0, //
      0, 0, 0, 1, 0,
    ], m);
  }

  if (alfa < 0.999) {
    m = _compor(<double>[
      1, 0, 0, 0, 0, //
      0, 1, 0, 0, 0, //
      0, 0, 1, 0, 0, //
      0, 0, 0, alfa.clamp(0.0, 1.0), 0,
    ], m);
  }
  return m;
}

List<double> _identidade() => <double>[
  1, 0, 0, 0, 0, //
  0, 1, 0, 0, 0, //
  0, 0, 1, 0, 0, //
  0, 0, 0, 1, 0,
];

/// [b] depois de [a], nas matrizes 4x5 do `ColorFilter.matrix`.
List<double> _compor(List<double> b, List<double> a) {
  final out = List<double>.filled(20, 0);
  for (var i = 0; i < 4; i++) {
    for (var j = 0; j < 5; j++) {
      var v = j == 4 ? b[i * 5 + 4] : 0.0;
      for (var k = 0; k < 4; k++) {
        v += b[i * 5 + k] * a[k * 5 + j];
      }
      out[i * 5 + j] = v;
    }
  }
  return out;
}
