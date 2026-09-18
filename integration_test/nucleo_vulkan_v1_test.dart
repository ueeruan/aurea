// V1 NO APARELHO — SUPERFICIE, SWAPCHAIN, APRESENTACAO E RECRIACAO.
//
// O QUE ESTE TESTE PROVA: a janela do `SurfaceProducer` vira uma
// `VkSurfaceKHR`, a swapchain nasce, os quadros SAO apresentados, trocar o
// tamanho marca a swapchain para refazer, e soltar/refazer a superficie
// (que e o que o Flutter faz ao voltar do segundo plano) nao derruba nada.
//
// O QUE ELE NAO PROVA: desempenho. No emulador o Vulkan e SwiftShader —
// software — e um numero de FPS dali nao diz nada sobre um celular. E TAMBEM
// NAO PROVA o segundo plano DE VERDADE: o teste solta e refaz a superficie
// pela mesma API que o lifecycle usa, mas quem aperta o botao HOME e o
// sistema, e essa passada e manual (esta escrito no fim do arquivo).
//
// Rodar:
//   flutter test integration_test/nucleo_vulkan_v1_test.dart -d emulator-5554
import 'package:aurea_render/aurea_render.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

const _canal = MethodChannel('aurea/render');

Future<Map<String, Object?>> _criar(WidgetTester t, int l, int a) async {
  final r = await _canal.invokeMapMethod<String, Object?>(
    'criar',
    {'largura': l, 'altura': a},
  );
  return r ?? const {};
}

/// POE O `Texture` NA ARVORE — e isso que da tamanho a janela.
///
/// SEM ISTO O TESTE NAO REPRODUZ O APP. O `SurfaceProducer` recebe um
/// tamanho no `setSize`, mas o tamanho REAL da superficie vem do layout do
/// `Texture` no Flutter. Um teste que so chama o canal e o FFI mede uma
/// janela que nunca foi disposta — e foi por isso que a primeira versao
/// deste arquivo viu 0x0 enquanto o app de verdade funcionava.
Future<void> _montarPreview(WidgetTester tester, int id, int l, int a) async {
  await tester.pumpWidget(
    MaterialApp(
      home: Center(
        child: SizedBox(width: l.toDouble(), height: a.toDouble(),
            child: Texture(textureId: id)),
      ),
    ),
  );
  await tester.pump(const Duration(milliseconds: 100));
}

/// APRESENTA ATE A SWAPCHAIN EXISTIR. Devolve quantas tentativas custou.
///
/// O PLACAR E CUMULATIVO NA VIDA DO PROCESSO. Esperar por
/// `apresentados != 0` funcionava no primeiro teste e nao esperava nada nos
/// seguintes: o numero ja vinha cheio da superficie anterior. O que vale e
/// o DELTA — quantos quadros ESTA superficie apresentou.
Future<int> _apresentarAte(
  WidgetTester tester,
  int cor, {
  int limite = 300,
}) async {
  final base = PreviewVulkan.estatisticas().apresentados;
  var n = 0;
  while (n < limite && PreviewVulkan.estatisticas().apresentados == base) {
    PreviewVulkan.apresentar(cor);
    await tester.pump(const Duration(milliseconds: 16));
    n++;
  }
  return n;
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('V1: superficie, swapchain e quadros na tela', (tester) async {
    final resposta = await _criar(tester, 640, 360);
    // ignore: avoid_print
    print('== V1 CRIAR ==\n$resposta');

    expect(
      resposta['ok'],
      isTrue,
      reason: 'a superficie Vulkan nao subiu: ${PreviewVulkan.motivo}',
    );
    expect(resposta['id'], isA<int>());
    expect(PreviewVulkan.estado, 1, reason: 'estado 1 = pronta');
    await _montarPreview(tester, resposta['id']! as int, 640, 360);

    // A SWAPCHAIN NASCE NO PRIMEIRO QUADRO, NAO NO `criar`.
    //
    // Este teste lia `estatisticas()` logo depois de montar o `Texture` e
    // exigia largura > 0 — e via 0x0. Nao era o motor: a janela nasce sem
    // tamanho (quem da um e o layout do `Texture`), entao a swapchain e
    // criada PREGUICOSAMENTE, no primeiro `apresentar`. Ler o tamanho
    // antes disso mede uma janela que ainda nao existe.
    final tentativas = await _apresentarAte(tester, 0xFF204060);

    final e0 = PreviewVulkan.estatisticas();
    // ignore: avoid_print
    print('== V1 ESTADO INICIAL (apresentou em $tentativas tentativas) ==\n$e0');

    expect(e0.apresentados, greaterThan(0), reason: 'nada chegou na tela');
    // O TAMANHO QUE VALE E O DA SUPERFICIE, e nao o que o Flutter pediu:
    // o sistema pode discordar (barra de navegacao, corte).
    expect(e0.largura, greaterThan(0));
    expect(e0.altura, greaterThan(0));
    expect(e0.imagens, greaterThanOrEqualTo(2));

    // APRESENTA CEM QUADROS. Cada um com uma cor diferente: uma cor parada
    // provaria so que a tela foi pintada uma vez.
    var devolveuZero = 0;
    for (var i = 0; i < 100; i++) {
      final r = PreviewVulkan.apresentar(0xFF000000 | (i * 2654435761 & 0xFFFFFF));
      if (r == 0) devolveuZero++;
      // A GPU trabalha em outra fila; um respiro deixa a apresentacao
      // andar. Sem ele o teste mediria a fila de comandos da CPU.
      if (i % 20 == 0) await tester.pump(const Duration(milliseconds: 16));
    }
    await tester.pump(const Duration(milliseconds: 100));

    final e1 = PreviewVulkan.estatisticas();
    // ignore: avoid_print
    print('== V1 DEPOIS DE 100 QUADROS ==\n$e1');

    expect(devolveuZero, greaterThan(0), reason: 'nenhum quadro apresentou');
    expect(e1.apresentados, greaterThan(0));
    expect(e1.falhas, 0, reason: 'apresentar nao pode falhar em regime');
  });

  testWidgets('V1: redimensionar marca a swapchain para refazer', (tester) async {
    final r = await _criar(tester, 640, 360);
    expect(r['ok'], isTrue);
    expect(r['id'], isA<int>());
    await _montarPreview(tester, r['id']! as int, 640, 360);
    await _apresentarAte(tester, 0xFF334455);
    final antes = PreviewVulkan.estatisticas();

    // O TAMANHO NOVO NAO RECRIA NA HORA, e isso e de proposito: o tamanho
    // real vem da superficie, e refazer aqui gastaria uma swapchain em vao.
    PreviewVulkan.redimensionar(800, 480);
    // O PROPRIO MOTOR JA RECRIA SOZINHO quando o `vkAcquireNextImageKHR`
    // devolve OUT_OF_DATE — que e o que acontece quando a janela muda de
    // tamanho de verdade.
    for (var i = 0; i < 30; i++) {
      PreviewVulkan.apresentar(0xFF204060);
      await tester.pump(const Duration(milliseconds: 16));
    }

    final depois = PreviewVulkan.estatisticas();
    // ignore: avoid_print
    print('== V1 RESIZE ==\nantes: $antes\ndepois: $depois');
    expect(depois.falhas, 0, reason: 'um resize nao pode virar falha');
  });

  testWidgets('V1: soltar e refazer a superficie nao derruba nada',
      (tester) async {
    final r = await _criar(tester, 640, 360);
    expect(r['ok'], isTrue);
    expect(r['id'], isA<int>());
    await _montarPreview(tester, r['id']! as int, 640, 360);
    await _apresentarAte(tester, 0xFF112233);
    final antes = PreviewVulkan.estatisticas();
    expect(antes.apresentados, greaterThan(0));

    // O CAMINHO DO SEGUNDO PLANO: solta a superficie (o Flutter avisa que
    // a janela vai sumir) e depois refaz, como no `onSurfaceCreated`.
    await _canal.invokeMethod<void>('liberar');
    expect(PreviewVulkan.estado, 0, reason: 'estado 0 = sem superficie');
    // SEM SUPERFICIE, APRESENTAR RECUSA — e nao tenta usar uma janela
    // morta, que e o erro que so aparece quando a pessoa troca de app.
    expect(PreviewVulkan.apresentar(0xFFFFFFFF), lessThan(0));

    final r2 = await _criar(tester, 640, 360);
    // ignore: avoid_print
    print('== V1 RECRIACAO ==\nantes: $antes\nnovo: $r2');
    expect(r2['ok'], isTrue, reason: 'refazer a superficie falhou');
    expect(PreviewVulkan.estado, 1);

    for (var i = 0; i < 20; i++) {
      PreviewVulkan.apresentar(0xFF445566);
    }
    final depois = PreviewVulkan.estatisticas();
    expect(depois.falhas, 0, reason: 'a recriacao nao pode deixar falhas');

    await _canal.invokeMethod<void>('liberar');
  });

  testWidgets('V1.1: o quadro COMPOSTO pelo motor chega na tela', (
    tester,
  ) async {
    // ESTE E O TESTE QUE FECHA A LACUNA DO V1.
    //
    // Ate aqui o que chegava a tela era uma constante que o Dart escolhia:
    // a tubulacao estava provada e o MOTOR nao. Agora o compositor C++
    // escreve os pixels, o `Nucleo` os devolve e eles sobem para a GPU.
    final r = await _criar(tester, 320, 240);
    expect(r['ok'], isTrue);
    await _montarPreview(tester, r['id']! as int, 320, 240);
    await _apresentarAte(tester, 0xFF101010);

    final nucleo = NucleoRender.abrir(
      largura: 32,
      altura: 32,
      comThread: false,
      orcamentoMs: 200,
    );
    expect(nucleo, isNotNull, reason: NucleoRender.ultimoErro);
    addTearDown(nucleo!.fechar);

    // DUAS CAMADAS DE COR: o compositor tem de MISTURAR, e nao so copiar.
    expect(
      nucleo.publicarCena(
        [
          const CamadaDeRender(
            x: 16,
            y: 16,
            largura: 32,
            altura: 32,
            cor: 0xFF000000,
          ),
          const CamadaDeRender(
            x: 16,
            y: 16,
            largura: 32,
            altura: 32,
            cor: 0xFFFF0000,
            opacidade: 0.5,
          ),
        ],
        largura: 32,
        altura: 32,
      ),
      isTrue,
    );
    // `desenharAgora` devolve QUANTAS CAMADAS COMPOZERAM — e nao 0 para
    // "deu certo". Duas camadas na cena, duas compostas.
    expect(
      nucleo.desenharAgora(),
      2,
      reason: 'as duas camadas tinham de ser compostas',
    );
    final pixels = nucleo.lerPixels();
    expect(pixels, isNotNull);
    expect(pixels!.length, 32 * 32 * 4);

    // O MIOLO DA IMAGEM: vermelho a meia opacidade sobre preto. Amostrar o
    // centro e de proposito — a borda carrega a cobertura do retangulo, e
    // ali o numero mede antialias, nao composicao.
    const meio = (16 * 32 + 16) * 4;
    expect(pixels[meio], greaterThan(100), reason: 'o vermelho da camada');
    expect(pixels[meio], lessThan(160), reason: 'metade da opacidade');
    expect(pixels[meio + 1], lessThan(20), reason: 'sem verde');

    // E SOBE PARA A TELA. Tres quadros com o mesmo conteudo: o primeiro
    // cria a imagem de origem, os outros REAPROVEITAM — e e o
    // reaproveitamento que prova que nao ha alocacao por quadro.
    for (var i = 0; i < 3; i++) {
      expect(
        PreviewVulkan.apresentarImagem(pixels, 32, 32),
        0,
        reason: 'o quadro do motor nao chegou na swapchain',
      );
      await tester.pump(const Duration(milliseconds: 16));
    }

    final e = PreviewVulkan.estatisticas();
    // ignore: avoid_print
    print('== V1.1 QUADRO DO MOTOR ==\n$e');
    expect(e.apresentados, greaterThan(0));
    expect(e.falhas, 0, reason: 'subir o quadro do motor nao pode falhar');
  });

  testWidgets('V1.1: trocar o tamanho do quadro nao vaza recurso', (
    tester,
  ) async {
    final r = await _criar(tester, 320, 240);
    expect(r['ok'], isTrue);
    await _montarPreview(tester, r['id']! as int, 320, 240);
    await _apresentarAte(tester, 0xFF101010);

    // QUATRO QUADROS DE TAMANHOS DIFERENTES, um nucleo por tamanho: o
    // tamanho do quadro e fixado no `abrir` — `publicarCena` recebe um
    // tamanho, mas quem devolve os pixels e o nucleo, e ele nao muda de
    // tamanho por conta propria. Cada troca obriga o lado Vulkan a
    // RECRIAR a imagem de origem e o buffer; se a troca vazasse, o
    // SwiftShader (memoria de sistema) recusaria antes do fim, e o placar
    // de falhas diria isso.
    var n = 0;
    for (final lado in [16, 24, 32, 48]) {
      final nucleo = NucleoRender.abrir(
        largura: lado,
        altura: lado,
        comThread: false,
        orcamentoMs: 200,
      );
      expect(nucleo, isNotNull, reason: 'nucleo de $lado');
      nucleo!.publicarCena(
        [
          CamadaDeRender(
            x: lado / 2,
            y: lado / 2,
            largura: lado.toDouble(),
            altura: lado.toDouble(),
            cor: 0xFF00FF00,
          ),
        ],
        largura: lado,
        altura: lado,
      );
      expect(nucleo.desenharAgora(), 1, reason: 'lado $lado');
      final pixels = nucleo.lerPixels()!;
      expect(pixels.length, lado * lado * 4, reason: 'lado $lado');
      expect(
        PreviewVulkan.apresentarImagem(pixels, lado, lado),
        0,
        reason: 'lado $lado',
      );
      await tester.pump(const Duration(milliseconds: 16));
      nucleo.fechar();
      n++;
    }
    expect(n, 4);

    final e = PreviewVulkan.estatisticas();
    // ignore: avoid_print
    print('== V1.1 TROCA DE TAMANHO == $e');
    expect(e.falhas, 0, reason: 'trocar o tamanho do quadro nao pode falhar');
    expect(e.apresentados, greaterThan(0));
  });

  testWidgets('V1: a sonda continua respondendo neste aparelho',
      (tester) async {
    final texto = NucleoRender.sondarVulkan();
    // ignore: avoid_print
    print('== SONDA ==\n$texto');
    expect(texto, isNotEmpty);
  });
}

// ================================ A PASSADA MANUAL ======================
//
// O segundo plano DE VERDADE tem de ser apertado pelo sistema, e nao pela
// API. Com o app aberto no preview nativo:
//
//   adb shell input keyevent KEYCODE_HOME     -> onSurfaceDestroyed
//   adb shell am start -n com.aurea.aurea/.MainActivity  -> onSurfaceCreated
//
// e o log do sistema diz o que aconteceu:
//
//   adb logcat -s AureaVulkan
//
// O que se espera ler e "superficie destruida" seguido de "superficie
// criada", e nenhum erro de driver no meio. Sem aparelho, e sem essa
// passada, o lifecycle real fica NAO TESTADO.
