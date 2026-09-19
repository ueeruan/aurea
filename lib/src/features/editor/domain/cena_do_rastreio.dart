import 'dart:math' as math;
import 'dart:ui';
import 'package:aurea/src/core/theme/aurea_colors.dart';

import 'algebra_numerica.dart';
import 'camera3d.dart';
import 'camera_solver3d.dart';
import 'element3d.dart';
import 'keyframe.dart';
import 'layer.dart';
import 'scene3d.dart';

/// DO RASTREIO PARA A CENA — o passo que transforma numeros em algo que
/// se edita.
///
/// O solver devolve poses e uma nuvem de pontos. Isso ainda nao serve
/// para ninguem: o que a pessoa quer e pousar um texto no chao do video
/// e ver ele ficar la. A ponte e uma CAMADA DE CENA 3D por cima do
/// clipe, com fundo transparente e uma camera que anda exatamente como a
/// camera de verdade andou. Dai em diante, tudo que entrar nessa cena
/// gruda no plano sem mais nenhum ajuste — que e o ponto do rastreio.
///
/// A parte delicada e a ORIENTACAO. O motor monta a base da camera a
/// partir do alvo e de um "cima" fixo, e deixa a inclinacao para o
/// parametro de giro. O rastreio, ao contrario, sabe exatamente para
/// onde o topo da imagem apontava. Traduzir um no outro e o que evita o
/// erro classico de camera rastreada: o enquadramento certo, a cena
/// inteira tombada.

/// A distancia focal em milimetros equivalente ao que o solver achou em
/// pixels. A largura do filme e a mesma que o resto do app usa (36 mm),
/// entao o numero sai comparavel com uma lente de verdade.
double focalEmMilimetros(SolucaoCamera3D s, {double larguraDoFilme = 36}) =>
    larguraDoFilme * s.focalPx / math.max(1, s.largura);

/// O GIRO DA CAMERA, em graus.
///
/// O motor calcula o "cima" da camera assim: pega a frente, cruza com o
/// eixo Y do mundo e volta. Isso da uma camera sempre nivelada. O giro e
/// o quanto o topo REAL da imagem se afasta desse nivelado — e sem ele
/// uma camera inclinada devolve a cena torta na direcao contraria.
double giroDaPose(PoseCamera pose, Vec3 posicao, Vec3 alvo) {
  final base = cameraBasis(RenderCamera(position: posicao, target: alvo));
  final cima = pose.cima;
  final c = Vec3(cima[0], cima[1], cima[2]);
  return math.atan2(c.dot(base.right), c.dot(base.up)) * 180 / math.pi;
}

Duration _tempoDoQuadro(int quadro, int fps) =>
    Duration(microseconds: (quadro * 1000000 / math.max(1, fps)).round());

/// A POSE NUM QUADRO FRACIONARIO da analise: posicao linear e rotacao por
/// slerp entre os dois quadros vizinhos. Camera lenta mostra o mesmo quadro
/// da fonte por varios quadros da composicao; sem interpolar, a camera
/// andaria em degraus.
PoseCamera poseNoQuadro(SolucaoCamera3D s, double quadro) {
  final ps = s.poses;
  if (ps.length == 1 || quadro <= ps.first.quadro) return ps.first;
  if (quadro >= ps.last.quadro) return ps.last;
  var lo = 0, hi = ps.length - 1;
  while (hi - lo > 1) {
    final m = (lo + hi) >> 1;
    if (ps[m].quadro <= quadro) {
      lo = m;
    } else {
      hi = m;
    }
  }
  final a = ps[lo], b = ps[hi];
  final u = ((quadro - a.quadro) / math.max(1, b.quadro - a.quadro))
      .clamp(0.0, 1.0);
  if (u == 0) return a;
  final ca = a.posicao, cb = b.posicao;
  final c = [
    ca[0] + (cb[0] - ca[0]) * u,
    ca[1] + (cb[1] - ca[1]) * u,
    ca[2] + (cb[2] - ca[2]) * u,
  ];
  final r = _slerp(a.rotacao, b.rotacao, u);
  final rc = r.aplicar(c);
  return PoseCamera(a.quadro, r, [-rc[0], -rc[1], -rc[2]]);
}

List<double> _quaternio(Mat3 r) {
  final m = r.m;
  final tr = m[0] + m[4] + m[8];
  double w, x, y, z;
  if (tr > 0) {
    final s = math.sqrt(tr + 1) * 2;
    w = s / 4;
    x = (m[7] - m[5]) / s;
    y = (m[2] - m[6]) / s;
    z = (m[3] - m[1]) / s;
  } else if (m[0] > m[4] && m[0] > m[8]) {
    final s = math.sqrt(1 + m[0] - m[4] - m[8]) * 2;
    w = (m[7] - m[5]) / s;
    x = s / 4;
    y = (m[3] + m[1]) / s;
    z = (m[2] + m[6]) / s;
  } else if (m[4] > m[8]) {
    final s = math.sqrt(1 + m[4] - m[0] - m[8]) * 2;
    w = (m[2] - m[6]) / s;
    x = (m[3] + m[1]) / s;
    y = s / 4;
    z = (m[7] + m[5]) / s;
  } else {
    final s = math.sqrt(1 + m[8] - m[0] - m[4]) * 2;
    w = (m[3] - m[1]) / s;
    x = (m[2] + m[6]) / s;
    y = (m[7] + m[5]) / s;
    z = s / 4;
  }
  return [w, x, y, z];
}

Mat3 _slerp(Mat3 ra, Mat3 rb, double u) {
  final a = _quaternio(ra);
  var b = _quaternio(rb);
  var d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  if (d < 0) {
    b = [for (final v in b) -v];
    d = -d;
  }
  List<double> q;
  if (d > .9995) {
    q = [for (var i = 0; i < 4; i++) a[i] + (b[i] - a[i]) * u];
  } else {
    final th = math.acos(d.clamp(-1.0, 1.0));
    final sa = math.sin((1 - u) * th) / math.sin(th);
    final sb = math.sin(u * th) / math.sin(th);
    q = [for (var i = 0; i < 4; i++) a[i] * sa + b[i] * sb];
  }
  final n = math.sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  final w = q[0] / n, x = q[1] / n, y = q[2] / n, z = q[3] / n;
  return Mat3([
    1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
    2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
    2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y),
  ]);
}

/// A CAMERA DO RASTREIO.
///
/// Sem [fonteNoTempo] (solucao antiga), um keyframe por quadro analisado no
/// tempo q/fps da camada — que so bate com clipe a 1x, sem reverso e sem
/// Time Remap. Com ele, a camera e assada no tempo da CAMADA: para cada
/// quadro do clipe, o instante da fonte que ele mostra vira o quadro da
/// analise, e a pose vem de la. Camera lenta, aceleracao, reverso e curva
/// de tempo passam a mover a camera junto com a imagem.
Camera3D cameraDoRastreio(
  SolucaoCamera3D s, {
  String id = 'rastreio_camera',
  String nome = 'Câmera rastreada',
  Duration Function(Duration local)? fonteNoTempo,
  Duration? duracaoDaCamada,
}) {
  // A distancia do alvo e so uma convencao (o alvo define direcao, nao
  // profundidade). Usar a profundidade tipica da cena mantem os numeros
  // do painel na mesma ordem de grandeza do resto.
  final profundidades = <double>[];
  for (final p in s.poses) {
    final pos = p.posicao;
    for (final x in s.nuvem.values) {
      profundidades.add(norma([x[0] - pos[0], x[1] - pos[1], x[2] - pos[2]]));
    }
    if (profundidades.length > 400) break;
  }
  final distancia = profundidades.isEmpty ? 800.0 : mediana(profundidades);

  final px = <Keyframe<double>>[];
  final py = <Keyframe<double>>[];
  final pz = <Keyframe<double>>[];
  final ax = <Keyframe<double>>[];
  final ay = <Keyframe<double>>[];
  final az = <Keyframe<double>>[];
  final giro = <Keyframe<double>>[];

  final inicio = s.inicioDaFonteUs;
  final assadas = <(Duration, PoseCamera)>[
    if (fonteNoTempo != null && inicio != null && duracaoDaCamada != null)
      for (
        var i = 0;
        i * 1000000 / math.max(1, s.fps) <= duracaoDaCamada.inMicroseconds;
        i++
      )
        () {
          final t = _tempoDoQuadro(i, s.fps);
          final fonte = fonteNoTempo(t).inMicroseconds;
          final quadro = (fonte - inicio) * s.fps / 1000000;
          return (t, poseNoQuadro(s, quadro));
        }()
    else
      for (final pose in s.poses) (_tempoDoQuadro(pose.quadro, s.fps), pose),
  ];

  for (final (t, pose) in assadas) {
    final pos = pose.posicao;
    final frente = pose.frente;
    final alvo = [
      pos[0] + frente[0] * distancia,
      pos[1] + frente[1] * distancia,
      pos[2] + frente[2] * distancia,
    ];
    px.add(Keyframe(time: t, value: pos[0]));
    py.add(Keyframe(time: t, value: pos[1]));
    pz.add(Keyframe(time: t, value: pos[2]));
    ax.add(Keyframe(time: t, value: alvo[0]));
    ay.add(Keyframe(time: t, value: alvo[1]));
    az.add(Keyframe(time: t, value: alvo[2]));
    giro.add(
      Keyframe(
        time: t,
        value: giroDaPose(
          pose,
          Vec3(pos[0], pos[1], pos[2]),
          Vec3(alvo[0], alvo[1], alvo[2]),
        ),
      ),
    );
  }

  return Camera3D(
    id: id,
    name: nome,
    posX: AnimatedDouble(px.first.value, px),
    posY: AnimatedDouble(py.first.value, py),
    posZ: AnimatedDouble(pz.first.value, pz),
    poiX: AnimatedDouble(ax.first.value, ax),
    poiY: AnimatedDouble(ay.first.value, ay),
    poiZ: AnimatedDouble(az.first.value, az),
    rotZ: AnimatedDouble(giro.first.value, giro),
    focalLength: AnimatedDouble(focalEmMilimetros(s)),
  );
}

/// A NUVEM DE PONTOS como um objeto so.
///
/// Um no por ponto seriam cem chamadas de desenho por quadro para
/// mostrar confetes. Como instancias de um unico no, e uma chamada — e a
/// nuvem serve para o que precisa servir: ver se o rastreio pegou o que
/// interessa, e escolher onde pousar as coisas.
SceneNode nuvemDoRastreio(
  SolucaoCamera3D s, {
  String id = 'rastreio_nuvem',
  String nome = 'Pontos do rastreio',
}) => SceneNode(
  id: id,
  name: nome,
  kind: Element3DKind.octahedron,
  size: 4,
  material: const Material3D(
    baseColor: Color(0xFF6FE3B0),
    kind: MaterialKind.unlit,
  ),
  instances: [for (final v in s.nuvem.values) Vec3(v[0], v[1], v[2])],
);

/// UM NULO NO PONTO ESCOLHIDO — o "criar nulo e camera" do After
/// Effects, na versao que faz sentido aqui.
///
/// Nulo e nao cubo porque o que se quer nao e um objeto: e um lugar. A
/// pessoa põe o texto como filho dele e o texto passa a viver naquele
/// canto do mundo real.
SceneNode? noNoPonto(
  SolucaoCamera3D s,
  int idDoPonto, {
  String? nome,
  bool comoNulo = true,
}) {
  final v = s.nuvem[idDoPonto];
  if (v == null) return null;
  return SceneNode(
    name: nome ?? 'Ponto $idDoPonto',
    kind: Element3DKind.cube,
    isNull: comoNulo,
    size: 30,
    x: AnimatedDouble(v[0]),
    y: AnimatedDouble(v[1]),
    z: AnimatedDouble(v[2]),
  );
}

/// A CAMADA PRONTA: cena 3D com fundo transparente, camera rastreada e a
/// nuvem de pontos, para entrar em cima do clipe.
Scene3DLayer camadaDoRastreio(
  SolucaoCamera3D s, {
  required Duration startTime,
  required Duration duration,
  required Offset position,
  String nome = 'Rastreio 3D',
  bool comNuvem = true,
  Duration Function(Duration local)? fonteNoTempo,
}) => Scene3DLayer(
  name: nome,
  startTime: startTime,
  duration: duration,
  position: AnimatedOffset(position),
  camera: cameraDoRastreio(
    s,
    fonteNoTempo: fonteNoTempo,
    duracaoDaCamada: duration,
  ),
  // Os ajudantes ficam LIGADOS: sem ver a nuvem e o horizonte, nao ha
  // como saber se o rastreio pegou o chao ou a parede — e descobrir isso
  // depois de montar a cena inteira e caro.
  showHelpers: comNuvem,
  scene: Scene3D(
    // Fundo transparente: o video e que aparece atras.
    showFloorGrid: false,
    lights: Scene3D.tresPontos,
    nodes: [if (comNuvem) nuvemDoRastreio(s)],
  ),
);

/// O QUE SE PODE POUSAR NUMA SUPERFICIE RASTREADA.
///
/// A lista e curta de proposito. Ela cobre os quatro usos que aparecem
/// de verdade: marcar um lugar (nulo), pintar uma area (solido), pôr um
/// objeto (forma) e escrever no chao (texto). Modelo importado nao entra
/// aqui porque escolher um modelo e outra tela inteira — o caminho e
/// criar o nulo e pendurar o modelo nele no estudio 3D.
enum ObjetoNoPlano {
  nulo,
  solido,
  forma,
  texto;

  String get emPalavras => switch (this) {
    ObjetoNoPlano.nulo => 'Nulo 3D',
    ObjetoNoPlano.solido => 'Sólido',
    ObjetoNoPlano.forma => 'Forma 3D',
    ObjetoNoPlano.texto => 'Texto',
  };

  String get explicacao => switch (this) {
    ObjetoNoPlano.nulo =>
      'Só um lugar no mundo. Pendure o que quiser nele depois.',
    ObjetoNoPlano.solido => 'Uma placa deitada na superfície.',
    ObjetoNoPlano.forma => 'Um objeto sólido em cima da superfície.',
    ObjetoNoPlano.texto => 'Uma placa com o seu texto, deitada na superfície.',
  };
}

/// PÕE UM OBJETO NA SUPERFICIE, ja com posicao, giro e tamanho certos.
///
/// E aqui que o rastreio vira uso. Sem isto, a pessoa teria de ler tres
/// coordenadas e tres angulos da nuvem e digitar tudo a mao — e ninguem
/// faz isso num celular. O plano ja sabe onde e para que lado; o objeto
/// so precisa nascer alinhado com ele.
///
/// O TAMANHO SAI DA SUPERFICIE. Um objeto de tamanho fixo some numa
/// mesa grande e cobre o quadro inteiro numa pequena, e nos dois casos a
/// pessoa acha que o rastreio errou.
SceneNode noNoPlano(
  PlanoLike plano,
  ObjetoNoPlano tipo, {
  String? nome,
  String? textureLayerId,
  Color cor = AureaColors.selectionText,
}) {
  final (rx, ry, rz) = plano.anglesEmGraus;
  // Metade da extensao dos pontos, com um piso: um plano formado por
  // tres pontos quase juntos nao pode virar um objeto invisivel.
  final tamanho = math.max(40.0, plano.tamanho * 0.9);
  // O objeto POUSA, e nao afunda: um solido deitado exatamente no plano
  // briga com ele na hora de decidir qual pixel fica na frente, e o
  // resultado pisca. Meio por cento do tamanho da cena resolve.
  final alturinha = tamanho * 0.005;
  final o = plano.origem;
  final n = plano.normal;
  return SceneNode(
    name: nome ?? tipo.emPalavras,
    kind: tipo == ObjetoNoPlano.forma
        ? Element3DKind.cube
        : Element3DKind.plane,
    isNull: tipo == ObjetoNoPlano.nulo,
    size: tipo == ObjetoNoPlano.nulo ? 30 : tamanho,
    x: AnimatedDouble(o[0] + n[0] * alturinha),
    y: AnimatedDouble(o[1] + n[1] * alturinha),
    z: AnimatedDouble(o[2] + n[2] * alturinha),
    rotX: AnimatedDouble(rx),
    rotY: AnimatedDouble(ry),
    rotZ: AnimatedDouble(rz),
    material: Material3D(
      baseColor: cor,
      textureLayerId: textureLayerId,
      // O texto e o solido nao recebem luz: eles sao GRAFISMO em cima do
      // video, e uma placa que escurece quando a luz da cena vira
      // parece um erro, nao um efeito.
      kind: tipo == ObjetoNoPlano.forma ? MaterialKind.pbr : MaterialKind.unlit,
      roughness: .5,
    ),
  );
}

/// O que [noNoPlano] precisa saber de um plano. E uma interface pequena
/// de proposito: o dominio da cena nao deve depender do modulo do
/// rastreio so para ler seis numeros.
abstract class PlanoLike {
  List<double> get origem;
  List<double> get normal;
  double get tamanho;
  (double, double, double) get anglesEmGraus;
}
