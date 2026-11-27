#!/usr/bin/env python3
"""Cook one textured, static GLB primitive into PSXE topology, poses and TIM."""
import argparse
import json
import math
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path


IDENTITY = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)


@dataclass
class Mesh:
    positions: list
    faces: list
    image: bytes
    double_sided: bool


def glb_chunks(path):
    data = path.read_bytes()
    if len(data) < 12 or struct.unpack_from("<4sII", data) != (b"glTF", 2, len(data)):
        raise ValueError("expected a complete glTF 2.0 binary file")
    chunks = {}
    offset = 12
    while offset < len(data):
        size, kind = struct.unpack_from("<I4s", data, offset)
        offset += 8
        if size % 4 or size > len(data) - offset or kind in chunks:
            raise ValueError("invalid GLB chunk")
        chunks[kind] = data[offset:offset + size]
        offset += size
    return json.loads(chunks[b"JSON"]), chunks[b"BIN\0"]


def matrix(node):
    if "matrix" in node:
        return node["matrix"]
    x, y, z, w = node.get("rotation", (0, 0, 0, 1))
    sx, sy, sz = node.get("scale", (1, 1, 1))
    tx, ty, tz = node.get("translation", (0, 0, 0))
    return (
        sx * (1 - 2*(y*y + z*z)), sx * 2*(x*y + z*w), sx * 2*(x*z - y*w), 0,
        sy * 2*(x*y - z*w), sy * (1 - 2*(x*x + z*z)), sy * 2*(y*z + x*w), 0,
        sz * 2*(x*z + y*w), sz * 2*(y*z - x*w), sz * (1 - 2*(x*x + y*y)), 0,
        tx, ty, tz, 1,
    )


def mesh_instance(doc):
    nodes = doc["nodes"]
    instances = []

    def visit(index, parent, ancestors):
        if index in ancestors:
            raise ValueError("cyclic GLB node hierarchy")
        node = nodes[index]
        local = matrix(node)
        world = tuple(
            sum(parent[row + 4*k] * local[k + 4*col] for k in range(4))
            for col in range(4) for row in range(4)
        )
        if "mesh" in node:
            instances.append((node, world))
        for child in node.get("children", []):
            visit(child, world, ancestors | {index})

    for root in doc["scenes"][doc.get("scene", 0)]["nodes"]:
        visit(root, IDENTITY, set())
    if len(instances) != 1:
        raise ValueError("expected one mesh node in the default scene")
    node, world = instances[0]
    primitives = doc["meshes"][node["mesh"]]["primitives"]
    if len(primitives) != 1 or "skin" in node or doc.get("animations"):
        raise ValueError("export one static mesh primitive without skin or animation")
    return primitives[0], world


def load_glb(path, yaw=0):
    doc, binary = glb_chunks(path)
    if len(doc["buffers"]) != 1 or "uri" in doc["buffers"][0]:
        raise ValueError("expected one embedded GLB buffer")
    buffer_size = doc["buffers"][0]["byteLength"]
    if not 0 < buffer_size <= len(binary):
        raise ValueError("invalid GLB buffer length")

    def view_bytes(index):
        view = doc["bufferViews"][index]
        start = view.get("byteOffset", 0)
        length = view["byteLength"]
        if view["buffer"] != 0 or start < 0 or not 0 < length <= buffer_size - start:
            raise ValueError("buffer view exceeds the embedded GLB buffer")
        return memoryview(binary)[start:start + length]

    def accessor(index, kind, formats):
        entry = doc["accessors"][index]
        if entry["type"] != kind or entry.get("sparse") or entry.get("normalized"):
            raise ValueError(f"unsupported {kind} accessor layout")
        fmt = formats.get(entry["componentType"])
        if fmt is None:
            raise ValueError(f"unsupported {kind} component type")
        unpack = struct.Struct("<" + fmt)
        data = view_bytes(entry["bufferView"])
        view = doc["bufferViews"][entry["bufferView"]]
        stride = view.get("byteStride", unpack.size)
        start = entry.get("byteOffset", 0)
        count = entry["count"]
        if (not 0 < count <= 3 * 65535 or start < 0 or stride < unpack.size or
                start + (count - 1) * stride + unpack.size > len(data)):
            raise ValueError("accessor exceeds its buffer view or PSXE mesh limits")
        return [unpack.unpack_from(data, start + i * stride) for i in range(count)]

    primitive, world = mesh_instance(doc)
    if primitive.get("mode", 4) != 4 or primitive.get("targets"):
        raise ValueError("expected triangles without morph targets")
    attributes = primitive["attributes"]
    positions = accessor(attributes["POSITION"], "VEC3", {5126: "fff"})
    uvs = accessor(attributes["TEXCOORD_0"], "VEC2", {5126: "ff"})
    indices = ([i for i, in accessor(primitive["indices"], "SCALAR", {5121: "B", 5123: "H", 5125: "I"})]
               if "indices" in primitive else list(range(len(positions))))
    if len(positions) != len(uvs) or len(indices) % 3 or max(indices) >= len(positions):
        raise ValueError("triangle indices and vertex attributes do not match")
    if any(not 0 <= v <= 1 for uv in uvs for v in uv):
        raise ValueError("UVs must fit a single texture in the 0..1 range")

    material = doc["materials"][primitive["material"]]
    pbr = material["pbrMetallicRoughness"]
    texture = pbr["baseColorTexture"]
    if (pbr.get("baseColorFactor", [1, 1, 1, 1]) != [1, 1, 1, 1] or
            texture.get("texCoord", 0) != 0 or texture.get("extensions") or
            material.get("alphaMode", "OPAQUE") == "BLEND"):
        raise ValueError("use an untinted base color image on TEXCOORD_0 without texture transforms or blending")
    image = doc["images"][doc["textures"][texture["index"]]["source"]]
    if image["mimeType"] not in ("image/png", "image/jpeg"):
        raise ValueError("expected an embedded PNG or JPEG base color image")

    a, b, c, _, d, e, f, _, g, h, i, _, tx, ty, tz, _ = world
    determinant = a*(e*i - f*h) - d*(b*i - c*h) + g*(b*f - c*e)
    if determinant == 0 or not math.isfinite(determinant):
        raise ValueError("mesh transform must be finite and invertible")
    sine, cosine = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
    remap = {}
    points = {}
    for index in dict.fromkeys(indices):
        x, y, z = positions[index]
        px = a*x + d*y + g*z + tx
        py = -(b*x + e*y + h*z + ty)
        pz = -(c*x + f*y + i*z + tz)
        point = tuple(round(value, 6) for value in (cosine*px + sine*pz, py, cosine*pz - sine*px))
        if not all(map(math.isfinite, point)):
            raise ValueError("non-finite mesh position")
        remap[index] = points.setdefault(point, len(points))
    if len(points) > 65535:
        raise ValueError("too many distinct positions for PSXE indices")

    # The GTE expects clockwise front faces in screen space.
    order = (0, 2, 1) if determinant > 0 else (0, 1, 2)
    faces = []
    for start in range(0, len(indices), 3):
        triangle = [indices[start + lane] for lane in order]
        faces.append((tuple(remap[j] for j in triangle), tuple(uvs[j] for j in triangle)))
    return Mesh(list(points), faces, bytes(view_bytes(image["bufferView"])), material.get("doubleSided", False))


def pack_vertices(positions, scale):
    data = bytearray()
    for point in positions:
        xyz = tuple(round(value * scale) for value in point)
        if any(not -32768 <= value <= 32767 for value in xyz):
            raise ValueError("scaled vertex exceeds the signed 16-bit coordinate range")
        data.extend(struct.pack("<hhhh", *xyz, 0))
    return data


def make_tim(image, width, height, bpp, black_key):
    if not 1 <= height <= 256 or not 1 <= width <= 256 or width % (4 if bpp == 8 else 2):
        raise ValueError("TIM must fit 256x256 with width aligned to 4 (8 bpp) or 2 (16 bpp)")
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-f", "image2pipe", "-i", "pipe:0",
         "-frames:v", "1", "-vf", f"scale={width}:{height}:flags=neighbor",
         "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"],
        input=image, stdout=subprocess.PIPE, check=True,
    ).stdout
    if len(raw) != width * height * 4:
        raise ValueError("could not decode the embedded base color image")

    colors = []
    for r, g, b, alpha in struct.iter_unpack("BBBB", raw):
        rgb555 = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
        transparent = alpha < 128 or (black_key and (r, g, b) == (0, 0, 0))
        colors.append(0 if transparent else rgb555 or 0x8000)

    def block(payload, words, rows):
        return struct.pack("<I4H", len(payload) + 12, 0, 0, words, rows) + payload

    if bpp == 8:
        palette = {0: 0}
        pixels = bytearray()
        for color in colors:
            index = palette.setdefault(color, len(palette))
            if index >= 256:
                raise ValueError("more than 255 opaque RGB555 colors, use --bpp 16")
            pixels.append(index)
        clut = struct.pack("<256H", *palette, *([0] * (256 - len(palette))))
        return struct.pack("<II", 0x10, 9) + block(clut, 256, 1) + block(pixels, width // 2, height)

    pixels = struct.pack(f"<{len(colors)}H", *colors)
    return struct.pack("<II", 0x10, 2) + block(pixels, width, height)


def write_asset(mesh, poses, scale, output, width=256, height=256, bpp=16, black_key=False):
    tim = make_tim(mesh.image, width, height, bpp, black_key)
    blob = bytearray()
    for pose in poses:
        if len(pose) != len(mesh.positions):
            raise ValueError("every pose must use the base mesh's vertex order and count")
        blob.extend(pack_vertices(pose, scale))
    if not blob or len(poses) > 65535:
        raise ValueError("expected 1..65535 poses")
    lines = [
        "/* Generated by tools/cook_glb.py. */",
        f"enum {{ COOKED_VERTEX_COUNT = {len(mesh.positions)}, COOKED_POSE_COUNT = {len(poses)} }};",
    ]
    flags = "PSXE_MATERIAL_TEXTURED"
    if mesh.double_sided:
        flags += " | PSXE_MATERIAL_DOUBLE_SIDED"
    lines.extend([
        f"#define COOKED_FACE(a, b, c, u0, v0, u1, v1, u2, v2) \\",
        f"    {{ {{ a, b, c }}, 0, {{ {{ u0, v0 }}, {{ u1, v1 }}, {{ u2, v2 }} }}, {{ 128, 128, 128 }}, {flags} }}",
        "static const psxe_face cooked_faces[] = {",
    ])
    for indices, uvs in mesh.faces:
        arguments = list(indices)
        for u, v in uvs:
            arguments.extend((min(int(u * width), width - 1), min(int(v * height), height - 1)))
        lines.append("    COOKED_FACE(" + ", ".join(map(str, arguments)) + "),")
    lines.extend(["};", "#undef COOKED_FACE", ""])

    output.mkdir(parents=True, exist_ok=True)
    (output / "mesh.inc").write_bytes("\n".join(lines).encode("ascii"))
    (output / "poses.bin").write_bytes(blob)
    (output / "color.tim").write_bytes(tim)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("glb", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--height", type=int, default=180, help="target height in PSXE units")
    parser.add_argument("--yaw", type=float, default=0, help="rotate around PSXE Y, in degrees")
    parser.add_argument("--size", type=int, default=256, help="square TIM edge in texels")
    parser.add_argument("--bpp", type=int, choices=(8, 16), default=16)
    parser.add_argument("--black-key", action="store_true", help="make exact black transparent")
    args = parser.parse_args()
    try:
        mesh = load_glb(args.glb, args.yaw)
        top = min(y for _, y, _ in mesh.positions)
        floor = max(y for _, y, _ in mesh.positions)
        if not 0 < args.height <= 32767 or floor <= top:
            raise ValueError("expected positive target and source mesh heights")
        pose = [(x, y - floor, z) for x, y, z in mesh.positions]
        write_asset(mesh, [pose], args.height / (floor - top), args.output,
                    args.size, args.size, args.bpp, args.black_key)
    except (OSError, ValueError, KeyError, IndexError, TypeError, struct.error, subprocess.CalledProcessError) as error:
        parser.exit(1, f"cook_glb: {error}\n")


if __name__ == "__main__":
    main()
