import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/layer_meta.dart';
import '../paineis/comum_de_objetos.dart' show FileiraDePilulas, LinhaDeAcao;
import '../paineis/pecas_centrais.dart' show respiroDoPainel;

// AS FOLHAS DE OFICIO: organizar a camada (rotulo, solo, timida,
// bloqueio, motion blur) e o loop de keyframes.
//
// As duas OBSERVAM o projeto gravado: cada toque muda o projeto e a folha
// se redesenha sozinha, sem estado proprio que pudesse ficar velho depois
// de um desfazer. Cada toque e uma chamada so do controlador.

// -------------------------------------------------------------- organizar

/// ORGANIZACAO (PR-X26): rotulo colorido, solo, timida, bloqueio e motion
/// blur da camada [layerId].
Future<void> showOrganizeSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) => mostrarAureaFolha<void>(
  context,
  titulo: 'Organizar',
  construtor: (folha) => Consumer(
    builder: (folha, ref, _) {
      final projeto = ref.watch(editorControllerProvider);
      final camada = projeto.layerById(layerId);
      // A camada sumiu com a folha aberta (desfazer que a levou): nada a
      // organizar, e nada que quebre.
      if (camada == null) return const SizedBox.shrink();
      final meta = projeto.metaOf(layerId);
      final c = ref.read(editorControllerProvider.notifier);

      AureaPropertyRow chave(
        String rotulo,
        String id,
        bool valor,
        VoidCallback alternar,
        String dica,
      ) => AureaPropertyRow.personalizada(
        rotulo: rotulo,
        chave: 'organizar-$id',
        filho: Row(
          children: [
            AureaToggle(valor: valor, aoMudar: (_) => alternar()),
            const SizedBox(width: AureaDims.e8),
            Expanded(
              child: AppText(
                dica,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: AureaEstilos.rotulo,
              ),
            ),
          ],
        ),
      );

      return SingleChildScrollView(
        padding: respiroDoPainel,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // DE QUEM e a folha: o nome e conteudo da pessoa (Text).
            SizedBox(
              height: AureaDims.itemDeLista,
              child: Row(
                children: [
                  Icon(
                    layerTypeIcon(camada),
                    size: AureaDims.iconeSm,
                    color: layerTypeColor(camada),
                  ),
                  const SizedBox(width: AureaDims.e8),
                  Expanded(
                    child: Text(
                      camada.name,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: AureaEstilos.corpo,
                    ),
                  ),
                ],
              ),
            ),
            AureaSection(
              titulo: 'Rótulo',
              chave: 'organizar-rotulo',
              recolhivel: false,
              filhos: [
                // Doze cores nao cabem numa linha de 360: a fileira quebra.
                Padding(
                  padding: const EdgeInsets.only(bottom: AureaDims.e8),
                  child: Wrap(
                    spacing: AureaDims.e8,
                    runSpacing: AureaDims.e8,
                    crossAxisAlignment: WrapCrossAlignment.center,
                    children: [
                      for (final (i, l) in LayerLabel.palette.indexed)
                        _AmostraDeRotulo(
                          key: ValueKey('organizar-rotulo-$i'),
                          cor: l.color,
                          escolhida: meta.label?.color == l.color,
                          aoTocar: () => c.setLayerLabel(layerId, l),
                        ),
                      Tocavel(
                        key: const ValueKey('organizar-rotulo-nenhum'),
                        onTap: () => c.setLayerLabel(layerId, null),
                        child: SizedBox(
                          width: 30,
                          height: 30,
                          child: Icon(
                            CupertinoIcons.clear_circled,
                            size: 22,
                            color: AureaCores.textoSecundario,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
            chave(
              'Solo',
              'solo',
              meta.solo,
              () => c.toggleSolo(layerId),
              'Só as camadas em solo aparecem',
            ),
            chave(
              'Tímida',
              'timida',
              meta.shy,
              () => c.toggleShy(layerId),
              'Some da timeline, continua no render',
            ),
            chave(
              'Bloquear',
              'bloquear',
              meta.locked,
              () => c.toggleLocked(layerId),
              'Não anda, não apara, não edita nem apaga',
            ),
            chave(
              'Motion blur',
              'motion-blur',
              meta.motionBlur,
              () => c.toggleLayerMotionBlur(layerId),
              'Borrão de movimento nesta camada',
            ),
          ],
        ),
      );
    },
  ),
);

/// UMA COR DE ROTULO: o circulo cheio, e a escolhida leva o visto por
/// dentro — sem anel em volta, que seria borda.
class _AmostraDeRotulo extends StatelessWidget {
  const _AmostraDeRotulo({
    super.key,
    required this.cor,
    required this.escolhida,
    required this.aoTocar,
  });

  final Color cor;
  final bool escolhida;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Tocavel(
    onTap: aoTocar,
    child: Container(
      width: 30,
      height: 30,
      decoration: BoxDecoration(color: cor, shape: BoxShape.circle),
      child: escolhida
          ? Icon(
              CupertinoIcons.checkmark_alt,
              size: AureaDims.iconeSm,
              // O visto contrasta com a propria cor: escuro nas claras,
              // claro nas escuras (Grafite).
              color: cor.computeLuminance() > .35
                  ? AureaCores.palco
                  : AureaCores.texto,
            )
          : null,
    ),
  );
}

// ------------------------------------------------------------------- loop

/// As propriedades que aceitam loop nesta folha, com o nome de cada uma.
const _propriedadesDoLoop = <(LayerProp, String)>[
  (LayerProp.position, 'Posição'),
  (LayerProp.scale, 'Escala'),
  (LayerProp.rotation, 'Rotação'),
  (LayerProp.opacity, 'Opacidade'),
];

/// LOOP DE KEYFRAMES (PR-X6): dois keyframes e um Ciclo ja sao uma
/// animacao infinita, sem encher a timeline.
///
/// Sem veu ([mostrarAureaFolha] com `modal: false`): quem escolhe o loop
/// quer ver o palco.
Future<void> showLoopSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) {
  // A propriedade escolhida e estado DA FOLHA; o loop em si e do projeto.
  var prop = LayerProp.position;
  return mostrarAureaFolha<void>(
    context,
    titulo: 'Loop de keyframes',
    modal: false,
    construtor: (folha) => StatefulBuilder(
      builder: (folha, setFolha) => Consumer(
        builder: (folha, ref, _) {
          final camada = ref.watch(
            editorControllerProvider.select((p) => p.layerById(layerId)),
          );
          if (camada == null) return const SizedBox.shrink();
          final c = ref.read(editorControllerProvider.notifier);
          final spec = _loopDe(camada, prop);
          final marcas = _marcasDe(camada, prop);
          return SingleChildScrollView(
            padding: respiroDoPainel,
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                AureaPropertyRow.personalizada(
                  rotulo: 'Propriedade',
                  chave: 'loop-propriedade',
                  filho: FileiraDePilulas<LayerProp>(
                    chave: 'loop-propriedade',
                    chaveDe: (p) => p.name,
                    opcoes: [for (final (p, _) in _propriedadesDoLoop) p],
                    atual: prop,
                    rotuloDe: (p) =>
                        _propriedadesDoLoop.firstWhere((e) => e.$1 == p).$2,
                    aoEscolher: (p) => setFolha(() => prop = p),
                  ),
                ),
                if (marcas < 2)
                  const AureaAvisoDoPainel(
                    texto:
                        'Esta propriedade precisa de 2 ou mais keyframes '
                        'para ter loop.',
                  )
                else ...[
                  AureaPropertyRow.personalizada(
                    rotulo: 'Loop',
                    chave: 'loop-modo',
                    filho: FileiraDePilulas<LoopMode>(
                      chave: 'loop-modo',
                      chaveDe: (m) => m.name,
                      opcoes: LoopMode.values,
                      atual: spec.mode,
                      rotuloDe: (m) => switch (m) {
                        LoopMode.none => 'Sem loop',
                        LoopMode.cycle => 'Ciclo',
                        LoopMode.pingPong => 'Vai-e-volta',
                        LoopMode.offset => 'Deslocado',
                        LoopMode.continueValue => 'Continuar',
                      },
                      aoEscolher: (m) => c.setPropertyLoop(
                        layerId,
                        prop,
                        spec.copyWith(mode: m),
                      ),
                    ),
                  ),
                  const AureaAvisoDoPainel(
                    texto:
                        'Ciclo repete do início · Vai-e-volta alterna a '
                        'direção · Deslocado soma o percurso a cada volta '
                        '(esteira) · Continuar mantém a velocidade final.',
                  ),
                  LinhaDeAcao(
                    key: const ValueKey('loop-inverter'),
                    rotulo: 'Inverter no tempo',
                    icone: CupertinoIcons.arrow_2_squarepath,
                    aoTocar: () => c.reversePropertyInTime(layerId, prop),
                  ),
                ],
              ],
            ),
          );
        },
      ),
    ),
  );
}

/// O loop gravado de [prop] na [camada]. Escala e inclinacao leem o eixo
/// X: o controlador grava o mesmo loop nos dois eixos.
LoopSpec _loopDe(Layer camada, LayerProp prop) => switch (prop) {
  LayerProp.position => camada.position.loop,
  LayerProp.scale => camada.scaleX.loop,
  LayerProp.rotation => camada.rotation.loop,
  LayerProp.opacity => camada.opacity.loop,
  LayerProp.skew => camada.skewX.loop,
  LayerProp.pivot => camada.pivot.loop,
  LayerProp.parent => LoopSpec.none,
};

/// Quantas marcas [prop] tem: loop so existe com duas ou mais.
int _marcasDe(Layer camada, LayerProp prop) => switch (prop) {
  LayerProp.position => camada.position.keyframes.length,
  LayerProp.scale => camada.scaleX.keyframes.length,
  LayerProp.rotation => camada.rotation.keyframes.length,
  LayerProp.opacity => camada.opacity.keyframes.length,
  LayerProp.skew => camada.skewX.keyframes.length,
  LayerProp.pivot => camada.pivot.keyframes.length,
  LayerProp.parent => 0,
};
