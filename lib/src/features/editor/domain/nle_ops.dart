import 'cut_ops.dart';
import 'layer.dart';

/// OPERACOES DE MONTAGEM — o que separa "mover retangulos" de editar.
///
/// Todas sao funcoes PURAS sobre a lista de camadas: entram camadas,
/// saem camadas. Isso mantem desfazer trivial e deixa cada regra
/// testavel sem interface.
///
/// A ideia que atravessa todas: uma trilha e uma FILA no tempo. Quando
/// se tira algo do meio, o que vem depois anda para tras; quando se
/// enfia algo no meio, o que vem depois anda para frente. Sem isso, o
/// buraco fica na tela e a pessoa arruma na mao.

/// Uma trilha e o conjunto de camadas que dividem a mesma faixa. Aqui a
/// trilha e identificada pela posicao na pilha, entao "mesma trilha" =
/// "mesmo indice de profundidade".
typedef TrackKey = int;

/// Camadas ordenadas por inicio.
List<Layer> _porTempo(Iterable<Layer> layers) =>
    [...layers]..sort((a, b) => a.startTime.compareTo(b.startTime));

/// EXCLUSAO COM ARRASTO: tira a camada e puxa para tras tudo que vinha
/// depois DELA, fechando o buraco.
///
/// E a diferenca entre "apaguei um trecho" e "apaguei um trecho e agora
/// tenho um silencio de tres segundos no meio".
List<Layer> rippleDelete(List<Layer> layers, String id) {
  final alvo = layers.where((l) => l.id == id).firstOrNull;
  if (alvo == null) return layers;
  final vao = alvo.duration;

  return [
    for (final l in layers)
      if (l.id != id)
        if (l.startTime >= alvo.endTime)
          l.copyLayer(startTime: l.startTime - vao)
        else
          l,
  ];
}

/// FECHAR BURACOS: empurra tudo para tras ate encostar no anterior.
///
/// Nao muda a ordem nem a duracao de ninguem — so tira o vazio. E o
/// comando que se usa depois de apagar varios trechos soltos.
List<Layer> closeGaps(List<Layer> layers, {Duration from = Duration.zero}) {
  final ordenadas = _porTempo(layers);
  final movidas = <String, Duration>{};
  var cursor = from;

  for (final l in ordenadas) {
    if (l.endTime <= from) continue;
    final novo = l.startTime < cursor ? l.startTime : cursor;
    movidas[l.id] = novo;
    cursor = novo + l.duration;
  }

  return [
    for (final l in layers)
      movidas.containsKey(l.id) && movidas[l.id] != l.startTime
          ? l.copyLayer(startTime: movidas[l.id])
          : l,
  ];
}

/// INSERIR: abre espaco em [at] do tamanho de [novo] e empurra para
/// frente tudo que comeca dali em diante. O que esta ATRAVESSADO no
/// ponto e dividido.
///
/// E o "insert edit" do NLE: enfiar um plano no meio sem atropelar o
/// que ja estava montado.
({List<Layer> layers, Layer inserted}) insertAt(
  List<Layer> layers,
  Layer novo,
  Duration at,
) {
  final vao = novo.duration;
  final out = <Layer>[];

  for (final l in layers) {
    if (l.startTime >= at) {
      out.add(l.copyLayer(startTime: l.startTime + vao));
      continue;
    }
    if (l.endTime > at) {
      // Atravessa o ponto: a parte de tras vai para depois do inserido.
      final antes = l.copyLayer(duration: at - l.startTime);
      final depois = l.duplicated().copyLayer(
        startTime: at + vao,
        duration: l.endTime - at,
      );
      out
        ..add(antes)
        ..add(depois);
      continue;
    }
    out.add(l);
  }

  final colocado = novo.copyLayer(startTime: at);
  out.add(colocado);
  return (layers: out, inserted: colocado);
}

/// SOBRESCREVER: poe [novo] em [at] apagando o que estava embaixo, sem
/// mexer no que esta fora do trecho.
///
/// A diferenca para inserir: aqui a linha do tempo NAO estica. E o que
/// se usa para trocar um plano por outro mantendo a sincronia do resto.
({List<Layer> layers, Layer inserted}) overwriteAt(
  List<Layer> layers,
  Layer novo,
  Duration at,
) {
  final fim = at + novo.duration;
  final out = <Layer>[];

  for (final l in layers) {
    // Fora do trecho: intacta.
    if (l.endTime <= at || l.startTime >= fim) {
      out.add(l);
      continue;
    }
    // Coberta por inteiro: some.
    if (l.startTime >= at && l.endTime <= fim) continue;

    // Sobra so a ponta da esquerda.
    if (l.startTime < at && l.endTime <= fim) {
      out.add(l.copyLayer(duration: at - l.startTime));
      continue;
    }
    // Sobra so a ponta da direita.
    if (l.startTime >= at && l.endTime > fim) {
      out.add(l.copyLayer(startTime: fim, duration: l.endTime - fim));
      continue;
    }
    // O novo cai no MEIO dela: sobram as duas pontas.
    out.add(l.copyLayer(duration: at - l.startTime));
    out.add(
      l.duplicated().copyLayer(startTime: fim, duration: l.endTime - fim),
    );
  }

  final colocado = novo.copyLayer(startTime: at);
  out.add(colocado);
  return (layers: out, inserted: colocado);
}

/// Avanca o PONTO DE ENTRADA na midia quando um pedaco vira o "depois"
/// de um corte. Sem isso, tirar um trecho do meio faz o pedaco de tras
/// repetir o audio (ou o video) que ja tinha passado.
Layer _avancarFonte(Layer l, Duration quanto) => switch (l) {
  VideoLayer v when hasTimeRemap(v) || v.reverse => () {
    final sliced = sliceVideoTrack(v, quanto, v.duration);
    return v.copyLayer(
      sourceOffset: sliced.sourceOffset,
      speed: 1,
      reverse: false,
      effects: replaceTimeRemap(v, sliced.track),
      clearTransitionIn: true,
    );
  }(),
  VideoLayer v => v.copyLayer(
    sourceOffset:
        v.sourceOffset +
        Duration(microseconds: (quanto.inMicroseconds * v.speed).round()),
    clearTransitionIn: true,
  ),
  AudioLayer a => a.copyLayer(
    sourceOffset:
        a.sourceOffset +
        Duration(microseconds: (quanto.inMicroseconds * a.speed).round()),
  ),
  _ => l,
};

/// UM corte, em UMA camada. Devolve o que sobra: nada, um pedaco ou
/// dois.
///
/// Sai daqui, e nao de dentro do laco, porque levantar e extrair fazem
/// exatamente o mesmo recorte — a unica diferenca e se o que ficou
/// depois anda para tras. Quando estavam duplicados, o pedaco nascido de
/// uma divisao tinha id novo e escapava do filtro de "so estas camadas":
/// o corte acontecia e o arrasto nao.
List<Layer> _cortar1(Layer l, Duration from, Duration to) {
  // Nem encosta no trecho.
  if (l.endTime <= from || l.startTime >= to) return [l];
  // Cabe inteira dentro: some.
  if (l.startTime >= from && l.endTime <= to) return const [];
  // So a ponta de tras foi cortada.
  if (l.startTime < from && l.endTime <= to) {
    final keep = from - l.startTime;
    if (l is VideoLayer && (hasTimeRemap(l) || l.reverse)) {
      final sliced = sliceVideoTrack(l, Duration.zero, keep);
      return [
        l.copyLayer(
          duration: keep,
          sourceOffset: sliced.sourceOffset,
          speed: 1,
          reverse: false,
          effects: replaceTimeRemap(l, sliced.track),
        ),
      ];
    }
    return [l.copyLayer(duration: keep)];
  }
  // So a ponta da frente foi cortada.
  if (l.startTime >= from && l.endTime > to) {
    return [
      _avancarFonte(
        l,
        to - l.startTime,
      ).copyLayer(startTime: to, duration: l.endTime - to),
    ];
  }
  // O trecho esta no meio: parte em dois.
  final firstDuration = from - l.startTime;
  final secondFrom = to - l.startTime;
  Layer first = l.copyLayer(duration: firstDuration);
  if (l is VideoLayer && (hasTimeRemap(l) || l.reverse)) {
    final sliced = sliceVideoTrack(l, Duration.zero, firstDuration);
    first = l.copyLayer(
      duration: firstDuration,
      sourceOffset: sliced.sourceOffset,
      speed: 1,
      reverse: false,
      effects: replaceTimeRemap(l, sliced.track),
    );
  }
  return [
    first,
    _avancarFonte(
      l.duplicated(),
      secondFrom,
    ).copyLayer(startTime: to, duration: l.endTime - to),
  ];
}

/// LEVANTAR (lift): tira o trecho e DEIXA o buraco. O oposto de
/// [rippleDelete] — usado quando a sincronia com outra trilha importa
/// mais do que fechar o vazio.
List<Layer> liftRange(
  List<Layer> layers,
  Duration from,
  Duration to, {
  Set<String>? only,
}) {
  if (to <= from) return layers;
  return [
    for (final l in layers)
      if (only != null && !only.contains(l.id)) l else ..._cortar1(l, from, to),
  ];
}

/// EXTRAIR (extract): tira o trecho E fecha o buraco.
List<Layer> extractRange(
  List<Layer> layers,
  Duration from,
  Duration to, {
  Set<String>? only,
}) {
  if (to <= from) return layers;
  final vao = to - from;
  final out = <Layer>[];
  for (final l in layers) {
    if (only != null && !only.contains(l.id)) {
      out.add(l);
      continue;
    }
    for (final pedaco in _cortar1(l, from, to)) {
      out.add(
        pedaco.startTime >= to
            ? pedaco.copyLayer(startTime: pedaco.startTime - vao)
            : pedaco,
      );
    }
  }
  return out;
}

/// Caminho da midia de uma camada, se ela tiver.
String? mediaPathOf(Layer l) => switch (l) {
  VideoLayer v => v.sourcePath,
  AudioLayer a => a.sourcePath,
  _ => null,
};

Duration _fonteDe(Layer l) => switch (l) {
  VideoLayer v => v.sourceOffset,
  AudioLayer a => a.sourceOffset,
  _ => Duration.zero,
};

double _velocidadeDe(Layer l) => switch (l) {
  VideoLayer v => v.speed,
  AudioLayer a => a.speed,
  _ => 1.0,
};

/// DA PARA JUNTAR [a] e [b] de volta num clipe so?
///
/// Sim quando sao dois pedacos do MESMO arquivo, encostados na linha do
/// tempo E em sequencia no tempo de origem. E exatamente a condicao em
/// que o corte foi feito — juntar desfaz o corte e devolve o clipe
/// original, com o mesmo ponto de entrada.
///
/// Encostados "na medida do quadro": exigir igualdade exata em
/// microssegundos reprovaria juncoes legitimas por causa de
/// arredondamento.
bool canJoin(
  Layer a,
  Layer b, {
  Duration tolerance = const Duration(milliseconds: 40),
}) {
  final pa = mediaPathOf(a);
  final pb = mediaPathOf(b);
  if (pa == null || pb == null || pa != pb) return false;
  if (a.runtimeType != b.runtimeType) return false;
  if ((_velocidadeDe(a) - _velocidadeDe(b)).abs() > 0.001) return false;

  // Encostados na linha.
  final vaoLinha = (b.startTime - a.endTime).inMicroseconds.abs();
  if (vaoLinha > tolerance.inMicroseconds) return false;

  // E em sequencia na FONTE: sem isto, dois trechos distantes do mesmo
  // arquivo "juntariam" e o video pularia no meio.
  final fimFonteA =
      _fonteDe(a) +
      Duration(
        microseconds: (a.duration.inMicroseconds * _velocidadeDe(a)).round(),
      );
  final vaoFonte = (_fonteDe(b) - fimFonteA).inMicroseconds.abs();
  return vaoFonte <= tolerance.inMicroseconds;
}

/// O vizinho da direita com que [id] pode ser juntado, se houver.
Layer? joinableNeighbour(List<Layer> layers, String id) {
  final alvo = layers.where((l) => l.id == id).firstOrNull;
  if (alvo == null) return null;
  for (final l in layers) {
    if (l.id == id) continue;
    if (canJoin(alvo, l)) return l;
  }
  return null;
}

/// JUNTA os dois num clipe so, desfazendo o corte.
///
/// O resultado fica com as propriedades do PRIMEIRO pedaco (posicao,
/// efeitos, mascaras) e a duracao somada — que e o clipe original de
/// volta.
List<Layer> joinAdjacent(List<Layer> layers, String idA, String idB) {
  final a = layers.where((l) => l.id == idA).firstOrNull;
  final b = layers.where((l) => l.id == idB).firstOrNull;
  if (a == null || b == null || !canJoin(a, b)) return layers;

  final juntos = a.copyLayer(duration: b.endTime - a.startTime);
  return [
    for (final l in layers)
      if (l.id != idB)
        if (l.id == idA) juntos else l,
  ];
}

/// Tira VARIOS trechos de uma camada so — e a operacao da decupagem.
///
/// Os trechos vem em tempo da LINHA (nao do arquivo) e sao aplicados do
/// ultimo para o primeiro: assim um corte nunca desloca outro que ainda
/// nao foi feito. Com [ripple], cada corte encosta o que vinha depois;
/// sem, deixa o buraco.
///
/// Cortar uma camada a parte em duas, e a segunda metade tambem pode
/// ser cortada — por isso a "familia" cresce a cada passo: comeca com a
/// camada original e recolhe todo pedaco nascido dos cortes.
List<Layer> removeRangesFrom(
  List<Layer> layers,
  String id,
  List<(Duration, Duration)> ranges, {
  bool ripple = true,
}) {
  if (ranges.isEmpty) return layers;
  final ordenados = [
    for (final r in ranges)
      if (r.$2 > r.$1) r,
  ]..sort((a, b) => a.$1.compareTo(b.$1));
  if (ordenados.isEmpty) return layers;

  var familia = {id};
  var atual = layers;
  for (final (de, ate) in ordenados.reversed) {
    final antes = {for (final l in atual) l.id};
    atual = ripple
        ? extractRange(atual, de, ate, only: familia)
        : liftRange(atual, de, ate, only: familia);
    final vivos = {for (final l in atual) l.id};
    familia = {
      for (final f in familia)
        if (vivos.contains(f)) f,
      for (final l in atual)
        if (!antes.contains(l.id)) l.id,
    };
  }
  return atual;
}

/// O contrario: fica so com [keep] e joga o resto fora.
///
/// A decupagem por silencio pensa assim — "mantenha onde tem fala" — e
/// e mais facil de conferir de cabeca do que a lista de buracos.
List<Layer> keepRangesOf(
  List<Layer> layers,
  String id,
  List<(Duration, Duration)> keep, {
  bool ripple = true,
}) {
  final alvo = layers.where((l) => l.id == id).firstOrNull;
  if (alvo == null) return layers;
  final dentro = [
    for (final r in keep)
      if (r.$2 > alvo.startTime && r.$1 < alvo.endTime)
        (
          r.$1 < alvo.startTime ? alvo.startTime : r.$1,
          r.$2 > alvo.endTime ? alvo.endTime : r.$2,
        ),
  ]..sort((a, b) => a.$1.compareTo(b.$1));
  // Nada a manter: a camada inteira e o trecho a tirar.
  if (dentro.isEmpty) {
    return removeRangesFrom(layers, id, [
      (alvo.startTime, alvo.endTime),
    ], ripple: ripple);
  }

  final fora = <(Duration, Duration)>[];
  var cursor = alvo.startTime;
  for (final r in dentro) {
    if (r.$1 > cursor) fora.add((cursor, r.$1));
    if (r.$2 > cursor) cursor = r.$2;
  }
  if (cursor < alvo.endTime) fora.add((cursor, alvo.endTime));
  return removeRangesFrom(layers, id, fora, ripple: ripple);
}

/// Onde ha VAZIO na linha do tempo, entre [from] e o fim.
///
/// Serve para o comando de fechar buracos avisar quantos existem — e
/// para o teste provar que fechou.
List<(Duration, Duration)> gapsIn(
  List<Layer> layers, {
  Duration from = Duration.zero,
}) {
  final ordenadas = _porTempo(layers.where((l) => l.endTime > from));
  final out = <(Duration, Duration)>[];
  var cursor = from;
  for (final l in ordenadas) {
    if (l.startTime > cursor) out.add((cursor, l.startTime));
    if (l.endTime > cursor) cursor = l.endTime;
  }
  return out;
}
