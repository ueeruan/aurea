import 'dart:async';

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

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  TextureCache.instance.observeMemoryPressure(WidgetsBinding.instance);
  final prefs = await SharedPreferences.getInstance();
  // Resolve a migalha do motor 3D antes de qualquer cena desenhar:
  // uma sessao que nao voltou de um quadro em GPU desliga a GPU
  // nesta.
  await Motor3DPreferencia.carregar(prefs);
  // O shader das mesclas proprias sobe uma vez, no comeco: compilar no
  // meio da edicao apareceria como engasgo no primeiro quadro.
  unawaited(CustomBlendBox.warmUp());
  // A curva do sRGB: sem ela, glow e desfoque somam luz no espaco
  // errado e saem acinzentados.
  // Resolve before opening a project: preview and export start on the same backend.
  await Future.wait([LinearLight.warmUp(), PixelEffectEngine.warmUp()]);
  // As fontes importadas precisam ser registradas de novo a cada
  // abertura: o registro do Flutter vive so enquanto o processo vive.
  await FontService.instance.loadAll();
  runApp(
    ProviderScope(
      overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
      child: const AureaApp(),
    ),
  );
}
