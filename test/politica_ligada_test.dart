// A POLITICA DE DESEMPENHO, LIGADA NOS CONSUMIDORES.
//
// O gerente ja publicava a politica e NINGUEM a lia: o aparelho
// esquentava, a escada descia degraus e a previa seguia no maximo —
// resolucao cheia, fotos de efeito em 2160 px, piramide de brilho
// inteira, video extraido em 360p. Este arquivo prende cada consumidor
// que foi ligado, e prende a regra que vale acima de todas:
//
//              A EXPORTACAO NUNCA LE A POLITICA.
//
// Rodar:  flutter test test/politica_ligada_test.dart
import 'package:aurea/src/features/editor/application/desempenho/aurea_performance_manager.dart';
import 'package:aurea/src/features/editor/application/quadros_de_video.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  final gerente = AureaPerformanceManager.instancia;

  tearDown(() async {
    await gerente.definirPerfil(PerfilDeDesempenho.automatico);
    gerente.politica.value = politicaPara(
      PerfilDeDesempenho.automatico,
      const SinaisDeDesempenho(),
    );
  });

  group('a decodificacao do video obedece a politica', () {
    test('frio: a altura de sempre', () {
      gerente.politica.value = politicaPara(
        PerfilDeDesempenho.automatico,
        const SinaisDeDesempenho(),
      );
      expect(QuadrosDeVideo.alturaAgora, QuadrosDeVideo.altura);
      expect(QuadrosDeVideo.alturaAgora, 360);
    });

    test('quente: cai para a reduzida; critico, para a minima', () {
      gerente.politica.value = politicaPara(
        PerfilDeDesempenho.automatico,
        const SinaisDeDesempenho(termico: EstadoTermico.alto),
      );
      expect(QuadrosDeVideo.alturaAgora, DecodificacaoDaPrevia.reduzida.alturaPx);

      gerente.politica.value = politicaPara(
        PerfilDeDesempenho.automatico,
        const SinaisDeDesempenho(termico: EstadoTermico.critico),
      );
      expect(QuadrosDeVideo.alturaAgora, DecodificacaoDaPrevia.minima.alturaPx);
      expect(QuadrosDeVideo.alturaAgora, lessThan(QuadrosDeVideo.altura));
    });
  });

  group('a exportacao e imune', () {
    test('a politica de exportacao e constante, venha o sinal que vier', () {
      const completa = PoliticaDeDesempenho.exportacao;
      for (final perfil in PerfilDeDesempenho.values) {
        for (final sinais in const [
          SinaisDeDesempenho(),
          SinaisDeDesempenho(termico: EstadoTermico.critico),
          SinaisDeDesempenho(memoriaEmEmergencia: true),
          SinaisDeDesempenho(tocando: true, interagindo: true),
          SinaisDeDesempenho(degrausPorTempo: 2, memoriaApertada: true),
        ]) {
          gerente.perfil.value = perfil;
          gerente.politica.value = politicaPara(perfil, sinais);
          // O que a exportacao le NAO e a politica em vigor: e a
          // constante. Nenhum sinal atravessa esta linha.
          expect(gerente.politicaDeExportacao, same(completa));
          expect(gerente.politicaDeExportacao.escalaDaPrevia, 1);
          expect(gerente.politicaDeExportacao.niveisDoBrilho, 5);
          expect(gerente.politicaDeExportacao.tetoDasFotosPx, 8192);
          expect(gerente.politicaDeExportacao.rascunho, isFalse);
          expect(
            gerente.politicaDeExportacao.decodificacao,
            DecodificacaoDaPrevia.completa,
          );
        }
      }
      gerente.perfil.value = PerfilDeDesempenho.automatico;
    });

    test(
      'os consumidores do palco leem a politica por TETO: nunca sobem nada',
      () {
        // O contrato dos tres numeros que o palco passou a ler. Um teto
        // so pode limitar: com o aparelho frio ele vale exatamente o que
        // o app fazia antes de existir politica.
        final frio = politicaPara(
          PerfilDeDesempenho.automatico,
          const SinaisDeDesempenho(),
        );
        expect(frio.escalaDaPrevia, 1, reason: 'nao encolhe a previa a toa');
        expect(frio.niveisDoBrilho, 5, reason: 'piramide inteira');
        expect(
          frio.tetoDasFotosPx,
          greaterThanOrEqualTo(2160),
          reason: 'o teto das fotos do palco (2160) tem de caber embaixo',
        );

        final critico = politicaPara(
          PerfilDeDesempenho.automatico,
          const SinaisDeDesempenho(termico: EstadoTermico.critico),
        );
        expect(critico.escalaDaPrevia, lessThan(frio.escalaDaPrevia));
        expect(critico.niveisDoBrilho, lessThan(frio.niveisDoBrilho));
        expect(critico.niveisDoBrilho, greaterThanOrEqualTo(1));
        expect(critico.tetoDasFotosPx, lessThan(frio.tetoDasFotosPx));
      },
    );
  });
}
