#!/usr/bin/env python3
"""Generates src/sim/material_tables.hpp and assets/shaders/materials.glsl from the material registry
src/sim/materials.toml.

Usage: python3 scripts/gen_materials.py [--check] [--registry FILE] [--output FILE] [--glsl FILE]
  (no flag)  rewrite the generated files
  --check    exit 1 when a generated file differs from the registry (CI job `format`)
  --registry, --output, --glsl  other paths, used by scripts/test-gen-materials.sh

The order of the [[material]] tables is the material ID (sim::Material). Numbers are emitted with
repr(), which round-trips a double exactly, so the tables carry the same bits as the registry values.
An enthalpy offset is emitted as the expression offset(from) + (c_from - c_to) * T + latent over the registry
values and evaluated by the C++ compiler; sim.unit checks it against the hand-written expressions it replaced.
"""
import argparse
import math
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REGISTRY = ROOT / "src" / "sim" / "materials.toml"
OUTPUT = ROOT / "src" / "sim" / "material_tables.hpp"
GLSL = ROOT / "assets" / "shaders" / "materials.glsl"

PHASES = ("empty", "solid", "powder", "liquid", "gas", "flame")
PHYSICAL = ("density", "heat_capacity", "conductance", "spawn_temperature", "ignition", "burn_rate",
            "heat_release", "residue", "strength")
KEYS = {"id", "name", "description", "phase", "color", *PHYSICAL}
OPTIONAL_KEYS = {"enthalpy_offset_of"}
TRANSITION_KEYS = {"from", "to", "temperature", "latent"}
# Material IDs are stored in recordings and index the thermal shader tables, so an ID never moves: the registry
# must start with exactly this list. A new material is appended to the registry and to this list in the same PR.
PINNED_IDS = ("empty", "stone", "sand", "water", "oil", "wood", "fire", "smoke", "steam", "acid", "lava", "gas",
              "coal", "gunpowder", "thermite", "nitroglycerin", "metal", "ice", "molten_metal", "ash")


def fail(message: str) -> None:
    sys.exit(f"gen_materials: {message}")


def cpp_string(material: str, key: str, text: object) -> str:
    if not isinstance(text, str) or any(ord(c) < 32 or ord(c) == 127 for c in text):
        fail(f"{material}.{key}: expected a single-line string")
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def number(material: str, key: str, value: object) -> str:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        fail(f"{material}.{key}: expected a number, got {value!r}")
    if not math.isfinite(value):
        fail(f"{material}.{key}: must be finite")
    return repr(value)


def load(registry: Path) -> dict:
    with registry.open("rb") as stream:
        data = tomllib.load(stream)
    if unknown := sorted(data.keys() - {"material", "transition", "particle_materials"}):
        fail(f"unknown top-level key {', '.join(unknown)}")
    materials = data.get("material", [])
    if not materials:
        fail(f"{registry.name} has no [[material]] table")
    if len(materials) > 255:
        fail("sim::Material is a std::uint8_t: at most 255 materials")
    seen = set()
    for index, entry in enumerate(materials):
        name = entry.get("id", f"#{index}")
        if not isinstance(name, str):
            fail(f"#{index}: id must be a string")
        if missing := sorted(KEYS - entry.keys()):
            fail(f"{name}: missing {', '.join(missing)}")
        if unknown := sorted(entry.keys() - KEYS - OPTIONAL_KEYS):
            fail(f"{name}: unknown key {', '.join(unknown)}")
        if not name.isidentifier() or name == "count" or name in seen:
            fail(f"{name}: id must be a unique C++ identifier other than 'count'")
        seen.add(name)
        if entry["phase"] not in PHASES:
            fail(f"{name}: phase must be one of {', '.join(PHASES)}")
        color = entry["color"]
        if (not isinstance(color, list) or len(color) != 3
                or any(isinstance(c, bool) or not isinstance(c, int) or not 0 <= c <= 255 for c in color)):
            fail(f"{name}: color must be three integers 0-255")
    ids = tuple(entry["id"] for entry in materials)
    if ids[:len(PINNED_IDS)] != PINNED_IDS:
        fail("material IDs are append-only: the registry must start with PINNED_IDS in the same order")
    if len(ids) != len(PINNED_IDS):
        fail(f"new material {ids[len(PINNED_IDS)]}: append its id to PINNED_IDS in scripts/gen_materials.py")
    index = {name: position for position, name in enumerate(ids)}
    aliases = {entry["id"]: entry["enthalpy_offset_of"] for entry in materials if "enthalpy_offset_of" in entry}
    for name, alias in aliases.items():
        if alias not in index or alias == name or alias in aliases:
            fail(f"{name}: enthalpy_offset_of must name another material that is not an alias itself")
    # A transition defines the enthalpy offset of its `to` material from its `from` material, so every `to`
    # is unique, is not an alias, and the transition that defines its `from` comes first.
    transitions, defined = data.get("transition", []), set()
    for position, entry in enumerate(transitions):
        label = f"transition #{position}"
        if not isinstance(entry, dict) or entry.keys() != TRANSITION_KEYS:
            fail(f"{label}: expected exactly the keys {', '.join(sorted(TRANSITION_KEYS))}")
        source, target = entry["from"], entry["to"]
        if source not in index or target not in index or source == target:
            fail(f"{label}: from and to must be two different material ids")
        if target in defined or target in aliases:
            fail(f"{label}: the enthalpy offset of {target} is already defined")
        if source in aliases or any(later["to"] == source for later in transitions[position:]):
            fail(f"{label}: the enthalpy offset of {source} must be defined before this transition")
        for key in ("temperature", "latent"):
            number(label, key, entry[key])
        defined.add(target)
    particles = data.get("particle_materials", [])
    if not isinstance(particles, list) or not particles or any(name not in index for name in particles):
        fail("particle_materials must be a non-empty list of material ids")
    return {"materials": materials, "transitions": transitions, "particles": particles}


def offset_expressions(registry: dict) -> dict[str, str]:
    capacity = {m["id"]: number(m["id"], "heat_capacity", m["heat_capacity"]) for m in registry["materials"]}
    offsets = {m["id"]: "0" for m in registry["materials"]}
    for t in registry["transitions"]:
        source, target = t["from"], t["to"]
        base = "" if offsets[source] == "0" else offsets[source] + " + "
        offsets[target] = (f"{base}({capacity[source]} - {capacity[target]}) * "
                           f"{number(target, 'temperature', t['temperature'])} + "
                           f"{number(target, 'latent', t['latent'])}")
    for m in registry["materials"]:
        if "enthalpy_offset_of" in m:
            offsets[m["id"]] = offsets[m["enthalpy_offset_of"]]
    return offsets


def render(registry: dict) -> str:
    materials, transitions, particles = registry["materials"], registry["transitions"], registry["particles"]
    offsets = offset_expressions(registry)
    out = [
        "// Generated by scripts/gen_materials.py from src/sim/materials.toml. Do not edit: change the registry and",
        "// rerun the script (CI job `format` runs it with --check).",
        "#pragma once",
        '#include "sim/material_types.hpp"',
        "",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "// clang-format off",
        "namespace strefa::sim {",
        "",
        "enum class Material : std::uint8_t {",
    ]
    out += [f"    {m['id']}," for m in materials]
    out += [
        "    count",
        "};",
        "inline constexpr std::array<MaterialDefinition, static_cast<std::size_t>(Material::count)> materials{{",
    ]
    for m in materials:
        r, g, b = m["color"]
        name, description = (cpp_string(m["id"], key, m[key]) for key in ("name", "description"))
        out.append(f"    {{{name}, {description}, Phase::{m['phase']}, {{{r}, {g}, {b}}}}},")
    out += [
        "}};",
        "// " + ", ".join(PHYSICAL),
        "inline constexpr std::array<PhysicalProperties, materials.size()> physical{{",
    ]
    for m in materials:
        values = ", ".join(number(m["id"], key, m[key]) for key in PHYSICAL)
        out.append(f"    {{{values}}}, // {m['id']}")
    out += [
        "}};",
        "// Phase change from `from` into `to` at `temperature` (K); latent heat in kJ per unit mass.",
        "struct PhaseTransition {",
        "    Material from, to;",
        "    double temperature, latent;",
        "};",
        f"inline constexpr std::array<PhaseTransition, {len(transitions)}> transitions{{{{",
    ]
    for t in transitions:
        out.append(f"    {{Material::{t['from']}, Material::{t['to']}, "
                   f"{number(t['to'], 'temperature', t['temperature'])}, {number(t['to'], 'latent', t['latent'])}}},")
    out += [
        "}};",
        "// Enthalpy offset of each material: offset(from) + (c_from - c_to) * temperature + latent of the",
        "// transition into it, 0 without one.",
        "inline constexpr std::array<double, materials.size()> enthalpy_offsets{{",
    ]
    out += [f"    {offsets[m['id']]}, // {m['id']}" for m in materials]
    out += [
        "}};",
        "// CPU-model material of each GPU particle material, in the order of gpu::Material.",
        f"inline constexpr std::array<Material, {len(particles)}> particle_materials{{{{",
    ]
    out += [f"    Material::{name}," for name in particles]
    out += ["}};", "", "} // namespace strefa::sim", "// clang-format on", ""]
    return "\n".join(out)


def render_glsl(registry: dict) -> str:
    out = [
        "// Generated by scripts/gen_materials.py from src/sim/materials.toml. Do not edit: change the registry and",
        "// rerun the script (CI job `format` runs it with --check).",
        "// Material IDs (sim::Material) and phase transitions: temperature in K, latent heat in kJ per unit mass.",
        "// Macros, not `const` variables: a shader that uses them compiles from the same tokens as with literals.",
    ]
    out += [f"#define MATERIAL_{m['id'].upper()} {position}" for position, m in enumerate(registry["materials"])]
    for t in registry["transitions"]:
        name = t["to"].upper()
        for key in ("temperature", "latent"):
            number(name, key, t[key])
            out.append(f"#define {name}_TRANSITION_{key.upper()} {float(t[key])!r}")
    return "\n".join(out) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate the material tables from the TOML registry.")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--registry", type=Path, default=REGISTRY)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--glsl", type=Path, default=GLSL)
    arguments = parser.parse_args()
    registry = load(arguments.registry)
    for output, text in ((arguments.output, render(registry)), (arguments.glsl, render_glsl(registry))):
        if arguments.check:
            current = output.read_text(encoding="utf-8").replace("\r\n", "\n") if output.exists() else ""
            if current != text:
                fail(f"{output.name} is stale: run python3 scripts/gen_materials.py")
            continue
        with output.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(text)


if __name__ == "__main__":
    main()
