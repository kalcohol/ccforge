#!/usr/bin/env python3
r"""Index source slices of cplusplus/draft LaTeX without running TeX.

Input is UTF-8 with LF line endings. Each requested \rSecN[label] ends at
the very next section marker, including a child section. Within it, count
literal \pnum commands from 1. Record ordinary text and each spec-element
command separately, keeping the command itself in the element's hash input.
A new paragraph, spec element, \index* directive, \begin{itemdecl},
\end{itemdescr}, or section ends the previous slice. Structural commands
are at the start of a line, with optional indentation. Skip standalone
index directive lines without ending the paragraph. Following text or
spec elements start new slices under the same ordinal. Index and
declaration material does not create rows. A codeblock inside a slice is
part of that slice, including its delimiters. A leading codeblock before
the first paragraph is a synopsis (ordinal 0), including its delimiters.

For execution.syn the synopsis is the three Appendix A slices, in order
(ordinal 0 for each). Each facility starts at its // \ref{...} comment
and ends at the next blank-line-separated facility comment at the same
indentation, a dedent out of its namespace, or the end of the codeblock.
Blank lines inside a facility, including the one before with_error, are
retained. Join the parallel and replacement namespace facilities with the
separator following the former. Appendix A scopes execution.syn to those
slices only.

Strip whitespace only at the ends of each slice, then SHA-256 the exact
UTF-8 bytes, including all interior spaces and LF. TSV starts with
clause, para, element, sha256. Repeated keys (notably the three synopsis
rows) are distinguished in --check by their occurrence within the key.
Requested labels retain their command-line order. Check mode compares
keyed rows, not ordering between distinct keys. A requested label absent
from the source has zero current rows if the baseline contains it.
--file-hash prints the final source hash to stderr, leaving stdout as TSV.

For an overlay, apply each selected file's unified hunks in input order
against the source bytes as they then stand. Match old text, including all
context, at exactly one line boundary, never by a line number or fuzz.
Select one modification of the input file's basename per diff, using
unquoted unified-diff paths. Hunks require nonempty old text to anchor them.
"""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile


HEADER = "clause\tpara\telement\tsha256"
ELEMENTS = (
    "constraints mandates expects hardexpects effects sync ensures returns "
    "throws complexity remarks errors recommended"
).split()
SECTION = re.compile(r"^[ \t]*\\rSec[0-9]+\[([^\]\s]+)\]", re.MULTILINE)
COMMAND = re.compile(
    r"^[ \t]*(\\(?:pnum|" + "|".join(ELEMENTS)
    + r")(?![A-Za-z])|\\index[A-Za-z]*[^\n]*|\\(?:begin\{(?:itemdecl|codeblock)\}"
    + r"|end\{(?:itemdecl|itemdescr|codeblock)\}))",
    re.MULTILINE,
)
HUNK = re.compile(rb"@@ -[0-9]+(?:,([0-9]+))? \+[0-9]+(?:,([0-9]+))? @@[^\n]*\n?")
NO_NEWLINE = b"\\ No newline at end of file"
SYNOPSIS_REFS = (
    ("exec.task.scheduler",),
    ("exec.task",),
    ("exec.par.scheduler", "exec.parschedrepl"),
)
FACILITY_COMMENT = re.compile(r"^([ \t]*)//[ \t]*\\ref\{([^}\s]+)\}")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_lf(path):
    data = path.read_bytes()
    if b"\r" in data:
        raise ValueError(f"{path}: expected LF line endings")
    return data


def diff_name(header):
    path = header[4:].rstrip(b"\n").split(b"\t", 1)[0]
    if path.startswith(b'"') or not path:
        raise ValueError("expected an unquoted unified-diff path")
    return path.rsplit(b"/", 1)[-1].decode("utf-8")


def read_hunk(lines, i, header):
    old_left, new_left = (int(count or b"1") for count in header.groups())
    old, new = [], []
    while old_left or new_left:
        if i == len(lines) or not lines[i].startswith((b" ", b"-", b"+")):
            raise ValueError("truncated unified-diff hunk")
        line = lines[i]
        i += 1
        body = line[1:]
        if i < len(lines) and lines[i].rstrip(b"\n") == NO_NEWLINE:
            body = body.removesuffix(b"\n")
            i += 1
        if line[:1] != b"+":
            old.append(body)
            old_left -= 1
        if line[:1] != b"-":
            new.append(body)
            new_left -= 1
        if old_left < 0 or new_left < 0:
            raise ValueError("incorrect unified-diff hunk counts")
    if i < len(lines) and lines[i].startswith((b" ", b"+", b"-", b"\\")):
        if not lines[i].startswith(b"--- "):
            raise ValueError("extra unified-diff hunk lines")
    return i, b"".join(old), b"".join(new)


def overlay(source, name, path):
    lines = read_lf(path).splitlines(keepends=True)
    i = applied = 0
    selected = seen = False
    while i < len(lines):
        line = lines[i]
        i += 1
        if line.startswith(b"diff --git "):
            selected = False
        elif line.startswith(b"--- "):
            if i == len(lines) or not lines[i].startswith(b"+++ "):
                raise ValueError(f"{path}: missing +++ file header")
            before, after = diff_name(line), diff_name(lines[i])
            i += 1
            selected = name in (before, after)
            if selected:
                if before != name or after != name or seen:
                    raise ValueError(f"{path}: expected one modification of {name}")
                seen = True
        elif line.startswith(b"@@"):
            header = HUNK.fullmatch(line)
            if not header:
                raise ValueError(f"{path}:{i}: invalid unified-diff hunk header")
            line_number = i
            i, old, new = read_hunk(lines, i, header)
            if not selected:
                continue
            if not old:
                raise ValueError(f"{path}:{line_number}: hunk has no old-text anchor")
            positions = []
            pos = source.find(old)
            while pos != -1:
                end = pos + len(old)
                if (pos == 0 or source[pos - 1:pos] == b"\n") and (
                    old.endswith(b"\n") or end == len(source)
                ):
                    positions.append(pos)
                pos = source.find(old, pos + 1)
            if len(positions) != 1:
                raise ValueError(f"{path}:{line_number}: old text matched {len(positions)} times")
            pos, = positions
            source = source[:pos] + new + source[pos + len(old):]
            applied += 1
    if not applied:
        raise ValueError(f"{path}: no hunks for {name}")
    return source


def execution_synopses(section):
    lines = section.splitlines(keepends=True)
    comments = [FACILITY_COMMENT.match(line) for line in lines]

    def facility(ref):
        starts = [i for i, comment in enumerate(comments) if comment and comment[2] == ref]
        if len(starts) != 1:
            raise ValueError(f"execution.syn: expected one synopsis anchor for {ref}")
        start, = starts
        indent = comments[start][1]
        end = start + 1
        while end < len(lines):
            line, comment = lines[end], comments[end]
            if line.strip() and (not line.startswith(indent) or line.lstrip().startswith(r"\end{codeblock}")):
                break
            if comment and comment[1] == indent and not lines[end - 1].strip():
                break
            end += 1
        return "".join(lines[start:end])

    for refs in SYNOPSIS_REFS:
        yield "".join(facility(ref) for ref in refs)


def slices(label, section):
    if label == "execution.syn":
        for synopsis in execution_synopses(section):
            yield 0, "synopsis", synopsis.strip()
        return

    start = None
    ordinal = 0
    element = "text"
    literal_end = None
    leading_allowed = True
    for match in COMMAND.finditer(section):
        token = match[1][1:]
        if literal_end:
            if token == literal_end:
                literal_end = None
                if start is not None and element == "synopsis":
                    yield 0, element, section[start:match.end()].strip()
                    start = None
                    leading_allowed = False
            continue
        if token == "begin{codeblock}":
            literal_end = "end{codeblock}"
            if start is None and ordinal == 0 and leading_allowed:
                start, element = match.start(), "synopsis"
            continue
        if start is not None:
            part = section[start:match.start()].strip()
            if part:
                yield ordinal, element, part
        if token == "pnum":
            ordinal += 1
            start, element = match.end(), "text"
        elif token.startswith("index") and start is not None:
            start, element = match.end(), "text"
        elif token in ELEMENTS and ordinal and start is not None:
            start, element = match.start(), token
        else:
            start = None
            if token == "begin{itemdecl}":
                literal_end = "end{itemdecl}"
    if literal_end:
        raise ValueError(f"{label}: expected \\{literal_end}")
    if start is not None:
        part = section[start:].strip()
        if part:
            yield ordinal, element, part


def extract(source, labels, missing_ok=()):
    sections = {}
    # Sections appearing as literal code are not TeX section boundaries.
    markers = []
    literal = None
    for match in re.finditer(
        SECTION.pattern + r"|^[ \t]*\\(begin|end)\{(codeblock|itemdecl)\}", source, re.M
    ):
        if match[2]:
            if literal is None and match[2] == "begin":
                literal = match[3]
            elif literal == match[3] and match[2] == "end":
                literal = None
        elif literal is None:
            markers.append(match)
    for i, marker in enumerate(markers):
        label = marker[1]
        if label in sections:
            raise ValueError(f"duplicate section label: {label}")
        end = markers[i + 1].start() if i + 1 < len(markers) else len(source)
        # Title text precedes the first structural command and is not a row.
        sections[label] = source[marker.end():end]

    for label in labels:
        if label not in sections:
            if label in missing_ok:
                continue
            raise ValueError(f"unknown section label: {label}")
        for para, element, part in slices(label, sections[label]):
            yield label, str(para), element, digest(part.encode("utf-8"))


def read_ledger(text):
    lines = text.splitlines()
    if not lines or lines[0] != HEADER:
        raise ValueError(f"expected ledger header {HEADER!r}")
    indexed = {}
    counts = {}
    for line in lines[1:]:
        row = tuple(line.split("\t"))
        if len(row) != 4 or not re.fullmatch(r"[0-9a-f]{64}", row[-1]):
            raise ValueError(f"invalid ledger row: {line!r}")
        key = row[:3]
        counts[key] = counts.get(key, 0) + 1
        indexed[(*key, counts[key])] = row[-1]
    return indexed


def check(old, new):
    stale = False
    for keys, category in ((old.keys() - new.keys(), "removed"),
                           (new.keys() - old.keys(), "added"),
                           (old.keys() & new.keys(), "changed")):
        for key in sorted(keys):
            if category == "changed" and old[key] == new[key]:
                continue
            stale = True
            detail = f"{old[key]} -> {new[key]}" if category == "changed" else (
                old[key] if category == "removed" else new[key]
            )
            print(category, *key, detail, sep="\t", file=sys.stderr)
    return int(stale)


def self_test():
    def require(condition, what):
        if not condition:
            raise AssertionError(what)

    with tempfile.TemporaryDirectory(prefix="clause-ledger-") as scratch:
        directory = Path(scratch)
        source = directory / "fixture.tex"
        first = directory / "first.diff"
        second = directory / "second.diff"
        ledger = directory / "ledger.tsv"

        # Include literal delimiter lookalikes inside code and a discarded
        # item declaration. These are synthetic, not draft text.
        synopsis = "\\begin{codeblock}\n int sample();\n\\end{codeblock}"
        example = (
            "\\begin{codeblock}\n  \\pnum\n  \\effects\n"
            "  \\rSec2[not-a-section]{Literal code}\n\\end{codeblock}"
        )
        elements = [
            "constraints", "mandates", "expects", "hardexpects", "effects",
            "sync", "ensures", "returns", "throws", "complexity", "remarks",
            "errors", "recommended",
        ]
        entries = [
            (0, "synopsis", synopsis),
            (1, "text", "alpha  é\n second line"),
            (1, "text", "continued text"),
        ]
        for name in elements:
            part = f"\\{name}\n  notes for {name}."
            if name == "effects":
                part += f"\n{example}"
            entries.append((1, name, part))
        entries.extend([(2, "text", "old text"), (3, "text", "last text")])
        source.write_bytes((
            "\\rSec1[fixture]{Fixture}\n"
            f"\\indexlibraryglobal{{sample}}%\n{synopsis}\n"
            "\\pnum\n\\indexlibraryglobal{alpha}%\n  alpha  é\n second line\n\n"
            "\\indexlibraryglobal{continued}\ncontinued text\n"
            "\\indexlibrarymember{sample}{scope}%\n"
            + "\n".join(part for _, _, part in entries[3:-2])
            + "\n\\indexlibrarymember{sample}{scope}%\n"
            "\\begin{itemdecl}\nint sample();\n\\pnum\n\\end{itemdecl}\n"
            "\\begin{itemdescr}\n"
            "\\pnum\nold text\n\\pnum\nlast text\n\\end{itemdescr}\n"
            "discarded text\n\\rSec2[child]{Child}\n\\pnum\nchild text\n\\returns\nchild result\n"
        ).encode("utf-8"))
        first.write_bytes(
            b"--- a/source/fixture.tex\n+++ b/source/fixture.tex\n"
            b"@@ -99,3 +123,3 @@\n \\pnum\n-old text\n+new text\n \\pnum\n"
        )
        second.write_bytes(
            b"diff --git a/other.tex b/other.tex\n"
            b"--- a/other.tex\n+++ b/other.tex\n@@ -1 +1 @@\n-a\n+b\n"
            b"diff --git a/fixture.tex b/fixture.tex\n"
            b"--- a/fixture.tex\n+++ b/fixture.tex\n"
            b"@@ -99,4 +99,5 @@\n \\pnum\n-new text\n+final text\n"
            b" \\pnum\n-last text\n+\\remarks\n+note\n"
        )

        def run(*args, labels=("fixture",)):
            return subprocess.run(
                [sys.executable, "-I", __file__, str(source), *labels, *args],
                capture_output=True, text=True, encoding="utf-8", check=False,
            )

        def tsv(items, label="fixture"):
            return HEADER + "\n" + "".join(
                f"{label}\t{para}\t{element}\t{digest(part.encode('utf-8'))}\n"
                for para, element, part in items
            )

        before = run()
        require(before.returncode == 0 and before.stdout == tsv(entries), "segmentation")
        ledger.write_bytes(before.stdout.encode("utf-8"))
        same = run("--check", str(ledger))
        require((same.returncode, same.stdout, same.stderr) == (0, "", ""), "fresh check")
        args = ("--overlay", str(first), "--overlay", str(second))
        after = run(*args, "--file-hash")
        expected_bytes = source.read_bytes().replace(
            b"old text\n\\pnum\nlast text\n", b"final text\n\\pnum\n\\remarks\nnote\n"
        )
        require(after.returncode == 0 and after.stderr == f"source sha256: {digest(expected_bytes)}\n",
                "ordered, byte-exact overlays")
        require(after.stdout == tsv(entries[:-2] + [
            (2, "text", "final text"), (3, "remarks", "\\remarks\nnote")
        ]), "overlaid segmentation")
        stale = run(*args, "--check", str(ledger))
        actual = [line.split("\t")[:5] for line in stale.stderr.splitlines()]
        require(stale.returncode == 1 and not stale.stdout and actual == [
            ["removed", "fixture", "3", "text", "1"],
            ["added", "fixture", "3", "remarks", "1"],
            ["changed", "fixture", "2", "text", "1"],
        ], "stale check")
        wrong_order = run("--overlay", str(second), "--overlay", str(first))
        require(wrong_order.returncode == 2 and "matched 0 times" in wrong_order.stderr,
                "missing hunk must be rejected")

        original = source.read_bytes()
        full = run(labels=("fixture", "child"))
        require(full.returncode == 0, "baseline with child clause")
        ledger.write_bytes(full.stdout.encode("utf-8"))
        source.write_bytes(original[:original.index(b"\\rSec2[child]")])
        removed = run("--overlay", str(first), "--check", str(ledger), labels=("child", "fixture"))
        actual = [line.split("\t")[:5] for line in removed.stderr.splitlines()]
        require(removed.returncode == 1 and not removed.stdout and actual == [
            ["removed", "child", "1", "returns", "1"],
            ["removed", "child", "1", "text", "1"],
            ["changed", "fixture", "2", "text", "1"],
        ], "missing baseline clause must report removals and continue comparing")
        unknown = run("--check", str(ledger), labels=("typo", "fixture"))
        require(unknown.returncode == 2 and "unknown section label: typo" in unknown.stderr,
                "unknown clause must still be rejected in check mode")
        unknown = run(labels=("child",))
        require(unknown.returncode == 2 and "unknown section label: child" in unknown.stderr,
                "generation must still reject absent clauses")

        source.write_bytes(original + expected_bytes.replace(
            b"final text\n\\pnum\n\\remarks\nnote\n", b"old text\n\\pnum\nlast text\n"
        ))
        ambiguous = run("--overlay", str(first))
        require(ambiguous.returncode == 2 and "matched 2 times" in ambiguous.stderr,
                "ambiguous hunk must be rejected")

        scheduler = ("  // \\ref{exec.task.scheduler}\n  class scheduler;\n\n"
                     "  template<class E> struct with_error;")
        task = "  // \\ref{exec.task}\n  template<class T> class task;"
        parallel = "  // \\ref{exec.par.scheduler}\n  class parallel;"
        replacement = ("  // \\ref{exec.parschedrepl}\n  namespace replacement {\n"
                       "    struct backend;\n  }")
        expected_synopses = tsv([
            (0, "synopsis", scheduler.strip()),
            (0, "synopsis", task.strip()),
            (0, "synopsis", f"{parallel}\n\n{replacement}".strip()),
        ], "execution.syn")
        for insert_unrelated in (False, True):
            facilities = []
            for i, facility in enumerate((scheduler, task, parallel, replacement)):
                facilities.append(facility)
                if insert_unrelated:
                    facilities.append(f"  // \\ref{{unrelated.{i}}}\n  struct unrelated_{i};")
            source.write_bytes((
                "\\rSec1[execution.syn]{Synopsis}\n\\begin{codeblock}\nnamespace std::execution {\n"
                + "\n\n".join(facilities)
                + "\n}\n\\end{codeblock}\n"
            ).encode("utf-8"))
            synopsis_rows = run(labels=("execution.syn",))
            require(synopsis_rows.returncode == 0 and synopsis_rows.stdout == expected_synopses,
                    f"scoped synopsis hashes (unrelated facilities inserted: {insert_unrelated})")
    print("clause-ledger self-test passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("source", type=Path, nargs="?")
    parser.add_argument("labels", nargs="*", help="exact LaTeX section labels")
    parser.add_argument("--overlay", action="append", type=Path, default=[],
                        metavar="DIFF", help="apply a unified diff (repeat in order)")
    parser.add_argument("--file-hash", action="store_true", help="report final source SHA-256")
    parser.add_argument("--check", type=Path, metavar="LEDGER", help="check an existing TSV")
    parser.add_argument("--self-test", action="store_true", help="run synthetic fixture checks")
    args = parser.parse_intermixed_args()
    if args.self_test:
        if args.source or args.labels or args.overlay or args.file_hash or args.check:
            parser.error("--self-test takes no other arguments")
        self_test()
        return 0
    if not args.source or not args.labels:
        parser.error("provide SOURCE and one or more section labels")
    if len(set(args.labels)) != len(args.labels):
        parser.error("section labels must be unique")
    try:
        source = read_lf(args.source)
        for diff in args.overlay:
            source = overlay(source, args.source.name, diff)
        if args.file_hash:
            print(f"source sha256: {digest(source)}", file=sys.stderr)
        baseline = read_ledger(read_lf(args.check).decode("utf-8")) if args.check else None
        missing_ok = {key[0] for key in baseline} if baseline is not None else ()
        rows = extract(source.decode("utf-8"), args.labels, missing_ok)
        ledger = "\n".join([HEADER, *("\t".join(row) for row in rows)]) + "\n"
        if baseline is not None:
            return check(baseline, read_ledger(ledger))
        sys.stdout.buffer.write(ledger.encode("utf-8"))
    except (OSError, ValueError) as error:
        print(f"clause-ledger: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
