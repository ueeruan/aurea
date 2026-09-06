import 'dart:async';

import 'package:flutter/material.dart';

/// AVISO RAPIDO que SEMPRE some.
///
/// O SnackBar do Flutter so comeca a contar o tempo dele quando a
/// animacao de entrada termina. Num editor que reconstroi a arvore a
/// cada quadro de preview, essa animacao as vezes nao chega ao fim — e
/// o aviso fica preso na tela para sempre, que e exatamente o bug de
/// "removi uma camada e o aviso nao sai".
///
/// Aqui o fechamento nao depende disso: um Timer proprio fecha o aviso
/// no prazo, aconteca o que acontecer com a animacao.
class AureaSnack {
  AureaSnack._();

  static Timer? _timer;

  static void show(
    BuildContext context,
    String message, {
    String? actionLabel,
    VoidCallback? onAction,
    Duration duration = const Duration(seconds: 4),
  }) {
    final messenger = ScaffoldMessenger.maybeOf(context);
    if (messenger == null) return;

    // Um aviso por vez: sem fila acumulada, sem aviso velho reaparecendo
    // depois que o novo some.
    _timer?.cancel();
    messenger.clearSnackBars();

    messenger.showSnackBar(
      SnackBar(
        content: Text(message),
        duration: duration,
        behavior: SnackBarBehavior.floating,
        showCloseIcon: actionLabel == null,
        action: actionLabel == null
            ? null
            : SnackBarAction(
                label: actionLabel,
                onPressed: () {
                  _timer?.cancel();
                  _timer = null;
                  onAction?.call();
                },
              ),
      ),
    );

    // A rede de seguranca: fecha por conta propria.
    _timer = Timer(duration + const Duration(milliseconds: 250), () {
      _timer = null;
      messenger.hideCurrentSnackBar();
    });
  }

  /// Fecha qualquer aviso aberto agora.
  static void hide(BuildContext context) {
    _timer?.cancel();
    _timer = null;
    ScaffoldMessenger.maybeOf(context)?.clearSnackBars();
  }
}
