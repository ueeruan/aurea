"""Resolve o particles.vert do merge B+C (uma vez so).

B (emissores estendidos, malha, curvas, colisao esfera/caixa) isolou o caminho
da particula em funcoes — `emit_start`, `particle_path`, `collide_plane`,
`collide_volume`, `emit_vertex`. C (cena 3D, espaco mundo, historico, desfoque
por subamostra) isolou o dele em `ps_*` — `ps_emit`, `ps_accept`, `ps_cone`,
`ps_depth`, `ps_gravity`, `ps_place`, `ps_place_streak`.

Os dois se encaixam sem sobreposicao: C troca as ENTRADAS (emissao do instante
do nascimento, cone de varios eixos, Z) e a SAIDA (ps_place/ps_place_streak,
que no 2D sem historico cai na MESMA conta de antes), e B traz o emissor
estendido e a colisao de volume. O bloco de fisica do C e um duplicado do
`particle_path` do B — fica o do B, que e o que a colisao tambem usa.

Trabalha por FAIXA DE LINHA (o arquivo em conflito esta no disco agora): trocar
por texto casado quebraria com os acentos do arquivo.

Uso: python tools/merge_particles_vert.py
"""
import io

P = "engine/shaders/particles/particles.vert"

# (inicio, fim, texto novo) — 1-based, inclusive.
BLOCOS = [
    # 1) biblioteca de funcoes: fica a dos DOIS lados
    (112, 280, '''#include "particles_extras.glsl"
FUNCOES_C
'''),
    # 2) emissao do nascimento + fluxo de aleatorio separado
    (552, 565, '''    // Emissão no instante do NASCIMENTO (keyframe depois não muda quem já
    // nasceu) e a aceitação da taxa animada.
    const PEmit em = ps_emit(birth);
    if (!isBurst && !ps_accept(em.rate, primaryIdx, k)) return;

    uint s = pcg(uint(primaryIdx) * 9781u ^ pcg(uint(k) * 6271u ^ uint(p.look.w)));
    // Fluxo SEPARADO para aparência e aux (8.2): ligar um aleatório não muda
    // a trajetória de ninguém — e projeto antigo continua igual.
    uint s2 = pcg(s ^ 0x68E31DA4u);
    const float auxRoll = rnd(s2);
'''),
    # 3) nascimento da secundaria
    (583, 596, '''        const float ang0 = em.dir + (rnd(s) - 0.5) * em.spread;
        const float spd0 = em.speed * (1.0 + (rnd(s) - 0.5) * 2.0 * p.emission.y);
        // O emissor estendido manda sobre a forma simples: e dele que saem os
        // pontos de nascimento (imagem, texto, forma, mascara, modelo).
        const vec3 st = emit_start(p.emitShape.x, s);
        const vec2 start = em.origin + st.xy;
        const vec2 cone0 = ps_cone(em.spread, primaryIdx, k);
        const vec2 v0 = spd0 * cone0.x * vec2(cos(ang0), sin(ang0)) + em.motion * p.emission.z;
        const vec2 acc = vec2(p.force.x, p.force.y) + p.physics.yz;
        const vec2 pAt = start + v0 * at + 0.5 * acc * at * at;
        const float zAt = ps_depth(st.z, spd0 * cone0.y, at, k);
'''),
    # 4) desenho da secundaria
    (614, 621, '''        if (!meshPass) v_shape = 0.0;   // secundária: disco macio
        // Gravidade da primária até o nascimento do aux + a do aux depois.
        const vec3 aGrav = ps_gravity(acc, at) + ps_gravity(acc, auxAge);
        emit_vertex(aPos, zAt + aGrav.z, aSize, 0.0, vec4(ac.rgb * aOp, aOp), c, s2, birth, aGrav);
'''),
    # 5) nascimento da primaria
    (628, 649, '''    const float ang = em.dir + (rnd(s) - 0.5) * em.spread;
    const float spd = em.speed * (1.0 + (rnd(s) - 0.5) * 2.0 * p.emission.y);
    const vec3 st = emit_start(p.emitShape.x, s);
    const vec2 cone = ps_cone(em.spread, primaryIdx, k);
    Particle P;
    P.start = em.origin + st.xy;
    P.z = st.z;
    P.ctr = em.origin;
    P.v0 = spd * cone.x * vec2(cos(ang), sin(ang)) + em.motion * p.emission.z;
    P.acc = vec2(p.force.x, p.force.y) + p.physics.yz;
    P.k = k;
'''),
    # 6) caminho do B + o Z e a gravidade do C
    (656, 728, '''        pos = particle_path(P, age);
        if (p.collide.x > 0.5 && p.collide.x < 1.5) pos = collide_plane(P, pos, age);
    }

    // Z (só no 3D) e a gravidade SEPARADA, para o espaço mundo agir nos eixos
    // do mundo — ver ps_place.
    const vec3 grav = ps_gravity(P.acc, age);
    const float posZ = ps_depth(P.z, spd * cone.y, age, k) + grav.z;
'''),
    # 7) rastro
    (760, 778, '''            const float wNow = max(size * 0.5 * mix(taper, 1.0, 0.35) * tl.x, 0.35);
            v_local = vec2(c.x * 2.0 - 1.0, along);
            v_color = vec4(rgb * a, a);
            // A cauda (along = −1) sai com a opacidade do rastro.
            v_misc = vec4(tl.y, 1.0, c);
            // 3D: a fita segue a velocidade com Z (velocidade + gravidade Z).
            const vec3 vel3 = vec3(vel, spd * cone.y + ps_accel_z() * age);
            ps_place_streak(vec3(pos, posZ), dir * (along * len) + perp * ((c.x * 2.0 - 1.0) * wNow),
                            vel3 * (along * len / vlen),
                            (c.x * 2.0 - 1.0) * wNow, birth, grav);
'''),
    # 8) vertice final
    (783, 792, '''    emit_vertex(pos, posZ, size, rot, vec4(rgb * a, a), c, s2, birth, grav);
'''),
]


def main():
    linhas = io.open(P, encoding="utf-8").read().splitlines()
    # o bloco 1 e o unico que precisa das duas versoes
    c1 = linhas[112:280]
    ini_c = next(i for i, l in enumerate(c1) if l.startswith("======="))
    bloco_c = [l for l in c1[ini_c + 1:] if not l.startswith(">>>>>>>")]
    for a, b, novo in sorted(BLOCOS, reverse=True):
        if "FUNCOES_C" in novo:
            novo = novo.replace("FUNCOES_C\n", "\n".join(bloco_c) + "\n")
        linhas[a - 1:b] = novo.rstrip("\n").splitlines()
    texto = "\n".join(linhas) + "\n"
    # assinatura do emit_vertex e a saida final dele
    texto = texto.replace(
        "void emit_vertex(vec2 pos, float z, float size, float rot, vec4 colPremul, vec2 c, inout uint s2) {",
        "void emit_vertex(vec2 pos, float z, float size, float rot, vec4 colPremul, vec2 c, inout uint s2,\n"
        "                 float birth, vec3 grav) {")
    texto = texto.replace(
        "    gl_Position = pc.clipFromLayer * vec4(pos + rl * size * 0.5, 0.0, 1.0);\n}",
        "    // Saída: no 2D sem histórico é a MESMA conta de antes (posição + canto\n"
        "    // girado); no 3D o billboard vai ao mundo pelos eixos da câmera da cena.\n"
        "    ps_place(vec3(pos, z), rl * size * 0.5, birth, grav);\n}")
    io.open(P, "w", encoding="utf-8").write(texto)
    print("conflitos restantes:", texto.count("<<<<<<<"),
          "| marcas C:", texto.count(">>>>>>>"))


if __name__ == "__main__":
    main()
