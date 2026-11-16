"""Pool execution, semantic regression records and atomic maintenance writes."""

import argparse
import hashlib
import json
import multiprocessing as mp
import os
import platform
import re

try:
    import resource
except ImportError:
    resource = None
import tempfile
import time
from pathlib import Path
from .search import explore
from . import source_lock


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def category(text):
    text = re.sub(r"\s*\([^ ()]+\.hpp:[^)]*\)", "", text)
    text = re.sub(r"\s*\([^)]* protocol(?:check)?\)", "", text)
    text = re.sub(r"\s*\[source:[^\]]*\]", "", text)
    return "v-" + hashlib.sha256(text.encode()).hexdigest()[:12]


def semantic(result, identity):
    fields = (
        "states",
        "transitions",
        "terminal_states",
        "abort_terminals",
        "states_with_blocked_thread",
        "max_depth",
        "max_store_buffer",
        "complete",
        "acyclic",
        "states_that_cannot_reach_completion",
        "states_on_or_after_cycles",
        "cycle_states_with_blocked_thread",
        "first_cycle_state_depth",
        "coverage",
    )
    record = {k: result[k] for k in fields if k in result}
    record["configuration"] = identity
    record["violations"] = sorted((category(v["category"]), v["length"]) for v in result["violations"])
    return record


def outcome(result):
    return (
        ",".join(
            "%s:%d" % (category(v["category"]), v["length"])
            for v in sorted(result["violations"], key=lambda x: (category(x["category"]), x["length"]))
        )
        or "pass"
    )


def expected_record(result):
    return "\t".join(
        str(x)
        for x in (
            result["name"],
            result["states"],
            result["transitions"],
            result["terminal_states"],
            outcome(result),
            hashlib.sha256(canonical(result["semantic"]).encode()).hexdigest(),
        )
    )


def atomic_write(path, text):
    path = Path(path)
    fd, tmp = tempfile.mkstemp(prefix="." + path.name, dir=str(path.parent))
    try:
        with os.fdopen(fd, "w") as fh:
            fh.write(text)
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def worker(item):
    # Import and configure in the child; compatible with spawn and forkserver.
    import importlib

    package = item[2]
    model = importlib.import_module(package + ".model")
    tiers = importlib.import_module(package + ".tiers")
    task, trace = item[:2]
    cfg = tiers.configuration(task)
    start = time.monotonic()
    r = explore(
        model, cfg, stop_on_first=task.get("stop", False), max_states=task.get("max", 8000000), trace_path=trace
    )
    r["semantic"] = semantic(r, tiers.identity(task))
    r["task"] = task
    r["peak_rss_kib"] = peak_rss_kib()
    r["wall_seconds"] = time.monotonic() - start
    return r


def peak_rss_kib():
    if resource is None:
        return None
    try:
        peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    except (OSError, AttributeError):
        return None
    return peak / 1024 if platform.system() == "Darwin" else peak


def guard_outputs(root, out, tasks):
    """Validate every destination before creating directories or opening files."""
    protected = (root / "formal").resolve()
    destinations = [out, out / "results.jsonl", out / "summary.md", out / "traces"]
    destinations += [out / "traces" / (task["name"] + ".txt") for task in tasks]
    for dest in destinations:
        if dest.is_symlink():
            raise ValueError("output destination is a symlink: " + str(dest))
        parent = dest.parent.resolve()
        resolved = dest.resolve()
        if any(p == protected or protected in p.parents for p in (parent, resolved)):
            raise ValueError("output destinations must be outside formal/: " + str(dest))


def default_jobs():
    # Budget 4 GiB per worker, retaining half of available host memory.
    memory = 2 * 1024**3
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemAvailable:"):
                memory = int(line.split()[1]) * 1024
    except OSError:
        pass
    return max(1, min(8, os.cpu_count() or 1, memory // (8 * 1024**3)))


def main(root, package, source):
    import importlib

    model = importlib.import_module(package + ".model")
    tiers = importlib.import_module(package + ".tiers")
    mutants = importlib.import_module(package + ".mutants")
    started = time.monotonic()
    ap = argparse.ArgumentParser(description="Bounded protocol exploration")
    ap.add_argument("--tier", choices=("quick", "full"), default="quick")
    ap.add_argument("--check-source", action="store_true")
    ap.add_argument("--steps", action="store_true")
    ap.add_argument("--jobs", type=int, default=default_jobs())
    ap.add_argument("--start-method", choices=mp.get_all_start_methods())
    ap.add_argument("--only")
    ap.add_argument("--out", type=Path)
    ap.add_argument("--update-expected", action="store_true")
    ap.add_argument("--refresh-lock", action="store_true")
    ap.add_argument("--assessment")
    args = ap.parse_args()
    if args.jobs < 1 or args.jobs > 16:
        ap.error("--jobs must be between 1 and 16")
    if args.update_expected and args.refresh_lock:
        ap.error("update and refresh cannot be combined")
    if (args.update_expected or args.refresh_lock) and (args.only is not None or args.steps or args.check_source):
        ap.error("transactions require a whole unfiltered tier run")
    if args.refresh_lock and not args.assessment:
        ap.error("--refresh-lock requires --assessment TEXT")
    try:
        if args.check_source:
            source_lock.check(root, model, package)
            print("source, checker, expected data and fragments: PASS")
            return 0
        if args.refresh_lock and args.tier == "quick":
            source_lock.quick_qualification(root, source, package)
        text = (root / source).read_text()
        # Resolve references once, outside the search hot path.
        for step in model.STEPS.values():
            source_lock.resolve(text, step.lines)
        if args.steps:
            for rid, pc, reference, kind, desc in model.step_table():
                print("%s.%s | %s | %s | %s" % (rid, pc, source_lock.resolve(text, reference), kind, desc))
            return 0
    except (ValueError, AssertionError) as exc:
        print(str(exc))
        return 1
    tasks = tiers.select(args.tier)
    if args.only is not None:
        tasks = [t for t in tasks if re.search(args.only, t["name"])]
    if not tasks:
        ap.error("no configurations selected")
    out = args.out or root / "build/formal" / package / args.tier
    out = out.absolute()
    try:
        guard_outputs(root, out, tasks)
    except ValueError as exc:
        ap.error(str(exc))
    (out / "traces").mkdir(parents=True, exist_ok=True)
    expected_path = root / "formal" / package / "expected/results.tsv"
    expected = {}
    if expected_path.exists():
        expected = {line.split("\t")[0]: line for line in expected_path.read_text().splitlines()[1:]}
    # Schedule larger graphs first to reduce pool tail latency; successor
    # ordering inside each configuration remains entirely model-defined.
    tasks.sort(key=lambda task: -int(expected.get(task["name"], task["name"] + "\t0").split("\t")[1]))
    errors, differences, results = [], [], []
    with (
        mp.get_context(args.start_method).Pool(args.jobs, maxtasksperchild=1) as pool,
        (out / "results.jsonl").open("w") as fh,
    ):
        for r in pool.imap_unordered(worker, [(t, str(out / "traces" / (t["name"] + ".txt")), package) for t in tasks]):
            results.append(r)
            fh.write(json.dumps(r, sort_keys=True) + "\n")
            fh.flush()
            if not r["complete"]:
                errors.append(r["name"] + ": incomplete exploration")
            declared = mutants.declared(r["task"])
            if bool(r["violations"]) != declared:
                errors.append(r["name"] + ": declared outcome violated")
            actual = expected_record(r)
            prior = expected.get(r["name"])
            if prior != actual:
                if prior is None:
                    differences.append(r["name"] + ": missing expected record")
                else:
                    columns = ("name", "states", "transitions", "terminal_states", "outcome", "semantic_sha256")
                    changes = [
                        "%s expected=%s actual=%s" % (field, old, new)
                        for field, old, new in zip(columns, prior.split("\t"), actual.split("\t"))
                        if old != new
                    ]
                    differences.append(r["name"] + ": " + "; ".join(changes))
    # Every declared killed mutant must retain a killer in each whole tier.
    if args.only is None:
        for mid, entry in mutants.CATALOG.items():
            if entry["outcome"] == "killed" and not any(
                r["task"].get("mut") == mid and r["violations"] for r in results
            ):
                errors.append(mid + ": no killing configuration")
    if args.update_expected and not errors:
        for r in results:
            expected[r["name"]] = expected_record(r)
        atomic_write(
            expected_path,
            "name\tstates\ttransitions\tterminal_states\toutcome\tsemantic_sha256\n"
            + "\n".join(expected[k] for k in sorted(expected))
            + "\n",
        )
        differences = []
    if args.refresh_lock and not errors and not differences:
        source_lock.self_test(root)
        lock = source_lock.qualification(
            root,
            source,
            package,
            args.tier,
            args.assessment,
            len(results),
            {"pass": sum(not r["violations"] for r in results), "fail": sum(bool(r["violations"]) for r in results)},
        )
        atomic_write(root / "formal" / package / "source.lock", json.dumps(lock, indent=2, sort_keys=True) + "\n")
    wall = time.monotonic() - started
    peaks = [r["peak_rss_kib"] for r in results if r["peak_rss_kib"] is not None]
    rss = ("%.1f KiB" % max(peaks)) if peaks else "unavailable"
    summary = [
        "# 协议模型检验",
        "",
        "配置数：%d；wall：%.3f s；workers：%d；最大 worker RSS：%s。" % (len(results), wall, args.jobs, rss),
        "",
        "| Mutant | Model | Killed / selected |",
        "|---|---|---|",
    ]
    for mid in sorted(mutants.CATALOG):
        for mod in sorted({r["task"]["model"] for r in results if r["task"].get("mut") == mid}):
            rows = [r for r in results if r["task"].get("mut") == mid and r["task"]["model"] == mod]
            summary.append("| %s | %s | %d / %d |" % (mid, mod, sum(bool(r["violations"]) for r in rows), len(rows)))
    summary += [
        "",
        "## Model aggregates",
        "",
        "| Model | Configurations | States | Transitions | Pass |",
        "|---|---:|---:|---:|---:|",
    ]
    for family in sorted({r["task"]["model"] for r in results}):
        rows = [r for r in results if r["task"]["model"] == family]
        summary.append(
            "| %s | %d | %d | %d | %d |"
            % (
                family,
                len(rows),
                sum(r["states"] for r in rows),
                sum(r["transitions"] for r in rows),
                sum(not r["violations"] for r in rows),
            )
        )
    summary += ["", "## 差异", ""] + errors + differences
    (out / "summary.md").write_text("\n".join(summary) + "\n")
    for error in errors + differences:
        print(error)
    print(
        "%d configurations; %.3fs; %d outcome errors; %d record differences"
        % (len(results), wall, len(errors), len(differences))
    )
    return int(bool(errors or differences))
