import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import 'core/l10n/app_language.dart';

import 'core/theme/app_theme.dart';
import 'core/theme/aurea_paleta.dart';
import 'features/community/presentation/cadastro_obrigatorio.dart';
import 'features/projects/presentation/home_shell.dart';
import 'features/settings/application/settings_controller.dart';

class AureaApp extends ConsumerStatefulWidget {
  const AureaApp({super.key});

  @override
  ConsumerState<AureaApp> createState() => _AureaAppState();
}

class _AureaAppState extends ConsumerState<AureaApp>
    with WidgetsBindingObserver {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  /// No modo 'sistema', o app acompanha a troca claro/escuro do aparelho
  /// enquanto esta aberto. Sem o observador, a raiz so relia o brilho no
  /// proximo rebuild por outro motivo — na pratica, ao reabrir o app.
  @override
  void didChangePlatformBrightness() {
    if (ref.read(settingsControllerProvider).temaSegueOSistema) {
      setState(() {});
    }
  }

  @override
  Widget build(BuildContext context) {
    // TEMA: a escolha dos Ajustes vira uma paleta ([AureaPaleta]). O editor
    // continua escuro por dentro mesmo no tema claro (palco de video).
    final modo = ref.watch(
      settingsControllerProvider.select((s) => s.themeMode),
    );
    final paleta = AureaPaleta.de(
      AureaPaleta.resolver(
        modo,
        WidgetsBinding.instance.platformDispatcher.platformBrightness,
      ),
    );
    // A PALETA E ESCRITA AQUI, antes de qualquer filho montar: AppColors,
    // AmColors e os pintores leem um estatico, sem contexto.
    final tema = AppTheme.tema(paleta: paleta);
    return MaterialApp(
      // A CHAVE REMONTA A ARVORE AO TROCAR DE TEMA. Um rebuild comum nao
      // basta: a arvore esta cheia de widgets `const` (que o Flutter pula,
      // por serem identicos entre builds) e as cores sao lidas de um
      // estatico, que nao registra dependencia. Com a chave nova, todo
      // `build` roda de novo com a paleta nova. O que se perde e a pilha do
      // Navigator — por isso o seletor de tema so existe na aba Ajustes, que
      // e rota raiz, e a aba ativa mora num provider acima daqui.
      key: ValueKey('tema-${paleta.id.name}'),
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
      theme: tema,
      home: const CadastroObrigatorioGate(child: HomeShell()),
    );
  }
}
