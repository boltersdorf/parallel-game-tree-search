#!/usr/bin/env python3
"""Correctness tests of every search against a plain minimax reference.

Every exact search has to return the minimax value of the same depth, for every
position, depth, thread count and repeat. The table versions run in the equal-depth
build, where a table cutoff only uses an entry of the same remaining depth, so they
search the same fixed-depth tree as the reference.
"""
import argparse
import functools
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POSITIONS = ['start', 'positions/position-midgame1', 'positions/position-midgame2',
             'positions/position-endgame']
DEPTHS = (2, 3, 4)
THREADS = (1, 2, 4, 8)

# no table, exact in the standard build
PLAIN = ['AB Seq Oracle', 'AB Parallel', 'AB Par Ord', 'AB Par YBW']
# with a table, exact in the equal-depth build
TABLE = ['AB Par TT', 'AB Par TT ID', 'AB Par TT ID Asp', 'AB Par TT ID Asp Ord',
         'AB Par TT ID Asp Ord YBW']

# one thread, endgame, depth 5: interior nodes, leaves, table cutoffs as measured on
# SuperMUC-NG (icpx 2025.3, job 5819721), the code here searches the same tree
# !!! the two wait concept versions are left out, at one thread they still queue
# their young brothers as tasks and the order a runtime runs queued tasks in is
# its own, llvm's and intel's give the measured tree, gnu's another one
MEASURED = {
    'AB Seq Oracle':            (235436, 1498754, 0),
    'AB Parallel':              (235436, 1498754, 0),
    'AB Par TT':                (160376, 902481, 42169),
    'AB Par TT ID':             (10299, 84311, 1908),
    'AB Par TT ID Asp':         (10303, 78188, 1948),
    'AB Par TT ID Asp Ord':     (8305, 48397, 2692),
    'AB Par Ord':               (28169, 83790, 0),
}


class Runner:
    def __init__(self, build, only=''):
        self.build = os.path.abspath(build)
        self.only = only
        self.passed = 0
        self.failed = []

    def selected(self, name):
        return self.only in name

    def report(self, name, errors):
        if errors:
            self.failed.append(name)
            print(f'FAIL  {name}')
            for e in errors:
                print(f'      {e}')
        else:
            self.passed += 1
            print(f'ok    {name}')

    def run(self, exe, args, threads=1):
        env = dict(os.environ, OMP_NUM_THREADS=str(threads))
        p = subprocess.run([os.path.join(self.build, exe)] + list(map(str, args)), cwd=ROOT,
                           env=env, capture_output=True, text=True, timeout=900)
        return p.returncode, p.stdout, p.stderr

    def search(self, exe, strategy, pos, depth, threads=1, repeats=1):
        """One row per repeat: value, move, interior nodes, leaves, table cutoffs."""
        code, out, err = self.run(exe, [strategy, pos, depth, repeats], threads)
        if code != 0:
            raise RuntimeError(f'{exe} exit code {code}: {err.strip()[-300:]}')
        rows = [l.split(',') for l in out.splitlines() if l.startswith(strategy + ',')]
        return [(int(r[5]), r[6], int(r[8]), int(r[9]), int(r[10])) for r in rows]

    @functools.lru_cache(maxsize=None)
    def reference(self, pos, depth):
        return self.search('abtest', 'Minmax Seq Oracle', pos, depth)[0][0]

    def value(self, exe, strategy, pos, depth, threads):
        name = f'{strategy} {os.path.basename(pos)} depth={depth} threads={threads}'
        if not self.selected(name):
            return
        repeats = 1 if threads == 1 else 3
        try:
            want = self.reference(pos, depth)
            got = [r[0] for r in self.search(exe, strategy, pos, depth, threads, repeats)]
        except (RuntimeError, subprocess.TimeoutExpired) as e:
            self.report(name, [str(e)])
            return
        errors = []
        if len(got) != repeats:
            errors.append(f'{len(got)} results, expected {repeats}')
        if any(v != want for v in got):
            errors.append(f'values {got}, minimax {want}')
        self.report(name, errors)

    def check(self, name, args, threads=1):
        if not self.selected(name):
            return
        code, out, err = self.run('checks', args, threads)
        self.report(name, [] if code == 0 else (out + err).strip().splitlines()[-4:])

    def measured(self, strategy):
        name = f'{strategy} endgame depth=5 node counts as measured'
        if not self.selected(name):
            return
        try:
            got = self.search('abtest', strategy, 'positions/position-endgame', 5)[0][2:]
        except (RuntimeError, subprocess.TimeoutExpired) as e:
            self.report(name, [str(e)])
            return
        want = MEASURED[strategy]
        self.report(name, [] if got == want else [f'interior, leaves, table cuts {got}, measured {want}'])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--build', default='build/portable', help='folder with the binaries')
    ap.add_argument('--only', default='', help='run only the cases whose name contains this text')
    ap.add_argument('--quick', action='store_true', help='fewer depths and thread counts')
    args = ap.parse_args()
    t = Runner(args.build, args.only)
    depths, threads = ((3, 4), (1, 4)) if args.quick else (DEPTHS, THREADS)
    move_threads = (2,) if args.quick else (2, 8)

    t.check('zobrist key updated per move equals the rebuild', ['zobrist', 400])
    t.check('table entries round-trip, torn entries rejected', ['table', 100000])
    t.check('a bound tied with an exact score loses the tie', ['pick'])

    for pos in POSITIONS:
        for d in depths:
            for th in threads:
                for s in PLAIN:
                    t.value('abtest', s, pos, d, th)
                for s in TABLE:
                    t.value('abtest-eq', s + ' EqDepth', pos, d, th)

    # a parallel search may report the right value with a move which does not reach
    # it, at depth 3 no position produces the tie which exposes that, 2 and 4 do
    for s in PLAIN[1:] + [s + ' EqDepth' for s in TABLE]:
        for pos in POSITIONS:
            for d in (2, 4):
                for th in move_threads:
                    t.check(f'{s} {os.path.basename(pos)} depth={d} threads={th} move reaches the value',
                            ['move', s, pos, d, 5], th)

    for s in MEASURED:
        t.measured(s)

    print(f'\n{t.passed} passed, {len(t.failed)} failed')
    return 1 if t.failed else 0


if __name__ == '__main__':
    sys.exit(main())
