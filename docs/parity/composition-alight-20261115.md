# Interface de composição

Atualização de 15/11/2026, usando Alight Motion 5.0.280 como referência de organização e navegação.

## Referência e evidências

A pasta fornecida foi `D:/Users/Ruan/Documents/AM REVERSE`. O APK correspondente está em `build/reference/alight-motion-5.0.280/reference.apk`, SHA-256 `4caeb6dea44e8f123b88833aef7b28dd54f4a48fb0cc6a9e5085ba4c1bbf4159`.

Os hashes de AndroidManifest.xml, resources.arsc, classes.dex e classes5.dex da pasta coincidem com os membros do arquivo. A comparação está em `build/reference/am-composition-20261115/target-identity.json`.

| Pergunta | Evidência | Resultado e limite |
|---|---|---|
| Qual é o aplicativo fornecido? | REA `ev_2bed0099dc1bfa19d01e8ac2ec769779ad3f1b114e8493aad0a6e2c4a3c10183`, `package.json` | Observado estaticamente. Pacote com.alightcreative.motion, versão5.0.280, build1028519. |
| Como a seleção apresenta ferramentas? | REA `ev_8d24ed8d98f4ca117c8c33b7c7af56d7dd89a645e04e2392bd01be5d5cee0bcc`, `layer-edit-class.json` | Classe WHy.s7w inspecionada novamente. Fluxo contextual confirmado estaticamente. |
| Como se organizam barras e painéis? | `build/reference/am-ui-20261014/layouts-resolved/`, `SPEC.md`, REA `ev_2859d033c3d06c9825f0be98fdcdd28760662ef5dd66023cdea0122ec8956e57` e `ev_55d6f0b74557dac8331e7675cc373e386a9241eb84aad563cb2f1e91fbae5035` | Recursos e classes do mesmo APK. Barra superior42dp, transporte46dp, área inferior280dp, painel da camada200dp, categorias contextuais e adição destacada. |
| Como funcionam seleção e modos da prévia? | REA `ev_19dbfa9fec106669f46d2e09dc6e71aa05422e240191f8c27563d2c4ff377dff`, `ev_64f14fd39783192b5749d38303fe3b74c43d07d4472afc82805c598f08be494c` e `ev_833384b3b0e166173e3176cdc7efdd318c140e7d0b0c9252af3c8ea98cbfc088` | Evidências estáticas anteriores reutilizadas, com identidade do alvo reconfirmada. |
| Aparência e fluidez exatas da referência em execução? | Sem captura de execução nesta análise | Permanecem sem comprovação. As constantes de animação lidas no aplicativo representam configuração. |

REA6.5.0 CLI e provider JADX0.7.1 foram usados nesta atualização. A integração MCP REA está ausente. O CLI existente permitiu a análise. O provider fornece observações estáticas e não verifica assinatura nem execução do APK. Layouts resolvidos por aapt2 da análise anterior complementam essas evidências.

## Implementação própria do Aurea

As decisões seguintes adaptam a referência ao conjunto de recursos do Aurea.

- A barra do projeto apresenta voltar, título editável, menu secundário, ajustes e exportação. O alvo de exportação mede 44×42.
- A barra da camada apresenta voltar, nome editável, vínculo, excluir e menu. 3D, abertura e alternância de desfoque de movimento e ações de plugins continuam acessíveis pelo menu.
- O menu secundário reúne assistente e um submenu de plugins, com os comandos e estados de disponibilidade existentes.
- A barra inferior, a adição de conteúdo e as ferramentas da camada usam o layout original do Aurea, conforme a correção do usuário durante a implementação.

Android/Kotlin e iOS/Swift implementam a mesma navegação. As regras de edição continuam nas APIs existentes do motor C++. A geometria da timeline e a propriedade da superfície nativa da prévia foram preservadas.

## Validação

Implementação, compilação e execução são registradas separadamente em `build/validation/composition-20261115/validation.json`.

Os testes existentes de controles secundários foram adaptados ao acesso pelo menu. O teste de copiar e colar usa a entrada atual do transporte. As jornadas de adição seguem a barra original.

Compilação Swift e execução em iPhone dependem de Xcode. As verificações estáticas disponíveis no Windows cobrem geometria e integração de fontes.
