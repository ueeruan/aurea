# Motion Capture do Aurea PC — BlazePose (MediaPipe) em ncnn — CONVERSÃO NOSSA

Só o Aurea PC usa estes arquivos (o celular detecta a pose com o ML Kit / Vision
do sistema). Android e iOS não copiam esta pasta: os avisos daqui vão para as
Licenças do PC pelo `desktop/build.gradle.kts` (`stageAppResources`).

## Origem

Google MediaPipe, licença **Apache-2.0** (texto em
`LICENSE-MediaPipe-BlazePose.txt`, aviso em `NOTICE-BlazePose.txt`; o cartão do
modelo diz "LICENSED UNDER Apache License, Version 2.0").

| Arquivo | URL | Bytes | SHA-256 |
| --- | --- | --- | --- |
| pose_detection.tflite | https://storage.googleapis.com/mediapipe-assets/pose_detection.tflite | 2.959.046 | 9ba9dd3d42efaaba86b4ff0122b06f29c4122e756b329d89dca1e297fd8f866c |
| pose_landmark_full.tflite | https://storage.googleapis.com/mediapipe-assets/pose_landmark_full.tflite | 6.440.512 | e9a5c5cb17f736fafd4c2ec1da3b3d331d6edbe8a0d32395855aeb2cdfd64b9f |
| pose_landmark_heavy.tflite (13/11) | https://storage.googleapis.com/mediapipe-assets/pose_landmark_heavy.tflite | 27.709.200 | 59e42d71bcd44cbdbabc419f0ff76686595fd265419566bd4009ef703ea8e1fe |

## Receita (`tools/mocap_pose_convert.py`)

Python 3.12, pacotes `tflite==2.18.0` e `numpy`. O grafo TFLite é lido direto e
cada operador vira a camada ncnn equivalente (sem pnnx/onnx). Pesos fp16 do
Google copiados sem mudança (etiqueta fp16 do ncnn); os pesos esparsos do
detector (DENSIFY) expandidos; só as saídas usadas ficam no grafo.

    python -I tools/mocap_pose_convert.py pose_detection.tflite pose_landmark_full.tflite pose_landmark_heavy.tflite engine/assets/mocap

O nome de saída segue o arquivo de origem; o heavy leva também o `heatmap`
(64×64×39) para o refino de x/y. O full sai idêntico ao de 12/11 (mesmos hashes).

| Saída | Bytes | SHA-256 |
| --- | --- | --- |
| pose_detection.param | 15.235 | cf8a940a3199367ae2f5ae898e1f67681690c6c5553c73b186bcfa54b690608a |
| pose_detection.bin | 5.975.036 | b062fe7b1463d28921a78adc10c6ffdb1df95d3bcdc1cd0806ba7bdadb4f3c22 |
| pose_landmark_full.param | 16.547 | 7fe36c3360e647d7df5d38c89c32de6a45c5c7f3f9ce0785b74bff7136b450b0 |
| pose_landmark_full.bin | 6.260.172 | 4ff2e1cab7f7e12a1e4c2bf738a425b6420db79945e38c21c7b5934149cfe3df |
| pose_landmark_heavy.param | 44.670 | 24634c59a3550ef994784750b271f9b06f9cd975e7e43320932962a56ac41e16 |
| pose_landmark_heavy.bin | 27.653.028 | 7e1e72abf6ea17176887aaab133dacde2b41b75fb6f8bbbb3bcf25764d292c93 |

Blobs: entrada `in0`. Pontos: `lm` [195 = 39 × (x, y, z, visibilidade,
presença), pixels do recorte 256], `flag` [1, já com sigmoide], `world` [117].
Detector (224², faixa −1..1): `cls8`/`reg8` (28², 2 âncoras), `cls16`/`reg16`
(14², 2), `cls32`/`reg32` (7², 6); 12 valores por âncora (caixa x, y, w, h e
4 pontos-chave).

## Conferência

Contra o intérprete TFLite do TensorFlow 2.21 (sem XNNPACK), mesma entrada,
erro máximo relativo ao maior valor: pontos 3e-5, mundo 4e-5, detector 7e-6.
Heavy (13/11, mesma conferência): pontos 2,1e-5, mundo 2,7e-6, heatmap 2,8e-5,
presença 0,99975 × 0,99975 na figura sintética.
No motor (`aurea_tests Pose`): figura desenhada pelo teste achada pelo detector
(0,965), presença 1,0, maior erro das juntas 3,2% da altura do quadro;
CPU × Vulkan 7,7e-6. Tempo por quadro rastreado neste PC (Ryzen 5 5500 +
RTX 3050): ~10 ms na CPU (4 threads), ~3,5–4 ms no Vulkan.

## No motor

`src/ai/PoseEstimator.cpp` (só Windows, `cmake/AureaNcnn.cmake`), lido em
tempo de execução de `engine-assets/mocap`. Fluxo do MediaPipe no modo vídeo:
detector → ROI girada (meio do quadril + ponto do tamanho do corpo, ×1,25) →
pontos; a ROI do quadro seguinte vem dos pontos auxiliares 33/34; o detector só
volta quando a presença cai abaixo de 0,5. A saída é a do ML Kit (x, y
normalizados, z na escala de x, confiança = presença) — o formato que o
MocapDetector.kt do Android manda ao motor.

## Qualidade (13/11, `aurea_pc_mocap_bench`, vídeos do dono)

O padrão do PC é a "Qualidade máxima": vídeo na resolução própria, recorte de
256 px pela pirâmide (sem serrilhado), heavy + refino pelo heatmap (célula =
coluna/64, a conta do MediaPipe; medido: +0,5 desloca os pontos 10 px), ROI
pelos auxiliares filtrados (One Euro 0,01/10), detector de novo na borda do
recorte e a cada 15 quadros (só para a MESMA pessoa), trocas de lado desfeitas
e os pontos de MUNDO (girados de volta pela ROI) no quadro companheiro
(`rig::kMocapWorldFlag`). O modo reduzido usa o full, 15 qps e 960 px.
Métricas e folhas de contato: `build/reference/mocap/eval/`.
