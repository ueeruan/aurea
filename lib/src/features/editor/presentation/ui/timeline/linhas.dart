import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../domain/video_project.dart';
import 'keyframes_da_timeline.dart';

/// AS CAMADAS ABERTAS: cada uma mostra, abaixo dela, uma linha por
/// propriedade animada. Estado de tela (nao vai para o arquivo).
final camadasExpandidasProvider = StateProvider<Set<String>>(
  (ref) => const <String>{},
);

/// UMA LINHA DA TIMELINE (28): uma camada ou uma propriedade animada dela.
///
/// Compara por VALOR: e a chave da memorizacao das linhas e da estrutura —
/// a lista so se refaz quando uma linha entra, sai ou troca de lugar.
@immutable
class LinhaDaTimeline {
  const LinhaDaTimeline.camada(this.layerId, this.indiceDaCamada)
    : trilha = null;

  const LinhaDaTimeline.trilha(
    this.layerId,
    this.indiceDaCamada,
    ChaveDaTrilha this.trilha,
  );

  final String layerId;

  /// A posicao da camada na lista do projeto (0 = a da frente).
  final int indiceDaCamada;

  /// Nula = a linha da propria camada.
  final ChaveDaTrilha? trilha;

  bool get ehCamada => trilha == null;

  /// A chave do widget (e dos testes).
  String get chave =>
      trilha == null ? 'linha-$layerId' : 'trilha-$layerId-${trilha!.nome}';

  @override
  bool operator ==(Object other) =>
      other is LinhaDaTimeline &&
      other.layerId == layerId &&
      other.indiceDaCamada == indiceDaCamada &&
      other.trilha == trilha;

  @override
  int get hashCode => Object.hash(layerId, indiceDaCamada, trilha);
}

/// A ESTRUTURA: a lista de linhas, comparada por valor. E o UNICO recorte
/// do projeto que a timeline observa — mover um clipe, mudar um numero ou
/// cravar um keyframe numa propriedade que ja tinha linha nao a mudam.
@immutable
class EstruturaDaTimeline {
  const EstruturaDaTimeline(this.linhas);

  factory EstruturaDaTimeline.de(VideoProject p, Set<String> abertas) {
    final linhas = <LinhaDaTimeline>[];
    for (var i = 0; i < p.layers.length; i++) {
      final l = p.layers[i];
      linhas.add(LinhaDaTimeline.camada(l.id, i));
      if (!abertas.contains(l.id)) continue;
      for (final t in trilhasAnimadas(l)) {
        linhas.add(LinhaDaTimeline.trilha(l.id, i, t.chave));
      }
    }
    return EstruturaDaTimeline(linhas);
  }

  final List<LinhaDaTimeline> linhas;

  @override
  bool operator ==(Object other) =>
      other is EstruturaDaTimeline && listEquals(other.linhas, linhas);

  @override
  int get hashCode => Object.hashAll(linhas);
}
