#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$project_dir/build-linux"
package_dir="$project_dir/dist/linux"

command -v cmake >/dev/null || {
    echo "Error: cmake is required."
    exit 1
}

generator="Unix Makefiles"
if command -v ninja >/dev/null; then
    generator="Ninja"
fi

cmake -S "$project_dir" -B "$build_dir" -G "$generator" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel

rm -rf "$package_dir"
cmake --install "$build_dir" --prefix "$package_dir"

echo
echo "Release build complete: $package_dir/bin/CrossTerm"