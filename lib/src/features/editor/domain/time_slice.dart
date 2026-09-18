import 'dart:math' as math;
import 'dart:ui';

import 'effect.dart';
import 'fx.dart' show fxHash01;
import 'layer.dart';

/// TIME SLICE: o quadro vira faixas paralelas e cada faixa mostra a mesma
/// camada num instante diferente (S_TimeSlice, Time Displacement, slit
/// scan). Em edits e transicao: as faixas do clipe chegam em sequencia.
///
/// Tudo aqui e funcao pura de (instante, parametros, semente).

/// Distribuicoes dos atrasos entre as faixas.
abstract final class DistribuicaoDoTimeSlice {
  static const escada = 0;
  static const linear = 1;
  static const centro = 2;
  static const aleatoria = 3;
  static const onda = 4;
}

double _curva(int curva, double x) {
  final t = x.clamp(0.0, 1.0);
  return switch (curva) {
    1 => t * t,
    2 => 1 - (1 - t) * (1 - t),
    3 => t < .5 ? 2 * t * t : 1 - math.pow(-2 * t + 2, 2) / 2,
    _ => t,
  };
}

/// O ATRASO DE CADA FAIXA, em quadros inteiros (negativo = passado).
///
/// * escada: -(N-1)/2 .. (N-1)/2 passos, com passo = maximo / ((N-1)/2) —
///   com maximo = (N-1)/2 e o S_TimeSlice (um quadro por faixa);
/// * linear: 0 na primeira faixa ate [maximo] na ultima, pela [curva];
/// * centro: 0 no meio, [maximo] nas bordas;
/// * aleatoria: sorteio por faixa em -maximo..maximo;
/// * onda: seno ao longo das faixas, com [ciclos], [fase] e uma
///   [varredura] em faixas por segundo que faz a onda andar no tempo.
List<int> atrasosDasFaixas({
  required int faixas,
  required int distribuicao,
  required double maximo,
  int curva = 0,
  double ciclos = 1,
  double fase = 0,
  double varredura = 0,
  double segundos = 0,
  int semente = 0,
  double deslocamento = 0,
}) {
  final n = faixas.clamp(1, 256);
  final m = maximo.isFinite ? maximo : 0.0;
  // UMA FAIXA SO NAO SE DESLOCA, em distribuicao nenhuma: ela cobre o
  // quadro inteiro, entao nao ha "outra faixa" para comparar — um quadro
  // inteiro num instante sorteado nao e fatia, e salto de tempo. Quem
  // quer isso usa o Posterize Time, que e o efeito dessa conta.
  if (n == 1) return const [0];
  return [
    for (var k = 0; k < n; k++)
      (() {
        final x = n == 1 ? 0.0 : k / (n - 1);
        final double atraso = switch (distribuicao) {
          DistribuicaoDoTimeSlice.linear => m * _curva(curva, x),
          DistribuicaoDoTimeSlice.centro => m * _curva(curva, (2 * x - 1).abs()),
          DistribuicaoDoTimeSlice.aleatoria =>
            m * (fxHash01(semente, 0x7153, k) * 2 - 1),
          DistribuicaoDoTimeSlice.onda =>
            m *
                math.sin(
                  2 * math.pi * (ciclos * x + fase + varredura * segundos / n),
                ),
          _ => (k - (n - 1) / 2) * (m / ((n - 1) / 2)),
        };
        return (atraso + deslocamento).round();
      })(),
  ];
}

/// A FAIXA [k] de [n] como um poligono no espaco da composicao.
///
/// [anguloGraus] e a direcao em que o indice cresce, com y para baixo:
/// 0 = da esquerda para a direita (faixas verticais), 90 = de cima para
/// baixo (faixas horizontais). [vao] tira metade de cada lado da faixa —
/// o que fica entre elas e transparente.
Path faixaDoTimeSlice(
  Size tamanho, {
  required double anguloGraus,
  required int k,
  required int n,
  double vao = 0,
}) {
  final a = anguloGraus * math.pi / 180;
  final d = Offset(math.cos(a), math.sin(a));
  final q = Offset(-d.dy, d.dx);
  double projeta(Offset p) => p.dx * d.dx + p.dy * d.dy;
  final cantos = [
    Offset.zero,
    Offset(tamanho.width, 0),
    Offset(0, tamanho.height),
    Offset(tamanho.width, tamanho.height),
  ];
  var e0 = double.infinity, e1 = -double.infinity;
  for (final c in cantos) {
    final s = projeta(c);
    e0 = math.min(e0, s);
    e1 = math.max(e1, s);
  }
  final largura = (e1 - e0) / n.clamp(1, 1 << 16);
  // As faixas das pontas se estendem alem do quadro: arredondamento
  // nunca deixa uma lasca sem imagem na borda.
  final folgaInicio = k == 0 ? largura : 0.0;
  final folgaFim = k == n - 1 ? largura : 0.0;
  final s0 = e0 + k * largura + vao / 2 - folgaInicio;
  final s1 = e0 + (k + 1) * largura - vao / 2 + folgaFim;
  // Vao maior que a faixa: nao sobra nada dela.
  if (s1 <= s0) return Path();
  final l = tamanho.longestSide * 2 + largura * 4;
  // Os pontos com projecao s formam a reta d*s + q*t; o centro de q e o
  // centro do quadro, para a faixa nunca ficar curta.
  final centro = Offset(tamanho.width / 2, tamanho.height / 2);
  final t0 = centro.dx * q.dx + centro.dy * q.dy;
  Offset ponto(double s, double t) => d * s + q * (t0 + t);
  final a0 = ponto(s0, -l), a1 = ponto(s0, l);
  final b1 = ponto(s1, l), b0 = ponto(s1, -l);
  return Path()
    ..moveTo(a0.dx, a0.dy)
    ..lineTo(a1.dx, a1.dy)
    ..lineTo(b1.dx, b1.dy)
    ..lineTo(b0.dx, b0.dy)
    ..close();
}

/// O instante LOCAL de uma camada deslocado [quadros] quadros e preso ao
/// intervalo dela ("segurar": antes do inicio vale o primeiro quadro,
/// depois do fim, o ultimo).
Duration localDeslocado(Layer layer, Duration local, int quadros, int fps) {
  final f = fps < 1 ? 30 : fps;
  final alvo = local + Duration(microseconds: (quadros * 1000000 / f).round());
  if (alvo < Duration.zero) return Duration.zero;
  final ultimo = layer.duration - const Duration(microseconds: 1);
  return alvo > ultimo ? (ultimo < Duration.zero ? Duration.zero : ultimo) : alvo;
}

/// POSTERIZE TIME: o instante local preso a uma grade de [taxa] quadros
/// por segundo, com [fase] (0..1) deslocando a grade.
Duration localPosterizado(Duration local, double taxa, double fase) {
  if (local <= Duration.zero) return Duration.zero;
  final r = taxa.clamp(.1, 240.0);
  final phi = fase.clamp(0.0, 1.0);
  final degrau = ((local.inMicroseconds / 1e6) * r + phi).floor();
  final segundos = (degrau - phi) / r;
  if (segundos <= 0) return Duration.zero;
  return Duration(microseconds: (segundos * 1e6).round());
}

/// O primeiro Time Slice / Posterize Time ligado de uma camada.
EffectInstance? efeitoDeTempo(Layer layer, EffectType tipo) {
  for (final e in layer.effects) {
    if (e.enabled && e.type == tipo) return e;
  }
  return null;
}

/// Os atrasos (em quadros) do Time Slice [e] no instante local [local].
List<int> atrasosDoEfeito(EffectInstance e, Duration local) =>
    atrasosDasFaixas(
      faixas: e.paramAt('slices', local).round().clamp(1, 64),
      distribuicao: e.paramAt('distribution', local).round().clamp(0, 4),
      maximo: e.paramAt('max_offset', local).clamp(-120.0, 120.0),
      curva: e.paramAt('curve', local).round().clamp(0, 3),
      ciclos: e.paramAt('cycles', local),
      fase: e.paramAt('phase', local),
      varredura: e.paramAt('sweep', local),
      segundos: local.inMicroseconds / 1e6,
      semente: e.paramAt('seed', local).round(),
      deslocamento: e.paramAt('frame_offset', local),
    );

/// OS OUTROS INSTANTES DA COMPOSICAO que um quadro em [t] vai pedir as
/// camadas de video: as faixas do Time Slice, o degrau do Posterize Time
/// e as copias do Echo. A exportacao decodifica esses quadros antes de
/// desenhar; sem isso, cada faixa mostraria o mesmo quadro do video.
Set<Duration> instantesDeOutroTempo(
  List<Layer> layers,
  Duration t,
  int fps,
) {
  final out = <Duration>{};
  for (final layer in layers) {
    if (!layer.activeAt(t)) continue;
    final local = layer.localTime(t);
    var base = local;
    final poster = efeitoDeTempo(layer, EffectType.posterizeTime);
    if (poster != null) {
      base = localPosterizado(
        local,
        poster.paramAt('rate', local),
        poster.paramAt('phase', local),
      );
      if (base != local) out.add(layer.startTime + base);
    }
    final fatias = efeitoDeTempo(layer, EffectType.timeSlice);
    if (fatias != null) {
      for (final atraso in atrasosDoEfeito(fatias, local).toSet()) {
        if (atraso == 0 && base == local) continue;
        out.add(layer.startTime + localDeslocado(layer, base, atraso, fps));
      }
    }
    final eco = efeitoDeTempo(layer, EffectType.echo);
    if (eco != null) {
      final n = eco.paramAt('ecos', local).round().clamp(1, 8);
      final gapUs = (eco.paramAt('intervalo', local) * 1e6).round();
      for (var i = 1; i <= n; i++) {
        final et = t - Duration(microseconds: gapUs * i);
        if (layer.activeAt(et)) out.add(et);
      }
    }
  }
  out.remove(t);
  return out;
}

/// A FICHA DO TIME SLICE, E OS PARAMETROS DO PLUGIN DE VERDADE.
///
/// ELA FALTAVA. O motor do Time Slice estava inteiro aqui e ligado no
/// palco desde 14/09 — mas sem ficha a pessoa nao tinha como pedir o
/// efeito nem como mexer num parametro, entao o caminho existia e nao
/// saia do lugar. A ficha entrou em 18/09.
///
/// OS QUATRO PRIMEIROS SAO OS DO S_TimeSlice, com os valores de fabrica
/// lidos do plugin no AE do dono (`Slice Direction` -90, `Slice Number`
/// 12, `Frame Offset` 0, `Interp Frames` 0). OS NOMES DAS CHAVES sao os
/// que o MOTOR le — trocar 'slices' por 'slice_number' aqui deixaria o
/// parametro mudo, e e por isso que o teste cobra os dois lados.
///
/// NAO ENTROU O `Interp Frames`: ele pede um quadro INTERPOLADO entre
/// dois, e o que o compositor sabe entregar e a camada montada num
/// instante — a interpolacao de verdade mora na exportacao (RIFE), nao
/// aqui. Prometer o parametro sem a conta seria pior do que nao te-lo.
///
/// OS DEMAIS SAO NOSSOS: a distribuicao dos atrasos entre as faixas, a
/// curva, a onda com ciclos e fase, e a semente. O plugin so tem a escada
/// (um quadro por faixa), que aqui e a `DistribuicaoDoTimeSlice.escada`.
const efeitosTimeSlice = <EffectType, EffectSpec>{
  EffectType.timeSlice: EffectSpec(
    id: 's_timeslice',
    name: 'Fatias no tempo',
    category: 'Time',
    synonyms: [
      'fatias no tempo',
      'time slice',
      'timeslice',
      'slit scan',
      'fatias',
      'cortina de tempo',
    ],
    params: {
      'mix': EffectParam('Mistura', 100, 0, 100, unit: '%', decimals: 1),
      'angle': EffectParam(
        'Direção das fatias',
        -90,
        -180,
        180,
        unit: '°',
        decimals: 1,
      ),
      'slices': EffectParam('Fatias', 12, 1, 64, decimals: 0),
      'frame_offset': EffectParam(
        'Deslocamento',
        0,
        -120,
        120,
        unit: 'q',
        decimals: 0,
        dragStep: .2,
      ),
      'gap': EffectParam('Vão', 0, 0, 10, decimals: 2, dragStep: .05),
      'distribution': EffectParam(
        'Distribuição',
        0,
        0,
        4,
        kind: ParamKind.choice,
        options: ['Escada', 'Linear', 'Centro', 'Aleatória', 'Onda'],
      ),
      'max_offset': EffectParam(
        'Atraso máximo',
        12,
        -120,
        120,
        unit: 'q',
        decimals: 0,
        dragStep: .2,
      ),
      'curve': EffectParam(
        'Curva',
        0,
        0,
        3,
        kind: ParamKind.choice,
        options: ['Reta', 'Quadrática', 'Raiz', 'S'],
      ),
      'cycles': EffectParam('Ciclos', 1, 0, 8, decimals: 2, dragStep: .05),
      'phase': EffectParam('Fase', 0, 0, 1, decimals: 2, dragStep: .01),
      'sweep': EffectParam('Varredura', 0, -4, 4, decimals: 2, dragStep: .05),
      'seed': EffectParam('Semente', 0, 0, 999, kind: ParamKind.seed),
    },
    presets: [
      // Os do plugin: a escada de um quadro por faixa.
      EffectPronto('Escada', {'distribution': 0, 'slices': 12, 'max_offset': 6}),
      EffectPronto('Escada larga', {
        'distribution': 0,
        'slices': 24,
        'max_offset': 12,
      }),
      EffectPronto('Chegando', {
        'distribution': 1,
        'slices': 16,
        'max_offset': 24,
        'curve': 1,
      }),
      EffectPronto('Saindo', {
        'distribution': 1,
        'slices': 16,
        'max_offset': -24,
        'curve': 2,
      }),
      EffectPronto('Do centro', {
        'distribution': 2,
        'slices': 20,
        'max_offset': 18,
      }),
      EffectPronto('Onda', {
        'distribution': 4,
        'slices': 24,
        'max_offset': 12,
        'cycles': 2,
        'sweep': 0.7,
      }),
      EffectPronto('Quadro a quadro', {
        'distribution': 0,
        'slices': 8,
        'max_offset': 4,
        'gap': 0.12,
      }),
      EffectPronto('Sortido', {
        'distribution': 3,
        'slices': 20,
        'max_offset': 16,
        'seed': 7,
      }),
    ],
    montar: ['slices', 'max_offset', 'distribution'],
  ),
};
