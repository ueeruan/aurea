import 'dart:async';
import 'dart:isolate';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea_render/aurea_render.dart';
import 'package:flutter/foundation.dart';

import '../domain/model_asset3d.dart';
import '../domain/scene3d.dart';
import 'fonte_de_malha.dart';
import 'motor3d_modo.dart';
import '../domain/orcamento_render.dart';

/// A CENA 3D DESENHADA PELO MOTOR NATIVO (DILIGENT).
///
/// ============================ O QUE ESTA CLASSE E =====================
/// A ponte entre o que a timeline diz e o que o motor desenha. Ela e o
/// UNICO lugar do aplicativo que conhece o motor 3D, e e por isso que o
/// preview e a exportacao desenham exatamente a mesma coisa: os dois
/// chamam [quadro] com o mesmo estado avaliado e recebem a mesma imagem.
///
/// O QUE ELA NAO E: nao ha cena propria, nem tela propria, nem estado
/// paralelo a timeline. Um modelo 3D e uma camada da composicao (§1), e a
/// cena de um quadro sai do avaliador — nos, luzes e camera sao os mesmos
/// objetos que a barra do tempo ja move.
///
/// ============================ O CAMINHO DE UM QUADRO ==================
///
///   Scene3D + RenderCamera (avaliados no tempo local)
///        -> Cena3D (a ABI do motor: camadas, luzes, camera)
///        -> Ponte3D.desenhar  (uma chamada por quadro, §39)
///        -> pixels RGBA premultiplicados
///        -> ui.Image
///
/// ============================ AS DUAS PORTAS DE GEOMETRIA =============
///
///   - ARQUIVO: o caminho do GLB/glTF/FBX/OBJ vai para o Assimp dentro do
///     motor, com esqueleto, materiais, texturas e clipes. A animacao e
///     amostrada e a pele e resolvida NA GPU (§13) — o aplicativo nunca
///     transforma vertice de modelo importado no fio da interface.
///   - MALHA PRONTA: primitivas, texto 3D e formas entram por
///     `criarModelo` como `MalhaCrua3D`, no MESMO acervo e com as mesmas
///     alcas do Assimp. O desenhador nao sabe de onde a geometria veio.
///
/// ============================ A IMPORTACAO TEM DOIS TEMPOS ============
/// Ler um GLB de 200 MB leva segundos, e segundos no fio da interface sao
/// o aplicativo travado (§26). Entao a leitura roda num isolate de
/// trabalho e o que atravessa e o ENDERECO da carga; a adocao acontece
/// aqui, num move instantaneo. Enquanto o modelo nao chega, a camada
/// desenha pelo pintor de sempre — um modelo aparece no lugar, e nao um
/// buraco.
class Motor3DNativo {
  Motor3DNativo._();

  static final Motor3DNativo instance = Motor3DNativo._();

  final Ponte3D _ponte = Ponte3D();
  final Cena3D _cena = Cena3D();
  final Map<int, CamadaDeCena3D> _camadas = <int, CamadaDeCena3D>{};
  final Map<int, LuzDeCena3D> _luzes = <int, LuzDeCena3D>{};

  /// UM MODELO POR GEOMETRIA. A chave e a identidade da malha (ou o
  /// caminho do arquivo), e nao a camada: dois objetos com o mesmo cubo
  /// dividem a mesma geometria na GPU, que e o que o acervo existe para
  /// fazer (§20).
  final Map<String, int> _alcaPorChave = <String, int>{};
  final Set<String> _pedindo = <String>{};

  /// OS AVISOS DA IMPORTACAO, por chave — o que o arquivo tinha de
  /// estranho (textura ausente, material sem nome). Vai para a ficha da
  /// camada, e nao para o console.
  final Map<String, String> _avisoDaChave = <String, String>{};

  bool _desistiu = false;
  String _motivo = '';

  /// AS IMAGENS PRONTAS, POR CHAVE DE ESTADO. A decodificacao de pixels e
  /// assincrona, entao um quadro pedido agora e entregue no seguinte —
  /// uma defasagem de um quadro, que e o preco de nao decodificar no fio
  /// do desenho. Na exportacao o quadro e esperado antes (§33), e ali nao
  /// ha defasagem nenhuma.
  ///
  /// MAIS DE UMA, e nao so a ultima: duas camadas de cena na mesma
  /// composicao pedem quadros diferentes no mesmo quadro da tela, e com
  /// um so lugar a segunda mostraria o quadro da primeira por um instante.
  /// O TETO existe porque cada imagem destas e do tamanho da composicao.
  final Map<String, ui.Image> _imagens = <String, ui.Image>{};
  final List<String> _ordemDasImagens = <String>[];
  static const _imagensGuardadas = 4;

  /// A ULTIMA IMAGEM PRONTA, de qualquer chave — o que aparece enquanto a
  /// chave pedida ainda nao voltou do decodificador.
  ui.Image? _ultima;
  Uint8List? _rascunho;
  bool _decodificando = false;
  String _chavePedida = '';
  Uint8List? _pixelsPedidos;

  void _guardarImagem(String chave, ui.Image imagem) {
    final antiga = _imagens[chave];
    if (antiga != null) {
      _ordemDasImagens.remove(chave);
      if (!identical(antiga, imagem)) antiga.dispose();
    }
    _imagens[chave] = imagem;
    _ordemDasImagens.add(chave);
    _ultima = imagem;
    while (_ordemDasImagens.length > _imagensGuardadas) {
      final fora = _ordemDasImagens.removeAt(0);
      final descartada = _imagens.remove(fora);
      if (descartada != null && !identical(descartada, _ultima)) {
        descartada.dispose();
      }
    }
  }

  /// Sobe quando uma imagem nova fica pronta. Quem desenha escuta.
  final ValueNotifier<int> revision = ValueNotifier<int>(0);

  /// O MOTOR PODE SER USADO NESTE APARELHO, AGORA?
  ///
  /// Tres perguntas, e todas precisam ser sim: a biblioteca tem a porta 3D
  /// compilada (nao tem no PC), a preferencia do dono permite a GPU, e uma
  /// queda anterior nao mandou desistir. Um "nao" aqui NAO e um erro — e o
  /// caminho antigo continuando, que e o que mantem o editor de pe (§43).
  bool get ligado =>
      !_desistiu &&
      Motor3D.disponivel &&
      (Motor3DPreferencia.instancia?.permiteGpu ?? true);

  /// POR QUE NAO, quando nao da — para a ficha e para a barra de estado.
  String get motivo {
    if (_desistiu) return _motivo;
    if (!Motor3D.disponivel) return 'esta versao nao tem o motor 3D nativo';
    final p = Motor3DPreferencia.instancia;
    if (p != null && !p.permiteGpu) return p.motivoDeNaoTentar;
    return '';
  }

  /// DESISTE DO MOTOR NESTA SESSAO. Quem chama e o desenho, quando a
  /// primeira tentativa falha: o caminho antigo volta a valer e o app
  /// continua de pe.
  void desistir(String razao) {
    if (_desistiu) return;
    _desistiu = true;
    _motivo = razao;
    debugPrint('Motor 3D nativo: desistindo — $razao');
    revision.value++;
  }

  // ========================================================= a geometria

  /// A FONTE DA MALHA DE CADA NO, com memoria — a MESMA classe que o
  /// pintor de CPU usa. Ela decide o LOD, avalia o modelo importado pelo
  /// Dart e entrega o material de cada face; trocar de motor nao pode
  /// trocar a forma, nem o material.
  final CacheDeMalhas _malhas = CacheDeMalhas();

  /// A ALCA DO MODELO DESTE NO, ou -1 enquanto ele nao esta pronto.
  ///
  /// UM NO NULO NAO TEM GEOMETRIA, e devolver -1 para ele e o certo: a
  /// camada continua existindo na cena (ela e um pai, e os filhos dela
  /// sao outras camadas), e nao desenha nada.
  int modeloDe(SceneNode no, Duration local, {Duration? fimDaCamada}) =>
      _modeloDoNo(no, local, fimDaCamada, null);

  /// O MESMO, guardando quais chaves o quadro usou: o que nao estiver na
  /// lista viva e solto no fim do quadro.
  int _modeloDoNo(
    SceneNode no,
    Duration local,
    Duration? fimDaCamada,
    Set<String>? vivas,
  ) {
    if (!ligado || no.isNull) return -1;
    final caminho = no.modelSource?.path;
    if (caminho != null && caminho.isNotEmpty) {
      return _modeloDeArquivo(caminho);
    }
    // O MODELO AVALIADO EM DART NAO TEM ARQUIVO para o Assimp ler — ele
    // veio de bytes escolhidos pelo dono, ou e um texto 3D montado letra
    // a letra. Entao a malha avaliada NESTE INSTANTE e que entra.
    final fonte = _fonteDoNo(no, local, fimDaCamada);
    if (fonte == null) return -1;
    final chave = 'm${fonte.assinatura}';
    final alca = _modeloDeMalha(chave, fonte);
    if (alca > 0) vivas?.add(chave);
    return alca;
  }

  /// A malha do no neste instante, pelo caminho canonico. Uma avaliacao
  /// que estoura (modelo corrompido, §43) NAO derruba o editor nem o
  /// motor: este no fica sem geometria neste quadro e o resto desenha.
  MalhaDoNo? _fonteDoNo(SceneNode no, Duration local, Duration? fimDaCamada) {
    try {
      return _malhas.doNo(
        no,
        local,
        // O LOD DA RECEITA quando o no esta em automatico: a mesma conta
        // que o orcamento de GPU usa para estimar a cena.
        lodDaReceita: (n) => lodAutomatico(n, false),
        assinaturaDoMaterial: assinaturaDoMaterial3D,
        fimDaCamada: fimDaCamada,
      );
    } catch (e) {
      debugPrint('Motor 3D nativo: o no "${no.name}" nao pode ser avaliado: $e');
      return null;
    }
  }

  /// O caminho já visto vira alca; o que ainda nao foi lido entra na fila.
  int _modeloDeArquivo(String caminho) {
    final chave = 'f:$caminho';
    final pronto = _alcaPorChave[chave];
    if (pronto != null) return pronto;
    if (_pedindo.contains(chave)) return -1;
    _pedindo.add(chave);
    unawaited(_carregar(chave, caminho));
    return -1;
  }

  int _modeloDeMalha(String chave, MalhaDoNo fonte) {
    // A CHAVE E A MALHA E O MATERIAL, e nao o no: dois objetos com o mesmo
    // cubo dividem a mesma geometria na GPU (§20). A assinatura ja cobre
    // malha, material e o quadro do modelo — quando ela muda, o que esta
    // na GPU nao serve mais.
    final pronto = _alcaPorChave[chave];
    if (pronto != null) return pronto;
    if (!_cria(chave, fonte)) return -1;
    return _alcaPorChave[chave] ?? -1;
  }

  bool _cria(String chave, MalhaDoNo fonte) {
    try {
      final malhas = malhasCruas3DDe(fonte);
      if (malhas.isEmpty) return false;
      final resultado = Motor3D.criarModelo(malhas);
      if (!resultado.deuCerto) {
        debugPrint(
          'Motor 3D nativo: a malha $chave nao entrou no acervo '
          '(${resultado.nomeDoErro}).',
        );
        return false;
      }
      _alcaPorChave[chave] = resultado.alca;
      return true;
    } catch (e) {
      // AQUI NAO SE DESISTE DO MOTOR: uma malha que nao entrou (memoria,
      // malha degenerada) e um objeto fora do quadro, e nao um motivo
      // para o aplicativo inteiro voltar ao pintor de CPU.
      debugPrint('Motor 3D nativo: falha ao criar a malha $chave: $e');
      return false;
    }
  }

  /// LE O ARQUIVO NA THREAD DE FUNDO E ADOTA AQUI. O endereco da carga e
  /// o unico dado que atravessa o isolate (§26).
  Future<void> _carregar(String chave, String caminho) async {
    try {
      final leitura = await Isolate.run(() {
        final carga = Motor3D.lerArquivo(caminho);
        if (carga == null) {
          return <Object?>[null, null, Motor3D.ultimoErro, Motor3D.ultimoAviso];
        }
        return <Object?>[carga.endereco, carga.relato, null, Motor3D.ultimoAviso];
      });
      if (!ligado) return;
      final endereco = leitura[0];
      if (endereco == null) {
        debugPrint('Motor 3D nativo: "$caminho" nao foi lido (${leitura[2]}).');
        return;
      }
      final resultado = Motor3D.adotar(
        Carga3D.deEndereco(endereco as int, leitura[1] as RelatoDeImportacao3D),
      );
      if (!resultado.deuCerto) {
        // O ACERVO CHEIO NAO E MOTIVO PARA DESISTIR: o modelo fica de fora
        // desta vez, o dono ve o motivo na ficha, e o resto da cena segue.
        debugPrint(
          'Motor 3D nativo: "$caminho" nao entrou no acervo '
          '(${resultado.nomeDoErro}).',
        );
        return;
      }
      _alcaPorChave[chave] = resultado.alca;
      final aviso = leitura[3];
      if (aviso is String && aviso.isNotEmpty) _avisoDaChave[chave] = aviso;
      revision.value++;
    } catch (e) {
      debugPrint('Motor 3D nativo: falha ao ler "$caminho": $e');
    } finally {
      _pedindo.remove(chave);
    }
  }

  /// A FICHA DE UM NO, para a barra de estado: quantos triangulos, quanta
  /// memoria, se ja subiu para a GPU.
  FichaDeModelo3D? fichaDe(SceneNode no) {
    if (!ligado) return null;
    final caminho = no.modelSource?.path;
    final alca = caminho != null && caminho.isNotEmpty
        ? _alcaPorChave['f:$caminho']
        : null;
    return alca == null ? null : Motor3D.ficha(alca);
  }

  String? avisoDe(SceneNode no) {
    final caminho = no.modelSource?.path;
    if (caminho == null) return null;
    return _avisoDaChave['f:$caminho'];
  }

  /// SOLTA TUDO QUE ESTE MOTOR SEGUROU. Chamado quando o projeto muda ou o
  /// editor fecha: um modelo segurado e um modelo que o acervo nao pode
  /// reaproveitar (§20).
  void limpar() {
    for (final alca in _alcaPorChave.values) {
      Motor3D.soltar(alca);
    }
    _alcaPorChave.clear();
    _pedindo.clear();
    _avisoDaChave.clear();
    _camadas.clear();
    _luzes.clear();
    _malhas.limpar();
    for (final imagem in _imagens.values) {
      imagem.dispose();
    }
    _imagens.clear();
    _ordemDasImagens.clear();
    _ultima = null;
    _ponte.esquecer();
    revision.value++;
  }

  // =========================================================== o quadro

  /// MONTA A CENA DO QUADRO a partir do que o avaliador entregou.
  ///
  /// [instanteNaAnimacao] e o tempo PARA O CLIPE, e nao o tempo da
  /// timeline: quem aplica velocidade, deslocamento e ciclo e a timeline
  /// (§12), e o motor recebe o instante ja resolvido — pedir 40 s de um
  /// clipe de 3 s nao pode virar um quadro aleatorio.
  Cena3D montar({
    required Scene3D cena,
    required RenderCamera? camera,
    required Duration local,
    required int largura,
    required int altura,
    double aspectoDaComposicao = 1,
    int sombra = 0,
    int amostras = 1,
  }) {
    // O FIM DA CAMADA VEM DA PROPRIA CENA, carimbado por quem a montou
    // (`cenaComNulosDaComposicao`): e a ancora da saida do texto animado, e
    // passar por fora seria uma segunda chance de esquecer.
    final fimDaCamada = cena.fimDaCamada;
    _cena.limpar();
    _cena.largura = largura;
    _cena.altura = altura;
    _cena.sombra = sombra;
    _cena.amostras = amostras;
    _cena.ambienteR = cena.ambient.clamp(0.0, 1.0);
    _cena.ambienteG = cena.ambient.clamp(0.0, 1.0);
    _cena.ambienteB = cena.ambient.clamp(0.0, 1.0);
    // O CEU E O CHAO DA CENA. Sao eles que dao direcao ao ambiente — sem
    // direcao um metal nao tem o que refletir e sai como uma cor chapada.
    // As cores vao em sRGB; o motor as leva ao linear com a mesma curva da
    // cor do painel.
    final ceu = cena.skyColor;
    _cena.ceuR = ceu.r;
    _cena.ceuG = ceu.g;
    _cena.ceuB = ceu.b;
    _cena.reflexoDoAmbiente = cena.envReflect.clamp(0.0, 1.0);
    final chao = cena.groundColor;
    _cena.chaoR = chao.r;
    _cena.chaoG = chao.g;
    _cena.chaoB = chao.b;

    final cam = _cena.camera;
    if (camera != null) {
      cam.posicaoX = camera.position.x;
      cam.posicaoY = camera.position.y;
      cam.posicaoZ = camera.position.z;
      cam.alvoX = camera.target.x;
      cam.alvoY = camera.target.y;
      cam.alvoZ = camera.target.z;
      cam.cimaX = camera.up.x;
      cam.cimaY = camera.up.y;
      cam.cimaZ = camera.up.z;
      cam.usarRotacao = false;
      // O FILME DO APLICATIVO E HORIZONTAL (36 mm sobre a largura da
      // composicao) e o motor quer o angulo VERTICAL: passar um pelo outro
      // deforma a perspectiva em tudo que nao for quadrado.
      final horizontal = camera.fovRadians;
      final aspecto = aspectoDaComposicao <= 1e-6 ? 1.0 : aspectoDaComposicao;
      cam.fovGraus = 2 *
          math.atan(math.tan(horizontal / 2) / aspecto) *
          180 /
          math.pi;
      cam.perto = camera.near;
      cam.longe = camera.far;
      cam.ortografica = camera.orthographic;
      cam.alturaOrtografica = camera.orthoScale;
    }

    var ordem = 0;
    final vivas = <String>{};
    for (final no in cena.nodes) {
      final instancias = no.instances;
      if (instancias.isEmpty) {
        _porNo(no, cena, local, ordem++, 1.0, Vec3.zero, fimDaCamada, vivas);
      } else {
        // AS COPIAS. O motor ainda nao tem instanciamento por GPU (§19):
        // cada copia e uma camada propria, com o deslocamento proprio. O
        // resultado na tela e o mesmo; o custo e um desenho por copia.
        for (final deslocamento in instancias) {
          _porNo(
            no,
            cena,
            local,
            ordem++,
            1.0,
            deslocamento,
            fimDaCamada,
            vivas,
          );
        }
      }
    }

    for (final luz in cena.lights) {
      final l = _luzes.putIfAbsent(ordem++, LuzDeCena3D.new);
      _mapearLuz(luz, l, local);
      _cena.luzes.add(l);
    }

    _soltarOMorto(vivas);
    return _cena;
  }

  /// SOLTA O QUE ESTE QUADRO NAO USA. Uma malha avaliada em Dart (texto
  /// 3D, modelo animado pelos ossos do aplicativo) e NOVA a cada quadro:
  /// sem esta varredura o acervo guardaria uma copia por quadro ate
  /// estourar o orcamento de memoria da GPU (§20).
  ///
  /// O QUE VEM DE ARQUIVO NAO SAI AQUI: ler e importar um GLB custa
  /// segundos, e uma camada escondida por um quadro nao pode pagar esse
  /// preco de novo. Esses ficam ate [limpar].
  void _soltarOMorto(Set<String> vivas) {
    final mortas = <String>[];
    for (final chave in _alcaPorChave.keys) {
      if (chave.startsWith('m') && !vivas.contains(chave)) mortas.add(chave);
    }
    for (final chave in mortas) {
      final alca = _alcaPorChave.remove(chave);
      if (alca != null) Motor3D.soltar(alca);
    }
  }

  void _porNo(
    SceneNode no,
    Scene3D cena,
    Duration local,
    int ordem,
    double escalaExtra,
    Vec3 deslocamento,
    Duration? fimDaCamada,
    Set<String> vivas,
  ) {
    final camada = _camadas.putIfAbsent(ordem, CamadaDeCena3D.new);
    final alca = _modeloDoNo(no, local, fimDaCamada, vivas);
    camada.alca = ordem;
    camada.modelo = alca;
    camada.camadaZ = ordem.toDouble();
    camada.visivel = no.visible;

    // A CADEIA DE PAIS e resolvida AQUI, e nao no motor: a cena do
    // aplicativo tem pais, e a camada do motor e um transform so. Um pai
    // girando com o filho deslocado faz o filho dar a volta (§10).
    final xf = resolveNodeTransform(cena, no, local);
    final escala = no.size * xf.scale * escalaExtra;
    camada.posicaoX = xf.position.x + deslocamento.x;
    camada.posicaoY = xf.position.y + deslocamento.y;
    camada.posicaoZ = xf.position.z + deslocamento.z;
    camada.rotacaoX = xf.rotX;
    camada.rotacaoY = xf.rotY;
    camada.rotacaoZ = xf.rotZ;
    camada.escalaX = escala;
    camada.escalaY = escala;
    camada.escalaZ = escala;
    camada.ancoraX = 0.5;
    camada.ancoraY = 0.5;
    camada.ancoraZ = 0.5;
    // A OPACIDADE DA CAMADA MULTIPLICA a do material (o motor faz a
    // conta), entao ela fica com o valor do controle e a malha carrega o
    // alfa que for dela.
    camada.opacidade = no.material.opacity.clamp(0.0, 1.0);
    // O MATERIAL VAI NA GEOMETRIA, e nao na camada. Um objeto com um
    // material por face so existe como varias malhas, cada uma com o
    // material dela; deixar a camada sobrescrever pintaria todas as faces
    // com a primeira cor. A tinta da camada fica BRANCA (neutra) porque
    // quem manda na cor e a malha.
    camada.cor = const Cor3D(255, 255, 255, 255);
    camada.material.ligado = false;

    // A ANIMACAO. O clipe e um indice; o instante vem do movimento, ja
    // com velocidade, deslocamento e ciclo aplicados.
    final asset = no.modelAsset;
    final clips = asset?.clips;
    final clip = no.modelMotion.clip;
    final temClipe = clips != null && clip >= 0 && clip < clips.length;
    if (!temClipe) {
      camada.animacao = -1;
      camada.tempoDaAnimacao = 0;
    } else {
      camada.animacao = clip;
      camada.tempoDaAnimacao = _instanteDaAnimacao(
        alca,
        clip,
        clips[clip].duration.inMicroseconds / 1e6,
        local.inMicroseconds / 1e6,
        no.modelMotion,
      );
    }
    _cena.camadas.add(camada);
  }

  /// O INSTANTE DENTRO DO CLIPE: velocidade, deslocamento e ciclo. O motor
  /// prende o tempo nas pontas de proposito (§12) — quem faz o clipe
  /// voltar ao comeco e esta conta, com o mesmo resultado para o preview e
  /// para a exportacao, porque nao depende de relogio nenhum (§34).
  static double _instanteDaAnimacao(
    int alca,
    int clip,
    double duracaoDoAsset,
    double segundos,
    ModelMotion3D movimento,
  ) {
    var t = segundos * movimento.speed + movimento.offset;
    var duracao = duracaoDoAsset;
    if (alca > 0) {
      final nativa = Motor3D.duracaoDaAnimacao(alca, clip);
      if (nativa > 0) duracao = nativa;
    }
    if (movimento.loop && duracao > 0) {
      t = t % duracao;
      if (t < 0) t += duracao;
    }
    return t;
  }

  void _mapearLuz(Light3D luz, LuzDeCena3D saida, Duration local) {
    // A AMBIENTE NAO E UMA LUZ DO MOTOR: ela ja viaja no `ambiente` da
    // cena, e mandar uma quarta luz seria contar a mesma luz duas vezes.
    // E uma luz de forca zero nao e uma luz acesa: a curva de intensidade
    // e a chave que liga e desliga, porque `Light3D` nao tem `visible`.
    final forca = luz.kind == Light3DKind.ambient
        ? 0.0
        : luz.intensity.valueAt(local);
    saida.ligada = forca > 0;
    saida.tipo = switch (luz.kind) {
      Light3DKind.directional => TipoDeLuz3D.direcional,
      Light3DKind.point => TipoDeLuz3D.pontual,
      Light3DKind.spot => TipoDeLuz3D.holofote,
      Light3DKind.ambient => TipoDeLuz3D.direcional,
    };
    final c = luz.color;
    saida.corR = c.r;
    saida.corG = c.g;
    saida.corB = c.b;
    saida.intensidade = forca.clamp(0.0, 16.0);
    saida.posicaoX = luz.position.x;
    saida.posicaoY = luz.position.y;
    saida.posicaoZ = luz.position.z;
    saida.direcaoX = luz.direction.x;
    saida.direcaoY = luz.direction.y;
    saida.direcaoZ = luz.direction.z;
    saida.alcance = luz.range;
    saida.anguloExternoGraus = luz.coneDegrees.clamp(0.0, 89.0);
    // O ANGULO INTERNO E O EXTERNO MENOS A PENUMBRA: o motor atenua entre
    // os dois, e um interno igual ao externo daria um circulo de borda
    // dura, que nao e o que "suavidade" quer dizer.
    saida.anguloInternoGraus = (luz.coneDegrees *
            (1 - luz.softness.clamp(0.0, 0.95)))
        .clamp(0.0, 89.0);
  }

  static Cor3D _cor(ui.Color c, int alfa) => Cor3D(
    (c.r * 255).round().clamp(0, 255),
    (c.g * 255).round().clamp(0, 255),
    (c.b * 255).round().clamp(0, 255),
    alfa.clamp(0, 255),
  );

  // ======================================================== o desenho

  /// DESENHA O QUADRO E DEVOLVE A IMAGEM PRONTA **AGORA**.
  ///
  /// A imagem devolvida pode ser a do quadro anterior: os pixels saem na
  /// hora, mas virar `ui.Image` custa um passo assincrono. Quem chama
  /// escuta [revision] e redesenha. Nulo quer dizer "nao ha 3D neste
  /// quadro" — e nao "preto": uma cena sem geometria nao entra na
  /// composicao de jeito nenhum.
  ui.Image? quadro(String chave) {
    if (!ligado) return null;
    final pronta = _imagens[chave];
    if (pronta != null) return pronta;
    // A CHAVE E DO ESTADO, E NAO DA IMAGEM: quem chama monta a chave com
    // tudo o que muda o desenho. Um pedido repetido nao redesenha.
    final pixels = _desenharAgora();
    if (pixels == null) return _ultima;
    unawaited(_decodificar(chave, pixels));
    // O QUE JA ESTA NA TELA: a chave nova volta no proximo quadro.
    return _ultima;
  }

  /// DESENHA E ESPERA A IMAGEM. E o caminho da exportacao (§33): la um
  /// quadro atrasado e um quadro ERRADO no arquivo, e nao um atraso.
  Future<ui.Image?> quadroEsperando(String chave) async {
    if (!ligado) return null;
    final pronta = _imagens[chave];
    if (pronta != null) return pronta;
    final pixels = _desenharAgora();
    if (pixels == null) return _ultima;
    return _decodificar(chave, pixels, esperar: true);
  }

  Uint8List? _desenharAgora() {
    try {
      if (!_ponte.desenhar(_cena)) return null;
      final pixels = _ponte.pixels();
      if (pixels == null) return null;
      final rascunho = _rascunho ??= Uint8List(pixels.length);
      if (rascunho.length != pixels.length) {
        _rascunho = Uint8List(pixels.length);
      }
      _rascunho!.setAll(0, pixels);
      return _rascunho;
    } catch (e) {
      desistir('$e');
      return null;
    }
  }

  /// A VISTA DOS PIXELS VALE ATE O PROXIMO DESENHO, e por isso ela e
  /// COPIADA antes de sair daqui: a decodificacao termina depois, e o
  /// quadro seguinte ja teria reescrito o buffer.
  Future<ui.Image?> _decodificar(
    String chave,
    Uint8List pixels, {
    bool esperar = false,
  }) async {
    final largura = _ponte.largura;
    final altura = _ponte.altura;
    if (largura <= 0 || altura <= 0) return null;
    final dados = pixels;
    if (!esperar && _decodificando) {
      // UM QUADRO POR VEZ. Sem isto, um aparelho lento acumularia
      // decodificacoes em fila e o atraso cresceria sem parar.
      _chavePedida = chave;
      _pixelsPedidos = dados;
      return _ultima;
    }
    _decodificando = true;
    try {
      final imagem = await _imagemDePixels(dados, largura, altura);
      if (imagem == null) return null;
      if (!ligado) {
        imagem.dispose();
        return null;
      }
      _guardarImagem(chave, imagem);
      revision.value++;
      return imagem;
    } finally {
      _decodificando = false;
      final proxima = _pixelsPedidos;
      if (proxima != null) {
        _pixelsPedidos = null;
        final chavePendente = _chavePedida;
        if (_imagens[chavePendente] == null) {
          unawaited(_decodificar(chavePendente, proxima));
        }
      }
    }
  }

  static Future<ui.Image?> _imagemDePixels(
    Uint8List pixels,
    int largura,
    int altura,
  ) {
    final completer = Completer<ui.Image?>();
    ui.decodeImageFromPixels(
      pixels,
      largura,
      altura,
      // OS PIXELS DO MOTOR SAEM PREMULTIPLICADOS (a mistura do alvo 3D e
      // `ONE, INV_SRC_ALPHA`) e este e o formato que o Flutter espera
      // premultiplicado. Converter aqui multiplicaria a cor pelo alfa uma
      // segunda vez e a silhueta do modelo ganharia um halo escuro.
      ui.PixelFormat.rgba8888,
      completer.complete,
    );
    return completer.future;
  }

  /// A CENA DO ULTIMO QUADRO MONTADO — as camadas, as luzes e a camera
  /// como o motor as recebeu. Ela e o que o desenho acabou de entregar,
  /// e serve para conferir a traducao (eixo, fov, cadeia de pais, tinta)
  /// sem depender de a placa estar no aparelho.
  Cena3D get ultimaCena => _cena;

  /// OS NUMEROS DO ULTIMO QUADRO: quantas camadas desenharam, quantas
  /// ficaram fora do campo, quantos triangulos. E o que responde "por que
  /// o modelo sumiu" sem chutar.
  NumerosDoQuadro3D get numeros {
    try {
      return Motor3D.numerosDoQuadro;
    } catch (_) {
      return const NumerosDoQuadro3D();
    }
  }
}

/// A MALHA DA CENA VIRA AS MALHAS DO MOTOR — UMA POR MATERIAL.
///
/// O EIXO NAO MUDA AQUI. A geometria da cena ja esta no espaco do motor:
/// Y para cima, Z para longe, a mesma convencao em que o avaliador
/// posiciona os nos. Entortar o eixo neste ponto desenharia a cena
/// espelhada, com a luz vindo do lado errado.
///
/// O MATERIAL VAI NO PEDACO, e nao na camada: um objeto com dois materiais
/// entra como duas malhas (a ABI diz isso — "uma malha com dois materiais e
/// duas entradas na lista"), cada uma com os indices das faces daquele
/// material. Cada malha leva SO os vertices que usa, para o buffer da GPU
/// nao carregar o modelo inteiro por material.
List<MalhaCrua3D> malhasCruas3DDe(MalhaDoNo fonte) {
  final malha = fonte.malha;
  final n = malha.verts.length;

  // AS FACES SAO POLIGONOS, E A GPU QUER TRIANGULOS. O leque a partir do
  // primeiro vertice vale para qualquer poligono convexo, que e o que o
  // catalogo inteiro produz.
  final porMaterial = <String, _Pedaco>{};
  for (var f = 0; f < malha.faces.length; f++) {
    final face = malha.faces[f];
    if (face.length < 3) continue;
    final material = f < fonte.materiais.length
        ? fonte.materiais[f]
        : const Material3D();
    final chave = assinaturaDoMaterial3D(material);
    final pedaco = porMaterial.putIfAbsent(
      chave,
      () => _Pedaco(material, _MapeadorDeVertices(n)),
    );
    for (var i = 1; i + 1 < face.length; i++) {
      pedaco.indices
        ..add(pedaco.vertices.de(face[0]))
        ..add(pedaco.vertices.de(face[i]))
        ..add(pedaco.vertices.de(face[i + 1]));
    }
  }

  final saida = <MalhaCrua3D>[];
  for (final chave in porMaterial.keys) {
    final pedaco = porMaterial[chave]!;
    final m = pedaco.material;
    final mapa = pedaco.vertices;
    if (mapa.usados.isEmpty) continue;

    final posicoes = Float32List(mapa.usados.length * 3);
    for (var i = 0; i < mapa.usados.length; i++) {
      final v = malha.verts[mapa.usados[i]];
      posicoes[i * 3] = v[0];
      posicoes[i * 3 + 1] = v[1];
      posicoes[i * 3 + 2] = v.length > 2 ? v[2] : 0.0;
    }

    // A UV E POR VERTICE e a normal tambem; as duas seguem o mapa, senao
    // a textura e a luz escorregariam para o vertice errado.
    Float32List? uvs;
    final uvsDaFonte = fonte.uvs;
    if (uvsDaFonte != null && uvsDaFonte.length >= n) {
      uvs = Float32List(mapa.usados.length * 2);
      for (var i = 0; i < mapa.usados.length; i++) {
        final uv = uvsDaFonte[mapa.usados[i]];
        if (uv != null) {
          uvs[i * 2] = uv.dx;
          uvs[i * 2 + 1] = uv.dy;
        }
      }
    }

    // SEM NORMAL, O PONTEIRO VAI NULO — e nao um buffer de zeros.
    //
    // A ABI diz "NULO faz o motor calcular a normal PLANA", e o motor so
    // calcula quando o ponteiro e nulo: um buffer de zeros passa por "tem
    // normal" e cada vertice fica com a normal (0,0,0). O `normalize` de um
    // vetor nulo nao da erro nenhum — da um modelo chapado e escuro, com a
    // luz direta valendo zero e so o ambiente aparecendo. Um OBJ sem `vn`,
    // que e a maioria dos que saem de scanner e de conversor, chegava assim:
    // o modelo inteiro virava uma silhueta cinza.
    //
    // Um modelo pode ter normal em PARTE dos vertices. Aí o nulo por vertice
    // continua sendo do motor, mas o buffer so vale quando ha pelo menos uma
    // normal de verdade — o que sobra de zero o importador nativo preenche
    // com a plana da face.
    Float32List? normais;
    final normaisDaFonte = fonte.normais;
    if (normaisDaFonte != null && normaisDaFonte.length >= n) {
      var alguma = false;
      for (var i = 0; i < mapa.usados.length && !alguma; i++) {
        alguma = normaisDaFonte[mapa.usados[i]] != null;
      }
      if (alguma) {
        normais = Float32List(mapa.usados.length * 3);
        for (var i = 0; i < mapa.usados.length; i++) {
          final v = normaisDaFonte[mapa.usados[i]];
          if (v != null) {
            normais[i * 3] = v.x;
            normais[i * 3 + 1] = v.y;
            normais[i * 3 + 2] = v.z;
          }
        }
      }
    }

    final semLuz = m.kind == MaterialKind.unlit;
    final transparencia = m.opacity < 0.999 ||
        m.kind == MaterialKind.transparent;
    saida.add(
      MalhaCrua3D(
        posicoes: posicoes,
        indices: Uint32List.fromList(pedaco.indices),
        normais: normais,
        uvs: uvs,
        // SEM NORMAL, O MOTOR CALCULA A PLANA — que e o certo para o cubo
        // chanfrado e para o prisma, e errado para a esfera; quem tem a
        // normal suave manda, e quem nao tem nao fica com a normal de outro.
        corBase: Motor3DNativo._cor(m.baseColor, 255),
        metalico: m.metallic.clamp(0.0, 1.0),
        rugosidade: m.roughness.clamp(0.0, 1.0),
        // SEM ILUMINACAO E EMISSIVO CHEIO: e o que um material `unlit` quer
        // dizer, e a cor do material ja e a cor final.
        forcaEmissiva: semLuz ? 1.0 : m.emissive.clamp(0.0, 4.0),
        emissivo: Motor3DNativo._cor(m.baseColor, 255),
        modo: m.kind == MaterialKind.cutout
            ? 1
            : (transparencia ? 2 : 0),
        alfaCorte: m.alphaCutoff.clamp(0.0, 1.0),
        faceDupla: m.doubleSided || m.kind == MaterialKind.cutout,
      ),
    );
  }
  return saida;
}

/// O MAPA DE UM PEDACO: quais vertices aquele material usa, na ordem em
/// que aparecem, e o indice novo de cada um.
class _MapeadorDeVertices {
  _MapeadorDeVertices(int total) : _novo = List<int>.filled(total, -1);

  final List<int> _novo;
  final List<int> usados = <int>[];

  int de(int antigo) {
    final pronto = _novo[antigo];
    if (pronto >= 0) return pronto;
    _novo[antigo] = usados.length;
    usados.add(antigo);
    return usados.length - 1;
  }
}

class _Pedaco {
  _Pedaco(this.material, this.vertices);

  final Material3D material;
  final _MapeadorDeVertices vertices;
  final List<int> indices = <int>[];
}

/// A ASSINATURA DE UM MATERIAL: tudo o que muda o desenho entra, e nada
/// que nao mude. Serve para dois papeis — reconhecer que a geometria na
/// GPU ainda vale, e agrupar as faces que compartilham o mesmo material
/// (o material do aplicativo nao tem igualdade por valor; comparar por
/// identidade criaria uma malha por face de um modelo importado).
String assinaturaDoMaterial3D(Material3D m) => '${m.kind.index}|'
    '${m.baseColor.toARGB32()}|${m.metallic}|${m.roughness}|'
    '${m.emissive}|${m.opacity}|${m.reflectivity}|${m.alphaCutoff}|'
    '${m.doubleSided}|${m.imagePath ?? ''}|'
    '${m.faceImagePaths.entries.map((e) => '${e.key}=${e.value}').join(',')}|'
    '${m.textureLayerId ?? ''}|${m.normalStrength}|${m.occlusionStrength}';

/// A CHAVE DE UM QUADRO DE CENA. Tudo o que muda o desenho entra aqui, e
/// nada que nao mude: e ela que decide se o motor desenha de novo.
String chaveDaCena({
  required Scene3D cena,
  required RenderCamera? camera,
  required Duration local,
  required int largura,
  required int altura,
  int sombra = 0,
  int amostras = 1,
}) {
  final b = StringBuffer()
    ..write(largura)
    ..write('x')
    ..write(altura)
    ..write('|s')
    ..write(sombra)
    ..write('a')
    ..write(amostras)
    ..write('|')
    ..write(local.inMicroseconds);
  if (camera != null) {
    b
      ..write('|c')
      ..write(camera.position.x)
      ..write(',')
      ..write(camera.position.y)
      ..write(',')
      ..write(camera.position.z)
      ..write(',')
      ..write(camera.target.x)
      ..write(',')
      ..write(camera.target.y)
      ..write(',')
      ..write(camera.target.z)
      ..write(',')
      ..write(camera.focalLength)
      ..write(',')
      ..write(camera.filmWidth)
      ..write(',')
      ..write(camera.orthographic ? 1 : 0)
      ..write(',')
      ..write(camera.orthoScale);
  }
  for (final no in cena.nodes) {
    if (!no.visible) continue;
    final pos = no.positionAt(local);
    b
      ..write('|n')
      ..write(no.id)
      ..write(':')
      ..write(no.kind.index)
      ..write(':')
      ..write(no.parentId ?? '')
      ..write(':')
      ..write(no.size)
      ..write(':')
      ..write(no.isNull ? 1 : 0)
      ..write(':')
      // A GEOMETRIA ENTRA PELA IDENTIDADE, e nao pelo nome: a malha de um
      // no nasce de novo a cada edicao da forma, e um nome igual com uma
      // malha diferente desenharia o quadro velho (§ cache por valor
      // resolveria o contrario — malha igual refeita nao muda nada).
      ..write(identityHashCode(no.mesh))
      ..write(':')
      ..write(identityHashCode(no.modelAsset))
      ..write(':')
      ..write(no.modelSource?.path ?? '')
      ..write(':')
      ..write(no.lod.index)
      ..write(':')
      ..write(no.subdivisions)
      ..write(':')
      ..write(no.useModelMaterials ? 1 : 0)
      ..write(':')
      ..write(no.material.baseColor.toARGB32())
      ..write(':')
      ..write(no.material.metallic)
      ..write(':')
      ..write(no.material.roughness)
      ..write(':')
      ..write(no.material.emissive)
      ..write(':')
      ..write(no.material.opacity)
      ..write(':')
      ..write(no.material.kind.index)
      ..write(':')
      ..write(no.material.alphaCutoff)
      ..write(':')
      ..write(no.material.doubleSided ? 1 : 0)
      ..write(':')
      ..write(no.material.reflectivity)
      ..write(':')
      ..write(no.material.imagePath ?? '')
      ..write(':')
      ..write(no.material.textureLayerId ?? '')
      ..write(':')
      ..write(no.modelMotion.clip)
      ..write(':')
      ..write(no.modelMotion.speed)
      ..write(':')
      ..write(no.modelMotion.offset)
      ..write(':')
      ..write(no.modelMotion.loop ? 1 : 0)
      ..write(':p')
      ..write(pos.x)
      ..write(',')
      ..write(pos.y)
      ..write(',')
      ..write(pos.z)
      ..write(':r')
      ..write(no.rotX.valueAt(local))
      ..write(',')
      ..write(no.rotY.valueAt(local))
      ..write(',')
      ..write(no.rotZ.valueAt(local))
      ..write(':s')
      ..write(no.scale.valueAt(local));
    // UMA COPIA A MAIS E UMA CAMADA A MAIS: a posicao de cada instancia
    // muda o desenho, entao ela entra na chave como qualquer transform.
    for (final copia in no.instances) {
      b
        ..write(':i')
        ..write(copia.x)
        ..write(',')
        ..write(copia.y)
        ..write(',')
        ..write(copia.z);
    }
  }
  for (final luz in cena.lights) {
    final forca = luz.intensity.valueAt(local);
    b
      ..write('|l')
      ..write(luz.id)
      ..write(':')
      ..write(luz.kind.index)
      ..write(':')
      ..write(luz.color.toARGB32())
      ..write(':')
      ..write(forca)
      ..write(':')
      ..write(luz.position.x)
      ..write(',')
      ..write(luz.position.y)
      ..write(',')
      ..write(luz.position.z)
      ..write(':')
      ..write(luz.direction.x)
      ..write(',')
      ..write(luz.direction.y)
      ..write(',')
      ..write(luz.direction.z)
      ..write(':')
      ..write(luz.range)
      ..write(':')
      ..write(luz.coneDegrees)
      ..write(':')
      ..write(luz.softness)
      ..write(':')
      ..write(luz.castsShadow ? 1 : 0);
  }
  return b.toString();
}
