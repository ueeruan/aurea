// APRIMORAMENTO POR IA NA EXPORTACAO DO EDITOR.
//
// A exportacao le o clipe marcado com "Aprimorar com IA" na resolucao da
// propria fonte (ver receitasDeExtracao) e, depois da camera lenta, cada
// PNG passa pela rede (native/enhance, ae_process_png): escala x1/x2/x4
// que cobre o tamanho do clipe na composicao, forca escolhida e reducao
// final ao encaixe. O quadro e substituido no lugar, com troca atomica.
//
// Falha aqui NAO vira quadro original com "sucesso": a exportacao para e
// diz qual clipe e por que (export_engine.dart).
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';

import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

import '../../editor/domain/aprimoramento_ia.dart';
import '../../enhance/application/native_enhancer.dart';
import 'interpolacao_rife.dart' show tamanhoDoPng;
import 'modelos_de_ia.dart';
import 'trabalho_de_ia.dart';

/// Quem aprimora os quadros extraidos. A exportacao so conhece isto
/// (testes usam um falso).
abstract class AprimoradorDeQuadros {
  /// A biblioteca com o caminho de PNG carrega neste aparelho.
  bool get disponivel;

  /// Aprimora EM LUGAR cada PNG de [arquivos]: a rede na menor escala que
  /// cobre o encaixe do quadro em [larguraDaComposicao] x
  /// [alturaDaComposicao], com [forca] (0..1), e o resultado reduzido a
  /// esse encaixe. Lanca StateError com o motivo; 'Cancelado' quando
  /// [cancelado] pede.
  Future<void> aprimorar({
    required List<String> arquivos,
    required double forca,
    required int larguraDaComposicao,
    required int alturaDaComposicao,
    PerfilDoAprimoramento perfil = PerfilDoAprimoramento.videoReal,
    double reducaoDeRuido = reducaoDeRuidoPadrao,
    void Function(int feitos, int total)? aoAvancar,
    bool Function()? cancelado,
  });
}

class AprimoradorIa implements AprimoradorDeQuadros {
  AprimoradorIa({
    Future<ByteData> Function(String asset)? carregarAsset,
    Future<Directory> Function()? pastaDeApoio,
    this.modelosProntos,
    this.exigirGpu = true,
  }) : _carregarAsset = carregarAsset ?? rootBundle.load,
       _pastaDeApoio = pastaDeApoio ?? getApplicationSupportDirectory;

  /// So o Android compila o motor (native/CMakeLists.txt).
  static AprimoradorDeQuadros? doAparelho() =>
      Platform.isAndroid ? AprimoradorIa() : null;

  /// Os arquivos de cada perfil, formato ncnn (ver assets/ai/README.md).
  /// A pasta do video real junta os dois modelos: `x4-wdn.bin` e o que
  /// preserva o grao, misturado pela reducao de ruido.
  static const modelosDoPerfil = {
    PerfilDoAprimoramento.videoReal: (
      subpasta: 'realesr-general-x4v3',
      arquivos: {
        'x4.param': 'assets/ai/realesr-general-x4v3/x4.param',
        'x4.bin': 'assets/ai/realesr-general-x4v3/x4.bin',
        'x4-wdn.bin': 'assets/ai/realesr-general-wdn-x4v3/x4.bin',
      },
    ),
    PerfilDoAprimoramento.animacao: (
      subpasta: 'realesr-animevideov3',
      arquivos: {
        'x4.param': 'assets/ai/realesr-animevideov3/x4.param',
        'x4.bin': 'assets/ai/realesr-animevideov3/x4.bin',
      },
    ),
  };

  final Future<ByteData> Function(String asset) _carregarAsset;
  final Future<Directory> Function() _pastaDeApoio;

  /// Pasta que ja tem o modelo de cada perfil (testes no host): pula a
  /// copia dos assets.
  final String Function(PerfilDoAprimoramento perfil)? modelosProntos;

  /// Sem GPU a rede levaria segundos por quadro no celular: e erro, e a
  /// pessoa decide exportar sem o aprimoramento.
  final bool exigirGpu;

  @override
  bool get disponivel => NativeEnhancer.pngAvailable;

  Future<String> prepararModelo(PerfilDoAprimoramento perfil) async {
    final pronto = modelosProntos;
    if (pronto != null) return pronto(perfil);
    final m = modelosDoPerfil[perfil]!;
    return prepararModeloDeIa(
      subpasta: m.subpasta,
      arquivos: m.arquivos,
      carregarAsset: _carregarAsset,
      pastaDeApoio: _pastaDeApoio,
    );
  }

  @override
  Future<void> aprimorar({
    required List<String> arquivos,
    required double forca,
    required int larguraDaComposicao,
    required int alturaDaComposicao,
    PerfilDoAprimoramento perfil = PerfilDoAprimoramento.videoReal,
    double reducaoDeRuido = reducaoDeRuidoPadrao,
    void Function(int feitos, int total)? aoAvancar,
    bool Function()? cancelado,
  }) async {
    if (arquivos.isEmpty) return;
    final modelo = await prepararModelo(perfil);
    await rodarTrabalhoDeIa<_PedidoIa>(
      corpo: _trabalhoIa,
      pedido: (porta, parar) => _PedidoIa(
        porta,
        modelo,
        arquivos,
        forca.isFinite ? forca.clamp(0.0, 1.0) : 1.0,
        larguraDaComposicao,
        alturaDaComposicao,
        parar,
        exigirGpu,
        perfil.index,
        reducaoDeRuido.isFinite
            ? reducaoDeRuido.clamp(0.0, 1.0)
            : reducaoDeRuidoPadrao,
      ),
      total: arquivos.length,
      aoAvancar: aoAvancar,
      cancelado: cancelado,
      interrompido: 'Aprimoramento interrompido',
    );
  }
}

class _PedidoIa {
  const _PedidoIa(
    this.porta,
    this.modelo,
    this.arquivos,
    this.forca,
    this.largura,
    this.altura,
    this.enderecoDeParar,
    this.exigirGpu,
    this.perfil,
    this.reducaoDeRuido,
  );
  final SendPort porta;
  final String modelo;
  final List<String> arquivos;
  final double forca;
  final int largura;
  final int altura;
  final int enderecoDeParar;
  final bool exigirGpu;
  final int perfil;
  final double reducaoDeRuido;
}

void _trabalhoIa(_PedidoIa p) {
  final parar = Pointer<Int32>.fromAddress(p.enderecoDeParar);
  NativeEnhancer? motor;
  try {
    for (var k = 0; k < p.arquivos.length; k++) {
      if (parar.value != 0) {
        p.porta.send('Cancelado');
        return;
      }
      final arquivo = p.arquivos[k];
      final f = File(arquivo).openSync();
      final Uint8List cabeca;
      try {
        cabeca = f.readSync(24);
      } finally {
        f.closeSync();
      }
      final tamanho = tamanhoDoPng(cabeca);
      if (tamanho == null) throw StateError('Quadro ilegível para a IA');
      final (w, h) = tamanho;
      final (fw, fh) = encaixar(w, h, p.largura, p.altura);
      final escala = escalaDaIa(w, h, fw, fh);
      motor ??= _abrirMotor(p);
      motor.processPng(
        arquivo,
        arquivo,
        scale: escala,
        strength: p.forca,
        fitW: fw,
        fitH: fh,
        parar: parar,
      );
      p.porta.send(k + 1);
    }
    p.porta.send(true);
  } catch (e) {
    p.porta.send(e is StateError ? e.message : '$e');
  } finally {
    motor?.close();
  }
}

NativeEnhancer _abrirMotor(_PedidoIa p) {
  final motor = NativeEnhancer.openProfile(
    p.modelo,
    PerfilDoAprimoramento.values[p.perfil],
    reducaoDeRuido: p.reducaoDeRuido,
    gpu: p.exigirGpu,
  );
  if (p.exigirGpu && !motor.info.gpu) {
    motor.close();
    throw StateError('sem GPU Vulkan para a IA neste aparelho');
  }
  return motor;
}
