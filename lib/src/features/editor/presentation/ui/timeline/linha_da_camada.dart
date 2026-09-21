import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter/gestures.dart' show DragStartBehavior;
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../application/keyframe_clipboard.dart';
import '../../../application/media_preview_service.dart';
import '../../../application/perfil3d.dart';
import '../../../domain/cut_ops.dart' show videoSourceSpan;
import '../../../domain/keyframe.dart' show kToleranciaDoKeyframe;
import '../../../domain/layer.dart';
import '../../../domain/onda_no_clipe.dart';
import '../../am/layer_look.dart';
import '../curva/curva.dart';
import '../shell/contrato.dart' show PropriedadeAtiva, propriedadeAtivaProvider;
import '../toolbar/menu_da_camada.dart' show mostrarMenuDaCamada;
import 'area_de_toque.dart';
import 'arrasto_de_losango.dart';
import 'cabecalho_da_camada.dart';
import 'estado_da_timeline.dart';
import 'geometria.dart';
import 'ima.dart';
import 'keyframes_da_timeline.dart';
import 'linhas.dart';
import 'pintor_da_linha.dart';
import 'sessao_de_gesto.dart';

/// O que o arrasto do clipe faz.
enum _Arrasto { mover, trimInicio, trimFim }

/// UMA CAMADA NA TIMELINE (28): o cabecalho fixo a esquerda e o clipe que
/// anda com o tempo.
///
/// RECONSTROI SO QUANDO A CAMADA DELA MUDA: observa a camada (por
/// `select`), a meta dela e se ela esta escolhida — nunca o projeto
/// inteiro, nunca o relogio. Arrastar um clipe muta o projeto a cada
/// quadro, e so a linha daquele clipe desce ate o pintor.
///
/// Os gestos (ver o mapa no topo de `timeline.dart`):
///   * toque no clipe ........ escolhe a camada
///   * toque longo no clipe .. menu da camada
///   * arrastar o clipe ESCOLHIDO .. move no tempo (ima + guia + toque)
///   * arrastar a alca (30 fora do clipe) .. apara (ima)
///   * toque no losango ...... escolhe a marca (soma a selecao) e leva o
///                             cabecote ate ela
///   * arrastar o losango .... move a marca (ima no cabecote)
///   * toque longo no losango  editor de curva
///   * cabecalho: toque escolhe (na escolhida, abre as propriedades);
///     toque longo + arrastar, ou a alca de 35, reordena
class LinhaDaCamada extends ConsumerStatefulWidget {
  const LinhaDaCamada({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<LinhaDaCamada> createState() => _LinhaDaCamadaState();
}

class _LinhaDaCamadaState extends ConsumerState<LinhaDaCamada>
    with AutomaticKeepAliveClientMixin {
  late EstadoDaTimeline _e;

  String get _id => widget.layerId;

  // ------------------------------------------------ arrasto do clipe/alca
  _Arrasto? _arrasto;
  SessaoDeGesto? _sessao;
  Ima? _ima;
  final HapticoDoIma _haptico = HapticoDoIma();
  int _duracaoUs = 0;
  double _deslocUs = 0;
  double _xDoDedo = 0;
  double _xInicial = 0;
  int _fps = 30;

  // ------------------------------------------------------------- losango
  ArrastoDeLosango? _losango;

  // ----------------------------------------------------------- reordenar
  bool _reordenando = false;

  // --------------------------------------------------------------- midia
  Timer? _pedidoDeMidia;
  String? _midiaPedida;

  /// UMA LINHA EM GESTO NAO MORRE: a lista e virtual, e a auto-rolagem pode
  /// levar a linha para fora da tela no meio do arrasto. Sem isto o
  /// reconhecedor dela morreria junto, e o dedo perderia o clipe.
  @override
  bool get wantKeepAlive =>
      _arrasto != null || _losango != null || _reordenando;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _e = EscopoDaTimeline.de(context);
  }

  @override
  void dispose() {
    _pedidoDeMidia?.cancel();
    if (_arrasto != null) {
      _sessao?.descartar();
      _e.guiaUs.value = null;
      _e.autoRolagem.parar();
      _e.soltarVista();
    }
    if (_losango != null) {
      _losango!.descartar();
      _e.autoRolagem.parar();
      _e.soltarVista();
    }
    if (_reordenando) _e.reordenar?.terminar(_id, cancelou: true);
    super.dispose();
  }

  // ============================================================ selecao

  void _escolher() {
    _e.playback.pause();
    // MODO SELECIONAR: o toque marca e desmarca.
    if (ref.read(modoSelecionarProvider)) {
      final r = alternarNaSelecao(
        ref.read(multiSelectProvider),
        ref.read(selectedLayerProvider),
        _id,
      );
      ref.read(multiSelectProvider.notifier).state = r.multi;
      ref.read(selectedLayerProvider.notifier).state = r.principal;
      return;
    }
    ref.read(multiSelectProvider.notifier).state = const {};
    ref.read(selectedLayerProvider.notifier).state = _id;
    _e.aoTocarNaCamada?.call(_id);
  }

  void _tocarNoClipe() {
    HapticFeedback.selectionClick();
    _escolher();
  }

  /// Na camada ja escolhida e animada, o toque no cabecalho abre (ou
  /// fecha) as linhas das propriedades.
  void _tocarNoCabecalho(bool temAnimacao) {
    HapticFeedback.selectionClick();
    if (temAnimacao &&
        ref.read(selectedLayerProvider) == _id &&
        !ref.read(modoSelecionarProvider)) {
      final abertas = ref.read(camadasExpandidasProvider);
      ref.read(camadasExpandidasProvider.notifier).state = abertas.contains(_id)
          ? ({...abertas}..remove(_id))
          : {...abertas, _id};
      return;
    }
    _escolher();
  }

  void _menu(LongPressStartDetails d) {
    HapticFeedback.mediumImpact();
    mostrarMenuDaCamada(
      context,
      ref,
      _id,
      posicao: d.globalPosition,
      playback: _e.playback,
    );
  }

  void _entrarNoGrupo() {
    HapticFeedback.selectionClick();
    ref.read(editorControllerProvider.notifier).enterGroup(_id);
  }

  void _avisar(String motivo) {
    HapticFeedback.lightImpact();
    AureaSnack.show(
      context,
      translate(context, motivo),
      duration: const Duration(milliseconds: 2400),
    );
  }

  // ============================================ mover o clipe e aparar

  void _comecarArrasto(_Arrasto tipo, DragStartDetails d) {
    final p = ref.read(editorControllerProvider);
    final l = p.layerById(_id);
    if (l == null || _arrasto != null) return;
    if (p.metaOf(_id).locked) {
      _avisar('Camada bloqueada: desbloqueie para editar');
      return;
    }
    _e.playback.pause();
    _arrasto = tipo;
    _sessao = SessaoDeGesto(ref.read(editorControllerProvider.notifier));
    _ima = Ima.para(p, excluir: _id);
    _fps = p.fps < 1 ? 30 : p.fps;
    _duracaoUs = l.duration.inMicroseconds;
    final origem = tipo == _Arrasto.trimFim
        ? l.endTime.inMicroseconds
        : l.startTime.inMicroseconds;
    _xDoDedo = d.localPosition.dx;
    _xInicial = _xDoDedo;
    // O ITEM FICA SOB O DEDO no ponto em que foi pego: o alvo de cada
    // passo e "instante sob o dedo + esta distancia", e nao a soma dos
    // passos — o ima nunca prende o dedo, e a auto-rolagem (que move o
    // tempo com o dedo parado) cai na mesma conta.
    _deslocUs = origem - _e.tempoDoX(_xDoDedo);
    _e.segurarVista();
    updateKeepAlive();
  }

  void _comecarTrim(DragStartDetails d) {
    final l = ref.read(editorControllerProvider).layerById(_id);
    if (l == null) return;
    final c = GeometriaDaLinha.clipe(_e, l);
    final lado =
        GeometriaDaLinha.naAlca(_e, l, d.localPosition) ??
        (d.localPosition.dx < (c.x0 + c.x1) / 2
            ? LadoDaAlca.inicio
            : LadoDaAlca.fim);
    HapticFeedback.lightImpact();
    _comecarArrasto(
      lado == LadoDaAlca.inicio ? _Arrasto.trimInicio : _Arrasto.trimFim,
      d,
    );
  }

  void _seguirArrasto(DragUpdateDetails d) {
    if (_arrasto == null) return;
    _xDoDedo = d.localPosition.dx;
    _aplicarArrasto();
    _e.autoRolagem.horizontal(_xDoDedo, _aplicarArrasto, desde: _xInicial);
  }

  void _aplicarArrasto() {
    final s = _sessao;
    final ima = _ima;
    final tipo = _arrasto;
    if (s == null || ima == null || tipo == null || !mounted) return;
    final desejado = naGradeDeQuadros(_e.tempoDoX(_xDoDedo) + _deslocUs, _fps);
    final cabecote = _e.vistaUs.value.round();
    final tol = _e.usPorPx(toleranciaDoImaDp);
    final int alvo;
    final int? guia;
    if (tipo == _Arrasto.mover) {
      final r = ima.encaixarIntervalo(
        desejado,
        _duracaoUs,
        cabecoteUs: cabecote,
        tolUs: tol,
      );
      alvo = math.max(0, r.inicioUs);
      guia = r.guiaUs;
    } else {
      final g = ima.alvoPerto(desejado, cabecoteUs: cabecote, tolUs: tol);
      alvo = math.max(0, g ?? desejado);
      guia = g;
    }
    _e.guiaUs.value = guia;
    _haptico.avisar(guia);
    s.pedir(() {
      final atual = ref.read(editorControllerProvider).layerById(_id);
      if (atual == null) return;
      final c = s.controlador;
      final para = Duration(microseconds: alvo);
      switch (tipo) {
        case _Arrasto.mover:
          if (atual.startTime == para) return;
          s.abrir();
          c.moveLayer(_id, para);
        case _Arrasto.trimInicio:
          if (atual.startTime == para) return;
          s.abrir();
          c.trimLayerStart(_id, para);
        case _Arrasto.trimFim:
          if (atual.endTime == para) return;
          s.abrir();
          c.trimLayerEnd(_id, para);
      }
    });
  }

  void _terminarArrasto() {
    if (_arrasto == null) return;
    _sessao?.encerrar();
    _sessao = null;
    _arrasto = null;
    _ima = null;
    _e.guiaUs.value = null;
    _e.autoRolagem.parar();
    _e.soltarVista();
    updateKeepAlive();
  }

  // ================================================================ losango

  int? _losangoSob(Offset p) {
    final l = ref.read(editorControllerProvider).layerById(_id);
    if (l == null) return null;
    return GeometriaDaLinha.losangoEm(
      _e,
      l.startTime.inMicroseconds,
      instantesDaCamada(l),
      p,
    );
  }

  void _tocarNoLosango(TapUpDetails d) {
    final l = ref.read(editorControllerProvider).layerById(_id);
    final us = _losangoSob(d.localPosition);
    if (l == null || us == null) return;
    HapticFeedback.selectionClick();
    _e.playback.pause();
    // O LOSANGO DA LINHA E UM INSTANTE: tocar escolhe todas as marcas de
    // propriedade dele (e tira, se ja estavam todas escolhidas). Soma a
    // selecao que ja havia — e assim que se escolhem varias.
    final marcas = marcasDoInstante(l, Duration(microseconds: us));
    if (marcas.isNotEmpty) {
      var sel = ref.read(keyframesSelecionadosProvider);
      final todas = marcas.every((m) => marcaSelecionada(sel, m));
      for (final m in marcas) {
        if (todas || !marcaSelecionada(sel, m)) sel = alternarMarca(sel, m);
      }
      ref.read(keyframesSelecionadosProvider.notifier).state = sel;
    }
    _e.playback.seek(Duration(microseconds: l.startTime.inMicroseconds + us));
  }

  /// TOQUE LONGO NO LOSANGO: o editor de curva do trecho que sai dele.
  void _segurarLosango(LongPressStartDetails d) {
    final l = ref.read(editorControllerProvider).layerById(_id);
    final us = _losangoSob(d.localPosition);
    if (l == null || us == null) return;
    HapticFeedback.mediumImpact();
    final trilha = _trilhaDaCurva(l, us);
    if (trilha == null) return;
    abrirEditorDeCurva(
      context,
      ref,
      layerId: _id,
      trilha: trilha,
      tempo: Duration(microseconds: l.startTime.inMicroseconds + us),
      playback: _e.playback,
    );
  }

  /// A curva de QUAL propriedade: a ativa, se ela tem marca ali; senao a
  /// primeira que tem (na ordem do painel); senao o primeiro efeito.
  TrilhaDaCurva? _trilhaDaCurva(Layer l, int us) {
    final ativa = ref.read(propriedadeAtivaProvider);
    final local = Duration(microseconds: us);
    final prop = ativa?.prop;
    if (prop != null && temMarcaDaPropEm(l, prop, local)) {
      return TrilhaDaCurva.transformacao(prop);
    }
    final efeitoAtivo = ativa?.efeitoId;
    for (final t in trilhasAnimadas(l)) {
      if (!temInstante(t.temposUs.toSet(), us)) continue;
      final chave = t.chave;
      if (efeitoAtivo != null && chave.efeitoId == efeitoAtivo) {
        return TrilhaDaCurva.efeito(efeitoAtivo);
      }
    }
    for (final t in trilhasAnimadas(l)) {
      if (!temInstante(t.temposUs.toSet(), us)) continue;
      final chave = t.chave;
      return chave.prop != null
          ? TrilhaDaCurva.transformacao(chave.prop!)
          : TrilhaDaCurva.efeito(chave.efeitoId!);
    }
    return null;
  }

  void _comecarLosango(DragStartDetails d) {
    final p = ref.read(editorControllerProvider);
    final l = p.layerById(_id);
    if (l == null || _losango != null) return;
    final tempos = instantesDaCamada(l);
    final origem = GeometriaDaLinha.losangoEm(
      _e,
      l.startTime.inMicroseconds,
      tempos,
      d.localPosition,
    );
    if (origem == null) return;
    if (p.metaOf(_id).locked) {
      _avisar('Camada bloqueada: desbloqueie para mover o keyframe');
      return;
    }
    final motivo = l.porQueNaoArrastaKeyframeEm(Duration(microseconds: origem));
    if (motivo == MotivoDoKeyframeParado.modulo) {
      _avisar(
        'Este keyframe anima forma, grade, lente ou cena 3D e ainda não se '
        'arrasta',
      );
      return;
    }
    if (motivo != null) return;
    _e.playback.pause();
    final c = ref.read(editorControllerProvider.notifier);
    _losango = ArrastoDeLosango.comecar(
      estado: _e,
      controlador: c,
      camada: l,
      vizinhas: tempos,
      origemUs: origem,
      xDoDedo: d.localPosition.dx,
      fps: p.fps,
      mover: (de, para) => c.moverKeyframe(
        _id,
        Duration(microseconds: de),
        Duration(microseconds: para),
      ),
    )..aoMover = _selecaoAcompanha;
    _e.segurarVista();
    updateKeepAlive();
  }

  void _seguirLosango(DragUpdateDetails d) {
    final a = _losango;
    if (a == null) return;
    a.seguir(d.localPosition.dx);
    _e.autoRolagem.horizontal(
      d.localPosition.dx,
      () => a.seguir(a.xDoDedo),
      desde: a.xInicial,
    );
  }

  void _terminarLosango() {
    final a = _losango;
    if (a == null) return;
    a.encerrar();
    _losango = null;
    _e.autoRolagem.parar();
    _e.soltarVista();
    updateKeepAlive();
  }

  /// A SELECAO ACOMPANHA A MARCA: e a mesma marca, so mudou de instante.
  /// (`moverKeyframe` leva o instante inteiro e nao mexe na selecao.)
  void _selecaoAcompanha(int de, int para) {
    final sel = ref.read(keyframesSelecionadosProvider);
    final tol = kToleranciaDoKeyframe.inMicroseconds;
    bool daqui(MarcaSelecionada m) =>
        m.layerId == _id && (m.tempo.inMicroseconds - de).abs() < tol;
    if (!sel.any(daqui)) return;
    ref.read(keyframesSelecionadosProvider.notifier).state = {
      for (final m in sel)
        daqui(m)
            ? (
                layerId: m.layerId,
                prop: m.prop,
                tempo: Duration(microseconds: para),
              )
            : m,
    };
  }

  // =============================================================== reordenar

  void _comecarReordenar(Offset global) {
    final r = _e.reordenar;
    if (r == null) return;
    final indice = ref
        .read(editorControllerProvider)
        .layers
        .indexWhere((l) => l.id == _id);
    if (indice < 0) return;
    _e.playback.pause();
    r.comecar(_id, global, indice: indice);
    if (r.id == _id) {
      setState(() => _reordenando = true);
      updateKeepAlive();
    }
  }

  void _moverReordenar(Offset global) => _e.reordenar?.mover(_id, global);

  void _terminarReordenar({required bool cancelou}) {
    if (!_reordenando) return;
    _e.reordenar?.terminar(_id, cancelou: cancelou);
    if (mounted) setState(() => _reordenando = false);
    updateKeepAlive();
  }

  // =================================================================== midia

  /// A ONDA E A TIRA SAO CARAS: pedidas uma vez, com a interface ociosa e
  /// nunca durante o play — a barra aparece na hora e ganha a previa quando
  /// ela fica pronta.
  void _pedirMidia(Layer l) {
    final caminho = switch (l) {
      VideoLayer v => v.sourcePath,
      AudioLayer a => a.sourcePath,
      _ => null,
    };
    if (caminho == null || caminho == _midiaPedida) return;
    _midiaPedida = caminho;
    _pedidoDeMidia?.cancel();
    _pedidoDeMidia = Timer(
      const Duration(milliseconds: 900),
      () => _pedirQuandoOcioso(l),
    );
  }

  void _pedirQuandoOcioso(Layer l) {
    if (!mounted) return;
    if (_e.playback.playing.value) {
      _pedidoDeMidia = Timer(
        const Duration(milliseconds: 500),
        () => _pedirQuandoOcioso(l),
      );
      return;
    }
    final s = MediaPreviewService.instance;
    if (l is AudioLayer) {
      s.ensureWaveform(l.sourcePath).ignore();
    } else if (l is VideoLayer) {
      // A TIRA COBRE O ARQUIVO INTEIRO quando a duracao e conhecida.
      s
          .ensureFilmstrip(
            l.sourcePath,
            l.sourceDuration ?? l.sourceOffset + videoSourceSpan(l),
          )
          .ignore();
    }
  }

  static final Expando<Float64List> _fontes = Expando('fonte-da-onda');

  MidiaDoClipe? _midia(Layer l) {
    final s = MediaPreviewService.instance;
    switch (l) {
      case VideoLayer v:
        final tira = s.stripOf(v.sourcePath);
        if (tira == null || tira.isEmpty) return null;
        final ini = v.sourceOffset;
        final fim = ini + videoSourceSpan(v);
        return MidiaDoClipe(
          tira: tira,
          inicio: ini,
          fim: fim,
          duracaoDaFonte: v.sourceDuration ?? fim,
        );
      case AudioLayer a:
        final p = s.pyramidOf(a.sourcePath);
        if (p == null || p.isEmpty) return null;
        final ini = a.sourceOffset;
        final fim = ini + a.sourceSpan;
        return MidiaDoClipe(
          piramide: p,
          fonte: _fontes[a] ??= fonteAoLongoDoClipe(a),
          inicio: ini,
          fim: fim,
          duracaoDaFonte: fim,
          ganho: s.ganhoDaOnda(a.sourcePath),
          mudo: a.volume <= 0 || a.audio.muted,
        );
      default:
        return null;
    }
  }

  // =================================================================== build

  @override
  Widget build(BuildContext context) {
    super.build(context);
    SondaDaTimeline.buildsDeLinha++;
    Perfil3D.contar('build.linha');
    final camada = ref.watch(
      projetoVisivelProvider.select((p) => p.layerById(_id)),
    );
    if (camada == null) {
      return const SizedBox(height: AureaDims.linhaDeCamada);
    }
    final meta = ref.watch(
      projetoVisivelProvider.select((p) {
        final m = p.metaOf(_id);
        return (oculta: m.hidden, travada: m.locked, etiqueta: m.label?.color);
      }),
    );
    final escolhida = ref.watch(selectedLayerProvider.select((s) => s == _id));
    final naMulti = ref.watch(
      multiSelectProvider.select((m) => m.contains(_id)),
    );
    final lote = ref.watch(multiSelectProvider.select((m) => m.isNotEmpty));
    final expandida = ref.watch(
      camadasExpandidasProvider.select((s) => s.contains(_id)),
    );
    final comLosangos = escolhida || naMulti;
    // OS LOSANGOS SAO DO PROJETO GRAVADO. A pendencia (o valor mexido fora
    // de uma marca) mostra na previa uma marca que ainda NAO existe; na
    // timeline ela nao aparece ate o losango do painel cravar — o losango
    // nao mente (`docs/keyframe-explicito.md`). O resto (posicao, duracao,
    // nome) vem do projeto que se ve.
    final real = comLosangos
        ? ref.watch(editorControllerProvider.select((p) => p.layerById(_id))) ??
              camada
        : camada;
    final PropriedadeAtiva? ativa = comLosangos
        ? ref.watch(propriedadeAtivaProvider)
        : null;
    final selecao = comLosangos
        ? ref.watch(keyframesSelecionadosProvider)
        : const <MarcaSelecionada>{};
    final tempos = instantesDaCamada(real);
    final losangos = comLosangos
        ? LosangosDaLinha(
            temposUs: tempos,
            acesos: instantesAcesos(real, ativa),
            selecionados: instantesSelecionados(selecao, _id),
          )
        : null;
    final travada = meta.travada;
    final podeEditar = escolhida && !travada;
    final comAlcas = podeEditar && !lote;
    final temAnimacao = trilhasAnimadas(real).isNotEmpty;
    _pedirMidia(camada);

    final e = _e;
    final inicioUs = camada.startTime.inMicroseconds;
    final base = DefaultTextStyle.of(context).style;
    final estiloDoNome = base.copyWith(
      fontSize: AureaDims.rotuloDoClipe,
      fontWeight: FontWeight.w600,
      color: AureaCores.texto,
      decoration: TextDecoration.none,
    );
    final cores = CoresDaTimeline.atuais;
    final cor = layerTypeColor(camada);
    final ehMidia = camada is VideoLayer || camada is AudioLayer;

    PintorDaLinha pintor(MidiaDoClipe? midia) => PintorDaLinha(
      estado: e,
      inicioUs: inicioUs,
      fimUs: camada.endTime.inMicroseconds,
      nome: camada.name,
      estiloDoNome: estiloDoNome,
      cor: cor,
      cores: cores,
      oculta: meta.oculta,
      travada: travada,
      escolhida: escolhida,
      naMulti: naMulti && lote,
      comAlcas: comAlcas,
      losangos: losangos,
      midia: midia,
    );

    Widget cabecalho() => CabecalhoDaCamada(
      layerId: _id,
      corDaFaixa: meta.etiqueta ?? cor,
      icone: layerTypeIcon(camada),
      escolhida: escolhida || naMulti,
      oculta: meta.oculta,
      temAnimacao: temAnimacao,
      expandida: expandida,
      miniatura: camada is VideoLayer
          ? MediaPreviewService.instance.stripOf(camada.sourcePath)?.firstOrNull
          : null,
      aoTocar: () => _tocarNoCabecalho(temAnimacao),
      aoAlternarOlho: () =>
          ref.read(editorControllerProvider.notifier).toggleHidden(_id),
      aoComecarReordenar: _comecarReordenar,
      aoMoverReordenar: _moverReordenar,
      aoTerminarReordenar: _terminarReordenar,
    );

    return SizedBox(
      height: AureaDims.linhaDeCamada,
      child: Stack(
        children: [
          Positioned.fill(
            key: const ValueKey('pintura'),
            child: RepaintBoundary(
              child: ehMidia
                  ? ValueListenableBuilder<int>(
                      valueListenable: MediaPreviewService.instance.revision,
                      builder: (context, _, _) =>
                          CustomPaint(painter: pintor(_midia(camada))),
                    )
                  : CustomPaint(painter: pintor(null)),
            ),
          ),
          // AS PECAS TEM CHAVE: uma peca que aparece ou some (a alca, o
          // veu do reordenar) nao pode deslocar as outras na pilha — a
          // peca deslocada seria remontada, e com ela morreria o
          // reconhecedor do dedo que a esta arrastando.
          if (_reordenando)
            Positioned.fill(
              key: const ValueKey('veu'),
              child: IgnorePointer(
                child: ColoredBox(
                  color: AureaCores.destaque.withValues(alpha: .14),
                ),
              ),
            ),
          // O CORPO DO CLIPE: escolhe, abre o menu, e (escolhido) move.
          Positioned.fill(
            key: const ValueKey('corpo'),
            child: AreaDeToqueCalculada(
              acerta: (p) => GeometriaDaLinha.noCorpo(e, camada, p),
              child: GestureDetector(
                key: ValueKey('clipe-$_id'),
                behavior: HitTestBehavior.opaque,
                dragStartBehavior: DragStartBehavior.down,
                onTap: _tocarNoClipe,
                // TOQUE DUPLO NUM GRUPO entra nele. So em grupos: o toque
                // duplo atrasa o toque simples.
                onDoubleTap: camada is GroupLayer ? _entrarNoGrupo : null,
                onLongPressStart: _menu,
                onHorizontalDragStart: podeEditar
                    ? (d) => _comecarArrasto(_Arrasto.mover, d)
                    : null,
                onHorizontalDragUpdate: podeEditar ? _seguirArrasto : null,
                onHorizontalDragEnd: podeEditar
                    ? (_) => _terminarArrasto()
                    : null,
                onHorizontalDragCancel: podeEditar ? _terminarArrasto : null,
                child: const SizedBox.expand(),
              ),
            ),
          ),
          // AS ALCAS DE TRIM: 30 de toque FORA de cada ponta.
          if (comAlcas)
            Positioned.fill(
              key: const ValueKey('alcas'),
              child: AreaDeToqueCalculada(
                acerta: (p) => GeometriaDaLinha.naAlca(e, camada, p) != null,
                child: GestureDetector(
                  key: ValueKey('alcas-$_id'),
                  behavior: HitTestBehavior.opaque,
                  dragStartBehavior: DragStartBehavior.down,
                  onHorizontalDragStart: _comecarTrim,
                  onHorizontalDragUpdate: _seguirArrasto,
                  onHorizontalDragEnd: (_) => _terminarArrasto(),
                  onHorizontalDragCancel: _terminarArrasto,
                  child: const SizedBox.expand(),
                ),
              ),
            ),
          // OS LOSANGOS: na metade de baixo da linha (a de cima e do clipe,
          // senao um clipe cheio de marcas nao se deixaria arrastar).
          if (losangos != null && tempos.isNotEmpty)
            Positioned.fill(
              key: const ValueKey('losangos'),
              child: AreaDeToqueCalculada(
                acerta: (p) =>
                    p.dy >= AureaDims.linhaDeCamada / 2 &&
                    GeometriaDaLinha.losangoEm(e, inicioUs, tempos, p) != null,
                child: GestureDetector(
                  key: ValueKey('losangos-$_id'),
                  behavior: HitTestBehavior.opaque,
                  dragStartBehavior: DragStartBehavior.down,
                  onTapUp: _tocarNoLosango,
                  onLongPressStart: _segurarLosango,
                  onHorizontalDragStart: _comecarLosango,
                  onHorizontalDragUpdate: _seguirLosango,
                  onHorizontalDragEnd: (_) => _terminarLosango(),
                  onHorizontalDragCancel: _terminarLosango,
                  child: const SizedBox.expand(),
                ),
              ),
            ),
          if (escolhida && !travada)
            Positioned(
              key: const ValueKey('alca-reordenar'),
              right: 0,
              top: 0,
              bottom: 0,
              width: AureaDims.alcaDeReordenar,
              child: AlcaDeReordenar(
                layerId: _id,
                aoComecar: _comecarReordenar,
                aoMover: _moverReordenar,
                aoTerminar: _terminarReordenar,
              ),
            ),
          Positioned(
            key: const ValueKey('cabecalho'),
            left: 0,
            top: 0,
            bottom: 0,
            width: AureaDims.cabecalhoDaCamada,
            child: camada is VideoLayer
                ? ValueListenableBuilder<int>(
                    valueListenable: MediaPreviewService.instance.revision,
                    builder: (context, _, _) => cabecalho(),
                  )
                : cabecalho(),
          ),
        ],
      ),
    );
  }
}
