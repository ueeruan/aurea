import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../storage/prefs.dart';
import 'translations.dart';

const appLanguages = <String, String>{
  'pt': 'Português', 'en': 'English', 'es': 'Español', 'ar': 'العربية',
  'ko': '한국어', 'ja': '日本語', 'zh': '简体中文', 'hi': 'हिन्दी',
  'id': 'Bahasa Indonesia', 'ru': 'Русский',
};
const languagePreferenceKey = 'settings.language';
final appLanguageProvider = NotifierProvider<AppLanguageController, String>(AppLanguageController.new);

class AppLanguageController extends Notifier<String> {
  @override
  String build() {
    final saved = ref.read(sharedPreferencesProvider).getString(languagePreferenceKey);
    return appLanguages.containsKey(saved) ? saved! : 'pt';
  }
  Future<void> select(String code) async {
    if (!appLanguages.containsKey(code)) return;
    await ref.read(sharedPreferencesProvider).setString(languagePreferenceKey, code);
    state = code;
  }
}

String translate(BuildContext context, String source) =>
    translateFor(Localizations.maybeLocaleOf(context)?.languageCode ?? 'pt', source);

String translateFor(String code, String source) {
  if (code == 'pt') return source;
  return appTranslations[source]?[code] ?? source;
}

/// Only application labels use this widget. User-authored content stays Text.
class AppText extends Text {
  const AppText(super.data, {super.key, super.style, super.strutStyle,
    super.textAlign, super.textDirection, super.locale, super.softWrap,
    super.overflow, super.textScaler, super.maxLines, super.semanticsLabel,
    super.textWidthBasis, super.textHeightBehavior});
  @override
  Widget build(BuildContext context) => Text(
    translate(context, data!), style: style, strutStyle: strutStyle,
    textAlign: textAlign, textDirection: textDirection, locale: locale,
    softWrap: softWrap, overflow: overflow, textScaler: textScaler,
    maxLines: maxLines, semanticsLabel: semanticsLabel == null ? null : translate(context, semanticsLabel!),
    textWidthBasis: textWidthBasis, textHeightBehavior: textHeightBehavior,
  ).build(context);
}

// ==========================================================================
// O TEXTO MONTADO — o molde vai ao catalogo, os valores entram depois.
// ==========================================================================
//
// O PROBLEMA, e ele e estrutural:
//
//   AppText('Excluir ${ids.length} projetos?')
//
// O Dart monta a string ANTES de o `AppText` existir, e o que chega ao
// catalogo e "Excluir 3 projetos?" — que nao casa com chave nenhuma. Ou
// seja: TODA frase com um numero, um nome ou um tempo dentro ficava em
// portugues, em qualquer idioma. Nao era falta de traducao; era falta de
// um caminho por onde traduzir.
//
// O CAMINHO E O MOLDE: a frase vai ao catalogo COM OS MARCADORES, e os
// valores entram depois de a traducao ter sido escolhida.
//
//   moldar(context, 'Excluir {0} projetos?', [ids.length])
//
// OS MARCADORES SAO POSICIONAIS, e nao nomeados, de proposito: quem traduz
// pode REORDENAR. Em japones o numero vem depois do que ele conta, em
// arabe a frase inteira se le da direita para a esquerda — um molde com
// nomes amarraria as duas linguas a ordem do portugues.
//
// O QUE ACONTECE QUANDO O MOLDE NAO ESTA NO CATALOGO: os valores sao
// substituidos do mesmo jeito, e a frase sai em portugues. Isso e
// deliberado — o contrario (devolver o molde cru) mostraria `{0}` na tela,
// que e pior do que uma frase na lingua errada.
final RegExp _marcadorDoMolde = RegExp(r'\{(\d+)\}');

/// Monta o texto de um molde no idioma [code].
String moldarPara(String code, String molde, List<Object?> valores) {
  final texto = translateFor(code, molde);
  return texto.replaceAllMapped(_marcadorDoMolde, (m) {
    final i = int.tryParse(m.group(1)!);
    if (i == null || i < 0 || i >= valores.length) return m.group(0)!;
    return '${valores[i]}';
  });
}

/// Monta o texto de um molde no idioma da tela.
String moldar(BuildContext context, String molde, List<Object?> valores) =>
    moldarPara(
      Localizations.maybeLocaleOf(context)?.languageCode ?? 'pt',
      molde,
      valores,
    );

/// O TEXTO MONTADO, na lingua certa.
///
/// Existe para o caso comum (um `Text` montado) nao obrigar quem escreve a
/// lembrar da ordem: molde primeiro, valores depois.
class AppTextMoldado extends StatelessWidget {
  const AppTextMoldado(
    this.molde,
    this.valores, {
    super.key,
    this.style,
    this.textAlign,
    this.maxLines,
    this.overflow,
    this.semanticsLabel,
  });

  final String molde;
  final List<Object?> valores;
  final TextStyle? style;
  final TextAlign? textAlign;
  final int? maxLines;
  final TextOverflow? overflow;
  final String? semanticsLabel;

  @override
  Widget build(BuildContext context) => Text(
    moldar(context, molde, valores),
    style: style,
    textAlign: textAlign,
    maxLines: maxLines,
    overflow: overflow,
    semanticsLabel: semanticsLabel == null
        ? null
        : moldar(context, semanticsLabel!, valores),
  );
}

/// OS MARCADORES DE UM MOLDE, para quem precisa conferir sem montar.
///
/// E o que o teste usa para garantir que uma traducao nao perdeu nem
/// inventou marcador: uma traducao com `{0}` a mais mostraria a chave crua
/// na tela, e uma com `{0}` a menos perderia o numero.
List<int> marcadoresDoMolde(String molde) => [
  for (final m in _marcadorDoMolde.allMatches(molde))
    if (int.tryParse(m.group(1)!) case final i?) i,
];
