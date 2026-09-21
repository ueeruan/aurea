import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../../domain/text_path.dart';
import '../paineis/comum_de_objetos.dart'
    show FileiraDePilulas, linhaDeLigar, linhaNumerica;
import '../paineis/pecas_centrais.dart' show respiroDoPainel;

/// TEXTO EM CAMINHO: selo circular, arco, ou acompanhando uma forma
/// desenhada no proprio projeto (com os operadores de forma e tudo).
///
/// Sem veu ([mostrarAureaFolha] com `modal: false`) e com teto de metade
/// da tela: quem curva um texto precisa ver o texto curvando.
///
/// Cada regua e um gesto so de desfazer (`linhaNumerica` abre e fecha o
/// grupo do controlador); pilula e interruptor sao um toque, uma mutacao.
Future<void> showTextPathSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) => mostrarAureaFolha<void>(
  context,
  titulo: 'Texto em caminho',
  modal: false,
  construtor: (folha) => ConstrainedBox(
    constraints: BoxConstraints(
      maxHeight: MediaQuery.sizeOf(folha).height * .5,
    ),
    child: Consumer(
      builder: (folha, ref, _) {
        final projeto = ref.watch(editorControllerProvider);
        final camada = projeto.layerById(layerId);
        // Camada que sumiu ou deixou de ser texto (desfazer com a folha
        // aberta): nada a curvar.
        if (camada is! TextLayer) return const SizedBox.shrink();
        final c = ref.read(editorControllerProvider.notifier);
        final spec = camada.textPath;
        void editar(TextPathSpec Function(TextPathSpec) fn) =>
            c.updateTextPath(layerId, fn);
        final formas = projeto.layers.whereType<ShapeLayer>().toList();
        final redondo =
            spec.kind == TextPathKind.circle || spec.kind == TextPathKind.arc;

        return SingleChildScrollView(
          padding: respiroDoPainel.copyWith(
            bottom:
                respiroDoPainel.bottom + MediaQuery.viewInsetsOf(folha).bottom,
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const AureaAvisoDoPainel(
                texto:
                    'Selo circular, arco, ou seguindo uma forma que você '
                    'desenhou — com os operadores e tudo.',
              ),
              AureaPropertyRow.personalizada(
                rotulo: 'Caminho',
                chave: 'caminho-tipo',
                filho: FileiraDePilulas<TextPathKind>(
                  chave: 'caminho-tipo',
                  chaveDe: (k) => k.name,
                  opcoes: TextPathKind.values,
                  atual: spec.kind,
                  rotuloDe: textPathKindLabel,
                  aoEscolher: (k) => editar((s) => s.copyWith(kind: k)),
                ),
              ),
              if (spec.kind == TextPathKind.layer) ...[
                if (formas.isEmpty)
                  const AureaAvisoDoPainel(
                    texto: 'Não há camada de forma no projeto para seguir.',
                  )
                else
                  // As formas em lista, com o nome inteiro: e conteudo da
                  // pessoa, e "Forma 3" cortado no meio de uma pilula nao
                  // diz qual e.
                  for (final f in formas)
                    AureaLayerRow(
                      key: ValueKey('caminho-forma-${f.id}'),
                      nome: f.name,
                      icone: layerTypeIcon(f),
                      corDaFaixa: layerTypeColor(f),
                      selecionada: spec.shapeLayerId == f.id,
                      aoTocar: () =>
                          editar((s) => s.copyWith(shapeLayerId: f.id)),
                    ),
              ],
              if (redondo) ...[
                linhaNumerica(
                  ref,
                  rotulo: 'Raio',
                  chave: 'caminho-raio',
                  valor: spec.radius,
                  min: 20,
                  max: 800,
                  aoMudar: (v) => editar((s) => s.copyWith(radius: v)),
                ),
                linhaNumerica(
                  ref,
                  rotulo: 'Começo',
                  chave: 'caminho-comeco',
                  valor: spec.startDeg,
                  min: -180,
                  max: 180,
                  unidade: '°',
                  aoMudar: (v) => editar((s) => s.copyWith(startDeg: v)),
                ),
              ],
              if (spec.kind == TextPathKind.arc)
                linhaNumerica(
                  ref,
                  rotulo: 'Abertura',
                  chave: 'caminho-abertura',
                  valor: spec.sweepDeg,
                  min: 10,
                  max: 360,
                  unidade: '°',
                  aoMudar: (v) => editar((s) => s.copyWith(sweepDeg: v)),
                ),
              if (spec.active) ...[
                linhaNumerica(
                  ref,
                  rotulo: 'Deslizar',
                  chave: 'caminho-deslizar',
                  valor: spec.offset,
                  min: -1000,
                  max: 1000,
                  aoMudar: (v) => editar((s) => s.copyWith(offset: v)),
                ),
                linhaNumerica(
                  ref,
                  rotulo: 'Espaço',
                  chave: 'caminho-espaco',
                  valor: spec.spacing,
                  min: -20,
                  max: 60,
                  aoMudar: (v) => editar((s) => s.copyWith(spacing: v)),
                ),
                AureaPropertyRow.personalizada(
                  rotulo: 'Alinhar',
                  chave: 'caminho-alinhar',
                  filho: FileiraDePilulas<TextPathAlign>(
                    chave: 'caminho-alinhar',
                    chaveDe: (a) => a.name,
                    opcoes: TextPathAlign.values,
                    atual: spec.align,
                    rotuloDe: (a) => switch (a) {
                      TextPathAlign.above => 'Acima',
                      TextPathAlign.on => 'Sobre',
                      TextPathAlign.below => 'Abaixo',
                    },
                    aoEscolher: (a) => editar((s) => s.copyWith(align: a)),
                  ),
                ),
                linhaDeLigar(
                  rotulo: 'Girar com a curva',
                  chave: 'caminho-girar',
                  valor: spec.perpendicular,
                  aoMudar: (v) =>
                      editar((s) => s.copyWith(perpendicular: v)),
                ),
                linhaDeLigar(
                  rotulo: 'Inverter o sentido',
                  chave: 'caminho-inverter',
                  valor: spec.reverse,
                  aoMudar: (v) => editar((s) => s.copyWith(reverse: v)),
                ),
                const AureaAvisoDoPainel(
                  texto:
                      'Animar "Deslizar" faz o texto correr pelo caminho — '
                      'é assim que um selo gira.',
                ),
              ],
            ],
          ),
        );
      },
    ),
  ),
);
