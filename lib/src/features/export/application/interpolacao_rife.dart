// CAMERA LENTA COM IA NA EXPORTACAO.
//
// A exportacao ja le cada clipe para PNGs e escolhe o quadro pela taxa de
// extracao (export_engine.dart). Para um clipe lento com interpolacao
// "movimento", a fonte e lida na taxa dela e o RIFE (native/enhance,
// aurea_rife.cpp) preenche os quadros do meio, com os mesmos nomes que o
// FFmpeg minterpolate escreveria. Qualquer falha (sem GPU, pouca memoria,
// quadro ilegivel) volta para o FFmpeg — nada muda para quem desenha.
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';

import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

import '../../editor/domain/plano_de_interpolacao.dart';
import '../../enhance/application/native_interpolator.dart';
import 'modelos_de_ia.dart';
import 'trabalho_de_ia.dart';

/// Quem faz os quadros do meio. A exportacao so conhece isto (testes
/// usam um falso).
abstract class InterpoladorDeQuadros {
  /// A biblioteca com o RIFE carrega neste aparelho.
  bool get disponivel;

  /// Escreve em [pastaSaida] um PNG por passo de [plano] (`%06d.png`, o
  /// indice de saida), lendo os quadros reais `%06d.png` de [pastaBase].
  /// Lanca StateError com o motivo; 'Cancelado' quando [cancelado] pede.
  Future<void> interpolar({
    required String pastaBase,
    required String pastaSaida,
    required List<PassoDeInterpolacao> plano,
    void Function(int feitos, int total)? aoAvancar,
    bool Function()? cancelado,
  });
}

/// MEMORIA DE GPU que o RIFE v4.6 pede para um quadro w x h, com folga de
/// 2x. Medido no host (RTX 3050, fp16): modelo 77 MB; 720p +184 MB e
/// 1080p +397 MB de trabalho, ~210 bytes por pixel.
int memoriaMinimaDaGpuMb(int largura, int altura) =>
    2 * (80 + (largura * altura * 210) ~/ (1024 * 1024));

/// Largura e altura do cabecalho de um PNG (IHDR), ou nulo.
(int, int)? tamanhoDoPng(Uint8List cabeca) {
  const assinatura = [137, 80, 78, 71, 13, 10, 26, 10];
  if (cabeca.length < 24) return null;
  for (var i = 0; i < 8; i++) {
    if (cabeca[i] != assinatura[i]) return null;
  }
  final d = ByteData.sublistView(cabeca);
  final w = d.getUint32(16), h = d.getUint32(20);
  if (w == 0 || h == 0) return null;
  return (w, h);
}

String _nome(int i) => i.toString().padLeft(6, '0');

class InterpoladorRife implements InterpoladorDeQuadros {
  InterpoladorRife({
    Future<ByteData> Function(String asset)? carregarAsset,
    Future<Directory> Function()? pastaDeApoio,
    this.modeloPronto,
    this.exigirGpu = true,
  }) : _carregarAsset = carregarAsset ?? rootBundle.load,
       _pastaDeApoio = pastaDeApoio ?? getApplicationSupportDirectory;

  /// So o Android empacota a biblioteca e o modelo (assets com
  /// `platforms: [android]` no pubspec).
  static InterpoladorDeQuadros? doAparelho() =>
      Platform.isAndroid ? InterpoladorRife() : null;

  /// rife-v4.6 do rife-ncnn-vulkan (ver assets/ai/README.md).
  static const modeloAssets = {
    'flownet.param': 'assets/ai/rife-v4.6/flownet.param',
    'flownet.bin': 'assets/ai/rife-v4.6/flownet.bin',
  };

  final Future<ByteData> Function(String asset) _carregarAsset;
  final Future<Directory> Function() _pastaDeApoio;

  /// Pasta que ja tem o modelo (testes no host): pula a copia dos assets.
  final String? modeloPronto;

  /// Sem GPU nao ha RIFE no celular: a CPU levaria segundos por quadro.
  final bool exigirGpu;

  @override
  bool get disponivel => NativeInterpolator.libraryAvailable;

  /// Copia o modelo dos assets para a pasta de apoio uma vez (o C++ le
  /// arquivo, nao asset). Arquivo com tamanho diferente e refeito.
  Future<String> prepararModelo() async =>
      modeloPronto ??
      await prepararModeloDeIa(
        subpasta: 'rife-v4.6',
        arquivos: modeloAssets,
        carregarAsset: _carregarAsset,
        pastaDeApoio: _pastaDeApoio,
      );

  @override
  Future<void> interpolar({
    required String pastaBase,
    required String pastaSaida,
    required List<PassoDeInterpolacao> plano,
    void Function(int feitos, int total)? aoAvancar,
    bool Function()? cancelado,
  }) async {
    if (plano.isEmpty) return;
    final modelo = plano.every((p) => p.copia) ? '' : await prepararModelo();
    final passos = Int32List(plano.length * 3);
    final instantes = Float64List(plano.length);
    for (var k = 0; k < plano.length; k++) {
      passos[k * 3] = plano[k].saida;
      passos[k * 3 + 1] = plano[k].a;
      passos[k * 3 + 2] = plano[k].b;
      instantes[k] = plano[k].t;
    }
    await rodarTrabalhoDeIa<_Pedido>(
      corpo: _trabalhoRife,
      pedido: (porta, parar) => _Pedido(
        porta,
        modelo,
        pastaBase,
        pastaSaida,
        passos,
        instantes,
        parar,
        exigirGpu,
      ),
      total: plano.length,
      aoAvancar: aoAvancar,
      cancelado: cancelado,
      interrompido: 'Interpolação interrompida',
    );
  }
}

class _Pedido {
  const _Pedido(
    this.porta,
    this.modelo,
    this.base,
    this.saida,
    this.passos,
    this.instantes,
    this.enderecoDeParar,
    this.exigirGpu,
  );
  final SendPort porta;
  final String modelo;
  final String base;
  final String saida;
  final Int32List passos;
  final Float64List instantes;
  final int enderecoDeParar;
  final bool exigirGpu;
}

void _trabalhoRife(_Pedido p) {
  final parar = Pointer<Int32>.fromAddress(p.enderecoDeParar);
  NativeInterpolator? motor;
  try {
    for (var k = 0; k < p.instantes.length; k++) {
      if (parar.value != 0) {
        p.porta.send('Cancelado');
        return;
      }
      final i = p.passos[k * 3], a = p.passos[k * 3 + 1], b = p.passos[k * 3 + 2];
      final t = p.instantes[k];
      final origem = '${p.base}/${_nome(a)}.png';
      final destino = '${p.saida}/${_nome(i)}.png';
      if (t == 0) {
        File(origem).copySync(destino);
      } else {
        motor ??= _abrirMotor(p, origem);
        motor.interpolatePng(origem, '${p.base}/${_nome(b)}.png', t, destino);
      }
      p.porta.send(k + 1);
    }
    p.porta.send(true);
  } catch (e) {
    p.porta.send(e is StateError ? e.message : '$e');
  } finally {
    motor?.close();
  }
}

NativeInterpolator _abrirMotor(_Pedido p, String primeiro) {
  final motor = NativeInterpolator.open(p.modelo, gpu: p.exigirGpu);
  final info = motor.info;
  if (info.gpu) {
    final f = File(primeiro).openSync();
    final Uint8List cabeca;
    try {
      cabeca = f.readSync(24);
    } finally {
      f.closeSync();
    }
    final tamanho = tamanhoDoPng(cabeca);
    if (tamanho == null) {
      motor.close();
      throw StateError('Quadro ilegível para o RIFE');
    }
    final minimo = memoriaMinimaDaGpuMb(tamanho.$1, tamanho.$2);
    if (info.heapBudgetMb > 0 && info.heapBudgetMb < minimo) {
      motor.close();
      throw StateError(
        'GPU com pouca memória para o RIFE em ${tamanho.$1}x${tamanho.$2} '
        '(${info.heapBudgetMb} MB, precisa de $minimo MB)',
      );
    }
  }
  return motor;
}
