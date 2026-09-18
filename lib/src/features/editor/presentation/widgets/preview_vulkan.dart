import 'dart:io' show Platform;

import 'package:aurea_render/aurea_render.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

/// LIGA O PREVIEW PELO MOTOR C++ NATIVO.
///
/// DESLIGADO POR PADRAO, e de proposito: o motor compoe um MAPA da cena, e
/// nao a imagem dela (ver a limitacao no comentario da classe). Ligar isto
/// troca a previa completa do Flutter por um mapa de composicao — util
/// para provar e medir o caminho nativo, e nao ainda para editar.
///
/// Quem acompanha o projeto (custo por quadro, falhas, texturas) tem os
/// numeros no selo do canto e em `PreviewVulkan.estatisticas()`.
final motorDoPreviewProvider = StateProvider<bool>((ref) => false);

/// O PREVIEW DESENHADO PELO RENDERCORE C++ (V1).
///
/// ============================== O QUE ISTO E =========================
/// O caminho completo de um quadro ate a tela, feito pelo motor nativo: o
/// Flutter entrega uma janela (`SurfaceProducer`), o C++ cria a superficie
/// Vulkan e a swapchain, e APRESENTA. O Flutter fica com o `Texture`, que
/// e um retangulo — nao ha arvore de widgets desenhando a composicao.
///
/// ======================= V1.1: O QUADRO E DO MOTOR ====================
/// O CONTEUDO VEM DO COMPOSITOR C++. A cada quadro: a cena e publicada no
/// `Nucleo` (que e C++), o `Nucleo` compoe na memoria dele, os bytes sobem
/// para a GPU por `apresentarImagem` e a swapchain apresenta. O Flutter
/// NAO desenha a composicao — ele entrega a janela e pede quadros.
///
/// O QUE ELE AINDA NAO DESENHA: o conteudo de verdade das camadas. A cena
/// publicada e feita de RETANGULOS com a cor, a posicao, a escala e a
/// opacidade de cada camada visivel — um mapa de composicao, e nao a
/// imagem dela. Video, texto, forma e 3D entram transformando cada camada
/// numa textura, que e a FASE 3; o que esta provado aqui e o caminho
/// inteiro (estado -> avaliador -> compositor -> GPU -> tela) com o
/// conteudo mais simples que existe.
///
/// O QUADRO E COMPOSTO NUM TAMANHO DE TRABALHO proprio, e nao no tamanho
/// da composicao: o compositor de referencia e CPU, e compor 1080x1920 em
/// CPU a cada quadro seria trocar um problema por outro. A swapchain
/// AMPLIA com filtro linear no `vkCmdBlitImage`.
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
    this.cena,
    this.larguraDoQuadro = 320,
    this.alturaDoQuadro = 180,
  });

  /// Em pixels fisicos da composicao (o tamanho da SUPERFICIE).
  final int largura;
  final int altura;

  /// Falso pausa os pedidos de quadro (o preview esta fora da tela).
  final bool ativo;

  /// A CENA, do jeito que o motor entende — lida a cada quadro, porque e
  /// ela que muda quando a pessoa edita. Nula = so a tubulacao (V1).
  final List<CamadaDeRender> Function()? cena;

  /// O TAMANHO DE TRABALHO do compositor, em pixels. Menor que a
  /// superficie de proposito: quem compoe e a CPU hoje, e o custo cresce
  /// com a area. Ver o comentario da classe.
  final int larguraDoQuadro;
  final int alturaDoQuadro;

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
  int _compostas = 0;
  bool _falhou = false;

  /// O MOTOR C++ DO PREVIEW: um so por widget, aberto no tamanho de
  /// trabalho. O buffer de leitura e dele, e nao ha alocacao por quadro.
  NucleoRender? _nucleo;

  /// O buffer que recebe o quadro composto, alocado UMA vez.
  Uint8List? _quadro;

  /// O MOTIVO de o motor nao ter subido, quando nao subiu. Vazio = subiu.
  String _motivoDoMotor = '';

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
      if (!_falhou) _abrirMotor();
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

  /// ABRE O MOTOR. Falhar aqui NAO derruba o app: o preview cai no aviso e
  /// o caminho antigo do Flutter continua valendo.
  void _abrirMotor() {
    if (widget.cena == null) return;  // modo tubulacao (V1)
    final nucleo = NucleoRender.abrir(
      largura: widget.larguraDoQuadro,
      altura: widget.alturaDoQuadro,
      backend: 0,
      comThread: false,  // quem manda o ritmo e o ticker do preview
      orcamentoMs: 1000 / 60,
    );
    if (nucleo == null) {
      _motivoDoMotor = NucleoRender.ultimoErro.isEmpty
          ? 'motor indisponivel'
          : NucleoRender.ultimoErro;
      _falhou = true;
      return;
    }
    _nucleo = nucleo;
    _quadro = Uint8List(
      widget.larguraDoQuadro * widget.alturaDoQuadro * 4,
    );
  }

  /// UM QUADRO: publica a cena, compoe no C++ e sobe para a GPU.
  ///
  /// Nada aqui desenha a composicao no Flutter — a unica coisa que chega a
  /// tela e o `Texture`, alimentado pelo motor.
  void _comporUmQuadro() {
    final nucleo = _nucleo;
    final buffer = _quadro;
    if (nucleo == null || buffer == null) return;
    final camadas = widget.cena!();
    if (!nucleo.publicarCena(
      camadas,
      largura: widget.larguraDoQuadro,
      altura: widget.alturaDoQuadro,
    )) {
      return;
    }
    _compostas = nucleo.desenharAgora();
    if (_compostas < 0) return;
    if (nucleo.lerPixelsEm(buffer) == 0) return;
    _ultimo = PreviewVulkan.apresentarImagem(
      buffer,
      widget.larguraDoQuadro,
      widget.alturaDoQuadro,
    );
  }

  void _ligarTicker() {
    _ticker?.dispose();
    _ticker = createTicker((_) {
      if (!widget.ativo || _falhou) return;
      _quadros++;
      if (_nucleo != null) {
        _comporUmQuadro();
        return;
      }
      // MODO TUBULACAO (V1, sem cena): a cor muda a cada quadro de
      // proposito — uma cor parada nao prova que os quadros estao
      // chegando, provaria so que a tela ficou pintada uma vez.
      _ultimo = PreviewVulkan.apresentar(corDeTeste(_quadros));
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
    // O MOTOR PRIMEIRO, A JANELA DEPOIS. O motor tem buffer e imagem
    // dentro da GPU; soltar a superficie antes deixaria os dois presos a
    // um dispositivo que ninguem mais usa.
    _nucleo?.fechar();
    _nucleo = null;
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
      final texto = _motivo.isNotEmpty
          ? _motivo
          : (_motivoDoMotor.isNotEmpty
                ? _motivoDoMotor
                : 'preview nativo indisponivel');
      return _Aviso(motivo: texto);
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
