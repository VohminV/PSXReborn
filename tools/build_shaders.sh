#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-"$root/shaders"}
header=${2:-"$root/src/generated/spirv_data.h"}
mkdir -p "$out" "$(dirname -- "$header")"
cd "$root"

glslangValidator -V --target-env vulkan1.1 -o "$out/present.vert.spv" shaders/present.vert
glslangValidator -V --target-env vulkan1.1 -o "$out/present.frag.spv" shaders/present.frag
glslangValidator -V --target-env vulkan1.1 -Ithird_party/fsr1 -o "$out/easu.comp.spv" shaders/easu.comp
glslangValidator -V --target-env vulkan1.1 -Ithird_party/fsr1 -o "$out/rcas.comp.spv" shaders/rcas.comp
python3 tools/embed_spirv.py --spv-dir "$out" --output "$header"
