# Efeitos solicitados do After Effects

Escopo: implementações nativas Android/iOS, sem distribuir os binários `.aex`.
O rastreamento 3D foi retirado deste pedido pelo usuário; preservar o existente.
Nenhum resultado deste documento afirma identidade visual ou compatibilidade de parâmetros com o AE.

| Efeito solicitado | Situação no Aurea |
|---|---|
| Glow | Glow nativo existente; equivalência com o AE não medida |
| Lens_Flare | Novo reflexo de lente procedural, uma passagem GPU |
| Posterize_Time | Posterizar tempo existente no motor temporal |
| ApplyColorLUT | Importação/aplicação de LUT 3D ainda pendente; curvas não substituem isso |
| Aud_Delay | Pendente no mixer compartilhado |
| Aud_Flange | Pendente no mixer compartilhado |
| Aud_Modulator | Pendente no mixer compartilhado |
| Aud_Reverb | Pendente no mixer compartilhado |
| Aud_Reverse | Reversão de áudio acompanha o reverso da camada existente; não é um efeito independente |
| Basic_3D | Transformações/cena 3D existentes; falta reproduzir o contrato específico do efeito |
| Blend | Modos de mesclagem de camada existentes; contrato específico do efeito ainda não implementado |
| Block_Dissolve | Nova dissolução determinística em blocos, tamanho e semente configuráveis |
| Box_Blur | Novo filtro separável de caixa; raio limitado a 64 px, bordas opcionais |
| Card Dance | Pendente; requer malha de cartões, fontes de controle e integração à câmera |
| Card Wipe | Pendente; requer transição da malha de cartões |
| DirectionalBlur | Novo desfoque direcional; raio até 64 px, ângulo e extensão das bordas |
| Displacement | Mapa externo de deslocamento pendente; turbulência procedural não o substitui |
| Drop_Shadow | Sombra de texto existente; sombra genérica de camada pendente |
| Linear_CK | Chroma key existente e ampliado; não implementa o algoritmo/contrato exato do Linear Color Key |
| Linear_Wipe | Nova varredura linear, ângulo, suavidade, direção e conclusão animáveis |
| Liquify | Pincel de deformação persistente pendente; efeito de lente existente não o substitui |
| Luma_Key | Chave de luma existente |
| Lumetri | Correções de cor individuais existentes; pacote Lumetri completo pendente |
| OpticsComp | Nova projeção radial com campo de visão e transformação inversa |
| Radial_Wipe | Nova varredura radial com centro, ângulo, suavidade e inversão |
| RadialShadow | Pendente |
| Ripple | Nova ondulação radial, centro, amplitude, fase, comprimento e atenuação |
| Shatter | Fratura, simulação e renderização ainda pendentes |
| Upscale | Upscale existente usa outro modelo/pipeline; não é o algoritmo Adobe |
| Wave_Warp | Onda nativa existente; equivalência com AE não medida |
| 3DGlasses | Composição estereoscópica/anaglifo ainda pendente |

Chroma key: preserva os quatro parâmetros anteriores e acrescenta corte preto/branco da máscara,
gamma das bordas, visualização de máscara/supressão e proteção de crominância do primeiro plano.
Permanece no passe de cor fundido. Esses controles não equivalem a reconstrução automática de cabelo.

Validação requerida: resultados GPU e compatibilidade de projetos antigos, geração de GLSL/Metal,
compilação das duas plataformas e cenas reais no Samsung/iPhone. Testes no PC não comprovam fluidez
em aparelhos. Comparações com AE precisam usar a mesma entrada, espaço de cor, parâmetros,
premultiplicação, bordas e instantes; não basta comparar nomes de efeitos.

Referência funcional pública: [Transições do After Effects](https://helpx.adobe.com/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/transition-effects.html).

## Verificação desta implementação

- Motor completo: 755 testes, 4.606.790 verificações, zero falhas.
- Os testes novos cobrem extremos das transições, reversão, repetibilidade,
  energia e direção dos desfoques, valores finitos, identidade e controles do chroma.
- Compilação nativa Android ARM64 e ARMv7 passou; compilação Kotlin também passou.
- Prévia dos oito efeitos novos renderizada pelo motor e inspeção visual de reflexo
  de lente, ondulação, compensação óptica e desfoque direcional.
- Logs: `build/effects-final-full-tests.log`, `build/effects-android-native-final.log`,
  `build/effects-android-32.log`, `build/effects-preview-audit.log`.
- Prévia: `output/effects-2113/`. Nenhum APK/IPA novo foi empacotado nesta validação.
- iOS/Metal, paridade com renders do AE e desempenho em aparelhos físicos continuam pendentes.
