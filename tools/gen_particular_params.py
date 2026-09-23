"""Reescreve `set_particle_param` com a tabela inteira do Aurea Particular.

Indice estavel = contrato com o Kotlin: o numero que a UI manda nao pode mudar
de significado entre versoes, senao um projeto salvo (ou um preset) aplica o
valor no parametro errado. Por isso o `switch` e explicito e os indices estao
numerados aqui, e nao derivados da ordem do struct.
"""
import io

P = "engine/src/engine/Engine.cpp"

NEW = r'''bool Engine::set_particle_param(u64 layerId, u32 param, f32 v) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::ParticleSystem || param >= static_cast<u32>(ParticleParam::Count)) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "particulas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    ParticleData& p = l->particles;
    // O INDICE e contrato com a UI (ver ParticleParam em Layer.hpp): nao
    // reordene sem renumerar os dois lados. Cada faixa de clamp existe para um
    // valor absurdo nao virar NaN no shader — que nao desenha nada e nao diz
    // por que.
    switch (static_cast<ParticleParam>(param)) {
        // --- Emissor ---------------------------------------------------------
        case ParticleParam::EmitterType:
            p.emitterType = static_cast<u32>(std::clamp(v, 0.0f, static_cast<f32>(ParticleEmitter::Count) - 1.0f));
            break;
        case ParticleParam::EmitterWidth:  p.emitterSize.x = std::clamp(v, 0.0f, 20000.0f); break;
        case ParticleParam::EmitterHeight: p.emitterSize.y = std::clamp(v, 0.0f, 20000.0f); break;
        case ParticleParam::EmitterRadius: p.emitterRadius = std::clamp(v, 0.0f, 20000.0f); break;
        case ParticleParam::EmitterRotation: p.emitterRotation = v; break;
        case ParticleParam::EmitterDepth: p.emitterDepth = std::clamp(v, 0.0f, 20000.0f); break;
        case ParticleParam::GridX: p.gridX = static_cast<u32>(std::clamp(v, 1.0f, 64.0f)); break;
        case ParticleParam::GridY: p.gridY = static_cast<u32>(std::clamp(v, 1.0f, 64.0f)); break;
        case ParticleParam::EmitFill: p.emitFill = v >= 0.5f; break;
        case ParticleParam::EmitterOffsetX: p.emitterOffset.x = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::EmitterOffsetY: p.emitterOffset.y = std::clamp(v, -20000.0f, 20000.0f); break;

        // --- Emissao ---------------------------------------------------------
        case ParticleParam::Rate: p.rate = std::clamp(v, 0.0f, 1000000.0f); break;
        case ParticleParam::Burst: p.burst = static_cast<u32>(std::clamp(v, 0.0f, 1000000.0f)); break;
        case ParticleParam::Lifetime: p.lifetime = std::clamp(v, 0.05f, 120.0f); break;
        case ParticleParam::LifeRandom: p.lifeRandom = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::Speed: p.speed = std::clamp(v, 0.0f, 20000.0f); break;
        case ParticleParam::SpeedRandom: p.speedRandom = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::Direction: p.direction = v; break;
        case ParticleParam::Spread: p.spread = std::clamp(v, 0.0f, 360.0f); break;
        case ParticleParam::InheritVelocity: p.inheritVelocity = std::clamp(v, 0.0f, 4.0f); break;
        case ParticleParam::Seed: p.seed = static_cast<u32>(std::clamp(v, 0.0f, 1000000.0f)); break;

        // --- Particula -------------------------------------------------------
        case ParticleParam::ParticleType:
            p.particleType = static_cast<u32>(std::clamp(v, 0.0f, static_cast<f32>(ParticleShape::Count) - 1.0f));
            break;
        case ParticleParam::Softness: p.softness = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::Rotation: p.rotation = v; break;
        case ParticleParam::RotationRandom: p.rotationRandom = std::clamp(v, 0.0f, 360.0f); break;
        case ParticleParam::Spin: p.spin = std::clamp(v, -3600.0f, 3600.0f); break;

        // --- Ao longo da vida ------------------------------------------------
        case ParticleParam::StartSize: p.startSize = std::clamp(v, 0.0f, 4000.0f); break;
        case ParticleParam::EndSize: p.endSize = std::clamp(v, 0.0f, 4000.0f); break;
        case ParticleParam::StartOpacity: p.startOpacity = std::clamp(v, 0.0f, 1.0f); break;
        case ParticleParam::EndOpacity: p.endOpacity = std::clamp(v, 0.0f, 1.0f); break;

        // --- Fisica ----------------------------------------------------------
        case ParticleParam::GravityX: p.gravity.x = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::GravityY: p.gravity.y = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::GravityZ: p.gravity.z = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::Drag: p.drag = std::clamp(v, 0.0f, 20.0f); break;
        case ParticleParam::WindX: p.wind.x = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::WindY: p.wind.y = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::Turbulence: p.turbulence = std::clamp(v, 0.0f, 5000.0f); break;
        case ParticleParam::TurbulenceScale: p.turbulenceScale = std::clamp(v, 0.05f, 20.0f); break;
        case ParticleParam::TurbulenceSpeed: p.turbulenceSpeed = std::clamp(v, 0.0f, 20.0f); break;
        case ParticleParam::Vortex: p.vortex = std::clamp(v, -3600.0f, 3600.0f); break;
        case ParticleParam::Attractor: p.attractor = std::clamp(v, -200.0f, 200.0f); break;

        // --- Rastro ----------------------------------------------------------
        case ParticleParam::TrailLength: p.trailLength = std::clamp(v, 0.0f, 2.0f); break;
        case ParticleParam::TrailTaper: p.trailTaper = std::clamp(v, 0.0f, 1.0f); break;

        // --- Aux -------------------------------------------------------------
        case ParticleParam::AuxCount: p.auxCount = static_cast<u32>(std::clamp(v, 0.0f, 16.0f)); break;
        case ParticleParam::AuxAt: p.auxAt = std::clamp(v, 0.0f, 0.99f); break;
        case ParticleParam::AuxLife: p.auxLife = std::clamp(v, 0.05f, 20.0f); break;
        case ParticleParam::AuxSpeed: p.auxSpeed = std::clamp(v, 0.0f, 10000.0f); break;
        case ParticleParam::AuxSize: p.auxSize = std::clamp(v, 0.0f, 500.0f); break;
        case ParticleParam::AuxSpread: p.auxSpread = std::clamp(v, 0.0f, 360.0f); break;

        // --- Colisao ---------------------------------------------------------
        case ParticleParam::Collision:
            p.collision = static_cast<u32>(std::clamp(v, 0.0f, static_cast<f32>(ParticleCollision::Count) - 1.0f));
            break;
        case ParticleParam::CollisionY: p.collisionY = std::clamp(v, -20000.0f, 20000.0f); break;
        case ParticleParam::CollisionBounce: p.collisionBounce = std::clamp(v, 0.0f, 1.0f); break;

        // --- Render ----------------------------------------------------------
        case ParticleParam::BlendMode: p.blendMode = static_cast<u32>(std::clamp(v, 0.0f, 1.0f)); break;
        case ParticleParam::MaxParticles: p.maxParticles = static_cast<u32>(std::clamp(v, 1.0f, 1000000.0f)); break;
        default: return false;
    }
    project_->mark_dirty();
    request_render();
    return true;
}
'''


def main():
    src = io.open(P, encoding="utf-8").read()
    begin = src.index("bool Engine::set_particle_param(u64 layerId, u32 param, f32 v) noexcept {")
    end = src.index("\n}\n", src.index("        default: break;\n    }\n    project_->mark_dirty();", begin)) + 3
    src = src[:begin] + NEW + src[end:]
    io.open(P, "w", encoding="utf-8", newline="\n").write(src)
    print("set_particle_param reescrito")


if __name__ == "__main__":
    main()
