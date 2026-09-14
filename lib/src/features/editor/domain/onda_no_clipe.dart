import 'dart:typed_data';

import 'cut_ops.dart';
import 'layer.dart';

/// EM QUE INSTANTE DO ARQUIVO cada ponto da barra do clipe esta.
///
/// Devolve n+1 instantes (segundos, absolutos no arquivo), uniformes ao
/// longo da duracao do clipe na timeline. E o que faz a onda acompanhar
/// o que TOCA: corte e divisao (sourceOffset), velocidade, reverso (a
/// onda sai espelhada) e Time Remap (a onda segue a curva) — o mesmo
/// mapeamento da previa e da exportacao, no nucleo C++.
///
/// Sem remap nem reverso a relacao e linear e dois pontos bastam; com
/// curva, [amostras] pontos.
Float64List fonteAoLongoDoClipe(Layer layer, {int amostras = 256}) {
  final dur = layer.duration.inMicroseconds;
  if (layer is VideoLayer) {
    final curva = hasTimeRemap(layer) || layer.reverse;
    final n = curva ? amostras.clamp(2, 4096) : 1;
    final out = Float64List(n + 1);
    for (var i = 0; i <= n; i++) {
      final local = Duration(microseconds: (dur * i / n).round());
      out[i] = videoAbsoluteSourceTimeAt(layer, local).inMicroseconds / 1e6;
    }
    return out;
  }
  if (layer is AudioLayer) {
    final ini = layer.sourceOffset.inMicroseconds / 1e6;
    return Float64List.fromList([ini, ini + layer.sourceSpan.inMicroseconds / 1e6]);
  }
  return Float64List(0);
}
