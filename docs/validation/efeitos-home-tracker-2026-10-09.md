# Efeitos, Home e tracker — 09/10/2026

## Implementação

Os oito efeitos foram acrescentados ao final do catálogo compartilhado:
WipeFlux, WipePlasma, S_Distort, Pixel Encoder, PixDither, BCC Glitch,
Displace e Starglow. C++ controla parâmetros, recursos externos e histórico;
GLSL implementa os passes. Kotlin e Swift expõem os mesmos parâmetros,
controles principais/avançados e textos em sete idiomas.

S_Distort resolve Lens e Matte separadamente. Wipes usam o progresso do
clipe visível. Glitch congela a entrada de vídeo no início do evento e
reconstrói disparos por cruzamento de limiar após seek. PixDither contém
difusão de erro real, além dos métodos de limiar ordenado. As implementações,
fontes de padrões e shaders foram escritos para o AUREA; nenhum asset ou
código extraído de outro aplicativo foi incorporado.

A Home usa lista com miniaturas e menu explícito no celular; mantém grade
em telas largas. Cabeçalho menor, barra inferior plana, criação central,
busca, ordenação e seleção em lote preservadas. Os rótulos da navegação não
quebram no meio de palavras com fonte ampliada; a acessibilidade mantém o
nome completo.

O tracker apresenta duas abas: Rastreio 2D e Câmera 3D. A câmera começa em
“1. Analisar vídeo”, com qualidade e uma ação principal. Após resolver,
mostra “2. Posicionar na cena”: criar câmera, escolher tipo e posicionar um
objeto. Exibição dos pontos, origem/escala e ajustes avançados ficam
recolhidos. Ferramentas de ponto abrem 2D; ferramenta de câmera abre 3D.
Análise, calibração, refinamento e criação continuam no motor compartilhado.

## Compilação e testes do motor

Host Windows, Release/Vulkan:

- Oito efeitos: 11 testes, 40.729 verificações, zero falhas.
- Catálogo: 176 efeitos, 1.661 parâmetros, 490 opções; 354 verificações.
- Histórico Motion Detect: 28 verificações, zero falhas.
- Histórico Datamosh: 25 verificações, zero falhas.
- Floyd Steinberg: zero pixels diferentes da referência CPU na entrada
  RGBA16F de 37×21, incluindo propagação horizontal.
- Revisão opcional: 1.360 verificações, zero falhas; seis sequências de
  60 quadros, entrada própria do tracker de 90 quadros e render PixDither 720p.
  Wipes já têm outras duas sequências de 60 quadros.

Logs: `build/validation/all-requested-effects-tests.log`,
`requested-effects-catalog-tests.log`, `history-motion-regression.log`,
`history-datamosh-regression.log` e `requested-effects-temporal-review.log`.

O tempo medido inclui render e readback no host, sem compressão do PNG.
320×180: aproximadamente 5–12 ms/quadro. Floyd Steinberg 1280×720:
aproximadamente 73–105 ms/quadro entre duas execuções de três quadros.
Isso limita a fluidez da prévia dessa opção; não é medição de exportação
Android/iOS nem uma garantia de FPS em aparelho. CSV bruto em
`build/reference/requested-effects/validation/native-render-timing.csv`.

## Android: compilação e execução nativa

APK uiTest x86_64 compilado com Gradle, instalado no emulador visível
`Aurea_API35` / `emulator-5554`, API 35, 1080×2340, densidade 440.
O aplicativo de revisão usa as telas de produção e entradas próprias;
os assets de revisão só entram no flavor uiTest.

Testes instrumentados finais: dois testes da Home, dois de cortes/extensão
até o cabeçote com undo e a regressão do menu compacto do editor. Cinco
aprovados (`home-trim-compact-final-instrumentation.log`).
Home conferida em retrato, texto do sistema em 150% e orientação horizontal.
O teste da barra inferior cobre 320 dp, fonte ampliada, centro da criação,
alvos de pelo menos 48 dp e callbacks dos cinco acessos.
Na Activity de revisão final, tocar na primeira linha abriu o projeto no
editor e o acesso Projetos retornou à Home; confirmado por XML nativo.
O build dessa Activity interativa passou em
`build/validation/home-interactive-review-android-build.log`.

Os oito painéis de efeitos foram abertos com imagem própria, com capturas
de parâmetros e prévias. Wipes conferidos em 50%, Glitch no pico do evento,
Pixel Encoder com padrão de células, Distort e Displace com barras, Starglow
com highlights próprios. PixDither foi conferido em Bayer e Floyd Steinberg;
a opção compute produziu uma prévia visível no Android. Isso é revisão
funcional pontual, sem benchmark temporal pareado do fornecedor.

Tracker executado com vídeo matemático próprio codificado em H.264:
progresso observado em 32%, solução com qualidade “Excelente”, seleção de
ponto, criação de “Câmera rastreada” e “Tracked anchor” confirmada pela
lista real de camadas. Um vídeo anterior com poucos detalhes produziu a
mensagem de falha por pouca textura. Capturas e XML ficam em
`build/reference/requested-effects/validation/android-tracker-*.{png,xml}`.
Isso comprova esse fluxo no emulador; não substitui QA em aparelho físico
com filmagens reais, nem todas as combinações de calibração e refinamento.

## iOS: implementação e limites de validação

Swift atualizado no mesmo trabalho. Catálogos gerados com 4.357 chaves em
sete idiomas. Auditorias estáticas de membros/API, símbolos da ponte,
projeto Xcode, tipos de parâmetros, layout e contrato de saídas Metal
passaram. Esses resultados verificam nomes/contratos, não tipos Swift,
compilação Metal ou execução da interface.

Compilação iOS e validação em iPhone/simulador **não executadas** neste host
Windows; exigem macOS/Xcode. Não há declaração de equivalência nativa entre
Android e iOS baseada apenas nessas auditorias.

## Fidelidade às referências

Referências de YouTube e documentação estão em
`docs/reference/effects-request-2026-10-09.md`. Os vídeos pesquisados não
fornecem renders originais isolados com a mesma entrada e parâmetros.
As sequências produzidas aqui são renders próprios, não goldens de plugins.
Nenhum efeito foi aprovado pelo usuário ou validado como idêntico.

Diferenças/pontos pendentes concretos:

- Wipes: campos de ruído, topologia, fase e brilho próprios, sem calibração
  pixel a pixel com Sapphire.
- S_Distort: gradiente de brilho e blur próprios, força ainda não calibrada
  contra a versão original.
- Pixel Encoder: dez famílias com atlas próprio; Numbers & Letters tem
  dígitos e um subconjunto de letras, sem o atlas completo do fornecedor.
- PixDither: 26 métodos e 12 paletas; faltam as demais paletas do fornecedor
  e importação/persistência ASE. Matrizes incomuns ainda sem comparação.
- Glitch: quatro grupos e congelamento, mas agenda e quatro padrões de
  dano próprios; não inclui Beat Reactor, nem toda a coleção do fornecedor.
- Displace: hipótese Displacer Pro, identidade não confirmada; iterações,
  força, antialiasing e refração usam calibração própria.
- Starglow: oito direções, três mapas e shimmer; kernel próprio e quatro
  modos de mistura, sem toda a coleção/calibração do original.

A implementação disponível para revisão não encerra a exigência “IDÊNTICOS”.
Para concluir essa exigência, a próxima evidência necessária é comparação
temporal pareada com os originais, controles/versionamento registrados e
capturas nativas das duas plataformas, além de completar os recursos acima.


## Atualização da entrega 0.0.8 / 2147

A compilação iOS Release passou no macOS/Xcode 26 em 09/10/2026, na execução
[37955312771](https://github.com/ueeruan/aurea/actions/runs/37955312771).
Bundle e IPA validados; 166/166 shaders aprovados pelo compilador/linker Metal
da Apple. As referências a compilação pendente acima registram o estado na
validação original. Execução em simulador iOS/iPhone continua pendente;
compilação não comprova execução das interfaces.
