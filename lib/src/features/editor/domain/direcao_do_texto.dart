import 'dart:ui' show TextDirection;

/// A DIRECAO BASE DE UM TEXTO, tirada do PRIMEIRO caractere com direcao.
///
/// O editor declarava `TextDirection.ltr` fixo no paragrafo do texto
/// animado. Com arabe isso e um defeito de verdade, e nao um detalhe:
/// quebra de linha, alinhamento `start`/`end` e a ordem dos trechos mistos
/// saem todos da direcao base. Um paragrafo arabe declarado LTR quebra e
/// alinha como se fosse latino.
///
/// E o primeiro caractere FORTE que decide — numero, espaco e pontuacao
/// nao tem direcao e nao votam. Sem nenhum caractere forte fica LTR, que e
/// o padrao do Unicode.
///
/// POR QUE UMA TABELA DE FAIXAS, E NAO UMA LISTA DE CARACTERES: o que
/// interessa sao os BLOCOS que escrevem da direita para a esquerda, e
/// listar codigo a codigo o alfabeto arabe inteiro (com as formas de
/// apresentacao) da uma lista de mil linhas que envelhece a cada versao do
/// Unicode. As faixas abaixo cobrem os blocos inteiros.
TextDirection direcaoDoTexto(String texto) {
  for (final r in texto.runes) {
    if (_eDireitaParaEsquerda(r)) return TextDirection.rtl;
    if (_eEsquerdaParaDireita(r)) return TextDirection.ltr;
  }
  return TextDirection.ltr;
}

/// Hebreu, arabe, sirio, thaana, nko, samaritano, mandaico e as formas de
/// apresentacao — os blocos que escrevem da direita para a esquerda.
bool _eDireitaParaEsquerda(int r) =>
    (r >= 0x0590 && r <= 0x05FF) || // hebraico
    (r >= 0x0600 && r <= 0x06FF) || // arabe
    (r >= 0x0700 && r <= 0x074F) || // sirio
    (r >= 0x0750 && r <= 0x077F) || // complemento arabe
    (r >= 0x0780 && r <= 0x07BF) || // thaana
    (r >= 0x07C0 && r <= 0x07FF) || // nko
    (r >= 0x0800 && r <= 0x083F) || // samaritano
    (r >= 0x0840 && r <= 0x085F) || // mandaico
    (r >= 0x0860 && r <= 0x086F) || // complemento sirio
    (r >= 0x08A0 && r <= 0x08FF) || // arabe estendido-A
    (r >= 0xFB1D && r <= 0xFB4F) || // hebraico de apresentacao
    (r >= 0xFB50 && r <= 0xFDFF) || // arabe de apresentacao A
    (r >= 0xFE70 && r <= 0xFEFF); // arabe de apresentacao B

/// Latinas (com acentos), grego, cirilico, kana, CJK e hangul.
bool _eEsquerdaParaDireita(int r) =>
    (r >= 0x0041 && r <= 0x005A) ||
    (r >= 0x0061 && r <= 0x007A) ||
    (r >= 0x00AA && r <= 0x00AA) ||
    (r >= 0x00B5 && r <= 0x00B5) ||
    (r >= 0x00BA && r <= 0x00BA) ||
    (r >= 0x00C0 && r <= 0x024F) || // latino estendido A e B
    (r >= 0x0370 && r <= 0x03FF) || // grego
    (r >= 0x0400 && r <= 0x04FF) || // cirilico
    (r >= 0x3040 && r <= 0x30FF) || // hiragana e katakana
    (r >= 0x3400 && r <= 0x4DBF) || // CJK estendido A
    (r >= 0x4E00 && r <= 0x9FFF) || // CJK unificado
    (r >= 0xAC00 && r <= 0xD7AF) || // hangul
    (r >= 0xF900 && r <= 0xFAFF); // CJK de compatibilidade
