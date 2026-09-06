import 'dart:ui';

import '../../editor/domain/effect.dart';
import '../../editor/domain/element3d.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/shape.dart';
import '../../editor/domain/video_project.dart';

/// MODELO "PINDOWN": a recriacao de um motion de referencia, cena por
/// cena, com o que o aplicativo faz.
///
/// O original tem 9,33 s a 30 fps em 720 x 1278 e cinco cenas:
///
///   quadros   0..43   a casa verde, com a faisca orbitando
///   quadros  44..69   a virada: flash, casa em chamas, piscada
///   quadros  70..139  o olho que chora sangue
///   quadros 140..207  a espada fincada no chao rachado
///   quadros 208..279  as coroas girando
///
/// TUDO AQUI FOI MEDIDO, nao estimado. A trajetoria da faisca sai quadro
/// a quadro por componentes conectados sobre o branco puro; as paradas do
/// gradiente do horizonte saem da coluna de pixels; a piscada vermelha
/// sai da diferenca R-B quadro a quadro; a coreografia das coroas sai da
/// mascara de cor. Os numeros abaixo tem cara de medidos porque sao.
///
/// DUAS COISAS PRECISARAM ENTRAR NO MOTOR para isto existir: o gradiente
/// de mais de duas paradas (a faixa do horizonte tem seis) e o solido
/// Coroa (`crownMesh`, o tubo com a borda de cima em dentes).

// ---------------------------------------------------------- a paleta
const _ceuTopo = Color(0xFF041124);
const _ceuBaixo = Color(0xFF15335B);
const _chaoTopo = Color(0xFF0A1F28);
const _chaoMeio = Color(0xFF21476F);
const _chaoVerde = Color(0xFF30BE9A);
const _chaoMenta = Color(0xFF3AFFAD);
const _chaoCiano = Color(0xFF58FFF2);
const _chaoClaro = Color(0xFF9DFFFD);

const _telhado = Color(0xFF28CF92);
const _paredeEscura = Color(0xFF0F3554);
const _vao = Color(0xFF0C2A46);
const _branco = Color(0xFFFFFFFF);

const _vermelhoClaro = Color(0xFFF06A5A);
const _rosaOlho = Color(0xFFF7BFC4);
const _sangue = Color(0xFFB3141A);
const _pupila = Color(0xFF4A0E12);

const _aco = Color(0xFFF2EFF4);
const _acoQuente = Color(0xFFFFB27A);
const _ceuEspada = Color(0xFF6C7BA8);
const _chaoEspada = Color(0xFFE98A63);
const _cabo = Color(0xFF3A3550);
const _pele = Color(0xFFFFE3D2);

const _coroa = Color(0xFFE8604F);

const _largura = 720;
const _altura = 1278;

/// A linha do chao da primeira cena, medida: e onde a coluna de pixels
/// despenca.
const double _linhaDoChao = 805;

Duration _s(double seg) => Duration(microseconds: (seg * 1e6).round());
Duration _q(int quadro) => _s(quadro / 30.0);
double _seg(Duration d) => d.inMicroseconds / 1e6;

const Easing _suave = Easing(x1: 0.25, y1: 0.1, x2: 0.25, y2: 1);
const Easing _mola = Easing.overshoot;

/// Curva de DEGRAU: segura o valor e troca de uma vez. E o que faz a
/// piscada piscar em vez de pulsar.
const Easing _degrau = Easing(x1: 1, y1: 0, x2: 1, y2: 0);

AnimatedDouble _ad(double base, [List<(double, double, Easing)>? kfs]) =>
    AnimatedDouble(base, [
      if (kfs != null)
        for (final k in kfs) Keyframe(time: _s(k.$1), value: k.$2, ease: k.$3),
    ]);

AnimatedOffset _ao(Offset base, [List<(double, Offset, Easing)>? kfs]) =>
    AnimatedOffset(base, [
      if (kfs != null)
        for (final k in kfs) Keyframe(time: _s(k.$1), value: k.$2, ease: k.$3),
    ]);

/// Glow. O original inteiro e desenhado com ele: nada tem borda dura.
EffectInstance _glow({double raio = 60, double intensidade = 0.9}) =>
    EffectInstance(type: EffectType.lightGlow, params: {
      'raio': _ad(raio),
      'intensity': _ad(intensidade * 100),
      'threshold': _ad(28),
    });

ShapeLayer _retangulo({
  required String id,
  required String nome,
  required Offset centro,
  required double w,
  required double h,
  required Duration inicio,
  required Duration duracao,
  Color? cor,
  ShapeGradientFill? gradiente,
  double raio = 0,
  AnimatedOffset? posicao,
  AnimatedDouble? opacidade,
  AnimatedDouble? sx,
  AnimatedDouble? sy,
  List<EffectInstance>? efeitos,
}) =>
    ShapeLayer(
      id: id,
      name: nome,
      startTime: inicio,
      duration: duracao,
      position: posicao ?? _ao(centro),
      opacity: opacidade,
      scaleX: sx,
      scaleY: sy,
      effects: efeitos,
      contents: [
        ShapeParametric(
          kind: ParamShapeKind.rect,
          sizeX: _ad(w),
          sizeY: _ad(h),
          roundness: _ad(raio),
        ),
        if (gradiente != null) gradiente else ShapeFill(color: cor ?? _branco),
      ],
    );

/// Uma forma qualquer por caminho: telhado, chama, palpebra, lamina.
ShapeLayer _caminho({
  required String id,
  required String nome,
  required Offset centro,
  required String d,
  required double tamanho,
  required Duration inicio,
  required Duration duracao,
  Color? cor,
  ShapeGradientFill? gradiente,
  AnimatedOffset? posicao,
  AnimatedDouble? opacidade,
  AnimatedDouble? sx,
  AnimatedDouble? sy,
  List<EffectInstance>? efeitos,
}) =>
    ShapeLayer(
      id: id,
      name: nome,
      startTime: inicio,
      duration: duracao,
      position: posicao ?? _ao(centro),
      opacity: opacidade,
      scaleX: sx,
      scaleY: sy,
      effects: efeitos,
      contents: [
        ShapeSvgPath(pathData: d, size: tamanho),
        if (gradiente != null) gradiente else ShapeFill(color: cor ?? _branco),
      ],
    );

// ===================================================== CENA 1 — A CASA

/// Centro e largura da faisca em cada segundo quadro, extraidos do video
/// por componentes conectados sobre o branco puro. Ela nao orbita em
/// circulo: sobe, mergulha para a esquerda, volta e sobe de novo mais
/// alto — um oito frouxo.
const _faisca = <(int, double, double, double)>[
  (4, 350, 796, 122),
  (6, 384, 719, 180),
  (8, 385, 642, 180),
  (10, 374, 574, 159),
  (12, 357, 509, 151),
  (14, 343, 460, 122),
  (16, 353, 433, 146),
  (18, 331, 447, 160),
  (20, 297, 520, 134),
  (22, 305, 590, 104),
  (24, 322, 662, 127),
  (26, 381, 697, 142),
  (28, 424, 607, 113),
  (30, 432, 418, 108),
  (32, 409, 261, 119),
  (34, 386, 230, 117),
  (36, 372, 225, 118),
  (38, 363, 238, 116),
  (40, 359, 288, 120),
  (42, 357, 404, 116),
];

/// O tamanho natural da faisca desenhada, medido no proprio render: com
/// escala 1 ela sai com 405 px.
const double _faiscaNatural = 405;

ShapeLayer _faiscaLayer() => ShapeLayer(
      id: 'p_faisca',
      name: 'Faisca',
      startTime: _q(3),
      duration: _q(41),
      position: AnimatedOffset(
        Offset(_faisca.first.$2, _faisca.first.$3),
        [
          for (final (q, x, y, _) in _faisca)
            Keyframe(time: _q(q - 3), value: Offset(x, y), ease: _suave),
        ],
      ),
      scaleX: AnimatedDouble(1, [
        for (final (q, _, _, w) in _faisca)
          Keyframe(time: _q(q - 3), value: w / _faiscaNatural, ease: _suave),
      ]),
      scaleY: AnimatedDouble(1, [
        for (final (q, _, _, w) in _faisca)
          Keyframe(time: _q(q - 3), value: w / _faiscaNatural, ease: _suave),
      ]),
      effects: [_glow(raio: 90, intensidade: 1.2)],
      contents: [
        ShapePath(primitive: ShapePrimitive.sparkle),
        ShapeFill(color: _branco),
      ],
    );

/// As pecas da casa, nas posicoes medidas no quadro 40 do original.
///
/// [vermelha] serve a segunda cena: a mesma casa, so que em chamas. Sao
/// as duas empilhadas, e a piscada e a de cima aparecendo e sumindo —
/// nao duas cenas trocando.
List<Layer> _casa({
  required Duration inicio,
  required Duration ate,
  bool vermelha = false,
  AnimatedDouble Function()? opacidade,
  String copia = '',
}) {
  final d = ate - inicio;
  // A CASA APARECE DUAS VEZES no filme, e cada aparicao e um jogo de
  // camadas proprio. Sem [copia] as duas usariam os mesmos ids, e id
  // repetido e chave repetida na timeline — o app cai na hora de montar
  // a lista.
  final sufixo = (vermelha ? '_r' : '') + copia;
  Color escuro() => vermelha ? const Color(0xFF5A1010) : _paredeEscura;
  Color vao() => vermelha ? const Color(0xFF450A0A) : _vao;

  return [
    _retangulo(
      id: 'p_empena$sufixo',
      nome: 'Empena',
      centro: const Offset(295, 716),
      w: 90,
      h: 198,
      cor: escuro(),
      opacidade: opacidade?.call(),
      inicio: inicio,
      duracao: d,
    ),
    _caminho(
      id: 'p_telhado$sufixo',
      nome: 'Telhado',
      centro: const Offset(380, 654),
      d: 'M12 88 L115 0 L218 88 L230 95 L0 95 Z',
      tamanho: 230,
      gradiente: vermelha
          ? ShapeGradientFill(
              colorA: const Color(0xFFFF8A6A),
              colorB: const Color(0xFFC8231C),
              angleDeg: 20,
            )
          : ShapeGradientFill(
              colorA: const Color(0xFF3CE6A4),
              colorB: _telhado,
              angleDeg: 20,
            ),
      opacidade: opacidade?.call(),
      efeitos: [_glow(raio: 40, intensidade: 0.5)],
      inicio: inicio,
      duracao: d,
    ),
    _retangulo(
      id: 'p_chamine$sufixo',
      nome: 'Chamine',
      centro: const Offset(397, 617),
      w: 25,
      h: 45,
      cor: escuro(),
      opacidade: opacidade?.call(),
      inicio: inicio,
      duracao: d,
    ),
    _retangulo(
      id: 'p_parede$sufixo',
      nome: 'Parede',
      centro: const Offset(410, 764),
      w: 140,
      h: 123,
      gradiente: vermelha
          ? ShapeGradientFill(
              colorA: const Color(0xFFFFA98C),
              colorB: _vermelhoClaro,
              angleDeg: 55,
            )
          : ShapeGradientFill(
              colorA: const Color(0xFF45F0A8),
              colorB: const Color(0xFF1FA97C),
              angleDeg: 55,
            ),
      opacidade: opacidade?.call(),
      efeitos: [_glow(raio: 50, intensidade: 0.7)],
      inicio: inicio,
      duracao: d,
    ),
    _retangulo(
      id: 'p_porta$sufixo',
      nome: 'Porta',
      centro: const Offset(412, 782),
      w: 25,
      h: 85,
      cor: vao(),
      opacidade: opacidade?.call(),
      inicio: inicio,
      duracao: d,
    ),
    for (final (i, x) in [368.0, 453.0].indexed)
      _retangulo(
        id: 'p_janela$i$sufixo',
        nome: 'Janela',
        centro: Offset(x, 758),
        w: 16,
        h: 34,
        cor: vao(),
        opacidade: opacidade?.call(),
        inicio: inicio,
        duracao: d,
      ),
  ];
}

/// O fundo das duas primeiras cenas: o ceu e a faixa do horizonte.
List<Layer> _fundoDaCasa({
  required String sufixo,
  required Duration inicio,
  required Duration duracao,
}) =>
    [
      _retangulo(
        id: 'p_chao$sufixo',
        nome: 'Horizonte',
        centro: Offset(360, (_linhaDoChao + _altura) / 2),
        w: 760,
        h: _altura - _linhaDoChao,
        // A FAIXA DO HORIZONTE: seis paradas medidas na coluna de
        // pixels. Com duas cores o meio vira uma mistura suja que nao
        // existe no original — foi por isso que o gradiente de N
        // paradas entrou no motor.
        gradiente: ShapeGradientFill(
          colorA: _chaoTopo,
          extras: const [_chaoMeio, _chaoVerde, _chaoMenta, _chaoCiano],
          colorB: _chaoClaro,
          angleDeg: 90,
        ),
        inicio: inicio,
        duracao: duracao,
      ),
      _retangulo(
        id: 'p_ceu$sufixo',
        nome: 'Ceu',
        centro: const Offset(360, 639),
        w: 760,
        h: 1320,
        gradiente: ShapeGradientFill(
          colorA: _ceuTopo,
          colorB: _ceuBaixo,
          angleDeg: 90,
        ),
        inicio: inicio,
        duracao: duracao,
      ),
    ];

List<Layer> _cenaDaCasa() {
  final ate = _q(44);
  return [
    _faiscaLayer(),
    ..._casa(inicio: Duration.zero, ate: ate),
    ..._fundoDaCasa(sufixo: '', inicio: Duration.zero, duracao: ate),
  ];
}

// ================================================== CENA 2 — A VIRADA
//
// A PISCADA, medida quadro a quadro pela diferenca R-B:
//   44        flash branco
//   45..46    vermelho     47..49  azul
//   50..52    vermelho     53..55  azul
//   56..58    vermelho     59..61  azul
//   62..69    vermelho (segura)
//   70..71    flash branco
//
// Tres batidas de tres quadros e depois segura. E ritmo, nao ruido: a
// curva dos keyframes e de DEGRAU, senao vira pulsacao.
const _piscada = <(int, double)>[
  (44, 1),
  (47, 0),
  (50, 1),
  (53, 0),
  (56, 1),
  (59, 0),
  (62, 1),
];

List<Layer> _cenaDaVirada() {
  final inicio = _q(44);
  final fim = _q(72);
  final d = fim - inicio;

  AnimatedDouble emChamas() => AnimatedDouble(1, [
        for (final (q, v) in _piscada)
          Keyframe(time: _q(q) - inicio, value: v, ease: _degrau),
      ]);

  return [
    // OS DOIS FLASHES: o da virada e o que abre o olho.
    _retangulo(
      id: 'p_flash',
      nome: 'Flash',
      centro: const Offset(360, 639),
      w: 760,
      h: 1320,
      cor: _branco,
      opacidade: AnimatedDouble(0.95, [
        Keyframe(time: Duration.zero, value: 0.95, ease: _suave),
        Keyframe(time: _q(46) - inicio, value: 0, ease: _suave),
        Keyframe(time: _q(69) - inicio, value: 0, ease: _suave),
        Keyframe(time: _q(70) - inicio, value: 1, ease: _degrau),
        Keyframe(time: _q(72) - inicio, value: 0, ease: _suave),
      ]),
      inicio: inicio,
      duracao: d,
    ),
    // AS CHAMAS saindo do telhado: quatro linguas que balancam FORA DE
    // FASE. Em fase, quatro chamas viram um bloco piscando.
    for (final (i, x, alt, atraso) in [
      (0, 352.0, 92.0, 0.0),
      (1, 386.0, 128.0, 0.08),
      (2, 420.0, 104.0, 0.16),
      (3, 448.0, 74.0, 0.05),
    ])
      _caminho(
        id: 'p_chama$i',
        nome: 'Chama',
        centro: Offset(x, 636 - alt / 2),
        d: 'M18 100 Q0 58 12 30 Q18 44 22 24 Q30 44 36 26 Q40 60 18 100 Z',
        tamanho: alt,
        cor: i.isEven ? _vermelhoClaro : const Color(0xFFFFC98A),
        opacidade: emChamas(),
        efeitos: [_glow(raio: 60, intensidade: 1.1)],
        sy: AnimatedDouble(0.8, [
          Keyframe(time: _s(atraso), value: 0.8, ease: _suave),
          Keyframe(time: _s(atraso + 0.18), value: 1.15, ease: _suave),
          Keyframe(time: _s(atraso + 0.36), value: 0.85, ease: _suave),
          Keyframe(time: _s(atraso + 0.54), value: 1.1, ease: _suave),
          Keyframe(time: _s(atraso + 0.72), value: 0.9, ease: _suave),
        ]),
        inicio: inicio,
        duracao: d,
      ),
    ..._casa(
      inicio: inicio,
      ate: fim,
      vermelha: true,
      opacidade: emChamas,
      copia: '2',
    ),
    ..._casa(inicio: inicio, ate: fim, copia: '2'),
    // O FUNDO em chamas por cima do azul, com a mesma piscada.
    _retangulo(
      id: 'p_fundo_fogo',
      nome: 'Fundo em chamas',
      centro: const Offset(360, 639),
      w: 760,
      h: 1320,
      gradiente: ShapeGradientFill(
        colorA: const Color(0xFFE04A2A),
        colorB: const Color(0xFF3A0604),
        radial: true,
      ),
      opacidade: emChamas(),
      inicio: inicio,
      duracao: d,
    ),
    ..._fundoDaCasa(sufixo: '2', inicio: inicio, duracao: d),
  ];
}

// ==================================================== CENA 3 — O OLHO
//
// O olho abre: medido, a palpebra vai de 309 x 199 no quadro 88 a
// 500 x 219 no 136 — abre bem mais em largura que em altura.
List<Layer> _cenaDoOlho() {
  final inicio = _q(70);
  final fim = _q(140);
  final d = fim - inicio;

  AnimatedDouble abrir(double de, double ate) => AnimatedDouble(de, [
        Keyframe(time: Duration.zero, value: de, ease: _suave),
        Keyframe(time: _q(88) - inicio, value: de, ease: _suave),
        Keyframe(time: _q(136) - inicio, value: ate, ease: _suave),
      ]);

  final lagrimaInicio = _q(96);
  return [
    // A FAISCA acima do olho, que cresce quando ele termina de abrir.
    ShapeLayer(
      id: 'p_faisca_olho',
      name: 'Faisca do olho',
      startTime: _q(104),
      duration: _q(36),
      position: _ao(const Offset(360, 470)),
      scaleX: _ad(0, [(0, 0.0, _mola), (0.7, 0.42, _mola)]),
      scaleY: _ad(0, [(0, 0.0, _mola), (0.7, 0.42, _mola)]),
      effects: [_glow(raio: 100, intensidade: 1.4)],
      contents: [
        ShapePath(primitive: ShapePrimitive.sparkle),
        ShapeFill(color: _branco),
      ],
    ),
    // A LAGRIMA: desce pela lateral direita e engorda na ponta.
    _caminho(
      id: 'p_lagrima',
      nome: 'Lagrima',
      centro: const Offset(455, 760),
      d: 'M30 0 Q46 42 46 66 Q46 92 30 92 Q14 92 14 66 Q14 42 30 0 Z',
      tamanho: 108,
      cor: _sangue,
      posicao: _ao(const Offset(455, 760), [
        (0, const Offset(455, 760), _suave),
        (_seg(_q(139) - lagrimaInicio), const Offset(470, 880), _suave),
      ]),
      sy: _ad(0.3, [(0, 0.3, _suave), (1.2, 1.0, _suave)]),
      efeitos: [_glow(raio: 40, intensidade: 0.7)],
      inicio: lagrimaInicio,
      duracao: fim - lagrimaInicio,
    ),
    _retangulo(
      id: 'p_pupila',
      nome: 'Pupila',
      centro: const Offset(348, 676),
      w: 62,
      h: 62,
      raio: 100,
      cor: _pupila,
      sx: abrir(0.2, 1),
      sy: abrir(0.2, 1),
      inicio: inicio,
      duracao: d,
    ),
    _retangulo(
      id: 'p_iris',
      nome: 'Iris',
      centro: const Offset(348, 672),
      w: 116,
      h: 116,
      raio: 100,
      cor: const Color(0xFF7A1C22),
      sx: abrir(0.2, 1),
      sy: abrir(0.2, 1),
      inicio: inicio,
      duracao: d,
    ),
    // A PALPEBRA: a amendoa clara.
    _caminho(
      id: 'p_olho',
      nome: 'Olho',
      centro: const Offset(360, 700),
      d: 'M0 60 Q120 0 260 44 Q160 128 0 60 Z',
      tamanho: 470,
      // O branco fica no canto de cima a esquerda e o rosa desce para
      // a direita, como na referencia — nao e um branco chapado.
      gradiente: ShapeGradientFill(
        colorA: _branco,
        extras: const [Color(0xFFFCDDE0)],
        colorB: _rosaOlho,
        angleDeg: 70,
      ),
      sx: abrir(0.55, 1.0),
      sy: abrir(0.75, 1.0),
      efeitos: [_glow(raio: 110, intensidade: 1.3)],
      inicio: inicio,
      duracao: d,
    ),
    _retangulo(
      id: 'p_fundo_olho',
      nome: 'Fundo do olho',
      centro: const Offset(360, 660),
      w: 760,
      h: 1320,
      gradiente: ShapeGradientFill(
        colorA: const Color(0xFF6E0A12),
        colorB: const Color(0xFF060102),
        radial: true,
      ),
      inicio: inicio,
      duracao: d,
    ),
  ];
}

// ================================================== CENA 4 — A ESPADA
//
// A lamina, medida: entra por cima e desce ate y 843 no quadro 172,
// sempre no eixo central (x 363). As maos chegam no 188.
List<Layer> _cenaDaEspada() {
  final inicio = _q(140);
  final fim = _q(208);
  final d = fim - inicio;

  /// A descida da espada, com o solavanco de quando ela crava.
  AnimatedOffset descida(Offset base) => AnimatedOffset(
        base.translate(0, -430),
        [
          Keyframe(
              time: Duration.zero, value: base.translate(0, -430), ease: _suave),
          Keyframe(time: _q(172) - inicio, value: base, ease: _suave),
          Keyframe(
              time: _q(190) - inicio, value: base.translate(0, 26), ease: _mola),
          Keyframe(time: _q(200) - inicio, value: base, ease: _mola),
        ],
      );

  final maosInicio = _q(188);
  return [
    // AS MAOS, uma acima da outra no cabo, entrando pelos lados.
    for (final (i, y, esp) in [(0, 706.0, -1.0), (1, 792.0, 1.0)])
      _caminho(
        id: 'p_mao$i',
        nome: 'Mao',
        centro: Offset(360 + esp * 150, y),
        d: 'M0 34 Q30 0 96 6 L150 6 Q168 22 150 40 L96 40 Q30 48 0 34 Z',
        tamanho: 250,
        cor: _pele,
        sx: _ad(esp),
        posicao: _ao(Offset(360 + esp * 320, y), [
          (0, Offset(360 + esp * 320, y), _suave),
          (_seg(_q(196) - maosInicio), Offset(360 + esp * 150, y), _suave),
        ]),
        efeitos: [_glow(raio: 70, intensidade: 1.0)],
        inicio: maosInicio,
        duracao: fim - maosInicio,
      ),
    _caminho(
      id: 'p_pomo',
      nome: 'Pomo',
      centro: const Offset(363, 640),
      d: 'M20 0 Q40 26 40 48 Q40 72 20 72 Q0 72 0 48 Q0 26 20 0 Z',
      tamanho: 70,
      cor: _acoQuente,
      posicao: descida(const Offset(363, 640)),
      efeitos: [_glow(raio: 50, intensidade: 1.0)],
      inicio: inicio,
      duracao: d,
    ),
    _retangulo(
      id: 'p_cabo',
      nome: 'Cabo',
      centro: const Offset(363, 730),
      w: 34,
      h: 147,
      cor: _cabo,
      posicao: descida(const Offset(363, 730)),
      inicio: inicio,
      duracao: d,
    ),
    _retangulo(
      id: 'p_guarda',
      nome: 'Guarda',
      centro: const Offset(363, 806),
      w: 237,
      h: 29,
      cor: _aco,
      posicao: descida(const Offset(363, 806)),
      efeitos: [_glow(raio: 40, intensidade: 0.8)],
      inicio: inicio,
      duracao: d,
    ),
    // A LAMINA, com a faixa de luz na diagonal — o detalhe que faz o
    // aco parecer aco e nao um retangulo branco.
    _caminho(
      id: 'p_lamina',
      nome: 'Lamina',
      centro: const Offset(363, 930),
      d: 'M0 0 L110 0 L110 210 L55 262 L0 210 Z',
      tamanho: 296,
      gradiente: ShapeGradientFill(
        colorA: _aco,
        extras: const [_acoQuente, Color(0xFFFFF3E6)],
        colorB: _aco,
        angleDeg: 58,
      ),
      posicao: descida(const Offset(363, 930)),
      efeitos: [_glow(raio: 80, intensidade: 1.2)],
      inicio: inicio,
      duracao: d,
    ),
    // A RACHADURA: o traco que NASCE do ponto onde a lamina entrou, em
    // vez de aparecer pronto.
    ShapeLayer(
      id: 'p_rachadura',
      name: 'Rachadura',
      startTime: _q(192),
      duration: _q(16),
      position: _ao(const Offset(360, 1058)),
      contents: [
        ShapeSvgPath(
          pathData: 'M10 40 L60 6 L74 30 L120 0 M120 0 L150 34 L196 12 '
              'M60 6 L44 44 M150 34 L138 60',
          size: 300,
        ),
        ShapeStroke(color: const Color(0xFF2B1420), width: AnimatedDouble(9)),
        TrimOperator(end: _ad(0, [(0, 0.0, _suave), (0.45, 1.0, _suave)])),
      ],
    ),
    _retangulo(
      id: 'p_chao_espada',
      nome: 'Chao',
      centro: const Offset(360, 1150),
      w: 760,
      h: 260,
      gradiente: ShapeGradientFill(
        colorA: _chaoEspada,
        colorB: const Color(0xFFF6C39B),
        angleDeg: 90,
      ),
      opacidade: _ad(0, [
        (0, 0.0, _suave),
        (_seg(_q(196) - inicio), 1.0, _suave),
      ]),
      inicio: inicio,
      duracao: d,
    ),
    _retangulo(
      id: 'p_fundo_espada',
      nome: 'Fundo',
      centro: const Offset(360, 700),
      w: 760,
      h: 1320,
      gradiente: ShapeGradientFill(
        colorA: const Color(0xFF9AA6C8),
        extras: const [_ceuEspada],
        colorB: const Color(0xFF272A45),
        radial: true,
      ),
      inicio: inicio,
      duracao: d,
    ),
  ];
}

// ================================================= CENA 5 — AS COROAS
//
// A coreografia, medida pela mascara de cor. As tres crescem enquanto
// se afastam do centro: a camera se aproxima.
List<Layer> _cenaDasCoroas() {
  final inicio = _q(208);
  final fim = _q(280);

  Element3DLayer coroa({
    required String id,
    required int nasce,
    required List<(int, double, double, double)> pontos,
    required double giro0,
    required double giro1,
    required double inclina,
  }) {
    final t0 = _q(nasce);
    // 190 de 'size' renderam 501 px de largura: e a proporcao entre o
    // tamanho da camada 3D e o que aparece na tela, medida no render.
    const porPixel = 190 / 501;
    final base = pontos.first.$4;
    return Element3DLayer(
      id: id,
      name: 'Coroa',
      startTime: t0,
      duration: fim - t0,
      kind: Element3DKind.crown,
      size: base * porPixel,
      color: _coroa,
      // Sem arestas: a referencia e sombreada lisa, e a malha aparecendo
      // entrega que aquilo e um poliedro de 48 lados.
      edges: false,
      position: AnimatedOffset(
        Offset(pontos.first.$2, pontos.first.$3),
        [
          for (final (q, x, y, _) in pontos)
            Keyframe(time: _q(q) - t0, value: Offset(x, y), ease: _suave),
        ],
      ),
      scaleX: AnimatedDouble(0.25, [
        Keyframe(time: Duration.zero, value: 0.25, ease: _mola),
        Keyframe(time: _s(0.5), value: 1, ease: _mola),
        for (final (q, _, _, w) in pontos)
          if (_q(q) - t0 > _s(0.5))
            Keyframe(time: _q(q) - t0, value: w / base, ease: _suave),
      ]),
      scaleY: AnimatedDouble(0.25, [
        Keyframe(time: Duration.zero, value: 0.25, ease: _mola),
        Keyframe(time: _s(0.5), value: 1, ease: _mola),
        for (final (q, _, _, w) in pontos)
          if (_q(q) - t0 > _s(0.5))
            Keyframe(time: _q(q) - t0, value: w / base, ease: _suave),
      ]),
      rotationY: _ad(giro0, [
        (0, giro0, Easing.linear),
        (_seg(fim - t0), giro1, Easing.linear),
      ]),
      rotationX: _ad(inclina),
      effects: [_glow(raio: 80, intensidade: 0.7)],
    );
  }

  return [
    coroa(
      id: 'p_coroa_c',
      nasce: 248,
      pontos: const [
        (250, 230, 899, 224),
        (258, 233, 940, 224),
        (266, 192, 951, 230),
        (274, 132, 1023, 268),
      ],
      giro0: 160,
      giro1: 430,
      inclina: 10,
    ),
    coroa(
      id: 'p_coroa_b',
      nasce: 232,
      pontos: const [
        (234, 512, 386, 238),
        (242, 543, 363, 238),
        (250, 480, 311, 254),
        (258, 464, 298, 264),
        (266, 500, 298, 262),
        (274, 557, 225, 318),
      ],
      giro0: 40,
      giro1: -240,
      inclina: 24,
    ),
    coroa(
      id: 'p_coroa_a',
      nasce: 208,
      pontos: const [
        (210, 368, 526, 380),
        (218, 359, 585, 388),
        (226, 367, 619, 380),
        (234, 370, 640, 380),
        (242, 371, 650, 378),
        (250, 373, 646, 388),
        (258, 373, 644, 390),
        (266, 373, 646, 400),
        (274, 382, 655, 486),
      ],
      giro0: -25,
      giro1: 300,
      inclina: 16,
    ),
    _retangulo(
      id: 'p_fundo_coroa',
      nome: 'Fundo',
      centro: const Offset(360, 639),
      w: 760,
      h: 1320,
      gradiente: ShapeGradientFill(
        colorA: const Color(0xFF14200E),
        colorB: const Color(0xFF05070A),
        radial: true,
      ),
      inicio: inicio,
      duracao: fim - inicio,
    ),
  ];
}

/// O modelo inteiro.
VideoProject buildPindownMotionTemplate() {
  return VideoProject(
    name: 'Pindown — recriacao',
    createdAt: DateTime(2026, 9, 3),
    fps: 30,
    // Em retrato o campo 'resolutionHeight' e o LADO CURTO: a largura.
    aspectRatio: _largura / _altura,
    resolutionHeight: _largura,
    // A PRIMEIRA CAMADA DA LISTA E A DE CIMA (o preview pinta em
    // 'layers.reversed'), e as cenas de tras vao por baixo.
    layers: [
      ..._cenaDasCoroas(),
      ..._cenaDaEspada(),
      ..._cenaDoOlho(),
      ..._cenaDaVirada(),
      ..._cenaDaCasa(),
    ],
  );
}
