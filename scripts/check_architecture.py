#!/usr/bin/env python3
"""Architecture ratchet: layer rules for #include and target_link_libraries, size budgets, QA symbols in StrefaZero.

    python3 scripts/check_architecture.py                        # layer rules + budgets (CI job `architecture`)
    python3 scripts/check_architecture.py --base origin/main     # also: allowlist and budgets never grow vs. the base
    python3 scripts/check_architecture.py --symbols build/noipo/StrefaZero   # QA symbols in the player binary (nm -C)
    python3 scripts/check_architecture.py --update               # lower the allowlist and budgets to today's values
    python3 scripts/check_architecture.py --report               # print the graph edges and every metric

Known violations live in tools/architecture_allowlist.txt and measured values in tools/architecture_budget.json.
Both may only go down: a new violation or a larger value fails, and so does a smaller one that was not written back
(run --update in the PR that pays the debt off). --update never raises a value.
"""
import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ALLOWLIST = "tools/architecture_allowlist.txt"
BUDGET = "tools/architecture_budget.json"
SYMBOL_KEY = "strefa_zero_qa_symbols"
# QA code that must not reach the player binary: the scene benchmark, the scenario runner and GPU profile dumps.
QA_SYMBOL = re.compile(r"Benchmark|ScenarioRunner|write_profile_samples")

# Module of every CMake target built from src/. Test executables (strefa_*_tests) are not part of the graph.
TARGET_MODULE = {
    "strefa_core": "core",
    "strefa_storage": "platform",
    "strefa_platform": "platform",
    "strefa_sim": "sim",
    "strefa_gameplay": "game",
    "strefa_game": "game",
    "strefa_launch": "launch",
    "strefa_gpu": "gpu",
    "strefa_ui": "ui",
    "strefa_app": "app",
    "strefa_zero": "app",
    "strefa_physics": "tools",
    "strefa_diagnostics": "qa",
    "strefa_qa": "qa",
}
EXTERNAL_TARGETS = {"strefa_options", "strefa_imgui"}
# Headers whose module differs from their .cpp: profiling.hpp declares the ProfileOptions/StepProfile types that
# ParticleWorld itself uses, while profiling.cpp (the profile dumps) builds into strefa_diagnostics.
HEADER_MODULE = {"src/gpu/profiling.hpp": "gpu"}
# Sources outside every target (headers without a .cpp, .inl) take the module of their directory.
DIRECTORY_MODULE = {"core": "core", "sim": "sim", "game": "game", "gpu": "gpu", "platform": "platform", "ui": "ui",
                    "app": "app", "tools": "tools"}

# Forbidden edges (from, to) with the reason. "*" matches every module except the target itself.
# Only the composition roots (app = StrefaZero, qa = StrefaQA) may assemble GPU and CPU worlds;
# the CPU model (sim) is the oracle and the GPU must not depend on it.
RULES = [
    ("gpu", "game", "GPU nie zależy od rozgrywki (protokół zamiast GameplaySession)"),
    ("gpu", "ui", "GPU nie zależy od interfejsu"),
    ("gpu", "app", "GPU nie zależy od aplikacji"),
    ("gpu", "sim", "GPU nie zależy od wyroczni CPU"),
    ("sim", "gpu", "wyrocznia CPU nie zależy od GPU"),
    ("sim", "ui", "wyrocznia CPU nie zależy od interfejsu"),
    ("sim", "app", "wyrocznia CPU nie zależy od aplikacji"),
    ("*", "gpu", "implementację GPU składają tylko app i qa"),
    ("*", "sim", "model CPU składają tylko app, qa i tools"),
    ("*", "qa", "kod QA nie trafia do gry"),
]
RULE_EXEMPT = {"gpu": {"app", "qa"}, "sim": {"app", "qa", "tools"}, "qa": set()}

SIZE_LITERAL = re.compile(r"(?<![\w.])(?:320|192)(?:\.0*)?[uUfF]?(?![\w.])")
# Inverse masses of sand (2.65) and oil (3.3125) written as literals instead of coming from a material table.
# Lines computing an expected value from them are test oracles, not definitions.
INVERSE_MASS = re.compile(r"(?<![\w.])(?:2\.65|3\.3125)[fF]?(?![\w.])")
SHADER_SUFFIXES = (".comp", ".frag", ".vert", ".glsl")


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def strip_comments(text):
    return re.sub(r"//[^\n]*|/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)


def source_files(*directories, suffixes=(".cpp", ".hpp", ".inl")):
    files = []
    for directory in directories:
        files += [p for p in sorted((ROOT / directory).rglob("*")) if p.suffix in suffixes and p.is_file()]
    return files


def rel(path):
    return path.relative_to(ROOT).as_posix()


# --- CMake graph ---------------------------------------------------------------------------------------------------


def cmake_commands(name):
    text = strip_cmake_comments(read("CMakeLists.txt"))
    for match in re.finditer(rf"\b{name}\s*\(([^)]*)\)", text):
        yield match.group(1).split()


def strip_cmake_comments(text):
    return re.sub(r"#[^\n]*", "", text)


def cmake_targets():
    sources, links = {}, {}
    for command in ("add_library", "add_executable"):
        for args in cmake_commands(command):
            name, rest = args[0], args[1:]
            sources[name] = [a for a in rest if a.startswith("src/")]
    for args in cmake_commands("target_link_libraries"):
        links.setdefault(args[0], []).extend(a for a in args[1:] if a not in ("PUBLIC", "PRIVATE", "INTERFACE"))
    return sources, links


def file_modules(sources):
    module = dict(HEADER_MODULE)
    for target, files in sources.items():
        if target in TARGET_MODULE:
            for path in files:
                module[path] = TARGET_MODULE[target]
    for path in source_files("src"):
        name = rel(path)
        if name in module:
            continue
        sibling = path.with_suffix(".cpp")
        if path.suffix == ".hpp" and rel(sibling) in module:
            module[name] = module[rel(sibling)]
        elif name == "src/main.cpp":
            module[name] = "app"
        else:
            module[name] = DIRECTORY_MODULE[path.relative_to(ROOT / "src").parts[0]]
    return module


def forbidden(source, target):
    if source == target:
        return None
    for rule_from, rule_to, reason in RULES:
        if rule_to != target or rule_from not in ("*", source):
            continue
        if rule_from == "*" and source in RULE_EXEMPT.get(target, ()):
            continue
        return reason
    return None


def violations():
    """Returns {(kind, from, to): [edge descriptions]} for every forbidden edge."""
    sources, links = cmake_targets()
    unknown = sorted(t for t in sources if t.startswith("strefa_") and not t.endswith("_tests")
                     and t not in TARGET_MODULE and t not in EXTERNAL_TARGETS)
    if unknown:
        sys.exit(f"Nieznany target CMake: {', '.join(unknown)}. Dopisz jego moduł do TARGET_MODULE w {rel(pathlib.Path(__file__))}.")
    module = file_modules(sources)
    found = {}
    for path in source_files("src"):
        name = rel(path)
        for header in re.findall(r'^\s*#\s*include\s+"([^"]+)"', read(name), flags=re.M):
            included = f"src/{header}"
            if included not in module:
                continue
            reason = forbidden(module[name], module[included])
            if reason:
                found.setdefault(("include", module[name], module[included]), []).append(f"{name} -> {included}")
    for target, libraries in links.items():
        for library in libraries:
            if target in TARGET_MODULE and library in TARGET_MODULE:
                source, dest = TARGET_MODULE[target], TARGET_MODULE[library]
                if forbidden(source, dest):
                    found.setdefault(("link", source, dest), []).append(f"{target} -> {library}")
    return found


def parse_allowlist(text):
    allowed = {}
    for number, line in enumerate(text.splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 4 or parts[0] not in ("include", "link") or not parts[3].isdigit():
            sys.exit(f"{ALLOWLIST}:{number}: oczekiwano `include|link <z> <do> <liczba krawędzi>`")
        allowed[tuple(parts[:3])] = int(parts[3])
    return allowed


def format_allowlist(found):
    lines = [
        "# Known layer violations (scripts/check_architecture.py). This list may only shrink.",
        "# <kind> <from> <to> <edges>: include = distinct (file, header) pairs, link = target_link_libraries entries.",
        "# Lower a count (or delete the line) with `python3 scripts/check_architecture.py --update` when you fix one.",
    ]
    lines += [f"{kind} {source} {dest} {len(edges)}" for (kind, source, dest), edges in sorted(found.items())]
    return "\n".join(lines) + "\n"


# --- Metrics -------------------------------------------------------------------------------------------------------


def class_body(text, name):
    start = text.index(f"class {name} {{")
    depth = 0
    for index in range(text.index("{", start), len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[start:index]
    raise ValueError(name)


def public_declarations():
    body = class_body(read("src/gpu/world.hpp"), "ParticleWorld")
    public = body[body.index("public:") + len("public:"):body.index("private:")]
    declarations, current = [], []
    for line in public.splitlines():
        code = line.split("//", 1)[0].strip()
        if not current and (not code or line.strip().startswith("//")):
            continue  # comment lines above a declaration describe a group, not this declaration
        current.append(line)
        if code.endswith(";"):
            declarations.append("\n".join(current))
            current = []
    return declarations


TEST_HOOK_NAME = re.compile(r"^(validate_|run_\w+_benchmark|read_|readback$|audit_|probe_)|_for_test$|diagnostic")


def test_hooks(declarations):
    hooks = []
    for declaration in declarations:
        code = " ".join(line.split("//", 1)[0] for line in declaration.splitlines())
        name = re.search(r"(~?\w+)\s*\(", code)
        if (name and TEST_HOOK_NAME.search(name.group(1))) or "validation only" in declaration:
            hooks.append(name.group(1) if name else declaration)
    return hooks


def function_lines(path, signature):
    lines = read(path).splitlines()
    start = next(i for i, line in enumerate(lines) if signature in line)
    depth = 0
    for index in range(start, len(lines)):
        depth += lines[index].count("{") - lines[index].count("}")
        if depth == 0 and "}" in lines[index]:
            return index - start + 1
    raise ValueError(signature)


def world_tu_lines():
    world = read("src/gpu/world.cpp")
    total = len(world.splitlines())
    for header in re.findall(r'^#include "(gpu/\w+\.inl)"', world, flags=re.M):
        total += len(read(f"src/{header}").splitlines())
    return total


def launch_option_bools():
    # Distinct boolean switches the parser sets (`result.<flag> = true`).
    return len(set(re.findall(r"\bresult\.(\w+) = true;", read("src/app/launch_options.cpp"))))


def count(pattern, files, comments=True):
    total = 0
    for path in files:
        text = path.read_text(encoding="utf-8")
        total += len(pattern.findall(text if comments else strip_comments(text)))
    return total


def inverse_mass_lines():
    total = 0
    for path in source_files("src", "assets/shaders", suffixes=(".cpp", ".hpp", ".inl") + SHADER_SUFFIXES):
        for line in strip_comments(path.read_text(encoding="utf-8")).splitlines():
            if INVERSE_MASS.search(line) and "expected" not in line.lower():
                total += 1
    return total


def metrics():
    cpp = source_files("src")
    glsl = source_files("assets/shaders", suffixes=SHADER_SUFFIXES)
    declarations = public_declarations()
    return {
        "world_cpp_lines": len(read("src/gpu/world.cpp").splitlines()),
        "world_tu_lines": world_tu_lines(),
        "particle_world_public_decls": len(declarations),
        "particle_world_test_hooks": len(test_hooks(declarations)),
        "application_cpp_lines": len(read("src/app/application.cpp").splitlines()),
        "application_run_lines": function_lines("src/app/application.cpp", "std::expected<int, std::string> run() {"),
        "launch_options_bools": launch_option_bools(),
        "inl_includes": count(re.compile(r'^\s*#\s*include\s+"[^"]+\.inl"', re.M), cpp),
        "size_literals_cpp": count(SIZE_LITERAL, cpp + source_files("tests")),
        "size_literals_glsl": count(SIZE_LITERAL, glsl),
        "inverse_mass_literal_lines": inverse_mass_lines(),
        "raw_ssbo_arrays": count(re.compile(r"std::array<GLuint,\s*8>"), cpp),
        "unsigned_ref_banks": count(re.compile(r"\bunsigned\s*&\s*(?:current|bank)\b"), cpp),
        "glsl_lines_over_200": sum(len(line) > 200 for p in glsl for line
                                   in p.read_text(encoding="utf-8").splitlines()),
    }


def qa_symbols(binary):
    nm = shutil.which("nm")
    if not nm:
        sys.exit("Brak programu nm (binutils).")
    output = subprocess.run([nm, "-C", binary], check=True, capture_output=True, text=True).stdout
    return sum(1 for line in output.splitlines() if QA_SYMBOL.search(line))


# --- Ratchet -------------------------------------------------------------------------------------------------------


def compare(name, actual, limit, errors, lowered):
    if actual > limit:
        errors.append(f"{name}: {actual} > {limit} (budżet może tylko maleć)")
    elif actual < limit:
        lowered.append(f"{name}: {actual} < {limit}")


def git_show(ref, path):
    result = subprocess.run(["git", "show", f"{ref}:{path}"], cwd=ROOT, capture_output=True, text=True)
    return result.stdout if result.returncode == 0 else None


def check_base(ref, allowed, budget, errors):
    base_allowlist, base_budget = git_show(ref, ALLOWLIST), git_show(ref, BUDGET)
    if base_allowlist is not None:
        base = parse_allowlist(base_allowlist)
        for key, edges in sorted(allowed.items()):
            if edges > base.get(key, 0):
                errors.append(f"{ALLOWLIST}: `{' '.join(key)} {edges}` przekracza {ref} ({base.get(key, 0)})")
    if base_budget is not None:
        base = json.loads(base_budget)
        for key, value in sorted(budget.items()):
            if key in base and value > base[key]:
                errors.append(f"{BUDGET}: {key} = {value} > {base[key]} na {ref}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", help="git ref whose allowlist and budgets this tree may not exceed")
    parser.add_argument("--symbols", metavar="BINARY", help="count QA symbols in StrefaZero built without IPO")
    parser.add_argument("--update", action="store_true", help="lower the allowlist and budgets to today's values")
    parser.add_argument("--report", action="store_true", help="print every violating edge and metric")
    parser.add_argument("--root", type=pathlib.Path, help="repository to check (default: this one)")
    args = parser.parse_args()
    if args.root:
        global ROOT
        ROOT = args.root.resolve()

    found = violations()
    allowed = parse_allowlist(read(ALLOWLIST))
    budget = json.loads(read(BUDGET))
    values = {SYMBOL_KEY: qa_symbols(args.symbols)} if args.symbols is not None else metrics()

    errors, lowered = [], []
    if args.symbols is None:
        for key in sorted(set(found) | set(allowed)):
            name = " ".join(key)
            edges = len(found.get(key, []))
            if key not in allowed:
                reason = forbidden(key[1], key[2])
                errors.append(f"nowe naruszenie warstw `{name}` ({reason}):\n    " + "\n    ".join(found[key]))
            else:
                compare(f"{ALLOWLIST} `{name}`", edges, allowed[key], errors, lowered)
                if edges > allowed[key]:
                    errors[-1] += "\n    " + "\n    ".join(found[key])
    for key, value in values.items():
        if key not in budget:
            errors.append(f"{BUDGET}: brak wartości {key}")
        else:
            compare(f"{BUDGET} {key}", value, budget[key], errors, lowered)
    if args.base:
        check_base(args.base, allowed, budget, errors)

    if args.report:
        for (kind, source, dest), edges in sorted(found.items()):
            print(f"{kind} {source} -> {dest}: {len(edges)}")
            for edge in edges:
                print(f"    {edge}")
        for key, value in values.items():
            print(f"{key} = {value} (budżet {budget.get(key, '-')})")

    if args.update:
        if errors:
            print("\n".join(errors), file=sys.stderr)
            sys.exit("--update tylko obniża wartości; najpierw usuń nowe naruszenia.")
        if args.symbols is None:
            (ROOT / ALLOWLIST).write_text(format_allowlist(found), encoding="utf-8", newline="\n")
        budget.update(values)
        (ROOT / BUDGET).write_text(json.dumps(budget, indent=2) + "\n", encoding="utf-8", newline="\n")
        print("\n".join(f"obniżono {line}" for line in lowered) or "Bez zmian.")
        return 0

    if lowered:
        errors += [f"{line}: dług spadł, zapisz to przez `python3 scripts/check_architecture.py --update`"
                   + (f" --symbols {args.symbols}" if args.symbols else "") for line in lowered]
    if errors:
        print("Strażnik architektury: " + str(len(errors)) + " błąd(y)", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        print("Opis reguł: CLAUDE.md, sekcja Architecture ratchet.", file=sys.stderr)
        return 1
    print("Strażnik architektury: OK" + (f" ({SYMBOL_KEY} = {values[SYMBOL_KEY]})" if args.symbols else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
