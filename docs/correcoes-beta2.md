# Beta 2 — correções e validação

## Android — build 2103

- A abertura de codecs de vídeo acontece fora do caminho que bloqueia o render e a leitura do estado pela interface.
- O prefetch não repete buscas e decodificação sem parar quando a janela não cabe no cache. O último frame disponível não é removido apenas por ultrapassar o orçamento individual.
- Miniaturas não fazem a varredura de keyframes usada no preview e usam prioridade de codec secundária.
- A Home usa os recursos `plurals` corretos ao mostrar a contagem de projetos.
- Vulkan 1.0 é aceito com shaders compatíveis. Vulkan 1.1 mantém importação externa de vídeo quando as extensões necessárias existem; falhas nas extensões opcionais permitem tentar um dispositivo sem elas. Erros de inicialização exibem etapa e identificação do aparelho.
- Texto 3D: escolha de fontes do catálogo, importação TTF/OTF, onda e giros por letra nos eixos X/Y/Z, intensidade, duração e defasagem. A receita é salva no projeto. Chrome/Gold usam chanfro para destacar reflexos; ambientes preparados em segundo plano atualizam o preview mesmo pausado.
- Avisos restaurados do Worker existente `mural-do-aurea.aureaapp.workers.dev/aviso`, com cache, expiração, dispensar aviso, links e popup único por ID. Nenhum aviso foi publicado no servidor.

## Validação concluída

- 623 testes do motor, 661.629 verificações, zero falhas.
- 39 testes Android, zero falhas.
- Renderização real na GPU do host usando o caminho de compatibilidade Vulkan 1.0.
- APKs release ARM64 e ARMv7 gerados e verificados com a assinatura anterior do app.
- 64 shaders traduzidos com SPIRV-Cross para Metal, preservando slots de recursos e tamanhos dos grupos de compute.
- 64/64 shaders aprovados pelo compilador Metal da Apple no macOS.
- Home Android abriu e recebeu o aviso real do Worker. A checagem de navegação no emulador foi interrompida por solicitação do usuário; o emulador foi encerrado.

Não houve teste físico no S21 nem no aparelho do beta tester. Os testes acima verificam as regressões encontradas no código; a confirmação de desempenho nesses aparelhos permanece pendente.

## iOS

O fluxo antigo de IPA sem assinatura foi adaptado ao app nativo C++/SwiftUI na branch autorizada `codex/native-beta2-ipa` de `ueeruan/aurea`. O workflow compila com Xcode no macOS e guarda o IPA como artefato; não envia ao TestFlight. O instalador de sideload precisa assinar o IPA.

Foram corrigidos os nomes da ponte Swift, o estado do seletor da Home, chaves de tradução duplicadas, a ordem de compilação/link e os formatos de vídeo Metal. Os shaders agora entram como blobs MSL, e a ponte ObjC++ é compilada uma única vez pelo CMake.

O IPA exige iOS 16.3 ou posterior. APIs opcionais de encoder introduzidas no iOS 17.4 são protegidas por verificação de disponibilidade. As propriedades de compressão são aplicadas à sessão com `VTSessionSetProperties`, conforme o [fluxo da Apple](https://developer.apple.com/documentation/videotoolbox/vtcompressionsession-api-collection), em vez de serem passadas como atributos de pixel buffer.

O primeiro teste no iPhone físico revelou um abort do Metal ao criar o cubemap neutro: uma textura `MTLTextureTypeCube` precisa de `arrayLength = 1`, embora tenha seis faces. O cubemap foi corrigido. Os 64 shaders também passaram a ser compilados em `.metallib` no macOS e carregados como binários no aparelho. A compilação de referência dessas correções foi [GitHub Actions 35880812778](https://github.com/ueeruan/aurea/actions/runs/35880812778).

O teste seguinte mostrou o app em execução, mas com a interface invisível. O log do aparelho confirmou a inicialização do motor em 57,6 ms. A causa era a conversão de `Color(hex:)`: os tokens RGB de seis dígitos eram interpretados com alfa zero. Agora RGB é opaco e ARGB de oito dígitos preserva seu alfa explícito. A [compilação do IPA com essa correção](https://github.com/ueeruan/aurea/actions/runs/35881702190) passou no Xcode. O IPA foi instalado pelo Sideloadly no iPhone conectado, e o usuário confirmou que a Home abriu.

O usuário confirmou que a Home abriu, mas que o fluxo de novo projeto e o visual diferiam do Android. A Home iOS agora usa a hierarquia do Android (marca, saudação, botão principal, ações rápidas, projeto recente e grade). A folha de criação tem prévia de proporção, medida livre, nome, resolução, FPS e botão “Criar projeto”. O modelo só entra no editor após salvar o arquivo; um erro de gravação deixa a Home aberta e mostra aviso. “Importar mídia” cria primeiro a composição na proporção da mídia, como no Android. O [build dessa interface no Xcode](https://github.com/ueeruan/aurea/actions/runs/35884058559) passou.

O teste físico dessa versão comprovou a gravação do projeto (603 bytes no log), mas o primeiro frame do editor causou `SIGABRT` em `MTLBlitCommandEncoder sampleCountersInBuffer`. O backend admitia suporte a contadores em limites de stage/draw como substituto de blit, embora amostrasse sempre num encoder de blit. Agora ele só declara esse recurso quando o limite de blit é suportado; a medição opcional de GPU também foi desligada no app iOS. O [IPA corrigido](https://github.com/ueeruan/aurea/actions/runs/35884907048) passou no Xcode, foi baixado, verificado e instalado no iPhone. A reabertura física do projeto ainda está sendo conferida.

O IPA fica em `entrega/aurea-beta2-unsigned.ipa`, após download da compilação. A verificação cobre ZIP, Mach-O ARM64, ícones, bundle `com.aurea.aurea` e versão 2.0.0/build 2103. Os hashes dos instaladores estão em `entrega/aurea-beta2-SHA256SUMS.txt`. O app nativo iOS ainda tem menos painéis que o Android; a lista de telas pendentes está em `engine/platform/ios/README.md`.

Depois da confirmação de que a Home estava próxima do Android mas o editor parecia outro app, a revisão foi feita exclusivamente no novo projeto `Aureabeta`. O editor iOS tinha uma tela de adicionar camadas sem nenhum botão para abri-la; o seletor de mídia também era dispensado antes de aparecer. A barra superior e a doca agora abrem o seletor, com categorias Forma/Mídia/Áudio/Texto/Elemento/3D. A doca mostra ferramentas do tipo selecionado, com aparar e dividir. Aparar o início compensa o deslocamento interno da mídia, e os keyframes usam esse deslocamento na linha do tempo. O painel de texto 2D edita conteúdo, tamanho e fonte; o de texto 3D expõe fonte importada, animação das letras e metal/rugosidade.

O [IPA da revisão do editor](https://github.com/ueeruan/aurea/actions/runs/35887103881) passou no Xcode, foi baixado e verificado em `entrega/aurea-beta2-unsigned.ipa` (SHA-256 `876efb9803e7cc56f3f591b14e5a290255764b3e1d19e9aac3fe77a0ddd35a31`). O Sideloadly terminou a instalação no iPhone conectado. Falta a confirmação no aparelho de criar e editar um projeto nessa compilação. A interface iOS ainda não cobre todos os painéis do Android; não tratar o build bem-sucedido como prova de paridade visual ou funcional completa.

As APIs públicas da Apple importam planos YUV como texturas separadas ([documentação](https://developer.apple.com/documentation/arkit/displaying-an-ar-experience-with-metal)). O caminho atual usa BGRA externo para vídeo de 8 bits e planos P010 para conteúdo de 10 bits.
