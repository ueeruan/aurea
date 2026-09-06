import 'dart:ui';

import '../../editor/domain/effect.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/layer_meta.dart';
import '../../editor/domain/shape.dart';
import '../../editor/domain/video_project.dart';

/// MODELO "NOTES": a recriacao, camada por camada, de um motion de app
/// (o Notas da Apple, no estilo dnyxstudios): icone e titulo com blur
/// de movimento, botao "+" que gira e vira o pill "+ New" clicado por
/// um cursor, a lista de notas e o menu de contexto com zoom e whip,
/// a checklist de compras, a barra de ferramentas de desenho subindo
/// em cascata, o "Well Done" com a faisca azul e o outro com glow.
///
/// Tudo aqui e o que o app ja faz: forma parametrica, texto, grupo,
/// keyframe com curva (overshoot = mola), desfoque gaussiano e
/// direcional animados, sombra de camada, glow. Doze segundos, 1080 x
/// 1080, 30 fps.
///
/// A linguagem de movimento do original, em tres regras:
///   ENTRA com escala de mola + desfoque que some;
///   SAI com escala + desfoque que cresce (nunca some seco);
///   TROCA de cena com whip: deslocamento rapido + desfoque direcional.

// ------------------------------------------------------------ paleta
const _fundo = Color(0xFFE9E9EE);
const _fundoEscuro = Color(0xFF1C1C1E);
const _tinta = Color(0xFF1C1C1E);
const _cinza = Color(0xFF8E8E93);
const _cinzaClaro = Color(0xFFE3E3E8);
const _branco = Color(0xFFFFFFFF);
const _amarelo = Color(0xFFF5C400);
const _amareloEscuro = Color(0xFFE0B000);
const _azul = Color(0xFF1E7BF0);
const _azulClaro = Color(0xFF6FB0FF);

const _dur = Duration(seconds: 12);

Duration _s(double seg) => Duration(microseconds: (seg * 1e6).round());

const Easing _mola = Easing.overshoot;
const Easing _suave = Easing(x1: 0.25, y1: 0.1, x2: 0.25, y2: 1);
const Easing _entra = Easing(x1: 0.05, y1: 0.7, x2: 0.1, y2: 1);
const Easing _sai = Easing(x1: 0.7, y1: 0, x2: 0.84, y2: 0);

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

/// Escala uniforme com a MESMA trilha nos dois eixos.
({AnimatedDouble x, AnimatedDouble y}) _escala(double base,
    [List<(double, double, Easing)>? kfs]) {
  return (x: _ad(base, kfs), y: _ad(base, kfs));
}

/// ENTRADA com desfoque: o desfoque some enquanto a opacidade sobe.
List<EffectInstance> _blurIn(double t0, double t1,
        {double forca = 0.6}) =>
    [
      EffectInstance(type: EffectType.gaussianBlur, params: {
        'amount': _ad(forca, [(t0, forca, _suave), (t1, 0.0, _suave)]),
      }),
    ];

/// SAIDA com desfoque: cresce ate sumir.
List<EffectInstance> _blurOut(double t0, double t1, {double forca = 0.6}) =>
    [
      EffectInstance(type: EffectType.gaussianBlur, params: {
        'amount': _ad(0, [(t0, 0.0, _sai), (t1, forca, _sai)]),
      }),
    ];

List<EffectInstance> _blurInOut(double a0, double a1, double b0, double b1,
        {double forca = 0.6}) =>
    [
      EffectInstance(type: EffectType.gaussianBlur, params: {
        'amount': _ad(forca, [
          (a0, forca, _suave),
          (a1, 0.0, _suave),
          (b0, 0.0, _sai),
          (b1, forca, _sai),
        ]),
      }),
    ];

/// WHIP: desfoque direcional que sobe e desce com o deslocamento.
EffectInstance _whip(double t0, double t1, double angulo,
        {double comprimento = 70}) =>
    EffectInstance(type: EffectType.directionalBlur, params: {
      'comprimento': _ad(0, [
        (t0, 0.0, _suave),
        ((t0 + t1) / 2, comprimento, _suave),
        (t1, 0.0, _suave),
      ]),
      'angulo': _ad(angulo),
    });

AnimatedDouble _opacidade(List<(double, double)> pontos) => _ad(
      pontos.first.$2,
      [for (final p in pontos) (p.$1, p.$2, _suave)],
    );

ShapeLayer _retangulo({
  required String id,
  required String nome,
  required Offset centro,
  required double w,
  required double h,
  required Color cor,
  double raio = 0,
  AnimatedOffset? posicao,
  AnimatedDouble? sx,
  AnimatedDouble? sy,
  AnimatedDouble? opacidade,
  AnimatedDouble? rotacao,
  List<EffectInstance>? efeitos,
  ShapeStroke? traco,
  ShapeGradientFill? gradiente,
}) =>
    ShapeLayer(
      id: id,
      name: nome,
      startTime: Duration.zero,
      duration: _dur,
      position: posicao ?? _ao(centro),
      scaleX: sx,
      scaleY: sy,
      opacity: opacidade,
      rotation: rotacao,
      effects: efeitos,
      contents: [
        ShapeParametric(
          kind: ParamShapeKind.rect,
          sizeX: _ad(w),
          sizeY: _ad(h),
          roundness: _ad(raio),
          roundnessPercent: false,
        ),
        if (gradiente != null) gradiente else ShapeFill(color: cor),
        ?traco,
      ],
    );

ShapeLayer _circulo({
  required String id,
  required String nome,
  required Offset centro,
  required double d,
  required Color cor,
  AnimatedDouble? sx,
  AnimatedDouble? sy,
  AnimatedDouble? opacidade,
  ShapeStroke? traco,
  bool soTraco = false,
}) =>
    ShapeLayer(
      id: id,
      name: nome,
      startTime: Duration.zero,
      duration: _dur,
      position: _ao(centro),
      scaleX: sx,
      scaleY: sy,
      opacity: opacidade,
      contents: [
        ShapeParametric(
            kind: ParamShapeKind.ellipse, sizeX: _ad(d), sizeY: _ad(d)),
        if (!soTraco) ShapeFill(color: cor),
        ?traco,
      ],
    );

TextLayer _texto({
  required String id,
  required String texto,
  required Offset centro,
  required double tamanho,
  Color cor = _tinta,
  bool negrito = true,
  AnimatedOffset? posicao,
  AnimatedDouble? sx,
  AnimatedDouble? sy,
  AnimatedDouble? opacidade,
  List<EffectInstance>? efeitos,
}) =>
    TextLayer(
      id: id,
      name: texto,
      startTime: Duration.zero,
      duration: _dur,
      text: texto,
      fontSize: tamanho,
      color: cor,
      bold: negrito,
      fontFamily: 'Roboto',
      position: posicao ?? _ao(centro),
      scaleX: sx,
      scaleY: sy,
      opacity: opacidade,
      effects: efeitos,
    );

LayerMeta _sombra({double opacidade = 0.12, double dist = 10, double tam = 30}) =>
    LayerMeta(
      styles: LayerStyles(
        dropShadow: ShadowStyle(
          color: const Color(0xFF000000),
          opacity: _ad(opacidade),
          angleDeg: _ad(90),
          distance: _ad(dist),
          size: _ad(tam),
        ),
      ),
    );

/// O projeto inteiro. Camadas em ORDEM DE PILHA: a primeira e a de
/// cima; o fundo vai por ultimo.
VideoProject buildNotesMotionTemplate() {
  final camadas = <Layer>[];
  final meta = <String, LayerMeta>{};

  // ================================================== A. icone + "Notes"
  // 0,0 - 1,7 s. O icone salta (mola), o titulo desliza de tras dele
  // com desfoque; saem juntos subindo, borrados.
  {
    final entraIcone = _escala(1, [(0.0, 0.0, _mola), (0.45, 1.0, _mola),
        (1.3, 1.0, _sai), (1.7, 0.6, _sai)]);
    final grupoIcone = GroupLayer(
      id: 'a_icone',
      name: 'Icone Notes',
      startTime: Duration.zero,
      duration: _s(1.75),
      // O grupo e a composicao inteira: fica no centro, e o PIVO e o
      // que poe a mola em cima do icone (a 165 px a esquerda do centro).
      position: _ao(const Offset(540, 540), [
        (1.3, const Offset(540, 540), _sai),
        (1.7, const Offset(540, 386), _sai),
      ]),
      pivot: _ao(const Offset(-165, -6)),
      scaleX: entraIcone.x,
      scaleY: entraIcone.y,
      opacity: _opacidade([(1.35, 1), (1.7, 0)]),
      effects: [..._blurOut(1.3, 1.7, forca: 0.5), _whip(1.3, 1.7, 90)],
      children: [
        // As tres linhas do papel.
        for (var i = 0; i < 3; i++)
          _retangulo(
            id: 'a_linha$i',
            nome: 'Linha',
            centro: Offset(375, 548 + i * 22.0),
            w: 84,
            h: 4,
            raio: 2,
            cor: const Color(0xFFD8D8DC),
          ),
        // A cobertura branca: esconde a metade de baixo da faixa, que
        // e arredondada para caber nos cantos do papel. Fica no trecho
        // de lados retos do papel, entao nenhum canto vaza.
        _retangulo(
          id: 'a_cobre',
          nome: 'Cobertura',
          centro: const Offset(375, 540),
          w: 150,
          h: 36,
          cor: _branco,
        ),
        // Faixa amarela com gradiente, arredondada como o papel.
        _retangulo(
          id: 'a_faixa',
          nome: 'Faixa amarela',
          centro: const Offset(375, 506),
          w: 150,
          h: 94,
          raio: 34,
          cor: _amarelo,
          gradiente: ShapeGradientFill(
              colorA: const Color(0xFFFFE066),
              colorB: _amarelo,
              angleDeg: 90),
        ),
        _retangulo(
          id: 'a_papel',
          nome: 'Papel',
          centro: const Offset(375, 534),
          w: 150,
          h: 150,
          raio: 34,
          cor: _branco,
        ),
      ],
    );
    // A faixa e recortada pelo papel: em vez de mascara, a faixa e uma
    // segunda forma arredondada so no topo — desenhada por cima do papel.
    camadas.add(grupoIcone);
    meta['a_icone'] = _sombra(opacidade: 0.14, dist: 8, tam: 26);

    camadas.add(_texto(
      id: 'a_titulo',
      texto: 'Notes',
      centro: const Offset(640, 534),
      tamanho: 92,
      posicao: _ao(const Offset(640, 534), [
        (0.25, const Offset(470, 534), _entra),
        (0.75, const Offset(640, 534), _entra),
        (1.3, const Offset(640, 534), _sai),
        (1.7, const Offset(640, 380), _sai),
      ]),
      opacidade: _opacidade([(0.25, 0), (0.45, 1), (1.35, 1), (1.7, 0)]),
      efeitos: [
        ..._blurInOut(0.25, 0.7, 1.3, 1.7, forca: 0.5),
        _whip(0.25, 0.7, 0, comprimento: 90),
        _whip(1.3, 1.7, 90),
      ],
    ));
  }

  // ================================================ B. botao "+" e "+ New"
  // 1,5 - 4,0 s. O quadrado branco salta; o "+" gira de 45 para 0; o
  // quadrado se alarga em pill e o "New" aparece; o cursor entra e
  // clica (o pill afunda e volta); tudo sai encolhendo e borrado.
  {
    final escBotao = _escala(1, [
      (1.5, 0.0, _mola),
      (1.95, 1.0, _mola),
      (3.2, 1.0, _suave),
      (3.32, 0.92, _suave),
      (3.5, 1.0, _mola),
      (3.65, 1.0, _sai),
      (4.0, 0.7, _sai),
    ]);
    camadas.add(ShapeLayer(
      id: 'b_pill',
      name: 'Botao + New',
      startTime: Duration.zero,
      duration: _dur,
      position: _ao(const Offset(540, 540)),
      scaleX: escBotao.x,
      scaleY: escBotao.y,
      rotation: _ad(0, [
        (3.2, 0.0, _suave),
        (3.3, -2.5, _suave),
        (3.5, 0.0, _mola),
      ]),
      opacity: _opacidade([(3.65, 1), (4.0, 0)]),
      effects: _blurOut(3.65, 4.0, forca: 0.5),
      contents: [
        ShapeParametric(
          kind: ParamShapeKind.rect,
          sizeX: _ad(180, [(2.4, 180.0, _mola), (2.85, 360.0, _mola)]),
          sizeY: _ad(180, [(2.4, 180.0, _mola), (2.85, 135.0, _mola)]),
          roundness: _ad(52, [(2.4, 52.0, _suave), (2.85, 67.0, _suave)]),
          roundnessPercent: false,
        ),
        ShapeFill(color: _branco),
      ],
    ));
    meta['b_pill'] = _sombra(opacidade: 0.16, dist: 10, tam: 34);

    // O "+": amarelo e grande no quadrado, cinza e menor no pill.
    camadas.add(_texto(
      id: 'b_mais',
      texto: '+',
      centro: const Offset(540, 528),
      tamanho: 110,
      cor: _amareloEscuro,
      negrito: false,
      posicao: _ao(const Offset(540, 528), [
        (2.4, const Offset(540, 528), _mola),
        (2.85, const Offset(452, 532), _mola),
        (3.2, const Offset(452, 532), _suave),
        (3.32, const Offset(452, 536), _suave),
        (3.5, const Offset(452, 532), _mola),
      ]),
      sx: _ad(1, [(1.55, 0.0, _mola), (2.0, 1.0, _mola), (2.4, 1.0, _suave),
          (2.85, 0.6, _suave), (3.65, 0.6, _sai), (4.0, 0.4, _sai)]),
      sy: _ad(1, [(1.55, 0.0, _mola), (2.0, 1.0, _mola), (2.4, 1.0, _suave),
          (2.85, 0.6, _suave), (3.65, 0.6, _sai), (4.0, 0.4, _sai)]),
      opacidade: _opacidade([(3.65, 1), (4.0, 0)]),
      efeitos: _blurOut(3.65, 4.0, forca: 0.5),
    )..toString());
    // O "+" gira de 45 graus ate parar reto, com mola.
    camadas[camadas.length - 1] = (camadas.last as TextLayer).copyLayer(
      rotation: _ad(0, [(1.55, 45.0, _mola), (2.1, 0.0, _mola)]),
    );

    camadas.add(_texto(
      id: 'b_new',
      texto: 'New',
      centro: const Offset(600, 540),
      tamanho: 56,
      cor: _cinza,
      negrito: false,
      posicao: _ao(const Offset(600, 540), [
        (2.55, const Offset(560, 540), _entra),
        (2.95, const Offset(600, 540), _entra),
        (3.2, const Offset(600, 540), _suave),
        (3.32, const Offset(600, 544), _suave),
        (3.5, const Offset(600, 540), _mola),
      ]),
      opacidade: _opacidade([(2.55, 0), (2.85, 1), (3.65, 1), (4.0, 0)]),
      efeitos: [
        ..._blurInOut(2.55, 2.95, 3.65, 4.0, forca: 0.5),
        _whip(2.55, 2.95, 0, comprimento: 50),
      ],
    ));

    // O CURSOR: seta classica, entra de baixo/direita e clica.
    camadas.add(ShapeLayer(
      id: 'b_cursor',
      name: 'Cursor',
      startTime: Duration.zero,
      duration: _dur,
      position: _ao(const Offset(700, 640), [
        (2.7, const Offset(860, 800), _suave),
        (3.15, const Offset(690, 620), _suave),
        (3.2, const Offset(690, 620), _suave),
        (3.32, const Offset(686, 626), _suave),
        (3.5, const Offset(690, 620), _mola),
        (3.65, const Offset(690, 620), _sai),
        (4.0, const Offset(760, 720), _sai),
      ]),
      opacity: _opacidade([(2.7, 0), (2.9, 1), (3.7, 1), (4.0, 0)]),
      scaleX: _ad(1, [(3.2, 1.0, _suave), (3.32, 0.9, _suave), (3.5, 1.0, _mola)]),
      scaleY: _ad(1, [(3.2, 1.0, _suave), (3.32, 0.9, _suave), (3.5, 1.0, _mola)]),
      contents: [
        ShapeSvgPath(
          pathData:
              'M0 0 L0 17 L4.2 13 L7.2 19.5 L9.6 18.4 L6.7 12 L12 12 Z',
          size: 66,
        ),
        ShapeFill(color: _branco),
        ShapeStroke(color: _tinta, width: AnimatedDouble(4)),
      ],
    ));
    meta['b_cursor'] = _sombra(opacidade: 0.25, dist: 4, tam: 10);
  }

  // ==================================================== C. lista de notas
  // 3,9 - 6,4 s. A tela "All iCloud" entra crescendo com desfoque; no
  // fim cresce mais e some (zoom para o menu), com whip para a esquerda.
  {
    final escLista = _escala(1, [
      (3.9, 0.82, _mola),
      (4.45, 1.0, _mola),
      (5.85, 1.0, _sai),
      (6.4, 1.35, _sai),
    ]);
    Widget0 linha(String id, String titulo, String detalhe, String pasta,
        double y) {
      return Widget0([
        _texto(
            id: '${id}_t',
            texto: titulo,
            centro: Offset(300 + titulo.length * 8.6, y),
            tamanho: 24,
            negrito: true),
        _texto(
            id: '${id}_d',
            texto: detalhe,
            centro: Offset(300 + detalhe.length * 5.6, y + 30),
            tamanho: 19,
            cor: _cinza,
            negrito: false),
        _texto(
            id: '${id}_p',
            texto: pasta,
            centro: Offset(300 + pasta.length * 5.2, y + 56),
            tamanho: 18,
            cor: _cinza,
            negrito: false),
      ]);
    }

    final filhos = <Layer>[
      _texto(
          id: 'c_folders',
          texto: '‹ Folders',
          centro: const Offset(272, 246),
          tamanho: 24,
          cor: _amareloEscuro,
          negrito: false),
      _texto(
          id: 'c_titulo',
          texto: 'All iCloud',
          centro: const Offset(342, 306),
          tamanho: 46,
          negrito: true),
      _texto(
          id: 'c_search',
          texto: 'Search',
          centro: const Offset(295, 368),
          tamanho: 22,
          cor: _cinza,
          negrito: false),
      _retangulo(
          id: 'c_busca',
          nome: 'Busca',
          centro: const Offset(500, 368),
          w: 540,
          h: 44,
          raio: 14,
          cor: _cinzaClaro),
      _texto(
          id: 'c_pinned',
          texto: 'Pinned',
          centro: const Offset(268, 420),
          tamanho: 24,
          negrito: true),
      ...linha('c_l1', 'Singapore and Philippines', '8:51 AM   Supertree Grove',
              'Trips', 470)
          .layers,
      ...linha('c_l2', 'Packing', 'Thursday   Sunscreen', 'Trips', 560).layers,
      _retangulo(
          id: 'c_card1',
          nome: 'Cartao 1',
          centro: const Offset(500, 530),
          w: 540,
          h: 176,
          raio: 18,
          cor: _branco),
      _texto(
          id: 'c_prev',
          texto: 'Previous 7 Days',
          centro: const Offset(318, 652),
          tamanho: 24,
          negrito: true),
      ...linha('c_l3', 'Countries to Visit', 'Wednesday', 'Trips', 700)
          .layers,
      ...linha('c_l4', 'TV Shows to Rewatch', 'Tuesday   Santa Clarita Diet',
              'Ming House', 790)
          .layers,
      ...linha('c_l5', 'Bucket List', 'Sunday   Burn Almond Cake', 'Fooood',
              880)
          .layers,
      _retangulo(
          id: 'c_card2',
          nome: 'Cartao 2',
          centro: const Offset(500, 806),
          w: 540,
          h: 264,
          raio: 18,
          cor: _branco),
    ];
    meta['c_card1'] = _sombra(opacidade: 0.06, dist: 4, tam: 14);
    meta['c_card2'] = _sombra(opacidade: 0.06, dist: 4, tam: 14);

    camadas.add(GroupLayer(
      id: 'c_lista',
      name: 'Lista All iCloud',
      startTime: Duration.zero,
      duration: _dur,
      position: _ao(const Offset(540, 540), [
        (3.9, const Offset(540, 600), _mola),
        (4.45, const Offset(540, 540), _mola),
        (5.85, const Offset(540, 540), _sai),
        (6.4, const Offset(300, 520), _sai),
      ]),
      scaleX: escLista.x,
      scaleY: escLista.y,
      opacity: _opacidade([(3.9, 0), (4.2, 1), (5.9, 1), (6.35, 0)]),
      effects: [
        ..._blurInOut(3.9, 4.4, 5.85, 6.4, forca: 0.5),
        _whip(6.0, 6.45, 0, comprimento: 120),
      ],
      children: filhos,
    ));
  }

  // ================================================== D. menu de contexto
  // 5,0 - 6,6 s. Nasce pequeno e borrado no canto do cartao; depois
  // CRESCE ate ocupar a tela, e sai no whip para a esquerda.
  {
    final escMenu = _escala(1, [
      (5.0, 0.7, _mola),
      (5.45, 1.0, _mola),
      (5.85, 1.0, _suave),
      (6.35, 1.9, _suave),
    ]);
    final itens = [
      'Find in Note',
      'Move Note',
      'Lines & Grids',
      'Recents',
      'Add to Homescreen',
    ];
    final filhos = <Layer>[
      for (final (i, rot) in ['Archive', 'Copy', 'Lock'].indexed) ...[
        _retangulo(
            id: 'd_ic$i',
            nome: 'Icone',
            centro: Offset(612 + i * 100.0, 372),
            w: 22,
            h: 22,
            raio: 5,
            cor: _tinta,
            traco: null),
        _texto(
            id: 'd_ir$i',
            texto: rot,
            centro: Offset(612 + i * 100.0, 404),
            tamanho: 17,
            cor: _tinta,
            negrito: false),
      ],
      for (final (i, it) in itens.indexed) ...[
        _texto(
            id: 'd_it$i',
            texto: it,
            centro: Offset(580 + it.length * 5.4, 456 + i * 48.0),
            tamanho: 21,
            cor: _tinta,
            negrito: false),
        _retangulo(
            id: 'd_ii$i',
            nome: 'Icone item',
            centro: Offset(818, 456 + i * 48.0),
            w: 18,
            h: 18,
            raio: 4,
            cor: _cinza),
        if (i < itens.length - 1)
          _retangulo(
              id: 'd_sep$i',
              nome: 'Separador',
              centro: Offset(710, 480 + i * 48.0),
              w: 280,
              h: 1.5,
              cor: const Color(0xFFDADADF)),
      ],
      _retangulo(
          id: 'd_cartao',
          nome: 'Cartao do menu',
          centro: const Offset(712, 528),
          w: 330,
          h: 340,
          raio: 26,
          cor: const Color(0xFFF7F7F9)),
    ];
    meta['d_cartao'] = _sombra(opacidade: 0.16, dist: 12, tam: 40);

    camadas.add(GroupLayer(
      id: 'd_menu',
      name: 'Menu de contexto',
      startTime: Duration.zero,
      duration: _dur,
      position: _ao(const Offset(540, 540), [
        (5.85, const Offset(540, 540), _suave),
        (6.35, const Offset(210, 500), _suave),
        (6.45, const Offset(-200, 500), _sai),
      ]),
      scaleX: escMenu.x,
      scaleY: escMenu.y,
      opacity: _opacidade([(5.0, 0), (5.3, 1), (6.4, 1), (6.6, 0)]),
      effects: [
        ..._blurIn(5.0, 5.4, forca: 0.5),
        _whip(6.2, 6.65, 0, comprimento: 140),
      ],
      children: filhos,
    ));
  }

  // ================================================ E. Groceries List
  // 6,4 - 8,2 s. Entra pelo whip (vindo da direita), itens em cascata,
  // os tres ultimos ganham o check amarelo com mola; sai subindo.
  {
    final itens = [
      'Bananas',
      'Apples',
      'Avocados',
      'Milk',
      'Lemons',
      'Blueberries',
      'Strawberries',
      'Eggs',
      'Bread',
    ];
    final filhos = <Layer>[
      _texto(
          id: 'e_back',
          texto: '‹ Notes',
          centro: const Offset(290, 330),
          tamanho: 22,
          cor: _amareloEscuro,
          negrito: false),
      _texto(
          id: 'e_undo',
          texto: 'Undo',
          centro: const Offset(590, 330),
          tamanho: 20,
          cor: _tinta,
          negrito: false),
      _retangulo(
          id: 'e_undo_pill',
          nome: 'Undo',
          centro: const Offset(590, 330),
          w: 110,
          h: 40,
          raio: 20,
          cor: _branco),
      _texto(
          id: 'e_done',
          texto: '⊖  Done',
          centro: const Offset(790, 330),
          tamanho: 22,
          cor: _amareloEscuro,
          negrito: false),
      _texto(
          id: 'e_titulo',
          texto: 'Groceries List',
          centro: const Offset(392, 398),
          tamanho: 38,
          negrito: true),
      for (final (i, it) in itens.indexed) ...[
        _texto(
          id: 'e_it$i',
          texto: it,
          centro: Offset(330 + it.length * 6.3, 452 + i * 44.0),
          tamanho: 23,
          cor: _tinta,
          negrito: false,
          posicao: _ao(Offset(330 + it.length * 6.3, 452 + i * 44.0), [
            (6.45 + i * 0.04, Offset(430 + it.length * 6.3, 452 + i * 44.0),
                _entra),
            (6.85 + i * 0.04, Offset(330 + it.length * 6.3, 452 + i * 44.0),
                _entra),
          ]),
          opacidade: _opacidade([(6.45 + i * 0.04, 0), (6.75 + i * 0.04, 1)]),
        ),
        if (i < 6)
          _circulo(
            id: 'e_c$i',
            nome: 'Circulo',
            centro: Offset(288, 452 + i * 44.0),
            d: 24,
            cor: _branco,
            soTraco: true,
            traco: ShapeStroke(color: const Color(0xFFC7C7CC), width: AnimatedDouble(2.5)),
            opacidade: _opacidade([(6.45 + i * 0.04, 0), (6.75 + i * 0.04, 1)]),
          )
        else ...[
          _circulo(
            id: 'e_c$i',
            nome: 'Check',
            centro: Offset(288, 452 + i * 44.0),
            d: 26,
            cor: _amarelo,
            sx: _ad(1, [
              (7.2 + (i - 6) * 0.15, 0.0, _mola),
              (7.55 + (i - 6) * 0.15, 1.0, _mola),
            ]),
            sy: _ad(1, [
              (7.2 + (i - 6) * 0.15, 0.0, _mola),
              (7.55 + (i - 6) * 0.15, 1.0, _mola),
            ]),
          ),
          ShapeLayer(
            id: 'e_ck$i',
            name: 'Tique',
            startTime: Duration.zero,
            duration: _dur,
            position: _ao(Offset(288, 452 + i * 44.0)),
            scaleX: _ad(1, [
              (7.25 + (i - 6) * 0.15, 0.0, _mola),
              (7.6 + (i - 6) * 0.15, 1.0, _mola),
            ]),
            scaleY: _ad(1, [
              (7.25 + (i - 6) * 0.15, 0.0, _mola),
              (7.6 + (i - 6) * 0.15, 1.0, _mola),
            ]),
            contents: [
              ShapeSvgPath(pathData: 'M2 7 L5.5 10.5 L12 3', size: 14),
              ShapeStroke(color: _branco, width: AnimatedDouble(2.6)),
            ],
          ),
        ],
      ],
    ];
    meta['e_undo_pill'] = _sombra(opacidade: 0.08, dist: 4, tam: 12);

    camadas.add(GroupLayer(
      id: 'e_groceries',
      name: 'Groceries List',
      startTime: Duration.zero,
      duration: _dur,
      position: _ao(const Offset(540, 540), [
        (6.35, const Offset(1000, 540), _suave),
        (6.65, const Offset(540, 540), _suave),
        (7.9, const Offset(540, 540), _sai),
        (8.25, const Offset(540, 300), _sai),
      ]),
      opacity: _opacidade([(6.35, 0), (6.55, 1), (7.95, 1), (8.25, 0)]),
      effects: [
        _whip(6.35, 6.75, 0, comprimento: 120),
        ..._blurOut(7.9, 8.25, forca: 0.5),
        _whip(7.9, 8.3, 90, comprimento: 90),
      ],
      children: filhos,
    ));
  }

  // ============================================ F. ferramentas de desenho
  // 8,0 - 9,5 s. Sete ferramentas sobem em cascata com mola e descem.
  {
    final cores = [
      _tinta,
      const Color(0xFF34C759),
      const Color(0xFFFF7A9A),
      _amarelo,
      const Color(0xFFB0B0B5),
      const Color(0xFFC9C9CE),
      _azul,
    ];
    for (var i = 0; i < 7; i++) {
      final x = 300 + i * 80.0;
      final atraso = i * 0.05;
      final pos = _ao(Offset(x, 560), [
        (8.0 + atraso, Offset(x, 900), _mola),
        (8.5 + atraso, Offset(x, 560), _mola),
        (9.05 + atraso, Offset(x, 560), _sai),
        (9.4 + atraso, Offset(x, 900), _sai),
      ]);
      final op = _opacidade([
        (8.0 + atraso, 0),
        (8.15 + atraso, 1),
        (9.1 + atraso, 1),
        (9.4 + atraso, 0),
      ]);
      // Corpo da ferramenta (a regua e mais larga).
      camadas.add(_retangulo(
        id: 'f_corpo$i',
        nome: 'Ferramenta $i',
        centro: Offset(x, 560),
        w: i == 5 ? 44 : 30,
        h: 150,
        raio: 10,
        cor: i == 5 ? const Color(0xFFF4F4F6) : _branco,
        posicao: pos,
        opacidade: op,
        efeitos: [_whip(8.0 + atraso, 8.5 + atraso, 90, comprimento: 60)],
      ));
      meta['f_corpo$i'] = _sombra(opacidade: 0.14, dist: 8, tam: 22);
      // Ponta colorida (e o tipo de ferramenta).
      camadas.insert(
        camadas.length - 1,
        _retangulo(
          id: 'f_ponta$i',
          nome: 'Ponta $i',
          centro: Offset(x, 560),
          w: i == 5 ? 34 : 18,
          h: i == 5 ? 6 : 36,
          raio: 4,
          cor: cores[i],
          posicao: _ao(Offset(x, 560), [
            (8.0 + atraso, Offset(x, 900 - 62), _mola),
            (8.5 + atraso, Offset(x, 560 - 62), _mola),
            (9.05 + atraso, Offset(x, 560 - 62), _sai),
            (9.4 + atraso, Offset(x, 900 - 62), _sai),
          ]),
          opacidade: op,
        ),
      );
    }
  }

  // ======================================================= G. Well Done
  // 9,4 - 10,9 s. A faisca azul cresce com mola e pulsa; os textos
  // entram com desfoque; tudo sai crescendo e borrado.
  {
    final escFaisca = _escala(1, [
      (9.4, 0.0, _mola),
      (9.85, 1.0, _mola),
      (10.1, 1.0, _suave),
      (10.3, 1.12, _suave),
      (10.5, 1.0, _suave),
      (10.55, 1.0, _sai),
      (10.9, 1.5, _sai),
    ]);
    ShapeLayer faisca(String id, Offset c, double tam, Color cor,
            {double giro = 0}) =>
        ShapeLayer(
          id: id,
          name: 'Faisca',
          startTime: Duration.zero,
      duration: _dur,
          position: _ao(c),
          scaleX: escFaisca.x,
          scaleY: escFaisca.y,
          rotation: _ad(giro),
          opacity: _opacidade([(10.55, 1), (10.9, 0)]),
          effects: _blurOut(10.55, 10.9, forca: 0.5),
          contents: [
            ShapeParametric(
              kind: ParamShapeKind.star,
              points: _ad(4),
              outerRadius: _ad(tam),
              innerRadius: _ad(tam * 0.22),
              outerRoundness: _ad(0),
              innerRoundness: _ad(40),
            ),
            ShapeFill(color: cor),
          ],
        );
    camadas.add(faisca('g_f1', const Offset(540, 426), 68, _azul));
    camadas.add(faisca('g_f2', const Offset(486, 376), 26, _azulClaro));
    camadas.add(faisca('g_f3', const Offset(596, 382), 16, _azulClaro));

    camadas.add(_texto(
      id: 'g_titulo',
      texto: 'Well Done',
      centro: const Offset(540, 570),
      tamanho: 66,
      sx: _ad(1, [(9.5, 0.9, _mola), (9.95, 1.0, _mola), (10.55, 1.0, _sai),
          (10.9, 1.35, _sai)]),
      sy: _ad(1, [(9.5, 0.9, _mola), (9.95, 1.0, _mola), (10.55, 1.0, _sai),
          (10.9, 1.35, _sai)]),
      opacidade: _opacidade([(9.5, 0), (9.8, 1), (10.55, 1), (10.9, 0)]),
      efeitos: _blurInOut(9.5, 9.95, 10.55, 10.9, forca: 0.5),
    ));
    for (final (i, linha) in ['You finished your grocery list', 'for the day!']
        .indexed) {
      camadas.add(_texto(
        id: 'g_sub$i',
        texto: linha,
        centro: Offset(540, 630 + i * 38.0),
        tamanho: 32,
        cor: _cinza,
        negrito: false,
        sx: _ad(1, [(9.6, 0.9, _mola), (10.05, 1.0, _mola), (10.55, 1.0, _sai),
            (10.9, 1.35, _sai)]),
        sy: _ad(1, [(9.6, 0.9, _mola), (10.05, 1.0, _mola), (10.55, 1.0, _sai),
            (10.9, 1.35, _sai)]),
        opacidade: _opacidade([(9.6, 0), (9.9, 1), (10.55, 1), (10.9, 0)]),
        efeitos: _blurInOut(9.6, 10.05, 10.55, 10.9, forca: 0.5),
      ));
    }
  }

  // ========================================================= H. outro
  // 10,7 - 12,0 s. Fundo escuro entra por fade; "dnyxstudios" com glow
  // cresce devagar, entrando pelo desfoque.
  {
    camadas.add(_texto(
      id: 'h_marca',
      texto: 'dnyxstudios',
      centro: const Offset(540, 540),
      tamanho: 104,
      cor: _branco,
      sx: _ad(1, [(10.8, 0.86, _suave), (12.0, 1.06, _suave)]),
      sy: _ad(1, [(10.8, 0.86, _suave), (12.0, 1.06, _suave)]),
      opacidade: _opacidade([(10.8, 0), (11.2, 1)]),
      efeitos: [
        ..._blurIn(10.8, 11.3, forca: 0.7),
        EffectInstance(type: EffectType.lightGlow, color: _branco, params: {
          'diffusion': _ad(0.55),
          'threshold': _ad(0.35),
          'intensity': _ad(0.9),
        }),
      ],
    ));
    camadas.add(_retangulo(
      id: 'h_fundo',
      nome: 'Fundo escuro',
      centro: const Offset(540, 540),
      w: 1080,
      h: 1080,
      cor: _fundoEscuro,
      opacidade: _opacidade([(10.6, 0), (10.9, 1)]),
    ));
  }

  // ============================================================ fundo
  camadas.add(_retangulo(
    id: 'z_fundo',
    nome: 'Fundo',
    centro: const Offset(540, 540),
    w: 1080,
    h: 1080,
    cor: _fundo,
  ));

  // ORDEM DE PILHA: o pill fica ABAIXO do "+", do "New" e do cursor.
  // A lista e de cima para baixo, e o pill foi criado antes deles.
  final pill = camadas.firstWhere((l) => l.id == 'b_pill');
  camadas.remove(pill);
  camadas.insert(camadas.indexWhere((l) => l.id == 'b_cursor') + 1, pill);
  // O menu de contexto abre POR CIMA da lista.
  final menu = camadas.firstWhere((l) => l.id == 'd_menu');
  camadas.remove(menu);
  camadas.insert(camadas.indexWhere((l) => l.id == 'c_lista'), menu);

  return VideoProject(
    name: 'Notes (modelo)',
    createdAt: DateTime(2026, 9, 2),
    aspectRatio: 1,
    fps: 30,
    resolutionHeight: 1080,
    layers: camadas,
    meta: meta,
  );
}

/// Um punhado de camadas que uma funcao devolve de uma vez.
class Widget0 {
  const Widget0(this.layers);
  final List<Layer> layers;
}
