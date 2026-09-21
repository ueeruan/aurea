import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/shape.dart';
import '../../../domain/shape_ops.dart';
import '../curva/curva.dart' show TrilhaDaCurva, abrirEditorDeCurva;
import 'comum_de_objetos.dart';
import 'pecas_centrais.dart' show FileiraDeAcoes, umPasso;

// ===========================================================================
// OS OPERADORES DA FORMA (aba "Operadores" do painel Forma)
// ===========================================================================
//
// O que o painel de cor do editor antigo guardava no fim ("OPERADORES") e o
// "Avançado" da aba Cantos: geometria composta, Trim Paths, Repeater, Morph,
// os operadores de caminho (deslocar, arredondar, zig zag, inchar, torcer,
// baguncar) e o Combinar (Merge Paths). Tudo pela API que ja existia no
// controlador; cada acao e um passo de desfazer e cada arrasto tambem.

/// O EDITOR DE CURVA de uma trilha da forma (parametro da primitiva ou
/// trilha de um item: traco, trim). Quem chama diz como achar a trilha e
/// como gravar a curva — o contrato do `showTrackCurveSheet` antigo.
void abrirCurvaDaForma(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback, {
  required String layerId,
  required String rotulo,
  required AnimatedDouble? Function(Layer camada) trilhaDe,
  required void Function(
    EditorController c,
    String layerId,
    Duration inicio,
    Easing curva,
  )
  gravar,
  required void Function(EditorController c, String layerId, Easing curva)
  gravarEmTodos,
}) {
  playback.pause();
  abrirEditorDeCurva(
    context,
    ref,
    layerId: layerId,
    trilha: TrilhaDaCurva.deUmaTrilha(
      rotulo: rotulo,
      trilhaDe: trilhaDe,
      gravar: gravar,
      gravarEmTodos: gravarEmTodos,
    ),
    tempo: playback.time.value,
    playback: playback,
  );
}

/// A trilha [chave] do item [itemId] da forma (traco, trim), ou nula.
AnimatedDouble? trilhaDoItemDaForma(Layer camada, String itemId, String chave) {
  if (camada is! ShapeLayer) return null;
  for (final i in camada.contents) {
    if (i.id == itemId) return EditorController.shapeItemTrack(i, chave);
  }
  return null;
}

/// O nome de um operador (rotulo de UI).
String nomeDoOperador(ShapeItem i) => switch (i) {
  TrimOperator _ => 'Trim Paths',
  RepeaterOperator _ => 'Repeater',
  ShapeMorph _ => 'Morph',
  OffsetPathOperator _ => 'Deslocar caminho',
  RoundCornersOperator _ => 'Arredondar cantos',
  ZigZagOperator _ => 'Zig zag',
  PuckerBloatOperator _ => 'Inchar e encolher',
  TwistOperator _ => 'Torcer',
  WigglePathOperator _ => 'Bagunçar caminho',
  MergePathsOperator _ => 'Combinar caminhos',
  _ => 'Operador',
};

/// O item e um operador que esta aba edita?
bool ehOperadorDaForma(ShapeItem i) =>
    i is TrimOperator ||
    i is RepeaterOperator ||
    i is ShapeMorph ||
    i is OffsetPathOperator ||
    i is RoundCornersOperator ||
    i is ZigZagOperator ||
    i is PuckerBloatOperator ||
    i is TwistOperator ||
    i is WigglePathOperator ||
    i is MergePathsOperator;

String _rotuloDoMerge(MergeMode m) => switch (m) {
  MergeMode.union => 'Unir',
  MergeMode.subtract => 'Subtrair',
  MergeMode.intersect => 'Interseção',
  MergeMode.exclude => 'Excluir',
};

String _rotuloDoAdicionar(ShapePathOp o) => switch (o) {
  ShapePathOp.offset => 'Deslocar',
  ShapePathOp.roundCorners => 'Arredondar',
  ShapePathOp.zigZag => 'Zig zag',
  ShapePathOp.puckerBloat => 'Inchar',
  ShapePathOp.twist => 'Torcer',
  ShapePathOp.wiggle => 'Bagunçar',
  ShapePathOp.merge => 'Combinar',
};

/// Os destinos do "Morfar para…" (os do editor antigo).
List<(String, ShapePath Function())> get _destinosDoMorph => [
  ('Círculo', () => ShapePath(primitive: ShapePrimitive.ellipse)),
  ('Retângulo', () => ShapePath(primitive: ShapePrimitive.roundedRectangle)),
  ('Estrela', () => ShapePath(primitive: ShapePrimitive.star)),
  ('Polígono', () => ShapePath(primitive: ShapePrimitive.polygon, points: 6)),
  ('Coração', () => ShapePath(primitive: ShapePrimitive.heart)),
  ('Arco', () => ShapePath(primitive: ShapePrimitive.arc)),
];

/// AS LINHAS DA ABA OPERADORES.
List<Widget> linhasDosOperadores(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  required ShapeLayer visivel,
  required ShapeLayer gravada,
  required Duration t,
  required PlaybackController playback,
}) {
  final c = ref.read(editorControllerProvider.notifier);
  final local = visivel.localTime(t);
  final operadores = [
    for (final i in visivel.contents)
      if (ehOperadorDaForma(i)) i,
  ];
  final podeMorfar = visivel.contents.any(
    (i) => i is ShapePath || i is ShapeMorph,
  );

  Widget remover(ShapeItem item) => LinhaDeAcao(
    key: ValueKey('operador-${item.id}-remover'),
    rotulo: item is ShapeMorph ? 'Desfazer o morph' : 'Remover',
    icone: CupertinoIcons.trash,
    destrutiva: true,
    aoTocar: () => umPasso(
      ref,
      () => item is ShapeMorph
          ? c.removeMorph(layerId, item.id)
          : c.removeShapeItem(layerId, item.id),
    ),
  );

  List<Widget> doTrim(TrimOperator trim) {
    final gravado = gravada.contents
        .whereType<TrimOperator>()
        .where((g) => g.id == trim.id)
        .firstOrNull;
    Widget linha(String rotulo, String chave, AnimatedDouble v, double min) =>
        linhaNumerica(
          ref,
          rotulo: rotulo,
          chave: 'operador-${trim.id}-$chave',
          valor: v.valueAt(local) * 100,
          min: min,
          max: 100,
          unidade: '%',
          losango: losangoDaTrilha(
            trilha: gravado == null
                ? null
                : EditorController.shapeItemTrack(gravado, chave),
            gravada: gravada,
            t: t,
            playback: playback,
            aoAlternar: () => c.toggleShapeItemTrackKeyframe(
              layerId,
              trim.id,
              chave,
              playback.time.value,
            ),
            aoCurva: () => abrirCurvaDaForma(
              context,
              ref,
              playback,
              layerId: layerId,
              rotulo: rotulo,
              trilhaDe: (l) => trilhaDoItemDaForma(l, trim.id, chave),
              gravar: (c, id, ini, e) =>
                  c.setShapeItemTrackSegmentEase(id, trim.id, chave, ini, e),
              gravarEmTodos: (c, id, e) =>
                  c.applyEaseToAllShapeItemTrackSegments(id, trim.id, chave, e),
            ),
          ),
          aoMudar: (x) => c.editTrim(
            layerId,
            trim.id,
            chave,
            playback.time.value,
            x / 100,
          ),
        );
    return [
      AureaPropertyRow.personalizada(
        rotulo: 'Modo',
        chave: 'operador-${trim.id}-modo',
        filho: FileiraDePilulas<bool>(
          chave: 'operador-${trim.id}-modo',
          chaveDe: (ind) => ind ? 'individual' : 'continuo',
          opcoes: const [false, true],
          atual: trim.individually,
          rotuloDe: (ind) => ind ? 'Individual' : 'Contínuo',
          aoEscolher: (ind) =>
              umPasso(ref, () => c.setTrimMode(layerId, trim.id, ind)),
        ),
      ),
      linha('Início', 'start', trim.start, 0),
      linha('Fim', 'end', trim.end, 0),
      linha('Deslocamento', 'offset', trim.offset, -100),
    ];
  }

  List<Widget> doRepeater(RepeaterOperator r) => [
    linhaNumerica(
      ref,
      rotulo: 'Cópias',
      chave: 'operador-${r.id}-copias',
      valor: r.copies.toDouble(),
      min: 1,
      max: 50,
      aoMudar: (v) => c.editRepeater(
        layerId,
        r.id,
        playback.time.value,
        copies: v.round().clamp(1, 50),
      ),
    ),
    linhaNumerica(
      ref,
      rotulo: 'Deslocar X',
      chave: 'operador-${r.id}-dx',
      valor: r.dx,
      min: -400,
      max: 400,
      aoMudar: (v) => c.editRepeater(layerId, r.id, playback.time.value, dx: v),
    ),
    linhaNumerica(
      ref,
      rotulo: 'Deslocar Y',
      chave: 'operador-${r.id}-dy',
      valor: r.dy,
      min: -400,
      max: 400,
      aoMudar: (v) => c.editRepeater(layerId, r.id, playback.time.value, dy: v),
    ),
    linhaNumerica(
      ref,
      rotulo: 'Rotação',
      chave: 'operador-${r.id}-rotacao',
      valor: r.rotation.valueAt(local),
      min: -180,
      max: 180,
      unidade: '°',
      aoMudar: (v) => c.editRepeater(
        layerId,
        r.id,
        playback.time.value,
        rotationDeg: v,
      ),
    ),
  ];

  List<Widget> doMorph(ShapeMorph m) {
    final gravado = gravada.contents
        .whereType<ShapeMorph>()
        .where((g) => g.id == m.id)
        .firstOrNull;
    return [
      linhaNumerica(
        ref,
        rotulo: 'Progresso',
        chave: 'operador-${m.id}-progresso',
        valor: m.progress.valueAt(local) * 100,
        min: 0,
        max: 100,
        unidade: '%',
        losango: losangoDaTrilha(
          trilha: gravado?.progress,
          gravada: gravada,
          t: t,
          playback: playback,
          aoAlternar: () =>
              c.toggleMorphKeyframe(layerId, m.id, playback.time.value),
        ),
        aoMudar: (v) =>
            c.editMorphProgress(layerId, m.id, playback.time.value, v / 100),
      ),
    ];
  }

  // O NUMERO PRINCIPAL de um operador de caminho: (rotulo, valor, min, max,
  // escala). Inchar guarda 0..1 e mostra em %.
  (String, AnimatedDouble, double, double, double)? numeroDoCaminho(
    ShapeItem i,
  ) => switch (i) {
    OffsetPathOperator o => ('Distância', o.amount, -300, 300, 1),
    RoundCornersOperator r => ('Raio', r.radius, 0, 300, 1),
    ZigZagOperator z => ('Altura', z.amplitude, 0, 300, 1),
    PuckerBloatOperator p => ('Força', p.amount, -100, 100, 100),
    TwistOperator tw => ('Ângulo', tw.angle, -720, 720, 1),
    WigglePathOperator w => ('Quanto', w.amount, 0, 300, 1),
    _ => null,
  };

  List<Widget> doCaminho(ShapeItem i) {
    if (i is MergePathsOperator) {
      return [
        AureaPropertyRow.personalizada(
          rotulo: 'Modo',
          chave: 'operador-${i.id}-merge',
          filho: AureaDropdown<MergeMode>(
            valor: i.mode,
            opcoes: MergeMode.values,
            rotuloDe: _rotuloDoMerge,
            titulo: 'Combinar caminhos',
            // O CONTROLADOR SO GIRA o modo: anda ate o escolhido, num
            // passo de desfazer (o `onSetMerge` do editor antigo).
            aoMudar: (m) {
              final passos =
                  (m.index - i.mode.index + MergeMode.values.length) %
                  MergeMode.values.length;
              if (passos == 0) return;
              umPasso(ref, () {
                for (var k = 0; k < passos; k++) {
                  c.cycleMergeMode(layerId, i.id);
                }
              });
            },
          ),
        ),
      ];
    }
    final n = numeroDoCaminho(i);
    if (n == null) return const [];
    final (rotulo, trilha, min, max, escala) = n;
    return [
      linhaNumerica(
        ref,
        rotulo: rotulo,
        chave: 'operador-${i.id}-valor',
        valor: trilha.valueAt(local) * escala,
        min: min,
        max: max,
        unidade: escala == 100 ? '%' : '',
        // O operador de caminho recebe o tempo LOCAL (e assim no
        // controlador).
        aoMudar: (v) => c.editPathOperator(
          layerId,
          i.id,
          visivel.localTime(playback.time.value),
          v / escala,
        ),
      ),
    ];
  }

  Future<void> morfar(BuildContext botao) async {
    final destinos = _destinosDoMorph;
    final i = await mostrarAureaMenu<int>(
      botao,
      titulo: 'Morfar para…',
      itens: [
        for (var k = 0; k < destinos.length; k++)
          AureaMenuItem(
            valor: k,
            rotulo: destinos[k].$1,
            chave: 'morfar-$k',
          ),
      ],
    );
    if (i == null) return;
    umPasso(ref, () => c.convertShapeToMorph(layerId, destinos[i].$2()));
  }

  return [
    if (operadores.isEmpty)
      const AureaAvisoDoPainel(
        texto:
            'Operadores mudam o caminho da forma: repetir, desenhar, '
            'combinar, torcer. Adicione um abaixo.',
      ),
    for (final op in operadores)
      AureaSection(
        titulo: switch (op) {
          ShapeMorph m => moldar(context, 'Morph · {0} → {1}', [
            translate(context, _nomeDaPrimitiva(m.from.primitive)),
            translate(context, _nomeDaPrimitiva(m.to.primitive)),
          ]),
          _ => nomeDoOperador(op),
        },
        chave: 'operador-${op.id}',
        filhos: [
          ...switch (op) {
            TrimOperator tr => doTrim(tr),
            RepeaterOperator r => doRepeater(r),
            ShapeMorph m => doMorph(m),
            _ => doCaminho(op),
          },
          remover(op),
        ],
      ),
    AureaSection(
      titulo: 'Adicionar',
      chave: 'operadores-adicionar',
      recolhivel: false,
      filhos: [
        FileiraDeAcoes(
          acoes: [
            AureaChip(
              key: const ValueKey('operador-add-trim'),
              rotulo: 'Trim Paths',
              icone: CupertinoIcons.plus,
              aoTocar: () => umPasso(
                ref,
                () => c.addShapeOperator(layerId, repeater: false),
              ),
            ),
            AureaChip(
              key: const ValueKey('operador-add-repeater'),
              rotulo: 'Repeater',
              icone: CupertinoIcons.plus,
              aoTocar: () => umPasso(
                ref,
                () => c.addShapeOperator(layerId, repeater: true),
              ),
            ),
            for (final op in ShapePathOp.values)
              AureaChip(
                key: ValueKey('operador-add-${op.name}'),
                rotulo: _rotuloDoAdicionar(op),
                icone: CupertinoIcons.plus,
                aoTocar: () => umPasso(ref, () => c.addPathOperator(layerId, op)),
              ),
            if (podeMorfar)
              Builder(
                builder: (botao) => AureaChip(
                  key: const ValueKey('operador-add-morph'),
                  rotulo: 'Morfar para…',
                  icone: CupertinoIcons.arrow_2_squarepath,
                  aoTocar: () => morfar(botao),
                ),
              ),
          ],
        ),
      ],
    ),
    // GEOMETRIA COMPOSTA: uma segunda geometria na mesma forma, antes do
    // Combinar — unir, subtrair e cruzar sem importar um SVG.
    AureaSection(
      titulo: 'Geometria composta',
      chave: 'operadores-geometria',
      recolhivel: false,
      filhos: [
        FileiraDeAcoes(
          acoes: [
            for (final (k, nome) in const [
              (ParamShapeKind.rect, 'Retângulo'),
              (ParamShapeKind.ellipse, 'Círculo'),
            ])
              AureaChip(
                key: ValueKey('operador-geometria-${k.name}'),
                rotulo: nome,
                icone: CupertinoIcons.plus,
                aoTocar: () =>
                    umPasso(ref, () => c.addCompoundShapeGeometry(layerId, k)),
              ),
          ],
        ),
      ],
    ),
  ];
}

String _nomeDaPrimitiva(ShapePrimitive p) => switch (p) {
  ShapePrimitive.rectangle || ShapePrimitive.roundedRectangle => 'Retângulo',
  ShapePrimitive.ellipse => 'Círculo',
  ShapePrimitive.polygon => 'Polígono',
  ShapePrimitive.star => 'Estrela',
  ShapePrimitive.ring => 'Anel',
  ShapePrimitive.arc => 'Arco',
  ShapePrimitive.wave => 'Onda',
  ShapePrimitive.heart => 'Coração',
  ShapePrimitive.gear => 'Engrenagem',
  ShapePrimitive.arrow => 'Seta',
  ShapePrimitive.check => 'Check',
  ShapePrimitive.plus => 'Mais',
  ShapePrimitive.drop => 'Gota',
  ShapePrimitive.flower => 'Flor',
  ShapePrimitive.sparkle => 'Faísca',
};
