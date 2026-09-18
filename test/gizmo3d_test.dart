// O GIZMO 3D — a matematica dos tres eixos na viewport.
//
// O QUE ESTES TESTES SEGURAM, e por que cada um existe:
//
//   * o eixo aponta para onde a CAMERA diz, e nao para uma direcao fixa —
//     e a diferenca entre um gizmo 3D e um desenho de gizmo;
//   * arrastar o eixo Z muda a PROFUNDIDADE (positionZ) e nada mais —
//     "profundidade Z real" era o pedido, e um Z que mexesse na posicao
//     seria 2.5D disfarcado;
//   * o tamanho na tela do passo unitario encolhe com a profundidade, e
//     ELE que faz o arrasto ser sensivel igual de perto e de longe;
//   * um eixo apontando para o olho recusa o gesto em vez de dar um salto;
//   * o giro tem sinal: girar o dedo num sentido tem de girar o objeto
//     naquele sentido, com a camera em qualquer posicao.
//
// Todos os numeros aqui sao calculados a partir da MESMA cadeia que pinta
// a camada (`effectiveTransform` -> `projetarProfundidade`). Nao ha valor
// cravado copiado da tela.
import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/gizmo3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

const double _w = 1920;
const double _h = 1080;

VideoProject _projeto(List<Layer> camadas) => VideoProject(
  name: 'gizmo',
  createdAt: DateTime(2026),
  layers: camadas,
);

ImageLayer _camada3D({
  double x = _w / 2,
  double y = _h / 2,
  double z = 0,
  double rotX = 0,
  double rotY = 0,
  double rot = 0,
}) => ImageLayer(
  name: 'camada',
  startTime: Duration.zero,
  duration: const Duration(seconds: 5),
  is3D: true,
  position: AnimatedOffset(Offset(x, y)),
  positionZ: AnimatedDouble(z),
  rotationX: AnimatedDouble(rotX),
  rotationY: AnimatedDouble(rotY),
  rotation: AnimatedDouble(rot),
  sourcePath: 'a.png',
);

/// A CAMERA VIVE NO CENTRO DA COMPOSICAO — e o que o botao do app cria
/// (`addCameraLayer` usa `_center`). Montar a camera na origem daria um
/// olho fora do quadro, e a cena inteira sairia deslocada: os primeiros
/// testes desta sessao falharam exatamente por isso, e o defeito estava
/// no teste, nao no gizmo.
CameraLayer _camera({double rotX = 0, double rotY = 0, double rot = 0}) =>
    CameraLayer(
      name: 'Camera',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      position: AnimatedOffset(const Offset(_w / 2, _h / 2)),
      rotationX: AnimatedDouble(rotX),
      rotationY: AnimatedDouble(rotY),
      rotation: AnimatedDouble(rot),
    );

void main() {
  const t = Duration.zero;

  group('origem', () {
    test('sem camera, a camada 3D fica onde foi posta', () {
      final p = _projeto([_camada3D(x: 400, y: 300)]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      expect(g.origem.dx, closeTo(400, 1e-6));
      expect(g.origem.dy, closeTo(300, 1e-6));
    });

    test('recuar em Z encolhe E puxa para o centro (o ponto de fuga)', () {
      final p = _projeto([_camada3D(x: 400, y: 300, z: 600)]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      final k = 1200 / (1200 + 600);
      expect(g.escala, closeTo(k, 1e-9));
      expect(g.origem.dx, closeTo(_w / 2 + (400 - _w / 2) * k, 1e-6));
      expect(g.origem.dy, closeTo(_h / 2 + (300 - _h / 2) * k, 1e-6));
    });

    test('passou da camera: sem gizmo, porque nao ha camada na tela', () {
      final p = _projeto([_camada3D(z: -2000)]);
      expect(gizmoDaCamada(p, p.layers.first, t), isNull);
    });
  });

  group('direcao dos eixos', () {
    test('de frente: X para a direita, Y para baixo, Z para o centro', () {
      final p = _projeto([_camada3D()]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      // X: uma unidade de posicao e um pixel para a direita.
      expect(g.x.dx, closeTo(1, 1e-9));
      expect(g.x.dy, closeTo(0, 1e-9));
      // Y: uma unidade de posicao e um pixel para BAIXO (o y do motor).
      expect(g.y.dx, closeTo(0, 1e-9));
      expect(g.y.dy, closeTo(1, 1e-9));
      // Z DE FRENTE NAO ANDA NA TELA: o eixo aponta para o olho. O pouco
      // que sobra e a convergencia ao ponto de fuga (a camada esta fora
      // do centro), e nao um eixo de verdade.
      expect(g.z.distance, lessThan(0.01));
    });

    test('a camera girando, o eixo X deixa de ser horizontal', () {
      // 60 graus em Y: a cena roda, e o X do mundo passa a ter componente
      // de profundidade — na tela ele encurta e o Y continua inteiro.
      final p = _projeto([_camada3D(), _camera(rotY: 60)]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      // cos 60 na largura, menos o encolhimento do proprio passo ao
      // ganhar profundidade (1200/(1200+sin 60)) — a convergencia, que e
      // real e nao um erro de arredondamento.
      expect(g.x.dx, closeTo(0.5 * 1200 / (1200 + 0.8660254), 1e-6));
      expect(g.x.dy.abs(), lessThan(1e-6));
      expect(g.y.dy, closeTo(1, 1e-9));
      // E O Z APARECE: com a cena de lado, recuar anda na tela.
      expect(g.z.dx.abs(), greaterThan(0.5));
    });

    test('a camera girando 90 em Y, o Z toma o lugar do X', () {
      final p = _projeto([_camada3D(), _camera(rotY: 90)]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      expect(g.x.distance, closeTo(0, 1e-6));
      expect(g.z.dx.abs(), closeTo(1, 1e-6));
    });

    test('recuar encolhe os eixos na proporcao exata da perspectiva', () {
      // SEM CAMERA, para a conta ficar fechada: recuar para z=600 tem de
      // encolher cada eixo exatamente por 1200/1800. Com a camera girada
      // o Z tambem entra no X e no Y (eixos nao comutam), e a razao
      // deixaria de ser a mesma — o que nao seria defeito, mas este teste
      // deixaria de medir o que diz medir.
      final longe = _projeto([_camada3D(z: 600)]);
      final perto = _projeto([_camada3D(z: 0)]);
      final g = gizmoDaCamada(longe, longe.layers.first, t)!;
      final g0 = gizmoDaCamada(perto, perto.layers.first, t)!;
      final k = 1200 / 1800;
      expect(g.escala, closeTo(k, 1e-9));
      expect(g.x.distance, closeTo(g0.x.distance * k, 1e-9));
      expect(g.y.distance, closeTo(g0.y.distance * k, 1e-9));
    });

    test('com a camera girada, recuar encolhe os tres assim mesmo', () {
      final longe = _projeto([_camada3D(z: 600), _camera(rotY: 40)]);
      final perto = _projeto([_camada3D(z: 0), _camera(rotY: 40)]);
      final g = gizmoDaCamada(longe, longe.layers.first, t)!;
      final g0 = gizmoDaCamada(perto, perto.layers.first, t)!;
      for (final e in EixoDoGizmo.values) {
        expect(
          g.direcao(e).distance,
          lessThan(g0.direcao(e).distance),
          reason: 'o eixo $e nao encolheu ao recuar',
        );
      }
    });
  });

  group('o essencial: a camera NAO escolhe o eixo, o dedo escolhe', () {
    test('arrastar o eixo Z mexe na PROFUNDIDADE e em nada mais', () {
      // A cena de lado, senao o Z nao anda na tela — de frente ele e um
      // ponto, que e o caso do teste seguinte.
      final p = _projeto([_camada3D(), _camera(rotY: 90)]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      // O Z aponta ao longo de +-x da tela. O dedo anda 100 px nesse
      // sentido, e o eixo responde com 100 unidades de profundidade.
      final v = valorArrastado(
        eixo: EixoDoGizmo.z,
        gizmo: g,
        deltaTela: Offset(100 * g.z.dx.sign, 0),
        posInicial: const Offset(300, 400),
        zInicial: 0,
      );
      expect(v.z, closeTo(100, 1e-6));
      expect(v.pos, isNull, reason: 'o Z nao pode mexer na posicao');
    });

    test('arrastar o eixo X mexe na posicao X e nao na profundidade', () {
      final p = _projeto([_camada3D()]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      final v = valorArrastado(
        eixo: EixoDoGizmo.x,
        gizmo: g,
        deltaTela: const Offset(37, 0),
        posInicial: const Offset(300, 400),
        zInicial: 0,
      );
      expect(v.pos, const Offset(337, 400));
      expect(v.z, isNull);
    });

    test('o movimento de lado NAO conta: so a parte no eixo', () {
      final p = _projeto([_camada3D()]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      final v = valorArrastado(
        eixo: EixoDoGizmo.x,
        gizmo: g,
        // 50 no eixo e 50 fora dele.
        deltaTela: const Offset(50, 50),
        posInicial: Offset.zero,
        zInicial: 0,
      );
      expect(v.pos!.dx, closeTo(50, 1e-9));
      expect(v.pos!.dy, 0);
    });

    test('de lado, 100 px de dedo viram MAIS que 100 de mundo', () {
      // Com a cena girada 60 em Y, uma unidade de X anda so 0,5 px na
      // tela. O dedo andando 100 px tem de valer 200 unidades — senao o
      // objeto ficaria para tras do dedo em toda cena girada.
      final p = _projeto([_camada3D(), _camera(rotY: 60)]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      final v = valorArrastado(
        eixo: EixoDoGizmo.x,
        gizmo: g,
        deltaTela: const Offset(100, 0),
        posInicial: Offset.zero,
        zInicial: 0,
      );
      expect(v.pos!.dx, closeTo(200, 0.2));
    });
  });

  group('eixo apontando para o olho', () {
    test('o Z de frente e curto demais e nao deixa arrastar', () {
      final p = _projeto([_camada3D()]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      expect(eixoVisivel(g, EixoDoGizmo.z), isFalse);
      expect(eixoVisivel(g, EixoDoGizmo.x), isTrue);
      expect(eixoVisivel(g, EixoDoGizmo.y), isTrue);
      // E o dedo em cima da origem do Z nao pega o Z.
      expect(eixoNoDedo(g, g.origem + const Offset(0, 60), 90),
          EixoDoGizmo.y);
    });

    test('a camera de lado faz o Z aparecer', () {
      final p = _projeto([_camada3D(), _camera(rotY: 90)]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      expect(eixoVisivel(g, EixoDoGizmo.z), isTrue);
      final ponta = pontaDoEixo(g, EixoDoGizmo.z, 90);
      expect(eixoNoDedo(g, ponta, 90), EixoDoGizmo.z);
    });

    test('a ponta do eixo fica a distancia pedida, em qualquer camera', () {
      for (final ry in [0.0, 40.0, 90.0]) {
        final p = _projeto([_camada3D(), _camera(rotY: ry)]);
        final g = gizmoDaCamada(p, p.layers.first, t)!;
        final ponta = pontaDoEixo(g, EixoDoGizmo.y, 90);
        expect((ponta - g.origem).distance, closeTo(90, 1e-6));
      }
    });
  });

  group('anel de giro', () {
    test('de frente, o anel do Z e um circulo de raio fixo', () {
      final p = _projeto([_camada3D()]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      final anel = anelDeGiro(g, EixoDoGizmo.z, 70, passos: 32);
      for (final pt in anel) {
        expect((pt - g.origem).distance, closeTo(70, 0.5));
      }
    });

    test('o anel do Z acompanha a elipse que a tela mostra', () {
      // A cena girada 60 em Y: o circulo do Z (no plano XY) vira uma
      // elipse achatada no eixo X, e achatada exatamente por cos 60.
      final p = _projeto([_camada3D(), _camera(rotY: 60)]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      final anel = anelDeGiro(g, EixoDoGizmo.z, 70, passos: 128);
      var maxX = 0.0, maxY = 0.0;
      for (final pt in anel) {
        maxX = maxX > (pt.dx - g.origem.dx).abs()
            ? maxX
            : (pt.dx - g.origem.dx).abs();
        maxY = maxY > (pt.dy - g.origem.dy).abs()
            ? maxY
            : (pt.dy - g.origem.dy).abs();
      }
      expect(maxY, closeTo(70, 0.6), reason: 'o Y nao encurta');
      expect(maxX, closeTo(35, 0.6), reason: 'cos 60 no X');
    });

    test('o dedo em cima do anel e reconhecido; longe dele, nao', () {
      final p = _projeto([_camada3D()]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      final anel = anelDeGiro(g, EixoDoGizmo.z, 70, passos: 64);
      expect(anelNoDedo(g, anel[10], 70), EixoDoGizmo.z);
      expect(anelNoDedo(g, g.origem + const Offset(0, 200), 70), isNull);
    });
  });

  group('anel de perfil', () {
    test('de frente, so o anel do Z tem area; X e Y sao tracos', () {
      // Um circulo visto de lado E um traco — e um traco em cima do braco
      // do eixo rouba o gesto de quem queria MOVER. Era o defeito: tocar
      // em qualquer ponto do braco de X pegava o anel de Y, e o dedo
      // girava quando queria andar.
      final p = _projeto([_camada3D()]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      expect(anelVisivel(g, EixoDoGizmo.z), isTrue);
      expect(anelVisivel(g, EixoDoGizmo.x), isFalse);
      expect(anelVisivel(g, EixoDoGizmo.y), isFalse);
    });

    test('virada de lado, o anel do X aparece e o do Z some', () {
      final p = _projeto([_camada3D(), _camera(rotY: 90)]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      expect(anelVisivel(g, EixoDoGizmo.x), isTrue);
      expect(anelVisivel(g, EixoDoGizmo.z), isFalse);
    });

    test('anel de perfil nao pega o dedo, mesmo em cima do traco', () {
      final p = _projeto([_camada3D()]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      // O anel de Y, de frente, e o proprio braco de X. O dedo em cima
      // dele tem de encontrar o EIXO, e nao o anel — e o ponto e escolhido
      // FORA do anel do Z (raio 200), que e o unico que tem area aqui.
      final noBraco = g.origem + g.x * 90;
      expect(tocouNoAnel(g, EixoDoGizmo.y, noBraco, 200), isFalse);
      expect(anelNoDedo(g, noBraco, 200), isNull);
      expect(eixoNoDedo(g, noBraco, 200), EixoDoGizmo.x);
    });
  });

  group('sinal do giro', () {
    test('de frente, o anel do Z gira no mesmo sentido do dedo', () {
      final p = _projeto([_camada3D()]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      expect(sinalDoGiro(g, EixoDoGizmo.z), 1.0);
    });

    test('o sinal acompanha a camera: virando a cena, ele troca', () {
      // A camera indo para tras da camada inverte quem esta de frente.
      final p = _projeto([_camada3D(), _camera(rotY: 180)]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      expect(sinalDoGiro(g, EixoDoGizmo.z), -1.0);
    });

    test('o sinal e sempre +-1, em qualquer pose', () {
      for (final rx in [0.0, 30.0, 80.0, 150.0]) {
        for (final ry in [0.0, 45.0, 90.0, 200.0]) {
          final p = _projeto([_camada3D(rotX: rx, rotY: ry)]);
          final g = gizmoDaCamada(p, p.layers.first, t)!;
          for (final e in EixoDoGizmo.values) {
            expect(sinalDoGiro(g, e).abs(), 1.0);
          }
        }
      }
    });
  });

  group('giro entre dois pontos', () {
    test('90 graus no sentido do relogio da tela', () {
      const c = Offset(100, 100);
      expect(giroEntre(c, c + const Offset(50, 0), c + const Offset(0, 50)),
          closeTo(90, 1e-9));
    });

    test('nao pula uma volta ao cruzar o eixo do angulo', () {
      const c = Offset.zero;
      // De 170 graus para -170: o passo real e 20 graus, nao 340.
      final de = Offset(50 * _cos(170), 50 * _sin(170));
      final para = Offset(50 * _cos(-170), 50 * _sin(-170));
      expect(giroEntre(c, de, para), closeTo(20, 1e-6));
    });

    test('somar passos de 30 graus chega aos 360, sem volta perdida', () {
      const c = Offset.zero;
      var total = 0.0;
      var anterior = const Offset(50, 0);
      for (var i = 1; i <= 12; i++) {
        final agora = Offset(50 * _cos(i * 30), 50 * _sin(i * 30));
        total += giroEntre(c, anterior, agora);
        anterior = agora;
      }
      expect(total, closeTo(360, 1e-6));
    });

    test('12 passos de 30 graus no outro sentido chegam a -360', () {
      const c = Offset.zero;
      var total = 0.0;
      var anterior = const Offset(50, 0);
      for (var i = 1; i <= 12; i++) {
        final agora = Offset(50 * _cos(-i * 30), 50 * _sin(-i * 30));
        total += giroEntre(c, anterior, agora);
        anterior = agora;
      }
      expect(total, closeTo(-360, 1e-6));
    });
  });

  group('a alca do eixo nao pega longe da ponta', () {
    test('o dedo depois da ponta nao pega o eixo', () {
      final p = _projeto([_camada3D()]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      final ponta = pontaDoEixo(g, EixoDoGizmo.x, 90);
      expect(tocouNoEixo(g, EixoDoGizmo.x, ponta, 90), isTrue);
      expect(
        tocouNoEixo(g, EixoDoGizmo.x, ponta + const Offset(200, 0), 90),
        isFalse,
      );
    });

    test('a origem pega o primeiro eixo da ordem fixa (X)', () {
      final p = _projeto([_camada3D(), _camera(rotY: 35)]);
      final g = gizmoDaCamada(p, p.layers.first, t)!;
      expect(eixoNoDedo(g, g.origem, 90), EixoDoGizmo.x);
    });
  });
}

double _cos(double graus) => math.cos(graus * math.pi / 180);
double _sin(double graus) => math.sin(graus * math.pi / 180);
