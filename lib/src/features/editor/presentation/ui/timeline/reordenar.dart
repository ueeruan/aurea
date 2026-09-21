import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../application/interacao.dart';
import 'estado_da_timeline.dart';
import 'linhas.dart';

/// REORDENAR A PILHA PELA TIMELINE.
///
/// A ORDEM DAS LINHAS E A ORDEM DO PALCO: a linha de cima e a camada da
/// frente (indice 0 da lista do projeto, que o palco pinta por ultimo).
///
/// O arrasto NAO muta o projeto a cada degrau: ele mostra onde a camada vai
/// cair (o traco de destino) e da um toque a cada degrau novo; soltar aplica
/// UMA mudanca — um passo de desfazer, e a linha que o dedo segura nunca
/// troca de lugar no meio do gesto (trocar mataria o reconhecedor dela).
class ControleDeReordenar {
  ControleDeReordenar({
    required this.estado,
    required this.lista,
    required this.chaveDaLista,
    required this.linhas,
    required this.controlador,
  });

  final EstadoDaTimeline estado;
  final ScrollController lista;
  final GlobalKey chaveDaLista;

  /// As linhas de agora (camadas e propriedades expandidas).
  final List<LinhaDaTimeline> Function() linhas;
  final EditorController Function() controlador;

  String? _id;
  int _origem = 0;
  int _destino = 0;
  Offset _ultimoDedo = Offset.zero;
  double? _yInicial;

  bool get ativo => _id != null;

  /// A camada em reordenacao (ou nula).
  String? get id => _id;

  /// Pega a camada [id] com o dedo em [global]. Camada bloqueada nao sai
  /// do lugar (o controlador recusaria em silencio; aqui nem comeca).
  void comecar(String id, Offset global, {required int indice}) {
    if (_id != null) return;
    final c = controlador();
    if (c.isLocked(id)) {
      HapticFeedback.lightImpact();
      return;
    }
    _id = id;
    _origem = indice;
    _destino = indice;
    estado.emReordenacao.value = id;
    HapticFeedback.mediumImpact();
    mover(id, global);
  }

  void mover(String id, Offset global) {
    if (_id != id) return;
    _ultimoDedo = global;
    _atualizar();
  }

  void _atualizar() {
    final box = chaveDaLista.currentContext?.findRenderObject();
    if (box is! RenderBox || !box.hasSize || !lista.hasClients) return;
    Interacao.marcar();
    final local = box.globalToLocal(_ultimoDedo);
    final yInicial = _yInicial ??= local.dy;
    final todas = linhas();
    if (todas.isEmpty) return;
    final conteudo = local.dy + lista.offset;
    final i = (conteudo / AureaDims.linhaDeCamada).floor().clamp(
      0,
      todas.length - 1,
    );
    final destino = todas[i].indiceDaCamada;
    if (destino != _destino) {
      _destino = destino;
      HapticFeedback.selectionClick();
    }
    estado.destinoDoReordenar.value = _destino == _origem
        ? null
        : _yDoTraco(todas, _destino, acima: _destino < _origem);
    estado.autoRolagem.vertical(
      local.dy,
      box.size.height,
      lista,
      _atualizar,
      desde: yInicial,
    );
  }

  /// O traco fica ACIMA da camada de destino quando se sobe, e ABAIXO da
  /// ultima linha dela (as propriedades abertas inclusive) quando se desce.
  double _yDoTraco(
    List<LinhaDaTimeline> todas,
    int destino, {
    required bool acima,
  }) {
    var primeira = -1;
    var ultima = -1;
    for (var i = 0; i < todas.length; i++) {
      if (todas[i].indiceDaCamada != destino) continue;
      if (primeira < 0) primeira = i;
      ultima = i;
    }
    if (primeira < 0) return 0;
    return (acima ? primeira : ultima + 1) * AureaDims.linhaDeCamada;
  }

  /// Soltou: a camada vai para o destino num passo so de desfazer.
  void terminar(String id, {required bool cancelou}) {
    if (_id != id) return;
    final delta = _destino - _origem;
    _limpar();
    if (cancelou || delta == 0) return;
    final c = controlador();
    c.runAsOneUndo(() => c.reorderLayer(id, delta));
    HapticFeedback.lightImpact();
  }

  void _limpar() {
    _id = null;
    _yInicial = null;
    estado.emReordenacao.value = null;
    estado.destinoDoReordenar.value = null;
    estado.autoRolagem.parar();
    Interacao.soltar();
  }

  /// A timeline saiu da tela no meio do gesto.
  void cancelar() {
    if (_id != null) _limpar();
  }
}
