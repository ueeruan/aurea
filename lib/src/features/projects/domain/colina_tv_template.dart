import 'dart:math' as math;
import 'dart:ui';

import '../../editor/domain/camera3d.dart';
import '../../editor/domain/effect.dart';
import '../../editor/domain/element3d.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/panorama3d.dart';
import '../../editor/domain/scene3d.dart';
import '../../editor/domain/shape.dart';
import '../../editor/domain/video_project.dart';
import 'malha_codigo.dart';
import 'textura_procedural.dart';

/// COLINA · A TV NO MORRO.
///
/// Recriacao de uma referencia de seis segundos: uma colina de grama com
/// flores, ceu limpo, uma TV de tubo vermelha com antena no topo e a
/// tela acesa, rochas em primeiro plano, e a camera girando devagar por
/// baixo enquanto se aproxima — com bloom, profundidade de campo e a
/// poeira no ar virando bola de bokeh.
///
/// Tudo aqui e feito com o que o motor tem: terreno como malha de
/// alturas com normais por vertice (sombreamento liso) e textura
/// procedural repetida; grama como tufos INSTANCIADOS (uma chamada por
/// variante), com a normal da ponta virada para o sol — e o contraluz
/// que acende as pontas; TV modelada em primitivas com a tela emissiva;
/// rochas com sombreamento plano; neblina para a serra ao fundo;
/// profundidade de campo com realce de bokeh; e ceu, sol e gradacao em
/// camadas 2D por tras e por cima. Toda geometria e determinista (hash
/// inteiro, sem relogio nem random), entao abrir o modelo duas vezes da
/// o mesmo.
///
/// ORCAMENTO: a cena fica abaixo de 15,5 mil triangulos por quadro
/// (ver `colinaTriangleBudget`), o que um iPhone 13 renderiza em CPU
/// perto de 30 fps. Um passe de GPU por camada, nunca por face — e a
/// regra que o pintor 3D segue desde o IPA 36.
const colinaDuration = Duration(seconds: 6);
const colinaFps = 30;
const colinaWidth = 1280.0;
const colinaHeight = 720.0;
const colinaTriangleBudget = 15500;

Duration _t(num seconds) =>
    Duration(microseconds: (seconds * 1000000).round());
AnimatedDouble _ad(double v) => AnimatedDouble(v);
AnimatedDouble _keys(List<(num, num)> values, {Easing ease = Easing.linear}) =>
    AnimatedDouble(values.first.$2.toDouble(), [
      for (final v in values)
        Keyframe(time: _t(v.$1), value: v.$2.toDouble(), ease: ease),
    ]);

/// Amostra uma funcao do tempo a cada 1/6 s: a camera e a poeira se
/// movem por curva continua, e o editor ve keyframes de verdade.
AnimatedDouble _sample(double Function(double) f) => AnimatedDouble(f(0), [
  for (var i = 0; i <= 36; i++)
    Keyframe(time: _t(i / 6), value: f(i / 6)),
]);

/// O SOL: atras e a direita, baixo. A direcao em que a luz VIAJA; o
/// oposto e para onde uma superficie precisa olhar para acender.
const _luzDoSol = Vec3(-.42, -.30, .86);
final Vec3 _paraOSol = (_luzDoSol * -1).normalized;

// ============================================================== TERRENO

/// O RELEVO: um morro arredondado no centro, ondulacoes suaves em volta
/// e uma subida leve para o fundo. Altura em unidades de mundo (y sobe).
double colinaAltura(double x, double z) {
  final dx = x / 1.18, dz = z - 30;
  final r2 = dx * dx + dz * dz;
  final morro = 238 * math.exp(-r2 / (440 * 440));
  final ondas = 42 * (fbm(x / 720 + 3.1, z / 720 + 7.7) - .5) +
      11 * (fbm(x / 170 + 9.3, z / 170 + 1.2, semente: 4) - .5);
  final fundo = .045 * math.max(0.0, -z - 520);
  return morro + ondas + fundo;
}

Vec3 _normalDoTerreno(double x, double z) {
  const e = 6.0;
  final hx = colinaAltura(x + e, z) - colinaAltura(x - e, z);
  final hz = colinaAltura(x, z + e) - colinaAltura(x, z - e);
  return Vec3(-hx, 2 * e, -hz).normalized;
}

// ============================================================== TEXTURAS

/// O MAPA DO CHAO: uma textura so cobrindo o terreno inteiro, em
/// coordenadas de mundo. E ela que pinta as manchas de terra com borda
/// MACIA na encosta (trocar material por triangulo dava remendo
/// angular), o verde variando em duas escalas, e o campo distante mais
/// frio e escuro — a perspectiva aerea que o olho espera.
const _mapaLado = 768;
const _mapaMeio = 1500.0;

String _texturaChao() => pngDataUri(_mapaLado, _mapaLado, (px, py, rgb) {
      final x = -_mapaMeio + px * 2 * _mapaMeio / _mapaLado;
      final z = -_mapaMeio + py * 2 * _mapaMeio / _mapaLado;
      final grande = fbm(x / 520 + 3, z / 520 + 9, semente: 11);
      final fino = fbm(x / 62 + 1, z / 62 + 5, oitavas: 2, semente: 12);
      final grao = ruido(px * 7 + py * 131) - .5;
      final seco =
          math.max(0.0, fbm(x / 640 + 5, z / 640 + 2, semente: 13) - .58) *
              2.4;
      var r = .21 + .09 * (grande - .5) + .06 * (fino - .5) + .035 * grao;
      var g = .48 + .16 * (grande - .5) + .12 * (fino - .5) + .06 * grao;
      var b = .11 + .04 * (grande - .5) + .03 * (fino - .5);
      r += .26 * seco;
      g += .10 * seco;
      b -= .04 * seco;
      // TERRA na encosta: mascara de ruido com borda suave, so no anel
      // do morro, nunca no topo (a TV pousa na grama).
      final dist = math.sqrt(x * x + (z - 30) * (z - 30));
      final anel = suave(((dist - 130) / 90).clamp(0.0, 1.0)) *
          (1 - suave(((dist - 520) / 120).clamp(0.0, 1.0)));
      final mancha = fbm(x / 230 + 2, z / 230 + 8, semente: 41);
      final terra = suave(((mancha - .60) / .10).clamp(0.0, 1.0)) * anel;
      final tn = fbm(x / 40, z / 40, oitavas: 2, semente: 21);
      final tr = .40 + .16 * (tn - .5) + .05 * grao;
      final tg = .21 + .09 * (tn - .5) + .04 * grao;
      final tb = .12 + .05 * (tn - .5) + .03 * grao;
      r += (tr - r) * terra;
      g += (tg - g) * terra;
      b += (tb - b) * terra;
      // CAMPO DISTANTE: mais escuro e azulado.
      final longe = suave(((dist - 800) / 700).clamp(0.0, 1.0)) * .55;
      r += (.10 - r) * longe;
      g += (.24 - g) * longe;
      b += (.18 - b) * longe;
      rgb[0] = canal8(r);
      rgb[1] = canal8(g);
      rgb[2] = canal8(b);
    });

/// ROCHA: cinza-castanho com veios.
String _texturaRocha() => pngDataUri(128, 128, (x, y, rgb) {
      final u = x / 128, v = y / 128;
      final n = fbm(u * 5, v * 5, semente: 31);
      final veio = math.max(
          0.0, .5 - (fbm(u * 9, v * 2.5, semente: 32) - .5).abs() * 6);
      final grao = ruido(x * 53 + y * 7) - .5;
      final base = .36 + .24 * (n - .5) + .06 * grao - .10 * veio;
      rgb[0] = canal8(base + .04);
      rgb[1] = canal8(base);
      rgb[2] = canal8(base - .04);
    });

// ============================================================== NOS

SceneNode _terreno() {
  final m = MalhaCodigo([
    materialCodigo('Chao', 0xffffffff, rugosidade: .88, imagem: _texturaChao()),
  ]);
  const n = 48;
  const meio = _mapaMeio;
  const passo = 2 * meio / n;
  final idx = <int>[];
  for (var j = 0; j <= n; j++) {
    for (var i = 0; i <= n; i++) {
      final x = -meio + i * passo, z = -meio + j * passo;
      final p = Vec3(x, colinaAltura(x, z), z);
      // UV = posicao no mapa do chao (0..1 sobre o terreno inteiro).
      final uv = Offset((x + meio) / (2 * meio), (z + meio) / (2 * meio));
      idx.add(m.vertice(0, p, _normalDoTerreno(x, z), uv: uv));
    }
  }
  int at(int i, int j) => j * (n + 1) + i;
  for (var j = 0; j < n; j++) {
    for (var i = 0; i < n; i++) {
      final a = idx[at(i, j)], b = idx[at(i + 1, j)];
      final c = idx[at(i + 1, j + 1)], d = idx[at(i, j + 1)];
      // Diagonal alternada: o relevo nao ganha um vies de direcao.
      if ((i + j).isEven) {
        m.tri(0, a, c, b);
        m.tri(0, a, d, c);
      } else {
        m.tri(0, a, d, b);
        m.tri(0, b, d, c);
      }
    }
  }
  return m.no('colina_terreno', 'Colina · terreno');
}

/// A SERRA AO FUNDO: um perfil de cumes que a neblina come.
SceneNode _serra() {
  final m = MalhaCodigo([
    materialCodigo('Serra', 0xff26343f, rugosidade: 1),
  ]);
  const n = 80;
  final frente = <int>[], cume = <int>[], tras = <int>[];
  for (var i = 0; i <= n; i++) {
    final x = -4400 + i * 8800 / n;
    var h = 150 + 240 * fbm(x / 1500 + 4, 0.3, semente: 51) +
        90 * fbm(x / 420 + 1, 0.7, oitavas: 2, semente: 52);
    // Mais alta a direita, como na referencia.
    h += 260 * suave(((x - 200) / 2600).clamp(0.0, 1.0));
    frente.add(m.vertice(0, Vec3(x, 0, -2100), const Vec3(0, .6, .8)));
    cume.add(m.vertice(0, Vec3(x, h, -2750), const Vec3(0, 1, 0)));
    tras.add(m.vertice(0, Vec3(x, 0, -3500), const Vec3(0, .6, -.8)));
  }
  for (var i = 0; i < n; i++) {
    m.tri(0, frente[i], frente[i + 1], cume[i]);
    m.tri(0, frente[i + 1], cume[i + 1], cume[i]);
    m.tri(0, cume[i], cume[i + 1], tras[i]);
    m.tri(0, cume[i + 1], tras[i + 1], tras[i]);
  }
  return m.no('colina_serra', 'Serra ao fundo');
}

/// UM TUFO DE GRAMA: quatro laminas, cada uma um triangulo alto e fino,
/// inclinadas para fora. A normal da PONTA vira para o sol e a da base
/// fica para cima: e o que acende a ponta em contraluz e deixa o pe na
/// sombra — a grama de verdade em fim de tarde e isso. Metade das
/// laminas vira para o outro lado, para o tufo nao ficar uniforme.
MalhaCodigo _tufo(int variante) {
  final m = MalhaCodigo([
    materialCodigo('Grama · lamina', 0xff78a532, rugosidade: .6, doisLados: true),
  ]);
  final altura = 22.0 + 12 * ruido(variante * 7 + 1);
  const cima = Vec3(0, 1, 0);
  for (var k = 0; k < 4; k++) {
    final ang = (k / 4) * 2 * math.pi + variante * .7 + ruido(variante * 11 + k) * 1.1;
    final inclina = .22 + .40 * ruido(variante * 13 + k * 5);
    final h = altura * (.7 + .5 * ruido(variante * 17 + k * 3));
    final dir = Vec3(math.cos(ang), 0, math.sin(ang));
    final lado = Vec3(-dir.z, 0, dir.x) * 1.1;
    final base = dir * (2.5 * ruido(variante * 19 + k));
    final topo = base + dir * (h * inclina) + Vec3(0, h, 0);
    final paraLuz = k.isEven ? _paraOSol : _paraOSol * -1;
    final nBase = (cima + dir * .3).normalized;
    final nPonta = (cima * .7 + paraLuz).normalized;
    final a = base - lado - Vec3(0, altura / 2, 0);
    final b = base + lado - Vec3(0, altura / 2, 0);
    final c = topo - Vec3(0, altura / 2, 0);
    final ia = m.vertice(0, a, nBase);
    final ib = m.vertice(0, b, nBase);
    final ic = m.vertice(0, c, nPonta);
    m.tri(0, ia, ib, ic);
  }
  return m;
}

/// ONDE A GRAMA NASCE: densa no morro e no primeiro plano, rala no
/// campo, e nunca debaixo da TV nem dentro das rochas.
List<Vec3> _espalha(int semente, int quantos, double alturaDoTufo) {
  final out = <Vec3>[];
  var i = 0;
  while (out.length < quantos && i < quantos * 40) {
    final u = ruido(semente * 100003 + i * 3);
    final v = ruido(semente * 100003 + i * 3 + 1);
    final p = ruido(semente * 100003 + i * 3 + 2);
    i++;
    final x = -1500 + u * 3000, z = -1500 + v * 3000;
    final r = math.sqrt(x * x + (z - 30) * (z - 30));
    // Densidade: morro cheio, primeiro plano cheio, campo ralo.
    var densidade = math.exp(-(r / 780) * (r / 780)) * .95 + .05;
    if (z > 250) densidade += .30;
    if (z < -400) densidade *= .5;
    if (r < 105) continue; // o pe da TV
    if (_dentroDeRocha(x, z)) continue;
    if (p > densidade) continue;
    out.add(Vec3(x, colinaAltura(x, z) + alturaDoTufo / 2, z));
  }
  return out;
}

/// AS ROCHAS: x, quanto o centro afunda abaixo do chao, z, raio x,
/// raio y, raio z, semente. As duas da frente ficam ENTRE a camera e o
/// morro, fora da trajetoria dela: e o primeiro plano desfocado da
/// referencia.
const _rochas = <(double, double, double, double, double, double, int)>[
  (-640, 44, 520, 150, 95, 130, 1),
  (420, 40, 440, 122, 84, 110, 2),
  (410, 30, 130, 100, 70, 90, 3),
  (-720, 26, -180, 96, 70, 88, 4),
  (150, 12, -70, 46, 30, 42, 5),
];

double _alturaDaRocha(int k) {
  final r = _rochas[k];
  return colinaAltura(r.$1, r.$3) - r.$2;
}

bool _dentroDeRocha(double x, double z) {
  for (final r in _rochas) {
    final dx = (x - r.$1) / r.$4, dz = (z - r.$3) / r.$6;
    if (dx * dx + dz * dz < .9) return true;
  }
  return false;
}

List<SceneNode> _grama() {
  final out = <SceneNode>[];
  for (var v = 0; v < 8; v++) {
    final m = _tufo(v);
    final altura = 22.0 + 12 * ruido(v * 7 + 1);
    out.add(m.no(
      'colina_grama_$v',
      'Grama · tufos ${v + 1}',
      posicao: Vec3.zero,
      instancias: _espalha(v + 1, 215, altura),
    ));
  }
  return out;
}

/// FLORES: quatro petalas (um triangulo cada) sobre um caule fino.
MalhaCodigo _flor(int cor, int semente) {
  final m = MalhaCodigo([
    materialCodigo('Flor', cor, rugosidade: .7, brilho: .15, doisLados: true),
    materialCodigo('Caule', 0xff4f7d2a, rugosidade: .8, doisLados: true),
  ]);
  const h = 15.0;
  final centro = Vec3(0, h / 2, 0);
  m.triPlano(1, Vec3(-.7, -h / 2, 0), Vec3(.7, -h / 2, 0), centro);
  const n = Vec3(0, 1, 0);
  for (var k = 0; k < 4; k++) {
    final ang = k * math.pi / 2 + ruido(semente + k) * .4;
    final dir = Vec3(math.cos(ang), 0, math.sin(ang));
    final lado = Vec3(-dir.z, 0, dir.x) * 3.4;
    final ponta = centro + dir * 7.6 + const Vec3(0, 1.2, 0);
    final ia = m.vertice(0, centro + lado * .5 + dir * .6 + const Vec3(0, .6, 0), n);
    final ib = m.vertice(0, centro - lado * .5 + dir * .6 + const Vec3(0, .6, 0), n);
    final ic = m.vertice(0, ponta, n);
    m.tri(0, ia, ib, ic);
  }
  return m;
}

List<SceneNode> _flores() => [
      _flor(0xffe0502a, 100).no('colina_flores_vermelhas', 'Flores · laranja',
          posicao: Vec3.zero, instancias: _espalha(31, 110, 15)),
      _flor(0xfff3efe4, 200).no('colina_flores_brancas', 'Flores · brancas',
          posicao: Vec3.zero, instancias: _espalha(32, 70, 15)),
      _flor(0xfff0c030, 300).no('colina_flores_amarelas', 'Flores · amarelas',
          posicao: Vec3.zero, instancias: _espalha(33, 42, 15)),
    ];

/// ROCHA: esfera deformada por ruido, faces PLANAS (o pintor sombreia
/// por face quando nao ha normal por vertice), com a textura de pedra
/// projetada esfericamente. A cor base multiplica a textura.
SceneNode _rocha(int k) {
  final r = _rochas[k];
  final vermelha = k == 1 || k == 2;
  final m = MalhaCodigo([
    materialCodigo(
      vermelha ? 'Arenito' : 'Basalto',
      vermelha ? 0xffd8906a : 0xffe6ddcf,
      rugosidade: .95,
      imagem: _texturaRocha(),
    ),
  ]);
  const aneis = 10, lados = 16;
  final cy = _alturaDaRocha(k);
  final centro = Vec3(r.$1, cy, r.$3);
  final pontos = <List<Vec3>>[];
  for (var a = 0; a <= aneis; a++) {
    final lat = math.pi * a / aneis;
    final anel = <Vec3>[];
    for (var l = 0; l < lados; l++) {
      final lon = 2 * math.pi * l / lados;
      final d = Vec3(math.sin(lat) * math.cos(lon), math.cos(lat),
          math.sin(lat) * math.sin(lon));
      final deform = .80 +
          .36 *
              fbm(d.x * 2.2 + r.$7 * 9, d.z * 2.2 + d.y * 1.7,
                  semente: 60 + r.$7);
      anel.add(Vec3(r.$1 + d.x * r.$4 * deform, cy + d.y * r.$5 * deform,
          r.$3 + d.z * r.$6 * deform));
    }
    pontos.add(anel);
  }
  Offset uv(int a, int l) => Offset(l / lados * 2, a / aneis);
  for (var a = 0; a < aneis; a++) {
    for (var l = 0; l < lados; l++) {
      final l2 = (l + 1) % lados;
      final p00 = pontos[a][l], p01 = pontos[a][l2];
      final p10 = pontos[a + 1][l], p11 = pontos[a + 1][l2];
      // Para fora: a face vira para longe do centro da rocha.
      final fora = ((p00 + p10 + p01) * (1 / 3) - centro);
      if (a > 0) {
        m.triPlano(0, p00, p01, p10,
            ua: uv(a, l), ub: uv(a, l + 1), uc: uv(a + 1, l), virado: fora);
      }
      if (a < aneis - 1) {
        m.triPlano(0, p01, p11, p10,
            ua: uv(a, l + 1), ub: uv(a + 1, l + 1), uc: uv(a + 1, l), virado: fora);
      }
    }
  }
  return m.no('colina_rocha_$k', 'Rocha ${k + 1}');
}

/// A TV DE TUBO: caixa arredondada (superelipsoide), bisel, tela
/// abaulada e emissiva, painel de botoes, pes e a antena em V. A frente
/// e +z: e para onde a camera olha.
SceneNode _tv() {
  // A TV da referencia ocupa uns 10% da largura no fim do plano.
  const e = .72;
  const hw = 76.0 * e, hh = 58.0 * e, hd = 68.0 * e;
  final m = MalhaCodigo([
    materialCodigo('Gabinete', 0xff6a3222, rugosidade: .45, metal: .12),
    materialCodigo('Bisel', 0xff1a1210, rugosidade: .7),
    materialCodigo('Tela', 0xfffff2dc, semLuz: true, brilho: 1),
    materialCodigo('Painel', 0xff2b2725, rugosidade: .6),
    materialCodigo('Metal', 0xffb9b5ad, rugosidade: .28, metal: .7),
    materialCodigo('Borracha', 0xff15110f, rugosidade: .9),
  ]);

  // GABINETE: superelipsoide — esfera cujas direcoes viram caixa de
  // cantos redondos. Normais lisas (a direcao), que e o que da o
  // reflexo macio do plastico.
  const aneis = 14, lados = 22;
  double sup(double v, double meio) =>
      meio * v.sign * math.pow(v.abs(), .24).toDouble();
  final idx = <List<int>>[];
  final frenteDe = <List<double>>[];
  for (var a = 0; a <= aneis; a++) {
    final lat = math.pi * a / aneis;
    final anel = <int>[];
    final dz = <double>[];
    for (var l = 0; l <= lados; l++) {
      final lon = 2 * math.pi * l / lados + math.pi / lados;
      final d = Vec3(math.sin(lat) * math.cos(lon), math.cos(lat),
          math.sin(lat) * math.sin(lon));
      final p = Vec3(sup(d.x, hw), sup(d.y, hh), sup(d.z, hd));
      anel.add(m.vertice(0, p, d));
      dz.add(d.z);
    }
    idx.add(anel);
    frenteDe.add(dz);
  }
  // A TAMPA DA FRENTE fica de fora: a moldura e a tela cobrem esse
  // buraco, e sem triangulos do gabinete ali nada compete com a tela
  // na ordenacao por centroide.
  bool tampa(int a, int l) => frenteDe[a][l] > .55;
  for (var a = 0; a < aneis; a++) {
    for (var l = 0; l < lados; l++) {
      final a00 = idx[a][l], a01 = idx[a][l + 1];
      final a10 = idx[a + 1][l], a11 = idx[a + 1][l + 1];
      if (a > 0 && !(tampa(a, l) && tampa(a, l + 1) && tampa(a + 1, l))) {
        m.tri(0, a00, a01, a10);
      }
      if (a < aneis - 1 &&
          !(tampa(a, l + 1) && tampa(a + 1, l + 1) && tampa(a + 1, l))) {
        m.tri(0, a01, a11, a10);
      }
    }
  }

  // FRENTE: uma PLACA escura cobrindo a frente inteira (ela esconde o
  // gabinete: o pintor ordena por centroide, e um triangulo grande do
  // gabinete podia vencer um pedaco da tela), e a tela abaulada com
  // folga na frente da placa.
  const frente = Vec3(0, 0, 1);
  const tw = 54.0 * e, th = 42.0 * e, zf = hd + 4;
  const cx = -4.0 * e, cy = 2.0 * e; // a tela a esquerda: o painel mora a direita
  const bw = 72.0 * e, bh = 55.0 * e;
  // A MOLDURA em quatro tiras que NAO passam por baixo da tela: assim
  // nenhum triangulo dela disputa a ordenacao com a tela.
  m.quadPlano(1, Vec3(-bw, bh, zf), Vec3(bw, bh, zf), Vec3(bw, cy + th, zf),
      Vec3(-bw, cy + th, zf), virado: frente);
  m.quadPlano(1, Vec3(-bw, cy - th, zf), Vec3(bw, cy - th, zf),
      Vec3(bw, -bh, zf), Vec3(-bw, -bh, zf), virado: frente);
  m.quadPlano(1, Vec3(-bw, cy + th, zf), Vec3(cx - tw, cy + th, zf),
      Vec3(cx - tw, cy - th, zf), Vec3(-bw, cy - th, zf), virado: frente);
  m.quadPlano(1, Vec3(cx + tw, cy + th, zf), Vec3(bw, cy + th, zf),
      Vec3(bw, cy - th, zf), Vec3(cx + tw, cy - th, zf), virado: frente);
  const g = 3;
  final tela = <List<int>>[];
  for (var j = 0; j <= g; j++) {
    final linha = <int>[];
    for (var i = 0; i <= g; i++) {
      final u = i / g * 2 - 1, v = j / g * 2 - 1;
      final bojo = 4.0 * (1 - u * u * .8) * (1 - v * v * .8);
      linha.add(m.vertice(
          2, Vec3(cx + u * tw, cy - v * th, zf + 1 + bojo), frente));
    }
    tela.add(linha);
  }
  for (var j = 0; j < g; j++) {
    for (var i = 0; i < g; i++) {
      // Anti-horario visto de frente (+z): topo-esq, baixo-esq, topo-dir.
      m.tri(2, tela[j][i], tela[j + 1][i], tela[j][i + 1]);
      m.tri(2, tela[j][i + 1], tela[j + 1][i], tela[j + 1][i + 1]);
    }
  }

  // PAINEL de botoes a direita da tela, com dois botoes.
  m.quadPlano(3, Vec3(58 * e, 20 * e, zf + .6), Vec3(70 * e, 20 * e, zf + .6),
      Vec3(70 * e, -26 * e, zf + .6), Vec3(58 * e, -26 * e, zf + .6),
      virado: frente);
  for (final y in [8.0 * e, -10.0 * e]) {
    _cilindro(m, 4, Vec3(64 * e, y, zf + .6), frente, 2.8, 3.0, lados: 8);
  }

  // PES.
  for (final x in [-52.0 * e, 52.0 * e]) {
    for (final z in [-40.0 * e, 40.0 * e]) {
      _cilindro(m, 5, Vec3(x, -hh - 5, z), const Vec3(0, 1, 0), 5, 6,
          lados: 8);
    }
  }

  // ANTENA: base atras, no topo, e duas hastes em V.
  _cilindro(m, 5, Vec3(3, hh + 1, -19), const Vec3(0, 1, 0), 6.5, 5, lados: 8);
  for (final lado in [-1.0, 1.0]) {
    final dir = Vec3(lado * .36, .9, -.22).normalized;
    _cilindro(m, 4, Vec3(3, hh + 5, -19), dir, 1.1, 96, lados: 6);
    _cilindro(m, 4, Vec3(3, hh + 5, -19) + dir * 94, dir, 1.8, 4, lados: 6);
  }

  final topo = colinaAltura(0, 40);
  return m.no(
    'colina_tv',
    'TV de tubo',
    posicao: Vec3(0, topo + hh - 6, 40),
    rotY: _ad(16),
    rotZ: _ad(-4),
    rotX: _ad(-3),
  );
}

/// Cilindro de faces planas, de [base] ao longo de [eixo].
void _cilindro(MalhaCodigo m, int material, Vec3 base, Vec3 eixo, double raio,
    double comprimento, {int lados = 8}) {
  final e = eixo.normalized;
  final ref = e.y.abs() < .9 ? const Vec3(0, 1, 0) : const Vec3(1, 0, 0);
  final u = ref.cross(e).normalized, v = e.cross(u);
  final topo = base + e * comprimento;
  for (var k = 0; k < lados; k++) {
    final a0 = 2 * math.pi * k / lados, a1 = 2 * math.pi * (k + 1) / lados;
    final r0 = (u * math.cos(a0) + v * math.sin(a0)) * raio;
    final r1 = (u * math.cos(a1) + v * math.sin(a1)) * raio;
    m.quadPlano(material, base + r0, base + r1, topo + r1, topo + r0,
        virado: r0 + r1);
    m.triPlano(material, topo, topo + r0, topo + r1, virado: e);
  }
}

/// POEIRA NO AR: pontos claros sem luz, que a profundidade de campo
/// transforma nas bolas de bokeh da referencia. Sobem devagar.
SceneNode _poeira() {
  final m = MalhaCodigo([
    materialCodigo('Poeira', 0xffeadcbd, semLuz: true),
  ]);
  // Um tetraedro minusculo por particula.
  const s = .8;
  m.triPlano(0, const Vec3(-s, -s, -s), const Vec3(s, -s, -s), const Vec3(0, s, 0));
  m.triPlano(0, const Vec3(s, -s, -s), const Vec3(0, -s, s), const Vec3(0, s, 0));
  m.triPlano(0, const Vec3(0, -s, s), const Vec3(-s, -s, -s), const Vec3(0, s, 0));
  m.triPlano(0, const Vec3(-s, -s, -s), const Vec3(0, -s, s), const Vec3(s, -s, -s));
  return m.no(
    'colina_poeira',
    'Poeira · bokeh',
    posicao: Vec3.zero,
    y: _keys([(0, 0), (6, 22)]),
    // Entre a camera e o morro: fora do foco, cada grao vira bola.
    instancias: [
      for (var i = 0; i < 48; i++)
        Vec3(
          (ruido(i * 3 + 5000) - .5) * 1500,
          60 + ruido(i * 3 + 5001) * 320,
          -100 + ruido(i * 3 + 5002) * 800,
        ),
    ],
  );
}

// ============================================================== CAMERA

/// A CAMERA DA REFERENCIA: rasteira, orbitando da esquerda para a
/// direita enquanto se aproxima e sobe um pouco, com um leve roll.
/// Os numeros vem da medicao da tela da TV no video (48 -> 72 px de
/// largura = aproximacao de 1,5x; o deslocamento lateral e a orbita).
Vec3 _alvo(double t) => Vec3(
      0 + 34 * math.sin(t * .9),
      colinaAltura(0, 40) + 50 + 8 * math.sin(t * 1.3),
      40,
    );

Vec3 _posicaoDaCamera(double t) {
  final e = suave((t / 6).clamp(0.0, 1.0));
  final ang = -.52 + .92 * e;
  final r = 1190 - 410 * e;
  final y = colinaAltura(0, 40) - 120 + 130 * e + 9 * math.sin(t * 1.7);
  return Vec3(r * math.sin(ang), math.max(y, 42), 40 + r * math.cos(ang));
}

Camera3D _camera() {
  final pos = _posicaoDaCamera;
  return Camera3D(
    id: 'colina_cam',
    name: 'Camera · orbita rasteira 24 mm',
    posX: _sample((t) => pos(t).x),
    posY: _sample((t) => pos(t).y),
    posZ: _sample((t) => pos(t).z),
    poiX: _sample((t) => _alvo(t).x),
    poiY: _sample((t) => _alvo(t).y),
    poiZ: _sample((t) => _alvo(t).z),
    rotZ: _keys([(0, 0), (2, -6.5), (4, -8), (6, -3)], ease: Easing.easeInOut),
    focalLength: _ad(24),
    dof: DepthOfField(
      enabled: true,
      focusDistance: _sample((t) => (pos(t) - _alvo(t)).length),
      aperture: _ad(14),
      blurLevel: _ad(100),
      irisShape: IrisShape.hexagon,
      irisRoundness: _ad(55),
      diffractionFringe: _ad(8),
      highlightGain: _ad(22),
      // So o que e LUZ vira bola: a poeira sem luz e a tela. A grama
      // acesa em contraluz fica abaixo disto.
      highlightThreshold: _ad(.86),
      highlightSaturation: _ad(1.15),
    ),
  );
}

// ============================================================== PROJETO

VideoProject buildColinaTvTemplate() {
  final scene = Scene3D(
    showFloorGrid: false,
    ambient: .36,
    skyColor: const Color(0xff8fb6d6),
    groundColor: const Color(0xff4e3d24),
    environment: EnvironmentKind.porDoSol,
    envReflect: .45,
    panorama: const Panorama3D(
        preset: PanoramaPreset.porDoSol, rotationDegrees: 35),
    // A neblina e a cor do ceu no horizonte: a serra some nela.
    fogColor: const Color(0xff6f8ea3),
    fogDensity: .00022,
    fogStart: 600,
    lights: [
      // O SOL: atras e a direita, baixo — contraluz que acende as
      // pontas da grama e desenha a silhueta do morro.
      Light3D(
        id: 'colina_sol',
        color: const Color(0xffffd8a0),
        direction: _luzDoSol,
        intensity: _ad(1.7),
      ),
      Light3D(
        id: 'colina_ceu',
        color: const Color(0xffa9c0dc),
        direction: const Vec3(.3, -.7, -.2),
        intensity: _ad(.6),
      ),
      Light3D(
        id: 'colina_recorte',
        color: const Color(0xfffff2cf),
        direction: const Vec3(.6, -.15, .75),
        intensity: _ad(.55),
      ),
    ],
    nodes: [
      _serra(),
      _terreno(),
      for (var k = 0; k < _rochas.length; k++) _rocha(k),
      _tv(),
      ..._grama(),
      ..._flores(),
      _poeira(),
    ],
  );

  const centro = Offset(colinaWidth / 2, colinaHeight / 2);
  return VideoProject(
    id: 'colina_tv_template',
    name: 'COLINA · A TV no morro',
    createdAt: DateTime(2026, 9, 6),
    aspectRatio: 16 / 9,
    resolutionHeight: 720,
    fps: colinaFps,
    layers: [
      // GRADACAO por cima de tudo: vinheta, grao, aberracao e o
      // equilibrio quente da referencia.
      AdjustmentLayer(
        id: 'colina_grade',
        name: 'Gradacao · vinheta e grao',
        startTime: Duration.zero,
        duration: colinaDuration,
        position: AnimatedOffset(centro),
        effects: [
          EffectInstance(type: EffectType.corrections, params: {
            'exposicao': _ad(.08),
            'contraste': _ad(.12),
            'sombras': _ad(.05),
            'temperatura': _ad(.06),
            'saturacao': _ad(.18),
          }),
          EffectInstance(type: EffectType.vignette, params: {
            'quantidade': _ad(.38),
            'raio': _ad(.95),
            'suavidade': _ad(.75),
          }),
          EffectInstance(type: EffectType.filmGrain, params: {
            'intensidade': _ad(.05),
            'tamanho': _ad(1.2),
          }),
        ],
      ),
      Scene3DLayer(
        id: 'colina_cena',
        name: 'COLINA · cena 3D editavel',
        startTime: Duration.zero,
        duration: colinaDuration,
        position: AnimatedOffset(centro),
        showHelpers: false,
        camera: _camera(),
        scene: scene,
        // BLOOM: a tela da TV queima para fora da propria borda.
        effects: [
          EffectInstance(
            type: EffectType.lightGlow,
            color: const Color(0xffffe6c0),
            params: {
              'threshold': _ad(80),
              'raio': _ad(14),
              'intensity': _ad(150),
              'piramide': _ad(3),
            },
          ),
        ],
      ),
      // O SOL: um disco quente baixo a direita, quase todo atras do
      // morro, e a bruma clara ao longo do horizonte.
      ShapeLayer(
        id: 'colina_sol_halo',
        name: 'Sol · halo',
        startTime: Duration.zero,
        duration: colinaDuration,
        position: AnimatedOffset(const Offset(1080, 470)),
        opacity: _ad(.5),
        contents: [
          ShapeParametric(
            kind: ParamShapeKind.ellipse,
            sizeX: _ad(480),
            sizeY: _ad(480),
          ),
          ShapeGradientFill(
            colorA: const Color(0xfffff3d2),
            colorB: const Color(0x00ffe2b0),
            radial: true,
          ),
        ],
      ),
      ShapeLayer(
        id: 'colina_bruma',
        name: 'Bruma do horizonte',
        startTime: Duration.zero,
        duration: colinaDuration,
        position: AnimatedOffset(const Offset(760, 432)),
        opacity: _ad(.22),
        // Um circulo com degrade radial, esticado pela camada: e assim
        // que o degrade fica eliptico e chega a zero na borda.
        scaleX: _ad(6.2),
        scaleY: _ad(1),
        contents: [
          ShapeParametric(
            kind: ParamShapeKind.ellipse,
            sizeX: _ad(240),
            sizeY: _ad(240),
          ),
          ShapeGradientFill(
            colorA: const Color(0xfffff0dc),
            colorB: const Color(0x00fff0dc),
            radial: true,
          ),
        ],
      ),
      // O CEU: azul profundo em cima, claro e quente no horizonte.
      ShapeLayer(
        id: 'colina_ceu',
        name: 'Ceu',
        startTime: Duration.zero,
        duration: colinaDuration,
        position: AnimatedOffset(centro),
        contents: [
          ShapeParametric(
            kind: ParamShapeKind.rect,
            sizeX: _ad(colinaWidth + 4),
            sizeY: _ad(colinaHeight + 4),
          ),
          ShapeGradientFill(
            colorA: const Color(0xff2a4a63),
            colorB: const Color(0xff92adbf),
            extras: const [Color(0xff3f6a86)],
            angleDeg: 90,
          ),
        ],
      ),
    ],
  );
}

/// Quantos triangulos a cena pede por quadro, contando instancias — o
/// numero que o teste de orcamento segura.
int colinaTriangles(VideoProject p) {
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
