import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/grid_rig.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_de_objetos.dart';

/// CLONAR — a grade de clones do nulo: o nulo posiciona as camadas
/// escolhidas numa grade (retangular, radial ou esferica), e o morph anima
/// de um arranjo a outro pelo caminho mais curto. A transformacao de cada
/// camada continua valendo por cima (mover uma nao quebra a grade).
///
/// Tres abas: Grade (camadas, arranjo, espacos), Variacao (giros,
/// profundidade, escalas, acaso, nulo controlador) e Proximidade (o
/// effector esferico que cresce e puxa quem esta perto).
class PainelClonar extends ConsumerStatefulWidget {
  const PainelClonar({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelClonar> createState() => _PainelClonarState();
}

class _PainelClonarState extends ConsumerState<PainelClonar> {
  static const _titulo = 'Clonar';
  static const _abas = ['Grade', 'Variação', 'Proximidade'];

  int _aba = 0;

  String get _id => widget.layerId;
  EditorController get _c => ref.read(editorControllerProvider.notifier);

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final visivel = camadaVisivel(ref, _id);
    final gravada = camadaGravada(ref, _id);
    final chave = 'painel-${PainelId.clonar.name}';
    if (visivel == null || gravada == null) {
      return const PainelSemCamada(titulo: _titulo);
    }
    if (visivel is! NullLayer || gravada is! NullLayer) {
      return PainelDeTipoErrado(
        titulo: _titulo,
        chave: chave,
        aviso: 'A grade de clones mora no objeto nulo.',
      );
    }
    final rig = visivel.grid;
    final rigGravado = gravada.grid;
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      abas: rig == null ? null : _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: escopo.fecharPainel,
      corpo: NoCabecote(
        construir: (context, t) => ListView(
          key: ValueKey('clonar-aba-${rig == null ? 'vazia' : _aba}'),
          padding: const EdgeInsets.fromLTRB(
            AureaDims.margemDoPainel,
            AureaDims.e4,
            AureaDims.margemDoPainel,
            AureaDims.topoDoPainel,
          ),
          children: rig == null || rigGravado == null
              ? [
                  const AureaAvisoDoPainel(
                    texto: 'Escolha as camadas que a grade vai repetir.',
                  ),
                  _linhaDasCamadas(null),
                ]
              : switch (_aba) {
                  0 => _grade(
                    visivel,
                    gravada,
                    rig,
                    rigGravado,
                    t,
                    escopo.playback,
                  ),
                  1 => _variacao(
                    visivel,
                    gravada,
                    rig,
                    rigGravado,
                    t,
                    escopo.playback,
                  ),
                  _ => _proximidade(visivel, rig, t),
                },
        ),
      ),
    );
  }

  // ------------------------------------------------------------ camadas

  Widget _linhaDasCamadas(GridRig? rig) => LinhaDePorta(
    key: const ValueKey('clonar-camadas'),
    rotulo: rig == null
        ? 'Escolher camadas'
        : 'Camadas da grade (${rig.assets.length})',
    icone: CupertinoIcons.square_stack_3d_up,
    aoTocar: _escolherCamadas,
  );

  /// A FOLHA DAS CAMADAS: cada toque poe ou tira da grade na hora — a
  /// ordem dos toques e a ordem dos clones.
  Future<void> _escolherCamadas() {
    EscopoDoEditor.of(context).playback.pause();
    return mostrarAureaFolha<void>(
      context,
      titulo: 'Camadas da grade',
      construtor: (folha) => Consumer(
        builder: (folha, ref, _) {
          final projeto = ref.watch(editorControllerProvider);
          final nulo = projeto.layerById(_id);
          final escolhidas = nulo is NullLayer
              ? (nulo.grid?.assets ?? const <String>[])
              : const <String>[];
          final candidatas = [
            for (final l in projeto.layers)
              if (l.id != _id && l is! NullLayer) l,
          ];
          if (candidatas.isEmpty) {
            return const Padding(
              padding: EdgeInsets.all(AureaDims.margemDoPainel),
              child: AureaAvisoDoPainel(
                texto: 'Não há outra camada para repetir.',
              ),
            );
          }
          return ListView(
            shrinkWrap: true,
            padding: const EdgeInsets.symmetric(vertical: AureaDims.e8),
            children: [
              for (final l in candidatas)
                AureaLayerRow(
                  key: ValueKey('clonar-camada-${l.id}'),
                  nome: l.name,
                  icone: layerTypeIcon(l),
                  selecionada: escolhidas.contains(l.id),
                  aoTocar: () {
                    final nova = [...escolhidas];
                    if (!nova.remove(l.id)) nova.add(l.id);
                    _c.setGridAssets(_id, nova);
                  },
                ),
            ],
          );
        },
      ),
    );
  }

  // --------------------------------------------------------------- grade

  Widget _trilha(
    String rotulo,
    String chave,
    AnimatedDouble visto,
    AnimatedDouble? gravado,
    Layer gravada,
    Duration t,
    PlaybackController playback, {
    required double min,
    required double max,
    double escala = 1,
    String unidade = '',
  }) {
    final local = gravada.localTime(t);
    return linhaNumerica(
      ref,
      rotulo: rotulo,
      chave: 'clonar-$chave',
      valor: visto.valueAt(local) * escala,
      min: min,
      max: max,
      unidade: unidade,
      losango: losangoDaTrilha(
        trilha: gravado,
        gravada: gravada,
        t: t,
        playback: playback,
        aoAlternar: () => _c.toggleGridParamKeyframe(_id, chave, t),
      ),
      aoMudar: (v) => _c.editGridParam(_id, chave, t, v / escala),
    );
  }

  List<Widget> _grade(
    NullLayer visivel,
    NullLayer gravada,
    GridRig rig,
    GridRig rigGravado,
    Duration t,
    PlaybackController playback,
  ) {
    final local = visivel.localTime(t);
    final morph = rig.transition.valueAt(local);
    return [
      _linhaDasCamadas(rig),
      AureaPropertyRow.personalizada(
        rotulo: 'Arranjo',
        chave: 'clonar-arranjo',
        filho: FileiraDePilulas<double>(
          chave: 'clonar-arranjo',
          chaveDe: (m) => '${m.round()}',
          opcoes: const [1.0, 2.0, 3.0],
          // ANIMADO, nenhum acende: o arranjo esta a caminho de outro.
          atual: rig.transition.isAnimated ? null : morph.roundToDouble(),
          rotuloDe: (m) => switch (m.round()) {
            1 => 'Retangular',
            2 => 'Radial',
            _ => 'Esférico',
          },
          // Com o morph animado, escolher um arranjo escreve no instante
          // (vale a regra da marca); parado, so troca o arranjo.
          aoEscolher: (m) => rig.transition.isAnimated
              ? _c.editGridTransition(_id, t, m)
              : _c.updateGrid(
                  _id,
                  (g) => g.copyWith(transition: AnimatedDouble(m)),
                ),
        ),
      ),
      linhaNumerica(
        ref,
        rotulo: 'Morph',
        chave: 'clonar-morph',
        valor: morph,
        min: 1,
        max: 3,
        casas: 2,
        losango: losangoDaTrilha(
          trilha: rigGravado.transition,
          gravada: gravada,
          t: t,
          playback: playback,
          aoAlternar: () => _c.toggleGridTransitionKeyframe(_id, t),
        ),
        aoMudar: (v) => _c.editGridTransition(_id, t, v),
      ),
      linhaNumerica(
        ref,
        rotulo: 'Colunas',
        chave: 'clonar-colunas',
        valor: rig.columns.toDouble(),
        min: 1,
        max: 12,
        aoMudar: (v) =>
            _c.updateGrid(_id, (g) => g.copyWith(columns: v.round())),
      ),
      _trilha(
        'Espaço X',
        'spacingX',
        rig.spacingX,
        rigGravado.spacingX,
        gravada,
        t,
        playback,
        min: 20,
        max: 800,
      ),
      _trilha(
        'Espaço Y',
        'spacingY',
        rig.spacingY,
        rigGravado.spacingY,
        gravada,
        t,
        playback,
        min: 20,
        max: 800,
      ),
      _trilha(
        'Raio',
        'radius',
        rig.radius,
        rigGravado.radius,
        gravada,
        t,
        playback,
        min: 40,
        max: 1200,
      ),
      _trilha(
        'Rotação',
        'rotation',
        rig.gridRotationDeg,
        rigGravado.gridRotationDeg,
        gravada,
        t,
        playback,
        min: -180,
        max: 180,
        unidade: '°',
      ),
      LinhaDeAcao(
        key: const ValueKey('clonar-remover'),
        rotulo: 'Remover a grade',
        icone: CupertinoIcons.trash,
        destrutiva: true,
        aoTocar: () => _c.removeGrid(_id),
      ),
    ];
  }

  // ------------------------------------------------------------ variacao

  List<Widget> _variacao(
    NullLayer visivel,
    NullLayer gravada,
    GridRig rig,
    GridRig rigGravado,
    Duration t,
    PlaybackController playback,
  ) {
    final nulos = [
      for (final l in ref.read(editorControllerProvider).layers)
        if (l is NullLayer && l.id != _id) l,
    ];
    return [
      _trilha(
        'Torção',
        'twist',
        rig.twistDeg,
        rigGravado.twistDeg,
        gravada,
        t,
        playback,
        min: -180,
        max: 180,
        unidade: '°',
      ),
      _trilha(
        'Escalonar',
        'stagger',
        rig.staggerDeg,
        rigGravado.staggerDeg,
        gravada,
        t,
        playback,
        min: -360,
        max: 360,
        unidade: '°',
      ),
      _trilha(
        'Profundidade',
        'zDepth',
        rig.zDepth,
        rigGravado.zDepth,
        gravada,
        t,
        playback,
        min: -400,
        max: 400,
      ),
      _trilha(
        'Escala na frente',
        'scaleFront',
        rig.scaleFront,
        rigGravado.scaleFront,
        gravada,
        t,
        playback,
        min: 10,
        max: 300,
        escala: 100,
        unidade: '%',
      ),
      _trilha(
        'Escala atrás',
        'scaleBack',
        rig.scaleBack,
        rigGravado.scaleBack,
        gravada,
        t,
        playback,
        min: 10,
        max: 300,
        escala: 100,
        unidade: '%',
      ),
      _trilha(
        'Acaso',
        'randomOffset',
        rig.randomOffset,
        rigGravado.randomOffset,
        gravada,
        t,
        playback,
        min: 0,
        max: 300,
      ),
      linhaNumerica(
        ref,
        rotulo: 'Semente',
        chave: 'clonar-semente',
        valor: rig.seed.toDouble(),
        min: 0,
        max: 100,
        aoMudar: (v) => _c.updateGrid(_id, (g) => g.copyWith(seed: v.round())),
      ),
      linhaDeLigar(
        rotulo: 'Embaralhar',
        chave: 'clonar-embaralhar',
        valor: rig.shuffle,
        aoMudar: (v) => _c.updateGrid(_id, (g) => g.copyWith(shuffle: v)),
      ),
      // NULO CONTROLADOR: um segundo nulo cuja transformacao modula a grade
      // (escala -> espaco e raio; giro Z -> rotacao; giro Y -> torcao).
      AureaPropertyRow.personalizada(
        rotulo: 'Controlador',
        chave: 'clonar-controlador',
        filho: AureaDropdown<String?>(
          valor: nulos.any((n) => n.id == rig.controllerId)
              ? rig.controllerId
              : null,
          opcoes: [null, for (final n in nulos) n.id],
          rotuloDe: (id) =>
              id == null ? '—' : nulos.firstWhere((n) => n.id == id).name,
          traduzir: false,
          titulo: 'Nulo controlador',
          aoMudar: (id) => _c.setGridController(_id, id),
        ),
      ),
    ];
  }

  // --------------------------------------------------------- proximidade

  List<Widget> _proximidade(NullLayer visivel, GridRig rig, Duration t) {
    final local = visivel.localTime(t);
    final prox = rig.proximity;
    final ligada = prox?.enabled ?? false;
    void mexer(ProximityGroup Function(ProximityGroup p) f) =>
        _c.updateGrid(_id, (g) {
          final p = g.proximity;
          return p == null ? g : g.copyWith(proximity: f(p));
        });
    return [
      linhaDeLigar(
        rotulo: 'Proximidade',
        chave: 'clonar-proximidade',
        valor: ligada,
        aoMudar: (v) => _c.updateGrid(
          _id,
          (g) => g.copyWith(
            proximity: (g.proximity ?? ProximityGroup()).copyWith(enabled: v),
          ),
        ),
      ),
      if (prox != null && ligada) ...[
        AureaPropertyRow.ponto(
          rotulo: 'Centro',
          chave: 'clonar-effector',
          x: prox.effector.valueAt(local).dx,
          y: prox.effector.valueAt(local).dy,
          aoMudarX: aCadaPasso(
            (v) => mexer((p) {
              final atual = p.effector.valueAt(local);
              return p.copyWith(
                effector: p.effector.edited(local, Offset(v, atual.dy)),
              );
            }),
          ),
          aoMudarY: aCadaPasso(
            (v) => mexer((p) {
              final atual = p.effector.valueAt(local);
              return p.copyWith(
                effector: p.effector.edited(local, Offset(atual.dx, v)),
              );
            }),
          ),
          aoComecarGesto: _c.beginGesture,
          aoTerminarGesto: _c.endGesture,
        ),
        linhaNumerica(
          ref,
          rotulo: 'Alcance',
          chave: 'clonar-alcance',
          valor: prox.radius.valueAt(local),
          min: 20,
          max: 800,
          aoMudar: (v) =>
              mexer((p) => p.copyWith(radius: p.radius.edited(local, v))),
        ),
        linhaNumerica(
          ref,
          rotulo: 'Escala máxima',
          chave: 'clonar-escala-max',
          valor: prox.scaleMax * 100,
          min: 20,
          max: 400,
          unidade: '%',
          aoMudar: (v) => mexer((p) => p.copyWith(scaleMax: v / 100)),
        ),
        linhaNumerica(
          ref,
          rotulo: 'Atrair',
          chave: 'clonar-atrair',
          valor: prox.attract.valueAt(local),
          min: -300,
          max: 300,
          aoMudar: (v) =>
              mexer((p) => p.copyWith(attract: p.attract.edited(local, v))),
        ),
        const AureaAvisoDoPainel(
          texto: 'O alcance é uma esfera: também vale em profundidade.',
        ),
      ],
    ];
  }
}
