import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../application/estilo_preset_store.dart';
import '../../../domain/estilo_preset.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/layer_meta.dart';
import '../../../domain/shape.dart';
import '../../am/borda_e_sombra_sheet.dart'
    show maximoDeBordas, nomesDasTerminacoes, novaBorda, sombrasProntas;
import '../../am/presets_screen.dart' show AbaDosPresets, abrirTelaDePresets;
import '../shell/contrato.dart';
import 'comum.dart';
import 'pecas_centrais.dart';

/// Grava [v] na trilha: a base quando ela esta parada, a marca do instante
/// quando ela anima (a mesma regra de todo numero do editor e do painel
/// antigo de borda e sombra).
AnimatedDouble _gravar(AnimatedDouble a, Duration local, double v) =>
    a.isAnimated
    ? a.withKeyframe(local, v, a.easeAt(local))
    : AnimatedDouble(v, const [], a.loop, a.expression);

/// BORDA E SOMBRA — o acabamento da camada:
///
///   Traco (so forma): cor, espessura, ponta, juncao, terminacoes
///   Bordas (ate quatro, por fora, por dentro ou no centro), em cartoes
///   Sombra · Sombra interna · Brilho, cada uma com o interruptor e os
///   numeros dela
///   Estilos prontos · Salvar estilo
///
/// A conta e a do painel antigo (`am/borda_e_sombra_sheet.dart`): as
/// sombras prontas, a borda nova que nasce por fora da ultima e o teto de
/// quatro bordas (cada uma e um passe inteiro na GPU).
class PainelBordaSombra extends ConsumerWidget {
  const PainelBordaSombra({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Borda e sombra';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.bordaSombra.name}';
    if (camada is AudioLayer) {
      return AureaPanel(
        titulo: _titulo,
        chave: chave,
        aoFechar: escopo.fecharPainel,
        filhos: const [
          AureaAvisoDoPainel(texto: 'Camada de áudio não tem borda.'),
        ],
      );
    }
    // SO O ACABAMENTO desta camada.
    final estilos = ref.watch(
      editorControllerProvider.select((p) => p.metaOf(layerId).styles),
    );
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      aoFechar: escopo.fecharPainel,
      corpo: NoCabecote(
        construir: (context, t) => ListView(
          padding: respiroDoPainel,
          children: _linhas(context, ref, camada, estilos, camada.localTime(t)),
        ),
      ),
    );
  }

  List<Widget> _linhas(
    BuildContext context,
    WidgetRef ref,
    Layer camada,
    LayerStyles estilos,
    Duration local,
  ) {
    final c = ref.read(editorControllerProvider.notifier);
    final id = layerId;
    void mudarEstilos(LayerStyles Function(LayerStyles) f) =>
        c.updateLayerStyles(id, f);
    void mudarBordas(List<StrokeStyle> novas) =>
        mudarEstilos((s) => comBordas(s, novas));

    // UM NUMERO DO ACABAMENTO: arrasto = um passo de desfazer.
    AureaPropertyRow numero(
      String rotulo,
      String chave,
      double valor,
      double min,
      double max,
      ValueChanged<double> mudar, {
      String unidade = 'px',
    }) => AureaPropertyRow(
      rotulo: rotulo,
      chave: chave,
      valor: valor.clamp(min, max).toDouble(),
      min: min,
      max: max,
      casas: 0,
      unidade: unidade,
      aoMudar: aCadaPasso((v) => mudar(v.clamp(min, max).toDouble())),
      aoComecarGesto: c.beginGesture,
      aoTerminarGesto: c.endGesture,
    );

    AureaPropertyRow interruptor(
      String rotulo,
      String chave,
      bool valor,
      ValueChanged<bool> mudar,
    ) => AureaPropertyRow.personalizada(
      rotulo: rotulo,
      chave: chave,
      filho: AureaToggle(
        valor: valor,
        aoMudar: (v) => umPasso(ref, () => mudar(v)),
      ),
    );

    AureaPropertyRow cor(
      String rotulo,
      String chave,
      Color atual,
      ValueChanged<Color> aplicar,
    ) => AureaPropertyRow.cor(
      rotulo: rotulo,
      chave: chave,
      cor: atual,
      aoTocar: () =>
          escolherCor(context, ref, inicial: atual, aplicar: aplicar),
    );

    // AS LINHAS DE UMA SOMBRA (externa ou interna).
    List<Widget> sombra(
      String prefixo,
      ShadowStyle s,
      void Function(ShadowStyle Function(ShadowStyle)) mudar,
    ) => [
      cor(
        'Cor',
        '$prefixo-cor',
        s.color,
        (nova) => mudar((x) => x.copyWith(color: nova)),
      ),
      numero(
        'Opacidade',
        '$prefixo-opacidade',
        s.opacity.valueAt(local) * 100,
        0,
        100,
        (v) => mudar(
          (x) => x.copyWith(opacity: _gravar(x.opacity, local, v / 100)),
        ),
        unidade: '%',
      ),
      numero(
        'Ângulo',
        '$prefixo-angulo',
        s.angleDeg.valueAt(local),
        -360,
        360,
        (v) =>
            mudar((x) => x.copyWith(angleDeg: _gravar(x.angleDeg, local, v))),
        unidade: '°',
      ),
      numero(
        'Distância',
        '$prefixo-distancia',
        s.distance.valueAt(local),
        0,
        300,
        (v) =>
            mudar((x) => x.copyWith(distance: _gravar(x.distance, local, v))),
      ),
      numero(
        'Desfoque',
        '$prefixo-desfoque',
        s.size.valueAt(local),
        0,
        120,
        (v) => mudar((x) => x.copyWith(size: _gravar(x.size, local, v))),
      ),
      numero(
        'Espalhar',
        '$prefixo-espalhar',
        s.spread.valueAt(local),
        0,
        100,
        (v) => mudar((x) => x.copyWith(spread: _gravar(x.spread, local, v))),
      ),
    ];

    final traco = camada is ShapeLayer
        ? camada.contents.whereType<ShapeStroke>().firstOrNull
        : null;
    final bordas = estilos.bordas;
    final sombraExterna = estilos.dropShadow;
    final sombraInterna = estilos.innerShadow;
    final brilho = estilos.outerGlow;

    return [
      if (camada is ShapeLayer)
        AureaSection(
          titulo: 'Traço',
          chave: 'borda-traco',
          filhos: [
            interruptor(
              'Traço',
              'borda-traco-ligado',
              traco != null,
              (v) => v ? c.ensureShapeStroke(id) : c.removeShapeStroke(id),
            ),
            if (traco != null) ...[
              cor(
                'Cor do traço',
                'borda-traco-cor',
                traco.color,
                (nova) =>
                    c.updateShapeStroke(id, (s) => s.copyWith(color: nova)),
              ),
              numero(
                'Espessura',
                'borda-traco-espessura',
                traco.width.valueAt(local),
                0,
                200,
                (v) => c.updateShapeStroke(
                  id,
                  (s) => s.copyWith(width: _gravar(s.width, local, v)),
                ),
              ),
              AureaPropertyRow.personalizada(
                rotulo: 'Ponta',
                chave: 'borda-traco-ponta',
                filho: AureaDropdown<StrokeCap>(
                  valor: traco.cap,
                  opcoes: const [
                    StrokeCap.butt,
                    StrokeCap.round,
                    StrokeCap.square,
                  ],
                  rotuloDe: (p) => switch (p) {
                    StrokeCap.butt => 'Reta',
                    StrokeCap.round => 'Redonda',
                    StrokeCap.square => 'Quadrada',
                  },
                  aoMudar: (p) => umPasso(
                    ref,
                    () => c.updateShapeStroke(id, (s) => s.copyWith(cap: p)),
                  ),
                ),
              ),
              AureaPropertyRow.personalizada(
                rotulo: 'Junção',
                chave: 'borda-traco-juncao',
                filho: AureaDropdown<StrokeJoin>(
                  valor: traco.join,
                  opcoes: const [
                    StrokeJoin.bevel,
                    StrokeJoin.round,
                    StrokeJoin.miter,
                  ],
                  rotuloDe: (j) => switch (j) {
                    StrokeJoin.bevel => 'Chanfro',
                    StrokeJoin.round => 'Redonda',
                    StrokeJoin.miter => 'Mitra',
                  },
                  aoMudar: (j) => umPasso(
                    ref,
                    () => c.updateShapeStroke(id, (s) => s.copyWith(join: j)),
                  ),
                ),
              ),
              for (final (rotulo, inicio) in const [
                ('Início', true),
                ('Fim', false),
              ])
                AureaPropertyRow.personalizada(
                  rotulo: rotulo,
                  chave: inicio
                      ? 'borda-terminacao-inicio'
                      : 'borda-terminacao-fim',
                  filho: AureaDropdown<TerminacaoDoTraco>(
                    valor: inicio ? traco.inicio : traco.fim,
                    opcoes: TerminacaoDoTraco.values,
                    rotuloDe: (x) => nomesDasTerminacoes[x] ?? x.name,
                    titulo: 'Ponta do traço',
                    aoMudar: (x) => umPasso(
                      ref,
                      () => c.updateShapeStroke(
                        id,
                        (s) =>
                            inicio ? s.copyWith(inicio: x) : s.copyWith(fim: x),
                      ),
                    ),
                  ),
                ),
              if (traco.inicio != TerminacaoDoTraco.nenhuma ||
                  traco.fim != TerminacaoDoTraco.nenhuma)
                AureaPropertyRow(
                  rotulo: 'Tamanho das pontas',
                  chave: 'borda-terminacao-tamanho',
                  valor: traco.tamanhoDaTerminacao.clamp(1, 10).toDouble(),
                  min: 1,
                  max: 10,
                  casas: 1,
                  unidade: '×',
                  aoMudar: aCadaPasso(
                    (v) => c.updateShapeStroke(
                      id,
                      (s) => s.copyWith(tamanhoDaTerminacao: v),
                    ),
                  ),
                  aoComecarGesto: c.beginGesture,
                  aoTerminarGesto: c.endGesture,
                ),
            ],
          ],
        ),
      AureaSection(
        titulo: 'Bordas',
        chave: 'bordas',
        filhos: [
          for (var i = 0; i < bordas.length; i++)
            _CartaoDaBorda(
              key: ValueKey('cartao-borda-$i'),
              indice: i,
              total: bordas.length,
              borda: bordas[i],
              local: local,
              // umPasso DENTRO de um arrasto nao empilha nada (o gesto ja
              // abriu o grupo); fora dele, o olho e a posicao viram um
              // passo proprio de desfazer.
              aoMudar: (nova) => umPasso(ref, () {
                final lista = [...bordas];
                lista[i] = nova;
                mudarBordas(lista);
              }),
              aoMenu: (acao) {
                final lista = [...bordas];
                switch (acao) {
                  case 'subir':
                    lista.insert(i - 1, lista.removeAt(i));
                  case 'descer':
                    lista.insert(i + 1, lista.removeAt(i));
                  case 'apagar':
                    lista.removeAt(i);
                }
                umPasso(ref, () => mudarBordas(lista));
              },
              cor: cor,
              numero: numero,
            ),
          if (bordas.length < maximoDeBordas)
            FileiraDeAcoes(
              acoes: [
                AureaChip(
                  key: const ValueKey('borda-adicionar'),
                  rotulo: 'Adicionar borda',
                  icone: CupertinoIcons.plus,
                  aoTocar: () => umPasso(
                    ref,
                    () => mudarBordas([...bordas, novaBorda(bordas, local)]),
                  ),
                ),
              ],
            ),
        ],
      ),
      AureaSection(
        titulo: 'Sombra',
        chave: 'sombra',
        filhos: [
          interruptor(
            'Sombra',
            'sombra-ligada',
            sombraExterna?.enabled ?? false,
            (v) => mudarEstilos(
              (s) => v
                  ? s.copyWith(
                      dropShadow: (s.dropShadow ?? ShadowStyle()).copyWith(
                        enabled: true,
                      ),
                    )
                  : s.copyWith(clearDropShadow: true),
            ),
          ),
          if (sombraExterna?.enabled ?? false) ...[
            FileiraDeAcoes(
              acoes: [
                for (final pronta in sombrasProntas.entries)
                  AureaChip(
                    key: ValueKey('sombra-pronta-${pronta.key}'),
                    rotulo: pronta.key,
                    aoTocar: () => umPasso(
                      ref,
                      () => mudarEstilos(
                        (s) => s.copyWith(
                          dropShadow: pronta.value().copyWith(
                            color: s.dropShadow?.color,
                          ),
                        ),
                      ),
                    ),
                  ),
              ],
            ),
            ...sombra(
              'sombra',
              sombraExterna!,
              (f) => mudarEstilos((s) => s.copyWith(dropShadow: f(s.dropShadow!))),
            ),
          ],
        ],
      ),
      AureaSection(
        titulo: 'Sombra interna',
        chave: 'sombra-interna',
        inicialmenteAberta: sombraInterna?.enabled ?? false,
        filhos: [
          interruptor(
            'Sombra interna',
            'sombra-interna-ligada',
            sombraInterna?.enabled ?? false,
            (v) => mudarEstilos(
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
          if (sombraInterna?.enabled ?? false)
            ...sombra(
              'sombra-interna',
              sombraInterna!,
              (f) =>
                  mudarEstilos((s) => s.copyWith(innerShadow: f(s.innerShadow!))),
            ),
        ],
      ),
      AureaSection(
        titulo: 'Brilho',
        chave: 'brilho',
        inicialmenteAberta: brilho?.enabled ?? false,
        filhos: [
          interruptor(
            'Brilho',
            'brilho-ligado',
            brilho?.enabled ?? false,
            (v) => mudarEstilos(
              (s) => v
                  ? s.copyWith(
                      outerGlow: (s.outerGlow ?? GlowStyle()).copyWith(
                        enabled: true,
                      ),
                    )
                  : s.copyWith(clearOuterGlow: true),
            ),
          ),
          if (brilho?.enabled ?? false) ...[
            cor(
              'Cor do brilho',
              'brilho-cor',
              brilho!.color,
              (nova) => mudarEstilos(
                (s) => s.copyWith(outerGlow: s.outerGlow!.copyWith(color: nova)),
              ),
            ),
            numero(
              'Opacidade',
              'brilho-opacidade',
              brilho.opacity.valueAt(local) * 100,
              0,
              100,
              (v) => mudarEstilos(
                (s) => s.copyWith(
                  outerGlow: s.outerGlow!.copyWith(
                    opacity: _gravar(s.outerGlow!.opacity, local, v / 100),
                  ),
                ),
              ),
              unidade: '%',
            ),
            numero(
              'Tamanho',
              'brilho-tamanho',
              brilho.size.valueAt(local),
              0,
              120,
              (v) => mudarEstilos(
                (s) => s.copyWith(
                  outerGlow: s.outerGlow!.copyWith(
                    size: _gravar(s.outerGlow!.size, local, v),
                  ),
                ),
              ),
            ),
          ],
        ],
      ),
      // O ACABAMENTO INTEIRO VIRA ESTILO: e o que a pessoa refaz mais
      // vezes — contorno branco com sombra, neon, adesivo.
      FileiraDeAcoes(
        acoes: [
          AureaChip(
            key: const ValueKey('estilo-galeria'),
            rotulo: 'Estilos prontos',
            icone: CupertinoIcons.square_grid_2x2,
            aoTocar: () => abrirTelaDePresets(
              context,
              layerId: id,
              at: EscopoDoEditor.of(context).playback.time.value,
              aba: AbaDosPresets.estilos,
            ),
          ),
          AureaChip(
            key: const ValueKey('estilo-salvar'),
            rotulo: 'Salvar estilo',
            icone: CupertinoIcons.bookmark,
            aoTocar: () => _salvarEstilo(context, estilos, camada.name),
          ),
        ],
      ),
    ];
  }
}

/// SALVAR O ACABAMENTO como estilo com nome, para qualquer camada de
/// qualquer projeto (a mesma loja do painel antigo).
Future<void> _salvarEstilo(
  BuildContext context,
  LayerStyles estilos,
  String sugestao,
) async {
  if (estilos.isEmpty) {
    AureaSnack.show(
      context,
      translate(context, 'Esta camada ainda não tem acabamento a salvar'),
    );
    return;
  }
  final campo = TextEditingController(text: sugestao);
  final nome = await showCupertinoDialog<String>(
    context: context,
    builder: (dialogo) => CupertinoAlertDialog(
      title: const AppText('Nome do estilo'),
      content: Padding(
        padding: const EdgeInsets.only(top: AureaDims.e10),
        child: CupertinoTextField(
          key: const ValueKey('estilo-nome-campo'),
          controller: campo,
          autofocus: true,
        ),
      ),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.of(dialogo).pop(),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          key: const ValueKey('estilo-nome-ok'),
          onPressed: () => Navigator.of(dialogo).pop(campo.text.trim()),
          child: const AppText('Salvar'),
        ),
      ],
    ),
  );
  campo.dispose();
  if (nome == null || nome.isEmpty) return;
  await EstiloPresetStore.instance.add(
    EstiloPreset(nome: nome, estilos: estilos),
  );
  if (!context.mounted) return;
  AureaSnack.show(
    context,
    '${translate(context, 'Estilo salvo para todos os projetos')}: $nome',
  );
}

/// UMA BORDA DA CAMADA num cartao do DS: olho (liga sem apagar), ⋯ (subir,
/// descer, apagar) e, aberta, cor, posicao, espessura e opacidade.
class _CartaoDaBorda extends StatelessWidget {
  const _CartaoDaBorda({
    super.key,
    required this.indice,
    required this.total,
    required this.borda,
    required this.local,
    required this.aoMudar,
    required this.aoMenu,
    required this.cor,
    required this.numero,
  });

  final int indice;
  final int total;
  final StrokeStyle borda;
  final Duration local;
  final ValueChanged<StrokeStyle> aoMudar;
  final ValueChanged<String> aoMenu;
  final AureaPropertyRow Function(String, String, Color, ValueChanged<Color>)
  cor;
  final AureaPropertyRow Function(
    String,
    String,
    double,
    double,
    double,
    ValueChanged<double>, {
    String unidade,
  })
  numero;

  @override
  Widget build(BuildContext context) {
    final k = 'borda-$indice';
    return AureaEffectCard(
      chave: k,
      nome: '${translate(context, 'Borda')} ${indice + 1}',
      traduzirNome: false,
      ligado: borda.enabled,
      inicialmenteAberto: true,
      aoAlternarLigado: () => aoMudar(borda.copyWith(enabled: !borda.enabled)),
      aoMenu: (botao) async {
        final escolha = await mostrarAureaMenu<String>(
          botao,
          itens: [
            if (indice > 0)
              const AureaMenuItem(
                valor: 'subir',
                rotulo: 'Subir',
                icone: CupertinoIcons.arrow_up,
              ),
            if (indice < total - 1)
              const AureaMenuItem(
                valor: 'descer',
                rotulo: 'Descer',
                icone: CupertinoIcons.arrow_down,
              ),
            const AureaMenuItem(
              valor: 'apagar',
              rotulo: 'Apagar',
              icone: CupertinoIcons.trash,
              destrutivo: true,
            ),
          ],
        );
        if (escolha != null) aoMenu(escolha);
      },
      filhos: [
        cor('Cor', '$k-cor', borda.color, (c) => aoMudar(borda.copyWith(color: c))),
        AureaPropertyRow.personalizada(
          rotulo: 'Posição',
          chave: '$k-posicao',
          filho: AureaDropdown<PosicaoDaBorda>(
            valor: borda.posicao,
            opcoes: PosicaoDaBorda.values,
            rotuloDe: (p) => switch (p) {
              PosicaoDaBorda.fora => 'Fora',
              PosicaoDaBorda.dentro => 'Dentro',
              PosicaoDaBorda.centro => 'Centro',
            },
            aoMudar: (p) => aoMudar(borda.copyWith(posicao: p)),
          ),
        ),
        numero(
          'Espessura',
          '$k-espessura',
          borda.width.valueAt(local),
          0,
          100,
          (v) => aoMudar(borda.copyWith(width: _gravar(borda.width, local, v))),
        ),
        numero(
          'Opacidade',
          '$k-opacidade',
          borda.opacity.valueAt(local) * 100,
          0,
          100,
          (v) => aoMudar(
            borda.copyWith(opacity: _gravar(borda.opacity, local, v / 100)),
          ),
          unidade: '%',
        ),
      ],
    );
  }
}
