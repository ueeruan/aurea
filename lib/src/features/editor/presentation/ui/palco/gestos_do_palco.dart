import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/gestures.dart';

// =====================================================================
// O MAPA DOS GESTOS DO PALCO (21/09) — ANTES E DEPOIS
// =====================================================================
//
// ANTES (58121bf). Quem pegava o dedo sobre a previa, do mais fundo (que
// entra primeiro na arena) ao mais raso:
//   1. FreehandOverlay (desenho livre): Tap + Pan de tela cheia, so com o
//      pedido de desenho ligado — e ai o palco desligava o proprio gesto.
//   2. MaskNodeEditor: Tap + Pan de tela cheia, so editando mascara/caminho.
//   3. GizmoDaCenaOverlay (objeto da cena 3D, outra frente): Tap + Pan, mas o
//      teste de toque so aceita onde ha alca (`_AreaDoGizmo`).
//   4. As fichas Mover | Girar | Escalar do gizmo da cena: Tap.
//   5. Resolucao da previa (canto de cima), FaixaDeBloqueio: Tap.
//   6. O PALCO (preview_stage): um Listener (toque duplo com DOIS dedos =
//      zoom 1x/2x) e UM GestureDetector(onScale*) com tudo dentro — alca da
//      forma, gizmo da camada 3D, alcas de escala e giro, selecao, arrasto,
//      pinca da camada e pinca de zoom.
//
// AS BRIGAS QUE O DONO SENTIA ("pinco e o objeto move", "seleciono e ele
// anda"):
//   a. A escala era a UNICA na arena e era aceita no dedo que DESCE: o
//      onScaleStart rodava com um dedo so e SELECIONAVA na hora. Quem ia
//      pincar o vazio perdia a selecao antes do segundo dedo chegar, e o
//      primeiro dedo, andando um pixel antes do segundo, ja MOVIA a camada.
//   b. Havendo camada escolhida, pinca EM QUALQUER LUGAR escalava a camada:
//      nao existia zoom da vista enquanto houvesse selecao.
//   c. Soltar um dos dois dedos reconfigurava a escala (onEnd + onStart com
//      um dedo): o dedo que ficava SELECIONAVA de novo o que estivesse
//      embaixo (ou tirava a selecao) e voltava a MOVER a camada.
//   d. Sem folga no arrasto: tocar para escolher com o dedo tremendo dois
//      pixels ja movia o objeto.
//   e. O arrasto nao abria gesto no desfazer: dependia da janela de 450 ms,
//      e um dedo que parava no meio virava dois passos.
//   f. A alca de giro era ABSOLUTA (angulo do dedo + 45): pegar a alca presa
//      na borda do palco dava um salto de rotacao.
//   g. Alcas invisiveis (marcadores 0x0) e a moldura de 4 px DA COMPOSICAO
//      (0,8 px na tela de um celular): a selecao quase nao se via.
//
// DEPOIS. Um reconhecedor so ([ReconhecedorDoPalco]) entra na arena; ele nao
// decide nada, so entrega os dedos a um arbitro ([ArbitroDoPalco]) que
// escolhe UM DONO por gesto e nao troca de dono no meio:
//   - dedo desce: nada muda ainda — so se anota o que esta embaixo dele
//     (alca da forma > gizmo da camada 3D > alcas de escala/giro > a camada
//     SELECIONADA (a vez do arrasto e dela) > a camada de cima > vazio).
//   - solta sem andar = TOQUE: escolhe a camada de cima que esta VISIVEL
//     naquele instante; no vazio, tira a selecao; dois toques no vazio
//     reenquadram a vista. Toque em alca nao faz nada. Toque nunca mexe no
//     relogio.
//   - anda 18 px (4 px numa alca) com UM dedo = ARRASTO do alvo anotado:
//     camada -> posicao (escolhe a camada no COMECO, nunca no meio); alca de
//     canto -> escala; alca de giro -> rotacao; vazio -> passeia a vista.
//   - SEGUNDO DEDO = PINCA, sempre (fecha antes o arrasto de um dedo, se
//     houver): com um dos dedos (ou o meio deles) na camada selecionada,
//     escala + giro da CAMADA — e so isso, a pinca nunca move a camada;
//     fora dela, ou sem selecao, zoom + passeio da VISTA (nunca do projeto).
//   - saiu um dos dedos da pinca da camada: o que sobra nao faz mais nada
//     ate todos subirem. Da pinca da vista, o que sobra continua passeando.
//   - o gizmo da cena 3D (outra frente) continua mais fundo na arvore e com
//     a mesma folga do Pan (36 px): onde ele tem alca, ele ganha a arena.
//   Um gesto = UM passo de desfazer (beginGesture no primeiro passo que
//   muda o projeto, endGesture ao soltar); `Interacao.marcar()` a cada passo
//   e `Interacao.soltar()` ao fim.

/// O QUE ESTAVA EMBAIXO DO DEDO QUANDO ELE DESCEU.
enum TipoDeAlvo {
  vazio,

  /// Uma camada que ainda nao e a escolhida (ou a de cima no ponto).
  camada,

  /// Dentro da caixa da camada JA escolhida: o arrasto e dela, mesmo que
  /// outra esteja por cima; o toque continua escolhendo a de cima.
  selecionada,
  alcaDeEscala,
  alcaDeGiro,
  alcaDaForma,
  gizmoDaCamada,
}

class AlvoNoPalco {
  const AlvoNoPalco(this.tipo, {this.arrasta, this.toca, this.chave});

  static const AlvoNoPalco vazio = AlvoNoPalco(TipoDeAlvo.vazio);

  final TipoDeAlvo tipo;

  /// A camada que um ARRASTO a partir daqui move (ou cuja alca puxa).
  final String? arrasta;

  /// A camada que um TOQUE aqui escolhe (a de cima que esta visivel).
  final String? toca;

  /// A alca da forma (`alcasDaForma`) sob o dedo.
  final String? chave;

  bool get ehAlca =>
      tipo == TipoDeAlvo.alcaDeEscala ||
      tipo == TipoDeAlvo.alcaDeGiro ||
      tipo == TipoDeAlvo.alcaDaForma ||
      tipo == TipoDeAlvo.gizmoDaCamada;

  @override
  String toString() => 'AlvoNoPalco($tipo, arrasta: $arrasta, toca: $toca)';
}

/// QUEM E O DONO DO GESTO EM CURSO.
enum DonoDoGesto {
  nenhum,

  /// Um dedo na tela, ainda dentro da folga: pode virar toque, arrasto ou
  /// pinca. Nada mudou no projeto.
  aDecidir,
  alca,
  moverCamada,

  /// Um dedo passeando a vista (arrasto que comecou no vazio).
  passearVista,
  pincaDaCamada,
  pincaDaVista,

  /// Nada mais acontece ate todos os dedos subirem (arrasto recusado, ou o
  /// dedo que sobrou de uma pinca da camada).
  encerrado,
}

/// O QUE O ARBITRO PEDE A QUEM EDITA. O arbitro decide o dono; quem
/// implementa isto so executa.
abstract interface class DelegadoDoPalco {
  /// O que esta sob [noPalco] agora (so leitura, nada muda).
  AlvoNoPalco alvoEm(Offset noPalco);

  /// Um dos dedos (ou o meio deles) esta sobre a camada escolhida — e ela
  /// aceita ser pincada (nao esta bloqueada nem no modo Selecionar)?
  bool pincaNaSelecao(Offset a, Offset b);

  /// Toque simples (sem andar) sobre [alvo].
  void tocou(AlvoNoPalco alvo, Offset noPalco);

  /// Dois toques seguidos no vazio.
  void tocouDuasVezesNoVazio(Offset noPalco);

  /// O dedo passou da folga: comeca o arrasto de [alvo]. `false` = recusado
  /// (cadeado, modo Selecionar): o gesto fica sem dono ate soltar.
  bool comecarArrasto(AlvoNoPalco alvo, Offset inicio);

  /// Um passo do arrasto de um dedo. [passo] e o quanto o dedo andou desde
  /// o passo anterior (no primeiro, desde onde desceu).
  void arrastar(Offset atual, Offset passo);

  /// Comeca uma pinca: da camada escolhida ou da vista.
  void comecarPinca({required bool daCamada, required Offset focal});

  /// Um passo da pinca. [escala] e [giro] (radianos) sao ACUMULADOS desde o
  /// comeco da pinca; [passo] e o quanto o meio dos dois dedos andou.
  void pincar({
    required double escala,
    required double giro,
    required Offset focal,
    required Offset passo,
  });

  /// Fim do arrasto ou da pinca em curso (fecha o passo de desfazer).
  void terminar();
}

class _Dedo {
  _Dedo(this.inicio) : atual = inicio;

  final Offset inicio;
  Offset atual;
}

/// O ARBITRO: recebe os dedos ja ganhos na arena e escolhe UM dono por
/// gesto. Nao conhece widget nem projeto — e por isso se testa sozinho.
class ArbitroDoPalco {
  ArbitroDoPalco(this.delegado);

  final DelegadoDoPalco delegado;

  /// A FOLGA DO ARRASTO DE CORPO: a do toque do Flutter. Abaixo dela o dedo
  /// esta tocando (escolher), e nao arrastando — e escolher nunca move.
  static const double folgaDoArrasto = kTouchSlop;

  /// NUMA ALCA A INTENCAO JA ESTA DITA: basta vencer o tremor do dedo. Com
  /// a folga do corpo, o primeiro passo da alca sairia atrasado 18 px.
  static const double folgaDaAlca = 4;

  /// Dois toques no vazio a menos disto (e perto um do outro) reenquadram.
  static const Duration janelaDoToqueDuplo = kDoubleTapTimeout;
  static const double raioDoToqueDuplo = kDoubleTapSlop;

  /// Giro abaixo disto numa pinca da camada e tremor, e nao intencao: sem
  /// a zona morta, toda pinca de escala entortava a camada um pouco.
  static const double zonaMortaDoGiro = 4 * math.pi / 180;

  // A ordem de chegada importa: a pinca usa os dois PRIMEIROS dedos.
  final Map<int, _Dedo> _dedos = <int, _Dedo>{};
  DonoDoGesto _dono = DonoDoGesto.nenhum;
  AlvoNoPalco _alvo = AlvoNoPalco.vazio;
  Offset _inicio = Offset.zero;

  // ---- pinca ----
  int? _a;
  int? _b;
  Offset _baseA = Offset.zero;
  Offset _baseB = Offset.zero;
  double _escalaBase = 1;
  double _giroBase = 0;
  double _escala = 1;
  double _giro = 0;
  Offset _focalAnterior = Offset.zero;

  // ---- toque duplo no vazio ----
  Offset? _primeiroToqueNoVazio;
  Timer? _janela;

  DonoDoGesto get dono => _dono;
  int get dedos => _dedos.length;

  void desceu(int id, Offset p) {
    if (_dedos.containsKey(id)) return;
    _dedos[id] = _Dedo(p);
    if (_dedos.length == 1) {
      _dono = DonoDoGesto.aDecidir;
      _inicio = p;
      _alvo = delegado.alvoEm(p);
      return;
    }
    if (_dedos.length == 2) _virarPinca();
    // Terceiro dedo em diante: a pinca continua nos dois primeiros.
  }

  void andou(int id, Offset p) {
    final d = _dedos[id];
    if (d == null) return;
    final passo = p - d.atual;
    d.atual = p;
    switch (_dono) {
      case DonoDoGesto.aDecidir:
        final folga = _alvo.ehAlca ? folgaDaAlca : folgaDoArrasto;
        if ((p - _inicio).distance <= folga) return;
        if (!delegado.comecarArrasto(_alvo, _inicio)) {
          _dono = DonoDoGesto.encerrado;
          return;
        }
        _dono = _alvo.ehAlca
            ? DonoDoGesto.alca
            : _alvo.tipo == TipoDeAlvo.vazio
            ? DonoDoGesto.passearVista
            : DonoDoGesto.moverCamada;
        // O PRIMEIRO PASSO LEVA A FOLGA INTEIRA: o objeto fica debaixo do
        // dedo, e nao 18 px atras dele pelo resto do arrasto.
        delegado.arrastar(p, p - _inicio);
      case DonoDoGesto.alca:
      case DonoDoGesto.moverCamada:
      case DonoDoGesto.passearVista:
        delegado.arrastar(p, passo);
      case DonoDoGesto.pincaDaCamada:
      case DonoDoGesto.pincaDaVista:
        if (id == _a || id == _b) _passoDaPinca();
      case DonoDoGesto.encerrado:
      case DonoDoGesto.nenhum:
        break;
    }
  }

  void subiu(int id, {bool cancelado = false}) {
    final d = _dedos.remove(id);
    if (d == null) return;
    if (_dedos.isEmpty) {
      switch (_dono) {
        case DonoDoGesto.aDecidir:
          if (!cancelado) _toque();
        case DonoDoGesto.alca:
        case DonoDoGesto.moverCamada:
        case DonoDoGesto.passearVista:
        case DonoDoGesto.pincaDaCamada:
        case DonoDoGesto.pincaDaVista:
          delegado.terminar();
        case DonoDoGesto.encerrado:
        case DonoDoGesto.nenhum:
          break;
      }
      _zerar();
      return;
    }
    if (_dono != DonoDoGesto.pincaDaCamada &&
        _dono != DonoDoGesto.pincaDaVista) {
      return;
    }
    if (_dedos.length >= 2) {
      // Saiu um dos dois da pinca e ha outro na tela: o par muda, e a
      // conta recomeca de onde estava (sem salto).
      if (id == _a || id == _b) _novoPar();
      return;
    }
    // SOBROU UM DEDO.
    delegado.terminar();
    if (_dono == DonoDoGesto.pincaDaVista) {
      final resto = _dedos.values.first;
      _dono = delegado.comecarArrasto(AlvoNoPalco.vazio, resto.atual)
          ? DonoDoGesto.passearVista
          : DonoDoGesto.encerrado;
    } else {
      // "A PINCA NUNCA MOVE A CAMADA": o dedo que ficou nao arrasta nada.
      _dono = DonoDoGesto.encerrado;
    }
  }

  /// O widget saiu da arvore no meio: esquece o gesto sem chamar ninguem.
  void descartar() {
    _janela?.cancel();
    _janela = null;
    _primeiroToqueNoVazio = null;
    _zerar();
  }

  // ------------------------------------------------------------------

  void _virarPinca() {
    switch (_dono) {
      case DonoDoGesto.alca:
      case DonoDoGesto.moverCamada:
      case DonoDoGesto.passearVista:
        // O arrasto de um dedo fecha o proprio passo antes da pinca.
        delegado.terminar();
      case DonoDoGesto.pincaDaCamada:
      case DonoDoGesto.pincaDaVista:
        return;
      case DonoDoGesto.aDecidir:
      case DonoDoGesto.encerrado:
      case DonoDoGesto.nenhum:
        break;
    }
    final ids = _dedos.keys.take(2).toList();
    final pa = _dedos[ids[0]]!.atual;
    final pb = _dedos[ids[1]]!.atual;
    final daCamada = delegado.pincaNaSelecao(pa, pb);
    _dono = daCamada ? DonoDoGesto.pincaDaCamada : DonoDoGesto.pincaDaVista;
    _escala = 1;
    _giro = 0;
    _novoPar();
    delegado.comecarPinca(daCamada: daCamada, focal: _focalAnterior);
  }

  void _novoPar() {
    final ids = _dedos.keys.take(2).toList();
    _a = ids[0];
    _b = ids[1];
    _baseA = _dedos[_a]!.atual;
    _baseB = _dedos[_b]!.atual;
    _escalaBase = _escala;
    _giroBase = _giro;
    _focalAnterior = (_baseA + _baseB) / 2;
  }

  void _passoDaPinca() {
    final a = _dedos[_a]?.atual;
    final b = _dedos[_b]?.atual;
    if (a == null || b == null) return;
    final base = _baseB - _baseA;
    final agora = b - a;
    if (base.distance > 1) {
      _escala = _escalaBase * agora.distance / base.distance;
      _giro = _giroBase + _angulo(agora.direction - base.direction);
    }
    final focal = (a + b) / 2;
    final passo = focal - _focalAnterior;
    _focalAnterior = focal;
    var giro = _giro;
    if (_dono == DonoDoGesto.pincaDaCamada) {
      giro = giro.abs() <= zonaMortaDoGiro
          ? 0
          : giro - zonaMortaDoGiro * giro.sign;
    }
    delegado.pincar(escala: _escala, giro: giro, focal: focal, passo: passo);
  }

  /// O angulo no intervalo (-pi, pi]: atravessar o eixo de tras nao pode
  /// virar um giro de 360 graus.
  static double _angulo(double a) {
    var r = a;
    while (r > math.pi) {
      r -= 2 * math.pi;
    }
    while (r <= -math.pi) {
      r += 2 * math.pi;
    }
    return r;
  }

  void _toque() {
    // Tocar numa alca nao escolhe nem tira nada: a alca e da camada que ja
    // esta escolhida, e o dedo mirava ela.
    if (_alvo.ehAlca) return;
    if (_alvo.tipo == TipoDeAlvo.vazio) {
      final primeiro = _primeiroToqueNoVazio;
      if (primeiro != null &&
          (_janela?.isActive ?? false) &&
          (_inicio - primeiro).distance <= raioDoToqueDuplo) {
        _janela?.cancel();
        _janela = null;
        _primeiroToqueNoVazio = null;
        delegado.tocouDuasVezesNoVazio(_inicio);
        return;
      }
      // UM RELOGIO, e nao o carimbo do evento: e o mesmo jeito do
      // reconhecedor de toque duplo do Flutter (e o que o teste controla).
      _primeiroToqueNoVazio = _inicio;
      _janela?.cancel();
      _janela = Timer(janelaDoToqueDuplo, () {
        _janela = null;
        _primeiroToqueNoVazio = null;
      });
    } else {
      _janela?.cancel();
      _janela = null;
      _primeiroToqueNoVazio = null;
    }
    delegado.tocou(_alvo, _inicio);
  }

  void _zerar() {
    _dedos.clear();
    _dono = DonoDoGesto.nenhum;
    _alvo = AlvoNoPalco.vazio;
    _a = null;
    _b = null;
    _escala = 1;
    _giro = 0;
    _escalaBase = 1;
    _giroBase = 0;
  }
}

/// O RECONHECEDOR DO PALCO: o unico do palco na arena.
///
/// Ele NAO DECIDE o que o gesto e — so disputa a arena e, depois de ganha,
/// entrega os dedos ao [arbitro]. Enquanto disputa, nada acontece no
/// projeto: se perder (para o gizmo da cena, o editor de nos, uma ficha),
/// o arbitro nem fica sabendo.
///
/// A DISPUTA COPIA A DO RECONHECEDOR DE ESCALA, que o palco usava ate aqui:
/// sozinho na arena ganha no dedo que desce (e o toque simples funciona);
/// com mais gente, ganha quando o meio dos dedos anda a folga do Pan (36 px)
/// ou a distancia entre dois dedos muda a folga da escala. Com a MESMA
/// folga do Pan, quem esta mais fundo na arvore (o gizmo da cena 3D) recebe
/// o movimento primeiro e ganha onde tem alca — o palco nunca rouba.
class ReconhecedorDoPalco extends OneSequenceGestureRecognizer {
  ReconhecedorDoPalco({super.debugOwner});

  ArbitroDoPalco? arbitro;

  final Map<int, _Dedo> _dedos = <int, _Dedo>{};
  bool _venceu = false;

  @override
  void addAllowedPointer(PointerDownEvent event) {
    super.addAllowedPointer(event);
  }

  @override
  void handleEvent(PointerEvent event) {
    final id = event.pointer;
    if (event is PointerDownEvent) {
      _dedos[id] = _Dedo(event.localPosition);
      if (_venceu) {
        // O PALCO JA E DONO DA SEQUENCIA: o dedo novo tambem e dele, mesmo
        // que tenha caido sobre uma alca de outra camada de toque.
        resolvePointer(id, GestureDisposition.accepted);
        arbitro?.desceu(id, event.localPosition);
      }
    } else if (event is PointerMoveEvent) {
      final d = _dedos[id];
      if (d != null) {
        d.atual = event.localPosition;
        if (_venceu) {
          arbitro?.andou(id, event.localPosition);
        } else if (_passouDaFolga(event.kind)) {
          resolve(GestureDisposition.accepted);
        }
      }
    } else if (event is PointerUpEvent || event is PointerCancelEvent) {
      final d = _dedos.remove(id);
      if (_venceu && d != null) {
        arbitro?.subiu(id, cancelado: event is PointerCancelEvent);
      }
    }
    stopTrackingIfPointerNoLongerDown(event);
  }

  bool _passouDaFolga(PointerDeviceKind kind) {
    if (_dedos.isEmpty) return false;
    var meio = Offset.zero;
    for (final d in _dedos.values) {
      meio += d.atual - d.inicio;
    }
    meio /= _dedos.length.toDouble();
    if (meio.distance > computePanSlop(kind, gestureSettings)) return true;
    if (_dedos.length >= 2) {
      final par = _dedos.values.take(2).toList();
      final antes = (par[1].inicio - par[0].inicio).distance;
      final agora = (par[1].atual - par[0].atual).distance;
      if ((agora - antes).abs() > computeScaleSlop(kind)) return true;
    }
    return false;
  }

  @override
  void acceptGesture(int pointer) {
    if (_venceu) return;
    _venceu = true;
    final a = arbitro;
    if (a == null) return;
    // O ARBITRO RECEBE A HISTORIA INTEIRA: onde cada dedo desceu e onde
    // esta agora. Ganhar a arena depois de 36 px nao pode fazer o objeto
    // perder os 36 px.
    for (final e in _dedos.entries) {
      a.desceu(e.key, e.value.inicio);
    }
    for (final e in _dedos.entries.toList()) {
      if (e.value.atual != e.value.inicio) a.andou(e.key, e.value.atual);
    }
  }

  @override
  void rejectGesture(int pointer) {
    final d = _dedos.remove(pointer);
    if (_venceu && d != null) arbitro?.subiu(pointer, cancelado: true);
    stopTrackingPointer(pointer);
  }

  @override
  void didStopTrackingLastPointer(int pointer) {
    if (!_venceu) resolve(GestureDisposition.rejected);
    _venceu = false;
    _dedos.clear();
  }

  @override
  String get debugDescription => 'palco';
}
