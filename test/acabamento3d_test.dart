// O ACABAMENTO DOS MODELOS 3D — o sistema de materiais do texto 3D.
//
// O QUE ESTES TESTES SEGURAM, e por que cada um existe:
//
//   * `corLisa` E O COMPORTAMENTO DE SEMPRE. Um projeto salvo antes
//     disto tem de abrir identico, e o teste compara o material com o
//     que o pintor usava (cor da camada, sem metal);
//   * OS METAIS SAO OS DO TEXTO 3D, e nao uma copia. Se alguem ajustar o
//     ouro do texto e esquecer o do elemento, o teste cai — que e o
//     ponto: "aparencia consistente" e um invariante, e nao um acordo;
//   * A RUGOSIDADE MUDA O DESENHO DO BRILHO. Sem isso ela seria um
//     numero guardado que ninguem ve;
//   * O AMBIENTE E O QUE DA A COR AO METAL. Um cromo no estudio e um
//     cromo na noite NAO podem sair iguais.
import 'package:aurea/src/features/editor/domain/acabamento3d.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// A COR DE UMA FACE QUALQUER, para comparar dois materiais.
Color _face(
  AcabamentoDoElemento3D a, {
  EnvironmentKind ambiente = EnvironmentKind.estudio,
  double nx = 0,
  double ny = 0,
  double nz = -1,
  Color cor = const Color(0xFF7C62FF),
}) => corDaFaceDoElemento3D(
  material: materialDoElemento3D(a, cor),
  ambiente: ambiente,
  nx: nx,
  ny: ny,
  nz: nz,
);

void main() {
  group('corLisa e o de sempre', () {
    test('sem metal, com a cor da camada', () {
      const cor = Color(0xFF7C62FF);
      final m = materialDoElemento3D(AcabamentoDoElemento3D.corLisa, cor);
      expect(m.cor, cor);
      expect(m.metalico, 0);
    });

    test('o sombreamento legado usa o MODULO: frente e costas empatam', () {
      // O PINTOR ANTIGO SOMBREAVA COM `0.34 + 0.66 * |dot|` — o modulo.
      // Uma face de costas para a luz saia tao clara quanto uma de frente,
      // e um projeto salvo antes disto conta com isso. Este teste existe
      // para que trocar por `max(0, dot)` NAO passe despercebido: o
      // resultado seria todo solido ja existente mais escuro.
      Color face(double s) => _face(
        AcabamentoDoElemento3D.corLisa,
        nx: luzX * s,
        ny: luzY * s,
        nz: luzZ * s,
      );
      expect(
        face(1).computeLuminance(),
        closeTo(face(-1).computeLuminance(), 1e-9),
      );
      // E de perfil (dot zero) sai MAIS ESCURA que de frente.
      final perfil = _face(
        AcabamentoDoElemento3D.corLisa,
        nx: -luzY,
        ny: luzX,
        nz: 0,
      );
      expect(
        face(1).computeLuminance(),
        greaterThan(perfil.computeLuminance()),
      );
    });

    test('o metal ESCURECE de costas para a luz — e o que o legado nao faz', () {
      final metal = materialDoElemento3D(
        AcabamentoDoElemento3D.acoEscovado,
        const Color(0xFF000000),
      );
      expect(metal.sombreamentoLegado, isFalse);
      final frente = corDaFaceDoElemento3D(
        material: metal,
        ambiente: EnvironmentKind.estudio,
        nx: luzX,
        ny: luzY,
        nz: luzZ,
      );
      final costas = corDaFaceDoElemento3D(
        material: metal,
        ambiente: EnvironmentKind.estudio,
        nx: -luzX,
        ny: -luzY,
        nz: luzZ,
      );
      expect(
        frente.computeLuminance(),
        greaterThan(costas.computeLuminance()),
      );
    });
  });

  group('os metais sao OS DO TEXTO 3D', () {
    test('cada acabamento le o material do estilo correspondente', () {
      for (final a in AcabamentoDoElemento3D.values) {
        final estilo = estiloDoAcabamento(a);
        if (estilo == null) {
          expect(a, AcabamentoDoElemento3D.corLisa);
          continue;
        }
        final doTexto = materiaisDoTexto3D(estilo).first;
        final m = materialDoElemento3D(a, const Color(0xFF000000));
        final c = (doTexto['color'] as List).cast<num>();
        expect(m.cor.r, closeTo(c[0].toDouble(), 1e-9), reason: '$a');
        expect(m.cor.g, closeTo(c[1].toDouble(), 1e-9), reason: '$a');
        expect(m.cor.b, closeTo(c[2].toDouble(), 1e-9), reason: '$a');
        expect(m.metalico, (doTexto['metallic'] as num).toDouble(),
            reason: '$a');
        expect(m.rugosidade, (doTexto['roughness'] as num).toDouble(),
            reason: '$a');
      }
    });

    test('o ouro e o cromo sao metais; o branco fosco nao', () {
      // E o que separa "metal" de "plastico claro": metalico zero no
      // branco fosco e o que faz ele difundir em vez de espelhar.
      expect(
        materialDoElemento3D(
          AcabamentoDoElemento3D.ouro,
          const Color(0xFF000000),
        ).metalico,
        1,
      );
      expect(
        materialDoElemento3D(
          AcabamentoDoElemento3D.brancoFosco,
          const Color(0xFF000000),
        ).metalico,
        0,
      );
    });

    test('o cromo e mais liso que o aco escovado', () {
      final cromo = materialDoElemento3D(
        AcabamentoDoElemento3D.cromo,
        const Color(0xFF000000),
      );
      final aco = materialDoElemento3D(
        AcabamentoDoElemento3D.acoEscovado,
        const Color(0xFF000000),
      );
      expect(cromo.rugosidade, lessThan(aco.rugosidade));
      // E O EXPOENTE ESPECULAR SEGUE A RUGOSIDADE: mais liso, ponto de
      // luz mais apertado.
      expect(cromo.expoenteEspecular, greaterThan(aco.expoenteEspecular));
    });

    test('o expoente tem teto e piso (nao estoura nem zera)', () {
      for (final r in [0.0, 0.02, 0.5, 1.0, 2.0]) {
        final m = MaterialDoElemento3D(
          cor: const Color(0xFFFFFFFF),
          metalico: 1,
          rugosidade: r,
        );
        expect(m.expoenteEspecular, greaterThanOrEqualTo(2));
        expect(m.expoenteEspecular, lessThanOrEqualTo(4096));
        expect(m.expoenteEspecular.isFinite, isTrue);
      }
    });
  });

  group('o ambiente e que da a cor ao metal', () {
    test('o mesmo cromo muda de cor conforme o mapa', () {
      // ESTE E O TESTE QUE PROVA QUE O REFLEXO NAO E DECORATIVO. Um cromo
      // no estudio e um cromo na noite nao podem sair iguais — se saissem,
      // o mapa de ambiente nao estaria sendo lido.
      final estudio = _face(AcabamentoDoElemento3D.cromo);
      final noite = _face(
        AcabamentoDoElemento3D.cromo,
        ambiente: EnvironmentKind.noite,
      );
      expect(
        (estudio.r - noite.r).abs() +
            (estudio.g - noite.g).abs() +
            (estudio.b - noite.b).abs(),
        greaterThan(0.05),
        reason: 'o ambiente nao chegou na cor da face',
      );
    });

    test('a cor lisa NAO depende do ambiente — e o comportamento antigo', () {
      // O reflexo legado (`reflect` na camada) continua sendo quem
      // responde pelo ambiente na cor lisa, e nao o material. Misturar os
      // dois contaria a mesma luz duas vezes.
      final a = _face(AcabamentoDoElemento3D.corLisa);
      final b = _face(
        AcabamentoDoElemento3D.corLisa,
        ambiente: EnvironmentKind.neon,
      );
      expect((a.r - b.r).abs(), lessThan(0.02));
      expect((a.g - b.g).abs(), lessThan(0.02));
      expect((a.b - b.b).abs(), lessThan(0.02));
    });

    test('a rugosidade espalha o sol refletido', () {
      // Com o mapa de estudio (que tem um softbox forte), o cromo liso
      // concentra o reflexo num ponto e o fosco o abre. As duas faces
      // abaixo estao FORA do ponto de brilho especular, entao a diferenca
      // que sobra vem do ambiente.
      final liso = _face(
        AcabamentoDoElemento3D.cromo,
        nx: -0.6,
        ny: 0.6,
        nz: -0.5,
      );
      final fosco = _face(
        AcabamentoDoElemento3D.brancoFosco,
        nx: -0.6,
        ny: 0.6,
        nz: -0.5,
      );
      expect(liso.computeLuminance(), isNot(closeTo(fosco.computeLuminance(), 0.001)));
    });

    test('a cor da camada NAO tinge o metal', () {
      // Um ouro continua ouro, qualquer que seja a cor da camada: e o que
      // faz "ouro" ser um acabamento, e nao uma cor.
      final a = _face(AcabamentoDoElemento3D.ouro, cor: const Color(0xFFFF0000));
      final b = _face(AcabamentoDoElemento3D.ouro, cor: const Color(0xFF00FF00));
      expect((a.r - b.r).abs(), lessThan(0.02));
      expect((a.g - b.g).abs(), lessThan(0.02));
      expect((a.b - b.b).abs(), lessThan(0.02));
    });
  });

  group('o nome no arquivo', () {
    test('todo acabamento vai e volta pelo nome', () {
      for (final a in AcabamentoDoElemento3D.values) {
        expect(acabamentoPorNome(nomeDoAcabamentoArquivo(a)), a);
      }
    });

    test('um nome desconhecido cai no neutro, e nao em erro', () {
      // Projeto de uma versao mais nova abre com o acabamento de sempre
      // em vez de nao abrir.
      expect(acabamentoPorNome(null), AcabamentoDoElemento3D.corLisa);
      expect(
        acabamentoPorNome('titânioPoliido'),
        AcabamentoDoElemento3D.corLisa,
      );
    });
  });

  group('os modelos novos', () {
    test('os quatro desenham, com faces e vertices', () {
      for (final k in const [
        Element3DKind.lente,
        Element3DKind.anelDeLuz,
        Element3DKind.diafragma,
        Element3DKind.placa,
      ]) {
        final m = element3DMesh(k);
        expect(m.verts.length, greaterThan(12), reason: '$k');
        expect(m.faces.length, greaterThan(8), reason: '$k');
        // TODA FACE APONTA PARA VERTICE QUE EXISTE: um indice fora da
        // lista nao quebra na montagem — quebra no meio do desenho, com
        // a excecao chegando na arvore de widgets.
        for (final f in m.faces) {
          expect(f.length, greaterThanOrEqualTo(3), reason: '$k');
          for (final i in f) {
            expect(i, greaterThanOrEqualTo(0), reason: '$k');
            expect(i, lessThan(m.verts.length), reason: '$k');
          }
        }
      }
    });

    test('a malha e a MESMA em duas chamadas (o cache e por tipo)', () {
      expect(identical(element3DMesh(Element3DKind.lente),
          element3DMesh(Element3DKind.lente)), isTrue);
    });

    test('os numeros dos tipos antigos NAO MUDARAM', () {
      // O ARQUIVO GRAVA O NOME, mas uma versao anterior le o INDICE. Se um
      // tipo novo entrasse no meio da lista, todo projeto salvo abriria
      // com outra forma naquela versao.
      expect(Element3DKind.cube.index, 0);
      expect(Element3DKind.crownFine.index, 16);
      expect(Element3DKind.lente.index, greaterThan(16));
    });

    test('todo tipo tem rotulo e nome de arquivo, e o nome e unico', () {
      final nomes = <String>{};
      for (final k in Element3DKind.values) {
        expect(element3DLabel(k), isNotEmpty);
        final nome = nomeDoElemento3D(k);
        expect(nome, isNotEmpty);
        expect(nomes.add(nome), isTrue, reason: 'nome repetido: $nome');
        expect(elemento3DPorNome(nome), k);
      }
    });

    test('um nome desconhecido devolve nulo, e nao um tipo qualquer', () {
      expect(elemento3DPorNome('naoExiste'), isNull);
      expect(elemento3DPorNome(null), isNull);
    });
  });
}
