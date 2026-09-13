import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_theme.dart';
import '../../../../core/widgets/aurea_logo.dart';
import '../../../community/application/conta_da_comunidade.dart';
import '../home_shell.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

/// O CABEÇALHO ANIMADO DE BOAS-VINDAS DA HOME.
///
/// Entrada escalonada:
/// 1. Emblema Áurea escala com pulso suave e aura luminosa.
/// 2. Saudação dinâmica ("Bom dia / tarde / noite") personalizada
///    com o apelido real da conta Cloudflare quando conectada.
/// 3. Subtítulo que convida à criação de animações e efeitos.
/// 4. Pílula de perfil no canto direito que leva diretamente à aba
///    do Perfil ou ao login rápido.
class AureaWelcomeHeader extends ConsumerStatefulWidget {
  const AureaWelcomeHeader({super.key});

  @override
  ConsumerState<AureaWelcomeHeader> createState() => _AureaWelcomeHeaderState();
}

class _AureaWelcomeHeaderState extends ConsumerState<AureaWelcomeHeader>
    with SingleTickerProviderStateMixin {
  late final AnimationController _anim;
  late final Animation<double> _logoScale;
  late final Animation<double> _titleFade;
  late final Animation<Offset> _titleSlide;
  late final Animation<double> _subtitleFade;

  @override
  void initState() {
    super.initState();
    _anim = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 900),
    );

    _logoScale = CurvedAnimation(
      parent: _anim,
      curve: const Interval(0.0, 0.65, curve: Curves.easeOutBack),
    );

    _titleFade = CurvedAnimation(
      parent: _anim,
      curve: const Interval(0.25, 0.75, curve: Curves.easeOut),
    );

    _titleSlide = Tween<Offset>(
      begin: const Offset(0, 0.25),
      end: Offset.zero,
    ).animate(
      CurvedAnimation(
        parent: _anim,
        curve: const Interval(0.25, 0.75, curve: Curves.easeOutCubic),
      ),
    );

    _subtitleFade = CurvedAnimation(
      parent: _anim,
      curve: const Interval(0.45, 1.0, curve: Curves.easeOut),
    );

    _anim.forward();
  }

  @override
  void dispose() {
    _anim.dispose();
    super.dispose();
  }

  String _saudacao() {
    final hora = DateTime.now().hour;
    if (hora >= 5 && hora < 12) return 'Bom dia';
    if (hora >= 12 && hora < 18) return 'Boa tarde';
    return 'Boa noite';
  }

  @override
  Widget build(BuildContext context) {
    final conta = ref.watch(contaDaComunidadeProvider);
    final saudacao = _saudacao();
    final apelido = conta?.apelido;

    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 10, 20, 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              // Logotipo com animação de escala e brilho
              ScaleTransition(
                scale: _logoScale,
                child: Container(
                  width: 44,
                  height: 44,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    boxShadow: [
                      BoxShadow(
                        color: AppColors.lime.withValues(alpha: 0.35),
                        blurRadius: 18,
                        spreadRadius: 1,
                      ),
                    ],
                  ),
                  child: const Center(child: AureaLogo(size: 40)),
                ),
              ),
              const SizedBox(width: 14),
              // Saudação e Título com Slide + Fade
              Expanded(
                child: SlideTransition(
                  position: _titleSlide,
                  child: FadeTransition(
                    opacity: _titleFade,
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            AppText(
                              apelido != null
                                  ? '$saudacao, '
                                  : 'Bem-vindo ao ',
                              style: TextStyle(
                                fontSize: 13,
                                fontWeight: FontWeight.w500,
                                color: AppColors.muted,
                                letterSpacing: 0.2,
                              ),
                            ),
                            if (apelido != null)
                              Flexible(
                                child: AppText(
                                  apelido,
                                  maxLines: 1,
                                  overflow: TextOverflow.ellipsis,
                                  style: TextStyle(
                                    fontSize: 13,
                                    fontWeight: FontWeight.w700,
                                    color: AppColors.lime,
                                  ),
                                ),
                              ),
                          ],
                        ),
                        const SizedBox(height: 2),
                        ShaderMask(
                          shaderCallback: (bounds) => const LinearGradient(
                            colors: [Color(0xFFFFFFFF), Color(0xFFD4E0ED)],
                          ).createShader(bounds),
                          child: const AppText('Áurea Motion',
                            style: TextStyle(
                              fontSize: 24,
                              fontWeight: FontWeight.w800,
                              letterSpacing: -0.6,
                              color: Colors.white,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
              // Botão de perfil / avatar no canto superior
              GestureDetector(
                onTap: () {
                  ref.read(homeTabProvider.notifier).state = 3;
                },
                child: AnimatedContainer(
                  duration: const Duration(milliseconds: 250),
                  padding: const EdgeInsets.symmetric(
                    horizontal: 10,
                    vertical: 6,
                  ),
                  decoration: BoxDecoration(
                    color: AppColors.surface,
                    borderRadius: BorderRadius.circular(24),
                    border: Border.all(
                      color: conta != null
                          ? AppColors.lime.withValues(alpha: 0.5)
                          : AppColors.hairline,
                      width: 1,
                    ),
                    boxShadow: [
                      if (conta != null)
                        BoxShadow(
                          color: AppColors.lime.withValues(alpha: 0.15),
                          blurRadius: 10,
                        ),
                    ],
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      if (conta?.avatar != null &&
                          File(conta!.avatar!).existsSync())
                        ClipOval(
                          child: Image.file(
                            File(conta.avatar!),
                            width: 22,
                            height: 22,
                            fit: BoxFit.cover,
                          ),
                        )
                      else
                        Container(
                          width: 22,
                          height: 22,
                          decoration: BoxDecoration(
                            shape: BoxShape.circle,
                            gradient: LinearGradient(
                              colors: conta != null
                                  ? [AppColors.lime, AppColors.violet]
                                  : [
                                      AppColors.muted.withValues(alpha: .4),
                                      AppColors.muted.withValues(alpha: .2),
                                    ],
                            ),
                          ),
                          alignment: Alignment.center,
                          child: AppText(
                            conta?.inicial ?? '?',
                            style: const TextStyle(
                              fontSize: 11,
                              fontWeight: FontWeight.w700,
                              color: Colors.black,
                            ),
                          ),
                        ),
                      const SizedBox(width: 6),
                      AppText(
                        conta != null ? 'Perfil' : 'Entrar',
                        style: TextStyle(
                          fontSize: 12,
                          fontWeight: FontWeight.w600,
                          color: conta != null
                              ? AppColors.onDark
                              : AppColors.muted,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 6),
          // Subtítulo descritivo animado
          FadeTransition(
            opacity: _subtitleFade,
            child: AppText('Estúdio de animação gráfica, vídeo e 3D em tempo real.',
              style: TextStyle(
                fontSize: 12.5,
                color: AppColors.muted.withValues(alpha: 0.85),
                letterSpacing: 0.1,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
