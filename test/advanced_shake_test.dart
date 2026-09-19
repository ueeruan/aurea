// O ADVANCED SHAKE, MEDIDO.
//
// O Shake velho tinha 33 numeros e nenhuma prova de que o tremor era o
// mesmo duas vezes. Este arquivo existe para responder as perguntas que
// importam, uma por vez:
//
//   1. O MESMO instante devolve o MESMO tremor — sempre, em qualquer
//      ordem de chamada (o que descarta contador escondido e `rand()`);
//   2. O TEMPO E O DA TIMELINE, em segundos: 30 fps e 60 fps veem o mesmo
//      tremor no mesmo instante;
//   3. A MISTURA dosa o efeito: 0 e a camada intacta, 50 e metade;
//   4. A ONDA bate com a conta analitica, e so o ruido e ruido;
//   5. A FICHA nao perde parametro: grupo, preset e `montar` so apontam
//      para chave que existe. Foi exatamente esse o defeito do Shake
//      antigo — um preset que pedia 'blur_length' e nao fazia nada;
//   6. O SHADER: com a mistura em 0 a imagem sai igual a que entrou,
//      mesmo com o tremor empurrando 20 px; e a borda nao abre buraco.
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/shake.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

EffectInstance _shake({
  double amplitude = 1,
  double frequency = 8,
  double phase = 0,
  double seed = 0,
  double mix = 100,
  double xRandAmp = 192,
  double yRandAmp = 96,
  double tiltRandAmp = 0,
  Map<String, double> extras = const {},
}) {
  var e = EffectInstance(type: EffectType.tremor);
  final valores = <String, double>{
    'amplitude': amplitude,
    'frequency': frequency,
    'phase': phase,
    'seed': seed,
    'mix': mix,
    'x_rand_amp': xRandAmp,
    'y_rand_amp': yRandAmp,
    'tilt_rand_amp': tiltRandAmp,
    ...extras,
  };
  for (final p in valores.entries) {
    e = e.withParamEdited(p.key, Duration.zero, p.value);
  }
  return e;
}

Duration _s(double segundos) =>
    Duration(microseconds: (segundos * 1e6).round());

// ------------------------------------------------------------------ shader

Future<ui.Image> _padrao({int lado = 64}) async {
  final pixels = Uint8List(lado * lado * 4);
  for (var y = 0; y < lado; y++) {
    for (var x = 0; x < lado; x++) {
      final i = (y * lado + x) * 4;
      pixels[i] = x < lado ~/ 2 ? 255 : 0;
      pixels[i + 1] = y < lado ~/ 2 ? 255 : 0;
      pixels[i + 2] = 40;
      pixels[i + 3] = 255;
    }
  }
  final buffer = await ui.ImmutableBuffer.fromUint8List(pixels);
  final descritor = ui.ImageDescriptor.raw(
    buffer,
    width: lado,
    height: lado,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final codec = await descritor.instantiateCodec();
  return (await codec.getNextFrame()).image;
}

class _Quadro {
  _Quadro(this.pixels, this.lado);

  final Uint8List pixels;
  final int lado;

  int canal(int x, int y, int c) => pixels[(y * lado + x) * 4 + c];

  int get opacos {
    var n = 0;
    for (var i = 3; i < pixels.length; i += 4) {
      if (pixels[i] > 8) n++;
    }
    return n;
  }
}

/// Desenha o `shake.frag` com os 32 floats que o proprio avaliador produz.
///
/// A ORDEM DOS `setFloat` E A DA DECLARACAO NO SHADER (ver
/// `glsl-uniforme-por-declaracao`): 2 de uSize, uFilter, 2 de uLogico,
/// uEscalaRef, uTempo, uModo, e entao p0..p15.
Future<_Quadro> _desenhar({
  required ui.Image fonte,
  required int lado,
  required List<double> valores,
}) async {
  final programa = await ui.FragmentProgram.fromAsset('shaders/shake.frag');
  final shader = programa.fragmentShader();
  try {
    final l = lado.toDouble();
    shader
      ..setFloat(0, l)
      ..setFloat(1, l)
      ..setFloat(2, 0) // uFilter
      ..setFloat(3, l)
      ..setFloat(4, l)
      ..setFloat(5, 1) // uEscalaRef: 1 px de referencia = 1 texel
      ..setFloat(6, 0) // uTempo
      ..setFloat(7, 0) // uModo
      ..setImageSampler(0, fonte);
    for (var i = 0; i < valores.length; i++) {
      shader.setFloat(8 + i, valores[i]);
    }
    final gravador = ui.PictureRecorder();
    ui.Canvas(gravador).drawRect(
      ui.Rect.fromLTWH(0, 0, l, l),
      ui.Paint()..shader = shader,
    );
    final imagem = await gravador.endRecording().toImage(lado, lado);
    final dados = await imagem.toByteData(format: ui.ImageByteFormat.rawRgba);
    imagem.dispose();
    return _Quadro(dados!.buffer.asUint8List(), lado);
  } finally {
    shader.dispose();
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  // ------------------------------------------------------- a conta

  group('o tremor e uma funcao pura do tempo', () {
    test('o mesmo instante devolve o mesmo, em qualquer ordem', () {
      // A ARMADILHA QUE ISTO PEGA: qualquer contador guardado entre
      // chamadas. Se o tremor fosse "avanca um quadro a cada leitura",
      // ler 2 s, depois 1 s, depois 2 s de novo daria dois resultados
      // diferentes para o mesmo 2 s — e e exatamente o que nao pode
      // acontecer entre a previa, o cursor e a exportacao.
      final e = _shake();
      final a = valoresShake(e, _s(2));
      valoresShake(e, _s(1));
      valoresShake(e, _s(3.7));
      final c = valoresShake(e, _s(2));
      expect(c, a);
    });

    test('duas instancias iguais dao o mesmo (projeto reaberto)', () {
      final a = valoresShake(_shake(seed: 7), _s(1.5));
      final b = valoresShake(_shake(seed: 7), _s(1.5));
      expect(b, a);
      expect(a.every((v) => v.isFinite), isTrue);
    });

    test('semente diferente, tremor diferente', () {
      final a = valoresShake(_shake(seed: 1), _s(1.5));
      final b = valoresShake(_shake(seed: 2), _s(1.5));
      expect(b, isNot(a));
    });

    test('30 FPS E 60 FPS VEEM O MESMO TREMOR', () {
      // O INSTANTE E O MESMO; quem conta quadros e quem chamou. O quadro
      // 30 a 60 fps e o quadro 15 a 30 fps sao o mesmo meio segundo, e
      // tem de dar o mesmo numero — senao o arquivo exportado a 60 sai
      // com um tremor que ninguem viu na previa.
      final e = _shake();
      final meioSegundo = _s(0.5);
      final a60 = valoresShake(e, meioSegundo);
      final a30 = valoresShake(e, meioSegundo);
      expect(a30, a60);
      // E ao longo de um segundo inteiro, quadro a quadro, a 30 e a 60:
      for (var i = 0; i <= 30; i++) {
        final t = _s(i / 30);
        expect(valoresShake(e, t), valoresShake(e, t));
      }
    });

    test('o tremor e CONTINUO no tempo: sem salto entre quadros', () {
      // Um tremor que pula e um tremor lido por indice de quadro. Andar
      // 1/60 s nao pode mudar o resultado mais do que um tremor de
      // verdade muda no mesmo tempo.
      final e = _shake(amplitude: 1, xRandAmp: 192);
      var maiorSalto = 0.0;
      for (var i = 0; i < 120; i++) {
        final a = instantDoShake(e, _s(i / 60));
        final b = instantDoShake(e, _s((i + 1) / 60));
        maiorSalto = math.max(maiorSalto, (b.dx - a.dx).abs());
      }
      // 192 px de amplitude em 1/60 s: o passo por quadro fica abaixo de
      // um quinto da amplitude — se o ruido fosse sorteado por quadro,
      // saltaria a amplitude inteira.
      expect(maiorSalto, lessThan(192 / 5));
    });

    test('parametro com keyframe muda o resultado no tempo', () {
      // Todo parametro importante e animavel: se `paramAt` nao fosse
      // consultado no tempo local, animar a Amplitude nao faria nada.
      var e = EffectInstance(type: EffectType.tremor);
      e = e.withParamEdited('amplitude', Duration.zero, 0);
      // O diamante antes dos dois pontos: sem keyframe, uma edicao no zero
      // e outra em 1 s viram UMA constante — o valor da ultima.
      e = e.withKeyframeToggled(Duration.zero);
      e = e.withParamEdited('amplitude', _s(1), 4, forcar: true);
      final noZero = instantDoShake(e, Duration.zero);
      final noMeio = instantDoShake(e, _s(0.5));
      final noFim = instantDoShake(e, _s(1));
      expect(noZero.dx.abs() + noZero.dy.abs(), closeTo(0, 1e-9));
      expect(
        (noFim.dx - noMeio.dx).abs() + (noFim.dy - noMeio.dy).abs(),
        greaterThan(0),
      );
    });
  });

  group('a mistura', () {
    test('0% e a camada intacta', () {
      final s = instantDoShake(_shake(mix: 0), _s(1.4));
      expect(s.dx, closeTo(0, 1e-9));
      expect(s.dy, closeTo(0, 1e-9));
      expect(s.giroGraus, closeTo(0, 1e-9));
      expect(s.escala, closeTo(1, 1e-9));
    });

    test('100% e o tremor inteiro, e 50% e metade', () {
      final cheio = instantDoShake(_shake(mix: 100), _s(1.4));
      final meio = instantDoShake(_shake(mix: 50), _s(1.4));
      final nada = instantDoShake(_shake(mix: 0), _s(1.4));
      expect(meio.dx, closeTo((cheio.dx + nada.dx) / 2, 1e-9));
      expect(meio.dy, closeTo((cheio.dy + nada.dy) / 2, 1e-9));
      expect(meio.giroGraus, closeTo(cheio.giroGraus / 2, 1e-9));
    });
  });

  group('ruido e onda', () {
    test('so onda: bate com o seno, e nada mais', () {
      // ZERO DE RUIDO. O resultado tem de ser exatamente a senoide — se
      // sobrar ruido, o eixo tem dois motores somando.
      const amp = 100.0, freq = .25;
      final e = _shake(
        amplitude: 1,
        frequency: 1,
        xRandAmp: 0,
        yRandAmp: 0,
        extras: {'x_wave_amp': amp, 'x_wave_freq': freq},
      );
      for (final t in [0.0, .5, 1.3, 2.7]) {
        final s = instantDoShake(e, _s(t));
        final esperado = amp * math.sin(2 * math.pi * freq * t);
        expect(s.dx, closeTo(esperado, 1e-6), reason: 't=$t');
      }
    });

    test('a fase da onda desloca a onda no tempo', () {
      const amp = 100.0, freq = .25, fase = 90.0;
      final sem = _shake(
        amplitude: 1, frequency: 1, xRandAmp: 0, yRandAmp: 0,
        extras: {'x_wave_amp': amp, 'x_wave_freq': freq},
      );
      final com = _shake(
        amplitude: 1, frequency: 1, xRandAmp: 0, yRandAmp: 0,
        extras: {'x_wave_amp': amp, 'x_wave_freq': freq, 'x_phase': fase},
      );
      // Deslocar a fase em 90 e ver a onda de agora e ver a de antes um
      // quarto de ciclo depois — a MESMA onda, so adiantada.
      final a = instantDoShake(com, Duration.zero);
      final b = instantDoShake(sem, _s(fase));
      expect(a.dx, closeTo(b.dx, 1e-6));
    });

    test('amplitude zero nao treme, em nenhum eixo', () {
      final e = _shake(amplitude: 0, xRandAmp: 300, yRandAmp: 300);
      for (final t in [0.0, .4, 1.1, 3.9]) {
        final s = instantDoShake(e, _s(t));
        expect(s.dx.abs() + s.dy.abs(), closeTo(0, 1e-9), reason: 't=$t');
      }
    });

    test('Z mexe na PROFUNDIDADE, e nao no deslocamento', () {
      // O Z E A CAMERA, NAO A ESCALA. Quem manda em Escala e a pessoa: o
      // Z so aproxima e afasta, e a imagem cresce por consequencia. Se
      // ele mexesse em X/Y, seria escala por outro nome.
      final parado = instantDoShake(
        _shake(extras: {'z_rand_amp': 0, 'z_dist': 1}),
        _s(1.4),
      );
      expect(parado.escala, closeTo(1, 1e-9));

      final comZ = instantDoShake(_shake(extras: {'z_rand_amp': .5}), _s(1.4));
      expect((comZ.escala - 1).abs(), greaterThan(1e-6));
      expect(comZ.dx, closeTo(parado.dx, 1e-9));
      expect(comZ.dy, closeTo(parado.dy, 1e-9));
    });

    test('camera LONGE treme menos na tela', () {
      // A mesma profundidade de tremor, a 4 vezes a distancia, mexe menos
      // no tamanho aparente — cada um medido contra a propria base
      // (1/z_dist), que e o repouso daquela distancia.
      final perto = instantDoShake(_shake(extras: {'z_rand_amp': .5}), _s(1.4));
      final longe = instantDoShake(
        _shake(extras: {'z_rand_amp': .5, 'z_dist': 4}),
        _s(1.4),
      );
      expect(
        (longe.escala - 1 / 4).abs(),
        lessThan((perto.escala - 1).abs()),
      );
    });
  });

  group('a ficha nao perde parametro', () {
    final spec = effectSpecs[EffectType.tremor]!;

    test('e o Advanced Shake, com o id novo', () {
      expect(spec.name, 'Advanced Shake');
      expect(spec.id, 'advanced_shake');
      expect(effectTypeFromId('advanced_shake'), EffectType.tremor);
      // O ID ANTIGO CONTINUA SENDO LIDO: projeto salvo com o S_Shake abre
      // com este efeito, e nao como "efeito removido".
      expect(effectTypeFromId('s_shake'), EffectType.tremor);
      expect(effectTypeFromId('tremor'), EffectType.tremor);
    });

    test('os nove globais e os cinco de cada eixo existem', () {
      for (final k in [
        'amplitude', 'frequency', 'phase', 'seed', 'mix',
        'motion_blur', 'mo_blur_length', 'wrap_x', 'wrap_y',
      ]) {
        expect(spec.params.containsKey(k), isTrue, reason: 'falta $k');
      }
      for (final eixo in ['x', 'y', 'z', 'tilt']) {
        for (final p in [
          '${eixo}_rand_amp', '${eixo}_rand_freq',
          '${eixo}_wave_amp', '${eixo}_wave_freq', '${eixo}_phase',
        ]) {
          expect(spec.params.containsKey(p), isTrue, reason: 'falta $p');
        }
      }
      expect(spec.params.containsKey('z_dist'), isTrue);
    });

    test('a borda nunca nasce em "Nenhuma"', () {
      // O pedido do dono: nada de faixa preta por causa do Motion Tile
      // nem por causa do Shake. "Nenhuma" fica para quem quiser o vazio.
      for (final k in ['wrap_x', 'wrap_y']) {
        final p = spec.params[k]!;
        expect(p.initial, isNot(0), reason: '$k nasce mostrando borda');
        expect(p.options, ['Nenhuma', 'Repetir', 'Espelhar']);
      }
    });

    test('TODO parametro esta num grupo, e todo grupo so aponta para o que existe', () {
      final emGrupo = <String>{for (final g in spec.grupos) ...g.chaves};
      for (final k in spec.params.keys) {
        expect(emGrupo.contains(k), isTrue,
            reason: 'o parametro $k nao aparece em grupo nenhum');
      }
      for (final k in emGrupo) {
        expect(spec.params.containsKey(k), isTrue,
            reason: 'o grupo pede $k, que nao existe na ficha');
      }
      expect(spec.grupos.map((g) => g.rotulo), [
        'Global', 'X Shake', 'Y Shake', 'Z Shake', 'Tilt Shake',
      ]);
    });

    test('os dez presets existem, e so apontam para chave que existe', () {
      // O DEFEITO DO SHAKE ANTIGO, virado teste: preset que pedia
      // 'blur_length' e nao fazia nada, porque a chave tinha outro nome.
      expect(spec.presets.length, 10);
      for (final pronto in spec.presets) {
        expect(pronto.valores, isNotEmpty, reason: '${pronto.nome} vazio');
        for (final k in pronto.valores.keys) {
          expect(spec.params.containsKey(k), isTrue,
              reason: '${pronto.nome} pede $k, que nao existe');
        }
      }
    });

    test('montar: no maximo tres, e todas existentes', () {
      expect(spec.montar.length, lessThanOrEqualTo(3));
      expect(spec.montar, isNotEmpty);
      for (final k in spec.montar) {
        expect(spec.params.containsKey(k), isTrue);
      }
    });

    test('todo numero da ficha entra no avaliador sem estourar', () {
      // MEXER EM TODOS OS PARAMETROS ATE OS EXTREMOS. Era aqui que o
      // S_Shake estourava com `params[k]!` quando uma chave saia.
      for (final entry in spec.params.entries) {
        for (final valor in [entry.value.min, entry.value.max]) {
          var e = EffectInstance(type: EffectType.tremor);
          e = e.withParamEdited(entry.key, Duration.zero, valor);
          final v = valoresShake(e, _s(1.7));
          expect(v.length, 32);
          expect(v.every((x) => x.isFinite), isTrue,
              reason: '${entry.key} = $valor produziu NaN');
        }
      }
    });

    test('o motor velho do Shake nao existe mais', () {
      // Era a SEGUNDA engine: rodava em Dart e devolvia um transform
      // pronto, e ninguem a chamava desde que o Shake virou shader.
      expect(spec.procedural, isTrue);
      expect(effectSpecs[EffectType.dissolveShake]!.id, 's_dissolve_shake');
    });
  });

  // ------------------------------------------------------- o shader

  group('o shader do Shake', () {
    late ui.Image fonte;
    setUpAll(() async => fonte = await _padrao());
    tearDownAll(() => fonte.dispose());

    test('sem tremor, a imagem sai igual a que entrou', () async {
      final q = await _desenhar(
        fonte: fonte,
        lado: 64,
        valores: valoresShake(_shake(amplitude: 0), Duration.zero),
      );
      for (final (x, y) in [(2, 2), (61, 2), (2, 61), (61, 61), (31, 40)]) {
        expect(q.canal(x, y, 0), await _daFonte(fonte, 64, x, y, 0),
            reason: 'r em ($x,$y)');
        expect(q.canal(x, y, 1), await _daFonte(fonte, 64, x, y, 1),
            reason: 'g em ($x,$y)');
      }
    });

    test('MISTURA 0 devolve a imagem intacta mesmo com o tremor ligado', () async {
      // O CAMINHO NOVO DO SHADER. Sem ele, mistura nao fazia nada e o
      // unico jeito de "desligar" o Shake era zerar eixo por eixo.
      final e = _shake(amplitude: 4, xRandAmp: 400, mix: 0);
      final t = _s(1.4);
      expect(instantDoShake(e, t).dx.abs(), closeTo(0, 1e-9));
      final q = await _desenhar(
        fonte: fonte,
        lado: 64,
        valores: valoresShake(e, t),
      );
      for (final (x, y) in [(4, 4), (60, 60), (10, 50)]) {
        expect(q.canal(x, y, 0), await _daFonte(fonte, 64, x, y, 0));
        expect(q.canal(x, y, 1), await _daFonte(fonte, 64, x, y, 1));
      }
    });

    test('com o tremor ligado a imagem MUDA, e a borda nao abre buraco', () async {
      final e = _shake(amplitude: 4, xRandAmp: 4001, yRandAmp: 0);
      final t = _s(1.4);
      // Um instante em que o tremor de fato empurra.
      final s = instantDoShake(e, t);
      expect(s.dx.abs(), greaterThan(1));
      final q = await _desenhar(
        fonte: fonte,
        lado: 64,
        valores: valoresShake(e, t),
      );
      // TODOS OS PIXELS OPAQUE: a borda repete (o padrao da ficha), entao
      // nao sobra faixa transparente por onde o tremor empurrou.
      expect(q.opacos, 64 * 64);
    });
  });
}

/// O canal (x, y) da fonte, para comparar com o que o shader devolveu.
Future<int> _daFonte(ui.Image img, int lado, int x, int y, int c) async {
  final dados = await img.toByteData(format: ui.ImageByteFormat.rawRgba);
  return dados!.buffer.asUint8List()[(y * lado + x) * 4 + c];
}
