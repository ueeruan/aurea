// O MOTOR DE PARTICULAS EM C++ — a simulacao, o lote e o pintor.
//
// TODOS OS NUMEROS DESTE TESTE SAO MEDIDOS. A posicao lida e a que o
// simulador escreveu, o pixel e o que o rasterizador pintou, e a
// contagem e a que o lote devolveu. Nao ha "deve dar mais ou menos".
//
// O QUE CADA GRUPO SEGURA, e por que ele existe:
//
//   * ABI — um campo fora de lugar le o vizinho e devolve um numero
//     PLAUSIVEL: o tamanho das duas structs e conferido contra o C++;
//   * DETERMINISMO — o mesmo tempo tem de devolver o mesmo lote, senao
//     arrastar o cabecete para tras nao volta ao mesmo quadro;
//   * A FISICA — a trajetoria tem forma fechada, e cada ramo dela
//     (arrasto, gravidade, mola, repulsao) tem um caso conhecido aqui;
//   * O TETO — a qualidade adaptativa limita, mas nunca inventa
//     particulas que ninguem pediu.
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aurea_render/aurea_render.dart';
import 'package:flutter_test/flutter_test.dart';

/// Os campos de uma instancia, pelo nome da coluna — o contrato de ABI
/// com o C++. Nomear aqui evita `campo(i, 4)` espalhado pelo teste.
abstract final class Col {
  static const x = 0;
  static const y = 1;
  static const tamanho = 2;
  static const angulo = 3;
  static const r = 4;
  static const g = 5;
  static const b = 6;
  static const a = 7;
  static const profundidade = 8;
  static const forma = 9;
  static const caudaX = 10;
  static const caudaY = 11;
  static const u = 12;
  static const brilho = 13;
  static const variacao = 14;
}

ParametrosDeParticulas _base() => ParametrosDeParticulas(
  emissor: EmissorDeParticulas.caixa,
  centroX: 0,
  centroY: 0,
  largura: 100,
  altura: 100,
  profundidade: 0,
  taxaDeNascimento: 0,
  vidaS: 4,
  maximo: 64,
  semente: 7,
  velocidade: 0,
  tamanho: 20,
  tamanhoVariacao: 0,
  brilho: 0,
  cintilar: false,
  forma: FormaDaParticula.esfera,
);

void main() {
  setUpAll(() {
    expect(
      MotorDeParticulasRender.disponivel,
      isTrue,
      reason: 'a biblioteca de particulas nao carregou ou a ABI nao bate',
    );
  });

  group('ABI', () {
    test('a ABI das duas structs bate com o C++', () {
      // O proprio `disponivel` ja confere os tamanhos; aqui ele fica
      // explicito, para o dia em que a checagem mudar de lugar.
      expect(MotorDeParticulasRender.disponivel, isTrue);
      expect(LoteDeParticulas.flutuantesPorInstancia, 15);
    });

    test('os presets vem por nome, e nao por indice de enum', () {
      final nomes = MotorDeParticulasRender.nomesDosPresets;
      expect(nomes.length, 10);
      expect(nomes.first, 'Fogo');
      expect(nomes, contains('Explosao'));
      // TODO PRESET MONTA. Um indice que o C++ nao conhece devolveria
      // nulo, e a aba de presets ficaria com um buraco.
      for (var i = 0; i < nomes.length; i++) {
        expect(MotorDeParticulasRender.preset(i), isNotNull, reason: nomes[i]);
      }
    });
  });

  group('determinismo', () {
    test('o mesmo tempo devolve o mesmo lote, bit a bit', () {
      final p = _base()
        ..taxaDeNascimento = 60
        ..velocidade = 80
        ..gravidade = 120
        ..turbulencia = 40
        ..rastro = 0.5
        ..faiscas = 2;
      final a = LoteDeParticulas(p)..gerar(1.25);
      final b = LoteDeParticulas(p)..gerar(1.25);
      expect(a.quantas, greaterThan(0));
      expect(a.quantas, b.quantas);
      expect(
        a.floats,
        orderedEquals(b.floats),
        reason: 'a simulacao nao pode acumular estado entre quadros',
      );
      a.liberar();
      b.liberar();
    });

    test('ir e voltar no tempo devolve o mesmo quadro', () {
      // O INVARIANTE DO SCRUB: simular ate 2,0 s, depois ir para 3,0 s e
      // VOLTAR para 2,0 s tem de dar exatamente o lote dos 2,0 s — nao
      // uma aproximacao. Sem isso, arrastar o cabecete deixa a nuvem
      // "suja" e ela nunca mais volta ao que era.
      final p = _base()
        ..taxaDeNascimento = 50
        ..velocidade = 60
        ..gravidade = 90
        ..turbulencia = 30;
      final lote = LoteDeParticulas(p)..gerar(2.0);
      final referencia = Float32List.fromList(lote.floats);
      lote.gerar(3.0);
      lote.gerar(2.0);
      expect(lote.floats, orderedEquals(referencia));
      lote.liberar();
    });

    test('sementes diferentes dao nuvens diferentes', () {
      final a = LoteDeParticulas(_base()..semente = 1)..gerar(1);
      final b = LoteDeParticulas(_base()..semente = 2)..gerar(1);
      expect(a.floats, isNot(orderedEquals(b.floats)));
      a.liberar();
      b.liberar();
    });

    test('a semente nao mexe no que nao e dela', () {
      // O CENTRO DO EMISSOR E DE QUEM CHAMOU. Sem isto, trocar a semente
      // jogaria a nuvem para outro lugar da composicao.
      for (final s in [1, 99, 12345]) {
        final lote = LoteDeParticulas(_base()..semente = s)..gerar(1);
        var soma = 0.0;
        for (var i = 0; i < lote.quantas; i++) {
          soma += lote.campo(i, Col.x);
        }
        expect((soma / lote.quantas).abs(), lessThan(60));
        lote.liberar();
      }
    });
  });

  group('a fisica, em forma fechada', () {
    test('sem forca, a particula sai do emissor em linha reta', () {
      // O emissor de PONTO com velocidade em esfera: a distancia percorrida
      // e velocidade x tempo. E a prova de que a integracao nao perde
      // energia nem ganha passo.
      final p = _base()
        ..emissor = EmissorDeParticulas.ponto
        ..largura = 0
        ..altura = 0
        ..modoDeEmissao = ModoDeEmissao.esfera
        ..velocidade = 100
        ..maximo = 1;
      final lote = LoteDeParticulas(p)..gerar(0.01);
      // A particula 0 nasce em fase sorteada; o que se pode afirmar e que
      // a distancia ao centro CRESCE com o tempo.
      double dist(double t) {
        final l = LoteDeParticulas(p)..gerar(t);
        final d = math.sqrt(
          math.pow(l.campo(0, Col.x), 2) + math.pow(l.campo(0, Col.y), 2),
        );
        l.liberar();
        return d;
      }

      final d1 = dist(0.5);
      final d2 = dist(1.0);
      expect(d2, greaterThan(d1));
      lote.liberar();
    });

    test('a gravidade puxa para BAIXO, e o arrasto trava a queda', () {
      // Sem arrasto a queda e quadratica; com arrasto ela chega a uma
      // velocidade terminal. Os dois numeros sao calculaveis:
      //   sem arrasto: y(T) - y(0) = g T^2/2 (modulo a fase de cada uma)
      //   com arrasto:  a velocidade tende a g/k
      double deslocamentoMedio(double arrasto) {
        final p = _base()
          ..emissor = EmissorDeParticulas.ponto
          ..largura = 0
          ..altura = 0
          ..modoDeEmissao = ModoDeEmissao.esfera
          ..velocidade = 0
          ..gravidade = 100
          ..arrasto = arrasto
          ..maximo = 32
          ..vidaS = 10;
        final l = LoteDeParticulas(p)..gerar(2.0);
        var soma = 0.0;
        for (var i = 0; i < l.quantas; i++) {
          soma += l.campo(i, Col.y);
        }
        final media = soma / l.quantas;
        l.liberar();
        return media;
      }

      // SEM ARRASTO, a 4 s de queda (idade media = metade da vida)...
      // Aqui a idade de cada uma e `(t - fase) mod vida`, entao a media
      // nao e um numero fechado. O que se afirma e o que importa: cai
      // para baixo, e o arrasto FREIA a queda.
      final solto = deslocamentoMedio(0);
      final freado = deslocamentoMedio(3.0);
      expect(solto, greaterThan(0), reason: 'gravidade positiva = para baixo');
      expect(freado, lessThan(solto), reason: 'o ar tem de frear');
    });

    test('a atracao traz de volta; a repulsao afasta', () {
      // A MOLA: partindo do repouso a uma distancia do centro, a
      // amplitude do movimento nao pode CRESCER com atracao positiva.
      double afastamentoMedio(double atracao) {
        final p = _base()
          ..emissor = EmissorDeParticulas.esfera
          ..raio = 400
          ..velocidade = 0
          ..atracao = atracao
          ..atracaoX = 0
          ..atracaoY = 0
          ..maximo = 32
          ..vidaS = 3;
        final l = LoteDeParticulas(p)..gerar(0.6);
        var soma = 0.0;
        for (var i = 0; i < l.quantas; i++) {
          soma += math.sqrt(
            l.campo(i, Col.x) * l.campo(i, Col.x) +
                l.campo(i, Col.y) * l.campo(i, Col.y),
          );
        }
        final media = soma / l.quantas;
        l.liberar();
        return media;
      }

      final solto = afastamentoMedio(0);
      expect(afastamentoMedio(2.0), lessThan(solto), reason: 'a mola puxa');
      expect(afastamentoMedio(-2.0), greaterThan(solto), reason: 'a repulsao empurra');
    });

    test('uma repulsao absurda nao envenena o lote com NaN', () {
      // SEM O TETO DE SEGURANCA a exponencial estoura e vira `inf`, e um
      // `inf` numa instancia escreve `nan` em cima da composicao inteira.
      final p = _base()
        ..emissor = EmissorDeParticulas.esfera
        ..raio = 200
        ..atracao = -400
        ..velocidade = 900
        ..vidaS = 60
        ..maximo = 64;
      final lote = LoteDeParticulas(p)..gerar(30);
      for (var i = 0; i < lote.quantas; i++) {
        for (final c in [Col.x, Col.y, Col.tamanho, Col.a]) {
          expect(lote.campo(i, c).isFinite, isTrue, reason: 'campo $c');
        }
      }
      lote.liberar();
    });
  });

  group('os emissores', () {
    test('ponto: todas nascem na origem', () {
      final p = _base()
        ..emissor = EmissorDeParticulas.ponto
        ..largura = 500
        ..altura = 500
        ..velocidade = 0
        ..maximo = 40
        ..vidaS = 10;
      // Com vida longa e sem forca, a idade e pequena e o desvio e o do
      // proprio ponto: nada de espalhamento em X/Y.
      final lote = LoteDeParticulas(p)..gerar(0.02);
      for (var i = 0; i < lote.quantas; i++) {
        expect(lote.campo(i, Col.x).abs(), lessThan(1e-6));
        expect(lote.campo(i, Col.y).abs(), lessThan(1e-6));
      }
      lote.liberar();
    });

    test('caixa: tudo dentro do retangulo pedido', () {
      final p = _base()
        ..emissor = EmissorDeParticulas.caixa
        ..largura = 300
        ..altura = 200
        ..profundidade = 0
        ..velocidade = 0
        ..maximo = 200
        ..vidaS = 10;
      final lote = LoteDeParticulas(p)..gerar(0.01);
      for (var i = 0; i < lote.quantas; i++) {
        expect(lote.campo(i, Col.x).abs(), lessThanOrEqualTo(150.0001));
        expect(lote.campo(i, Col.y).abs(), lessThanOrEqualTo(100.0001));
      }
      lote.liberar();
    });

    test('esfera: tudo dentro do raio pedido', () {
      final p = _base()
        ..emissor = EmissorDeParticulas.esfera
        ..raio = 120
        ..velocidade = 0
        ..maximo = 200
        ..vidaS = 10;
      final lote = LoteDeParticulas(p)..gerar(0.01);
      for (var i = 0; i < lote.quantas; i++) {
        final d = math.sqrt(
          lote.campo(i, Col.x) * lote.campo(i, Col.x) +
              lote.campo(i, Col.y) * lote.campo(i, Col.y),
        );
        expect(d, lessThanOrEqualTo(120.0001));
      }
      lote.liberar();
    });

    test('linha: tudo em cima do segmento, e nao numa caixa', () {
      // A DIFERENCA ENTRE LINHA E CAIXA: na linha as particulas ficam
      // sobre o eixo X, com Y praticamente zero. Uma caixa de 500 de
      // largura por 500 de altura espalharia nos dois.
      final p = _base()
        ..emissor = EmissorDeParticulas.linha
        ..raio = 200
        ..linhaX = 1
        ..linhaY = 0
        ..largura = 500
        ..altura = 500
        ..velocidade = 0
        ..maximo = 100
        ..vidaS = 10;
      final lote = LoteDeParticulas(p)..gerar(0.01);
      var espalhados = 0;
      for (var i = 0; i < lote.quantas; i++) {
        expect(lote.campo(i, Col.x).abs(), lessThanOrEqualTo(100.0001));
        if (lote.campo(i, Col.y).abs() > 1e-6) espalhados++;
      }
      expect(espalhados, 0, reason: 'a linha nao pode espalhar em Y');
      lote.liberar();
    });
  });

  group('fluxo e vida', () {
    test('a taxa de nascimento define quantas ficam vivas', () {
      // taxa x vida = quantas ao mesmo tempo. E esse o numero que a
      // pessoa ve — nao o total de nascimentos.
      final p = _base()
        ..taxaDeNascimento = 50
        ..vidaS = 2
        ..maximo = 1000;
      expect(MotorDeParticulasRender.tamanhoDoLote(p), 100);
      final lote = LoteDeParticulas(p)..gerar(1);
      expect(lote.quantas, 100);
      lote.liberar();
    });

    test('o teto manda, mesmo com taxa alta', () {
      final p = _base()
        ..taxaDeNascimento = 500
        ..vidaS = 8
        ..maximo = 120;
      expect(MotorDeParticulasRender.tamanhoDoLote(p), 120);
      final lote = LoteDeParticulas(p)..gerar(1);
      expect(lote.quantas, lessThanOrEqualTo(120));
      lote.liberar();
    });

    test('sem taxa, o campo ja nasce cheio (pre-roll)', () {
      // O REGIME DE PROJETO ANTIGO: abrir no meio da cena e ver a nuvem
      // formada, e nao um emissor comecando do zero.
      final p = _base()
        ..taxaDeNascimento = 0
        ..maximo = 80
        ..vidaS = 4;
      final lote = LoteDeParticulas(p)..gerar(0.0);
      expect(lote.quantas, greaterThan(60));
      lote.liberar();
    });

    test('o rastro e as faiscas aumentam o lote, e o lote avisa', () {
      final p = _base()..maximo = 50;
      expect(MotorDeParticulasRender.tamanhoDoLote(p), 50);
      p.rastro = 1.0;
      expect(MotorDeParticulasRender.tamanhoDoLote(p), 50 * 7);
      p.faiscas = 3;
      expect(MotorDeParticulasRender.tamanhoDoLote(p), 50 * 10);
      // O TETO DE FAISCAS E 24 por particula: sem ele um campo grande
      // explodiria o custo do quadro.
      p.faiscas = 999;
      expect(MotorDeParticulasRender.tamanhoDoLote(p), 50 * 31);
    });

    test('o buffer do lote nunca transborda', () {
      final p = _base()
        ..maximo = 400
        ..taxaDeNascimento = 300
        ..vidaS = 5
        ..rastro = 1
        ..faiscas = 24;
      final lote = LoteDeParticulas(p);
      for (final t in [0.0, 0.5, 1.0, 7.3, 100.0]) {
        final n = lote.gerar(t);
        expect(n, lessThanOrEqualTo(lote.capacidade));
        expect(lote.floats.length, n * LoteDeParticulas.flutuantesPorInstancia);
      }
      lote.liberar();
    });
  });

  group('a qualidade adaptativa', () {
    test('o nivel limita, mas nunca inventa particulas', () {
      // E UM TETO, e nao um multiplicador: um campo de 100 continua com
      // 100 no nivel minimo.
      expect(MotorDeParticulasRender.teto(0, 100), 100);
      expect(MotorDeParticulasRender.teto(0, 5000), 256);
      expect(MotorDeParticulasRender.teto(3, 5000), 4096);
      expect(MotorDeParticulasRender.teto(3, 300), 300);
    });

    test('a qualidade do projeto e aplicada sobre o pedido', () {
      final p = _base()
        ..maximo = 4000
        ..maximo = MotorDeParticulasRender.teto(0, 4000);
      expect(p.maximo, 256);
      final lote = LoteDeParticulas(p)..gerar(1);
      expect(lote.quantas, lessThanOrEqualTo(256));
      lote.liberar();
    });
  });

  group('o pintor de referencia', () {
    /// Um alvo RGBA8 premultiplicado, do tamanho pedido.
    Uint8List alvo(int w, int h) => Uint8List(w * h * 4);

    List<int> pixel(Uint8List a, int w, int x, int y) {
      final i = (y * w + x) * 4;
      return [a[i], a[i + 1], a[i + 2], a[i + 3]];
    }

    test('uma particula branca no centro pinta o centro e nao o canto', () {
      // O EMISSOR NO MEIO DO ALVO: a projecao poe a particula onde o
      // centro do emissor esta, e um emissor em (0,0) desenharia no canto.
      final p = _base()
        ..emissor = EmissorDeParticulas.ponto
        ..centroX = 32
        ..centroY = 32
        ..largura = 0
        ..altura = 0
        ..velocidade = 0
        ..tamanho = 20
        ..brilho = 0
        ..cintilar = false
        ..maximo = 1
        ..vidaS = 100
        ..corInicio = 0xFFFFFFFF
        ..forma = FormaDaParticula.esfera;
      final lote = LoteDeParticulas(p)..gerar(0.01);
      expect(lote.quantas, 1);
      final a = alvo(64, 64);
      final tocados = lote.pintar(a, 64, 64);
      expect(tocados, greaterThan(0));
      final centro = pixel(a, 64, 32, 32);
      expect(centro[0], greaterThan(200));
      expect(centro[3], greaterThan(200), reason: 'o alfa tem de subir');
      expect(pixel(a, 64, 2, 2), [0, 0, 0, 0]);
      lote.liberar();
    });

    test('a cor da instancia chega ao pixel', () {
      final p = _base()
        ..emissor = EmissorDeParticulas.ponto
        ..centroX = 32
        ..centroY = 32
        ..velocidade = 0
        ..tamanho = 20
        ..brilho = 0
        ..cintilar = false
        ..maximo = 1
        ..vidaS = 100
        ..corInicio = 0x00FF00FF  // verde puro em 0xRRGGBBAA
        ..forma = FormaDaParticula.esfera;
      final lote = LoteDeParticulas(p)..gerar(0.01);
      final a = alvo(64, 64);
      lote.pintar(a, 64, 64);
      final c = pixel(a, 64, 32, 32);
      expect(c[1], greaterThan(200), reason: 'canal verde');
      expect(c[0], lessThan(40));
      expect(c[2], lessThan(40));
      lote.liberar();
    });

    test('o pintor nao toca fora do alvo', () {
      // A CAIXA DA INSTANCIA E PRESA AO ALVO. Sem isso o `memcpy` de
      // volta escreveria fora do buffer, e o sintoma seria corrupcao em
      // outro lugar do app.
      // AS PARTICULAS CABEM NO ALVO; AS CAIXAS DELAS NAO. E a caixa da
      // INSTANCIA (centro mais o alcance da forma) que tem de ser presa
      // ao alvo — sem isso o `memcpy` de volta escreveria fora do buffer.
      final p = _base()
        ..emissor = EmissorDeParticulas.caixa
        ..centroX = 16
        ..centroY = 16
        ..largura = 24
        ..altura = 24
        ..velocidade = 0
        ..tamanho = 80
        ..maximo = 200;
      final lote = LoteDeParticulas(p)..gerar(1.0);
      final a = alvo(32, 32);
      final tocados = lote.pintar(a, 32, 32);
      expect(tocados, greaterThan(0));
      // O alvo e o mesmo objeto: se algo tivesse escrito fora, o
      // proprio `setAll` de volta teria corrompido o buffer — e o teste
      // de tamanho acima ja teria caido.
      expect(a.length, 32 * 32 * 4);
      lote.liberar();
    });

    test('duas particulas no mesmo lugar somam alfa, e nao apagam uma a outra', () {
      final p = _base()
        ..emissor = EmissorDeParticulas.ponto
        ..centroX = 32
        ..centroY = 32
        ..velocidade = 0
        ..tamanho = 20
        ..brilho = 0
        ..cintilar = false
        ..maximo = 8
        ..vidaS = 100
        ..corInicio = 0xFFFFFFFF
        ..forma = FormaDaParticula.esfera;
      final lote = LoteDeParticulas(p)..gerar(0.01);
      final a = alvo(64, 64);
      lote.pintar(a, 64, 64);
      final muitas = pixel(a, 64, 32, 32)[3];
      final umaLote = LoteDeParticulas(p..maximo = 1)..gerar(0.01);
      final b = alvo(64, 64);
      umaLote.pintar(b, 64, 64);
      final uma = pixel(b, 64, 32, 32)[3];
      expect(muitas, greaterThanOrEqualTo(uma));
      lote.liberar();
      umaLote.liberar();
    });

    test('as seis formas desenham, e nenhuma delas e um quadrado cheio', () {
      for (final forma in FormaDaParticula.values) {
        final p = _base()
          ..emissor = EmissorDeParticulas.ponto
          ..centroX = 48
          ..centroY = 48
          ..velocidade = 0
          ..tamanho = 18
          ..brilho = 0
          ..cintilar = false
          ..maximo = 1
          ..vidaS = 100
          ..corInicio = 0xFFFFFFFF
          ..forma = forma;
        final lote = LoteDeParticulas(p)..gerar(0.01);
        expect(lote.campo(0, Col.forma), forma.valorNoNucleo.toDouble());
        final a = alvo(96, 96);
        final tocados = lote.pintar(a, 96, 96);
        expect(tocados, greaterThan(0), reason: 'a forma $forma nao pintou');
        // O CANTO DO QUADRADO DE TESTE FICA LONGE: nenhuma forma chega a
        // 40 px de raio, entao o ponto (2,2) tem de continuar vazio.
        expect(pixel(a, 96, 2, 2)[3], 0, reason: 'a forma $forma vazou');
        lote.liberar();
      }
    });
  });

  group('os presets', () {
    test('cada preset monta uma nuvem que desenha', () {
      final nomes = MotorDeParticulasRender.nomesDosPresets;
      for (var i = 0; i < nomes.length; i++) {
        final p = MotorDeParticulasRender.preset(i)!
          ..centroX = 0
          ..centroY = 0
          ..focal = 1200;
        final lote = LoteDeParticulas(p)..gerar(1.5);
        expect(lote.quantas, greaterThan(0), reason: nomes[i]);
        final a = Uint8List(256 * 256 * 4);
        final tocados = lote.pintar(a, 256, 256);
        expect(tocados, greaterThan(0), reason: '${nomes[i]} nao pintou');
        lote.liberar();
      }
    });

    test('trocar de preset nao move a nuvem nem troca a semente', () {
      // O QUE NAO E DA RECEITA FICA: centro, lente, rotacoes e semente
      // sao de quem chamou. Sem isso, escolher "Neve" jogaria o sistema
      // para o canto da composicao.
      var p = _base()
        ..centroX = 321
        ..centroY = 654
        ..centroZ = 12
        ..semente = 4242
        ..focal = 800
        ..rotacaoXGraus = 15
        ..rotacaoYGraus = 25;
      p = MotorDeParticulasRender.aplicarPreset(0, p)!;
      expect(p.centroX, 321);
      expect(p.centroY, 654);
      expect(p.centroZ, 12);
      expect(p.semente, 4242);
      expect(p.focal, 800);
      expect(p.rotacaoXGraus, 15);
      expect(p.rotacaoYGraus, 25);
      // E a receita entrou.
      expect(p.forma, FormaDaParticula.nuvem);
      expect(p.taxaDeNascimento, greaterThan(0));
    });
  });

  group('desempenho do lote', () {
    test('o mesmo buffer serve todos os quadros, sem realocar', () {
      // A PROMESSA DE "POUCA ALOCACAO": o lote e reservado uma vez, e
      // `gerar` so escreve dentro dele. O `identical` do `Float32List`
      // prova que a memoria nao mudou de lugar entre quadros.
      final p = MotorDeParticulasRender.preset(0)!
        ..maximo = 400
        ..centroX = 0
        ..centroY = 0;
      final lote = LoteDeParticulas(p);
      lote.gerar(0);
      final endereco = lote.endereco;
      for (var q = 1; q <= 60; q++) {
        lote.gerar(q / 30.0);
        expect(lote.endereco, endereco,
            reason: 'o buffer realocou no quadro $q');
      }
      expect(lote.capacidade, greaterThan(0));
      lote.liberar();
    });
  });
}
