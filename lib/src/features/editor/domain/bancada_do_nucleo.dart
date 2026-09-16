/// A BANCADA DO NUCLEO: as cinco cenas A-E que medem o motor ANTES e
/// DEPOIS de cada pedaco ir para o nucleo em C++.
///
/// A regra do projeto e nao migrar as cegas: motor atual medido, candidato
/// medido, comparacao. Para a comparacao valer, as cenas sao GERADAS (sem
/// arquivo do usuario), identicas em qualquer aparelho, e so usam efeitos
/// que existem no catalogo de hoje.
///
///   A  1 forma, 5 s, 2 keyframes                    — o piso do motor
///   B  video + texto animado + efeitos              — o caso comum
///   C  video + motion graph + efeitos               — curvas e expressao
///   D  3D + video + texto + efeitos                 — tudo junto
///   E  3D complexo + animacao + materiais + luzes   — o teto
///
/// A medicao ([MedidaDaBancada]) separa o fio da interface (build) da
/// rasterizacao: e essa separacao que diz se o trabalho pesado ainda
/// esta no caminho do dedo.
library;

import 'dart:math' as math;
import 'dart:ui' show Color, Offset;

import 'effect.dart';
import 'estresse3d.dart';
import 'keyframe.dart';
import 'layer.dart';
import 'text_animator.dart';
import 'video_project.dart';

enum CenaDaBancada { a, b, c, d, e }

class ReceitaDaBancada {
  const ReceitaDaBancada({
    required this.id,
    required this.titulo,
    required this.descricao,
    this.video = false,
  });

  final CenaDaBancada id;
  final String titulo;
  final String descricao;

  /// A cena usa o video de teste gerado pela tela.
  final bool video;

  String get letra => id.name.toUpperCase();
}

const receitasDaBancada = <ReceitaDaBancada>[
  ReceitaDaBancada(
    id: CenaDaBancada.a,
    titulo: '1 forma, 5 s, 2 keyframes',
    descricao: 'O piso: o custo do palco sem nada pesado.',
  ),
  ReceitaDaBancada(
    id: CenaDaBancada.b,
    titulo: 'Video + texto + efeitos',
    descricao: 'Video cobrindo, texto animado letra a letra, tres efeitos.',
    video: true,
  ),
  ReceitaDaBancada(
    id: CenaDaBancada.c,
    titulo: 'Video + motion graph + efeitos',
    descricao: 'Cinquenta keyframes com curvas, expressao wiggle, efeitos.',
    video: true,
  ),
  ReceitaDaBancada(
    id: CenaDaBancada.d,
    titulo: '3D + video + texto + efeitos',
    descricao: 'Cena 3D de sessenta objetos sobre video, texto e efeitos.',
    video: true,
  ),
  ReceitaDaBancada(
    id: CenaDaBancada.e,
    titulo: '3D complexo + animacao + materiais + luzes',
    descricao: 'Sessenta objetos PBR animados, emissivo, luzes com sombra.',
  ),
];

/// A duracao de todas as cenas (a timeline que o scrubbing percorre).
const duracaoDaBancada = Duration(seconds: 10);

/// O projeto da cena [id]. [video] e o caminho do video de teste; sem
/// ele as cenas com video seguem sem a camada (e a medida diz isso).
VideoProject montarCenaDaBancada(CenaDaBancada id, {String? video}) {
  final camadas = <Layer>[];
  const dur = duracaoDaBancada;
  const centro = Offset(960, 540);

  VideoLayer? camadaDeVideo(List<EffectInstance> efeitos) => video == null
      ? null
      : VideoLayer(
          id: 'bancada-video',
          name: 'Video de teste',
          startTime: Duration.zero,
          duration: dur,
          sourcePath: video,
          sourceDuration: dur,
          effects: efeitos,
        );

  TextLayer textoAnimado() => TextLayer(
    id: 'bancada-texto',
    name: 'Texto animado',
    startTime: Duration.zero,
    duration: dur,
    text: 'AUREA NUCLEO',
    fontSize: 140,
    position: AnimatedOffset(const Offset(960, 860)),
    animators: [
      // Uma onda que atravessa as letras: posicao, escala e opacidade
      // por letra, o caso que o motor de texto sofre mais.
      TextAnimator(
        name: 'Onda',
        selectors: [
          RangeSelector(
            end: AnimatedDouble(.35),
            offset: AnimatedDouble(-.35, [
              const Keyframe(time: Duration.zero, value: -.35),
              const Keyframe(
                time: Duration(seconds: 5),
                value: 1,
                ease: Easing.easeInOut,
              ),
            ]),
          ),
        ],
        properties: [
          AnimatorProperty(
            type: TextAnimProp.positionY,
            value: AnimatedDouble(-80),
          ),
          AnimatorProperty(
            type: TextAnimProp.scale,
            value: AnimatedDouble(160),
          ),
          AnimatorProperty(
            type: TextAnimProp.opacity,
            value: AnimatedDouble(40),
          ),
        ],
      ),
    ],
  );

  List<EffectInstance> efeitosDeCor() => [
    EffectInstance(type: EffectType.unsharpMask),
    EffectInstance(type: EffectType.hueSaturation),
    EffectInstance(type: EffectType.vignette),
  ];

  switch (id) {
    case CenaDaBancada.a:
      camadas.add(
        ShapeLayer(
          id: 'bancada-forma',
          name: 'Forma',
          startTime: Duration.zero,
          duration: const Duration(seconds: 5),
          position: AnimatedOffset(const Offset(400, 540))
              .withKeyframe(Duration.zero, const Offset(400, 540))
              .withKeyframe(
                const Duration(seconds: 5),
                const Offset(1520, 540),
                Easing.easeInOut,
              ),
        ),
      );
    case CenaDaBancada.b:
      final v = camadaDeVideo(efeitosDeCor());
      if (v != null) camadas.add(v);
      camadas.add(textoAnimado());
    case CenaDaBancada.c:
      final v = camadaDeVideo([
        EffectInstance(type: EffectType.vhsDamage),
        EffectInstance(type: EffectType.hueSaturation),
      ]);
      if (v != null) camadas.add(v);
      const curvas = [
        Easing.easeInOut,
        Easing.overshoot,
        Easing.easeOut,
        Easing.bounce,
      ];
      camadas.add(
        ShapeLayer(
          id: 'bancada-grafico',
          name: 'Motion graph',
          startTime: Duration.zero,
          duration: dur,
          position: AnimatedOffset(centro),
          rotation: AnimatedDouble(
            0,
            [
              for (var k = 0; k <= 50; k++)
                Keyframe(
                  time: Duration(milliseconds: k * 200),
                  value: (k.isEven ? 1 : -1) * 25.0 * (k % 7),
                  ease: curvas[k % curvas.length],
                ),
            ],
            LoopSpec.none,
            'value + wiggle(3, 40)',
          ),
          scaleX: AnimatedDouble(1, [
            for (var k = 0; k <= 20; k++)
              Keyframe(
                time: Duration(milliseconds: k * 500),
                value: k.isEven ? 1.0 : 1.8,
                ease: curvas[(k + 1) % curvas.length],
              ),
          ]),
          effects: [EffectInstance(type: EffectType.turbulentDisplace)],
        ),
      );
    case CenaDaBancada.d:
      final v = camadaDeVideo(efeitosDeCor());
      if (v != null) camadas.add(v);
      camadas.add(
        Scene3DLayer(
          id: 'bancada-3d',
          name: 'Cena 3D',
          startTime: Duration.zero,
          duration: dur,
          scene: cenaObjetos100(fundo: null).copyWith(
            nodes: cenaObjetos100(fundo: null).nodes.take(60).toList(),
          ),
          showHelpers: false,
        ),
      );
      camadas.add(textoAnimado());
    case CenaDaBancada.e:
      final pbr = cenaPbrAnimada();
      final luzes = cenaLuzesESombras();
      camadas.add(
        Scene3DLayer(
          id: 'bancada-3d',
          name: 'Cena 3D complexa',
          startTime: Duration.zero,
          duration: dur,
          scene: pbr.copyWith(
            nodes: [...pbr.nodes, ...luzes.nodes],
            lights: [...pbr.lights, ...luzes.lights],
          ),
          showHelpers: false,
        ),
      );
  }

  return VideoProject(
    name: 'Bancada ${id.name.toUpperCase()}',
    createdAt: DateTime(2026, 9, 16),
    aspectRatio: 16 / 9,
    fps: 30,
    resolutionHeight: 1080,
    layers: camadas,
    backgroundColor: const Color(0xFF0B0E14),
  );
}

/// Um quadro medido: o que o Flutter reporta em `FrameTiming`.
class QuadroMedido {
  const QuadroMedido({
    required this.buildMs,
    required this.rasterMs,
    required this.totalMs,
  });

  /// Fio da interface: build, layout e gravacao da pintura.
  final double buildMs;

  /// Fio de rasterizacao: a GPU recebendo o quadro.
  final double rasterMs;

  /// Do vsync ate o quadro pronto.
  final double totalMs;
}

/// As estatisticas de uma fase (tocando, arrastando) de uma cena.
class MedidaDaBancada {
  MedidaDaBancada(List<QuadroMedido> quadros, {required this.segundos})
    : quadros = quadros.length,
      build = SerieDaBancada([for (final q in quadros) q.buildMs]),
      raster = SerieDaBancada([for (final q in quadros) q.rasterMs]),
      total = SerieDaBancada([for (final q in quadros) q.totalMs]),
      perdidos60 = quadros.where((q) => q.totalMs > 1000 / 60 * 1.5).length,
      perdidos30 = quadros.where((q) => q.totalMs > 1000 / 30 * 1.5).length,
      travadas = quadros.where((q) => q.totalMs > 120).length;

  final int quadros;
  final double segundos;
  final SerieDaBancada build, raster, total;

  /// Quadros que passaram de 1,5 vsync a 60 e a 30 fps.
  final int perdidos60, perdidos30;

  /// Acima de 120 ms: o que se sente como app travado.
  final int travadas;

  /// Quadros entregues por segundo de relogio (nao 1000/mediana: um
  /// palco parado entrega poucos quadros e isso tambem e resposta).
  double get fps => segundos <= 0 ? 0 : quadros / segundos;

  String linha(String fase) =>
      '$fase: $quadros quadros em ${segundos.toStringAsFixed(1)} s '
      '(${fps.toStringAsFixed(1)} fps) · '
      'UI ${build.resumo} · raster ${raster.resumo} · '
      'total ${total.resumo} · perdidos 60/30: $perdidos60/$perdidos30 · '
      'travadas $travadas';
}

/// Mediana, p95 e pior de uma serie de tempos em ms.
class SerieDaBancada {
  SerieDaBancada(List<double> valores) : _ordenados = List.of(valores)..sort();

  final List<double> _ordenados;

  double _em(double f) => _ordenados.isEmpty
      ? 0
      : _ordenados[(f * (_ordenados.length - 1)).round().clamp(
          0,
          _ordenados.length - 1,
        )];

  double get mediana => _em(.5);
  double get p95 => _em(.95);
  double get pior => _ordenados.isEmpty ? 0 : _ordenados.last;

  String get resumo =>
      'med ${mediana.toStringAsFixed(1)} / p95 ${p95.toStringAsFixed(1)} / '
      'pior ${pior.toStringAsFixed(0)} ms';
}

/// A posicao do cursor no SCRUBBING: vai e volta pela timeline inteira
/// a [velocidade] vezes o tempo real, como um dedo arrastando.
Duration tempoDoArrasto(Duration decorrido, {double velocidade = 1.5}) {
  final totalUs = duracaoDaBancada.inMicroseconds;
  final andado =
      (decorrido.inMicroseconds * velocidade).round() % (2 * totalUs);
  return Duration(
    microseconds: andado <= totalUs ? andado : 2 * totalUs - andado,
  );
}

/// Memoria ao fim de cada ciclo de montar e desmontar a mesma cena. O que
/// importa e a TENDENCIA: crescimento continuo e vazamento.
class CiclosDeMemoria {
  CiclosDeMemoria(this.rssMbPorCiclo);

  final List<int> rssMbPorCiclo;

  /// Inclinacao (MB por ciclo) pelos minimos quadrados, sem o primeiro
  /// ciclo (caches que enchem uma vez nao sao vazamento).
  double get mbPorCiclo {
    final ys = rssMbPorCiclo.skip(1).toList();
    final n = ys.length;
    if (n < 2) return 0;
    final mx = (n - 1) / 2;
    final my = ys.reduce((a, b) => a + b) / n;
    var num = 0.0, den = 0.0;
    for (var i = 0; i < n; i++) {
      num += (i - mx) * (ys[i] - my);
      den += (i - mx) * (i - mx);
    }
    return den == 0 ? 0 : num / den;
  }

  bool get pareceVazar => rssMbPorCiclo.length >= 4 && mbPorCiclo > 3;

  String get linha =>
      'ciclos de montar/desmontar: ${rssMbPorCiclo.join(' -> ')} MB · '
      '${mbPorCiclo >= 0 ? '+' : ''}${mbPorCiclo.toStringAsFixed(1)} MB por ciclo'
      '${pareceVazar ? ' · CRESCIMENTO CONTINUO' : ''}';
}

/// Para quem precisa de um numero so por cena (o resumo do relatorio).
double piorDe(Iterable<double> valores) =>
    valores.fold(0.0, (a, b) => math.max(a, b));
