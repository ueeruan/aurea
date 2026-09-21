import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/ds/ds.dart';
import '../../../core/l10n/app_language.dart';
import '../../editor/application/editor_controller.dart';
import '../../editor/domain/layer.dart' show CaptionLayer;
import '../../editor/domain/lottie_export.dart';
import '../../editor/domain/template_pack.dart';
import '../../editor/presentation/ui/paineis/comum_de_objetos.dart'
    show LinhaDeAcao;
import '../../editor/presentation/ui/paineis/pecas_centrais.dart'
    show respiroDoPainel;
import '../../editor/presentation/ui/shell/ajustes_do_projeto.dart'
    show LinhaDeAjuste;
import '../../projects/domain/pacote_aurea.dart';

// ===========================================================================
// OUTROS FORMATOS — o que NAO e video
// ===========================================================================
//
// Tudo o que decide o VIDEO (formato, tamanho, quadros, codec, qualidade)
// mora na tela de exportacao, a porta unica: esta folha ja repetiu aqueles
// controles e os dois resumos discordavam do peso do arquivo. O que fica
// aqui e o que nunca foi video: legendas `.srt`, Lottie, SVG animado,
// template e o pacote `.aurea`.
//
// A folha le o projeto por conta propria (um `Consumer` dentro dela): o
// `ref` de quem abriu fica so pela assinatura publica, porque a tela de
// exportacao pode sair da arvore com a folha aberta.

/// A fracao da tela que a folha pode ocupar: sobra o topo da tela de
/// exportacao, que e onde o toque fora fecha.
const _fracaoDaFolha = .7;

/// Abre "Outros formatos" (chamada pela tela de exportacao).
Future<void> showOutrosFormatosSheet(
  BuildContext context,
  WidgetRef ref,
) async {
  await mostrarAureaFolha<void>(
    context,
    titulo: 'Outros formatos',
    construtor: (folha) => ConstrainedBox(
      constraints: BoxConstraints(
        maxHeight: MediaQuery.sizeOf(folha).height * _fracaoDaFolha,
      ),
      child: const _OutrosFormatos(),
    ),
  );
}

class _OutrosFormatos extends ConsumerStatefulWidget {
  const _OutrosFormatos();

  @override
  ConsumerState<_OutrosFormatos> createState() => _OutrosFormatosState();
}

class _OutrosFormatosState extends ConsumerState<_OutrosFormatos> {
  /// Um arquivo por vez: o seletor de "salvar como" do sistema nao aceita
  /// dois pedidos abertos, e o segundo toque sumiria sem resposta.
  bool _ocupado = false;

  /// O resultado da ultima acao, DENTRO da folha: um aviso flutuante
  /// nasceria na tela de exportacao, atras dela.
  String? _status;
  bool _statusDeErro = false;

  void _avisar(String texto, {bool erro = false}) {
    if (!mounted) return;
    setState(() {
      _ocupado = false;
      _status = texto;
      _statusDeErro = erro;
    });
  }

  /// SALVAR OS BYTES onde a pessoa escolher.
  ///
  /// O nome sai limpo dos caracteres que o Windows e o Android recusam
  /// (um projeto "Meu/logo" viraria pasta). No celular o proprio seletor
  /// grava os [bytes]; no desktop ele so devolve o caminho, e a gravacao
  /// e nossa.
  Future<void> _salvarBytes(String nome, Uint8List bytes, String rotulo) async {
    setState(() => _ocupado = true);
    try {
      final nomeLimpo = nome.replaceAll(RegExp(r'[<>:"/\\|?*\x00-\x1f]'), '_');
      final caminho = await FilePicker.platform.saveFile(
        dialogTitle: moldar(context, 'Salvar {0}', [rotulo]),
        fileName: nomeLimpo,
        type: FileType.custom,
        allowedExtensions: [nomeLimpo.split('.').last],
        bytes: bytes,
      );
      if (caminho != null && !Platform.isAndroid && !Platform.isIOS) {
        await File(caminho).writeAsBytes(bytes, flush: true);
      }
      if (!mounted) return;
      _avisar(
        caminho == null
            ? translate(context, 'Exportação cancelada')
            : moldar(context, '{0} salvo', [rotulo]),
      );
    } catch (e) {
      if (!mounted) return;
      _avisar(
        moldar(context, 'Não foi possível salvar: {0}', [e]),
        erro: true,
      );
    }
  }

  Future<void> _salvarTexto(String nome, String conteudo, String rotulo) =>
      _salvarBytes(nome, Uint8List.fromList(utf8.encode(conteudo)), rotulo);

  Future<void> _exportarSvg() async {
    final projeto = ref.read(editorControllerProvider);
    final String svg;
    try {
      svg = exportAnimatedSvg(projeto);
    } catch (_) {
      _avisar(
        translate(context, 'Não foi possível gerar o SVG deste projeto.'),
        erro: true,
      );
      return;
    }
    // Sem nenhum caminho vetorial o arquivo sairia vazio: melhor dizer o
    // que falta do que entregar um SVG em branco.
    if (!svg.contains('<path')) {
      _avisar(
        translate(context, 'Adicione uma forma vetorial para exportar SVG.'),
        erro: true,
      );
      return;
    }
    await _salvarTexto(
      '${projeto.name}.svg',
      svg,
      translate(context, 'SVG animado'),
    );
  }

  void _exportarLottie() {
    final projeto = ref.read(editorControllerProvider);
    final saida = exportLottie(projeto);
    _salvarTexto(
      '${projeto.name}.json',
      const JsonEncoder.withIndent('  ').convert(saida.json),
      saida.skipped > 0
          ? moldar(context, 'Lottie ({0} camadas, {1} puladas)', [
              saida.exported,
              saida.skipped,
            ])
          : moldar(context, 'Lottie ({0} camadas)', [saida.exported]),
    );
  }

  void _exportarLegendas(CaptionLayer legenda) {
    final srt = ref
        .read(editorControllerProvider.notifier)
        .exportCaptionsSrt(legenda.id);
    // Nulo so quando a camada deixou de ser legenda (apagada por baixo):
    // a folha observa o projeto e a linha ja vai sumir.
    if (srt == null) return;
    final nome = legenda.name.replaceAll(RegExp(r'[^A-Za-z0-9_-]+'), '_');
    _salvarTexto('$nome.srt', srt, translate(context, 'legendas (.srt)'));
  }

  @override
  Widget build(BuildContext context) {
    final projeto = ref.watch(editorControllerProvider);
    final c = ref.read(editorControllerProvider.notifier);
    final problemas = validateForLottie(projeto);
    final bloqueios = problemas.where((i) => i.blocking).toList();
    final avisos = problemas.where((i) => !i.blocking).toList();
    final doTemplate = validateTemplate(projeto);
    final travaDoTemplate = doTemplate.where((i) => i.blocking).toList();
    final legendas = projeto.layers.whereType<CaptionLayer>().toList();
    final status = _status;

    return ListView(
      shrinkWrap: true,
      padding: respiroDoPainel,
      children: [
        if (legendas.isNotEmpty)
          AureaSection(
            titulo: 'Legendas',
            chave: 'outros-legendas',
            recolhivel: false,
            filhos: [
              // Uma por camada de legenda. O nome e da pessoa: vai montado
              // no molde, que ja sai traduzido.
              for (final legenda in legendas)
                LinhaDeAcao(
                  key: ValueKey('export-srt-${legenda.id}'),
                  rotulo: moldar(context, 'Legendas (.srt) · {0}', [
                    legenda.name,
                  ]),
                  icone: CupertinoIcons.captions_bubble,
                  habilitada: !_ocupado,
                  aoTocar: () => _exportarLegendas(legenda),
                ),
            ],
          ),
        AureaSection(
          titulo: 'Para produto (Lottie / SVG)',
          chave: 'outros-produto',
          recolhivel: false,
          filhos: [
            // O MODO COMPATIVEL avisa desde o comeco, enquanto a pessoa
            // monta, em vez de surpreender no fim.
            LinhaDeAjuste(
              key: const ValueKey('exportar-modo-lottie'),
              icone: CupertinoIcons.checkmark_shield,
              titulo: 'Modo compatível com Lottie',
              subtitulo:
                  'Avisa sobre o que não sobrevive enquanto você monta.',
              ligado: projeto.lottieMode,
              aoTocar: () =>
                  c.runAsOneUndo(() => c.setLottieMode(!projeto.lottieMode)),
            ),
            // O VALIDADOR: o que nao sobrevive, camada por camada — antes
            // de exportar, e nao depois de a pessoa abrir o arquivo.
            _LinhaDeAviso(
              key: const ValueKey('exportar-lottie-veredito'),
              icone: bloqueios.isEmpty
                  ? CupertinoIcons.checkmark_circle
                  : CupertinoIcons.exclamationmark_triangle,
              cor: bloqueios.isEmpty ? AureaCores.destaque : AureaCores.perigo,
              texto: bloqueios.isEmpty
                  ? translate(context, 'Tudo sobrevive ao Lottie.')
                  : moldar(context, '{0} camada(s) NÃO sobrevivem:', [
                      bloqueios.length,
                    ]),
              forte: true,
            ),
            for (final i in bloqueios)
              _LinhaDeAviso(
                icone: CupertinoIcons.exclamationmark_triangle,
                cor: AureaCores.perigo,
                texto: '${i.layerName}: ${translate(context, i.message)}',
              ),
            for (final i in avisos)
              _LinhaDeAviso(
                icone: CupertinoIcons.info_circle,
                cor: AureaCores.textoSecundario,
                texto: '${i.layerName}: ${translate(context, i.message)}',
              ),
            LinhaDeAcao(
              key: const ValueKey('exportar-lottie'),
              rotulo: 'Exportar Lottie (.json)',
              icone: CupertinoIcons.doc_text,
              habilitada: !_ocupado,
              aoTocar: _exportarLottie,
            ),
            LinhaDeAcao(
              key: const ValueKey('exportar-svg'),
              rotulo: 'Exportar SVG animado',
              icone: CupertinoIcons.scribble,
              habilitada: !_ocupado,
              aoTocar: _exportarSvg,
            ),
          ],
        ),
        AureaSection(
          titulo: 'Template',
          chave: 'outros-template',
          recolhivel: false,
          filhos: [
            _LinhaDeAviso(
              key: const ValueKey('exportar-template-resumo'),
              icone: travaDoTemplate.isEmpty
                  ? CupertinoIcons.slider_horizontal_3
                  : CupertinoIcons.exclamationmark_triangle,
              cor: travaDoTemplate.isEmpty
                  ? AureaCores.textoSecundario
                  : AureaCores.perigo,
              texto: travaDoTemplate.isEmpty
                  ? moldar(
                      context,
                      '{0} campo(s) para quem receber preencher.',
                      [projeto.exposed.length],
                    )
                  : translate(context, travaDoTemplate.first.message),
            ),
            for (final aviso in doTemplate.where((i) => !i.blocking))
              _LinhaDeAviso(
                icone: CupertinoIcons.info_circle,
                cor: AureaCores.textoSecundario,
                texto: translate(context, aviso.message),
              ),
            LinhaDeAcao(
              key: const ValueKey('exportar-template'),
              rotulo: 'Exportar template',
              icone: CupertinoIcons.square_on_square,
              habilitada: !_ocupado && travaDoTemplate.isEmpty,
              aoTocar: () => _salvarTexto(
                '${projeto.name}.aurea-template.json',
                TemplatePack(name: projeto.name, project: projeto).encode(),
                translate(context, 'Template'),
              ),
            ),
            // O PACOTE: projeto + midias num arquivo so, que abre em
            // qualquer aparelho.
            LinhaDeAcao(
              key: const ValueKey('exportar-pacote'),
              rotulo: 'Exportar pacote .aurea',
              icone: CupertinoIcons.archivebox,
              habilitada: !_ocupado,
              aoTocar: () => _salvarBytes(
                '${projeto.name}.aurea',
                PacoteAurea.montar(projeto),
                translate(context, 'Pacote do projeto'),
              ),
            ),
          ],
        ),
        if (status != null)
          Padding(
            key: const ValueKey('exportar-status'),
            padding: const EdgeInsets.only(top: AureaDims.e8),
            // Ja montado e traduzido na hora do aviso: Text, nao AppText.
            child: Text(
              status,
              style: AureaEstilos.propriedade.copyWith(
                color: _statusDeErro ? AureaCores.perigo : AureaCores.destaque,
              ),
            ),
          ),
      ],
    );
  }
}

/// Uma linha de aviso do validador: icone pequeno e o texto ja montado
/// (o nome da camada e conteudo; a mensagem passou pelo catalogo).
class _LinhaDeAviso extends StatelessWidget {
  const _LinhaDeAviso({
    super.key,
    required this.icone,
    required this.cor,
    required this.texto,
    this.forte = false,
  });

  final IconData icone;
  final Color cor;
  final String texto;
  final bool forte;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: AureaDims.e4),
    child: Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.only(top: 1),
          child: Icon(icone, size: AureaDims.iconeSm - 2, color: cor),
        ),
        const SizedBox(width: AureaDims.e6),
        Expanded(
          child: Text(
            texto,
            style: forte
                ? AureaEstilos.corpo.copyWith(
                    fontWeight: FontWeight.w600,
                    color: cor,
                  )
                : AureaEstilos.propriedade.copyWith(fontSize: 11, height: 1.35),
          ),
        ),
      ],
    ),
  );
}
