"""Shared vector clocks, coherent atomic histories and plain-access race checks.

Publications contain separate HB clocks and observed modification-order bounds.
Only actual source modifications may call publish. Object indices belong to the
client. The client advances its thread clock once per observable step.
"""

def put(seq, i, value):
    return seq[:i] + (value,) + seq[i + 1:]


def change(w, field, i, value):
    setattr(w, field, put(getattr(w, field), i, value))


def merge(a, b):
    return tuple(max(x, y) for x, y in zip(a, b))


def synchronization(w, t):
    """HB publication carries clocks and observed MO bounds independently."""
    return w.vc[t], w.views[t]


def merge_sync(a, b):
    return merge(a[0], b[0]), merge(a[1], b[1])


def empty_sync(w):
    return (0,) * len(w.vc), (0,) * len(w.hist)


class Memory:
    def __init__(self, config, detect):
        self.config, self.detect = config, detect

    def acquire(self, w, t, publication):
        if self.config.hb or self.config.stale:
            clock, observed = publication
            change(w, 'vc', t, merge(w.vc[t], clock))
            change(w, 'views', t, merge(w.views[t], observed))

    def access(self, w, t, name, write=False):
        """Vector-clock race checks, including lifetime end as a conflicting write."""
        if not self.config.hb:
            return
        records = dict(w.accesses)
        old = records.get(name, ())
        for u, clock, written in old:
            if u != t and (write or written) and any(x > y for x, y in zip(clock, w.vc[t])):
                self.detect(w, 'HB: unordered access to ' + name)
        records[name] = ((t, w.vc[t], write),) if write else old + ((t, w.vc[t], False),)
        # Keep only the last read per thread, without discarding the last writer.
        if not write:
            records[name] = tuple(v for i, v in enumerate(records[name])
                                  if v[2] or not any(x[0] == v[0] and not x[2] for x in records[name][i+1:]))
        w.accesses = tuple(sorted(records.items()))

    def publish(self, w, t, obj, value, order='release', rmw=False):
        """A real atomic modification: MO clock and release-sequence clock differ."""
        previous = w.hist[obj][-1][2]
        if rmw and order == 'acq_rel':
            self.acquire(w, t, previous)
        release = synchronization(w, t) if order in ('release', 'acq_rel') else empty_sync(w)
        if rmw:
            release = merge_sync(previous, release)
        entry = (value, w.vc[t], release)
        hist = list(w.hist)
        hist[obj] = hist[obj] + (entry,) if self.config.stale else (entry,)
        w.hist = tuple(hist)
        change(w, 'views', t, put(w.views[t], obj, len(hist[obj]) - 1))

    def load_choices(self, w, t, obj):
        if not self.config.stale:
            return (len(w.hist[obj]) - 1,)
        floor = w.views[t][obj]
        # Write-read coherence includes writes by other threads that happen-before.
        for i, (_, clock, _) in enumerate(w.hist[obj]):
            if all(x <= y for x, y in zip(clock, w.vc[t])):
                floor = max(floor, i)
        return tuple(range(floor, len(w.hist[obj])))

    def load(self, w, t, obj, alternative, order='acquire'):
        value, _, release = w.hist[obj][alternative]
        change(w, 'views', t, put(w.views[t], obj, alternative))
        if order == 'acquire':
            self.acquire(w, t, release)
        return value
