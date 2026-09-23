"""Liga o Aurea Particular no renderizador: 7 -> 20 blocos e a contagem com aux.

Fica como arquivo (e nao heredoc) porque o texto tem chaves e crases.
"""
import io

HPP = "engine/include/aurea/render/Renderer.hpp"
CPP = "engine/src/render/Renderer.cpp"

OLD_DECL = """    // Partículas: o bloco de parâmetros do shader (7 vec4), nº de slots, blend.
    Vec4     particleBlock[7]{};
    u32      particleSlots = 0;
    bool     particleAdditive = true;"""

NEW_DECL = """    // Partículas (Aurea Particular): o bloco de parâmetros do shader, nº de
    // instâncias e blend. O bloco cresceu de 7 para 20 vec4 quando o sistema
    // deixou de ser três presets fixos e virou sistema parametrizável —
    // emissor, física, rastro, aux e colisão precisam caber.
    static constexpr u32 kParticleBlocks = 20;
    Vec4     particleBlock[kParticleBlocks]{};
    u32      particleSlots = 0;      ///< primárias (o aux multiplica por 1+n)
    bool     particleAdditive = true;"""

OLD_FILL = """                rl.source.particleBlock[0] = Vec4{rate, life, pd.speed, pd.spread * kDeg2Rad};
                // Gravidade do modelo: y para CIMA (−980 = cai); a tela tem y para baixo.
                rl.source.particleBlock[1] = Vec4{pd.gravity.x, -pd.gravity.y, pd.startSize, pd.endSize};
                rl.source.particleBlock[2] = Vec4{pd.startOpacity, pd.endOpacity, pd.direction * kDeg2Rad, static_cast<f32>(pd.seed % 1000003u)};
                rl.source.particleBlock[3] = Vec4{pd.emitterSize.x, pd.emitterSize.y, tsec, static_cast<f32>(slots)};
                rl.source.particleBlock[4] = lin(pd.startColor);
                rl.source.particleBlock[5] = lin(pd.endColor);
                rl.source.particleBlock[6] = Vec4{lw * 0.5f + pd.emitterOffset.x, lh * 0.5f + pd.emitterOffset.y, 0, 0};
                rl.source.particleSlots = slots;
                rl.source.particleAdditive = pd.blendMode == 1;"""

NEW_FILL = """                // Bloco 0..6: o que ja existia (taxa, vida, forcas, cor, origem).
                rl.source.particleBlock[0] = Vec4{rate, life, pd.speed, pd.spread * kDeg2Rad};
                // Gravidade do modelo: y para CIMA (−980 = cai); a tela tem y para baixo.
                rl.source.particleBlock[1] = Vec4{pd.gravity.x, -pd.gravity.y, pd.startSize, pd.endSize};
                rl.source.particleBlock[2] = Vec4{pd.startOpacity, pd.endOpacity, pd.direction * kDeg2Rad, static_cast<f32>(pd.seed % 1000003u)};
                rl.source.particleBlock[3] = Vec4{pd.emitterSize.x, pd.emitterSize.y, tsec, static_cast<f32>(slots)};
                rl.source.particleBlock[4] = lin(pd.startColor);
                rl.source.particleBlock[5] = lin(pd.endColor);
                rl.source.particleBlock[6] = Vec4{lw * 0.5f + pd.emitterOffset.x, lh * 0.5f + pd.emitterOffset.y, 0, 0};
                // Bloco 8..: Aurea Particular.
                rl.source.particleBlock[7] = Vec4{static_cast<f32>(pd.emitterType), pd.emitterRadius,
                                                  pd.emitterRotation * kDeg2Rad, pd.emitterDepth};
                rl.source.particleBlock[8] = Vec4{static_cast<f32>(pd.gridX), static_cast<f32>(pd.gridY),
                                                  pd.emitFill ? 1.0f : 0.0f, static_cast<f32>(pd.burst)};
                rl.source.particleBlock[9] = Vec4{pd.lifeRandom, pd.speedRandom, pd.inheritVelocity,
                                                  static_cast<f32>(pd.particleType)};
                rl.source.particleBlock[10] = Vec4{pd.softness, pd.rotation * kDeg2Rad,
                                                   pd.rotationRandom * kDeg2Rad, pd.spin * kDeg2Rad};
                rl.source.particleBlock[11] = Vec4{pd.drag, pd.wind.x, -pd.wind.y, pd.turbulence};
                rl.source.particleBlock[12] = Vec4{pd.turbulenceScale, pd.turbulenceSpeed,
                                                   pd.vortex * kDeg2Rad, pd.attractor};
                rl.source.particleBlock[13] = Vec4{pd.trailLength, pd.trailTaper,
                                                   static_cast<f32>(pd.auxCount), pd.auxAt};
                rl.source.particleBlock[14] = Vec4{pd.auxLife, pd.auxSpeed, pd.auxSize, pd.auxSpread * kDeg2Rad};
                rl.source.particleBlock[15] = Vec4{static_cast<f32>(pd.collision), pd.collisionY,
                                                   pd.collisionBounce, 0.0f};
                rl.source.particleBlock[16] = lin(pd.auxColor);
                // "Velocity from motion": a velocidade da CAMADA no instante,
                // em px/s — o shader soma uma fracao dela a velocidade inicial.
                {
                    const Vec3 lp = l->transform.position.evaluate(local, comp);
                    const f32 dt = static_cast<f32>(1.0 / std::max(1.0, fps));
                    const Vec3 lp1 = l->transform.position.evaluate(FrameIndex{local.value + 1}, comp);
                    rl.source.particleBlock[17] = Vec4{(lp1.x - lp.x) / dt, -(lp1.y - lp.y) / dt, 0, 0};
                }
                rl.source.particleBlock[18] = Vec4{lw / std::max(1.0f, lh), 0, 0, 0};
                rl.source.particleBlock[19] = Vec4{0, 0, 0, 0};
                // O estouro entra alem das slots do fluxo continuo: com taxa 0
                // (so estouro) `slots` seria 1 e o burst nao teria onde caber.
                rl.source.particleSlots = std::min<u32>(cap, slots + pd.burst);
                rl.source.particleAdditive = pd.blendMode == 1;"""

OLD_SLOTS = """                const u32 slots = std::min<u32>(cap,
                                                static_cast<u32>(std::ceil(rate * life * 1.25f)) + 1u);"""

NEW_SLOTS = """                // Uma primaria vive `life`; com o estouro, `burst` nascem no
                // instante zero. O aux multiplica as instancias (cada primaria
                // gera `auxCount` secundarias), e o teto de particulas manda.
                const u32 auxMul = 1u + std::min<u32>(pd.auxCount, 16u);
                const u32 slots = std::min<u32>(cap / auxMul,
                                                static_cast<u32>(std::ceil(rate * life * 1.25f)) + pd.burst + 1u);"""


def main():
    for path, pairs in ((HPP, [(OLD_DECL, NEW_DECL)]), (CPP, [(OLD_SLOTS, NEW_SLOTS), (OLD_FILL, NEW_FILL)])):
        src = io.open(path, encoding="utf-8").read()
        for old, new in pairs:
            if old not in src:
                # Tolerar CRLF do checkout.
                old_crlf = old.replace("\n", "\r\n")
                if old_crlf in src:
                    src = src.replace(old_crlf, new.replace("\n", "\r\n"), 1)
                    continue
                raise SystemExit(f"trecho nao encontrado em {path}")
            src = src.replace(old, new, 1)
        io.open(path, "w", encoding="utf-8", newline="\n").write(src)
    print("renderer ligado ao Particular")


if __name__ == "__main__":
    main()
