import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/storage/prefs.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/font_service.dart';
import '../../../domain/layer.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_de_objetos.dart';

/// O rotulo da camada SEM fonte escolhida (desenha na fonte do sistema).
const rotuloDaFontePadrao = 'Padrão';

// -------------------------------------------- favoritas e recentes (prefs)
//
// AS MESMAS CHAVES do navegador de fontes antigo: quem ja tinha favoritado
// fontes continua vendo as estrelas. Sem preferencias (testes), vivem na
// memoria.

const _chaveDasFavoritas = 'fontes.favoritas';
const _chaveDasRecentes = 'fontes.recentes';

/// Quantas fontes usadas por ultimo ficam na lista de recentes.
const maximoDeFontesRecentes = 8;

final List<String> _favoritasNaMemoria = [];
final List<String> _recentesNaMemoria = [];

SharedPreferences? _prefs(WidgetRef ref) {
  try {
    return ref.read(sharedPreferencesProvider);
  } catch (_) {
    return null;
  }
}

List<String> fontesFavoritas(WidgetRef ref) =>
    _prefs(ref)?.getStringList(_chaveDasFavoritas) ?? [..._favoritasNaMemoria];

List<String> fontesRecentes(WidgetRef ref) =>
    _prefs(ref)?.getStringList(_chaveDasRecentes) ?? [..._recentesNaMemoria];

void _gravar(
  WidgetRef ref,
  String chave,
  List<String> memoria,
  List<String> v,
) {
  final prefs = _prefs(ref);
  if (prefs == null) {
    memoria
      ..clear()
      ..addAll(v);
    return;
  }
  prefs.setStringList(chave, v);
}

/// A ESTRELA de uma fonte: poe ou tira das favoritas.
void alternarFonteFavorita(WidgetRef ref, String familia) {
  final lista = fontesFavoritas(ref);
  if (!lista.remove(familia)) lista.add(familia);
  _gravar(ref, _chaveDasFavoritas, _favoritasNaMemoria, lista);
}

/// A fonte usada agora vai para o topo das recentes.
void registrarFonteUsada(WidgetRef ref, String familia) {
  final lista = fontesRecentes(ref)
    ..remove(familia)
    ..insert(0, familia);
  if (lista.length > maximoDeFontesRecentes) {
    lista.removeRange(maximoDeFontesRecentes, lista.length);
  }
  _gravar(ref, _chaveDasRecentes, _recentesNaMemoria, lista);
}

/// FONTE — escolher olhando: cada linha e o PROPRIO texto da camada
/// desenhado na fonte, com o nome embaixo. Busca pelo nome, favoritas e
/// recentes no topo, e o "+" do cabecalho importa .ttf/.otf.
///
/// A lista e preguicosa (`ListView.builder`): quem importou cem fontes
/// nao paga cem paragrafos de uma vez.
///
/// O NOME DA FONTE e conteudo, nao rotulo: vai em `Text`, sem catalogo.
class PainelFonte extends ConsumerStatefulWidget {
  const PainelFonte({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelFonte> createState() => _PainelFonteState();
}

/// Uma linha da lista: cabecalho de secao ou fonte.
sealed class _Item {
  const _Item();
}

class _Secao extends _Item {
  const _Secao(this.titulo);
  final String titulo;
}

class _Fonte extends _Item {
  const _Fonte(this.secao, this.familia);

  /// 'fav', 'rec' ou 'todas' — a mesma fonte pode aparecer em duas.
  final String secao;

  /// Nulo = a fonte padrao.
  final String? familia;
}

class _PainelFonteState extends ConsumerState<PainelFonte> {
  static const _titulo = 'Fonte';

  String _busca = '';
  bool _importando = false;

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, widget.layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.fonte.name}';
    if (camada is! TextLayer) {
      return PainelDeTipoErrado(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é de texto.',
      );
    }
    return ValueListenableBuilder<int>(
      // FONTE IMPORTADA FICA PRONTA DEPOIS (o registro e assincrono): a
      // lista se refaz quando o servico avisa.
      valueListenable: FontService.instance.revision,
      builder: (context, _, _) => AureaPanel(
        titulo: _titulo,
        chave: chave,
        aoFechar: escopo.fecharPainel,
        acoes: [
          Tocavel(
            key: const ValueKey('fonte-importar'),
            onTap: _importando ? null : _importar,
            child: SizedBox(
              width: AureaDims.toqueConfortavel,
              height: AureaDims.cabecalhoDoPainel,
              child: Icon(
                CupertinoIcons.plus,
                size: AureaDims.iconeMd,
                color: _importando
                    ? AureaCores.textoSecundario
                    : AureaCores.destaque,
              ),
            ),
          ),
        ],
        corpo: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(
                AureaDims.margemDoPainel,
                0,
                AureaDims.margemDoPainel,
                AureaDims.e4,
              ),
              child: CupertinoTextField(
                key: const ValueKey('fonte-busca'),
                placeholder: translate(context, 'Procurar fonte'),
                placeholderStyle: AureaEstilos.corpo.copyWith(
                  color: AureaCores.textoSecundario,
                ),
                style: AureaEstilos.corpo,
                prefix: Padding(
                  padding: const EdgeInsets.only(left: AureaDims.e8),
                  child: Icon(
                    CupertinoIcons.search,
                    size: AureaDims.iconeSm,
                    color: AureaCores.textoSecundario,
                  ),
                ),
                padding: const EdgeInsets.symmetric(
                  horizontal: AureaDims.e8,
                  vertical: AureaDims.e6,
                ),
                decoration: BoxDecoration(
                  color: AureaCores.campo,
                  borderRadius: BorderRadius.circular(AureaDims.raioXl),
                ),
                onTapOutside: (_) => FocusScope.of(context).unfocus(),
                onChanged: (v) => setState(() => _busca = v),
              ),
            ),
            Expanded(child: _lista(camada)),
          ],
        ),
      ),
    );
  }

  List<_Item> _itens() {
    final todas = FontService.instance.families;
    final q = _busca.trim().toLowerCase();
    if (q.isNotEmpty) {
      final achadas = [
        for (final f in todas)
          if (f.toLowerCase().contains(q)) _Fonte('todas', f),
      ];
      return achadas;
    }
    final favoritas = [
      for (final f in fontesFavoritas(ref))
        if (todas.contains(f)) f,
    ];
    final recentes = [
      for (final f in fontesRecentes(ref))
        if (todas.contains(f)) f,
    ];
    return [
      if (favoritas.isNotEmpty) ...[
        const _Secao('Favoritas'),
        for (final f in favoritas) _Fonte('fav', f),
      ],
      if (recentes.isNotEmpty) ...[
        const _Secao('Recentes'),
        for (final f in recentes) _Fonte('rec', f),
      ],
      const _Secao('Todas'),
      const _Fonte('todas', null),
      for (final f in todas) _Fonte('todas', f),
    ];
  }

  Widget _lista(TextLayer camada) {
    final itens = _itens();
    if (itens.isEmpty) {
      return const Padding(
        padding: EdgeInsets.symmetric(horizontal: AureaDims.margemDoPainel),
        child: AureaAvisoDoPainel(texto: 'Nenhuma fonte com esse nome.'),
      );
    }
    final favoritas = fontesFavoritas(ref);
    final amostra = camada.text.trim().isEmpty
        ? 'Aa Bb Cc 123'
        : camada.text.split('\n').first;
    return ListView.builder(
      key: const ValueKey('fonte-lista'),
      padding: const EdgeInsets.fromLTRB(
        AureaDims.margemDoPainel,
        0,
        AureaDims.e6,
        AureaDims.topoDoPainel,
      ),
      itemCount: itens.length,
      itemBuilder: (context, i) => switch (itens[i]) {
        _Secao(:final titulo) => Padding(
          padding: const EdgeInsets.only(top: AureaDims.e8),
          child: Text(
            translate(context, titulo).toUpperCase(),
            style: AureaEstilos.secao,
          ),
        ),
        _Fonte(:final secao, :final familia) => _LinhaDaFonte(
          key: ValueKey('fonte-$secao-${familia ?? 'padrao'}'),
          familia: familia,
          amostra: amostra,
          escolhida: camada.fontFamily == familia,
          favorita: familia != null && favoritas.contains(familia),
          aoEscolher: () => _escolher(familia),
          aoFavoritar: familia == null
              ? null
              : () => setState(() => alternarFonteFavorita(ref, familia)),
          aoApagar: familia == null || FontService.instance.isBundled(familia)
              ? null
              : () => _apagar(familia, camada),
        ),
      },
    );
  }

  void _escolher(String? familia) {
    final c = ref.read(editorControllerProvider.notifier);
    if (familia == null) {
      c.editTextLayer(widget.layerId, clearFont: true);
      return;
    }
    c.editTextLayer(widget.layerId, fontFamily: familia);
    registrarFonteUsada(ref, familia);
  }

  Future<void> _apagar(String familia, TextLayer camada) async {
    final escolha = await mostrarAureaMenu<bool>(
      context,
      titulo: familia,
      itens: const [
        AureaMenuItem(
          valor: true,
          rotulo: 'Apagar fonte do aparelho',
          icone: CupertinoIcons.trash,
          destrutivo: true,
        ),
      ],
    );
    if (escolha != true) return;
    await FontService.instance.remove(familia);
    if (!mounted) return;
    if (camada.fontFamily == familia) {
      ref
          .read(editorControllerProvider.notifier)
          .editTextLayer(widget.layerId, clearFont: true);
    }
    setState(() {});
  }

  /// IMPORTAR .ttf/.otf: a fonte e copiada para dentro do app (o projeto
  /// continua abrindo se o arquivo original sumir) e a primeira importada
  /// ja vai para a camada.
  Future<void> _importar() async {
    if (_importando) return;
    FocusScope.of(context).unfocus();
    EscopoDoEditor.of(context).playback.pause();
    setState(() => _importando = true);
    try {
      final r = await FilePicker.platform.pickFiles(
        type: FileType.custom,
        allowedExtensions: const ['ttf', 'otf'],
        allowMultiple: true,
      );
      if (r == null) return;
      final resultado = await FontService.instance.importMany(
        r.files.map((f) => f.path).whereType<String>(),
      );
      if (!mounted) return;
      if (resultado.imported.isNotEmpty) {
        _escolher(resultado.imported.first);
      }
      final falhas = r.files.length - resultado.imported.length;
      AureaSnack.show(
        context,
        falhas > 0
            ? '${resultado.imported.length} fontes importadas · '
                  '$falhas arquivos não puderam ser lidos'
            : '${resultado.imported.length} fontes importadas',
      );
    } catch (_) {
      if (mounted) AureaSnack.show(context, 'Não foi possível abrir as fontes');
    } finally {
      if (mounted) setState(() => _importando = false);
    }
  }
}

/// UMA FONTE NA LISTA: a amostra na propria fonte (18) e o nome embaixo;
/// a estrela e, nas importadas, o apagar.
class _LinhaDaFonte extends StatelessWidget {
  const _LinhaDaFonte({
    super.key,
    required this.familia,
    required this.amostra,
    required this.escolhida,
    required this.favorita,
    required this.aoEscolher,
    this.aoFavoritar,
    this.aoApagar,
  });

  final String? familia;
  final String amostra;
  final bool escolhida;
  final bool favorita;
  final VoidCallback aoEscolher;
  final VoidCallback? aoFavoritar;
  final VoidCallback? aoApagar;

  @override
  Widget build(BuildContext context) {
    final nome = familia;
    return Tocavel(
      onTap: aoEscolher,
      onLongPress: aoApagar,
      encolhe: 1,
      child: SizedBox(
        height: AureaDims.linhaDePropriedade,
        child: Row(
          children: [
            Expanded(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    amostra,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      fontSize: 18,
                      height: 1.15,
                      fontFamily: resolveFontFamily(nome),
                      color: escolhida ? AureaCores.destaque : AureaCores.texto,
                    ),
                  ),
                  if (nome == null)
                    AppText(
                      rotuloDaFontePadrao,
                      maxLines: 1,
                      style: AureaEstilos.rotulo,
                    )
                  else
                    Text(
                      nome,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: AureaEstilos.rotulo,
                    ),
                ],
              ),
            ),
            if (escolhida)
              Icon(
                CupertinoIcons.checkmark_alt,
                size: AureaDims.iconeSm,
                color: AureaCores.destaque,
              ),
            if (aoFavoritar != null)
              Tocavel(
                key: ValueKey('fonte-estrela-${nome ?? ''}'),
                onTap: aoFavoritar,
                child: SizedBox(
                  width: AureaDims.toqueConfortavel,
                  height: AureaDims.toqueConfortavel,
                  child: Icon(
                    favorita ? CupertinoIcons.star_fill : CupertinoIcons.star,
                    size: AureaDims.iconeSm,
                    color: favorita
                        ? AureaCores.acao
                        : AureaCores.textoSecundario,
                  ),
                ),
              )
            else
              const SizedBox(width: AureaDims.toqueConfortavel),
          ],
        ),
      ),
    );
  }
}
