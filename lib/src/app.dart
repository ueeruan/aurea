import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'core/l10n/app_language.dart';

import 'core/theme/app_theme.dart';
import 'features/projects/presentation/home_shell.dart';
import 'features/settings/application/settings_controller.dart';

class AureaApp extends ConsumerWidget {
  const AureaApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // TEMA (Fase 6): escuro, claro ou o do sistema. O editor continua
    // escuro por dentro (palco de video); Projetos e Ajustes acompanham.
    final modo = ref.watch(
      settingsControllerProvider.select((s) => s.themeMode),
    );
    final sistema =
        WidgetsBinding.instance.platformDispatcher.platformBrightness ==
        Brightness.light;
    final claro = modo == 'claro' || (modo == 'sistema' && sistema);
    return MaterialApp(
      // A chave remonta a arvore ao trocar de tema: as cores fixas das
      // telas sao lidas na montagem.
      key: ValueKey('tema-${claro ? 'claro' : 'escuro'}'),
      title: 'Aurea',
      locale: Locale(ref.watch(appLanguageProvider)),
      supportedLocales: [for (final code in appLanguages.keys) Locale(code)],
      localizationsDelegates: GlobalMaterialLocalizations.delegates,
      // O LAYOUT NAO ESPELHA. Em arabe o Flutter viraria o app inteiro
      // da direita para a esquerda — timeline, transporte, paineis. Os
      // testadores arabes pediram o contrario: "o programa deve
      // continuar como esta, sem mudar de direcao". O texto traduz; a
      // direcao do editor fica.
      builder: (context, child) => Directionality(
        textDirection: TextDirection.ltr,
        child: child ?? const SizedBox.shrink(),
      ),
      debugShowCheckedModeBanner: false,
      theme: AppTheme.tema(claro: claro),
      home: const HomeShell(),
    );
  }
}
