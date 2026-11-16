"""Interned-state BFS and whole-graph completion reachability."""

import time
from array import array
from collections import deque
from .trace import renderer


def explore(model, cfg, stop_on_first=True, max_states=None, trace_path=None, want_graph=True):
    """BFS.  Returns a result dict."""
    model.configure(cfg)
    initial, successors = model.initial, model.successors
    encode, state = model.key, model.state
    state_violations = model.state_violations
    terminal_violations = model.terminal_violations
    is_blocked, is_aborted = model.is_blocked, model.is_aborted
    describe = model.describe
    t0 = time.time()
    w0 = initial()
    k0 = encode(w0)
    index = {k0: 0}
    keys = [k0]
    parent = array("i", [-1])
    depth = array("i", [0])
    esrc = array("I")
    edst = array("I")
    res = {"name": cfg.name, "violations": [], "complete": True}
    viols = {}
    nterm = 0
    nabort = 0
    nblockedstates = 0
    ntrans = 0
    i = 0

    def record(cat, pidx, meta, w):
        if cat in viols:
            return
        viols[cat] = (depth[pidx] + (1 if meta else 0), pidx, meta, w)

    while i < len(keys):
        k = keys[i]
        succ = successors(k)
        if not succ:
            w = state(k)
            violations = terminal_violations(w)
            nterm += 1
            if is_aborted(w):
                nabort += 1
            if violations:
                for cat in violations:
                    record(cat, i, None, w)
                if stop_on_first:
                    break
        else:
            if is_blocked(k):
                nblockedstates += 1
        bad = False
        for meta, w in succ:
            ntrans += 1
            violations = state_violations(w)
            if violations:
                for cat in violations:
                    record(cat, i, meta, w)
                bad = True
                continue  # do not expand beyond a violation
            k2 = encode(w)
            j = index.get(k2)
            if j is None:
                j = len(keys)
                index[k2] = j
                keys.append(k2)
                parent.append(i)
                depth.append(depth[i] + 1)
            if want_graph:
                esrc.append(i)
                edst.append(j)
        if bad and stop_on_first:
            break
        i += 1
        if max_states and len(keys) > max_states:
            res["complete"] = False
            break
    explored_all = i >= len(keys)
    res["complete"] = res["complete"] and (explored_all or bool(viols))
    res["states"] = len(keys)
    res["transitions"] = ntrans
    res["terminal_states"] = nterm
    res["abort_terminals"] = nabort
    res["states_with_blocked_thread"] = nblockedstates
    res["max_depth"] = max(depth)
    res.update(model.metrics())

    # ---------- liveness on the full graph
    if explored_all and not viols and want_graph:
        n = len(keys)
        indeg = array("I", bytes(4 * n))
        outdeg = array("I", bytes(4 * n))
        for e in range(len(esrc)):
            indeg[edst[e]] += 1
            outdeg[esrc[e]] += 1
        # CSR forward
        start = array("I", bytes(4 * (n + 1)))
        for s in range(n):
            start[s + 1] = start[s] + outdeg[s]
        pos = array("I", start[:n]) if n else array("I")
        adj = array("I", bytes(4 * len(esrc)))
        for e in range(len(esrc)):
            s = esrc[e]
            adj[pos[s]] = edst[e]
            pos[s] += 1
        # Kahn
        q = deque(s for s in range(n) if indeg[s] == 0)
        seen = 0
        deg = array("I", indeg)
        while q:
            s = q.popleft()
            seen += 1
            for e in range(start[s], start[s + 1]):
                d = adj[e]
                deg[d] -= 1
                if deg[d] == 0:
                    q.append(d)
        res["acyclic"] = seen == n
        if seen != n:
            # backward reachability from good terminals (all done or abort)
            rstart = array("I", bytes(4 * (n + 1)))
            for s in range(n):
                rstart[s + 1] = rstart[s] + indeg[s]
            rpos = array("I", rstart[:n])
            radj = array("I", bytes(4 * len(esrc)))
            for e in range(len(esrc)):
                d = edst[e]
                radj[rpos[d]] = esrc[e]
                rpos[d] += 1
            good = bytearray(n)
            q = deque()
            for s in range(n):
                if outdeg[s] == 0:
                    good[s] = 1
                    q.append(s)
            while q:
                s = q.popleft()
                for e in range(rstart[s], rstart[s + 1]):
                    p = radj[e]
                    if not good[p]:
                        good[p] = 1
                        q.append(p)
            ntrap = n - sum(good)
            res["states_that_cannot_reach_completion"] = ntrap
            if ntrap:
                s = next(x for x in range(n) if not good[x])
                record("livelock: state from which completion is unreachable", s, None, state(keys[s]))
            # cycles in which some thread stays blocked: states still having deg>0 lie on or behind cycles
            cyc = [s for s in range(n) if deg[s] > 0]
            res["states_on_or_after_cycles"] = len(cyc)
            res["cycle_states_with_blocked_thread"] = sum(1 for s in cyc if is_blocked(keys[s]))
            sx = min(cyc, key=lambda x: depth[x])
            res["first_cycle_state_depth"] = depth[sx]
            res["_cycle_state"] = sx

    # Snapshot coverage before trace reconstruction replays successors.
    res["coverage"] = model.coverage()
    dump = renderer(model, cfg, keys, parent, depth)
    for cat, (d, pidx, meta, w) in sorted(viols.items(), key=lambda kv: (kv[1][0], kv[0])):
        res["violations"].append({"category": cat, "length": d})
    if trace_path and (viols or res.get("_cycle_state") is not None):
        with open(trace_path, "w") as fh:
            for cat, (d, pidx, meta, w) in sorted(viols.items(), key=lambda kv: (kv[1][0], kv[0])):
                dump(cat, d, pidx, meta, w, fh)
            if res.get("_cycle_state") is not None:
                sx = res["_cycle_state"]
                dump(
                    "state on a cycle (no safety violation); shortest path to it",
                    depth[sx],
                    sx,
                    None,
                    state(keys[sx]),
                    fh,
                )
        res["trace"] = trace_path
    res.pop("_cycle_state", None)
    res["seconds"] = round(time.time() - t0, 1)
    return res
