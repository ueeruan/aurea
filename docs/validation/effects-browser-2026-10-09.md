# Catálogo de efeitos — 09/10/2026

## Implementação

Android e iOS receberam busca sempre visível, abas Todos / Novos / Favoritos /
Recentes, categorias horizontais e uma grade adaptativa de cartões maiores.
Os cartões mostram o nome em até três linhas, categoria, identificação de novo
e favorito. Um toque adiciona; toque longo altera o favorito. A busca mantém o
índice de nomes e sinônimos em sete idiomas, incluindo os nomes de referência.
Todos os textos novos foram adicionados aos sete catálogos das duas plataformas.

O motor C++ marca os oito efeitos recentes no bit 0 da palavra de metadados do
catálogo. O POD permanece com 32 bytes; Android e a ponte iOS consomem a mesma
informação. Novos contém Displace, S_Distort, BCC Glitch, Pixel Encoder,
PixDither, Starglow, WipeFlux e WipePlasma, respeitando os filtros da camada.

As prévias usam os shaders próprios dos efeitos no motor compartilhado. O novo
passe de comparação mantém a imagem original à esquerda e o resultado à
direita, com divisor e fundo quadriculado para transparência. Os rótulos mantêm
essa posição também em idiomas RTL. A demonstração usa um instante fixo para
efeitos animados. Os parâmetros de demonstração de Pixel Encoder e Starglow
foram ajustados, sem alterar os valores padrão de efeitos aplicados ao projeto.
As duas interfaces invalidam o cache antigo e exibem estados de carregamento,
indisponibilidade e efeitos que precisam ser vistos na timeline.

Em telas menores ou com fonte ampliada, a grade reduz as colunas. As abas
permitem rolagem quando os rótulos não cabem. Não foram incorporados shaders,
código ou assets extraídos de aplicativos de referência.

## Compilação

- Android: `assembleUiTest` e `assembleUiTestAndroidTest` passaram; ABI x86_64.
- Motor Windows: `aurea_tests` Release compilado, incluindo o shader novo.
- iOS: auditorias de API Swift, símbolos da ponte, projeto Xcode, layout,
  parâmetros de efeitos e saídas de fragment shaders passaram.
- Compilação Xcode e execução nativa iOS permanecem pendentes: não há macOS,
  simulador iOS ou iPhone conectado neste ambiente Windows. Auditoria estática
  não comprova compilação ou execução nativa.

## Validação executada

- 22 testes unitários Android de busca/catálogo: zero falhas.
- 7 testes do motor de prévias: 30 verificações, zero falhas. Inclui o teste que
  comprova preservação da metade original, resultado na metade direita e alfa
  opaco da comparação.
- Catálogo completo do motor: 170 prévias renderizadas, seis recusadas por
  classe temporal/global; 177 verificações, zero falhas.
- Android API 35 no emulador visível: teste de navegação passou em retrato normal
  e em 320 dp com fonte de 130%. Conferidos Novos (oito efeitos), favorito,
  Recentes, categorias, abertura de ferramentas, aplicação de VHS e busca por
  sinônimo. Uma primeira captura em fonte ampliada revelou corte em Favoritos;
  a correção com abas roláveis foi testada e capturada novamente.
- A versão final também passou em retrato e com dimensões horizontais
  2340×1080 (simuladas por `wm size`), incluindo a aplicação de Pixel Encoder
  diretamente pela aba Novos e recuperação de uma busca sem resultados.
  A primeira tentativa usando somente `user_rotation` permaneceu em retrato;
  somente `android-wide-test.log` e `wide-fx-add-new.png` comprovam a tela larga.

Logs e capturas estão em `build/reference/effects-browser-20261009/`.


## Atualização da entrega 0.0.8 / 2147

A compilação iOS Release passou no macOS/Xcode 26 em 09/10/2026, na execução
[37955312771](https://github.com/ueeruan/aurea/actions/runs/37955312771).
Bundle e IPA validados; 166/166 shaders aprovados pelo compilador/linker Metal
da Apple. As referências a compilação pendente acima registram o estado na
validação original. Execução em simulador iOS/iPhone continua pendente;
compilação não comprova execução das interfaces.
