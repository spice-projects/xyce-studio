#!/bin/bash

# any failed copy, sed or zip must abort the packaging: a partial package
# published as a release artifact is worse than a failed job (issue #267)
set -euo pipefail

# project version, defaults to 0.0.1
PROJECT_VERSION=${1:-0.0.1}
# path to the compiled plugin executable, defaults to the debug build output
EXECUTABLE=${2:-.build-debug/xyce-studio}
# plugin entrypoint name as referenced by src/plugin.json
ENTRYPOINT_NAME=${3:-xyce-studio}
# platform, allowed values: macos, linux, windows
PLATFORM=${4:-macos}
# path(s) to shared libraries shipped beside the executable: every argument
# from the 5th onward, space separated, defaults to none. Collecting all of
# them (${*:5}) instead of only $5 is what makes an unquoted caller list work;
# each library may be passed as its own argument or all inside one argument
SHARED_LIBRARIES="${*:5}"

# fail early when the executable is missing
if [ ! -f "$EXECUTABLE" ] && [ ! -d "$EXECUTABLE" ]; then
    echo "error: executable not found at $EXECUTABLE" >&2
    exit 1
fi

# fail early when a shared library is expected but missing
for library in $SHARED_LIBRARIES; do
    if [ ! -f "$library" ]; then
        echo "error: shared library not found at $library" >&2
        exit 1
    fi
done

# destination folder
mkdir -p dist
rm -rf dist/*

# create temporary folder
temp_dir=$(mktemp -d)

# initialize folder structure
mkdir -p "$temp_dir"/plugins

# copy metadata, replace "0.0.0" with version, replace platform
sed "s/0.0.0/$PROJECT_VERSION/g; s/\"macos\"/\"$PLATFORM\"/" metadata.json > dist/metadata.json
cp dist/metadata.json "$temp_dir"/metadata.json

# copy package icon
mkdir -p "$temp_dir"/resources
cp plugin-icon-64x64.png "$temp_dir"/resources/icon.png

# copy plugin manifest and icons
sed "s|ENTRYPOINT_NAME|$ENTRYPOINT_NAME|g" src/plugin.json > "$temp_dir"/plugins/plugin.json
cp src/plugin-icon-24x24.png "$temp_dir"/plugins/

# copy the compiled executable under the entrypoint name
cp -r "$EXECUTABLE" "$temp_dir"/plugins/

# copy the shared libraries beside the executable so they resolve through @executable_path
for library in $SHARED_LIBRARIES; do
    cp "$library" "$temp_dir"/plugins/
done

# distribution file
output_zip="$(pwd)/dist/xyce-studio-$PROJECT_VERSION.zip"

# create package, flat structure
(cd "$temp_dir" && zip -r "$output_zip" .)

# verify the archive really carries every library: catching it here keeps a
# truncated package from ever reaching a release (issue #267)
zip_listing=$(zipinfo -1 "$output_zip")
for library in $SHARED_LIBRARIES; do
    if ! grep -Fqx "plugins/$(basename "$library")" <<< "$zip_listing"; then
        echo "error: $(basename "$library") missing from $output_zip" >&2
        rm -rf "$temp_dir"
        exit 1
    fi
done

# clean up temporary folder
rm -rf "$temp_dir"

exit 0
