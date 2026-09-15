import 'dart:ui' show BlendMode;

import '../domain/layer.dart';
import '../domain/video_project.dart';
import 'editor_controller.dart';

/// AS OPERACOES DO LOTE: o que a barra de multi-selecao faz com varias
/// camadas de uma vez. Cada uma e UM passo de desfazer.
///
/// Moram fora do controlador porque sao composicoes das operacoes de uma
/// camada so (aparar, dividir, mover, agrupar) — a regra de cada uma
/// continua num lugar so.

/// As camadas do lote na ordem da pilha (a de cima primeiro).
List<Layer> camadasDoLote(VideoProject projeto, Iterable<String> ids) {
  final escolhidas = ids.toSet();
  return [
    for (final l in projeto.layers)
      if (escolhidas.contains(l.id)) l,
  ];
}

/// O cabecote passa por dentro de alguma camada do lote? E o que decide
/// entre aparar/dividir e estender/mover na barra de baixo.
bool cabecoteDentroDoLote(
  VideoProject projeto,
  Iterable<String> ids,
  Duration t,
) => camadasDoLote(projeto, ids).any((l) => l.activeAt(t));

/// AGRUPAR E MASCARAR: a camada de cima vira mascara das de baixo — elas
/// so aparecem onde ela cobre. AGRUPAR E RECORTAR: o contrario, a de
/// cima fura as de baixo. O grupo isola o efeito (so os irmaos sofrem).
void agruparComForma(
  EditorController controller,
  VideoProject projeto,
  Iterable<String> ids, {
  required bool recortar,
}) {
  final camadas = camadasDoLote(projeto, ids);
  if (camadas.length < 2) return;
  controller.runAsOneUndo(() {
    controller.setBlendMode(
      camadas.first.id,
      recortar ? BlendMode.dstOut : BlendMode.dstIn,
    );
    controller.groupLayers([for (final l in camadas) l.id]);
  });
}

/// Apara o comeco das camadas do lote que o cabecote atravessa.
void aparaInicioDoLote(
  EditorController controller,
  VideoProject projeto,
  Iterable<String> ids,
  Duration t,
) {
  final alvo = [
    for (final l in camadasDoLote(projeto, ids))
      if (l.activeAt(t)) l.id,
  ];
  if (alvo.isEmpty) return;
  controller.runAsOneUndo(() {
    for (final id in alvo) {
      controller.trimLayerStart(id, t);
    }
  });
}

/// Apara o fim das camadas do lote que o cabecote atravessa.
void aparaFimDoLote(
  EditorController controller,
  VideoProject projeto,
  Iterable<String> ids,
  Duration t,
) {
  final alvo = [
    for (final l in camadasDoLote(projeto, ids))
      if (l.activeAt(t)) l.id,
  ];
  if (alvo.isEmpty) return;
  controller.runAsOneUndo(() {
    for (final id in alvo) {
      controller.trimLayerEnd(id, t);
    }
  });
}

/// Divide no cabecote todas as camadas do lote que passam por ele.
void dividirLote(
  EditorController controller,
  VideoProject projeto,
  Iterable<String> ids,
  Duration t,
) {
  final alvo = [
    for (final l in camadasDoLote(projeto, ids))
      if (l.activeAt(t)) l.id,
  ];
  if (alvo.isEmpty) return;
  controller.runAsOneUndo(() {
    for (final id in alvo) {
      controller.splitLayer(id, t);
    }
  });
}

/// ESTENDER ATE O CABECOTE: cada camada cresce na direcao dele — a que
/// acaba antes estica o fim, a que comeca depois estica o comeco. O
/// limite do arquivo de midia continua valendo (aparar nao inventa
/// quadro).
void estenderLoteAteOCabecote(
  EditorController controller,
  VideoProject projeto,
  Iterable<String> ids,
  Duration t,
) {
  final camadas = camadasDoLote(projeto, ids);
  if (camadas.isEmpty) return;
  controller.runAsOneUndo(() {
    for (final l in camadas) {
      if (t >= l.endTime) {
        controller.trimLayerEnd(l.id, t);
      } else if (t < l.startTime) {
        controller.trimLayerStart(l.id, t);
      }
    }
  });
}

/// MOVER ATE O CABECOTE: cada camada desliza ate encostar nele — a de
/// antes pelo fim, a de depois pelo comeco. A duracao nao muda.
void moverLoteAteOCabecote(
  EditorController controller,
  VideoProject projeto,
  Iterable<String> ids,
  Duration t,
) {
  final camadas = camadasDoLote(projeto, ids);
  if (camadas.isEmpty) return;
  controller.runAsOneUndo(() {
    for (final l in camadas) {
      if (t >= l.endTime) {
        controller.moveLayer(l.id, t - l.duration);
      } else if (t < l.startTime) {
        controller.moveLayer(l.id, t);
      }
    }
  });
}

/// Todas comecam junto com a que comeca primeiro.
void alinharIniciosNoTempo(
  EditorController controller,
  VideoProject projeto,
  Iterable<String> ids,
) {
  final camadas = camadasDoLote(projeto, ids);
  if (camadas.length < 2) return;
  final inicio = camadas
      .map((l) => l.startTime)
      .reduce((a, b) => a < b ? a : b);
  controller.runAsOneUndo(() {
    for (final l in camadas) {
      if (l.startTime != inicio) controller.moveLayer(l.id, inicio);
    }
  });
}

/// Todas acabam junto com a que acaba por ultimo.
void alinharFinsNoTempo(
  EditorController controller,
  VideoProject projeto,
  Iterable<String> ids,
) {
  final camadas = camadasDoLote(projeto, ids);
  if (camadas.length < 2) return;
  final fim = camadas.map((l) => l.endTime).reduce((a, b) => a > b ? a : b);
  controller.runAsOneUndo(() {
    for (final l in camadas) {
      final inicio = fim - l.duration;
      if (l.startTime != inicio) controller.moveLayer(l.id, inicio);
    }
  });
}

/// DISTRIBUIR NA TIMELINE: uma depois da outra, sem vao, na ordem em que
/// ja comecavam, a partir do comeco da primeira.
void distribuirNoTempo(
  EditorController controller,
  VideoProject projeto,
  Iterable<String> ids,
) {
  final camadas = camadasDoLote(projeto, ids)
    ..sort((a, b) => a.startTime.compareTo(b.startTime));
  if (camadas.length < 2) return;
  var cursor = camadas.first.startTime;
  controller.runAsOneUndo(() {
    for (final l in camadas) {
      if (l.startTime != cursor) controller.moveLayer(l.id, cursor);
      cursor += l.duration;
    }
  });
}
