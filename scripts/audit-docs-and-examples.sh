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
audit_tmp="$(mktemp -d -t ccforge-doc-audit.XXXXXX)"
trap 'rm -rf -- "${audit_tmp}"' EXIT

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
        >"${audit_tmp}/private-paths"; then
        cat "${audit_tmp}/private-paths" >&2
        fail "private host/path pattern found in committed docs/scripts"
    fi
}

check_example_refs_exist() {
    log "check documented example references exist"
    local refs
    refs="${audit_tmp}/example-refs"
    rg --no-filename -o 'example/[A-Za-z0-9_./+-]+\.cpp' docs README*.md >"${refs}" || true
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

    rg --no-filename -o 'example/[A-Za-z0-9_./+-]+\.cpp' docs README*.md |
        sort -u >"${refs}" || true
    printf '%s\n' example/*.cpp | sort -u >"${all_examples}"

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
    printf '%s\n' example/*.cpp | sed 's|^example/||' |
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
    { rg --no-filename -o 'FORGE_TEST_ENABLE_[A-Z_]*[A-Z]' docs/testing.md || true; } |
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
        's/^(option|_forge_define_tristate_option)\((FORGE_ENABLE_(FORGE_)?[A-Z_]+).*/\2/p' \
        forge.cmake | sort -u >"${declared}"
    { rg --no-filename -o 'FORGE_ENABLE_(FORGE_)?[A-Z_]+' docs/testing.md || true; } |
        sort -u >"${documented}"

    if ! cmp -s "${declared}" "${documented}"; then
        diff -u "${declared}" "${documented}" >&2 || true
        fail "docs/testing.md Forge feature gates differ from forge.cmake"
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

if [[ "${failures}" -ne 0 ]]; then
    fail "${failures} audit check(s) failed"
    exit 1
fi

log "audit passed"
