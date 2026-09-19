import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:aurea/src/core/theme/aurea_colors.dart';

import '../../../core/l10n/app_language.dart';
import '../../../core/theme/app_theme.dart';
import '../../../core/ui/tocavel.dart';
import '../../../core/widgets/aurea_logo.dart';

/// A chave do aceite: presente = a pessoa ja passou pela primeira tela.
const chaveDoAceite = 'abertura.aceite';

/// A PRIMEIRA ABERTURA: quem chega ve o que a Aurea faz em tres linhas
/// e combina as regras da casa antes de entrar. Nao volta a aparecer.
Future<void> showBoasVindas(BuildContext context) => showModalBottomSheet<void>(
  context: context,
  isDismissible: false,
  enableDrag: false,
  isScrollControlled: true,
  backgroundColor: AppColors.surface,
  shape: const RoundedRectangleBorder(
    borderRadius: BorderRadius.vertical(top: Radius.circular(24)),
  ),
  builder: (sheet) => SafeArea(
    child: SingleChildScrollView(
      padding: const EdgeInsets.fromLTRB(24, 28, 24, 20),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Center(child: AureaLogo(size: 64)),
          const SizedBox(height: 14),
          Center(
            child: AppText(
              'Bem-vindo à Aurea',
              style: Theme.of(sheet).textTheme.headlineLarge
                  ?.copyWith(fontSize: 24),
            ),
          ),
          const SizedBox(height: 22),
          const _Linha(
            icone: CupertinoIcons.square_stack_3d_up,
            titulo: 'Camadas de verdade',
            texto:
                'Vídeo, foto, texto, forma e som na mesma timeline, '
                'com keyframes em tudo.',
          ),
          const _Linha(
            icone: CupertinoIcons.wand_stars,
            titulo: 'Efeitos que trabalham',
            texto:
                'Mais de noventa efeitos com prévia animada, presets e '
                'busca em português.',
          ),
          const _Linha(
            icone: CupertinoIcons.arrow_up_doc,
            titulo: 'Sai do aparelho pronto',
            texto:
                'Exportação em MP4, GIF, PNG e pacote .aurea para levar '
                'o projeto com as mídias.',
          ),
          const SizedBox(height: 18),
          AppText(
            'Ao continuar, você combina usar a Aurea com mídias que pode '
            'usar: o que você importa e publica é responsabilidade sua.',
            style: TextStyle(fontSize: 12, height: 1.5, color: AppColors.muted),
          ),
          const SizedBox(height: 16),
          SizedBox(
            width: double.infinity,
            height: 52,
            child: Tocavel(
              key: const ValueKey('abertura-comecar'),
              haptico: true,
              onTap: () => Navigator.of(sheet).pop(),
              child: Container(
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: AppColors.lime,
                  borderRadius: BorderRadius.circular(14),
                ),
                child: AppText(
                  'Começar a editar',
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w700,
                    color: AppColors.modoClaro
                        ? const Color(0xFFFFFFFF)
                        : AureaColors.onAccent,
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    ),
  ),
);

class _Linha extends StatelessWidget {
  const _Linha({
    required this.icone,
    required this.titulo,
    required this.texto,
  });

  final IconData icone;
  final String titulo;
  final String texto;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 16),
    child: Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Container(
          width: 40,
          height: 40,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: AppColors.lime.withValues(alpha: .12),
            borderRadius: BorderRadius.circular(12),
          ),
          child: Icon(icone, size: 20, color: AppColors.lime),
        ),
        const SizedBox(width: 14),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              AppText(
                titulo,
                style: TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w700,
                  color: AppColors.onDark,
                ),
              ),
              const SizedBox(height: 2),
              AppText(
                texto,
                style: TextStyle(
                  fontSize: 12.5,
                  height: 1.4,
                  color: AppColors.muted,
                ),
              ),
            ],
          ),
        ),
      ],
    ),
  );
}
