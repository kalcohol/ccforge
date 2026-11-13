#!/usr/bin/env bash
#
# audit-docs-and-examples.sh — lightweight documentation/example consistency
# checks.
#
# This script is intentionally small and local. It does not prove API
# correctness; it catches stale documentation references, leaked private paths,
# and example registration drift before those issues reach reviews.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

failures=0
mkdir -p "${REPO_ROOT}/build/review"
audit_tmp="$(mktemp -d "${REPO_ROOT}/build/review/doc-audit.XXXXXX")"
# Keep audit scratch output for inspection; no source-tree cleanup is needed.

log() {
    printf '[doc-audit] %s\n' "$*"
}

fail() {
    printf '[doc-audit] ERROR: %s\n' "$*" >&2
    failures=$((failures + 1))
}

check_no_private_paths() {
    log "check private host/path leaks"
    if rg -n 'px13|D:\\Program|/data/projects|/home/|C:\\Users' \
        docs README* scripts test \
        --glob '!scripts/audit-docs-and-examples.sh' \
        >"${audit_tmp}/private-paths"; then
        cat "${audit_tmp}/private-paths" >&2
        fail "private host/path pattern found in committed docs/scripts/tests"
    fi
}

check_example_refs_exist() {
    log "check documented example references exist"
    local refs
    refs="${audit_tmp}/example-refs"
    rg --no-filename -o 'example/[A-Za-z0-9_./+-]+\.(cpp|cc|cxx)' docs README*.md >"${refs}" || true
    while IFS= read -r ref; do
        [[ -z "${ref}" ]] && continue
        if [[ ! -f "${ref}" ]]; then
            fail "documented example does not exist: ${ref}"
        fi
    done < <(sort -u "${refs}")
}

check_all_examples_are_indexed() {
    log "check every example is indexed"
    local refs all_examples
    refs="${audit_tmp}/indexed-example-refs"
    all_examples="${audit_tmp}/all-indexed-examples"

    rg --no-filename -o 'example/[A-Za-z0-9_./+-]+\.(cpp|cc|cxx)' docs README*.md |
        sort -u >"${refs}" || true
    printf '%s\n' example/*.cpp example/*.cc | sort -u >"${all_examples}"

    while IFS= read -r src; do
        [[ -z "${src}" ]] && continue
        if ! grep -qxF "${src}" "${refs}"; then
            fail "example is not indexed in documentation: ${src}"
        fi
    done <"${all_examples}"

}

check_markdown_links() {
    log "check local markdown links"
    local links
    links="${audit_tmp}/markdown-links"
    rg -n -o '\[[^]]+\]\(([^)#][^)]*)\)' docs README*.md ROADMAP.md containers/README.md test --glob '*.md' --glob '*.txt' >"${links}" || true
    while IFS=: read -r file line match; do
        [[ -z "${match:-}" ]] && continue
        local target
        target="${match##*(}"
        target="${target%)}"
        target="${target%%#*}"
        [[ -z "${target}" ]] && continue
        [[ "${target}" =~ ^https?:// ]] && continue
        [[ "${target}" =~ ^mailto: ]] && continue

        local base resolved
        base="$(dirname "${file}")"
        if [[ "${target}" == /* ]]; then
            resolved=".${target}"
        else
            resolved="${base}/${target}"
        fi
        if [[ ! -e "${resolved}" ]]; then
            fail "${file}:${line}: local markdown link target missing: ${target}"
        fi
    done <"${links}"
}

check_forge_header_refs_exist() {
    log "check documented Forge header references"
    local headers
    headers="${audit_tmp}/forge-header-refs"
    rg --no-filename -o '<forge/[A-Za-z0-9_./+-]+\.hpp>' \
        docs README*.md example |
        tr -d '<>' | sort -u >"${headers}" || true

    while IFS= read -r header; do
        [[ -z "${header}" ]] && continue
        if [[ ! -f "include/${header}" ]]; then
            fail "documented Forge header does not exist: <${header}>"
        fi
    done <"${headers}"
}

check_example_cmake_sources() {
    log "check example CMake registrations"
    local registered all_examples
    registered="${audit_tmp}/registered-examples"
    all_examples="${audit_tmp}/all-examples"

    sed -n -E 's/.*forge_add_(standard_)?example\((([A-Z_]+) )?[^ ]* ([^ )]*\.(cpp|cc|cxx)).*/\4/p' \
        example/CMakeLists.txt | grep -v '[$]' >>"${registered}" || true

    awk '
        /foreach\(_forge_example_target IN ITEMS/ { in_items = 1; next }
        in_items && /^[[:space:]]*\)/ { in_items = 0; next }
        in_items {
            for (i = 1; i <= NF; ++i) {
                token = $i
                gsub(/[()]/, "", token)
                if (token ~ /^forge_[A-Za-z0-9_]+$/) {
                    print token "_example.cpp"
                }
            }
            if ($0 ~ /\)/) {
                in_items = 0
            }
        }
    ' example/CMakeLists.txt >>"${registered}"

    sed -n 's/.*PRIVATE \([^ )]*\.cc\).*/\1/p' example/CMakeLists.txt >>"${registered}"
    sort -u "${registered}" -o "${registered}"
    printf '%s\n' example/*.cpp example/*.cc | sed 's|^example/||' |
        sort -u >"${all_examples}"

    while IFS= read -r src; do
        [[ -z "${src}" ]] && continue
        if [[ ! -f "example/${src}" ]]; then
            fail "example/CMakeLists.txt registers missing source: ${src}"
        fi
    done <"${registered}"

    while IFS= read -r src; do
        [[ -z "${src}" ]] && continue
        if ! grep -qxF "${src}" "${registered}"; then
            fail "example source is not registered in example/CMakeLists.txt: ${src}"
        fi
    done <"${all_examples}"

}

check_stale_execution_names() {
    log "check stale std::execution extension names"
    if rg -n 'std::execution::(ensure_started|start_detached)' \
        docs README*.md >"${audit_tmp}/stale-execution-names"; then
        if rg -v 'Removed non-WD extension name|No longer exposed' \
            "${audit_tmp}/stale-execution-names" |
            rg -v '非 WD|已移除|不再.*暴露' >&2; then
            fail "stale std::execution extension reference found"
        fi
    fi
}

check_portable_sync_wait_spelling() {
    log "check portable sync_wait spelling"
    : >"${audit_tmp}/sync-wait-spelling"
    rg -n '[A-Za-z_][A-Za-z0-9_:]*::sync_wait(_with_variant)?' \
        docs README*.md example >>"${audit_tmp}/sync-wait-spelling" || true
    rg -n 'std::execution::sync_wait(_with_variant)?' \
        include >>"${audit_tmp}/sync-wait-spelling" || true
    if rg -v 'std::this_thread::sync_wait(_with_variant)?' \
        "${audit_tmp}/sync-wait-spelling" \
        >"${audit_tmp}/nonportable-sync-wait"; then
        cat "${audit_tmp}/nonportable-sync-wait" >&2
        fail "public docs/examples must use std::this_thread::sync_wait"
    fi
}

check_readme_parity() {
    log "check multilingual README structure"

    local readmes=(README.md README.zh-CN.md README.ja.md)
    local reference_features reference_fences reference_links current
    reference_features="${audit_tmp}/readme-features"
    reference_fences="${audit_tmp}/readme-fences"
    reference_links="${audit_tmp}/readme-links"
    current="${audit_tmp}/readme-current"

    if ! rg '^\| `[^`]+`' "${readmes[0]}" >"${current}"; then
        fail "${readmes[0]}: feature table is missing"
    fi
    sed -E 's/^\| (`[^`]+`).*/\1/' "${current}" >"${reference_features}"
    if ! rg '^```' "${readmes[0]}" >"${reference_fences}"; then
        fail "${readmes[0]}: fenced code blocks are missing"
    fi
    { rg --no-filename -o '\]\([^)]*\)' "${readmes[0]}" || true; } |
        sed -E 's/^\]\((.*)\)$/\1/' |
        { grep -Ev '^README(\.zh-CN|\.ja)?\.md$' || true; } |
        sort -u >"${reference_links}"

    for readme in "${readmes[@]:1}"; do
        if ! rg '^\| `[^`]+`' "${readme}" >"${current}.raw"; then
            fail "${readme}: feature table is missing"
        fi
        sed -E 's/^\| (`[^`]+`).*/\1/' "${current}.raw" >"${current}"
        if ! cmp -s "${reference_features}" "${current}"; then
            fail "${readme}: feature table keys differ from README.md"
        fi

        if ! rg '^```' "${readme}" >"${current}"; then
            fail "${readme}: fenced code blocks are missing"
        fi
        if ! cmp -s "${reference_fences}" "${current}"; then
            fail "${readme}: fenced code block structure differs from README.md"
        fi

        { rg --no-filename -o '\]\([^)]*\)' "${readme}" || true; } |
            sed -E 's/^\]\((.*)\)$/\1/' |
            { grep -Ev '^README(\.zh-CN|\.ja)?\.md$' || true; } |
            sort -u >"${current}"
        if ! cmp -s "${reference_links}" "${current}"; then
            fail "${readme}: local documentation links differ from README.md"
        fi
    done

    local marker
    for marker in \
            'C++23' \
            'CMake 3.20' \
            'find_package(CCForge CONFIG REQUIRED)' \
            'forge::forge' \
            'forge::std' \
            'include(/path/to/ccforge/forge.cmake)' \
            'add_subdirectory(ccforge)' \
            'GoogleTest' \
            '3rdparty/googletest' \
            'scripts/verify-native.sh' \
            'scripts/verify-install-package.sh'; do
        for readme in "${readmes[@]}"; do
            if ! grep -qF "${marker}" "${readme}"; then
                fail "${readme}: missing shared README marker: ${marker}"
            fi
        done
    done

}

check_test_switch_docs() {
    log "check documented test switches"

    local declared documented
    declared="${audit_tmp}/declared-test-options"
    documented="${audit_tmp}/documented-test-options"

    sed -n 's/^option(\(FORGE_TEST_ENABLE_[A-Z_]*\).*/\1/p' \
        test/CMakeLists.txt | sort -u >"${declared}"
    { rg --no-filename -o 'FORGE_TEST_ENABLE_[A-Z_]*[A-Z]' docs/testing.md docs/install-options.md || true; } |
        sort -u >"${documented}"

    if ! cmp -s "${declared}" "${documented}"; then
        diff -u "${declared}" "${documented}" >&2 || true
        fail "docs/testing.md test switches differ from test/CMakeLists.txt"
    fi

}

check_feature_gate_docs() {
    log "check documented Forge feature gates"

    local declared documented
    declared="${audit_tmp}/declared-feature-options"
    documented="${audit_tmp}/documented-feature-options"

    sed -n -E \
        's/^(option|_forge_define_tristate_option)\((FORGE_ENABLE_FORGE_[A-Z_]+).*/\2/p' \
        forge.cmake | sort -u >"${declared}"
    { rg --no-filename -o 'FORGE_ENABLE_FORGE_[A-Z_]+' docs/testing.md docs/install-options.md || true; } |
        sort -u >"${documented}"

    if ! cmp -s "${declared}" "${documented}"; then
        diff -u "${declared}" "${documented}" >&2 || true
        fail "docs/testing.md Forge feature gates differ from forge.cmake"
    fi

}


check_governance_guards() {
    log "check governance guards and positive/negative self-tests"
    if ! python3 - <<'PYGUARDS'
from pathlib import Path
import re
import subprocess
import posixpath
import fnmatch
import tempfile

errors = []
def require(ok, message):
    if not ok: errors.append(message)
def force_ok(flag, native, page):
    return flag in native and flag in page
def macro_ok(macro, page):
    return macro in page
def example_ok(name, page):
    return bool(re.search(r'\]\([^)]*example/' + re.escape(name) + r'\)', page))
def repository_inventory():
    return set(subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z']).decode().split('\0')) - {''}
inventory = repository_inventory()
tracked_roots = {x.split('/')[0] for x in subprocess.check_output(['git', 'ls-files', '-z']).decode().split('\0') if '/' in x}
# This text guard checks links and inline paths rooted in tracked top-level
# directories, regardless of suffix. Unknown roots and prose outside backticks
# can escape it; URLs, system paths, headers and inline build outputs are exempt.
# Inventory includes untracked nonignored files so staging does not affect results.
def path_ok(text, tracked=None, page='docs/guide.md'):
    tracked = inventory if tracked is None else tracked
    if re.search(r'build/review/|/data/|/home/|\.agents/|\.codex/', text):
        return False
    def source_ok(ref):
        ref = ref.split('#')[0]
        if ref.startswith('/tmp/'): return False
        if not ref or re.match(r'\w+://|mailto:', ref): return True
        candidates = {posixpath.normpath(ref), posixpath.normpath(posixpath.join(posixpath.dirname(page), ref))}
        if ref.startswith('forge/'): candidates.add('include/' + ref)
        return any(c in tracked or any(x.startswith(c.rstrip('/') + '/') or fnmatch.fnmatchcase(x, c) for x in tracked) for c in candidates)
    for ref in re.findall(r'\[[^]]+\]\(([^)]+)\)', text):
        if not source_ok(ref): return False
    roots = tracked_roots if tracked is inventory else {x.split('/')[0] for x in tracked if '/' in x}
    for match in re.finditer(r'(?<!`)`([^`\n]+)`(?!`)', text):
        span = match[1]
        if '/tmp/' in span: return False
        if span.startswith('<') and span.endswith('>'): continue
        prefix = text[text.rfind('\n', 0, match.start()) + 1:match.start()]
        if re.search(r'(?:不会添加|没有)\s*$', prefix): continue
        for ref in re.findall(r'(?<![\w:])/?(?:[.\w*+-]+/)+[\w.*+/-]+', span):
            # Unknown roots may be C++ expressions or API name pairs.
            if '://' in span or ref.startswith(('/usr/', '/etc/', '/dev/')): continue
            if ref.split('/')[0] not in roots | {'build'}: continue
            if ref.startswith('build/'): continue
            if not source_ok(ref): return False
    return True
def status_ok(text, status):
    return bool(re.search(r'^> 状态：' + status + r'；最后核对：[^\n]+；取代关系：', text, re.M))
# Conservative text guard: reject only obvious numeric shapes in policy-free
# calls. Unknown expressions (including policy variables), template syntax and
# sender variables can escape detection; this is not a C++ type/parser check.
def spelling_ok(text):
    def policy_guard(kind, expr):
        macro = 'FORGE_HAS_NATIVE_EXECUTION_POLICIES'
        if not re.search(r'\b' + macro + r'\b', expr): return None
        compact = re.sub(r'\s+', '', expr)
        if kind in {'ifdef', 'ifndef'} and compact == macro:
            return kind == 'ifdef'
        if kind == 'if':
            if compact == '!' + macro: return False
            native = r'(?:' + macro + r'|defined\(' + macro + r'\))'
            if re.fullmatch(native + r'(?:&&defined\(__cpp_lib_execution\))?', compact):
                return True
        # Convergence rule: accept uncertain policy guards rather than reject
        # legitimate compatibility calls. Unrelated guards grant no exemption.
        return 'unknown'
    # Preserve offsets and newlines while removing comments and string literals.
    clean = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',
                   lambda m: re.sub(r'[^\n]', ' ', m[0]), text, flags=re.S)
    if re.search(r'(?:std::execution|ex)::(?:empty_env|make_env|make_prop)\b', clean): return False
    branches = []
    allowed = [False] * (len(clean) + 1)
    offset = 0
    for line in clean.splitlines(True):
        directive = re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', line)
        if directive:
            kind, expr = directive.groups()
            expr = expr.strip()
            if kind in {'if', 'ifdef', 'ifndef'}:
                branches.append(policy_guard(kind, expr))
            elif kind == 'else' and branches:
                if isinstance(branches[-1], bool): branches[-1] = not branches[-1]
            elif kind == 'elif' and branches:
                # Prior alternatives also constrain this branch; do not guess.
                branches[-1] = 'unknown' if branches[-1] is not None or policy_guard('if', expr) is not None else None
            elif kind == 'endif' and branches: branches.pop()
        allowed[offset:offset+len(line)] = [False in branches or 'unknown' in branches] * len(line)
        offset += len(line)
    shape = re.compile(r'\s*[0-9]+[uUlL]*\s*$')
    for call in re.finditer(r'(?:ex|std::execution)::bulk\s*\(', clean):
        args, start, stack = [], call.end(), []
        for i in range(start, len(clean)):
            ch = clean[i]
            if ch in '([{': stack.append(ch)
            elif ch in ')]}':
                if not stack:
                    args.append(clean[start:i]); break
                stack.pop()
            elif ch == ',' and not stack:
                args.append(clean[start:i]); start = i + 1
        # Angle brackets can contain commas or comparisons: do not guess.
        if any('<' in arg or '>' in arg for arg in args): continue
        policy_free = (len(args) == 2 and shape.fullmatch(args[0])) or (
            len(args) == 3 and re.fullmatch(r'\s*(?:ex|std::execution)::just\([^)]*\)\s*', args[0])
            and shape.fullmatch(args[1]))
        if policy_free and not allowed[call.start()]: return False
    return True
def link_ok(target, inventory):
    return target in inventory
def header_ok(text):
    return all(re.search(r'^// '+key+r': .+', text, re.M) for key in ['Demonstrates','Key APIs','Requires','See also'])

# Each predicate gets one passing and one rejecting fixture.
pairs = [
 ('force', force_ok('FORCE', 'FORCE', 'FORCE'), force_ok('FORCE', 'FORCE', '')),
 ('macro', macro_ok('__cpp_lib_x', '__cpp_lib_x = 1'), macro_ok('__cpp_lib_x', '')),
 ('examples', example_ok('demo.cc', '[demo](../../example/demo.cc)'), example_ok('demo.cc', '')),
 ('paths', path_ok('[guide](testing.md)', {'docs/testing.md'}), path_ok('`build/review/log`', set())),
 ('status', status_ok('> 状态：done；最后核对：2026-11-13；取代关系：无。','done'), status_ok('> 状态：active；最后核对：today；取代关系：无。','done')),
 ('spellings', spelling_ok('ex::bulk(ex::seq, 5, fn);'), spelling_ok('ex::bulk(5, fn);')),
 ('links', link_ok('example/demo.cc', {'example/demo.cc'}), link_ok('missing.txt', {'example/demo.cc'})),
 ('headers', header_ok('// Demonstrates: demo\n// Key APIs: api\n// Requires: C++23\n// See also: docs/x.md'), header_ok('// Demonstrates: demo')),
]
for name, positive, negative in pairs:
    require(positive and not negative, name + ': self-test failed')
    print('[doc-audit] self-test ' + name + ': positive PASS, negative PASS')

# Additional regression fixtures cover generated outputs versus private files,
# both bulk overloads, multiline arguments and nested preprocessor branches.
for text, expected in [
    ('`build/llvm/example/demo`', True),
    ('`build/review/log.txt`', False),
    ('`scripts/untracked.md`', False),
    ('`scripts/untracked.unusual-suffix`', False),
    ('[private](../notes/untracked.md)', False),
    ('`scripts/verify-native.sh`', True),
    ('[output](../build/llvm/log.txt)', False),
]:
    require(path_ok(text, {'scripts/verify-native.sh'}) == expected, 'path regression: ' + text)
for text, expected in [
    ('ex::bulk(sender, ex::par, 5, fn)', True),
    ('ex::bulk(policy, 5, fn)', True),
    ('ex::bulk(sender, policy, 5, fn)', True),
    ('ex::bulk(make_policy<A, B>(), 5, fn)', True),
    ('ex::bulk(sender, make_policy<A, B>(), 5, fn)', True),
    ('ex::bulk(5, make_fn<A, B>())', True),
    ('ex::bulk(ex::just(1), 5, fn)', False),
    ('ex::bulk(unknown_shape, fn)', True),
    ('#if FORGE_HAS_NATIVE_EXECUTION_POLICIES && defined(__cpp_lib_execution)\nex::bulk(ex::seq, 5, fn);\n#else\nex::bulk(5, fn);\n#endif', True),
    ('ex::bulk(\n ex::seq,\n 5, fn)', True),
    ('std::execution::bulk(\n ex::just(1),\n std::execution::par,\n 5, fn)', True),
    ('#if FORGE_HAS_NATIVE_EXECUTION_POLICIES\nex::bulk(ex::seq, 5, fn);\n#else\n#if OTHER\n#else\nex::bulk(5, fn);\n#endif\n#endif', True),
    ('#if FORGE_HAS_NATIVE_EXECUTION_POLICIES\n#endif\n#if OTHER\n#else\nex::bulk(5, fn);\n#endif', False),
    ('#if FORGE_HAS_NATIVE_EXECUTION_POLICIES\nex::bulk(5, fn);\n#endif', False),
    ('#if FORGE_HAS_NATIVE_EXECUTION_POLICIES\n#else\n#endif\nex::bulk(5, fn);', False),
]:
    require(spelling_ok(text) == expected, 'bulk regression: ' + text)
# Equivalent compatibility guards, with unrelated nested levels on either side.
for guard, compatibility_first in [
    ('#ifndef FORGE_HAS_NATIVE_EXECUTION_POLICIES', True),
    ('#if !FORGE_HAS_NATIVE_EXECUTION_POLICIES', True),
    ('#if defined(FORGE_HAS_NATIVE_EXECUTION_POLICIES) && defined(__cpp_lib_execution)', False),
    ('#if FORGE_HAS_NATIVE_EXECUTION_POLICIES && defined(__cpp_lib_execution)', False),
]:
    for nested in (False, True):
        compatibility = 'ex::bulk(5, fn);\nstd::execution::bulk(ex::just(1), 5, fn);'
        if nested: compatibility = '#if INNER\n#else\n' + compatibility + '\n#endif'
        native = 'ex::bulk(ex::seq, 5, fn);'
        first, second = (compatibility, native) if compatibility_first else (native, compatibility)
        text = guard + '\n' + first + '\n#else\n' + second + '\n#endif'
        if nested: text = '#if OUTER\n' + text + '\n#endif'
        require(spelling_ok(text), 'equivalent bulk fallback: ' + text)
        # The opposite branch and calls after #endif must still be checked.
        wrong = guard + '\n' + (native if compatibility_first else compatibility) + '\n#else\n' + (compatibility if compatibility_first else native) + '\n#endif'
        require(not spelling_ok(wrong), 'native bulk branch must reject: ' + wrong)
        require(not spelling_ok(text + '\nex::bulk(5, fn);'), 'bulk fallback scope leaked')
for text in [
    '#if (FORGE_HAS_NATIVE_EXECUTION_POLICIES && OTHER)\nex::bulk(5, fn);\n#else\nex::bulk(5, fn);\n#endif',
    '#if FORGE_HAS_NATIVE_EXECUTION_POLICIES\n#elif OTHER\nex::bulk(5, fn);\n#else\nex::bulk(5, fn);\n#endif',
]:
    require(spelling_ok(text), 'uncertain policy guard must accept: ' + text)
print('[doc-audit] path and bulk regression fixtures checked')

# Simulate ls-files output before staging: a new nonignored file is usable.
from unittest.mock import patch
with patch('subprocess.check_output', return_value=b'docs/tracked.md\0docs/new.odd\0') as listing:
    unstaged = repository_inventory()
    listing.assert_called_once_with(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'])
    require(path_ok('[new](new.odd)', unstaged), 'untracked nonignored inventory fixture failed')
    require(not path_ok('[ignored](ignored.odd)', unstaged), 'ignored inventory fixture failed')
print('[doc-audit] unstaged inventory fixture: PASS')

# Execute the actual startup block against a checkout with no build directory.
startup = Path('scripts/audit-docs-and-examples.sh').read_text().split('failures=0\n', 1)[1].split('\nlog() {', 1)[0]
with tempfile.TemporaryDirectory(dir='build/review') as fresh:
    result = subprocess.run(['bash', '-c', 'set -eu\nREPO_ROOT="$1"\n' + startup, 'audit-startup', str(Path(fresh).resolve())], capture_output=True, text=True)
    require(result.returncode == 0 and bool(list(Path(fresh).glob('build/review/doc-audit.*'))), 'fresh-checkout startup failed: ' + result.stderr)
print('[doc-audit] fresh-checkout startup: PASS')

pages = {k: Path('docs/backports/'+v+'.md').read_text() for k,v in {
 'SIMD':'simd','SENDERS':'execution','CONSTANT_WRAPPER':'mdspan',
 'MDSPAN_PADDED_LAYOUTS':'mdspan','SUBMDSPAN':'mdspan','LINALG':'linalg','HIVE':'hive'}.items()}
native = Path('docs/native-handoff.md').read_text()
for suffix in re.findall(r'option\(FORGE_FORCE_(\w+)_BACKPORT', Path('cmake/ForgeBackportProbes.cmake').read_text()):
    flag='FORGE_FORCE_'+suffix+'_BACKPORT'
    require(force_ok(flag,native,pages.get(suffix,'')), flag+': missing documentation')
for p in Path('backport').rglob('*'):
    if not p.is_file(): continue
    for macro in re.findall(r'^\s*#\s*define\s+(__cpp_lib_\w+)', p.read_text(), re.M):
        suffix='SIMD' if 'simd' in macro else 'CONSTANT_WRAPPER' if 'constant_wrapper' in macro else 'SUBMDSPAN' if 'submdspan' in macro else 'HIVE'
        require(macro_ok(macro,pages[suffix]), str(p)+': '+macro+' missing')
# Example ownership comes from the public standard-example registrations.
cmake=Path('example/CMakeLists.txt').read_text()
for suffix, target, src in re.findall(r'forge_add_standard_example\((\w+) (\w+) ([\w.]+)', cmake):
    require(example_ok(src,pages[suffix]), src+': missing backport-page link')
for p in Path('docs').rglob('*.md'):
    require(path_ok(p.read_text(), page=str(p)), str(p)+': private/untracked path')
index=Path('docs/roadmap/README.md').read_text()
for p in Path('docs/roadmap').glob('*.md'):
    if p.name=='README.md':
        require(status_ok(p.read_text(),'active'),str(p)+': missing index status')
        continue
    row=re.search(r'\]\('+re.escape(p.name)+r'\) \| [^|]+ \| (\w+) \|',index)
    require(bool(row) and status_ok(p.read_text(),row[1]),str(p)+': status/index mismatch')
for p in Path('example').iterdir():
    if p.suffix not in {'.cpp','.cc','.cxx'}: continue
    require(spelling_ok(p.read_text()),str(p)+': nonportable env/bulk')
    # unique_resource is explicitly frozen by the owner, including its example.
    if p.name!='unique_resource_example.cpp':
        require(header_ok(p.read_text()),str(p)+': missing purpose header')
# Same link predicate covers ROADMAP, containers, test text and non-.cpp sources.
files=list(Path('docs').rglob('*.md'))+list(Path('.').glob('README*.md'))+[Path('ROADMAP.md'),Path('containers/README.md')]+list(Path('test').rglob('*.txt'))
for p in files:
    for target in re.findall(r'\[[^]]+\]\(([^)]+)\)',p.read_text()):
        target=target.split('#')[0]
        if not target or re.match(r'\w+://|mailto:',target): continue
        resolved=p.parent/target
        require(link_ok(str(resolved), {str(resolved)} if resolved.exists() else set()),str(p)+': broken link '+target)
if errors:
    for e in errors: print('[doc-audit] ERROR: '+e)
    raise SystemExit(1)
PYGUARDS
    then
        fail "governance guards failed"
    fi
}

check_no_private_paths
check_example_refs_exist
check_all_examples_are_indexed
check_markdown_links
check_forge_header_refs_exist
check_example_cmake_sources
check_stale_execution_names
check_portable_sync_wait_spelling
check_readme_parity
check_test_switch_docs
check_feature_gate_docs
check_governance_guards

if [[ "${failures}" -ne 0 ]]; then
    fail "${failures} audit check(s) failed"
    exit 1
fi

log "audit passed"
