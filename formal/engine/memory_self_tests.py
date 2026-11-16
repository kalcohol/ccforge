"""Client-independent memory contract tests, including absent synchronization."""
from types import SimpleNamespace
from .memory import Memory, change, empty_sync, synchronization


def run():
    for size in (1, 4, 7):
        cfg = SimpleNamespace(hb=True, stale=True)
        errors = []
        mem = Memory(cfg, lambda w, e: errors.append(e))
        z = (0, 0, 0)
        pub = (z, (0,) * size)
        w = SimpleNamespace(vc=(z, z, z), views=((0,) * size,) * 3,
                            hist=tuple(((0, z, pub),) for _ in range(size)), accesses=())
        assert empty_sync(w) == pub
        change(w, 'vc', 0, (2, 0, 0))
        mem.access(w, 0, 'payload', True)
        mem.publish(w, 0, 0, 1)
        change(w, 'vc', 1, (0, 3, 0))
        mem.publish(w, 1, 0, 2, 'relaxed', rmw=True)
        assert w.hist[0][-1][1] == (0, 3, 0)
        assert w.hist[0][-1][2][0] == (2, 0, 0)
        mem.load(w, 2, 0, 2)
        assert w.vc[2] == (2, 0, 0)
        mem.access(w, 2, 'payload')
        assert not errors
        assert mem.load_choices(w, 2, 0) == (2,)
        mem.acquire(w, 2, synchronization(w, 1))
        assert mem.load_choices(w, 2, 0) == (2,)
        mem.publish(w, 1, 0, 3, 'acq_rel', rmw=True)
        assert w.vc[1][0] == 2
        mem.load(w, 2, 0, 3)
        assert w.vc[2][1] == 3
        mem.publish(w, 0, 0, 4, 'relaxed')
        assert w.hist[0][-1][2] == pub
        mem.access(w, 1, 'unpublished', True)
        mem.access(w, 0, 'unpublished')
        assert errors  # No synthetic HB edge from the relaxed store.
    # Observation bounds cross an HB edge even without the writer's clock.
    w.vc = (z, z, z)
    w.views = ((0,) * size,) * 3
    w.hist = tuple(((0, z, pub),) for _ in range(size))
    change(w, 'vc', 0, (5, 0, 0))
    mem.publish(w, 0, 0, 1, 'relaxed', rmw=True)
    mem.load(w, 1, 0, 1)
    assert w.vc[1][0] == 0
    mem.acquire(w, 2, synchronization(w, 1))
    assert mem.load_choices(w, 2, 0) == (1,) and w.vc[2][0] == 0
