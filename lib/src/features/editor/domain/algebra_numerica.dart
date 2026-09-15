import 'dart:math' as math;

/// A ALGEBRA QUE O RASTREIO DE CAMERA PRECISA — e so ela.
///
/// Resolver uma camera a partir de video e, no fundo, tres contas de
/// algebra linear repetidas muitas vezes: achar o vetor do nucleo de um
/// sistema homogeneo (matriz essencial, triangulacao), separar uma
/// rotacao de uma translacao dentro de uma matriz, e resolver um sistema
/// pequeno a cada passo de refinamento.
///
/// Nao ha pacote de algebra no projeto e nao vale trazer um: o que se
/// usa cabe aqui, e cada peca abaixo e testavel sozinha — que e o unico
/// jeito de confiar num solver, porque quando ele erra o sintoma e
/// "a cena escorrega", nunca "a linha 210 esta errada".

/// Matriz 3x3, por linhas.
class Mat3 {
  const Mat3(this.m);

  /// Nove numeros em ordem de leitura: m[linha * 3 + coluna].
  final List<double> m;

  static const identidade = Mat3([1, 0, 0, 0, 1, 0, 0, 0, 1]);
  static const zero = Mat3([0, 0, 0, 0, 0, 0, 0, 0, 0]);

  double at(int linha, int coluna) => m[linha * 3 + coluna];

  Mat3 get transposta =>
      Mat3([m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]]);

  Mat3 operator *(Mat3 o) {
    final r = List<double>.filled(9, 0);
    for (var i = 0; i < 3; i++) {
      for (var j = 0; j < 3; j++) {
        var s = 0.0;
        for (var k = 0; k < 3; k++) {
          s += m[i * 3 + k] * o.m[k * 3 + j];
        }
        r[i * 3 + j] = s;
      }
    }
    return Mat3(r);
  }

  Mat3 escalada(double s) => Mat3([for (final v in m) v * s]);

  /// Aplica a matriz a um vetor de tres numeros.
  List<double> aplicar(List<double> v) => [
    m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
    m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
    m[6] * v[0] + m[7] * v[1] + m[8] * v[2],
  ];

  double get determinante =>
      m[0] * (m[4] * m[8] - m[5] * m[7]) -
      m[1] * (m[3] * m[8] - m[5] * m[6]) +
      m[2] * (m[3] * m[7] - m[4] * m[6]);

  /// Matriz do produto vetorial: `cruzada(a) * b == a x b`.
  static Mat3 cruzada(List<double> a) =>
      Mat3([0, -a[2], a[1], a[2], 0, -a[0], -a[1], a[0], 0]);

  @override
  String toString() => 'Mat3(${m.map((v) => v.toStringAsFixed(4)).join(', ')})';
}

/// Norma euclidiana.
double norma(List<double> v) {
  var s = 0.0;
  for (final x in v) {
    s += x * x;
  }
  return math.sqrt(s);
}

/// O mesmo vetor com norma 1 (ou ele mesmo, se for nulo).
List<double> normalizar(List<double> v) {
  final n = norma(v);
  if (n < 1e-15) return List<double>.from(v);
  return [for (final x in v) x / n];
}

double produtoInterno(List<double> a, List<double> b) {
  var s = 0.0;
  for (var i = 0; i < a.length && i < b.length; i++) {
    s += a[i] * b[i];
  }
  return s;
}

List<double> produtoVetorial(List<double> a, List<double> b) => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];

/// DECOMPOSICAO DE UMA MATRIZ SIMETRICA em autovalores e autovetores,
/// pelo metodo de Jacobi ciclico.
///
/// Por que Jacobi e nao algo mais moderno: e curto, nao precisa de
/// bibliotecas, e para matrizes pequenas (3x3, 4x4, 9x9 — os tamanhos
/// que aparecem aqui) converge em poucas varreduras com precisao de
/// maquina. E, principalmente, e ESTAVEL: nao explode com matrizes quase
/// singulares, que e exatamente o caso quando o video tem pouca
/// paralaxe.
///
/// Devolve os pares ordenados do MENOR autovalor para o maior — porque
/// quase todo uso aqui quer o menor (o vetor do nucleo).
({List<double> valores, List<List<double>> vetores}) autovaloresSimetrica(
  List<List<double>> entrada, {
  int varreduras = 100,
}) {
  final n = entrada.length;
  final a = [for (final linha in entrada) List<double>.from(linha)];
  // v comeca na identidade e acumula todas as rotacoes; no fim, cada
  // COLUNA de v e um autovetor.
  final v = [
    for (var i = 0; i < n; i++)
      [for (var j = 0; j < n; j++) i == j ? 1.0 : 0.0],
  ];

  // A tolerancia e RELATIVA ao tamanho da matriz. Absoluta parecia
  // funcionar e nao funcionava: A^T A de coordenadas em pixel tem
  // entradas na casa de 1e10, e 1e-14 nunca chegava — o laco gastava as
  // cem varreduras inteiras toda vez.
  var escala = 0.0;
  for (var i = 0; i < n; i++) {
    escala = math.max(escala, a[i][i].abs());
  }
  final tolerancia = math.max(1e-300, escala * 1e-14);

  for (var varredura = 0; varredura < varreduras; varredura++) {
    // Soma do que esta fora da diagonal: e o que a rotacao tem de zerar.
    var fora = 0.0;
    for (var p = 0; p < n - 1; p++) {
      for (var q = p + 1; q < n; q++) {
        fora += a[p][q].abs();
      }
    }
    if (fora < tolerancia) break;

    for (var p = 0; p < n - 1; p++) {
      for (var q = p + 1; q < n; q++) {
        if (a[p][q].abs() < 1e-300) continue;
        final theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
        // theta == 0 (dois autovalores iguais) precisa de meia volta de
        // 45 graus. Sem este caso o t da formula sai zero, a rotacao
        // vira identidade e o par nunca zera: o laco roda para sempre
        // sem mudar nada.
        final t = theta == 0
            ? 1.0
            : theta.sign / (theta.abs() + math.sqrt(theta * theta + 1));
        final c = 1 / math.sqrt(t * t + 1);
        final s = t * c;

        for (var k = 0; k < n; k++) {
          final akp = a[k][p], akq = a[k][q];
          a[k][p] = c * akp - s * akq;
          a[k][q] = s * akp + c * akq;
        }
        for (var k = 0; k < n; k++) {
          final apk = a[p][k], aqk = a[q][k];
          a[p][k] = c * apk - s * aqk;
          a[q][k] = s * apk + c * aqk;
        }
        for (var k = 0; k < n; k++) {
          final vkp = v[k][p], vkq = v[k][q];
          v[k][p] = c * vkp - s * vkq;
          v[k][q] = s * vkp + c * vkq;
        }
      }
    }
  }

  final ordem = [for (var i = 0; i < n; i++) i]
    ..sort((x, y) => a[x][x].compareTo(a[y][y]));
  return (
    valores: [for (final i in ordem) a[i][i]],
    vetores: [
      for (final i in ordem) [for (var k = 0; k < n; k++) v[k][i]],
    ],
  );
}

/// O vetor que melhor satisfaz `A x = 0` com |x| = 1.
///
/// E o autovetor do MENOR autovalor de A^T A. Formar A^T A dobra o
/// numero de condicao — para o 8 pontos isso importaria se os dados nao
/// fossem normalizados antes; como sao (e tem de ser, sempre), o ganho
/// de simplicidade compensa.
List<double> nucleo(List<List<double>> a) {
  if (a.isEmpty) return const [];
  final colunas = a.first.length;
  final ata = [
    for (var i = 0; i < colunas; i++) List<double>.filled(colunas, 0.0),
  ];
  for (final linha in a) {
    for (var i = 0; i < colunas; i++) {
      for (var j = i; j < colunas; j++) {
        ata[i][j] += linha[i] * linha[j];
      }
    }
  }
  for (var i = 0; i < colunas; i++) {
    for (var j = 0; j < i; j++) {
      ata[i][j] = ata[j][i];
    }
  }
  return autovaloresSimetrica(ata).vetores.first;
}

/// Resolve `A x = b` por eliminacao com pivo parcial.
///
/// Devolve null quando o sistema e singular — e quem chama tem de tratar
/// isso, porque num solver de camera "singular" acontece o tempo todo
/// (quadro sem movimento, ponto visto por uma camera so).
List<double>? resolverSistema(List<List<double>> entrada, List<double> b) {
  final n = entrada.length;
  if (n == 0 || b.length != n) return null;
  final a = [
    for (var i = 0; i < n; i++) [...entrada[i], b[i]],
  ];

  for (var col = 0; col < n; col++) {
    var pivo = col;
    for (var linha = col + 1; linha < n; linha++) {
      if (a[linha][col].abs() > a[pivo][col].abs()) pivo = linha;
    }
    if (a[pivo][col].abs() < 1e-14) return null;
    if (pivo != col) {
      final t = a[pivo];
      a[pivo] = a[col];
      a[col] = t;
    }
    final d = a[col][col];
    for (var j = col; j <= n; j++) {
      a[col][j] /= d;
    }
    for (var linha = 0; linha < n; linha++) {
      if (linha == col) continue;
      final f = a[linha][col];
      if (f == 0) continue;
      for (var j = col; j <= n; j++) {
        a[linha][j] -= f * a[col][j];
      }
    }
  }
  return [for (var i = 0; i < n; i++) a[i][n]];
}

/// A ROTACAO DE UM VETOR DE ROTACAO (formula de Rodrigues).
///
/// Rotacao guardada como tres numeros (eixo vezes angulo) e o que
/// permite refinar a pose com um sistema linear: tres angulos de Euler
/// travam (gimbal) e uma matriz de nove numeros tem seis restricoes
/// escondidas.
Mat3 rotacaoDeVetor(List<double> w) {
  final theta = norma(w);
  if (theta < 1e-12) return Mat3.identidade;
  final k = [w[0] / theta, w[1] / theta, w[2] / theta];
  final cruz = Mat3.cruzada(k);
  final s = math.sin(theta), c = math.cos(theta);
  final cruz2 = cruz * cruz;
  return Mat3([
    for (var i = 0; i < 9; i++)
      Mat3.identidade.m[i] + s * cruz.m[i] + (1 - c) * cruz2.m[i],
  ]);
}

/// O caminho de volta: o vetor de rotacao de uma matriz de rotacao.
List<double> vetorDeRotacao(Mat3 r) {
  final traco = r.at(0, 0) + r.at(1, 1) + r.at(2, 2);
  final cos = ((traco - 1) / 2).clamp(-1.0, 1.0);
  final theta = math.acos(cos);
  if (theta < 1e-9) return [0, 0, 0];
  if (theta > math.pi - 1e-6) {
    // Perto de 180 graus a formula do seno perde tudo; ai o eixo sai da
    // diagonal de R + I, que continua bem condicionada.
    final d = [
      math.sqrt(math.max(0, (r.at(0, 0) + 1) / 2)),
      math.sqrt(math.max(0, (r.at(1, 1) + 1) / 2)),
      math.sqrt(math.max(0, (r.at(2, 2) + 1) / 2)),
    ];
    final maior = d[0] >= d[1] && d[0] >= d[2] ? 0 : (d[1] >= d[2] ? 1 : 2);
    final eixo = List<double>.from(d);
    if (maior == 0) {
      if (r.at(0, 1) < 0) eixo[1] = -eixo[1];
      if (r.at(0, 2) < 0) eixo[2] = -eixo[2];
    } else if (maior == 1) {
      if (r.at(0, 1) < 0) eixo[0] = -eixo[0];
      if (r.at(1, 2) < 0) eixo[2] = -eixo[2];
    } else {
      if (r.at(0, 2) < 0) eixo[0] = -eixo[0];
      if (r.at(1, 2) < 0) eixo[1] = -eixo[1];
    }
    final u = normalizar(eixo);
    return [u[0] * theta, u[1] * theta, u[2] * theta];
  }
  final k = 1 / (2 * math.sin(theta));
  return [
    (r.at(2, 1) - r.at(1, 2)) * k * theta,
    (r.at(0, 2) - r.at(2, 0)) * k * theta,
    (r.at(1, 0) - r.at(0, 1)) * k * theta,
  ];
}

/// A rotacao valida mais proxima de [m].
///
/// Depois de qualquer conta numerica a matriz "de rotacao" ja nao e
/// ortogonal — e uma rotacao suja escala e cisalha a cena inteira, o que
/// aparece como objetos que incham e encolhem ao longo do plano.
/// Ortonormalizar a cada passo e barato e evita esse arrasto.
Mat3 rotacaoMaisProxima(Mat3 m) {
  final x = normalizar([m.at(0, 0), m.at(0, 1), m.at(0, 2)]);
  var y = [m.at(1, 0), m.at(1, 1), m.at(1, 2)];
  final proj = produtoInterno(x, y);
  y = normalizar([y[0] - proj * x[0], y[1] - proj * x[1], y[2] - proj * x[2]]);
  final z = produtoVetorial(x, y);
  return Mat3([x[0], x[1], x[2], y[0], y[1], y[2], z[0], z[1], z[2]]);
}

/// Mediana — usada o tempo todo para medir erro sem que um ponto
/// perdido puxe a media.
double mediana(List<double> valores) {
  if (valores.isEmpty) return 0;
  final v = [...valores]..sort();
  final meio = v.length ~/ 2;
  return v.length.isOdd ? v[meio] : (v[meio - 1] + v[meio]) / 2;
}
