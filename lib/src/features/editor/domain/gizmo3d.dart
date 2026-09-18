import 'dart:math' as math;
import 'dart:ui';

import 'layer.dart';
import 'video_project.dart';

/// O GIZMO 3D — os tres eixos da camada desenhados na viewport, e o dedo
/// que os arrasta.
///
/// ISTO NAO E UM DESENHO DECORATIVO, E AS DIRECOES NAO SAO ADIVINHADAS.
///
/// A direcao de cada eixo na tela e MEDIDA: pergunta-se a mesma cadeia que
/// pinta a camada (`effectiveTransform` -> `projetarProfundidade`) onde a
/// camada cairia com um passo a mais naquela propriedade, e a diferenca
/// entre as duas respostas e o eixo. Nao ha seno cravado, nao ha tabela
/// por angulo e nao ha convencao de sinal escrita a mao em lugar nenhum —
/// se o parentesco, a camera, o rig ou a projecao mudarem, o eixo acompanha
/// sozinho, porque ele e a derivada daquilo, e nao uma copia.
///
/// A CONSEQUENCIA PRATICA: arrastar o eixo Z muda `positionZ` e mais nada,
/// e o quanto ele muda e exatamente o quanto o dedo andou NAQUELE eixo.
/// Um eixo apontando para o olho tem comprimento zero na tela e recusa o
/// gesto (ver [eixoVisivel]) em vez de dar um salto — nao ha o que
/// arrastar quando nao ha para onde.
///
/// CONVENCAO (a do motor inteiro):
///   * X cresce para a DIREITA (vermelho);
///   * Y cresce para BAIXO (verde) — o y do Flutter, que e o da composicao;
///   * Z cresce AFASTANDO-SE do olho (azul) — e por isso recuar encolhe.
///
/// As cores sao as canonicas das ferramentas 3D, e nao gosto: quem ja usou
/// Blender ou Maya procura o eixo pela cor antes de ler o rotulo.
enum EixoDoGizmo { x, y, z }

/// X VERMELHO.
const Color corDoEixoX = Color(0xFFFF3B30);

/// Y VERDE.
const Color corDoEixoY = Color(0xFF30D158);

/// Z AZUL.
const Color corDoEixoZ = Color(0xFF0A84FF);

Color corDoEixo(EixoDoGizmo e) => switch (e) {
  EixoDoGizmo.x => corDoEixoX,
  EixoDoGizmo.y => corDoEixoY,
  EixoDoGizmo.z => corDoEixoZ,
};

String nomeDoEixo(EixoDoGizmo e) => switch (e) {
  EixoDoGizmo.x => 'X',
  EixoDoGizmo.y => 'Y',
  EixoDoGizmo.z => 'Z',
};

/// AS DUAS OUTRAS PONTAS DO CICLO x -> y -> z -> x.
///
/// `rotationMatrix` monta a pose como Rz*Ry*Rx, entao o giro de X leva o Y
/// para o Z, o de Y leva o Z para o X e o de Z leva o X para o Y — a mesma
/// regra da mao direita. E o que [sinalDoGiro] pergunta.
(EixoDoGizmo, EixoDoGizmo) cicloDoEixo(EixoDoGizmo e) => switch (e) {
  EixoDoGizmo.x => (EixoDoGizmo.y, EixoDoGizmo.z),
  EixoDoGizmo.y => (EixoDoGizmo.z, EixoDoGizmo.x),
  EixoDoGizmo.z => (EixoDoGizmo.x, EixoDoGizmo.y),
};

/// O GIZMO NUM INSTANTE: onde ele fica e o que cada eixo vale na tela.
///
/// [x], [y] e [z] sao pixels de composicao POR UNIDADE DA PROPRIEDADE, e
/// ja incluem a perspectiva — recuar encolhe os tres, porque os tres saem
/// do mesmo ponto de fuga.
class GizmoNaTela {
  const GizmoNaTela({
    required this.origem,
    required this.x,
    required this.y,
    required this.z,
    required this.escala,
  });

  /// Onde a camada esta, em pixels da composicao.
  final Offset origem;

  final Offset x;
  final Offset y;
  final Offset z;

  /// O fator de perspectiva (1200 / (1200 + z)) do instante.
  final double escala;

  Offset direcao(EixoDoGizmo e) => switch (e) {
    EixoDoGizmo.x => x,
    EixoDoGizmo.y => y,
    EixoDoGizmo.z => z,
  };
}

/// A CAMADA PASSOU DA CAMERA? Nao ha gizmo possivel: ela nao e desenhada.
const double _zPertoDaCamera = -1100;

/// OS EIXOS DA CAMADA NA TELA. Nulo quando a camada esta atras da camera
/// (nao aparece — e por isso nao pode ter alca).
GizmoNaTela? gizmoDaCamada(VideoProject project, Layer layer, Duration t) {
  final ortografica = cameraAtivaEm(project, t)?.opcoes.ortografica ?? false;

  /// ONDE A CAMADA CAI NA COMPOSICAO, com um deslocamento autoral por
  /// cima. E a MESMA pergunta que o pintor responde, com a mesma cadeia.
  Offset? tela({Offset? pos, double? z}) {
    final eff = effectiveTransform(
      project,
      layer,
      t,
      null,
      AjusteDeTransform(pos: pos, z: z),
    );
    final vista = projetarProfundidade(
      project,
      eff.pos,
      eff.z,
      ortografica: ortografica,
    );
    return vista?.pos;
  }

  final base = effectiveTransform(project, layer, t);
  final vista = projetarProfundidade(
    project,
    base.pos,
    base.z,
    ortografica: ortografica,
  );
  if (vista == null || base.z <= _zPertoDaCamera) return null;

  final local = layer.localTime(t);
  final p0 = layer.position.valueAt(local);
  final z0 = layer.positionZ.valueAt(local);

  final origem = vista.pos;
  // O PASSO NAO PODE SER ZERO. Uma camada com keyframe de posicao no
  // instante pode ter qualquer valor, mas o passo e sempre 1 unidade
  // autoral — o que muda com a profundidade e a RESPOSTA na tela, e e
  // justamente ela que encolhe.
  final px = tela(pos: p0 + const Offset(1, 0), z: z0);
  final py = tela(pos: p0 + const Offset(0, 1), z: z0);
  final pz = tela(pos: p0, z: z0 + 1);

  return GizmoNaTela(
    origem: origem,
    x: (px ?? origem) - origem,
    y: (py ?? origem) - origem,
    // O EIXO Z SOME QUANDO O PASSO SAI DO CAMPO DE VISAO: devolver o
    // vetor do ponto projetado a partir da origem daria um eixo de
    // comprimento absurdo. Zero e a resposta honesta (o eixo aponta para
    // o olho), e [eixoVisivel] recusa o gesto.
    z: (pz ?? origem) - origem,
    escala: vista.escala,
  );
}

/// UM EIXO QUE APONTA PARA O OLHO NAO DA PARA ARRASTAR.
///
/// Visto de frente, o eixo Z e um ponto: "andar nesse eixo" nao move nada
/// na tela, e o arrasto viraria um salto de sensibilidade imprevisivel. A
/// alca continua sendo DESENHADA — esconder o eixo seria esconder que ele
/// existe —, mas recusa o gesto, e o palco diz por que.
///
/// O LIMITE E UMA RAZAO, e nao um numero de pixels.
///
/// Comparar com um comprimento em pixels seria comparar com a escala do
/// palco, que muda com o zoom: o mesmo eixo ficaria utilizavel ou nao
/// conforme o tamanho da janela. E os eixos aqui medem "pixels por unidade
/// da propriedade", uma unidade minuscula — todos eles seriam "curtos"
/// nessa regua. O que decide e o quanto ele encolheu PERTO DOS OUTROS:
/// um eixo menor que um decimo do maior esta virado para o olho.
const double kEixoQuasePonto = 0.10;

bool eixoVisivel(GizmoNaTela g, EixoDoGizmo e) {
  final maior = [g.x.distance, g.y.distance, g.z.distance].reduce(
    (a, b) => a > b ? a : b,
  );
  if (maior <= 0) return false;
  return g.direcao(e).distance >= maior * kEixoQuasePonto;
}

/// QUANTO ANDAR NA PROPRIEDADE PARA O DEDO SAIR DE [deltaTela].
///
/// E a projecao do movimento do dedo SOBRE a direcao do eixo: a parte do
/// gesto que anda no eixo, e nada da que anda de lado. Dividir pelo
/// quadrado do comprimento faz um eixo curto na tela (apontando para o
/// olho) responder na mesma proporcao de um eixo longo — sem isso a
/// sensibilidade mudaria com o angulo da camera, que e o defeito que faz
/// um gizmo parecer quebrado.
double avancoNoEixo(Offset direcao, Offset deltaTela) {
  final d2 = direcao.distanceSquared;
  if (d2 < 1e-9) return 0;
  return (deltaTela.dx * direcao.dx + deltaTela.dy * direcao.dy) / d2;
}

/// O SINAL DO GIRO: girar em torno de [e] pelo angulo da TELA aumenta ou
/// diminui a propriedade?
///
/// A rotacao positiva leva a ponta do eixo seguinte do ciclo para a do
/// seguinte — a regra da mao direita. Na tela, "levar b para c" e um
/// sentido ou o outro conforme a orientacao dos dois, e e o produto
/// vetorial 2D que diz qual. Sem esta conta o anel de X girava ao
/// contrario com a camera atras da camada.
double sinalDoGiro(GizmoNaTela g, EixoDoGizmo e) {
  final (b, c) = cicloDoEixo(e);
  final vb = g.direcao(b);
  final vc = g.direcao(c);
  // PRODUTO NULO: os dois eixos do ciclo sao colineares na tela (a camada
  // esta de perfil e um deles virou um ponto). Nao ha sentido a escolher;
  // +1 e a resposta estavel.
  return (vb.dx * vc.dy - vb.dy * vc.dx) >= 0 ? 1.0 : -1.0;
}

/// O ANGULO (graus) GIRADO ENTRE DOIS PONTOS EM VOLTA DE [centro].
///
/// Nao e `atan2(para) - atan2(de)` cru: essa diferenca pula 360 graus ao
/// cruzar o eixo do angulo, e o objeto daria uma volta inteira no meio do
/// gesto. O desvio e reduzido a faixa (-180, 180], que e o passo real do
/// dedo entre dois eventos.
double giroEntre(Offset centro, Offset de, Offset para) {
  final a0 = math.atan2(de.dy - centro.dy, de.dx - centro.dx);
  final a1 = math.atan2(para.dy - centro.dy, para.dx - centro.dx);
  var d = (a1 - a0) * 180 / math.pi;
  while (d > 180) {
    d -= 360;
  }
  while (d <= -180) {
    d += 360;
  }
  return d;
}

/// O VALOR DA PROPRIEDADE DEPOIS DE ARRASTAR O EIXO [e] POR [deltaTela].
///
/// X e Y mexem na POSICAO (o `Offset` autoral); Z mexe na PROFUNDIDADE.
/// Devolver os dois num record so evita que o palco escolha o campo por
/// conta propria e erre um deles.
({Offset? pos, double? z}) valorArrastado({
  required EixoDoGizmo eixo,
  required GizmoNaTela gizmo,
  required Offset deltaTela,
  required Offset posInicial,
  required double zInicial,
}) {
  final passo = avancoNoEixo(gizmo.direcao(eixo), deltaTela);
  return switch (eixo) {
    EixoDoGizmo.x => (pos: posInicial + Offset(passo, 0), z: null),
    EixoDoGizmo.y => (pos: posInicial + Offset(0, passo), z: null),
    EixoDoGizmo.z => (pos: null, z: zInicial + passo),
  };
}

/// A PONTA DO EIXO [e], a [comprimento] pixels de COMPOSICAO da origem.
Offset pontaDoEixo(GizmoNaTela g, EixoDoGizmo e, double comprimento) {
  final d = g.direcao(e);
  final modulo = d.distance;
  if (modulo < 1e-6) return g.origem;
  return g.origem + d * (comprimento / modulo);
}

/// O ANEL DE GIRO DO EIXO [e]: a circunferencia de raio [raio] no plano
/// dos outros dois eixos, projetada pelo que eles medem na tela.
///
/// Uma circunferencia de verdade no espaco vira uma ELIPSE na tela, e e
/// isso que se desenha: a base e o par de direcoes unitarias dos outros
/// dois eixos. Achatar num circulo faria o anel mentir sobre a orientacao
/// da camada — a mentira exata que este gizmo existe para nao contar.
List<Offset> anelDeGiro(
  GizmoNaTela g,
  EixoDoGizmo e,
  double raio, {
  int passos = 64,
}) {
  final (b, c) = cicloDoEixo(e);
  final db = g.direcao(b);
  final dc = g.direcao(c);
  // AS DIRECOES ENTRAM INTEIRAS, sem virar unitarias. E o comprimento
  // delas que achata a elipse: com a camera de lado, o eixo que aponta
  // para o olho mede quase nada na tela, e o anel tem de sair fino —
  // exatamente como uma circunferencia vista de perfil. Normalizar daria
  // um circulo perfeito numa cena que nao tem circulo nenhum.
  return [
    for (var i = 0; i <= passos; i++)
      g.origem +
          db * (raio * math.cos(i * 2 * math.pi / passos)) +
          dc * (raio * math.sin(i * 2 * math.pi / passos)),
  ];
}

/// O ANEL DE [e] TEM AREA NA TELA? Nao basta existir: um anel visto de
/// perfil e um TRACO, e um traco em cima do braco do eixo rouba o gesto
/// de quem queria MOVER.
///
/// E o caso da camada de frente: os aneis de X e de Y viram segmentos ao
/// longo do proprio eixo — nao ha circunferencia nenhuma para pegar, e o
/// unico anel de verdade e o do Z. Sem esta guarda, tocar em qualquer
/// ponto do braco de X pegava o ANEL de Y, e o dedo girava quando queria
/// andar.
///
/// O limite e a ABERTURA em relacao ao maior eixo: a area normalizada
/// pelo quadrado do maior comprimento e o seno do angulo entre os dois
/// eixos do ciclo.
const double kAnelMinimoDeAbertura = 0.20;

bool anelVisivel(GizmoNaTela g, EixoDoGizmo e) {
  final (b, c) = cicloDoEixo(e);
  final vb = g.direcao(b);
  final vc = g.direcao(c);
  final maior = [g.x.distance, g.y.distance, g.z.distance].reduce(
    (a, b) => a > b ? a : b,
  );
  if (maior <= 0) return false;
  final area = (vb.dx * vc.dy - vb.dy * vc.dx).abs();
  return area >= maior * maior * kAnelMinimoDeAbertura;
}

/// O DEDO ESTA EM CIMA DO EIXO [e]?
///
/// Distancia ao SEGMENTO, e nao a reta infinita: a alca acaba na ponta, e
/// mirar depois dela — do lado de fora do gizmo — nao pode pegar.
bool tocouNoEixo(
  GizmoNaTela g,
  EixoDoGizmo e,
  Offset dedo,
  double comprimento, {
  double tolerancia = 22,
}) {
  final inicio = g.origem;
  final v = pontaDoEixo(g, e, comprimento) - inicio;
  final l2 = v.distanceSquared;
  if (l2 < 1e-6) return (dedo - inicio).distance <= tolerancia;
  final t =
      (((dedo - inicio).dx * v.dx + (dedo - inicio).dy * v.dy) / l2).clamp(
        0.0,
        1.0,
      );
  return (dedo - (inicio + v * t)).distance <= tolerancia;
}

/// O DEDO ESTA EM CIMA DO ANEL DE [e]?
bool tocouNoAnel(
  GizmoNaTela g,
  EixoDoGizmo e,
  Offset dedo,
  double raio, {
  double tolerancia = 26,
}) {
  // A DISTANCIA A ELIPSE, e nao ao circulo de raio fixo: num anel visto
  // de lado, olhar so o raio daria uma coroa perfeita onde nao ha nada
  // desenhado.
  // ANEL DE PERFIL NAO SE PEGA: ele nao e um anel, e um traco por cima do
  // braco do eixo — ver [anelVisivel].
  if (!anelVisivel(g, e)) return false;
  var melhor = double.infinity;
  for (final p in anelDeGiro(g, e, raio, passos: 48)) {
    final d = (dedo - p).distance;
    if (d < melhor) melhor = d;
  }
  return melhor <= tolerancia;
}

/// O EIXO MAIS PROXIMO DO DEDO, ou nulo. A ORDEM DE PROVA e X, Y, Z — o
/// desempate tem de ser fixo, senao dois eixos quase sobrepostos ficam
/// alternando entre gestos e o objeto treme.
EixoDoGizmo? eixoNoDedo(
  GizmoNaTela g,
  Offset dedo,
  double comprimento, {
  double tolerancia = 22,
}) {
  for (final e in EixoDoGizmo.values) {
    if (!eixoVisivel(g, e)) continue;
    if (tocouNoEixo(g, e, dedo, comprimento, tolerancia: tolerancia)) return e;
  }
  return null;
}

/// O ANEL MAIS PROXIMO DO DEDO, ou nulo.
EixoDoGizmo? anelNoDedo(
  GizmoNaTela g,
  Offset dedo,
  double raio, {
  double tolerancia = 26,
}) {
  for (final e in EixoDoGizmo.values) {
    if (tocouNoAnel(g, e, dedo, raio, tolerancia: tolerancia)) return e;
  }
  return null;
}
