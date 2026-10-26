# AureaHyper — terceiros (motor e caminho Turbo)

Lista do que o lado do motor do AureaHyper (0.3.2) usa de terceiros. Todo o
resto do Turbo — estimativa de movimento por blocos 16x16 com busca
hierárquica, interpolação bidirecional com oclusão, redução de ruído temporal
alinhada por blocos, desbloqueio 8x8, desentrelaçamento por direção de borda e
a detecção de entrelaçado, estabilização — é código próprio do Aurea. Nada do
FFmpeg (yadif, w3fdif, minterpolate, deshake...) nem de outro aplicativo entra
nestes passes.

As redes (Lite/Max, `engine/src/hyper/neural/**`, `engine/assets/hyper/**`)
têm a lista própria do agente das redes.

| Peça | Onde | Uso | Licença |
| --- | --- | --- | --- |
| AMD FidelityFX Super Resolution 1.0 (EASU + RCAS), `ffx_fsr1.h` | `engine/shaders/hyper/fsr_easu.frag`, `fsr_rcas.frag` | algoritmo portado e reescrito em GLSL 450 (Hyper Upscale General, Turbo) | MIT |
| Anime4K 0.9 ("push" de linhas e de gradiente), bloc97 | `engine/shaders/hyper/a4k_*.frag` | a ideia do algoritmo, reimplementada (Hyper Upscale Anime, Turbo) | MIT |
| Real-ESRGAN animevideov3 (já no app desde a 0.1.4, `ai/Upscaler`) | `engine/src/hyper/HyperRegistry.cpp` (etapa embutida) | pesos e rede existentes, reaproveitados como Hyper Upscale Anime Lite/Max | BSD-3-Clause (registrada com o upscaler da 0.1.4) |

---

## AMD FidelityFX Super Resolution 1.0

Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Anime4K

Copyright (c) 2019-2021 bloc97

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
