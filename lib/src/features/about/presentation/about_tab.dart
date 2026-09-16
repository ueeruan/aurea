import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/storage/prefs.dart';
import '../../../core/theme/app_theme.dart';
import '../../../core/ui/snack.dart';
import '../../../core/utils/versao_do_app.dart';
import '../../projects/presentation/release_notice.dart';
import '../../../core/widgets/aurea_logo.dart';
import 'report_sheet.dart';
import '../../help/presentation/quick_guide_screen.dart';

const _appVersion = AureaAutor.versao;

/// A chave do modo escondido: sete toques na versao ligam.
const chaveDoModoDev = 'dev.escondido';

/// Aba Sobre: identidade do app, versao e creditos.
class AboutTab extends ConsumerStatefulWidget {
  const AboutTab({super.key});

  @override
  ConsumerState<AboutTab> createState() => _AboutTabState();
}

class _AboutTabState extends ConsumerState<AboutTab> {
  var _toques = 0;

  bool get _dev {
    try {
      return ref.read(sharedPreferencesProvider).getBool(chaveDoModoDev) ??
          false;
    } catch (_) {
      return false;
    }
  }

  /// SETE TOQUES NA VERSAO ligam as ferramentas de desenvolvedor — o
  /// aperto de mao classico, longe de quem nao procura.
  void _toqueNaVersao() {
    _toques++;
    if (_toques < 7) return;
    _toques = 0;
    final novo = !_dev;
    try {
      ref.read(sharedPreferencesProvider).setBool(chaveDoModoDev, novo);
    } catch (_) {}
    setState(() {});
    AureaSnack.show(
      context,
      novo
          ? 'Ferramentas de desenvolvedor ligadas'
          : 'Ferramentas de desenvolvedor desligadas',
    );
  }

  Future<void> _limparAvisos() async {
    try {
      final prefs = ref.read(sharedPreferencesProvider);
      for (final chave in prefs.getKeys()) {
        if (chave == releaseNoticeSeenKey ||
            chave.startsWith('dica') ||
            chave.startsWith('primeiro')) {
          await prefs.remove(chave);
        }
      }
    } catch (_) {}
    if (mounted) {
      AureaSnack.show(context, 'Avisos e dicas voltam na próxima abertura');
    }
  }

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      bottom: false,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(20, 12, 20, 120),
        children: [
          AppText('Sobre', style: Theme.of(context).textTheme.headlineLarge),
          const SizedBox(height: 32),
          const Center(child: AureaLogo(size: 96)),
          const SizedBox(height: 18),
          Center(
            child: AppText(
              'Aurea',
              style: Theme.of(context).textTheme.headlineLarge
                  ?.copyWith(fontSize: 28),
            ),
          ),
          const SizedBox(height: 4),
          Center(
            child: AppText(
              'Editor de video e composicao',
              style: Theme.of(context).textTheme.bodySmall,
            ),
          ),
          const SizedBox(height: 10),
          Center(
            child: GestureDetector(
              key: const ValueKey('sobre-versao'),
              behavior: HitTestBehavior.opaque,
              onTap: _toqueNaVersao,
              child: Container(
                padding: const EdgeInsets.symmetric(
                  horizontal: 12,
                  vertical: 5,
                ),
                decoration: BoxDecoration(
                  color: AppColors.lime.withValues(alpha: 0.12),
                  borderRadius: BorderRadius.circular(20),
                ),
                child: AppText(
                  'Versao $_appVersion',
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    color: AppColors.lime,
                  ),
                ),
              ),
            ),
          ),
          const SizedBox(height: 24),
          // FERRAMENTAS DE DESENVOLVEDOR: so existem depois dos sete
          // toques, e tudo aqui e reversivel.
          if (_dev) ...[
            Material(
              color: AppColors.surface,
              clipBehavior: Clip.antiAlias,
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(16),
              ),
              child: Column(
                children: [
                  ListTile(
                    key: const ValueKey('dev-build'),
                    leading: Icon(
                      CupertinoIcons.wrench,
                      color: AppColors.lime,
                      size: 21,
                    ),
                    title: const AppText('Ferramentas de desenvolvedor'),
                    subtitle: AppText(
                      'Versao $_appVersion · build $buildDoApp',
                      style: TextStyle(fontSize: 12, color: AppColors.muted),
                    ),
                  ),
                  Padding(
                    padding: const EdgeInsets.only(left: 16),
                    child: Divider(color: AppColors.hairline),
                  ),
                  ListTile(
                    key: const ValueKey('dev-limpar-avisos'),
                    leading: Icon(
                      CupertinoIcons.arrow_counterclockwise,
                      color: AppColors.lime,
                      size: 21,
                    ),
                    title: const AppText('Rever avisos e dicas'),
                    subtitle: AppText(
                      'Novidades da versao e dicas de primeiro uso voltam',
                      style: TextStyle(fontSize: 12, color: AppColors.muted),
                    ),
                    onTap: _limparAvisos,
                  ),
                ],
              ),
            ),
            const SizedBox(height: 18),
          ],
          const BetaBanner(),
          const SizedBox(height: 18),
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: AppColors.surface,
              borderRadius: BorderRadius.circular(16),
            ),
            child: AppText(
              'Aurea e um editor de video e composicao para celular: '
              'timeline multi-trilha, preview em tempo real e exportacao '
              'direto do aparelho, sem depender de nuvem.',
              style: Theme.of(context).textTheme.bodyMedium
                  ?.copyWith(color: AppColors.muted),
            ),
          ),
          const SizedBox(height: 22),
          Material(
            color: AppColors.surface,
            clipBehavior: Clip.antiAlias,
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(16),
            ),
            child: Column(
              children: [
                ListTile(
                  leading: Icon(CupertinoIcons.book, color: AppColors.lime),
                  title: const AppText('Como usar o AUREA'),
                  subtitle: const AppText(
                    'Guia rápido e ajuda dos efeitos · offline',
                  ),
                  trailing: const Icon(CupertinoIcons.chevron_right, size: 16),
                  onTap: () => Navigator.of(context).push(
                    MaterialPageRoute<void>(
                      builder: (_) => const QuickGuideScreen(),
                    ),
                  ),
                ),
                ListTile(
                  leading: Icon(
                    CupertinoIcons.bolt,
                    color: AppColors.lime,
                    size: 21,
                  ),
                  title: AppText('Tecnologia'),
                  subtitle: AppText(
                    'Flutter + FFmpeg',
                    style: TextStyle(fontSize: 12, color: AppColors.muted),
                  ),
                ),
                Padding(
                  padding: const EdgeInsets.only(left: 16),
                  child: Divider(color: AppColors.hairline),
                ),
                ListTile(
                  leading: Icon(
                    CupertinoIcons.exclamationmark_bubble,
                    color: AppColors.lime,
                    size: 21,
                  ),
                  title: const AppText('Reportar erro ou sugerir'),
                  subtitle: AppText(
                    'Vai direto para o criador',
                    style: TextStyle(fontSize: 12, color: AppColors.muted),
                  ),
                  trailing: Icon(
                    CupertinoIcons.chevron_right,
                    size: 16,
                    color: AppColors.muted,
                  ),
                  onTap: () => showReportSheet(context),
                ),
                Padding(
                  padding: const EdgeInsets.only(left: 16),
                  child: Divider(color: AppColors.hairline),
                ),
                ListTile(
                  leading: Icon(
                    CupertinoIcons.person_crop_circle,
                    color: AppColors.lime,
                    size: 21,
                  ),
                  title: const AppText('Criador'),
                  subtitle: AppText(
                    '${AureaAutor.nome}  ·  @${AureaAutor.instagram}  ·  '
                    'TikTok @${AureaAutor.tiktok}',
                    style: TextStyle(fontSize: 12, color: AppColors.muted),
                  ),
                  trailing: Icon(
                    CupertinoIcons.chevron_right,
                    size: 16,
                    color: AppColors.muted,
                  ),
                  onTap: () => showReportSheet(context),
                ),
                Padding(
                  padding: const EdgeInsets.only(left: 16),
                  child: Divider(color: AppColors.hairline),
                ),
                ListTile(
                  leading: Icon(
                    CupertinoIcons.doc_text,
                    color: AppColors.lime,
                    size: 21,
                  ),
                  title: const AppText('Licencas de codigo aberto'),
                  trailing: Icon(
                    CupertinoIcons.chevron_right,
                    size: 16,
                    color: AppColors.muted,
                  ),
                  onTap: () => showLicensePage(
                    context: context,
                    applicationName: 'Aurea',
                    applicationVersion: _appVersion,
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 26),
          Center(
            child: AppText(
              'Feito por ${AureaAutor.nome} com Flutter',
              style: Theme.of(context).textTheme.bodySmall
                  ?.copyWith(fontSize: 11),
            ),
          ),
        ],
      ),
    );
  }
}
