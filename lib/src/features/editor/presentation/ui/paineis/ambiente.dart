import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/element3d.dart';
import '../../../domain/layer.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_3d.dart';

/// AMBIENTE — o mundo em volta dos objetos: o estudio que eles refletem,
/// quanto refletem, a luz solta no ar e as cores do ceu e do chao.
///
/// SO O QUE O MOTOR USA. Neblina, panorama, sonda, piso espelhado e
/// tonemap entram na chave do quadro e nunca chegam ao motor: uma regua
/// que nao muda um pixel e pior que regua nenhuma.
class PainelAmbiente extends ConsumerWidget {
  const PainelAmbiente({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Ambiente';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.ambiente.name}';
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
      corpo: CorpoDoAmbiente3D(layerId: layerId),
    );
  }
}

/// O CORPO DO AMBIENTE, usado pelo painel Ambiente e pela aba Ambiente do
/// inspector da cena.
class CorpoDoAmbiente3D extends ConsumerWidget {
  const CorpoDoAmbiente3D({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final camada = camadaVisivel(ref, layerId);
    if (camada is! Scene3DLayer) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);
    final cena = camada.scene;
    return ListView(
      key: const ValueKey('ambiente-corpo'),
      padding: paddingDoPainel,
      children: [
        AureaPropertyRow.personalizada(
          rotulo: 'Estúdio',
          chave: 'ambiente-estudio',
          filho: AureaDropdown<EnvironmentKind>(
            key: const ValueKey('ambiente-estudio-escolha'),
            valor: cena.environment,
            opcoes: EnvironmentKind.values,
            rotuloDe: environmentLabel,
            titulo: 'Estúdio',
            aoMudar: (k) => c.setSceneEnvironment(layerId, k),
          ),
        ),
        linhaSemLosango(
          c,
          rotulo: 'Reflexo',
          chave: 'ambiente-reflexo',
          valor: cena.envReflect * 100,
          min: 0,
          max: 100,
          unidade: '%',
          aoMudar: (v) => c.setSceneEnvReflect(layerId, v / 100),
        ),
        linhaSemLosango(
          c,
          rotulo: 'Luz ambiente',
          chave: 'ambiente-luz',
          valor: cena.ambient * 100,
          min: 0,
          max: 300,
          unidade: '%',
          aoMudar: (v) => c.setSceneAmbient(layerId, v / 100),
        ),
        linhaDeCor(
          context,
          rotulo: 'Cor do céu',
          chave: 'ambiente-ceu',
          cor: cena.skyColor,
          aoMudar: (cor) => c.setSceneSkyColor(layerId, cor),
        ),
        linhaDeCor(
          context,
          rotulo: 'Cor do chão',
          chave: 'ambiente-chao',
          cor: cena.groundColor,
          aoMudar: (cor) => c.setSceneGroundColor(layerId, cor),
        ),
      ],
    );
  }
}
