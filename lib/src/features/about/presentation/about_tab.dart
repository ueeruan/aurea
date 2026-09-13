import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/theme/app_theme.dart';
import '../../../core/widgets/aurea_logo.dart';
import 'report_sheet.dart';
import '../../help/presentation/quick_guide_screen.dart';

const _appVersion = AureaAutor.versao;

/// Aba Sobre: identidade do app, versao e creditos.
class AboutTab extends StatelessWidget {
  const AboutTab({super.key});

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
            child: AppText('Aurea',
              style: Theme.of(context).textTheme.headlineLarge
                  ?.copyWith(fontSize: 28),
            ),
          ),
          const SizedBox(height: 4),
          Center(
            child: AppText('Editor de video e composicao',
              style: Theme.of(context).textTheme.bodySmall,
            ),
          ),
          const SizedBox(height: 10),
          Center(
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 5),
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
          const SizedBox(height: 24),
          const BetaBanner(),
          const SizedBox(height: 18),
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: AppColors.surface,
              borderRadius: BorderRadius.circular(16),
            ),
            child: AppText('Aurea e um editor de video e composicao para celular: '
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
                  subtitle: const AppText('Guia rápido e ajuda dos efeitos · offline',
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
                  subtitle: AppText('Flutter + FFmpeg',
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
                  subtitle: AppText('Vai direto para o criador',
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
