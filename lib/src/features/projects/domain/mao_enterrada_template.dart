import 'dart:math' as math;
import 'dart:ui';


import 'package:aurea_render/aurea_render.dart';
import '../../editor/domain/camera3d.dart';
import '../../editor/domain/camera_cuts.dart';
import '../../editor/domain/effect.dart';
import '../../editor/domain/element3d.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/scene3d.dart';
import '../../editor/domain/video_project.dart';
import 'malha_codigo.dart';
import 'textura_procedural.dart';

/// A MAO ENTERRADA — 15 s, quatro planos, para por o motor 3D a prova.
///
/// O que esta cena cobra do motor, de proposito:
///   1. UMA MALHA ORGANICA de verdade — a mao e um tubo varrido de secao
///      eliptica, com raio variavel, sem nenhuma peca encaixada em
///      outra;
///   2. UM TERRENO grande com relevo de tres oitavas de ruido, para a luz
///      rasante desenhar as cristas;
///   3. UM CEU proprio, texturizado, com o sol onde a luz diz que ele
///      esta;
///   4. QUATRO CAMERAS com corte seco, uma delas em contraluz direto.
///
/// POR QUE CADA DEDO E UMA PECA SO. A primeira versao montava cada dedo
/// com tres tubos e uma bola em cada junta. Parecia certo no papel e
/// saiu errado na tela: onde duas superficies se atravessam, um
/// desenhador que ordena por profundidade nao tem como decidir quem vem
/// antes, e apareciam lascas escuras em TODA junta — quinze por mao.
/// Varrer um tubo so ao longo do dedo inteiro, engrossando nos nos,
/// elimina essas quinze — e de quebra fica mais parecido com um dedo.
///
/// SOBRAM SEIS: os cinco dedos e o polegar entram na palma, e la eles se
/// atravessam de verdade. Isso nao tem como evitar sem recortar
/// geometria, e nao precisa: o motor de GPU tem buffer de profundidade e
/// resolve por pixel. Quem ve a lasca e o pintor de CPU, que e a reserva
/// — e e por isso que o dump visual mostra um degrau no dedo anelar que
/// o aparelho nao mostra. Comecar o dedo mais fundo na palma nao
/// resolve; foi tentado.

const maoFps = 30;
const maoDuracao = Duration(seconds: 15);
const _dur = 15.0;

/// Os quatro planos, em segundos.
const maoCortes = <double>[0, 3.9, 8.0, 11.6];

/// A direcao em que a luz do sol VIAJA. O sol esta no sentido contrario,
/// e o ceu e desenhado a partir disso — se um dia a luz mudar, o sol
/// muda de lugar junto.
const _direcaoDoSol = Vec3(.55, -.16, .82);

Duration _t(double s) => Duration(microseconds: (s * 1e6).round());

/// Amostra uma funcao do tempo como keyframes (um a cada 3 quadros: o
/// suficiente para um movimento de camera continuo sem inchar o arquivo).
AnimatedDouble _amostra(double Function(double) f) {
  final kfs = <Keyframe<double>>[];
  for (var i = 0; i <= (_dur * maoFps / 3).round(); i++) {
    final s = i * 3 / maoFps;
    kfs.add(Keyframe(time: _t(s), value: f(s)));
  }
  return AnimatedDouble(f(0), kfs);
}

AnimatedDouble _chaves(List<(double, double)> pares) =>
    AnimatedDouble(pares.first.$2, [
      for (final (s, v) in pares)
        Keyframe(time: _t(s), value: v, ease: Easing.appleStandard),
    ]);

// --------------------------------------------------------------- areia

double _ruidoDaAreia(double x, double y) {
  final s = math.sin(x * 12.9898 + y * 78.233) * 43758.5453;
  return s - s.floor();
}

double _suave(double t) => t * t * (3 - 2 * t);

/// Ruido interpolado: e o que da morro em vez de serra.
double _valor(double x, double y) {
  final x0 = x.floorToDouble(), y0 = y.floorToDouble();
  final fx = _suave(x - x0), fy = _suave(y - y0);
  final a = _ruidoDaAreia(x0, y0), b = _ruidoDaAreia(x0 + 1, y0);
  final c = _ruidoDaAreia(x0, y0 + 1), d = _ruidoDaAreia(x0 + 1, y0 + 1);
  final cima = a + (b - a) * fx;
  final baixo = c + (d - c) * fx;
  return cima + (baixo - cima) * fy;
}

/// A ALTURA DA AREIA em (x, z), ja com o monte que cobre o punho.
///
/// A cova larga em volta e o monte estreito no centro contam a mesma
/// historia: a areia cedeu ao redor e se acumulou encostada na mao. Sem
/// os dois, a mao parece plantada num chao liso.
double alturaDaAreia(double x, double z) {
  final grande = _valor(x / 900, z / 900) * 175;
  final medio = _valor(x / 320 + 5, z / 320 - 3) * 58;
  final fino = _valor(x / 90 - 11, z / 90 + 7) * 12;
  // Cristas alongadas na direcao do vento (x), como duna de verdade.
  final crista = math.sin(x / 260 + math.sin(z / 700) * 1.6) * 24;
  // O relevo e CENTRADO NA MEDIA (o ruido vale 0,5 em media, e as tres
  // oitavas somam 245 no maximo). Sem centrar, atenuar o ruido perto da
  // mao puxava o chao para o zero absoluto em vez de para o nivel medio
  // da duna — e a mao afundava num poco.
  final relevo = grande + medio + fino + crista - 122;

  final d2 = x * x + z * z;
  // Perto da mao o ruido cede lugar ao que foi desenhado de proposito:
  // a cova larga onde a areia cedeu e o monte encostado no punho. Com o
  // ruido em cima, o acaso decidia se a mao nascia num morro ou num
  // buraco.
  final perto = math.exp(-d2 / (2 * 300 * 300));
  final cova = -80 * math.exp(-d2 / (2 * 260 * 260));
  final monte = 96 * math.exp(-d2 / (2 * 118 * 118));
  return relevo * (1 - .8 * perto) - 78 + cova + monte;
}

// --------------------------------------------------------------- malha

/// TUBO VARRIDO ao longo de uma linha, com secao ELIPTICA e raio
/// variavel.
///
/// A peca de que a mao inteira e feita. Os aneis sao levados adiante por
/// TRANSPORTE PARALELO: cada anel gira junto com a curva em vez de ser
/// recalculado do zero. Recalculando, um tubo que curva acaba torcendo
/// sobre si mesmo — e num dedo isso aparece como a pele girando entre
/// uma falange e outra.
void _tuboVarrido(
  MalhaCodigo m,
  int mat,
  List<Vec3> pontos,
  List<double> raiosU,
  List<double> raiosV, {
  int lados = 14,
  bool tampaFinal = true,
  bool tampaInicial = false,
}) {
  if (pontos.length < 2) return;
  final n = pontos.length;

  // Tangente em cada ponto: diferenca central no meio, unilateral nas
  // pontas.
  final tangentes = <Vec3>[];
  for (var i = 0; i < n; i++) {
    final a = pontos[math.max(0, i - 1)];
    final b = pontos[math.min(n - 1, i + 1)];
    final d = (b - a);
    tangentes.add(d.length < 1e-9 ? const Vec3(0, 1, 0) : d.normalized);
  }

  var u = () {
    final t0 = tangentes.first;
    final ref = t0.y.abs() < .9 ? const Vec3(0, 1, 0) : const Vec3(1, 0, 0);
    return ref.cross(t0).normalized;
  }();

  final aneis = <List<int>>[];
  for (var i = 0; i < n; i++) {
    final t = tangentes[i];
    // Transporte paralelo: tira do u anterior a parte que aponta na
    // direcao nova.
    final proj = u.dot(t);
    u = (u - t * proj).normalized;
    if (u.length < .5) {
      final ref = t.y.abs() < .9 ? const Vec3(0, 1, 0) : const Vec3(1, 0, 0);
      u = ref.cross(t).normalized;
    }
    final v = t.cross(u);
    final anel = <int>[];
    for (var k = 0; k <= lados; k++) {
      final ang = 2 * math.pi * k / lados;
      final c = math.cos(ang), s = math.sin(ang);
      final desloc = u * (c * raiosU[i]) + v * (s * raiosV[i]);
      // A normal de uma elipse nao e a direcao do ponto: e o gradiente,
      // que divide pelo raio em vez de multiplicar.
      final normal = (u * (c / raiosU[i]) + v * (s / raiosV[i])).normalized;
      anel.add(
        m.vertice(
          mat,
          pontos[i] + desloc,
          normal,
          uv: Offset(k / lados, i / (n - 1)),
        ),
      );
    }
    aneis.add(anel);
  }

  for (var i = 0; i < n - 1; i++) {
    for (var k = 0; k < lados; k++) {
      final a = aneis[i][k], b = aneis[i][k + 1];
      final c = aneis[i + 1][k + 1], d = aneis[i + 1][k];
      m
        ..tri(mat, a, b, c)
        ..tri(mat, a, c, d);
    }
  }

  if (tampaFinal) {
    final ponta = pontos.last + tangentes.last * (raiosU.last * .9);
    final ip = m.vertice(mat, ponta, tangentes.last, uv: const Offset(.5, 1));
    for (var k = 0; k < lados; k++) {
      m.tri(mat, aneis.last[k], aneis.last[k + 1], ip);
    }
  }
  if (tampaInicial) {
    final atras = pontos.first - tangentes.first * (raiosU.first * .9);
    final ip = m.vertice(
      mat,
      atras,
      tangentes.first * -1,
      uv: const Offset(.5, 0),
    );
    for (var k = 0; k < lados; k++) {
      m.tri(mat, aneis.first[k + 1], aneis.first[k], ip);
    }
  }
}

/// ARREDONDA A PONTA de um tubo varrido.
///
/// Fechar o tubo com um leque ate um unico ponto faz um CONE — foi o que
/// saiu na primeira rodada, e cada dedo terminava em bico de lapis. A
/// ponta redonda sai do proprio perfil: mais alguns aneis andando na
/// direcao da tangente enquanto o raio cai por um quarto de circulo.
({List<Vec3> pontos, List<double> raios}) _arredondar(
  List<Vec3> pontos,
  List<double> raios, {
  int passos = 4,
  double achatar = 1.0,
}) {
  if (pontos.length < 2) return (pontos: pontos, raios: raios);
  final p = [...pontos];
  final r = [...raios];
  final t = (pontos.last - pontos[pontos.length - 2]).normalized;
  final raio = raios.last;
  for (var k = 1; k <= passos; k++) {
    final ang = math.pi / 2 * k / passos;
    p.add(pontos.last + t * (raio * math.sin(ang) * achatar));
    r.add(math.max(0.02, raio * math.cos(ang)));
  }
  return (pontos: p, raios: r);
}

/// Interpola uma linha de juntas em pontos densos, com o raio variando
/// junto e um engrossamento em cada junta.
({List<Vec3> pontos, List<double> raios}) _adensar(
  List<Vec3> juntas,
  List<double> raios, {
  int porTrecho = 5,
  double no = .17,
}) {
  final p = <Vec3>[];
  final r = <double>[];
  for (var i = 0; i < juntas.length - 1; i++) {
    for (var k = 0; k < porTrecho; k++) {
      final f = k / porTrecho;
      p.add(juntas[i] + (juntas[i + 1] - juntas[i]) * f);
      // O engrossamento e maior no comeco do trecho (onde esta a junta)
      // e some no meio dele: e o no do dedo.
      final bojo = 1 + no * math.exp(-f * f * 26);
      r.add((raios[i] + (raios[i + 1] - raios[i]) * f) * bojo);
    }
  }
  p.add(juntas.last);
  r.add(raios.last);
  return (pontos: p, raios: r);
}

/// UM DEDO: uma linha de quatro juntas que dobra mais a cada falange,
/// varrida como um tubo so.
void _dedo(
  MalhaCodigo m,
  int mat, {
  required Vec3 base,
  required double comprimento,
  required double raio,
  required double abertura,
  required double curva,
}) {
  const proporcoes = [.42, .33, .25];
  final juntas = <Vec3>[base];
  var p = base;
  var dir = Vec3(math.sin(abertura), math.cos(abertura), 0).normalized;
  for (var i = 0; i < 3; i++) {
    p = p + dir * (comprimento * proporcoes[i]);
    juntas.add(p);
    // Cada falange dobra mais: e o que faz a mao parecer viva em vez de
    // um garfo. A dobra e para tras, como uma mao relaxada.
    final ang = curva * (i + 1) / 3;
    dir = Vec3(
      dir.x,
      dir.y * math.cos(ang) - dir.z * math.sin(ang),
      dir.y * math.sin(ang) + dir.z * math.cos(ang),
    ).normalized;
  }
  final perfil = [raio, raio * .87, raio * .74, raio * .58];
  final densa = _adensar(juntas, perfil);
  final linha = _arredondar(densa.pontos, densa.raios, passos: 5, achatar: 1.3);
  _tuboVarrido(
    m,
    mat,
    linha.pontos,
    linha.raios,
    [for (final r in linha.raios) r * .92],
    lados: 16,
    tampaFinal: false,
  );
}

/// A MAO: palma e punho num tubo so, mais cinco dedos.
MalhaCodigo maoMalha() {
  final m = MalhaCodigo([materialCodigo('Pele', 0xffb98668, rugosidade: .56)]);
  const pele = 0;

  // PALMA E PUNHO: um tubo de secao achatada que sobe do que esta
  // enterrado ate a linha dos dedos. Uma peca so — nada para emendar.
  const eixo = <Vec3>[
    // O punho desce ate -330 e o tubo e FECHADO embaixo. A areia no
    // centro fica por volta de -80, mas ela cede numa cova em volta da
    // mao — e de dentro da cova da para ver mais fundo do que a conta
    // sugere. Com o tubo aberto, aparecia a boca oca do pulso.
    Vec3(8, -330, -10),
    Vec3(6, -230, -8),
    Vec3(4, -180, -6),
    Vec3(2, -130, -3),
    Vec3(0, -84, 0),
    Vec3(0, -40, 2),
    Vec3(0, 4, 3),
    Vec3(0, 44, 3),
    Vec3(0, 70, 2),
    Vec3(0, 84, 0),
  ];
  const largura = [30.0, 34.0, 38.0, 43.0, 50.0, 58.0, 63.0, 61.0, 55.0, 46.0];
  const espessura = [
    19.0,
    21.0,
    23.0,
    25.0,
    27.0,
    28.0,
    28.0,
    26.0,
    23.0,
    19.0,
  ];
  // O alto da palma fecha REDONDO e achatado: e o dorso da mao, de onde
  // os dedos saem. Fechado em bico, virava um espeto entre os dedos.
  final palma = _arredondar(eixo, largura, passos: 3, achatar: .55);
  _tuboVarrido(
    m,
    pele,
    palma.pontos,
    palma.raios,
    [
      ...espessura,
      for (var i = espessura.length; i < palma.raios.length; i++)
        espessura.last * palma.raios[i] / largura.last,
    ],
    lados: 20,
    tampaFinal: false,
    tampaInicial: true,
  );

  // OS QUATRO DEDOS. Comprimento e abertura de mao real: o medio e o
  // mais longo, o minimo o mais curto e o mais aberto. Os de fora dobram
  // mais — mao aberta nunca e um leque plano.
  const dedos = <(double, double, double, double, double, double)>[
    // x, y da base, comprimento, raio, abertura, curva
    (-46, 60, 148, 13.5, -.33, .34),
    (-16, 72, 166, 14.5, -.11, .22),
    (15, 69, 156, 14.0, .10, .26),
    (44, 54, 124, 12.0, .33, .40),
  ];
  for (final (x, y, comp, raio, abertura, curva) in dedos) {
    _dedo(
      m,
      pele,
      base: Vec3(x, y, 2),
      comprimento: comp,
      raio: raio,
      abertura: abertura,
      curva: curva,
    );
  }

  // POLEGAR: comeca DENTRO da palma, para a emenda ficar escondida, e
  // sai para fora e para frente.
  final polegar = _adensar(
    const [
      Vec3(-30, -28, 4),
      Vec3(-76, 2, 16),
      Vec3(-100, 42, 26),
      Vec3(-108, 70, 32),
    ],
    const [19, 16, 13, 10.5],
  );
  final pontaPolegar = _arredondar(
    polegar.pontos,
    polegar.raios,
    passos: 5,
    achatar: 1.3,
  );
  _tuboVarrido(
    m,
    pele,
    pontaPolegar.pontos,
    pontaPolegar.raios,
    [for (final r in pontaPolegar.raios) r * .9],
    lados: 16,
    tampaFinal: false,
  );
  return m;
}

/// AS DUNAS: uma grade grande com o relevo de [alturaDaAreia]. As normais
/// saem da propria funcao (derivada por diferenca), que e o que deixa a
/// luz rasante desenhar as cristas.
MalhaCodigo dunasMalha({double lado = 5200, int passos = 66}) {
  final m = MalhaCodigo([materialCodigo('Areia', 0xffc9a978, rugosidade: .96)]);
  const areia = 0;

  final ids = <List<int>>[];
  final meio = lado / 2;
  for (var i = 0; i <= passos; i++) {
    final linha = <int>[];
    final z = -meio + lado * i / passos;
    for (var j = 0; j <= passos; j++) {
      final x = -meio + lado * j / passos;
      const e = 12.0;
      final n = Vec3(
        alturaDaAreia(x - e, z) - alturaDaAreia(x + e, z),
        2 * e,
        alturaDaAreia(x, z - e) - alturaDaAreia(x, z + e),
      ).normalized;
      linha.add(
        m.vertice(
          areia,
          Vec3(x, alturaDaAreia(x, z), z),
          n,
          uv: Offset(j / passos * 8, i / passos * 8),
        ),
      );
    }
    ids.add(linha);
  }
  // A ORDEM DOS VERTICES DECIDE PARA QUE LADO A FACE OLHA, e quem olha
  // para o outro lado e descartado. Na primeira versao o chao estava
  // enrolado ao contrario: sumia visto de cima, que e de onde a camera
  // sempre olha, e sobrava so a faixa distante onde a duna se inclina
  // para a lente.
  for (var i = 0; i < passos; i++) {
    for (var j = 0; j < passos; j++) {
      final a = ids[i][j], b = ids[i][j + 1];
      final c = ids[i + 1][j + 1], d = ids[i + 1][j];
      m
        ..tri(areia, a, c, b)
        ..tri(areia, a, d, c);
    }
  }
  return m;
}

/// O CEU DE FIM DE TARDE, pintado numa textura e vestido numa cupula.
///
/// Nao ha ceu no motor: o que existe e uma cor de fundo chapada, e cor
/// chapada acima de uma duna nao e um deserto, e um recorte. Uma cupula
/// texturizada resolve com mil triangulos, e — detalhe que importa — face
/// com textura nao recebe neblina, entao o degrade chega inteiro ate a
/// lente enquanto as dunas continuam sumindo na distancia.
String _texturaDoCeu() {
  final sol = _direcaoDoSol * -1;
  final lonSol = math.atan2(sol.z, sol.x);
  final elevSol = math.asin(sol.y.clamp(-1.0, 1.0));

  // As paradas do degrade em ELEVACAO (radianos acima do horizonte), e
  // nao em fracao da cupula. A diferenca importa: um plano cinemascope
  // mostra uns 25 graus de ceu, e com o degrade espalhado pela cupula
  // inteira esses 25 graus caiam todos dentro da mesma faixa laranja —
  // ceu chapado, que foi o que saiu na primeira rodada.
  const paradas = <(double, int)>[
    (1.20, 0xff24345f),
    (0.62, 0xff4a4a78),
    (0.30, 0xff8f5f74),
    (0.12, 0xffcf8355),
    (0.02, 0xfff5b06a),
    (-0.02, 0xffffc98a),
    (-0.12, 0xff8a5530),
    (-0.60, 0xff3d2415),
  ];

  return pngDataUri(256, 128, (px, py, rgb) {
    final u = px / 255;
    final v = py / 127;
    final elev = math.pi / 2 - v * _ceuAberturaRad;

    var r = 0.0, g = 0.0, b = 0.0;
    if (elev >= paradas.first.$1) {
      final c = Color(paradas.first.$2);
      r = c.r;
      g = c.g;
      b = c.b;
    } else if (elev <= paradas.last.$1) {
      final c = Color(paradas.last.$2);
      r = c.r;
      g = c.g;
      b = c.b;
    } else {
      for (var i = 0; i < paradas.length - 1; i++) {
        final alto = paradas[i], baixo = paradas[i + 1];
        if (elev <= alto.$1 && elev >= baixo.$1) {
          final k = (alto.$1 - elev) / (alto.$1 - baixo.$1);
          final ca = Color(alto.$2), cb = Color(baixo.$2);
          r = ca.r + (cb.r - ca.r) * k;
          g = ca.g + (cb.g - ca.g) * k;
          b = ca.b + (cb.b - ca.b) * k;
          break;
        }
      }
    }

    // O SOL, no lugar que a luz da cena diz. Um disco pequeno e um halo
    // largo: sem o halo o sol vira um adesivo redondo.
    var dLon = (u * 2 * math.pi - (lonSol < 0 ? lonSol + 2 * math.pi : lonSol))
        .abs();
    if (dLon > math.pi) dLon = 2 * math.pi - dLon;
    final dElev = elev - elevSol;
    final d = math.sqrt(dLon * dLon * .55 + dElev * dElev * 2.4);
    final halo = math.exp(-d * d / (2 * 0.38 * 0.38));
    final disco = d < 0.05 ? 1.0 : 0.0;
    final brilho = (halo * .8 + disco).clamp(0.0, 1.0);
    r += (1.00 - r) * brilho;
    g += (0.94 - g) * brilho;
    b += (0.76 - b) * brilho;

    rgb[0] = canal8(r);
    rgb[1] = canal8(g);
    rgb[2] = canal8(b);
  });
}

/// Ate onde a cupula desce abaixo do horizonte.
const _ceuAberturaRad = 2.0;

/// A CUPULA. Vestida por dentro, sem luz e com as duas faces ligadas —
/// assim nao importa de que lado a camera a atravessa.
MalhaCodigo ceuMalha({double raio = 3200, int aneis = 18, int lados = 36}) {
  final m = MalhaCodigo([
    materialCodigo(
      'Ceu',
      0xffffffff,
      semLuz: true,
      doisLados: true,
      imagem: _texturaDoCeu(),
    ),
  ]);
  const ceu = 0;
  final ids = <List<int>>[];
  for (var i = 0; i <= aneis; i++) {
    final lat = _ceuAberturaRad * i / aneis;
    final linha = <int>[];
    for (var j = 0; j <= lados; j++) {
      final lon = 2 * math.pi * j / lados;
      final dir = Vec3(
        math.sin(lat) * math.cos(lon),
        math.cos(lat),
        math.sin(lat) * math.sin(lon),
      );
      linha.add(
        m.vertice(ceu, dir * raio, dir * -1, uv: Offset(j / lados, i / aneis)),
      );
    }
    ids.add(linha);
  }
  for (var i = 0; i < aneis; i++) {
    for (var j = 0; j < lados; j++) {
      final a = ids[i][j], b = ids[i][j + 1];
      final c = ids[i + 1][j + 1], d = ids[i + 1][j];
      m
        ..tri(ceu, a, c, b)
        ..tri(ceu, a, d, c);
    }
  }
  return m;
}

// -------------------------------------------------------------- cameras

/// O centro da mao, na altura dos dedos (o ponto que as cameras miram).
const _mao = Vec3(0, 150, 0);

/// Uma camera de um plano — CONGELADA fora do proprio plano.
///
/// Os movimentos aqui sao funcoes lineares do tempo, e uma reta que vale
/// de 11,6 s a 15 s tambem "vale" em t=0: e ali ela ja esta a mil e
/// setecentas unidades abaixo da areia. Isso nunca aparece no video (a
/// camera nem esta no ar nesse instante), mas aparece na hora de abrir a
/// cena no editor e escolher a camera 4 para conferir o enquadramento.
/// Travar o tempo na janela da tomada resolve de uma vez.
Camera3D _plano(
  String id,
  String nome,
  Vec3 Function(double) onde,
  Vec3 Function(double) mira,
  double lente,
  double Function(double) giro, {
  required double entra,
  required double sai,
}) {
  double janela(double t) => t.clamp(entra, sai);
  return Camera3D(
    id: id,
    name: nome,
    posX: _amostra((t) => onde(janela(t)).x),
    posY: _amostra((t) => onde(janela(t)).y),
    posZ: _amostra((t) => onde(janela(t)).z),
    poiX: _amostra((t) => mira(janela(t)).x),
    poiY: _amostra((t) => mira(janela(t)).y),
    poiZ: _amostra((t) => mira(janela(t)).z),
    focalLength: AnimatedDouble(lente),
    rotZ: _amostra((t) => giro(janela(t))),
  );
}

List<Camera3D> maoCameras() => [
  // 01 · A DESCOBERTA: longe e baixo, a mao pequena e fora do centro,
  // com o deserto ocupando o quadro. Empurra devagar.
  _plano(
    'mao_cam_1',
    '01 · A descoberta / 45 mm',
    // A PALMA OLHA PARA 24 GRAUS (a rotacao Y da mao). Todo azimute de
    // camera aqui e escolhido EM RELACAO A ISSO: uma mao e um objeto
    // chato, e duas cameras a noventa graus uma da outra dao um plano de
    // frente e outro em que os dedos viram uma coluna so. Esta fica a
    // -45 graus da palma: tres quartos.
    (t) => Vec3(-591 + t * 32, 210 - t * 9, 1540 - t * 84),
    (t) => _mao + Vec3(60, 18 + t * 3, 0),
    45,
    (t) => -1.4 + t * .12,
    entra: maoCortes[0],
    sai: maoCortes[1],
  ),
  // 02 · OS DEDOS: perto, rente ao chao, orbitando devagar. A 820
  // unidades com 60 mm os dedos ocupam meio quadro — a 430, como estava
  // antes, um dedo sozinho cobria a tela inteira.
  _plano(
    'mao_cam_2',
    '02 · Os dedos / 60 mm',
    (t) {
      // O ANGULO DA ORBITA E ESCOLHIDO PELA LUZ, nao pelo enquadramento.
      // Em .62 rad a camera ficava do lado oposto ao sol e a mao saia
      // toda em sombra chapada. Aqui ela orbita a uns 105 graus do sol:
      // metade em luz, metade em sombra, que e o que da volume.
      // 64 graus de azimute: 40 graus da palma, e a uns 105 graus do
      // sol. Tres quartos e meia-luz, que e o que da volume. Antes disso
      // a orbita passava do lado oposto ao sol e a mao saia chapada de
      // sombra.
      final a = 1.25 - (t - maoCortes[1]) * .075;
      return _mao + Vec3(980 * math.sin(a), 24, 980 * math.cos(a));
    },
    (t) => _mao + Vec3(0, 12 - (t - maoCortes[1]) * 4, 0),
    60,
    (t) => 2 + (t - maoCortes[1]) * .5,
    entra: maoCortes[1],
    sai: maoCortes[2],
  ),
  // 03 · CONTRALUZ: quase encostada na areia, olhando para cima com o
  // sol atras dos dedos. E o plano que testa o tonemap.
  _plano(
    'mao_cam_3',
    '03 · Contraluz / 28 mm',
    (t) =>
        Vec3(190 - (t - maoCortes[2]) * 12, 46 + (t - maoCortes[2]) * 34, 330),
    (t) => _mao + Vec3(-30, 42 + (t - maoCortes[2]) * 10, -40),
    28,
    (t) => 4 - (t - maoCortes[2]) * .8,
    entra: maoCortes[2],
    sai: maoCortes[3],
  ),
  // 04 · O DESERTO: sobe e abre, a mao vira um detalhe na duna.
  _plano(
    'mao_cam_4',
    '04 · O deserto / 35 mm',
    (t) {
      final k = t - maoCortes[3];
      return Vec3(60 + k * 40, 420 + k * 185, 980 + k * 230);
    },
    (t) => _mao + Vec3(0, -70, 0),
    35,
    (t) => -2 - (t - maoCortes[3]) * .6,
    entra: maoCortes[3],
    sai: _dur,
  ),
];

// ---------------------------------------------------------------- cena

VideoProject buildMaoEnterradaTemplate() {
  final cameras = maoCameras();
  final mao = maoMalha();
  final dunas = dunasMalha();
  final ceu = ceuMalha();

  return VideoProject(
    id: 'mao_enterrada_template',
    name: 'MAO ENTERRADA · 15 s',
    createdAt: DateTime(2026, 9, 7),
    // Cinemascope: a faixa larga e o que faz o deserto parecer deserto.
    aspectRatio: 1280 / 536,
    resolutionHeight: 804,
    fps: maoFps,
    backgroundColor: const Color(0xff1a0f0a),
    markers: [
      for (var i = 0; i < 4; i++)
        Marker(time: _t(maoCortes[i]), label: cameras[i].name),
    ],
    layers: [
      // A COR do filme, por cima de tudo: contraste, um tique de
      // saturacao, vinheta e grao. E o que separa "render" de "plano".
      AdjustmentLayer(
        name: 'Cor do filme',
        startTime: Duration.zero,
        duration: maoDuracao,
        effects: [
          // A GRADACAO vinha de um efeito so, "Corrections", que saiu do
          // catalogo em 16/09. As tres partes dele viraram os tres efeitos
          // oficiais que fazem o mesmo, com a unidade convertida do
          // normalizado (-1..1) para a faixa de cada ficha:
          //   contraste .20  -> Contraste 20      (0..100)
          //   sombras   .06  -> Saida preto 15,3   (0..255)
          //   altas    -.12  -> Saida branco 224,4 (0..255)
          //   saturacao .12  -> Saturacao 12       (-100..100)
          // `temperatura` (.14, quente) NAO tem equivalente no catalogo
          // oficial e caiu. O plano perde o calorzinho ate alguem
          // reescrever a gradacao com o catalogo de agora.
          EffectInstance(type: EffectType.brightnessContrast)
              .withParamEdited('contrast', Duration.zero, 20),
          EffectInstance(type: EffectType.levels)
              .withParamEdited('output_black', Duration.zero, 15.3)
              .withParamEdited('output_white', Duration.zero, 224.4),
          EffectInstance(type: EffectType.hueSaturation)
              .withParamEdited('master_saturation', Duration.zero, 12),
          EffectInstance(type: EffectType.vignette)
              .withParamEdited('quantidade', Duration.zero, .40)
              .withParamEdited('suavidade', Duration.zero, .75),
        ],
      ),
      // POEIRA no ar: parte da atmosfera, nao decoracao.
      //
      // A RECEITA E A DO PRESET "Poeira", ajustada para o grão quente
      // deste deserto. Antes eram doze campos soltos na camada; agora e
      // um objeto do motor — e uma receita nova so muda o que ela quer.
      ParticulasLayer(
        name: 'Poeira no ar',
        startTime: Duration.zero,
        duration: maoDuracao,
        parametros: ParametrosDeParticulas(
          emissor: EmissorDeParticulas.caixa,
          largura: 2200,
          altura: 1200,
          profundidade: 1400,
          vidaS: 9,
          velocidade: 12,
          gravidade: -2.5,
          tamanho: 3,
          forma: FormaDaParticula.esfera,
          corInicio: 0xE8C9A0FF,
          brilho: 0.15,
          opacidade: 0.26,
          opacidadeNaVida: OpacidadeNaVida.entraESai,
          maximo: 80,
        ),
        opacity: AnimatedDouble(.26),
      ),
      // A CENA 3D.
      Scene3DLayer(
        id: 'mao_cena',
        name: 'Deserto · cena 3D',
        startTime: Duration.zero,
        duration: maoDuracao,
        position: AnimatedOffset(const Offset(960, 402)),
        showHelpers: false,
        camera: cameras.first,
        extraCameras: cameras.skip(1).toList(),
        shots: [
          for (var i = 0; i < 4; i++)
            CameraShot(time: _t(maoCortes[i]), cameraId: cameras[i].id),
        ],
        // Abre do preto e fecha no preto: 15 s com comeco e fim.
        opacity: _chaves([(0, 0), (.7, 1), (14.2, 1), (15, 0)]),
        scene: Scene3D(
          showFloorGrid: false,
          environment: EnvironmentKind.porDoSol,
          envReflect: .28,
          background: const Color(0xff2b1a12),
          // A neblina e o que da distancia: sem ela a duna do fundo tem
          // a mesma cor da do primeiro plano e a cena fica chapada. O
          // ceu nao entra nessa conta — face com textura nao recebe
          // neblina, e por isso o degrade sobrevive inteiro.
          fogColor: const Color(0xffd08f52),
          fogDensity: .00042,
          fogStart: 620,
          ambient: .30,
          skyColor: const Color(0xffe9b784),
          groundColor: const Color(0xff6b4a30),
          lights: [
            // O SOL, quase no horizonte e atras da mao.
            Light3D(
              id: 'mao_sol',
              color: const Color(0xffffb463),
              direction: _direcaoDoSol,
              intensity: AnimatedDouble(2.1),
              castsShadow: true,
            ),
            // O CEU: a luz azul que sobra e abre as sombras.
            Light3D(
              id: 'mao_ceu',
              color: const Color(0xff86a8d8),
              direction: const Vec3(-.35, -.85, -.30),
              intensity: AnimatedDouble(.55),
            ),
            // O CONTRALUZ que desenha a silhueta dos dedos.
            Light3D(
              id: 'mao_recorte',
              color: const Color(0xffffd9ae),
              direction: const Vec3(-.10, -.30, .95),
              intensity: AnimatedDouble(1.25),
            ),
          ],
          nodes: [
            ceu.no('mao_ceu_cupula', 'Céu'),
            dunas.no('mao_dunas', 'Dunas'),
            mao.no(
              'mao_mao',
              'Mão',
              // Tomba um pouco para a camera e balanca de leve: a mao
              // "assenta" na areia em vez de posar.
              rotX: _amostra((t) => -10 + .8 * math.sin(t * .5)),
              rotY: _amostra((t) => 24 + .6 * math.sin(t * .37)),
              rotZ: _amostra((t) => -7 + .5 * math.sin(t * .43)),
            ),
          ],
        ),
      ),
    ],
  );
}
