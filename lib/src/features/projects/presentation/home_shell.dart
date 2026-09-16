import 'package:aurea/src/core/l10n/app_language.dart';

import 'dart:async';
import 'dart:ui';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/storage/prefs.dart';
import 'boas_vindas.dart';
import 'release_notice.dart';

import '../../../core/theme/app_theme.dart';
import '../../about/presentation/about_tab.dart';
import '../../community/presentation/community_tab.dart';
import '../../settings/presentation/settings_tab.dart';
import '../../user/presentation/user_tab.dart';
import 'aviso_ao_vivo.dart';
import 'projects_tab.dart';

final homeTabProvider = StateProvider<int>((ref) => 0);

/// Casca principal: abas com tab bar translucida estilo iOS
/// (blur + hairline, conteudo rolando por baixo).
class HomeShell extends ConsumerStatefulWidget {
  const HomeShell({super.key});

  @override
  ConsumerState<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends ConsumerState<HomeShell> {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) unawaited(_showUpdateOnce());
    });
  }

  Future<void> _showUpdateOnce() async {
    final prefs = ref.read(sharedPreferencesProvider);
    // A PRIMEIRA ABERTURA vem antes de qualquer novidade: quem nunca
    // entrou ve as boas-vindas (e o combinado), uma vez so.
    if (prefs.getString(chaveDoAceite) == null) {
      if (ModalRoute.of(context)?.isCurrent != true) return;
      await showBoasVindas(context);
      try {
        await prefs.setString(chaveDoAceite, DateTime.now().toIso8601String());
      } catch (_) {}
      // Quem acabou de conhecer o app nao precisa das novidades da
      // versao no mesmo instante.
      try {
        await prefs.setString(releaseNoticeSeenKey, releaseNoticeRevision);
      } catch (_) {}
      return;
    }
    if (prefs.getString(releaseNoticeSeenKey) == releaseNoticeRevision) return;
    if (ModalRoute.of(context)?.isCurrent != true) return;
    await showReleaseNotice(context);
    // Persist after dismissal, so an interrupted launch can show it again.
    try {
      await prefs.setString(releaseNoticeSeenKey, releaseNoticeRevision);
    } catch (_) {
      // A preferences failure must never prevent opening the editor.
    }
  }

  static const _tabs = [
    (CupertinoIcons.house, CupertinoIcons.house_fill, 'Inicio'),
    // A COMUNIDADE FICA EM SEGUNDO, ao lado do Inicio: e para onde se
    // vai depois de terminar um trabalho, e nao um canto de ajustes.
    (CupertinoIcons.person_2, CupertinoIcons.person_2_fill, 'Comunidade'),
    (
      CupertinoIcons.slider_horizontal_3,
      CupertinoIcons.slider_horizontal_3,
      'Ajustes',
    ),
    (CupertinoIcons.person, CupertinoIcons.person_fill, 'Perfil'),
    (CupertinoIcons.info_circle, CupertinoIcons.info_circle_fill, 'Sobre'),
  ];

  void _select(int i) {
    if (i == ref.read(homeTabProvider)) return;
    HapticFeedback.selectionClick();
    ref.read(homeTabProvider.notifier).state = i;
  }

  @override
  Widget build(BuildContext context) {
    final index = ref.watch(homeTabProvider);
    return Scaffold(
      extendBody: true,
      body: Column(
        children: [
          // O AVISO AO VIVO fica ACIMA das abas: e a unica coisa que o
          // app inteiro mostra sem ninguem pedir, e por isso vive fora
          // de qualquer aba.
          const AvisoAoVivo(),
          Expanded(
            child: IndexedStack(
              index: index,
              children: const [
                ProjectsTab(),
                CommunityTab(),
                SettingsTab(),
                UserTab(),
                AboutTab(),
              ],
            ),
          ),
        ],
      ),
      bottomNavigationBar: ClipRect(
        child: BackdropFilter(
          filter: ImageFilter.blur(sigmaX: 24, sigmaY: 24),
          child: Container(
            decoration: BoxDecoration(
              color: AppColors.background.withValues(alpha: 0.72),
              border: Border(
                top: BorderSide(color: AppColors.hairline, width: 0.5),
              ),
            ),
            child: SafeArea(
              top: false,
              child: SizedBox(
                height: 54,
                child: Row(
                  children: [
                    for (var i = 0; i < _tabs.length; i++)
                      Expanded(
                        child: _TabItem(
                          icon: _tabs[i].$1,
                          activeIcon: _tabs[i].$2,
                          label: _tabs[i].$3,
                          selected: i == index,
                          onTap: () => _select(i),
                        ),
                      ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _TabItem extends StatelessWidget {
  const _TabItem({
    required this.icon,
    required this.activeIcon,
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final IconData icon;
  final IconData activeIcon;
  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final color = selected ? AppColors.lime : AppColors.muted;
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(selected ? activeIcon : icon, size: 24, color: color),
          const SizedBox(height: 3),
          Flexible(
            child: AppText(
              label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 10.5,
                fontWeight: FontWeight.w500,
                letterSpacing: 0.1,
                color: color,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
