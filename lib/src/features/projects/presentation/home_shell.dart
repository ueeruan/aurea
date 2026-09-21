import 'dart:async';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' show Scaffold;
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/l10n/app_language.dart';
import '../../../core/storage/prefs.dart';
import '../../../core/ds/ds.dart';
import '../../../core/ui/tocavel.dart';
import '../../about/presentation/about_tab.dart';
import '../../community/presentation/community_tab.dart';
import '../../settings/presentation/settings_tab.dart';
import '../../community/presentation/social_pages.dart';
import 'aviso_ao_vivo.dart';
import 'faixa_de_atualizacao.dart';
import 'boas_vindas.dart';
import 'projects_tab.dart';
import 'release_notice.dart';

final homeTabProvider = StateProvider<int>((ref) => 0);
final novoProjetoSolicitadoProvider = StateProvider<int>((ref) => 0);

/// Casca principal: as abas e o "+" de criar no meio da barra.
///
/// - filhos do [IndexedStack] com [PageStorageKey]: o scroll de cada aba
///   sobrevive a troca sem reconstruir a lista do zero;
/// - a barra e chapada (tom, sem blur): nada de passe de GPU por quadro;
/// - [_TabItem] e const e nao aloca estilo por build (estilos estaticos).
class HomeShell extends ConsumerStatefulWidget {
  const HomeShell({super.key});

  @override
  ConsumerState<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends ConsumerState<HomeShell> {
  // Record const: nome, icone, icone ativo. Uma alocacao so, na classe.
  static const _tabs = <({IconData icon, IconData active, String label})>[
    (
      icon: CupertinoIcons.house,
      active: CupertinoIcons.house_fill,
      label: 'Inicio',
    ),
    // A COMUNIDADE FICA EM SEGUNDO, ao lado do Inicio: e para onde se
    // vai depois de terminar um trabalho, e nao um canto de ajustes.
    (
      icon: CupertinoIcons.person_2,
      active: CupertinoIcons.person_2_fill,
      label: 'Comunidade',
    ),
    (
      icon: CupertinoIcons.slider_horizontal_3,
      active: CupertinoIcons.slider_horizontal_3,
      label: 'Ajustes',
    ),
    (
      icon: CupertinoIcons.person,
      active: CupertinoIcons.person_fill,
      label: 'Perfil',
    ),
    (
      icon: CupertinoIcons.info_circle,
      active: CupertinoIcons.info_circle_fill,
      label: 'Sobre',
    ),
  ];

  // PageStorageKey preserva o CustomScrollView de cada aba entre trocas.
  static const _tabBodies = <Widget>[
    ProjectsTab(key: PageStorageKey('tab-inicio')),
    CommunityTab(key: PageStorageKey('tab-comunidade')),
    SettingsTab(key: PageStorageKey('tab-ajustes')),
    SocialProfilePage(key: PageStorageKey('tab-perfil'), embedded: true),
    AboutTab(key: PageStorageKey('tab-sobre')),
  ];

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
      if (!mounted || ModalRoute.of(context)?.isCurrent != true) return;
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
    if (!mounted || ModalRoute.of(context)?.isCurrent != true) return;
    await showReleaseNotice(context);
    // Persist after dismissal, so an interrupted launch can show it again.
    try {
      await prefs.setString(releaseNoticeSeenKey, releaseNoticeRevision);
    } catch (_) {
      // A preferences failure must never prevent opening the editor.
    }
  }

  void _select(int i) {
    if (i == ref.read(homeTabProvider)) return;
    HapticFeedback.selectionClick();
    ref.read(homeTabProvider.notifier).state = i;
  }

  @override
  Widget build(BuildContext context) {
    final index = ref.watch(homeTabProvider);
    return Scaffold(
      body: Column(
        children: [
          // O AVISO AO VIVO fica ACIMA das abas: e a unica coisa que o
          // app inteiro mostra sem ninguem pedir, e por isso vive fora
          // de qualquer aba.
          const AvisoAoVivo(),
          // A VERSAO NOVA fica embaixo dos avisos e acima das abas, pelo
          // mesmo motivo: e coisa que o app inteiro mostra sem ninguem
          // pedir. O aviso vem primeiro porque ele e sobre o que esta
          // acontecendo AGORA; a atualizacao pode esperar um minuto.
          const FaixaDeAtualizacao(),
          Expanded(
            child: IndexedStack(index: index, children: _tabBodies),
          ),
        ],
      ),
      // A BARRA DE ABAS E CHAPADA: tom de palco (o mais fundo) abaixo do
      // cromo das abas, sem blur e sem linha. O blur custava um passe de
      // GPU inteiro a cada quadro de rolagem, so para um vidro que a
      // separacao por tom ja resolve.
      bottomNavigationBar: ColoredBox(
        color: AureaCores.palco,
        child: SafeArea(
          top: false,
          child: SizedBox(
            height: 54,
            child: Row(
              children: [
                for (final i in const [0, 1, -1, 3, 2])
                  Expanded(
                    child: i == -1
                        ? Center(
                            child: Semantics(
                              button: true,
                              label: translate(context, 'Criar projeto'),
                              child: Tocavel(
                                key: const ValueKey('home-criar'),
                                haptico: true,
                                onTap: () {
                                  _select(0);
                                  ref
                                      .read(
                                        novoProjetoSolicitadoProvider.notifier,
                                      )
                                      .state++;
                                },
                                child: Container(
                                  width: 52,
                                  height: 34,
                                  decoration: BoxDecoration(
                                    color: AureaCores.acao,
                                    borderRadius: BorderRadius.circular(
                                      AureaDims.raioPilula,
                                    ),
                                  ),
                                  child: Icon(
                                    CupertinoIcons.plus,
                                    size: AureaDims.iconeLg,
                                    color: AureaCores.sobreAcao,
                                  ),
                                ),
                              ),
                            ),
                          )
                        : _TabItem(
                            icon: _tabs[i].icon,
                            activeIcon: _tabs[i].active,
                            label: _tabs[i].label,
                            selected: i == index,
                            onTap: () => _select(i),
                          ),
                  ),
              ],
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

  // Estilos estaticos: zero alocacao de TextStyle por build.
  static const _selectedStyle = TextStyle(
    fontSize: 10.5,
    fontWeight: FontWeight.w500,
    letterSpacing: 0.1,
  );
  static const _idleStyle = TextStyle(
    fontSize: 10.5,
    fontWeight: FontWeight.w500,
    letterSpacing: 0.1,
  );

  @override
  Widget build(BuildContext context) {
    final color = selected ? AureaCores.destaque : AureaCores.textoSecundario;
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
              style: (selected ? _selectedStyle : _idleStyle).copyWith(
                color: color,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
