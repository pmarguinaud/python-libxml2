#!/bin/bash
# Run the test suite under valgrind, including the subprocess cases of
# test_regress.py. Any invalid memory access fails the case it happens in.
# Extra arguments go to pytest, e.g. tests/run_valgrind.sh tests/test_10_namespaces.py
cd "$(dirname "$0")/.." || exit 1
logs=$(mktemp -d)
PYTHONMALLOC=malloc REGRESS_TIMEOUT=300 REGRESS_SKIP_LEAKS=1 valgrind -q --trace-children=yes \
    --error-exitcode=99 --num-callers=12 --log-file="$logs/vg.%p" \
    python3 -m pytest -q -p no:cacheprovider --tb=line "${@:-tests}"
rc=$?
errors=$(cat "$logs"/vg.* | grep -cE 'Invalid (read|write|free)')
echo "valgrind: $errors invalid access(es), pytest rc=$rc"
if [ "$errors" -gt 0 ]; then echo "logs kept in $logs"; else rm -rf "$logs"; fi
exit $rc
