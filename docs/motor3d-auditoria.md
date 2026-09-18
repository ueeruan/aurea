# Auditoria do motor 3D — a bancada do pior caso

Gerado por `test/motor3d_auditoria_test.dart`. Cada linha é
um GLB fabricado no próprio teste e levado pelo caminho
real do aplicativo: importador → formato interno → ponte
GLB → (Filament).

Memória é a estimativa do próprio `ModelAsset3D`, que conta
o custo no heap do Dart — não a memória de GPU. FPS, memória
de GPU e iPhone 13 **não estão aqui**: precisam de aparelho.

| Asset | Arquivo | Abriu | Import | Ponte | Triângulos | Prims | Mats | Esq. | Anim. | Heap estimado | Textura no heap | GLB da ponte | Esq. na ponte | Anim. na ponte |
| --- | ---: | :--: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--: | :--: |
| Simples | 0.0 MB | sim | 12 ms | 27 ms | 8 | 1 | 1 | 0 | 0 | 0.0 MB | 0.0 MB | 0.0 MB | **não** | **não** |
| Médio (50k tri) | 1.4 MB | sim | 111 ms | 241 ms | 51200 | 1 | 1 | 0 | 0 | 2.8 MB | 0.0 MB | 1.1 MB | **não** | **não** |
| Complexo (60 materiais) | 0.1 MB | sim | 113 ms | 533 ms | 192000 | 60 | 60 | 0 | 0 | 10.6 MB | 0.0 MB | 4.2 MB | **não** | **não** |
| Muitos objetos (500) | 0.0 MB | sim | 19 ms | 232 ms | 64000 | 500 | 1 | 0 | 0 | 3.9 MB | 0.0 MB | 1.6 MB | **não** | **não** |
| Pesado (250k tri) | 6.9 MB | sim | 109 ms | 569 ms | 259200 | 1 | 1 | 0 | 0 | 13.9 MB | 0.0 MB | 6.9 MB | **não** | **não** |
| Personagem (skin) | 0.1 MB | sim | 60 ms | 19 ms | 3200 | 1 | 1 | 1 | 1 | 0.2 MB | 0.0 MB | 0.1 MB | **não** | **não** |
| Morph targets | 0.1 MB | sim | 7 ms | 28 ms | 1800 | 1 | 1 | 0 | 0 | 0.1 MB | 0.0 MB | 0.0 MB | **não** | **não** |
| Transparência | 0.1 MB | sim | 1 ms | 5 ms | 1800 | 1 | 1 | 0 | 0 | 0.1 MB | 0.0 MB | 0.0 MB | **não** | **não** |
| Textura 2048² | 4.0 MB | sim | 28 ms | 173 ms | 800 | 1 | 1 | 0 | 0 | 5.4 MB | 5.3 MB | 4.0 MB | **não** | **não** |
| Textura 4096² | 16.0 MB | sim | 118 ms | 465 ms | 800 | 1 | 1 | 0 | 0 | 21.4 MB | 21.3 MB | 16.0 MB | **não** | **não** |
| GLB com cauda | 0.0 MB | sim | 0 ms | 1 ms | 32 | 1 | 1 | 0 | 0 | 0.0 MB | 0.0 MB | 0.0 MB | **não** | **não** |
| Draco (comprimido) | 0.0 MB | **NÃO** | 2 ms | 0 ms | 0 | 0 | 0 | 0 | 0 | 0.0 MB | 0.0 MB | 0.0 MB | **não** | **não** |
| KTX2 / BasisU | 0.0 MB | **NÃO** | 0 ms | 0 ms | 0 | 0 | 0 | 0 | 0 | 0.0 MB | 0.0 MB | 0.0 MB | **não** | **não** |
| Meshopt | 0.0 MB | sim | 0 ms | 0 ms | 200 | 1 | 1 | 0 | 0 | 0.0 MB | 0.0 MB | 0.0 MB | **não** | **não** |

- **Draco (comprimido)** recusado: Extensao obrigatoria nao suportada: KHR_draco_mesh_compression. Exporte GLB sem compressao Draco (Meshopt funciona) e com texturas PNG/JPEG.
- **KTX2 / BasisU** recusado: Extensao obrigatoria nao suportada: KHR_texture_basisu. Exporte GLB sem compressao Draco (Meshopt funciona) e com texturas PNG/JPEG.

