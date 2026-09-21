import 'package:flutter/scheduler.dart';

import '../../../application/editor_controller.dart';
import '../../../application/interacao.dart';

/// UM GESTO DE EDICAO NA TIMELINE: mover, aparar, arrastar losango.
///
/// Duas regras que a timeline antiga aprendeu apanhando:
///
/// UM GESTO, UM DESFAZER. O passo de desfazer abre na PRIMEIRA mutacao de
/// verdade ([abrir], chamado por quem muta) e fecha ao soltar. Segurar e
/// soltar sem mover nao deixa passo vazio; um dedo parado meio segundo no
/// meio do arrasto nao parte o gesto em dois.
///
/// UMA MUTACAO POR QUADRO. O aparelho entrega toque a 120 ou 240 Hz numa
/// tela de 60: sem isto, dois a quatro eventos por quadro pagariam cada um
/// o ima, a mutacao do projeto e a gravacao adiada — para desenhar UM
/// quadro. Mover e aparar calculam o alvo ABSOLUTO a partir da origem do
/// gesto, entao descartar os pedidos intermediarios nao perde nada.
class SessaoDeGesto {
  SessaoDeGesto(this.controlador);

  /// Guardado no comeco: o fim tambem chega pelo `dispose` de uma linha
  /// que saiu da arvore no meio do gesto, e ali o `ref` ja nao responde.
  final EditorController controlador;

  bool _aberto = false;
  bool _viva = true;
  void Function()? _pendente;
  bool _quadroPedido = false;

  bool get viva => _viva;

  /// Abre o passo de desfazer (uma vez). Chamar logo ANTES de mutar.
  void abrir() {
    if (_aberto || !_viva) return;
    controlador.beginGesture();
    _aberto = true;
  }

  /// Pede [mutacao] para o proximo quadro; um pedido novo substitui o
  /// anterior ainda nao aplicado.
  void pedir(void Function() mutacao) {
    if (!_viva) return;
    // O DEDO ESTA MEXENDO: o palco pode baixar a qualidade enquanto dura.
    Interacao.marcar();
    _pendente = mutacao;
    if (_quadroPedido) return;
    _quadroPedido = true;
    SchedulerBinding.instance.scheduleFrameCallback((_) {
      _quadroPedido = false;
      aplicar();
    });
  }

  /// Aplica ja o que estiver pendente.
  void aplicar() {
    final f = _pendente;
    _pendente = null;
    if (f == null || !_viva) return;
    f();
  }

  /// Soltou o dedo: o ultimo pedido sai (e onde o item parou), o passo de
  /// desfazer fecha e o palco volta a qualidade cheia.
  void encerrar() {
    if (!_viva) return;
    aplicar();
    _viva = false;
    if (_aberto) controlador.endGesture();
    _aberto = false;
    Interacao.soltar();
  }

  /// A linha morreu no meio do gesto: nada mais muta (mexer no projeto
  /// durante a desmontagem mexe na arvore que esta sendo desfeita), mas o
  /// passo de desfazer fecha — um gesto perdido nao pode desligar o
  /// desfazer pelo resto da sessao.
  void descartar() {
    if (!_viva) return;
    _pendente = null;
    _viva = false;
    if (_aberto) controlador.endGesture();
    _aberto = false;
    Interacao.soltar();
  }
}
