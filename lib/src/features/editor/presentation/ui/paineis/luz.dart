import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../application/editor_controller.dart';
import '../../../application/interacao.dart';
import '../../../domain/layer.dart';
import '../../../domain/scene3d.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_3d.dart';

String rotuloDaLuz(Light3DKind k) => switch (k) {
  Light3DKind.directional => 'Direcional',
  Light3DKind.point => 'Ponto',
  Light3DKind.ambient => 'Ambiente',
  Light3DKind.spot => 'Foco',
};

/// A LUZ ESCOLHIDA (nula = a primeira da cena). Vale para o painel Luz e
/// para a aba Iluminacao do inspector: escolher numa e ver a mesma na outra.
final luzEscolhidaProvider = StateProvider<String?>((ref) => null);

/// LUZ — as luzes da cena 3D: qual, o tipo, a cor, a intensidade (com
/// losango: e a unica trilha animavel de uma luz), o alcance, o cone, a
/// suavidade e a sombra; e acrescentar ou tirar luz.
class PainelLuz extends ConsumerWidget {
  const PainelLuz({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Luz';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.luz.name}';
    if (camada is! Scene3DLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é uma cena 3D.',
        portas: const [],
      );
    }
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      aoFechar: escopo.fecharPainel,
      corpo: CorpoDaLuz3D(layerId: layerId),
    );
  }
}

/// O CORPO DA LUZ, usado pelo painel Luz e pela aba Iluminacao do
/// inspector da cena.
class CorpoDaLuz3D extends ConsumerWidget {
  const CorpoDaLuz3D({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada is! Scene3DLayer) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);
    final luzes = camada.scene.lights;
    final escolhida = ref.watch(luzEscolhidaProvider);
    final luz =
        luzes.where((l) => l.id == escolhida).firstOrNull ?? luzes.firstOrNull;

    return NoCabecote(
      construir: (context, t) => ListView(
        key: const ValueKey('luz-corpo'),
        padding: paddingDoPainel,
        children: [
          if (luzes.length > 1)
            LinhaDeFichas<Light3D>(
              rotulo: 'Qual luz',
              chave: 'luz-qual',
              valores: luzes,
              // "1. Direcional", "2. Ponto": duas luzes do mesmo tipo
              // precisam de nomes diferentes na ficha.
              traduzir: false,
              rotuloDe: (l) =>
                  '${luzes.indexOf(l) + 1}. '
                  '${translate(context, rotuloDaLuz(l.kind))}',
              escolhido: luz,
              chaveDe: (l) => 'luz-${l.id}',
              aoEscolher: (l) =>
                  ref.read(luzEscolhidaProvider.notifier).state = l.id,
            ),
          if (luz == null)
            const AureaAvisoDoPainel(
              texto: 'Sem luzes. O ambiente ainda ilumina a cena.',
            )
          else ...[
            LinhaDeFichas<Light3DKind>(
              rotulo: 'Tipo',
              chave: 'luz-tipo',
              valores: Light3DKind.values,
              rotuloDe: rotuloDaLuz,
              escolhido: luz.kind,
              chaveDe: (k) => 'luz-tipo-${k.name}',
              aoEscolher: (k) => c.setSceneLightKind(layerId, luz.id, k),
            ),
            linhaDeCor(
              context,
              rotulo: 'Cor',
              chave: 'luz-cor',
              cor: luz.color,
              aoMudar: (cor) => c.setSceneLightColor(layerId, luz.id, cor),
            ),
            () {
              final kf = losangoDasMarcas(
                marcasUs: [
                  for (final d in c.sceneLightKeyframeTimes(
                    luz,
                    PropDaLuz.intensidade,
                  ))
                    d.inMicroseconds,
                ],
                camada: camada,
                t: t,
                playback: escopo.playback,
                aoAlternar: () => c.toggleSceneLightKeyframe(
                  layerId,
                  luz.id,
                  PropDaLuz.intensidade,
                  t,
                ),
              );
              return AureaPropertyRow(
                rotulo: 'Intensidade',
                chave: 'luz-intensidade',
                valor:
                    (c.sceneLightValueAt(
                              luz,
                              PropDaLuz.intensidade,
                              camada.localTime(t),
                            ) *
                            100)
                        .clamp(0, 500)
                        .toDouble(),
                min: 0,
                max: 500,
                casas: 0,
                unidade: '%',
                keyframe: kf.estado,
                aoAnterior: kf.anterior,
                aoProximo: kf.proximo,
                aoComecarGesto: c.beginGesture,
                aoTerminarGesto: c.endGesture,
                aoMudar: (v) {
                  Interacao.marcar();
                  c.editSceneLightProp(
                    layerId,
                    luz.id,
                    PropDaLuz.intensidade,
                    t,
                    v.clamp(0, 500) / 100,
                  );
                },
              );
            }(),
            if (luz.kind == Light3DKind.point || luz.kind == Light3DKind.spot)
              linhaSemLosango(
                c,
                rotulo: 'Alcance',
                chave: 'luz-alcance',
                valor: luz.range,
                min: 1,
                max: 20000,
                aoMudar: (v) => c.setSceneLightRange(layerId, luz.id, v),
              ),
            if (luz.kind == Light3DKind.spot)
              linhaSemLosango(
                c,
                rotulo: 'Cone',
                chave: 'luz-cone',
                valor: luz.coneDegrees,
                min: 1,
                max: 179,
                unidade: '°',
                aoMudar: (v) => c.setSceneLightCone(layerId, luz.id, v),
              ),
            linhaSemLosango(
              c,
              rotulo: 'Suavidade',
              chave: 'luz-suavidade',
              valor: luz.softness * 100,
              min: 0,
              max: 100,
              unidade: '%',
              aoMudar: (v) => c.setSceneLightSoftness(layerId, luz.id, v / 100),
            ),
            linhaDeInterruptor(
              rotulo: 'Sombra',
              chave: 'luz-sombra',
              valor: luz.castsShadow,
              aoMudar: (v) => c.setSceneLightShadow(layerId, luz.id, v),
            ),
          ],
          GradeDeAcoes(
            acoes: [
              AcaoDoPainel(
                chave: 'luz-adicionar',
                icone: CupertinoIcons.plus_circle,
                rotulo: 'Adicionar luz',
                aoTocar: () => c.addSceneLight(layerId, Light3DKind.point),
              ),
              AcaoDoPainel(
                chave: 'luz-remover',
                icone: CupertinoIcons.trash,
                rotulo: 'Remover luz',
                aoTocar: luz == null
                    ? null
                    : () {
                        c.removeSceneLight(layerId, luz.id);
                        ref.read(luzEscolhidaProvider.notifier).state = null;
                      },
              ),
            ],
          ),
        ],
      ),
    );
  }
}
