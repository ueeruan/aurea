import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui';

/// DESENHO A MAO LIVRE (v1.1.1): as ferramentas de pintar dentro de uma
/// camada de desenho.
enum FerramentaDeDesenho { caneta, pincel, balde, borracha }

String rotuloDaFerramenta(FerramentaDeDesenho f) => switch (f) {
  FerramentaDeDesenho.caneta => 'Caneta',
  FerramentaDeDesenho.pincel => 'Pincel',
  FerramentaDeDesenho.balde => 'Balde',
  FerramentaDeDesenho.borracha => 'Borracha',
};

/// UM TRACO do desenho, no espaco da camada. O balde guarda aqui o
/// contorno da regiao que pintou (fechado).
class TracoDoDesenho {
  TracoDoDesenho({
    required this.ferramenta,
    required List<Offset> pontos,
    this.cor = const Color(0xFFFFFFFF),
    this.espessura = 12,
    this.dureza = 1,
    this.opacidade = 1,
  }) : pontos = List.unmodifiable(pontos);

  final FerramentaDeDesenho ferramenta;
  final List<Offset> pontos;
  final Color cor;
  final double espessura;

  /// 1 = borda dura; 0 = borda bem macia (so o pincel usa).
  final double dureza;
  final double opacidade;

  TracoDoDesenho deslocado(Offset delta) => TracoDoDesenho(
    ferramenta: ferramenta,
    pontos: [for (final p in pontos) p + delta],
    cor: cor,
    espessura: espessura,
    dureza: dureza,
    opacidade: opacidade,
  );

  Map<String, dynamic> toJson() => {
    'f': ferramenta.index,
    'c': cor.toARGB32(),
    'e': espessura,
    if (dureza != 1) 'd': dureza,
    if (opacidade != 1) 'o': opacidade,
    // Pontos achatados em x,y com uma casa: um traco longo nao vira um
    // arquivo gordo.
    'p': [
      for (final p in pontos) ...[
        double.parse(p.dx.toStringAsFixed(1)),
        double.parse(p.dy.toStringAsFixed(1)),
      ],
    ],
  };

  static TracoDoDesenho? fromJson(Object? raw) {
    if (raw is! Map) return null;
    final lista = raw['p'];
    if (lista is! List) return null;
    final pontos = <Offset>[
      for (var i = 0; i + 1 < lista.length; i += 2)
        Offset((lista[i] as num).toDouble(), (lista[i + 1] as num).toDouble()),
    ];
    final indice = (raw['f'] as num?)?.toInt() ?? 0;
    return TracoDoDesenho(
      ferramenta: FerramentaDeDesenho.values[indice.clamp(
        0,
        FerramentaDeDesenho.values.length - 1,
      )],
      pontos: pontos,
      cor: Color((raw['c'] as num?)?.toInt() ?? 0xFFFFFFFF),
      espessura: (raw['e'] as num?)?.toDouble() ?? 12,
      dureza: (raw['d'] as num?)?.toDouble() ?? 1,
      opacidade: (raw['o'] as num?)?.toDouble() ?? 1,
    );
  }
}

/// O caminho de um traco: curvas quadraticas pelos pontos medios, o que
/// tira o serrilhado do dedo sem perder o desenho.
Path caminhoDoTraco(List<Offset> pontos, {bool fechado = false}) {
  final path = Path();
  if (pontos.isEmpty) return path;
  path.moveTo(pontos.first.dx, pontos.first.dy);
  if (pontos.length == 1) {
    path.lineTo(pontos.first.dx + .01, pontos.first.dy);
    return path;
  }
  if (fechado) {
    for (final p in pontos.skip(1)) {
      path.lineTo(p.dx, p.dy);
    }
    return path..close();
  }
  for (var i = 1; i < pontos.length - 1; i++) {
    final meio = (pontos[i] + pontos[i + 1]) / 2;
    path.quadraticBezierTo(pontos[i].dx, pontos[i].dy, meio.dx, meio.dy);
  }
  path.lineTo(pontos.last.dx, pontos.last.dy);
  return path;
}

/// AS PINCELADAS DE UM TRACO: o caminho e a tinta de cada passada. O
/// pincel macio sao passadas cada vez mais finas somando a opacidade (sem
/// desfoque na GPU); a borracha tira tinta do que ja estava pintado.
List<(Path, Paint)> pinceladasDoTraco(TracoDoDesenho t) {
  final cor = t.cor.withValues(alpha: t.cor.a * t.opacidade.clamp(0.0, 1.0));
  Paint linha(double largura, Color c) => Paint()
    ..style = PaintingStyle.stroke
    ..strokeWidth = math.max(.5, largura)
    ..strokeCap = StrokeCap.round
    ..strokeJoin = StrokeJoin.round
    ..color = c;
  switch (t.ferramenta) {
    case FerramentaDeDesenho.caneta:
      return [(caminhoDoTraco(t.pontos), linha(t.espessura, cor))];
    case FerramentaDeDesenho.borracha:
      return [
        (
          caminhoDoTraco(t.pontos),
          linha(t.espessura, Color.fromRGBO(0, 0, 0, t.opacidade.clamp(0.0, 1.0)))
            ..blendMode = BlendMode.dstOut,
        ),
      ];
    case FerramentaDeDesenho.balde:
      // O contorno da regiao passa pelo meio das celulas: a borda de
      // tinta com a [espessura] da grade fecha a fresta ate os tracos.
      final regiao = caminhoDoTraco(t.pontos, fechado: true);
      return [
        (
          regiao,
          Paint()
            ..style = PaintingStyle.fill
            ..color = cor,
        ),
        (regiao, linha(t.espessura, cor)),
      ];
    case FerramentaDeDesenho.pincel:
      final dureza = t.dureza.clamp(0.0, 1.0);
      if (dureza >= .95) {
        return [(caminhoDoTraco(t.pontos), linha(t.espessura, cor))];
      }
      const passadas = 4;
      final caminho = caminhoDoTraco(t.pontos);
      // A BORDA MACIA sao passadas cada vez mais finas, da mais larga e
      // rala ate a do miolo. A tinta de cada uma e a que FALTA para a
      // soma chegar na rampa: assim o centro para exatamente na
      // opacidade pedida em vez de somar quatro vezes ela.
      final alvo = cor.a;
      var somado = 0.0;
      final saida = <(Path, Paint)>[];
      for (var k = 0; k < passadas; k++) {
        final ate = alvo * (k + 1) / passadas;
        final passo = somado >= 1 ? 0.0 : (ate - somado) / (1 - somado);
        somado = ate;
        saida.add((
          caminho,
          linha(
            t.espessura *
                (dureza + (1 - dureza) * (passadas - 1 - k) / (passadas - 1)),
            cor.withValues(alpha: passo.clamp(0.0, 1.0)),
          ),
        ));
      }
      return saida;
  }
}

/// O BALDE: a regiao vazia que contem [ponto], cercada pelos tracos ja
/// feitos e pela [area]. Rasteriza os tracos numa grade, inunda a partir
/// do ponto e devolve o contorno da mancha (fechado, simplificado). Nulo
/// quando o ponto cai em cima de tinta ou fora da area.
List<Offset>? regiaoDoBalde(
  List<TracoDoDesenho> tracos,
  Offset ponto,
  Rect area, {
  double celula = 4,
}) {
  if (!area.contains(ponto)) return null;
  final cols = (area.width / celula).ceil().clamp(1, 2000);
  final rows = (area.height / celula).ceil().clamp(1, 2000);
  final parede = Uint8List(cols * rows);

  // Marca as celulas cobertas por tinta (tracos de caneta e pincel e
  // regioes de balde; a borracha abre caminho).
  void pintar(Offset p, double raio, int valor) {
    final r = math.max(celula / 2, raio);
    final c0 = ((p.dx - r - area.left) / celula).floor().clamp(0, cols - 1);
    final c1 = ((p.dx + r - area.left) / celula).ceil().clamp(0, cols - 1);
    final r0 = ((p.dy - r - area.top) / celula).floor().clamp(0, rows - 1);
    final r1 = ((p.dy + r - area.top) / celula).ceil().clamp(0, rows - 1);
    for (var y = r0; y <= r1; y++) {
      for (var x = c0; x <= c1; x++) {
        final centro = Offset(
          area.left + (x + .5) * celula,
          area.top + (y + .5) * celula,
        );
        if ((centro - p).distance <= r) parede[y * cols + x] = valor;
      }
    }
  }

  for (final t in tracos) {
    if (t.ferramenta == FerramentaDeDesenho.balde) continue;
    final valor = t.ferramenta == FerramentaDeDesenho.borracha ? 0 : 1;
    final raio = t.espessura / 2;
    for (var i = 0; i < t.pontos.length; i++) {
      final a = t.pontos[i];
      pintar(a, raio, valor);
      if (i + 1 < t.pontos.length) {
        final b = t.pontos[i + 1];
        final passos = ((b - a).distance / (celula / 2)).ceil();
        for (var s = 1; s < passos; s++) {
          pintar(Offset.lerp(a, b, s / passos)!, raio, valor);
        }
      }
    }
  }

  final inicioX = ((ponto.dx - area.left) / celula).floor().clamp(0, cols - 1);
  final inicioY = ((ponto.dy - area.top) / celula).floor().clamp(0, rows - 1);
  if (parede[inicioY * cols + inicioX] == 1) return null;

  // Inundacao em fila (sem recursao: regioes grandes estouravam a pilha).
  final cheio = Uint8List(cols * rows);
  final fila = <int>[inicioY * cols + inicioX];
  cheio[fila.first] = 1;
  var cabeca = 0;
  while (cabeca < fila.length) {
    final i = fila[cabeca++];
    final x = i % cols, y = i ~/ cols;
    for (final (dx, dy) in const [(1, 0), (-1, 0), (0, 1), (0, -1)]) {
      final nx = x + dx, ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
      final j = ny * cols + nx;
      if (cheio[j] == 1 || parede[j] == 1) continue;
      cheio[j] = 1;
      fila.add(j);
    }
  }

  // Contorno externo da mancha (vizinhanca de Moore), no canto das celulas.
  bool dentro(int x, int y) =>
      x >= 0 && y >= 0 && x < cols && y < rows && cheio[y * cols + x] == 1;
  // A celula mais acima e a esquerda da mancha comeca o contorno.
  var sx = -1, sy = -1;
  for (var y = 0; y < rows && sx < 0; y++) {
    for (var x = 0; x < cols; x++) {
      if (cheio[y * cols + x] == 1) {
        sx = x;
        sy = y;
        break;
      }
    }
  }
  if (sx < 0) return null;
  const vizinhos = [
    (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1),
  ];
  final contorno = <Offset>[];
  var cx = sx, cy = sy;
  var direcao = 6; // vindo de cima
  final limite = cols * rows * 4;
  for (var passo = 0; passo < limite; passo++) {
    contorno.add(
      Offset(area.left + (cx + .5) * celula, area.top + (cy + .5) * celula),
    );
    var achou = false;
    for (var k = 0; k < 8; k++) {
      final d = (direcao + 6 + k) % 8;
      final (dx, dy) = vizinhos[d];
      if (dentro(cx + dx, cy + dy)) {
        cx += dx;
        cy += dy;
        direcao = d;
        achou = true;
        break;
      }
    }
    if (!achou) break;
    if (cx == sx && cy == sy && contorno.length > 2) break;
  }
  if (contorno.length < 3) {
    // Mancha de uma celula so: um quadradinho.
    final c = contorno.isEmpty
        ? Offset(area.left + (sx + .5) * celula, area.top + (sy + .5) * celula)
        : contorno.first;
    final m = celula / 2;
    return [
      c + Offset(-m, -m),
      c + Offset(m, -m),
      c + Offset(m, m),
      c + Offset(-m, m),
    ];
  }
  return _simplificar(contorno, celula * .75);
}

/// Douglas-Peucker num contorno fechado.
List<Offset> _simplificar(List<Offset> pts, double tolerancia) {
  if (pts.length < 4) return pts;
  final manter = List<bool>.filled(pts.length, false);
  manter[0] = true;
  manter[pts.length - 1] = true;
  final pilha = <(int, int)>[(0, pts.length - 1)];
  while (pilha.isNotEmpty) {
    final (a, b) = pilha.removeLast();
    var pior = 0.0;
    var indice = -1;
    final pa = pts[a], pb = pts[b];
    final ab = pb - pa;
    final comprimento = ab.distance;
    for (var i = a + 1; i < b; i++) {
      final ap = pts[i] - pa;
      final d = comprimento < 1e-9
          ? ap.distance
          : (ab.dx * ap.dy - ab.dy * ap.dx).abs() / comprimento;
      if (d > pior) {
        pior = d;
        indice = i;
      }
    }
    if (indice >= 0 && pior > tolerancia) {
      manter[indice] = true;
      pilha
        ..add((a, indice))
        ..add((indice, b));
    }
  }
  return [
    for (var i = 0; i < pts.length; i++)
      if (manter[i]) pts[i],
  ];
}
