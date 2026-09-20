import 'dart:typed_data';

import 'package:aurea_render/aurea_render.dart';
import 'package:flutter_test/flutter_test.dart';

/// A ABI DA PORTA 3D — O TAMANHO DE CADA STRUCT E A COPIA DE CADA CAMPO.
///
/// POR QUE ISTO E UM TESTE E NAO UM COMENTARIO: uma struct do lado Dart
/// que nao bate byte a byte com a do C++ NAO AVISA. Ela nao falha, nao
/// estoura, nao aparece no log — ela le o campo do vizinho e devolve um
/// numero plausivel. O modelo aparece deslocado, ou nao aparece, e a
/// causa fica a tres camadas de distancia do sintoma.
///
/// OS NUMEROS DE BAIXO SAO OS `static_assert` DO `api_3d.cpp`, escritos de
/// novo aqui de proposito: se alguem mexer nos dois lados ao mesmo tempo,
/// o teste continua pegando — e a unica coisa que pega.
void main() {
  group('tamanhos das structs', () {
    // A ORDEM E A DO ENUM `Aurea3DTamanho`.
    const esperado = <String, int>{
      'camera': 72,
      'material': 40,
      'camada': 128,
      'luz': 60,
      // O CEU, O CHAO, A FORCA DO REFLEXO E O MAPA DE AMBIENTE (ponteiro,
      // largura e niveis). Em 64 bits, 176; o `static_assert` do
      // `api_3d.cpp` diz 164 em 32 bits, e o teste roda em 64.
      'cena': 176,
      'relato': 56,
      'ficha': 80,
      'opcoes': 24,
      // OS CINCO MAPAS DO PBR entraram no fim da malha crua: cinco
      // ponteiros, dez tamanhos e a forca da oclusao. 80 -> 168.
      'malhaCrua': 168,
    };

    test('o Dart declara cada struct do tamanho do C++', () {
      final doDart = Motor3D.tamanhosDoDart;
      expect(doDart.length, esperado.length);
      var i = 0;
      for (final entrada in esperado.entries) {
        expect(
          doDart[i],
          entrada.value,
          reason: 'a struct ${entrada.key} do lado Dart tem '
              '${doDart[i]} bytes e o C++ espera ${entrada.value}',
        );
        i++;
      }
    });

    test('o enum de tamanho tem a mesma ordem dos dois lados', () {
      expect(Tamanho3D.camera, 0);
      expect(Tamanho3D.material, 1);
      expect(Tamanho3D.camada, 2);
      expect(Tamanho3D.luz, 3);
      expect(Tamanho3D.cena, 4);
      expect(Tamanho3D.relato, 5);
      expect(Tamanho3D.ficha, 6);
      expect(Tamanho3D.opcoes, 7);
      // A MALHA CRUA ENTROU NO FIM DA LISTA, e nao no meio: os oito
      // primeiros indices ja estavam compilados do outro lado, e mexer
      // neles mudaria o significado de um numero que ja existe.
      expect(Tamanho3D.malhaCrua, 8);
      expect(Tamanho3D.quantos, 9);
    });

    test('a biblioteca compilada concorda com o Dart', () {
      if (!Motor3D.disponivel) {
        // O MOTOR 3D NAO E COMPILADO NO PC — ver `Motor3D.disponivel`. O
        // teste acima ja provou o que da para provar aqui.
        return;
      }
      expect(Motor3D.tamanhos, Motor3D.tamanhosDoDart);
    });
  });

  group('as copias', () {
    // A `copiarDe` E O CAMINHO DE TODO QUADRO: a camada da timeline e
    // copiada para a camada da cena sessenta vezes por segundo. Um campo
    // esquecido nela nao da erro nenhum — ele simplesmente nunca chega ao
    // motor, e o controle correspondente fica sem efeito na tela.

    test('a camera copia campo por campo', () {
      final a = CameraDeCena3D();
      final b = CameraDeCena3D()
        ..posicaoX = 1
        ..posicaoY = 2
        ..posicaoZ = 3
        ..alvoX = 4
        ..alvoY = 5
        ..alvoZ = 6
        ..rotacaoX = 7
        ..rotacaoY = 8
        ..rotacaoZ = 9
        ..usarRotacao = true
        ..cimaX = 0
        ..cimaY = 0
        ..cimaZ = 1
        ..fovGraus = 60
        ..perto = 0.1
        ..longe = 900
        ..ortografica = true
        ..alturaOrtografica = 7;
      a.copiarDe(b);
      expect(a.posicaoX, 1);
      expect(a.posicaoY, 2);
      expect(a.posicaoZ, 3);
      expect(a.alvoX, 4);
      expect(a.alvoY, 5);
      expect(a.alvoZ, 6);
      expect(a.rotacaoX, 7);
      expect(a.rotacaoY, 8);
      expect(a.rotacaoZ, 9);
      expect(a.usarRotacao, isTrue);
      expect(a.cimaX, 0);
      expect(a.cimaY, 0);
      expect(a.cimaZ, 1);
      expect(a.fovGraus, 60);
      expect(a.perto, 0.1);
      expect(a.longe, 900);
      expect(a.ortografica, isTrue);
      expect(a.alturaOrtografica, 7);
    });

    test('a luz copia campo por campo', () {
      final a = LuzDeCena3D();
      final b = LuzDeCena3D()
        ..tipo = TipoDeLuz3D.holofote
        ..posicaoX = 1
        ..posicaoY = 2
        ..posicaoZ = 3
        ..direcaoX = 4
        ..direcaoY = 5
        ..direcaoZ = 6
        ..corR = 0.1
        ..corG = 0.2
        ..corB = 0.3
        ..intensidade = 8
        ..alcance = 12
        ..anguloInternoGraus = 15
        ..anguloExternoGraus = 40
        ..ligada = false;
      a.copiarDe(b);
      expect(a.tipo, TipoDeLuz3D.holofote);
      expect(a.posicaoX, 1);
      expect(a.posicaoY, 2);
      expect(a.posicaoZ, 3);
      expect(a.direcaoX, 4);
      expect(a.direcaoY, 5);
      expect(a.direcaoZ, 6);
      expect(a.corR, 0.1);
      expect(a.corG, 0.2);
      expect(a.corB, 0.3);
      expect(a.intensidade, 8);
      expect(a.alcance, 12);
      expect(a.anguloInternoGraus, 15);
      expect(a.anguloExternoGraus, 40);
      expect(a.ligada, isFalse);
    });

    test('o material copia campo por campo, inclusive o -1', () {
      final a = MaterialDaCamada3D();
      final b = MaterialDaCamada3D()
        ..ligado = true
        ..corBase = const Cor3D(10, 20, 30, 40)
        ..metalico = 0.75
        ..rugosidade = 0.25
        ..forcaEmissiva = 2
        ..emissivo = const Cor3D(1, 2, 3, 4)
        ..modo = 2
        ..faceDupla = true
        ..alfaCorte = 0.5
        ..semTexturaDeCor = true;
      a.copiarDe(b);
      expect(a.ligado, isTrue);
      expect(a.corBase.r, 10);
      expect(a.corBase.g, 20);
      expect(a.corBase.b, 30);
      expect(a.corBase.a, 40);
      expect(a.metalico, 0.75);
      expect(a.rugosidade, 0.25);
      expect(a.forcaEmissiva, 2);
      expect(a.emissivo.r, 1);
      expect(a.emissivo.g, 2);
      expect(a.emissivo.b, 3);
      expect(a.emissivo.a, 4);
      expect(a.modo, 2);
      expect(a.faceDupla, isTrue);
      expect(a.alfaCorte, 0.5);
      expect(a.semTexturaDeCor, isTrue);

      // E O QUE E O PADRAO: "-1 = o que o modelo traz". Se o padrao virar
      // zero, mexer em qualquer controle apaga o material do arquivo.
      final novo = MaterialDaCamada3D();
      expect(novo.metalico, -1);
      expect(novo.rugosidade, -1);
      expect(novo.forcaEmissiva, -1);
      expect(novo.alfaCorte, -1);
      expect(novo.modo, -1);
    });

    test('a camada copia o material dela junto', () {
      final a = CamadaDeCena3D();
      final b = CamadaDeCena3D()
        ..alca = 9
        ..modelo = 3
        ..animacao = 1
        ..tempoDaAnimacao = 2.5
        ..visivel = false
        ..posicaoX = 1
        ..rotacaoY = 30
        ..escalaZ = 2
        ..ancoraX = 0
        ..opacidade = 0.5
        ..cor = const Cor3D(1, 2, 3, 4)
        ..camadaZ = -7;
      b.material
        ..ligado = true
        ..metalico = 0.9;
      a.copiarDe(b);
      expect(a.alca, 9);
      expect(a.modelo, 3);
      expect(a.animacao, 1);
      expect(a.tempoDaAnimacao, 2.5);
      expect(a.visivel, isFalse);
      expect(a.posicaoX, 1);
      expect(a.rotacaoY, 30);
      expect(a.escalaZ, 2);
      expect(a.ancoraX, 0);
      expect(a.opacidade, 0.5);
      expect(a.cor.b, 3);
      expect(a.camadaZ, -7);
      expect(a.material.ligado, isTrue);
      expect(a.material.metalico, 0.9);
    });
  });

  group('a cena', () {
    test('nasce sem camada e sem luz, e o limpar esvazia', () {
      final c = Cena3D();
      expect(c.camadas, isEmpty);
      expect(c.luzes, isEmpty);
      c.camadas.add(CamadaDeCena3D());
      c.luzes.add(LuzDeCena3D());
      c.limpar();
      expect(c.camadas, isEmpty);
      expect(c.luzes, isEmpty);
    });

    test('o padrao da camera enxerga a origem de longe', () {
      final c = CameraDeCena3D();
      expect(c.posicaoZ, 5);
      expect(c.fovGraus, 45);
      expect(c.perto, lessThan(c.longe));
      expect(c.ortografica, isFalse);
      // Um `cima` degenerado derruba a matriz de vista. O padrao nao pode
      // ser zero em lugar nenhum.
      expect(c.cimaX == 0 && c.cimaY == 0 && c.cimaZ == 0, isFalse);
    });

    test('a luz ambiente nasce com um pouco de luz nos tres canais', () {
      final c = Cena3D();
      expect(c.ambienteR, greaterThan(0));
      expect(c.ambienteG, greaterThan(0));
      expect(c.ambienteB, greaterThan(0));
    });
  });

  group('a malha crua', () {
    // ESTE E O CAMINHO DO CUBO — e o de tudo o que o app ja constroi sem
    // arquivo. As recusas sao testaveis sem motor nenhum, porque acontecem
    // ANTES da chamada nativa.

    test('uma lista vazia e recusada como argumento', () {
      final r = Motor3D.criarModelo(const <MalhaCrua3D>[]);
      expect(r.deuCerto, isFalse);
      expect(r.nomeDoErro, 'argumento');
      expect(r.alca, 0);
    });

    test('posicoes curtas demais sao recusadas antes de tudo', () {
      // QUATRO VERTICES DECLARADOS, TRES NO VETOR: ler o quarto seria ler
      // memoria alheia. A conferencia acontece no Dart, antes do `ffi`.
      final r = Motor3D.criarModelo([
        MalhaCrua3D(
          posicoes: Float32List(9),
          indices: Uint32List.fromList(<int>[0, 1, 2]),
          quantidadeDeVertices: 4,
        ),
      ]);
      expect(r.deuCerto, isFalse);
      expect(r.nomeDoErro, 'argumento');
    });

    test('indices curtos demais sao recusados', () {
      final r = Motor3D.criarModelo([
        MalhaCrua3D(
          posicoes: Float32List(9),
          indices: Uint32List.fromList(<int>[0, 1, 2]),
          quantidadeDeVertices: 3,
          quantidadeDeIndices: 6,
        ),
      ]);
      expect(r.deuCerto, isFalse);
      expect(r.nomeDoErro, 'argumento');
    });

    test('a malha conta os proprios vertices quando nao se diz', () {
      final m = MalhaCrua3D(
        posicoes: Float32List(36), // doze vertices
        indices: Uint32List.fromList(<int>[0, 1, 2]),
      );
      expect(m.quantidadeDeVertices, 12);
      expect(m.quantidadeDeIndices, 3);
    });

    test('o material nasce com os padroes do glTF', () {
      final m = MalhaCrua3D(
        posicoes: Float32List(9),
        indices: Uint32List.fromList(<int>[0, 1, 2]),
      );
      expect(m.corBase.r, 255);
      expect(m.corBase.a, 255);
      expect(m.modo, 0); // opaco
      expect(m.faceDupla, isFalse);
      expect(m.alfaCorte, 0.5); // o padrao do glTF para MASK
      // SEM NORMAIS QUER DIZER "CALCULE A PLANA", e nao "nao ha normal".
      expect(m.normais, isNull);
    });
  });

  group('a ausencia do motor', () {
    test('perguntar pelo motor sem biblioteca nao estoura', () {
      // ESTE E O CAMINHO DO PC, onde o Diligent e o Assimp nao sao
      // compilados. Se a sonda levantasse, TODO teste que importa o pacote
      // morreria — e nao e isso que "nao tem motor 3D aqui" quer dizer.
      expect(() => Motor3D.disponivel, returnsNormally);
      expect(() => Motor3D.versao, returnsNormally);
      expect(() => Motor3D.tamanhos, returnsNormally);
      if (!Motor3D.disponivel) {
        expect(Motor3D.versao, -1);
        expect(Motor3D.tamanhos, isEmpty);
      }
    });
  });
}
