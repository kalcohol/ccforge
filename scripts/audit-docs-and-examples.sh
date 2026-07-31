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
        docs README* scripts --glob '!scripts/audit-docs-and-examples.sh' \
        >/tmp/ccforge-doc-audit-private.$$; then
        cat /tmp/ccforge-doc-audit-private.$$ >&2
        fail "private host/path pattern found in committed docs/scripts"
    fi
    rm -f /tmp/ccforge-doc-audit-private.$$
}

check_example_refs_exist() {
    log "check documented example references exist"
    local refs
    refs="$(mktemp -t ccforge-doc-audit-examples.XXXXXX)"
    rg --no-filename -o 'example/[A-Za-z0-9_./+-]+\.cpp' docs README*.md >"${refs}" || true
    while IFS= read -r ref; do
        [[ -z "${ref}" ]] && continue
        if [[ ! -f "${ref}" ]]; then
            fail "documented example does not exist: ${ref}"
        fi
    done < <(sort -u "${refs}")
    rm -f "${refs}"
}

check_all_examples_are_indexed() {
    log "check every example is indexed"
    local refs all_examples
    refs="$(mktemp -t ccforge-doc-audit-indexed-examples.XXXXXX)"
    all_examples="$(mktemp -t ccforge-doc-audit-all-indexed-examples.XXXXXX)"

    rg --no-filename -o 'example/[A-Za-z0-9_./+-]+\.cpp' docs README*.md |
        sort -u >"${refs}" || true
    find example -maxdepth 1 -type f -name '*.cpp' -printf 'example/%f\n' |
        sort -u >"${all_examples}"

    while IFS= read -r src; do
        [[ -z "${src}" ]] && continue
        if ! grep -qxF "${src}" "${refs}"; then
            fail "example is not indexed in documentation: ${src}"
        fi
    done <"${all_examples}"

    rm -f "${refs}" "${all_examples}"
}

check_markdown_links() {
    log "check local markdown links"
    local links
    links="$(mktemp -t ccforge-doc-audit-links.XXXXXX)"
    rg -n -o '\[[^]]+\]\(([^)#][^)]*)\)' docs README*.md >"${links}" || true
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
    rm -f "${links}"
}

check_example_cmake_sources() {
    log "check example CMake registrations"
    local registered all_examples
    registered="$(mktemp -t ccforge-doc-audit-registered.XXXXXX)"
    all_examples="$(mktemp -t ccforge-doc-audit-all-examples.XXXXXX)"

    sed -n 's/.*forge_add_example([^ ]* \([^ )]*\.cpp\).*/\1/p' \
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

    sort -u "${registered}" -o "${registered}"
    find example -maxdepth 1 -type f -name '*.cpp' -printf '%f\n' | sort -u >"${all_examples}"

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

    rm -f "${registered}" "${all_examples}"
}

check_stale_execution_names() {
    log "check stale std::execution extension names"
    if rg -n 'std::execution::(ensure_started|start_detached)' \
        docs README*.md >/tmp/ccforge-doc-audit-exec.$$; then
        if rg -v 'Removed non-WD extension name|No longer exposed' \
            /tmp/ccforge-doc-audit-exec.$$ |
            rg -v '非 WD|已移除|不再.*暴露' >&2; then
            fail "stale std::execution extension reference found"
        fi
    fi
    rm -f /tmp/ccforge-doc-audit-exec.$$
}

check_portable_sync_wait_spelling() {
    log "check portable sync_wait spelling"
    if rg -n 'std::execution::sync_wait(_with_variant)?' \
        docs README*.md example include >/tmp/ccforge-doc-audit-sync-wait.$$; then
        cat /tmp/ccforge-doc-audit-sync-wait.$$ >&2
        fail "public docs/examples must use std::this_thread::sync_wait"
    fi
    rm -f /tmp/ccforge-doc-audit-sync-wait.$$
}

check_readme_parity() {
    log "check multilingual README structure"

    local readmes=(README.md README.zh-CN.md README.ja.md)
    local reference_features reference_fences reference_links current
    reference_features="$(mktemp -t ccforge-doc-audit-readme-features.XXXXXX)"
    reference_fences="$(mktemp -t ccforge-doc-audit-readme-fences.XXXXXX)"
    reference_links="$(mktemp -t ccforge-doc-audit-readme-links.XXXXXX)"
    current="$(mktemp -t ccforge-doc-audit-readme-current.XXXXXX)"

    rg '^\| `[^`]+`' "${readmes[0]}" |
        sed -E 's/^\| (`[^`]+`).*/\1/' >"${reference_features}"
    rg '^```' "${readmes[0]}" >"${reference_fences}"
    rg --no-filename -o '\]\([^)]*\)' "${readmes[0]}" |
        sed -E 's/^\]\((.*)\)$/\1/' |
        grep -Ev '^README(\.zh-CN|\.ja)?\.md$' |
        sort -u >"${reference_links}"

    for readme in "${readmes[@]:1}"; do
        rg '^\| `[^`]+`' "${readme}" |
            sed -E 's/^\| (`[^`]+`).*/\1/' >"${current}"
        if ! cmp -s "${reference_features}" "${current}"; then
            fail "${readme}: feature table keys differ from README.md"
        fi

        rg '^```' "${readme}" >"${current}"
        if ! cmp -s "${reference_fences}" "${current}"; then
            fail "${readme}: fenced code block structure differs from README.md"
        fi

        rg --no-filename -o '\]\([^)]*\)' "${readme}" |
            sed -E 's/^\]\((.*)\)$/\1/' |
            grep -Ev '^README(\.zh-CN|\.ja)?\.md$' |
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

    rm -f "${reference_features}" "${reference_fences}" \
        "${reference_links}" "${current}"
}

check_test_switch_docs() {
    log "check documented test switches"

    local declared documented
    declared="$(mktemp -t ccforge-doc-audit-test-options.XXXXXX)"
    documented="$(mktemp -t ccforge-doc-audit-test-docs.XXXXXX)"

    sed -n 's/^option(\(FORGE_TEST_ENABLE_[A-Z_]*\).*/\1/p' \
        test/CMakeLists.txt | sort -u >"${declared}"
    rg --no-filename -o 'FORGE_TEST_ENABLE_[A-Z_]*[A-Z]' docs/testing.md |
        sort -u >"${documented}"

    if ! cmp -s "${declared}" "${documented}"; then
        diff -u "${declared}" "${documented}" >&2 || true
        fail "docs/testing.md test switches differ from test/CMakeLists.txt"
    fi

    rm -f "${declared}" "${documented}"
}

check_feature_gate_docs() {
    log "check documented Forge feature gates"

    local declared documented
    declared="$(mktemp -t ccforge-doc-audit-feature-options.XXXXXX)"
    documented="$(mktemp -t ccforge-doc-audit-feature-docs.XXXXXX)"

    sed -n -E \
        's/^(option|_forge_define_tristate_option)\((FORGE_ENABLE_FORGE_[A-Z_]+).*/\2/p' \
        forge.cmake | sort -u >"${declared}"
    rg --no-filename -o 'FORGE_ENABLE_FORGE_[A-Z_]+' docs/testing.md |
        sort -u >"${documented}"

    if ! cmp -s "${declared}" "${documented}"; then
        diff -u "${declared}" "${documented}" >&2 || true
        fail "docs/testing.md Forge feature gates differ from forge.cmake"
    fi

    rm -f "${declared}" "${documented}"
}

check_no_private_paths
check_example_refs_exist
check_all_examples_are_indexed
check_markdown_links
check_example_cmake_sources
check_stale_execution_names
check_portable_sync_wait_spelling
check_readme_parity
check_test_switch_docs
check_feature_gate_docs

if [[ "${failures}" -ne 0 ]]; then
    fail "${failures} audit check(s) failed"
    exit 1
fi

log "audit passed"
