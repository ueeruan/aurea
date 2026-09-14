/// A VERSAO QUE O APLICATIVO DIZ TER.
///
/// Ela estava escrita a mao na tela Sobre e parou em `1.2.0 (35)` — 32
/// entregas atras. Isso nao e cosmetico: quando alguem manda um registro
/// de travada, a primeira pergunta e "de qual build?", e a tela que devia
/// responder respondia errado. Uma rodada inteira de diagnostico se
/// perdeu nisso.
///
/// Agora e uma constante so, e `test/versao_bate_com_pubspec_test.dart`
/// falha se ela sair de sincronia com o `pubspec.yaml`. Esquecer de
/// atualizar deixou de ser possivel em silencio.
/// A NUMERACAO VOLTOU PARA TRAS DE PROPOSITO: 1.6.9 -> 1.0.0-beta.1.
///
/// Os 1.6.x eram versoes de desenvolvimento, e nunca houve um 1.0. A
/// interface de edicao foi refeita do zero contra a referencia medida, o
/// painel deixou de ser estrutura e virou controle, e efeitos, curva e
/// keyframes voltaram ligados — e o primeiro beta do 1.0 de verdade.
///
/// O NUMERO DA COMPILACAO NAO VOLTA. Ele so cresce, sempre: e por ele
/// que a loja e o aparelho decidem o que e mais novo, e um `versionCode`
/// menor faz a instalacao ser recusada sem explicacao util.
const versaoDoApp = '1.0.5-beta';

/// O numero da compilacao — o que muda a cada IPA/APK entregue.
const buildDoApp = 80;

/// Como aparece para quem le: `1.0.0-beta.1 (71)`.
const versaoCompleta = '$versaoDoApp ($buildDoApp)';
