#!/bin/bash

# clang-format linter for the C++ sources under src/ and tests/
# exits non-zero when any file violates the project .clang-format

# root of the repository, independent of the current working directory
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# generated sources that clang-format must not reformat
EXCLUDED=(
    "src/ui/font_data.h"
)

# collect the C++ sources of the working tree, dropping the generated files.  untracked files are
# included so a freshly added source is linted before it is committed, which is when a formatting
# violation is cheapest to fix (a tracked-only listing let a new file reach CI unformatted)
files=()
while IFS= read -r file; do
    [ -z "$file" ] && continue
    files+=("$file")
done < <(git ls-files --cached --others --exclude-standard 'src/**' 'tests/**' | grep -E '\.(cpp|h|c\+\+|mm)$' | grep -Fxv -f <(printf '%s\n' "${EXCLUDED[@]}"))

if [ ${#files[@]} -eq 0 ]; then
    echo "error: no C++ source files found" >&2
    exit 1
fi

# clang-format's --dry-run --Werror prints each violation and exits non-zero
clang-format --dry-run --Werror "${files[@]}"
