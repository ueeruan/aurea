// A SONDA DO VULKAN NO ANDROID DE VERDADE (emulador ou aparelho).
//
// O QUE ESTE TESTE PROVA, E O QUE ELE NAO PROVA.
//
// Prova que o RenderCore C++ sobe o Vulkan daquele aparelho: instancia,
// dispositivo fisico, familia de fila grafica, `vkCreateDevice` e o teto
// de textura. E a primeira metade do backend de GPU, e a metade onde os
// aparelhos costumam falhar.
//
// NAO PROVA QUE O AUREA DESENHA EM GPU. Isso e o estagio V1 (superficie e
// swapchain) e o V2 (o compositor em shader), que ainda nao existem. Um
// teste que dissesse "GPU ok" aqui estaria mentindo por antecipacao.
//
// Rodar:
//   flutter test integration_test/nucleo_vulkan_test.dart -d emulator-5554
//
// O EMULADOR PODE NAO TER VULKAN. Um AVD com renderizacao por software
// (`-gpu swiftshader_indirect`) responde "Vulkan indisponivel", e isso
// NAO e falha do Aurea: o teste aceita as duas respostas, mas exige que a
// sonda DIGA qual delas — uma sonda muda nao serve para nada.
import 'package:aurea_render/aurea_render.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  test('a sonda do Vulkan responde no aparelho, e diz o que achou', () {
    expect(
      NucleoRender.disponivel,
      isTrue,
      reason: 'a biblioteca do RenderCore nao carregou no aparelho',
    );

    final texto = NucleoRender.sondarVulkan();
    // ignore: avoid_print
    print('== VULKAN NO APARELHO ==\n$texto');

    expect(texto, isNotEmpty, reason: 'a sonda nao pode ficar muda');

    if (texto.contains('indisponivel')) {
      // RESPOSTA LEGITIMA num AVD sem GPU. O que se cobra e que ela venha
      // com motivo, e nao como um vazio.
      expect(texto.length, greaterThan('Vulkan indisponivel: '.length));
      // ignore: avoid_print
      print('AVISO: este aparelho nao tem Vulkan. Nada a validar aqui.');
      return;
    }

    // COM VULKAN, TUDO O QUE A SONDA PROMETE TEM DE ESTAR NO TEXTO. O
    // nome do dispositivo e o teto de textura sao os dois numeros que
    // decidem se uma composicao cabe — e se algum dia pararem de ser
    // lidos, o teste cai aqui em vez de cair num aparelho de cliente.
    expect(texto, contains('fila grafica'));
    expect(texto, contains('textura max'));
    expect(
      texto,
      isNot(contains('fila grafica: NAO')),
      reason: 'sem fila grafica nao ha desenho nenhum',
    );
  });
}
