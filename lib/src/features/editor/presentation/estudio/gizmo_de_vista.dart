import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart';

import '../../../../core/ui/am_colors.dart';
import '../../domain/camera3d.dart';
import '../../domain/estudio_ux.dart';
import 'estado_do_estudio.dart';

/// O GIZMO DE VISTA: para onde a cena esta virada, e um toque para virar.
///
/// Existe porque "não depender apenas de gestos" é um pedido explícito:
/// orbitar com o dedo continua valendo, mas quem não descobriu o gesto
/// precisa de um caminho visível. E porque as vistas ortográficas são a
/// única resposta para "está atrás ou é só menor?" — a perspectiva
/// esconde isso por definição.
///
/// Fechado ele é um cubinho com o nome da vista. Aberto, as seis faces
/// mais o retorno à câmera.
class GizmoDeVista extends StatefulWidget {
  const GizmoDeVista({
    super.key,
    required this.navegacao,
    required this.camera,
    required this.tempo,
  });

  final NavegacaoDaVista navegacao;
  final Camera3D camera;
  final Duration tempo;

  @override
  State<GizmoDeVista> createState() => _GizmoDeVistaState();
}

class _GizmoDeVistaState extends State<GizmoDeVista> {
  bool _aberto = false;

  void _ir(SceneView v) {
    widget.navegacao.verVista(v, camera: widget.camera, tempo: widget.tempo);
    setState(() => _aberto = false);
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: widget.navegacao,
    builder: (context, _) {
      if (!_aberto) {
        return Semantics(
          container: true,
          excludeSemantics: true,
          button: true,
          label: 'Vista: ${sceneViewLabel(widget.navegacao.vista)}',
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: () => setState(() => _aberto = true),
            child: Container(
              width: 44,
              height: 44,
              decoration: BoxDecoration(
                color: AmColors.panelHigh.withValues(alpha: .92),
                borderRadius: BorderRadius.circular(12),
                border: Border.all(color: AmColors.hairline),
              ),
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(
                    Icons.view_in_ar_rounded,
                    size: 18,
                    color: AmColors.accent,
                  ),
                  const SizedBox(height: 2),
                  FittedBox(
                    child: Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 3),
                      child: AppText(
                        sceneViewLabel(widget.navegacao.vista),
                        style: const TextStyle(
                          fontSize: 8,
                          color: AmColors.muted,
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        );
      }
      return Container(
        width: 150,
        padding: const EdgeInsets.all(8),
        decoration: BoxDecoration(
          color: AmColors.panelHigh.withValues(alpha: .96),
          borderRadius: BorderRadius.circular(14),
          border: Border.all(color: AmColors.hairline),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          mainAxisSize: MainAxisSize.min,
          children: [
            _Face(
              rotulo: 'Vista Camera',
              texto: 'Camera',
              icone: Icons.videocam_rounded,
              escolhida: widget.navegacao.pelaCamera,
              aoTocar: () => _ir(SceneView.camera),
            ),
            const Divider(height: 9, color: AmColors.hairline),
            Wrap(
              spacing: 5,
              runSpacing: 5,
              children: [
                for (final v in vistasPredefinidas)
                  _Chip(
                    rotulo: sceneViewLabel(v),
                    escolhida: widget.navegacao.vista == v,
                    aoTocar: () => _ir(v),
                  ),
              ],
            ),
            const Divider(height: 9, color: AmColors.hairline),
            // A VISTA LIVRE NAO MEXE NA CAMERA: e uma bancada de
            // trabalho. E o comando "alinhar camera a vista" que
            // compromete o que se achou aqui.
            _Face(
              rotulo: 'Vista Livre',
              texto: 'Livre',
              icone: Icons.threed_rotation_rounded,
              escolhida: widget.navegacao.livre,
              aoTocar: () => _ir(SceneView.custom1),
            ),
          ],
        ),
      );
    },
  );
}

class _Face extends StatelessWidget {
  const _Face({
    required this.rotulo,
    required this.texto,
    required this.icone,
    required this.escolhida,
    required this.aoTocar,
  });

  final String rotulo;
  final String texto;
  final IconData icone;
  final bool escolhida;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    selected: escolhida,
    label: rotulo,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: SizedBox(
        height: 30,
        child: Row(
          children: [
            Icon(
              icone,
              size: 15,
              color: escolhida ? AmColors.accent : AmColors.muted,
            ),
            const SizedBox(width: 7),
            AppText(texto,
              style: TextStyle(
                fontSize: 11.5,
                fontWeight: FontWeight.w600,
                color: escolhida ? AmColors.accent : AmColors.text,
              ),
            ),
          ],
        ),
      ),
    ),
  );
}

class _Chip extends StatelessWidget {
  const _Chip({
    required this.rotulo,
    required this.escolhida,
    required this.aoTocar,
  });

  final String rotulo;
  final bool escolhida;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    selected: escolhida,
    label: 'Vista $rotulo',
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 6),
        decoration: BoxDecoration(
          color: escolhida ? AmColors.chip : null,
          borderRadius: BorderRadius.circular(7),
          border: escolhida ? null : Border.all(color: AmColors.hairline),
        ),
        child: AppText(
          rotulo,
          style: TextStyle(
            fontSize: 10.5,
            fontWeight: FontWeight.w600,
            color: escolhida ? AmColors.accent : AmColors.text,
          ),
        ),
      ),
    ),
  );
}
