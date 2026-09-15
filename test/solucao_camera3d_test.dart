// A FICHA DA SOLUCAO e o chao — o modelo de dados do rastreio, provado
// sem motor nenhum: a solucao aqui e construida na mao, com a camera
// conhecida, porque a ficha (qualidade, estrelas, gravacao) e o chao sao
// operacoes SOBRE a solucao e valem para qualquer motor que a produza.
import 'dart:convert';
import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/camera_solver3d.dart';
import 'package:aurea/src/features/editor/domain/plano_do_rastreio.dart';
import 'package:flutter_test/flutter_test.dart';

/// Uma solucao completa e coerente: camera lateral com leve arco, nuvem
/// num volume, e a ficha (erros, vistas) preenchida como um motor real
/// preencheria — com o erro medio sendo o RMS ponderado pelas vistas,
/// que e a conta que `semPontos` refaz ao apagar pontos.
SolucaoCamera3D _sintetica({int quadros = 12, int pontos = 60}) {
  final rng = math.Random(7);
  final nuvem = <int, List<double>>{
    for (var i = 0; i < pontos; i++)
      i: [
        (rng.nextDouble() - .5) * 900,
        (rng.nextDouble() - .5) * 500,
        900 + rng.nextDouble() * 900,
      ],
  };
  final poses = <PoseCamera>[];
  for (var q = 0; q < quadros; q++) {
    final u = q / (quadros - 1);
    final pos = [-420 + 840 * u, 40 * math.sin(u * math.pi), -60 * u];
    final r = rotacaoDeVetor([0.03 * math.sin(u * 3), -0.22 * (u - .5), 0.1 * u]);
    final rt = r.aplicar(pos);
    poses.add(PoseCamera(q, r, [-rt[0], -rt[1], -rt[2]]));
  }
  final erros = <int, double>{};
  final vistas = <int, int>{};
  for (var i = 0; i < pontos; i++) {
    // Um terco excelente, um terco mediano, um terco ruim de verdade.
    erros[i] = switch (i % 3) {
      0 => 0.3 + rng.nextDouble() * 0.4,
      1 => 1.2 + rng.nextDouble() * 0.6,
      _ => 3.5 + rng.nextDouble() * 3,
    };
    vistas[i] = 4 + rng.nextInt(quadros - 3);
  }
  var soma = 0.0;
  var n = 0;
  for (final e in erros.entries) {
    soma += e.value * e.value * vistas[e.key]!;
    n += vistas[e.key]!;
  }
  return SolucaoCamera3D(
    largura: 640,
    altura: 360,
    focalPx: 760,
    poses: poses,
    nuvem: nuvem,
    erroPixels: math.sqrt(soma / n),
    quadros: quadros,
    fps: 8,
    errosPorPonto: erros,
    vistasPorPonto: vistas,
    pontosSeguidos: pontos + 15,
  );
}

void main() {
  group('a ficha do solve', () {
    final s = _sintetica();

    test('cada ponto tem erro, permanencia e qualidade', () {
      expect(s.errosPorPonto.length, s.nuvem.length);
      expect(s.vistasPorPonto.length, s.nuvem.length);
      expect(s.pontosSeguidos, greaterThanOrEqualTo(s.nuvem.length));
      for (final id in s.nuvem.keys) {
        expect(s.errosPorPonto[id], isNotNull);
        expect(s.qualidadeDoPonto(id), isNotNull);
      }
      expect(s.pontosBons, greaterThan(0));
    });

    test('as estrelas nao passam do que a quantidade de pontos sustenta', () {
      // Erro baixissimo com poucos pontos nao pode dar cinco estrelas: a
      // cena e fragil, e uma ficha otimista faria a pessoa confiar nela.
      final poucos = s.copiarCom(
        nuvem: {for (final e in s.nuvem.entries.take(10)) e.key: e.value},
        erroPixels: 0.1,
      );
      expect(poucos.estrelas, lessThanOrEqualTo(2));
    });

    test('um ponto visto em poucos quadros nao passa de fraco', () {
      final id = s.nuvem.keys.first;
      final apertado = s.copiarCom(
        errosPorPonto: {...s.errosPorPonto, id: 0.2},
        vistasPorPonto: {...s.vistasPorPonto, id: 2},
      );
      expect(apertado.qualidadeDoPonto(id), QualidadeDoPonto.fraco);
    });

    test('apagar os ruins melhora o erro medio', () {
      final ruins = s.pontosDaQualidade({
        QualidadeDoPonto.ruim,
        QualidadeDoPonto.fraco,
      });
      final limpa = semPontos(s, ruins.toSet());
      expect(limpa.nuvem.length, s.nuvem.length - ruins.length);
      expect(ruins, isNotEmpty, reason: 'a sintetica tem pontos ruins');
      expect(limpa.erroPixels, lessThan(s.erroPixels));
      // A camera nao se mexe: apagar pontos e uma operacao reversivel.
      expect(limpa.poses.length, s.poses.length);
    });

    test('a solucao atravessa a gravacao com a ficha inteira', () {
      final voltou = SolucaoCamera3D.decode(jsonEncode(s.toJson()))!;
      expect(voltou.nuvem.length, s.nuvem.length);
      expect(voltou.errosPorPonto.length, s.errosPorPonto.length);
      expect(voltou.pontosSeguidos, s.pontosSeguidos);
      expect(voltou.estrelas, s.estrelas);
    });
  });

  test('definirChao poe os pontos escolhidos no y = 0', () {
    final s = _sintetica();
    // Cinco pontos quaisquer da nuvem viram o chao. Eles nao sao
    // coplanares de verdade (a nuvem e um volume), entao o que se pode
    // exigir e o que a conta promete: o plano MEDIO deles vira y = 0, e
    // a direcao y passa a ser a de menor espalhamento — que e o que faz
    // um texto deitado ficar deitado.
    final ids = s.nuvem.keys.take(5).toList();
    final chao = definirChao(s, ids);
    final ys = [for (final id in ids) chao.nuvem[id]![1]];
    expect(
      ys.reduce((a, b) => a + b) / ys.length,
      closeTo(0, 1e-9),
      reason: 'o centro dos pontos escolhidos vira a origem',
    );
    double espalhamento(int eixo) {
      final v = [for (final id in ids) chao.nuvem[id]![eixo]];
      final m = v.reduce((a, b) => a + b) / v.length;
      return math.sqrt(
        v.map((x) => (x - m) * (x - m)).reduce((a, b) => a + b) / v.length,
      );
    }

    expect(espalhamento(1), lessThan(espalhamento(0)));
    expect(espalhamento(1), lessThan(espalhamento(2)));
    // A camera continua vendo a mesma coisa: o erro nao muda.
    expect(chao.erroPixels, closeTo(s.erroPixels, 1e-9));
  });
}
