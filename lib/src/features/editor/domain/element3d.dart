import 'dart:math' as math;

/// Elementos 3D nativos: solidos gerados por codigo (nenhum asset
/// externo), girados de verdade no espaco — vertices rotacionados e
/// projetados por face, nunca um "cartao" inclinado. A malha e funcao
/// pura do tipo: mesmo kind -> mesma malha (cache estatico).
enum Element3DKind {
  cube,
  pyramid,
  cone,
  sphere,
  cylinder,
  prism,
  diamond,
  torus,
  star,
  plane,
  capsule,
  tube,
  octahedron,
  wedge,
  dome,
  crown,
  crownFine,
}

/// AMBIENTE que os objetos refletem — e que colore o reflexo.
///
/// Um mapa procedural, sem um pixel de textura: ceu, chao e horizonte,
/// mais o que cada ambiente tem de caracteristico. No estudio e a
/// SOFTBOX — a faixa clara e larga que, refletida numa superficie, e o
/// que o olho le como "metal". No neon, as duas faixas de cor no
/// horizonte. Sem ambiente nao ha reflexo: espelho de nada e preto.
/// [ceu] e mantido no mesmo indice para compatibilidade com projetos antigos.
/// A grade atual de presets usa os outros seis ambientes.
enum EnvironmentKind { estudio, ceu, porDoSol, neon, noite, branco, interior }

String environmentLabel(EnvironmentKind k) => switch (k) {
  EnvironmentKind.estudio => 'Estudio',
  EnvironmentKind.ceu => 'Ceu',
  EnvironmentKind.porDoSol => 'Por do sol',
  EnvironmentKind.neon => 'Neon',
  EnvironmentKind.noite => 'Noite',
  EnvironmentKind.branco => 'Branco',
  EnvironmentKind.interior => 'Interior',
};

(double, double, double) _mistura(
  (double, double, double) a,
  (double, double, double) b,
  double t,
) => (
  a.$1 + (b.$1 - a.$1) * t,
  a.$2 + (b.$2 - a.$2) * t,
  a.$3 + (b.$3 - a.$3) * t,
);

/// Cor do ambiente na direcao (dx, dy, dz), com Y PARA CIMA. Devolve
/// (r, g, b) — pode passar de 1 no brilho de uma luz, e o tonemap
/// depois comprime.
///
/// [sun*] e uma luz forte opcional (a direcional da cena): o reflexo
/// dela e o pontinho de brilho que corre pela superficie quando o
/// objeto gira. [sunSharp] concentra (superficie lisa) ou espalha
/// (rugosa) esse ponto.
(double, double, double) environmentColor(
  EnvironmentKind kind,
  double dx,
  double dy,
  double dz, {
  double sunX = 0,
  double sunY = 0,
  double sunZ = 0,
  double sunR = 1,
  double sunG = 1,
  double sunB = 1,
  double sunSharp = 60,
  double sunGain = 0,
}) {
  final len = math.sqrt(dx * dx + dy * dy + dz * dz);
  if (len < 1e-9) return (0.3, 0.3, 0.3);
  final x = dx / len, y = dy / len, z = dz / len;

  final (double, double, double) topo, horizonte, chao;
  var faixa = (0.0, 0.0, 0.0);
  switch (kind) {
    case EnvironmentKind.estudio:
      topo = (0.58, 0.60, 0.64);
      horizonte = (0.42, 0.43, 0.46);
      chao = (0.14, 0.14, 0.15);
    case EnvironmentKind.ceu:
      topo = (0.30, 0.52, 0.95);
      horizonte = (0.82, 0.89, 0.98);
      chao = (0.30, 0.26, 0.20);
      faixa = (0.10, 0.08, 0.02);
    case EnvironmentKind.porDoSol:
      topo = (0.16, 0.12, 0.38);
      horizonte = (1.00, 0.52, 0.22);
      chao = (0.10, 0.07, 0.09);
      faixa = (0.35, 0.12, 0.00);
    case EnvironmentKind.neon:
      topo = (0.04, 0.02, 0.10);
      horizonte = (0.10, 0.06, 0.18);
      chao = (0.03, 0.02, 0.06);
    case EnvironmentKind.noite:
      topo = (0.015, 0.025, 0.075);
      horizonte = (0.07, 0.11, 0.20);
      chao = (0.012, 0.014, 0.025);
      faixa = (0.03, 0.05, 0.10);
    case EnvironmentKind.branco:
      topo = (1.05, 1.05, 1.05);
      horizonte = (0.92, 0.92, 0.92);
      chao = (0.68, 0.68, 0.68);
    case EnvironmentKind.interior:
      topo = (0.42, 0.34, 0.25);
      horizonte = (0.68, 0.52, 0.34);
      chao = (0.12, 0.09, 0.07);
      faixa = (0.22, 0.12, 0.04);
  }

  final t = y.abs();
  var cor = y >= 0
      ? _mistura(horizonte, topo, math.pow(t, 0.7).toDouble())
      : _mistura(horizonte, chao, math.pow(t, 0.6).toDouble());

  // Faixa do horizonte: onde o ceu encosta no chao ha sempre um brilho.
  final banda = math.exp(-(y * y) / (2 * 0.10 * 0.10));
  var r = cor.$1 + faixa.$1 * banda;
  var g = cor.$2 + faixa.$2 * banda;
  var b = cor.$3 + faixa.$3 * banda;

  switch (kind) {
    case EnvironmentKind.estudio:
      // A softbox: um retangulo claro, alto, um pouco a esquerda.
      final wy = math.exp(-math.pow((y - 0.50) / 0.16, 2).toDouble());
      final wx = math.exp(-math.pow((x + 0.25) / 0.34, 2).toDouble());
      final w = wy * wx * (z > -0.2 ? 1.0 : 0.35);
      r += 0.95 * w;
      g += 0.95 * w;
      b += 0.98 * w;
    case EnvironmentKind.neon:
      // Cian de um lado, magenta do outro, as duas coladas no horizonte.
      final w = math.exp(-(y * y) / (2 * 0.16 * 0.16));
      final lado = (x + 1) / 2; // 0 = esquerda (magenta), 1 = direita (cian)
      r += w * (0.95 * (1 - lado) + 0.10 * lado);
      g += w * (0.15 * (1 - lado) + 0.85 * lado);
      b += w * (0.75 * (1 - lado) + 0.95 * lado);
    case EnvironmentKind.ceu:
    case EnvironmentKind.porDoSol:
    case EnvironmentKind.noite:
    case EnvironmentKind.branco:
    case EnvironmentKind.interior:
      break;
  }

  if (sunGain > 0) {
    final sl = math.sqrt(sunX * sunX + sunY * sunY + sunZ * sunZ);
    if (sl > 1e-9) {
      final d = (x * sunX + y * sunY + z * sunZ) / sl;
      if (d > 0) {
        final spot = math.pow(d, sunSharp).toDouble() * sunGain;
        r += sunR * spot;
        g += sunG * spot;
        b += sunB * spot;
      }
    }
  }
  return (r, g, b);
}

/// Malha em coordenadas unitarias (meia-extensao ~1). O pintor escala
/// pelo tamanho da camada e projeta com a focal padrao do app (1200).
class Element3DMesh {
  Element3DMesh(this.verts, this.faces);

  /// Cada vertice e [x, y, z]; y positivo desce (convencao de tela).
  final List<List<double>> verts;

  /// Cada face e uma lista de indices (poligono plano).
  final List<List<int>> faces;
}

final Map<Element3DKind, Element3DMesh> _meshCache = {};

Element3DMesh element3DMesh(Element3DKind kind) =>
    _meshCache[kind] ??= _build(kind);

Element3DMesh _build(Element3DKind kind) {
  switch (kind) {
    case Element3DKind.cube:
      return _cuboChanfrado();

    case Element3DKind.pyramid:
      return Element3DMesh(
        [
          [0, -1.1, 0],
          [-1, 1, -1],
          [1, 1, -1],
          [1, 1, 1],
          [-1, 1, 1],
        ],
        [
          [1, 2, 3, 4],
          [0, 1, 2],
          [0, 2, 3],
          [0, 3, 4],
          [0, 4, 1],
        ],
      );

    case Element3DKind.cone:
      return _lathe(
        segments: 24,
        apex: const [0.0, -1.1, 0.0],
        ringY: 1,
        ringR: 1,
        withCap: true,
      );

    case Element3DKind.sphere:
      return _sphere(stacks: 10, slices: 16);

    case Element3DKind.cylinder:
      return _cylinder(segments: 20);

    case Element3DKind.prism:
      return _extrude(
        outline: const [
          [0.0, -1.0],
          [1.0, 1.0],
          [-1.0, 1.0],
        ],
        halfDepth: 0.8,
      );

    case Element3DKind.diamond:
      return _diamond();

    case Element3DKind.torus:
      return _torus(major: 0.72, minor: 0.3, around: 18, tube: 10);

    case Element3DKind.star:
      final pts = <List<double>>[];
      for (var i = 0; i < 10; i++) {
        final r = i.isEven ? 1.0 : 0.45;
        final a = -math.pi / 2 + i * math.pi / 5;
        pts.add([r * math.cos(a), r * math.sin(a)]);
      }
      return _extrude(outline: pts, halfDepth: 0.28);

    case Element3DKind.plane:
      return _plane();

    case Element3DKind.capsule:
      return _capsule(slices: 18, arcos: 5);

    case Element3DKind.tube:
      return _tube(segments: 24, inner: 0.62);

    case Element3DKind.crown:
      return crownMesh(vale: 0.32);
    case Element3DKind.crownFine:
      return crownMesh(segments: 120, inner: 0.985, vale: 0.4, altura: 0.36);

    case Element3DKind.octahedron:
      return Element3DMesh(
        [
          [0, -1.15, 0],
          [0, 1.15, 0],
          [-1, 0, 0],
          [0, 0, -1],
          [1, 0, 0],
          [0, 0, 1],
        ],
        [
          [0, 2, 3],
          [0, 3, 4],
          [0, 4, 5],
          [0, 5, 2],
          [1, 3, 2],
          [1, 4, 3],
          [1, 5, 4],
          [1, 2, 5],
        ],
      );

    case Element3DKind.wedge:
      // Uma rampa: triangulo retangulo extrudado.
      return _extrude(
        outline: const [
          [-1.0, 1.0],
          [1.0, 1.0],
          [1.0, -1.0],
        ],
        halfDepth: 1.0,
      );

    case Element3DKind.dome:
      return _dome(slices: 18, arcos: 6);
  }
}

/// PLANO: um cartao. E a forma que mais recebe imagem — uma foto, um
/// logo, uma tela. Duas faces em sentidos opostos, porque cada lado
/// precisa da SUA normal: o descarte de costas deixa passar a que olha
/// para a camera e some com a outra.
Element3DMesh _plane() => Element3DMesh(
  [
    [-1, -1, 0],
    [1, -1, 0],
    [1, 1, 0],
    [-1, 1, 0],
  ],
  [
    [0, 1, 2, 3],
    [3, 2, 1, 0],
  ],
);

/// SOLIDO DE REVOLUCAO: um perfil (raio, y) girado em torno de Y.
/// Raio zero vira polo (um vertice so); os aneis vizinhos viram quads
/// ou, contra o polo, triangulos.
Element3DMesh _revolve(
  List<List<double>> perfil,
  int slices, {
  bool tampaInicio = false,
  bool tampaFim = false,
}) {
  final verts = <List<double>>[];
  final aneis = <List<int>>[];
  for (final p in perfil) {
    final r = p[0], y = p[1];
    if (r.abs() < 1e-9) {
      aneis.add([verts.length]);
      verts.add([0, y, 0]);
    } else {
      final anel = <int>[];
      for (var i = 0; i < slices; i++) {
        final a = 2 * math.pi * i / slices;
        anel.add(verts.length);
        verts.add([r * math.cos(a), y, r * math.sin(a)]);
      }
      aneis.add(anel);
    }
  }
  final faces = <List<int>>[];
  for (var k = 0; k + 1 < aneis.length; k++) {
    final a = aneis[k], b = aneis[k + 1];
    if (a.length == 1 && b.length == 1) continue;
    for (var i = 0; i < slices; i++) {
      final j = (i + 1) % slices;
      if (a.length == 1) {
        faces.add([a[0], b[j], b[i]]);
      } else if (b.length == 1) {
        faces.add([a[i], a[j], b[0]]);
      } else {
        faces.add([a[i], a[j], b[j], b[i]]);
      }
    }
  }
  if (tampaInicio && aneis.first.length > 1) {
    faces.add([for (final i in aneis.first) i]);
  }
  if (tampaFim && aneis.last.length > 1) {
    faces.add([for (final i in aneis.last) i]);
  }
  return Element3DMesh(verts, faces);
}

/// CAPSULA: cilindro com as duas pontas em meia esfera.
Element3DMesh _capsule({required int slices, required int arcos}) {
  const raio = 0.55;
  const meio = 0.45;
  final perfil = <List<double>>[];
  for (var k = 0; k <= arcos; k++) {
    final a = math.pi / 2 * k / arcos;
    perfil.add([raio * math.sin(a), -meio - raio * math.cos(a)]);
  }
  for (var k = arcos; k >= 0; k--) {
    final a = math.pi / 2 * k / arcos;
    perfil.add([raio * math.sin(a), meio + raio * math.cos(a)]);
  }
  return _revolve(perfil, slices);
}

/// CUPULA: meia esfera com a base fechada, centrada na propria altura.
Element3DMesh _dome({required int slices, required int arcos}) {
  // Polo em -0.5 (cima), base em +0.5: centrada na propria altura.
  final perfil = <List<double>>[
    for (var k = 0; k <= arcos; k++)
      [
        math.sin(math.pi / 2 * k / arcos),
        0.5 - math.cos(math.pi / 2 * k / arcos),
      ],
  ];
  return _revolve(perfil, slices, tampaFim: true);
}

/// TUBO: cilindro oco. Parede de fora, parede de dentro e os dois aneis.
/// A COROA — um tubo cuja borda de cima sobe e desce em dentes.
///
/// Nao e um solido de ocasiao: e o tubo com a aresta superior modulada.
/// Com [dentes] em 0 ela volta a ser um tubo; em 5 e a coroa; em 20, um
/// anel serrilhado. O que faltava para desenhar coroa, ameia e engrenagem
/// em 3D era exatamente isto.
///
/// [vale] e a altura da parte baixa entre as pontas, de 0 (dente ate o
/// chao) a 1 (sem dente).
Element3DMesh crownMesh({
  int segments = 48,
  double inner = 0.82,
  int dentes = 5,
  double vale = 0.5,
  double altura = 0.55,
}) {
  if (dentes <= 0) return _tube(segments: segments, inner: inner);
  final n = math.max(12, segments);

  /// A altura do topo neste angulo. O dente e triangular, nao senoidal:
  /// a referencia tem pontas retas, e o seno daria uma onda.
  double topo(int i) {
    final fase = (i / n * dentes) % 1.0;
    // Sobe ate o meio do dente e desce ate o fim — ponta no meio.
    final t = fase < 0.5 ? fase * 2 : (1 - fase) * 2;
    // A banda e BAIXA E LARGA: a referencia e uma faixa, nao um copo.
    // O vale desce ate a metade da banda; a ponta chega no topo dela.
    return -altura + 2 * altura * (vale + (1 - vale) * t);
  }

  final verts = <List<double>>[];
  // Quatro aneis: fora-baixo, fora-topo, dentro-topo, dentro-baixo.
  for (var i = 0; i < n; i++) {
    final a = 2 * math.pi * i / n;
    verts.add([math.cos(a), -altura, math.sin(a)]);
  }
  for (var i = 0; i < n; i++) {
    final a = 2 * math.pi * i / n;
    verts.add([math.cos(a), topo(i), math.sin(a)]);
  }
  for (var i = 0; i < n; i++) {
    final a = 2 * math.pi * i / n;
    verts.add([inner * math.cos(a), topo(i), inner * math.sin(a)]);
  }
  for (var i = 0; i < n; i++) {
    final a = 2 * math.pi * i / n;
    verts.add([inner * math.cos(a), -altura, inner * math.sin(a)]);
  }

  int at(int anel, int i) => anel * n + i % n;
  final faces = <List<int>>[
    for (var i = 0; i < n; i++) ...[
      // Parede de fora, aresta de cima (a que faz o dente), parede de
      // dentro e o fundo.
      [at(0, i), at(0, i + 1), at(1, i + 1), at(1, i)],
      [at(1, i), at(1, i + 1), at(2, i + 1), at(2, i)],
      [at(2, i), at(2, i + 1), at(3, i + 1), at(3, i)],
      [at(3, i), at(3, i + 1), at(0, i + 1), at(0, i)],
    ],
  ];
  return Element3DMesh(verts, faces);
}

Element3DMesh _tube({required int segments, required double inner}) {
  final verts = <List<double>>[];
  // 0: fora-baixo, 1: fora-cima, 2: dentro-cima, 3: dentro-baixo.
  for (final (r, y) in [(1.0, -1.0), (1.0, 1.0), (inner, 1.0), (inner, -1.0)]) {
    for (var i = 0; i < segments; i++) {
      final a = 2 * math.pi * i / segments;
      verts.add([r * math.cos(a), y, r * math.sin(a)]);
    }
  }
  int at(int anel, int i) => anel * segments + i % segments;
  final faces = <List<int>>[];
  for (var i = 0; i < segments; i++) {
    faces.add([at(0, i), at(0, i + 1), at(1, i + 1), at(1, i)]);
    faces.add([at(1, i), at(1, i + 1), at(2, i + 1), at(2, i)]);
    faces.add([at(2, i), at(2, i + 1), at(3, i + 1), at(3, i)]);
    faces.add([at(3, i), at(3, i + 1), at(0, i + 1), at(0, i)]);
  }
  return Element3DMesh(verts, faces);
}

/// Cone/funil: aro no plano Y + apex; cap opcional no aro.
Element3DMesh _lathe({
  required int segments,
  required List<double> apex,
  required double ringY,
  required double ringR,
  required bool withCap,
}) {
  final verts = <List<double>>[apex];
  for (var i = 0; i < segments; i++) {
    final a = 2 * math.pi * i / segments;
    verts.add([ringR * math.cos(a), ringY, ringR * math.sin(a)]);
  }
  final faces = <List<int>>[];
  for (var i = 0; i < segments; i++) {
    faces.add([0, 1 + i, 1 + (i + 1) % segments]);
  }
  if (withCap) {
    faces.add([for (var i = 0; i < segments; i++) 1 + i]);
  }
  return Element3DMesh(verts, faces);
}

Element3DMesh _sphere({required int stacks, required int slices}) {
  final verts = <List<double>>[];
  for (var st = 0; st <= stacks; st++) {
    final phi = math.pi * st / stacks;
    final y = -math.cos(phi);
    final r = math.sin(phi);
    for (var sl = 0; sl < slices; sl++) {
      final th = 2 * math.pi * sl / slices;
      verts.add([r * math.cos(th), y, r * math.sin(th)]);
    }
  }
  final faces = <List<int>>[];
  int at(int st, int sl) => st * slices + sl % slices;
  for (var st = 0; st < stacks; st++) {
    for (var sl = 0; sl < slices; sl++) {
      faces.add([
        at(st, sl),
        at(st, sl + 1),
        at(st + 1, sl + 1),
        at(st + 1, sl),
      ]);
    }
  }
  return Element3DMesh(verts, faces);
}

Element3DMesh _cylinder({required int segments}) {
  final verts = <List<double>>[];
  for (final y in const [-1.0, 1.0]) {
    for (var i = 0; i < segments; i++) {
      final a = 2 * math.pi * i / segments;
      verts.add([math.cos(a), y, math.sin(a)]);
    }
  }
  final faces = <List<int>>[];
  for (var i = 0; i < segments; i++) {
    final j = (i + 1) % segments;
    faces.add([i, j, segments + j, segments + i]);
  }
  faces.add([for (var i = 0; i < segments; i++) i]);
  faces.add([for (var i = 0; i < segments; i++) segments + i]);
  return Element3DMesh(verts, faces);
}

/// Extrusao de um contorno 2D (x, y) ao longo de Z: frente + tras + lados.
Element3DMesh _extrude({
  required List<List<double>> outline,
  required double halfDepth,
}) {
  final n = outline.length;
  final verts = <List<double>>[
    for (final p in outline) [p[0], p[1], -halfDepth],
    for (final p in outline) [p[0], p[1], halfDepth],
  ];
  final faces = <List<int>>[
    [for (var i = 0; i < n; i++) i],
    [for (var i = 0; i < n; i++) n + i],
    for (var i = 0; i < n; i++) [i, (i + 1) % n, n + (i + 1) % n, n + i],
  ];
  return Element3DMesh(verts, faces);
}

/// Gema lapidada: mesa hexagonal em cima, cinta hexagonal maior no meio
/// e pavilhao em ponta — 1 mesa + 6 facetas de coroa + 6 de pavilhao.
Element3DMesh _diamond() {
  final verts = <List<double>>[];
  for (var i = 0; i < 6; i++) {
    final a = math.pi / 6 + 2 * math.pi * i / 6;
    verts.add([0.55 * math.cos(a), -0.72, 0.55 * math.sin(a)]);
  }
  for (var i = 0; i < 6; i++) {
    final a = math.pi / 6 + 2 * math.pi * i / 6;
    verts.add([1.0 * math.cos(a), -0.18, 1.0 * math.sin(a)]);
  }
  verts.add([0, 1.05, 0]);
  final faces = <List<int>>[
    [0, 1, 2, 3, 4, 5],
    for (var i = 0; i < 6; i++) [i, (i + 1) % 6, 6 + (i + 1) % 6, 6 + i],
    for (var i = 0; i < 6; i++) [6 + i, 6 + (i + 1) % 6, 12],
  ];
  return Element3DMesh(verts, faces);
}

Element3DMesh _torus({
  required double major,
  required double minor,
  required int around,
  required int tube,
}) {
  final verts = <List<double>>[];
  for (var i = 0; i < around; i++) {
    final u = 2 * math.pi * i / around;
    for (var j = 0; j < tube; j++) {
      final v = 2 * math.pi * j / tube;
      final r = major + minor * math.cos(v);
      verts.add([r * math.cos(u), minor * math.sin(v), r * math.sin(u)]);
    }
  }
  final faces = <List<int>>[];
  int at(int i, int j) => (i % around) * tube + j % tube;
  for (var i = 0; i < around; i++) {
    for (var j = 0; j < tube; j++) {
      faces.add([at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)]);
    }
  }
  return Element3DMesh(verts, faces);
}

String element3DLabel(Element3DKind kind) => switch (kind) {
  Element3DKind.cube => 'Cubo',
  Element3DKind.pyramid => 'Piramide',
  Element3DKind.cone => 'Cone',
  Element3DKind.sphere => 'Esfera',
  Element3DKind.cylinder => 'Cilindro',
  Element3DKind.prism => 'Prisma',
  Element3DKind.diamond => 'Diamante',
  Element3DKind.torus => 'Anel 3D',
  Element3DKind.star => 'Estrela 3D',
  Element3DKind.plane => 'Plano',
  Element3DKind.capsule => 'Capsula',
  Element3DKind.tube => 'Tubo',
  Element3DKind.octahedron => 'Octaedro',
  Element3DKind.wedge => 'Rampa',
  Element3DKind.dome => 'Cupula',
  Element3DKind.crown => 'Coroa',
  Element3DKind.crownFine => 'Coroa fina',
};

/// CUBO COM CHANFRO nas arestas.
///
/// Objeto real nao tem canto infinitamente afiado: sempre ha uma faixa
/// estreita na aresta, e e a luz batendo NELA que a gente le como
/// volume. Um cubo de canto perfeito perde essa faixa, e o resultado
/// parece um desenho de cubo, nao um cubo.
///
/// Custa doze faces de aresta e oito de canto — nada perto do que
/// entrega. O chanfro e pequeno de proposito: grande demais vira
/// almofada.
Element3DMesh _cuboChanfrado({double chanfro = 0.075}) {
  const a = 1.0;
  final c = 1 - chanfro.clamp(0.01, 0.4);

  // Para cada um dos oito cantos, tres vertices: o canto recuado em X,
  // em Y e em Z. E o que abre espaco para a faixa da aresta.
  final verts = <List<double>>[];
  final idx = <String, int>{};
  for (final sx in const [-1.0, 1.0]) {
    for (final sy in const [-1.0, 1.0]) {
      for (final sz in const [-1.0, 1.0]) {
        for (var eixo = 0; eixo < 3; eixo++) {
          final v = [sx * a, sy * a, sz * a];
          v[eixo] = v[eixo] / a * c;
          idx['$sx|$sy|$sz|$eixo'] = verts.length;
          verts.add(v);
        }
      }
    }
  }
  int em(double sx, double sy, double sz, int eixo) =>
      idx['$sx|$sy|$sz|$eixo']!;

  final faces = <List<int>>[];

  // AS SEIS FACES viraram octogonos: os cantos foram cortados.
  for (var eixo = 0; eixo < 3; eixo++) {
    final u = (eixo + 1) % 3;
    final w = (eixo + 2) % 3;
    for (final sinal in const [-1.0, 1.0]) {
      final pontos = <(double, int)>[];
      for (final su in const [-1.0, 1.0]) {
        for (final sw in const [-1.0, 1.0]) {
          final sig = List<double>.filled(3, 0);
          sig[eixo] = sinal;
          sig[u] = su;
          sig[w] = sw;
          // Os dois vertices deste canto que ficam NESTA face sao os
          // recuados nos outros dois eixos.
          for (final recuo in [u, w]) {
            final i = em(sig[0], sig[1], sig[2], recuo);
            final v = verts[i];
            pontos.add((math.atan2(v[w], v[u]), i));
          }
        }
      }
      // Ordenar pelo angulo garante poligono convexo sem depender de
      // acertar a volta na mao.
      pontos.sort((p1, p2) => p1.$1.compareTo(p2.$1));
      faces.add([for (final ponto in pontos) ponto.$2]);
    }
  }

  // AS DOZE FAIXAS DE ARESTA: onde duas faces se encontram.
  for (var i = 0; i < 3; i++) {
    for (var j = i + 1; j < 3; j++) {
      final k = 3 - i - j;
      for (final si in const [-1.0, 1.0]) {
        for (final sj in const [-1.0, 1.0]) {
          final s0 = List<double>.filled(3, 0);
          s0[i] = si;
          s0[j] = sj;
          s0[k] = -1;
          final s1 = List<double>.from(s0);
          s1[k] = 1;
          faces.add([
            em(s0[0], s0[1], s0[2], i),
            em(s0[0], s0[1], s0[2], j),
            em(s1[0], s1[1], s1[2], j),
            em(s1[0], s1[1], s1[2], i),
          ]);
        }
      }
    }
  }

  // OS OITO CANTOS: o triangulinho que sobra.
  for (final sx in const [-1.0, 1.0]) {
    for (final sy in const [-1.0, 1.0]) {
      for (final sz in const [-1.0, 1.0]) {
        faces.add([em(sx, sy, sz, 0), em(sx, sy, sz, 1), em(sx, sy, sz, 2)]);
      }
    }
  }

  return Element3DMesh(verts, faces);
}
