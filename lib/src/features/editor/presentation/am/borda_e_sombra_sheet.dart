import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer.dart';
import '../../domain/layer_meta.dart';
import '../../domain/shape.dart';
import '../context/parameter_row.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'color_picker_sheet.dart';

/// BORDA E SOMBRA (v1.1.1): o traco da forma com pontas e juncoes, as
/// bordas empilhadas de qualquer camada (fora, dentro ou no centro), a
/// sombra projetada, a sombra interna e o brilho.
Future<void> showBordaESombraSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) {
  playback.pause();
  return showParamSheet(
    context,
    title: 'Borda e sombra',
    heightFactor: .72,
    builder: (_) => ValueListenableBuilder<Duration>(
      valueListenable: playback.time,
      builder: (_, t, _) => BordaESombra(layerId: layerId, tempo: t),
    ),
  );
}

/// Grava [v] na trilha: base quando parada, keyframe no instante quando
/// animada (a mesma regra de todo numero do editor).
AnimatedDouble _editar(AnimatedDouble a, Duration local, double v) =>
    a.isAnimated
    ? a.withKeyframe(local, v, a.easeAt(local))
    : AnimatedDouble(v, const [], a.loop, a.expression);

/// Os nomes das pontas do traco.
const nomesDasTerminacoes = <TerminacaoDoTraco, String>{
  TerminacaoDoTraco.nenhuma: 'Nenhuma',
  TerminacaoDoTraco.seta: 'Seta',
  TerminacaoDoTraco.setaCheia: 'Seta cheia',
  TerminacaoDoTraco.setaVazada: 'Seta vazada',
  TerminacaoDoTraco.circuloCheio: 'Círculo cheio',
  TerminacaoDoTraco.circuloVazado: 'Círculo vazado',
  TerminacaoDoTraco.losango: 'Losango',
  TerminacaoDoTraco.losangoCheio: 'Losango cheio',
  TerminacaoDoTraco.quadrado: 'Quadrado',
  TerminacaoDoTraco.quadradoCheio: 'Quadrado cheio',
  TerminacaoDoTraco.gotaCheia: 'Gota cheia',
  TerminacaoDoTraco.gotaVazada: 'Gota vazada',
  TerminacaoDoTraco.linhaT: 'Linha em T',
};

/// No maximo quatro bordas: cada uma e um passe inteiro na GPU.
const maximoDeBordas = 4;

/// SOMBRAS PRONTAS: um toque liga uma sombra ja ajustada.
final sombrasProntas = <String, ShadowStyle Function()>{
  'Suave': () => ShadowStyle(
    opacity: AnimatedDouble(.35),
    angleDeg: AnimatedDouble(270),
    distance: AnimatedDouble(18),
    size: AnimatedDouble(48),
  ),
  'Dura': () => ShadowStyle(
    opacity: AnimatedDouble(.6),
    angleDeg: AnimatedDouble(315),
    distance: AnimatedDouble(10),
    size: AnimatedDouble(0),
  ),
  'Longa': () => ShadowStyle(
    opacity: AnimatedDouble(.45),
    angleDeg: AnimatedDouble(300),
    distance: AnimatedDouble(60),
    size: AnimatedDouble(24),
  ),
  'Contato': () => ShadowStyle(
    opacity: AnimatedDouble(.55),
    angleDeg: AnimatedDouble(270),
    distance: AnimatedDouble(4),
    size: AnimatedDouble(8),
    spread: AnimatedDouble(2),
  ),
};

class BordaESombra extends ConsumerWidget {
  const BordaESombra({super.key, required this.layerId, required this.tempo});

  final String layerId;
  final Duration tempo;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final projeto = ref.watch(editorControllerProvider);
    final layer = projeto.layerById(layerId);
    if (layer == null || layer is AudioLayer) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);
    final local = layer.localTime(tempo);
    final estilos = projeto.metaOf(layerId).styles;
    final bordas = estilos.bordas;
    final traco = layer is ShapeLayer
        ? layer.contents.whereType<ShapeStroke>().firstOrNull
        : null;

    void mudarEstilos(LayerStyles Function(LayerStyles) f) =>
        c.updateLayerStyles(layerId, f);
    void mudarBordas(List<StrokeStyle> novas) =>
        mudarEstilos((s) => comBordas(s, novas));

    return ListView(
      key: const ValueKey('borda-e-sombra'),
      padding: const EdgeInsets.fromLTRB(16, 4, 16, 24),
      children: [
        if (layer is ShapeLayer) ...[
          _Titulo(
            'Traço',
            chave: 'borda-traco-ligado',
            ligado: traco != null,
            onLigar: (v) =>
                v ? c.ensureShapeStroke(layerId) : c.removeShapeStroke(layerId),
          ),
          if (traco != null) ...[
            _LinhaDeCor(
              chave: 'borda-traco-cor',
              rotulo: 'Cor do traço',
              cor: traco.color,
              onCor: (cor) =>
                  c.updateShapeStroke(layerId, (s) => s.copyWith(color: cor)),
            ),
            ParameterRow(
              label: 'Espessura',
              value: traco.width.valueAt(local),
              min: 0,
              max: 200,
              unitsPerPixel: .25,
              decimals: 1,
              unit: 'px',
              onChanged: (v) => c.updateShapeStroke(
                layerId,
                (s) => s.copyWith(width: _editar(s.width, local, v)),
              ),
            ),
            const SizedBox(height: 6),
            _Segmentos<StrokeCap>(
              chave: 'borda-traco-ponta',
              rotulo: 'Ponta',
              valor: traco.cap,
              opcoes: const {
                StrokeCap.butt: 'Reta',
                StrokeCap.round: 'Redonda',
                StrokeCap.square: 'Quadrada',
              },
              onEscolher: (v) =>
                  c.updateShapeStroke(layerId, (s) => s.copyWith(cap: v)),
            ),
            _Segmentos<StrokeJoin>(
              chave: 'borda-traco-juncao',
              rotulo: 'Junção',
              valor: traco.join,
              opcoes: const {
                StrokeJoin.bevel: 'Chanfro',
                StrokeJoin.round: 'Redonda',
                StrokeJoin.miter: 'Mitra',
              },
              onEscolher: (v) =>
                  c.updateShapeStroke(layerId, (s) => s.copyWith(join: v)),
            ),
            Row(
              children: [
                Expanded(
                  child: _EscolhaDeTerminacao(
                    chave: 'borda-terminacao-inicio',
                    rotulo: 'Início',
                    valor: traco.inicio,
                    onEscolher: (v) => c.updateShapeStroke(
                      layerId,
                      (s) => s.copyWith(inicio: v),
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: _EscolhaDeTerminacao(
                    chave: 'borda-terminacao-fim',
                    rotulo: 'Fim',
                    valor: traco.fim,
                    onEscolher: (v) =>
                        c.updateShapeStroke(layerId, (s) => s.copyWith(fim: v)),
                  ),
                ),
              ],
            ),
            if (traco.inicio != TerminacaoDoTraco.nenhuma ||
                traco.fim != TerminacaoDoTraco.nenhuma)
              ParameterRow(
                label: 'Tamanho das pontas',
                value: traco.tamanhoDaTerminacao,
                min: 1,
                max: 10,
                unitsPerPixel: .02,
                decimals: 1,
                unit: '×',
                onChanged: (v) => c.updateShapeStroke(
                  layerId,
                  (s) => s.copyWith(tamanhoDaTerminacao: v),
                ),
              ),
            const AppText(
              'As pontas aparecem nos traços abertos (linha, arco, desenho).',
              style: TextStyle(fontSize: 11, color: AmColors.muted),
            ),
          ],
          const SizedBox(height: 14),
        ],
        const _Secao('Bordas'),
        for (var i = 0; i < bordas.length; i++)
          _CartaoDaBorda(
            indice: i,
            borda: bordas[i],
            local: local,
            total: bordas.length,
            onMudar: (nova) {
              final lista = [...bordas];
              lista[i] = nova;
              mudarBordas(lista);
            },
            onExcluir: () => mudarBordas([...bordas]..removeAt(i)),
            onMover: (delta) {
              final alvo = i + delta;
              if (alvo < 0 || alvo >= bordas.length) return;
              final lista = [...bordas];
              lista.insert(alvo, lista.removeAt(i));
              mudarBordas(lista);
            },
          ),
        if (bordas.length < maximoDeBordas)
          Tocavel(
            key: const ValueKey('borda-adicionar'),
            onTap: () => mudarBordas([...bordas, novaBorda(bordas, local)]),
            child: Container(
              height: 44,
              margin: const EdgeInsets.only(top: 4),
              decoration: BoxDecoration(
                color: AmColors.chip,
                borderRadius: BorderRadius.circular(10),
              ),
              child: const Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(CupertinoIcons.plus, size: 16, color: AmColors.accent),
                  SizedBox(width: 6),
                  AppText(
                    'Adicionar borda',
                    style: TextStyle(color: AmColors.text, fontSize: 14),
                  ),
                ],
              ),
            ),
          ),
        const SizedBox(height: 16),
        _Titulo(
          'Sombra',
          chave: 'sombra-ligada',
          ligado: estilos.dropShadow?.enabled ?? false,
          onLigar: (v) => mudarEstilos(
            (s) => v
                ? s.copyWith(
                    dropShadow: (s.dropShadow ?? ShadowStyle()).copyWith(
                      enabled: true,
                    ),
                  )
                : s.copyWith(clearDropShadow: true),
          ),
        ),
        if (estilos.dropShadow?.enabled ?? false) ...[
          SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: Row(
              children: [
                for (final pronta in sombrasProntas.entries)
                  Padding(
                    padding: const EdgeInsets.only(right: 8, bottom: 8),
                    child: Tocavel(
                      key: ValueKey('sombra-pronta-${pronta.key}'),
                      onTap: () => mudarEstilos(
                        (s) => s.copyWith(
                          dropShadow: pronta.value().copyWith(
                            color: s.dropShadow?.color,
                          ),
                        ),
                      ),
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 14,
                          vertical: 8,
                        ),
                        decoration: BoxDecoration(
                          color: AmColors.chip,
                          borderRadius: BorderRadius.circular(18),
                        ),
                        child: AppText(
                          pronta.key,
                          style: const TextStyle(
                            fontSize: 12.5,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                    ),
                  ),
              ],
            ),
          ),
          _EdicaoDaSombra(
            prefixo: 'sombra',
            sombra: estilos.dropShadow!,
            local: local,
            onMudar: (f) =>
                mudarEstilos((s) => s.copyWith(dropShadow: f(s.dropShadow!))),
          ),
        ],
        const SizedBox(height: 16),
        _Titulo(
          'Sombra interna',
          chave: 'sombra-interna-ligada',
          ligado: estilos.innerShadow?.enabled ?? false,
          onLigar: (v) => mudarEstilos(
            (s) => v
                ? s.copyWith(
                    innerShadow:
                        (s.innerShadow ??
                                ShadowStyle(
                                  opacity: AnimatedDouble(.35),
                                  angleDeg: AnimatedDouble(270),
                                  distance: AnimatedDouble(4),
                                  size: AnimatedDouble(12),
                                ))
                            .copyWith(enabled: true),
                  )
                : s.copyWith(clearInnerShadow: true),
          ),
        ),
        if (estilos.innerShadow?.enabled ?? false)
          _EdicaoDaSombra(
            prefixo: 'sombra-interna',
            sombra: estilos.innerShadow!,
            local: local,
            onMudar: (f) => mudarEstilos(
              (s) => s.copyWith(innerShadow: f(s.innerShadow!)),
            ),
          ),
        const SizedBox(height: 16),
        _Titulo(
          'Brilho',
          chave: 'brilho-ligado',
          ligado: estilos.outerGlow?.enabled ?? false,
          onLigar: (v) => mudarEstilos(
            (s) => v
                ? s.copyWith(
                    outerGlow: (s.outerGlow ?? GlowStyle()).copyWith(
                      enabled: true,
                    ),
                  )
                : s.copyWith(clearOuterGlow: true),
          ),
        ),
        if (estilos.outerGlow?.enabled ?? false) ...[
          _LinhaDeCor(
            chave: 'brilho-cor',
            rotulo: 'Cor do brilho',
            cor: estilos.outerGlow!.color,
            onCor: (cor) => mudarEstilos(
              (s) => s.copyWith(outerGlow: s.outerGlow!.copyWith(color: cor)),
            ),
          ),
          ParameterRow(
            label: 'Opacidade',
            value: estilos.outerGlow!.opacity.valueAt(local) * 100,
            min: 0,
            max: 100,
            unitsPerPixel: .35,
            decimals: 0,
            unit: '%',
            onChanged: (v) => mudarEstilos(
              (s) => s.copyWith(
                outerGlow: s.outerGlow!.copyWith(
                  opacity: _editar(s.outerGlow!.opacity, local, v / 100),
                ),
              ),
            ),
          ),
          ParameterRow(
            label: 'Tamanho',
            value: estilos.outerGlow!.size.valueAt(local),
            min: 0,
            max: 120,
            unitsPerPixel: .3,
            decimals: 0,
            unit: 'px',
            onChanged: (v) => mudarEstilos(
              (s) => s.copyWith(
                outerGlow: s.outerGlow!.copyWith(
                  size: _editar(s.outerGlow!.size, local, v),
                ),
              ),
            ),
          ),
        ],
      ],
    );
  }
}

/// A BORDA NOVA aparece por fora das que ja existem: cada borda se mede
/// da beira da camada e as de baixo ficam por tras, entao a nova nasce
/// mais larga que a ultima.
StrokeStyle novaBorda(List<StrokeStyle> bordas, Duration local) {
  if (bordas.isEmpty) {
    return StrokeStyle(color: const Color(0xFFFFFFFF), width: AnimatedDouble(6));
  }
  final ultima = bordas.last;
  const cores = [Color(0xFFFFFFFF), Color(0xFF12151A)];
  return StrokeStyle(
    color: cores[bordas.length % 2],
    width: AnimatedDouble(
      (ultima.width.valueAt(local) + 6).clamp(1.0, 100.0).toDouble(),
    ),
    posicao: ultima.posicao,
  );
}

class _Secao extends StatelessWidget {
  const _Secao(this.texto);

  final String texto;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(top: 6, bottom: 8),
    child: AppText(
      texto,
      style: const TextStyle(
        fontSize: 15,
        fontWeight: FontWeight.w700,
        color: AmColors.text,
      ),
    ),
  );
}

/// Titulo de secao com o interruptor dela.
class _Titulo extends StatelessWidget {
  const _Titulo(
    this.texto, {
    required this.chave,
    required this.ligado,
    required this.onLigar,
  });

  final String texto;
  final String chave;
  final bool ligado;
  final ValueChanged<bool> onLigar;

  @override
  Widget build(BuildContext context) => Row(
    children: [
      Expanded(child: _Secao(texto)),
      CupertinoSwitch(
        key: ValueKey(chave),
        value: ligado,
        activeTrackColor: AmColors.accent,
        onChanged: onLigar,
      ),
    ],
  );
}

/// Cor, opacidade, angulo, distancia, desfoque e espalhamento de uma
/// sombra (projetada ou interna).
class _EdicaoDaSombra extends StatelessWidget {
  const _EdicaoDaSombra({
    required this.prefixo,
    required this.sombra,
    required this.local,
    required this.onMudar,
  });

  final String prefixo;
  final ShadowStyle sombra;
  final Duration local;
  final void Function(ShadowStyle Function(ShadowStyle)) onMudar;

  @override
  Widget build(BuildContext context) {
    final d = sombra;
    return Column(
      children: [
        _LinhaDeCor(
          chave: '$prefixo-cor',
          rotulo: 'Cor',
          cor: d.color,
          onCor: (cor) => onMudar((x) => x.copyWith(color: cor)),
        ),
        ParameterRow(
          label: 'Opacidade',
          value: d.opacity.valueAt(local) * 100,
          min: 0,
          max: 100,
          unitsPerPixel: .35,
          decimals: 0,
          unit: '%',
          onChanged: (v) => onMudar(
            (x) => x.copyWith(opacity: _editar(x.opacity, local, v / 100)),
          ),
        ),
        ParameterRow(
          label: 'Ângulo',
          value: d.angleDeg.valueAt(local),
          min: -360,
          max: 360,
          unitsPerPixel: 1,
          decimals: 0,
          unit: '°',
          onChanged: (v) => onMudar(
            (x) => x.copyWith(angleDeg: _editar(x.angleDeg, local, v)),
          ),
        ),
        ParameterRow(
          label: 'Distância',
          value: d.distance.valueAt(local),
          min: 0,
          max: 300,
          unitsPerPixel: .5,
          decimals: 0,
          unit: 'px',
          onChanged: (v) => onMudar(
            (x) => x.copyWith(distance: _editar(x.distance, local, v)),
          ),
        ),
        ParameterRow(
          label: 'Desfoque',
          value: d.size.valueAt(local),
          min: 0,
          max: 120,
          unitsPerPixel: .3,
          decimals: 0,
          unit: 'px',
          onChanged: (v) =>
              onMudar((x) => x.copyWith(size: _editar(x.size, local, v))),
        ),
        ParameterRow(
          label: 'Espalhar',
          value: d.spread.valueAt(local),
          min: 0,
          max: 100,
          unitsPerPixel: .25,
          decimals: 0,
          unit: 'px',
          onChanged: (v) =>
              onMudar((x) => x.copyWith(spread: _editar(x.spread, local, v))),
        ),
      ],
    );
  }
}

class _LinhaDeCor extends StatelessWidget {
  const _LinhaDeCor({
    required this.chave,
    required this.rotulo,
    required this.cor,
    required this.onCor,
  });

  final String chave;
  final String rotulo;
  final Color cor;
  final ValueChanged<Color> onCor;

  @override
  Widget build(BuildContext context) => Tocavel(
    key: ValueKey(chave),
    onTap: () async {
      final nova = await showColorPicker(
        context,
        initial: cor,
        onChanged: onCor,
      );
      if (nova != null) onCor(nova);
    },
    child: Container(
      height: 44,
      margin: const EdgeInsets.only(bottom: 6),
      padding: const EdgeInsets.symmetric(horizontal: 10),
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Row(
        children: [
          Container(
            width: 26,
            height: 26,
            decoration: BoxDecoration(
              color: cor,
              borderRadius: BorderRadius.circular(7),
              border: Border.all(color: Colors.white24),
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: AppText(
              rotulo,
              style: const TextStyle(fontSize: 14, color: AmColors.text),
            ),
          ),
          const Icon(
            CupertinoIcons.chevron_right,
            size: 14,
            color: AmColors.muted,
          ),
        ],
      ),
    ),
  );
}

class _Segmentos<T extends Object> extends StatelessWidget {
  const _Segmentos({
    required this.chave,
    required this.rotulo,
    required this.valor,
    required this.opcoes,
    required this.onEscolher,
  });

  final String chave;
  final String rotulo;
  final T valor;
  final Map<T, String> opcoes;
  final ValueChanged<T> onEscolher;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 8),
    child: Row(
      children: [
        SizedBox(
          width: 64,
          child: AppText(
            rotulo,
            style: const TextStyle(fontSize: 12.5, color: AmColors.muted),
          ),
        ),
        Expanded(
          child: CupertinoSlidingSegmentedControl<T>(
            key: ValueKey(chave),
            groupValue: valor,
            thumbColor: AmColors.accentDim,
            backgroundColor: AmColors.chip,
            children: {
              for (final e in opcoes.entries)
                e.key: Padding(
                  key: ValueKey('$chave-${e.value}'),
                  padding: const EdgeInsets.symmetric(vertical: 7),
                  child: AppText(
                    e.value,
                    style: const TextStyle(
                      fontSize: 12.5,
                      color: AmColors.text,
                    ),
                  ),
                ),
            },
            onValueChanged: (v) {
              if (v != null) onEscolher(v);
            },
          ),
        ),
      ],
    ),
  );
}

/// A ESCOLHA DE UMA PONTA: o botao mostra a atual desenhada; o toque abre
/// a grade com todas.
class _EscolhaDeTerminacao extends StatelessWidget {
  const _EscolhaDeTerminacao({
    required this.chave,
    required this.rotulo,
    required this.valor,
    required this.onEscolher,
  });

  final String chave;
  final String rotulo;
  final TerminacaoDoTraco valor;
  final ValueChanged<TerminacaoDoTraco> onEscolher;

  @override
  Widget build(BuildContext context) => Tocavel(
    key: ValueKey(chave),
    onTap: () async {
      final escolhida = await showModalBottomSheet<TerminacaoDoTraco>(
        context: context,
        backgroundColor: AmColors.panel,
        builder: (folha) => SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(16),
            child: Wrap(
              spacing: 10,
              runSpacing: 10,
              children: [
                for (final tipo in TerminacaoDoTraco.values)
                  Tocavel(
                    key: ValueKey('$chave-${tipo.name}'),
                    onTap: () => Navigator.of(folha).pop(tipo),
                    child: Container(
                      width: 72,
                      height: 64,
                      decoration: BoxDecoration(
                        color: tipo == valor
                            ? AmColors.accentDim
                            : AmColors.chip,
                        borderRadius: BorderRadius.circular(10),
                      ),
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          CustomPaint(
                            size: const Size(46, 20),
                            painter: _PintorDaTerminacao(tipo),
                          ),
                          const SizedBox(height: 4),
                          AppText(
                            nomesDasTerminacoes[tipo]!,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(
                              fontSize: 9.5,
                              color: AmColors.muted,
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ),
      );
      if (escolhida != null) onEscolher(escolhida);
    },
    child: Container(
      height: 52,
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.symmetric(horizontal: 10),
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Row(
        children: [
          AppText(
            rotulo,
            style: const TextStyle(fontSize: 12, color: AmColors.muted),
          ),
          const Spacer(),
          CustomPaint(
            size: const Size(46, 20),
            painter: _PintorDaTerminacao(valor),
          ),
        ],
      ),
    ),
  );
}

/// Desenha um traco curto terminando na ponta [tipo].
class _PintorDaTerminacao extends CustomPainter {
  const _PintorDaTerminacao(this.tipo);

  final TerminacaoDoTraco tipo;

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = AmColors.text
      ..strokeWidth = 2
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;
    final y = size.height / 2;
    final ponta = Offset(size.width - 8, y);
    canvas.drawLine(Offset(4, y), ponta, paint);
    final desenho = caminhoDaTerminacao(tipo, ponta, const Offset(1, 0), 10);
    if (desenho == null) return;
    final (caminho, cheia) = desenho;
    canvas.drawPath(
      caminho,
      paint..style = cheia ? PaintingStyle.fill : PaintingStyle.stroke,
    );
  }

  @override
  bool shouldRepaint(_PintorDaTerminacao old) => old.tipo != tipo;
}

/// UMA BORDA DA LISTA: cor, posicao, espessura, opacidade, subir/descer
/// e excluir.
class _CartaoDaBorda extends StatelessWidget {
  const _CartaoDaBorda({
    required this.indice,
    required this.borda,
    required this.local,
    required this.total,
    required this.onMudar,
    required this.onExcluir,
    required this.onMover,
  });

  final int indice;
  final StrokeStyle borda;
  final Duration local;
  final int total;
  final ValueChanged<StrokeStyle> onMudar;
  final VoidCallback onExcluir;
  final ValueChanged<int> onMover;

  @override
  Widget build(BuildContext context) => Container(
    key: ValueKey('borda-$indice'),
    margin: const EdgeInsets.only(bottom: 8),
    padding: const EdgeInsets.fromLTRB(10, 8, 6, 8),
    decoration: BoxDecoration(
      color: AmColors.panelHigh,
      borderRadius: BorderRadius.circular(12),
    ),
    child: Column(
      children: [
        Row(
          children: [
            Tocavel(
              key: ValueKey('borda-cor-$indice'),
              onTap: () async {
                final nova = await showColorPicker(
                  context,
                  initial: borda.color,
                  onChanged: (cor) => onMudar(borda.copyWith(color: cor)),
                );
                if (nova != null) onMudar(borda.copyWith(color: nova));
              },
              child: Container(
                width: 28,
                height: 28,
                decoration: BoxDecoration(
                  color: borda.color,
                  shape: BoxShape.circle,
                  border: Border.all(color: Colors.white30),
                ),
              ),
            ),
            const SizedBox(width: 8),
            AppText(
              'Borda ${indice + 1}',
              style: const TextStyle(fontSize: 13, color: AmColors.text),
            ),
            const Spacer(),
            IconButton(
              key: ValueKey('borda-subir-$indice'),
              tooltip: 'Subir',
              onPressed: indice == 0 ? null : () => onMover(-1),
              icon: const Icon(CupertinoIcons.chevron_up, size: 16),
              color: AmColors.text,
            ),
            IconButton(
              key: ValueKey('borda-descer-$indice'),
              tooltip: 'Descer',
              onPressed: indice == total - 1 ? null : () => onMover(1),
              icon: const Icon(CupertinoIcons.chevron_down, size: 16),
              color: AmColors.text,
            ),
            IconButton(
              key: ValueKey('borda-excluir-$indice'),
              tooltip: 'Excluir borda',
              onPressed: onExcluir,
              icon: const Icon(CupertinoIcons.trash, size: 17),
              color: AmColors.pink,
            ),
          ],
        ),
        _Segmentos<PosicaoDaBorda>(
          chave: 'borda-posicao-$indice',
          rotulo: 'Posição',
          valor: borda.posicao,
          opcoes: const {
            PosicaoDaBorda.fora: 'Fora',
            PosicaoDaBorda.dentro: 'Dentro',
            PosicaoDaBorda.centro: 'Centro',
          },
          onEscolher: (p) => onMudar(borda.copyWith(posicao: p)),
        ),
        ParameterRow(
          label: 'Espessura',
          value: borda.width.valueAt(local),
          min: 0,
          max: 100,
          unitsPerPixel: .2,
          decimals: 0,
          unit: 'px',
          onChanged: (v) =>
              onMudar(borda.copyWith(width: _editar(borda.width, local, v))),
        ),
        ParameterRow(
          label: 'Opacidade',
          value: borda.opacity.valueAt(local) * 100,
          min: 0,
          max: 100,
          unitsPerPixel: .35,
          decimals: 0,
          unit: '%',
          onChanged: (v) => onMudar(
            borda.copyWith(opacity: _editar(borda.opacity, local, v / 100)),
          ),
        ),
      ],
    ),
  );
}
