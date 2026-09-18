import 'dart:io' show Platform;

import 'package:aurea_render/aurea_render.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';

/// O PREVIEW DESENHADO PELO RENDERCORE C++ (V1).
///
/// ============================== O QUE ISTO E =========================
/// O caminho completo de um quadro ate a tela, feito pelo motor nativo: o
/// Flutter entrega uma janela (`SurfaceProducer`), o C++ cria a superficie
/// Vulkan e a swapchain, e APRESENTA. O Flutter fica com o `Texture`, que
/// e um retangulo — nao ha arvore de widgets desenhando a composicao.
///
/// ============================== V1: SO UMA COR =======================
/// O conteudo do quadro e uma cor solida. O compositor em shader e o V2, e
/// misturar os dois agora deixaria a tela preta sem dizer se o erro foi a
/// cor errada ou a swapchain recusada. O V1 existe para provar a tubulacao:
/// superficie, swapchain, sincronizacao, apresentacao e RECRIACAO.
///
/// ============================== QUEM MANDA E O C++ ===================
/// O ticker daqui so PEDE quadros; quem decide se a swapchain precisa ser
/// refeita e o motor, e a resposta dele (-2 = refeita, descarte este) e
/// respeitada em vez de tratada como erro.
class PreviewNativo extends StatefulWidget {
  const PreviewNativo({
    super.key,
    required this.largura,
    required this.altura,
    this.ativo = true,
  });

  /// Em pixels fisicos da composicao.
  final int largura;
  final int altura;

  /// Falso pausa os pedidos de quadro (o preview esta fora da tela).
  final bool ativo;

  /// Um aparelho onde o V1 pode rodar. O iOS nao tem este caminho ainda.
  static bool get suportado => Platform.isAndroid;

  @override
  State<PreviewNativo> createState() => _PreviewNativoState();
}

class _PreviewNativoState extends State<PreviewNativo>
    with SingleTickerProviderStateMixin {
  static const _canal = MethodChannel('aurea/render');

  Ticker? _ticker;
  int? _textura;
  String _motivo = '';
  int _ultimo = 0;
  int _quadros = 0;
  bool _falhou = false;

  @override
  void initState() {
    super.initState();
    _abrir();
  }

  Future<void> _abrir() async {
    try {
      final resposta = await _canal.invokeMapMethod<String, Object?>(
        'criar',
        {'largura': widget.largura, 'altura': widget.altura},
      );
      if (!mounted) return;
      final id = resposta?['id'];
      final ok = resposta?['ok'] == true;
      setState(() {
        _textura = id is int ? id : null;
        _falhou = !ok || _textura == null;
        _motivo = ok ? '' : PreviewVulkan.motivo;
      });
      if (!_falhou) _ligarTicker();
    } catch (e) {
      // SEM SUPERFICIE NAO HA PREVIEW NATIVO — e o app NAO cai por isso.
      // A tela diz o motivo e o caminho antigo continua valendo.
      if (!mounted) return;
      setState(() {
        _falhou = true;
        _motivo = '$e';
      });
    }
  }

  void _ligarTicker() {
    _ticker?.dispose();
    _ticker = createTicker((_) {
      if (!widget.ativo || _falhou) return;
      // A COR MUDA A CADA QUADRO DE PROPOSITO: uma cor parada nao prova
      // que os quadros estao chegando — provaria so que a tela ficou
      // pintada uma vez.
      final cor = corDeTeste(_quadros);
      _ultimo = PreviewVulkan.apresentar(cor);
      _quadros++;
    })..start();
  }

  @override
  void didUpdateWidget(PreviewNativo old) {
    super.didUpdateWidget(old);
    if (old.largura != widget.largura || old.altura != widget.altura) {
      // O TAMANHO MUDOU (rotacao, divisor arrastado). O motor decide
      // quando refazer a swapchain: o tamanho real vem da superficie, e
      // nao do que o Flutter pediu.
      PreviewVulkan.redimensionar(widget.largura, widget.altura);
    }
    if (old.ativo != widget.ativo) {
      if (widget.ativo) {
        _ligarTicker();
      } else {
        _ticker?.stop();
      }
    }
  }

  @override
  void dispose() {
    _ticker?.dispose();
    // SOLTA A JANELA E A SWAPCHAIN. Um `SurfaceProducer` nao liberado
    // mantem a janela viva e a GPU presa a ela.
    _canal.invokeMethod<void>('liberar').catchError((_) {});
    super.dispose();
  }

  /// A COR DE TESTE DO V1, com os canais separados para uma troca de R e B
  /// nao passar despercebida.
  static int corDeTeste(int semente) {
    final r = (semente * 37) & 0xFF;
    final g = 0x40 + ((semente * 11) & 0x7F);
    final b = 0xFF - ((semente * 53) & 0x7F);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
  }

  @override
  Widget build(BuildContext context) {
    if (_falhou || _textura == null) {
      return _Aviso(motivo: _motivo.isEmpty ? 'preview nativo indisponivel' : _motivo);
    }
    return Stack(
      fit: StackFit.expand,
      children: [
        Texture(textureId: _textura!),
        // O ESTADO FICA VISIVEL: um preview que nao apresenta nada e uma
        // tela parada, e sem o numero na tela nao ha como distinguir "o
        // motor parou" de "a cor de agora e essa".
        Positioned(
          left: 6,
          bottom: 4,
          child: Text(
            'V1 vulkan · ${_quadros}q · ultimo=$_ultimo',
            style: const TextStyle(fontSize: 10, color: Color(0x88FFFFFF)),
          ),
        ),
      ],
    );
  }
}

class _Aviso extends StatelessWidget {
  const _Aviso({required this.motivo});

  final String motivo;

  @override
  Widget build(BuildContext context) => ColoredBox(
    color: const Color(0xFF14161C),
    child: Center(
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Text(
          motivo,
          textAlign: TextAlign.center,
          style: const TextStyle(fontSize: 12, color: Color(0xFF8A93A6)),
        ),
      ),
    ),
  );
}
