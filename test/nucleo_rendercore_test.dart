import 'dart:typed_data';

import 'package:aurea_render/aurea_render.dart';
import 'package:flutter_test/flutter_test.dart';

/// O RENDERCORE C++ — ABI, composicao, recursos, shaders e quadro.
///
/// TODOS OS NUMEROS DESTE TESTE SAO MEDIDOS, e nao estimados: a cor lida
/// e a cor que o rasterizador escreveu, o tempo e o que o relogio contou,
/// a memoria e a que o gerenciador diz ter. Nao ha "deve dar mais ou
/// menos" em lugar nenhum.
///
/// O BACKEND E O DE REFERENCIA (CPU). E o que existe hoje: Metal e Vulkan
/// ainda nao foram escritos, e pedir GPU ao nucleo devolve `null` em vez
/// de um motor que diz ser o que nao e — ha teste para isso tambem.
void main() {
  setUpAll(() {
    expect(
      NucleoRender.disponivel,
      isTrue,
      reason: 'a biblioteca do RenderCore nao carregou',
    );
  });

  /// Um nucleo pequeno e deterministico: sem thread, escala cheia, UMA
  /// amostra por pixel e a QUALIDADE AUTOMATICA DESLIGADA. Com o controle
  /// ligado, um quadro mais lento baixaria o nivel no meio do teste e o
  /// alvo mudaria de tamanho — o teste de cor exata passaria a medir
  /// outra coisa. O controle tem teste proprio.
  NucleoRender abrir({int largura = 16, int altura = 16, int orcamento = 0}) {
    final n = NucleoRender.abrir(
      largura: largura,
      altura: altura,
      orcamentoDeRecursos: orcamento,
      comThread: false,
      amostras: 1,
      escalaInterna: 1.0,
    );
    expect(n, isNotNull, reason: 'o nucleo nao abriu');
    n!.definirAutomatica(false);
    addTearDown(n.fechar);
    return n;
  }

  /// O PIXEL (x, y) DO ULTIMO QUADRO, como a pessoa veria.
  List<int> pixel(NucleoRender n, int x, int y, int largura) {
    final p = n.lerPixels()!;
    final i = (y * largura + x) * 4;
    return [p[i], p[i + 1], p[i + 2], p[i + 3]];
  }

  CamadaDeRender corCobrindo(int w, int h, int cor) => CamadaDeRender(
    x: w / 2,
    y: h / 2,
    largura: w.toDouble(),
    altura: h.toDouble(),
    cor: cor,
  );

  group('ABI', () {
    test('as structs tem o mesmo tamanho nos dois lados', () {
      // UM `Struct` MAL DECLARADO NAO AVISA: le o campo do vizinho. Os
      // numeros vem do `static_assert` do C++.
      expect(NucleoRender.tamanhoDaCamada, 56);
      expect(NucleoRender.tamanhoDaCurva, 80);
      expect(NucleoRender.tamanhoDoKeyframe, 96);
    });

    test('pedir GPU sem backend escrito devolve nulo, e nao um motor falso', () {
      final gpu = NucleoRender.abrir(
        largura: 8,
        altura: 8,
        backend: 2, // vulkan
        comThread: false,
      );
      expect(gpu, isNull);
    });
  });

  group('composicao', () {
    test('uma camada de cor cobre o alvo com a cor exata', () {
      final n = abrir();
      expect(n.publicarCena([corCobrindo(16, 16, 0xFFFF0000)], largura: 16, altura: 16), isTrue);
      expect(n.desenharAgora(), 1);

      for (final p in [
        pixel(n, 0, 0, 16),
        pixel(n, 15, 15, 16),
        pixel(n, 8, 8, 16),
      ]) {
        expect(p, [255, 0, 0, 255]);
      }
    });

    test('a caixa so cobre onde ela esta', () {
      final n = abrir();
      // UM QUADRADO DE 8x8 NO CANTO SUPERIOR ESQUERDO, com a ancora no
      // canto (0,0) — o pivo do Aurea quando ninguem mexeu.
      n.publicarCena([
        const CamadaDeRender(
          x: 0,
          y: 0,
          largura: 8,
          altura: 8,
          ancoraX: 0,
          ancoraY: 0,
          cor: 0xFF00FF00,
        ),
      ], largura: 16, altura: 16);
      n.desenharAgora();

      expect(pixel(n, 0, 0, 16), [0, 255, 0, 255]);
      expect(pixel(n, 7, 7, 16), [0, 255, 0, 255]);
      // FORA DA CAIXA: transparente, e nao preto opaco — a diferenca
      // importa, porque um alvo nao limpo deixaria a borda preta.
      expect(pixel(n, 8, 8, 16), [0, 0, 0, 0]);
      expect(pixel(n, 15, 0, 16), [0, 0, 0, 0]);
    });

    test('a opacidade mistura com o que ja estava', () {
      final n = abrir();
      n.publicarCena([
        corCobrindo(16, 16, 0xFF0000FF), // azul opaco
        CamadaDeRender(
          x: 8,
          y: 8,
          largura: 16,
          altura: 16,
          opacidade: 0.5,
          cor: 0xFFFF0000,
        ),
      ], largura: 16, altura: 16);
      n.desenharAgora();

      // METADE DA COR: 0,5 de vermelho sobre azul opaco.
      final p = pixel(n, 8, 8, 16);
      expect(p[0], 128);
      expect(p[1], 0);
      expect(p[2], 128);
      expect(p[3], 255);
    });

    test('os modos de mistura sao os do C++, e nao um apelido', () {
      List<int> comModo(MisturaDeRender modo) {
        final n = abrir();
        n.publicarCena([
          corCobrindo(16, 16, 0xFF0000FF),
          CamadaDeRender(
            x: 8,
            y: 8,
            largura: 16,
            altura: 16,
            mistura: modo,
            cor: 0xFFFF0000,
          ),
        ], largura: 16, altura: 16);
        n.desenharAgora();
        return pixel(n, 8, 8, 16);
      }

      // azul (0,0,1) por vermelho (1,0,0), os dois opacos.
      expect(comModo(MisturaDeRender.multiplicar), [0, 0, 0, 255]);
      expect(comModo(MisturaDeRender.tela), [255, 0, 255, 255]);
      expect(comModo(MisturaDeRender.somar), [255, 0, 255, 255]);
      expect(comModo(MisturaDeRender.escurecer), [0, 0, 0, 255]);
      expect(comModo(MisturaDeRender.clarear), [255, 0, 255, 255]);
      expect(comModo(MisturaDeRender.diferenca), [255, 0, 255, 255]);
      // SOBREPOR com fundo 1,0: 1 - 2*(1-b)*(1-s) = 1.
      expect(comModo(MisturaDeRender.sobrepor), [255, 0, 0, 255]);
    });

    test('a ordem da lista e a ordem do desenho: o ultimo por cima', () {
      final n = abrir();
      n.publicarCena([
        corCobrindo(16, 16, 0xFF00FF00),
        corCobrindo(16, 16, 0xFFFF0000),
      ], largura: 16, altura: 16);
      n.desenharAgora();
      expect(pixel(n, 4, 4, 16), [255, 0, 0, 255]);
    });

    test('a rotacao gira a caixa em volta da ancora', () {
      final n = abrir();
      // UM RETANGULO LARGO E BAIXO, girado 90 graus em volta do centro:
      // o que era largo vira alto.
      n.publicarCena([
        const CamadaDeRender(
          x: 8,
          y: 8,
          largura: 12,
          altura: 4,
          rotacaoGraus: 90,
          cor: 0xFFFFFFFF,
        ),
      ], largura: 16, altura: 16);
      n.desenharAgora();

      expect(pixel(n, 8, 8, 16), [255, 255, 255, 255], reason: 'o centro');
      expect(pixel(n, 8, 13, 16), [255, 255, 255, 255], reason: 'agora e alto');
      expect(pixel(n, 13, 8, 16), [0, 0, 0, 0], reason: 'e ja nao e largo');
    });

    test('a textura entra com amostra bilinear', () {
      final n = abrir();
      // UM QUADRO 2x2: VERMELHO E VERDE EM CIMA, AZUL E BRANCO EMBAIXO.
      final rgba = Uint8List.fromList([
        255, 0, 0, 255, /* */ 0, 255, 0, 255,
        0, 0, 255, 255, /* */ 255, 255, 255, 255,
      ]);
      final id = n.registrarTextura(rgba, 2, 2);
      expect(id, isNot(0));

      n.publicarCena([
        CamadaDeRender(
          tipo: TipoDeCamadaDeRender.textura,
          textura: id,
          x: 8,
          y: 8,
          largura: 8,
          altura: 8,
        ),
      ], largura: 16, altura: 16);
      n.desenharAgora();

      // NA JUNCAO DOS QUATRO TEXELS, os tres canais entram na conta. O
      // numero exato depende de onde cai o centro do pixel em relacao ao
      // centro dos texels; o que se cobra aqui e que os TRES apareçam e
      // que o alfa seja cheio — um vizinho-mais-proximo devolveria um
      // canal puro (0 ou 255) em vez de uma mistura.
      final centro = pixel(n, 8, 8, 16);
      for (final canal in [centro[0], centro[1], centro[2]]) {
        expect(canal, inInclusiveRange(96, 160), reason: 'mistura de texels');
      }
      expect(centro[3], 255);
    });

    test('a escala interna reduz o alvo sem mudar a composicao', () {
      final n = abrir();
      n.definirAutomatica(false);
      n.definirQualidade(amostras: 1, escalaInterna: 0.5, orcamentoMs: 100);
      n.publicarCena([corCobrindo(16, 16, 0xFFFF0000)], largura: 16, altura: 16);
      n.desenharAgora();

      // METADE DA RESOLUCAO: o alvo lido tem 8x8, e a cor continua certa
      // porque as camadas foram escaladas junto com ele.
      final p = n.lerPixels()!;
      expect(p.length, 8 * 8 * 4);
      expect([p[0], p[1], p[2], p[3]], [255, 0, 0, 255]);
    });
  });

  group('recursos', () {
    test('o teto morde: o ocioso mais velho sai para o novo caber', () {
      // TETO DE 4 TEXTURAS DE 8x8x4 = 1024 BYTES. A quinta NAO e
      // recusada: o gerenciador despeja a mais antiga, que ninguem esta
      // segurando. Recusar com a casa cheia de coisa ociosa seria o
      // comportamento errado — e o cache infinito tambem.
      // O ORCAMENTO INCLUI O ALVO DO QUADRO, que ja nasce aberto: 16x16
      // RGBA = 1024 bytes. Sobram 1024 para quatro texturas de 8x8.
      final n = abrir(orcamento: 1024 + 4 * 8 * 8 * 4);
      final rgba = Uint8List(8 * 8 * 4);
      for (var i = 0; i < 6; i++) {
        expect(n.registrarTextura(rgba, 8, 8), isNot(0));
      }
      final e = n.estatisticas();
      // SETE CRIADOS: o alvo do quadro mais as seis texturas. Duas
      // sairam, e cinco continuam vivos — o alvo (que o nucleo segura) e
      // as quatro ultimas texturas.
      expect(e.recursosCriados, 7);
      expect(e.recursosDespejados, 2, reason: 'as duas mais antigas sairam');
      expect(e.recursosVivos, 5);
      expect(
        e.bytesEmUso,
        lessThanOrEqualTo(e.bytesOrcamento),
        reason: 'o uso nunca passa do orcamento',
      );
    });

    test('o que nao cabe nem com a casa vazia e recusado, e nao despejado', () {
      // UMA TEXTURA MAIOR QUE O ORCAMENTO INTEIRO. Aqui a resposta certa
      // e recusar: aceitar obrigaria a soltar tudo que existe (inclusive
      // o que o compositor esta lendo) e mesmo assim estourar.
      final n = abrir(orcamento: 64 * 64 * 4);
      expect(n.registrarTextura(Uint8List(128 * 128 * 4), 128, 128), 0);
      expect(n.estatisticas().recursosRecusados, 1);
      // SO O ALVO DO QUADRO CONTINUA VIVO: a recusa nao despejou nada.
      expect(n.estatisticas().recursosVivos, 1);
    });

    test('o alvo do quadro nao e realocado por quadro', () {
      final n = abrir();
      n.publicarCena([corCobrindo(16, 16, 0xFFFFFFFF)], largura: 16, altura: 16);
      for (var i = 0; i < 30; i++) {
        n.desenharAgora();
      }
      final e = n.estatisticas();
      expect(e.quadros, 30);

      // TRINTA QUADROS, UMA ALOCACAO DE ALVO: a do primeiro. Se este
      // numero subisse junto com os quadros, haveria uma alocacao de
      // alvo por quadro — o serrote de memoria que o cache de quadro
      // existe para evitar.
      expect(e.recursosCriados, 1);
      expect(e.recursosReaproveitados, 0);
    });

    test('trocar de nivel e voltar reaproveita o alvo antigo', () {
      final n = abrir();
      n.publicarCena([corCobrindo(16, 16, 0xFFFFFFFF)], largura: 16, altura: 16);
      n.desenharAgora();

      // METADE DA RESOLUCAO E DE VOLTA: a volta tem de achar o alvo de
      // 16x16 no cache. Sem isto, cada oscilacao de qualidade custaria
      // 1 KB de alocacao nova — e a qualidade adaptativa oscila.
      n.definirQualidade(amostras: 1, escalaInterna: 0.5, orcamentoMs: 100);
      n.desenharAgora();
      n.definirQualidade(amostras: 1, escalaInterna: 1.0, orcamentoMs: 100);
      n.desenharAgora();

      final e = n.estatisticas();
      expect(e.recursosCriados, 2, reason: 'dois tamanhos, dois alvos');
      expect(e.recursosReaproveitados, 1, reason: 'o de 16x16 voltou do cache');
    });
  });

  group('qualidade adaptativa', () {
    test('o controle desce o nivel quando o orcamento nao fecha', () {
      // UM ORCAMENTO IMPOSSIVEL, E DE PROPOSITO: um microssegundo por
      // quadro nao existe em aparelho nenhum, entao o p95 estoura com
      // certeza e a reacao do controle fica deterministica. Forcar a
      // maquina a ficar lenta daria o mesmo resultado e um teste que
      // depende da carga do computador.
      //
      // E O ORCAMENTO NAO SE MEXE: o controle cede RESOLUCAO para caber
      // no alvo, e nao reescreve o alvo. Sem isso, o pedido de um
      // microssegundo viraria 16,667 ms na primeira reacao.
      final n = NucleoRender.abrir(
        largura: 96,
        altura: 96,
        comThread: false,
        amostras: 2,
        escalaInterna: 1.0,
        orcamentoMs: 0.001,
      );
      expect(n, isNotNull);
      addTearDown(n!.fechar);

      // SEM QUADRO NENHUM AINDA: a qualidade e a de largada.
      expect(n.estatisticas().escalaInterna, 1.0);
      expect(n.estatisticas().orcamentoMs, 0.001);

      n.publicarCena([corCobrindo(96, 96, 0xFF808080)], largura: 96, altura: 96);
      for (var i = 0; i < 60; i++) {
        n.desenharAgora();
      }

      final depois = n.estatisticas();
      expect(
        depois.escalaInterna,
        lessThan(1.0),
        reason: 'o controle tem de ceder resolucao antes de o aparelho travar',
      );
      expect(depois.amostrasPorPixel, lessThan(4));
      // O ORCAMENTO NAO SE MEXE: ele e a politica, e o degrau e o meio.
      expect(depois.orcamentoMs, 0.001);
    });
  });

  group('shaders', () {
    test('o pre-aquecimento compila uma vez e reaproveita depois', () {
      final n = abrir();
      final lote = [
        (nome: 'correcao/leitura', fonte: 'fonte A', versao: 1),
        (nome: 'glow/passe', fonte: 'fonte B', versao: 1),
        (nome: 'blur/horizontal', fonte: 'fonte C', versao: 1),
      ];

      expect(n.preAquecer(lote), 3);
      expect(n.estatisticas().shadersCompilados, 3);

      // SEGUNDA VEZ: NADA COMPILA. E esta a conta que impede a travada no
      // meio do play — o mesmo lote, ja pronto, nao pode custar nada.
      expect(n.preAquecer(lote), 3);
      final e = n.estatisticas();
      expect(e.shadersCompilados, 3);
      expect(e.shadersReaproveitados, 3);
    });

    test('fonte vazia falha e nao entra no cache', () {
      final n = abrir();
      expect(n.preAquecer([(nome: 'vazio', fonte: '', versao: 1)]), 0);
      expect(n.estatisticas().shadersFalhas, greaterThan(0));
      expect(n.estatisticas().shadersCompilados, 0);
    });
  });

  group('quadro', () {
    test('o relogio conta os quadros e mede o tempo de verdade', () {
      final n = abrir(largura: 64, altura: 64);
      n.publicarCena([corCobrindo(64, 64, 0xFF123456)], largura: 64, altura: 64);
      for (var i = 0; i < 40; i++) {
        n.desenharAgora();
      }
      final e = n.estatisticas();
      expect(e.quadros, 40);
      expect(e.quadrosNaJanela, 40);
      // O CUSTO E MEDIDO, e nao inventado: compor 64x64 custa ALGUM tempo,
      // e a mediana tem de ser maior que zero. Um numero exato aqui seria
      // um numero de maquina de desenvolvimento virando contrato.
      expect(e.cpuMedianaMs, greaterThan(0));
      expect(e.cpuMaxMs, greaterThanOrEqualTo(e.cpuMedianaMs));
      expect(e.cpuP95Ms, greaterThanOrEqualTo(e.cpuMedianaMs));
    });

    test('cena igual nao se recompõe: a impressao digital poupa a GPU', () {
      final n = abrir();
      n.publicarCena(
        [corCobrindo(16, 16, 0xFF00FF00)],
        largura: 16,
        altura: 16,
        impressao: 7,
      );
      expect(n.desenharAgora(), 1);
      for (var i = 0; i < 5; i++) {
        n.desenharAgora();
      }
      expect(n.estatisticas().quadrosReaproveitados, 5);

      // CENA NOVA, IMPRESSAO NOVA: volta a compor.
      n.publicarCena(
        [corCobrindo(16, 16, 0xFF0000FF)],
        largura: 16,
        altura: 16,
        impressao: 8,
      );
      expect(n.desenharAgora(), 1);
    });

    test('sem cena publicada o quadro falha em vez de desenhar lixo', () {
      final n = abrir();
      expect(n.desenharAgora(), lessThan(0));
    });

    test('a thread desenha o que a UI publica, sem travar a UI', () async {
      final n = NucleoRender.abrir(
        largura: 32,
        altura: 32,
        comThread: true,
        amostras: 1,
        escalaInterna: 1.0,
      );
      expect(n, isNotNull);
      addTearDown(n!.fechar);

      n.publicarCena([corCobrindo(32, 32, 0xFFFF8800)], largura: 32, altura: 32);
      // CEM PEDIDOS, E O CHAMADOR NAO ESPERA NENHUM: `pedirQuadro` so
      // acorda a thread. Se ele bloqueasse, esta linha levaria o tempo de
      // cem quadros — e num celular seria um gesto engasgado.
      for (var i = 0; i < 100; i++) {
        n.pedirQuadro();
      }
      // A THREAD E OUTRA: o placar so chega depois de ela trabalhar.
      var quadros = 0;
      for (var i = 0; i < 200 && quadros == 0; i++) {
        await Future<void>.delayed(const Duration(milliseconds: 10));
        quadros = n.estatisticas().quadros;
      }
      expect(quadros, greaterThan(0), reason: 'a thread do render nao andou');

      // E O QUADRO NAO SAI DA GPU. Um nucleo com thread e o nucleo de
      // producao, e nele a leitura de pixels e RECUSADA: se esta chamada
      // passasse, o caminho GPU->CPU->GPU que o motor existe para evitar
      // estaria de volta num play de verdade.
      expect(n.lerPixels(), isNull);
    });
  });
}
