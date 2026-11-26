"""Lexical checks for the standard-backport / Forge-extension boundary."""

from bisect import bisect_right
import fnmatch
import os
from pathlib import Path
import posixpath
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
FORGE_PATHS = ("include/forge/*", "test/forge/*", "test/unique_resource/*",
               "test/source_consumer/*", "test/install_consumer/*")
ABSENCE = {
    "test/consumer_hive.cpp": ("forge/execution.hpp",),
    "test/install_consumer/std_main.cpp": ("forge/execution.hpp", "forge/unique_resource.hpp"),
    "test/std_target/compile/test_std_target.cpp": ("forge/execution.hpp", "forge/unique_resource.hpp"),
}
OWNER_HELPER = "test/TestOwner.cmake"
CONFIG_FIXTURES = (
    # These fixtures seed the real configuration input under test.
    "test/probe_fingerprint/test_configuration_inputs.cmake",
    "test/submdspan/gate_fixture/CMakeLists.txt",
)
PREPARED_COMPILE = "_forge_check_prepared_cxx_source_compiles"
FILE_COMPILE = "_forge_detect_cxx_source_compiles"
PROBE_CALLERS = {
    "check_cxx_source_compiles": {(OWNER_HELPER, PREPARED_COMPILE)},
    "check_cxx_source_runs": {(OWNER_HELPER, "_forge_check_native_cxx_source_runs")},
    PREPARED_COMPILE: {
        (OWNER_HELPER, "_forge_check_native_cxx_source_compiles"),
        (OWNER_HELPER, "_forge_check_std_cxx_source_compiles"),
        ("test/CMakeLists.txt", FILE_COMPILE),
        ("test/CMakeLists.txt", "forge_check_rejection_warning_control"),
    },
    FILE_COMPILE: {
        ("test/CMakeLists.txt", "forge_detect_cxx_source_compiles"),
        ("test/CMakeLists.txt", "forge_expect_cxx_source_compiles"),
    },
}
CPP_FILE = re.compile(r"\.(?:[ch]|cc|cp|cpp|cxx|c\+\+|hh|hpp|hxx|h\+\+|inc|inl|ipp|tcc|tpp|cppm|ixx)(?:\.in)?$", re.I)
CPP_TOKENS = re.compile(
    r'''(?P<comment>//[^\n]*|/\*.*?\*/)|(?P<raw>R"([^()\s\\]{0,16})\(.*?\)\3")'''
    r'''|"(?:\\.|[^"\\\n])*"|(?<![\w'])(?:u8|[uUL])?'(?:\\.|[^'\\\n])*' ''', re.S | re.X)
CMAKE_TOKENS = re.compile(
    r'''(?P<comment>\#\[(=*)\[.*?\]\2\]|\#[^\n]*)|\[(=*)\[(?P<bracket>.*?)\]\3\]'''
    r'''|"(?P<quoted>(?:\\.|[^"\\])*)"|\\[()]''', re.S)
CMAKE_MARK = re.compile(r"\b([a-z_]\w*)\s*\(|[()]", re.I)
CMAKE_WORD = re.compile(r"[a-z_]\w*(?=$|[\s()])", re.I)
ROOT_INPUT = re.compile(r"\bFORGE_INCLUDE_DIR\b")
FIXTURE_SETUP = re.compile(r"(?im)^[ \t]*set\s*\(\s*(FORGE_INCLUDE_DIR)(?=[\s)])")
SPLICE = re.compile(r"\\[ \t\v\f]*\n")
HEADER = re.compile(r'<([^>\n]+)>|"([^"\n]+)"')
DIRECTIVE = re.compile(r"^[ \t\v\f]*(#|%:)[ \t\v\f]*(\w+)\b([^\n]*)", re.M)
HAS_INCLUDE = re.compile(r"\b__has_include(?:_next)?\s*\(\s*")
GUARD_ERROR = re.compile(r"\n[ \t\v\f]*(?:#|%:)[ \t\v\f]*error\b")
SPACE = re.compile(r"\s*")
STD = re.compile(r"\bstd\s*::")
QUALIFIER = re.compile(r"\s*(?:template\s+)?(\w+)\s*")
TEMPLATE_MARK = re.compile(r"[<>(){}\[\];]")
OTHER_PRIVATE = re.compile(r"\bnamespace\s+\w+\s*=\s*(?:::\s*)?std\b|\w*__forge_\w*")
SAL = re.compile(r"\b(?:__nothrow|__callback|__in|__out|__inout|__range|__success|__checkReturn|__fallthrough|__notnull|__format_string|__reserved|__deref|__ecount|__bcount|__override|__allocator)\b")


def is_forge(path):
    return any(fnmatch.fnmatchcase(path, glob) for glob in FORGE_PATHS) or (
        path.startswith("example/") and Path(path).name.startswith("forge_"))


def blank(match):
    return re.sub(r"[^\n]", " ", match[0])


def absence_guard(path):
    checks = [r"__has_include\s*\(\s*<" + re.escape(h) + r">\s*\)" for h in ABSENCE[path]]
    prefix = r"\s*defined\s*\(\s*CCFORGE_CONSUMER_STD_ONLY\s*\)\s*&&" if path == "test/consumer_hive.cpp" else ""
    return prefix + r"\s*" + r"\s*\|\|\s*".join(checks) + r"\s*"


def directive_headers(kind, body, source, start, end):
    if kind in ("include", "include_next"):
        operand = HEADER.match(source, start + SPACE.match(body).end(), end)
        if operand:
            yield operand
            return
    after = 0
    for probe in HAS_INCLUDE.finditer(body):
        if probe.start() >= after:
            operand = HEADER.match(source, start + probe.end(), end)
            if operand:
                yield operand
                after = operand.end() - start


def extension_header(root, path, operand):
    angle, quote = operand.groups()
    header = (angle or quote).replace("\\", "/")
    bases = ["include", "backport"]
    if quote:
        bases.insert(0, posixpath.dirname(path))
    extension = (root / "include/forge").as_posix() + "/"
    return any(posixpath.normpath(posixpath.join(root.as_posix(), base, header)).startswith(extension)
               for base in bases)


def private_std(tokens, pos):
    while member := QUALIFIER.match(tokens, pos):
        if member[1].startswith("__"):
            return True
        pos = member.end()
        if tokens.startswith("<", pos):
            closers = [">"]
            for mark in TEMPLATE_MARK.finditer(tokens, pos + 1):
                ch = mark[0]
                if ch == closers[-1]:
                    closers.pop()
                    if not closers:
                        pos = SPACE.match(tokens, mark.end()).end()
                        break
                elif ch in "([{":
                    closers.append({"(": ")", "[": "]", "{": "}"}[ch])
                elif ch == "<" and closers[-1] == ">":
                    closers.append(">")
                elif ch == ";" and closers[-1] == ">":
                    return False
            else:
                return False
        if not tokens.startswith("::", pos):
            return False
        pos += 2
    return False


def cmake_literal(code, pos):
    pos = SPACE.match(code, pos).end()
    match = CMAKE_WORD.match(code, pos) or CMAKE_TOKENS.match(code, pos)
    if not match:
        return "", pos
    word = match[0] if match.re is CMAKE_WORD else (match["quoted"] or match["bracket"] or "")
    return word.lower(), match.end()


def probe_violations(path, code, shape):
    scopes, depth = [], 0
    for mark in CMAKE_MARK.finditer(shape):
        if mark[0] == ")":
            depth -= 1
            continue
        depth += 1
        if depth != 1 or not mark[1]:
            continue
        name = mark[1].lower()
        if name in ("function", "macro", "cmake_language"):
            arg, pos = cmake_literal(code, mark.end())
            if name != "cmake_language":
                scopes.append((name, arg))
                continue
            if arg != "call":
                continue
            name, _ = cmake_literal(code, pos)
        if name in ("endfunction", "endmacro") and scopes:
            scopes.pop()
        elif name in PROBE_CALLERS:
            function = scopes[-1][1] if scopes and scopes[-1][0] == "function" else None
            if (path, function) not in PROBE_CALLERS[name]:
                yield 5, code.count("\n", 0, mark.start()) + 1


def violations(root, path):
    forge = is_forge(path)
    cmake = path.endswith((".cmake", ".cmake.in", "CMakeLists.txt", "CMakeLists.txt.in"))
    cpp = CPP_FILE.search(path) or (path.startswith(("backport/", "include/", "test/", "example/"))
                                    and not Path(path).suffix)
    if not (cpp or cmake) or not (root / path).is_file():
        return
    source = (root / path).read_text(encoding="utf-8-sig", errors="replace")
    if cpp:
        # Track where each removed newline falls in the spliced source.
        splices, removed = [], 0
        for splice in SPLICE.finditer(source):
            splices.append(splice.start() - removed)
            removed += len(splice[0])
        source = SPLICE.sub("", source)

        def line_at(pos):
            return source.count("\n", 0, pos) + bisect_right(splices, pos) + 1

        # The standard-only install TU lives beside full consumer sources.
        if not forge or path in ABSENCE:
            # Keep literals opaque without treating them as directive whitespace.
            clean = CPP_TOKENS.sub(lambda m: blank(m) if m["comment"] else re.sub(r"[^\n]", "@", m[0]), source)
            for m in DIRECTIVE.finditer(clean):
                _, kind, body = m.groups()
                if (kind == "if" and path in ABSENCE and re.fullmatch(absence_guard(path), body)
                        and GUARD_ERROR.match(clean, m.end())):
                    continue
                operands = directive_headers(kind, body, source, m.start(3), m.end())
                if any(extension_header(root, path, operand) for operand in operands):
                    yield 1, line_at(m.start(1))
        if path.startswith(("backport/", "include/forge/")):
            tokens = CPP_TOKENS.sub(blank, source)
        if path.startswith("include/forge/"):
            private = [m.start() for m in STD.finditer(tokens) if private_std(tokens, m.end())]
            private.extend(m.start() for m in OTHER_PRIVATE.finditer(tokens))
            for pos in sorted(private):
                yield 2, line_at(pos)
        if path.startswith(("backport/", "include/forge/")):
            for m in SAL.finditer(tokens):
                yield 4, line_at(m.start())
    if cmake and not forge and path.startswith(("test/", "example/")):
        code = CMAKE_TOKENS.sub(lambda m: blank(m) if m["comment"] else m[0], source)
        shape = CMAKE_TOKENS.sub(blank, code)
        if path != OWNER_HELPER:
            setup = {m.start(1) for m in FIXTURE_SETUP.finditer(shape)} if path in CONFIG_FIXTURES else ()
            for m in ROOT_INPUT.finditer(code):
                if m.start() not in setup:
                    yield 3, code.count("\n", 0, m.start()) + 1
        yield from probe_violations(path, code, shape)


def self_test():
    with tempfile.TemporaryDirectory(prefix="ccforge-layering-", dir="/tmp") as scratch:
        def expect(path, source, *expected):
            file = Path(scratch) / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(source, encoding="utf-8")
            actual = tuple(violations(Path(scratch), path))
            if len(actual) != len(expected) or any(want not in (issue, issue[0])
                                                    for want, issue in zip(expected, actual)):
                sys.exit(f"[layering] self-test {path}: expected {expected}, got {actual}")

        expect("example/new.cpp", '#include "forge/x.hpp"\n', 1)
        expect("backport/execution", "# include_next /* header */ <forge/x.hpp>\n", 1)
        expect("test/new.cpp", '#include "../include/forge/execution.hpp"\n', 1)
        expect("test/new.cpp", "#include <../include/./forge/execution.hpp>\n", 1)
        expect("test/new.cpp", '#if __has_include /*comment*/ ("../include/forge/execution.hpp")\n#endif\n', 1)
        expect("test/new.cpp", '#define PROBE_TEXT "__has_include(<forge/x.hpp>)"\n'
               '#define RAW R"tag(\n#if __has_include(<forge/x.hpp>)\n)tag"\n'
               '#include "forge/../other.hpp"\n')
        expect("test/new.cpp", "#define A \\ \t\n 1\n"
               "#inc\\ \t\nlude <for\\\nge/x.hpp>\n"
               "#if __has_\\ \t\ninclude(<forge/x.hpp>)\n#endif\n", (1, 3), (1, 6))
        for glob in (*FORGE_PATHS, "example/forge_*"):
            expect(glob.replace("*", "new.cpp"), "#include <forge/x.hpp>\n")
        # These controls are fixed independently of the production allowlist.
        guards = {
            "test/consumer_hive.cpp": "#if defined(CCFORGE_CONSUMER_STD_ONLY) && __has_include(<forge/execution.hpp>)",
            "test/install_consumer/std_main.cpp": "#if __has_include(<forge/execution.hpp>) || __has_include(<forge/unique_resource.hpp>)",
            "test/std_target/compile/test_std_target.cpp": "#if __has_include(<forge/execution.hpp>) || __has_include(<forge/unique_resource.hpp>)",
        }
        pairs = {
            ("test/consumer_hive.cpp", "forge/execution.hpp"),
            ("test/install_consumer/std_main.cpp", "forge/execution.hpp"),
            ("test/install_consumer/std_main.cpp", "forge/unique_resource.hpp"),
            ("test/std_target/compile/test_std_target.cpp", "forge/execution.hpp"),
            ("test/std_target/compile/test_std_target.cpp", "forge/unique_resource.hpp"),
        }
        if set(guards) != set(ABSENCE) or pairs != {
                (path, header) for path, headers in ABSENCE.items() for header in headers}:
            sys.exit("[layering] self-test: unexpected absence allowlist")
        for path, guard in guards.items():
            expect(path, guard + "\n#error exposed\n#endif\n")
            expect(path, guard + "\n#endif\n", 1)
            expect(path, "#if __has_include(<forge/other.hpp>)\n#error exposed\n#endif\n", 1)
            expect("test/unlisted.cpp", guard + "\n#error exposed\n#endif\n", 1)
        for path, header in sorted(pairs):
            expect(path, f"#include <{header}>\n", 1)
            guard = f"#if __has_include(<{header}>)\n"
            expect(path, guard + "#endif\n", 1)
            expect("test/unlisted.cpp", guard + "#error exposed\n#endif\n", 1)

        expect("include/forge/new.hpp", "std :: execution :: __private x; namespace x = ::std; int x__forge_y;", 2, 2, 2)
        expect("include/forge/new.hpp", "using bad = std::vector<int>::__private;", 2)
        expect("include/forge/new.hpp", "using bad = std::vector<std::vector<int>>::__private;", 2)
        expect("include/forge/new.hpp", "using good = std::vector<std::vector<int>>::value_type;")
        expect("test/new.cmake", 'set(inc "${FORGE_INCLUDE_DIR}")', 3)
        expect("example/new.cmake", 'set(inc "${FORGE_INCLUDE_DIR}")', 3)
        for path in ("test/probe_fingerprint/test_configuration_inputs.cmake",
                     "test/submdspan/gate_fixture/CMakeLists.txt"):
            expect(path, 'set(FORGE_INCLUDE_DIR "fixture")\nset(inc "${FORGE_INCLUDE_DIR}")', (3, 2))
        for path in ("test/TestOwner.cmake", "test/forge/new.cmake",
                     "forge.cmake", "cmake/ForgeConfiguration.cmake", "cmake/ForgeExtensions.cmake"):
            expect(path, 'set(inc "${FORGE_INCLUDE_DIR}")')
        expect("backport/new.hpp", "int x = 1'000 + __in + 2'000; int __in_more; // __out\n"
               'char s[] = "__out";', 4)
        expect("include/forge/new.hpp", "int __allocator;", 4)
        expect("test/new.cmake", "Check_CXX_Source_Compiles (x Y)\ncheck_cxx_source_runs\n(x Z)", 5, 5)
        expect("example/CMakeLists.txt", "check_cxx_source_runs(x Y)", 5)
        expect("test/forge/new.cmake", "check_cxx_source_compiles(x Y)")
        expect("test/new.cmake", "cmake_language(CALL #comment\n check_cxx_source_compiles x Y)\n"
               'cmake_language(CALL "Check_CXX_Source_Runs" x Y)\n'
               "cmake_language(CALL [[_forge_check_prepared_cxx_source_compiles]] x Y)\n"
               "_forge_check_prepared_cxx_source_compiles(x Z)", (5, 1), (5, 3), (5, 4), (5, 5))
        expect("test/new.cmake", 'set(text "cmake_language(CALL check_cxx_source_compiles x Y)")')
        owner_helper = (ROOT / OWNER_HELPER).read_text(encoding="utf-8")
        expect(OWNER_HELPER, owner_helper + "\ncheck_cxx_source_runs(x Z)", 5)
        backend = ("function(_forge_check_prepared_cxx_source_compiles source result)\n"
                   "  # Bind the primitive even if the layout changes.\n"
                   "  Check_CXX_Source_Compiles (x Y)\nendfunction()\n")
        expect(OWNER_HELPER, backend)
        expect("test/new.cmake", backend, 5)
        expect(OWNER_HELPER, backend.replace("function", "macro"), 5)
        expect(OWNER_HELPER, backend.replace("Check_CXX_Source_Compiles", "check_cxx_source_runs"), 5)
        expect("test/CMakeLists.txt", "function(unapproved)\n"
               "  _forge_check_prepared_cxx_source_compiles(x Y)\nendfunction()", 5)
        file_probe = ("function({caller})\n"
                      "  _forge_detect_cxx_source_compiles(result probe.cpp)\n"
                      '  cmake_language(CALL "_forge_detect_cxx_source_compiles" result probe.cpp)\n'
                      "endfunction()\n")
        expect("test/CMakeLists.txt", file_probe.format(caller="unapproved"), (5, 2), (5, 3))
        for caller in ("forge_detect_cxx_source_compiles", "forge_expect_cxx_source_compiles"):
            expect("test/CMakeLists.txt", file_probe.format(caller=caller))
            expect("test/new.cmake", file_probe.format(caller=caller), (5, 2), (5, 3))
    print("[layering] self-test passed (ownership, rules 1–5 and all exceptions)")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif sys.argv[1:]:
        sys.exit("usage: check-layering.py [--self-test]")
    else:
        files = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=ROOT)
        issues = [(os.fsdecode(p), rule, line) for p in sorted(set(files.split(b"\0")) - {b""})
                  for rule, line in violations(ROOT, os.fsdecode(p))]
        for path, rule, line in issues:
            print(f"[layering] {path}:{line}: rule {rule} violated", file=sys.stderr)
        if issues:
            sys.exit(1)
        print("[layering] passed (tracked and non-ignored new files)")
