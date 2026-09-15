import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';

import 'am_colors.dart';
import 'am_widgets.dart' show previewStageKey;

/// CONTA-GOTAS: fotografa o palco, mostra a foto no mesmo lugar e deixa o
/// dedo passear por ela com uma lupa da cor embaixo. Soltar escolhe; o X
/// desiste. Devolve nulo quando nao ha palco (fora do editor) ou quando a
/// pessoa desiste.
Future<Color?> pegarCorDoPalco(BuildContext context) async {
  final alvo = previewStageKey.currentContext?.findRenderObject();
  if (alvo is! RenderRepaintBoundary || !alvo.attached || !alvo.hasSize) {
    return null;
  }
  final origem = alvo.localToGlobal(Offset.zero);
  final tamanho = alvo.size;
  await WidgetsBinding.instance.endOfFrame;
  if (!alvo.attached) return null;
  final imagem = await alvo.toImage(pixelRatio: 1);
  final dados = await imagem.toByteData(format: ui.ImageByteFormat.rawRgba);
  if (dados == null || !context.mounted) {
    imagem.dispose();
    return null;
  }
  final cor = await Navigator.of(context).push<Color>(
    PageRouteBuilder<Color>(
      opaque: false,
      barrierColor: Colors.black54,
      pageBuilder: (_, _, _) => Material(
        type: MaterialType.transparency,
        child: _ContaGotas(
          imagem: imagem,
          dados: dados,
          origem: origem,
          tamanho: tamanho,
        ),
      ),
    ),
  );
  // A foto e descartada pela propria tela, quando ela sai de vez: a
  // animacao de saida ainda pinta a imagem depois de o push voltar.
  return cor;
}

/// Ha palco montado para o conta-gotas fotografar? Fora do editor (um
/// seletor aberto noutra tela) o botao nem aparece.
bool palcoParaContaGotas() {
  final alvo = previewStageKey.currentContext?.findRenderObject();
  return alvo is RenderRepaintBoundary && alvo.attached;
}

/// A cor do pixel em [p] (coordenadas da foto), ou nulo fora dela.
Color? corNoPixel(ByteData dados, int largura, int altura, Offset p) {
  final x = p.dx.floor();
  final y = p.dy.floor();
  if (x < 0 || y < 0 || x >= largura || y >= altura) return null;
  final i = (y * largura + x) * 4;
  if (i + 3 >= dados.lengthInBytes) return null;
  return Color.fromARGB(
    dados.getUint8(i + 3),
    dados.getUint8(i),
    dados.getUint8(i + 1),
    dados.getUint8(i + 2),
  );
}

class _ContaGotas extends StatefulWidget {
  const _ContaGotas({
    required this.imagem,
    required this.dados,
    required this.origem,
    required this.tamanho,
  });

  final ui.Image imagem;
  final ByteData dados;
  final Offset origem;
  final Size tamanho;

  @override
  State<_ContaGotas> createState() => _ContaGotasState();
}

class _ContaGotasState extends State<_ContaGotas> {
  Offset? _dedo;
  Color? _cor;

  @override
  void dispose() {
    widget.imagem.dispose();
    super.dispose();
  }

  void _mover(Offset local) {
    final sx = widget.imagem.width / widget.tamanho.width;
    final sy = widget.imagem.height / widget.tamanho.height;
    final cor = corNoPixel(
      widget.dados,
      widget.imagem.width,
      widget.imagem.height,
      Offset(local.dx * sx, local.dy * sy),
    );
    setState(() {
      _dedo = local;
      if (cor != null) _cor = cor.withValues(alpha: 1);
    });
  }

  void _soltar() {
    final cor = _cor;
    if (cor != null) Navigator.of(context).pop(cor);
  }

  @override
  Widget build(BuildContext context) {
    final dedo = _dedo;
    final cor = _cor;
    return Stack(
      children: [
        Positioned.fromRect(
          rect: widget.origem & widget.tamanho,
          child: GestureDetector(
            key: const ValueKey('conta-gotas-palco'),
            behavior: HitTestBehavior.opaque,
            onPanDown: (d) => _mover(d.localPosition),
            onPanUpdate: (d) => _mover(d.localPosition),
            onPanEnd: (_) => _soltar(),
            onTapUp: (d) {
              _mover(d.localPosition);
              _soltar();
            },
            child: Stack(
              clipBehavior: Clip.none,
              children: [
                Positioned.fill(
                  child: RawImage(image: widget.imagem, fit: BoxFit.fill),
                ),
                if (dedo != null && cor != null) ...[
                  Positioned(
                    left: dedo.dx - 8,
                    top: dedo.dy - 8,
                    child: IgnorePointer(
                      child: Container(
                        width: 16,
                        height: 16,
                        decoration: BoxDecoration(
                          shape: BoxShape.circle,
                          border: Border.all(color: Colors.white, width: 2),
                        ),
                      ),
                    ),
                  ),
                  Positioned(
                    left: dedo.dx - 25,
                    top: dedo.dy - 86,
                    child: IgnorePointer(
                      child: Container(
                        key: const ValueKey('conta-gotas-lupa'),
                        width: 50,
                        height: 50,
                        decoration: BoxDecoration(
                          color: cor,
                          shape: BoxShape.circle,
                          border: Border.all(color: Colors.white, width: 3),
                          boxShadow: const [
                            BoxShadow(color: Colors.black45, blurRadius: 8),
                          ],
                        ),
                      ),
                    ),
                  ),
                ],
              ],
            ),
          ),
        ),
        Positioned(
          left: 16,
          right: 16,
          top: widget.origem.dy + widget.tamanho.height + 16,
          child: Row(
            children: [
              const Expanded(
                child: AppText(
                  'Arraste sobre o palco e solte na cor que quer.',
                  style: TextStyle(color: AmColors.text, fontSize: 14),
                ),
              ),
              CupertinoButton(
                key: const ValueKey('conta-gotas-cancelar'),
                padding: EdgeInsets.zero,
                onPressed: () => Navigator.of(context).pop(),
                child: const Icon(
                  CupertinoIcons.xmark_circle_fill,
                  size: 30,
                  color: AmColors.text,
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}
