# Modelos do AureaHyper (0.3.2) — caminhos Lite e Max

Receita única e reproduzível: `tools/hyper_models.py` (download só das URLs
oficiais com SHA-256 fixado, conversão ncnn, quantização int8, manifesto).
Cada pasta tem `manifest.json` (nome, versão, licença, URL de origem, SHA-256
da origem e de cada arquivo convertido, tamanho) e `LICENSE`.

Ferramentas (fora do repositório, em `build/hyper-tools/`): venv com torch
2.14.1+cpu, pnnx 20250430 e numpy; `ncnnoptimize`/`ncnn2table`/`ncnn2int8`
compilados da árvore ncnn FIXADA do motor (`305837fd…`, NCNN_INT8=ON, CPU).
Checkpoints abertos com `torch.load(weights_only=True)`; as arquiteturas são
escritas no próprio script (nenhum `.py` baixado é importado ou executado).

| Pasta | Rede | Licença | Uso | Bytes embutidos |
| --- | --- | --- | --- | --- |
| `span-x2`, `span-x4` | SPAN ch48 (NTIRE 2024), Conv3XC re-parametrizadas, cor corrigida | Apache-2.0 | Upscale General **Lite** (fp16; CPU com conta fp16 onde houver) | 0,84 MB + 0,91 MB |
| `realesr-general-x4v3` | realesr-general-x4v3 + wdn-x4v3 (v0.2.5.0) | BSD-3-Clause | Upscale General **Max**; Clean Lite/Max | 4,90 MB |
| `realesr-animevideov3` | animevideov3 ×4 int8 MISTO (o fp16 já está em `assets/ai`) | BSD-3-Clause | Upscale Anime **Lite** na CPU | 0,85 MB |
| `rife-v4.6` | RIFE v4.6 (rife-ncnn-vulkan 20221029, pesos fp16 oficiais) | MIT | Motion Lite/Max | 10,63 MB |
| `gfpgan-v1.4` | GFPGAN v1.4 (GFPGANv1Clean 512) | Apache-2.0 (+ notas) | Face Lite/Max | **sob demanda** (177,6 MB) |

Total embutido no `libaurea.so` de cada ABI: ~18,1 MB (os pesos quase não
comprimem no APK). O GFPGAN fica fora: só o manifesto e a licença estão aqui;
os arquivos saem em `build/hyper-tools/out/ondemand/gfpgan-v1.4/` e o motor os
procura em `set_model_directory(<pasta>)/gfpgan-v1.4/`, conferindo tamanho e
SHA-256 (`src/hyper/neural/OndemandHashes.inl`). Falta hospedar e a tela
baixar (fora do motor).

## Conversões

- **x4v3 / wdn-x4v3**: `torch.jit.trace` → pnnx (fp32) → `ncnnoptimize` fp32.
  Os dois grafos são idênticos (conferido); cada `.bin` fp32 é gravado como
  `.bin16` (cada palavra de 4 bytes em meia precisão — as marcas de tipo do
  ncnn valem 0 e continuam 0). O motor decodifica os dois e mistura
  `w·x4v3 + (1−w)·wdn` palavra a palavra, w = força do denoise: é a
  interpolação de pesos que o `inference_realesrgan.py -dn` do Real-ESRGAN
  documenta (`dni_weight = [dn, 1 − dn]`).
- **SPAN**: o `.zip` oficial (Google Drive do README dos autores; não há
  release no GitHub) — só `spanx2_ch48.pth` / `spanx4_ch48.pth`, chave
  `params`. Cada Conv3XC (1×1 → 3×3 → 1×1 + atalho 1×1) vira UMA 3×3 com a
  álgebra de `Conv3XC.update_params` (o que a avaliação oficial faz). Os
  `eval_conv` guardados no checkpoint estão DESATUALIZADOS (diferem dos ramos
  de treino) e não são usados. Conferido contra os ramos de treino: 1,7e-6
  (×2) e 2,7e-6 (×4) no miolo de uma imagem natural.
  **Correção de cor (desvio documentado)**: os dois checkpoints devolvem um
  campo plano v como a·v + b, b ≈ +0,03 (×2) e +0,04–0,07 (×4) por canal (a
  imagem sai 8–20 níveis mais clara). Contra as saídas que os próprios autores
  publicam no .zip (`SPANX2_CH48/visualization/Set5`, reduzidas e ampliadas de
  novo; só para validar, nada disso entra no app) a rede crua dá 30–32 dB e a
  corrigida 39–44 dB no ×2 (29–34 dB contra 23–26 no ×4). A correção é medida
  da própria rede (9 campos planos) e dobrada na última conv. A causa não foi
  achada (o "SRModelRAW" do treino não é público) — validar em vídeo real.
  fp16 = `ncnnoptimize 65536`. **int8: recusado** — em todas as conv a saída
  explode (a conv_2, sozinha, leva a média de 0,54 a 5,2: a atenção
  multiplicativa amplifica o erro); o misto que preserva 40 dB (5 de 22 conv)
  é só ~6% mais rápido no host. A CPU usa conta fp16 onde houver (ARMv8.2),
  conferida pelo medidor.
  Atenção: com ruído uniforme de faixa inteira o SPAN sai do regime de treino
  (saídas de ±200); em imagem natural é estável.
- **animevideov3 int8 misto**: do `.bin` embutido em `assets/ai` → fp32 →
  `ncnn2table method=kl` com 48 imagens SINTÉTICAS 96×96 geradas pelo script →
  `ncnn2int8` só nas 11 de 18 conv que, sozinhas, custam < 0,5 dB (medido
  camada a camada); rede inteira 35,5 dB do fp32 (todas em int8: 28,9 dB),
  ~25% mais rápida no host.
- **RIFE v4.6**: os `.param/.bin` oficiais sem mudança. A camada `rife.Warp`
  é implementada pelo Aurea (`src/hyper/neural/NeuralMotion.cpp`). O motor
  extrai o fluxo (blob `327`, 4 canais) e a máscara (`332`) e monta o quadro em
  resolução cheia.
- **GFPGAN v1.4**: a convolução modulada do StyleGAN2 (peso por amostra) foi
  reescrita na forma equivalente "não fundida" (`conv(x·s, W)·rsqrt(Σ s²·ΣW² + ε)`),
  com pesos constantes (a soma em escala 1/64: Σ s²·W² chega a 1,6e6 e
  estoura o fp16 do Vulkan; o ×√2 entra no vetor de demodulação, senão o pnnx
  gera um Eltwise com broadcast que o ncnn não faz), e o ruído = os buffers fixos do checkpoint
  (`randomize_noise=False`, determinístico). Conferido contra a forma
  original transcrita (conv agrupada com peso modulado): 1,5e-5. pnnx →
  `ncnnoptimize` fp16.

## Licenças — o que a tela de licenças do app precisa listar

Real-ESRGAN (BSD-3, já listado), SPAN (Apache-2.0), RIFE (MIT:
rife-ncnn-vulkan e Practical-RIFE), GFPGAN (Apache-2.0 com o bloco de
terceiros do LICENSE dele) e o gabarito de 5 pontos do FFHQ do facexlib (MIT)
usado no alinhamento.

Riscos a avaliar ANTES de vender (não são de código):
- **GFPGAN**: treinado no FFHQ, cujas imagens são CC BY-NC-SA 4.0 (NVIDIA). O
  LICENSE do GFPGAN cita ainda código NVIDIA (só nos kernels CUDA da versão
  não-clean, que não usamos) e DFDNet (CC BY-NC-SA, código não usado).
- **SPAN**: os pesos não têm licença própria; vêm do README do repositório
  Apache-2.0 (treino DIV2K/LSDIR, destilado do SRFormer).

## NPU (NNAPI / Core ML) — não implementado

O ncnn não tem backend NNAPI nem Core ML. Caminho viável, por etapas:
1. Exportar as mesmas redes (o script já tem as arquiteturas) para ONNX →
   TFLite (int8 por tensor, NNAPI delegate) e → Core ML (`coremltools`,
   mlprogram fp16, Neural Engine).
2. Android: TFLite + NNAPI delegate (~1,2 MB por ABI de runtime) ou o NNAPI C
   API direto (sem runtime extra, mas um grafo por resolução de tile). NNAPI
   é DEPRECIADO no Android 15 e os drivers variam muito; GPU delegate do
   TFLite é a alternativa. Tiles fixos (96+2·12) simplificam: um modelo por
   tamanho.
3. iOS: `.mlpackage` compilado no app (SPAN ×2 ≈ 0,8 MB fp16), chamado por
   ObjC++ na thread do grafo; o Neural Engine roda SPAN/x4v3 em poucos ms por
   tile, mas a primeira compilação leva segundos e o Swift/ObjC precisa de um
   ponto de entrada (dono: agente das telas).
4. Cada backend novo precisa da mesma conferência do medidor (tile de
   referência) e de testes em aparelho.
Custo estimado: dias de trabalho por plataforma + aparelhos; ficou fora desta
entrega para não lançar um caminho quebrado. `DeviceProfile::npu` continua só
informativo.
