# AUREA PC — beta teste 0.0.1

Entrega local de 13/10/2026: editor nativo Windows x64, com espaços de trabalho
Composição e Edição orientados pela análise REA do After Effects e Premiere Pro
instalados nesta máquina. A implementação utiliza o motor C++ e o renderizador
Vulkan do AUREA, com componentes, cores, ícones e lógica de interface próprios.

## Abrir o beta

[Pacote portátil ZIP](C:/Users/Ruan/Documents/Aureabeta/output/aurea-pc-beta-0.0.1.zip)
— extraia a pasta e abra `AUREA-PC.exe`, mantendo as DLLs ao lado do executável.
Requer Windows 10/11 x64 e driver de GPU com Vulkan. Não há instalador.

- [Executável já extraído](C:/Users/Ruan/Documents/Aureabeta/output/aurea-pc-beta-0.0.1/AUREA-PC.exe).
- [Instruções e comando de build](C:/Users/Ruan/Documents/Aureabeta/docs/pc-beta001.md).
- `Ctrl+F2`: Composição; `Ctrl+F3`: Edição.
- `Ctrl+I`: importar; `Ctrl+S`: salvar; `Ctrl+O`: abrir projeto.

O pacote inclui cinco DLLs FFmpeg, runtime MSVC local, licenças e instruções.
O manifesto confere 32 arquivos; o ZIP contém 33 membros, incluindo o manifesto.

| Artefato | Bytes | SHA-256 |
| --- | ---: | --- |
| AUREA-PC.exe | 85.701.632 | `6aac89e5d9278190a520487a23bf0b30f6705c67748ace94f64961a763543879` |
| aurea-pc-beta-0.0.1.zip | 95.676.925 | `da7c5495e3a2ed0f5b4c6a5d4a6f9d0b99229b771d17012099d09fa8ed3970bf` |

## Implementação

Composição organiza Projeto/Controles de efeitos à esquerda, a composição no
centro, Propriedades/Efeitos à direita e camadas/keyframes abaixo. Edição move
Projeto para a região inferior esquerda, mantém o monitor central e organiza
os controles de efeitos, propriedades e timeline em torno dele. Os divisores
são ajustáveis, com foco por teclado, nomes acessíveis e fontes adaptadas ao DPI.

O host conserva importação real de imagem, vídeo e áudio, texto e formas,
catálogo de efeitos com parâmetros, projetos `.aurea`, histórico, keyframes,
curvas, reprodução, render 2D/3D e exportação MP4. A timeline possui faixas V/A,
waveform consultada no motor e cores por tipo de camada. Keyframes, navegação
anterior/próximo e segmentos usam as consultas do motor compartilhado; o gráfico
converte tempo da composição para tempo local da camada antes de consultar a curva.

Esta beta não implementa toda a suite Adobe: um Source Monitor independente,
mixer completo e docking por arraste não foram entregues. A identidade visual
exata, todos os gestos e equivalência de pixels com AE/PR não foram comprovados.
Nenhum código, shader, fonte, ícone ou asset extraído da Adobe foi incorporado
ao produto. Materiais de referência e resultados brutos permanecem em
`build/reference/`.

## Engenharia reversa com REA

A skill `reverse-engineer-anything` e o leitor portátil de recursos PE do REA
identificaram os recursos do AfterFX.exe e AfterFXLib.dll. Quatro XMLs de
workspace foram materializados pelos intervalos exatos retornados pelo REA,
com tamanho e SHA-256 conferidos. Um parser complementar projetou a hierarquia
dos painéis e as proporções publicadas. IDs e valores são observações estáticas;
sua interpretação espacial é derivada, não uma medição da janela em execução.

- [Referência After Effects](C:/Users/Ruan/Documents/Aureabeta/docs/parity/after-effects-pc-reference-20261013.md): DLL 26.5.0.89, 3.015 recursos e 6.041 entradas; evidência `ev_1eb2b91893ebc4338433cb0a4ef0a6021a526a8c39053338ae0dfd7b56247ab6`.
- [Referência Premiere Pro](C:/Users/Ruan/Documents/Aureabeta/docs/parity/premiere-pc-reference-20261013.md): executável 26.5.2.5; layouts Editing, Effects e Assembly publicados na instalação; evidência `ev_1ecb8acfe40b3f2891099ece3e6766bdcbddcafd458ee46c7313dd3d03900bc0`.

O EXE do Premiere tem 780.886.536 bytes e excede o limite de 512 MiB do leitor PE
REA disponível. A limitação foi registrada. Os XMLs selecionados foram agrupados
em um ZIP criado para análise, inventariado pelo REA e comparado byte a byte com
os originais. Esse ZIP é um carrier do analista, não um pacote distribuído pela
Adobe. Ghidra/IDA não estavam configurados; não se recuperou pseudocódigo nativo.

A captura de UI nativa do REA disponível exige macOS/Mach-O e não cobre este
host Windows. Computer Use foi usado como complemento: observou AE em projeto
vazio com uma composição de teste, e Premiere na tela inicial/diálogo de novo
projeto. O editor do Premiere não foi observado. O addon Neucurve visível na
instalação do usuário foi distinguido dos quatro layouts padrão selecionados.

O usuário interrompeu Computer Use com Esc. Toda interação com janelas foi
encerrada; nenhuma automação alternativa de UI foi executada depois disso.

## Compilação e validação

Release Windows x64 compilado com MSVC 19.51.36260, CMake 4.4.3 e Ninja. Um único
diretório incremental, `engine/build/pc-beta001`, foi reutilizado. O build passou
a reconhecer o prefixo português de `/showIncludes`, registrando dependências
de headers corretamente. O encoder PNG foi corrigido para usar formato BGRA
aceito pelo WIC, com preservação exata dos pixels conferida no teste.

**197 verificações distintas passaram, zero falhas:** 165 combinações de
geometria temporal/zoom/scroll e 32 verificações de integração. As mesmas 197
passaram no executável portátil com `PATH` restrito a Windows/System32 e ao
pacote, sem DLLs do ambiente de build.

Houve execução real de render Vulkan numa NVIDIA GeForce RTX 3050, Vulkan
1.4.351, PNG com caminho Unicode e round-trip dos pixels, importação e decode de
mídia, salvar/reabrir projeto, keyframes/navegação/undo, render 2D/3D e exportação
de composição H.264/AAC em MP4 com 30/30 quadros. A codificação de vídeo testada
usou libx264 em software. O teste de áudio lê além do priming AAC e exige amostras
não silenciosas.

- [Relatório de validação](C:/Users/Ruan/Documents/Aureabeta/build/validation/pc-beta001/VALIDACAO.md).
- [Resultados e integridade da entrega](C:/Users/Ruan/Documents/Aureabeta/build/validation/pc-beta001/delivery-validation.json).
- [Teste do pacote portátil](C:/Users/Ruan/Documents/Aureabeta/build/validation/pc-beta001/package-self-test/desktop-tests.txt).

Os testes `--self-test` são headless e não criam a janela do editor. **Validação
visual e interativa dos dois workspaces permanece pendente após a interrupção
com Esc**, incluindo gestos, navegação por teclado e salvar pela interface.

O REA também inventariou a entrega final: PE32+ x64 com hash correspondente ao
EXE, evidência `ev_645a38ff8d966bf8383d6481380a04e6a0ed5c9fdb11983b8759a2950d478915`,
e o ZIP final, evidência `ev_a9b3bb1494031e4c31d47a0937881b769f45c4703c1cda84a7a79b7bd3b4c010`.
Respostas em `build/reference/aurea-pc-beta-0.0.1/`. Esse inventário estático
confirma os artefatos analisados, não substitui execução ou avaliação visual.

As mudanças desta entrega estão no host Windows e na preparação de seu build.
Regras de edição continuam no motor C++ compartilhado. Android e iOS não tiveram
novo build ou validação nativa neste trabalho; seus resultados anteriores são
separados dos resultados Windows. Alterações e arquivos preexistentes no projeto
foram preservados; não foi criado commit.
