# THIRD PARTY NOTICES AND LICENSES

This project incorporates and interfaces with open source components for optical flow, neural network inference, and graphics processing. Below are the licenses and notices for these technologies.

---

## 1. RIFE: Real-Time Intermediate Flow Estimation for Video Frame Interpolation
- **Project URL**: https://github.com/megvii-research/ECCV2022-RIFE
- **Authors**: Zhewei Huang, Tianyuan Zhang, Wen Heng, Boxin Shi, Shuchang Zhou (Megvii Research)
- **Code License**: MIT License
```text
MIT License

Copyright (c) 2020-2024 Megvii Research

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Model Weights Notice
- Pre-trained RIFE weights provided by original academic repositories are subject to their respective release terms (MIT for open checkpoints, or CC-BY-NC for research-only checkpoints).
- For commercial distribution, use models trained on authorized datasets or checkpoints verified under the MIT / Apache 2.0 license.

---

## 2. ncnn Neural Network Inference Framework
- **Project URL**: https://github.com/Tencent/ncnn
- **Developer**: Tencent
- **License**: BSD 3-Clause License
```text
Copyright (C) 2017 THL A29 Limited, a Tencent company. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## 3. Vulkan Headers and Loader
- **Project URL**: https://github.com/KhronosGroup/Vulkan-Headers
- **Developer**: The Khronos Group Inc.
- **License**: Apache License 2.0
```text
Copyright 2015-2024 The Khronos Group Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

---

## 4. rife-ncnn-vulkan Implementation Reference
- **Project URL**: https://github.com/nihui/rife-ncnn-vulkan
- **Author**: nihui
- **License**: MIT License
```text
Copyright (C) 2020-2024 nihui

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

# Avisos que faltavam (levantados em 17/09/2026)

Este arquivo cobria 4 dos ~14 componentes efetivamente embarcados. A
auditoria de 17/09 levantou os demais. **Este documento não é parecer
jurídico: REVISÃO JURÍDICA NECESSÁRIA antes de distribuir.**

## FFmpeg (via `ffmpeg_kit_flutter_new_full`)

- **Projeto:** https://ffmpeg.org
- **Licença: GNU LESSER GENERAL PUBLIC LICENSE, versão 3.**
- **Por que não é GPL:** a variante escolhida é a `full`, que exclui os
  codecs GPL (`x264`, `x265`, `xvidcore`, `vid.stab`). A escolha está
  correta e o comentário do `pubspec.yaml` confere com o README do pacote.
- **O que a LGPL-3 exige e NÃO está sendo cumprido:** o §4 pede que quem
  recebe o binário possa RELIGAR o aplicativo com uma versão modificada da
  biblioteca. No Android isso é possível em tese; **no iOS os frameworks
  entram estáticos dentro de um IPA assinado**, o que torna a religação
  impraticável. Também falta o próprio texto da licença acompanhando a
  distribuição.
- **Caminhos de correção, em ordem:** (1) escrever esta seção com o texto
  completo e a oferta de código-fonte; (2) no iOS, avaliar se o
  `AVFoundation` — que o app já usa em `ios/Runner/VideoEncoderPlugin.swift`
  — não cobre o que o FFmpeg faz lá, deixando o FFmpeg só no Android.

## whisper.cpp (via `whisper_flutter_new`, vendorizado em `packages/`)

- **Projeto:** https://github.com/ggerganov/whisper.cpp — **licença MIT**
- **O MODELO de voz** vem de https://huggingface.co/ggerganov/whisper.cpp —
  **licença MIT**, fixado no commit
  `5359861c739e955e79d9a303bcbc70fb988958b1`.
- **O PROBLEMA É O WRAPPER.** `packages/whisper_flutter_new/LICENSE` é
  **GNU GPL versão 3**, e não há nenhum outro arquivo de licença no pacote:
  o `whisper.cpp` vendorizado dentro dele não preserva o cabeçalho MIT. O
  pacote foi **modificado por este projeto** (o patch de checagem de nulo em
  `src/main.cpp`) e é ligado **estaticamente** no APK e no IPA.
- **Consequência:** um aplicativo fechado que incorpora e modifica código
  GPL-3 precisa liberar o conjunto sob GPL-3. Não é uma questão de crédito:
  é a licença do produto.
- **Correção:** como o `whisper.cpp` de origem é MIT, o caminho é trocar o
  wrapper por uma ligação própria compilada pelo *build hook* do Dart — o
  mesmo padrão que `packages/aurea_core` já usa. Não é feito nesta rodada
  porque exige reescrever a ponte e revalidar a transcrição no aparelho.

## Avisos de atribuição que também faltavam

Todos exigem que o texto da licença acompanhe o binário. Sugestão: uma tela
em **Ajustes › Sobre** (`lib/src/features/about/presentation/report_sheet.dart`)
com a íntegra — o arquivo do repositório não acompanha o aplicativo.

| Componente | Onde | Licença |
| --- | --- | --- |
| meshoptimizer | `packages/aurea_meshopt` | MIT (Arseny Kapoulkine) |
| flutter_scene | `pubspec.yaml` | MIT (Brandon DeRosier) |
| Real-ESRGAN e os 4 modelos | `assets/ai/` | BSD-3 |
| nlohmann/json | `packages/whisper_flutter_new/src/json/` | MIT |
| dr_wav | `packages/whisper_flutter_new/src/whisper.cpp/examples/` | domínio público |
| stb_image / stb_image_write | `native/enhance/tools/` | MIT / domínio público |
| photo_manager | `pubspec.yaml` | Apache-2.0 |
| gal | `pubspec.yaml` | Apache-2.0 |
| ncnn | baixado no configure do CMake | BSD-3 |
| Pacotes Dart restantes (archive, image, file_picker, vector_math, crypto, uuid, url_launcher, shared_preferences, path, riverpod, video_player, image_picker, characters, cupertino_icons, ffi, flutter_scene, whisper) | `pubspec.lock` | MIT/BSD/Apache-2.0 |
