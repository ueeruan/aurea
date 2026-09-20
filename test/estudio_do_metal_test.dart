// O ESTUDIO QUE O METAL REFLETE — o mapa de ambiente, provado no PC.
//
// O pedido do dono: "texto metalico igual ao Element 3D". Um metal nao tem
// cor propria: ele e o que ele reflete. Refletindo duas cores (ceu e chao),
// o ouro sai como um bronze fosco e chapado, sem nenhuma softbox desenhando
// a quina da letra. O estudio ja estava escrito no aplicativo desde o pintor
// de CPU; o que faltava era ele CHEGAR na placa.
//
// O motor nao roda no PC, entao aqui se prova o que ele RECEBE: o mapa com a
// cadeia de desfoque, normalizado, e o caminho que o poe na cena.

import 'dart:math' as math;

import 'package:aurea/src/features/editor/application/motor3d_nativo.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/environment_radiance.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

/// O tamanho de cada nivel da cadeia, do maior para o menor.
List<(int, int)> _niveis(int largura, int niveis) {
  final saida = <(int, int)>[];
  var l = largura, a = largura ~/ 2;
  for (var k = 0; k < niveis; k++) {
    saida.add((l, a));
    l = l <= 2 ? l : l >> 1;
    a = a <= 1 ? a : a >> 1;
  }
  return saida;
}

void main() {
  test('o mapa tem a media em 1 e as softboxes muito acima dela', () {
    for (final kind in EnvironmentKind.values) {
      final mapa = mapaDeAmbiente3D(kind);
      final (l0, a0) = _niveis(mapa.largura, mapa.niveis).first;
      var media = 0.0, peso = 0.0, pico = 0.0;
      for (var y = 0; y < a0; y++) {
        final latitude = math.pi * (y + 0.5) / a0;
        final w = math.sin(latitude);
        for (var x = 0; x < l0; x++) {
          final i = (y * l0 + x) * 4;
          final lum = 0.2126 * mapa.pixels[i] +
              0.7152 * mapa.pixels[i + 1] +
              0.0722 * mapa.pixels[i + 2];
          media += lum * w;
          peso += w;
          if (lum > pico) pico = lum;
        }
      }
      // A MEDIA VALE 1: e o que mantem o brilho da cena onde ele estava e
      // deixa so a DIRECAO mudar. Sem isso, um estudio com picos de 14
      // estouraria a cena inteira para branco.
      expect(media / peso, closeTo(1.0, 0.01), reason: kind.name);
      // E O PICO CONTINUA SENDO UM PICO: e ele que desenha a quina clara.
      // So os ambientes de estudio tem softbox — os outros sao um gradiente
      // de ceu, e ali um pico alto seria um defeito.
      if (kind == EnvironmentKind.estudio ||
          kind == EnvironmentKind.estudioMetal) {
        expect(pico, greaterThan(3.0), reason: '${kind.name}: sem softbox');
      }
    }
  });

  test('o estudio metal reflete softboxes, e nao um gradiente', () {
    final mapa = mapaDeAmbiente3D(EnvironmentKind.estudioMetal);
    final (l0, a0) = _niveis(mapa.largura, mapa.niveis).first;
    double em(int x, int y) => mapa.pixels[(y * l0 + x) * 4];

    // O MAIS CLARO DE TUDO: radiancia muito acima de 1, e no ceu — a caixa
    // principal do estudio fica no alto.
    var pico = 0.0;
    var picoX = 0, picoY = 0;
    for (var y = 0; y < a0; y++) {
      for (var x = 0; x < l0; x++) {
        final v = em(x, y);
        if (v > pico) {
          pico = v;
          picoX = x;
          picoY = y;
        }
      }
    }
    expect(pico, greaterThan(5.0), reason: 'sem softbox: nada para refletir');
    expect(picoY, lessThan(a0 ~/ 2), reason: 'a caixa principal saiu do ceu');
    // A TIRA VERTICAL DA DIREITA: um segundo pico, do outro lado — e o que
    // da a quina da letra um brilho que corre quando ela gira.
    var tira = 0.0;
    for (var y = 0; y < a0; y++) {
      for (var x = (l0 * 2) ~/ 3; x < l0; x++) {
        final v = em(x, y);
        if (v > tira) tira = v;
      }
    }
    expect(tira, greaterThan(2.0), reason: 'a tira da direita sumiu');
    // E O FUNDO: quase preto. E o CONTRASTE que o olho le como metal; um
    // estudio claro por igual daria um borrao de bronze.
    var fundo = 0.0;
    for (var y = (a0 * 3) ~/ 4; y < a0; y++) {
      for (var x = 0; x < l0; x++) {
        final v = em(x, y);
        if (v > fundo) fundo = v;
      }
    }
    expect(fundo, lessThan(0.2), reason: 'o fundo claro demais: sem contraste');
    // ignore: avoid_print
    print('ESTUDIO: pico=${pico.toStringAsFixed(1)} em ($picoX,$picoY), '
        'tira=${tira.toStringAsFixed(1)} fundo=${fundo.toStringAsFixed(3)}');
  });

  test('a cadeia de niveis encolhe ate virar quase uma cor so', () {
    const largura = 256, niveis = niveisDoMapaDeAmbiente;
    final mapa = mapaDeAmbiente3D(EnvironmentKind.estudioMetal);
    final tamanhos = _niveis(largura, niveis);
    var esperado = 0;
    for (final (l, a) in tamanhos) {
      esperado += l * a * 4;
    }
    expect(mapa.pixels.length, esperado);
    expect(mapa.largura, largura);
    expect(mapa.niveis, niveis);
    expect(tamanhos.last, (2, 1), reason: 'o ultimo nivel tem de ser 2x1');

    // A CADEIA NAO DESVIA: cada nivel tem de continuar valendo a media do
    // mapa, que e 1. E ESTE o defeito que a ponderacao por area conserta —
    // sem ela, cada nivel descia puxado pelos polos e o valor DOBRAVA a
    // cada passo: o metal fosco (que amostra o ultimo nivel) sairia cem
    // vezes claro demais.
    var deslocamento = 0;
    for (final (l, a) in tamanhos) {
      var soma = 0.0;
      final n = l * a;
      for (var i = 0; i < n; i++) {
        soma += mapa.pixels[deslocamento + i * 4];
      }
      expect(
        soma / n,
        inInclusiveRange(0.6, 1.6),
        reason: 'o nivel ${l}x$a saiu da media',
      );
      deslocamento += n * 4;
    }
  });

  test('a cena leva o estudio do tipo dela para o motor', () {
    final cena = Scene3DLayer(
      id: 'cena',
      name: 'Texto 3D',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      scene: Scene3D(
        nodes: [SceneNode(name: 'TEXTO')],
        environment: EnvironmentKind.estudioMetal,
      ),
    );
    final motor = Motor3DNativo.instance;
    final montada = motor.montar(
      cena: cena.scene,
      camera: const RenderCamera(position: Vec3(0, 0, 120)),
      local: Duration.zero,
      largura: 512,
      altura: 512,
    );
    expect(montada.ambienteMapa, isNotNull);
    expect(montada.ambienteMapaLargura, 256);
    expect(montada.ambienteMapaNiveis, niveisDoMapaDeAmbiente);
    // O MESMO MAPA PARA A MESMA CENA: assar de novo a cada quadro seria
    // dezenas de milissegundos por quadro sem nada mudar.
    final outra = motor.montar(
      cena: cena.scene,
      camera: const RenderCamera(position: Vec3(0, 0, 120)),
      local: Duration.zero,
      largura: 512,
      altura: 512,
    );
    expect(identical(outra.ambienteMapa, montada.ambienteMapa), isTrue);
    motor.limpar();
  });

  test('assar o estudio custa poucos milissegundos', () {
    // O MAPA E ASSADO NO FIO DA INTERFACE, e nao num isolate: abrir um
    // isolate custa mais de um segundo no iPhone (medido em 15/09). O preco
    // e este numero — e ele tem de caber entre dois quadros.
    final relogio = Stopwatch()..start();
    mapaDeAmbiente3D(EnvironmentKind.estudioMetal);
    relogio.stop();
    // ignore: avoid_print
    print('MAPA DE AMBIENTE: ${relogio.elapsedMilliseconds} ms');
    expect(relogio.elapsedMilliseconds, lessThan(120));
  });
}
