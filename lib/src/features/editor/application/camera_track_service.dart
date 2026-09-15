import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:math' as math;

import 'package:aurea_tracker2/aurea_tracker2.dart' show At2ProgressoNativo;
import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../domain/camera_solver3d.dart';
import '../domain/motor_de_rastreio.dart';
import '../domain/pontos_seguidos.dart';

/// O RASTREIO DE CAMERA 3D, do arquivo ate a solucao — servico do motor
/// 2.0.
///
/// A conta mora no motor nativo (packages/aurea_tracker2); este servico
/// e o que a liga ao mundo: arranja os quadros, tira o trabalho pesado
/// da thread da interface, guarda o resultado e devolve o mesmo
/// resultado da proxima vez.
///
/// AS TRES REGRAS DA VERSAO 2, escritas com o sangue da versao 1:
///
///   1. SEM RESERVA SILENCIOSA. O motor 1 tinha um `catch (_)` que, se a
///      biblioteca nativa nao carregasse no aparelho, deixava um solver
///      antigo "resolver" em um segundo — e a pessoa via uma cena
///      fantasma sem nenhum aviso. Aqui, motor ausente e um ERRO DITO.
///   2. NENHUMA ANALISE SEM LASTRO. Os quadros extraidos sao conferidos
///      contra a duracao pedida; um ffmpeg que devolve meia duzia de
///      quadros vira "video ilegivel", nunca uma cena de meia duzia de
///      quadros.
///   3. A SOLUCAO ASSINA. Toda solucao gravada carrega o nome do motor e
///      o tempo da analise — da para provar NO APARELHO quem resolveu e
///      quanto custou.
class CameraTrackService {
  CameraTrackService._();
  static final instance = CameraTrackService._();

  final Map<String, SolucaoCamera3D> _cache = {};
  final Set<String> _emAndamento = {};

  /// Avisa a interface quando uma analise termina.
  final ValueNotifier<int> revision = ValueNotifier(0);

  /// Progresso 0..1, a ETAPA e os numeros ao vivo. Um rastreio leva
  /// dezenas de segundos: barra sem legenda parece travada, e uma
  /// "analise" que termina em um segundo tem de parecer o que e —
  /// impossivel.
  final ValueNotifier<double> progress = ValueNotifier(0);
  final ValueNotifier<String> etapa = ValueNotifier('');
  final ValueNotifier<EtapaDoRastreio?> fase = ValueNotifier(null);

  /// O QUE FOI SEGUIDO, guardado para o RE-SOLVE: apagar pontos ruins e
  /// recalcular nao pode custar reler o video inteiro.
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

  /// A analise le o video a 640 px e 24 quadros/s — o motor segue com
  /// subpixel, entao mais resolucao nao acha nada que isso nao ache.
  static const _larguraDaAnalise = 640;
  static const _taxaDaAnalise = 24;

  /// RASTREIA a camera de um clipe com o motor 2.0.
  ///
  /// Devolve a solucao, ou lanca [RastreioException] com o motivo em
  /// portugues — motor ausente e video ilegivel INCLUIDOS. Nao existe
  /// caminho de reserva: e o motor nativo ou e um erro com nome.
  Future<SolucaoCamera3D> rastrear({
    required String layerId,
    required String sourcePath,
    required Duration start,
    required Duration duration,
    ModoDoSolve modo = ModoDoSolve.equilibrado,
    TipoDeTomada tipoDeTomada = TipoDeTomada.auto,
    double? focalPx,
    double? proporcao,
  }) async {
    if (_emAndamento.contains(layerId)) {
      throw const RastreioException(
        FalhaDoRastreio.naoConvergiu,
        'Já tem um rastreio rodando nesse clipe.',
      );
    }
    _emAndamento.add(layerId);
    final relogio = Stopwatch()..start();
    try {
      // 1. O MOTOR EXISTE? A pergunta vem primeiro e a resposta e dita.
      final versao = versaoDoMotor();
      if (versao == null) {
        throw const RastreioException(
          FalhaDoRastreio.motorIndisponivel,
          'O motor de rastreio não carregou neste aparelho, então não '
          'teve análise nenhuma. Me avise qual é o seu celular que eu '
          'investigo — rastrear sem o motor daria uma cena inventada.',
        );
      }

      // 2. OS QUADROS, com lastro.
      _dizer(EtapaDoRastreio.lendo, 0);
      final prop = proporcao != null && proporcao.isFinite && proporcao > 0
          ? proporcao
          : 16 / 9;
      const largura = _larguraDaAnalise;
      final altura = math.max(64, (largura / prop / 2).round() * 2);
      final maximoDeQuadros = modo.maximoDeQuadros;
      final cru = await _quadrosCinzaCrus(
        sourcePath,
        start: start,
        duration: duration,
        largura: largura,
        altura: altura,
        maxFrames: maximoDeQuadros,
      );
      if (cru == null) {
        throw const RastreioException(
          FalhaDoRastreio.videoIlegivel,
          'Não consegui ler os quadros desse vídeo. Tente reimportar o '
          'arquivo, ou exportá-lo de novo de onde veio.',
        );
      }
      try {
        if (cru.quadros < _taxaDaAnalise * 2) {
          throw const RastreioException(
            FalhaDoRastreio.poucosQuadros,
            'Esse trecho é curto demais para rastrear. '
            'Use pelo menos dois segundos de vídeo.',
          );
        }
        // O ffmpeg dizendo "deu certo" com um punhado de quadros e o
        // outro jeito de a analise de um segundo nascer. Confere-se a
        // CONTA: o trecho pedido, na taxa da analise, tinha de render
        // isto — menos de 60% e leitura estragada, nao analise.
        final esperados = math.min(
          maximoDeQuadros,
          (duration.inMilliseconds * _taxaDaAnalise / 1000).floor(),
        );
        if (esperados >= _taxaDaAnalise * 2 && cru.quadros < esperados * 0.6) {
          throw RastreioException(
            FalhaDoRastreio.videoIlegivel,
            'O trecho tem ${(duration.inMilliseconds / 1000).toStringAsFixed(1)} s, '
            'mas a leitura só devolveu ${(cru.quadros / _taxaDaAnalise).toStringAsFixed(1)} s '
            'de quadros. Sem o vídeo inteiro não dá para rastrear.',
          );
        }

        // 3. SEGUIR E RESOLVER, fora da thread da interface, com o
        // progresso DE VERDADE atravessando o isolate: o motor conta em
        // que quadro esta e em que passo do ajuste vai.
        _dizer(EtapaDoRastreio.achandoPontos, .04);
        final quadros = cru.quadros;
        final caminho = cru.arquivo.path;
        final pontosDoModo = modo.pontos;
        final ouvinte = NativeCallable<At2ProgressoNativo>.listener(
          (int faseN, double fracao, int a, int b, Pointer<Void> alvo) {
            if (faseN == 1) {
              fase.value = EtapaDoRastreio.achandoPontos;
              etapa.value = 'Seguindo os pontos... quadro $a de $b';
              progress.value = .04 + .40 * fracao;
            } else if (faseN == 2) {
              fase.value = EtapaDoRastreio.resolvendo;
              etapa.value = 'Reconstruindo o movimento da câmera...';
              progress.value = .44 + .54 * fracao;
            }
          },
        );
        ({List<PontoSeguido> pontos, SolucaoCamera3D solucao}) r;
        try {
          final endereco = ouvinte.nativeFunction.address;
          r = await Isolate.run(
            () => rastrearArquivoCru(
              caminho,
              largura: largura,
              altura: altura,
              quadros: quadros,
              fps: _taxaDaAnalise,
              focalPx: focalPx,
              tipoDeTomada: tipoDeTomada,
              maximoDePontos: pontosDoModo,
              enderecoDoProgresso: endereco,
            ),
          );
        } finally {
          ouvinte.close();
        }
        _rastros[layerId] = _Rastros(
          pontos: r.pontos,
          largura: largura,
          altura: altura,
          quadros: quadros,
          fps: _taxaDaAnalise,
          inicioDaFonteUs: start.inMicroseconds,
        );
        final solucao = r.solucao.copiarCom(
          inicioDaFonteUs: start.inMicroseconds,
          motor: 'aurea_tracker2 $versao',
          analiseMs: relogio.elapsedMilliseconds,
        );
        _dizer(EtapaDoRastreio.pronto, 1);
        await guardar(layerId, solucao);
        return solucao;
      } finally {
        try {
          cru.arquivo.deleteSync();
        } catch (_) {}
      }
    } finally {
      _emAndamento.remove(layerId);
      etapa.value = '';
      fase.value = null;
    }
  }

  /// QUADROS CRUS EM CINZA (um arquivo so, largura x altura bytes por
  /// quadro, sem PNG) — a comida do motor.
  Future<({File arquivo, int quadros})?> _quadrosCinzaCrus(
    String path, {
    required Duration start,
    required Duration duration,
    required int largura,
    required int altura,
    required int maxFrames,
  }) async {
    final base = await getApplicationSupportDirectory();
    final dir = Directory('${base.path}/track_camera')
      ..createSync(recursive: true);
    final saida = File('${dir.path}/cinza.raw');
    if (saida.existsSync()) saida.deleteSync();
    final segundos = duration.inMilliseconds / 1000.0;
    if (segundos <= 0) return null;
    final session = await FFmpegKit.executeWithArguments([
      '-y',
      '-ss',
      (start.inMilliseconds / 1000.0).toStringAsFixed(3),
      '-t',
      segundos.toStringAsFixed(3),
      '-i',
      path,
      '-vf',
      'fps=$_taxaDaAnalise,scale=$largura:$altura,format=gray',
      '-frames:v',
      '$maxFrames',
      '-f',
      'rawvideo',
      '-pix_fmt',
      'gray',
      saida.path,
    ]);
    if (!ReturnCode.isSuccess(await session.getReturnCode()) ||
        !saida.existsSync()) {
      return null;
    }
    return (arquivo: saida, quadros: saida.lengthSync() ~/ (largura * altura));
  }

  /// RESOLVE DE NOVO com o que ja foi lido do video — depois de apagar
  /// pontos ruins ou trocar uma opcao. Sem os rastros guardados a
  /// resposta e null, e quem chamou manda rastrear do comeco.
  Future<SolucaoCamera3D?> resolverDeNovo(
    String layerId, {
    Set<int> pontosApagados = const {},
    TipoDeTomada tipoDeTomada = TipoDeTomada.auto,
    double? focalPx,
  }) async {
    final r = _rastros[layerId];
    if (r == null || _emAndamento.contains(layerId)) return null;
    _emAndamento.add(layerId);
    final relogio = Stopwatch()..start();
    _dizer(EtapaDoRastreio.resolvendo, .5);
    try {
      final usados = [
        for (final p in r.pontos)
          if (!pontosApagados.contains(p.id)) p,
      ];
      final obs = observacoesDosPontos(usados);
      final largura = r.largura;
      final altura = r.altura;
      final quadros = r.quadros;
      final fps = r.fps;
      final quantos = usados.length;
      var solucao = await Isolate.run(
        () => resolverCamera3DNativo(
          obs,
          largura: largura,
          altura: altura,
          quadros: quadros,
          fps: fps,
          focalPx: focalPx,
          tipoDeTomada: tipoDeTomada,
          pontosSeguidos: quantos,
        ),
      );
      final versao = versaoDoMotor();
      solucao = solucao.copiarCom(
        inicioDaFonteUs: r.inicioDaFonteUs,
        motor: versao == null ? null : 'aurea_tracker2 $versao',
        analiseMs: relogio.elapsedMilliseconds,
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
/// antes de terminar.
enum EtapaDoRastreio {
  lendo,
  achandoPontos,
  resolvendo,
  pronto;

  String get emPalavras => switch (this) {
    EtapaDoRastreio.lendo => 'Lendo o vídeo...',
    EtapaDoRastreio.achandoPontos => 'Achando e seguindo os pontos...',
    EtapaDoRastreio.resolvendo => 'Reconstruindo o movimento da câmera...',
    EtapaDoRastreio.pronto => 'Pronto',
  };
}

/// Quanto tempo e quantos pontos cada modo gasta. O que muda e a
/// quantidade — nunca a matematica, e nunca o rigor da validacao.
extension ReceitaDoModo on ModoDoSolve {
  /// Quantos pontos o seguidor mantem vivos.
  int get pontos => switch (this) {
    ModoDoSolve.rapido => 400,
    ModoDoSolve.equilibrado => 650,
    ModoDoSolve.preciso => 900,
  };

  /// Teto de quadros analisados (24/s). Um video longo nao pode virar
  /// RAM de celular: o rapido para em 15 s, os outros em 30 s.
  int get maximoDeQuadros => switch (this) {
    ModoDoSolve.rapido => 360,
    ModoDoSolve.equilibrado => 720,
    ModoDoSolve.preciso => 720,
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
