import 'package:flutter/cupertino.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

/// PEDIR UM NOME, E DEVOLVER NULO QUANDO NAO VEM NOME.
///
/// Ele nasceu dentro do estudio 3D e era usado por meia duzia de telas de
/// fora dele (adicionar camada, ajustes do projeto, acoes da camada). Com
/// o estudio apagado, o nome continuou sendo preciso — entao ele sai de
/// la e passa a morar no lugar dos dialogos da casa.
///
/// Vazio e nulo, e nao string vazia: quem chama trata o cancelamento e o
/// "nao escrevi nada" do mesmo jeito, e um nome em branco na lista de
/// camadas seria uma linha sem nada escrito.
Future<String?> pedirNome(
  BuildContext context, {
  required String titulo,
  required String atual,
}) async {
  final ctrl = TextEditingController(text: atual);
  final r = await showCupertinoDialog<String>(
    context: context,
    builder: (ctx) => CupertinoAlertDialog(
      title: AppText(titulo),
      content: Padding(
        padding: const EdgeInsets.only(top: 12),
        child: CupertinoTextField(
          key: const ValueKey('estudio-nome'),
          controller: ctrl,
          autofocus: true,
          onSubmitted: (v) => Navigator.pop(ctx, v),
        ),
      ),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.pop(ctx),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          isDefaultAction: true,
          onPressed: () => Navigator.pop(ctx, ctrl.text),
          child: const AppText('OK'),
        ),
      ],
    ),
  );
  ctrl.dispose();
  final nome = r?.trim();
  return nome == null || nome.isEmpty ? null : nome;
}
