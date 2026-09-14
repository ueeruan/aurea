import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart' show ValueListenable;
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

import '../../../domain/amostra_dos_efeitos.dart';
import '../../../domain/effect.dart';
import 'effect_thumbnail.dart';

/// AS TIRAS DAS PREVIAS no pacote do app: o manifesto (qual preset cada
/// efeito usou) e as imagens, com teto em memoria.
///
/// Uma tira de 8 quadros de 200 px ocupa 1,3 MB decodificada; 24 delas
/// cobrem a grade visivel com folga sem pesar no iPhone.
class PreviasDosEfeitos {
  PreviasDosEfeitos._();

  static final PreviasDosEfeitos instance = PreviasDosEfeitos._();

  /// Nos testes a animacao nao corre: um relogio que nunca para impede o
  /// `pumpAndSettle` de terminar.
  static bool animar = !Platform.environment.containsKey('FLUTTER_TEST');

  static const int _teto = 24;

  Map<String, int?>? _presets;

  /// O manifesto ja foi procurado (achado ou nao). Sem ele, a galeria usa a
  /// miniatura antiga em vez de ficar com o quadrado vazio.
  bool manifestoLido = false;
  Future<Map<String, int?>?>? _lendoManifesto;
  final LinkedHashMap<String, ui.Image> _imagens = LinkedHashMap();
  final Map<String, Future<ui.Image?>> _lendo = {};

  Future<Map<String, int?>?> manifesto() {
    if (_presets != null) return Future.value(_presets);
    return _lendoManifesto ??= () async {
      try {
        final texto = await rootBundle.loadString(
          '$pastaDasPrevias/manifesto.json',
        );
        final m = jsonDecode(texto) as Map<String, dynamic>;
        if (m['versao'] != versaoDasPrevias) return null;
        final efeitos = m['efeitos'] as Map<String, dynamic>;
        return _presets = {
          for (final e in efeitos.entries)
            e.key: (e.value as Map<String, dynamic>)['preset'] as int?,
        };
      } catch (_) {
        return null;
      } finally {
        manifestoLido = true;
      }
    }();
  }

  /// O preset que a previa mostrou, para o toque entregar o mesmo efeito.
  /// Nulo sem manifesto ou quando o efeito usou os valores iniciais.
  EffectPronto? prontoDaPrevia(EffectType tipo) {
    final spec = effectSpecs[tipo]!;
    final i = _presets?[spec.id];
    if (i == null || i < 0 || i >= spec.presets.length) return null;
    return spec.presets[i];
  }

  bool temPrevia(EffectType tipo) =>
      _presets?.containsKey(effectSpecs[tipo]!.id) ?? false;

  ui.Image? imagemPronta(String id) {
    final img = _imagens.remove(id);
    if (img != null) _imagens[id] = img;
    return img;
  }

  Future<ui.Image?> imagem(String id) {
    final pronta = imagemPronta(id);
    if (pronta != null) return Future.value(pronta);
    return _lendo.putIfAbsent(id, () async {
      try {
        final dados = await rootBundle.load('$pastaDasPrevias/$id.jpg');
        final codec = await ui.instantiateImageCodec(
          dados.buffer.asUint8List(),
        );
        final quadro = await codec.getNextFrame();
        codec.dispose();
        _imagens[id] = quadro.image;
        while (_imagens.length > _teto) {
          final velho = _imagens.keys.first;
          _imagens.remove(velho)?.dispose();
        }
        return quadro.image;
      } catch (_) {
        return null;
      } finally {
        _lendo.remove(id);
      }
    });
  }
}

/// O RELOGIO DAS PREVIAS: um so para a grade inteira, para todos os tiles
/// trocarem de quadro juntos e so enquanto a galeria esta aberta.
class RelogioDasPrevias extends StatefulWidget {
  const RelogioDasPrevias({super.key, required this.child});

  final Widget child;

  static ValueListenable<int>? de(BuildContext context) => context
      .dependOnInheritedWidgetOfExactType<_RelogioHerdado>()
      ?.quadro;

  @override
  State<RelogioDasPrevias> createState() => _RelogioDasPreviasState();
}

class _RelogioDasPreviasState extends State<RelogioDasPrevias>
    with SingleTickerProviderStateMixin {
  final _quadro = ValueNotifier<int>(0);
  Ticker? _ticker;

  @override
  void initState() {
    super.initState();
    PreviasDosEfeitos.instance.manifesto().then((_) {
      if (mounted) setState(() {});
    });
    if (PreviasDosEfeitos.animar) {
      _ticker = createTicker((passou) {
        final q =
            (passou.inMicroseconds * fpsDaPrevia ~/ 1000000) % quadrosDaPrevia;
        if (q != _quadro.value) _quadro.value = q;
      })..start();
    }
  }

  @override
  void dispose() {
    _ticker?.dispose();
    _quadro.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) =>
      _RelogioHerdado(quadro: _quadro, child: widget.child);
}

class _RelogioHerdado extends InheritedWidget {
  const _RelogioHerdado({required this.quadro, required super.child});

  final ValueListenable<int> quadro;

  @override
  bool updateShouldNotify(_RelogioHerdado old) => old.quadro != quadro;
}

/// A PREVIA DE UM EFEITO: a tira tocando em loop. Sem tira no pacote (efeito
/// novo ainda sem previa gerada), a miniatura antiga.
class PreviaDoEfeito extends StatefulWidget {
  const PreviaDoEfeito({super.key, required this.tipo, required this.lado});

  final EffectType tipo;
  final double lado;

  @override
  State<PreviaDoEfeito> createState() => _PreviaDoEfeitoState();
}

class _PreviaDoEfeitoState extends State<PreviaDoEfeito> {
  ui.Image? _tira;

  String get _id => effectSpecs[widget.tipo]!.id;

  @override
  void initState() {
    super.initState();
    _carregar();
  }

  @override
  void didUpdateWidget(PreviaDoEfeito old) {
    super.didUpdateWidget(old);
    if (old.tipo != widget.tipo) {
      _tira = null;
      _carregar();
    }
  }

  void _carregar() {
    final previas = PreviasDosEfeitos.instance;
    _tira = previas.imagemPronta(_id);
    if (_tira != null) return;
    previas.manifesto().then((m) {
      if (m == null || !m.containsKey(_id) || !mounted) {
        if (mounted) setState(() {});
        return;
      }
      previas.imagem(_id).then((img) {
        if (mounted) setState(() => _tira = img);
      });
    });
  }

  @override
  Widget build(BuildContext context) {
    final lado = widget.lado;
    final tira = _tira;
    final previas = PreviasDosEfeitos.instance;
    if (tira == null) {
      // Manifesto lido e sem esta previa: a miniatura antiga faz o papel.
      if (previas.manifestoLido && !previas.temPrevia(widget.tipo)) {
        return EffectThumbnail(type: widget.tipo, size: lado);
      }
      return SizedBox(
        width: lado,
        height: lado,
        child: const DecoratedBox(
          decoration: BoxDecoration(
            color: Color(0xFF262C36),
            borderRadius: BorderRadius.all(Radius.circular(10)),
          ),
        ),
      );
    }
    final relogio = RelogioDasPrevias.de(context);
    Widget pintura(int quadro) => CustomPaint(
      key: ValueKey('miniatura-$_id'),
      size: Size(lado, lado),
      painter: _PintorDaTira(tira, quadro),
    );
    return ClipRRect(
      borderRadius: BorderRadius.circular(10),
      child: RepaintBoundary(
        child: relogio == null
            ? pintura(0)
            : ValueListenableBuilder<int>(
                valueListenable: relogio,
                builder: (_, q, _) => pintura(q),
              ),
      ),
    );
  }
}

class _PintorDaTira extends CustomPainter {
  _PintorDaTira(this.tira, this.quadro);

  final ui.Image tira;
  final int quadro;

  @override
  void paint(Canvas canvas, Size size) {
    final ladoDaFonte = tira.height.toDouble();
    final n = (tira.width / ladoDaFonte).floor().clamp(1, 64);
    final q = quadro % n;
    canvas.drawImageRect(
      tira,
      Rect.fromLTWH(q * ladoDaFonte, 0, ladoDaFonte, ladoDaFonte),
      Offset.zero & size,
      Paint()..filterQuality = FilterQuality.medium,
    );
  }

  @override
  bool shouldRepaint(_PintorDaTira old) =>
      old.quadro != quadro || !identical(old.tira, tira);
}
