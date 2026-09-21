import 'dart:ui' as ui;

import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';

/// O CABECALHO DA CAMADA (70 x 28), fixo a esquerda enquanto o tempo corre
/// por baixo: faixa de cor (10) · miniatura (32) · olho (44 de toque).
///
/// Toque: seleciona (na ja escolhida, abre ou fecha as linhas das
/// propriedades animadas). Toque longo: pega a camada para REORDENAR — o
/// arrasto vertical leva a camada a outro degrau da pilha.
class CabecalhoDaCamada extends StatelessWidget {
  const CabecalhoDaCamada({
    super.key,
    required this.layerId,
    required this.corDaFaixa,
    required this.icone,
    required this.escolhida,
    required this.oculta,
    required this.temAnimacao,
    required this.expandida,
    required this.aoTocar,
    required this.aoAlternarOlho,
    required this.aoComecarReordenar,
    required this.aoMoverReordenar,
    required this.aoTerminarReordenar,
    this.miniatura,
  });

  final String layerId;
  final Color corDaFaixa;
  final IconData icone;
  final bool escolhida;
  final bool oculta;
  final bool temAnimacao;
  final bool expandida;

  /// Quadro do video (a primeira miniatura da tira), quando ha.
  final ui.Image? miniatura;
  final VoidCallback aoTocar;
  final VoidCallback aoAlternarOlho;
  final ValueChanged<Offset> aoComecarReordenar;
  final ValueChanged<Offset> aoMoverReordenar;

  /// [cancelou] = o sistema tirou o dedo (nao aplica).
  final void Function({required bool cancelou}) aoTerminarReordenar;

  @override
  Widget build(BuildContext context) {
    final corDoIcone = oculta ? AureaCores.textoSecundario : AureaCores.texto;
    // O dedo pode sair do toque longo por caminhos que o reconhecedor nao
    // avisa depois de aceito (o sistema cancela o ponteiro): o `Listener`
    // e a rede de seguranca — sem ele a camada ficaria "levantada".
    return Listener(
      onPointerCancel: (_) => aoTerminarReordenar(cancelou: true),
      child: GestureDetector(
        key: ValueKey('cabecalho-$layerId'),
        behavior: HitTestBehavior.opaque,
        onTap: aoTocar,
        onLongPressStart: (d) => aoComecarReordenar(d.globalPosition),
        onLongPressMoveUpdate: (d) => aoMoverReordenar(d.globalPosition),
        onLongPressEnd: (_) => aoTerminarReordenar(cancelou: false),
        child: ColoredBox(
          color: AureaCores.cromo,
          child: Padding(
            // O fundo do cabecalho e uma pilula de 26 (1 de folga em cima e
            // embaixo), reta na borda da tela e redonda do lado do tempo.
            padding: const EdgeInsets.symmetric(vertical: 1),
            child: DecoratedBox(
              decoration: BoxDecoration(
                color: escolhida ? AureaCores.campoAlto : AureaCores.elevado,
                borderRadius: const BorderRadius.horizontal(
                  right: Radius.circular(AureaDims.raioPilula),
                ),
              ),
              child: Stack(
                children: [
                  // A FAIXA DE COR do tipo (ou da etiqueta da camada).
                  Positioned(
                    left: 0,
                    top: 1,
                    bottom: 1,
                    width: AureaDims.faixaDeCor,
                    child: ColoredBox(
                      color: corDaFaixa.withValues(alpha: oculta ? .45 : 1),
                    ),
                  ),
                  // A MINIATURA: o quadro do video, ou o icone do tipo.
                  Positioned(
                    left: AureaDims.faixaDeCor,
                    top: 0,
                    bottom: 0,
                    width: AureaDims.miniaturaDoCabecalho,
                    child: Padding(
                      // A imagem fica nos 16 da esquerda da miniatura: o
                      // toque do olho (44) comeca em 26, e tocar na
                      // miniatura para escolher nao pode esconder a camada.
                      padding: const EdgeInsets.fromLTRB(2, 3, 14, 3),
                      child: miniatura != null
                          ? ClipRRect(
                              borderRadius: BorderRadius.circular(
                                AureaDims.raioXs,
                              ),
                              child: RawImage(
                                image: miniatura,
                                fit: BoxFit.cover,
                                opacity: AlwaysStoppedAnimation(
                                  oculta ? .45 : 1,
                                ),
                              ),
                            )
                          : Icon(
                              icone,
                              size: AureaDims.iconeSm,
                              color: corDoIcone,
                            ),
                    ),
                  ),
                  // A CAMADA TEM ANIMACAO: a setinha diz que ha linhas para
                  // abrir (toque no cabecalho da camada escolhida).
                  if (temAnimacao)
                    Positioned(
                      left: AureaDims.faixaDeCor + 19,
                      bottom: 1,
                      child: IgnorePointer(
                        child: Icon(
                          expandida
                              ? CupertinoIcons.chevron_down
                              : CupertinoIcons.chevron_right,
                          key: ValueKey('expandir-$layerId'),
                          size: 8,
                          color: AureaCores.keyframe,
                        ),
                      ),
                    ),
                  // O OLHO: 44 de toque na ponta direita do cabecalho.
                  Positioned(
                    right: 0,
                    top: 0,
                    bottom: 0,
                    width: AureaDims.olhoDoCabecalho,
                    child: Semantics(
                      button: true,
                      label: translate(
                        context,
                        oculta ? 'Mostrar camada' : 'Ocultar camada',
                      ),
                      child: GestureDetector(
                        key: ValueKey('olho-$layerId'),
                        behavior: HitTestBehavior.opaque,
                        onTap: () {
                          HapticFeedback.lightImpact();
                          aoAlternarOlho();
                        },
                        child: Padding(
                          padding: const EdgeInsets.fromLTRB(15, 5, 9, 5),
                          child: Icon(
                            oculta
                                ? CupertinoIcons.eye_slash
                                : CupertinoIcons.eye,
                            size: AureaDims.iconeSm,
                            color: corDoIcone,
                          ),
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// A ALCA DE REORDENAR (35), na ponta direita da linha escolhida: o arrasto
/// vertical dela reordena direto, sem esperar o toque longo.
class AlcaDeReordenar extends StatelessWidget {
  const AlcaDeReordenar({
    super.key,
    required this.layerId,
    required this.aoComecar,
    required this.aoMover,
    required this.aoTerminar,
  });

  final String layerId;
  final ValueChanged<Offset> aoComecar;
  final ValueChanged<Offset> aoMover;
  final void Function({required bool cancelou}) aoTerminar;

  @override
  Widget build(BuildContext context) => GestureDetector(
    key: ValueKey('alca-reordenar-$layerId'),
    behavior: HitTestBehavior.opaque,
    onVerticalDragStart: (d) => aoComecar(d.globalPosition),
    onVerticalDragUpdate: (d) => aoMover(d.globalPosition),
    onVerticalDragEnd: (_) => aoTerminar(cancelou: false),
    onVerticalDragCancel: () => aoTerminar(cancelou: true),
    child: Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: DecoratedBox(
        decoration: BoxDecoration(
          color: AureaCores.elevado,
          borderRadius: const BorderRadius.horizontal(
            left: Radius.circular(AureaDims.raioPilula),
          ),
        ),
        child: Padding(
          padding: const EdgeInsets.only(left: 6),
          child: Icon(
            CupertinoIcons.line_horizontal_3,
            size: 14,
            color: AureaCores.textoSecundario,
          ),
        ),
      ),
    ),
  );
}
