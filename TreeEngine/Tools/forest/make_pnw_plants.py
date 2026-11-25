"""Writes Game/Resources/Scenes/pnw-plants.scene.json: south Puget Sound trees and understory as L-systems.

Each species is one `tree` object (its own generator, bark and leaf atlas regions), shown full size on a meadow, with
the tree LOD schedule Forest uses. Placement in Hungerford copies these sources (Tools/terrain, later). Edit the rules
here and re-run; the scene file is generated.

    python make_pnw_plants.py [--out <scene.json>]
"""
import argparse
import json
import math
import os
import random
import re

HERE = os.path.dirname(os.path.abspath(__file__))
RESOURCES = os.path.normpath(os.path.join(HERE, '..', '..', 'Game', 'Resources'))
DEG = math.pi / 180
LOD_ON = True
LOD = {"distances": [10, 30, 42, 60, 85, 120, 170, 240], "hysteresis": 0.1}


def texture(stem, wrap):
    return {"source": f"Nature/{stem}", "wrap": wrap}


def bark(name, tint, roughness=1.0, scale=(1, 3)):
    return {"baseColor": tint + [1], "metallic": 0, "roughness": roughness, "specular": 0.3,
            "textureCoordScale": list(scale),
            "baseColorTexture": texture(f"bark_{name}_color.jpg", True),
            "normalTexture": texture(f"bark_{name}_normal.jpg", True),
            "metallicRoughnessTexture": texture(f"bark_{name}_roughness.jpg", True)}


def foliage(atlas, tint, specular=0.35):
    return {"baseColor": tint + [1], "metallic": 0, "roughness": 1, "specular": specular, "alphaMode": "mask",
            "alphaCutoff": 0.5,
            "baseColorTexture": texture(f"{atlas}_color.png", False),
            "normalTexture": texture(f"{atlas}_normal.png", False),
            "metallicRoughnessTexture": texture(f"{atlas}_roughness.png", False)}


MATERIALS = {
    "meadow": {"baseColor": [0.11, 0.15, 0.06, 1], "metallic": 0, "roughness": 0.95},
    # Douglas fir: thick, corky, deeply furrowed red-brown bark.
    "bark-douglas-fir": bark("pine", [0.75, 0.62, 0.55]),
    # Western hemlock: darker, finely furrowed grey-brown.
    "bark-hemlock": bark("furrowed", [0.7, 0.6, 0.55]),
    # Western red cedar: long fibrous strips, red-grey.
    "bark-cedar": bark("willow", [1.25, 1.0, 0.9], scale=(1, 2)),
    # Red alder: smooth grey with white lichen; bigleaf maple: grey-brown, mossy.
    "bark-alder": bark("furrowed", [1.35, 1.35, 1.3], scale=(1, 2)),
    "bark-maple": bark("furrowed", [0.95, 0.95, 0.85]),
    # Pacific madrone: smooth, peeling red-orange.
    "bark-madrone": {"baseColor": [0.42, 0.13, 0.06, 1], "metallic": 0, "roughness": 0.45, "specular": 0.4},
    "stem-shrub": {"baseColor": [0.17, 0.12, 0.07, 1], "metallic": 0, "roughness": 0.85},
    "stem-green": {"baseColor": [0.12, 0.17, 0.06, 1], "metallic": 0, "roughness": 0.8},
    "stem-cane": {"baseColor": [0.22, 0.1, 0.09, 1], "metallic": 0, "roughness": 0.6},
    # Foliage: one atlas per group, tinted per species.
    "needles-fir": foliage("pnw_conifer", [0.62, 0.72, 0.55]),
    "needles-hemlock": foliage("pnw_conifer", [0.5, 0.62, 0.5]),
    "sprays-cedar": foliage("pnw_conifer", [0.45, 0.58, 0.36]),
    "fronds-fern": foliage("pnw_conifer", [0.75, 0.85, 0.65]),
    "leaves-broadleaf": foliage("pnw_broadleaf", [0.95, 1.0, 0.9]),
    "leaves-glossy": foliage("pnw_broadleaf", [0.85, 0.95, 0.8], specular=0.6),
    "grass": foliage("canyon_grasses", [0.75, 0.95, 0.6]),
}


def regions(name):
    path = os.path.join(RESOURCES, 'Nature', f'{name}.regions.json')
    return json.load(open(path, encoding='utf-8'))


def tree(tid, position, trunk, leaf, atlas, leaf_regions, generator, *, twist=60, size=0.25, shape='skinnedCylinder',
         lod=True):
    if not LOD_ON:
        # Full detail at any distance: one LOD distance past the scene. (Leaving lod out isn't that: the parser's
        # default is a 10 m distance, beyond which deep twigs draw simplified.)
        obj_lod = {"distances": [100000]}
    else:
        obj_lod = LOD if lod else None
    obj = {"id": tid, "type": "tree", "transform": {"position": position}, "animationSpeed": 10,
           "trunkShape": shape, "materials": {"trunk": trunk, "leaf": leaf},
           "leaves": {"atlas": atlas, "twistDegrees": twist, "sizeVariation": size, "regions": leaf_regions},
           "generator": dict(generator, angle=DEG, segmentLength=1)}
    if obj_lod:
        obj["lod"] = obj_lod
    return obj


def rules(**r):
    """rules(A_s_="...") is the rule for A(s).

    The engine's rule parser evaluates `s * k` and `s + k` but reads a bare `s` as 0, so a variable passed on unchanged
    is written `s * 1` (a trunk segment `!(s)` would otherwise have no thickness)."""
    bare = re.compile(r'\(([a-z])\s*([,)])')
    return [{"input": f"{k.split('_')[0]}({k.split('_')[1]})", "output": bare.sub(r"(\1 * 1\2", v)}
            for k, v in r.items()]


# --- Trees ------------------------------------------------------------------------------------------------------
# Symbols: F(l, spread) stem, L(l) leaf, Y yaw about the stem, Z / z pitch away from / toward the stem's axis,
# !(t) thickness, [ ] branch. A second argument to an angle or F is a random spread. Each rewrite step adds one
# whorl, so lower branches have grown for more steps than upper ones: the crown tapers by itself.

DOUGLAS_FIR = {
    # ~30 m: a straight excurrent trunk, whorls of 4 limbs that dip and turn up at the tips, dense flat sprays.
    "axiom": "!(1) F(4) A(1)",
    "iterations": 17,
    "thickness": 0.45,
    "rules": rules(
        A_s_="!(s) F(1.5, 0.12) [Z(82, 8) B(s)] [Y(90, 15) Z(86, 8) B(s * 0.9)] [Y(180, 15) Z(78, 8) B(s * 0.95)] "
             "[Y(270, 15) Z(84, 8) B(s * 0.9)] Y(45, 20) A(s * 0.94)",
        B_s_="!(s * 0.2) F(0.45, 0.2) [Y(90, 20) Z(70, 10) C(1)] [Y(270, 20) Z(70, 10) C(1)] [Y(0, 40) Z(35, 10) C(1)] "
             "z(4, 3) B(s * 0.93)",
        C_s_="!(0.025) F(0.2, 0.3) [Y(90) Z(50, 15) L(0.5)] [Y(270) Z(50, 15) L(0.5)] F(0.2, 0.3) "
             "[Y(90) Z(50, 15) L(0.45)] [Y(270) Z(50, 15) L(0.45)] L(0.55)",
    ),
}

WESTERN_HEMLOCK = {
    # ~26 m: finer, drooping limbs and dense, darker, smaller sprays.
    "axiom": "!(1) F(3) A(1)",
    "iterations": 17,
    "thickness": 0.36,
    "rules": rules(
        A_s_="!(s) F(1.35, 0.12) [Z(88, 8) B(s)] [Y(90, 15) Z(92, 8) B(s * 0.9)] [Y(180, 15) Z(85, 8) B(s * 0.95)] "
             "[Y(270, 15) Z(90, 8) B(s * 0.9)] Y(45, 20) A(s * 0.94)",
        B_s_="!(s * 0.17) F(0.38, 0.2) [Y(90, 20) Z(75, 10) C(1)] [Y(270, 20) Z(75, 10) C(1)] [Y(0, 40) Z(40, 10) C(1)] "
             "Z(5, 3) B(s * 0.92)",
        C_s_="!(0.02) F(0.18, 0.3) [Y(90) Z(60, 15) L(0.38)] [Y(270) Z(60, 15) L(0.38)] F(0.15, 0.3) "
             "[Y(90) Z(60, 15) L(0.35)] [Y(270) Z(60, 15) L(0.35)] Z(25) L(0.4)",
    ),
}

WESTERN_RED_CEDAR = {
    # ~28 m: a buttressed, fluted base; limbs that dip and sweep up into a J; lacy sprays hanging from them.
    "axiom": "!(1.6) F(0.6) !(1.05) F(2.4) A(1)",
    "iterations": 17,
    "thickness": 0.5,
    "rules": rules(
        A_s_="!(s) F(1.45, 0.12) [Z(80, 8) B(s)] [Y(90, 15) Z(84, 8) B(s * 0.9)] [Y(180, 15) Z(78, 8) B(s * 0.95)] "
             "[Y(270, 15) Z(82, 8) B(s * 0.9)] Y(45, 20) A(s * 0.93)",
        B_s_="!(s * 0.2) F(0.5, 0.2) [Y(90, 25) Z(115, 15) C(1)] [Y(270, 25) Z(115, 15) C(1)] "
             "[Y(0, 40) Z(60, 15) C(1)] Z(2, 2) B(s * 0.93)",
        C_s_="!(0.02) F(0.2, 0.3) [Y(70) Z(30, 10) L(0.6)] [Y(250) Z(30, 10) L(0.6)] F(0.15) "
             "[Y(20) Z(25, 10) L(0.55)] L(0.65)",
    ),
}

RED_ALDER = {
    # ~20 m: a long clear pale trunk, a narrow rounded crown of upswept limbs, many mid-size serrated leaves.
    "axiom": "!(1) F(5, 0.1) Z(3) A(1)",
    "iterations": 12,
    "thickness": 0.24,
    "rules": rules(
        A_s_="!(s) F(1.15, 0.15) [Y(0, 30) Z(50, 12) B(s)] [Y(180, 30) Z(46, 12) B(s * 0.85)] Y(97, 15) Z(2, 3) "
             "A(s * 0.9)",
        B_s_="!(s * 0.45) F(0.6, 0.25) [Y(90, 40) Z(45, 15) T(1)] [Y(270, 40) Z(45, 15) T(1)] [Y(0, 40) Z(25, 10) T(1)] "
             "z(4, 4) B(s * 0.82)",
        T_s_="!(0.03) F(0.25, 0.3) [Y(60) Z(55, 15) L(0.3)] [Y(180) Z(55, 15) L(0.28)] [Y(300) Z(55, 15) L(0.3)] "
             "F(0.22, 0.3) [Y(30) Z(55, 15) L(0.28)] [Y(150) Z(55, 15) L(0.28)] [Y(270) Z(55, 15) L(0.28)] L(0.3)",
    ),
}

BIGLEAF_MAPLE = {
    # ~22 m: a short trunk forking into several big ascending limbs, a broad dome of very large leaves.
    "axiom": "!(1) F(3.5, 0.1) [Z(26, 6) M(1)] [Y(95) Z(34, 6) M(0.9)] [Y(190) Z(30, 6) M(0.95)] [Y(285) Z(32, 6) M(0.85)]",
    "iterations": 9,
    "thickness": 0.34,
    "rules": rules(
        M_s_="!(s * 0.8) F(1.5, 0.15) [Y(0, 40) Z(55, 12) B(s * 0.7)] [Y(180, 40) Z(50, 12) B(s * 0.6)] "
             "Y(97, 20) Z(4, 4) M(s * 0.85)",
        B_s_="!(s * 0.5) F(0.75, 0.25) [Y(90, 40) Z(45, 15) T(1)] [Y(270, 40) Z(45, 15) T(1)] [Y(0, 40) Z(25, 10) T(1)] "
             "z(2, 4) B(s * 0.82)",
        T_s_="!(0.035) F(0.3, 0.3) [Y(45) Z(60, 15) L(0.5)] [Y(165) Z(60, 15) L(0.5)] [Y(285) Z(60, 15) L(0.5)] "
             "F(0.25) [Y(100) Z(50, 15) L(0.45)] [Y(280) Z(50, 15) L(0.45)] L(0.5)",
    ),
}

PACIFIC_MADRONE = {
    # ~15 m: a leaning, crooked red trunk dividing into twisting limbs; glossy evergreen leaves crowded at the tips.
    "axiom": "!(1) Z(14) F(2.5, 0.1) z(10) F(1.5) [Z(30, 8) M(1)] [Y(150) Z(38, 8) M(0.85)] [Y(260) Z(25, 8) M(0.7)]",
    "iterations": 9,
    "thickness": 0.26,
    "rules": rules(
        M_s_="!(s * 0.75) F(1.0, 0.2) Z(9, 9) [Y(0, 50) Z(50, 15) B(s * 0.7)] [Y(180, 50) Z(45, 15) B(s * 0.6)] "
             "Y(110, 30) z(8, 8) M(s * 0.85)",
        B_s_="!(s * 0.45) F(0.6, 0.3) Z(8, 8) [Y(90, 40) Z(40, 15) T(1)] [Y(270, 40) Z(40, 15) T(1)] "
             "[Y(0, 40) Z(20, 10) T(1)] B(s * 0.8)",
        T_s_="!(0.03) F(0.2, 0.3) [Y(50) Z(55, 15) L(0.24)] [Y(170) Z(55, 15) L(0.24)] [Y(290) Z(55, 15) L(0.24)] "
             "F(0.12) [Y(0) Z(45) L(0.22)] [Y(120) Z(45) L(0.22)] [Y(240) Z(45) L(0.22)] L(0.24)",
    ),
}

VINE_MAPLE = {
    # ~5 m: several slender arching stems from the base, spreading layers of leaves.
    "axiom": "[Z(18, 6) S(1)] [Y(90) Z(28, 6) S(0.9)] [Y(180) Z(22, 6) S(0.95)] [Y(270) Z(32, 6) S(0.8)] "
             "[Y(45) Z(40, 6) S(0.7)]",
    "iterations": 9,
    "thickness": 0.05,
    "rules": rules(
        S_s_="!(s) F(0.45, 0.15) [Y(0, 40) Z(55, 12) T(1)] [Y(180, 40) Z(55, 12) T(1)] Y(97, 20) Z(6, 4) S(s * 0.88)",
        T_s_="!(0.25) F(0.25, 0.3) [Y(60) Z(65, 15) L(0.2)] [Y(180) Z(65, 15) L(0.2)] [Y(300) Z(65, 15) L(0.2)] "
             "F(0.15) [Y(0) Z(60) L(0.18)] [Y(180) Z(60) L(0.18)] L(0.2)",
    ),
}

# --- Understory -------------------------------------------------------------------------------------------------

SWORD_FERN = {
    # ~1 m: a fountain of stiff fronds from one crown.
    "axiom": " ".join(f"[Y({a}) Z({z}, 8) L({l})]" for a, z, l in [
        (0, 25, 1.0), (30, 40, 0.95), (60, 30, 1.05), (90, 45, 0.9), (120, 28, 1.0), (150, 42, 0.95),
        (180, 32, 1.0), (210, 48, 0.85), (240, 35, 1.0), (270, 38, 0.95), (300, 30, 1.0), (330, 44, 0.9),
        (15, 60, 0.8), (75, 62, 0.8), (135, 58, 0.85), (195, 63, 0.8), (255, 60, 0.85), (315, 61, 0.8),
        (45, 15, 0.9), (165, 18, 0.9), (285, 16, 0.9)]),
    "iterations": 1,
    "thickness": 0.01,
    "rules": [],
}

SALAL = {
    # ~1 m: a dense thicket of zigzag stems with leathery oval leaves.
    "axiom": " ".join(f"[Y({a}) Z({z}, 8) S({s})]" for a, z, s in [
        (0, 15, 1), (45, 30, 0.9), (90, 22, 1), (135, 35, 0.85), (180, 18, 1), (225, 32, 0.9), (270, 25, 0.95),
        (315, 38, 0.8), (20, 50, 0.6), (200, 50, 0.6)]),
    "iterations": 8,
    "thickness": 0.012,
    "rules": rules(
        S_s_="!(s) F(0.11, 0.2) [Y(0) Z(60, 15) L(0.11)] [Y(120) Z(60, 15) L(0.11)] [Y(240) Z(60, 15) L(0.11)] "
             "[Y(60, 30) Z(45, 10) T(1)] Y(90, 20) Z(6, 8) S(s * 0.9)",
        T_s_="F(0.08) [Y(0) Z(50) L(0.1)] [Y(120) Z(50) L(0.1)] [Y(240) Z(50) L(0.1)] L(0.1)",
    ),
}

OREGON_GRAPE = {
    # ~1.2 m: upright stems with spiny, holly-like leaflets in opposite pairs.
    "axiom": "[S(1)] [Y(70) Z(15, 6) S(0.85)] [Y(140) Z(12, 6) S(0.9)] [Y(210) Z(18, 6) S(0.8)] [Y(290) Z(14, 6) S(0.9)]",
    "iterations": 8,
    "thickness": 0.012,
    "rules": rules(
        S_s_="!(s) F(0.13, 0.15) [Y(90) Z(55, 10) P(1)] [Y(270) Z(55, 10) P(1)] Y(90, 15) S(s * 0.93)",
        P_s_="F(0.05) [Y(90) Z(60) L(0.09)] [Y(270) Z(60) L(0.09)] F(0.06) [Y(90) Z(60) L(0.09)] [Y(270) Z(60) L(0.09)] "
             "L(0.1)",
    ),
}

RED_HUCKLEBERRY = {
    # ~2.5 m: an airy shrub of bright green, angled twigs and small leaves.
    "axiom": "[Z(10, 6) S(1)] [Y(90) Z(20, 6) S(0.85)] [Y(180) Z(16, 6) S(0.9)] [Y(270) Z(24, 6) S(0.8)]",
    "iterations": 8,
    "thickness": 0.015,
    "rules": rules(
        S_s_="!(s) F(0.3, 0.2) [Y(0, 40) Z(55, 12) T(1)] [Y(180, 40) Z(55, 12) T(1)] Y(97, 20) Z(5, 5) S(s * 0.9)",
        T_s_="!(0.4) F(0.12, 0.3) [Y(60) Z(55) L(0.06)] [Y(180) Z(55) L(0.06)] [Y(300) Z(55) L(0.06)] "
             "Z(30) F(0.1) [Y(0) Z(55) L(0.06)] [Y(120) Z(55) L(0.06)] [Y(240) Z(55) L(0.06)] "
             "z(50) F(0.1) [Y(60) Z(55) L(0.06)] [Y(180) Z(55) L(0.06)] [Y(300) Z(55) L(0.06)] L(0.06)",
    ),
}

BLACKBERRY = {
    # ~2 m mound: thick canes arching up and over, big compound leaves along them.
    "axiom": " ".join(f"[Y({a}) Z({z}, 6) K(1)]" for a, z in [
        (0, 14), (50, 22), (100, 12), (150, 20), (200, 16), (250, 24), (300, 10), (340, 18)]),
    "iterations": 10,
    "thickness": 0.02,
    "rules": rules(
        K_s_="!(s) F(0.3, 0.15) [Y(90, 30) Z(55, 12) L(0.3)] [Y(270, 30) Z(55, 12) L(0.3)] [Y(0, 30) Z(40, 12) L(0.28)] "
             "Z(9, 3) K(s * 0.93)",
    ),
}

PASTURE_GRASS = {
    # ~0.5 m tufts of blades.
    "axiom": " ".join(f"[Y({a}) Z({z}, 6) L({l})]" for a, z, l in [
        (0, 6, 0.55), (30, 16, 0.5), (60, 10, 0.6), (90, 20, 0.45), (120, 8, 0.55), (150, 18, 0.5),
        (180, 12, 0.6), (210, 22, 0.45), (240, 9, 0.55), (270, 17, 0.5), (300, 13, 0.6), (330, 24, 0.45),
        (15, 28, 0.4), (105, 30, 0.4), (195, 28, 0.4), (285, 30, 0.4), (45, 4, 0.65), (225, 5, 0.65)]),
    "iterations": 1,
    "thickness": 0.005,
    "rules": [],
}


def build(seed=7, lod=True):
    random.seed(seed)
    conifer = regions('pnw_conifer')
    broadleaf = regions('pnw_broadleaf')
    grasses = regions('canyon_grasses')
    atlases = {"pnw-conifer": conifer, "pnw-broadleaf": broadleaf, "canyon-grasses": grasses}
    objects = [{"id": "ground", "type": "primitive", "shape": "cylinderHD", "transform": {"scale": [140, 0.01, 140]},
                "material": "meadow"}]
    # Trees in a row at the back, understory in front, each labelled by its id.
    objects += [
        tree("douglas-fir", [-36, 0, 40], "bark-douglas-fir", "needles-fir", "pnw-conifer", {"fir-a": 1, "fir-b": 1},
             DOUGLAS_FIR, twist=25),
        tree("western-hemlock", [-20, 0, 44], "bark-hemlock", "needles-hemlock", "pnw-conifer",
             {"hemlock-a": 1, "hemlock-b": 1}, WESTERN_HEMLOCK, twist=25),
        tree("western-red-cedar", [-4, 0, 40], "bark-cedar", "sprays-cedar", "pnw-conifer", {"cedar-spray": 1},
             WESTERN_RED_CEDAR, twist=40),
        tree("red-alder", [12, 0, 42], "bark-alder", "leaves-broadleaf", "pnw-broadleaf",
             {"alder-a": 1, "alder-b": 1}, RED_ALDER, twist=80, size=0.3),
        tree("bigleaf-maple", [30, 0, 42], "bark-maple", "leaves-broadleaf", "pnw-broadleaf",
             {"maple-a": 1, "maple-b": 1}, BIGLEAF_MAPLE, twist=80, size=0.3),
        tree("pacific-madrone", [46, 0, 36], "bark-madrone", "leaves-glossy", "pnw-broadleaf",
             {"madrone-a": 1, "madrone-b": 1}, PACIFIC_MADRONE, twist=70, size=0.25),
        tree("vine-maple", [-14, 0, 16], "stem-shrub", "leaves-broadleaf", "pnw-broadleaf",
             {"maple-a": 1, "maple-b": 1}, VINE_MAPLE, twist=80, size=0.3, lod=False),
        tree("red-huckleberry", [-6, 0, 12], "stem-green", "leaves-broadleaf", "pnw-broadleaf",
             {"huckleberry-a": 1, "huckleberry-b": 1}, RED_HUCKLEBERRY, twist=80, size=0.3, lod=False),
        tree("himalayan-blackberry", [2, 0, 12], "stem-cane", "leaves-broadleaf", "pnw-broadleaf", {"bramble": 1},
             BLACKBERRY, twist=60, size=0.25, lod=False),
        tree("salal", [-2, 0, 6], "stem-shrub", "leaves-glossy", "pnw-broadleaf", {"salal-a": 1, "salal-b": 1},
             SALAL, twist=80, size=0.3, lod=False),
        tree("oregon-grape", [2, 0, 5], "stem-shrub", "leaves-glossy", "pnw-broadleaf",
             {"oregon-grape-a": 1, "oregon-grape-b": 1}, OREGON_GRAPE, twist=60, size=0.25, lod=False),
        tree("sword-fern", [-5, 0, 4], "stem-green", "fronds-fern", "pnw-conifer",
             {"sword-fern-a": 1, "sword-fern-b": 1}, SWORD_FERN, twist=30, size=0.2, lod=False),
        tree("pasture-grass", [5.5, 0, 4], "stem-green", "grass", "canyon-grasses",
             {"sedge-1": 1, "sedge-2": 1, "sedge-3": 1, "sedge-4": 1, "sedge-5": 1}, PASTURE_GRASS, twist=40, size=0.3, lod=False),
    ]
    # A patch of instanced grass and ferns, as they will be scattered on the terrain.
    for i in range(24):
        x, z = random.uniform(-12, 12), random.uniform(-2, 3)
        objects.append({"id": f"pasture-grass-{i + 2}", "instanceOf": "pasture-grass",
                         "transform": {"position": [round(x, 2), 0, round(z, 2)],
                                       "rotationDegrees": [0, random.uniform(0, 360), 0]}})
    for i in range(5):
        x, z = random.uniform(-12, -7), random.uniform(5, 10)
        objects.append({"id": f"sword-fern-{i + 2}", "instanceOf": "sword-fern",
                        "transform": {"position": [round(x, 2), 0, round(z, 2)],
                                      "rotationDegrees": [0, random.uniform(0, 360), 0]}})
    return {
        "version": 1, "id": "LoadPnwPlants", "priority": 0,
        "name": "PNW Plants - south Puget Sound trees and understory",
        "camera": {"position": [2, 5, -14], "rotationDegrees": [-2, 0, 0]},
        "render": {"clearColor": [0.48, 0.54, 0.58, 1], "farClippingPlane": 400,
                   "environment": {"diffuse": "Environment/terra-daylight-diffuse.dds",
                                   "specular": "Environment/terra-daylight-specular.dds"},
                   "sky": {"cubeMap": "Environment/terra-daylight-sky.dds"},
                   "ambientOcclusion": {"radius": 1.25, "strength": 0.65}},
        "light": {"ambient": [0, 0, 0, 1], "diffuse": [2.3, 2.3, 2.3, 1], "specular": [2.3, 2.3, 2.3, 1],
                  "direction": [0.6259292648824133, -0.7141802822623909, 0.3133034947002239],
                  "angularDiameterDegrees": 2, "shadowSamples": 8},
        "materials": MATERIALS, "atlases": atlases, "objects": objects}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', default=os.path.join(RESOURCES, 'Scenes', 'pnw-plants.scene.json'))
    parser.add_argument('--no-lod', action='store_true', help='full detail at any distance (for comparing species)')
    args = parser.parse_args()
    global LOD_ON
    LOD_ON = not args.no_lod
    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(build(), f, indent=2)
        f.write('\n')
    print(f'wrote {args.out}')


if __name__ == '__main__':
    main()
