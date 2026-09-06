import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_theme.dart';
import '../../projects/application/projects_controller.dart';
import '../application/user_profile_controller.dart';

/// Aba Usuario: perfil local editavel e resumo de atividade.
class UserTab extends ConsumerWidget {
  const UserTab({super.key});

  Future<void> _editProfile(BuildContext context, WidgetRef ref) async {
    final profile = ref.read(userProfileProvider);
    final nameController = TextEditingController(text: profile.name);
    final emailController = TextEditingController(text: profile.email);

    final saved = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: AppColors.surfaceHigh,
        shape:
            RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
        title: const Text('Editar perfil'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            CupertinoTextField(
              controller: nameController,
              placeholder: 'Nome',
              textCapitalization: TextCapitalization.words,
              style: const TextStyle(color: AppColors.onDark, fontSize: 16),
              padding: const EdgeInsets.symmetric(
                  horizontal: 14, vertical: 12),
              decoration: BoxDecoration(
                color: AppColors.background,
                borderRadius: BorderRadius.circular(12),
              ),
            ),
            const SizedBox(height: 10),
            CupertinoTextField(
              controller: emailController,
              placeholder: 'E-mail (opcional)',
              keyboardType: TextInputType.emailAddress,
              style: const TextStyle(color: AppColors.onDark, fontSize: 16),
              padding: const EdgeInsets.symmetric(
                  horizontal: 14, vertical: 12),
              decoration: BoxDecoration(
                color: AppColors.background,
                borderRadius: BorderRadius.circular(12),
              ),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Cancelar'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Salvar'),
          ),
        ],
      ),
    );

    if (saved == true) {
      ref.read(userProfileProvider.notifier).setName(nameController.text);
      ref.read(userProfileProvider.notifier).setEmail(emailController.text);
    }
    nameController.dispose();
    emailController.dispose();
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final profile = ref.watch(userProfileProvider);
    final projectCount = ref.watch(projectsControllerProvider).length;

    return SafeArea(
      bottom: false,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(20, 12, 20, 120),
        children: [
          Text('Usuario', style: Theme.of(context).textTheme.headlineLarge),
          const SizedBox(height: 28),
          Center(
            child: Column(
              children: [
                Container(
                  width: 96,
                  height: 96,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    gradient: const LinearGradient(
                      begin: Alignment.topLeft,
                      end: Alignment.bottomRight,
                      colors: [AppColors.lime, AppColors.violet],
                    ),
                    boxShadow: [
                      BoxShadow(
                        color: AppColors.violet.withValues(alpha: 0.30),
                        blurRadius: 28,
                      ),
                    ],
                  ),
                  child: Center(
                    child: Container(
                      width: 90,
                      height: 90,
                      decoration: const BoxDecoration(
                        shape: BoxShape.circle,
                        color: AppColors.background,
                      ),
                      alignment: Alignment.center,
                      child: Text(
                        profile.initial,
                        style: const TextStyle(
                          fontSize: 36,
                          fontWeight: FontWeight.w700,
                          letterSpacing: -0.5,
                          color: AppColors.lime,
                        ),
                      ),
                    ),
                  ),
                ),
                const SizedBox(height: 14),
                Text(profile.name,
                    style: Theme.of(context).textTheme.titleLarge),
                if (profile.email.isNotEmpty)
                  Padding(
                    padding: const EdgeInsets.only(top: 2),
                    child: Text(profile.email,
                        style: Theme.of(context).textTheme.bodySmall),
                  ),
                const SizedBox(height: 14),
                CupertinoButton(
                  padding: const EdgeInsets.symmetric(
                      horizontal: 18, vertical: 8),
                  color: AppColors.surfaceHigh,
                  borderRadius: BorderRadius.circular(20),
                  onPressed: () => _editProfile(context, ref),
                  child: const Text(
                    'Editar perfil',
                    style: TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w600,
                      letterSpacing: -0.2,
                      color: AppColors.lime,
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 30),
          Row(
            children: [
              Expanded(
                child: _StatCard(
                  icon: CupertinoIcons.film,
                  value: '$projectCount',
                  label: projectCount == 1 ? 'Projeto' : 'Projetos',
                ),
              ),
              const SizedBox(width: 12),
              const Expanded(
                child: _StatCard(
                  icon: CupertinoIcons.share,
                  value: '0',
                  label: 'Exportacoes',
                ),
              ),
            ],
          ),
          const SizedBox(height: 22),
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: AppColors.surface,
              borderRadius: BorderRadius.circular(16),
            ),
            child: Row(
              children: [
                const Icon(CupertinoIcons.cloud,
                    color: AppColors.muted, size: 20),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    'Perfil local. Login e sincronizacao na nuvem chegam em versoes futuras.',
                    style: Theme.of(context).textTheme.bodySmall,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _StatCard extends StatelessWidget {
  const _StatCard({
    required this.icon,
    required this.value,
    required this.label,
  });

  final IconData icon;
  final String value;
  final String label;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 18),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(16),
      ),
      child: Column(
        children: [
          Icon(icon, color: AppColors.lime, size: 21),
          const SizedBox(height: 8),
          Text(
            value,
            style: const TextStyle(
              fontSize: 22,
              fontWeight: FontWeight.w700,
              letterSpacing: -0.4,
              color: AppColors.onDark,
            ),
          ),
          Text(label, style: Theme.of(context).textTheme.bodySmall),
        ],
      ),
    );
  }
}
