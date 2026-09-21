import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../shell/contrato.dart';
import 'comum.dart';

/// TRANSFORMAR — posicao, escala, rotacao, opacidade, inclinacao e pivo,
/// uma sub-aba por propriedade (como na referencia: a aba ativa e a
/// propriedade cujos losangos importam).
///
/// Tudo pela API do controlador (`editPosition`, `editScaleUniform`...),
/// com o `t` do cabecote VIVO e um passo de desfazer por arrasto.
class PainelTransformar extends ConsumerStatefulWidget {
  const PainelTransformar({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelTransformar> createState() => _PainelTransformarState();
}

class _PainelTransformarState extends ConsumerState<PainelTransformar> {
  int _aba = 0;

  static const _abas = [
    'Posição',
    'Escala',
    'Rotação',
    'Opacidade',
    'Inclinação',
    'Pivô',
  ];

  static const _props = [
    LayerProp.position,
    LayerProp.scale,
    LayerProp.rotation,
    LayerProp.opacity,
    LayerProp.skew,
    LayerProp.pivot,
  ];

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final visivel = camadaVisivel(ref, widget.layerId);
    final gravada = camadaGravada(ref, widget.layerId);
    if (visivel == null || gravada == null) {
      return const PainelSemCamada(titulo: 'Transformar');
    }
    final c = ref.read(editorControllerProvider.notifier);
    return AureaPanel(
      titulo: 'Transformar',
      chave: 'painel-${PainelId.transformar.name}',
      abas: _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: escopo.fecharPainel,
      acoes: [
        Tocavel(
          key: const ValueKey('transformar-3d'),
          onTap: () => c.toggle3D(widget.layerId),
          child: SizedBox(
            width: AureaDims.toqueConfortavel,
            height: AureaDims.cabecalhoDoPainel,
            child: Icon(
              CupertinoIcons.cube,
              size: AureaDims.iconeMd,
              color: visivel.is3D
                  ? AureaCores.destaque
                  : AureaCores.textoSecundario,
            ),
          ),
        ),
      ],
      corpo: NoCabecote(
        construir: (context, t) {
          final local = visivel.localTime(t);
          final prop = _props[_aba];
          final kf = losangoDaPropriedade(
            ref,
            gravada: gravada,
            prop: prop,
            t: t,
            playback: escopo.playback,
          );
          final id = widget.layerId;
          AureaPropertyRow numero(
            String rotulo,
            double valor,
            ValueChanged<double> mudar, {
            double min = double.negativeInfinity,
            double max = double.infinity,
            String unidade = '',
            int casas = 1,
            bool comLosango = true,
          }) => AureaPropertyRow(
            rotulo: rotulo,
            valor: valor,
            aoMudar: mudar,
            min: min,
            max: max,
            unidade: unidade,
            casas: casas,
            keyframe: comLosango ? kf.estado : null,
            aoAnterior: kf.anterior,
            aoProximo: kf.proximo,
            aoResetar: () => c.resetProp(id, prop),
            aoComecarGesto: c.beginGesture,
            aoTerminarGesto: c.endGesture,
          );

          final linhas = <Widget>[
            ...switch (prop) {
              LayerProp.position => [
                AureaPropertyRow.ponto(
                  rotulo: 'Posição',
                  x: visivel.position.valueAt(local).dx,
                  y: visivel.position.valueAt(local).dy,
                  aoMudarX: (v) => c.editPosition(
                    id,
                    t,
                    Offset(v, visivel.position.valueAt(local).dy),
                  ),
                  aoMudarY: (v) => c.editPosition(
                    id,
                    t,
                    Offset(visivel.position.valueAt(local).dx, v),
                  ),
                  keyframe: kf.estado,
                  aoAnterior: kf.anterior,
                  aoProximo: kf.proximo,
                  aoResetar: () => c.resetProp(id, prop),
                  aoComecarGesto: c.beginGesture,
                  aoTerminarGesto: c.endGesture,
                ),
                if (visivel.is3D)
                  numero(
                    'Profundidade',
                    visivel.positionZ.valueAt(local),
                    (v) => c.editPositionZ(id, t, v),
                    comLosango: false,
                  ),
              ],
              LayerProp.scale => [
                numero(
                  'Escala',
                  visivel.scaleX.valueAt(local) * 100,
                  (v) => c.editScaleUniform(id, t, v / 100),
                  min: 0,
                  max: 400,
                  unidade: '%',
                ),
                numero(
                  'Largura',
                  visivel.scaleX.valueAt(local) * 100,
                  (v) => c.editScaleX(id, t, v / 100),
                  unidade: '%',
                  comLosango: false,
                ),
                numero(
                  'Altura',
                  visivel.scaleY.valueAt(local) * 100,
                  (v) => c.editScaleY(id, t, v / 100),
                  unidade: '%',
                  comLosango: false,
                ),
              ],
              LayerProp.rotation => [
                numero(
                  'Rotação',
                  visivel.rotation.valueAt(local),
                  (v) => c.editRotation(id, t, v),
                  unidade: '°',
                ),
                if (visivel.is3D) ...[
                  numero(
                    'Rotação X',
                    visivel.rotationX.valueAt(local),
                    (v) => c.editRotationX(id, t, v),
                    unidade: '°',
                    comLosango: false,
                  ),
                  numero(
                    'Rotação Y',
                    visivel.rotationY.valueAt(local),
                    (v) => c.editRotationY(id, t, v),
                    unidade: '°',
                    comLosango: false,
                  ),
                ],
              ],
              LayerProp.opacity => [
                numero(
                  'Opacidade',
                  visivel.opacity.valueAt(local) * 100,
                  (v) => c.editOpacity(id, t, v / 100),
                  min: 0,
                  max: 100,
                  unidade: '%',
                  casas: 0,
                ),
              ],
              LayerProp.skew => [
                numero(
                  'Inclinação X',
                  visivel.skewX.valueAt(local),
                  (v) => c.editSkewX(id, t, v),
                  unidade: '°',
                ),
                numero(
                  'Inclinação Y',
                  visivel.skewY.valueAt(local),
                  (v) => c.editSkewY(id, t, v),
                  unidade: '°',
                  comLosango: false,
                ),
              ],
              LayerProp.pivot => [
                AureaPropertyRow.ponto(
                  rotulo: 'Pivô',
                  x: visivel.pivot.valueAt(local).dx,
                  y: visivel.pivot.valueAt(local).dy,
                  aoMudarX: (v) => c.editPivot(
                    id,
                    t,
                    Offset(v, visivel.pivot.valueAt(local).dy),
                  ),
                  aoMudarY: (v) => c.editPivot(
                    id,
                    t,
                    Offset(visivel.pivot.valueAt(local).dx, v),
                  ),
                  keyframe: kf.estado,
                  aoAnterior: kf.anterior,
                  aoProximo: kf.proximo,
                  aoResetar: () => c.resetProp(id, prop),
                  aoComecarGesto: c.beginGesture,
                  aoTerminarGesto: c.endGesture,
                ),
              ],
              LayerProp.parent => const <Widget>[],
            },
          ];
          return ListView(
            padding: const EdgeInsets.fromLTRB(
              AureaDims.margemDoPainel,
              AureaDims.e4,
              AureaDims.margemDoPainel,
              AureaDims.topoDoPainel,
            ),
            children: linhas,
          );
        },
      ),
    );
  }
}
