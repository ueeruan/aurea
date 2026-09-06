import 'dart:math' as math;
import 'dart:ui';

import '../../editor/domain/camera3d.dart';
import '../../editor/domain/camera_cuts.dart';
import '../../editor/domain/effect.dart';
import '../../editor/domain/element3d.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/model_asset3d.dart';
import '../../editor/domain/panorama3d.dart';
import '../../editor/domain/scene3d.dart';
import '../../editor/domain/video_project.dart';
import 'malha_codigo.dart';
import 'textura_procedural.dart';

/// DERIVA · O ASTRONAUTA PERDIDO.
///
/// Dezoito segundos em tres tomadas, uma cena 3D so — o corte e de
/// CAMERA, nao de projeto, e por isso o astronauta continua girando o
/// mesmo giro quando a tomada troca. E o que separa um filme de tres
/// clipes colados.
///
///   01 · A DERIVA        (0–6 s, 35 mm)  ele e pequeno, o mundo e grande
///   02 · O ULTIMO OLHAR  (6–11 s, 85 mm) o rosto, e o planeta no visor
///   03 · O SILENCIO      (11–18 s, 24 mm) ele vira um ponto e some
///
/// A LUZ E A HISTORIA. No vacuo nao ha ar para espalhar luz: existe o
/// sol, duro e branco, e existe o preto. A sombra nao e cinza, e nada —
/// por isso o ambiente fica em 0,04 e metade do corpo desaparece. O
/// unico consolo e um azul fraquissimo vindo do planeta, longe demais
/// para salvar alguem. E disso que a cena e triste: nao ha o que
/// preencher a sombra.
///
/// Tudo aqui e a mesma camada 3D editavel do app: abra o Estudio, mude a
/// luz, mova a camera. O astronauta e o modelo importado (o mesmo do
/// Monolito), o resto e geometria montada em codigo — determinista, sem
/// relogio nem sorteio, entao abrir duas vezes da o mesmo filme.
const derivaDuration = Duration(seconds: 18);
const derivaFps = 30;
const derivaWidth = 1280.0;
const derivaHeight = 720.0;

/// O teto de triangulos por quadro: sem o astronauta importado a cena e
/// leve; com ele, cabe folgado no que um iPhone 13 renderiza.
const derivaTriangleBudget = 40000;

/// Os tempos das tres tomadas, em segundos.
const derivaTomadas = [0.0, 6.0, 11.0];

Duration _t(num seconds) =>
    Duration(microseconds: (seconds * 1000000).round());
AnimatedDouble _ad(double v) => AnimatedDouble(v);
AnimatedDouble _keys(List<(num, num)> values, {Easing ease = Easing.linear}) =>
    AnimatedDouble(values.first.$2.toDouble(), [
      for (final v in values)
        Keyframe(time: _t(v.$1), value: v.$2.toDouble(), ease: ease),
    ]);

/// Amostra uma curva do tempo a cada 1/4 s: a camera e o giro do corpo
/// viram keyframes de verdade, que se pode pegar e mexer no editor.
AnimatedDouble _sample(double Function(double) f) => AnimatedDouble(f(0), [
  for (var i = 0; i <= 72; i++) Keyframe(time: _t(i / 4), value: f(i / 4)),
]);

// ============================================================ O CORPO

/// ONDE ELE ESTA, a cada instante. Uma reta: no vacuo nada freia, e essa
/// e a crueldade do plano — a trajetoria nunca muda de ideia.
Vec3 astronautaEm(double t) => Vec3(-150 + 27 * t, 46 - 3.4 * t, 40 - 6 * t);

/// O GIRO SEM CONTROLE. Tres eixos em velocidades que nao fecham entre
/// si: nunca se repete, e e isso que o olho le como "ele nao consegue
/// mais se endireitar".
double _giroX(double t) => 12 + 7.4 * t;
double _giroY(double t) => -140 - 11.7 * t;
double _giroZ(double t) => 5 + 4.3 * t;

SceneNode _astronauta(ModelAsset3D? modelo) => SceneNode(
      id: 'deriva_astronauta',
      name: 'Astronauta',
      size: 58,
      // Sem o modelo importado, uma capsula: a cena continua legivel.
      kind: Element3DKind.capsule,
      modelAsset: modelo,
      useModelMaterials: modelo != null,
      material: const Material3D(baseColor: Color(0xffe6e3dc), roughness: .55),
      x: _sample((t) => astronautaEm(t).x),
      y: _sample((t) => astronautaEm(t).y),
      z: _sample((t) => astronautaEm(t).z),
      rotX: _sample(_giroX),
      rotY: _sample(_giroY),
      rotZ: _sample(_giroZ),
    );

// ========================================================== O UNIVERSO

/// O CAMPO DE ESTRELAS: quadradinhos sem luz numa casca enorme em volta.
///
/// Sem luz porque estrela nao recebe luz — ela e a luz; e o mesmo motivo
/// pelo qual elas viram bolas de bokeh na tomada fechada, onde o foco
/// esta a duzentas unidades e elas a quinze mil.
List<SceneNode> _estrelas() {
  final out = <SceneNode>[];
  // O lado do quadradinho e o que decide o tamanho na tela: a 15 mil
  // unidades e 35 mm, 100 unidades dao uns 4 px.
  const brilhos = [
    ('brilhantes', 0xfff8faff, 130.0, 70),
    ('medias', 0xffd2dcf4, 85.0, 130),
    ('fracas', 0xff97a4c4, 55.0, 200),
  ];
  for (var k = 0; k < brilhos.length; k++) {
    final (nome, cor, lado, quantas) = brilhos[k];
    final m = MalhaCodigo([
      materialCodigo('Estrela · $nome', cor, semLuz: true, doisLados: true),
    ]);
    final h = lado / 2;
    m.quadPlano(
      0,
      Vec3(-h, -h, 0),
      Vec3(h, -h, 0),
      Vec3(h, h, 0),
      Vec3(-h, h, 0),
      virado: const Vec3(0, 0, 1),
    );
    out.add(m.no(
      'deriva_estrelas_$k',
      'Estrelas · $nome',
      posicao: Vec3.zero,
      instancias: [
        for (var i = 0; i < quantas; i++)
          () {
            // Distribuicao uniforme na esfera (o cosseno do angulo polar
            // e que precisa ser uniforme, nao o angulo).
            final u = ruido(i * 3 + k * 7919 + 11) * 2 - 1;
            final a = 2 * math.pi * ruido(i * 3 + k * 7919 + 12);
            final r = 15000 * (.8 + .35 * ruido(i * 3 + k * 7919 + 13));
            final s = math.sqrt(math.max(0.0, 1 - u * u));
            return Vec3(r * s * math.cos(a), r * u, r * s * math.sin(a));
          }(),
      ],
    ));
  }
  return out;
}

/// O PLANETA: a casa, longe demais. Textura procedural de um mundo frio
/// — mares escuros, terra palida, gelo nos polos, nuvens em faixas.
String _texturaDoPlaneta() => pngDataUri(1024, 512, (px, py, rgb) {
      final lon = px / 1024, lat = py / 512;
      // A latitude comprime perto dos polos: amostrar em coordenada
      // esferica evita as manchas esticadas la em cima.
      final phi = (lat - .5) * math.pi;
      final cx = math.cos(phi) * math.cos(lon * 2 * math.pi);
      final cz = math.cos(phi) * math.sin(lon * 2 * math.pi);
      final cy = math.sin(phi);
      final continente = fbm(cx * 2.6 + 4, cz * 2.6 + cy * 1.9 + 7, semente: 301);
      final detalhe = fbm(cx * 9 + 1, cz * 9 + cy * 6 + 3, oitavas: 2, semente: 302);
      final terra = suave(((continente - .52) / .10).clamp(0.0, 1.0));
      // Mar: azul-esverdeado escuro, quase sem saturacao.
      var r = .035 + .03 * (detalhe - .5);
      var g = .075 + .04 * (detalhe - .5);
      var b = .135 + .05 * (detalhe - .5);
      // Terra: ocre palido, sem viço.
      r += (.30 + .10 * (detalhe - .5) - r) * terra;
      g += (.27 + .09 * (detalhe - .5) - g) * terra;
      b += (.21 + .07 * (detalhe - .5) - b) * terra;
      // Gelo nos polos, entrando devagar.
      final gelo = suave(((cy.abs() - .62) / .22).clamp(0.0, 1.0));
      r += (.80 - r) * gelo;
      g += (.85 - g) * gelo;
      b += (.92 - b) * gelo;
      // Nuvens: faixas alongadas em longitude, como as de verdade.
      final nuvem = suave(
        ((fbm(cx * 3.4 + 9, cz * 3.4 + cy * 7.5 + 2, semente: 303) - .55) / .16)
            .clamp(0.0, 1.0),
      );
      r += (.88 - r) * nuvem * .8;
      g += (.90 - g) * nuvem * .8;
      b += (.94 - b) * nuvem * .8;
      rgb[0] = canal8(r);
      rgb[1] = canal8(g);
      rgb[2] = canal8(b);
    });

/// Esfera de latitude e longitude, com normal por vertice (lisa) e UV
/// equiretangular — a mesma projecao da textura.
MalhaCodigo _esfera(
  double raio,
  Map<String, dynamic> material, {
  int meridianos = 48,
  int paralelos = 24,
}) {
  final m = MalhaCodigo([material]);
  final grade = <List<int>>[];
  for (var j = 0; j <= paralelos; j++) {
    final v = j / paralelos;
    final phi = (v - .5) * math.pi;
    final linha = <int>[];
    for (var i = 0; i <= meridianos; i++) {
      final u = i / meridianos;
      final theta = u * 2 * math.pi;
      final n = Vec3(
        math.cos(phi) * math.cos(theta),
        math.sin(phi),
        math.cos(phi) * math.sin(theta),
      );
      linha.add(m.vertice(0, n * raio, n, uv: Offset(u, 1 - v)));
    }
    grade.add(linha);
  }
  for (var j = 0; j < paralelos; j++) {
    for (var i = 0; i < meridianos; i++) {
      final a = grade[j][i], b = grade[j][i + 1];
      final c = grade[j + 1][i + 1], d = grade[j + 1][i];
      if (j > 0) m.tri(0, a, c, b);
      if (j < paralelos - 1) m.tri(0, a, d, c);
    }
  }
  return m;
}

const _centroDoPlaneta = Vec3(1100, -3300, -5600);
const _raioDoPlaneta = 2750.0;

SceneNode _planeta() {
  final m = _esfera(
    _raioDoPlaneta,
    materialCodigo(
      'Superficie',
      0xffffffff,
      rugosidade: .92,
      imagem: _texturaDoPlaneta(),
    ),
  );
  final c = m.caixa();
  return SceneNode(
    id: 'deriva_planeta',
    name: 'O planeta',
    size: c.meio,
    modelAsset: m.asset('O planeta'),
    x: _ad(_centroDoPlaneta.x),
    y: _ad(_centroDoPlaneta.y),
    z: _ad(_centroDoPlaneta.z),
    // Gira devagar: em dezoito segundos anda meio grau. Nao se ve —
    // se sente.
    rotY: _keys([(0, 0), (18, .5)]),
    rotZ: _ad(-14),
  );
}

/// O SOL: um disco branco, sem luz propria no calculo (a luz e a
/// direcional) mas emissivo, para o brilho pegar nele.
SceneNode _sol() {
  final m = _esfera(
    260,
    {
      'name': 'Sol',
      'color': [1.0, 1.0, 1.0, 1.0],
      'emissive': 1.0,
      'unlit': true,
      'metallic': 0.0,
      'roughness': 1.0,
    },
    meridianos: 24,
    paralelos: 12,
  );
  final c = m.caixa();
  final p = _luzDoSol * -16000;
  return SceneNode(
    id: 'deriva_sol',
    name: 'Sol',
    size: c.meio,
    modelAsset: m.asset('Sol'),
    x: _ad(p.x),
    y: _ad(p.y),
    z: _ad(p.z),
  );
}

/// A direcao em que a luz do sol VIAJA. De cima, da direita e de tras:
/// contraluz — o corpo vira silhueta com um fio de luz na borda, e a
/// frente, que e onde estaria o rosto, fica no escuro.
const _luzDoSol = Vec3(-.55, -.26, .79);

// ======================================================== OS DESTROCOS

/// UM PEDACO retorcido de casco: cubo deformado por ruido, faces planas.
MalhaCodigo _fragmento(int semente, double tamanho) {
  final m = MalhaCodigo([
    materialCodigo('Casco', 0xff9aa2ab, rugosidade: .42, metal: .75),
    materialCodigo('Isolamento', 0xffd8c9a8, rugosidade: .9),
  ]);
  const lados = 6, aneis = 5;
  final pontos = <List<Vec3>>[];
  for (var a = 0; a <= aneis; a++) {
    final lat = math.pi * a / aneis;
    final anel = <Vec3>[];
    for (var l = 0; l < lados; l++) {
      final lon = 2 * math.pi * l / lados;
      final d = Vec3(
        math.sin(lat) * math.cos(lon),
        math.cos(lat),
        math.sin(lat) * math.sin(lon),
      );
      // Deformacao forte: destroço nao e pedra, e chapa rasgada.
      final k = .35 +
          1.15 * fbm(d.x * 3 + semente * 5, d.z * 3 + d.y * 2.2, semente: 70 + semente);
      anel.add(Vec3(d.x * tamanho * k, d.y * tamanho * k * .7, d.z * tamanho * k));
    }
    pontos.add(anel);
  }
  for (var a = 0; a < aneis; a++) {
    for (var l = 0; l < lados; l++) {
      final l2 = (l + 1) % lados;
      final p00 = pontos[a][l], p01 = pontos[a][l2];
      final p10 = pontos[a + 1][l], p11 = pontos[a + 1][l2];
      final fora = (p00 + p10 + p01) * (1 / 3);
      final mat = ruido(semente * 31 + a * 7 + l) > .78 ? 1 : 0;
      if (a > 0) m.triPlano(mat, p00, p01, p10, virado: fora);
      if (a < aneis - 1) m.triPlano(mat, p01, p11, p10, virado: fora);
    }
  }
  return m;
}

/// A NUVEM DE DESTROCOS. Tres grupos, cada um girando no proprio ritmo:
/// as instancias giram junto com o no, entao o conjunto roda como um
/// enxame — que e como destroço de verdade se comporta.
List<SceneNode> _destrocos() => [
      for (var k = 0; k < 3; k++)
        _fragmento(k + 1, 9.0 + k * 7).no(
          'deriva_destrocos_$k',
          'Destrocos · grupo ${k + 1}',
          posicao: Vec3.zero,
          rotX: _sample((t) => t * (5 + k * 3.5)),
          rotY: _sample((t) => -t * (4 + k * 2.5)),
          instancias: [
            for (var i = 0; i < 9; i++)
              () {
                final base = astronautaEm(0);
                return Vec3(
                  base.x + (ruido(i * 3 + k * 991 + 1) - .5) * 1500,
                  base.y + (ruido(i * 3 + k * 991 + 2) - .5) * 700,
                  base.z + (ruido(i * 3 + k * 991 + 3) - .5) * 1400,
                );
              }(),
          ],
        ),
    ];

/// O CABO CORTADO: a linha de vida que arrebentou, boiando sozinha.
/// Nao esta preso a ele — e esse o ponto.
SceneNode _cabo() {
  final m = MalhaCodigo([
    materialCodigo('Cabo', 0xffb9b09a, rugosidade: .8),
  ]);
  const segmentos = 26, raio = 1.7;
  Vec3 ponto(double s) => Vec3(
        -150 + 300 * s,
        26 * math.sin(s * 5.2) * (1 - s * .4),
        18 * math.sin(s * 8.1 + 1.2),
      );
  for (var i = 0; i < segmentos; i++) {
    final a = ponto(i / segmentos), b = ponto((i + 1) / segmentos);
    final eixo = (b - a);
    if (eixo.length < 1e-6) continue;
    final e = eixo.normalized;
    final ref = e.y.abs() < .9 ? const Vec3(0, 1, 0) : const Vec3(1, 0, 0);
    final u = ref.cross(e).normalized, v = e.cross(u);
    for (var k = 0; k < 4; k++) {
      final a0 = 2 * math.pi * k / 4, a1 = 2 * math.pi * (k + 1) / 4;
      final r0 = (u * math.cos(a0) + v * math.sin(a0)) * raio;
      final r1 = (u * math.cos(a1) + v * math.sin(a1)) * raio;
      m.quadPlano(0, a + r0, a + r1, b + r1, b + r0, virado: r0 + r1);
    }
  }
  final p = astronautaEm(0) + const Vec3(210, -60, 150);
  final c = m.caixa();
  return SceneNode(
    id: 'deriva_cabo',
    name: 'Cabo de seguranca · rompido',
    size: c.meio,
    modelAsset: m.asset('Cabo de seguranca'),
    x: _ad(p.x),
    y: _ad(p.y),
    z: _ad(p.z),
    rotX: _sample((t) => 14 + t * 2.2),
    rotY: _sample((t) => -40 - t * 3.1),
    rotZ: _sample((t) => t * 1.4),
  );
}

// ============================================================ CAMERAS

Camera3D _tomada(
  String id,
  String nome,
  Vec3 Function(double) posicao,
  Vec3 Function(double) alvo,
  double lente, {
  double Function(double)? roll,
  DepthOfField? dof,
}) =>
    Camera3D(
      id: id,
      name: nome,
      posX: _sample((t) => posicao(t).x),
      posY: _sample((t) => posicao(t).y),
      posZ: _sample((t) => posicao(t).z),
      poiX: _sample((t) => alvo(t).x),
      poiY: _sample((t) => alvo(t).y),
      poiZ: _sample((t) => alvo(t).z),
      focalLength: _ad(lente),
      rotZ: _sample(roll ?? (t) => 0),
      dof: dof,
    );

List<Camera3D> _cameras() => [
      // 01 · A DERIVA. Grande angular, de longe: ele ocupa pouco quadro,
      // e o vazio ocupa o resto. A camera se aproxima devagar, como quem
      // ainda tem esperanca.
      _tomada(
        'deriva_cam_1',
        '01 · A deriva / 35 mm',
        (t) {
          final k = 1 - .09 * t;
          return astronautaEm(t) + Vec3(-330 * k, 118 * k, 470 * k);
        },
        astronautaEm,
        35,
        roll: (t) => -1.5 - t * .55,
      ),
      // 02 · O ULTIMO OLHAR. Lente longa e perto: o fundo comprime, as
      // estrelas viram bolas fora de foco, e so o capacete fica nitido.
      _tomada(
        'deriva_cam_2',
        '02 · O ultimo olhar / 85 mm',
        (t) {
          final a = .85 - (t - 6) * .14;
          return astronautaEm(t) +
              Vec3(700 * math.sin(a), 96 + (t - 6) * 4.5, 700 * math.cos(a));
        },
        // Mira no CORPO: ele esta girando sem controle, e um ponto fixo
        // acima do centro nao e a cabeca — e o vazio ao lado dela.
        astronautaEm,
        85,
        roll: (t) => 3 + (t - 6) * .7,
        dof: DepthOfField(
          enabled: true,
          focusDistance: _ad(706),
          aperture: _ad(24),
          blurLevel: _ad(100),
          irisShape: IrisShape.heptagon,
          irisRoundness: _ad(55),
          diffractionFringe: _ad(10),
          // O realce so pega o que e luz de verdade (estrela, sol): o
          // traje iluminado fica abaixo do limiar e nao vira bola.
          highlightGain: _ad(14),
          highlightThreshold: _ad(.9),
          highlightSaturation: _ad(1.1),
        ),
      ),
      // 03 · O SILENCIO. A camera PARA. Ela nao acompanha mais — ele que
      // se afasta, e some. Grande angular para o planeta caber e ele nao.
      _tomada(
        'deriva_cam_3',
        '03 · O silencio / 24 mm',
        (t) => astronautaEm(11) + const Vec3(-520, 150, 880),
        astronautaEm,
        24,
        roll: (t) => -(t - 11) * .45,
      ),
    ];

// ============================================================= PROJETO

VideoProject buildDerivaTemplate({ModelAsset3D? astronauta}) {
  final cameras = _cameras();
  final scene = Scene3D(
    showFloorGrid: false,
    // O PRETO DO ESPACO. Nao e cinza escuro: e o fundo de tudo.
    background: const Color(0xff03040a),
    // Ambiente quase zero: no vacuo a sombra nao tem quem a preencha.
    ambient: .055,
    skyColor: const Color(0xff1a2740),
    groundColor: const Color(0xff05070c),
    environment: EnvironmentKind.noite,
    envReflect: .55,
    panorama: const Panorama3D(preset: PanoramaPreset.noite),
    // Vacuo: nada de neblina.
    fogDensity: 0,
    lights: [
      // O SOL. Uma luz, dura, branca com um resto de amarelo. E ela que
      // faz a silhueta.
      Light3D(
        id: 'deriva_sol',
        color: const Color(0xfffff4e2),
        direction: _luzDoSol,
        castsShadow: true,
        softness: .05,
        intensity: _ad(3.1),
      ),
      // O PLANETA devolvendo um azul frio, de baixo. E o unico consolo —
      // fraco de proposito.
      Light3D(
        id: 'deriva_planeta_bounce',
        color: const Color(0xff5b82b6),
        direction: const Vec3(-.2, .92, .34),
        intensity: _ad(.46),
      ),
      // Um contorno atras, quase nada, para o corpo nao se colar no preto.
      Light3D(
        id: 'deriva_recorte',
        color: const Color(0xff8fa4c8),
        direction: const Vec3(.62, .1, -.78),
        intensity: _ad(.42),
      ),
    ],
    nodes: [
      ..._estrelas(),
      _planeta(),
      _sol(),
      ..._destrocos(),
      _cabo(),
      _astronauta(astronauta),
    ],
  );

  const centro = Offset(derivaWidth / 2, derivaHeight / 2);
  return VideoProject(
    id: 'deriva_template',
    name: 'DERIVA · O astronauta perdido',
    createdAt: DateTime(2026, 9, 6),
    aspectRatio: 16 / 9,
    resolutionHeight: 720,
    fps: derivaFps,
    markers: [
      for (var i = 0; i < 3; i++)
        Marker(time: _t(derivaTomadas[i]), label: cameras[i].name),
    ],
    layers: [
      // A GRADACAO: frio, contrastado, dessaturado. Tristeza em cor e
      // isto — tirar o viço sem apagar a imagem.
      AdjustmentLayer(
        id: 'deriva_grade',
        name: 'Gradacao · frio e vinheta',
        startTime: Duration.zero,
        duration: derivaDuration,
        position: AnimatedOffset(centro),
        effects: [
          EffectInstance(type: EffectType.corrections, params: {
            'contraste': _ad(.16),
            'sombras': _ad(-.10),
            'altas': _ad(-.05),
            'temperatura': _ad(-.16),
            'saturacao': _ad(-.14),
          }),
          EffectInstance(type: EffectType.vignette, params: {
            'quantidade': _ad(.52),
            'raio': _ad(.88),
            'suavidade': _ad(.8),
          }),
          EffectInstance(type: EffectType.filmGrain, params: {
            'intensidade': _ad(.07),
            'tamanho': _ad(1.4),
          }),
        ],
      ),
      Scene3DLayer(
        id: 'deriva_cena',
        name: 'DERIVA · cena 3D editavel',
        startTime: Duration.zero,
        duration: derivaDuration,
        position: AnimatedOffset(centro),
        showHelpers: false,
        camera: cameras.first,
        extraCameras: cameras.skip(1).toList(),
        shots: [
          for (var i = 0; i < 3; i++)
            CameraShot(
              time: _t(derivaTomadas[i]),
              cameraId: cameras[i].id,
            ),
        ],
        // Abre do preto e volta para o preto: o filme comeca depois do
        // acidente e termina antes do fim.
        opacity: _keys([(0, 0), (1.2, 1), (16.2, 1), (18, 0)]),
        scene: scene,
        effects: [
          EffectInstance(
            type: EffectType.lightGlow,
            color: const Color(0xffdfe8ff),
            params: {
              'threshold': _ad(82),
              'raio': _ad(22),
              'intensity': _ad(135),
              'piramide': _ad(4),
            },
          ),
        ],
      ),
    ],
  );
}

/// Triangulos por quadro, contando instancias.
int derivaTriangles(VideoProject p) {
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
