#!/bin/bash

# check-link-boundary.sh - verify that a shipping binary carries no dev-only symbol
# a dev-only feature is excluded at link time, so its absence has to be checked rather than assumed
# usage: check-link-boundary.sh <executable> [forbidden-symbol-substring]

# the executable to inspect
BINARY="$1"

# the dev-only symbol that must not appear in it, which is the mangled albor::automation namespace every host symbol carries
FORBIDDEN="${2:-albor10automation}"

if [ -z "$BINARY" ] || [ ! -f "$BINARY" ]; then
    echo "error: no binary to inspect: $BINARY" >&2
    exit 1
fi

# collect the symbols the binary defines
symbols="$(nm --defined-only "$BINARY" 2>/dev/null)"

# report the offending symbols so the failure names the leak
found="$(echo "$symbols" | grep -F "$FORBIDDEN")"

if [ -n "$found" ]; then
    echo "error: dev-only symbol '$FORBIDDEN' is linked into $BINARY" >&2
    echo "$found" >&2
    echo "a release link must not carry the automation host, so build the package without ALBOR_AUTOMATION" >&2
    exit 1
fi

# report an intact boundary so the check is visible in the build log
echo "link-boundary: $BINARY carries no '$FORBIDDEN' symbol"
