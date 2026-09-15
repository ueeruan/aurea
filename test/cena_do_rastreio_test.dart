import 'dart:convert';
import 'dart:math' as math;
import 'dart:ui';

import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/camera_solver3d.dart';
import 'package:aurea/src/features/editor/domain/cena_do_rastreio.dart';
import 'package:aurea/src/features/editor/domain/pontos_seguidos.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:flutter_test/flutter_test.dart';

/// A PROVA DE QUE O RASTREIO GRUDA.
///
/// Os testes do solver mostram que ele acha a camera certa. Isso ainda
/// nao garante nada para quem usa: entre a solucao e a tela existe uma
/// traducao (posicao e alvo, giro em graus, distancia focal em
/// milimetros) e um erro ali poe a cena inteira tombada com o
/// enquadramento certinho — o defeito classico de camera rastreada, e o
/// mais dificil de perceber olhando so os numeros.
///
/// Entao o teste fecha o circulo: monta uma cena conhecida, resolve,
/// converte para a camera do app, e projeta a nuvem de volta USANDO A
/// PROJECAO DO MOTOR. Os pontos tem de cair onde estavam no video. Se
/// caem, gruda.
void main() {
  const largura = 240, altura = 135, quadros = 16;
  const focal = largura * 1.25;

  /// A mesma cena dos testes do solver: pontos num volume e uma camera
  /// que anda de lado com leve arco e uma INCLINACAO de verdade — sem
  /// inclinacao o erro de giro passaria despercebido.
  ({List<PontoSeguido> pontos, Map<int, Map<int, Offset>> obs}) cena() {
    final rng = math.Random(11);
    final mundo = [
      for (var i = 0; i < 90; i++)
        [
          (rng.nextDouble() - .5) * 900,
          (rng.nextDouble() - .5) * 500,
          900 + rng.nextDouble() * 900,
        ],
    ];
    final obs = <int, Map<int, Offset>>{};
    for (var q = 0; q < quadros; q++) {
      final u = q / (quadros - 1);
      final pos = [-420 + 840 * u, 40 * math.sin(u * math.pi), -60 * u];
      // O terceiro numero e a inclinacao da camera (giro no eixo do
      // olhar): 0,25 rad = 14 graus de camera torta.
      final r = rotacaoDeVetor([
        0.03 * math.sin(u * 3),
        -0.22 * (u - .5),
        0.25 * u,
      ]);
      final rt = r.aplicar(pos);
      final t = [-rt[0], -rt[1], -rt[2]];
      for (var i = 0; i < mundo.length; i++) {
        final p = projetar(r, t, mundo[i]);
        if (p == null) continue;
        final px = largura / 2 + p[0] * focal;
        final py = altura / 2 + p[1] * focal;
        if (px < 4 || py < 4 || px > largura - 4 || py > altura - 4) continue;
        (obs[i] ??= {})[q] = Offset(px, py);
      }
    }
    return (
      pontos: [
        for (final e in obs.entries)
          if (e.value.length >= 6)
            PontoSeguido(e.key, e.value.keys.reduce(math.min), e.value),
      ],
      obs: obs,
    );
  }

  /// A projecao do motor: mesma base, mesma escala, mesmo Y invertido
  /// que [renderScene3D] usa para por um vertice na tela.
  Offset? naTela(RenderCamera cam, Vec3 ponto, Size viewport) {
    final base = cameraBasis(cam);
    final f = viewport.width / 2 / math.tan(cam.fovRadians / 2);
    final rel = ponto - cam.position;
    final z = rel.dot(base.forward);
    if (z <= 1e-6) return null;
    return Offset(
      viewport.width / 2 + rel.dot(base.right) * f / z,
      viewport.height / 2 - rel.dot(base.up) * f / z,
    );
  }

  test('a camera montada reprojeta a nuvem em cima do video', () {
    final c = cena();
    final s = resolverCamera3D(
      c.pontos,
      largura: largura,
      altura: altura,
      quadros: quadros,
      fps: 8,
      focalPx: focal,
    );
    expect(s.erroPixels, lessThan(0.3));

    final cam = cameraDoRastreio(s);
    const viewport = Size(largura * 1.0, altura * 1.0);

    final erros = <double>[];
    for (final pose in s.poses) {
      final t = Duration(microseconds: (pose.quadro * 1000000 / 8).round());
      final rc = cam.renderAt(t);
      for (final e in s.nuvem.entries) {
        final visto = c.obs[e.key]?[pose.quadro];
        if (visto == null) continue;
        final v = e.value;
        final tela = naTela(rc, Vec3(v[0], v[1], v[2]), viewport);
        if (tela == null) continue;
        erros.add((tela - visto).distance);
      }
    }

    expect(erros.length, greaterThan(200), reason: 'muitos pontos conferidos');
    // O erro tem de ser o do solver e nada mais: se a conversao para a
    // camera do app estivesse errada, ele explodiria aqui.
    expect(
      mediana(erros),
      lessThan(0.5),
      reason: 'a cena 3D cai em cima do que a camera de verdade viu',
    );
    var pior = 0.0;
    for (final e in erros) {
      pior = math.max(pior, e);
    }
    expect(pior, lessThan(3.0), reason: 'nem o pior ponto escorrega');
  });

  test('a distancia focal em milimetros bate com a focal em pixels', () {
    final c = cena();
    final s = resolverCamera3D(
      c.pontos,
      largura: largura,
      altura: altura,
      quadros: quadros,
      focalPx: focal,
    );
    final mm = focalEmMilimetros(s);
    // 36 mm de filme e a convencao do app inteiro.
    expect(mm, closeTo(36 * focal / largura, 1e-9));
    // E o angulo de visao que sai dela e o mesmo que a focal em pixels
    // descreve.
    final fov = 2 * math.atan(36 / (2 * mm));
    expect(largura / 2 / math.tan(fov / 2), closeTo(focal, 1e-6));
  });

  test('a camada nasce pronta: camera rastreada e nuvem visivel', () {
    final c = cena();
    final s = resolverCamera3D(
      c.pontos,
      largura: largura,
      altura: altura,
      quadros: quadros,
      fps: 8,
      focalPx: focal,
    );
    final camada = camadaDoRastreio(
      s,
      startTime: const Duration(seconds: 1),
      duration: const Duration(seconds: 2),
      position: const Offset(960, 540),
    );
    expect(camada.startTime, const Duration(seconds: 1));
    expect(camada.camera.posX.keyframes.length, s.poses.length);
    // Fundo transparente: o video e que aparece atras.
    expect(camada.scene.background, isNull);
    final nuvem = camada.scene.nodes.single;
    expect(nuvem.instances.length, s.nuvem.length);

    // E um nulo em qualquer ponto rastreado cai onde o ponto esta.
    final id = s.nuvem.keys.first;
    final no = noNoPonto(s, id)!;
    expect(no.isNull, isTrue);
    expect(no.x.valueAt(Duration.zero), closeTo(s.nuvem[id]![0], 1e-9));
    expect(no.y.valueAt(Duration.zero), closeTo(s.nuvem[id]![1], 1e-9));
    expect(no.z.valueAt(Duration.zero), closeTo(s.nuvem[id]![2], 1e-9));
    expect(noNoPonto(s, -1), isNull);
  });

  test('a solucao sobrevive a ida e volta para o disco', () {
    final c = cena();
    final s = resolverCamera3D(
      c.pontos,
      largura: largura,
      altura: altura,
      quadros: quadros,
      focalPx: focal,
    );
    final volta = SolucaoCamera3D.decode(jsonEncode(s.toJson()));
    expect(volta, isNotNull);
    expect(volta!.poses.length, s.poses.length);
    expect(volta.nuvem.length, s.nuvem.length);
    expect(volta.focalPx, closeTo(s.focalPx, 1e-9));
    for (var i = 0; i < s.poses.length; i++) {
      expect(volta.poses[i].quadro, s.poses[i].quadro);
      for (var k = 0; k < 9; k++) {
        expect(volta.poses[i].rotacao.m[k], closeTo(s.poses[i].rotacao.m[k], 1e-9));
      }
    }
    expect(SolucaoCamera3D.decode('nao e json'), isNull);
    expect(SolucaoCamera3D.decode('{}'), isNull);
  });
}
