import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'pecas_centrais.dart';

/// TRANSFORMAR — o "Move & Transform" da referencia: uma sub-aba por
/// propriedade, e a sub-aba ATIVA e a propriedade cujos keyframes a
/// timeline destaca ([propriedadeAtivaProvider]).
///
///   Posicao    X · Y (e Profundidade com a camada em 3D)
///   Escala     uniforme · largura · altura
///   Rotacao    Z (e X · Y em 3D, com o interruptor da camada 3D)
///   Opacidade  0–100 %
///   Inclinacao X · Y
///   Pivo       X · Y
///
/// UM SISTEMA DE KEYFRAME: todas as linhas de uma aba mostram o MESMO
/// losango, porque sao a mesma trilha no controlador (`toggleKeyframe`
/// crava X, Y e Z de uma vez). Tres losangos independentes mentiriam.
///
/// Tudo pela API do controlador, com o `t` do cabecote VIVO, um passo de
/// desfazer por arrasto (aoComecarGesto/aoTerminarGesto) e o sinal de
/// interacao a cada passo.
class PainelTransformar extends ConsumerStatefulWidget {
  const PainelTransformar({super.key, required this.layerId});

  final String layerId;

  /// As abas, na ordem da referencia. Publico: o teste e a timeline leem.
  static const abas = [
    'Posição',
    'Escala',
    'Rotação',
    'Opacidade',
    'Inclinação',
    'Pivô',
  ];

  /// A propriedade de cada aba.
  static const propriedades = [
    LayerProp.position,
    LayerProp.scale,
    LayerProp.rotation,
    LayerProp.opacity,
    LayerProp.skew,
    LayerProp.pivot,
  ];

  @override
  ConsumerState<PainelTransformar> createState() => _PainelTransformarState();
}

class _PainelTransformarState extends ConsumerState<PainelTransformar>
    with PropriedadeAtivaDoPainel {
  int _aba = 0;

  @override
  void initState() {
    super.initState();
    _anunciar();
  }

  void _anunciar() => ativarPropriedade(
    PropriedadeAtiva.transformacao(PainelTransformar.propriedades[_aba]),
  );

  void _trocarAba(int i) {
    setState(() => _aba = i);
    _anunciar();
  }

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final visivel = camadaVisivel(ref, widget.layerId);
    final gravada = camadaGravada(ref, widget.layerId);
    if (visivel == null || gravada == null) {
      return const PainelSemCamada(titulo: 'Transformar');
    }
    final c = ref.read(editorControllerProvider.notifier);
    final id = widget.layerId;
    return AureaPanel(
      titulo: 'Transformar',
      chave: 'painel-${PainelId.transformar.name}',
      abas: PainelTransformar.abas,
      abaAtiva: _aba,
      aoTrocarAba: _trocarAba,
      aoFechar: escopo.fecharPainel,
      acoes: [
        AcaoDoCabecalho(
          key: const ValueKey('transformar-3d'),
          icone: CupertinoIcons.cube,
          ativo: visivel.is3D,
          aoTocar: () => umPasso(ref, () => c.toggle3D(id)),
        ),
      ],
      corpo: NoCabecote(
        construir: (context, t) {
          final local = visivel.localTime(t);
          final prop = PainelTransformar.propriedades[_aba];
          final kf = losangoDaPropriedade(
            ref,
            gravada: gravada,
            prop: prop,
            t: t,
            playback: escopo.playback,
          );
          void resetar() => umPasso(ref, () => c.resetProp(id, prop));

          // UMA LINHA NUMERICA DA ABA, com o losango da aba.
          AureaPropertyRow numero(
            String rotulo,
            double valor,
            ValueChanged<double> mudar, {
            double min = double.negativeInfinity,
            double max = double.infinity,
            double? sensibilidade,
            String unidade = '',
            int casas = 1,
          }) => AureaPropertyRow(
            rotulo: rotulo,
            valor: valor,
            aoMudar: aCadaPasso(mudar),
            min: min,
            max: max,
            sensibilidade: sensibilidade,
            unidade: unidade,
            casas: casas,
            keyframe: kf.estado,
            aoAnterior: kf.anterior,
            aoProximo: kf.proximo,
            aoResetar: resetar,
            aoComecarGesto: c.beginGesture,
            aoTerminarGesto: c.endGesture,
          );

          // UM PONTO (X e Y na mesma linha). O outro eixo e relido do
          // projeto a cada passo: o dedo que arrasta X nao pode levar o Y
          // de volta ao valor de quando a linha nasceu.
          AureaPropertyRow ponto(
            String rotulo,
            AnimatedOffset Function(Layer l) trilha,
            void Function(Offset) gravar,
          ) {
            final atual = trilha(visivel).valueAt(local);
            Offset agora() {
              final l = ref.read(projetoVisivelProvider).layerById(id);
              if (l == null) return atual;
              return trilha(l).valueAt(l.localTime(escopo.playback.time.value));
            }

            return AureaPropertyRow.ponto(
              rotulo: rotulo,
              x: atual.dx,
              y: atual.dy,
              sensibilidade: 1,
              aoMudarX: aCadaPasso((v) => gravar(Offset(v, agora().dy))),
              aoMudarY: aCadaPasso((v) => gravar(Offset(agora().dx, v))),
              keyframe: kf.estado,
              aoAnterior: kf.anterior,
              aoProximo: kf.proximo,
              aoResetar: resetar,
              aoComecarGesto: c.beginGesture,
              aoTerminarGesto: c.endGesture,
            );
          }

          final linhas = <Widget>[
            ...switch (prop) {
              LayerProp.position => [
                ponto(
                  'Posição',
                  (l) => l.position,
                  (o) => c.editPosition(id, escopo.playback.time.value, o),
                ),
                if (visivel.is3D)
                  numero(
                    'Profundidade',
                    visivel.positionZ.valueAt(local),
                    (v) => c.editPositionZ(id, escopo.playback.time.value, v),
                    sensibilidade: 1,
                  ),
              ],
              LayerProp.scale => [
                numero(
                  'Escala',
                  visivel.scaleX.valueAt(local) * 100,
                  (v) => c.editScaleUniform(
                    id,
                    escopo.playback.time.value,
                    v / 100,
                  ),
                  min: 0,
                  unidade: '%',
                ),
                numero(
                  'Largura',
                  visivel.scaleX.valueAt(local) * 100,
                  (v) =>
                      c.editScaleX(id, escopo.playback.time.value, v / 100),
                  unidade: '%',
                ),
                numero(
                  'Altura',
                  visivel.scaleY.valueAt(local) * 100,
                  (v) =>
                      c.editScaleY(id, escopo.playback.time.value, v / 100),
                  unidade: '%',
                ),
              ],
              LayerProp.rotation => [
                numero(
                  'Rotação',
                  visivel.rotation.valueAt(local),
                  (v) => c.editRotation(id, escopo.playback.time.value, v),
                  unidade: '°',
                ),
                // X E Y SO EXISTEM NA CAMADA 3D: sem perspectiva, girar em
                // X so achata a camada, e um numero que "nao faz nada"
                // parece defeito. O interruptor fica aqui, na aba em que
                // a pessoa procura girar em 3D.
                AureaPropertyRow.personalizada(
                  rotulo: 'Camada 3D',
                  chave: 'camada-3d',
                  filho: AureaToggle(
                    valor: visivel.is3D,
                    aoMudar: (_) => umPasso(ref, () => c.toggle3D(id)),
                  ),
                ),
                if (visivel.is3D) ...[
                  numero(
                    'Rotação X',
                    visivel.rotationX.valueAt(local),
                    (v) => c.editRotationX(id, escopo.playback.time.value, v),
                    unidade: '°',
                  ),
                  numero(
                    'Rotação Y',
                    visivel.rotationY.valueAt(local),
                    (v) => c.editRotationY(id, escopo.playback.time.value, v),
                    unidade: '°',
                  ),
                ],
              ],
              LayerProp.opacity => [
                numero(
                  'Opacidade',
                  visivel.opacity.valueAt(local) * 100,
                  (v) =>
                      c.editOpacity(id, escopo.playback.time.value, v / 100),
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
                  (v) => c.editSkewX(id, escopo.playback.time.value, v),
                  unidade: '°',
                ),
                numero(
                  'Inclinação Y',
                  visivel.skewY.valueAt(local),
                  (v) => c.editSkewY(id, escopo.playback.time.value, v),
                  unidade: '°',
                ),
              ],
              LayerProp.pivot => [
                ponto(
                  'Pivô',
                  (l) => l.pivot,
                  (o) => c.editPivot(id, escopo.playback.time.value, o),
                ),
              ],
              LayerProp.parent => const <Widget>[],
            },
          ];
          return ListView(padding: respiroDoPainel, children: linhas);
        },
      ),
    );
  }
}
