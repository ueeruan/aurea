"""Reescreve os presets de particula e o add_particles no Engine.cpp (Fase 8.2).

Fica como arquivo (e nao como heredoc no shell) porque o texto tem crases,
aspas e chaves que o shell come.
"""
import io
import os

P = "engine/src/engine/Engine.cpp"

PRESETS = r'''namespace {

/// Os presets do AUREA PARTICULAR.
///
/// Os tres primeiros sao os nomes que o app tinha como SISTEMAS SEPARADOS
/// (Faiscas, Neve, Poeira de luz): viraram preset do mesmo sistema, que e o que
/// eles sempre foram por dentro. Os outros sete sao do sistema novo.
///
/// Um preset e so um ponto de partida — todos os parametros continuam
/// editaveis depois, e nenhum deles cria um motor proprio.
void particle_preset(ParticleData& p, u32 preset, f32 w, f32 h) noexcept {
    p = ParticleData{};
    switch (preset) {
        case 1:   // Neve: cai devagar, de toda a largura do topo
            p.rate = 40; p.lifetime = 9; p.speed = 70; p.spread = 25; p.gravity = Vec3{0, 0, 0};
            p.startSize = 9; p.endSize = 9; p.startOpacity = 0.9f; p.endOpacity = 0.5f;
            p.startColor = Vec4{1, 1, 1, 1}; p.endColor = Vec4{0.85f, 0.92f, 1, 1};
            p.direction = 90; p.blendMode = 0;
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{w, 10}; p.emitterOffset = Vec2{0, -h * 0.5f - 10};
            p.wind = Vec3{18, 0, 0};   // deriva lateral: neve nao cai reta
            p.particleType = static_cast<u32>(ParticleShape::Soft);
            break;
        case 2:   // Poeira de luz: sobe devagar, grande e suave
            p.rate = 12; p.lifetime = 6; p.speed = 25; p.spread = 360; p.gravity = Vec3{0, 0, 0};
            p.startSize = 26; p.endSize = 44; p.startOpacity = 0.55f; p.endOpacity = 0;
            p.startColor = Vec4{1, 0.92f, 0.75f, 1}; p.endColor = Vec4{1, 0.8f, 0.55f, 1};
            p.direction = -90; p.blendMode = 1;
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{w, h};
            p.turbulence = 12; p.turbulenceScale = 0.6f;
            p.particleType = static_cast<u32>(ParticleShape::Soft);
            p.softness = 0.8f;
            break;
        case 3:   // Chuva: rapida, fina, com rastro
            p.rate = 420; p.lifetime = 1.1f; p.speed = 1400; p.spread = 3; p.direction = 92;
            p.gravity = Vec3{0, -600, 0}; p.startSize = 2; p.endSize = 2;
            p.startOpacity = 0.75f; p.endOpacity = 0.25f;
            p.startColor = Vec4{0.72f, 0.82f, 1, 1}; p.endColor = Vec4{0.6f, 0.75f, 1, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{w, 8}; p.emitterOffset = Vec2{0, -h * 0.5f - 8};
            p.particleType = static_cast<u32>(ParticleShape::Streak);
            p.trailLength = 0.045f; p.maxParticles = 24000;
            break;
        case 4:   // Vaga-lumes: poucos, lentos, piscando pela vida
            p.rate = 9; p.lifetime = 7; p.speed = 34; p.spread = 360; p.gravity = Vec3{0, 0, 0};
            p.startSize = 7; p.endSize = 3; p.startOpacity = 0; p.endOpacity = 0;
            p.startColor = Vec4{1, 0.95f, 0.45f, 1}; p.endColor = Vec4{0.75f, 1, 0.4f, 1};
            p.direction = -90; p.blendMode = 1;
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{w, h};
            p.turbulence = 26; p.turbulenceScale = 0.45f;
            p.attractor = -4;   // afasta devagar do centro
            p.particleType = static_cast<u32>(ParticleShape::Soft);
            p.softness = 0.9f;
            break;
        case 5:   // Brasas: sobem, esfriam e apagam
            p.rate = 90; p.lifetime = 2.6f; p.speed = 210; p.spread = 40; p.direction = -92;
            p.gravity = Vec3{0, -60, 0}; p.startSize = 8; p.endSize = 2;
            p.startOpacity = 1; p.endOpacity = 0;
            p.startColor = Vec4{1, 0.78f, 0.30f, 1}; p.endColor = Vec4{0.85f, 0.18f, 0.05f, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Sphere);
            p.emitterRadius = 26; p.emitterOffset = Vec2{0, h * 0.30f};
            p.turbulence = 40; p.turbulenceScale = 1.4f;
            p.rotationRandom = 180; p.spin = 90;
            p.auxCount = 2; p.auxAt = 0.45f; p.auxLife = 0.55f; p.auxSpeed = 130;
            p.auxSize = 3; p.auxSpread = 200; p.auxColor = Vec4{1, 0.55f, 0.15f, 1};
            break;
        case 6:   // Confete: estoura e cai girando, quicando no chao
            p.rate = 0; p.burst = 220; p.lifetime = 3.4f; p.speed = 620; p.spread = 360;
            p.direction = -90; p.gravity = Vec3{0, -900, 0}; p.drag = 0.9f;
            p.startSize = 13; p.endSize = 13; p.startOpacity = 1; p.endOpacity = 0.9f;
            p.startColor = Vec4{1, 0.30f, 0.45f, 1}; p.endColor = Vec4{0.35f, 0.75f, 1, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Point);
            p.emitterOffset = Vec2{0, -h * 0.12f};
            p.particleType = static_cast<u32>(ParticleShape::Square);
            p.rotationRandom = 180; p.spin = 420;
            p.collision = static_cast<u32>(ParticleCollision::Plane);
            p.collisionY = h * 0.45f; p.collisionBounce = 0.25f;
            break;
        case 7:   // Campo de estrelas: pontos distantes, quase parados
            p.rate = 60; p.lifetime = 13; p.speed = 4; p.spread = 360; p.gravity = Vec3{0, 0, 0};
            p.startSize = 3; p.endSize = 2; p.startOpacity = 0; p.endOpacity = 0;
            p.startColor = Vec4{0.85f, 0.9f, 1, 1}; p.endColor = Vec4{1, 1, 1, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{w, h};
            p.particleType = static_cast<u32>(ParticleShape::Soft);
            p.softness = 0.7f;
            break;
        case 8:   // Poeira magica: espiral fechada subindo
            p.rate = 70; p.lifetime = 4.2f; p.speed = 120; p.spread = 20; p.direction = -90;
            p.gravity = Vec3{0, 0, 0}; p.drag = 0.5f;
            p.startSize = 10; p.endSize = 1; p.startOpacity = 0.95f; p.endOpacity = 0;
            p.startColor = Vec4{0.75f, 0.55f, 1, 1}; p.endColor = Vec4{0.35f, 0.85f, 1, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Disc);
            p.emitterRadius = 60;
            p.vortex = 220; p.attractor = 6;
            p.turbulence = 20; p.turbulenceScale = 1.1f;
            p.auxCount = 3; p.auxAt = 0.3f; p.auxLife = 0.7f; p.auxSpeed = 60;
            p.auxSize = 4; p.auxSpread = 360; p.auxColor = Vec4{0.8f, 0.6f, 1, 1};
            break;
        case 9:   // Explosao de logo: estoura para fora segurando o rastro
            p.rate = 0; p.burst = 320; p.lifetime = 1.8f; p.speed = 780; p.spread = 360;
            p.direction = -90; p.gravity = Vec3{0, 0, 0}; p.drag = 1.6f;
            p.startSize = 9; p.endSize = 1; p.startOpacity = 1; p.endOpacity = 0;
            p.startColor = Vec4{1, 0.85f, 0.4f, 1}; p.endColor = Vec4{1, 0.25f, 0.1f, 1};
            p.emitterType = static_cast<u32>(ParticleEmitter::Point);
            p.particleType = static_cast<u32>(ParticleShape::Streak);
            p.trailLength = 0.09f; p.trailTaper = 1;
            p.auxCount = 2; p.auxAt = 0.5f; p.auxLife = 0.4f; p.auxSpeed = 200;
            p.auxSize = 3; p.auxSpread = 360; p.auxColor = Vec4{1, 0.6f, 0.2f, 1};
            break;
        default:  // 0 — Faiscas: jato para cima com gravidade
            p.rate = 120; p.lifetime = 1.3f; p.speed = 480; p.spread = 50; p.gravity = Vec3{0, -900, 0};
            p.startSize = 10; p.endSize = 2; p.startOpacity = 1; p.endOpacity = 0;
            p.direction = -90; p.blendMode = 1;
            p.emitterType = static_cast<u32>(ParticleEmitter::Box);
            p.emitterSize = Vec2{24, 24};
            p.particleType = static_cast<u32>(ParticleShape::Streak);
            p.trailLength = 0.06f;
            p.turbulence = 30; p.turbulenceScale = 1.2f;
            break;
    }
}

} // namespace

'''


def main():
    src = io.open(P, encoding="utf-8").read()
    begin = src.index("namespace {\nvoid particle_preset(ParticleData& p, u32 preset, f32 w, f32 h) noexcept {")
    end = src.index("Result<u64> Engine::add_particles(u32 preset) noexcept {")
    src = src[:begin] + PRESETS + src[end:]

    old = (
        '    history_.before_mutation(*comp, project_->timeline().current(), "adicionar particulas");\n'
        '    modelRevision_.fetch_add(1, std::memory_order_acq_rel);\n'
        '    const char* names[] = {"Faíscas", "Neve", "Poeira de luz"};\n'
        '    const LayerId lid = comp->add_layer(LayerKind::ParticleSystem, names[std::min<u32>(preset, 2u)]);'
    )
    new = (
        '    history_.before_mutation(*comp, project_->timeline().current(), "adicionar particulas");\n'
        '    modelRevision_.fetch_add(1, std::memory_order_acq_rel);\n'
        '    // UM sistema, UM nome. Os tres nomes antigos viraram preset: criar uma\n'
        '    // camada chamada "Neve" sugeria um motor de neve, e nao existe motor de\n'
        '    // neve — e o Particular com os parametros da neve.\n'
        '    const u32 which = std::min<u32>(preset, 9u);\n'
        '    const LayerId lid = comp->add_layer(LayerKind::ParticleSystem, "Aurea Particular");'
    )
    assert old in src, "trecho do add_layer nao encontrado"
    src = src.replace(old, new, 1)
    io.open(P, "w", encoding="utf-8", newline="\n").write(src)
    print("presets e add_particles reescritos")


if __name__ == "__main__":
    main()
