import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' show Scaffold;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/effect_preset_store.dart';
import '../../../application/estilo_preset_store.dart';
import '../../../domain/effect.dart';
import '../../../domain/effect_preset.dart';
import '../../../domain/estilo_preset.dart';
import '../../../domain/layer_meta.dart';
import 'comum_de_objetos.dart' show LinhaDeAcao;
import 'efeitos/previa_do_efeito.dart';
import 'pecas_centrais.dart' show umPasso;

enum AbaDosPresets { efeitos, estilos }

/// A TELA DOS PRESETS: tudo o que a pessoa guardou para usar de novo, com
/// a cara de cada um. Antes dela dava para SALVAR um preset e nunca mais
/// achar — a lista nao aparecia em lugar nenhum.
///
/// E uma TELA (rota), e nao uma folha: e uma lista longa com busca, que
/// numa folha de meia altura mostraria quatro linhas por vez.
Future<void> abrirTelaDePresets(
  BuildContext context, {
  required String layerId,
  required Duration at,
  AbaDosPresets aba = AbaDosPresets.efeitos,
}) => Navigator.of(context).push(
  CupertinoPageRoute<void>(
    builder: (_) => TelaDePresets(layerId: layerId, at: at, aba: aba),
  ),
);

class TelaDePresets extends ConsumerStatefulWidget {
  const TelaDePresets({
    super.key,
    required this.layerId,
    required this.at,
    this.aba = AbaDosPresets.efeitos,
  });

  final String layerId;

  /// O instante em que o preset de efeito entra (o cabecote de quando a
  /// tela abriu).
  final Duration at;
  final AbaDosPresets aba;

  @override
  ConsumerState<TelaDePresets> createState() => _TelaDePresetsState();
}

class _TelaDePresetsState extends ConsumerState<TelaDePresets> {
  late AbaDosPresets _aba = widget.aba;
  String _busca = '';

  @override
  void initState() {
    super.initState();
    EffectPresetStore.instance.load();
    EstiloPresetStore.instance.load();
  }

  bool _casa(String nome, Iterable<String> extras) {
    final q = _busca.trim().toLowerCase();
    if (q.isEmpty) return true;
    return nome.toLowerCase().contains(q) ||
        extras.any((e) => e.toLowerCase().contains(q));
  }

  /// O AVISO vai ao [AureaSnack], e nao a uma linha da tela: aplicar fecha
  /// a tela, e um recado preso nela sumiria junto antes de ser lido.
  void _avisar(String texto) {
    if (!mounted) return;
    AureaSnack.show(context, texto);
  }

  /// EXPORTAR: o preset vira um arquivo que se manda para alguem.
  Future<void> _exportar(String nome, Map<String, dynamic> dados) async {
    final limpo = nome.replaceAll(RegExp(r'[<>:"/\\|?*\x00-\x1f]'), '_');
    final bytes = Uint8List.fromList(
      utf8.encode(const JsonEncoder.withIndent('  ').convert(dados)),
    );
    final titulo = translate(context, 'Salvar preset');
    try {
      final caminho = await FilePicker.platform.saveFile(
        dialogTitle: titulo,
        fileName: '$limpo.aurea.json',
        type: FileType.custom,
        allowedExtensions: const ['json'],
        bytes: bytes,
      );
      if (caminho == null) return;
      // No celular o seletor ja grava os bytes; no desktop ele so devolve
      // o caminho, e quem grava somos nos.
      if (!Platform.isAndroid && !Platform.isIOS) {
        await File(caminho).writeAsBytes(bytes, flush: true);
      }
      if (!mounted) return;
      _avisar(moldar(context, '"{0}" salvo no arquivo', [nome]));
    } catch (_) {
      if (!mounted) return;
      _avisar(translate(context, 'Não deu para salvar o arquivo'));
    }
  }

  /// IMPORTAR: um preset que veio de fora entra na lista da pessoa, na aba
  /// do tipo dele (estilo tem `estilos`; efeito tem `effects`).
  Future<void> _importar() async {
    try {
      final r = await FilePicker.platform.pickFiles(
        type: FileType.custom,
        allowedExtensions: const ['json'],
        withData: true,
      );
      final arquivo = r?.files.firstOrNull;
      if (arquivo == null) return;
      final bruto = arquivo.bytes != null
          ? utf8.decode(arquivo.bytes!)
          : await File(arquivo.path!).readAsString();
      final m = jsonDecode(bruto);
      if (!mounted) return;
      if (m is! Map<String, dynamic>) {
        _avisar(translate(context, 'Esse arquivo não é um preset'));
        return;
      }
      if (m.containsKey('estilos')) {
        await EstiloPresetStore.instance.add(estiloPresetFromJson(m));
        if (!mounted) return;
        setState(() => _aba = AbaDosPresets.estilos);
        _avisar(translate(context, 'Estilo importado'));
      } else if (m.containsKey('effects')) {
        await EffectPresetStore.instance.add(effectPresetFromJson(m));
        if (!mounted) return;
        setState(() => _aba = AbaDosPresets.efeitos);
        _avisar(translate(context, 'Preset importado'));
      } else {
        _avisar(translate(context, 'Esse arquivo não é um preset'));
      }
    } catch (_) {
      if (!mounted) return;
      _avisar(translate(context, 'Não deu para ler o arquivo'));
    }
  }

  /// O MENU DE UM PRESET: uma folha com as acoes, cada uma uma linha.
  /// O de fabrica nao se renomeia nem se apaga — so se aplica e exporta.
  Future<void> _menuDoPreset({
    required String nome,
    required bool meu,
    required VoidCallback aplicar,
    required Future<void> Function() exportar,
    required Future<void> Function(String novo) renomear,
    required Future<void> Function() excluir,
  }) async {
    final acao = await mostrarAureaFolha<String>(
      context,
      construtor: (folha) => Padding(
        padding: const EdgeInsets.fromLTRB(
          AureaDims.margemDoPainel,
          0,
          AureaDims.margemDoPainel,
          AureaDims.e10,
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            SizedBox(
              height: AureaDims.itemDeLista,
              child: Align(
                alignment: Alignment.centerLeft,
                child: _NomeDoPreset(
                  nome: nome,
                  meu: meu,
                  estilo: AureaEstilos.titulo,
                ),
              ),
            ),
            LinhaDeAcao(
              key: const ValueKey('preset-aplicar'),
              rotulo: 'Aplicar nesta camada',
              icone: CupertinoIcons.checkmark_alt,
              aoTocar: () => Navigator.of(folha).pop('aplicar'),
            ),
            LinhaDeAcao(
              key: const ValueKey('preset-exportar'),
              rotulo: 'Exportar para um arquivo',
              icone: CupertinoIcons.square_arrow_up,
              aoTocar: () => Navigator.of(folha).pop('exportar'),
            ),
            if (meu) ...[
              LinhaDeAcao(
                key: const ValueKey('preset-renomear'),
                rotulo: 'Renomear',
                icone: CupertinoIcons.pencil,
                aoTocar: () => Navigator.of(folha).pop('renomear'),
              ),
              LinhaDeAcao(
                key: const ValueKey('preset-excluir'),
                rotulo: 'Excluir',
                icone: CupertinoIcons.trash,
                destrutiva: true,
                aoTocar: () => Navigator.of(folha).pop('excluir'),
              ),
            ],
          ],
        ),
      ),
    );
    if (!mounted) return;
    switch (acao) {
      case 'aplicar':
        aplicar();
      case 'exportar':
        await exportar();
      case 'renomear':
        final novo = await showCupertinoDialog<String>(
          context: context,
          builder: (_) => _DialogoDoNome(atual: nome),
        );
        if (novo != null && novo.isNotEmpty) await renomear(novo);
        if (mounted) setState(() {});
      case 'excluir':
        await excluir();
        if (mounted) setState(() {});
    }
  }

  /// APLICAR UM PRESET DE EFEITO: um passo de desfazer, e a tela fecha.
  /// O aviso (efeito que esta versao nao conhece) vem antes de fechar,
  /// para aparecer por cima do editor.
  void _aplicarEfeito(EffectPreset p) {
    final c = ref.read(editorControllerProvider.notifier);
    var avisos = const <String>[];
    umPasso(
      ref,
      () => avisos = c.applyPreset(widget.layerId, p, at: widget.at),
    );
    if (avisos.isNotEmpty) _avisar(translate(context, avisos.first));
    Navigator.of(context).maybePop();
  }

  /// APLICAR UM ESTILO: troca o acabamento inteiro da camada, um passo.
  void _aplicarEstilo(EstiloPreset e) {
    final c = ref.read(editorControllerProvider.notifier);
    umPasso(ref, () => c.aplicarEstilo(widget.layerId, e));
    Navigator.of(context).maybePop();
  }

  @override
  Widget build(BuildContext context) {
    // SCAFFOLD, e nao so uma pilha de widgets: o aviso do [AureaSnack]
    // precisa de um Scaffold registrado para aparecer.
    return Scaffold(
      backgroundColor: AureaCores.painel,
      body: SafeArea(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            SizedBox(
              height: AureaDims.cabecalhoDoPainel + AureaDims.e6,
              child: Row(
                children: [
                  Tocavel(
                    key: const ValueKey('presets-voltar'),
                    onTap: () => Navigator.of(context).maybePop(),
                    child: SizedBox(
                      width: AureaDims.toqueConfortavel,
                      height: AureaDims.toqueConfortavel,
                      child: Icon(
                        CupertinoIcons.chevron_left,
                        size: AureaDims.iconeMd,
                        color: AureaCores.texto,
                      ),
                    ),
                  ),
                  Expanded(
                    child: AppText(
                      'Presets',
                      maxLines: 1,
                      style: AureaEstilos.titulo.copyWith(fontSize: 17),
                    ),
                  ),
                  Semantics(
                    button: true,
                    label: translate(context, 'Importar de um arquivo'),
                    child: Tocavel(
                      key: const ValueKey('presets-importar'),
                      onTap: _importar,
                      child: SizedBox(
                        width: AureaDims.toqueConfortavel,
                        height: AureaDims.toqueConfortavel,
                        child: Icon(
                          CupertinoIcons.tray_arrow_down,
                          size: AureaDims.iconeMd,
                          color: AureaCores.texto,
                        ),
                      ),
                    ),
                  ),
                  const SizedBox(width: AureaDims.e6),
                ],
              ),
            ),
            _AbasDosPresets(
              ativa: _aba,
              aoTrocar: (a) => setState(() => _aba = a),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(
                AureaDims.margemDoPainel,
                AureaDims.e6,
                AureaDims.margemDoPainel,
                AureaDims.e8,
              ),
              child: CupertinoSearchTextField(
                key: const ValueKey('presets-busca'),
                placeholder: translate(context, 'Buscar'),
                style: AureaEstilos.corpo,
                backgroundColor: AureaCores.campo,
                onChanged: (v) => setState(() => _busca = v),
              ),
            ),
            Expanded(
              child: _aba == AbaDosPresets.efeitos
                  ? _listaDeEfeitos()
                  : _listaDeEstilos(),
            ),
          ],
        ),
      ),
    );
  }

  Widget _lista(List<Widget> linhas) {
    if (linhas.isEmpty) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(AureaDims.e20),
          child: AppText(
            'Nada aqui ainda. Salve um preset a partir de uma camada e ele aparece nesta lista, em todos os projetos.',
            textAlign: TextAlign.center,
            style: AureaEstilos.propriedade,
          ),
        ),
      );
    }
    return ListView(
      padding: const EdgeInsets.fromLTRB(
        AureaDims.margemDoPainel,
        0,
        AureaDims.e6,
        AureaDims.topoDoPainel,
      ),
      children: linhas,
    );
  }

  Widget _listaDeEfeitos() => ValueListenableBuilder<int>(
    valueListenable: EffectPresetStore.instance.revision,
    builder: (context, _, _) {
      final todos = [
        ...EffectPresetStore.instance.presets,
        ...factoryPresets(),
      ];
      // UM RELOGIO para a lista inteira: as previas trocam de quadro
      // juntas, e so enquanto a tela esta aberta.
      return RelogioDasPrevias(
        child: _lista([
          for (final p in todos)
            if (_casa(p.name, [p.category, ...p.tags]))
              _LinhaDoPreset(
                key: ValueKey('preset-efeito-${p.id}'),
                nome: p.name,
                meu: !p.builtIn,
                detalhe: p.effects.length == 1
                    ? const AppText('1 efeito')
                    : AppTextMoldado('{0} efeitos', [p.effects.length]),
                previa: _previaDoPreset(p),
                aoTocar: () => _aplicarEfeito(p),
                aoMenu: () => _menuDoPreset(
                  nome: p.name,
                  meu: !p.builtIn,
                  aplicar: () => _aplicarEfeito(p),
                  exportar: () => _exportar(p.name, effectPresetToJson(p)),
                  renomear: (novo) =>
                      EffectPresetStore.instance.rename(p.id, novo),
                  excluir: () => EffectPresetStore.instance.remove(p.id),
                ),
              ),
        ]),
      );
    },
  );

  /// A CARA DE UM PRESET DE EFEITO: a previa do primeiro efeito dele.
  Widget _previaDoPreset(EffectPreset p) {
    final tipo = p.effects.firstOrNull?.type;
    if (tipo == null || effectSpecs[tipo] == null) {
      return ColoredBox(color: AureaCores.campo);
    }
    return PreviaDoEfeito(tipo: tipo, lado: _LinhaDoPreset.ladoDaPrevia);
  }

  Widget _listaDeEstilos() => ValueListenableBuilder<int>(
    valueListenable: EstiloPresetStore.instance.revisao,
    builder: (context, _, _) {
      final todos = [
        ...EstiloPresetStore.instance.estilos,
        ...estilosDeFabrica(),
      ];
      return _lista([
        for (final e in todos)
          if (_casa(e.nome, e.partes))
            _LinhaDoPreset(
              key: ValueKey('preset-estilo-${e.id}'),
              nome: e.nome,
              meu: !e.deFabrica,
              // As partes sao rotulos do app ("borda", "sombra"): cada uma
              // passa pelo catalogo antes de juntar.
              detalhe: e.partes.isEmpty
                  ? const AppText('sem acabamento')
                  : Text(
                      e.partes.map((p) => translate(context, p)).join(' · '),
                    ),
              previa: CustomPaint(
                painter: _PintorDoEstilo(
                  e.estilos,
                  fundo: AureaCores.campoAlto,
                  corpo: AureaCores.textoSecundario,
                ),
              ),
              aoTocar: () => _aplicarEstilo(e),
              aoMenu: () => _menuDoPreset(
                nome: e.nome,
                meu: !e.deFabrica,
                aplicar: () => _aplicarEstilo(e),
                exportar: () => _exportar(e.nome, estiloPresetToJson(e)),
                renomear: (novo) =>
                    EstiloPresetStore.instance.renomear(e.id, novo),
                excluir: () => EstiloPresetStore.instance.remove(e.id),
              ),
            ),
      ]);
    },
  );
}

/// AS DUAS ABAS (Efeitos · Estilos), com a cara do [AureaTabs]: texto so,
/// a ativa no destaque com o traco de 2 embaixo.
///
/// Nao e o proprio [AureaTabs] por causa das CHAVES: ele numera as abas
/// (`<chave>-<indice>`), e a tela sempre as chamou pelo nome
/// (`presets-aba-efeitos`), que e o que os testes procuram.
class _AbasDosPresets extends StatelessWidget {
  const _AbasDosPresets({required this.ativa, required this.aoTrocar});

  final AbaDosPresets ativa;
  final ValueChanged<AbaDosPresets> aoTrocar;

  @override
  Widget build(BuildContext context) => SizedBox(
    height: AureaDims.abas,
    child: Padding(
      padding: const EdgeInsets.symmetric(
        horizontal: AureaDims.margemDoPainel - AureaDims.e10,
      ),
      child: Row(
        children: [
          for (final aba in AbaDosPresets.values)
            Tocavel(
              key: ValueKey('presets-aba-${aba.name}'),
              onTap: aba == ativa ? null : () => aoTrocar(aba),
              encolhe: 1,
              child: Padding(
                padding: const EdgeInsets.symmetric(
                  horizontal: AureaDims.e10,
                ),
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    const SizedBox(height: 4),
                    AppText(
                      switch (aba) {
                        AbaDosPresets.efeitos => 'Efeitos',
                        AbaDosPresets.estilos => 'Estilos',
                      },
                      maxLines: 1,
                      style: TextStyle(
                        fontSize: AureaDims.textoDePropriedade,
                        fontWeight: aba == ativa
                            ? FontWeight.w600
                            : FontWeight.w500,
                        color: aba == ativa
                            ? AureaCores.destaque
                            : AureaCores.textoSecundario,
                      ),
                    ),
                    const SizedBox(height: 6),
                    Container(
                      width: 16,
                      height: 2,
                      decoration: BoxDecoration(
                        color: AureaCores.destaque.withValues(
                          alpha: aba == ativa ? 1 : 0,
                        ),
                        borderRadius: BorderRadius.circular(1),
                      ),
                    ),
                  ],
                ),
              ),
            ),
        ],
      ),
    ),
  );
}

/// O NOME DE UM PRESET: o de fabrica e rotulo do app (vai ao catalogo de
/// traducao); o da pessoa e conteudo dela, e fica como ela escreveu.
class _NomeDoPreset extends StatelessWidget {
  const _NomeDoPreset({
    required this.nome,
    required this.meu,
    required this.estilo,
  });

  final String nome;
  final bool meu;
  final TextStyle estilo;

  @override
  Widget build(BuildContext context) => meu
      ? Text(
          nome,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: estilo,
        )
      : AppText(
          nome,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: estilo,
        );
}

/// UMA LINHA DA LISTA: previa, nome, detalhe e o menu. Tocar aplica;
/// segurar (ou o ⋯) abre as acoes. A linha da lista compacta do catalogo
/// de efeitos (57 de altura, previa de 40), sem caixa em volta.
class _LinhaDoPreset extends StatelessWidget {
  const _LinhaDoPreset({
    super.key,
    required this.nome,
    required this.meu,
    required this.detalhe,
    required this.previa,
    required this.aoTocar,
    required this.aoMenu,
  });

  static const double ladoDaPrevia = 40;

  final String nome;
  final bool meu;
  final Widget detalhe;
  final Widget previa;
  final VoidCallback aoTocar;
  final VoidCallback aoMenu;

  @override
  Widget build(BuildContext context) => Tocavel(
    onTap: aoTocar,
    onLongPress: aoMenu,
    encolhe: 1,
    child: SizedBox(
      height: AureaDims.blocoDePainel,
      child: Row(
        children: [
          ClipRRect(
            borderRadius: BorderRadius.circular(AureaDims.raioMd),
            child: SizedBox.square(dimension: ladoDaPrevia, child: previa),
          ),
          const SizedBox(width: AureaDims.e10),
          Expanded(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                _NomeDoPreset(
                  nome: nome,
                  meu: meu,
                  estilo: AureaEstilos.corpo.copyWith(
                    fontWeight: FontWeight.w600,
                  ),
                ),
                DefaultTextStyle.merge(
                  style: AureaEstilos.rotulo,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  child: detalhe,
                ),
              ],
            ),
          ),
          Tocavel(
            onTap: aoMenu,
            child: SizedBox(
              width: AureaDims.toqueConfortavel,
              height: AureaDims.toqueConfortavel,
              child: Icon(
                CupertinoIcons.ellipsis,
                size: AureaDims.iconeSm,
                color: AureaCores.textoSecundario,
              ),
            ),
          ),
        ],
      ),
    ),
  );
}

/// O NOME NOVO de um preset. Estado proprio para o controlador do campo
/// morrer junto com o dialogo: descarta-lo logo depois do `await` quebraria
/// o campo, que ainda existe durante a animacao de saida.
class _DialogoDoNome extends StatefulWidget {
  const _DialogoDoNome({required this.atual});

  final String atual;

  @override
  State<_DialogoDoNome> createState() => _DialogoDoNomeState();
}

class _DialogoDoNomeState extends State<_DialogoDoNome> {
  late final TextEditingController _campo = TextEditingController(
    text: widget.atual,
  );

  @override
  void dispose() {
    _campo.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => CupertinoAlertDialog(
    title: const AppText('Nome do preset'),
    content: Padding(
      padding: const EdgeInsets.only(top: AureaDims.e10),
      child: CupertinoTextField(
        key: const ValueKey('preset-nome-campo'),
        controller: _campo,
        autofocus: true,
      ),
    ),
    actions: [
      CupertinoDialogAction(
        onPressed: () => Navigator.of(context).pop(),
        child: const AppText('Cancelar'),
      ),
      CupertinoDialogAction(
        key: const ValueKey('preset-nome-ok'),
        onPressed: () => Navigator.of(context).pop(_campo.text.trim()),
        child: const AppText('Salvar'),
      ),
    ],
  );
}

/// A CARA DE UM ESTILO: um quadrado com o acabamento por cima, que e a
/// unica forma honesta de mostrar sombra, brilho e borda.
///
/// [fundo] e [corpo] chegam de fora (do tema em vigor): o pintor nao le
/// cor sozinho, e por isso sabe quando o tema mudou.
class _PintorDoEstilo extends CustomPainter {
  const _PintorDoEstilo(
    this.estilos, {
    required this.fundo,
    required this.corpo,
  });

  final LayerStyles estilos;
  final Color fundo;
  final Color corpo;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(Offset.zero & size, Paint()..color = fundo);
    final lado = math.min(size.width, size.height) * .52;
    final caixa = RRect.fromRectAndRadius(
      Rect.fromCenter(
        center: size.center(Offset.zero),
        width: lado,
        height: lado,
      ),
      Radius.circular(lado * .18),
    );
    // A escala das medidas: a previa antiga tinha ~150 px de lado e as
    // medidas do estilo eram desenhadas a meio. Numa previa de 40 o mesmo
    // fator estouraria o quadrado, entao ele acompanha o tamanho.
    final k = .5 * math.min(size.width, size.height) / 150;
    const t = Duration.zero;
    final brilho = estilos.outerGlow;
    if (brilho != null && brilho.enabled) {
      canvas.drawRRect(
        caixa,
        Paint()
          ..color = brilho.color.withValues(
            alpha: brilho.opacity.valueAt(t).clamp(0.0, 1.0),
          )
          ..maskFilter = MaskFilter.blur(
            BlurStyle.normal,
            math.max(1, brilho.size.valueAt(t) * k),
          ),
      );
    }
    final sombra = estilos.dropShadow;
    if (sombra != null && sombra.enabled) {
      canvas.drawRRect(
        caixa.shift(sombra.offsetAt(t) * k),
        Paint()
          ..color = sombra.color.withValues(
            alpha: sombra.opacity.valueAt(t).clamp(0.0, 1.0),
          )
          ..maskFilter = MaskFilter.blur(
            BlurStyle.normal,
            math.max(1, sombra.size.valueAt(t) * k),
          ),
      );
    }
    // As bordas de fora para dentro, para a primeira ficar na frente.
    for (final borda in estilos.bordas.reversed) {
      if (!borda.enabled) continue;
      canvas.drawRRect(
        caixa,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = math.max(1, borda.width.valueAt(t) * k * 1.2)
          ..color = borda.color.withValues(
            alpha: borda.opacity.valueAt(t).clamp(0.0, 1.0),
          ),
      );
    }
    final cobertura = estilos.colorOverlay;
    canvas.drawRRect(
      caixa,
      Paint()
        ..color = cobertura != null && cobertura.enabled
            ? cobertura.color.withValues(
                alpha: cobertura.opacity.valueAt(t).clamp(0.0, 1.0),
              )
            : corpo,
    );
    final interna = estilos.innerShadow;
    if (interna != null && interna.enabled) {
      canvas
        ..save()
        ..clipRRect(caixa)
        ..drawRRect(
          caixa.shift(interna.offsetAt(t) * k).inflate(2),
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeWidth = math.max(2, interna.size.valueAt(t) * k * 1.2)
            ..color = interna.color.withValues(
              alpha: interna.opacity.valueAt(t).clamp(0.0, 1.0),
            )
            ..maskFilter = MaskFilter.blur(
              BlurStyle.normal,
              math.max(1, interna.size.valueAt(t) * k * .8),
            ),
        )
        ..restore();
    }
  }

  @override
  bool shouldRepaint(_PintorDoEstilo old) =>
      !identical(old.estilos, estilos) ||
      old.fundo != fundo ||
      old.corpo != corpo;
}
