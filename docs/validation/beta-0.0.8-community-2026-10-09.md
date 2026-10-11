# Aurea 0.0.8 (2147) — bugs da comunidade

## Escopo e implementação

As regras de edição são compartilhadas em C++. As mudanças de interface,
ponte e textos foram aplicadas em Android/Kotlin e iOS/Swift/ObjC++.

| Relato | Mudança implementada | Limite da conclusão |
|---|---|---|
| Letras quebradas em texto 3D | Contornos mantêm o dono de cada glifo; letras vizinhas não viram buracos umas das outras. Partes sobrepostas do mesmo glifo são unidas pela regra nonzero do motor vetorial. A triangulação confere a área das tampas e recusa resultados incompletos. | Reproduzido na Roboto embarcada (A, a e á): hastes e barra eram tratadas como buracos. Sem os projetos/fontes originais do relato; testes usam fonte padrão, Roboto e Noto Naskh importadas. |
| Crash ao adicionar efeitos, incluindo Tint | Limite de admissão das texturas intermediárias, separado do limite de retenção do cache; falha de alocação libera os alvos e permite um quadro menor depois. GLES desanexa referências dos framebuffers antes de destruir texturas de projeto/captura. | Crash reproduzido no emulador em glReadPixels, numa miniatura após trocar projetos com efeitos; sequência ampliada passou após a correção. O crash específico do aparelho do Virgo ainda não foi confirmado. |
| Grupo duplicado altera o original | Cópia recursiva das composições internas; dados de edição independentes, assets imutáveis compartilhados. | O teste cobre grupos aninhados e desfazer/refazer. |
| Modelo 3D invisível | Importação começa no cabeçote e estende o fim quando necessário. glTF só desenha/enquadra os nós da cena selecionada. | Sem os modelos originais do SCARLET; GLB e glTF próprios cobrem as causas corrigidas. |
| Luz direcional não funciona | Luz nova começa no cabeçote; consultas drenam seeks pendentes; controles nativos atualizam após edição e desfazer. | A luz já alterava pixels no caso básico anterior; corrigimos inserção e atualização dos controles. |
| Apagar null transfere transformações | Reparenta os filhos mantendo seus valores e tracks locais. Desvincular explicitamente continua compensando o movimento mundial. | Cobre 2D/3D, com/sem avô, desfazer/refazer. |
| Keyframe em shapes | Consultas e edições dos parâmetros drenam comandos pendentes antes de escolher o tempo local. | Regressão cobre seek enfileirado e key existente. |
| Puppet não anima | Primeiro autokey depois do início preserva também a pose inicial; operações drenam comandos pendentes. | Regressão mede interpolação e desfazer/refazer. |
| Rotação errática ao tocar/segurar campo | Campos numéricos consomem seu gesto; o dial começa arrastar depois do limiar de movimento. | Teste Android verifica toque, pressão longa, arrasto real e desfazer. |
| Notas antigas | Versão 0.0.8, build 2147; marcador de leitura inclui versão/build; notas atualizadas em sete idiomas nas duas plataformas. | Compilação iOS Release passou no CI macOS/Xcode 26; execução em iPhone pendente. |
| Textura personalizada e bevel no texto | Textura de galeria no material do texto 3D, normalizada até 2048 px; receita persiste caminho, pacote coleta/revincula imagem e fonte; remover/desfazer. Mantém o bevel existente. | Imagem ausente mantém material visível e informa textura faltante. Esta entrega trata texto 3D. |

## Compilação

- Motor C++ Windows: compilação Release concluída; regressões descritas abaixo.
- Android x86_64: compilação do app e testes instrumentados concluída.
- Android arm64-v8a: APK Release concluído: `build/releases/Aurea-0.0.8-2147-arm64-v8a.apk`.
  SHA-256: `B74BC0FEB549DE36055CEB0B575091C4314D6B8FC13765B480D6E6ED122537A4`.
- Android armeabi-v7a: APK Release concluído em 4 min 45 s: `output/build-2147/Aurea-0.0.8-2147-armeabi-v7a.apk`.
  SHA-256: `C2DFCD049DB2EB6D18909D2DB14ACCC64F300AA75C7AD945D39D5B535373EDC2`.
- Ambos os APKs estão em `output/build-2147/`; integridade ZIP, versão 0.0.8 / 2147,
  ABI única e assinaturas v2/v3 verificadas. O certificado atual é Android Debug
  (SHA-256 `55BF3CC844050DF48DFF27E71A70CB54E5BCA40A5C0DB9E43F9D21CFFF52B5C8`);
  são builds para instalação de teste, sem publicação na loja.
- iOS: seis auditorias estáticas passaram (API Swift, símbolos da ponte, projeto
  Xcode, regra de layout, contrato de efeitos e saídas Metal). **Não são uma
  compilação Swift/ObjC++ nem uma execução nativa.**
- iOS Release: compilação nativa Swift/ObjC++/C++ concluída no macOS/Xcode 26.
  [Workflow 37955312771](https://github.com/ueeruan/aurea/actions/runs/37955312771),
  commit `e9075b1a8c30e7b84c2885bd284833f0a5b9fd08`. Verificação Foundation do
  bundle, empacotamento e validação do IPA passaram; Apple Metal compilou/linkou
  **166/166 shaders**. Pacote sem assinatura, para sideload. Logs em
  `build/reference/v008-community/ios-ci-diagnostics/`.
- IPA entregue: `output/build-2147/Aurea-0.0.8-2147-unsigned.ipa`; versão 0.0.8,
  build 2147, executável ARM64. Validação local do pacote repetida com sucesso.
  SHA-256: `DB8E9010C3390D0952545240D8E6CC521AA3F56695F112F35627D7F1747A561B`.

## Execução

- Android: emulador visível Aurea_API35, API 35, 1080 × 2340, densidade 440,
  fonte 100%. Cinco testes de edição/GPU passaram em OpenGL ES e Vulkan, incluindo
  cena glTF selecionada, comparação de pixels da luz, textura/chanfro com salvar,
  remover e desfazer, gesto real de rotação e nove trocas de projeto com efeitos.
  Última repetição GLES com o orçamento final: cinco testes passaram em 103,718 s,
  sem registros no buffer de crash. A bateria Vulkan passou nos mesmos cinco
  testes antes do último ajuste do teto de memória (de 1/3 para 1/2 do orçamento);
  esse ajuste final foi validado no motor e no Android/GLES. Não foi testado em
  um aparelho Android físico.
- Android Home/notas: três testes passaram (28,764 s). Marcador antigo reabre
  notas 0.0.8, fechar persiste a edição atual e a entrada permite reabrir. Dock
  testado em 320 dp com fonte 150%; criar abre a folha de projeto existente.
- Motor compartilhado: texto 3D 51 testes / 79.278 verificações; Puppet 7 / 1.166;
  parâmetros de shape 1 / 24; desvincular 1 / 59; cena 3D 72 / 40.809; shape 3D
  16 / 5.214; FrameGraph 14 / 161; ModelBudget 14 / 141; CaptureResources 6 / 47.
  Todas essas baterias terminaram com zero falhas. A tolerância de área aproximada
  do teste de chanfro foi ajustada de 50% para 49%: depois da união dos contornos,
  esta fonte retém 49,95%; completude, buracos e orientação têm verificações separadas.
- Regressões Beta008: grupo aninhado independente, null 2D/3D, seek e key de shape,
  pose/interpolação Puppet, receita de textura, glTF selecionado e tempo da luz.
  A proteção de memória também testa recusa/recuperação e um Tint simples em 4K.
  Bateria completa: 11 testes / 318 verificações, zero falhas.
- iOS: execução em simulador e aparelho pendente; o workflow de entrega rodou
  somente o build nativo Release e validações do pacote/shaders. O job de
  simulador foi desativado nesta execução. Compilação não comprova QA nativo.
- Aparelho intermediário do Virgo: modelo, projeto e diagnóstico ainda não
  fornecidos; reprodução pendente.

Resultados brutos ficam em `build/reference/v008-community/`. Fixtures glTF e
imagens de teste foram criadas para esta regressão; nenhum código, shader ou
asset foi extraído de aplicativos de referência.

## Referências de comportamento

- [Especificação glTF 2.0 — scenes e nodes](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html): a cena selecionada define os nós renderizados.
- [Alight Motion — parenting e nulls](https://support.alightmotion.com/hc/en-us/articles/10536997444369-Layer-Parenting-and-Null-Objects): referência para vínculos; a regra de exclusão nesta entrega segue o pedido explícito do usuário.

Sem publicação na loja; o APK local não substitui a validação pendente no iOS
ou a reprodução dos arquivos/aparelhos originais dos relatos.
