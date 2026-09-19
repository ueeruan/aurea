import 'dart:async';
import 'dart:ui' show PlatformDispatcher;

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'src/app.dart';
import 'src/core/storage/prefs.dart';
import 'src/features/editor/application/font_service.dart';
import 'src/features/editor/application/motor3d_modo.dart';
import 'src/features/editor/application/texture_cache.dart';
import 'src/features/editor/presentation/widgets/custom_blend.dart';
import 'src/features/editor/presentation/widgets/linear_light.dart';
import 'src/features/editor/presentation/widgets/pixel_effect_engine.dart';
import 'src/features/editor/domain/estilizar_lote2.dart';
import 'src/features/editor/presentation/widgets/passe_de_cor.dart';
import 'src/features/editor/application/qualidade3d_controller.dart';
import 'src/features/editor/application/registro_de_travadas.dart';
import 'src/features/settings/application/grafico_preferencia.dart';

Future<void> main() async {
  // A LIGACAO PROPRIA MEDE O QUADRO DESDE O PRIMEIRO CODIGO DART DELE.
  // Sem ela, o registro de travadas confundiria tela parada com tela
  // travada. Ver [RegistroDeTravadas].
  LigacaoQueMedeOQuadro();

  // BLINDAGEM CONTRA FECHAMENTOS INESPERADOS (crashes):
  // Exceções assíncronas não-tratadas (de isolates, timers, canais de plataforma)
  // são interceptadas aqui em vez de derrubar o processo no Android e iOS.
  FlutterError.onError = (details) {
    FlutterError.presentError(details);
    debugPrint('FlutterError interceptado: ${details.exceptionAsString()}');
  };
  PlatformDispatcher.instance.onError = (error, stack) {
    debugPrint('PlatformDispatcher erro interceptado: $error\n$stack');
    return true; // Retornar true marca o erro como tratado e impede o encerramento do app
  };
  TextureCache.instance.observeMemoryPressure(WidgetsBinding.instance);
  final prefs = await SharedPreferences.getInstance();
  // Resolve a migalha do motor 3D antes de qualquer cena desenhar:
  // uma sessao que nao voltou de um quadro em GPU desliga a GPU
  // nesta.
  await Motor3DPreferencia.carregar(prefs);
  // O APLICATIVO PASSA A MEDIR A SI MESMO. Tres tentativas de corrigir
  // "o app congela" falharam porque as medicoes eram feitas num PC, em
  // depuracao, numa bancada. Agora todo quadro acima de 120 ms fica
  // registrado no aparelho, com o que estava acontecendo e com o
  // renderizador em uso — que e a pergunta que faltava responder.
  RegistroDeTravadas.comecar();
  RegistroDeTravadas.contextoAtual = descreverMotor3D;
  // A API de desenho no Android (Vulkan ou OpenGL ES) e lida pela
  // MainActivity antes de o motor subir; aqui so se confirma, no
  // primeiro quadro, que a sessao esta viva — e o que desarma a migalha.
  // O teto de qualidade 3D dos Ajustes; o orcamento vem do aparelho
  // na primeira sonda.
  await ControladorDeQualidade3D.instancia.carregar(prefs);
  final grafico = await GraficoPreferencia.carregar(prefs);
  WidgetsBinding.instance.addPostFrameCallback(
    (_) => unawaited(grafico.confirmarVivo()),
  );
  // O shader das mesclas proprias sobe uma vez, no comeco: compilar no
  // meio da edicao apareceria como engasgo no primeiro quadro.
  unawaited(CustomBlendBox.warmUp());
  // As fontes importadas precisam ser registradas de novo a cada
  // abertura: o registro do Flutter vive so enquanto o processo vive.
  //
  // ESTA E A UNICA COISA QUE A ABERTURA ESPERA. Tudo o mais — a curva do
  // sRGB, os shaders de cor, os treze shaders do Sapphire — sobe DEPOIS
  // do primeiro quadro.
  //
  // POR QUE MUDOU: a abertura esperava a compilacao de TODOS os shaders
  // antes de chamar `runApp`. Compilar shader e trabalho de driver, e num
  // aparelho de entrada isso custa segundos; enquanto nao terminasse, o
  // que estava na tela era a LOGO do sistema — o app parecia travado, e
  // o dono relatou exatamente isso. Nada aqui e necessario para o
  // primeiro quadro: quem espera por eles e o efeito que os usa, e o
  // motor ja sabe esperar (quem pede um shader que ainda nao chegou
  // redesenha quando ele chega).
  await FontService.instance.loadAll();
  // O PRIMEIRO QUADRO NAO ESPERA SHADER NENHUM.
  unawaited(
    Future.wait([
      LinearLight.warmUp(),
      PixelEffectEngine.warmUp(),
      MotorDeCorrecao.warmUp(),
      MotorSapphire.warmUp(assetsDosShadersSapphire),
    ]).timeout(
      // TETO DE SEGURANCA: se um driver travar numa compilacao, isso nao
      // pode segurar nada — nem a abertura, nem a memoria do app.
      const Duration(seconds: 20),
      onTimeout: () {
        debugPrint('AUREA: warm-up de shader passou do teto');
        return const <void>[];
      },
    ),
  );
  runApp(
    ProviderScope(
      overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
      child: const AureaApp(),
    ),
  );
}
