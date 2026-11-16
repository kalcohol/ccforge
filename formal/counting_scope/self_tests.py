"""Fast fidelity and memory-abstraction regressions; no tier exploration."""

from . import model
from .tiers import full, select, configuration
from .mutants import CATALOG, failure_manifest


def run():
    text = model.SOURCE.read_text()
    model.check_duplicates(text)
    for fragment in ('__count_.fetch_add(1, std::memory_order_relaxed);',
                     'state = __state_.load(std::memory_order_relaxed);',
                     'joiner->__registered = true;'):
        changed = text.replace(fragment, fragment.replace(';', '; drift();'), 1)
        try:
            model.check_duplicates(changed)
        except ValueError as exc:
            assert 'bodies differ' in str(exc)
        else:
            raise AssertionError('one-sided source drift accepted')
    model.check_duplicates(text.replace('joiner->__registered = true;',
                                        'joiner->__registered = /* comment */ true;', 1))
    tasks, quick = full(), select('quick')
    assert len({t['name'] for t in tasks}) == len(tasks)
    assert {t['mut'] for t in quick} >= set(CATALOG)
    assert failure_manifest() <= {t['name'] for t in tasks}
    for task in tasks:
        cfg = configuration(task)
        assert len(cfg.threads) + 1 <= 5 and cfg.nholders <= 3 and cfg.njoiners <= 2
    # An acquire load cannot predate an HB write. Concurrent older values stay
    # legal until observed; subsequent loads retain read-read coherence.
    model.configure(model.Cfg(stale=True, threads=((), ()), initial_holders=()))
    w = model.initial()
    model.change(w, 'vc', 0, (2, 0, 0))
    model.publish(w, 0, 2, True)
    assert model.load_choices(w, 1, 2) == (0, 1)
    model.acquire(w, 1, ((2, 0, 0), (0, 0, 0, 0)))
    assert model.load_choices(w, 1, 2) == (1,)
    model.configure(model.Cfg(stale=True, threads=((), ()), initial_holders=()))
    w = model.initial()
    model.change(w, 'vc', 0, (2, 0, 0))
    model.publish(w, 0, 2, True)
    model.load(w, 1, 2, 1)
    assert model.load_choices(w, 1, 2) == (1,)
    # Core-language precondition: even a callback on its own constructor's
    # thread cannot destroy the fused object until the constructor returns.
    model.configure(model.Cfg(ncallbacks=1, inputs=((0, 1),), threads=((),)))
    w = model.initial()
    assert not model.enabled(w, 0, ('destroy_cb', 0))
    model.change(w, 'constructed', 0, True)
    assert model.enabled(w, 0, ('destroy_cb', 0))

    # Compare spelling, not the matcher's opaque literal placeholder; even
    # semantically equal escape spellings differ in this maintenance guard.
    for left, right in (('"a"', '"b"'), ('1', '2'), ("'x'", "'y'"),
                        ('R"(a)"', 'R"(b)"'), ('"a"', '"\\x61"')):
        fixture = text.replace('void close() noexcept {',
                               'void close() noexcept { probe(' + left + ');')
        changed = fixture.replace('probe(' + left + ');', 'probe(' + right + ');', 1)
        try:
            model.check_duplicates(changed)
        except ValueError as exc:
            assert 'bodies differ: close' in str(exc)
        else:
            raise AssertionError('literal-only drift accepted')
        model.check_duplicates(fixture)
    from engine.source_lock import analyze, definition_candidates
    for function in ('scope_association::__release', '__count', '__is_closed',
                     'scope_association::try_associate', 'scope_token::try_associate'):
        stream, _, _, *_ = analyze(text)
        body = next(iter(definition_candidates(text, 'simple_counting_scope::' + function)))
        offset = stream[body][1] + 1
        try:
            model.check_duplicates(text[:offset] + 'drift();' + text[offset:])
        except ValueError as exc:
            assert 'bodies differ' in str(exc)
        else:
            raise AssertionError('modeled duplicate drift accepted: ' + function)

    # A relaxed RMW continues the head's release sequence but does not release
    # its own preceding actions. MO coherence nevertheless sees that action.
    model.configure(model.Cfg(hb=True, stale=True, threads=((), (), ()), initial_holders=()))
    w = model.initial()
    model.change(w, 'vc', 0, (2, 0, 0, 0))
    model.publish(w, 0, 1, 1)
    model.change(w, 'vc', 1, (0, 3, 0, 0))
    model.publish(w, 1, 1, 2, 'relaxed', rmw=True)
    assert w.hist[1][-1][1] == (0, 3, 0, 0)
    assert w.hist[1][-1][2][0] == (2, 0, 0, 0)
    model.load(w, 2, 1, 2)
    assert w.vc[2][0] == 2 and w.vc[2][1] == 0
    model.acquire(w, 2, ((0, 3, 0, 0), (0, 0, 0, 0)))
    assert model.load_choices(w, 2, 1) == (2,)
    model.publish(w, 1, 1, 1, 'acq_rel', rmw=True)
    assert w.vc[1][0] == 2
    model.load(w, 2, 1, 3)
    assert w.vc[2][1] == 3
    # A relaxed store ends the sequence, and must invent no synchronization.
    model.publish(w, 1, 1, 9, 'relaxed')
    assert not any(w.hist[1][-1][2][0]) and not any(w.hist[1][-1][2][1])

    # Actual rejected-association and non-final-decrement paths have no state
    # publication; the rejected path has no count modification either.
    model.configure(model.Cfg(stale=True, threads=((), ()), initial_holders=()))
    w = model.initial()
    model.execute(w, 0, ('state_store', 'joined'))
    hist = w.hist
    model.execute(w, 1, ('associate_read', 0))
    assert w.hist == hist
    model.load(w, 0, 0, len(w.hist[0])-1)
    assert w.vc[0][1] == 0  # No release from the rejecting thread.
    model.configure(model.Cfg(stale=True, threads=((), ()), nholders=2, initial_holders=(0, 1)))
    w = model.initial()
    hist = w.hist[0]
    model.execute(w, 0, ('decrement', 0))
    assert w.hist[0] == hist and not w.threads[0]
    model.configure(model.Cfg(stale=True, threads=((), ())))
    w = model.initial()
    model.execute(w, 0, ('decrement', 0))
    model.change(w, 'threads', 0, ())
    model.execute(w, 0, ('decrement_state',))
    assert len(w.hist[0]) == 1 and not w.threads[0]

    # A false request has no retention obligation of its own, even while the
    # winning request still has an independently snapshotted pending node.
    model.configure(model.Cfg(ncallbacks=1, inputs=((0,),), initial_callbacks=(0,), threads=((), ())))
    w = model.initial()
    model.execute(w, 0, ('request', 0))
    assert w.retained[0] == (0,)
    model.execute(w, 1, ('request', 0))
    model.execute(w, 1, ('request_end', 0, False))
    assert not w.errors and w.retained[0] == (0,)
    model.execute(w, 0, ('request_end', 0, True))
    assert any('retained registration' in x for x in w.errors)
    assert not w.obligated[0]  # Independent of the selection transition.

    # Every selected callback identity must remain a distinct request choice.
    from .scenarios import SCENARIOS
    model.configure(model.Cfg(**SCENARIOS['s_multi']))
    w = model.initial()
    model.change(w, 'threads', 0, (('request_loop', 0),))
    children = model.successors(model.key(w))
    assert len(children) == 3
    assert {tuple(i for i, thread in enumerate(child.active) if thread == 0)
            for _, child in children} == {(0,), (2,), (4,)}
    for item in tasks:
        cfg = configuration(item)
        assert cfg.limit == (2 if item['scen'] == 'd_limit' else None)
    assert {'s_multi', 's_external_complete', 'f_observer_teardown',
            'd_unused_closed_join', 'd_unused_closed_destroy', 'd_joining_observers',
            'review_inline_registration_lifetime', 'u_pre0', 'u_pre1'} <= {t['scen'] for t in quick}

    # A losing input can return with an outstanding commitment on the other
    # thread. The flag alone never satisfies delivery, and dispatch is checked
    # independently of both selection and the request-return predicate.
    model.configure(model.Cfg(**SCENARIOS['u_retained_deadline']))
    w = model.initial()
    for t, op in ((0, ('request', 0)), (1, ('request', 1)),
                  (0, ('request_loop', 0)), (1, ('request_loop', 1)),
                  (0, ('invoke', 0, 0)), (1, ('invoke', 0, 1)),
                  (1, ('invoke_end', 0, 1)), (1, ('request_end', 1, True))):
        model.execute(w, t, op)
    assert w.flag[0] and w.calls[0] == 0 and w.regs == (2, 3)
    assert w.commitments == ((0, 0, 0),) and not w.errors
    assert not model.state_violations(w)
    # This return is not evidence that Fn's effects HB the losing requester.
    # The outstanding commitment, rather than the flag, is the reason it passes.
    saved = w.commitments
    w.commitments = ()
    model.change(w, 'retained', 1, (1,))
    model.change(w, 'requests', 1, 1)
    model.execute(w, 1, ('request_end', 1, True))
    assert any('retained registration' in x for x in w.errors)
    w.errors, w.commitments = (), saved
    model.execute(w, 0, ('fn', 0))
    assert w.commitments == ((0, 0, 1),) and w.calls == (1,)
    model.execute(w, 0, ('fn', 0))
    assert w.commitments == ((0, 0, 2),)
    assert 'F3: commitment entered Fn more than once' in model.state_violations(w)

    for replacement in ((), (('await', 'missing'),), (('deregister', 0, 1),)):
        model.configure(model.Cfg(**SCENARIOS['u_retained_deadline']))
        w = model.initial()
        model.execute(w, 0, ('invoke', 0, 0))
        model.change(w, 'threads', 0, replacement)
        assert any('commitment abandoned or protocol wait' in x for x in model.state_violations(w))
        assert any('commitment did not enter Fn exactly once' in x for x in model.terminal_violations(w))

    # Lock exclusion lasts through every visible write up to separate unlock.
    model.configure(model.Cfg(threads=((), ())))
    w = model.initial()
    model.execute(w, 0, ('release', 0))
    model.execute(w, 0, ('decrement', 0))
    assert w.owner == 0 and w.count == 0
    assert not model.enabled(w, 1, ('destroy_scope',))
    assert model.enabled(w, 1, ('count',))
    model.execute(w, 0, ('unlock',))
    assert model.enabled(w, 1, ('destroy_scope',))
    model.configure(model.Cfg(scope_kind='simple_counting_scope', threads=((),), initial_holders=()))
    w = model.initial()
    model.execute(w, 0, ('scope_dead', True))
    assert not w.alive and all(w.source_alive)  # No inplace source member.

    # Real unchanged-value stores still publish: close on joined, and an
    # accepted association on open. Neither can be optimized away in history.
    model.configure(model.Cfg(stale=True, threads=((),), nholders=2))
    w = model.initial()
    model.execute(w, 0, ('associate_read', 1))
    assert w.threads[0][0] == ('state_store', 'open')
    model.execute(w, 0, ('state_store', 'open'))
    model.execute(w, 0, ('increment', 1))
    assert len(w.hist[0]) == 2 and len(w.hist[1]) == 2
    assert w.hist[1][-1][2] == ((0, 0), (0, 0, 0, 0))  # No release head.
    model.execute(w, 0, ('state_store', 'joined'))
    size = len(w.hist[0])
    model.execute(w, 0, ('close_read',))
    assert w.threads[0][0] == ('state_store', 'joined')
    model.execute(w, 0, ('state_store', 'joined'))
    assert len(w.hist[0]) == size + 1

    model.configure(model.Cfg(hb=True, threads=((),), initial_holders=()))
    w = model.initial()
    before = model.key(w)
    model.execute(w, 0, ('release', 0))
    assert model.key(w) == before  # Disengaged handle has no scope dependency.

    # Directive contents are tokens too; the fragment matcher masks them, but
    # the equivalence guard must not hide a one-sided macro or literal drift.
    fixture = text.replace('void close() noexcept {',
                           'void close() noexcept {\n#define LOCAL_PROBE "a"\n')
    model.check_duplicates(fixture)
    try:
        model.check_duplicates(fixture.replace('#define LOCAL_PROBE "a"',
                                               '#define LOCAL_PROBE "b"', 1))
    except ValueError as exc:
        assert 'bodies differ: close' in str(exc)
    else:
        raise AssertionError('directive-only drift accepted')

    coherence_handoff_tests()
    # Removing either lock invalidates both participants' plain-access merges.
    for mutant in ('J_decrement_unlocked', 'J_join_unlocked'):
        model.configure(model.Cfg(mut=mutant, threads=((),)))
        w = model.initial()
        model.execute(w, 0, ('list_read', 0))
        assert w.threads[0][0] == ('list_push', 0) and not w.joiners
        model.change(w, 'threads', 0, ())
        w.joiners = (0,)
        model.execute(w, 0, ('list_detach',))
        assert w.threads[0][0] == ('list_clear', 0) and w.joiners == (0,)


def coherence_handoff_tests():
    """A relaxed increment read HB-before another read constrains its MO floor."""
    def observed():
        model.configure(model.Cfg(name='review_hb_observer_handoff', stale=True,
            threads=((), (), ()), initial_holders=(), ncallbacks=1, inputs=((0,),)))
        w = model.initial()
        model.change(w, 'vc', 0, (2, 0, 0, 0))
        model.execute(w, 0, ('increment', 0))
        model.change(w, 'vc', 1, (0, 2, 0, 0))
        model.execute(w, 1, ('count',), 1)
        # The observer does not acquire the relaxed RMW writer's clock.
        assert w.vc[1][0] == 0 and w.views[1][1] == 1
        assert model.load_choices(w, 2, 1) == (0, 1)
        return w

    def constrained(w):
        assert model.load_choices(w, 2, 1) == (1,)
        assert w.vc[2][0] == 0  # Read-read coherence is not writer publication.

    w = observed()
    model.execute(w, 1, ('signal', 'observed'))
    model.execute(w, 2, ('await', 'observed'))
    constrained(w)
    # Every abstract HB edge uses the same pair of independent clocks/bounds.
    w = observed()
    model.execute(w, 1, ('unlock',))
    model.mutex_enter(w, 2, 'join')
    constrained(w)
    w = observed()
    model.C.scheduler = 'queued'
    model.execute(w, 1, ('schedule', 0))
    model.acquire(w, 2, w.queue[0][1])
    constrained(w)
    w = observed()
    model.execute(w, 1, ('ctor_end', 0))
    model.execute(w, 2, ('wait_ctor', 0))
    constrained(w)
    w = observed()
    model.execute(w, 1, ('register', 0, 0))
    model.execute(w, 2, ('request_loop', 0))
    constrained(w)
    w = observed()
    model.execute(w, 1, ('invoke_end', 0, 0))
    model.execute(w, 2, ('deregister', 0, 0))
    constrained(w)
    w = observed()
    model.execute(w, 1, ('request', 0))
    model.execute(w, 1, ('request_end', 0, True))
    model.execute(w, 2, ('wait_idle',))
    constrained(w)
    for wait in (('wait_join', 0), ('wait_all',)):
        w = observed()
        w.scope, w.count, w.holders = 'joined', 0, (False,)
        model.execute(w, 1, ('receive', 0))
        model.execute(w, 2, wait)
        constrained(w)
    # Individual join waits import only that completion's clock and bounds.
    # Each completion observes a different relaxed write by a fourth thread,
    # so its observation bounds cannot be inferred from its HB clock.
    for wait in (('wait_join', 0), ('wait_join', 1), ('wait_all',)):
        model.configure(model.Cfg(name='review_single_join_handoff', hb=True, stale=True,
            threads=((), (), ()), initial_holders=(), njoiners=2))
        w = model.initial()
        w.scope = 'joined'
        for j in range(2):
            model.publish(w, 3, 2+j, True, 'relaxed')
            model.change(w, 'vc', j, tuple(2 if u == j else 0 for u in range(4)))
            model.load(w, j, 2+j, 1)
            model.execute(w, j, ('receive', j))
        assert not w.errors and model.enabled(w, 2, wait)
        assert model.load_choices(w, 2, 2) == model.load_choices(w, 2, 3) == (0, 1)
        model.execute(w, 2, wait)
        for j in range(2):
            acquired = wait[0] == 'wait_all' or wait[1] == j
            assert w.vc[2][j] == (2 if acquired else 0)
            assert w.views[2][2+j] == int(acquired)
            assert model.load_choices(w, 2, 2+j) == ((1,) if acquired else (0, 1))
        assert w.vc[2][3] == 0  # Neither relaxed writer publication is imported.
    w = observed()
    model.publish(w, 1, 0, 'open')
    model.load(w, 2, 0, 1)
    constrained(w)
    # A relaxed RMW must also refrain from publishing the writer's read bounds.
    w = observed()
    model.publish(w, 1, 2, True, 'relaxed', rmw=True)
    model.load(w, 2, 2, 1)
    assert model.load_choices(w, 2, 1) == (0, 1)
    model.publish(w, 1, 2, True, 'acq_rel', rmw=True)
    model.load(w, 2, 2, 2)
    constrained(w)
