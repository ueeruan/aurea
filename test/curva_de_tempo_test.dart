// CURVA DE TEMPO (domain/curva_de_tempo.dart): os pontos que o dedo move
// e a trilha que o nucleo toca tem de dizer a mesma coisa, quadro a quadro.
import 'package:aurea/src/features/editor/domain/curva_de_tempo.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/time_core.dart';
import 'package:flutter_test/flutter_test.dart';

Duration _ms(int ms) => Duration(milliseconds: ms);

PontoDeTempo _p(int ms, double v, [ModoDoPonto modo = ModoDoPonto.linear]) =>
    PontoDeTempo(t: _ms(ms), v: v, modo: modo);

double _valor(AnimatedDouble curva, Duration t) =>
    coreValue(curva, t, linear: true);

/// Todos os quadros (30 fps) de 0 a [fim].
Iterable<Duration> _quadros(Duration fim) sync* {
  for (var q = 0; q * 1000000 / 30 <= fim.inMicroseconds; q++) {
    yield Duration(microseconds: (q * 1000000 / 30).round());
  }
}

void main() {
  group('curvaDosPontos', () {
    test('linear e identidade', () {
      final curva = curvaDosPontos([_p(0, 0), _p(1500, 1.5), _p(4000, 4)]);
      for (final k in curva.keyframes) {
        expect(k.ease.isLinear, isTrue);
      }
      for (var ms = 0; ms <= 4000; ms += 50) {
        expect(_valor(curva, _ms(ms)), closeTo(ms / 1000, 1e-9));
      }
    });

    test('segurar vira hold e para o quadro ate o proximo ponto', () {
      final curva = curvaDosPontos([
        _p(0, 0),
        _p(1000, 1, ModoDoPonto.segurar),
        _p(3000, 1),
        _p(4000, 2),
      ]);
      expect(curva.keyframes[1].ease.type, EasingType.hold);
      expect(_valor(curva, _ms(1500)), 1);
      expect(_valor(curva, _ms(2999)), 1);
      expect(_valor(curva, _ms(3500)), closeTo(1.5, 1e-9));
    });

    test('PCHIP e monotono e nao passa dos pontos', () {
      final pontos = [
        _p(0, 0, ModoDoPonto.suave),
        _p(1000, 0.2, ModoDoPonto.suave),
        _p(2000, 1.8, ModoDoPonto.suave),
        _p(3000, 2.0, ModoDoPonto.suave),
        _p(4000, 4.0, ModoDoPonto.suave),
      ];
      final curva = curvaDosPontos(pontos);
      var anterior = double.negativeInfinity;
      for (var ms = 0; ms <= 4000; ms += 5) {
        final v = _valor(curva, _ms(ms));
        expect(v, greaterThanOrEqualTo(anterior - 1e-9), reason: '$ms ms');
        anterior = v;
        final i = (ms ~/ 1000).clamp(0, 3);
        expect(v, greaterThanOrEqualTo(pontos[i].v - 1e-9));
        expect(v, lessThanOrEqualTo(pontos[i + 1].v + 1e-9));
      }
      // No pico a tangente e zero: a curva nao sobe acima dele.
      final pico = curvaDosPontos([
        _p(0, 0, ModoDoPonto.suave),
        _p(2000, 2, ModoDoPonto.suave),
        _p(4000, 1, ModoDoPonto.suave),
      ]);
      for (var ms = 0; ms <= 4000; ms += 5) {
        expect(_valor(pico, _ms(ms)), lessThanOrEqualTo(2 + 1e-9));
        expect(_valor(pico, _ms(ms)), greaterThanOrEqualTo(-1e-9));
      }
    });
  });

  group('pontosDaCurva', () {
    test('ida e volta devolve os mesmos pontos e os mesmos quadros', () {
      final pontos = [
        _p(0, 0, ModoDoPonto.suave),
        _p(1000, 1.5, ModoDoPonto.suave),
        _p(2000, 2.0, ModoDoPonto.linear),
        _p(2500, 2.6, ModoDoPonto.segurar),
        PontoDeTempo(
          t: _ms(3000),
          v: 2.6,
          modo: ModoDoPonto.livre,
          easeLivre: Easing.easeIn,
        ),
        _p(4000, 4.0, ModoDoPonto.linear),
      ];
      final curva = curvaDosPontos(pontos);
      final lidos = pontosDaCurva(curva);
      expect(lidos, pontos);
      final refeita = curvaDosPontos(lidos);
      for (final t in _quadros(_ms(4000))) {
        expect(_valor(refeita, t), closeTo(_valor(curva, t), 1e-9));
      }
    });

    test('preset antigo e bezier a mao voltam intactos como livre', () {
      final antiga = AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0, Easing.easeInOut)
          .withKeyframe(_ms(1000), 0.6, Easing.easeInOut)
          .withKeyframe(
            _ms(2500),
            2.2,
            const Easing(x1: 1 / 3, y1: 0.9, x2: 2 / 3, y2: 0.2),
          )
          .withKeyframe(_ms(4000), 4);
      final lidos = pontosDaCurva(antiga);
      expect(lidos[0].modo, ModoDoPonto.livre);
      expect(lidos[1].modo, ModoDoPonto.livre);
      expect(lidos[2].modo, ModoDoPonto.livre);
      final refeita = curvaDosPontos(lidos);
      for (final t in _quadros(_ms(4000))) {
        expect(_valor(refeita, t), closeTo(_valor(antiga, t), 1e-9));
      }
    });

    test('reta de dois pontos le linear', () {
      final reta = AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(_ms(4000), 8);
      final lidos = pontosDaCurva(reta);
      expect(lidos.map((p) => p.modo), everyElement(ModoDoPonto.linear));
      expect(lidos.map((p) => p.v), [0, 8]);
    });
  });

  group('inserirNaCurva', () {
    test('num trecho linear nao muda nenhum quadro', () {
      final pontos = [_p(0, 0), _p(1000, 2), _p(4000, 3)];
      final antes = curvaDosPontos(pontos);
      final r = inserirNaCurva(pontos, _ms(2500));
      expect(r.criou, isTrue);
      expect(r.indice, 2);
      expect(r.pontos, hasLength(4));
      expect(r.pontos[2].modo, ModoDoPonto.linear);
      expect(r.pontos[2].v, closeTo(2.5, 1e-12));
      final depois = curvaDosPontos(r.pontos);
      for (final t in _quadros(_ms(4000))) {
        expect(_valor(depois, t), closeTo(_valor(antes, t), 1e-9));
      }
    });

    test('num trecho segurado continua segurando', () {
      final pontos = [
        _p(0, 0),
        _p(1000, 1, ModoDoPonto.segurar),
        _p(3000, 2.5),
        _p(4000, 3.5),
      ];
      final antes = curvaDosPontos(pontos);
      final r = inserirNaCurva(pontos, _ms(2000));
      expect(r.pontos[2].modo, ModoDoPonto.segurar);
      final depois = curvaDosPontos(r.pontos);
      for (final t in _quadros(_ms(4000))) {
        expect(_valor(depois, t), closeTo(_valor(antes, t), 1e-9));
      }
    });

    test('num trecho livre de bezier divide sem mudar o desenho', () {
      final pontos = [
        const PontoDeTempo(
          t: Duration.zero,
          v: 0,
          modo: ModoDoPonto.livre,
          easeLivre: Easing.easeInOut,
        ),
        _p(4000, 4),
      ];
      final antes = curvaDosPontos(pontos);
      final r = inserirNaCurva(pontos, _ms(1300));
      expect(r.criou, isTrue);
      final depois = curvaDosPontos(r.pontos);
      for (final t in _quadros(_ms(4000))) {
        expect(_valor(depois, t), closeTo(_valor(antes, t), 1e-6));
      }
    });

    test('perto de um ponto devolve o indice dele, sem criar', () {
      final pontos = [_p(0, 0), _p(2000, 2), _p(4000, 4)];
      final r = inserirNaCurva(pontos, _ms(2010));
      expect(r.criou, isFalse);
      expect(r.indice, 1);
      expect(identical(r.pontos, pontos), isTrue);
    });

    test('ao lado de um ponto suave nasce suave', () {
      final pontos = [_p(0, 0), _p(2000, 1, ModoDoPonto.suave), _p(4000, 4)];
      final r = inserirNaCurva(pontos, _ms(1000));
      expect(r.pontos[1].modo, ModoDoPonto.suave);
    });
  });

  group('moverPonto', () {
    final pontos = [_p(0, 0), _p(1000, 1), _p(2000, 2), _p(4000, 4)];
    const duracao = Duration(seconds: 4);

    test('as pontas so sobem e descem', () {
      final primeiro = moverPonto(
        pontos,
        0,
        t: _ms(500),
        v: -3,
        duracao: duracao,
        vMin: -1,
        vMax: 6,
      );
      expect(primeiro[0].t, Duration.zero);
      expect(primeiro[0].v, -1);
      final ultimo = moverPonto(
        pontos,
        3,
        t: _ms(3000),
        v: 9,
        duracao: duracao,
        vMin: -1,
        vMax: 6,
      );
      expect(ultimo[3].t, duracao);
      expect(ultimo[3].v, 6);
      expect(pontos[3].v, 4, reason: 'a lista original nao muda');
    });

    test('os do meio ficam entre os vizinhos com folga', () {
      final direita = moverPonto(
        pontos,
        1,
        t: _ms(3000),
        v: 1.2,
        duracao: duracao,
        vMin: 0,
        vMax: 8,
      );
      expect(direita[1].t, _ms(2000) - kFolgaEntrePontos);
      expect(direita[1].v, 1.2);
      final esquerda = moverPonto(
        pontos,
        1,
        t: _ms(-1000),
        duracao: duracao,
        vMin: 0,
        vMax: 8,
      );
      expect(esquerda[1].t, kFolgaEntrePontos);
      final livre = moverPonto(
        pontos,
        2,
        t: _ms(2500),
        v: 2.5,
        duracao: duracao,
        vMin: 0,
        vMax: 8,
      );
      expect(livre[2].t, _ms(2500));
      expect(livre[2].v, 2.5);
    });
  });

  test('ima encaixa no congelar e na velocidade normal', () {
    final pontos = [_p(0, 0), _p(1000, 1), _p(3000, 2)];
    final congelar = imaDoValor(
      pontos,
      1,
      t: _ms(1000),
      v: 0.05,
      tolerancia: 0.1,
    );
    expect(congelar.valor, 0);
    expect(congelar.ima, ImaDoValor.valorDoAnterior);
    final proximo = imaDoValor(
      pontos,
      1,
      t: _ms(1000),
      v: 1.97,
      tolerancia: 0.1,
    );
    expect(proximo.valor, 2);
    expect(proximo.ima, ImaDoValor.valorDoProximo);
    final normal = imaDoValor(
      pontos,
      1,
      t: _ms(1000),
      v: 1.04,
      tolerancia: 0.1,
    );
    expect(normal.valor, closeTo(1, 1e-12));
    expect(normal.ima, ImaDoValor.normalDesdeOAnterior);
    final solto = imaDoValor(pontos, 1, t: _ms(1000), v: 0.5, tolerancia: 0.1);
    expect(solto.valor, 0.5);
    expect(solto.ima, isNull);
  });

  group('prontos', () {
    const duracao = Duration(seconds: 4);

    void confereLimites(List<PontoDeTempo> pontos, double? vMax) {
      expect(pontos.first.t, Duration.zero);
      expect(pontos.last.t, duracao);
      for (var i = 0; i + 1 < pontos.length; i++) {
        expect(pontos[i + 1].t, greaterThan(pontos[i].t));
      }
      final curva = curvaDosPontos(pontos);
      for (final t in _quadros(duracao)) {
        final v = _valor(curva, t);
        expect(v, greaterThanOrEqualTo(-1e-9));
        if (vMax != null) expect(v, lessThanOrEqualTo(vMax + 1e-9));
      }
    }

    test('ficam dentro da fonte', () {
      for (final vMax in <double?>[null, 10, 2]) {
        confereLimites(curvaReta(duracao, vMax: vMax), vMax);
        confereLimites(curvaCameraLentaNoMeio(duracao, vMax: vMax), vMax);
        confereLimites(curvaAcelerando(duracao, vMax: vMax), vMax);
        confereLimites(curvaDesacelerando(duracao, vMax: vMax), vMax);
      }
    });

    test('reta em 1x segura o ultimo quadro quando a fonte acaba', () {
      final curta = curvaReta(duracao, vMax: 2);
      expect(curta.map((p) => p.v), [0, 2, 2]);
      expect(velocidadeDoTrecho(curta, 0), closeTo(1, 1e-9));
      expect(velocidadeDoTrecho(curta, 1), 0);
      expect(curvaReta(duracao).map((p) => p.v), [0, 4]);
    });

    test('camera lenta, acelerando e desacelerando tem a velocidade certa', () {
      final lenta = curvaCameraLentaNoMeio(duracao);
      expect(velocidadeDoTrecho(lenta, 0), closeTo(1, 1e-5));
      expect(velocidadeDoTrecho(lenta, 1), closeTo(0.3, 1e-5));
      expect(velocidadeDoTrecho(lenta, 2), closeTo(1, 1e-5));
      final acelera = curvaAcelerando(duracao);
      expect(acelera.last.v, closeTo(4, 1e-9));
      expect(velocidadeDoTrecho(acelera, 0), closeTo(0.5, 1e-5));
      expect(velocidadeDoTrecho(acelera, 2), closeTo(1.5, 1e-5));
      final freia = curvaDesacelerando(duracao);
      expect(freia.last.v, closeTo(4, 1e-9));
      expect(velocidadeDoTrecho(freia, 0), closeTo(1.5, 1e-5));
      expect(velocidadeDoTrecho(freia, 2), closeTo(0.5, 1e-5));
      // Pontos internos suaves: a leitura da trilha devolve os mesmos modos.
      expect(pontosDaCurva(curvaDosPontos(lenta)), lenta);
    });

    test('inverter corre o mesmo trecho de tras para frente', () {
      final pontos = [_p(0, 0), _p(1000, 3, ModoDoPonto.suave), _p(4000, 4)];
      final invertida = inverterCurva(pontos);
      expect(invertida.map((p) => p.v), [4, 1, 0]);
      expect(invertida.map((p) => p.t), pontos.map((p) => p.t));
      expect(invertida.map((p) => p.modo), pontos.map((p) => p.modo));
      expect(inverterCurva(invertida), pontos);
      final a = curvaDosPontos(pontos);
      final b = curvaDosPontos(invertida);
      for (final t in _quadros(duracao)) {
        expect(_valor(b, t), closeTo(4 - _valor(a, t), 1e-9));
      }
      expect(velocidadeDoTrecho(invertida, 0), closeTo(-3, 1e-9));
    });
  });

  test('ajustar as pontas nao muda os quadros dentro do clipe', () {
    final trilha = AnimatedDouble(0)
        .withKeyframe(_ms(1000), 1)
        .withKeyframe(_ms(3000), 3);
    final pontos = ajustarPontasAoClipe(pontosDaCurva(trilha), _ms(4000));
    expect(pontos.map((p) => p.t), [
      Duration.zero,
      _ms(1000),
      _ms(3000),
      _ms(4000),
    ]);
    expect(pontos.first.v, closeTo(0, 1e-9));
    expect(pontos.last.v, closeTo(4, 1e-9));
    final curva = curvaDosPontos(pontos);
    for (final t in _quadros(_ms(4000))) {
      expect(_valor(curva, t), closeTo(_valor(trilha, t), 1e-9));
    }
  });

  test('limites da fonte sao relativos ao inicio do clipe', () {
    final conhecidos = limitesDaFonte(
      sourceOffset: _ms(2000),
      sourceDuration: _ms(10000),
      duracao: _ms(4000),
    );
    expect(conhecidos.vMin, -2);
    expect(conhecidos.vMax, 8);
    final semFonte = limitesDaFonte(
      sourceOffset: Duration.zero,
      duracao: _ms(4000),
      pontos: [_p(0, 0), _p(4000, 3)],
    );
    expect(semFonte.vMax, 6);
  });
}
