// AS CONTAS DO PREVIEW EM RASCUNHO: interacao, fotos, alvo 3D — e a
// exportacao imune a tudo isso.
import 'package:aurea/src/features/editor/domain/orcamento_render.dart';
import 'package:aurea/src/features/editor/presentation/widgets/rascunho_do_preview.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('emRascunho', () {
    test('liga tocando, liga interagindo, e nunca exportando', () {
      expect(emRascunho(exporting: false, tocando: false, interagindo: false), isFalse);
      expect(emRascunho(exporting: false, tocando: true, interagindo: false), isTrue);
      expect(emRascunho(exporting: false, tocando: false, interagindo: true), isTrue);
      // A EXPORTACAO E IMUNE: com o relogio andando E o dedo no comando.
      expect(emRascunho(exporting: true, tocando: true, interagindo: true), isFalse);
    });
  });

  group('fotosDoPreview', () {
    test('interagindo: metade da escala e teto 1080; parado: tudo inteiro', () {
      final parado = fotosDoPreview(escalaDoPalco: 1, tocando: false, interagindo: false);
      expect(parado.escalaDoPalco, 1);
      expect(parado.tetoPx, 2160);
      final dedo = fotosDoPreview(escalaDoPalco: 1, tocando: false, interagindo: true);
      expect(dedo.escalaDoPalco, .5);
      expect(dedo.tetoPx, 1080);
      final play = fotosDoPreview(escalaDoPalco: 1, tocando: true, interagindo: false);
      expect(play.escalaDoPalco, 1, reason: 'tocando so o teto cai');
      expect(play.tetoPx, 1080);
    });
  });

  group('alvo3DDoPreview', () {
    final alta = ReceitaDeQualidade.de(Qualidade3D.alta);

    test('o tamanho fisico do palco limita o alvo, em multiplos de 64', () {
      // Composicao 1080x1920 num palco que mostra 0,3 px fisico por px
      // logico (uns 324x576): o alvo nao e mais o 608x1080 da receita.
      final alvo = alvo3DDoPreview(
        compLargura: 1080,
        compAltura: 1920,
        receita: alta,
        escalaFisica: .3,
      );
      // ignore: avoid_print
      print('ALVO 3D a 0,3 de escala fisica: ${alvo.largura}x${alvo.altura}');
      expect(alvo.altura % 64, 0, reason: 'lado maior em multiplos de 64');
      expect(alvo.altura, 576);
      expect(alvo.largura, 324);
      expect(alvo.altura, lessThan(1080));
    });

    test('a receita continua sendo o teto quando o palco e grande', () {
      final alvo = alvo3DDoPreview(
        compLargura: 1080,
        compAltura: 1920,
        receita: alta,
        escalaFisica: 3,
      );
      expect(alvo.altura, 1088, reason: '1080 da receita, subido a 64');
    });

    test('histerese: uma pinca de 10% nao troca o alvo, 30% troca', () {
      final base = alvo3DDoPreview(
        compLargura: 1080,
        compAltura: 1920,
        receita: alta,
        escalaFisica: .3,
      );
      final pinca = alvo3DDoPreview(
        compLargura: 1080,
        compAltura: 1920,
        receita: alta,
        escalaFisica: .33,
        anterior: base,
      );
      expect(pinca, base, reason: 'o motor nao recria alvos por 10%');
      final grande = alvo3DDoPreview(
        compLargura: 1080,
        compAltura: 1920,
        receita: alta,
        escalaFisica: .4,
        anterior: base,
      );
      expect(grande, isNot(base));
      expect(grande.altura, greaterThan(base.altura));
    });

    test('segurando (dedo no comando) o alvo anterior fica', () {
      final base = alvo3DDoPreview(
        compLargura: 1080,
        compAltura: 1920,
        receita: alta,
        escalaFisica: .3,
      );
      final seguro = alvo3DDoPreview(
        compLargura: 1080,
        compAltura: 1920,
        receita: alta,
        escalaFisica: .15,
        anterior: base,
        segurar: true,
      );
      expect(seguro, base);
    });

    test('sem tamanho nao ha alvo', () {
      expect(
        alvo3DDoPreview(compLargura: 0, compAltura: 10, receita: alta, escalaFisica: 1),
        (largura: 0, altura: 0),
      );
    });
  });

  group('qualidade3DDoPreview', () {
    test('interagindo a sombra desce um nivel e o MSAA fica', () {
      final q = qualidade3DDoPreview(sombra: 3, amostras: 4, interagindo: true);
      expect(q.sombra, 2);
      expect(q.amostras, 4, reason: 'trocar MSAA recria o alvo no motor');
      expect(
        qualidade3DDoPreview(sombra: 0, amostras: 4, interagindo: true).sombra,
        0,
      );
      final parado = qualidade3DDoPreview(sombra: 3, amostras: 4, interagindo: false);
      expect(parado.sombra, 3);
    });
  });
}
