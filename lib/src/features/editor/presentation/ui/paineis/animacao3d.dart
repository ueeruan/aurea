import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../../domain/model_asset3d.dart';
import '../../../domain/scene3d.dart';
import '../../../domain/text_anim.dart';
import '../../../domain/texto3d_animado.dart';
import '../../widgets/gizmo_da_cena_overlay.dart'
    show noDaCenaSelecionadoProvider;
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_3d.dart';

String rotuloDaVez(TextAnimSlot s) => switch (s) {
  TextAnimSlot.entrada => 'Entrada',
  TextAnimSlot.enfase => 'Ênfase',
  TextAnimSlot.saida => 'Saída',
};

/// ANIMACAO — as animacoes PRONTAS do 3D.
///
///  * Texto 3D: entrada, enfase e saida com os MESMOS presets do texto
///    comum, letra a letra na malha extrudada;
///  * modelo importado: o clipe que veio dentro do arquivo (GLB/FBX), a
///    velocidade, o comeco e o repetir.
///
/// Quando o objeto nao traz clipe, o painel diz isso em vez de oferecer
/// reguas que nao mexem em nada — e aponta os losangos do inspector.
class PainelAnimacao3D extends ConsumerWidget {
  const PainelAnimacao3D({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Animação';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.animacao3d.name}';
    if (camada is! Scene3DLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é 3D.',
        portas: const [],
      );
    }
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      aoFechar: escopo.fecharPainel,
      corpo: CorpoDaAnimacao3D(layerId: layerId),
    );
  }
}

/// O CORPO DA ANIMACAO, usado pelo painel Animacao e pela aba Animacao do
/// inspector da cena.
class CorpoDaAnimacao3D extends ConsumerWidget {
  const CorpoDaAnimacao3D({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final camada = camadaVisivel(ref, layerId);
    if (camada is! Scene3DLayer) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);
    final noTexto = noDoTexto3D(camada);
    final modeloDoTexto = noTexto == null
        ? null
        : camada.scene.nodeById(noTexto)?.modelAsset;
    if (noTexto != null && modeloDoTexto != null) {
      return ListView(
        key: const ValueKey('animacao-texto3d'),
        padding: paddingDoPainel,
        children: _doTexto(context, c, camada, noTexto, modeloDoTexto),
      );
    }
    final no = noEmFoco(camada.scene, ref.watch(noDaCenaSelecionadoProvider));
    return ListView(
      key: const ValueKey('animacao-cena'),
      padding: paddingDoPainel,
      children: [
        EscolhaDoObjeto3D(cena: camada.scene, escolhido: no),
        if (no == null)
          const AureaAvisoDoPainel(
            texto: 'Esta cena não tem objeto para animar.',
          )
        else
          ..._doModelo(context, c, camada, no),
      ],
    );
  }

  /// UMA ESCOLHA POR VEZ (entrada, enfase, saida). Trocar uma nao mexe nas
  /// outras duas; "Nenhuma" tira so aquela.
  List<Widget> _doTexto(
    BuildContext context,
    EditorController c,
    Scene3DLayer camada,
    String noId,
    ModelAsset3D modelo,
  ) {
    final atuais = animsDoTexto3D(modelo);
    return [
      for (final vez in TextAnimSlot.values)
        () {
          final atual = atuais.where((a) => a.slot == vez).firstOrNull;
          final opcoes = ['', for (final s in textAnimsForSlot(vez)) s.id];
          return AureaPropertyRow.personalizada(
            rotulo: rotuloDaVez(vez),
            chave: 'animacao-${vez.name}',
            filho: AureaDropdown<String>(
              key: ValueKey('animacao-${vez.name}-escolha'),
              valor: atual?.specId ?? '',
              opcoes: opcoes,
              rotuloDe: (id) =>
                  id.isEmpty ? 'Nenhuma' : (textAnimSpecById(id)?.label ?? id),
              titulo: rotuloDaVez(vez),
              aoMudar: (id) => c.setTexto3DAnims(camada.id, noId, [
                for (final a in atuais)
                  if (a.slot != vez) a,
                if (id.isNotEmpty) TextAnim(specId: id, slot: vez),
              ]),
            ),
          );
        }(),
    ];
  }

  /// O CLIPE QUE VEIO NO ARQUIVO.
  List<Widget> _doModelo(
    BuildContext context,
    EditorController c,
    Scene3DLayer camada,
    SceneNode n,
  ) {
    final nomes = n.modelAsset?.clipNames ?? const <String>[];
    if (nomes.isEmpty) {
      return const [
        AureaAvisoDoPainel(
          texto:
              'Este objeto não traz animação própria. Anime pelos '
              'losangos do inspector (Cena › Transformar).',
        ),
      ];
    }
    final movimento = n.modelMotion;
    void mexer(ModelMotion3D Function(ModelMotion3D) f) =>
        c.setSceneNodeMotion(camada.id, n.id, f(n.modelMotion));
    return [
      LinhaDeFichas<int>(
        rotulo: 'Clipe',
        chave: 'animacao-clipe',
        valores: [-1, for (var i = 0; i < nomes.length; i++) i],
        // O nome do clipe e conteudo do arquivo; "Parado" e rotulo.
        traduzir: false,
        rotuloDe: (i) => i < 0 ? translate(context, 'Parado') : nomes[i],
        escolhido: movimento.clip,
        chaveDe: (i) => 'animacao-clipe-$i',
        aoEscolher: (i) => mexer((m) => m.copyWith(clip: i)),
      ),
      linhaSemLosango(
        c,
        rotulo: 'Velocidade',
        chave: 'animacao-velocidade',
        valor: movimento.speed * 100,
        min: -400,
        max: 400,
        unidade: '%',
        aoMudar: (v) => mexer((m) => m.copyWith(speed: v / 100)),
      ),
      linhaSemLosango(
        c,
        rotulo: 'Começar em',
        chave: 'animacao-comeco',
        valor: movimento.offset,
        min: 0,
        max: 600,
        casas: 2,
        unidade: 's',
        aoMudar: (v) => mexer((m) => m.copyWith(offset: v)),
      ),
      linhaDeInterruptor(
        rotulo: 'Repetir',
        chave: 'animacao-repetir',
        valor: movimento.loop,
        aoMudar: (v) => mexer((m) => m.copyWith(loop: v)),
      ),
    ];
  }
}
