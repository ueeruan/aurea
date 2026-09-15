// ROTACOES 3D COM VARIAS CAMADAS — o relato de campo era "saem
// invertidas". A orientacao composta (pai + camera) tem uma verdade
// unica: a MATRIZ. Estes testes varrem combinacoes de angulos (incluindo
// perto do gimbal) e cobram que os angulos devolvidos montem exatamente
// a matriz da composicao — para TODAS as camadas do lote, nao so as
// faceis.
//
// A causa achada: `vistoPelaCamera` girava a POSICAO pela cadeia inversa
// correta, mas a ORIENTACAO subtraia angulo por angulo. Com a camera
// girada num eixo so, bate; com dois eixos, rotacao nao comuta e cada
// camada saia com um erro proprio.

import 'dart:math' as math;

import 'package:flutter_test/flutter_test.dart';
import 'package:vector_math/vector_math_64.dart' as vm;

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/rotation_math.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

double _frobenius(vm.Matrix4 a, vm.Matrix4 b) {
  var soma = 0.0;
  for (var i = 0; i < 3; i++) {
    for (var j = 0; j < 3; j++) {
      final d = a.entry(i, j) - b.entry(i, j);
      soma += d * d;
    }
  }
  return math.sqrt(soma);
}

const _angulosDaCamera = [
  (0.0, 0.0, 0.0),
  (30.0, 0.0, 0.0),
  (0.0, 45.0, 0.0),
  (0.0, 0.0, 60.0),
  (60.0, 45.0, 0.0),
  (30.0, 45.0, 60.0),
  (90.0, 10.0, 20.0), // gimbal da camera
  (180.0, 0.0, 0.0),
];

const _angulosDaCamada = [
  (0.0, 0.0, 0.0),
  (25.0, 0.0, 0.0),
  (0.0, 35.0, 0.0),
  (0.0, 0.0, 50.0),
  (40.0, 70.0, 15.0),
  (90.0, 90.0, 0.0), // gimbal da camada
  (-120.0, 160.0, -80.0),
];

void main() {
  VideoProject projeto(List<Layer> layers) => VideoProject(
    name: 'p',
    createdAt: DateTime(2026),
    aspectRatio: 16 / 9,
    resolutionHeight: 1080,
    layers: layers,
  );

  group('camera da composicao', () {
    test('a orientacao vista e R(camera)^-1 * R(camada), sempre', () {
      for (final (cx, cy, cz) in _angulosDaCamera) {
        final cam = CameraLayer(
          name: 'Cam',
          startTime: Duration.zero,
          duration: const Duration(seconds: 5),
          rotationX: AnimatedDouble(cx),
          rotationY: AnimatedDouble(cy),
          rotation: AnimatedDouble(cz),
        );
        final p = projeto([cam]);
        for (final (lx, ly, lz) in _angulosDaCamada) {
          final mundo = LayerTransform(
            pos: const Offset(1200, 400),
            rot: lz,
            rotX: lx,
            rotY: ly,
            scale: 1,
            z: 120,
          );
          final vista = vistoPelaCamera(p, cam, Duration.zero, mundo);
          final esperada = rotationMatrix(cx, cy, cz)..transpose();
          esperada.multiply(rotationMatrix(lx, ly, lz));
          final obtida = rotationMatrix(vista.rotX, vista.rotY, vista.rot);
          expect(
            _frobenius(obtida, esperada),
            lessThan(1e-6),
            reason:
                'camera ($cx,$cy,$cz) x camada ($lx,$ly,$lz): '
                'angulos (${vista.rotX}, ${vista.rotY}, ${vista.rot})',
          );
        }
      }
    });

    test('com a camera num eixo so, os angulos continuam os de sempre', () {
      // O conserto nao pode mudar o caso simples: keyframes existentes
      // gravados com a conta antiga continuam batendo.
      final cam = CameraLayer(
        name: 'Cam',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        rotationY: AnimatedDouble(30),
      );
      final p = projeto([cam]);
      final vista = vistoPelaCamera(
        p,
        cam,
        Duration.zero,
        const LayerTransform(
          pos: Offset(960, 540),
          rot: 0,
          rotX: 0,
          rotY: 50,
          scale: 1,
          z: 0,
        ),
      );
      expect(vista.rotY, closeTo(20, 1e-6));
      expect(vista.rotX, closeTo(0, 1e-6));
      expect(vista.rot, closeTo(0, 1e-6));
    });

    test('duas camadas de frente continuam de frente sob a mesma camera', () {
      // O sintoma de campo: no MESMO quadro, uma camada aparecia certa e
      // a outra "invertida". De frente = a normal da camada aponta para
      // a camera; apos a vista, as duas orientacoes tem de ser IGUAIS.
      final cam = CameraLayer(
        name: 'Cam',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        rotationX: AnimatedDouble(35),
        rotationY: AnimatedDouble(-50),
      );
      final p = projeto([cam]);
      // Mesma orientacao escrita de dois jeitos equivalentes (a segunda
      // e a mesma matriz por outro caminho de Euler).
      final a = vistoPelaCamera(
        p,
        cam,
        Duration.zero,
        const LayerTransform(
          pos: Offset(700, 500),
          rot: 0,
          rotX: 35,
          rotY: -50,
          scale: 1,
          z: 0,
        ),
      );
      final b = vistoPelaCamera(
        p,
        cam,
        Duration.zero,
        LayerTransform(
          pos: const Offset(1300, 500),
          rot: rotationAngles(rotationMatrix(35, -50, 0)).$3,
          rotX: rotationAngles(rotationMatrix(35, -50, 0)).$1,
          rotY: rotationAngles(rotationMatrix(35, -50, 0)).$2,
          scale: 1,
          z: 0,
        ),
      );
      final ma = rotationMatrix(a.rotX, a.rotY, a.rot);
      final mb = rotationMatrix(b.rotX, b.rotY, b.rot);
      expect(_frobenius(ma, mb), lessThan(1e-6));
      // E "de frente" de verdade: identidade.
      expect(_frobenius(ma, vm.Matrix4.identity()), lessThan(1e-6));
    });
  });

  group('lote de camadas num nulo 3D', () {
    test('todas as filhas orbitam e giram pela MESMA matriz do nulo', () {
      for (final (nx, ny, nz) in _angulosDaCamera) {
        final nulo = NullLayer(
          name: 'Nulo',
          startTime: Duration.zero,
          duration: const Duration(seconds: 5),
          position: AnimatedOffset(const Offset(960, 540)),
          rotationX: AnimatedDouble(nx),
          rotationY: AnimatedDouble(ny),
          rotation: AnimatedDouble(nz),
        );
        final filhas = <TextLayer>[];
        final links = <PropertyLink>[];
        for (var i = 0; i < _angulosDaCamada.length; i++) {
          final (lx, ly, lz) = _angulosDaCamada[i];
          final filha = TextLayer(
            name: 'F$i',
            text: 'F$i',
            startTime: Duration.zero,
            duration: const Duration(seconds: 5),
            position: AnimatedOffset(Offset(960 + 80.0 * (i + 1), 540)),
            rotationX: AnimatedDouble(lx),
            rotationY: AnimatedDouble(ly),
            rotation: AnimatedDouble(lz),
            is3D: true,
          );
          filhas.add(filha);
          // Vinculo capturado com o nulo EM REPOUSO (base identidade):
          // o delta e a rotacao inteira do nulo.
          links.add(
            PropertyLink(
              targetLayerId: filha.id,
              targetProp: LayerProp.parent,
              sourceLayerId: nulo.id,
              offsetX: 960,
              offsetY: 540,
            ),
          );
        }
        final p = projeto([nulo, ...filhas]).copyWith(links: links);
        final rNulo = rotationMatrix(nx, ny, nz);
        for (var i = 0; i < filhas.length; i++) {
          final (lx, ly, lz) = _angulosDaCamada[i];
          final eff = effectiveTransform(p, filhas[i], Duration.zero);
          // Orientacao: R(nulo) * R(filha).
          final esperada = rNulo.clone()
            ..multiply(rotationMatrix(lx, ly, lz));
          final obtida = rotationMatrix(eff.rotX, eff.rotY, eff.rot);
          expect(
            _frobenius(obtida, esperada),
            lessThan(1e-6),
            reason: 'nulo ($nx,$ny,$nz), filha $i ($lx,$ly,$lz)',
          );
          // Posicao: ORBITA — o desvio da filha girado pelo nulo, e nao
          // a filha girando no proprio eixo com a posicao parada.
          final v = rNulo.transform3(vm.Vector3(80.0 * (i + 1), 0, 0));
          expect(eff.pos.dx, closeTo(960 + v.x, 1e-6));
          expect(eff.pos.dy, closeTo(540 + v.y, 1e-6));
          expect(eff.z, closeTo(v.z, 1e-6));
        }
      }
    });
  });
}
