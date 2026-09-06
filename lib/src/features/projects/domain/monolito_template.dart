import 'dart:math' as math;
import 'dart:ui';

import '../../editor/domain/camera3d.dart';
import '../../editor/domain/effect.dart';
import '../../editor/domain/element3d.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/panorama3d.dart';
import '../../editor/domain/scene3d.dart';
import '../../editor/domain/shape.dart';
import '../../editor/domain/video_project.dart';
import '../application/modelos_empacotados.dart';
import 'abyss_cinematic_template.dart' show buildAbyssExplorer;
import 'malha_codigo.dart';
import 'textura_procedural.dart';

/// MONOLITO · O ASTRONAUTA E A PORTA.
///
/// Recriacao de uma referencia: floresta noturna afogada em neblina, um
/// bloco de concreto claro visto pela quina, uma porta aberta no bloco
/// com o interior de pedra acendendo em magenta, um astronauta flutuando
/// diante dela em contraluz, arvores nuas em silhueta e grama alta no
/// primeiro plano. A camera balanca devagar de um lado para o outro,
/// num ciclo de dezesseis segundos.
///
/// E a cena que o motor em GPU foi feito para desenhar: a porta e
/// material EMISSIVO (o bloom acende), e tambem uma LUZ SPOT magenta com
/// sombra suave — e ela que desenha o astronauta e a mancha rosa no
/// chao; a neblina exponencial come as arvores do fundo; e as camadas
/// de luz (planos translucidos no cone da porta) sao o volume de luz
/// atravessando a neblina, que nenhum dos dois motores calcula sozinho.
/// No pintor em CPU a mesma cena continua legivel: neblina, luzes e
/// emissivo existem la tambem, sem sombra nem bloom.
///
/// O ASTRONAUTA e o explorador do Abismo ate o modelo importado chegar:
/// e um no com modelo, entao trocar e importar o seu e apontar aqui.
const monolitoDuration = Duration(seconds: 16);
const monolitoFps = 30;
const monolitoWidth = 1280.0;
const monolitoHeight = 720.0;
const monolitoTriangleBudget = 17000;

/// Com os modelos reais (astronauta, portal, arvores escaneadas) a cena
/// e para a GPU; este e o teto que ainda cabe num iPhone 13.
const monolitoTriangleBudgetComModelos = 80000;

Duration _t(num seconds) =>
    Duration(microseconds: (seconds * 1000000).round());
AnimatedDouble _ad(double v) => AnimatedDouble(v);
AnimatedDouble _sample(double Function(double) f) => AnimatedDouble(f(0), [
  for (var i = 0; i <= 96; i++)
    Keyframe(time: _t(i / 6), value: f(i / 6)),
]);

/// O BLOCO fica girado: e pela quina que a referencia o mostra.
const _giro = -22.0;
const _bw = 460.0, _bh = 350.0, _bd = 300.0;
const _centroDoBloco = Vec3(150, _bh / 2, 0);

/// A PORTA: na face da frente, perto da aresta esquerda. Em coordenadas
/// locais do bloco (x para a direita, y para cima, z para a frente).
const _pw = 100.0, _ph = 250.0, _pProfundidade = 70.0;
const _portaCentroLocal = Vec3(-_bw / 2 + 110, _ph / 2, _bd / 2);

/// Um ponto local do bloco em coordenadas de mundo.
Vec3 _mundo(Vec3 local) {
  final a = _giro * math.pi / 180;
  return Vec3(
    _centroDoBloco.x + local.x * math.cos(a) + local.z * math.sin(a),
    local.y,
    _centroDoBloco.z - local.x * math.sin(a) + local.z * math.cos(a),
  );
}

/// A direcao local girada (sem transladar).
Vec3 _direcao(Vec3 local) {
  final a = _giro * math.pi / 180;
  return Vec3(
    local.x * math.cos(a) + local.z * math.sin(a),
    local.y,
    -local.x * math.sin(a) + local.z * math.cos(a),
  ).normalized;
}

// ============================================================== TEXTURAS

/// O CHAO da floresta: terra escura, folhas caidas, musgo em manchas.
String _texturaChao() => pngDataUri(512, 512, (px, py, rgb) {
      final u = px / 512, v = py / 512;
      final grande = fbm(u * 5 + 3, v * 5 + 9, semente: 81);
      final fino = fbm(u * 40 + 1, v * 40 + 5, oitavas: 2, semente: 82);
      final grao = ruido(px * 7 + py * 131) - .5;
      final musgo = suave(((fbm(u * 7 + 2, v * 7 + 4, semente: 83) - .58) / .12).clamp(0.0, 1.0));
      var r = .11 + .05 * (grande - .5) + .04 * (fino - .5) + .03 * grao;
      var g = .09 + .04 * (grande - .5) + .04 * (fino - .5) + .03 * grao;
      var b = .06 + .02 * (grande - .5) + .02 * (fino - .5) + .02 * grao;
      r += (.10 - r) * musgo;
      g += (.16 - g) * musgo;
      b += (.06 - b) * musgo;
      // Folhas caidas: pontos um pouco mais claros e quentes.
      if (ruido(px * 31 + py * 17 + 9) > .93) {
        r += .10;
        g += .06;
      }
      rgb[0] = canal8(r);
      rgb[1] = canal8(g);
      rgb[2] = canal8(b);
    });

/// CONCRETO claro, com manchas e poros.
String _texturaConcreto() => pngDataUri(256, 256, (px, py, rgb) {
      final u = px / 256, v = py / 256;
      final mancha = fbm(u * 3 + 7, v * 3 + 1, semente: 91);
      final poro = fbm(u * 24 + 2, v * 24 + 8, oitavas: 2, semente: 92);
      final grao = ruido(px * 53 + py * 7) - .5;
      final base = .62 + .14 * (mancha - .5) + .08 * (poro - .5) + .05 * grao;
      rgb[0] = canal8(base);
      rgb[1] = canal8(base + .005);
      rgb[2] = canal8(base + .01);
    });

/// A PEDRA DA PORTA: blocos claros com juntas, para o magenta iluminar.
String _texturaPedra() => pngDataUri(128, 256, (px, py, rgb) {
      final linha = py ~/ 32;
      final desl = linha.isEven ? 0 : 32;
      final jx = ((px + desl) % 64) < 3, jy = (py % 32) < 3;
      final n = fbm(px / 40 + 3, py / 40 + 5, oitavas: 2, semente: 101);
      var base = .82 + .12 * (n - .5);
      if (jx || jy) base -= .35;
      rgb[0] = canal8(base);
      rgb[1] = canal8(base * .9);
      rgb[2] = canal8(base);
    });

// ================================================================== NOS

SceneNode _chao() {
  final m = MalhaCodigo([
    materialCodigo('Chao da floresta', 0xffffffff, rugosidade: .95, imagem: _texturaChao()),
  ]);
  const n = 36;
  const meio = 3000.0;
  const passo = 2 * meio / n;
  double altura(double x, double z) =>
      14 * (fbm(x / 420 + 1, z / 420 + 2, semente: 71) - .5) +
      4 * (fbm(x / 90 + 5, z / 90 + 7, oitavas: 2, semente: 72) - .5);
  final idx = <int>[];
  for (var j = 0; j <= n; j++) {
    for (var i = 0; i <= n; i++) {
      final x = -meio + i * passo, z = -meio + j * passo;
      const e = 8.0;
      final nrm = Vec3(
        -(altura(x + e, z) - altura(x - e, z)),
        2 * e,
        -(altura(x, z + e) - altura(x, z - e)),
      ).normalized;
      // O mapa se repete a cada 600 unidades: detalhe perto, sem estourar.
      idx.add(m.vertice(0, Vec3(x, altura(x, z), z), nrm,
          uv: Offset(x / 600, z / 600)));
    }
  }
  int at(int i, int j) => j * (n + 1) + i;
  for (var j = 0; j < n; j++) {
    for (var i = 0; i < n; i++) {
      final a = idx[at(i, j)], b = idx[at(i + 1, j)];
      final c = idx[at(i + 1, j + 1)], d = idx[at(i, j + 1)];
      if ((i + j).isEven) {
        m.tri(0, a, c, b);
        m.tri(0, a, d, c);
      } else {
        m.tri(0, a, d, b);
        m.tri(0, b, d, c);
      }
    }
  }
  return m.no('monolito_chao', 'Chao da floresta');
}

/// O BLOCO com a porta: seis faces de concreto, a frente com o buraco da
/// porta, o tunel da porta em concreto e a parede do fundo em pedra
/// EMISSIVA — e o interior aceso que a referencia mostra.
SceneNode _monolito() {
  final concreto = _texturaConcreto();
  final m = MalhaCodigo([
    materialCodigo('Concreto', 0xffffffff, rugosidade: .92, imagem: concreto),
    {
      'name': 'Pedra acesa',
      'color': [1.0, .62, 1.0, 1.0],
      'metallic': 0.0,
      'roughness': .8,
      'emissive': 1.0,
      'unlit': true,
      'image': _texturaPedra(),
    },
  ]);
  const hw = _bw / 2, hh = _bh, hd = _bd / 2;
  Vec3 p(double x, double y, double z) => _mundo(Vec3(x, y, z));
  Vec3 fora(Vec3 local) => _direcao(local);

  // Topo, fundo (nao se ve), esquerda, direita, tras.
  m.quadPlano(0, p(-hw, hh, -hd), p(hw, hh, -hd), p(hw, hh, hd), p(-hw, hh, hd), virado: const Vec3(0, 1, 0));
  m.quadPlano(0, p(-hw, 0, -hd), p(-hw, hh, -hd), p(-hw, hh, hd), p(-hw, 0, hd), virado: fora(const Vec3(-1, 0, 0)));
  m.quadPlano(0, p(hw, 0, -hd), p(hw, 0, hd), p(hw, hh, hd), p(hw, hh, -hd), virado: fora(const Vec3(1, 0, 0)));
  m.quadPlano(0, p(-hw, 0, -hd), p(hw, 0, -hd), p(hw, hh, -hd), p(-hw, hh, -hd), virado: fora(const Vec3(0, 0, -1)));

  // FRENTE em quatro pedacos ao redor da porta.
  final px0 = _portaCentroLocal.x - _pw / 2, px1 = _portaCentroLocal.x + _pw / 2;
  final frente = fora(const Vec3(0, 0, 1));
  m.quadPlano(0, p(-hw, 0, hd), p(px0, 0, hd), p(px0, hh, hd), p(-hw, hh, hd), virado: frente);
  m.quadPlano(0, p(px1, 0, hd), p(hw, 0, hd), p(hw, hh, hd), p(px1, hh, hd), virado: frente);
  m.quadPlano(0, p(px0, _ph, hd), p(px1, _ph, hd), p(px1, hh, hd), p(px0, hh, hd), virado: frente);

  // TUNEL da porta: laterais e teto em concreto, fundo em pedra acesa.
  final zi = hd - _pProfundidade;
  m.quadPlano(0, p(px0, 0, hd), p(px0, _ph, hd), p(px0, _ph, zi), p(px0, 0, zi), virado: fora(const Vec3(1, 0, 0)));
  m.quadPlano(0, p(px1, 0, hd), p(px1, 0, zi), p(px1, _ph, zi), p(px1, _ph, hd), virado: fora(const Vec3(-1, 0, 0)));
  m.quadPlano(0, p(px0, _ph, hd), p(px1, _ph, hd), p(px1, _ph, zi), p(px0, _ph, zi), virado: const Vec3(0, -1, 0));
  m.triPlano(1, p(px0, 0, zi), p(px1, 0, zi), p(px1, _ph, zi),
      ua: const Offset(0, 1), ub: const Offset(1, 1), uc: const Offset(1, 0), virado: frente);
  m.triPlano(1, p(px0, 0, zi), p(px1, _ph, zi), p(px0, _ph, zi),
      ua: const Offset(0, 1), ub: const Offset(1, 0), uc: const Offset(0, 0), virado: frente);

  return m.no('monolito_bloco', 'Monolito de concreto');
}

/// AS CAMADAS DE LUZ: planos translucidos magenta no cone da porta, cada
/// um maior e mais fraco que o anterior. E a luz atravessando a neblina.
SceneNode _volumeDeLuz() {
  final m = MalhaCodigo([
    for (var k = 0; k < 4; k++)
      {
        'name': 'Camada de luz ${k + 1}',
        // A opacidade entra ao quadrado no pintor (cor x opacidade):
        // guardar a raiz da opacidade desejada.
        'color': [.85, .40, .90, math.sqrt(.07 * (1 - k / 5))],
        'alpha': 'BLEND',
        'unlit': true,
        'doubleSided': true,
        'emissive': .6,
      },
  ]);
  for (var k = 0; k < 4; k++) {
    final z = _bd / 2 + 25 + 55 * k;
    final w = 150 + 110 * k, h = 270 + 20 * k;
    final cx = _portaCentroLocal.x, cy = _ph / 2 + 10 * k;
    m.quadPlano(
      k,
      _mundo(Vec3(cx - w / 2, math.max(2, cy - h / 2), z)),
      _mundo(Vec3(cx + w / 2, math.max(2, cy - h / 2), z)),
      _mundo(Vec3(cx + w / 2, cy + h / 2, z)),
      _mundo(Vec3(cx - w / 2, cy + h / 2, z)),
      virado: _direcao(const Vec3(0, 0, 1)),
    );
  }
  return m.no('monolito_volume_de_luz', 'Volume de luz · camadas');
}

/// O ASTRONAUTA (o explorador do Abismo ate o seu modelo chegar): flutua
/// diante da porta, de frente para ela, subindo e descendo devagar.
SceneNode _astronauta(MonolitoModelos? modelos) {
  final base = _mundo(Vec3(_portaCentroLocal.x - 165, 118, _bd / 2 + 150));
  return SceneNode(
    id: 'monolito_astronauta',
    name: modelos == null ? 'Astronauta · provisorio' : 'Astronauta',
    size: 92,
    modelAsset: modelos?.astronauta ?? buildAbyssExplorer(),
    // Sem o modelo real, o explorador do Abismo em branco: o dele e
    // vermelho, e o contraluz magenta precisa de branco para desenhar.
    useModelMaterials: modelos != null,
    material: const Material3D(baseColor: Color(0xffe9e6df), roughness: .5),
    x: _ad(base.x),
    y: _sample((t) => base.y + 8 * math.sin(2 * math.pi * t / 5.2)),
    z: _ad(base.z),
    rotX: _sample((t) => -10 + 3 * math.sin(2 * math.pi * t / 7)),
    // De frente para a porta (o visor do modelo olha para +z local).
    rotY: _sample((t) => _giro - 100 + 6 * math.sin(2 * math.pi * t / 9)),
    rotZ: _ad(-6),
  );
}

/// O PORTAL (o modelo voxel importado) parado na abertura da porta, com
/// o roxo aceso pelo material emissivo que [acenderPortal] separou.
SceneNode _portal(MonolitoModelos modelos) {
  final p = _mundo(Vec3(_portaCentroLocal.x, 125, _bd / 2 + 22));
  return SceneNode(
    id: 'monolito_portal',
    name: 'Portal',
    size: 125,
    modelAsset: modelos.portal,
    x: _ad(p.x),
    y: _ad(p.y),
    z: _ad(p.z),
    rotY: _ad(_giro),
  );
}

/// A ARVORE ESCANEADA na posicao mais proxima da camera. Uma so: no
/// pintor em CPU cada copia custa a malha inteira de novo, e uma arvore
/// de verdade em primeiro plano ja faz o trabalho que as procedurais do
/// fundo nao fazem. O escaneamento e Z-para-cima: deitar em X poe o
/// tronco de pe.
List<SceneNode> _arvoresReais(MonolitoModelos modelos) => [
      for (var k = 0; k < 1; k++)
        SceneNode(
          id: 'monolito_arvore_real_$k',
          name: 'Arvore escaneada ${k + 1}',
          size: 420,
          modelAsset: modelos.arvore,
          x: _ad(_arvores[k].$1),
          y: _ad(420),
          z: _ad(_arvores[k].$2),
          rotX: _ad(-90),
          rotZ: _ad(ruido(k + 600) * 360),
        ),
    ];

/// Uma HASTE afunilada (tronco, galho): cilindro de [r0] a [r1].
void _haste(
  MalhaCodigo m,
  int material,
  Vec3 base,
  Vec3 eixo,
  double r0,
  double r1,
  double comprimento, {
  int lados = 6,
}) {
  final e = eixo.normalized;
  final ref = e.y.abs() < .9 ? const Vec3(0, 1, 0) : const Vec3(1, 0, 0);
  final u = ref.cross(e).normalized, v = e.cross(u);
  final topo = base + e * comprimento;
  for (var k = 0; k < lados; k++) {
    final a0 = 2 * math.pi * k / lados, a1 = 2 * math.pi * (k + 1) / lados;
    final d0 = u * math.cos(a0) + v * math.sin(a0);
    final d1 = u * math.cos(a1) + v * math.sin(a1);
    m.quadPlano(material, base + d0 * r0, base + d1 * r0, topo + d1 * r1,
        topo + d0 * r1, virado: d0 + d1);
  }
}

/// ARVORE NUA: tronco afunilado e galhos finos, como as da referencia.
MalhaCodigo _arvore(int semente, double altura) {
  final m = MalhaCodigo([
    materialCodigo('Casca', 0xff2b2622, rugosidade: .95),
  ]);
  const cima = Vec3(0, 1, 0);
  _haste(m, 0, Vec3.zero, cima, 13, 5, altura, lados: 7);
  final galhos = 7 + (ruido(semente * 3) * 5).floor();
  for (var g = 0; g < galhos; g++) {
    final h = altura * (.35 + .6 * ruido(semente * 31 + g));
    final az = 2 * math.pi * ruido(semente * 37 + g * 3);
    final el = (.25 + .5 * ruido(semente * 41 + g * 5)) * math.pi / 2;
    final dir = Vec3(math.cos(az) * math.cos(el), math.sin(el), math.sin(az) * math.cos(el));
    final comp = 110 + 170 * ruido(semente * 43 + g * 7);
    final base = Vec3(0, h, 0);
    _haste(m, 0, base, dir, 4.5, 1.6, comp, lados: 5);
    // Um galho secundario em metade deles.
    if (g.isEven) {
      final dir2 = Vec3(dir.x + .5 * (ruido(semente + g) - .5), dir.y + .4, dir.z + .5 * (ruido(semente * 7 + g) - .5)).normalized;
      _haste(m, 0, base + dir * (comp * .55), dir2, 2.2, .9, comp * .5, lados: 4);
    }
  }
  return m;
}

const _arvores = <(double, double, double, int)>[
  // x, z, altura, semente
  (-560, 240, 760, 1),
  (-820, -140, 900, 2),
  (600, 330, 720, 3),
  (860, -80, 880, 4),
  (-300, -720, 940, 5),
  (220, -820, 860, 6),
  (560, -640, 910, 7),
  (-680, -900, 980, 8),
  (980, -520, 840, 9),
  (-60, -1120, 1000, 10),
];

List<SceneNode> _arvoresDaFloresta({int desde = 0}) => [
      for (var k = desde; k < _arvores.length; k++)
        _arvore(_arvores[k].$4, _arvores[k].$3).no(
          'monolito_arvore_$k',
          'Arvore ${k + 1}',
          posicao: Vec3(_arvores[k].$1, _arvores[k].$3 / 2, _arvores[k].$2),
          rotY: _ad(ruido(k + 500) * 360),
        ),
    ];

/// A MATA DO FUNDO: troncos em silhueta que a neblina come.
SceneNode _mata() {
  final m = MalhaCodigo([
    materialCodigo('Tronco distante', 0xff1d1b19, rugosidade: 1),
  ]);
  _haste(m, 0, const Vec3(0, -500, 0), const Vec3(0, 1, 0), 16, 8, 1000, lados: 5);
  return m.no(
    'monolito_mata',
    'Mata do fundo',
    posicao: Vec3.zero,
    instancias: [
      for (var i = 0; i < 34; i++)
        Vec3(
          -2600 + ruido(i * 3 + 700) * 5200,
          500 + ruido(i * 3 + 702) * 160,
          -1300 - ruido(i * 3 + 701) * 1100,
        ),
    ],
  );
}

/// UM TUFO de grama alta e escura: quatro laminas finas.
MalhaCodigo _tufo(int variante) {
  final m = MalhaCodigo([
    materialCodigo('Grama alta', 0xff3a4a26, rugosidade: .7, doisLados: true),
  ]);
  final altura = 46.0 + 26 * ruido(variante * 7 + 1);
  const cima = Vec3(0, 1, 0);
  for (var k = 0; k < 4; k++) {
    final ang = (k / 4) * 2 * math.pi + variante * .7 + ruido(variante * 11 + k) * 1.1;
    final inclina = .18 + .35 * ruido(variante * 13 + k * 5);
    final h = altura * (.7 + .5 * ruido(variante * 17 + k * 3));
    final dir = Vec3(math.cos(ang), 0, math.sin(ang));
    final lado = Vec3(-dir.z, 0, dir.x) * 1.4;
    final base = dir * (2.5 * ruido(variante * 19 + k));
    final topo = base + dir * (h * inclina) + Vec3(0, h, 0);
    final n = (cima + dir * .4).normalized;
    final ia = m.vertice(0, base - lado - Vec3(0, altura / 2, 0), n);
    final ib = m.vertice(0, base + lado - Vec3(0, altura / 2, 0), n);
    final ic = m.vertice(0, topo - Vec3(0, altura / 2, 0), n);
    m.tri(0, ia, ib, ic);
  }
  return m;
}

/// ONDE A GRAMA NASCE: densa no primeiro plano (entre a camera e a
/// porta), rala perto do bloco, nenhuma dentro dele.
List<Vec3> _espalhaGrama(int semente, int quantos, double alturaDoTufo) {
  final out = <Vec3>[];
  var i = 0;
  while (out.length < quantos && i < quantos * 40) {
    final u = ruido(semente * 100003 + i * 3);
    final v = ruido(semente * 100003 + i * 3 + 1);
    final p = ruido(semente * 100003 + i * 3 + 2);
    i++;
    final x = -1400 + u * 2800, z = -600 + v * 1700;
    // Dentro do bloco (com folga), fora.
    final local = _localDe(Vec3(x, 0, z));
    if (local.x.abs() < _bw / 2 + 30 && local.z.abs() < _bd / 2 + 30) continue;
    var densidade = .12 + .8 * suave(((z - 200) / 700).clamp(0.0, 1.0));
    if (x.abs() > 900) densidade *= .5;
    if (p > densidade) continue;
    out.add(Vec3(x, alturaDoTufo / 2 - 4, z));
  }
  return out;
}

/// Coordenada de mundo em local do bloco (o inverso de [_mundo]).
Vec3 _localDe(Vec3 mundo) {
  final a = -_giro * math.pi / 180;
  final dx = mundo.x - _centroDoBloco.x, dz = mundo.z - _centroDoBloco.z;
  return Vec3(dx * math.cos(a) + dz * math.sin(a), mundo.y, -dx * math.sin(a) + dz * math.cos(a));
}

List<SceneNode> _grama() => [
      for (var v = 0; v < 6; v++)
        _tufo(v).no(
          'monolito_grama_$v',
          'Grama alta · ${v + 1}',
          posicao: Vec3.zero,
          instancias: _espalhaGrama(v + 1, 250, 46.0 + 26 * ruido(v * 7 + 1)),
        ),
    ];

/// O ARBUSTO da direita: um punhado de folhas escuras num elipsoide.
SceneNode _arbusto() {
  final m = MalhaCodigo([
    materialCodigo('Folha', 0xff1e2c18, rugosidade: .8, doisLados: true),
  ]);
  for (var k = 0; k < 3; k++) {
    final ang = k * 2 * math.pi / 3;
    final d = Vec3(math.cos(ang), 0, math.sin(ang));
    final lado = Vec3(-d.z, 0, d.x) * 9;
    m.quadPlano(0, d * 2 - lado, d * 2 + lado, d * 20 + lado + const Vec3(0, 8, 0), d * 20 - lado + const Vec3(0, 8, 0), virado: const Vec3(0, 1, 0));
  }
  return m.no(
    'monolito_arbusto',
    'Arbusto',
    posicao: Vec3.zero,
    instancias: [
      for (var i = 0; i < 90; i++)
        () {
          final a = 2 * math.pi * ruido(i * 3 + 900);
          final r = math.sqrt(ruido(i * 3 + 901));
          final y = ruido(i * 3 + 902);
          return Vec3(640 + r * math.cos(a) * 150, 20 + y * 190, 420 + r * math.sin(a) * 110);
        }(),
    ],
  );
}

// ============================================================== CAMERA

/// O PIVO: entre o astronauta e a porta, a meia altura.
Vec3 get _pivo => _mundo(Vec3(_portaCentroLocal.x - 70, 128, _bd / 2 + 60));

/// A CAMERA balanca de um lado para o outro num ciclo de 16 s, rasteira,
/// olhando o pivo pela quina do bloco.
Vec3 _posicaoDaCamera(double t) {
  final fase = 2 * math.pi * t / 16;
  final frente = _direcao(const Vec3(0, 0, 1));
  final direita = _direcao(const Vec3(1, 0, 0));
  final ang = .42 * math.sin(fase);
  final r = 700 + 50 * math.sin(fase + 1.1);
  final d = (frente * math.cos(ang) + direita * math.sin(ang)) * r;
  return Vec3(_pivo.x + d.x, 118 + 12 * math.sin(fase + .6), _pivo.z + d.z);
}

Camera3D _camera() {
  final pos = _posicaoDaCamera;
  final alvo = _pivo;
  return Camera3D(
    id: 'monolito_cam',
    name: 'Camera · balanco rasteiro 32 mm',
    posX: _sample((t) => pos(t).x),
    posY: _sample((t) => pos(t).y),
    posZ: _sample((t) => pos(t).z),
    poiX: _sample((t) => alvo.x + 10 * math.sin(2 * math.pi * t / 16 + .4)),
    poiY: _ad(alvo.y),
    poiZ: _ad(alvo.z),
    rotZ: _sample((t) => 1.5 * math.sin(2 * math.pi * t / 16)),
    focalLength: _ad(32),
    dof: DepthOfField(
      enabled: true,
      focusDistance: _sample((t) => (pos(t) - alvo).length),
      aperture: _ad(16),
      blurLevel: _ad(100),
      irisShape: IrisShape.hexagon,
      irisRoundness: _ad(60),
      highlightGain: _ad(18),
      highlightThreshold: _ad(.85),
      highlightSaturation: _ad(1.2),
    ),
  );
}

// ============================================================== PROJETO

VideoProject buildMonolitoTemplate({MonolitoModelos? modelos}) {
  final luzDaPorta = _mundo(Vec3(_portaCentroLocal.x, 130, _bd / 2 + 12));
  final scene = Scene3D(
    showFloorGrid: false,
    ambient: .16,
    skyColor: const Color(0xff2a3442),
    groundColor: const Color(0xff0a0a0a),
    environment: EnvironmentKind.noite,
    envReflect: .3,
    panorama: const Panorama3D(preset: PanoramaPreset.noite),
    fogColor: const Color(0xff2e3540),
    fogDensity: .0014,
    fogStart: 120,
    lights: [
      // A PORTA como luz: spot magenta com sombra, apontando para fora.
      Light3D(
        id: 'monolito_porta_spot',
        kind: Light3DKind.spot,
        color: const Color(0xffd66ae6),
        position: luzDaPorta,
        direction: _direcao(const Vec3(0, -.18, 1)),
        coneDegrees: 120,
        softness: .7,
        range: 1500,
        castsShadow: true,
        intensity: _ad(2.6),
      ),
      Light3D(
        id: 'monolito_porta_omni',
        kind: Light3DKind.point,
        color: const Color(0xffc45ad8),
        position: luzDaPorta,
        range: 900,
        intensity: _ad(1.2),
      ),
      // A LUA: fria, fraca, de cima e da esquerda.
      Light3D(
        id: 'monolito_lua',
        color: const Color(0xff6e7ea6),
        direction: const Vec3(.35, -.8, -.3),
        castsShadow: true,
        intensity: _ad(.55),
      ),
    ],
    nodes: [
      _mata(),
      _chao(),
      _monolito(),
      if (modelos != null) ..._arvoresReais(modelos),
      ..._arvoresDaFloresta(desde: modelos == null ? 0 : 1),
      _arbusto(),
      ..._grama(),
      _astronauta(modelos),
      if (modelos != null) _portal(modelos),
      _volumeDeLuz(),
    ],
  );

  const centro = Offset(monolitoWidth / 2, monolitoHeight / 2);
  return VideoProject(
    id: 'monolito_template',
    name: 'MONOLITO · O astronauta e a porta',
    createdAt: DateTime(2026, 9, 6),
    aspectRatio: 16 / 9,
    resolutionHeight: 720,
    fps: monolitoFps,
    layers: [
      AdjustmentLayer(
        id: 'monolito_grade',
        name: 'Gradacao · grao e vinheta',
        startTime: Duration.zero,
        duration: monolitoDuration,
        position: AnimatedOffset(centro),
        effects: [
          EffectInstance(type: EffectType.corrections, params: {
            'contraste': _ad(.10),
            'sombras': _ad(-.04),
            'temperatura': _ad(-.12),
            'saturacao': _ad(.12),
          }),
          EffectInstance(type: EffectType.vignette, params: {
            'quantidade': _ad(.45),
            'raio': _ad(.9),
            'suavidade': _ad(.8),
          }),
          EffectInstance(type: EffectType.filmGrain, params: {
            'intensidade': _ad(.08),
            'tamanho': _ad(1.3),
          }),
        ],
      ),
      Scene3DLayer(
        id: 'monolito_cena',
        name: 'MONOLITO · cena 3D editavel',
        startTime: Duration.zero,
        duration: monolitoDuration,
        position: AnimatedOffset(centro),
        showHelpers: false,
        camera: _camera(),
        scene: scene,
        // BLOOM da porta e do volume de luz.
        effects: [
          EffectInstance(
            type: EffectType.lightGlow,
            color: const Color(0xfff0b0ff),
            params: {
              'threshold': _ad(78),
              'raio': _ad(18),
              'intensity': _ad(95),
              'piramide': _ad(4),
            },
          ),
        ],
      ),
      // O FUNDO: a noite atras da neblina.
      ShapeLayer(
        id: 'monolito_fundo',
        name: 'Noite',
        startTime: Duration.zero,
        duration: monolitoDuration,
        position: AnimatedOffset(centro),
        contents: [
          ShapeParametric(
            kind: ParamShapeKind.rect,
            sizeX: _ad(monolitoWidth + 4),
            sizeY: _ad(monolitoHeight + 4),
          ),
          ShapeGradientFill(
            colorA: const Color(0xff1a1f24),
            colorB: const Color(0xff101214),
            angleDeg: 90,
          ),
        ],
      ),
    ],
  );
}

/// Triangulos por quadro, contando instancias — o numero que o teste de
/// orcamento segura.
int monolitoTriangles(VideoProject p) {
  var total = 0;
  for (final layer in p.layers) {
    if (layer is! Scene3DLayer) continue;
    for (final n in layer.scene.nodes) {
      final tris = n.modelAsset?.triangleCount ?? 0;
      total += tris * math.max(1, n.instances.length);
    }
  }
  return total;
}
