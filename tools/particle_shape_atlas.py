"""Bake the particle picker atlas with the app's fragment shader.

Run with Python, ModernGL, and Pillow. The output is shared by Android and iOS.
"""
import io
import json
from pathlib import Path
import re

import moderngl
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHADER = ROOT / "engine/shaders/effects/particular.frag"
ANDROID = ROOT / "android/app/src/main/res/drawable-nodpi/particle_shape_atlas.png"
IOS = ROOT / "engine/platform/ios/app/Assets.xcassets/ParticleShapeAtlas.imageset"
CELL = 80
COLUMNS = 7
COUNT = 35

VERTEX = """#version 330
uniform vec4 style;
out vec2 v_corner;
out vec4 v_color;
flat out float v_feather;
flat out vec4 v_shape;
void main() {
    const vec2 verts[6] = vec2[6](vec2(-1,-1),vec2(1,-1),vec2(-1,1),
                                 vec2(-1,1),vec2(1,-1),vec2(1,1));
    gl_Position = vec4(verts[gl_VertexID] * style.zw, 0, 1);
    v_corner = vec2(verts[gl_VertexID].x, -verts[gl_VertexID].y);
    v_color = vec4(0.29, 0.58, 0.98, 1);
    v_feather = style.y;
    v_shape = vec4(style.x, 0.42, 1, 0.27);
}
"""


def fragment_source():
    shader = SHADER.read_text(encoding="utf-8").replace("#version 450", "#version 330")
    shader = re.sub(r'^#include .*$', '', shader, flags=re.M)
    shader = re.sub(r'layout\(set = 0, binding = AUREA_TEX0\) ', '', shader)
    shader = re.sub(r'layout\(location = \d\) (?=(?:flat )?in )', '', shader)
    shader = re.sub(r'^layout\(location = 1\) out vec4 o_scene;.*$', '', shader, flags=re.M)
    shader = re.sub(r'^\s*o_scene = .*?;\n', '\n', shader, flags=re.M)
    # GLSL 330 requires constants to use compile-time initializers.
    shader = re.sub(r'\bconst ', '', shader)
    shader = shader.replace("void main()", "void render_shape()")
    return shader + """
void main() {
    render_shape();
    o_color.rgb = pow(clamp(o_color.rgb / max(o_color.a, 1e-6), 0.0, 1.0), vec3(1.0 / 2.2));
}
"""


def sample_texture():
    from math import cos, sin

    pixels = bytearray()
    for y in range(48):
        for x in range(48):
            wave = 0.5 + 0.5 * sin(y * 0.43 + cos(x * 0.27) * 1.6)
            pixels.extend((round(136 + wave * 119), round(194 + wave * 60), round(234 + wave * 21), 255))
    return bytes(pixels)


def bake():
    context = moderngl.create_standalone_context(require=330)
    width = CELL * COLUMNS
    height = CELL * ((COUNT + COLUMNS - 1) // COLUMNS)
    framebuffer = context.simple_framebuffer((width, height), components=4)
    framebuffer.use()
    framebuffer.clear(0, 0, 0, 0)
    program = context.program(vertex_shader=VERTEX, fragment_shader=fragment_source())
    mesh = context.vertex_array(program, [])
    sprite = context.texture((48, 48), 4, sample_texture())
    sprite.use(0)
    program["u_sprite"].value = 0
    for shape in range(COUNT):
        y = height - (shape // COLUMNS + 1) * CELL
        context.viewport = ((shape % COLUMNS) * CELL, y, CELL, CELL)
        motion = shape in (9, 14, 15, 33)
        program["style"].value = (shape, 0.60 if shape == 0 else 0.17, 0.61 if motion else 0.86, 0.86)
        mesh.render(mode=moderngl.TRIANGLES, vertices=6)
    atlas = Image.frombytes("RGBA", (width, height), framebuffer.read(components=4))
    return atlas.transpose(Image.Transpose.FLIP_TOP_BOTTOM)


def main():
    atlas = bake()
    encoded = io.BytesIO()
    atlas.save(encoded, format="PNG", optimize=True)
    data = encoded.getvalue()
    ANDROID.parent.mkdir(parents=True, exist_ok=True)
    IOS.mkdir(parents=True, exist_ok=True)
    ANDROID.write_bytes(data)
    (IOS / "particle_shape_atlas.png").write_bytes(data)
    manifest = {"images": [{"filename": "particle_shape_atlas.png", "idiom": "universal"}],
                "info": {"author": "xcode", "version": 1}}
    (IOS / "Contents.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"{COUNT} particle shapes, {len(data):,} bytes")


if __name__ == "__main__":
    main()
