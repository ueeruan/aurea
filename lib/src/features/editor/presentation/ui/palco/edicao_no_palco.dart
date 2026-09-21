import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../application/editor_controller.dart';
import '../../../application/interacao.dart';
import '../../../application/playback_controller.dart';
import '../../../application/ui/editor_session.dart';
import '../../../application/ui/opcoes_de_visualizacao.dart';
import '../../../domain/layer.dart';
import '../../../domain/mask.dart';
import '../../../domain/selection_geometry.dart';
import '../../../domain/shape.dart';
import '../../../domain/video_project.dart';
import 'alcas_do_palco.dart';
import 'gestos_do_palco.dart';

/// O QUE A EDICAO PRECISA DO PALCO — e so isto.
///
/// A geometria (onde o quadro caiu e por quanto esta escalado) e medida
/// pelo LayoutBuilder do palco; a vista (zoom e passeio), a linha de apoio
/// e o gizmo da camada 3D moram la. A edicao le e pede por aqui, sem saber
/// como o palco desenha.
abstract interface class PalcoVivo {
  PlaybackController get playback;

  /// Canto de cima e da esquerda do quadro da composicao, no palco.
  Offset get origem;

  /// Composicao -> palco (ja com o zoom da vista).
  double get escala;

  /// A area inteira do palco.
  Size get tamanho;

  /// O zoom da VISTA (1 = ajustado a janela). Nunca e do projeto.
  double get zoom;

  void definirVista({required double zoom, required Offset pan});

  /// A linha de apoio do encaixe (nulo = sem linha naquele eixo).
  void mostrarEncaixe(double? x, double? y);

  /// O aviso do cadeado, com o botao de desbloquear.
  void avisarBloqueio(String camadaId);

  // O GIZMO DA CAMADA 3D (eixos e aneis de uma camada com o 3D ligado)
  // continua morando no palco: aqui so se decide QUANDO ele e o dono.
  bool gizmoDaCamadaEm(Offset noPalco);
  bool pegarGizmoDaCamada(Offset noPalco);
  void arrastarGizmoDaCamada(Offset passo, Offset noPalco);
  void soltarGizmoDaCamada();
}

enum _Modo {
  vista,
  gizmo,
  forma,
  escala,
  giro,
  mover,
  pincaDaCamada,
  pincaDaVista,
}

/// A EDICAO NO PALCO: executa o que o [ArbitroDoPalco] decidiu.
///
/// Um gesto = UM passo de desfazer: o gesto so e aberto no primeiro passo
/// que de fato muda o projeto (tocar e soltar nao deixa passo vazio) e e
/// fechado ao soltar.
class EdicaoNoPalco implements DelegadoDoPalco {
  EdicaoNoPalco(this.ref, this.palco);

  final WidgetRef ref;
  final PalcoVivo palco;

  /// A alca sob o dedo agora (`escala`, `giro`, `forma:<chave>`); o pintor
  /// a desenha com o raio de escolhida.
  final ValueNotifier<String?> alcaAtiva = ValueNotifier<String?>(null);

  /// Folga, em pixels de tela, para pegar alvo pequeno.
  static const double folgaDoToque = 12;

  _Modo? _modo;
  String? _id;
  bool _desfazerAberto = false;
  bool _avisouBloqueio = false;

  // alcas de canto e de giro
  Offset _pivo = Offset.zero;
  Offset _dedoInicial = Offset.zero;
  double _escalaX0 = 1;
  double _escalaY0 = 1;
  double _giro0 = 0;
  double _anguloAnterior = 0;
  double _giroAcumulado = 0;

  // alca da forma
  String? _chaveDaForma;
  Offset _centroDoDesenho = Offset.zero;

  // mover
  Offset _pos0 = Offset.zero;
  Offset _acumulado = Offset.zero;
  double? _encaixeX;
  double? _encaixeY;

  // vista
  Offset _pan = Offset.zero;
  double _zoom0 = 1;
  double _escalaDoPalco0 = 1;
  Offset _origem0 = Offset.zero;
  Offset _focal0 = Offset.zero;

  EditorController get _c => ref.read(editorControllerProvider.notifier);
  Duration get _t => palco.playback.time.value;

  void dispose() => alcaAtiva.dispose();

  // ================================================================ alvo

  Offset _naComposicao(Offset noPalco) =>
      (noPalco - palco.origem) / (palco.escala <= 0 ? 1 : palco.escala);

  /// [comp] cai na caixa de [l]? Com [folga] (em pixels de TELA), o ponto
  /// pode estar ate ai fora dela: alvo pequeno tambem tem de dar para pegar.
  bool _contem(VideoProject p, Layer l, Duration t, Offset comp, double folga) {
    final caixa = _c.layerBoxRect(l, t, scaled: false);
    if (caixa.isEmpty) return false;
    final m = selectionTransform(p, l, t);
    final inversa = Matrix4.tryInvert(m);
    if (inversa == null) return false;
    final local = MatrixUtils.transformPoint(inversa, comp);
    if (caixa.contains(local)) return true;
    if (folga <= 0) return false;
    final perto = Offset(
      local.dx.clamp(caixa.left, caixa.right),
      local.dy.clamp(caixa.top, caixa.bottom),
    );
    return (MatrixUtils.transformPoint(m, perto) - comp).distance *
            palco.escala <=
        folga;
  }

  /// A CAMADA DE CIMA QUE ESTA VISIVEL em [noPalco] AGORA (nula = vazio).
  ///
  /// So entra quem aparece no quadro deste instante: fora do tempo, oculta,
  /// fora do solo, fonte de matte, transparente, som, ajuste e bloqueada
  /// ficam de fora — senao engolem o toque do que se ve. Primeiro quem
  /// CONTEM o ponto; so depois a folga de 12 px (uma borda proxima nao
  /// ganha de quem esta debaixo do dedo).
  String? camadaNoPonto(Offset noPalco) {
    final p = ref.read(editorControllerProvider);
    final t = _t;
    final comp = _naComposicao(noPalco);
    final fontes = <String>{
      for (final l in p.layers)
        if (matteEscondeAFonte(l.matteMode) && l.matteSourceId != null)
          l.matteSourceId!,
    };
    final ordem = depthSortPaintOrder(
      p.layers.reversed.toList(),
      t,
      project: p,
    ).reversed;
    final candidatas = <Layer>[
      for (final l in [
        ...ordem.where((l) => l is! NullLayer),
        ...ordem.whereType<NullLayer>(),
      ])
        if (l is! AudioLayer &&
            l is! AdjustmentLayer &&
            l is! CameraLayer &&
            l.activeAt(t) &&
            !p.isHidden(l.id) &&
            p.rendersInPreview(l.id) &&
            !fontes.contains(l.id) &&
            !p.metaOf(l.id).locked &&
            l.opacity.valueAt(l.localTime(t)) > .01)
          l,
    ];
    for (final l in candidatas) {
      if (_contem(p, l, t, comp, 0)) return l.id;
    }
    for (final l in candidatas) {
      if (_contem(p, l, t, comp, folgaDoToque)) return l.id;
    }
    return null;
  }

  GeometriaDaSelecao? _geometria(VideoProject p, Layer l, Duration t) {
    if (l is AudioLayer || !l.activeAt(t)) return null;
    return geometriaDaSelecao(
      projeto: p,
      camada: l,
      caixa: _c.layerBoxRect(l, t, scaled: false),
      t: t,
      origem: palco.origem,
      escala: palco.escala,
      palco: palco.tamanho,
    );
  }

  bool get _editandoForma =>
      ref.read(editorSessionProvider).panel == EditorPanel.editShape;

  @override
  AlvoNoPalco alvoEm(Offset noPalco) {
    // Um gesto novo comeca aqui: o aviso do cadeado vale de novo.
    _avisouBloqueio = false;
    final p = ref.read(editorControllerProvider);
    final t = _t;
    final toca = camadaNoPonto(noPalco);
    final sel = ref.read(selectedLayerProvider);
    final camada = sel == null ? null : p.layerById(sel);
    if (camada != null && !ref.read(modoSelecionarProvider)) {
      final bloqueada = p.metaOf(camada.id).locked;
      final aberta = !bloqueada && camada.activeAt(t);
      // 1. A ALCA DA FORMA: e o que se esta editando (Editar forma aberto).
      if (aberta && _editandoForma && camada is ShapeLayer) {
        for (final a in alcasDaFormaNaTela(
          projeto: p,
          camada: camada,
          t: t,
          origem: palco.origem,
          escala: palco.escala,
        )) {
          if ((noPalco - a.ponto).distance <= 24) {
            return AlvoNoPalco(
              TipoDeAlvo.alcaDaForma,
              arrasta: camada.id,
              chave: a.chave,
            );
          }
        }
      }
      // 2. O GIZMO DA CAMADA 3D vem antes das alcas 2D: ele vive no meio da
      // camada, e num objeto pequeno os dois se encostam — quem mira o eixo
      // nao quer redimensionar. Bloqueada, ele continua desenhado (apagado)
      // e continua sendo o alvo: o arrasto e recusado em silencio, em vez
      // de passear a vista por baixo dele.
      if (palco.gizmoDaCamadaEm(noPalco)) {
        return AlvoNoPalco(TipoDeAlvo.gizmoDaCamada, arrasta: camada.id);
      }
      // 3. AS ALCAS: a mais perto do dedo, dentro do raio.
      final g = aberta ? _geometria(p, camada, t) : null;
      if (g != null) {
        AlvoNoPalco? melhor;
        var dist = double.infinity;
        final dGiro = (noPalco - g.giro).distance;
        if (dGiro <= raioDeToqueDoGiro) {
          melhor = AlvoNoPalco(TipoDeAlvo.alcaDeGiro, arrasta: camada.id);
          dist = dGiro;
        }
        for (final canto in g.cantosDeEscala) {
          final d = (noPalco - canto).distance;
          if (d <= raioDeToqueDaAlca && d < dist) {
            melhor = AlvoNoPalco(TipoDeAlvo.alcaDeEscala, arrasta: camada.id);
            dist = d;
          }
        }
        if (melhor != null) return melhor;
      }
      // 4. DENTRO DA ESCOLHIDA, O ARRASTO E DELA (mesmo bloqueada: ai o
      // arrasto avisa do cadeado em vez de passear a vista). O toque
      // continua escolhendo a de cima.
      if (camada.activeAt(t) &&
          _contem(p, camada, t, _naComposicao(noPalco), 0)) {
        return AlvoNoPalco(
          TipoDeAlvo.selecionada,
          arrasta: camada.id,
          toca: toca ?? camada.id,
        );
      }
    }
    if (toca != null) {
      return AlvoNoPalco(TipoDeAlvo.camada, arrasta: toca, toca: toca);
    }
    return AlvoNoPalco.vazio;
  }

  @override
  bool pincaNaSelecao(Offset a, Offset b) {
    if (ref.read(modoSelecionarProvider)) return false;
    final sel = ref.read(selectedLayerProvider);
    if (sel == null) return false;
    final p = ref.read(editorControllerProvider);
    final l = p.layerById(sel);
    final t = _t;
    if (l == null || l is AudioLayer || !l.activeAt(t)) return false;
    if (p.metaOf(sel).locked) return false;
    return [
      a,
      b,
      (a + b) / 2,
    ].any((q) => _contem(p, l, t, _naComposicao(q), folgaDoToque));
  }

  // =============================================================== toque

  @override
  void tocou(AlvoNoPalco alvo, Offset noPalco) {
    final id = alvo.toca;
    // MODO SELECIONAR: tocar marca e desmarca; o vazio nao desfaz o lote.
    if (ref.read(modoSelecionarProvider)) {
      if (id == null) return;
      final r = alternarNaSelecao(
        ref.read(multiSelectProvider),
        ref.read(selectedLayerProvider),
        id,
      );
      ref.read(multiSelectProvider.notifier).state = r.multi;
      ref.read(selectedLayerProvider.notifier).state = r.principal;
      return;
    }
    // O TOQUE ESCOLHE E MAIS NADA: nao pausa, nao busca, nao mexe no
    // relogio. Quem escolhe uma camada no palco nao pediu para o
    // cabecote pular.
    if (id == ref.read(selectedLayerProvider) &&
        ref.read(multiSelectProvider).isEmpty) {
      return;
    }
    ref.read(multiSelectProvider.notifier).state = const {};
    ref.read(selectedLayerProvider.notifier).state = id;
  }

  @override
  void tocouDuasVezesNoVazio(Offset noPalco) {
    HapticFeedback.selectionClick();
    palco.definirVista(zoom: 1, pan: Offset.zero);
  }

  // ============================================================= arrasto

  @override
  bool comecarArrasto(AlvoNoPalco alvo, Offset inicio) {
    _zerarModo();
    if (alvo.tipo == TipoDeAlvo.vazio) {
      _modo = _Modo.vista;
      _pan = _panEfetivo(palco.escala);
      return true;
    }
    if (ref.read(modoSelecionarProvider)) return false;
    final id = alvo.arrasta;
    if (id == null) return false;
    final p = ref.read(editorControllerProvider);
    final camada = p.layerById(id);
    if (camada == null) return false;
    // O CADEADO diz o que houve, uma vez por gesto: sem o aviso, o dedo
    // ficaria arrastando o nada e a pessoa concluiria que o app travou.
    // O gizmo apagado ja diz que esta fechado (e a faixa do cadeado esta no
    // alto do palco): ali a recusa e muda.
    if (p.metaOf(id).locked) {
      if (!_avisouBloqueio && alvo.tipo != TipoDeAlvo.gizmoDaCamada) {
        _avisouBloqueio = true;
        palco.avisarBloqueio(id);
      }
      return false;
    }
    // EDITAR TOCANDO e editar um alvo que foge: o arrasto para o relogio
    // onde o dedo esta (o toque de escolher, nao — ele nunca mexe no tempo).
    if (palco.playback.playing.value) palco.playback.pause();
    // ARRASTAR UMA CAMADA QUE NAO E A ESCOLHIDA escolhe ela NO COMECO — e
    // so no comeco: passar por cima de outra no meio nao troca nada.
    if (alvo.tipo == TipoDeAlvo.camada &&
        (ref.read(selectedLayerProvider) != id ||
            ref.read(multiSelectProvider).isNotEmpty)) {
      ref.read(multiSelectProvider.notifier).state = const {};
      ref.read(selectedLayerProvider.notifier).state = id;
    }
    final t = _t;
    final local = camada.localTime(t);
    _id = id;
    switch (alvo.tipo) {
      case TipoDeAlvo.gizmoDaCamada:
        if (!palco.pegarGizmoDaCamada(inicio)) return false;
        _modo = _Modo.gizmo;
      case TipoDeAlvo.alcaDaForma:
        if (camada is! ShapeLayer || alvo.chave == null) return false;
        _modo = _Modo.forma;
        _chaveDaForma = alvo.chave;
        _centroDoDesenho = shapeBounds(evaluateShape(camada.contents, local))
            .center;
        alcaAtiva.value = 'forma:${alvo.chave}';
      case TipoDeAlvo.alcaDeEscala:
      case TipoDeAlvo.alcaDeGiro:
        final g = _geometria(p, camada, t);
        if (g == null) return false;
        _modo = alvo.tipo == TipoDeAlvo.alcaDeGiro ? _Modo.giro : _Modo.escala;
        _pivo = g.pivo;
        _dedoInicial = inicio;
        _escalaX0 = camada.scaleX.valueAt(local);
        _escalaY0 = camada.scaleY.valueAt(local);
        _giro0 = camada.rotation.valueAt(local);
        _anguloAnterior = (inicio - _pivo).direction;
        _giroAcumulado = 0;
        alcaAtiva.value = _modo == _Modo.giro ? 'giro' : 'escala';
      case TipoDeAlvo.camada:
      case TipoDeAlvo.selecionada:
        _modo = _Modo.mover;
        _pos0 = camada.position.valueAt(local);
        _acumulado = Offset.zero;
      case TipoDeAlvo.vazio:
        break;
    }
    Interacao.marcar();
    return true;
  }

  @override
  void arrastar(Offset atual, Offset passo) {
    final modo = _modo;
    if (modo == null) return;
    // No ajustado (zoom 1) nao ha o que passear: nada muda, e a previa nao
    // tem por que cair para o rascunho.
    if (modo == _Modo.vista && palco.zoom == 1.0) return;
    Interacao.marcar();
    switch (modo) {
      case _Modo.vista:
        _pan = _prender(_pan + passo, palco.escala);
        palco.definirVista(zoom: palco.zoom, pan: _pan);
      case _Modo.gizmo:
        palco.arrastarGizmoDaCamada(passo, atual);
      case _Modo.forma:
        _arrastarAlcaDaForma(atual);
      case _Modo.escala:
        final antes = math.max(8.0, (_dedoInicial - _pivo).distance);
        _aplicarEscala((atual - _pivo).distance / antes);
      case _Modo.giro:
        // O GIRO E RELATIVO: o quanto o dedo varreu em volta do pivo desde
        // que pegou a alca. O angulo absoluto do dedo (+45) fazia a camada
        // saltar quando a alca estava presa na borda do palco.
        final ang = (atual - _pivo).direction;
        var d = ang - _anguloAnterior;
        while (d > math.pi) {
          d -= 2 * math.pi;
        }
        while (d <= -math.pi) {
          d += 2 * math.pi;
        }
        _anguloAnterior = ang;
        _giroAcumulado += d;
        final id = _id;
        if (id == null) return;
        _abrirDesfazer();
        _c.editRotation(id, _t, _giro0 + _giroAcumulado * 180 / math.pi);
        _informar(id);
      case _Modo.mover:
        _mover(passo);
      case _Modo.pincaDaCamada:
      case _Modo.pincaDaVista:
        break;
    }
  }

  // =============================================================== pinca

  @override
  void comecarPinca({required bool daCamada, required Offset focal}) {
    _zerarModo();
    Interacao.marcar();
    if (daCamada) {
      final id = ref.read(selectedLayerProvider);
      final camada = id == null
          ? null
          : ref.read(editorControllerProvider).layerById(id);
      if (camada == null) return;
      if (palco.playback.playing.value) palco.playback.pause();
      final local = camada.localTime(_t);
      _id = camada.id;
      _escalaX0 = camada.scaleX.valueAt(local);
      _escalaY0 = camada.scaleY.valueAt(local);
      _giro0 = camada.rotation.valueAt(local);
      _modo = _Modo.pincaDaCamada;
      return;
    }
    _modo = _Modo.pincaDaVista;
    _zoom0 = palco.zoom <= 0 ? 1 : palco.zoom;
    _escalaDoPalco0 = palco.escala <= 0 ? 1 : palco.escala;
    _origem0 = palco.origem;
    _focal0 = focal;
  }

  @override
  void pincar({
    required double escala,
    required double giro,
    required Offset focal,
    required Offset passo,
  }) {
    final modo = _modo;
    if (modo == null) return;
    Interacao.marcar();
    if (modo == _Modo.pincaDaCamada) {
      final id = _id;
      if (id == null) return;
      // ESCALA E GIRO, E SO. A pinca nunca escreve a posicao: o meio dos
      // dois dedos andando nao move a camada.
      _aplicarEscala(escala);
      _c.editRotation(id, _t, _giro0 + giro * 180 / math.pi);
      _informar(id);
      return;
    }
    if (modo != _Modo.pincaDaVista) return;
    // O ZOOM DA VISTA ANCORADO NOS DEDOS: o ponto da composicao que estava
    // sob o meio dos dedos quando a pinca comecou continua sob eles.
    var z = (_zoom0 * escala).clamp(.25, 4.0);
    if ((z - 1).abs() < .04) z = 1.0;
    final ajuste = _escalaDoPalco0 / _zoom0;
    final s = ajuste * z;
    final ponto = (_focal0 - _origem0) / _escalaDoPalco0;
    final origem = focal - ponto * s;
    _pan = z == 1.0 ? Offset.zero : _prender(origem - _centrado(s), s);
    palco.definirVista(zoom: z, pan: _pan);
  }

  // ================================================================= fim

  @override
  void terminar() {
    if (_modo == _Modo.gizmo) palco.soltarGizmoDaCamada();
    final houve = _modo != null;
    _fecharDesfazer();
    if (_encaixeX != null || _encaixeY != null) {
      _encaixeX = null;
      _encaixeY = null;
      palco.mostrarEncaixe(null, null);
    }
    if (ref.read(infobarProvider) != null) {
      ref.read(infobarProvider.notifier).state = null;
    }
    alcaAtiva.value = null;
    _zerarModo();
    _avisouBloqueio = false;
    // SOLTOU: a qualidade cheia volta ja, sem esperar a folga do sinal.
    if (houve) Interacao.soltar();
  }

  // ============================================================ internos

  void _zerarModo() {
    _modo = null;
    _id = null;
    _chaveDaForma = null;
  }

  void _abrirDesfazer() {
    if (_desfazerAberto) return;
    _c.beginGesture();
    _desfazerAberto = true;
  }

  void _fecharDesfazer() {
    if (!_desfazerAberto) return;
    _c.endGesture();
    _desfazerAberto = false;
  }

  /// A ESCALA PELO FATOR, com a proporcao da camada: escala X e Y
  /// diferentes continuam diferentes, e o espelhado continua espelhado.
  void _aplicarEscala(double fator) {
    final id = _id;
    if (id == null || !fator.isFinite) return;
    double limite(double s) {
      final m = s.abs().clamp(.05, 8.0);
      return s < 0 ? -m : m;
    }

    _abrirDesfazer();
    final t = _t;
    if (_escalaX0 == _escalaY0) {
      _c.editScaleUniform(id, t, limite(_escalaX0 * fator));
    } else {
      _c.editScaleX(id, t, limite(_escalaX0 * fator));
      _c.editScaleY(id, t, limite(_escalaY0 * fator));
    }
    _informar(id);
  }

  void _arrastarAlcaDaForma(Offset noPalco) {
    final id = _id;
    final chave = _chaveDaForma;
    if (id == null || chave == null) return;
    final p = ref.read(editorControllerProvider);
    final l = p.layerById(id);
    if (l is! ShapeLayer) return;
    final forma = l.contents.whereType<ShapeParametric>().firstOrNull;
    if (forma == null) return;
    final t = _t;
    final inversa = Matrix4.tryInvert(selectionTransform(p, l, t));
    if (inversa == null) return;
    final naCaixa = MatrixUtils.transformPoint(inversa, _naComposicao(noPalco));
    final valores = valoresDaAlcaDaForma(
      forma,
      chave,
      naCaixa + _centroDoDesenho,
      l.localTime(t),
    );
    _abrirDesfazer();
    for (final e in valores.entries) {
      _c.editShapeParam(id, e.key, t, e.value);
    }
    ref.read(infobarProvider.notifier).state = DadosDaInfobar.pares([
      for (final e in valores.entries)
        (
          fichaDoParametroDaForma(e.key, forma.kind).rotulo,
          e.value.toStringAsFixed(0),
        ),
    ]);
  }

  /// MOVER A CAMADA: trava de eixo, encaixe e linha de apoio — a mesma
  /// conta do palco antigo, agora dentro de um gesto so de desfazer.
  void _mover(Offset passo) {
    final id = _id;
    if (id == null) return;
    final escala = palco.escala <= 0 ? 1.0 : palco.escala;
    final passoNaComposicao = passo / escala;
    if (passoNaComposicao == Offset.zero) return;
    _acumulado += passoNaComposicao;
    final p = ref.read(editorControllerProvider);
    final t = _t;
    _abrirDesfazer();

    // ALINHAMENTO: enquanto o dedo anda so num eixo, o outro fica quieto.
    // O criterio e absoluto (12 px), e nao uma proporcao: o eixo se solta
    // assim que a pessoa move de verdade para o outro lado.
    const folgaDoEixo = 12.0;
    var alvo = _pos0 + _acumulado;
    final adx = _acumulado.dx.abs();
    final ady = _acumulado.dy.abs();
    if (adx > 24 && ady < folgaDoEixo) {
      alvo = Offset(alvo.dx, _pos0.dy);
    } else if (ady > 24 && adx < folgaDoEixo) {
      alvo = Offset(_pos0.dx, alvo.dy);
    }

    // ENCAIXE: dez pixels DE TELA. A tolerancia e uma zona morta — dentro
    // dela o objeto fica parado enquanto o dedo anda —, e dez ainda pega o
    // alinhamento sem parecer travado.
    final snap = 10 / escala;
    // QUEM PASSA CORRENDO NAO ESTA MIRANDO: um passo maior que a propria
    // tolerancia atravessa a zona inteira, e o encaixe nao agarra.
    if (passoNaComposicao.distance > snap) {
      _c.editPosition(id, t, alvo);
      _mostrarEncaixe(null, null);
      _informar(id);
      return;
    }
    final camada = p.layerById(id);
    if (camada == null) return;
    final size = _c.layerBoxSize(camada, t);
    final half = Offset(size.width / 2, size.height / 2);
    final xs = <double>[
      p.outputWidth / 2,
      half.dx,
      p.outputWidth - half.dx,
      ...p.guides.vertical,
      ...p.guides.vertical.map((g) => g + half.dx),
      ...p.guides.vertical.map((g) => g - half.dx),
    ];
    final ys = <double>[
      p.outputHeight / 2,
      half.dy,
      p.outputHeight - half.dy,
      ...p.guides.horizontal,
      ...p.guides.horizontal.map((g) => g + half.dy),
      ...p.guides.horizontal.map((g) => g - half.dy),
    ];
    for (final outra in p.layers) {
      if (outra.id == id || !outra.activeAt(t)) continue;
      final oc = outra.position.valueAt(outra.localTime(t));
      final os = _c.layerBoxSize(outra, t);
      final oh = Offset(os.width / 2, os.height / 2);
      xs
        ..add(oc.dx)
        ..add(oc.dx - oh.dx + half.dx)
        ..add(oc.dx + oh.dx - half.dx)
        ..add(oc.dx - oh.dx - half.dx)
        ..add(oc.dx + oh.dx + half.dx);
      ys
        ..add(oc.dy)
        ..add(oc.dy - oh.dy + half.dy)
        ..add(oc.dy + oh.dy - half.dy)
        ..add(oc.dy - oh.dy - half.dy)
        ..add(oc.dy + oh.dy + half.dy);
    }
    double? pegouX;
    double? pegouY;
    for (final x in xs) {
      if ((alvo.dx - x).abs() < snap) {
        alvo = Offset(x, alvo.dy);
        pegouX = x;
        break;
      }
    }
    for (final y in ys) {
      if ((alvo.dy - y).abs() < snap) {
        alvo = Offset(alvo.dx, y);
        pegouY = y;
        break;
      }
    }
    _mostrarEncaixe(pegouX, pegouY);
    _c.editPosition(id, t, alvo);
    _informar(id);
  }

  /// A LINHA APARECE COM O ENCAIXE E SOME COM ELE — e o dedo sente o
  /// alinhamento: um toque leve cada vez que um eixo ENCAIXA num alvo novo.
  void _mostrarEncaixe(double? x, double? y) {
    if (x == _encaixeX && y == _encaixeY) return;
    final novo = (x != null && x != _encaixeX) || (y != null && y != _encaixeY);
    _encaixeX = x;
    _encaixeY = y;
    palco.mostrarEncaixe(x, y);
    if (novo) HapticFeedback.lightImpact();
  }

  /// O NUMERO QUE O DEDO ESTA MUDANDO, na barra de informacoes.
  void _informar(String id) {
    final camada = ref.read(editorControllerProvider).layerById(id);
    if (camada == null) return;
    final local = camada.localTime(_t);
    final pos = camada.position.valueAt(local);
    final escala = camada.scaleX.valueAt(local);
    final giro = camada.rotation.valueAt(local);
    ref.read(infobarProvider.notifier).state = DadosDaInfobar.pares([
      ('X', pos.dx.toStringAsFixed(0)),
      ('Y', pos.dy.toStringAsFixed(0)),
      ('Escala', '${(escala * 100).toStringAsFixed(0)}%'),
      ('Rotação', '${giro.toStringAsFixed(1)}°'),
    ]);
  }

  // ---- vista ----

  Size get _composicao {
    final p = ref.read(editorControllerProvider);
    return Size(p.outputWidth.toDouble(), p.outputHeight.toDouble());
  }

  /// Onde o quadro ficaria, na escala [s], sem passeio (centrado).
  Offset _centrado(double s) {
    final c = _composicao;
    final a = palco.tamanho;
    return Offset((a.width - c.width * s) / 2, (a.height - c.height * s) / 2);
  }

  Offset _panEfetivo(double s) => palco.origem - _centrado(s);

  /// O PASSEIO NAO DEIXA A COMPOSICAO FUGIR DA JANELA (a mesma folga que o
  /// palco aplica ao desenhar). Prender aqui tambem evita a zona morta de
  /// quem passou do limite e volta.
  Offset _prender(Offset pan, double s) {
    final c = _composicao;
    final a = palco.tamanho;
    final fx = math.max(0.0, (c.width * s - a.width) / 2) + 48;
    final fy = math.max(0.0, (c.height * s - a.height) / 2) + 48;
    return Offset(pan.dx.clamp(-fx, fx), pan.dy.clamp(-fy, fy));
  }
}
