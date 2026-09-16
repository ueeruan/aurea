/// AS NOVE CENAS DO TESTE DE ESTRESSE — geradas, para rodar em qualquer
/// aparelho sem depender de arquivo nenhum.
///
/// O criterio de aprovacao nao e fps: e o app continuar vivo e
/// respondendo. Cada cena mira um recurso que ja matou app em celular:
/// chamadas demais, triangulos demais, texturas grandes, atlas de sombra,
/// bloom e animacao por quadro, e tudo isso ao mesmo tempo com video,
/// texto e efeitos 2D disputando a mesma GPU.
///
/// O que se ve aqui e so o dominio (puro, testavel). Video e texturas
/// sao arquivos que a tela de estresse gera e passa por caminho.
library;

import 'dart:math' as math;
import 'dart:ui' show Color;

import 'element3d.dart';
import 'keyframe.dart';
import 'scene3d.dart';

enum TesteDeEstresse {
  objetos100,
  poligonos1M,
  texturasGrandes,
  luzesESombras,
  pbrAnimacao,
  tresDMaisVideo,
  tresDMaisMotionGraph,
  tudoJunto,
  extrema,
}

class ReceitaDeEstresse {
  const ReceitaDeEstresse({
    required this.id,
    required this.titulo,
    required this.descricao,
    required this.cena,
    this.video = false,
    this.texto = false,
    this.efeitos = false,
    this.motionGraph = false,
  });

  final TesteDeEstresse id;
  final String titulo;
  final String descricao;
  final Scene3D cena;

  /// A tela poe uma camada de video atras da cena.
  final bool video;

  /// A tela poe uma camada de texto por cima.
  final bool texto;

  /// A tela poe uma forma com Unsharp Mask, Vignette e VHS Damage.
  final bool efeitos;

  /// A tela poe uma forma com cinquenta keyframes e uma expressao.
  final bool motionGraph;

  int get numero => id.index + 1;
}

// ------------------------------------------------------------- malhas

/// Uma esfera UV com [segmentos] meridianos e [aneis] paralelos: o jeito
/// mais simples de pedir um milhao de triangulos numa malha so.
///
/// Triangulos = segmentos x (aneis - 2) x 2 + 2 x segmentos.
Element3DMesh esferaUV(int segmentos, int aneis, {double raio = 1}) {
  final verts = <List<double>>[];
  final faces = <List<int>>[];
  verts.add([0, -raio, 0]); // polo de cima (y desce)
  for (var a = 1; a < aneis; a++) {
    final phi = math.pi * a / aneis;
    final y = -math.cos(phi) * raio;
    final r = math.sin(phi) * raio;
    for (var s = 0; s < segmentos; s++) {
      final theta = 2 * math.pi * s / segmentos;
      verts.add([math.cos(theta) * r, y, math.sin(theta) * r]);
    }
  }
  verts.add([0, raio, 0]); // polo de baixo
  final ultimo = verts.length - 1;
  int idx(int anel, int seg) => 1 + (anel - 1) * segmentos + (seg % segmentos);
  for (var s = 0; s < segmentos; s++) {
    faces.add([0, idx(1, s + 1), idx(1, s)]);
  }
  for (var a = 1; a < aneis - 1; a++) {
    for (var s = 0; s < segmentos; s++) {
      final a0 = idx(a, s), a1 = idx(a, s + 1);
      final b0 = idx(a + 1, s), b1 = idx(a + 1, s + 1);
      faces.add([a0, a1, b1]);
      faces.add([a0, b1, b0]);
    }
  }
  for (var s = 0; s < segmentos; s++) {
    faces.add([idx(aneis - 1, s), idx(aneis - 1, s + 1), ultimo]);
  }
  return Element3DMesh(verts, faces);
}

int triangulosDe(Element3DMesh m) {
  var n = 0;
  for (final f in m.faces) {
    if (f.length >= 3) n += f.length - 2;
  }
  return n;
}

// -------------------------------------------------------------- luzes

Light3D luzPrincipal({bool sombra = true}) => Light3D(
  kind: Light3DKind.directional,
  color: const Color(0xFFFFF4E0),
  intensity: AnimatedDouble(1.3),
  direction: const Vec3(-0.45, 0.7, 0.5),
  castsShadow: sombra,
  softness: 0.25,
);

Light3D luzDePreenchimento() => Light3D(
  kind: Light3DKind.directional,
  color: const Color(0xFFBFD4FF),
  intensity: AnimatedDouble(0.45),
  direction: const Vec3(0.6, 0.3, -0.4),
);

List<Light3D> luzesBasicas({bool sombra = true}) => [
  luzPrincipal(sombra: sombra),
  luzDePreenchimento(),
];

// -------------------------------------------------------------- cores

const _paleta = [
  Color(0xFFE94F64),
  Color(0xFFF2A950),
  Color(0xFF6ED37A),
  Color(0xFF52B9F2),
  Color(0xFFA57BF2),
  Color(0xFFF2F2F2),
  Color(0xFF3A3F4A),
  Color(0xFFD9B26F),
];

Material3D _materialVariado(int i, {double emissive = 0}) => Material3D(
  baseColor: _paleta[i % _paleta.length],
  metallic: (i % 4) / 3,
  roughness: 0.15 + ((i * 7) % 10) / 12,
  emissive: emissive,
);

// -------------------------------------------------------------- cenas

/// TESTE 1 — cem objetos numa grade: cem chamadas de desenho, cem
/// materiais, sombra da luz principal. Sem [fundo] a cena e transparente
/// (para o video atras aparecer).
Scene3D cenaObjetos100({Color? fundo = const Color(0xFF0B0E14)}) {
  final nodes = <SceneNode>[];
  const tipos = [
    Element3DKind.cube,
    Element3DKind.sphere,
    Element3DKind.cylinder,
    Element3DKind.cone,
    Element3DKind.pyramid,
  ];
  for (var i = 0; i < 100; i++) {
    final col = i % 10, lin = i ~/ 10;
    nodes.add(
      SceneNode(
        name: 'Objeto ${i + 1}',
        kind: tipos[i % tipos.length],
        material: _materialVariado(i),
        size: 44,
        x: AnimatedDouble(-315.0 + col * 70),
        y: AnimatedDouble(-315.0 + lin * 70),
        z: AnimatedDouble(((i * 37) % 200) - 100.0),
        rotY: AnimatedDouble(0, [
          Keyframe(time: Duration.zero, value: 0),
          Keyframe(time: const Duration(seconds: 5), value: 360),
        ]),
      ),
    );
  }
  return Scene3D(nodes: nodes, lights: luzesBasicas(), background: fundo);
}

/// TESTE 2 — um milhao de poligonos numa malha so (esfera de 720 x 700).
Scene3D cenaPoligonos1M() {
  final malha = esferaUV(720, 700);
  return Scene3D(
    nodes: [
      SceneNode(
        name: 'Um milhao',
        kind: Element3DKind.sphere,
        mesh: malha,
        lod: MeshLod3D.high,
        material: const Material3D(
          baseColor: Color(0xFFD9D9E3),
          metallic: 0.2,
          roughness: 0.35,
        ),
        size: 260,
        rotY: AnimatedDouble(0, [
          Keyframe(time: Duration.zero, value: 0),
          Keyframe(time: const Duration(seconds: 5), value: 180),
        ]),
      ),
    ],
    lights: luzesBasicas(),
    background: const Color(0xFF0B0E14),
  );
}

/// TESTE 3 — seis texturas grandes (a tela gera PNGs de 2048), uma por
/// objeto; sem caminhos, os objetos ficam sem textura e o teste vale
/// como "seis objetos".
Scene3D cenaTexturasGrandes(List<String> caminhos) {
  final nodes = <SceneNode>[];
  for (var i = 0; i < 6; i++) {
    final caminho = i < caminhos.length ? caminhos[i] : null;
    nodes.add(
      SceneNode(
        name: 'Textura ${i + 1}',
        kind: i.isEven ? Element3DKind.cube : Element3DKind.sphere,
        material: Material3D(
          baseColor: const Color(0xFFFFFFFF),
          roughness: 0.5,
          imagePath: caminho,
        ),
        size: 120,
        x: AnimatedDouble(-300.0 + (i % 3) * 300),
        y: AnimatedDouble(i < 3 ? -150 : 150),
        rotY: AnimatedDouble(0, [
          Keyframe(time: Duration.zero, value: 0),
          Keyframe(time: const Duration(seconds: 5), value: 360),
        ]),
      ),
    );
  }
  return Scene3D(
    nodes: nodes,
    lights: luzesBasicas(),
    background: const Color(0xFF0B0E14),
  );
}

/// TESTE 4 — uma direcional com sombra, seis spots com sombra e oito
/// pontuais sobre quarenta objetos: o atlas de sombra e o custo aqui.
Scene3D cenaLuzesESombras() {
  final nodes = <SceneNode>[];
  for (var i = 0; i < 40; i++) {
    final col = i % 8, lin = i ~/ 8;
    nodes.add(
      SceneNode(
        name: 'Bloco ${i + 1}',
        kind: i % 3 == 0 ? Element3DKind.sphere : Element3DKind.cube,
        material: _materialVariado(i),
        size: 60,
        x: AnimatedDouble(-350.0 + col * 100),
        y: AnimatedDouble(-200.0 + lin * 100),
        z: AnimatedDouble(((i * 53) % 300) - 150.0),
      ),
    );
  }
  final luzes = <Light3D>[luzPrincipal()];
  for (var i = 0; i < 6; i++) {
    final ang = 2 * math.pi * i / 6;
    luzes.add(
      Light3D(
        kind: Light3DKind.spot,
        color: _paleta[i % _paleta.length],
        intensity: AnimatedDouble(1.6),
        position: Vec3(math.cos(ang) * 420, -300, math.sin(ang) * 420),
        direction: Vec3(-math.cos(ang) * .6, 0.7, -math.sin(ang) * .6),
        coneDegrees: 42,
        castsShadow: true,
        range: 1100,
        softness: 0.3,
      ),
    );
  }
  for (var i = 0; i < 8; i++) {
    final ang = 2 * math.pi * i / 8 + .3;
    luzes.add(
      Light3D(
        kind: Light3DKind.point,
        color: _paleta[(i + 3) % _paleta.length],
        intensity: AnimatedDouble(1.0),
        position: Vec3(math.cos(ang) * 260, -80, math.sin(ang) * 260),
        range: 520,
      ),
    );
  }
  return Scene3D(
    nodes: nodes,
    lights: luzes,
    background: const Color(0xFF07090E),
  );
}

/// TESTE 5 — sessenta objetos PBR variados, com emissivo (bloom) e
/// animacao por keyframe em cada um.
Scene3D cenaPbrAnimada() {
  final nodes = <SceneNode>[];
  for (var i = 0; i < 60; i++) {
    final ang = 2 * math.pi * i / 60;
    final raio = 180.0 + (i % 3) * 110;
    nodes.add(
      SceneNode(
        name: 'PBR ${i + 1}',
        kind: Element3DKind.values[i % 5],
        material: _materialVariado(i, emissive: i % 5 == 0 ? 0.8 : 0),
        size: 40,
        x: AnimatedDouble(math.cos(ang) * raio),
        z: AnimatedDouble(math.sin(ang) * raio),
        y: AnimatedDouble(0, [
          Keyframe(time: Duration.zero, value: -120.0 + (i % 7) * 40),
          Keyframe(
            time: const Duration(milliseconds: 2500),
            value: 120.0 - (i % 7) * 40,
          ),
          Keyframe(
            time: const Duration(seconds: 5),
            value: -120.0 + (i % 7) * 40,
          ),
        ]),
        rotX: AnimatedDouble(0, [
          Keyframe(time: Duration.zero, value: 0),
          Keyframe(time: const Duration(seconds: 5), value: 720),
        ]),
        rotZ: AnimatedDouble(0, [
          Keyframe(time: Duration.zero, value: 0),
          Keyframe(time: const Duration(seconds: 5), value: -360),
        ]),
      ),
    );
  }
  return Scene3D(
    nodes: nodes,
    lights: luzesBasicas(),
    background: const Color(0xFF0B0E14),
  );
}

/// TESTE 9 — tudo de uma vez: a malha de um milhao, as luzes com sombra,
/// as texturas, emissivo e neblina. E a cena que NAO cabe no ideal de
/// aparelho nenhum: o que se mede e se o app continua vivo.
Scene3D cenaExtrema(List<String> texturas) {
  final base = cenaLuzesESombras();
  final pesada = cenaPoligonos1M();
  final tex = cenaTexturasGrandes(texturas);
  final nodes = <SceneNode>[
    ...pesada.nodes,
    for (final n in base.nodes.take(24)) n,
    for (var i = 0; i < tex.nodes.length; i++)
      SceneNode(
        name: 'Extrema tex ${i + 1}',
        kind: Element3DKind.sphere,
        material: Material3D(
          baseColor: const Color(0xFFFFFFFF),
          imagePath: i < texturas.length ? texturas[i] : null,
          emissive: i.isEven ? 0.6 : 0,
        ),
        size: 90,
        x: AnimatedDouble(-380.0 + i * 150),
        y: AnimatedDouble(-320),
      ),
  ];
  return Scene3D(
    nodes: nodes,
    lights: base.lights,
    fogDensity: 0.0009,
    fogStart: 300,
    fogColor: const Color(0xFF0F1A24),
  );
}

/// As nove receitas, na ordem do pedido.
List<ReceitaDeEstresse> receitasDeEstresse({
  List<String> texturas = const [],
}) => [
  ReceitaDeEstresse(
    id: TesteDeEstresse.objetos100,
    titulo: '100 objetos',
    descricao: 'Cem chamadas de desenho, cem materiais, sombra.',
    cena: cenaObjetos100(),
  ),
  ReceitaDeEstresse(
    id: TesteDeEstresse.poligonos1M,
    titulo: '1 milhao de poligonos',
    descricao: 'Uma esfera de 720 x 700 numa malha so.',
    cena: cenaPoligonos1M(),
  ),
  ReceitaDeEstresse(
    id: TesteDeEstresse.texturasGrandes,
    titulo: 'Texturas grandes',
    descricao: 'Seis texturas de 2048 x 2048, uma por objeto.',
    cena: cenaTexturasGrandes(texturas),
  ),
  ReceitaDeEstresse(
    id: TesteDeEstresse.luzesESombras,
    titulo: 'Multiplas luzes + sombras',
    descricao: 'Uma direcional, seis spots e oito pontuais, sombra em sete.',
    cena: cenaLuzesESombras(),
  ),
  ReceitaDeEstresse(
    id: TesteDeEstresse.pbrAnimacao,
    titulo: 'PBR + animacao',
    descricao: 'Sessenta objetos animados por keyframe, com emissivo.',
    cena: cenaPbrAnimada(),
  ),
  ReceitaDeEstresse(
    id: TesteDeEstresse.tresDMaisVideo,
    titulo: '3D + video',
    descricao: 'Os cem objetos sobre um video de 720p.',
    cena: cenaObjetos100(fundo: null),
    video: true,
  ),
  ReceitaDeEstresse(
    id: TesteDeEstresse.tresDMaisMotionGraph,
    titulo: '3D + Motion Graph',
    descricao: 'PBR animado e uma forma com cinquenta keyframes e expressao.',
    cena: cenaPbrAnimada(),
    motionGraph: true,
  ),
  ReceitaDeEstresse(
    id: TesteDeEstresse.tudoJunto,
    titulo: '3D + video + texto + efeitos',
    descricao: 'Cem objetos, video, texto e glow, glow volumetrico e grao.',
    cena: cenaObjetos100(fundo: null),
    video: true,
    texto: true,
    efeitos: true,
  ),
  ReceitaDeEstresse(
    id: TesteDeEstresse.extrema,
    titulo: 'Cena extremamente pesada',
    descricao:
        'Um milhao de poligonos, sete sombras, texturas, neblina, video, '
        'texto, efeitos e motion graph — de uma vez.',
    cena: cenaExtrema(texturas),
    video: true,
    texto: true,
    efeitos: true,
    motionGraph: true,
  ),
];
