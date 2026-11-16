"""Counterexample reconstruction, using ordered model successors."""


def renderer(model, cfg, keys, parent, depth):
    def path_to(idx):
        p = []
        while idx != -1:
            p.append(idx)
            idx = parent[idx]
        p.reverse()
        return p

    def dump(cat, d, pidx, meta, w, fh):
        fh.write(
            "=== %s\n=== config %s\n=== violation: %s\n=== length (transitions): %d\n"
            % (cfg.name, getattr(model, "cfg_summary", repr)(cfg), cat, d)
        )
        p = path_to(pidx)
        initial = model.state(keys[p[0]])
        describe_initial = getattr(model, "describe_initial", lambda state: "(initial) " + repr(state))
        fh.write("    %s\n" % describe_initial(initial))
        for a, b in zip(p, p[1:]):
            kb = keys[b]
            for m2, w2 in model.successors(keys[a]):
                if model.key(w2) == kb:
                    fh.write("%4d %s\n" % (depth[b], model.describe(m2, w2)))
                    break
        if meta:
            fh.write("%4d %s\n" % (d, model.describe(meta, w)))
        fh.write("   >>> %s\n\n" % cat)

    return dump
