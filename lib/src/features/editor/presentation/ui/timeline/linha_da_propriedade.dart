import 'package:flutter/gestures.dart' show DragStartBehavior;
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../application/editor_controller.dart';
import '../../../application/keyframe_clipboard.dart';
import '../../../application/perfil3d.dart';
import '../curva/curva.dart';
import '../shell/contrato.dart' show propriedadeAtivaProvider;
import 'area_de_toque.dart';
import 'arrasto_de_losango.dart';
import 'estado_da_timeline.dart';
import 'geometria.dart';
import 'keyframes_da_timeline.dart';
import 'pintor_da_linha.dart';

/// UMA PROPRIEDADE ANIMADA da camada aberta (28): o nome dela no lugar do
/// cabecalho e os losangos SO dela no trilho do tempo da camada.
///
/// A da propriedade ATIVA acende (nome e losangos em destaque); as outras
/// ficam apagadas. Tocar no nome faz dela a ativa.
///
/// Losango: toque escolhe (soma a selecao), arrastar move SO a marca desta
/// propriedade (as outras do mesmo instante ficam), toque longo abre a
/// curva desta propriedade.
class LinhaDaPropriedade extends ConsumerStatefulWidget {
  const LinhaDaPropriedade({
    super.key,
    required this.layerId,
    required this.chave,
  });

  final String layerId;
  final ChaveDaTrilha chave;

  @override
  ConsumerState<LinhaDaPropriedade> createState() => _LinhaDaPropriedadeState();
}

class _LinhaDaPropriedadeState extends ConsumerState<LinhaDaPropriedade>
    with AutomaticKeepAliveClientMixin {
  late EstadoDaTimeline _e;
  ArrastoDeLosango? _arrasto;

  String get _id => widget.layerId;
  ChaveDaTrilha get _chave => widget.chave;

  @override
  bool get wantKeepAlive => _arrasto != null;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _e = EscopoDaTimeline.de(context);
  }

  @override
  void dispose() {
    if (_arrasto != null) {
      _arrasto!.descartar();
      _e.autoRolagem.parar();
      _e.soltarVista();
    }
    super.dispose();
  }

  TrilhaAnimada? _trilha() {
    final l = ref.read(editorControllerProvider).layerById(_id);
    return l == null ? null : trilhaDe(l, _chave);
  }

  int? _losangoSob(Offset p) {
    final l = ref.read(editorControllerProvider).layerById(_id);
    final t = _trilha();
    if (l == null || t == null) return null;
    return GeometriaDaLinha.losangoEm(
      _e,
      l.startTime.inMicroseconds,
      t.temposUs,
      p,
    );
  }

  void _ativar() {
    HapticFeedback.selectionClick();
    ref.read(propriedadeAtivaProvider.notifier).state = ativaDaTrilha(_chave);
  }

  void _tocarNoLosango(TapUpDetails d) {
    final l = ref.read(editorControllerProvider).layerById(_id);
    final us = _losangoSob(d.localPosition);
    if (l == null || us == null) return;
    HapticFeedback.selectionClick();
    _e.playback.pause();
    final prop = _chave.prop;
    if (prop != null) {
      ref.read(keyframesSelecionadosProvider.notifier).state = alternarMarca(
        ref.read(keyframesSelecionadosProvider),
        (layerId: _id, prop: prop, tempo: Duration(microseconds: us)),
      );
    }
    // Mexer num losango desta linha poe a propriedade dela em foco.
    ref.read(propriedadeAtivaProvider.notifier).state = ativaDaTrilha(_chave);
    _e.playback.seek(Duration(microseconds: l.startTime.inMicroseconds + us));
  }

  void _segurarLosango(LongPressStartDetails d) {
    final l = ref.read(editorControllerProvider).layerById(_id);
    final us = _losangoSob(d.localPosition);
    if (l == null || us == null) return;
    HapticFeedback.mediumImpact();
    final prop = _chave.prop;
    abrirEditorDeCurva(
      context,
      ref,
      layerId: _id,
      trilha: prop != null
          ? TrilhaDaCurva.transformacao(prop)
          : TrilhaDaCurva.efeito(_chave.efeitoId!),
      tempo: Duration(microseconds: l.startTime.inMicroseconds + us),
      playback: _e.playback,
    );
  }

  void _comecar(DragStartDetails d) {
    final p = ref.read(editorControllerProvider);
    final l = p.layerById(_id);
    final t = _trilha();
    if (l == null || t == null || _arrasto != null) return;
    final origem = GeometriaDaLinha.losangoEm(
      _e,
      l.startTime.inMicroseconds,
      t.temposUs,
      d.localPosition,
    );
    if (origem == null) return;
    if (p.metaOf(_id).locked) {
      HapticFeedback.lightImpact();
      return;
    }
    _e.playback.pause();
    final c = ref.read(editorControllerProvider.notifier);
    final prop = _chave.prop;
    final efeito = _chave.efeitoId;
    _arrasto = ArrastoDeLosango.comecar(
      estado: _e,
      controlador: c,
      camada: l,
      vizinhas: t.temposUs,
      origemUs: origem,
      xDoDedo: d.localPosition.dx,
      fps: p.fps,
      mover: (de, para) {
        if (prop == null) {
          return c.moverKeyframeDoEfeito(
            _id,
            efeito!,
            Duration(microseconds: de),
            Duration(microseconds: para),
          );
        }
        // VARIAS ESCOLHIDAS, e esta entre elas: andam todas juntas (a
        // relacao de tempo entre elas e o que escolher varias promete).
        final sel = ref.read(keyframesSelecionadosProvider);
        final esta = (
          layerId: _id,
          prop: prop,
          tempo: Duration(microseconds: de),
        );
        if (sel.length > 1 && marcaSelecionada(sel, esta)) {
          return c.moverKeyframes(sel, Duration(microseconds: para - de));
        }
        return c.moverKeyframeDaProp(
          _id,
          prop,
          Duration(microseconds: de),
          Duration(microseconds: para),
        );
      },
    );
    _e.segurarVista();
    updateKeepAlive();
  }

  void _seguir(DragUpdateDetails d) {
    final a = _arrasto;
    if (a == null) return;
    a.seguir(d.localPosition.dx);
    _e.autoRolagem.horizontal(
      d.localPosition.dx,
      () => a.seguir(a.xDoDedo),
      desde: a.xInicial,
    );
  }

  void _terminar() {
    final a = _arrasto;
    if (a == null) return;
    a.encerrar();
    _arrasto = null;
    _e.autoRolagem.parar();
    _e.soltarVista();
    updateKeepAlive();
  }

  @override
  Widget build(BuildContext context) {
    super.build(context);
    SondaDaTimeline.buildsDeLinha++;
    Perfil3D.contar('build.linha');
    final camada = ref.watch(
      editorControllerProvider.select((p) => p.layerById(_id)),
    );
    final trilha = camada == null ? null : trilhaDe(camada, _chave);
    if (camada == null || trilha == null) {
      return const SizedBox(height: AureaDims.linhaDeCamada);
    }
    final ativa = ref.watch(
      propriedadeAtivaProvider.select((a) => trilhaEstaAtiva(_chave, a)),
    );
    final algumaAtiva = ref.watch(
      propriedadeAtivaProvider.select((a) => a != null),
    );
    final prop = _chave.prop;
    final selecionados = prop == null
        ? const <int>{}
        : ref
              .watch(
                keyframesSelecionadosProvider.select(
                  (s) => _Conjunto(instantesSelecionadosDaProp(s, _id, prop)),
                ),
              )
              .valores;
    final travada = ref.watch(
      editorControllerProvider.select((p) => p.metaOf(_id).locked),
    );
    final e = _e;
    final inicioUs = camada.startTime.inMicroseconds;
    final cores = CoresDaTimeline.atuais;
    final base = DefaultTextStyle.of(context).style;
    return SizedBox(
      height: AureaDims.linhaDeCamada,
      child: Stack(
        children: [
          Positioned.fill(
            child: RepaintBoundary(
              child: CustomPaint(
                painter: PintorDaTrilha(
                  estado: e,
                  inicioUs: inicioUs,
                  fimUs: camada.endTime.inMicroseconds,
                  losangos: LosangosDaLinha(
                    temposUs: trilha.temposUs,
                    // Outra propriedade em foco: esta fica apagada inteira.
                    acesos: algumaAtiva && !ativa ? const <int>{} : null,
                    selecionados: selecionados,
                  ),
                  cores: cores,
                  ativa: ativa,
                ),
              ),
            ),
          ),
          Positioned.fill(
            child: AreaDeToqueCalculada(
              acerta: (p) =>
                  GeometriaDaLinha.losangoEm(e, inicioUs, trilha.temposUs, p) !=
                  null,
              child: GestureDetector(
                key: ValueKey('losangos-$_id-${_chave.nome}'),
                behavior: HitTestBehavior.opaque,
                dragStartBehavior: DragStartBehavior.down,
                onTapUp: _tocarNoLosango,
                onLongPressStart: _segurarLosango,
                onHorizontalDragStart: travada ? null : _comecar,
                onHorizontalDragUpdate: travada ? null : _seguir,
                onHorizontalDragEnd: travada ? null : (_) => _terminar(),
                onHorizontalDragCancel: travada ? null : _terminar,
                child: const SizedBox.expand(),
              ),
            ),
          ),
          // O NOME DA PROPRIEDADE no lugar do cabecalho; tocar poe em foco.
          Positioned(
            left: 0,
            top: 0,
            bottom: 0,
            width: AureaDims.cabecalhoDaCamada,
            child: GestureDetector(
              key: ValueKey('trilha-nome-$_id-${_chave.nome}'),
              behavior: HitTestBehavior.opaque,
              onTap: _ativar,
              child: ColoredBox(
                color: AureaCores.cromo,
                child: Padding(
                  padding: const EdgeInsets.only(
                    left: AureaDims.faixaDeCor + AureaDims.e4,
                    right: AureaDims.e4,
                  ),
                  child: Align(
                    alignment: Alignment.centerLeft,
                    child: AppText(
                      trilha.rotulo,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: base.copyWith(
                        fontSize: AureaDims.textoDeRotulo,
                        fontWeight: ativa ? FontWeight.w700 : FontWeight.w500,
                        color: ativa
                            ? AureaCores.destaque
                            : AureaCores.textoSecundario,
                        decoration: TextDecoration.none,
                      ),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// Um conjunto que compara por valor (o `select` so avisa quando muda).
@immutable
class _Conjunto {
  const _Conjunto(this.valores);

  final Set<int> valores;

  @override
  bool operator ==(Object other) =>
      other is _Conjunto &&
      other.valores.length == valores.length &&
      other.valores.containsAll(valores);

  @override
  int get hashCode => Object.hashAllUnordered(valores);
}
