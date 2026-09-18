import 'dart:math' as math;
import 'dart:ui';

import '../../editor/domain/camera3d.dart';
import '../../editor/domain/camera_cuts.dart';
import '../../editor/domain/effect.dart';
import '../../editor/domain/element3d.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/panorama3d.dart';
import '../../editor/domain/scene3d.dart';
import '../../editor/domain/video_project.dart';
import 'malha_codigo.dart';
import 'textura_procedural.dart';

/// FLOR · DEZ SEGUNDOS DE MANHA.
///
/// Uma rosa numa mesa, luz de janela, e uma camera na MAO. Dez segundos
/// em tres tomadas, uma cena 3D so — o corte e de camera, entao a flor
/// continua a mesma flor quando a tomada troca.
///
///   01 · A LUZ CHEGA  (0–3,4 s, 50 mm)  a flor inteira, luz rasante
///   02 · O DETALHE    (3,4–6,8 s, 100 mm) a borda da petala e o orvalho
///   03 · ELA RESPIRA  (6,8–10 s, 35 mm)  a camera recua e a luz abre
///
/// O QUE FAZ PARECER FOTOGRAFIA, aqui, sao quatro coisas — e nenhuma
/// delas e "mais poligono":
///
///   A MAO       camera na mao nao treme, ela DERIVA. Ver [_mao].
///   O FOCO      macro com pouca profundidade e o que o olho reconhece
///               como lente de verdade; o fundo vira bola de luz.
///   A BORDA     a luz de tras atravessa a petala fina. Nao ha
///               subsuperficie no motor, entao ela e desenhada: a
///               textura clareia e perde cor da base para a ponta.
///   A CONCHA    petala nao e papel plano. As bordas sobem em v², e e
///               essa curva que segura a sombra dentro da flor.
///
/// Tudo e geometria montada em codigo — determinista, sem sorteio, sem
/// relogio: abrir duas vezes da o mesmo filme. E tudo e a mesma camada
/// 3D editavel do app: abra o Estudio, mova a luz, troque a lente.
const florDuration = Duration(seconds: 10);
const florFps = 30;
const florWidth = 1280.0;
const florHeight = 720.0;

/// O teto de triangulos por quadro. A conta que importa: o pintor em CPU
/// custa ~5 ms por mil triangulos visiveis num desktop, e um iPhone
/// cobra duas a tres vezes isso. Abaixo de dez mil a cena roda tambem
/// sem o motor em GPU — e e por isso que a flor tem o detalhe que tem, e
/// nao mais.
const florTriangleBudget = 10000;

/// Os tempos das tres tomadas, em segundos.
const florTomadas = [0.0, 3.4, 6.8];

/// Quantas amostras por segundo a camera vira keyframe.
///
/// Doze. Nao e capricho: a mao tem componentes ate ~3,5 Hz, e amostrar
/// a 12 Hz e o que os representa sem virar serrilha. A 4 Hz — a taxa
/// que basta para uma camera em trilho — o tremor vira degrau.
const florHz = 12;

Duration _t(num seconds) => Duration(microseconds: (seconds * 1000000).round());
AnimatedDouble _ad(double v) => AnimatedDouble(v);
AnimatedDouble _keys(List<(num, num)> values, {Easing ease = Easing.linear}) =>
    AnimatedDouble(values.first.$2.toDouble(), [
      for (final v in values)
        Keyframe(time: _t(v.$1), value: v.$2.toDouble(), ease: ease),
    ]);

/// Amostra uma curva do tempo a [florHz]: a camera vira keyframe de
/// verdade, que da para pegar e mexer no editor.
AnimatedDouble _sample(double Function(double) f) => AnimatedDouble(f(0), [
  for (var i = 0; i <= florHz * 10; i++)
    Keyframe(time: _t(i / florHz), value: f(i / florHz)),
]);

// ============================================================== A MAO

/// Ruido de valor em UMA dimensao, suave, deterministico.
double _ruidoT(double t, int semente) {
  final i = t.floor();
  final f = suave(t - i);
  double h(int k) => ruido(k * 9176 + semente * 7919) * 2 - 1;
  return h(i) + (h(i + 1) - h(i)) * f;
}

/// A MAO DE QUEM SEGURA A CAMERA.
///
/// Camera na mao nao treme: ela DERIVA. O que o olho reconhece como
/// "alguem gravando" e a soma de coisas de velocidades diferentes — o
/// balanco do corpo (~0,6 Hz), a correcao do pulso quando o
/// enquadramento escapa (~1,5 Hz), e um resto de tremor fino por cima.
/// Ruido branco puro parece camera quebrada; senoide pura parece
/// trilho. E a MISTURA que parece gente.
///
/// A respiracao entra so no eixo vertical, que e onde ela aparece.
Vec3 _mao(double t, int semente, double amplitude) {
  double eixo(int s) =>
      _ruidoT(t * 0.62, s) * 1.00 +
      _ruidoT(t * 1.55, s + 31) * 0.42 +
      _ruidoT(t * 3.40, s + 67) * 0.16;
  final respiracao = math.sin(t * 2 * math.pi * 0.26 + semente);
  return Vec3(
    amplitude * eixo(semente),
    amplitude * (eixo(semente + 5) * .70 + respiracao * .55),
    amplitude * eixo(semente + 9),
  );
}

/// O BALANCO DO HORIZONTE. Quem segura na mao nao segura no nivel.
double _rolo(double t, int semente, double amplitude) =>
    amplitude *
    (_ruidoT(t * 0.50, semente) + _ruidoT(t * 1.30, semente + 3) * .35);

// =========================================================== A PETALA

/// UMA PETALA, como superficie parametrica.
///
/// `u` corre da base a ponta, `v` atravessa a largura (-1 a 1). Tres
/// coisas separam uma petala de uma folha de papel dobrada:
///
///   PERFIL   estreita na base, cheia no meio, arredondada na ponta
///   CONCHA   as bordas sobem em v² — e o que segura a sombra dentro
///   ABERTURA a espinha gira de quase vertical (miolo) para fora e para
///            baixo (petalas externas), integrando o angulo passo a
///            passo em vez de interpolar pontos: a curva sai continua.
class _Petala {
  _Petala({
    required this.azimute,
    required this.raioBase,
    required this.alturaBase,
    required this.comprimento,
    required this.largura,
    required this.angIni,
    required this.angFim,
    required this.concha,
    this.torcao = 0,
    this.ondas = 0,
    this.nu = 9,
    this.nv = 6,
  }) {
    final az = azimute * math.pi / 180;
    _rad = Vec3(math.cos(az), 0, math.sin(az));
    _tan = Vec3(-math.sin(az), 0, math.cos(az));
    var r = raioBase, y = alturaBase;
    final dl = comprimento / _passos;
    for (var i = 0; i <= _passos; i++) {
      final uu = i / _passos;
      final a = (angIni + (angFim - angIni) * uu * uu) * math.pi / 180;
      _rs.add(r);
      _ys.add(y);
      _angs.add(a);
      r += math.sin(a) * dl;
      y += math.cos(a) * dl;
    }
  }

  final double azimute;
  final double raioBase;
  final double alturaBase;
  final double comprimento;
  final double largura;
  final double angIni;
  final double angFim;
  final double concha;
  final double torcao;
  final double ondas;
  final int nu;
  final int nv;

  static const _passos = 16;
  static const _up = Vec3(0, 1, 0);

  late final Vec3 _rad;
  late final Vec3 _tan;
  final _rs = <double>[];
  final _ys = <double>[];
  final _angs = <double>[];

  /// Meia largura em `u`. Cheia de 0,2 a 0,8 e arredondando na ponta —
  /// o expoente 6 e o que deixa a ponta CHEIA em vez de bicuda.
  double _meiaLargura(double u) {
    final c = u.clamp(0.0, 1.0);
    final sobe = math.min(1.0, 2.4 * math.sqrt(c));
    final fecha = math.sqrt(math.max(0.0, 1 - math.pow(c, 6).toDouble()));
    return largura * sobe * fecha;
  }

  Vec3 ponto(double u, double v) {
    final iu = (u.clamp(0.0, 1.0)) * _passos;
    final i0 = iu.floor().clamp(0, _passos);
    final i1 = math.min(i0 + 1, _passos);
    final f = iu - i0;
    final r = _rs[i0] + (_rs[i1] - _rs[i0]) * f;
    final y = _ys[i0] + (_ys[i1] - _ys[i0]) * f;
    final a = _angs[i0] + (_angs[i1] - _angs[i0]) * f;

    // A face da petala: perpendicular ao avanco, no plano meridiano.
    final face = _rad * math.cos(a) - _up * math.sin(a);
    // Torcao: a secao gira em volta da espinha ao longo do comprimento.
    final tw = torcao * math.pi / 180 * u;
    final lateral = _tan * math.cos(tw) + face * math.sin(tw);
    final normal = face * math.cos(tw) - _tan * math.sin(tw);

    final w = _meiaLargura(u);
    // A concha abre com u: perto da base a petala e quase plana.
    final copo = concha * (.35 + .65 * u) * v * v;
    // A borda ondulada e o que tira o ar de peca fabricada.
    final onda = ondas * math.sin(v * 3.1 + u * 1.7) * u * u;
    return _rad * r + _up * y + lateral * (v * w) + normal * (copo + onda);
  }

  Vec3 _normal(double u, double v) {
    const e = .012;
    final du =
        ponto((u + e).clamp(0.0, 1.0), v) - ponto((u - e).clamp(0.0, 1.0), v);
    final dv =
        ponto(u, (v + e).clamp(-1.0, 1.0)) - ponto(u, (v - e).clamp(-1.0, 1.0));
    final n = du.cross(dv).normalized;
    // A petala e de dois lados; a normal aponta para fora da flor.
    return n.dot(_rad) < 0 && n.y < 0 ? n * -1 : n;
  }

  void emitir(MalhaCodigo m, int material) {
    final grade = <List<int>>[];
    for (var i = 0; i <= nu; i++) {
      final u = i / nu;
      final linha = <int>[];
      for (var j = 0; j <= nv; j++) {
        final v = -1 + 2 * j / nv;
        linha.add(
          m.vertice(
            material,
            ponto(u, v),
            _normal(u, v),
            uv: Offset(v * .5 + .5, u),
          ),
        );
      }
      grade.add(linha);
    }
    for (var i = 0; i < nu; i++) {
      for (var j = 0; j < nv; j++) {
        m.tri(material, grade[i][j], grade[i + 1][j], grade[i + 1][j + 1]);
        m.tri(material, grade[i][j], grade[i + 1][j + 1], grade[i][j + 1]);
      }
    }
  }
}

// ========================================================== AS TEXTURAS

/// A PETALA VISTA DE PERTO.
///
/// A cor nao e uniforme e nunca foi: a base guarda o vermelho (e onde a
/// petala e grossa e a luz nao passa), a ponta perde cor e ganha luz (e
/// onde ela e fina e o sol atravessa). Isso e o que um motor com
/// subsuperficie faria sozinho; aqui e desenhado — e o desenho e o que
/// faz a flor parecer viva em vez de pintada.
String _texturaPetala() => pngDataUri(192, 192, (x, y, rgb) {
  final u = x / 191.0; // atravessa a largura
  final v = y / 191.0; // base -> ponta
  final grao = fbm(u * 26, v * 9, oitavas: 3, semente: 7);
  // Veios abrindo em leque da base para a ponta.
  final leque = (u - .5) * (.35 + v * 1.5);
  final veio = math.sin(leque * 46 + v * 2.4) * .5 + .5;

  final base = math.pow(1 - v, 1.6).toDouble();
  var r = .97 - base * .13 + (grao - .5) * .05;
  var g = .78 - base * .33 + (grao - .5) * .05;
  var b = .78 - base * .29 + (grao - .5) * .04;

  final k = veio * .055 * (.3 + v * .7);
  r += k;
  g += k * .8;
  b += k * .8;

  // A borda da petala e mais fina: mais clara e menos vermelha.
  final borda = math.pow((u - .5).abs() * 2, 3).toDouble();
  r += borda * .04;
  g += borda * .09;
  b += borda * .08;

  rgb[0] = canal8(r);
  rgb[1] = canal8(g);
  rgb[2] = canal8(b);
});

/// A FOLHA: verde escuro com a nervura central clara e o grao do limbo.
String _texturaFolha() => pngDataUri(128, 128, (x, y, rgb) {
  final u = x / 127.0;
  final v = y / 127.0;
  final grao = fbm(u * 18, v * 30, oitavas: 3, semente: 21);
  final centro = math.pow(1 - (u - .5).abs() * 2, 8).toDouble();
  final lateral =
      math.sin((u - .5) * 26 + v * 9) * .5 + .5; // nervuras secundarias
  var r = .13 + grao * .10 + centro * .16 + lateral * .03;
  var g = .27 + grao * .16 + centro * .20 + lateral * .05;
  var b = .11 + grao * .08 + centro * .10 + lateral * .02;
  // O topo da folha pega mais luz que a base.
  final topo = v * .07;
  r += topo;
  g += topo * 1.3;
  b += topo * .6;
  rgb[0] = canal8(r);
  rgb[1] = canal8(g);
  rgb[2] = canal8(b);
});

/// A MESA: madeira escura, fora de foco na maior parte do filme — mas
/// e ela que da a cor quente que sobe por baixo da flor.
String _texturaMesa() => pngDataUri(160, 160, (x, y, rgb) {
  final u = x / 159.0;
  final v = y / 159.0;
  // Veio de madeira: linhas esticadas num eixo.
  final anel = math.sin((v * 7 + fbm(u * 3, v * 22, oitavas: 3) * 2.4) * 6.2);
  final grao = fbm(u * 40, v * 6, oitavas: 4, semente: 3);
  final k = .5 + anel * .5;
  var r = .17 + k * .07 + grao * .05;
  var g = .12 + k * .05 + grao * .04;
  var b = .085 + k * .03 + grao * .025;
  rgb[0] = canal8(r);
  rgb[1] = canal8(g);
  rgb[2] = canal8(b);
});

// ============================================================ A FLOR

/// Tres tons de petala, do miolo para fora.
///
/// A rosa nao tem UMA cor: o miolo e fundo e saturado (a luz nao entra
/// onde a petala e grossa e esta encoberta) e a borda de fora e quase
/// creme (fina, e o sol atravessa). Isso seria trabalho de textura — mas
/// o pintor em CPU projeta imagem por CAIXA, ignorando as coordenadas
/// que a malha carrega; so o motor em GPU as usa. Entao o degrade que
/// tem de aparecer nos dois vive aqui, no MATERIAL de cada coroa.
const _matPetalaFora = 0;
const _matPetalaMeio = 1;
const _matPetalaMiolo = 2;
const _matCentro = 3;
const _matCaule = 4;
const _matFolha = 5;
const _matOrvalho = 6;
const _matSepala = 7;

List<Map<String, dynamic>> _materiaisDaPlanta() => [
  // A de fora: quase creme. Branco puro estoura na luz e some; o rubor
  // e o que segura o volume nas altas.
  materialCodigo(
    'Petala de fora',
    0xfff6e7e1,
    rugosidade: .64,
    doisLados: true,
    imagem: _texturaPetala(),
  ),
  materialCodigo(
    'Petala do meio',
    0xffeccdc6,
    rugosidade: .60,
    doisLados: true,
    imagem: _texturaPetala(),
  ),
  materialCodigo(
    'Petala do miolo',
    0xffd9a49e,
    rugosidade: .56,
    doisLados: true,
    imagem: _texturaPetala(),
  ),
  materialCodigo('Estames', 0xffd8a63c, rugosidade: .52),
  materialCodigo('Caule', 0xff44603a, rugosidade: .60),
  materialCodigo(
    'Folha',
    0xff3d6134,
    rugosidade: .48,
    doisLados: true,
    imagem: _texturaFolha(),
  ),
  // Orvalho: quase espelho. Nao ha transparencia no motor, entao o que
  // faz a gota ser gota e o realce — liso a ponto de devolver a janela.
  materialCodigo('Orvalho', 0xfff2f7ff, rugosidade: .045, metal: .12),
  // A sepala e uma folhinha, e nao um pedaco de caule: verde mais
  // escuro, com a mesma textura de nervura da folha.
  materialCodigo(
    'Sepala',
    0xff35522c,
    rugosidade: .55,
    doisLados: true,
    imagem: _texturaFolha(),
  ),
];

/// As coroas de petalas, de dentro para fora.
///
/// Quatro coroas com passo de ouro entre elas: se as petalas de uma
/// coroa caissem em cima das da anterior, a flor viraria uma estrela
/// simetrica — que e exatamente o que nenhuma flor e.
const _coroas = <(int, double, double, double, double, double, double, double)>[
  // (petalas, raioBase, alturaBase, comprimento, largura, angIni, angFim, concha)
  // ROSA E UM CONE QUE SE ABRE, nao um prato. As de dentro sao quase
  // verticais e LONGAS: sao elas que constroem a altura do miolo. As de
  // fora nascem mais BAIXO, no receptaculo, e se deitam. Com todas as
  // coroas abrindo no mesmo angulo e na mesma altura — como estava — a
  // flor sai um disco, e disco nenhum olho aceita como rosa.
  (5, 0.7, 39.6, 9.0, 2.6, 3, 12, 1.6),
  (7, 1.5, 39.2, 10.5, 3.4, 8, 28, 1.8),
  (9, 2.5, 38.6, 11.5, 4.6, 20, 52, 2.0),
  (10, 3.6, 37.8, 12.5, 5.9, 34, 82, 2.0),
  (11, 4.8, 36.8, 13.5, 7.0, 48, 112, 1.7),
];

/// De qual tom e a coroa. As duas de dentro sao o fundo, as duas do meio
/// a transicao, a de fora o creme.
int _tomDaCoroa(int c) => switch (c) {
  0 || 1 => _matPetalaMiolo,
  2 || 3 => _matPetalaMeio,
  _ => _matPetalaFora,
};

void _emitirFlor(MalhaCodigo m) {
  for (var c = 0; c < _coroas.length; c++) {
    final (n, raio, alt, comp, larg, a0, a1, concha) = _coroas[c];
    final material = _tomDaCoroa(c);
    for (var i = 0; i < n; i++) {
      // Cada petala com a sua diferenca: sem isso sao clones, e clone
      // nao existe em planta nenhuma.
      final s = c * 31 + i * 7;
      final j1 = ruido(s) - .5;
      final j2 = ruido(s + 101) - .5;
      final j3 = ruido(s + 211) - .5;
      _Petala(
        azimute: i * 360 / n + c * 68.75 + j1 * 9,
        raioBase: raio,
        alturaBase: alt + j2 * .35,
        comprimento: comp * (1 + j1 * .10),
        largura: larg * (1 + j2 * .10),
        angIni: a0,
        angFim: a1 * (1 + j3 * .09),
        concha: concha * (1 + j1 * .16),
        // Pouca torcao: com 18°±22° as petalas saiam cisalhadas, como
        // laminas. Rosa torce, mas torce pouco.
        torcao: 9 + j2 * 12,
        ondas: .3 + j3 * .26,
        // As de dentro sao pequenas na tela: menos grade nelas. As de
        // fora sao as que se veem no macro, e ali a borda facetada
        // denuncia tudo — por isso a grade fina.
        nu: c <= 1 ? 7 : 11,
        nv: c <= 1 ? 5 : 7,
      ).emitir(m, material);
    }
  }
}

/// O CENTRO: uma cupula baixa de estames. Cupula, e nao esfera — a
/// metade de baixo nunca aparece e custaria o mesmo.
void _emitirCentro(MalhaCodigo m) {
  const raio = 1.5;
  const centro = Vec3(0, 39.5, 0);
  const nu = 10, nv = 6;
  final grade = <List<int>>[];
  for (var j = 0; j <= nv; j++) {
    final phi = (j / nv) * math.pi * .46; // so a calota
    final linha = <int>[];
    for (var i = 0; i <= nu; i++) {
      final th = i / nu * 2 * math.pi;
      // O relevo dos estames: pequeno, mas e o que pega a luz rasante.
      final rug = 1 + .12 * math.sin(th * 9) * math.sin(phi * 7);
      final n = Vec3(
        math.sin(phi) * math.cos(th),
        math.cos(phi),
        math.sin(phi) * math.sin(th),
      ).normalized;
      linha.add(m.vertice(_matCentro, centro + n * (raio * rug), n));
    }
    grade.add(linha);
  }
  for (var j = 0; j < nv; j++) {
    for (var i = 0; i < nu; i++) {
      m.tri(_matCentro, grade[j][i], grade[j + 1][i], grade[j + 1][i + 1]);
      m.tri(_matCentro, grade[j][i], grade[j + 1][i + 1], grade[j][i + 1]);
    }
  }
}

/// O CAULE, curvo. Caule reto e haste de metal: o peso da flor entorta
/// a ponta, e e essa curva que diz que ha peso ali em cima.
void _emitirCaule(MalhaCodigo m) {
  const segmentos = 12, lados = 8;
  Vec3 eixo(double s) =>
      Vec3(-1.6 * s * s + .3 * math.sin(s * 2.2), s * 39.2, 1.1 * s * s * s);
  final aneis = <List<int>>[];
  for (var i = 0; i <= segmentos; i++) {
    final s = i / segmentos;
    final p = eixo(s);
    final adiante =
        (eixo(math.min(1, s + .02)) - eixo(math.max(0, s - .02))).normalized;
    // Base de secao perpendicular ao eixo.
    final lado = Vec3(0, 0, 1).cross(adiante).normalized;
    final outro = adiante.cross(lado).normalized;
    // Afina para cima, como qualquer haste.
    final raio = 1.25 - .55 * s;
    final anel = <int>[];
    for (var j = 0; j <= lados; j++) {
      final th = j / lados * 2 * math.pi;
      final n = (lado * math.cos(th) + outro * math.sin(th)).normalized;
      anel.add(m.vertice(_matCaule, p + n * raio, n));
    }
    aneis.add(anel);
  }
  for (var i = 0; i < segmentos; i++) {
    for (var j = 0; j < lados; j++) {
      m.tri(_matCaule, aneis[i][j], aneis[i + 1][j], aneis[i + 1][j + 1]);
      m.tri(_matCaule, aneis[i][j], aneis[i + 1][j + 1], aneis[i][j + 1]);
    }
  }
}

/// AS SEPALAS: as pontinhas verdes que seguram a flor por baixo. Sem
/// elas a rosa parece plantada num palito.
void _emitirSepalas(MalhaCodigo m) {
  for (var i = 0; i < 5; i++) {
    _Petala(
      azimute: i * 72 + 14,
      raioBase: 1.4,
      alturaBase: 36.4,
      comprimento: 6.4,
      largura: 1.5,
      angIni: 96,
      angFim: 148,
      concha: .8,
      torcao: 8,
      nu: 6,
      nv: 4,
    ).emitir(m, _matSepala);
  }
}

/// AS FOLHAS. Sao petalas com outros numeros: longas, quase sem concha,
/// saindo do caule para os lados.
void _emitirFolhas(MalhaCodigo m) {
  const folhas = <(double, double, double, double)>[
    // (azimute, altura no caule, comprimento, angulo final)
    (25, 24.5, 13.0, 108),
    (155, 17.0, 11.4, 116),
    (268, 29.0, 9.6, 98),
  ];
  for (final (az, alt, comp, ang) in folhas) {
    _Petala(
      azimute: az,
      raioBase: .9,
      alturaBase: alt,
      comprimento: comp,
      largura: 3.1,
      angIni: 62,
      angFim: ang,
      concha: .55,
      torcao: 14,
      ondas: .5,
      nu: 7,
      nv: 4,
    ).emitir(m, _matFolha);
  }
}

/// O ORVALHO. Cinco gotas, pousadas em pontos calculados na SUPERFICIE
/// das petalas — nao no ar perto delas. Uma gota flutuando a um
/// milimetro da petala e a coisa que denuncia a cena inteira.
void _emitirOrvalho(MalhaCodigo m) {
  const pousos = <(int, int, double, double, double)>[
    // (coroa, indice da petala, u, v, raio)
    (3, 0, .62, -.42, .52),
    (3, 2, .74, .30, .40),
    (2, 1, .55, .46, .34),
    (3, 5, .48, -.20, .30),
    (2, 4, .68, -.35, .26),
  ];
  for (final (c, i, u, v, raio) in pousos) {
    final (n, r0, alt, comp, larg, a0, a1, concha) = _coroas[c];
    final s = c * 31 + i * 7;
    final j1 = ruido(s) - .5;
    final j2 = ruido(s + 101) - .5;
    final j3 = ruido(s + 211) - .5;
    final p = _Petala(
      azimute: i * 360 / n + c * 68.75 + j1 * 9,
      raioBase: r0,
      alturaBase: alt + j2 * .35,
      comprimento: comp * (1 + j1 * .10),
      largura: larg * (1 + j2 * .10),
      angIni: a0,
      angFim: a1 * (1 + j3 * .09),
      concha: concha * (1 + j1 * .16),
      torcao: 18 + j2 * 22,
      ondas: .35 + j3 * .3,
    );
    // Encostada: o centro afunda um pouco na petala, como agua pousada.
    final centro = p.ponto(u, v) + p._normal(u, v) * (raio * .62);
    _emitirEsfera(m, _matOrvalho, centro, raio, nu: 8, nv: 5);
  }
}

void _emitirEsfera(
  MalhaCodigo m,
  int material,
  Vec3 centro,
  double raio, {
  int nu = 10,
  int nv = 6,
  double achatamento = 1,
}) {
  final grade = <List<int>>[];
  for (var j = 0; j <= nv; j++) {
    final phi = j / nv * math.pi;
    final linha = <int>[];
    for (var i = 0; i <= nu; i++) {
      final th = i / nu * 2 * math.pi;
      final n = Vec3(
        math.sin(phi) * math.cos(th),
        math.cos(phi),
        math.sin(phi) * math.sin(th),
      );
      linha.add(
        m.vertice(
          material,
          centro + Vec3(n.x * raio, n.y * raio * achatamento, n.z * raio),
          n.normalized,
        ),
      );
    }
    grade.add(linha);
  }
  for (var j = 0; j < nv; j++) {
    for (var i = 0; i < nu; i++) {
      m.tri(material, grade[j][i], grade[j + 1][i], grade[j + 1][i + 1]);
      m.tri(material, grade[j][i], grade[j + 1][i + 1], grade[j][i + 1]);
    }
  }
}

SceneNode _planta() {
  final m = MalhaCodigo(_materiaisDaPlanta());
  _emitirCaule(m);
  _emitirFolhas(m);
  _emitirSepalas(m);
  _emitirFlor(m);
  _emitirCentro(m);
  _emitirOrvalho(m);
  return m.no('flor_planta', 'Rosa');
}

// ========================================================== O AMBIENTE

/// A MESA. Subdividida de proposito: uma luz direcional sobre dois
/// triangulos gigantes ilumina em bloco, e o degrade da janela some.
SceneNode _mesa() {
  final m = MalhaCodigo([
    materialCodigo('Mesa', 0xff2a1d14, rugosidade: .78, imagem: _texturaMesa()),
  ]);
  const lado = 520.0, n = 10;
  final grade = <List<int>>[];
  for (var i = 0; i <= n; i++) {
    final linha = <int>[];
    for (var j = 0; j <= n; j++) {
      final x = -lado / 2 + lado * i / n;
      final z = -lado / 2 + lado * j / n;
      linha.add(
        m.vertice(
          0,
          Vec3(x, 0, z),
          const Vec3(0, 1, 0),
          uv: Offset(i / n * 3, j / n * 3),
        ),
      );
    }
    grade.add(linha);
  }
  // O ENROLAMENTO IMPORTA. Na ordem (i,j) → (i+1,j) → (i+1,j+1) a
  // normal da face aponta para BAIXO, e o motor descarta o que esta de
  // costas: a mesa existia e nao aparecia — a flor pairava no preto. A
  // normal por vertice ser (0,1,0) nao salva; quem decide o descarte e
  // a ordem dos indices.
  for (var i = 0; i < n; i++) {
    for (var j = 0; j < n; j++) {
      m.tri(0, grade[i][j], grade[i + 1][j + 1], grade[i + 1][j]);
      m.tri(0, grade[i][j], grade[i][j + 1], grade[i + 1][j + 1]);
    }
  }
  return m.no('flor_mesa', 'Mesa');
}

/// POR QUE NAO HA "LUZES DE FUNDO" AQUI.
///
/// A primeira versao tinha catorze esferinhas claras atras da flor, para
/// virarem bolas de bokeh no desfoque. Nao viraram. O pintor em CPU nao
/// desfoca geometria distante — ele desenha as esferas nitidas — e o
/// passe de realce, que faz a bola, roda DEPOIS da geometria e sem teste
/// de profundidade: no macro elas apareciam POR CIMA das petalas.
///
/// O resultado eram bolinhas brancas boiando no ar. O fundo agora vem do
/// panorama do ambiente, desfocado, que e o que um quarto atras de uma
/// lente aberta realmente e.

// =========================================================== A CAMERA

Camera3D _tomada(
  String id,
  String nome,
  Vec3 Function(double) posicao,
  Vec3 Function(double) alvo,
  double lente, {
  double Function(double)? roll,
  DepthOfField? dof,
}) => Camera3D(
  id: id,
  name: nome,
  posX: _sample((t) => posicao(t).x),
  posY: _sample((t) => posicao(t).y),
  posZ: _sample((t) => posicao(t).z),
  poiX: _sample((t) => alvo(t).x),
  poiY: _sample((t) => alvo(t).y),
  poiZ: _sample((t) => alvo(t).z),
  focalLength: _ad(lente),
  rotZ: _sample(roll ?? (t) => 0),
  dof: dof,
);

/// Interpola dois pontos com uma entrada e saida suaves — o movimento
/// de base, aquele que o operador QUER fazer. O tremor entra por cima.
Vec3 _entre(Vec3 a, Vec3 b, double k) {
  final s = k.clamp(0.0, 1.0);
  final e = s * s * (3 - 2 * s);
  return a + (b - a) * e;
}

List<Camera3D> _cameras() => [
  // 01 · A LUZ CHEGA. Plano medio, 50 mm, empurrando devagar. A mao
  // esta calma aqui: quem comeca a gravar ainda esta se ajeitando.
  _tomada(
    'flor_cam_1',
    '01 · A luz chega / 50 mm',
    (t) =>
        _entre(const Vec3(-64, 56, 108), const Vec3(-49, 50, 84), t / 3.4) +
        _mao(t, 3, 1.5),
    (t) => const Vec3(-1, 41, 0) + _mao(t + .35, 17, .8),
    50,
    roll: (t) => -1.1 + _rolo(t, 41, .9),
    dof: DepthOfField(
      enabled: true,
      focusDistance: _ad(104),
      aperture: _ad(20),
      blurLevel: _ad(85),
      irisShape: IrisShape.heptagon,
      irisRoundness: _ad(60),
      highlightGain: _ad(16),
      highlightThreshold: _ad(.72),
      highlightSaturation: _ad(1.15),
    ),
  ),
  // 02 · O DETALHE. Macro de 100 mm, quase encostada, foco na borda da
  // petala com a gota. Aqui a mao aparece: a mesma derivazinha de antes
  // ocupa muito mais quadro, e e isso que da a sensacao de macro na
  // mao. O operador ate corrige — e a correcao tambem se ve.
  _tomada(
    'flor_cam_2',
    '02 · O detalhe / 100 mm',
    (t) =>
        _entre(
          const Vec3(23.5, 47.5, 33),
          const Vec3(18.5, 44.5, 26),
          (t - 3.4) / 3.4,
        ) +
        _mao(t, 61, .55),
    (t) => const Vec3(7.5, 43.5, 5.5) + _mao(t + .5, 89, .30),
    100,
    roll: (t) => .8 + _rolo(t, 7, .55),
    dof: DepthOfField(
      enabled: true,
      // A distancia ate a gota. Errar isto por pouco e o que faz um
      // macro parecer maquete: o foco tem de cair NA petala.
      focusDistance: _keys([(3.4, 21.5), (6.8, 19.5)]),
      // f/3,6 e nao f/2. A profundidade continua rasa — a petala da
      // frente nitida, a de tras ja desmanchada — mas o fundo para de
      // virar disco. Abertura demais aqui nao e mais bonito: e o quadro
      // coberto por bolas.
      aperture: _ad(28),
      blurLevel: _ad(110),
      irisShape: IrisShape.heptagon,
      irisRoundness: _ad(75),
      diffractionFringe: _ad(6),
      highlightGain: _ad(9),
      highlightThreshold: _ad(.82),
      highlightSaturation: _ad(1.15),
    ),
  ),
  // 03 · ELA RESPIRA. Recua e sobe, 35 mm. A mao solta: no fim de uma
  // tomada longa o braco cansa, e o quadro balanca mais.
  _tomada(
    'flor_cam_3',
    '03 · Ela respira / 35 mm',
    (t) =>
        _entre(
          const Vec3(-26, 42, 58),
          const Vec3(-50, 66, 104),
          (t - 6.8) / 3.2,
        ) +
        _mao(t, 131, 2.0),
    (t) =>
        _entre(const Vec3(0, 40, 0), const Vec3(-2, 47, 0), (t - 6.8) / 3.2) +
        _mao(t + .6, 149, 1.0),
    35,
    roll: (t) => .4 + _rolo(t, 97, 1.3),
    dof: DepthOfField(
      enabled: true,
      focusDistance: _keys([(6.8, 62), (10, 108)]),
      aperture: _ad(18),
      blurLevel: _ad(80),
      irisShape: IrisShape.heptagon,
      irisRoundness: _ad(60),
      highlightGain: _ad(14),
      highlightThreshold: _ad(.74),
      highlightSaturation: _ad(1.1),
    ),
  ),
];

// ============================================================= PROJETO

VideoProject buildFlorTemplate() {
  final cameras = _cameras();
  final scene = Scene3D(
    showFloorGrid: false,
    // SEM COR DE FUNDO, de proposito. O pintor pinta a cor OU o
    // panorama — a cor vence, e enquanto ela existia o quarto atras da
    // flor era um preto liso. Nulo aqui e o que deixa o ambiente
    // aparecer nos dois motores.
    background: null,
    // Interior: a sombra tem quem a preencha — parede, teto, a propria
    // mesa. Por isso o ambiente aqui e alto, ao contrario do vacuo.
    // Ambiente mais baixo do que "interior" pede, de proposito: com
    // .30 a flor saia chapada, sem lado escuro. A sombra tem de existir
    // para haver volume.
    ambient: .22,
    skyColor: const Color(0xffcdd9ea),
    groundColor: const Color(0xff2a1d14),
    environment: EnvironmentKind.interior,
    envReflect: .42,
    // O FUNDO PRECISA APARECER. Sem `showBackground` o panorama so
    // alimenta reflexo e ambiente, e atras da flor fica o preto liso —
    // que le como estudio de render, nao como quarto de manha. Desfocado
    // porque e isso que uma lente aberta faz com o fundo.
    panorama: const Panorama3D(
      preset: PanoramaPreset.interior,
      showBackground: true,
      backgroundBlur: 22,
      intensity: .92,
    ),
    fogDensity: 0,
    lights: [
      // A JANELA. Uma luz grande e macia entrando de lado e por cima —
      // e ela que modela a flor. Suavidade alta: janela nao faz sombra
      // de recorte, faz sombra que abre.
      Light3D(
        id: 'flor_janela',
        color: const Color(0xfffff1d9),
        direction: const Vec3(-.58, -.66, -.48),
        castsShadow: true,
        softness: .62,
        intensity: _keys([(0, 2.5), (6.8, 2.5), (10, 3.15)]),
      ),
      // O CONTRALUZ. Baixo e por tras: e o que acende a BORDA da petala.
      // Sem ele a flor cola no fundo escuro e vira recorte.
      Light3D(
        id: 'flor_contraluz',
        color: const Color(0xffffd8a4),
        direction: const Vec3(.30, -.16, .93),
        intensity: _keys([(0, 1.05), (6.8, 1.05), (10, 1.5)]),
      ),
      // O AZUL DE VOLTA. O ceu que entra pela janela e frio; ele cai na
      // sombra e impede que a sombra vire um buraco preto.
      Light3D(
        id: 'flor_ceu',
        color: const Color(0xffa8bfe0),
        direction: const Vec3(.66, .58, .48),
        intensity: _ad(.62),
      ),
      // O QUENTE DA MESA subindo por baixo — pouco, mas e o que amarra
      // a flor no lugar onde ela esta.
      Light3D(
        id: 'flor_mesa_bounce',
        color: const Color(0xff8a5c3a),
        direction: const Vec3(-.10, .95, .28),
        intensity: _ad(.34),
      ),
    ],
    nodes: [_mesa(), _planta()],
  );

  const centro = Offset(florWidth / 2, florHeight / 2);
  return VideoProject(
    id: 'flor_template',
    name: 'FLOR · Dez segundos de manha',
    createdAt: DateTime(2026, 9, 6),
    aspectRatio: 16 / 9,
    resolutionHeight: florHeight.round(),
    fps: florFps,
    markers: [
      for (var i = 0; i < 3; i++)
        Marker(time: _t(florTomadas[i]), label: cameras[i].name),
    ],
    layers: [
      // A GRADACAO. Manha e isto: um pouco de quente nas altas, a sombra
      // levantada de leve (luz de janela nunca fecha em preto), e grao
      // fino. O grao e o que mais engana o olho — imagem limpa demais
      // le como render, nao como filmagem.
      AdjustmentLayer(
        id: 'flor_grade',
        name: 'Gradacao · manha',
        startTime: Duration.zero,
        duration: florDuration,
        position: AnimatedOffset(centro),
        effects: [
          EffectInstance(
              type: EffectType.brightnessContrast,
              params: { 'contrast': _ad(12) },
            ),
            EffectInstance(
              type: EffectType.levels,
              params: { 'output_black': _ad(17.85), 'output_white': _ad(244.8) },
            ),
            EffectInstance(
              type: EffectType.hueSaturation,
              params: { 'master_saturation': _ad(6) },
            ),
          EffectInstance(
            type: EffectType.vignette,
            params: {
              'quantidade': _ad(.34),
              'raio': _ad(.92),
              'suavidade': _ad(.85),
            },
          ),
        ],
      ),
      Scene3DLayer(
        id: 'flor_cena',
        name: 'FLOR · cena 3D editavel',
        startTime: Duration.zero,
        duration: florDuration,
        position: AnimatedOffset(centro),
        showHelpers: false,
        camera: cameras.first,
        extraCameras: cameras.skip(1).toList(),
        shots: [
          for (var i = 0; i < 3; i++)
            CameraShot(time: _t(florTomadas[i]), cameraId: cameras[i].id),
        ],
        opacity: _keys([(0, 0), (.55, 1), (9.4, 1), (10, 0)]),
        scene: scene,
        effects: [
          // Um brilho curto e branco, so no que ja e luz: a borda da
          // petala no contraluz e as bolas do fundo. Raio pequeno de
          // proposito — halo grande le como sonho, e isto e manha.
          EffectInstance(
            type: EffectType.brilho,
            color: const Color(0xfffff2e0),
            params: {
              'threshold': _ad(80),
              'raio': _ad(16),
              'intensity': _ad(110),
              'piramide': _ad(3),
            },
          ),
        ],
      ),
    ],
  );
}

/// Triangulos por quadro, contando instancias.
int florTriangles(VideoProject p) {
  var total = 0;
  for (final layer in p.layers) {
    if (layer is! Scene3DLayer) continue;
    for (final n in layer.scene.nodes) {
      final tris = n.modelAsset?.triangleCount ?? 0;
      total += tris * math.max(1, n.instances.length);
    }
  }
  return total;
}
