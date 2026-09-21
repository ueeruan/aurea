import 'package:flutter/cupertino.dart';

import 'tokens.dart';

/// LIGA/DESLIGA — o interruptor Cupertino, na cor de acao do tema.
///
/// Cupertino e nao Material por regra de estilo do app. Cabe na linha de
/// propriedade de 51 sem encolher.
class AureaToggle extends StatelessWidget {
  const AureaToggle({
    super.key,
    required this.valor,
    required this.aoMudar,
    this.habilitado = true,
  });

  final bool valor;
  final ValueChanged<bool> aoMudar;
  final bool habilitado;

  @override
  Widget build(BuildContext context) => CupertinoSwitch(
    value: valor,
    activeTrackColor: AureaCores.acao,
    inactiveTrackColor: AureaCores.campoAlto,
    onChanged: habilitado ? aoMudar : null,
  );
}
