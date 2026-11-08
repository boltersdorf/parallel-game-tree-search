#!/usr/bin/env python3
"""Reintroduces one defect at a time into a copy of the sources and runs the quick suite.

Every mutant has to fail at least one case. Extra make arguments are passed through,
for example CXX=clang++.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# file, original text, mutated text, defect
MUTANTS = [
    ('src/search/zobrist.cpp',
     '    hash ^= hashVariables[122];\n    return hash;', '    return hash;',
     'incremental key without the side to move'),
    ('src/parallel/cumulative/tt.cpp',
     '? TTBound::LOWER :', '? TTBound::EXACT :',
     'fail high stored as an exact entry'),
    ('src/parallel/cumulative/tt_id_aw.cpp',
     'if (score <= alpha && alpha > minEvaluation()) {', 'if (false) {',
     'aspiration fail low taken as the value'),
    ('src/search/pick_best.h',
     '(score[i] == score[best] && exact && !bestExact)', 'false',
     'tie between a bound and an exact score decided by the move order'),
    ('src/parallel/cumulative/tt_id_aw_mo_ybwc.cpp',
     '            sc[0] = best_eval;\n', '',
     'eldest brother left out when the young brothers are merged'),
]


def main():
    make_args = sys.argv[1:]
    caught = 0
    with tempfile.TemporaryDirectory() as work:
        for path, old, new, what in MUTANTS:
            for d in ('src', 'tests', 'positions', 'build'):
                shutil.rmtree(os.path.join(work, d), ignore_errors=True)
            for d in ('src', 'tests', 'positions'):
                shutil.copytree(os.path.join(ROOT, d), os.path.join(work, d))
            shutil.copy(os.path.join(ROOT, 'Makefile'), work)

            target = os.path.join(work, path)
            src = open(target).read()
            if src.count(old) < 1:
                sys.exit(f'mutation site not found in {path}')
            open(target, 'w').write(src.replace(old, new))

            b = subprocess.run(['make', '-C', work, 'PORTABLE=1', '-j4', 'build/portable/abtest',
                                'build/portable/abtest-eq', 'build/portable/checks'] + make_args,
                               capture_output=True, text=True)
            if b.returncode != 0:
                sys.exit(b.stdout + b.stderr)
            t = subprocess.run([sys.executable, 'tests/run_tests.py', '--quick'], cwd=work,
                               capture_output=True, text=True)
            failed = sum(l.startswith('FAIL') for l in t.stdout.splitlines())
            print(f'{failed:4d} failed cases: {what} ({path})', flush=True)
            caught += failed > 0

    print(f'{caught} of {len(MUTANTS)} mutants caught')
    return 0 if caught == len(MUTANTS) else 1


if __name__ == '__main__':
    sys.exit(main())
