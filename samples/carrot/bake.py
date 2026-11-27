#!/usr/bin/env python3
"""Rebuild the Carrot Man demo assets from the original Sketchfab GLB."""
import argparse
import math
from pathlib import Path
import subprocess
import sys

SAMPLE = Path(__file__).resolve().parent
sys.path.insert(0, str(SAMPLE.parents[1] / "tools"))
from cook_glb import load_glb, write_asset


def ramp(value, start, end):
    t = max(0, min(1, (value - start) / (end - start)))
    return t * t * (3 - 2 * t)


def step(phase):
    phase %= 1
    stride = 0.67
    if phase < 0.5:
        # The planted foot moves backward relative to the advancing body.
        return -stride + 4 * stride * phase, 0
    time = (phase - 0.5) * 2
    return stride * math.cos(math.pi * time), 0.42 * math.sin(math.pi * time)


def walk_pose(points, phase):
    sine = math.sin(phase * math.tau)
    bob = 0.11 * sine * sine
    left = step(phase + 0.25)
    right = step(phase + 0.75)
    pose = []
    for x, y, z in points:
        # Smoothly blend each modeled foot into the torso around the hips.
        legs = ramp(y, 0.12, 0.78)
        side = ramp(x, -0.15, 0.15)
        reach = (1 - side) * left[0] + side * right[0]
        lift = (1 - side) * left[1] + side * right[1]
        arm = ramp(abs(x), 0.82, 1.68) * (1 - ramp(abs(y + 1), 0.12, 0.48))
        leaves = ramp(-y, 2.68, 4.64)
        pose.append((
            x + 0.09 * sine * (1 - legs) + 0.17 * sine * leaves,
            y - (1 - legs) * bob - legs * lift + 0.12 * arm * abs(sine),
            z + legs * reach + math.copysign(1, x) * 0.48 * arm * sine - 0.08 * leaves,
        ))
    return pose


def idle_pose(points, phase):
    sway = math.sin(phase * math.tau)
    return [
        (x + 0.12 * sway * ramp(-y, 2.68, 4.64),
         y - 0.05 * (1 - math.cos(phase * math.tau)) * (1 - ramp(y, 0.12, 0.78)),
         z)
        for x, y, z in points
    ]


def fit_atlas_cards(mesh):
    # Keep the cutout cards inside their atlas islands when the GPU rounds UVs.
    glasses = (173, 0, 251, 78)
    leaves = (157, 110, 236, 189)
    glasses_vertices = set()
    midpoints = {}
    faces = []
    for corners, uvs in mesh.faces:
        pixels = [(u * 256, v * 256) for u, v in uvs]
        is_glasses = False
        for bounds in (glasses, leaves):
            x0, y0, x1, y1 = bounds
            if all(x0 - 1 <= u <= x1 + 1 and y0 - 1 <= v <= y1 + 1 for u, v in pixels):
                uvs = tuple(
                    (max(x0 + 1, min(x1 - 1, u)) / 256,
                     max(y0 + 1, min(y1 - 1, v)) / 256) for u, v in pixels
                )
                is_glasses = bounds == glasses
                break

        if not is_glasses:
            faces.append((corners, uvs))
            continue

        # Smaller glasses triangles sort more locally against the curved face.
        vertices = list(corners)
        texels = list(uvs)
        for a, b in ((0, 1), (1, 2), (2, 0)):
            edge = tuple(sorted((corners[a], corners[b])))
            if edge not in midpoints:
                midpoints[edge] = len(mesh.positions)
                mesh.positions.append(tuple((m + n) / 2 for m, n in
                                            zip(mesh.positions[edge[0]], mesh.positions[edge[1]])))
            vertices.append(midpoints[edge])
            texels.append(tuple((m + n) / 2 for m, n in zip(uvs[a], uvs[b])))
        glasses_vertices.update(vertices)
        for indices in ((0, 3, 5), (3, 1, 4), (5, 4, 2), (3, 4, 5)):
            faces.append((tuple(vertices[i] for i in indices), tuple(texels[i] for i in indices)))
    mesh.faces = faces
    for i in glasses_vertices:
        x, y, z = mesh.positions[i]
        # Bow the middle forward to sort over the face, with the ends tucked in.
        mesh.positions[i] = (x, y, z - 0.30 + 0.62 * x * x)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("glb", type=Path)
    parser.add_argument("--output", type=Path, default=SAMPLE / "assets")
    args = parser.parse_args()
    try:
        # The scene's world transforms straighten the body. Turn its face toward
        # the default camera (-Z) so the demo can use directional yaw values.
        mesh = load_glb(args.glb, yaw=90)
        fit_atlas_cards(mesh)
        floor = max(y for _, y, _ in mesh.positions)
        poses = [idle_pose(mesh.positions, frame / 4) for frame in range(4)]
        poses.extend(walk_pose(mesh.positions, frame / 12) for frame in range(12))
        poses = [[(x, y - floor, z) for x, y, z in pose] for pose in poses]
        write_asset(mesh, poses, 32, args.output, bpp=8, black_key=True)
    except (OSError, ValueError, KeyError, IndexError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"bake: {error}\n")


if __name__ == "__main__":
    main()
