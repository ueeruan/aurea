import 'dart:convert';
import 'dart:io';
import 'dart:isolate';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import 'dart:ui' show Offset;

import 'package:aurea_tracker/aurea_tracker.dart';

import '../domain/camera_nativa.dart';
import '../domain/camera_solver3d.dart';
import '../domain/pontos_seguidos.dart';
import 'tracking_service.dart';

/// O RASTREIO DE CAMERA 3D, do arquivo ate a solucao.
///
/// A conta em si mora no dominio e nao sabe o que e um video. Este
/// servico e o que a liga ao mundo: arranja os quadros, tira o trabalho
/// pesado da thread da interface, guarda o resultado e devolve o mesmo
/// resultado da proxima vez.
///
/// GUARDAR IMPORTA MAIS DO QUE PARECE. O solver tem sorteio dentro
/// (RANSAC); com semente fixa ele e deterministico, mas ainda assim
/// resolver de novo a cada abertura do projeto significaria esperar
/// segundos por algo que ja se sabe — e, pior, qualquer mudanca futura
/// no algoritmo moveria o objeto que a pessoa colou no plano. A solucao
/// gravada e a que vale ate ela mandar rastrear outra vez.
class CameraTrackService {
  CameraTrackService._();
  static final instance = CameraTrackService._();

  final Map<String, SolucaoCamera3D> _cache = {};
  final Set<String> _emAndamento = {};

  /// Avisa a interface quando uma analise termina.
  final ValueNotifier<int> revision = ValueNotifier(0);

  /// Progresso 0..1 e em que ETAPA a analise esta. Um rastreio leva
  /// dezenas de segundos: barra sem legenda parece travada, e "por
  /// favor aguarde" nao diz se falta muito.
  final ValueNotifier<double> progress = ValueNotifier(0);
  final ValueNotifier<String> etapa = ValueNotifier('');
  final ValueNotifier<EtapaDoRastreio?> fase = ValueNotifier(null);

  /// O QUE FOI SEGUIDO, guardado para o RE-SOLVE.
  ///
  /// Sem isto, mexer numa opcao e mandar recalcular obrigaria a ler o
  /// video de novo — dezenas de segundos para repetir a parte que nao
  /// mudou. Com os rastros na memoria, refazer a conta e quase imediato,
  /// e e isso que torna "apagar os pontos ruins e resolver de novo" um
  /// gesto usavel em vez de uma ameaca.
  final Map<String, _Rastros> _rastros = {};

  SolucaoCamera3D? dataFor(String layerId) => _cache[layerId];
  bool isRunning(String layerId) => _emAndamento.contains(layerId);

  /// Da para recalcular sem reler o video?
  bool podeResolverDeNovo(String layerId) => _rastros.containsKey(layerId);

  /// Adota uma solucao vinda de fora (copia entre camadas, testes).
  void adotar(String layerId, SolucaoCamera3D s) {
    _cache[layerId] = s;
    revision.value++;
  }

  /// CORTAR OU DUPLICAR O CLIPE NAO PERDE O RASTREIO. A solucao e um
  /// ATIVO do trecho da fonte, nao da camada: as duas metades de um
  /// corte mostram pedacos do mesmo trecho, e o mapeamento por instante
  /// da fonte ('src0') faz cada metade pegar as poses certas sozinha.
  /// Copia a memoria na hora e o arquivo em segundo plano.
  Future<void> clonar(String deId, String paraId) async {
    final s = _cache[deId];
    if (s != null) {
      _cache[paraId] = s;
      final r = _rastros[deId];
      if (r != null) _rastros[paraId] = r;
      revision.value++;
    }
    try {
      final pasta = (await _pasta()).path;
      final de = File('$pasta/$deId.json');
      if (de.existsSync()) await de.copy('$pasta/$paraId.json');
    } catch (_) {}
  }

  Future<Directory> _pasta() async {
    final base = await getApplicationSupportDirectory();
    final d = Directory('${base.path}/camera3d');
    if (!d.existsSync()) d.createSync(recursive: true);
    return d;
  }

  /// Le do disco o que ja foi resolvido antes.
  Future<void> load(String layerId) async {
    if (_cache.containsKey(layerId)) return;
    try {
      final f = File('${(await _pasta()).path}/$layerId.json');
      if (!f.existsSync()) return;
      final s = SolucaoCamera3D.decode(await f.readAsString());
      if (s != null) {
        _cache[layerId] = s;
        revision.value++;
      }
    } catch (_) {}
  }

  /// RASTREIA a camera de um clipe.
  ///
  /// Devolve a solucao, ou lanca [RastreioException] com o motivo em
  /// portugues quando o plano nao permite — que e uma resposta melhor do
  /// que uma cena inventada.
  Future<SolucaoCamera3D> rastrear({
    required String layerId,
    required String sourcePath,
    required Duration start,
    required Duration duration,
    ModoDoSolve modo = ModoDoSolve.equilibrado,
    TipoDeTomada tipoDeTomada = TipoDeTomada.auto,
    int? fps,
    int? maximoDePontos,
    double? focalPx,
    double? proporcao,
  }) async {
    if (_emAndamento.contains(layerId)) {
      throw const RastreioException(
        FalhaDoRastreio.naoConvergiu,
        'Ja tem um rastreio rodando nesse clipe.',
      );
    }
    _emAndamento.add(layerId);
    // O MOTOR NOVO (C++): quadros crus a 640 px e 24 quadros/s, seguidor KLT
    // com subpixel, homografia para chao plano, ajuste de feixes com a focal
    // livre e pose de todo quadro. So se ele nao carregar fica o antigo.
    try {
      final nova = await _rastrearComMotorNovo(
        layerId: layerId,
        sourcePath: sourcePath,
        start: start,
        duration: duration,
        tipoDeTomada: tipoDeTomada,
        focalPx: focalPx,
        proporcao: proporcao,
      );
      if (nova != null) return nova;
    } on RastreioException {
      _emAndamento.remove(layerId);
      etapa.value = '';
      fase.value = null;
      rethrow;
    } catch (_) {
      // Motor indisponivel neste aparelho: segue pelo caminho antigo.
    }
    final taxa = fps ?? modo.fps;
    final quantos = maximoDePontos ?? modo.pontos;
    _dizer(EtapaDoRastreio.lendo, 0);
    try {
      final frames = await TrackingService.instance.grayFrames(
        sourcePath,
        start: start,
        duration: duration,
        fps: taxa,
        maxFrames: modo.maximoDeQuadros,
      );
      if (frames.length < 8) {
        throw const RastreioException(
          FalhaDoRastreio.poucosPontos,
          'Esse trecho é curto demais para rastrear. '
          'Use pelo menos dois segundos de vídeo.',
        );
      }

      _dizer(EtapaDoRastreio.achandoPontos, .22);

      // O RESTO SAI DA THREAD DA INTERFACE. Seguir cem pontos por duzentos
      // quadros e depois resolver a camera sao segundos de conta pura; na
      // thread principal isso e o app congelado, e app congelado a pessoa
      // fecha.
      final seguidos = await Isolate.run(
        () => seguirPontos(
          frames,
          maximoDePontos: quantos,
          distanciaMinima: 8,
          duracaoMinima: 6,
        ),
      );
      _rastros[layerId] = _Rastros(
        pontos: seguidos,
        largura: frames.first.width,
        altura: frames.first.height,
        quadros: frames.length,
        fps: taxa,
      );

      _dizer(EtapaDoRastreio.lendoACena, .45);
      final leitura = lerCena(
        seguidos,
        largura: frames.first.width,
        quadros: frames.length,
      );
      if (leitura == LeituraDaCena.tripeOuGiro &&
          tipoDeTomada == TipoDeTomada.auto) {
        // A recusa vem aqui, e nao depois de resolver: nada do que vem
        // adiante mudaria a resposta, e a pessoa esperaria por nada.
        throw const RastreioException(
          FalhaDoRastreio.semParalaxe,
          'Essa filmagem nao da rastreio 3D: a camera gira, mas nao anda. '
          'Sem deslocamento nao ha profundidade para medir. Filme andando '
          'alguns passos, com coisas perto e longe no quadro.',
        );
      }

      _dizer(EtapaDoRastreio.resolvendo, .55);
      final solucao = await Isolate.run(
        () => resolverCamera3D(
          seguidos,
          largura: frames.first.width,
          altura: frames.first.height,
          quadros: frames.length,
          fps: taxa,
          focalPx: focalPx,
          tipoDeTomada: tipoDeTomada,
          rodadasDeRefino: modo.refinos,
        ),
      );

      _dizer(EtapaDoRastreio.pronto, 1);
      await guardar(layerId, solucao);
      return solucao;
    } finally {
      _emAndamento.remove(layerId);
      etapa.value = '';
      fase.value = null;
    }
  }

  static const _larguraNova = 640;
  static const _taxaNova = 24;
  static const _quadrosNovos = 720;

  /// Devolve nulo so quando o video nao pode ser lido assim (a pessoa ve o
  /// caminho antigo tentar); falha de rastreio sobe como RastreioException.
  Future<SolucaoCamera3D?> _rastrearComMotorNovo({
    required String layerId,
    required String sourcePath,
    required Duration start,
    required Duration duration,
    required TipoDeTomada tipoDeTomada,
    double? focalPx,
    double? proporcao,
  }) async {
    attVersion(); // carrega o motor: sem ele, cai no catch de quem chamou
    final prop = proporcao != null && proporcao.isFinite && proporcao > 0
        ? proporcao
        : 16 / 9;
    const largura = _larguraNova;
    final altura = math.max(64, (largura / prop / 2).round() * 2);
    _dizer(EtapaDoRastreio.lendo, 0);
    final cru = await TrackingService.instance.quadrosCinzaCrus(
      sourcePath,
      start: start,
      duration: duration,
      fps: _taxaNova,
      largura: largura,
      altura: altura,
      maxFrames: _quadrosNovos,
    );
    if (cru == null) return null;
    if (cru.quadros < 8) {
      throw const RastreioException(
        FalhaDoRastreio.poucosPontos,
        'Esse trecho é curto demais para rastrear. '
        'Use pelo menos dois segundos de vídeo.',
      );
    }
    _dizer(EtapaDoRastreio.achandoPontos, .2);
    final caminho = cru.arquivo.path;
    final quadros = cru.quadros;
    try {
      final r = await Isolate.run(
        () => rastrearArquivoCru(
          caminho,
          largura: largura,
          altura: altura,
          quadros: quadros,
          fps: _taxaNova,
          focalPx: focalPx,
          tipoDeTomada: tipoDeTomada,
        ),
      );
      _rastros[layerId] = _Rastros(
        pontos: r.pontos,
        largura: largura,
        altura: altura,
        quadros: quadros,
        fps: _taxaNova,
        inicioDaFonteUs: start.inMicroseconds,
      );
      final solucao = r.solucao.copiarCom(inicioDaFonteUs: start.inMicroseconds);
      _dizer(EtapaDoRastreio.pronto, 1);
      await guardar(layerId, solucao);
      return solucao;
    } finally {
      try {
        cru.arquivo.deleteSync();
      } catch (_) {}
      _emAndamento.remove(layerId);
      etapa.value = '';
      fase.value = null;
    }
  }

  /// RESOLVE DE NOVO com o que ja foi lido do video.
  ///
  /// Serve para depois de apagar pontos ruins ou trocar uma opcao. Sem
  /// os rastros guardados nao da: ai a resposta e null, e quem chamou
  /// manda rastrear do comeco.
  Future<SolucaoCamera3D?> resolverDeNovo(
    String layerId, {
    Set<int> pontosApagados = const {},
    ModoDoSolve modo = ModoDoSolve.equilibrado,
    TipoDeTomada tipoDeTomada = TipoDeTomada.auto,
    double? focalPx,
  }) async {
    final r = _rastros[layerId];
    if (r == null || _emAndamento.contains(layerId)) return null;
    _emAndamento.add(layerId);
    _dizer(EtapaDoRastreio.resolvendo, .5);
    try {
      final usados = [
        for (final p in r.pontos)
          if (!pontosApagados.contains(p.id)) p,
      ];
      SolucaoCamera3D solucao;
      try {
        final obs = observacoesDosPontos(usados);
        solucao = await Isolate.run(
          () => resolverCamera3DNativo(
            obs,
            largura: r.largura,
            altura: r.altura,
            quadros: r.quadros,
            fps: r.fps,
            focalPx: focalPx,
            tipoDeTomada: tipoDeTomada,
            pontosSeguidos: usados.length,
          ),
        );
      } on RastreioException {
        rethrow;
      } catch (_) {
        solucao = await Isolate.run(
          () => resolverCamera3D(
            usados,
            largura: r.largura,
            altura: r.altura,
            quadros: r.quadros,
            fps: r.fps,
            focalPx: focalPx,
            tipoDeTomada: tipoDeTomada,
            rodadasDeRefino: modo.refinos,
          ),
        );
      }
      if (r.inicioDaFonteUs != null) {
        solucao = solucao.copiarCom(inicioDaFonteUs: r.inicioDaFonteUs);
      }
      _dizer(EtapaDoRastreio.pronto, 1);
      await guardar(layerId, solucao);
      return solucao;
    } finally {
      _emAndamento.remove(layerId);
      etapa.value = '';
      fase.value = null;
    }
  }

  void _dizer(EtapaDoRastreio e, double p) {
    fase.value = e;
    etapa.value = e.emPalavras;
    progress.value = p;
  }

  /// Joga fora a solucao — para rastrear de novo com outros ajustes.
  Future<void> clear(String layerId) async {
    _cache.remove(layerId);
    try {
      final f = File('${(await _pasta()).path}/$layerId.json');
      if (f.existsSync()) f.deleteSync();
    } catch (_) {}
    revision.value++;
  }

  /// Substitui a solucao guardada — usado depois de definir o chao.
  Future<void> guardar(String layerId, SolucaoCamera3D s) async {
    _cache[layerId] = s;
    try {
      await File('${(await _pasta()).path}/$layerId.json')
          .writeAsString(jsonEncode(s.toJson()), flush: true);
    } catch (_) {}
    revision.value++;
  }
}

/// AS ETAPAS, com o nome que aparece na tela.
///
/// Nomear cada uma nao e enfeite: um rastreio que fica trinta segundos
/// em "Analisando..." parece travado, e a pessoa fecha o aplicativo
/// antes de terminar. Vendo "Seguindo os pontos" virar "Reconstruindo a
/// cena", ela sabe que ha progresso mesmo quando a barra anda devagar.
enum EtapaDoRastreio {
  lendo,
  achandoPontos,
  lendoACena,
  resolvendo,
  pronto;

  String get emPalavras => switch (this) {
    EtapaDoRastreio.lendo => 'Lendo o vídeo...',
    EtapaDoRastreio.achandoPontos => 'Achando e seguindo os pontos...',
    EtapaDoRastreio.lendoACena => 'Vendo que tipo de cena é...',
    EtapaDoRastreio.resolvendo => 'Reconstruindo o movimento da câmera...',
    EtapaDoRastreio.pronto => 'Pronto',
  };
}

/// Os rastros 2D guardados para o re-solve.
class _Rastros {
  const _Rastros({
    required this.pontos,
    required this.largura,
    required this.altura,
    required this.quadros,
    required this.fps,
    this.inicioDaFonteUs,
  });

  final List<PontoSeguido> pontos;
  final int largura;
  final int altura;
  final int quadros;
  final int fps;
  final int? inicioDaFonteUs;
}

/// SEGUE E RESOLVE a partir do arquivo cru (roda num isolate): le um quadro
/// por vez (sem guardar o video na memoria), segue os pontos no motor novo e
/// resolve a camera. Devolve os rastros (para "resolver de novo") e a
/// solucao.
({List<PontoSeguido> pontos, SolucaoCamera3D solucao}) rastrearArquivoCru(
  String caminho, {
  required int largura,
  required int altura,
  required int quadros,
  required int fps,
  double? focalPx,
  TipoDeTomada tipoDeTomada = TipoDeTomada.auto,
}) {
  final seguidor = SeguidorDePontosNativo(largura, altura, maximoDePontos: 450);
  late final Float64List obs;
  final arquivo = File(caminho).openSync();
  try {
    final quadro = Uint8List(largura * altura);
    for (var q = 0; q < quadros; q++) {
      if (arquivo.readIntoSync(quadro) < quadro.length) break;
      seguidor.empurrar(quadro, q);
    }
    obs = seguidor.observacoes();
  } finally {
    arquivo.closeSync();
    seguidor.fechar();
  }
  final porId = <int, Map<int, Offset>>{};
  for (var i = 0; i + 4 <= obs.length; i += 4) {
    (porId[obs[i].round()] ??= {})[obs[i + 1].round()] =
        Offset(obs[i + 2], obs[i + 3]);
  }
  final pontos = <PontoSeguido>[
    for (final e in porId.entries)
      if (e.value.length >= 6)
        PontoSeguido(e.key, e.value.keys.reduce(math.min), e.value),
  ];
  final solucao = resolverCamera3DNativo(
    observacoesDosPontos(pontos),
    largura: largura,
    altura: altura,
    quadros: quadros,
    fps: fps,
    focalPx: focalPx,
    tipoDeTomada: tipoDeTomada,
    pontosSeguidos: pontos.length,
  );
  return (pontos: pontos, solucao: solucao);
}
