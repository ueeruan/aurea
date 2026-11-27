"""Build the original, editable props and thumbnails bundled with Model Library."""
import json
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "engine/assets/model-library/ModelLibrary"
PALETTE = {
    "Graphite": ("262e3c", 0.25, 0.52),
    "Black glass": ("152840", 0.08, 0.22),
    "Brushed silver": ("adb7cb", 0.94, 0.28),
    "Off white": ("e9ecf6", 0.08, 0.45),
    "Rubber": ("171c28", 0.0, 0.89),
    "Blue light": ("54a5fd", 0.1, 0.26),
    "Mint": ("8bd8c5", 0.1, 0.48),
    "Sky glass": ("64aabf", 0.53, 0.26),
    "Concrete": ("adb2b7", 0.0, 0.92),
    "Terracotta": ("ae715a", 0.0, 0.65),
    "Screen blue": ("347aa1", 0.06, 0.29),
    "Screen violet": ("6d659e", 0.06, 0.4),
}


def rgb(material):
    return np.frombuffer(bytes.fromhex(PALETTE[material][0]), dtype=np.uint8) / 255


def linear(color):
    return np.where(color <= 0.04045, color / 12.92, ((color + 0.055) / 1.055) ** 2.4)


def rect_ring(width, height, radius, count=5):
    radius = min(radius, width * 0.499, height * 0.499)
    pts = []
    for quadrant in range(4):
        corner = math.pi * (quadrant + 0.5) / 2
        cx = math.copysign(width / 2 - radius, math.cos(corner))
        cy = math.copysign(height / 2 - radius, math.sin(corner))
        for section in range(count):
            angle = math.pi * (quadrant + section / (count - 1)) / 2
            pts.append((cx + radius * math.cos(angle), cy + radius * math.sin(angle)))
    return pts


def circle_ring(radius, count=28):
    return [(radius * math.cos(t), radius * math.sin(t))
            for t in np.arange(count) * 2 * math.pi / count]


def loft(rings):
    """Close a sequence of counterclockwise rings and connect the corresponding edges."""
    triangles = []
    count = len(rings[0])
    for near, far in zip(rings, rings[1:]):
        for i in range(count):
            j = (i + 1) % count
            triangles.extend(([near[i], near[j], far[i]], [near[j], far[j], far[i]]))
    for ring, side in ((rings[0], -1), (rings[-1], 1)):
        center = np.mean(ring, axis=0)
        for i in range(count):
            triangles.append([center, ring[i], ring[(i + side) % count]])
    return np.asarray(triangles)


def box(size, radius=0.02):
    w, h, d = size
    bevel = min(radius * 0.55, d * 0.26, w * 0.14, h * 0.14)
    return loft([
        [(x, y, z) for x, y in rect_ring(w - 2 * trim, h - 2 * trim, max(0.001, radius - trim))]
        for z, trim in ((-d / 2, bevel), (-d / 2 + bevel, 0), (d / 2 - bevel, 0), (d / 2, bevel))
    ])


def radial(profile, axis="y", count=28):
    triangles = loft([[(x, y, z) for x, y in circle_ring(radius, count)] for z, radius in profile])
    if axis == "y":
        return triangles[..., [0, 2, 1]] * (1, 1, -1)
    if axis == "x":
        return triangles[..., [2, 1, 0]] * (1, 1, -1)
    return triangles


def arc(radius, width, depth, low=0.0, high=math.pi):
    outer, inner = radius + width / 2, radius - width / 2
    angles = np.linspace(low, high, 38)
    points = [(r * math.cos(t), r * math.sin(t)) for r, sweep in ((outer, angles), (inner, reversed(angles)))
              for t in sweep]
    # The concave arc uses paired quads instead of a triangle fan at its center.
    n = len(angles)
    back = [(x, y, -depth / 2) for x, y in points]
    front = [(x, y, depth / 2) for x, y in points]
    border = loft([back, front])[:n * 4]
    faces = []
    for side, ring in ((-1, back), (1, front)):
        for i in range(n - 1):
            a, b, c, d = ring[i], ring[i + 1], ring[2 * n - 2 - i], ring[2 * n - 1 - i]
            faces.extend(([a, b, c], [a, c, d]) if side > 0 else ([a, c, b], [a, d, c]))
    return np.concatenate((border, np.asarray(faces)))


def rotate_z(triangles, t):
    s, c = math.sin(t), math.cos(t)
    return triangles @ np.array(((c, s, 0), (-s, c, 0), (0, 0, 1)))


class Model:
    def __init__(self, name):
        self.name, self.parts = name, []

    def add(self, name, material, triangles, position=(0, 0, 0)):
        triangles = np.asarray(triangles) + position
        cross = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
        lengths = np.linalg.norm(cross, axis=1)
        good = lengths > 1e-8
        self.parts.append((name, material, triangles[good], cross[good] / lengths[good, None]))

    def box(self, name, material, position, size, radius=0.02, angle=0.0):
        self.add(name, material, rotate_z(box(size, radius), angle), position)


def television():
    m = Model("Television")
    m.box("Back housing", "Graphite", (0, 1.32, -.08), (3.4, 1.95, .22), .10)
    m.box("Metal frame", "Brushed silver", (0, 1.32, .05), (3.36, 1.90, .11), .075)
    m.box("Display", "Black glass", (0, 1.34, .118), (3.22, 1.76, .045), .04)
    m.box("Screen light", "Screen blue", (0, 1.34, .141), (3.12, 1.67, .006), .02)
    slopes = [[(-1.55, .54, .145), (1.55, .54, .145), (.36, 1.58, .145)],
              [(1.55, .54, .145), (1.55, 1.35, .145), (.36, 1.58, .145)]]
    m.add("Screen artwork", "Screen violet", slopes)
    m.box("Power indicator", "Blue light", (0, .405, .119), (.085, .022, .018), .01)
    for direction, side in ((-1, "Left"), (1, "Right")):
        m.box(f"{side} stand support", "Graphite", (direction * 1.00, .26, -.05), (.11, .45, .15), .04, -direction * .29)
        m.box(f"{side} foot", "Brushed silver", (direction * 1.06, .048, .045), (.34, .09, .69), .035)
    return m


def phone():
    m = Model("Smartphone")
    m.box("Metal chassis", "Brushed silver", (0, 0, 0), (1.20, 2.42, .18), .18)
    m.box("Back cover", "Mint", (0, 0, -.095), (1.15, 2.37, .055), .155)
    m.box("Front glass", "Black glass", (0, 0, .098), (1.16, 2.37, .042), .147)
    m.box("Display", "Screen violet", (0, 0, .122), (1.05, 2.25, .008), .125)
    m.add("Screen artwork", "Screen blue", [[(-.455, -.48, .127), (.525, -.89, .127), (.525, .32, .127)],
                                             [(-.455, -.48, .127), (.525, .32, .127), (-.525, .76, .127)],
                                             [(-.525, -.89, .127), (-.455, -.48, .127), (-.525, .76, .127)]])
    m.box("Earpiece", "Rubber", (0, 1.02, .131), (.31, .075, .018), .035)
    m.box("Rear camera plate", "Graphite", (-.23, .78, -.125), (.51, .57, .07), .11)
    for n, (x, y) in enumerate(((-.34, .9), (-.15, .69)), 1):
        m.add(f"Rear lens {n}", "Black glass", radial([(-.08, .078), (-.065, .091), (-.01, .091)], axis="z"), (x, y, -.11))
    m.box("Power button", "Graphite", (.606, .45, 0), (.035, .27, .07), .014)
    m.box("Volume buttons", "Graphite", (-.606, .49, 0), (.04, .34, .07), .012)
    return m


def microphone():
    m = Model("Studio microphone")
    m.add("Weighted base", "Graphite", radial([(.01, .59), (.06, .64), (.13, .64), (.18, .55)]))
    m.add("Stand column", "Brushed silver", radial([(.14, .09), (.77, .09)]))
    m.add("Swivel mount", "Graphite", radial([(-.34, .09), (-.31, .13), (.31, .13), (.34, .09)], axis="x"), (0, .91, 0))
    m.add("Microphone body", "Graphite", radial([(.84, .13), (.89, .25), (1.57, .30), (1.66, .28)]))
    m.add("Hood", "Rubber", radial([(1.55, .28), (1.69, .33), (2.16, .33), (2.28, .28), (2.35, .15), (2.37, .035)]))
    grille = np.concatenate([radial([(y, .326), (y + .018, .339), (y + .035, .325)], count=32)
                             for y in np.linspace(1.72, 2.16, 9)])
    m.add("Metal grille", "Brushed silver", grille)
    m.add("Gain knob", "Brushed silver", radial([(0, .064), (.055, .072), (.08, .060)], axis="z"), (0, 1.2, .27))
    m.box("Status light", "Blue light", (0, 1.38, .296), (.075, .024, .014), .01)
    return m


def headphones():
    m = Model("Headphones")
    m.add("Spring headband", "Brushed silver", arc(.89, .14, .43, -.16, math.pi+.16), (0, 1.18, 0))
    m.add("Headband cushion", "Rubber", arc(.799, .12, .38, .20, math.pi-.20), (0, 1.18, 0))
    for direction, side in ((-1, "Left"), (1, "Right")):
        m.box(f"{side} slider", "Brushed silver", (direction*.88, .99, 0), (.13, .37, .19), .05, direction*.14)
        m.add(f"{side} cup", "Graphite", radial([(-.19, .28), (-.15, .37), (.13, .37), (.19, .28)], axis="x"),
              (direction*.87, .66, 0))
        m.add(f"{side} pad", "Rubber", radial([(-.073, .26), (-.052, .34), (.052, .34), (.073, .26)], axis="x"),
              (direction*.684, .66, 0))
        m.add(f"{side} badge", "Mint", radial([(-.012, .19), (.012, .19)], axis="x"),
              (direction*1.063, .66, 0))
    return m


def console():
    m = Model("White game console")
    m.box("Console stand", "Rubber", (0, .07, 0), (1.02, .14, 1.43), .20)
    m.box("Graphite chassis", "Graphite", (0, 1.31, 0), (.64, 2.50, 1.13), .18)
    for axis, name in ((-1, "Left panel"), (1, "Right panel")):
        rings = []
        for y, x, depth in ((.14, .32, .94), (.54, .335, 1.17), (1.9, .355, 1.28),
                            (2.44, .444, 1.35), (2.61, .477, 1.23), (2.72, .507, .97)):
            rings.append([(u + axis*x, v, y) for u, v in rect_ring(.106, depth, .045)])
        m.add(name, "Off white", loft(rings)[..., [0, 2, 1]] * (1, 1, -1))
    m.box("Optical drive housing", "Off white", (.30, .74, .442), (.26, .96, .37), .105)
    m.box("Disc slot", "Rubber", (.292, .76, .634), (.025, .55, .014), .01)
    m.box("Power light", "Blue light", (-.26, 2.03, .532), (.022, .80, .021), .009)
    vents = np.concatenate([box((.41, .024, .022), .008) + (0, .96 + .084*i, .569) for i in range(10)])
    m.add("Cooling vents", "Rubber", vents)
    m.box("USB port", "Brushed silver", (-.05, .67, .568), (.10, .07, .019), .012)
    return m


def mouse():
    m = Model("Wireless mouse")
    m.box("Base plate", "Rubber", (0, .103, -.05), (1.26, .20, 2.08), .42)
    rings = []
    for z, w, h, y in ((-1.09, .28, .32, .20), (-.94, .71, .54, .31), (-.51, 1.14, .65, .37),
                       (.05, 1.26, .57, .34), (.55, 1.19, .38, .245), (.94, .91, .29, .20), (1.03, .63, .24, .18)):
        rings.append([(x, v + y, z) for x, v in rect_ring(w, h, .19)])
    m.add("Curved palm shell", "Graphite", loft(rings))
    for direction, side in ((-1, "Left"), (1, "Right")):
        m.box(f"{side} button", "Brushed silver", (direction*.284, .436, .66), (.48, .12, .71), .057)
    m.add("Scroll wheel", "Rubber", radial([(-.08, .14), (.08, .14)], axis="x"), (0, .448, .49))
    m.box("Indicator", "Blue light", (0, .671, -.58), (.084, .039, .18), .016)
    m.box("Side buttons", "Mint", (-.623, .28, -.26), (.055, .09, .43), .025)
    return m


def building():
    m = Model("City building")
    m.box("Pavement", "Concrete", (0, .07, 0), (2.37, .14, 1.91), .09)
    m.box("Ground floor", "Graphite", (0, .35, 0), (2.04, .53, 1.56), .05)
    m.box("Entrance", "Sky glass", (0, .365, .801), (.68, .42, .082), .04)
    m.box("Main tower", "Terracotta", (0, 1.99, 0), (1.93, 2.84, 1.44), .046)
    for floor in range(5):
        y = .889 + floor * .525
        windows = [box((.32, .27, .039), .012) + (x, y, .741) for x in (-.65, -.216, .216, .65)]
        for sign in (-1, 1):
            for z in (-.42, 0, .42):
                sill = box((.041, .27, .28), .012) + (sign*.980, y, z)
                windows.append(sill)
        m.add(f"Floor {floor + 1} windows", "Sky glass", np.concatenate(windows))
        m.box(f"Floor {floor + 1} trim", "Concrete", (0, y - .22, 0), (1.99, .065, 1.51), .021)
    m.box("Roof cap", "Brushed silver", (0, 3.47, 0), (2.03, .14, 1.55), .048)
    m.box("Roof services", "Concrete", (-.27, 3.63, -.14), (.74, .28, .55), .034)
    m.add("Antenna", "Brushed silver", radial([(3.76, .045), (4.25, .022)], count=12), (-.30, 0, -.14))
    return m


PROPS = [
    ("aurea-television", "TV", "TV", television),
    ("aurea-phone", "Celular", "Smartphone", phone),
    ("aurea-microphone", "Microfone de estúdio", "Studio microphone", microphone),
    ("aurea-headphones", "Fones de ouvido", "Headphones", headphones),
    ("aurea-console", "Console branco", "White game console", console),
    ("aurea-mouse", "Mouse sem fio", "Wireless mouse", mouse),
    ("aurea-building", "Prédio urbano", "City building", building),
]


def save_glb(model, path):
    chunks = bytearray()
    doc = dict(asset=dict(version="2.0", generator="Aurea Editor · Original Models"),
               scene=0, scenes=[dict(nodes=[0])], nodes=[dict(name=model.name, children=[])],
               meshes=[], materials=[], buffers=[dict(byteLength=0)], bufferViews=[], accessors=[])

    def buffer(values, component, shape, target, bounds=False):
        view = len(doc["bufferViews"])
        raw = values.tobytes()
        doc["bufferViews"].append(dict(buffer=0, byteOffset=len(chunks), byteLength=len(raw), target=target))
        chunks.extend(raw)
        chunks.extend(b"\x00" * (-len(chunks) % 4))
        result = dict(bufferView=view, componentType=component, count=len(values), type=shape)
        if bounds:
            result.update(min=values.min(axis=0).tolist(), max=values.max(axis=0).tolist())
        doc["accessors"].append(result)
        return len(doc["accessors"]) - 1

    for name, material, triangles, normals in model.parts:
        color = linear(rgb(material)).tolist()
        _, metal, roughness = PALETTE[material]
        entry = dict(name=f"{name} · {material}", doubleSided=True,
                     pbrMetallicRoughness=dict(baseColorFactor=color + [1], metallicFactor=metal,
                                               roughnessFactor=roughness))
        if material == "Blue light":
            entry["emissiveFactor"] = color
        slot = len(doc["materials"])
        doc["materials"].append(entry)
        world = triangles.reshape(-1, 3)
        center = (world.max(axis=0) + world.min(axis=0)) / 2
        positions = buffer((world - center).astype("<f4"), 5126, "VEC3", 34962, bounds=True)
        normal = buffer(np.repeat(normals, 3, axis=0).astype("<f4"), 5126, "VEC3", 34962)
        indices = buffer(np.arange(len(world), dtype="<u2"), 5123, "SCALAR", 34963)
        primitive = dict(attributes=dict(POSITION=positions, NORMAL=normal), indices=indices, material=slot)
        doc["nodes"][0]["children"].append(len(doc["nodes"]))
        doc["nodes"].append(dict(name=name, translation=center.tolist(), mesh=len(doc["meshes"])))
        doc["meshes"].append(dict(name=name, primitives=[primitive]))
    doc["buffers"][0]["byteLength"] = len(chunks)
    metadata = json.dumps(doc, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    metadata += b" " * (-len(metadata) % 4)
    path.write_bytes(struct.pack("<III", 0x46546C67, 2, 28 + len(metadata) + len(chunks)) +
                     struct.pack("<II", len(metadata), 0x4E4F534A) + metadata +
                     struct.pack("<II", len(chunks), 0x004E4942) + chunks)


def thumbnail(model, path):
    """Render the actual model with an orthographic camera and a soft studio backdrop."""
    height, width = 460, 536
    front = np.array((.56, .71, 1.0) if model.name == "Wireless mouse" else (.46, .30, 1.0))
    front /= np.linalg.norm(front)
    right = np.cross((0, 1, 0), front)
    right /= np.linalg.norm(right)
    basis = np.array((right, -np.cross(front, right), front)).T
    projected = [triangles @ basis for _, _, triangles, _ in model.parts]
    points = np.concatenate(projected).reshape(-1, 3)
    lo, hi = points.min(axis=0), points.max(axis=0)
    scale = min(width * .80 / (hi[0] - lo[0]), height * .79 / (hi[1] - lo[1]))
    offset = np.array((width / 2, height * .49)) - (lo[:2] + hi[:2]) / 2 * scale
    yy, xx = np.mgrid[0:height, 0:width]
    light = np.exp(-3 * (((xx - width*.43)/width) ** 2 + ((yy - height*.41)/height) ** 2))
    shadow = np.exp(-(((xx - width/2)/(width*.26)) ** 2 + ((yy - (hi[1]*scale+offset[1]+9))/(height*.031)) ** 2))
    output = np.zeros((height, width, 3), dtype=np.float64) + np.array((13., 19., 30.))
    output += light[:, :, None] * np.array((22., 32., 46.))
    output *= 1 - shadow[:, :, None] * .49
    depth = np.full((height, width), -np.inf)
    lamp = np.array((-.41, .70, .63))
    for (_, material, _, normals), part in zip(model.parts, projected):
        shades = .43 + .53*np.maximum(0, normals @ lamp) + .17*np.maximum(0, normals @ front)
        color = rgb(material)*255
        for triangle, shade in zip(part, shades):
            (x1, y1), (x2, y2), (x3, y3) = triangle[:, :2]*scale + offset
            x0 = max(0, math.floor(min(x1, x2, x3)))
            y0 = max(0, math.floor(min(y1, y2, y3)))
            xe = min(width, math.ceil(max(x1, x2, x3))+1)
            ye = min(height, math.ceil(max(y1, y2, y3))+1)
            det = (y2-y3)*(x1-x3) + (x3-x2)*(y1-y3)
            if abs(det) < 1e-7:
                continue
            yy, xx = np.ogrid[y0:ye, x0:xe]
            a = ((y2-y3)*(xx+.5-x3) + (x3-x2)*(yy+.5-y3))/det
            b = ((y3-y1)*(xx+.5-x3) + (x1-x3)*(yy+.5-y3))/det
            c = 1-a-b
            z = a*triangle[0, 2]+b*triangle[1, 2]+c*triangle[2, 2]
            region = depth[y0:ye, x0:xe]
            mask = (a >= 0) & (b >= 0) & (c >= 0) & (z > region)
            region[mask] = z[mask]
            output[y0:ye, x0:xe][mask] = np.clip(color * shade, 0, 255)
    Image.fromarray(output.astype(np.uint8)).resize((268, 230), Image.Resampling.LANCZOS).save(path, quality=91)


def main():
    catalog = OUTPUT / "catalog.json"
    existing = json.loads(catalog.read_text(encoding="utf-8"))
    additions = []
    for key, pt, en, build in PROPS:
        model = build()
        save_glb(model, OUTPUT / f"{key}.glb")
        thumbnail(model, OUTPUT / f"{key}.jpg")
        additions.append(dict(id=key, names=dict(pt=pt, en=en), provider="Aurea",
                              category="realistic", source="builtin:aurea-original", license=""))
        print(f"{key}: {len(model.parts)} editable parts")
    keys = {entry["id"] for entry in additions}
    catalog.write_text(json.dumps(additions + [entry for entry in existing if entry["id"] not in keys],
                                  ensure_ascii=False, indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
