#!/bin/bash

# clang-format linter for the C++ sources under slint/, core/, studio/, studio-tests/ and tests/
# exits non-zero when any file violates the project .clang-format
# exits non-zero when .clang-format itself is not the pinned style file
# exits non-zero when a comment rule of STYLE-GUIDE.md is violated in a file the change touched
# set XYCE_COMMENT_BASE=<ref> to measure the comment rules against a branch point instead of HEAD
#
# the tool is part of the style, because a clang-format major can change how it formats objective c++
# literals and one line blocks: the tree is formatted with clang-format 22, so run this same major
# before trusting a failure, which is what keeps one style on every platform

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# the digest of the style file this repository ships, so an environment that
# overwrites it with clang-format's own dump output fails the check instead of
# silently reformatting the whole tree against a different style.
# the digest is taken over the carriage returns removed, because windows
# checks the tree out with CRLF line endings and macos and linux check it out
# with LF, and one pin has to name the file on every platform
CLANG_FORMAT_SHA256="4f9e53ddf0d41a6c5e758cf5300fe9f0128a3d64f4550a99f82e522b95643260"

# hash the style file with whatever digest tool the host provides, reading the
# carriage returns out first so a checked out line ending never decides the pin
clang_format_digest() {
    if command -v shasum > /dev/null 2>&1; then
        tr -d '\r' < "$1" | shasum -a 256 | cut -d' ' -f1
    elif command -v sha256sum > /dev/null 2>&1; then
        tr -d '\r' < "$1" | sha256sum | cut -d' ' -f1
    else
        tr -d '\r' < "$1" | openssl dgst -sha256 | sed 's/.*= *//'
    fi
}

# reject a missing style file, because clang-format would then fall back to its built in style
if [ ! -f .clang-format ]; then
    echo "error: .clang-format is missing, restore it before checking the format" >&2
    exit 1
fi

# reject a style file that is not the pinned one
actual_digest="$(clang_format_digest .clang-format)"
if [ "$actual_digest" != "$CLANG_FORMAT_SHA256" ]; then
    echo "error: .clang-format does not match the pinned style file" >&2
    echo "  expected $CLANG_FORMAT_SHA256" >&2
    echo "  actual   $actual_digest" >&2
    echo "  restore it, and update CLANG_FORMAT_SHA256 only when the style change is intended" >&2
    exit 1
fi

# generated sources that clang-format must not reformat
EXCLUDED=(
    "slint/src/ui/font_data.h"
)

# the regex below matches every extension family in this repository:
#   *.cpp   *.h            — core, studio, tests
#   *.mm    (e.g. *.macos.mm)   — Objective-C++
#   *.c++   (e.g. *.linux.c++, *.win32.c++) — platform C++ extensions
#   *.metal — a shader the metal toolchain compiles, which only the comment sweep below covers
#   *.hlsl  — a shader the d3d toolchain compiles, which only the comment sweep below covers
# adding a new extension family requires updating this list, the grep below and the comment sweep

# collect the tracked and the not yet committed C++ sources, dropping the generated files
files=()
while IFS= read -r file; do
    [ -z "$file" ] && continue
    [ -f "$file" ] && files+=("$file")
done < <(git ls-files --cached --others --exclude-standard 'slint/**' 'core/**' 'studio/**' 'studio-tests/**' 'tests/**' | grep -E '\.(cpp|h|c\+\+|mm)$' | grep -Fxv -f <(printf '%s\n' "${EXCLUDED[@]}"))

if [ ${#files[@]} -eq 0 ]; then
    echo "error: no C++ source files found" >&2
    exit 1
fi

# the longest a comment line may be, which is where the shipped tree sits at its ninety ninth percentile, so a
# longer one wraps into a paragraph where STYLE-GUIDE.md asks for one short line (§Comments)
COMMENT_LINE_LIMIT=124

# the ref the comment rules are measured against, which keeps the sweep history in the issue tracker
# while every file a change touches is held to the rule; override it to measure against a branch point
COMMENT_BASE_REF="${XYCE_COMMENT_BASE:-HEAD}"

# report the comment rules over the files a change touches, because a rule a change cannot affect is not its business
check_comment_rules() {
    local limit="$1"
    local base="$2"
    local changed=()
    local file

    # the tracked files this change touched and the ones it added, which together are what the rules cover
    while IFS= read -r file; do
        [ -z "$file" ] && continue
        [ -f "$file" ] && changed+=("$file")
    done < <({ git diff --name-only --diff-filter=ACMR "$base" -- '*.cpp' '*.h' '*.c++' '*.mm' '*.metal' '*.hlsl'; git ls-files --others --exclude-standard -- '*.cpp' '*.h' '*.c++' '*.mm' '*.metal' '*.hlsl'; } | sort -u)

    if [ ${#changed[@]} -eq 0 ]; then
        # nothing changed, so there is nothing to hold to the rules
        return 0
    fi

    local failures=0
    local reported

    # every comment line in a touched file has to be short, because the whole file is made compliant, not only the lines the change adds
    for file in "${changed[@]}"; do
        reported="$(awk -v limit="$limit" '{ if ($0 ~ /^[[:space:]]*\/\// && length($0) > limit) print FILENAME ":" FNR ": comment is " length($0) " characters, the limit is " limit }' "$file")"
        if [ -n "$reported" ]; then
            echo "$reported" >&2
            failures=$((failures + $(printf '%s\n' "$reported" | wc -l | tr -d ' ')))
        fi
    done

    # a touched file carries no consecutive comment lines, because a file this change touches is fixed in the same
    # change, which is the rule the sweep issue records for the suites that still carry the violation (§Comments)
    for file in "${changed[@]}"; do
        # the pairs are reported with their first line, which is the one a change would move the second comment onto
        reported="$(awk '
            /^\/\// || /^[[:space:]]*\/\// { if (previous_was_comment) print FILENAME ":" NR - 1 ": two comment lines in a row"; previous_was_comment = 1; next }
            { previous_was_comment = 0 }
        ' "$file")"
        if [ -n "$reported" ]; then
            echo "$reported" >&2
            failures=$((failures + $(printf '%s\n' "$reported" | wc -l | tr -d ' ')))
        fi
    done

    if [ "$failures" -ne 0 ]; then
        # the rule is restated, so a failure says what to do rather than only where
        echo "error: $failures comment rule violations, see STYLE-GUIDE.md §Comments" >&2
        echo "  one short comment line per statement, never a paragraph of comment lines" >&2
        echo "  a file this change touches is made fully compliant for any violation it already had (STYLE-GUIDE.md §Comments)" >&2
        return 1
    fi

    # report an intact rule set, so the check is visible in the build log
    echo "comments: ${#changed[@]} changed files hold no long comment line and no consecutive pair"
    return 0
}

# clang-format's --dry-run --Werror prints each violation and exits non-zero
clang-format --dry-run --Werror "${files[@]}"
status=$?

# the comment rules run after the format check, and either one decides the exit code
check_comment_rules "$COMMENT_LINE_LIMIT" "$COMMENT_BASE_REF" || status=1

exit "$status"
