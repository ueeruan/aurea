import 'package:aurea/src/features/editor/application/desempenho/escada_por_tempo_de_quadro.dart';
import 'package:aurea/src/features/editor/application/desempenho/politica_de_desempenho.dart';
import 'package:aurea/src/features/editor/domain/orcamento_render.dart';
import 'package:flutter_test/flutter_test.dart';

/// A CONTA DO DESEMPENHO, sem aparelho nenhum.
///
/// `politicaPara` e uma funcao pura: perfil + sinais -> o que a PREVIA
/// pode gastar. O que estes testes prendem e:
///
///  * Automatico com o aparelho frio e o app de sempre (nada e reduzido);
///  * a escada so DESCE, e desce por temperatura, memoria e tempo de quadro;
///  * nos perfis fixos o tempo de quadro nao mexe na resolucao sozinho;
///  * Maxima qualidade so cede no extremo;
///  * e a EXPORTACAO nao passa por esta conta em nenhum caminho.
void main() {
  const frio = SinaisDeDesempenho();

  group('automatico', () {
    test('frio e parado: qualidade cheia, como antes de existir politica', () {
      final p = politicaPara(PerfilDeDesempenho.automatico, frio);
      expect(p.escalaDaPrevia, 1);
      expect(p.niveisDoBrilho, 5);
      expect(p.sombra3D, isTrue);
      expect(p.amostras3D, 4);
      expect(p.teto3D, Qualidade3D.ultra);
      expect(p.fpsAlvoDaPrevia, 0, reason: 'a taxa do projeto');
      expect(p.rascunho, isFalse);
      expect(p.decodificacao, DecodificacaoDaPrevia.completa);
    });

    test('esquentando alivia a previa; quente corta a metade', () {
      final morno = politicaPara(
        PerfilDeDesempenho.automatico,
        const SinaisDeDesempenho(termico: EstadoTermico.subindo),
      );
      expect(morno.escalaDaPrevia, .75);
      expect(morno.sombra3D, isTrue, reason: 'o primeiro degrau ainda nao');

      final quente = politicaPara(
        PerfilDeDesempenho.automatico,
        const SinaisDeDesempenho(termico: EstadoTermico.alto),
      );
      expect(quente.escalaDaPrevia, .5);
      expect(quente.sombra3D, isFalse);
      expect(quente.fpsAlvoDaPrevia, 30);
      expect(quente.miniaturasPorLote, lessThan(morno.miniaturasPorLote));
    });

    test('critico: um quarto, o minimo para continuar editando', () {
      final p = politicaPara(
        PerfilDeDesempenho.automatico,
        const SinaisDeDesempenho(termico: EstadoTermico.critico),
      );
      expect(p.escalaDaPrevia, .25);
      expect(p.miniaturasPorLote, 0);
      expect(p.decodificacao, DecodificacaoDaPrevia.minima);
      expect(p.motivo, contains('temperatura'));
    });

    test('o tempo de quadro desce a escada sozinho', () {
      final p = politicaPara(
        PerfilDeDesempenho.automatico,
        const SinaisDeDesempenho(degrausPorTempo: 2),
      );
      expect(p.escalaDaPrevia, .5);
      expect(p.motivo, contains('quadros atrasando'));
    });

    test('calor e quadro lento se SOMAM', () {
      final p = politicaPara(
        PerfilDeDesempenho.automatico,
        const SinaisDeDesempenho(
          termico: EstadoTermico.subindo,
          degrausPorTempo: 2,
        ),
      );
      expect(p.escalaDaPrevia, .25, reason: '1 + 2 = 3 degraus');
    });

    test('memoria no limite vai direto ao ultimo degrau', () {
      final p = politicaPara(
        PerfilDeDesempenho.automatico,
        const SinaisDeDesempenho(memoriaEmEmergencia: true),
      );
      expect(p.escalaDaPrevia, .25);
      expect(p.motivo, contains('memoria'));
    });
  });

  group('perfis fixos', () {
    test('economia e o ponto de partida mais leve', () {
      final p = politicaPara(PerfilDeDesempenho.economia, frio);
      expect(p.escalaDaPrevia, .5);
      expect(p.fpsAlvoDaPrevia, 30);
      expect(p.sombra3D, isFalse);
      expect(p.decodificacao, DecodificacaoDaPrevia.reduzida);
    });

    test('quadro lento NAO muda a resolucao num perfil escolhido', () {
      final base = politicaPara(PerfilDeDesempenho.equilibrado, frio);
      final comAtraso = politicaPara(
        PerfilDeDesempenho.equilibrado,
        const SinaisDeDesempenho(degrausPorTempo: 2),
      );
      expect(comAtraso.escalaDaPrevia, base.escalaDaPrevia);
    });

    test('mas o CALOR desce qualquer perfil', () {
      final p = politicaPara(
        PerfilDeDesempenho.equilibrado,
        const SinaisDeDesempenho(termico: EstadoTermico.alto),
      );
      expect(p.escalaDaPrevia, .5);
      expect(p.sombra3D, isFalse);
    });

    test('a escada nunca MELHORA o perfil', () {
      final p = politicaPara(
        PerfilDeDesempenho.economia,
        const SinaisDeDesempenho(termico: EstadoTermico.subindo),
      );
      final base = politicaPara(PerfilDeDesempenho.economia, frio);
      expect(p.escalaDaPrevia, lessThanOrEqualTo(base.escalaDaPrevia));
      expect(p.niveisDoBrilho, lessThanOrEqualTo(base.niveisDoBrilho));
      expect(p.fpsAlvoDaPrevia, base.fpsAlvoDaPrevia);
    });
  });

  group('maxima qualidade', () {
    test('aguenta calor e quadro lento sem ceder', () {
      final p = politicaPara(
        PerfilDeDesempenho.maximaQualidade,
        const SinaisDeDesempenho(
          termico: EstadoTermico.alto,
          degrausPorTempo: 2,
          memoriaApertada: true,
        ),
      );
      expect(p.escalaDaPrevia, 1);
      expect(p.niveisDoBrilho, 5);
    });

    test('nem tocando baixa o acabamento', () {
      final p = politicaPara(
        PerfilDeDesempenho.maximaQualidade,
        const SinaisDeDesempenho(tocando: true, interagindo: true),
      );
      expect(p.rascunho, isFalse);
    });

    test('cede na temperatura critica e no aviso de memoria', () {
      final critico = politicaPara(
        PerfilDeDesempenho.maximaQualidade,
        const SinaisDeDesempenho(termico: EstadoTermico.critico),
      );
      expect(critico.escalaDaPrevia, .5);

      final memoria = politicaPara(
        PerfilDeDesempenho.maximaQualidade,
        const SinaisDeDesempenho(memoriaEmEmergencia: true),
      );
      expect(memoria.escalaDaPrevia, .25);
    });
  });

  group('rascunho', () {
    test('tocando ou com o dedo no comando', () {
      expect(politicaPara(PerfilDeDesempenho.automatico, frio).rascunho, false);
      expect(
        politicaPara(
          PerfilDeDesempenho.automatico,
          const SinaisDeDesempenho(tocando: true),
        ).rascunho,
        isTrue,
      );
      expect(
        politicaPara(
          PerfilDeDesempenho.automatico,
          const SinaisDeDesempenho(interagindo: true),
        ).rascunho,
        isTrue,
      );
    });
  });

  group('o 3D nunca promete mais do que o controlador deu', () {
    test('sem MSAA e sem sombra na receita, a politica tambem nao tem', () {
      final p = politicaPara(
        PerfilDeDesempenho.automatico,
        const SinaisDeDesempenho(receita3D: ReceitaDeQualidade.baixa),
      );
      expect(p.amostras3D, 1);
      expect(p.sombra3D, ReceitaDeQualidade.baixa.sombras);
    });
  });

  group('A EXPORTACAO E IMUNE', () {
    test('a politica de exportacao e sempre a completa', () {
      const e = PoliticaDeDesempenho.exportacao;
      expect(e.escalaDaPrevia, 1);
      expect(e.niveisDoBrilho, 5);
      expect(e.amostras3D, 4);
      expect(e.sombra3D, isTrue);
      expect(e.teto3D, Qualidade3D.ultra);
      expect(e.rascunho, isFalse);
    });

    test('nenhum sinal, em nenhum perfil, a altera', () {
      const piorCaso = SinaisDeDesempenho(
        termico: EstadoTermico.critico,
        memoriaApertada: true,
        memoriaEmEmergencia: true,
        degrausPorTempo: 2,
        interagindo: true,
        tocando: true,
        receita3D: ReceitaDeQualidade.emergencia,
      );
      for (final perfil in PerfilDeDesempenho.values) {
        // A conta da previa desaba...
        final previa = politicaPara(perfil, piorCaso);
        expect(previa.escalaDaPrevia, lessThanOrEqualTo(1));
        // ...e a constante da exportacao continua onde estava.
        expect(
          PoliticaDeDesempenho.exportacao.escalaDaPrevia,
          1,
          reason: 'o video nao pode sair pior porque o celular esquentou',
        );
        expect(PoliticaDeDesempenho.exportacao.niveisDoBrilho, 5);
        expect(PoliticaDeDesempenho.exportacao.teto3D, Qualidade3D.ultra);
      }
    });
  });

  group('igualdade por valor', () {
    test('mesmos sinais, mesma politica (o ValueNotifier fica quieto)', () {
      expect(
        politicaPara(PerfilDeDesempenho.automatico, frio),
        politicaPara(PerfilDeDesempenho.automatico, frio),
      );
    });
  });

  group('escada pelo tempo de quadro', () {
    test('doze quadros lentos descem um degrau; um pico nao', () {
      final e = EscadaPorTempoDeQuadro();
      for (var i = 0; i < 11; i++) {
        e.amostra(50);
      }
      expect(e.degraus, 0, reason: 'onze ainda nao');
      expect(e.amostra(50), isTrue);
      expect(e.degraus, 1);
    });

    test('um quadro folgado no meio zera a contagem', () {
      final e = EscadaPorTempoDeQuadro();
      for (var i = 0; i < 11; i++) {
        e.amostra(50);
      }
      e.amostra(8);
      for (var i = 0; i < 11; i++) {
        e.amostra(50);
      }
      expect(e.degraus, 0);
    });

    test('nao passa de dois degraus', () {
      // A CARENCIA ENTRE DEGRAUS (2 s) e o que impede a escada de virar
      // oscilador, entao o relogio tem de andar entre um degrau e outro.
      var agora = DateTime(2026, 9, 20, 12);
      final e = EscadaPorTempoDeQuadro(agora: () => agora);
      for (var i = 0; i < 200; i++) {
        agora = agora.add(const Duration(milliseconds: 80));
        e.amostra(80);
      }
      expect(e.degraus, 2);
    });

    test('dois degraus nao cabem na mesma carencia', () {
      var agora = DateTime(2026, 9, 20, 12);
      final e = EscadaPorTempoDeQuadro(agora: () => agora);
      for (var i = 0; i < 12; i++) {
        e.amostra(80);
      }
      expect(e.degraus, 1);
      // Mais doze lentos no MESMO instante: o segundo degrau nao sai.
      for (var i = 0; i < 12; i++) {
        e.amostra(80);
      }
      expect(e.degraus, 1, reason: 'a carencia de 2 s nao passou');
      agora = agora.add(const Duration(seconds: 3));
      for (var i = 0; i < 12; i++) {
        e.amostra(80);
      }
      expect(e.degraus, 2);
    });

    test('os quadros logo depois de acomodar nao votam', () {
      final e = EscadaPorTempoDeQuadro();
      e.acomodar();
      expect(e.acomodando, isTrue);
      for (var i = 0; i < EscadaPorTempoDeQuadro.quadrosDeAcomodacao; i++) {
        e.amostra(80);
      }
      expect(e.acomodando, isFalse);
      // Onze lentos depois da acomodacao ainda nao bastam: a contagem
      // recomecou do zero, e os quadros descartados nao entraram nela.
      for (var i = 0; i < 11; i++) {
        e.amostra(80);
      }
      expect(e.degraus, 0);
      expect(e.amostra(80), isTrue);
    });

    test('sem motivo para o quadro existir, a contagem morre', () {
      final e = EscadaPorTempoDeQuadro();
      for (var i = 0; i < 11; i++) {
        e.amostra(80);
      }
      e.pausar();
      for (var i = 0; i < 11; i++) {
        e.amostra(80);
      }
      expect(e.degraus, 0, reason: 'as duas rajadas nao podem somar');
    });

    test('so sobe depois da espera, e com folga sustentada', () {
      var agora = DateTime(2026, 9, 20, 12);
      final e = EscadaPorTempoDeQuadro(agora: () => agora);
      for (var i = 0; i < 12; i++) {
        e.amostra(50);
      }
      expect(e.degraus, 1);
      for (var i = 0; i < 300; i++) {
        e.amostra(8);
      }
      expect(e.degraus, 1, reason: 'oito segundos ainda nao passaram');
      agora = agora.add(const Duration(seconds: 9));
      for (var i = 0; i < 300; i++) {
        e.amostra(8);
      }
      expect(e.degraus, 0);
    });
  });
}
