import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';

/// O AVISO DE CAMADA BLOQUEADA, num lugar so.
///
/// O cadeado recusa em silencio no controlador (ver `_replace`), porque
/// quem recusa nem sempre tem tela — e o desfazer e a exportacao passam
/// pelo mesmo caminho. Quem TEM tela chama isto: a frase diz o que
/// aconteceu e o botao desfaz o bloqueio ali mesmo, sem obrigar a achar o
/// cadeado.
///
/// Um aviso so existe porque a acao foi PEDIDA: quem chega aqui esta com
/// o dedo na camada. Recusar sem dizer nada e o que faz a pessoa concluir
/// que o app travou.
void avisarCamadaBloqueada(
  BuildContext context,
  WidgetRef ref,
  String mensagem,
  String camadaId,
) {
  AureaSnack.show(
    context,
    mensagem,
    actionLabel: 'Desbloquear',
    onAction: () =>
        ref.read(editorControllerProvider.notifier).toggleLocked(camadaId),
  );
}

/// A frase padrao de uma acao recusada pelo cadeado.
String fraseDeBloqueio(String acao) =>
    'Camada bloqueada: desbloqueie para $acao';
