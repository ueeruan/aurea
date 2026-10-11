// Beta 07–08/10: Tile, Squeeze, luz 3D, Smooth Motion e Roto Brush.

// "Tiles: só funciona dentro do quadrado do vídeo; fora fica fundo escuro".
// Um vídeo quadrado num quadro 16:9 com o Motion Tile padrão: o quadro todo é
// ladrilho (cópias periódicas do vídeo), nada do fundo preto aparece.
AUREA_TEST(Gpu, MotionTileOnASquareVideoCoversTheWholeFrame) {
    AUREA_REQUIRE_GPU();
    for (int moved = 0; moved < 2; ++moved) {
        Scene s(256, 144);
        SyntheticConfig cfg;
        cfg.width = cfg.height = 64;
        const f32 x = moved ? 90.0f : 128.0f, y = moved ? 60.0f : 72.0f;
        const LayerId id = s.video(cfg, x, y);
        s.add_effect(id, effect_keys::kMotionTile);
        const FloatImage img = s.render();
        u32 dark = 0, wrong = 0;
        const i32 left = static_cast<i32>(x) - 32, top = static_cast<i32>(y) - 32;
        for (u32 py = 2; py < 144; py += 6) {
            for (u32 px = 2; px < 256; px += 6) {
                const Vec4 v = img.v(px, py);
                if (v.w < 0.99f || (v.x + v.y + v.z) < 0.02f) ++dark;
                // O ponto equivalente dentro do vídeo (período = 64 px).
                const i32 lx = ((static_cast<i32>(px) - left) % 64 + 64) % 64;
                const i32 ly = ((static_cast<i32>(py) - top) % 64 + 64) % 64;
                if (lx < 3 || lx > 60 || ly < 3 || ly > 60) continue;   // longe da emenda
                if (!near4(v, img.v(static_cast<u32>(left + lx), static_cast<u32>(top + ly)), 0.03f)) ++wrong;
            }
        }
        std::printf("    square video tile (moved %d): %u dark, %u off-period samples\n", moved, dark, wrong);
        AUREA_CHECK_EQ(dark, 0u);
        AUREA_CHECK_EQ(wrong, 0u);
    }
}

// "Squeeze não parece squeeze": o novo aperta a cintura no eixo e estufa no
// outro, com os cantos parados; o salvo (sem o slot "algorithm") continua o
// esticar uniforme de antes.
AUREA_TEST(Gpu, SqueezePinchesTheWaistAndKeepsTheCorners) {
    AUREA_REQUIRE_GPU();
    const Vec4 white{1, 1, 1, 1}, black{0, 0, 0, 1};
    auto run = [&](int mode, f32 strength) {
        Scene s(256, 144);
        const LayerId id = s.solid(128, 96, white, 128, 72);   // caixa (64,24)-(192,120)
        auto& fx = s.add_effect(id, "aurea.distort.squeeze");
        fx.params[0].constant.v[0] = strength;
        if (mode == 0) fx.params.resize(3);                    // instância salva antes do slot
        return s.render();
    };
    {
        const FloatImage img = run(1, 25.0f);
        // Cintura: a borda esquerda no meio entra 25% do meio-tamanho (16 px).
        AUREA_CHECK(near4(img.v(70, 72), black, 0.02f));
        AUREA_CHECK(near4(img.v(84, 72), white, 0.02f));
        // Cantos parados.
        AUREA_CHECK(near4(img.v(66, 26), white, 0.02f));
        AUREA_CHECK(near4(img.v(190, 118), white, 0.02f));
        // O meio de cima estufa (12 px) para fora da caixa original.
        AUREA_CHECK(near4(img.v(128, 18), white, 0.02f));
        AUREA_CHECK(near4(img.v(128, 8), black, 0.02f));
        // Sem degrau: a cintura desce suave ao longo da borda.
        int prev = 0;
        bool monotonic = true;
        for (u32 y = 26; y <= 72; y += 4) {
            int edge = 0;
            for (u32 x = 60; x < 128; ++x)
                if (img.v(x, y).x > 0.5f) { edge = static_cast<int>(x); break; }
            if (edge < prev) monotonic = false;
            prev = edge;
        }
        AUREA_CHECK(monotonic);
        AUREA_CHECK(prev >= 78 && prev <= 82);
    }
    {
        // Negativo: o eixo troca (cintura em cima/baixo, laterais estufam).
        const FloatImage img = run(1, -25.0f);
        AUREA_CHECK(near4(img.v(128, 28), black, 0.02f));
        AUREA_CHECK(near4(img.v(56, 72), white, 0.02f));
        AUREA_CHECK(near4(img.v(66, 26), white, 0.02f));
    }
    {
        // Projeto antigo: esticar uniforme (fator e^0,5 em X), canto some.
        const FloatImage img = run(0, 25.0f);
        AUREA_CHECK(near4(img.v(66, 26), black, 0.02f));
        AUREA_CHECK(near4(img.v(80, 72), black, 0.02f));
        AUREA_CHECK(near4(img.v(100, 72), white, 0.02f));
        AUREA_CHECK(near4(img.v(100, 26), white, 0.02f));
    }
}
