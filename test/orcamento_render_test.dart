import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/orcamento_render.dart';
import 'package:flutter_test/flutter_test.dart';

/// O ORCAMENTO DE GPU E A CONTA QUE DECIDE O NIVEL ANTES DO PRIMEIRO
/// QUADRO. O que se fixa aqui e o que a auditoria achou no motor: o custo
/// por pixel dos alvos (HDR + MSAA), o atlas de sombra em RGBA32F, e o
/// que isso da em 1080p e em 4K num aparelho de 4 GB (o iPhone 13).
void main() {
  const mb = 1024 * 1024;
  const gb = 1024 * mb;

  const leve = PerfilDaCena(
    triangulos: 1200,
    vertices: 3600,
    chamadas: 100,
    texturas: 0,
    temPanoramaImagem: false,
    spotsComSombra: 0,
    direcionalComSombra: true,
    animada: false,
    emissiva: false,
    dofPedido: false,
  );

  const extrema = PerfilDaCena(
    triangulos: 1000000,
    vertices: 3000000,
    chamadas: 40,
    texturas: 6,
    temPanoramaImagem: false,
    spotsComSombra: 6,
    direcionalComSombra: true,
    animada: false,
    emissiva: true,
    dofPedido: false,
  );

  group('o orcamento do aparelho', () {
    test('um quinto da RAM, entre 320 MB e 2 GB', () {
      expect(orcamentoGpuBytes(4 * gb), closeTo(0.20 * 4 * gb, mb));
      expect(orcamentoGpuBytes(2 * gb), closeTo(0.22 * 2 * gb, mb));
      expect(orcamentoGpuBytes(16 * gb), 2048 * mb);
      expect(orcamentoGpuBytes(1 * gb), 320 * mb);
    });

    test('sem informacao, um aparelho modesto', () {
      expect(orcamentoGpuBytes(0), 512 * mb);
    });

    test('as faixas de pressao tem margem', () {
      expect(pressaoDaFracao(0.1), NivelDePressao.seguro);
      expect(pressaoDaFracao(0.5), NivelDePressao.alerta);
      expect(pressaoDaFracao(0.7), NivelDePressao.pressao);
      expect(pressaoDaFracao(0.85), NivelDePressao.emergencia);
    });
  });

  group('a conta', () {
    test('cada degrau da escada custa menos que o anterior', () {
      int custo(Qualidade3D q) {
        final r = ReceitaDeQualidade.de(q);
        final alvo = alvoDoPreview(1080, 1920, r);
        return estimarGpu(
          perfil: extrema,
          receita: r,
          larguraPx: alvo.largura,
          alturaPx: alvo.altura,
        ).total;
      }

      final custos = [for (final q in Qualidade3D.values) custo(q)];
      for (var i = 1; i < custos.length; i++) {
        expect(
          custos[i],
          lessThan(custos[i - 1]),
          reason:
              '${Qualidade3D.values[i]} nao e mais barato que ${Qualidade3D.values[i - 1]}',
        );
      }
    });

    test('MSAA 4x em 4K passa de um giga so em alvos de render', () {
      // A descoberta da auditoria: cor HDR de 8 bytes x 4 amostras, mais
      // profundidade, mais resolve, vezes dois quadros em voo. Era o
      // que uma exportacao 4K de cena 3D pedia ao iPhone 13.
      final e = estimarGpu(
        perfil: leve,
        receita: ReceitaDeQualidade.alta,
        larguraPx: 3840,
        alturaPx: 2160,
      );
      expect(e.alvosDeRender, greaterThan(950 * mb));
      final semMsaa = estimarGpu(
        perfil: leve,
        receita: ReceitaDeQualidade.media,
        larguraPx: 3840,
        alturaPx: 2160,
      );
      expect(semMsaa.alvosDeRender, lessThan(e.alvosDeRender ~/ 2));
    });

    test('cada spot com sombra e um tile inteiro do atlas', () {
      // Alvo de 1080 no lado maior: a sombra efetiva de "alta" e 1024.
      final sem = estimarGpu(
        perfil: leve,
        receita: ReceitaDeQualidade.alta,
        larguraPx: 1080,
        alturaPx: 1080,
      );
      final com = estimarGpu(
        perfil: extrema,
        receita: ReceitaDeQualidade.alta,
        larguraPx: 1080,
        alturaPx: 1080,
      );
      // alta: 2 cascatas + 2 spots (o teto) x 1024^2 x 20 bytes x 2 quadros.
      expect(com.sombras, 4 * 1024 * 1024 * 20 * 2);
      expect(sem.sombras, 2 * 1024 * 1024 * 20 * 2);
      expect(com.spotsComSombra, 2, reason: 'alta deixa dois spots com sombra');
      final emergencia = estimarGpu(
        perfil: extrema,
        receita: ReceitaDeQualidade.emergencia,
        larguraPx: 1080,
        alturaPx: 1080,
      );
      expect(emergencia.sombras, 0);
    });

    test('o teto de textura entra na conta', () {
      final ultra = estimarGpu(
        perfil: extrema,
        receita: ReceitaDeQualidade.ultra,
        larguraPx: 10,
        alturaPx: 10,
      );
      final baixa = estimarGpu(
        perfil: extrema,
        receita: ReceitaDeQualidade.baixa,
        larguraPx: 10,
        alturaPx: 10,
      );
      expect(ultra.texturas, greaterThan(baixa.texturas * 10));
    });

    test('o alvo do preview respeita escala e teto por nivel', () {
      // Retrato 1080x1920: o teto de 1080 vale no lado MAIOR (como o
      // preview sempre fez), entao o alvo e 608x1080.
      expect(alvoDoPreview(1080, 1920, ReceitaDeQualidade.alta), (
        largura: 608,
        altura: 1080,
      ));
      expect(alvoDoPreview(1080, 1920, ReceitaDeQualidade.baixa), (
        largura: 405,
        altura: 720,
      ));
      // Composicao 4K no preview: o teto de 1080 no lado maior manda.
      final quatroK = alvoDoPreview(3840, 2160, ReceitaDeQualidade.alta);
      expect(quatroK.largura, 1080);
      expect(quatroK.altura, 608);
      expect(alvoDoPreview(0, 0, ReceitaDeQualidade.alta), (
        largura: 0,
        altura: 0,
      ));
    });
  });

  group('a escolha pelo orcamento', () {
    final iphone13 = orcamentoGpuBytes(4 * gb);

    test('iPhone 13, cena comum no preview: o topo, com folga', () {
      // O alvo do preview e 608x1080; a sombra efetiva e 1024. Cabe.
      final r = escolherPeloOrcamento(
        perfil: leve,
        orcamentoBytes: iphone13,
        larguraPx: 1080,
        alturaPx: 1920,
      );
      expect(r.nivel.index, lessThanOrEqualTo(Qualidade3D.alta.index));
      expect(
        r.estimativa.total,
        lessThanOrEqualTo(iphone13 * fracaoSeguraDoOrcamento),
      );
    });

    test(
      'iPhone 13, a cena extrema: mais baixo que a comum, e ainda dentro',
      () {
        final comum = escolherPeloOrcamento(
          perfil: leve,
          orcamentoBytes: iphone13,
          larguraPx: 1080,
          alturaPx: 1920,
        );
        final r = escolherPeloOrcamento(
          perfil: extrema,
          orcamentoBytes: iphone13,
          larguraPx: 1080,
          alturaPx: 1920,
        );
        expect(r.nivel.index, greaterThan(comum.nivel.index));
        expect(
          r.estimativa.total,
          lessThanOrEqualTo(iphone13 * fracaoSeguraDoOrcamento),
        );
      },
    );

    test('aparelho de 2 GB: a cena extrema desce ate baixa, sem crash', () {
      final r = escolherPeloOrcamento(
        perfil: extrema,
        orcamentoBytes: orcamentoGpuBytes(2 * gb),
        larguraPx: 1080,
        alturaPx: 1920,
      );
      expect(r.nivel.index, greaterThanOrEqualTo(Qualidade3D.media.index));
      expect(
        r.estimativa.total,
        lessThanOrEqualTo(orcamentoGpuBytes(2 * gb) * fracaoSeguraDoOrcamento),
      );
    });

    test('a sombra efetiva nao passa do que o alvo aproveita', () {
      expect(sombraEfetiva(ReceitaDeQualidade.ultra, 1080), 1024);
      expect(sombraEfetiva(ReceitaDeQualidade.ultra, 2160), 2048);
      expect(sombraEfetiva(ReceitaDeQualidade.ultra, 640), 512);
      expect(sombraEfetiva(ReceitaDeQualidade.baixa, 2160), 512);
      expect(sombraEfetiva(ReceitaDeQualidade.emergencia, 2160), 0);
      expect(
        sombraEfetiva(ReceitaDeQualidade.ultra, 0),
        2048,
        reason: 'sem alvo ainda: a da receita',
      );
    });

    test('o teto da pessoa vale acima do orcamento', () {
      final r = escolherPeloOrcamento(
        perfil: leve,
        orcamentoBytes: orcamentoGpuBytes(12 * gb),
        larguraPx: 1080,
        alturaPx: 1920,
        teto: Qualidade3D.baixa,
      );
      expect(r.nivel, Qualidade3D.baixa);
    });

    test('quando nada cabe, e emergencia — e o motor desenha mesmo assim', () {
      final r = escolherPeloOrcamento(
        perfil: extrema,
        orcamentoBytes: 32 * mb,
        larguraPx: 1080,
        alturaPx: 1920,
      );
      expect(r.nivel, Qualidade3D.emergencia);
      expect(r.estimativa.total, greaterThan(0));
    });
  });

  group('a exportacao', () {
    test('4K no iPhone 13: mantem a resolucao, larga o MSAA', () {
      final r = receitaDeExportacao(
        perfil: leve,
        orcamentoBytes: orcamentoGpuBytes(4 * gb),
        larguraPx: 3840,
        alturaPx: 2160,
      );
      expect(r.escala, 1.0, reason: 'a resolucao da exportacao e sagrada');
      expect(
        r.nivel.index,
        greaterThanOrEqualTo(Qualidade3D.media.index),
        reason: 'MSAA em 4K nao cabe: e o que passava de um giga',
      );
      expect(
        r.estimativa.total,
        lessThanOrEqualTo(orcamentoGpuBytes(4 * gb) * fracaoSeguraDaExportacao),
      );
    });

    test('1080p no iPhone 13: a receita cheia', () {
      final r = receitaDeExportacao(
        perfil: leve,
        orcamentoBytes: orcamentoGpuBytes(4 * gb),
        larguraPx: 1080,
        alturaPx: 1920,
      );
      expect(r.nivel.index, lessThanOrEqualTo(Qualidade3D.alta.index));
      expect(r.escala, 1.0);
    });

    test(
      'so quando nem a emergencia cabe a escala desce — e desce ate caber',
      () {
        final r = receitaDeExportacao(
          perfil: extrema,
          orcamentoBytes: orcamentoGpuBytes(2 * gb),
          larguraPx: 7680,
          alturaPx: 4320,
        );
        expect(r.nivel, Qualidade3D.emergencia);
        expect(r.escala, lessThan(1.0));
        expect(r.escala, greaterThanOrEqualTo(0.3));
      },
    );
  });

  test('bytes legiveis', () {
    expect(bytesLegiveis(512), '512 B');
    expect(bytesLegiveis(3 * 1024), '3 kB');
    expect(bytesLegiveis(300 * mb), '300 MB');
    expect(bytesLegiveis(3 * gb), '3.00 GB');
  });

  test('o alvo da cena 3D no preview tem o mesmo tamanho tocando e parado', () {
    // O motor recria todas as texturas de trabalho quando o tamanho do
    // alvo muda. A escala depende da area, da receita e da resolucao de
    // previa dos Ajustes — e de mais nada.
    for (final nivel in Qualidade3D.values) {
      final receita = ReceitaDeQualidade.de(nivel);
      for (final (l, a) in const [
        (1920.0, 1080.0),
        (1080.0, 1920.0),
        (640.0, 480.0),
      ]) {
        final e = escalaDoAlvoDoPreview3D(l, a, receita, 1);
        expect(e, greaterThan(0));
        expect(math.max(l, a) * e, lessThanOrEqualTo(1080.0001));
        expect(e, lessThanOrEqualTo(escalaDoPreview(l, a, receita)));
        expect(
          escalaDoAlvoDoPreview3D(l, a, receita, .5),
          closeTo(e * .5, 1e-12),
        );
      }
    }
    expect(
      escalaDoAlvoDoPreview3D(
        double.nan,
        10,
        ReceitaDeQualidade.de(Qualidade3D.alta),
        1,
      ),
      lessThanOrEqualTo(1),
    );
  });
}
