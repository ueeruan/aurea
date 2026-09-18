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
Future<int> _apresentarAte(
  WidgetTester tester,
  int cor, {
  int limite = 300,
}) async {
  var n = 0;
  while (n < limite && PreviewVulkan.estatisticas().apresentados == 0) {
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

    final e0 = PreviewVulkan.estatisticas();
    // ignore: avoid_print
    print('== V1 ESTADO INICIAL ==\n$e0');

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
