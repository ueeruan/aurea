/// O ORCAMENTO DE RECURSOS DO MOTOR 3D — e a regua com que ele se mede.
///
/// Um celular nao avisa antes de matar o app: o iOS mata por memoria
/// (jetsam) e por thread presa (watchdog) sem excecao para pegar; o
/// Metal cancela um quadro que demora demais (GPU timeout) e o motor
/// morre junto. Nada disso e "a cena e pesada demais" — e o app que
/// deixou a conta chegar ao limite sem olhar para ela.
///
/// Este arquivo e a conta. Antes de desenhar, o motor ESTIMA quanto de
/// GPU a cena vai custar em cada nivel de qualidade (alvos de render,
/// atlas de sombra, texturas, geometria) e escolhe o nivel mais alto que
/// cabe com folga no orcamento do aparelho. A estimativa e conservadora
/// de proposito: e melhor comecar um degrau abaixo e subir do que
/// comecar acima e cair.
///
/// Os numeros vem do motor de verdade (flutter_scene 0.20, Impeller):
/// cor HDR em r16g16b16a16 (8 bytes por pixel), MSAA 4x quando ligado,
/// atlas de sombra em r32g32b32a32 (16 bytes por pixel) mais profundidade,
/// dois quadros em voo no pool de texturas transitorias. Quando o motor
/// mudar, a conta muda aqui — e nos testes que a fixam.
library;

import 'dart:math' as math;

import 'element3d.dart';
import 'scene3d.dart';

/// Os niveis, do mais rico ao de emergencia. A ORDEM importa: e a escada
/// que o controlador desce um degrau por vez quando ha pressao e sobe
/// devagar quando sobra folga.
enum Qualidade3D { ultra, alta, media, baixa, emergencia }

String qualidade3dRotulo(Qualidade3D q) => switch (q) {
  Qualidade3D.ultra => 'Ultra',
  Qualidade3D.alta => 'Alta',
  Qualidade3D.media => 'Media',
  Qualidade3D.baixa => 'Baixa',
  Qualidade3D.emergencia => 'Emergencia',
};

/// O TETO que a pessoa escolhe nos Ajustes. Automatico deixa o
/// orcamento decidir; os outros cravam um maximo — util para comparar,
/// para poupar bateria, ou para forcar o melhor num aparelho forte.
enum TetoDeQualidade3D { automatico, maxima, equilibrada, leve }

String tetoDeQualidade3dRotulo(TetoDeQualidade3D t) => switch (t) {
  TetoDeQualidade3D.automatico => 'Automatica',
  TetoDeQualidade3D.maxima => 'Maxima',
  TetoDeQualidade3D.equilibrada => 'Equilibrada',
  TetoDeQualidade3D.leve => 'Leve',
};

Qualidade3D tetoComoNivel(TetoDeQualidade3D t) => switch (t) {
  TetoDeQualidade3D.automatico || TetoDeQualidade3D.maxima => Qualidade3D.ultra,
  TetoDeQualidade3D.equilibrada => Qualidade3D.media,
  TetoDeQualidade3D.leve => Qualidade3D.baixa,
};

/// A RECEITA de um nivel: cada botao do motor, com o valor daquele nivel.
///
/// A escada desce na ordem que o pedido fixou — o que custa mais e se ve
/// menos cai primeiro: MSAA e resolucao de sombra antes de escala de
/// render, escala antes de textura, textura antes de LOD.
class ReceitaDeQualidade {
  const ReceitaDeQualidade({
    required this.nivel,
    required this.escalaRender,
    required this.msaa,
    required this.sombraResolucao,
    required this.cascatas,
    required this.sombrasSpotMax,
    required this.bloom,
    required this.dof,
    required this.texturaMax,
    required this.lod,
    required this.ladoMaximoPreview,
  });

  final Qualidade3D nivel;

  /// Multiplica a resolucao do alvo de render (1 = a da composicao).
  final double escalaRender;

  /// MSAA 4x; desligado vira FXAA, que custa um alvo de 4 bytes/pixel em
  /// vez de quatro amostras de 8.
  final bool msaa;

  /// Lado do tile de sombra (0 = sem sombra).
  final int sombraResolucao;

  /// Cascatas da luz direcional.
  final int cascatas;

  /// Quantos spots podem projetar sombra ao mesmo tempo.
  final int sombrasSpotMax;

  final bool bloom;
  final bool dof;

  /// Lado maximo da textura de material subida para a GPU.
  final int texturaMax;

  /// Qual malha de um no com LODs e usada.
  final MeshLod3D lod;

  /// Lado maximo do alvo de render no preview (a exportacao nao passa por
  /// aqui: ela e limitada pelo orcamento, nao por um teto fixo).
  final int ladoMaximoPreview;

  static const ultra = ReceitaDeQualidade(
    nivel: Qualidade3D.ultra,
    escalaRender: 1.0,
    msaa: true,
    sombraResolucao: 2048,
    cascatas: 2,
    sombrasSpotMax: 4,
    bloom: true,
    dof: true,
    texturaMax: 2048,
    lod: MeshLod3D.high,
    ladoMaximoPreview: 1440,
  );

  static const alta = ReceitaDeQualidade(
    nivel: Qualidade3D.alta,
    escalaRender: 1.0,
    msaa: true,
    sombraResolucao: 1024,
    cascatas: 2,
    sombrasSpotMax: 2,
    bloom: true,
    dof: true,
    texturaMax: 1024,
    lod: MeshLod3D.auto,
    ladoMaximoPreview: 1080,
  );

  static const media = ReceitaDeQualidade(
    nivel: Qualidade3D.media,
    escalaRender: 0.85,
    msaa: false,
    sombraResolucao: 1024,
    cascatas: 2,
    sombrasSpotMax: 1,
    bloom: true,
    dof: false,
    texturaMax: 1024,
    lod: MeshLod3D.auto,
    ladoMaximoPreview: 1080,
  );

  static const baixa = ReceitaDeQualidade(
    nivel: Qualidade3D.baixa,
    escalaRender: 0.7,
    msaa: false,
    sombraResolucao: 512,
    cascatas: 1,
    sombrasSpotMax: 0,
    bloom: false,
    dof: false,
    texturaMax: 512,
    lod: MeshLod3D.medium,
    ladoMaximoPreview: 720,
  );

  /// So o que mantem a cena visivel e o app vivo.
  static const emergencia = ReceitaDeQualidade(
    nivel: Qualidade3D.emergencia,
    escalaRender: 0.5,
    msaa: false,
    sombraResolucao: 0,
    cascatas: 0,
    sombrasSpotMax: 0,
    bloom: false,
    dof: false,
    texturaMax: 256,
    lod: MeshLod3D.low,
    ladoMaximoPreview: 480,
  );

  static ReceitaDeQualidade de(Qualidade3D q) => switch (q) {
    Qualidade3D.ultra => ultra,
    Qualidade3D.alta => alta,
    Qualidade3D.media => media,
    Qualidade3D.baixa => baixa,
    Qualidade3D.emergencia => emergencia,
  };

  bool get sombras => sombraResolucao > 0 && cascatas > 0;
}

/// Quao perto do limite. As faixas tem MARGEM de proposito: reagir em
/// 100% e reagir depois de morrer.
enum NivelDePressao { seguro, alerta, pressao, emergencia }

String nivelDePressaoRotulo(NivelDePressao n) => switch (n) {
  NivelDePressao.seguro => 'seguro',
  NivelDePressao.alerta => 'alerta',
  NivelDePressao.pressao => 'pressao',
  NivelDePressao.emergencia => 'emergencia',
};

NivelDePressao pressaoDaFracao(double fracao) {
  if (fracao >= 0.85) return NivelDePressao.emergencia;
  if (fracao >= 0.70) return NivelDePressao.pressao;
  if (fracao >= 0.50) return NivelDePressao.alerta;
  return NivelDePressao.seguro;
}

/// O ORCAMENTO DE GPU do aparelho, em bytes, para as alocacoes do motor 3D.
///
/// Num celular a GPU divide a RAM com todo o resto — o Impeller das
/// camadas 2D, os decodificadores de video, o heap do Dart. O iOS mata
/// o app em primeiro plano bem antes da RAM acabar (num aparelho de 4 GB,
/// por volta de 2 GB de uso). Reservar um quinto da RAM para a cena 3D
/// deixa o resto do app respirar; o teto de 2 GB e porque acima disso o
/// ganho visual nao paga o risco.
int orcamentoGpuBytes(int ramBytes) {
  const mb = 1024 * 1024;
  if (ramBytes <= 0) return 512 * mb; // sem informacao: aparelho de 2.5 GB
  final fracao = ramBytes <= 3 * 1024 * mb ? 0.22 : 0.20;
  return (ramBytes * fracao).round().clamp(320 * mb, 2048 * mb);
}

/// A fracao do orcamento que uma escolha pode usar e ainda ser SEGURA.
/// O resto e a margem para o que a estimativa nao ve (pool do motor,
/// compilacao de pipelines, o pico de um quadro).
const double fracaoSeguraDoOrcamento = 0.60;

/// Na exportacao a folga pode ser menor: o quadro e desenhado em serie,
/// sem o preview 2D e a interface disputando a GPU ao mesmo tempo — mas
/// a captura do quadro e o codificador ainda pedem o seu.
const double fracaoSeguraDaExportacao = 0.75;

/// O que a cena pede da GPU, em bytes, por categoria.
class EstimativaGpu {
  const EstimativaGpu({
    required this.alvosDeRender,
    required this.sombras,
    required this.texturas,
    required this.ambiente,
    required this.geometria,
    required this.larguraPx,
    required this.alturaPx,
    required this.triangulos,
    required this.chamadas,
    required this.texturasContadas,
    required this.spotsComSombra,
  });

  final int alvosDeRender;
  final int sombras;
  final int texturas;
  final int ambiente;
  final int geometria;
  final int larguraPx;
  final int alturaPx;
  final int triangulos;
  final int chamadas;
  final int texturasContadas;
  final int spotsComSombra;

  int get total => alvosDeRender + sombras + texturas + ambiente + geometria;

  double fracaoDe(int orcamento) => orcamento <= 0 ? 1 : total / orcamento;

  NivelDePressao pressaoEm(int orcamento) =>
      pressaoDaFracao(fracaoDe(orcamento));

  static const vazia = EstimativaGpu(
    alvosDeRender: 0,
    sombras: 0,
    texturas: 0,
    ambiente: 0,
    geometria: 0,
    larguraPx: 0,
    alturaPx: 0,
    triangulos: 0,
    chamadas: 0,
    texturasContadas: 0,
    spotsComSombra: 0,
  );
}

/// O que a estimativa precisa saber da cena — separado da cena para a
/// conta ser barata e testavel sem montar uma [Scene3D] inteira.
class PerfilDaCena {
  const PerfilDaCena({
    required this.triangulos,
    required this.vertices,
    required this.chamadas,
    required this.texturas,
    required this.temPanoramaImagem,
    required this.spotsComSombra,
    required this.direcionalComSombra,
    required this.animada,
    required this.emissiva,
    required this.dofPedido,
  });

  /// Triangulos desenhados por quadro (ja no LOD da receita).
  final int triangulos;
  final int vertices;

  /// Primitivas (uma chamada de desenho cada).
  final int chamadas;

  /// Caminhos de textura distintos.
  final int texturas;
  final bool temPanoramaImagem;
  final int spotsComSombra;
  final bool direcionalComSombra;

  /// Alguma malha muda por quadro (modelo animado): buffers atualizaveis.
  final bool animada;
  final bool emissiva;
  final bool dofPedido;

  static const nada = PerfilDaCena(
    triangulos: 0,
    vertices: 0,
    chamadas: 0,
    texturas: 0,
    temPanoramaImagem: false,
    spotsComSombra: 0,
    direcionalComSombra: false,
    animada: false,
    emissiva: false,
    dofPedido: false,
  );

  /// Le a cena. [lod] decide qual malha de cada no conta.
  factory PerfilDaCena.de(Scene3D scene, {MeshLod3D lod = MeshLod3D.auto}) {
    var tri = 0, verts = 0, chamadas = 0;
    var animada = false, emissiva = false;
    final texturas = <String>{};
    for (final n in scene.nodes) {
      if (!n.visible || n.isNull) continue;
      final asset = n.modelAsset;
      if (asset != null) {
        tri += asset.triangleCount;
        // Modelo: um vertice por indice de canto no pior caso (faces
        // planas); os lisos compartilham, mas a conta e conservadora.
        verts += asset.triangleCount * 3;
        chamadas += asset.primitives.isEmpty ? 1 : asset.primitives.length;
        final motion = n.modelMotion;
        if (motion.keys.isNotEmpty ||
            (motion.clip >= 0 && motion.clip < asset.clips.length)) {
          animada = true;
        }
        final mats = asset.data['materials'] as List? ?? const [];
        for (final m in mats) {
          if (((m as Map)['emissive'] as num? ?? 0) > 0) emissiva = true;
          final img = m['image'] as String?;
          if (img != null && img.isNotEmpty) texturas.add(img);
        }
      } else {
        final mesh = malhaParaOrcamento(n, lod);
        final faces = mesh.faces.length;
        tri += faces;
        verts += faces * 3;
        chamadas += 1;
      }
      if (n.instances.isNotEmpty) chamadas += 0; // instanciado: uma chamada
      if (n.material.emissive > 0) emissiva = true;
      final path = n.material.imagePath;
      if (path != null && path.isNotEmpty) texturas.add(path);
      for (final p in n.material.faceImagePaths.values) {
        if (p.isNotEmpty) texturas.add(p);
      }
    }
    var spots = 0;
    var direcional = false;
    for (final l in scene.lights) {
      if (l.kind == Light3DKind.spot && l.castsShadow) spots++;
      if (l.kind == Light3DKind.directional && l.castsShadow) direcional = true;
    }
    return PerfilDaCena(
      triangulos: tri,
      vertices: verts,
      chamadas: chamadas,
      texturas: texturas.length,
      temPanoramaImagem: scene.panorama.hasImage,
      spotsComSombra: spots,
      direcionalComSombra: direcional,
      animada: animada,
      emissiva: emissiva,
      dofPedido: false,
    );
  }

  PerfilDaCena comDof(bool dof) => PerfilDaCena(
    triangulos: triangulos,
    vertices: vertices,
    chamadas: chamadas,
    texturas: texturas,
    temPanoramaImagem: temPanoramaImagem,
    spotsComSombra: spotsComSombra,
    direcionalComSombra: direcionalComSombra,
    animada: animada,
    emissiva: emissiva,
    dofPedido: dof,
  );
}

/// A malha de [node] que vale para a conta em [lod] — a mesma regra que
/// o pintor em CPU e o motor em GPU usam para desenhar.
Element3DMesh malhaParaOrcamento(SceneNode node, MeshLod3D lod) {
  final alta = node.mesh ?? element3DMesh(node.kind);
  final escolhido = switch (lod) {
    MeshLod3D.low => node.lowMesh ?? node.mediumMesh ?? node.mesh,
    MeshLod3D.medium => node.mediumMesh ?? node.mesh,
    MeshLod3D.high => node.mesh,
    MeshLod3D.auto => lodAutomatico(node, false),
  };
  return escolhido ?? alta;
}

/// O LOD automatico que o dominio ja usava, agora compartilhado: acima
/// de 150 mil triangulos vai para o baixo, acima de 60 mil para o medio.
Element3DMesh? lodAutomatico(SceneNode node, bool draftMode) {
  final high = node.mesh;
  if (draftMode) return node.lowMesh ?? node.mediumMesh ?? high;
  final triangles = high?.faces.length ?? 0;
  if (triangles > 150000) return node.lowMesh ?? node.mediumMesh ?? high;
  if (triangles > 60000) return node.mediumMesh ?? node.lowMesh ?? high;
  return high;
}

/// A RESOLUCAO DE SOMBRA QUE VALE para um alvo cujo lado maior e
/// [maiorLado]: a da receita, mas nunca mais que o alvo aproveita. Um
/// tile de 2048 num alvo de 1080 sao 335 MB de atlas para uma sombra
/// que nao fica mais nitida — 1024 basta ate 1440p, 512 ate 720p.
int sombraEfetiva(ReceitaDeQualidade receita, int maiorLado) {
  if (!receita.sombras) return 0;
  final teto = maiorLado <= 0
      ? receita.sombraResolucao
      : maiorLado > 1440
      ? 2048
      : maiorLado > 720
      ? 1024
      : 512;
  return math.min(receita.sombraResolucao, teto);
}

/// A CONTA. Tudo em bytes; as constantes sao as do motor.
EstimativaGpu estimarGpu({
  required PerfilDaCena perfil,
  required ReceitaDeQualidade receita,
  required int larguraPx,
  required int alturaPx,
}) {
  final px = math.max(0, larguraPx) * math.max(0, alturaPx);
  const quadrosEmVoo = 2;

  // ALVOS DE RENDER. Cor HDR de 8 bytes; com MSAA 4x sao quatro amostras
  // mais o resolve; profundidade de 4 bytes por amostra; os passos de
  // exibicao (tonemap, FXAA, blit) em 4 bytes.
  var porPixel = 0.0;
  if (receita.msaa) {
    porPixel += 8 * 4 + 8; // cor MSAA + resolve
    porPixel += 4 * 4; // profundidade MSAA
  } else {
    porPixel += 8; // cor
    porPixel += 4; // profundidade
    porPixel += 4; // FXAA
  }
  porPixel += 4 * 2; // passos de exibicao
  if (receita.bloom && perfil.emissiva) porPixel += 8 / 3; // cadeia de mips
  if (receita.dof && perfil.dofPedido) porPixel += 8 * 0.5 + 4; // meia res
  final alvos = (px * porPixel * quadrosEmVoo).round();

  // SOMBRAS. Um atlas por quadro: (cascatas + spots) tiles de lado
  // [sombraResolucao], cor em 16 bytes/pixel e profundidade em 4.
  var sombras = 0;
  if (receita.sombras) {
    final spots = perfil.spotsComSombra < receita.sombrasSpotMax
        ? perfil.spotsComSombra
        : receita.sombrasSpotMax;
    final tiles = (perfil.direcionalComSombra ? receita.cascatas : 0) + spots;
    final lado = sombraEfetiva(receita, math.max(larguraPx, alturaPx));
    final pxTile = lado * lado;
    sombras = pxTile * tiles * (16 + 4) * quadrosEmVoo;
  }

  // TEXTURAS. Sem saber o tamanho real antes de decodificar, a conta e
  // o teto: quadrada, RGBA, com mips (x1.33).
  final texturas =
      (perfil.texturas * receita.texturaMax * receita.texturaMax * 4 * 1.34)
          .round();

  // AMBIENTE. Panorama por imagem: cubemap de radiancia com mips e o de
  // irradiancia; o ceu procedural e pequeno.
  final ambiente = perfil.temPanoramaImagem
      ? (6 * 1024 * 1024 * 8 * 1.34).round() + 6 * 64 * 64 * 8
      : 6 * 256 * 256 * 8;

  // GEOMETRIA. 32 bytes por vertice (posicao, normal, uv) e 4 por
  // indice; malha animada vive num anel de buffers atualizaveis.
  var geometria = perfil.vertices * 32 + perfil.triangulos * 3 * 4;
  if (perfil.animada) geometria *= 3;

  return EstimativaGpu(
    alvosDeRender: alvos,
    sombras: sombras,
    texturas: texturas,
    ambiente: ambiente,
    geometria: geometria,
    larguraPx: larguraPx,
    alturaPx: alturaPx,
    triangulos: perfil.triangulos,
    chamadas: perfil.chamadas,
    texturasContadas: perfil.texturas,
    spotsComSombra: !receita.sombras
        ? 0
        : (perfil.spotsComSombra < receita.sombrasSpotMax
              ? perfil.spotsComSombra
              : receita.sombrasSpotMax),
  );
}

/// O tamanho em pixels do alvo do preview para [receita], a partir do
/// tamanho da area em pixels da composicao: escala da receita, e um teto
/// no lado maior.
({int largura, int altura}) alvoDoPreview(
  double largura,
  double altura,
  ReceitaDeQualidade receita,
) {
  if (!largura.isFinite || !altura.isFinite || largura <= 0 || altura <= 0) {
    return (largura: 0, altura: 0);
  }
  final maior = math.max(largura, altura);
  final escala = math.min(
    receita.escalaRender,
    math.min(1.0, receita.ladoMaximoPreview / maior),
  );
  return (largura: (largura * escala).ceil(), altura: (altura * escala).ceil());
}

/// A escala real (0..1) com que o preview desenha em [receita].
double escalaDoPreview(
  double largura,
  double altura,
  ReceitaDeQualidade receita,
) {
  if (!largura.isFinite || !altura.isFinite || largura <= 0 || altura <= 0) {
    return 1;
  }
  final maior = math.max(largura, altura);
  return math.min(
    receita.escalaRender,
    math.min(1.0, receita.ladoMaximoPreview / maior),
  );
}

/// A ESCALA DO ALVO DA CENA 3D NO PREVIEW — e ela NAO sabe se o video
/// esta tocando.
///
/// O teto era 720 px tocando e 1080 parado. So que o flutter_scene joga
/// fora todas as texturas de trabalho quando o tamanho do alvo muda (cor,
/// profundidade, MSAA, bloom) e cria outras enquanto as antigas ainda
/// estao em voo; o lado da sombra sai desse tamanho, entao as luzes eram
/// refeitas junto. Cada play e cada pausa era esse pico de memoria e um
/// engasgo — com um cubo so. O rascunho continua cortando o que e caro
/// (profundidade de campo, reflexos); quem baixa a resolucao quando o
/// quadro pesa e o nivel de qualidade, que muda devagar e com folga.
double escalaDoAlvoDoPreview3D(
  double largura,
  double altura,
  ReceitaDeQualidade receita,
  double resolucaoDoPreview,
) {
  final maior = math.max(largura, altura);
  final teto = !maior.isFinite || maior <= 0
      ? 1.0
      : math.min(1.0, 1080 / maior);
  return math.min(teto, escalaDoPreview(largura, altura, receita)) *
      resolucaoDoPreview.clamp(.125, 1).toDouble();
}

/// ESCOLHE o nivel mais alto que cabe na fracao segura do orcamento,
/// nunca acima de [teto]. Se nem a emergencia cabe, e emergencia mesmo:
/// o motor desenha o que der, e o controlador vai ler a pressao real.
({Qualidade3D nivel, EstimativaGpu estimativa}) escolherPeloOrcamento({
  required PerfilDaCena perfil,
  required int orcamentoBytes,
  required double larguraPx,
  required double alturaPx,
  Qualidade3D teto = Qualidade3D.ultra,
  bool exportando = false,
}) {
  EstimativaGpu? ultima;
  for (final q in Qualidade3D.values) {
    if (q.index < teto.index) continue;
    final receita = ReceitaDeQualidade.de(q);
    final alvo = exportando
        ? (largura: larguraPx.ceil(), altura: alturaPx.ceil())
        : alvoDoPreview(larguraPx, alturaPx, receita);
    final e = estimarGpu(
      perfil: perfil,
      receita: receita,
      larguraPx: alvo.largura,
      alturaPx: alvo.altura,
    );
    ultima = e;
    if (e.total <= orcamentoBytes * fracaoSeguraDoOrcamento) {
      return (nivel: q, estimativa: e);
    }
  }
  return (nivel: Qualidade3D.emergencia, estimativa: ultima!);
}

/// NA EXPORTACAO a resolucao e sagrada, mas a memoria nao e infinita: se
/// nem a receita [nivel] cabe no tamanho pedido, a escala do alvo desce
/// (0.9, 0.8, ...) ate caber — e o quadro final e ampliado de volta. Um
/// 4K com MSAA e bloom pede mais de um giga de alvos; num aparelho de
/// 4 GB isso e o app fechando no meio da exportacao.
({Qualidade3D nivel, double escala, EstimativaGpu estimativa})
receitaDeExportacao({
  required PerfilDaCena perfil,
  required int orcamentoBytes,
  required double larguraPx,
  required double alturaPx,
}) {
  // Primeiro: a receita mais rica que cabe em escala 1 — sem tocar na
  // resolucao. Ultra e alta diferem em sombra e textura, nao em pixels.
  final limite = orcamentoBytes * fracaoSeguraDaExportacao;
  for (final q in Qualidade3D.values) {
    final e = estimarGpu(
      perfil: perfil,
      receita: ReceitaDeQualidade.de(q),
      larguraPx: larguraPx.ceil(),
      alturaPx: alturaPx.ceil(),
    );
    if (e.total <= limite) return (nivel: q, escala: 1.0, estimativa: e);
  }
  // Nem a emergencia cabe em escala 1: desce a escala, na emergencia.
  var escala = 0.9;
  while (escala >= 0.3) {
    final e = estimarGpu(
      perfil: perfil,
      receita: ReceitaDeQualidade.emergencia,
      larguraPx: (larguraPx * escala).ceil(),
      alturaPx: (alturaPx * escala).ceil(),
    );
    if (e.total <= limite) {
      return (nivel: Qualidade3D.emergencia, escala: escala, estimativa: e);
    }
    escala -= 0.1;
  }
  return (
    nivel: Qualidade3D.emergencia,
    escala: 0.3,
    estimativa: estimarGpu(
      perfil: perfil,
      receita: ReceitaDeQualidade.emergencia,
      larguraPx: (larguraPx * .3).ceil(),
      alturaPx: (alturaPx * .3).ceil(),
    ),
  );
}

String bytesLegiveis(int bytes) {
  if (bytes >= 1024 * 1024 * 1024) {
    return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(2)} GB';
  }
  if (bytes >= 1024 * 1024) return '${(bytes / (1024 * 1024)).round()} MB';
  if (bytes >= 1024) return '${(bytes / 1024).round()} kB';
  return '$bytes B';
}
