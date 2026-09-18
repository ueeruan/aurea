import 'dart:math' as math;
import 'dart:ui';

import '../../editor/domain/camera3d.dart';
import '../../editor/domain/effect.dart';
import '../../editor/domain/element3d.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/panorama3d.dart';
import '../../editor/domain/scene3d.dart';
import '../../editor/domain/video_project.dart';
import 'malha_codigo.dart';
import 'textura_procedural.dart';

/// PRISMA · DEZESSETE SEGUNDOS EM LOOP.
///
/// Recriacao, quadro a quadro, de uma referencia de motion 3D abstrato
/// (720x720, 24 fps, 54 s): um LOOP de dezessete segundos que a
/// referencia repete tres vezes. O que se reconstroi aqui e o loop —
/// tocado em repeticao ele e o video inteiro. Oito momentos, seis cenas
/// 3D, cada uma com a propria camera e as proprias luzes:
///
///   01 · AS GEMAS      (0,0–2,6 s)  dois poliedros facetados de pessego
///                                   entram pelos cantos, se encontram
///                                   ponta com ponta e uma faisca acende
///                                   entre eles; do encontro nasce uma
///                                   bolinha cromada
///   02 · O ANEL        (2,3–4,9 s)  seis discos de gradiente giram numa
///                                   orbita que aperta e abre
///   03 · O CUBO        (4,5–6,1 s)  um cubo de vidro fosco amarelo nasce
///                                   do centro, gira, vira losango chapado,
///                                   faisca, e foge
///   04 · O ALVO        (6,0–10,5 s) um quadrado de gradiente com aneis
///                                   empilhados dentro; o quadrado vira e
///                                   some, os aneis viram um alvo chapado e
///                                   a camera mergulha pelo furo
///   05 · OS CONES      (10,4–14,7 s) dois cones dourados ponta com ponta,
///                                   uma esfera cromada nasce na juncao,
///                                   cresce, vira listras de glitch e foge
///   06 · O FEIXE       (14,6–17 s)  duas esferas quentes se tocam, um feixe
///                                   de luz varre o ponto de contato e um
///                                   plano preto gira ate cobrir o quadro —
///                                   o preto que fecha o loop e o preto em
///                                   que ele comeca
///
/// Tudo e a mesma camada 3D editavel do app: geometria montada em codigo
/// (determinista — abrir duas vezes da o mesmo filme), texturas de
/// gradiente geradas na hora, keyframes que dao para pegar e mexer.
/// Os tempos vem da leitura da referencia a quatro quadros por segundo;
/// as formas, de quadros isolados a dez por segundo.
const prismaDuration = Duration(milliseconds: 17000);
const prismaFps = 24;
const prismaLado = 1080.0;

/// O comeco de cada cena, em segundos — e o que os marcadores mostram.
const prismaCenas = [0.0, 2.3, 4.5, 6.0, 10.4, 14.6];

/// O teto de triangulos por quadro (ver flor_template.dart).
const prismaTriangleBudget = 10000;

Duration _t(num s) => Duration(microseconds: (s * 1000000).round());
AnimatedDouble _ad(double v) => AnimatedDouble(v);
AnimatedDouble _keys(List<(num, num)> v, {Easing ease = Easing.linear}) =>
    AnimatedDouble(v.first.$2.toDouble(), [
      for (final k in v)
        Keyframe(time: _t(k.$1), value: k.$2.toDouble(), ease: ease),
    ]);

/// Uma curva do tempo amostrada a 12 Hz entre [inicio] e [fim]: vira
/// keyframe de verdade, que da para editar.
AnimatedDouble _curva(num inicio, num fim, double Function(double) f) {
  const hz = 12;
  final n = ((fim - inicio) * hz).round();
  return AnimatedDouble(f(inicio.toDouble()), [
    for (var i = 0; i <= n; i++)
      Keyframe(time: _t(inicio + i / hz), value: f(inicio + i / hz)),
  ]);
}

double _suave(double k) {
  final s = k.clamp(0.0, 1.0);
  return s * s * (3 - 2 * s);
}

double _entre(double a, double b, double k) => a + (b - a) * _suave(k);

const _centro = Offset(prismaLado / 2, prismaLado / 2);
const _pontoDeVista = Vec3(0, 0, 1000);

// ============================================================ TEXTURAS

/// Um gradiente vertical de tres paradas, 4 x 64 pixels — o suficiente
/// para um disco chapado de gradiente, que e o que a referencia usa.
String _gradiente(int topo, int meio, int base) {
  final ct = Color(topo), cm = Color(meio), cb = Color(base);
  return pngDataUri(4, 64, (x, y, rgb) {
    final v = y / 63;
    final Color a, b;
    final double k;
    if (v < .5) {
      a = ct;
      b = cm;
      k = v / .5;
    } else {
      a = cm;
      b = cb;
      k = (v - .5) / .5;
    }
    rgb[0] = canal8(a.r + (b.r - a.r) * k);
    rgb[1] = canal8(a.g + (b.g - a.g) * k);
    rgb[2] = canal8(a.b + (b.b - a.b) * k);
  });
}

// ============================================================== MALHAS

/// A GEMA: um icosaedro com a tampa de um polo afundada — um poco
/// pentagonal escuro no lugar da ponta, que e a marca da referencia.
/// Faces planas de proposito (normal por face): e o facetado que vende a
/// pedra. O polo do poco fica em +y.
MalhaCodigo _gema() {
  final m = MalhaCodigo([
    materialCodigo(
      'Pessego',
      0xfff4b46c,
      rugosidade: .42,
      metal: .06,
      brilho: .05,
    ),
    materialCodigo('Poco', 0xff6a1a4e, rugosidade: .5, brilho: .35),
  ]);
  const anelY = 0.4472;
  const anelR = 0.8944;
  Vec3 anel(int i, bool cima) {
    final a = 2 * math.pi * i / 5 + (cima ? 0 : math.pi / 5);
    return Vec3(
      anelR * math.cos(a),
      cima ? anelY : -anelY,
      anelR * math.sin(a),
    );
  }

  const fundo = Vec3(0, -1, 0);
  const poco = Vec3(0, 0.38, 0);
  for (var i = 0; i < 5; i++) {
    final a0 = anel(i, true), a1 = anel(i + 1, true);
    final b0 = anel(i, false), b1 = anel(i + 1, false);
    // A faixa do meio: dois triangulos por passo.
    m.triPlano(0, a0, b0, a1, virado: (a0 + b0 + a1) * (1 / 3));
    m.triPlano(0, a1, b0, b1, virado: (a1 + b0 + b1) * (1 / 3));
    // A tampa de baixo, pontuda.
    m.triPlano(0, b0, fundo, b1, virado: (b0 + fundo + b1) * (1 / 3));
    // O POCO: no lugar da tampa de cima, cinco faces que descem para
    // dentro. Viradas para fora do buraco (para o polo), e nao para o
    // centro da gema — e o que as deixa visiveis olhando de cima.
    m.triPlano(1, a0, a1, poco, virado: const Vec3(0, 1, 0));
  }
  return m;
}

/// Uma esfera lisa com UV (u = volta, v = altura): o gradiente da
/// textura corre de cima para baixo sem costura.
MalhaCodigo _esferaUV(
  List<Map<String, dynamic>> materiais, {
  int stacks = 14,
  int slices = 24,
}) {
  final m = MalhaCodigo(materiais);
  final grade = <List<int>>[];
  for (var st = 0; st <= stacks; st++) {
    final phi = math.pi * st / stacks;
    final y = math.cos(phi);
    final r = math.sin(phi);
    final linha = <int>[];
    for (var sl = 0; sl <= slices; sl++) {
      final a = 2 * math.pi * sl / slices;
      final p = Vec3(r * math.cos(a), y, r * math.sin(a));
      linha.add(
        m.vertice(0, p, p.normalized, uv: Offset(sl / slices, st / stacks)),
      );
    }
    grade.add(linha);
  }
  for (var st = 0; st < stacks; st++) {
    for (var sl = 0; sl < slices; sl++) {
      m.tri(0, grade[st][sl], grade[st + 1][sl], grade[st + 1][sl + 1]);
      m.tri(0, grade[st][sl], grade[st + 1][sl + 1], grade[st][sl + 1]);
    }
  }
  return m;
}

/// Um anel chapado no plano XY (normal +z), entre [rIn] e [rOut], com a
/// textura correndo de cima para baixo pelo anel inteiro.
MalhaCodigo _anel(
  List<Map<String, dynamic>> materiais,
  double rIn,
  double rOut, {
  int seg = 48,
}) {
  final m = MalhaCodigo(materiais);
  Offset uv(double x, double y) => Offset(.5, (rOut - y) / (2 * rOut));
  for (var i = 0; i < seg; i++) {
    final a0 = 2 * math.pi * i / seg, a1 = 2 * math.pi * (i + 1) / seg;
    final p0 = Vec3(rIn * math.cos(a0), rIn * math.sin(a0), 0);
    final p1 = Vec3(rOut * math.cos(a0), rOut * math.sin(a0), 0);
    final p2 = Vec3(rOut * math.cos(a1), rOut * math.sin(a1), 0);
    final p3 = Vec3(rIn * math.cos(a1), rIn * math.sin(a1), 0);
    m.triPlano(
      0,
      p0,
      p1,
      p2,
      ua: uv(p0.x, p0.y),
      ub: uv(p1.x, p1.y),
      uc: uv(p2.x, p2.y),
      virado: const Vec3(0, 0, 1),
    );
    m.triPlano(
      0,
      p0,
      p2,
      p3,
      ua: uv(p0.x, p0.y),
      ub: uv(p2.x, p2.y),
      uc: uv(p3.x, p3.y),
      virado: const Vec3(0, 0, 1),
    );
  }
  return m;
}

/// Um quadrado 2 x 2 no plano XY com gradiente vertical.
MalhaCodigo _quadrado(List<Map<String, dynamic>> materiais) {
  final m = MalhaCodigo(materiais);
  const a = Vec3(-1, -1, 0),
      b = Vec3(1, -1, 0),
      c = Vec3(1, 1, 0),
      d = Vec3(-1, 1, 0);
  m.triPlano(
    0,
    a,
    b,
    c,
    ua: const Offset(0, 1),
    ub: const Offset(1, 1),
    uc: const Offset(1, 0),
    virado: const Vec3(0, 0, 1),
  );
  m.triPlano(
    0,
    a,
    c,
    d,
    ua: const Offset(0, 1),
    ub: const Offset(1, 0),
    uc: const Offset(0, 0),
    virado: const Vec3(0, 0, 1),
  );
  return m;
}

/// Uma barra fina no plano XY — a faisca e o feixe. [comprimento] no eixo
/// escolhido, [espessura] no outro; a caixa fica unitaria no maior lado.
MalhaCodigo _barra(
  List<Map<String, dynamic>> materiais, {
  required bool vertical,
  double espessura = .012,
}) {
  final m = MalhaCodigo(materiais);
  final hx = vertical ? espessura : 1.0, hy = vertical ? 1.0 : espessura;
  m.quadPlano(
    0,
    Vec3(-hx, -hy, 0),
    Vec3(hx, -hy, 0),
    Vec3(hx, hy, 0),
    Vec3(-hx, hy, 0),
    virado: const Vec3(0, 0, 1),
  );
  return m;
}

SceneNode _no(
  MalhaCodigo m,
  String id,
  String nome, {
  required double tamanho,
  AnimatedDouble? x,
  AnimatedDouble? y,
  AnimatedDouble? z,
  AnimatedDouble? rotX,
  AnimatedDouble? rotY,
  AnimatedDouble? rotZ,
  AnimatedDouble? escala,
  String? paiId,
}) => SceneNode(
  id: id,
  name: nome,
  size: tamanho,
  x: x,
  y: y,
  z: z,
  rotX: rotX,
  rotY: rotY,
  rotZ: rotZ,
  scale: escala,
  parentId: paiId,
  modelAsset: m.asset(nome),
);

List<Map<String, dynamic>> _chapado(
  String nome,
  String textura, {
  double brilho = 0,
}) => [
  materialCodigo(
    nome,
    0xffffffff,
    semLuz: true,
    imagem: textura,
    brilho: brilho,
  ),
];

// ============================================================= CAMERAS

Camera3D _cameraFixa(String id, String nome, {double lente = 40}) => Camera3D(
  id: id,
  name: nome,
  posX: _ad(_pontoDeVista.x),
  posY: _ad(_pontoDeVista.y),
  posZ: _ad(_pontoDeVista.z),
  poiX: _ad(0),
  poiY: _ad(0),
  poiZ: _ad(0),
  focalLength: _ad(lente),
);

Camera3D _cameraCurva(
  String id,
  String nome,
  num inicio,
  num fim,
  Vec3 Function(double) posicao, {
  Vec3 Function(double)? alvo,
  double lente = 40,
}) => Camera3D(
  id: id,
  name: nome,
  posX: _curva(inicio, fim, (t) => posicao(t).x),
  posY: _curva(inicio, fim, (t) => posicao(t).y),
  posZ: _curva(inicio, fim, (t) => posicao(t).z),
  poiX: _curva(inicio, fim, (t) => (alvo?.call(t) ?? const Vec3(0, 0, 0)).x),
  poiY: _curva(inicio, fim, (t) => (alvo?.call(t) ?? const Vec3(0, 0, 0)).y),
  poiZ: _curva(inicio, fim, (t) => (alvo?.call(t) ?? const Vec3(0, 0, 0)).z),
  focalLength: _ad(lente),
);

Scene3DLayer _camada(
  String id,
  String nome,
  num inicio,
  num fim, {
  required Scene3D cena,
  required Camera3D camera,
  AnimatedDouble? opacidade,
  List<EffectInstance> efeitos = const [],
}) => Scene3DLayer(
  id: id,
  name: nome,
  startTime: _t(inicio),
  duration: _t(fim - inicio),
  position: AnimatedOffset(_centro),
  showHelpers: false,
  camera: camera,
  scene: cena,
  opacity: opacidade,
  effects: efeitos,
);

EffectInstance _glow(
  String id, {
  double limiar = 74,
  double raio = 28,
  double forca = 120,
}) => EffectInstance(
  id: id,
  type: EffectType.brilho,
  color: const Color(0xfffff3e4),
  params: {
    'threshold': _ad(limiar),
    'raio': _ad(raio),
    'intensity': _ad(forca),
    'piramide': _ad(3),
  },
);

// ======================================================== 01 · AS GEMAS

/// Duas gemas entram pelos cantos (0–1 s), param ponta com ponta e uma
/// faisca pisca entre elas (1,3–2,0 s); as gemas recuam e do ponto do
/// encontro nasce uma bolinha cromada (2,0–2,6 s). O poco escuro de cada
/// gema fica virado para a outra.
Scene3DLayer _cenaGemas() {
  const tamanho = 300.0;
  final gema = _gema();
  // A ponta que toca e o anel do poco. O modelo e normalizado pela caixa
  // (centro e maior lado), entao a altura do anel em unidades do no e a
  // do anel menos o centro, dividida pelo meio-lado — e nao 0,447.
  final caixa = gema.caixa();
  final ponta = (0.4472 - caixa.centro.y) / caixa.meio * tamanho;
  final encosto = ponta + 30;
  final faisca = _barra([
    materialCodigo('Faisca', 0xffffffff, semLuz: true, brilho: 1.0),
  ], vertical: true);
  final cena = Scene3D(
    showFloorGrid: false,
    background: null,
    ambient: .16,
    skyColor: const Color(0xff1a1220),
    groundColor: const Color(0xff120a10),
    environment: EnvironmentKind.noite,
    envReflect: .22,
    panorama: const Panorama3D(preset: PanoramaPreset.neon, intensity: .5),
    lights: [
      // A luz quente da frente, de cima e da direita: e ela que da o
      // pessego das faces.
      Light3D(
        id: 'prisma_gema_chave',
        color: const Color(0xffffd4a2),
        direction: const Vec3(-.35, -.6, -.7),
        intensity: _ad(2.3),
      ),
      // Magenta subindo da esquerda: as faces de baixo ficam rosadas.
      Light3D(
        id: 'prisma_gema_magenta',
        color: const Color(0xffe23d9a),
        direction: const Vec3(.55, .55, -.5),
        intensity: _ad(1.15),
      ),
      // Azul de tras, pela direita: o contorno frio.
      Light3D(
        id: 'prisma_gema_azul',
        color: const Color(0xff5b8dff),
        direction: const Vec3(-.6, -.2, .75),
        intensity: _ad(1.5),
      ),
    ],
    nodes: [
      _no(
        gema,
        'prisma_gema_cima',
        'Gema de cima',
        tamanho: tamanho,
        x: _keys([(0, -470), (1.0, 0)], ease: Easing.easeOut),
        y: _keys([(0, 640), (1.0, encosto), (2.05, encosto), (2.6, 980)]),
        rotX: _ad(180),
        rotY: _keys([(0, 55), (1.0, 0)], ease: Easing.easeOut),
        rotZ: _keys([(0, -38), (1.0, 0)], ease: Easing.easeOut),
      ),
      _no(
        gema,
        'prisma_gema_baixo',
        'Gema de baixo',
        tamanho: tamanho,
        x: _keys([(0, 470), (1.0, 0)], ease: Easing.easeOut),
        y: _keys([(0, -640), (1.0, -encosto), (2.05, -encosto), (2.6, -980)]),
        rotY: _keys([(0, -55), (1.0, 0)], ease: Easing.easeOut),
        rotZ: _keys([(0, 38), (1.0, 0)], ease: Easing.easeOut),
      ),
      // A FAISCA: uma barra branca que acende, pisca e apaga. Escala e
      // o que a liga e desliga — zero e invisivel.
      _no(
        faisca,
        'prisma_faisca',
        'Faisca',
        tamanho: 34,
        escala: _keys([
          (1.25, 0),
          (1.32, 1),
          (1.45, .35),
          (1.58, 1),
          (1.72, .5),
          (1.88, 1),
          (2.02, .2),
          (2.1, 0),
        ]),
      ),
      // A BOLINHA CROMADA que nasce do encontro.
      SceneNode(
        id: 'prisma_bolinha',
        name: 'Bolinha cromada',
        kind: Element3DKind.sphere,
        material: const Material3D(
          name: 'Cromo',
          baseColor: Color(0xffe4e9f4),
          metallic: 1,
          roughness: .06,
          reflectivity: 1,
        ),
        size: 58,
        scale: _keys([(1.95, 0), (2.3, 1), (2.6, 1.15)], ease: Easing.easeOut),
      ),
    ],
  );
  return _camada(
    'prisma_gemas',
    '01 · As gemas',
    0,
    2.6,
    cena: cena,
    camera: _cameraFixa('prisma_cam_gemas', '01 · Gemas / 40 mm'),
    opacidade: _keys([(0, 1), (2.45, 1), (2.6, 0)]),
    efeitos: [_glow('prisma_fx_gemas_glow', limiar: 78, raio: 24, forca: 130)],
  );
}

// ========================================================== 02 · O ANEL

/// Seis discos de gradiente numa orbita: nascem do centro (0–0,6 s),
/// giram numa roda que aperta e abre (o que os faz se sobrepor), e no
/// fim a roda abre de vez e os discos somem (2,0–2,6 s) enquanto o cubo
/// nasce por baixo.
Scene3DLayer _cenaAnel() {
  const cores = [
    (0xfff9c2ea, 0xffe5a8f0, 0xffc98cf2), // rosa -> lavanda
    (0xff6cb3f8, 0xff9aa9f3, 0xffc9a4f4), // azul -> lilas
    (0xfff4744f, 0xffd4482e, 0xff8d3520), // vermelho -> queimado
    (0xfff7aa4c, 0xfff5bf62, 0xfff7dc84), // laranja -> ouro
    (0xfff6b6e6, 0xfff8cfb0, 0xfff9e68c), // rosa -> amarelo
    (0xff2d8d69, 0xff3f8f99, 0xff5d95d8), // verde -> azul
  ];
  double raio(double t) {
    if (t < .6) return 205 * _suave(t / .6);
    if (t > 2.0) {
      return 205 +
          70 * math.sin(2 * math.pi * .42 * 1.4) +
          (t - 2.0) / .6 * 190;
    }
    return 205 + 70 * math.sin(2 * math.pi * .42 * (t - .6));
  }

  double escala(double t) {
    if (t < .5) return _suave(t / .5);
    if (t > 2.0) return 1 - _suave((t - 2.0) / .6);
    return 1;
  }

  final nos = <SceneNode>[];
  for (var i = 0; i < 6; i++) {
    final (topo, meio, base) = cores[i];
    final m = _esferaUV([
      materialCodigo(
        'Disco ${i + 1}',
        0xffffffff,
        semLuz: true,
        imagem: _gradiente(topo, meio, base),
      ),
    ]);
    double angulo(double t) =>
        2 * math.pi * i / 6 +
        1.1 * t +
        .25 * math.sin(2 * math.pi * .3 * t + i);
    nos.add(
      _no(
        m,
        'prisma_disco_$i',
        'Disco ${i + 1}',
        tamanho: 122,
        x: _curva(0, 2.6, (t) => raio(t) * math.cos(angulo(t))),
        y: _curva(0, 2.6, (t) => raio(t) * math.sin(angulo(t))),
        // A profundidade oscila para os discos passarem uns na frente dos
        // outros — e a sobreposicao que a referencia mostra.
        z: _curva(0, 2.6, (t) => 90 * math.sin(angulo(t) + t)),
        escala: _curva(0, 2.6, escala),
      ),
    );
  }
  final cena = Scene3D(
    showFloorGrid: false,
    background: null,
    ambient: .6,
    environment: EnvironmentKind.estudio,
    envReflect: 0,
    nodes: nos,
  );
  return _camada(
    'prisma_anel',
    '02 · O anel',
    2.3,
    4.9,
    cena: cena,
    camera: _cameraFixa('prisma_cam_anel', '02 · Anel / 40 mm'),
    opacidade: _keys([(0, 0), (.2, 1), (2.45, 1), (2.6, 0)]),
    efeitos: [_glow('prisma_fx_anel_glow', limiar: 82, raio: 22, forca: 70)],
  );
}

// ========================================================== 03 · O CUBO

/// O cubo de vidro fosco: nasce do centro, gira, vira losango chapado
/// (a face de frente, girada a 45 graus), uma faisca, e foge para cima
/// e para a direita encolhendo.
Scene3DLayer _cenaCubo() {
  final faisca = _barra([
    materialCodigo('Faisca do cubo', 0xffffffff, semLuz: true, brilho: 1.0),
  ], vertical: true);
  final cena = Scene3D(
    showFloorGrid: false,
    background: null,
    ambient: .3,
    skyColor: const Color(0xfffff2c0),
    groundColor: const Color(0xff2a2410),
    environment: EnvironmentKind.estudio,
    envReflect: .5,
    lights: [
      Light3D(
        id: 'prisma_cubo_chave',
        color: const Color(0xfffff0c8),
        direction: const Vec3(-.4, -.55, -.72),
        intensity: _ad(2.1),
      ),
      Light3D(
        id: 'prisma_cubo_fundo',
        color: const Color(0xffe0b050),
        direction: const Vec3(.6, .3, .74),
        intensity: _ad(.9),
      ),
    ],
    nodes: [
      SceneNode(
        id: 'prisma_cubo',
        name: 'Cubo de vidro',
        kind: Element3DKind.cube,
        material: const Material3D(
          name: 'Vidro fosco',
          baseColor: Color(0xfff2e28c),
          kind: MaterialKind.transparent,
          opacity: .82,
          roughness: .55,
          metallic: .05,
          emissive: .16,
          doubleSided: true,
        ),
        size: 230,
        scale: _keys([(0, 0), (.35, 1), (1.15, 1), (1.5, .35), (1.62, 0)]),
        x: _keys([(1.15, 0), (1.62, 300)], ease: Easing.easeIn),
        y: _keys([(1.15, 0), (1.62, 330)], ease: Easing.easeIn),
        rotX: _keys([(0, 28), (.9, 22), (1.15, 0)]),
        rotY: _keys([(0, 30), (.9, 75), (1.15, 90)]),
        rotZ: _keys([(.9, 0), (1.15, 45), (1.62, 45)]),
      ),
      // O MIOLO mais escuro: as manchas que o vidro fosco deixa ver.
      SceneNode(
        id: 'prisma_cubo_miolo',
        name: 'Miolo do cubo',
        kind: Element3DKind.cube,
        material: const Material3D(
          name: 'Miolo',
          baseColor: Color(0xffb0902c),
          roughness: .7,
          metallic: .2,
        ),
        size: 140,
        scale: _keys([(0, 0), (.35, 1), (1.15, 1), (1.5, .35), (1.62, 0)]),
        x: _keys([(1.15, 0), (1.62, 300)], ease: Easing.easeIn),
        y: _keys([(1.15, 0), (1.62, 330)], ease: Easing.easeIn),
        rotX: _keys([(0, 28), (.9, 22), (1.15, 0)]),
        rotY: _keys([(0, 30), (.9, 75), (1.15, 90)]),
        rotZ: _keys([(.9, 0), (1.15, 45), (1.62, 45)]),
      ),
      _no(
        faisca,
        'prisma_faisca_cubo',
        'Faisca do cubo',
        tamanho: 140,
        escala: _keys([(1.02, 0), (1.1, 1), (1.24, 0)]),
      ),
    ],
  );
  return _camada(
    'prisma_cubo_cena',
    '03 · O cubo',
    4.5,
    6.1,
    cena: cena,
    camera: _cameraFixa('prisma_cam_cubo', '03 · Cubo / 40 mm'),
    opacidade: _keys([(0, 0), (.15, 1), (1.5, 1), (1.6, 0)]),
    efeitos: [_glow('prisma_fx_cubo_glow', limiar: 70, raio: 30, forca: 140)],
  );
}

// ========================================================== 04 · O ALVO

/// O quadrado de gradiente com seis aneis empilhados dentro. Nasce
/// pequeno no canto de baixo (0–0,3 s), roda de losango para quadrado
/// (0,95–1,35 s), o quadrado vira de lado e some enquanto os aneis viram
/// um alvo chapado (1,3–2,3 s), o alvo gira devagar e a camera mergulha
/// pelo furo (3,3–4,5 s). Os aneis de dentro ficam mais perto da camera:
/// e isso que, de esguelha, os empilha como na referencia.
Scene3DLayer _cenaAlvo() {
  const cores = [
    (0xfff8c68e, 0xfff58a5e, 0xfff0704a), // 0, o de dentro: pessego -> vermelho
    (0xfff4a2d2, 0xffc586e0, 0xff9c6be3), // rosa -> roxo
    (0xfff7da7e, 0xfff5bf62, 0xfff2a65a), // amarelo -> laranja
    (0xff6e9cf5, 0xff9d9bf3, 0xffc79cf0), // azul -> lilas
    (0xfff5a3cf, 0xfff8c8de, 0xfff7d6e3), // rosa -> branco
    (0xfff28c5a, 0xfff49f60, 0xfff7b26b), // laranja
  ];
  final nos = <SceneNode>[
    // O GRUPO: um nulo que carrega tudo. Posicao, rotacao e escala do
    // conjunto moram aqui.
    SceneNode(
      id: 'prisma_alvo_grupo',
      name: 'Alvo (grupo)',
      isNull: true,
      x: _keys([(0, 300), (.3, 0)], ease: Easing.easeOut),
      y: _keys([(0, -280), (.3, 0)], ease: Easing.easeOut),
      scale: _keys([
        (0, .12),
        (.3, 1),
        (3.3, 1),
        (4.45, 4.6),
      ], ease: Easing.easeIn),
      rotZ: _keys([(0, 45), (.95, 45), (1.35, 0), (2.3, 0), (4.5, 40)]),
      rotX: _keys([(1.3, 0), (1.75, 55), (2.3, 0)]),
    ),
    _no(
      _quadrado(
        _chapado('Quadrado', _gradiente(0xfff5a18a, 0xffee6c8a, 0xff8c4fb3)),
      ),
      'prisma_alvo_quadrado',
      'Quadrado',
      tamanho: 330,
      z: _ad(-12),
      paiId: 'prisma_alvo_grupo',
      // Vira de lado (90 graus) e desaparece na propria espessura.
      rotX: _keys([(1.3, 0), (1.75, 90)], ease: Easing.easeIn),
      escala: _keys([(1.7, 1), (1.78, 0)]),
    ),
  ];
  for (var k = 0; k < 6; k++) {
    final (topo, meio, base) = cores[k];
    final rIn = 42.0 + 40 * k, rOut = rIn + 36;
    nos.add(
      _no(
        _anel(
          _chapado('Anel ${k + 1}', _gradiente(topo, meio, base)),
          rIn,
          rOut,
        ),
        'prisma_alvo_anel_$k',
        'Anel ${k + 1}',
        tamanho: rOut,
        z: _ad(8 + 18.0 * (5 - k)),
        paiId: 'prisma_alvo_grupo',
      ),
    );
  }
  final cena = Scene3D(
    showFloorGrid: false,
    background: null,
    ambient: .6,
    environment: EnvironmentKind.estudio,
    envReflect: 0,
    nodes: nos,
  );
  return _camada(
    'prisma_alvo',
    '04 · O alvo',
    6.0,
    10.5,
    cena: cena,
    camera: _cameraCurva(
      'prisma_cam_alvo',
      '04 · Alvo / 40 mm',
      0,
      4.5,
      (t) => Vec3(0, 0, _entre(1000, 820, (t - 3.3) / 1.2)),
    ),
    opacidade: _keys([(0, 0), (.15, 1), (4.35, 1), (4.5, 0)]),
    efeitos: [_glow('prisma_fx_alvo_glow', limiar: 84, raio: 20, forca: 60)],
  );
}

// ========================================================= 05 · OS CONES

/// Dois cones dourados ponta com ponta. A camera comeca olhando de cima
/// (ve dois discos) e desce para o lado revelando a ampulheta (0–1 s);
/// uma esfera cromada nasce na juncao e cresce (1,25–1,9 s); os cones
/// somem e a esfera, ja azul e branca, treme em listras de glitch
/// (2,5–3,6 s) e foge para o canto (3,4–4,2 s).
Scene3DLayer _cenaCones() {
  const tamanho = 430.0;
  const encosto = 1.1 * tamanho;
  const cromo = Material3D(
    name: 'Cromo',
    baseColor: Color(0xffdde6f5),
    metallic: 1,
    roughness: .05,
    reflectivity: 1,
  );
  final pilula = _esferaUV([
    materialCodigo(
      'Pilula',
      0xffffffff,
      semLuz: true,
      brilho: .3,
      imagem: _gradiente(0xff9cc7ff, 0xffffffff, 0xff4a78e8),
    ),
  ]);
  final cena = Scene3D(
    showFloorGrid: false,
    background: null,
    ambient: .14,
    skyColor: const Color(0xffffd9a8),
    groundColor: const Color(0xff2a1608),
    environment: EnvironmentKind.porDoSol,
    envReflect: .6,
    panorama: const Panorama3D(preset: PanoramaPreset.porDoSol, intensity: .9),
    lights: [
      Light3D(
        id: 'prisma_cone_chave',
        color: const Color(0xffffe2b4),
        direction: const Vec3(.4, -.5, -.75),
        intensity: _ad(2.4),
      ),
      Light3D(
        id: 'prisma_cone_rim',
        color: const Color(0xffff8a3c),
        direction: const Vec3(-.5, .6, -.25),
        intensity: _ad(1.3),
      ),
    ],
    nodes: [
      for (final (id, nome, y, rotX) in [
        ('prisma_cone_cima', 'Cone de cima', encosto, 0.0),
        ('prisma_cone_baixo', 'Cone de baixo', -encosto, 180.0),
      ])
        SceneNode(
          id: id,
          name: nome,
          kind: Element3DKind.cone,
          material: const Material3D(
            name: 'Ouro',
            baseColor: Color(0xffe9a83e),
            metallic: .85,
            roughness: .28,
            reflectivity: .8,
          ),
          size: tamanho,
          y: _ad(y),
          rotX: _ad(rotX),
          scale: _keys([(0, .35), (.6, 1), (2.4, 1), (2.75, 0)]),
        ),
      SceneNode(
        id: 'prisma_esfera_cromo',
        name: 'Esfera cromada',
        kind: Element3DKind.sphere,
        material: cromo,
        size: 62,
        scale: _keys([(1.25, 0), (1.9, 1), (2.4, 1), (2.7, 0)]),
      ),
      _no(
        pilula,
        'prisma_pilula',
        'Esfera de glitch',
        tamanho: 62,
        escala: _keys([(2.4, 0), (2.7, 2.2), (3.5, 2.2), (4.25, .12)]),
        x: _keys([(3.4, 0), (4.25, 720)], ease: Easing.easeIn),
        y: _keys([(3.4, 0), (4.25, 620)], ease: Easing.easeIn),
      ),
    ],
  );
  return _camada(
    'prisma_cones',
    '05 · Os cones',
    10.4,
    14.7,
    cena: cena,
    camera: _cameraCurva(
      'prisma_cam_cones',
      '05 · Cones / 40 mm',
      0,
      4.3,
      (t) => Vec3(
        _entre(-640, 0, t / 1.0),
        _entre(520, 0, t / 1.0),
        _entre(560, 1000, t / 1.0),
      ),
    ),
    opacidade: _keys([(0, 0), (.12, 1), (4.2, 1), (4.3, 0)]),
    efeitos: [
      _glow('prisma_fx_cones_glow', limiar: 76, raio: 26, forca: 110),
      // O GLITCH em listras, so enquanto a esfera treme.
      EffectInstance(
        id: 'prisma_fx_glitch',
        type: EffectType.glitchify,
        params: {
          'amount': _keys([(2.45, 0), (2.7, 1.6), (3.4, 1.4), (3.62, 0)]),
          'speed': _ad(6),
          'intervalo': _ad(.1),
          'deslize': _ad(.2),
          'escala': _ad(.6),
          'cor': _ad(.6),
          'luz': _ad(.5),
          'desfoque': _ad(.3),
          'rgb': _ad(.6),
        },
      ),
    ],
  );
}

// ========================================================== 06 · O FEIXE

/// Duas esferas quentes vem dos cantos e se tocam (0–1,45 s); no ponto
/// de contato um feixe de luz varre (0,75–1,8 s) e um plano preto nasce
/// dali girando ate cobrir tudo (1,5–2,4 s). O preto que fecha e o preto
/// em que o loop comeca.
Scene3DLayer _cenaFeixe() {
  final quente = _gradiente(0xfff8c29a, 0xffee5a4e, 0xfffae49a);
  final feixe = _barra(
    [materialCodigo('Feixe', 0xfffff1d8, semLuz: true, brilho: .9)],
    vertical: false,
    espessura: .004,
  );
  final cena = Scene3D(
    showFloorGrid: false,
    background: null,
    ambient: .6,
    environment: EnvironmentKind.estudio,
    envReflect: 0,
    nodes: [
      for (final (id, nome, sinal) in [
        ('prisma_quente_a', 'Esfera quente A', 1.0),
        ('prisma_quente_b', 'Esfera quente B', -1.0),
      ])
        _no(
          _esferaUV([
            materialCodigo(
              nome,
              0xffffffff,
              semLuz: true,
              brilho: .12,
              imagem: quente,
            ),
          ]),
          id,
          nome,
          tamanho: 205,
          x: _keys([
            (0, 620 * sinal),
            (1.0, 150 * sinal),
            (1.45, 0),
          ], ease: Easing.easeOut),
          y: _keys([
            (0, 520 * sinal),
            (1.0, 170 * sinal),
            (1.45, 212 * sinal),
          ], ease: Easing.easeOut),
          escala: _keys([(0, .6), (.6, 1)], ease: Easing.easeOut),
        ),
      _no(
        feixe,
        'prisma_feixe',
        'Feixe',
        tamanho: 760,
        z: _ad(30),
        escala: _keys([(.75, 0), (.9, 1), (1.6, 1), (1.8, 0)]),
        rotZ: _keys([(.75, 28), (1.6, -18)]),
      ),
      // O PLANO PRETO: nasce do ponto de contato e gira ate cobrir.
      SceneNode(
        id: 'prisma_cortina',
        name: 'Plano preto',
        kind: Element3DKind.plane,
        material: const Material3D(
          name: 'Preto',
          baseColor: Color(0xff050508),
          kind: MaterialKind.unlit,
          doubleSided: true,
        ),
        size: 900,
        z: _ad(60),
        scale: _keys([
          (1.5, 0),
          (1.8, .3),
          (2.1, .7),
          (2.4, 2.4),
        ], ease: Easing.easeIn),
        rotZ: _keys([(1.5, 40), (2.3, 22)]),
      ),
    ],
  );
  return _camada(
    'prisma_feixe_cena',
    '06 · O feixe',
    14.6,
    17.0,
    cena: cena,
    camera: _cameraFixa('prisma_cam_feixe', '06 · Feixe / 40 mm'),
    opacidade: _keys([(0, 0), (.15, 1)]),
    efeitos: [_glow('prisma_fx_feixe_glow', limiar: 72, raio: 34, forca: 150)],
  );
}

// ============================================================== O FUNDO

/// O preto do quadro e os brilhos de fundo — bolas de cor la atras,
/// desfocadas pelo efeito da camada, que acendem e apagam conforme a
/// cena: magenta e azul nas gemas, ambar no anel, magenta no alvo, rosa e
/// laranja no feixe.
Scene3DLayer _fundo() {
  SceneNode brilho(
    String id,
    String nome,
    int cor,
    double x,
    double y,
    double tamanho,
    List<(num, num)> escala,
  ) => SceneNode(
    id: id,
    name: nome,
    kind: Element3DKind.sphere,
    material: Material3D(
      name: nome,
      baseColor: Color(cor),
      kind: MaterialKind.unlit,
      opacity: .62,
    ),
    size: tamanho,
    x: _ad(x),
    y: _ad(y),
    z: _ad(-2600),
    scale: _keys(escala),
  );
  final cena = Scene3D(
    showFloorGrid: false,
    background: const Color(0xff070609),
    ambient: 1,
    environment: EnvironmentKind.estudio,
    envReflect: 0,
    nodes: [
      brilho(
        'prisma_fundo_magenta',
        'Brilho magenta',
        0xffb0247a,
        -1500,
        -1200,
        1100,
        [(0, 1), (2.4, 1), (2.7, 0), (6.0, 0), (6.3, 1), (10.3, 1), (10.6, 0)],
      ),
      brilho('prisma_fundo_azul', 'Brilho azul', 0xff2c55c8, 1450, 1250, 950, [
        (0, .9),
        (2.4, .9),
        (2.7, 0),
        (6.0, 0),
        (6.3, .6),
        (10.3, .6),
        (10.6, 0),
      ]),
      brilho(
        'prisma_fundo_ambar',
        'Brilho ambar',
        0xffe0a23a,
        1500,
        1300,
        1000,
        [(0, 0), (2.3, 0), (2.6, 1), (4.6, 1), (4.9, 0)],
      ),
      brilho('prisma_fundo_rosa', 'Brilho rosa', 0xffd0507a, 1500, 900, 1000, [
        (0, 0),
        (14.6, 0),
        (14.9, 1),
      ]),
      brilho(
        'prisma_fundo_laranja',
        'Brilho laranja',
        0xffe07a2a,
        -1500,
        -1000,
        1100,
        [(0, 0), (14.6, 0), (14.9, 1)],
      ),
    ],
  );
  return _camada(
    'prisma_fundo',
    'Fundo e brilhos',
    0,
    17.0,
    cena: cena,
    camera: _cameraFixa('prisma_cam_fundo', 'Fundo / 40 mm'),
    efeitos: [
    ],
  );
}

// ============================================================= PROJETO

VideoProject buildPrismaTemplate() {
  final cenas = [
    _cenaGemas(),
    _cenaAnel(),
    _cenaCubo(),
    _cenaAlvo(),
    _cenaCones(),
    _cenaFeixe(),
  ];
  return VideoProject(
    id: 'prisma_template',
    name: 'PRISMA · Dezessete segundos em loop',
    createdAt: DateTime(2026, 9, 7),
    aspectRatio: 1,
    resolutionHeight: prismaLado.round(),
    fps: prismaFps,
    markers: [
      for (var i = 0; i < cenas.length; i++)
        Marker(time: _t(prismaCenas[i]), label: cenas[i].name),
    ],
    layers: [
      // A GRADACAO: vinheta, um grao fino e um pouco de contraste — o
      // que separa render limpo de imagem.
      AdjustmentLayer(
        id: 'prisma_grade',
        name: 'Gradacao',
        startTime: Duration.zero,
        duration: prismaDuration,
        position: AnimatedOffset(_centro),
        effects: [
          EffectInstance(
              type: EffectType.brightnessContrast,
              params: { 'contrast': _ad(7) },
            ),
            EffectInstance(
              type: EffectType.hueSaturation,
              params: { 'master_saturation': _ad(8) },
            ),
        ],
      ),
      // As cenas, da ultima para a primeira: a lista e de cima para
      // baixo, e a cena mais nova fica por cima na transicao.
      for (final c in cenas.reversed) c,
      _fundo(),
    ],
  );
}

/// Triangulos por quadro no pior caso: soma de tudo que existe em cada
/// camada 3D (o motor so desenha o que esta visivel no instante).
int prismaTriangles(VideoProject p) {
  var total = 0;
  for (final layer in p.layers) {
    if (layer is! Scene3DLayer) continue;
    for (final n in layer.scene.nodes) {
      final tris =
          n.modelAsset?.triangleCount ??
          (n.isNull ? 0 : element3DMesh(n.kind).faces.length);
      total += tris * math.max(1, n.instances.length);
    }
  }
  return total;
}
