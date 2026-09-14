// O ESTUDIO DO TEMPO por baixo: o vocabulario do AE (velocidade e
// influencia por lado) sobre as alcas bezier de sempre, avaliado pelo
// nucleo C++ — a curva desenhada, a derivada e o quadro saem da mesma conta.
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/remapear_tempo.dart';
import 'package:flutter_test/flutter_test.dart';

AnimatedDouble _rampa() => AnimatedDouble(0)
    .withKeyframe(Duration.zero, 0)
    .withKeyframe(const Duration(seconds: 2), 4)
    .withKeyframe(const Duration(seconds: 4), 6);

void main() {
  test('velocidade e influencia fazem ida e volta EXATA', () {
    var t = _rampa();
    // Trecho 0: Δt = 2 s, Δs = 4 s (secante 2x).
    t = trilhaComSuavidade(
      t,
      0,
      saida: const SuavidadeTemporal(velocidade: 6, influencia: .5),
    );
    t = trilhaComSuavidade(
      t,
      1,
      entrada: const SuavidadeTemporal(velocidade: .5, influencia: .25),
      saida: const SuavidadeTemporal(velocidade: -2, influencia: .4),
    );
    final p = pontosDaTrilha(t);
    expect(p[0].saida!.velocidade, closeTo(6, 1e-9));
    expect(p[0].saida!.influencia, closeTo(.5, 1e-9));
    expect(p[1].entrada!.velocidade, closeTo(.5, 1e-9));
    expect(p[1].entrada!.influencia, closeTo(.25, 1e-9));
    expect(p[1].saida!.velocidade, closeTo(-2, 1e-9));
    expect(p[1].saida!.influencia, closeTo(.4, 1e-9));
    expect(p[1].tipo, TipoDoPontoDeTempo.bezier);
    // As pontas sem trecho daquele lado nao tem suavidade.
    expect(p[0].entrada, isNull);
    expect(p[2].saida, isNull);
  });

  test('o grafo de velocidade (derivada do nucleo) confere com as alcas', () {
    var t = _rampa();
    t = trilhaComSuavidade(
      t,
      0,
      saida: const SuavidadeTemporal(velocidade: 6, influencia: .5),
    );
    // Na saida do keyframe a derivada e a velocidade pedida...
    expect(
      velocidadeDaCurva(t, const Duration(microseconds: 1)),
      closeTo(6, 1e-3),
    );
    // ...e o valor continua cravado nos keyframes.
    expect(valorDaCurva(t, Duration.zero), closeTo(0, 1e-9));
    expect(valorDaCurva(t, const Duration(seconds: 2)), closeTo(4, 1e-9));
  });

  test('suavizar (easy ease) zera a velocidade nos dois lados', () {
    final t = trilhaSuavizada(_rampa(), 1);
    final p = pontosDaTrilha(t)[1];
    expect(p.entrada!.velocidade, closeTo(0, 1e-9));
    expect(p.saida!.velocidade, closeTo(0, 1e-9));
    expect(
      velocidadeDaCurva(t, const Duration(seconds: 2)).abs(),
      lessThan(1e-3),
    );
  });

  test('manter congela o trecho de saida; linear endireita os dois lados', () {
    var t = trilhaComTipo(_rampa(), 1, TipoDoPontoDeTempo.manter);
    expect(pontosDaTrilha(t)[1].tipo, TipoDoPontoDeTempo.manter);
    expect(valorDaCurva(t, const Duration(seconds: 3)), closeTo(4, 1e-9));
    expect(velocidadeDaCurva(t, const Duration(seconds: 3)).abs(), 0);
    t = trilhaComTipo(trilhaSuavizada(_rampa(), 1), 1, TipoDoPontoDeTempo.linear);
    final p = pontosDaTrilha(t)[1];
    expect(p.tipo, TipoDoPontoDeTempo.linear);
    expect(p.entrada!.velocidade, closeTo(2, 1e-9), reason: 'secante do trecho');
    expect(p.saida!.velocidade, closeTo(1, 1e-9));
  });

  test('auto bezier alinha a tangente pelos vizinhos; continuo faz a media', () {
    final auto = trilhaAutoBezier(_rampa(), 1);
    final pa = pontosDaTrilha(auto)[1];
    // Catmull-Rom: (6 - 0) / (4 s - 0 s) = 1,5.
    expect(pa.entrada!.velocidade, closeTo(1.5, 1e-9));
    expect(pa.saida!.velocidade, closeTo(1.5, 1e-9));

    var t = trilhaComSuavidade(
      _rampa(),
      1,
      entrada: const SuavidadeTemporal(velocidade: 3, influencia: .3),
      saida: const SuavidadeTemporal(velocidade: 1, influencia: .3),
    );
    t = trilhaContinua(t, 1);
    final pc = pontosDaTrilha(t)[1];
    expect(pc.entrada!.velocidade, closeTo(2, 1e-9));
    expect(pc.saida!.velocidade, closeTo(2, 1e-9));
  });

  test('espelhar inverte a curva inteira sem mudar o desenho', () {
    final t = trilhaSuavizada(_rampa(), 1);
    final e = curvaEspelhada(t, 6);
    for (var ms = 0; ms <= 4000; ms += 130) {
      final d = Duration(milliseconds: ms);
      expect(valorDaCurva(e, d), closeTo(6 - valorDaCurva(t, d), 1e-9));
    }
  });

  test('reverso a partir do meio segura o passado e espelha o futuro', () {
    final t = reversoAPartirDe(_rampa(), const Duration(seconds: 2), 6);
    // Antes do pivo nada muda; no pivo o valor e o mesmo.
    expect(valorDaCurva(t, const Duration(seconds: 1)), closeTo(2, 1e-9));
    expect(valorDaCurva(t, const Duration(seconds: 2)), closeTo(4, 1e-9));
    // Depois, espelhado: o keyframe de 6 vira 2·4 − 6 = 2.
    expect(valorDaCurva(t, const Duration(seconds: 4)), closeTo(2, 1e-9));
    expect(velocidadeDaCurva(t, const Duration(seconds: 3)), lessThan(0));
  });

  test('identidade reproduz normal: valor e derivada', () {
    final t = curvaIdentidade(const Duration(seconds: 3), 3);
    expect(valorDaCurva(t, const Duration(seconds: 3)), closeTo(3, 1e-9));
    expect(
      velocidadeDaCurva(t, const Duration(seconds: 1)),
      closeTo(1, 1e-6),
    );
    expect(pontosDaTrilha(t)[0].tipo, TipoDoPontoDeTempo.linear);
  });

  test('congelado (Δs = 0) le velocidade 0, qualquer que seja a alca', () {
    var t = AnimatedDouble(0)
        .withKeyframe(Duration.zero, 2)
        .withKeyframe(const Duration(seconds: 1), 2)
        .withKeyframe(const Duration(seconds: 2), 4);
    t = trilhaComSuavidade(
      t,
      0,
      saida: const SuavidadeTemporal(velocidade: 5, influencia: .5),
    );
    final p = pontosDaTrilha(t)[0];
    expect(p.saida!.velocidade, closeTo(0, 1e-9));
    expect(valorDaCurva(t, const Duration(milliseconds: 500)), closeTo(2, 1e-9));
  });

  test('a grade gruda no quadro e, com mais forca, nas guias', () {
    final naGuia = ajustarTempoComGrade(
      const Duration(milliseconds: 1013),
      fps: 30,
      guias: const [Duration(seconds: 1)],
    );
    expect(naGuia.tempo, const Duration(seconds: 1));
    expect(naGuia.naGuia, isTrue);
    final noQuadro = ajustarTempoComGrade(
      const Duration(milliseconds: 1013),
      fps: 30,
    );
    expect(noQuadro.naGuia, isFalse);
    expect(noQuadro.tempo, const Duration(milliseconds: 1000));
  });
}
