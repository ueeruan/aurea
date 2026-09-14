/// A COR DE UM VIDEO IMPORTADO, e como le-lo sem trocar a cor.
///
/// O que se decide aqui e a MATRIZ e a FAIXA com que os quadros de um
/// video sao convertidos para RGB na exportacao. O preview usa o player
/// do sistema (ExoPlayer, AVPlayer), que decide isso pelas etiquetas do
/// arquivo — e, quando nao ha etiqueta, pelo tamanho: HD e BT.709, SD e
/// BT.601. A exportacao tem de decidir IGUAL, senao o mesmo video sai
/// com vermelhos e verdes deslocados em relacao ao que se viu.
///
/// Sem esta decisao, o FFmpeg supoe BT.601 para tudo que nao tem
/// etiqueta — e o JPEG intermediario que se usava era lido como 601
/// mesmo quando o conteudo era 709. Era a cor diferente na exportacao.
library;

/// O que o `ffprobe` disse sobre a cor de um fluxo de video.
class CorDoVideo {
  const CorDoVideo({
    this.matriz,
    this.faixa,
    this.transferencia,
    this.primarias,
    required this.largura,
    required this.altura,
  });

  /// Le as propriedades cruas de um fluxo (`color_space`, `color_range`,
  /// `color_transfer`, `color_primaries`, `width`, `height`). Qualquer
  /// campo ausente ou "unknown" vira nulo.
  factory CorDoVideo.deProps(Map<dynamic, dynamic> props) {
    String? etiqueta(String chave) {
      final v = props[chave];
      if (v is! String) return null;
      final s = v.trim().toLowerCase();
      if (s.isEmpty || s == 'unknown' || s == 'unspecified') return null;
      return s;
    }

    int numero(String chave) {
      final v = props[chave];
      if (v is num) return v.toInt();
      if (v is String) return int.tryParse(v) ?? 0;
      return 0;
    }

    return CorDoVideo(
      matriz: etiqueta('color_space'),
      faixa: etiqueta('color_range'),
      transferencia: etiqueta('color_transfer'),
      primarias: etiqueta('color_primaries'),
      largura: numero('width'),
      altura: numero('height'),
    );
  }

  /// Sem informacao nenhuma: so o tamanho decide.
  const CorDoVideo.desconhecida({required this.largura, required this.altura})
    : matriz = null,
      faixa = null,
      transferencia = null,
      primarias = null;

  final String? matriz;
  final String? faixa;
  final String? transferencia;
  final String? primarias;
  final int largura;
  final int altura;

  /// HDR de verdade (PQ ou HLG): precisa de tonemap para virar SDR.
  bool get hdr =>
      transferencia == 'smpte2084' || transferencia == 'arib-std-b67';

  /// HD pelo criterio dos players: 720 linhas ou 1280 colunas.
  bool get hd => largura >= 1280 || altura >= 720;

  /// A matriz como o `setparams` do FFmpeg a chama.
  String get matrizResolvida => matriz ?? (hd ? 'bt709' : 'bt470bg');

  /// A matriz como o `scale` do FFmpeg a chama em `in_color_matrix`.
  String get matrizParaScale => switch (matrizResolvida) {
    'bt709' => 'bt709',
    'bt470bg' || 'smpte170m' || 'bt601' => 'bt601',
    'bt2020nc' || 'bt2020c' || 'bt2020' => 'bt2020',
    'smpte240m' => 'smpte240m',
    'fcc' => 'fcc',
    // Uma matriz que o scale nao conhece: cai no criterio de tamanho.
    _ => hd ? 'bt709' : 'bt601',
  };

  /// A faixa: video sem etiqueta e limitado ("tv"), como todo player supoe.
  String get faixaResolvida => faixa == 'pc' ? 'pc' : 'tv';
}

/// O trecho comum: encaixa na composicao e sai em RGB, que nao tem
/// matriz para adivinhar. E a saida em RGB que faz o PNG intermediario
/// carregar a cor certa — um JPEG carrega YCbCr, e quem o abre supoe a
/// matriz do JFIF (601), esteja o conteudo em 709 ou nao.
String _encaixe(CorDoVideo cor, int largura, int altura, int? areaMaxima) =>
    '${_escala(largura, altura, areaMaxima)}'
    ':in_color_matrix=${cor.matrizParaScale}:in_range=${cor.faixaResolvida}'
    ',format=rgb24';

/// O `scale` da extracao. Normal: encaixa na composicao. PARA A IA
/// ([areaMaxima]): a propria fonte, sem ampliar (quem amplia e a rede),
/// reduzida so se passar da area — conta feita pelo FFmpeg DEPOIS de girar,
/// entao vale para video em pe. Aspas simples protegem as virgulas.
String _escala(int largura, int altura, int? areaMaxima) {
  if (areaMaxima == null) {
    return 'scale=$largura:$altura:force_original_aspect_ratio=decrease';
  }
  final k = 'min(1,sqrt($areaMaxima/(iw*ih)))';
  return "scale=w='max(1,trunc(iw*$k))':h='max(1,trunc(ih*$k))'";
}

/// Filtro para video SDR: a matriz e a faixa certas, e mais nada.
///
/// `setparams` etiqueta os quadros com a decisao tomada (vale para
/// quem nao tinha etiqueta); `in_color_matrix`/`in_range` no `scale`
/// cravam a mesma decisao na unica conversao YUV->RGB que acontece.
String filtroSdr(
  CorDoVideo cor, {
  required int fps,
  required int largura,
  required int altura,
  int? areaMaxima,
}) =>
    'fps=$fps'
    ',setparams=colorspace=${cor.matrizResolvida}:range=${cor.faixaResolvida}'
    ',${_encaixe(cor, largura, altura, areaMaxima)}';

/// Filtro para video HDR (PQ/HLG): lineariza, mapeia para o alcance
/// SDR e sai em BT.709 — a receita canonica do FFmpeg com o zimg.
///
/// Sem isto, um video HDR do iPhone (que grava HDR por padrao) sai
/// acinzentado e sem cor: os valores PQ eram lidos como se fossem
/// gama comum. O resultado nao e identico ao tonemap da Apple, mas e
/// a mesma imagem — e nao a lavada de antes.
String filtroHdrParaSdr(
  CorDoVideo cor, {
  required int fps,
  required int largura,
  required int altura,
  int? areaMaxima,
}) {
  const sdr = CorDoVideo(
    matriz: 'bt709',
    faixa: 'tv',
    largura: 1920,
    altura: 1080,
  );
  return 'fps=$fps'
      ',zscale=t=linear:npl=100'
      ',format=gbrpf32le'
      ',zscale=p=bt709'
      ',tonemap=tonemap=hable:desat=0'
      ',zscale=t=bt709:m=bt709:r=tv'
      ',format=yuv420p'
      ',${_encaixe(sdr, largura, altura, areaMaxima)}';
}

/// Filtro de reserva: o de antes, so que em RGB. Serve se um FFmpeg
/// nao conhecer alguma opcao das receitas acima — um quadro com a cor
/// aproximada vale mais que exportacao nenhuma.
String filtroDeReserva({
  required int fps,
  required int largura,
  required int altura,
  int? areaMaxima,
}) => 'fps=$fps,${_escala(largura, altura, areaMaxima)},format=rgb24';

/// As receitas, na ordem em que se tenta.
///
/// [areaMaximaParaIa]: os quadros vao para o aprimoramento por IA e saem na
/// resolucao da fonte (no maximo esta area), nao na da composicao.
List<String> receitasDeExtracao(
  CorDoVideo cor, {
  required int fps,
  required int largura,
  required int altura,
  int? areaMaximaParaIa,
}) => [
  if (cor.hdr)
    filtroHdrParaSdr(
      cor,
      fps: fps,
      largura: largura,
      altura: altura,
      areaMaxima: areaMaximaParaIa,
    ),
  filtroSdr(
    cor,
    fps: fps,
    largura: largura,
    altura: altura,
    areaMaxima: areaMaximaParaIa,
  ),
  filtroDeReserva(
    fps: fps,
    largura: largura,
    altura: altura,
    areaMaxima: areaMaximaParaIa,
  ),
];

/// A TAXA DE QUADROS de um fluxo pelas propriedades do ffprobe.
///
/// `avg_frame_rate` primeiro: e a media real, a que vale para o video de
/// celular com taxa variavel. Sem ela, `r_frame_rate`. "30000/1001" vira
/// 29,97; "0/0", ausente ou absurdo (acima de 480) vira nulo — e quem
/// chama usa a taxa da composicao.
double? fpsDeProps(Map<dynamic, dynamic> props) {
  double? razao(Object? v) {
    double? r;
    if (v is num) {
      r = v.toDouble();
    } else if (v is String) {
      final partes = v.trim().split('/');
      final n = double.tryParse(partes[0].trim());
      final d = partes.length > 1 ? double.tryParse(partes[1].trim()) : 1.0;
      if (n == null || d == null || d == 0) return null;
      r = n / d;
    }
    if (r == null || !r.isFinite || r < 1 || r > 480) return null;
    return r;
  }

  return razao(props['avg_frame_rate']) ?? razao(props['r_frame_rate']);
}

/// A ROTACAO DE EXIBICAO do fluxo em graus (0, 90, 180 ou 270), pelas
/// propriedades do ffprobe: a matriz de exibicao (`side_data_list`,
/// FFmpeg 5+) ou a etiqueta `rotate` (antes disso). Video de celular em pe
/// costuma vir gravado deitado com -90 aqui.
int rotacaoDeProps(Map<dynamic, dynamic> props) {
  num? graus;
  final lados = props['side_data_list'];
  if (lados is List) {
    for (final lado in lados) {
      if (lado is Map && lado['rotation'] is num) {
        graus = lado['rotation'] as num;
        break;
      }
    }
  }
  if (graus == null) {
    final tags = props['tags'];
    if (tags is Map) {
      final r = tags['rotate'];
      graus = r is num ? r : (r is String ? num.tryParse(r.trim()) : null);
    }
  }
  if (graus == null || !graus.isFinite) return 0;
  final quartos = (graus / 90).round();
  return ((quartos * 90) % 360 + 360) % 360;
}
