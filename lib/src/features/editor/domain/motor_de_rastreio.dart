import 'dart:ffi';
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' show Offset;

import 'package:aurea_tracker2/aurea_tracker2.dart';

import 'algebra_numerica.dart';
import 'camera_solver3d.dart';
import 'pontos_seguidos.dart';

/// A PONTE COM O MOTOR 2.0 (packages/aurea_tracker2).
///
/// Duas regras que vem direto do defeito que enterrou o motor 1:
///
///   1. SEM RESERVA SILENCIOSA. Se a biblioteca nativa nao carregar, a
///      resposta e um erro DITO ([FalhaDoRastreio.motorIndisponivel]) —
///      nunca um solver de mentira que "resolve" em um segundo.
///   2. TODA RECUSA TEM NOME. O motor devolve codigos; aqui cada um vira
///      uma mensagem que diz o que aconteceu e o que fazer.

/// A versao do motor nativo, ou null quando a biblioteca nao carregou
/// neste aparelho. E a UNICA pergunta que se faz antes de rastrear.
String? versaoDoMotor() {
  try {
    return versaoDoMotorNativo();
  } catch (_) {
    return null;
  }
}

/// Os rastros no formato do motor: [id, quadro, x, y].
Float64List observacoesDosPontos(List<PontoSeguido> pontos) {
  var n = 0;
  for (final p in pontos) {
    n += p.observacoes.length;
  }
  final out = Float64List(n * 4);
  var i = 0;
  for (final p in pontos) {
    for (final e in p.observacoes.entries) {
      out[i++] = p.id.toDouble();
      out[i++] = e.key.toDouble();
      out[i++] = e.value.dx;
      out[i++] = e.value.dy;
    }
  }
  return out;
}

/// Chama um aviso de progresso NATIVO pelo endereco — e assim que o
/// isolate da analise fala com a thread da interface: o endereco de um
/// NativeCallable.listener atravessa o isolate como numero, e a chamada
/// entrega a mensagem no isolate dono.
void avisarProgresso(int endereco, int fase, double fracao, int a, int b) {
  if (endereco == 0) return;
  final f = Pointer<NativeFunction<At2ProgressoNativo>>.fromAddress(endereco)
      .asFunction<void Function(int, double, int, int, Pointer<Void>)>();
  f(fase, fracao, a, b, nullptr);
}

/// Resolve a camera pelo motor 2.0 e traduz a resposta para o contrato
/// do app ([SolucaoCamera3D] com o mundo ja arrumado), ou lanca
/// [RastreioException] com o motivo em portugues.
SolucaoCamera3D resolverCamera3DNativo(
  Float64List observacoes, {
  required int largura,
  required int altura,
  required int quadros,
  required int fps,
  double? focalPx,
  TipoDeTomada tipoDeTomada = TipoDeTomada.auto,
  int pontosSeguidos = 0,
  int enderecoDoProgresso = 0,
}) {
  final r = resolverCenaNativa(
    observacoes: observacoes,
    largura: largura,
    altura: altura,
    quadros: quadros,
    fps: fps,
    focal: focalPx ?? 0,
    tripe: tipoDeTomada == TipoDeTomada.tripe,
    progresso: enderecoDoProgresso,
  );
  switch (r.codigo) {
    case at2Ok:
      break;
    case at2ErrPoucosQuadros:
      throw const RastreioException(
        FalhaDoRastreio.poucosQuadros,
        'Esse trecho é curto demais para rastrear. '
        'Use pelo menos dois segundos de vídeo.',
      );
    case at2ErrPoucosPontos:
      throw const RastreioException(
        FalhaDoRastreio.poucosPontos,
        'Poucos pontos para rastrear. O plano precisa de textura: parede '
        'lisa, céu limpo ou desfoque forte não dão onde agarrar.',
      );
    case at2ErrSemParalaxe:
      throw const RastreioException(
        FalhaDoRastreio.semParalaxe,
        'Não deu para medir profundidade nesse trecho. Filme andando '
        'alguns passos, com coisas perto e longe no quadro.',
      );
    default:
      throw const RastreioException(
        FalhaDoRastreio.naoConvergiu,
        'A câmera não fechou nesse trecho. Tente um trecho sem cortes e '
        'com menos borrão.',
      );
  }
  final poses = <PoseCamera>[
    for (var i = 0; i + 13 <= r.poses.length; i += 13)
      PoseCamera(
        r.poses[i].round(),
        Mat3([for (var k = 1; k <= 9; k++) r.poses[i + k]]),
        [r.poses[i + 10], r.poses[i + 11], r.poses[i + 12]],
      ),
  ];
  final nuvem = <int, List<double>>{};
  final erros = <int, double>{};
  final vistas = <int, int>{};
  for (var i = 0; i + 6 <= r.pontos.length; i += 6) {
    final id = r.pontos[i].round();
    nuvem[id] = [r.pontos[i + 1], r.pontos[i + 2], r.pontos[i + 3]];
    erros[id] = r.pontos[i + 4];
    vistas[id] = r.pontos[i + 5].round();
  }
  if (poses.isEmpty || nuvem.isEmpty) {
    throw const RastreioException(
      FalhaDoRastreio.naoConvergiu,
      'A câmera não fechou nesse trecho.',
    );
  }
  return arrumarMundo(
    SolucaoCamera3D(
      largura: largura,
      altura: altura,
      focalPx: r.focal,
      poses: poses,
      nuvem: nuvem,
      erroPixels: r.erro,
      quadros: quadros,
      fps: fps,
      errosPorPonto: erros,
      vistasPorPonto: vistas,
      pontosSeguidos: pontosSeguidos,
      tipoDeTomada: r.tripe ? TipoDeTomada.tripe : tipoDeTomada,
      distorcao: r.distorcao,
    ),
  );
}

/// SEGUE E RESOLVE a partir do arquivo cru (roda num isolate): le um
/// quadro por vez (sem carregar o video na memoria), segue os pontos no
/// motor e resolve a camera. Devolve os rastros (para o "resolver de
/// novo") e a solucao.
({List<PontoSeguido> pontos, SolucaoCamera3D solucao}) rastrearArquivoCru(
  String caminho, {
  required int largura,
  required int altura,
  required int quadros,
  required int fps,
  double? focalPx,
  TipoDeTomada tipoDeTomada = TipoDeTomada.auto,
  int maximoDePontos = 700,
  int enderecoDoProgresso = 0,
}) {
  final seguidor = SeguidorDePontos2(
    largura,
    altura,
    maximoDePontos: maximoDePontos,
  );
  late final Float64List obs;
  final arquivo = File(caminho).openSync();
  try {
    final quadro = Uint8List(largura * altura);
    for (var q = 0; q < quadros; q++) {
      if (arquivo.readIntoSync(quadro) < quadro.length) break;
      seguidor.empurrar(quadro, q);
      if ((q & 7) == 0) {
        avisarProgresso(
          enderecoDoProgresso,
          1,
          (q + 1) / quadros,
          q + 1,
          quadros,
        );
      }
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
    enderecoDoProgresso: enderecoDoProgresso,
  );
  return (pontos: pontos, solucao: solucao);
}
