import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../core/storage/prefs.dart';
import '../../../../core/ui/snack.dart';
import '../../../../core/ui/tocavel.dart';
import '../../domain/codigo_de_cor.dart';
import '../context/parameter_row.dart' show showNumberInput;
import 'am_colors.dart';
import 'conta_gotas.dart';

/// SELETOR DE COR — qualquer cor, nao uma paleta fixa.
///
/// Tres jeitos de escolher (quadro de saturacao e brilho, roda de matiz,
/// canais RGB), alfa, codigo para copiar e colar, conta-gotas do palco,
/// a cor ORIGINAL ao lado da NOVA (tocar a original volta a ela) e as
/// cores guardadas da pessoa. Devolve a cor viva enquanto se arrasta
/// (via [onChanged]), porque escolher cor olhando o resultado e diferente
/// de escolher e so depois ver.
Future<Color?> showColorPicker(
  BuildContext context, {
  required Color initial,
  ValueChanged<Color>? onChanged,
  bool withAlpha = true,
  List<Color> recent = const [],
}) {
  return showModalBottomSheet<Color>(
    context: context,
    backgroundColor: AmColors.panel,
    isScrollControlled: true,
    barrierColor: Colors.black38,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
    builder: (ctx) => SeletorDeCor(
      initial: initial,
      onChanged: onChanged,
      withAlpha: withAlpha,
      recent: recent,
    ),
  );
}

enum AbaDaCor { quadro, roda, rgb }

const _chaveDasAmostras = 'cor.amostras';
const _chaveDaAba = 'cor.aba';

/// No maximo tantas cores guardadas.
const maximoDeAmostras = 24;

class SeletorDeCor extends StatefulWidget {
  const SeletorDeCor({
    super.key,
    required this.initial,
    this.onChanged,
    this.withAlpha = true,
    this.recent = const [],
  });

  final Color initial;
  final ValueChanged<Color>? onChanged;
  final bool withAlpha;
  final List<Color> recent;

  @override
  State<SeletorDeCor> createState() => _SeletorDeCorState();
}

class _SeletorDeCorState extends State<SeletorDeCor> {
  late HSVColor _hsv;
  late double _alpha;
  late final TextEditingController _hex;
  AbaDaCor _aba = AbaDaCor.quadro;
  SharedPreferences? _prefs;
  bool _arrastandoAmostra = false;

  /// Sem preferencias (testes, previa), as cores guardadas vivem aqui.
  static final List<Color> _amostrasNaMemoria = [];
  List<Color> _amostras = [];

  static const _rapidas = <Color>[
    Color(0xFFFFFFFF),
    Color(0xFF000000),
    Color(0xFFB8FF3D),
    Color(0xFF7C62FF),
    Color(0xFF35C4E7),
    Color(0xFF2BE3A0),
    Color(0xFFFFB020),
    Color(0xFFFF6B6B),
    Color(0xFFFF4FA3),
    Color(0xFF8B94A3),
    Color(0xFF1E242E),
    Color(0xFFE9EDF2),
  ];

  @override
  void initState() {
    super.initState();
    _hsv = HSVColor.fromColor(widget.initial.withValues(alpha: 1));
    _alpha = widget.withAlpha ? widget.initial.a : 1;
    _hex = TextEditingController(text: _hexOf(_current));
    try {
      _prefs = ProviderScope.containerOf(
        context,
        listen: false,
      ).read(sharedPreferencesProvider);
    } catch (_) {}
    final aba = _prefs?.getInt(_chaveDaAba);
    if (aba != null && aba >= 0 && aba < AbaDaCor.values.length) {
      _aba = AbaDaCor.values[aba];
    }
    _amostras = _lerAmostras();
  }

  @override
  void dispose() {
    _hex.dispose();
    super.dispose();
  }

  List<Color> _lerAmostras() {
    final prefs = _prefs;
    if (prefs == null) return [..._amostrasNaMemoria];
    return [
      for (final s in prefs.getStringList(_chaveDasAmostras) ?? const [])
        if (int.tryParse(s, radix: 16) case final v?) Color(v),
    ];
  }

  void _gravarAmostras() {
    final prefs = _prefs;
    if (prefs == null) {
      _amostrasNaMemoria
        ..clear()
        ..addAll(_amostras);
      return;
    }
    prefs.setStringList(_chaveDasAmostras, [
      for (final c in _amostras) c.toARGB32().toRadixString(16).padLeft(8, '0'),
    ]);
  }

  Color get _current => _hsv.toColor().withValues(alpha: _alpha);

  static String _hexOf(Color c) =>
      codigoHexDaCor(c, comAlfa: false).substring(1);

  void _emit({bool syncHex = true}) {
    if (syncHex) _hex.text = _hexOf(_current);
    widget.onChanged?.call(_current);
    setState(() {});
  }

  void _aplicar(Color c) {
    _hsv = HSVColor.fromColor(c.withValues(alpha: 1));
    if (widget.withAlpha) _alpha = c.a;
    _emit();
  }

  void _applyHex(String raw) {
    final c = corDoCodigo(raw);
    if (c == null) return;
    _hsv = HSVColor.fromColor(c.withValues(alpha: 1));
    if (widget.withAlpha && raw.replaceAll('#', '').trim().length == 8) {
      _alpha = c.a;
    }
    _emit(syncHex: false);
  }

  void _escolherAba(AbaDaCor aba) {
    setState(() => _aba = aba);
    _prefs?.setInt(_chaveDaAba, aba.index);
  }

  Future<void> _copiar(String codigo) async {
    try {
      await Clipboard.setData(ClipboardData(text: codigo));
    } catch (_) {}
    if (mounted) AureaSnack.show(context, 'Código copiado: $codigo');
  }

  Future<void> _colar() async {
    String? texto;
    try {
      texto = (await Clipboard.getData(Clipboard.kTextPlain))?.text;
    } catch (_) {}
    final cor = texto == null ? null : corDoCodigo(texto);
    if (!mounted) return;
    if (cor == null) {
      AureaSnack.show(context, 'Não é um código de cor');
      return;
    }
    _aplicar(cor);
  }

  Future<void> _contaGotas() async {
    final cor = await pegarCorDoPalco(context);
    if (!mounted) return;
    if (cor == null) return;
    _hsv = HSVColor.fromColor(cor);
    _emit();
  }

  void _salvarAmostra() {
    final c = _current;
    setState(() {
      _amostras
        ..removeWhere((x) => x.toARGB32() == c.toARGB32())
        ..insert(0, c);
      if (_amostras.length > maximoDeAmostras) {
        _amostras.removeRange(maximoDeAmostras, _amostras.length);
      }
    });
    _gravarAmostras();
  }

  void _apagarAmostra(int i) {
    if (i < 0 || i >= _amostras.length) return;
    setState(() => _amostras.removeAt(i));
    _gravarAmostras();
  }

  @override
  Widget build(BuildContext context) {
    final maxH = MediaQuery.of(context).size.height * 0.8;
    final cor = _current;
    return SafeArea(
      child: ConstrainedBox(
        constraints: BoxConstraints(maxHeight: maxH),
        child: SingleChildScrollView(
          padding: EdgeInsets.fromLTRB(
            18,
            12,
            18,
            16 + MediaQuery.of(context).viewInsets.bottom,
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  const AppText(
                    'Cor',
                    style: TextStyle(
                      fontSize: 17,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                  const SizedBox(width: 12),
                  _OriginalENova(
                    original: widget.initial,
                    nova: cor,
                    onOriginal: () => _aplicar(widget.initial),
                  ),
                  const Spacer(),
                  if (_temPalco)
                    IconButton(
                      key: const ValueKey('cor-conta-gotas'),
                      tooltip: 'Conta-gotas do palco',
                      onPressed: _contaGotas,
                      icon: const Icon(
                        CupertinoIcons.eyedropper,
                        size: 20,
                        color: AmColors.text,
                      ),
                    ),
                  CupertinoButton(
                    key: const ValueKey('cor-pronto'),
                    padding: const EdgeInsets.only(left: 6),
                    onPressed: () => Navigator.of(context).pop(cor),
                    child: const AppText(
                      'Pronto',
                      style: TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w600,
                        color: AmColors.accent,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 10),
              CupertinoSlidingSegmentedControl<AbaDaCor>(
                key: const ValueKey('cor-abas'),
                groupValue: _aba,
                thumbColor: AmColors.accentDim,
                backgroundColor: AmColors.chip,
                children: {
                  for (final (aba, nome) in const [
                    (AbaDaCor.quadro, 'Quadro'),
                    (AbaDaCor.roda, 'Roda'),
                    (AbaDaCor.rgb, 'RGB'),
                  ])
                    aba: Padding(
                      key: ValueKey('cor-aba-${aba.name}'),
                      padding: const EdgeInsets.symmetric(vertical: 7),
                      child: AppText(
                        nome,
                        style: const TextStyle(
                          fontSize: 13,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                },
                onValueChanged: (a) {
                  if (a != null) _escolherAba(a);
                },
              ),
              const SizedBox(height: 12),
              switch (_aba) {
                AbaDaCor.quadro => _quadro(),
                AbaDaCor.roda => _roda(),
                AbaDaCor.rgb => _canais(),
              },
              if (widget.withAlpha) ...[
                const SizedBox(height: 10),
                Row(
                  children: [
                    Expanded(
                      child: _Strip(
                        key: const ValueKey('cor-alfa'),
                        height: 26,
                        painter: _AlphaPainter(color: _hsv.toColor()),
                        position: _alpha,
                        onChanged: (v) {
                          _alpha = v.clamp(0.0, 1.0);
                          _emit();
                        },
                      ),
                    ),
                    const SizedBox(width: 10),
                    _ValorTocavel(
                      chave: 'cor-alfa-valor',
                      texto: '${(_alpha * 100).round()}%',
                      onTap: () async {
                        final v = await showNumberInput(
                          context,
                          value: _alpha * 100,
                          unit: '%',
                          min: 0,
                          max: 100,
                          decimals: 0,
                          title: 'Opacidade da cor',
                        );
                        if (v == null) return;
                        _alpha = v / 100;
                        _emit();
                      },
                    ),
                  ],
                ),
              ],
              const SizedBox(height: 12),
              Row(
                children: [
                  const AppText(
                    '#',
                    style: TextStyle(fontSize: 14, color: AmColors.muted),
                  ),
                  const SizedBox(width: 4),
                  SizedBox(
                    width: 96,
                    child: TextField(
                      key: const ValueKey('cor-hex'),
                      controller: _hex,
                      onSubmitted: _applyHex,
                      onChanged: (v) {
                        final n = v.replaceAll('#', '').length;
                        if (n == 6 || n == 8) _applyHex(v);
                      },
                      textCapitalization: TextCapitalization.characters,
                      inputFormatters: [
                        LengthLimitingTextInputFormatter(9),
                        FilteringTextInputFormatter.allow(
                          RegExp(r'[0-9a-fA-F#]'),
                        ),
                      ],
                      style: const TextStyle(
                        fontSize: 14,
                        color: AmColors.text,
                      ),
                      decoration: InputDecoration(
                        isDense: true,
                        filled: true,
                        fillColor: AmColors.chip,
                        contentPadding: const EdgeInsets.symmetric(
                          horizontal: 10,
                          vertical: 9,
                        ),
                        border: OutlineInputBorder(
                          borderRadius: BorderRadius.circular(8),
                          borderSide: BorderSide.none,
                        ),
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: FittedBox(
                      fit: BoxFit.scaleDown,
                      alignment: Alignment.centerLeft,
                      child: Text(
                        'H ${_hsv.hue.round()}°  S ${(_hsv.saturation * 100).round()}%  '
                        'V ${(_hsv.value * 100).round()}%',
                        key: const ValueKey('cor-hsv'),
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.muted,
                          fontFeatures: [FontFeature.tabularFigures()],
                        ),
                      ),
                    ),
                  ),
                  PopupMenuButton<String>(
                    key: const ValueKey('cor-copiar'),
                    tooltip: 'Copiar código',
                    color: AmColors.panelHigh,
                    icon: const Icon(
                      CupertinoIcons.doc_on_doc,
                      size: 19,
                      color: AmColors.text,
                    ),
                    itemBuilder: (_) => [
                      PopupMenuItem(
                        key: const ValueKey('cor-copiar-hex'),
                        value: codigoHexDaCor(cor),
                        child: AppText(
                          codigoHexDaCor(cor),
                          style: const TextStyle(color: AmColors.text),
                        ),
                      ),
                      PopupMenuItem(
                        key: const ValueKey('cor-copiar-rgba'),
                        value: codigoRgbaDaCor(cor),
                        child: AppText(
                          codigoRgbaDaCor(cor),
                          style: const TextStyle(color: AmColors.text),
                        ),
                      ),
                    ],
                    onSelected: _copiar,
                  ),
                  IconButton(
                    key: const ValueKey('cor-colar'),
                    tooltip: 'Colar código de cor',
                    onPressed: _colar,
                    icon: const Icon(
                      CupertinoIcons.doc_on_clipboard,
                      size: 19,
                      color: AmColors.text,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              Row(
                children: [
                  const Expanded(
                    child: AppText(
                      'Minhas cores',
                      style: TextStyle(fontSize: 12, color: AmColors.muted),
                    ),
                  ),
                  if (_amostras.isNotEmpty)
                    const AppText(
                      'segure e arraste para a lixeira',
                      style: TextStyle(fontSize: 10.5, color: AmColors.muted),
                    ),
                ],
              ),
              const SizedBox(height: 8),
              SizedBox(
                height: 40,
                child: Row(
                  children: [
                    if (_arrastandoAmostra)
                      DragTarget<int>(
                        key: const ValueKey('cor-lixeira'),
                        onAcceptWithDetails: (d) => _apagarAmostra(d.data),
                        builder: (_, candidatos, _) => Container(
                          width: 40,
                          height: 40,
                          decoration: BoxDecoration(
                            color: candidatos.isEmpty
                                ? AmColors.chip
                                : AmColors.pink.withValues(alpha: .35),
                            shape: BoxShape.circle,
                          ),
                          child: const Icon(
                            CupertinoIcons.trash,
                            size: 18,
                            color: AmColors.pink,
                          ),
                        ),
                      )
                    else
                      Tocavel(
                        key: const ValueKey('cor-salvar'),
                        onTap: _salvarAmostra,
                        child: Container(
                          width: 40,
                          height: 40,
                          decoration: const BoxDecoration(
                            color: AmColors.chip,
                            shape: BoxShape.circle,
                          ),
                          child: const Icon(
                            CupertinoIcons.plus,
                            size: 18,
                            color: AmColors.accent,
                          ),
                        ),
                      ),
                    const SizedBox(width: 8),
                    Expanded(
                      child: ListView.separated(
                        scrollDirection: Axis.horizontal,
                        itemCount: _amostras.length,
                        separatorBuilder: (_, _) => const SizedBox(width: 8),
                        itemBuilder: (_, i) {
                          final c = _amostras[i];
                          final bolinha = _Bolinha(
                            cor: c,
                            marcada: c.toARGB32() == cor.toARGB32(),
                          );
                          return LongPressDraggable<int>(
                            key: ValueKey('cor-amostra-$i'),
                            data: i,
                            feedback: _Bolinha(cor: c, marcada: true, lado: 44),
                            childWhenDragging: Opacity(
                              opacity: .3,
                              child: bolinha,
                            ),
                            onDragStarted: () =>
                                setState(() => _arrastandoAmostra = true),
                            onDragEnd: (_) =>
                                setState(() => _arrastandoAmostra = false),
                            child: Tocavel(
                              onTap: () => _aplicar(c),
                              child: bolinha,
                            ),
                          );
                        },
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 12),
              const AppText(
                'Rápidas',
                style: TextStyle(fontSize: 12, color: AmColors.muted),
              ),
              const SizedBox(height: 8),
              Wrap(
                spacing: 10,
                runSpacing: 10,
                children: [
                  for (final c in [...widget.recent, ..._rapidas])
                    Tocavel(
                      onTap: () => _aplicar(c),
                      child: _Bolinha(
                        cor: c,
                        marcada: c.toARGB32() == cor.toARGB32(),
                        lado: 30,
                      ),
                    ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  bool get _temPalco => palcoParaContaGotas();

  Widget _quadro() => Column(
    children: [
      LayoutBuilder(
        builder: (context, c) {
          final w = c.maxWidth;
          const h = 170.0;
          return GestureDetector(
            key: const ValueKey('cor-quadro'),
            behavior: HitTestBehavior.opaque,
            onPanDown: (d) => _pickSV(d.localPosition, w, h),
            onPanUpdate: (d) => _pickSV(d.localPosition, w, h),
            child: SizedBox(
              width: w,
              height: h,
              child: CustomPaint(
                painter: _SvPainter(
                  hue: _hsv.hue,
                  saturation: _hsv.saturation,
                  value: _hsv.value,
                ),
              ),
            ),
          );
        },
      ),
      const SizedBox(height: 14),
      _Strip(
        key: const ValueKey('cor-matiz'),
        height: 26,
        painter: _HuePainter(),
        position: _hsv.hue / 360,
        onChanged: (v) {
          _hsv = _hsv.withHue((v * 360).clamp(0.0, 359.999));
          _emit();
        },
      ),
    ],
  );

  /// A RODA: anel de matiz em volta de um quadro de saturacao e brilho.
  Widget _roda() => LayoutBuilder(
    builder: (context, c) {
      final lado = math.min(c.maxWidth, 240.0);
      final raio = lado / 2;
      const espessura = 26.0;
      final quadro = (raio - espessura - 8) * math.sqrt2;
      void tocar(Offset p, {required bool comecou}) {
        final centro = Offset(raio, raio);
        final d = p - centro;
        final dist = d.distance;
        final noAnel = dist >= raio - espessura - 4;
        if (comecou) _arrastandoNoAnel = noAnel;
        if (_arrastandoNoAnel) {
          final ang = (math.atan2(d.dy, d.dx) * 180 / math.pi + 360) % 360;
          _hsv = _hsv.withHue(ang.clamp(0.0, 359.999));
        } else {
          final origem = centro - Offset(quadro / 2, quadro / 2);
          final q = p - origem;
          _hsv = _hsv
              .withSaturation((q.dx / quadro).clamp(0.0, 1.0))
              .withValue((1 - q.dy / quadro).clamp(0.0, 1.0));
        }
        _emit();
      }

      return Center(
        child: GestureDetector(
          key: const ValueKey('cor-roda'),
          behavior: HitTestBehavior.opaque,
          onPanDown: (d) => tocar(d.localPosition, comecou: true),
          onPanUpdate: (d) => tocar(d.localPosition, comecou: false),
          child: SizedBox(
            width: lado,
            height: lado,
            child: CustomPaint(
              painter: _RodaPainter(
                hue: _hsv.hue,
                saturation: _hsv.saturation,
                value: _hsv.value,
                espessura: espessura,
                quadro: quadro,
              ),
            ),
          ),
        ),
      );
    },
  );

  bool _arrastandoNoAnel = false;

  /// RGB: um trilho por canal, cada um pintado do canal zerado ao cheio.
  Widget _canais() {
    final c = _hsv.toColor();
    int canal(double v) => (v * 255).round().clamp(0, 255);
    Widget linha(String nome, int valor, Color de, Color ate, Color Function(int) com) =>
        Padding(
          padding: const EdgeInsets.only(bottom: 10),
          child: Row(
            children: [
              SizedBox(
                width: 16,
                child: Text(
                  nome,
                  style: const TextStyle(fontSize: 13, color: AmColors.muted),
                ),
              ),
              Expanded(
                child: _Strip(
                  key: ValueKey('cor-canal-$nome'),
                  height: 26,
                  painter: _GradientePainter(de: de, ate: ate),
                  position: valor / 255,
                  onChanged: (v) {
                    _hsv = HSVColor.fromColor(com((v * 255).round()));
                    _emit();
                  },
                ),
              ),
              const SizedBox(width: 10),
              _ValorTocavel(
                chave: 'cor-canal-$nome-valor',
                texto: '$valor',
                onTap: () async {
                  final v = await showNumberInput(
                    context,
                    value: valor.toDouble(),
                    min: 0,
                    max: 255,
                    decimals: 0,
                    title: nome,
                  );
                  if (v == null) return;
                  _hsv = HSVColor.fromColor(com(v.round()));
                  _emit();
                },
              ),
            ],
          ),
        );
    final r = canal(c.r), g = canal(c.g), b = canal(c.b);
    return Column(
      children: [
        linha(
          'R',
          r,
          Color.fromARGB(255, 0, g, b),
          Color.fromARGB(255, 255, g, b),
          (v) => Color.fromARGB(255, v, g, b),
        ),
        linha(
          'G',
          g,
          Color.fromARGB(255, r, 0, b),
          Color.fromARGB(255, r, 255, b),
          (v) => Color.fromARGB(255, r, v, b),
        ),
        linha(
          'B',
          b,
          Color.fromARGB(255, r, g, 0),
          Color.fromARGB(255, r, g, 255),
          (v) => Color.fromARGB(255, r, g, v),
        ),
      ],
    );
  }

  void _pickSV(Offset p, double w, double h) {
    _hsv = _hsv
        .withSaturation((p.dx / w).clamp(0.0, 1.0))
        .withValue((1 - p.dy / h).clamp(0.0, 1.0));
    _emit();
  }
}

/// A ORIGINAL (esquerda, toque volta a ela) e a NOVA lado a lado, com
/// xadrez atras para a transparencia aparecer.
class _OriginalENova extends StatelessWidget {
  const _OriginalENova({
    required this.original,
    required this.nova,
    required this.onOriginal,
  });

  final Color original;
  final Color nova;
  final VoidCallback onOriginal;

  @override
  Widget build(BuildContext context) => ClipRRect(
    borderRadius: BorderRadius.circular(8),
    child: SizedBox(
      width: 76,
      height: 28,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Expanded(
            child: Tocavel(
              key: const ValueKey('cor-original'),
              onTap: onOriginal,
              child: CustomPaint(
                painter: _XadrezPainter(),
                child: ColoredBox(color: original),
              ),
            ),
          ),
          Expanded(
            child: CustomPaint(
              key: const ValueKey('cor-nova'),
              painter: _XadrezPainter(),
              child: ColoredBox(color: nova),
            ),
          ),
        ],
      ),
    ),
  );
}

class _Bolinha extends StatelessWidget {
  const _Bolinha({required this.cor, required this.marcada, this.lado = 40});

  final Color cor;
  final bool marcada;
  final double lado;

  @override
  Widget build(BuildContext context) => SizedBox(
    width: lado,
    height: lado,
    child: CustomPaint(
      painter: _XadrezPainter(circulo: true),
      child: Container(
        decoration: BoxDecoration(
          color: cor,
          shape: BoxShape.circle,
          border: Border.all(
            color: marcada ? AmColors.accent : AmColors.hairline,
            width: 2,
          ),
        ),
      ),
    ),
  );
}

class _ValorTocavel extends StatelessWidget {
  const _ValorTocavel({
    required this.chave,
    required this.texto,
    required this.onTap,
  });

  final String chave;
  final String texto;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Tocavel(
    key: ValueKey(chave),
    onTap: onTap,
    child: Container(
      width: 52,
      height: 30,
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(8),
      ),
      child: Text(
        texto,
        style: const TextStyle(
          fontSize: 13,
          fontWeight: FontWeight.w700,
          color: AmColors.accent,
          fontFeatures: [FontFeature.tabularFigures()],
        ),
      ),
    ),
  );
}

class _Strip extends StatelessWidget {
  const _Strip({
    super.key,
    required this.height,
    required this.painter,
    required this.position,
    required this.onChanged,
  });

  final double height;
  final CustomPainter painter;
  final double position;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, c) {
        final w = c.maxWidth;
        void pick(Offset p) => onChanged((p.dx / w).clamp(0.0, 1.0));
        return GestureDetector(
          behavior: HitTestBehavior.opaque,
          onPanDown: (d) => pick(d.localPosition),
          onPanUpdate: (d) => pick(d.localPosition),
          child: SizedBox(
            width: w,
            height: height,
            child: Stack(
              clipBehavior: Clip.none,
              children: [
                ClipRRect(
                  borderRadius: BorderRadius.circular(8),
                  child: CustomPaint(size: Size(w, height), painter: painter),
                ),
                Positioned(
                  left: (position.clamp(0.0, 1.0) * w) - 7,
                  top: -2,
                  child: Container(
                    width: 14,
                    height: height + 4,
                    decoration: BoxDecoration(
                      borderRadius: BorderRadius.circular(7),
                      border: Border.all(color: Colors.white, width: 2.5),
                    ),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _SvPainter extends CustomPainter {
  const _SvPainter({
    required this.hue,
    required this.saturation,
    required this.value,
  });

  final double hue;
  final double saturation;
  final double value;

  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    final rrect = RRect.fromRectAndRadius(rect, const Radius.circular(10));
    canvas.save();
    canvas.clipRRect(rrect);
    _pintarQuadroSV(canvas, rect, hue);
    canvas.restore();
    _pintarMira(
      canvas,
      Offset(saturation * size.width, (1 - value) * size.height),
    );
  }

  @override
  bool shouldRepaint(_SvPainter old) =>
      old.hue != hue || old.saturation != saturation || old.value != value;
}

void _pintarQuadroSV(Canvas canvas, Rect rect, double hue) {
  // Base: branco -> matiz pura.
  canvas.drawRect(
    rect,
    Paint()
      ..shader = LinearGradient(
        colors: [Colors.white, HSVColor.fromAHSV(1, hue, 1, 1).toColor()],
      ).createShader(rect),
  );
  // Por cima: transparente -> preto.
  canvas.drawRect(
    rect,
    Paint()
      ..shader = const LinearGradient(
        begin: Alignment.topCenter,
        end: Alignment.bottomCenter,
        colors: [Colors.transparent, Colors.black],
      ).createShader(rect),
  );
}

void _pintarMira(Canvas canvas, Offset p) {
  canvas.drawCircle(
    p,
    9,
    Paint()
      ..color = Colors.white.withValues(alpha: 0.9)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.5,
  );
  canvas.drawCircle(
    p,
    9,
    Paint()
      ..color = Colors.black.withValues(alpha: 0.35)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1,
  );
}

class _RodaPainter extends CustomPainter {
  const _RodaPainter({
    required this.hue,
    required this.saturation,
    required this.value,
    required this.espessura,
    required this.quadro,
  });

  final double hue;
  final double saturation;
  final double value;
  final double espessura;
  final double quadro;

  @override
  void paint(Canvas canvas, Size size) {
    final centro = size.center(Offset.zero);
    final raio = size.shortestSide / 2;
    final anel = Rect.fromCircle(center: centro, radius: raio - espessura / 2);
    canvas.drawCircle(
      centro,
      raio - espessura / 2,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = espessura
        ..shader = SweepGradient(
          colors: [
            for (var i = 0; i <= 6; i++)
              HSVColor.fromAHSV(1, (i * 60.0) % 360, 1, 1).toColor(),
          ],
        ).createShader(anel),
    );
    final ang = hue * math.pi / 180;
    final marca = centro +
        Offset(math.cos(ang), math.sin(ang)) * (raio - espessura / 2);
    canvas.drawCircle(
      marca,
      espessura / 2 - 1,
      Paint()
        ..color = Colors.white
        ..style = PaintingStyle.stroke
        ..strokeWidth = 3,
    );
    final q = Rect.fromCenter(center: centro, width: quadro, height: quadro);
    canvas.save();
    canvas.clipRRect(RRect.fromRectAndRadius(q, const Radius.circular(8)));
    _pintarQuadroSV(canvas, q, hue);
    canvas.restore();
    _pintarMira(
      canvas,
      Offset(q.left + saturation * quadro, q.top + (1 - value) * quadro),
    );
  }

  @override
  bool shouldRepaint(_RodaPainter old) =>
      old.hue != hue ||
      old.saturation != saturation ||
      old.value != value ||
      old.quadro != quadro;
}

class _HuePainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    canvas.drawRect(
      rect,
      Paint()
        ..shader = LinearGradient(
          colors: [
            for (var i = 0; i <= 6; i++)
              HSVColor.fromAHSV(1, i * 59.99, 1, 1).toColor(),
          ],
        ).createShader(rect),
    );
  }

  @override
  bool shouldRepaint(_HuePainter old) => false;
}

class _GradientePainter extends CustomPainter {
  const _GradientePainter({required this.de, required this.ate});

  final Color de;
  final Color ate;

  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    canvas.drawRect(
      rect,
      Paint()..shader = LinearGradient(colors: [de, ate]).createShader(rect),
    );
  }

  @override
  bool shouldRepaint(_GradientePainter old) => old.de != de || old.ate != ate;
}

/// Xadrez, para a transparencia ficar legivel.
class _XadrezPainter extends CustomPainter {
  const _XadrezPainter({this.circulo = false});

  final bool circulo;

  @override
  void paint(Canvas canvas, Size size) {
    const s = 6.0;
    final a = Paint()..color = const Color(0xFF3A4150);
    final b = Paint()..color = const Color(0xFF2A303B);
    canvas.save();
    if (circulo) {
      canvas.clipPath(Path()..addOval(Offset.zero & size));
    }
    for (var y = 0.0; y < size.height; y += s) {
      for (var x = 0.0; x < size.width; x += s) {
        final even = ((x / s).floor() + (y / s).floor()).isEven;
        canvas.drawRect(Rect.fromLTWH(x, y, s, s), even ? a : b);
      }
    }
    canvas.restore();
  }

  @override
  bool shouldRepaint(_XadrezPainter old) => old.circulo != circulo;
}

class _AlphaPainter extends CustomPainter {
  const _AlphaPainter({required this.color});

  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    const _XadrezPainter().paint(canvas, size);
    final rect = Offset.zero & size;
    canvas.drawRect(
      rect,
      Paint()
        ..shader = LinearGradient(colors: [color.withValues(alpha: 0), color])
            .createShader(rect),
    );
  }

  @override
  bool shouldRepaint(_AlphaPainter old) => old.color != color;
}

/// Botao de cor padrao: mostra a cor e abre o seletor completo.
class ColorWell extends StatelessWidget {
  const ColorWell({
    super.key,
    required this.color,
    required this.onChanged,
    this.label,
    this.size = 30,
    this.withAlpha = true,
    this.recent = const [],
  });

  final Color color;
  final ValueChanged<Color> onChanged;
  final String? label;
  final double size;
  final bool withAlpha;
  final List<Color> recent;

  @override
  Widget build(BuildContext context) {
    final well = GestureDetector(
      onTap: () async {
        final before = color;
        final picked = await showColorPicker(
          context,
          initial: before,
          withAlpha: withAlpha,
          recent: recent,
          onChanged: onChanged,
        );
        // Fechar arrastando mantem a ultima cor vista, que e o que a
        // pessoa acabou de escolher olhando.
        if (picked != null) onChanged(picked);
      },
      child: Container(
        width: size,
        height: size,
        decoration: BoxDecoration(
          color: color,
          borderRadius: BorderRadius.circular(math.min(8, size / 3)),
          border: Border.all(color: AmColors.hairline, width: 1.5),
        ),
      ),
    );
    if (label == null) return well;
    return Row(
      children: [
        Expanded(
          child: AppText(
            label!,
            style: const TextStyle(fontSize: 12, color: AmColors.muted),
          ),
        ),
        well,
      ],
    );
  }
}
