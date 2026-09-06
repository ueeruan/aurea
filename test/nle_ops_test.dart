import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/nle_ops.dart';

Duration _s(num v) => Duration(milliseconds: (v * 1000).round());

TextLayer _clip(String nome, num inicio, num dur) => TextLayer(
      name: nome,
      startTime: _s(inicio),
      duration: _s(dur),
      text: nome,
    );

/// Devolve (nome, inicio, fim) em segundos, ordenado por inicio.
List<(String, double, double)> _linha(List<Layer> layers) {
  final out = [
    for (final l in layers)
      (
        l.name,
        l.startTime.inMilliseconds / 1000,
        l.endTime.inMilliseconds / 1000
      ),
  ]..sort((a, b) => a.$2.compareTo(b.$2));
  return out;
}

void main() {
  group('Exclusao com arrasto', () {
    test('tira o trecho e puxa o que vinha depois', () {
      final t = [_clip('A', 0, 2), _clip('B', 2, 3), _clip('C', 5, 1)];
      final r = rippleDelete(t, t[1].id);
      expect(_linha(r), [
        ('A', 0.0, 2.0),
        ('C', 2.0, 3.0), // andou 3 s para tras
      ]);
    });

    test('o que vem ANTES nao se mexe', () {
      final t = [_clip('A', 0, 2), _clip('B', 4, 1)];
      final r = rippleDelete(t, t[1].id);
      expect(_linha(r), [('A', 0.0, 2.0)]);
    });

    test('id que nao existe nao muda nada', () {
      final t = [_clip('A', 0, 2)];
      expect(rippleDelete(t, 'nada'), same(t));
    });
  });

  group('Fechar buracos', () {
    test('encosta tudo, sem mudar ordem nem duracao', () {
      final t = [_clip('A', 0, 2), _clip('B', 5, 1), _clip('C', 9, 3)];
      final r = closeGaps(t);
      expect(_linha(r), [
        ('A', 0.0, 2.0),
        ('B', 2.0, 3.0),
        ('C', 3.0, 6.0),
      ]);
    });

    test('depois de fechar nao sobra buraco nenhum', () {
      final t = [_clip('A', 1, 2), _clip('B', 6, 1), _clip('C', 10, 2)];
      expect(gapsIn(t).length, 3); // inclui o vazio do inicio
      expect(gapsIn(closeGaps(t)), isEmpty);
    });

    test('fecha so do ponto pedido para frente', () {
      final t = [_clip('A', 0, 2), _clip('B', 6, 1), _clip('C', 10, 1)];
      final r = closeGaps(t, from: _s(6));
      expect(_linha(r), [
        ('A', 0.0, 2.0), // intacta: acaba antes do ponto
        ('B', 6.0, 7.0),
        ('C', 7.0, 8.0),
      ]);
    });

    test('linha ja encostada nao muda', () {
      final t = [_clip('A', 0, 2), _clip('B', 2, 2)];
      expect(_linha(closeGaps(t)), _linha(t));
    });
  });

  group('Inserir', () {
    test('empurra para frente o que comeca dali', () {
      final t = [_clip('A', 0, 2), _clip('B', 2, 2)];
      final r = insertAt(t, _clip('NOVO', 0, 1), _s(2));
      expect(_linha(r.layers), [
        ('A', 0.0, 2.0),
        ('NOVO', 2.0, 3.0),
        ('B', 3.0, 5.0),
      ]);
    });

    test('divide quem estava atravessado no ponto', () {
      final t = [_clip('A', 0, 4)];
      final r = insertAt(t, _clip('NOVO', 0, 1), _s(1));
      expect(_linha(r.layers), [
        ('A', 0.0, 1.0),
        ('NOVO', 1.0, 2.0),
        ('A', 2.0, 5.0), // o resto de A, depois do inserido
      ]);
    });

    test('a linha do tempo ESTICA pelo tamanho do inserido', () {
      final t = [_clip('A', 0, 2), _clip('B', 2, 2)];
      final antes = t.map((l) => l.endTime).reduce((a, b) => a > b ? a : b);
      final r = insertAt(t, _clip('N', 0, 3), _s(1));
      final depois =
          r.layers.map((l) => l.endTime).reduce((a, b) => a > b ? a : b);
      expect(depois - antes, _s(3));
    });

    test('o inserido volta com o inicio ja no lugar', () {
      final r = insertAt([], _clip('N', 99, 1), _s(4));
      expect(r.inserted.startTime, _s(4));
    });
  });

  group('Sobrescrever', () {
    test('apaga o que estava embaixo sem esticar a linha', () {
      final t = [_clip('A', 0, 2), _clip('B', 2, 2), _clip('C', 4, 2)];
      final r = overwriteAt(t, _clip('N', 0, 2), _s(2));
      expect(_linha(r.layers), [
        ('A', 0.0, 2.0),
        ('N', 2.0, 4.0), // B sumiu inteira
        ('C', 4.0, 6.0), // C nao andou
      ]);
    });

    test('sobra a ponta de quem so foi coberta em parte', () {
      final t = [_clip('A', 0, 4)];
      final r = overwriteAt(t, _clip('N', 0, 2), _s(3));
      expect(_linha(r.layers), [
        ('A', 0.0, 3.0),
        ('N', 3.0, 5.0),
      ]);
    });

    test('caindo no meio, sobram as DUAS pontas', () {
      final t = [_clip('A', 0, 6)];
      final r = overwriteAt(t, _clip('N', 0, 2), _s(2));
      expect(_linha(r.layers), [
        ('A', 0.0, 2.0),
        ('N', 2.0, 4.0),
        ('A', 4.0, 6.0),
      ]);
    });

    test('quem esta fora do trecho fica intacto', () {
      final t = [_clip('A', 0, 1), _clip('B', 8, 1)];
      final r = overwriteAt(t, _clip('N', 0, 2), _s(3));
      expect(_linha(r.layers), [
        ('A', 0.0, 1.0),
        ('N', 3.0, 5.0),
        ('B', 8.0, 9.0),
      ]);
    });
  });

  group('Levantar e extrair', () {
    test('levantar tira o trecho e DEIXA o buraco', () {
      final t = [_clip('A', 0, 6)];
      final r = liftRange(t, _s(2), _s(4));
      expect(_linha(r), [
        ('A', 0.0, 2.0),
        ('A', 4.0, 6.0),
      ]);
      expect(gapsIn(r).length, 1);
    });

    test('extrair tira o trecho E fecha o buraco', () {
      final t = [_clip('A', 0, 2), _clip('B', 2, 2), _clip('C', 4, 2)];
      final r = extractRange(t, _s(2), _s(4));
      expect(_linha(r), [
        ('A', 0.0, 2.0),
        ('C', 2.0, 4.0), // andou 2 s para tras
      ]);
    });

    test('faixa invertida ou vazia nao faz nada', () {
      final t = [_clip('A', 0, 4)];
      expect(liftRange(t, _s(3), _s(1)), same(t));
      expect(extractRange(t, _s(2), _s(2)), same(t));
    });

    test('so as camadas escolhidas sao afetadas', () {
      final t = [_clip('A', 0, 6), _clip('B', 0, 6)];
      final r = liftRange(t, _s(2), _s(4), only: {t[0].id});
      final b = r.where((l) => l.name == 'B').toList();
      expect(b.length, 1);
      expect(b.first.duration, _s(6));
    });
  });

  group('Buracos', () {
    test('acha o vazio do inicio e o do meio', () {
      final t = [_clip('A', 1, 1), _clip('B', 5, 1)];
      final g = gapsIn(t);
      expect(g.length, 2);
      expect(g[0].$1, Duration.zero);
      expect(g[0].$2, _s(1));
      expect(g[1].$1, _s(2));
      expect(g[1].$2, _s(5));
    });

    test('camadas sobrepostas nao inventam buraco', () {
      final t = [_clip('A', 0, 5), _clip('B', 2, 5)];
      expect(gapsIn(t), isEmpty);
    });

    test('linha vazia nao tem buraco', () {
      expect(gapsIn(const []), isEmpty);
    });
  });

  group('Ponto de entrada na midia', () {
    AudioLayer fala(num inicio, num dur, {Duration? off}) => AudioLayer(
          name: 'fala',
          startTime: _s(inicio),
          duration: _s(dur),
          sourcePath: 'fala.wav',
          sourceOffset: off ?? Duration.zero,
        );

    // Tirar um pedaco do MEIO de uma locucao: o rabo tem que continuar
    // de onde parou. Sem avancar a fonte, ele repetia o audio que ja
    // tinha tocado — a frase saia gaguejando.
    test('o rabo do corte continua de onde parou', () {
      final r = liftRange([fala(0, 10)], _s(3), _s(5));
      final rabo = r.whereType<AudioLayer>().reduce(
          (a, b) => a.startTime > b.startTime ? a : b);
      expect(rabo.startTime, _s(5));
      expect(rabo.sourceOffset, _s(5));
    });

    test('a cabeca do corte nao mexe na fonte', () {
      final r = liftRange([fala(0, 10)], _s(3), _s(5));
      final cabeca = r.whereType<AudioLayer>().reduce(
          (a, b) => a.startTime < b.startTime ? a : b);
      expect(cabeca.sourceOffset, Duration.zero);
      expect(cabeca.duration, _s(3));
    });

    test('soma ao ponto de entrada que ja existia', () {
      final r = liftRange([fala(0, 10, off: _s(4))], _s(2), _s(6));
      final rabo = r.whereType<AudioLayer>().reduce(
          (a, b) => a.startTime > b.startTime ? a : b);
      expect(rabo.sourceOffset, _s(10));
    });

    test('clipe que so comeca dentro do trecho tambem avanca', () {
      final r = liftRange([fala(2, 8)], _s(0), _s(5));
      final unico = r.whereType<AudioLayer>().single;
      expect(unico.startTime, _s(5));
      expect(unico.sourceOffset, _s(3));
    });

    test('video mantem o mesmo comportamento', () {
      final v = VideoLayer(
        name: 'tomada',
        startTime: Duration.zero,
        duration: _s(10),
        sourcePath: 'a.mp4',
        sourceOffset: _s(1),
      );
      final r = extractRange([v], _s(4), _s(6));
      final rabo = r.whereType<VideoLayer>().reduce(
          (a, b) => a.sourceOffset > b.sourceOffset ? a : b);
      expect(rabo.sourceOffset, _s(7));
      // Extrair fecha o buraco: o rabo encosta na cabeca.
      expect(rabo.startTime, _s(4));
    });

    test('clipe fora do trecho nao e tocado', () {
      final r = liftRange([fala(8, 2, off: _s(3))], _s(0), _s(5));
      expect(r.whereType<AudioLayer>().single.sourceOffset, _s(3));
    });
  });

  group('Decupagem — varios trechos de uma vez', () {
    AudioLayer fala(num inicio, num dur) => AudioLayer(
          name: 'fala',
          startTime: _s(inicio),
          duration: _s(dur),
          sourcePath: 'fala.wav',
        );

    test('tres pausas viram quatro pedacos encostados', () {
      final r = removeRangesFrom(
        [fala(0, 20)],
        'x',
        const [],
      );
      expect(r.length, 1, reason: 'lista vazia nao mexe em nada');

      final falas = [fala(0, 20)];
      final id = falas.first.id;
      final cortado = removeRangesFrom(falas, id, [
        (_s(2), _s(3)),
        (_s(7), _s(9)),
        (_s(14), _s(15)),
      ]);
      expect(cortado.length, 4);
      final linha = _linha(cortado);
      // 20 s menos 4 s de pausa = 16 s, tudo colado.
      expect(linha.first.$2, 0);
      expect(linha.last.$3, closeTo(16, 0.01));
      for (var i = 1; i < linha.length; i++) {
        expect(linha[i].$2, closeTo(linha[i - 1].$3, 0.01),
            reason: 'nao pode sobrar buraco entre os pedacos');
      }
    });

    // A ordem de aplicacao e o que costuma quebrar: cortar de frente
    // para tras desloca os trechos seguintes, que ainda usam o tempo
    // antigo, e os cortes saem todos fora de lugar.
    test('a ordem dos trechos na lista nao muda o resultado', () {
      final base = fala(0, 20);
      final a = removeRangesFrom([base], base.id, [
        (_s(2), _s(3)),
        (_s(7), _s(9)),
      ]);
      final b = removeRangesFrom([base], base.id, [
        (_s(7), _s(9)),
        (_s(2), _s(3)),
      ]);
      expect(_linha(a), _linha(b));
    });

    test('cada pedaco continua o audio de onde parou', () {
      final base = fala(0, 20);
      final r = removeRangesFrom([base], base.id, [
        (_s(2), _s(3)),
        (_s(7), _s(9)),
      ]).whereType<AudioLayer>().toList()
        ..sort((a, b) => a.startTime.compareTo(b.startTime));
      expect(r[0].sourceOffset, Duration.zero);
      expect(r[1].sourceOffset, _s(3));
      expect(r[2].sourceOffset, _s(9));
    });

    test('sem arrasto, os buracos ficam', () {
      final base = fala(0, 20);
      final r = removeRangesFrom(
          [base], base.id, [(_s(5), _s(8))],
          ripple: false);
      expect(gapsIn(r), hasLength(1));
      expect(gapsIn(r).first.$1, _s(5));
    });

    test('outras camadas nao entram no corte', () {
      final base = fala(0, 20);
      final outra = _clip('titulo', 0, 20);
      final r = removeRangesFrom([base, outra], base.id, [(_s(5), _s(8))]);
      final t = r.whereType<TextLayer>().single;
      expect(t.startTime, Duration.zero);
      expect(t.duration, _s(20));
    });
  });

  group('Decupagem — ficar so com o que interessa', () {
    AudioLayer fala(num inicio, num dur) => AudioLayer(
          name: 'fala',
          startTime: _s(inicio),
          duration: _s(dur),
          sourcePath: 'fala.wav',
        );

    test('mantem os trechos pedidos e encosta um no outro', () {
      final base = fala(0, 10);
      final r = keepRangesOf([base], base.id, [
        (_s(1), _s(3)),
        (_s(6), _s(8)),
      ]);
      final linha = _linha(r);
      expect(linha, hasLength(2));
      expect(linha[0].$2, 0);
      expect(linha[0].$3, closeTo(2, 0.01));
      expect(linha[1].$2, closeTo(2, 0.01));
      expect(linha[1].$3, closeTo(4, 0.01));
    });

    test('trecho que passa da borda e aparado', () {
      final base = fala(2, 6);
      final r = keepRangesOf([base], base.id, [(_s(0), _s(100))]);
      final linha = _linha(r);
      expect(linha, hasLength(1));
      expect(linha.first.$2, 2);
      expect(linha.first.$3, 8);
    });

    test('sem nada a manter, a camada some', () {
      final base = fala(0, 10);
      final r = keepRangesOf([base], base.id, const []);
      expect(r.whereType<AudioLayer>(), isEmpty);
    });

    test('camada que nao existe nao faz nada', () {
      final base = fala(0, 10);
      expect(keepRangesOf([base], 'nao-existe', const []), hasLength(1));
    });
  });

  group('Juntar adjacentes', () {
    VideoLayer clipe(num inicio, num dur, {num off = 0, String p = 'a.mp4'}) =>
        VideoLayer(
          name: 'tomada',
          startTime: _s(inicio),
          duration: _s(dur),
          sourcePath: p,
          sourceOffset: _s(off),
        );

    // A prova que o documento pede: cortar e juntar de volta devolve o
    // clipe IDENTICO ao original.
    test('cortar e juntar devolve o original', () {
      final inteiro = clipe(0, 10);
      final a = inteiro.copyLayer(duration: _s(4));
      final b = inteiro.duplicated().copyLayer(
            startTime: _s(4),
            duration: _s(6),
          );
      final b2 = b.copyLayer(sourceOffset: _s(4));

      expect(canJoin(a, b2), isTrue);
      final r = joinAdjacent([a, b2], a.id, b2.id);
      expect(r, hasLength(1));
      final j = r.first as VideoLayer;
      expect(j.startTime, Duration.zero);
      expect(j.duration, _s(10));
      expect(j.sourceOffset, Duration.zero);
    });

    test('separados na linha nao juntam', () {
      final a = clipe(0, 4);
      final b = clipe(6, 4, off: 4);
      expect(canJoin(a, b), isFalse);
    });

    // Sem checar a FONTE, dois trechos distantes do mesmo arquivo
    // "juntariam" e o video pularia no meio.
    test('em sequencia na linha mas nao na fonte nao juntam', () {
      final a = clipe(0, 4);
      final b = clipe(4, 4, off: 30);
      expect(canJoin(a, b), isFalse);
    });

    test('arquivos diferentes nao juntam', () {
      expect(canJoin(clipe(0, 4), clipe(4, 4, off: 4, p: 'b.mp4')), isFalse);
    });

    test('tipos diferentes nao juntam', () {
      final v = clipe(0, 4);
      final a = AudioLayer(
        name: 'f',
        startTime: _s(4),
        duration: _s(4),
        sourcePath: 'a.mp4',
        sourceOffset: _s(4),
      );
      expect(canJoin(v, a), isFalse);
    });

    test('velocidades diferentes nao juntam', () {
      final a = clipe(0, 4);
      final b = clipe(4, 4, off: 4).copyLayer(speed: 2);
      expect(canJoin(a, b), isFalse);
    });

    // Exigir igualdade exata em microssegundos reprovaria juncoes
    // legitimas por causa de arredondamento.
    test('um quadro de folga ainda junta', () {
      final a = clipe(0, 4);
      final b = clipe(4.02, 4, off: 4.02);
      expect(canJoin(a, b), isTrue);
    });

    test('acha o vizinho que da para juntar', () {
      final a = clipe(0, 4);
      final b = clipe(4, 4, off: 4);
      final c = clipe(20, 4, off: 40);
      expect(joinableNeighbour([a, b, c], a.id)?.id, b.id);
      expect(joinableNeighbour([a, c], a.id), isNull);
      expect(joinableNeighbour([a, b], 'fantasma'), isNull);
    });

    test('juntar o que nao da nao mexe em nada', () {
      final a = clipe(0, 4);
      final c = clipe(20, 4, off: 40);
      expect(joinAdjacent([a, c], a.id, c.id), hasLength(2));
      expect(joinAdjacent([a, c], a.id, 'fantasma'), hasLength(2));
    });

    test('o caminho da midia sai certo por tipo', () {
      expect(mediaPathOf(clipe(0, 1)), 'a.mp4');
      expect(mediaPathOf(_clip('titulo', 0, 1)), isNull);
    });
  });
}
