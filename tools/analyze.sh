#!/usr/bin/env bash
# Size, stack and static analysis of a firmware build. Hardware-free.
#
#   tools/analyze.sh [BUILD_DIR] [--top N] [--tidy] [--cppcheck] [--all]
#
# BUILD_DIR defaults to tools/dev_firmware.sh's ~/.cache/ds5dongle/build-pico2_w.
#   size      section sizes, the RAM image split (libopus / other .time_critical
#             / the rest of .data), and the N biggest symbols in RAM and flash
#   stack     the N deepest stack frames, when the build was configured with
#             -DENABLE_STACK_USAGE=ON (.su files next to the objects)
#   --tidy    clang-tidy over our src/*.cpp from compile_commands.json
#   --cppcheck  cppcheck over the same files
# Neither tool needs root: tools/analyze.sh --install puts both into
# ~/.cache/ds5dongle/tools/venv (pip wheels: clang-tidy, cppcheck).
set -eu  # no pipefail: "| head" closing early is fine here
FW="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${XDG_CACHE_HOME:-$HOME/.cache}/ds5dongle/build-pico2_w"
TOP=20
TIDY=false
CPPCHECK=false
VENV="${XDG_CACHE_HOME:-$HOME/.cache}/ds5dongle/tools/venv"
while [ $# -gt 0 ]; do
  case "$1" in
    --top) TOP="$2"; shift ;;
    --tidy) TIDY=true ;;
    --cppcheck) CPPCHECK=true ;;
    --all) TIDY=true; CPPCHECK=true ;;
    --install)
      python3 -m venv "$VENV"
      "$VENV/bin/pip" install -q clang-tidy==21.1.6 cppcheck==1.5.3
      echo "Installed into $VENV"; exit 0 ;;
    -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) BUILD="$1" ;;
  esac
  shift
done
ELF="$BUILD/ds5-bridge-oled.elf"
MAP="$ELF.map"
[ -f "$ELF" ] || { echo "No $ELF (build first: tools/dev_firmware.sh)" >&2; exit 1; }
SIZE=arm-none-eabi-size
NM=arm-none-eabi-nm

echo "== Sections ($ELF)"
$SIZE -A "$ELF" | awk '$1 ~ /^\.(text|rodata|data|bss|heap|ram_vector_table|boot2|ARM)/ || $1 == "section" { printf "  %-20s %9s\n", $1, $2 }'
# .data is RAM at run time and also stored in flash (copied at boot).
$SIZE -A "$ELF" | awk '
  $1 ~ /^\.(boot2|text|rodata|ARM\.extab|ARM\.exidx|binary_info|data)$/ { flash += $2 }
  $1 ~ /^\.(ram_vector_table|data|bss|heap|scratch_x|scratch_y|stack1_dummy|stack_dummy)$/ { ram += $2 }
  END { printf "  flash image %9d   static RAM (incl. .data, .bss, heap reserve, stacks) %9d\n", flash, ram }'

if [ -f "$MAP" ]; then
  echo
  echo "== RAM image (.data) by origin, from the link map"
  # Input sections are "<name> <addr> <size> <object>" (or name alone, the rest
  # on the next line). Only sections placed in RAM (0x20000000..) count.
  awk '
    function add(name, addr, size, obj) {
      if (addr !~ /^0x0*2[0-9a-f]{7}$/) return
      sz = strtonum(size)
      if (name ~ /^\.time_critical\.opus/ || obj ~ /libopus\.a/) opus += sz
      else if (name ~ /^\.time_critical/) tc += sz
      else if (name ~ /^\.data/) data += sz
    }
    /^ \.[^ ]+$/ { pending = $1; next }
    pending != "" && $1 ~ /^0x/ { add(pending, $1, $2, $3); pending = ""; next }
    /^ \.[^ ]+ +0x/ { add($1, $2, $3, $4) }
    { pending = "" }
    END {
      printf "  libopus (code+tables)  %9d\n", opus
      printf "  other .time_critical   %9d\n", tc
      printf "  .data variables        %9d\n", data
    }' "$MAP"
fi

echo
echo "== $TOP biggest symbols in RAM (0x2...)"
$NM -S --size-sort -C "$ELF" | awk '$1 ~ /^2/ && $4 !~ /^__/ && $4 != "end" { printf "  %8d  %s  %s\n", strtonum("0x" $2), $3, substr($0, index($0, $4)) }' | sort -rn | head -n "$TOP"
echo
echo "== $TOP biggest symbols in flash (0x1...)"
$NM -S --size-sort -C "$ELF" | awk '$1 ~ /^1/ && $4 !~ /^__/ && $4 != "end" { printf "  %8d  %s  %s\n", strtonum("0x" $2), $3, substr($0, index($0, $4)) }' | sort -rn | head -n "$TOP"

echo
SU=$(find "$BUILD" -name "*.su" -print -quit 2>/dev/null)
if [ -n "$SU" ]; then
  echo "== $TOP deepest stack frames (-fstack-usage; per function, not call chains)"
  find "$BUILD" -name '*.su' -print0 | xargs -0 cat | awk -F'\t' '{ n = split($1, p, ":"); printf "%8d  %-9s %s:%s\n", $2, $3, gensub(/.*\//, "", 1, p[1]), p[n] }' | sort -rn | head -n "$TOP"
  echo "  ...of which in our src/:"
  find "$BUILD/CMakeFiles/ds5-bridge.dir/src" -name '*.su' -print0 2>/dev/null | xargs -0 cat | awk -F'\t' '{ n = split($1, p, ":"); printf "%8d  %-9s %s:%s\n", $2, $3, gensub(/.*\//, "", 1, p[1]), p[n] }' | sort -rn | head -n "$TOP"
  echo "  (dynamic/bounded = alloca or VLA in the frame; core1's stack is audio_core1_stack in src/audio.cpp)"
else
  echo "== Stack: configure with -DENABLE_STACK_USAGE=ON for the deepest-frame list"
fi

tool() {  # tool NAME -> path, or empty
  if [ -x "$VENV/bin/$1" ]; then echo "$VENV/bin/$1"; elif command -v "$1" >/dev/null; then command -v "$1"; fi
}
SRCS=$(cd "$FW" && ls src/*.cpp | sed "s|^|$FW/|")

if $TIDY; then
  CT=$(tool clang-tidy)
  [ -n "$CT" ] || { echo "clang-tidy not found (tools/analyze.sh --install)" >&2; exit 1; }
  echo
  echo "== clang-tidy ($($CT --version | grep -o 'version [0-9.]*'))"
  # The ARM GCC's own headers: clang can't find them from the cross compile line.
  GCC_INC=$(arm-none-eabi-g++ -mcpu=cortex-m33 -xc++ -E -v /dev/null 2>&1 | awk '/^#include <...> search starts here/{f=1;next} /^End of search list/{f=0} f{print "--extra-arg=-isystem" $1}')
  # bugprone/clang-analyzer/performance, minus what is noise for this code base
  # (C-style register/flag arithmetic, short names, magic numbers in protocol tables).
  CHECKS='-*,bugprone-*,clang-analyzer-*,performance-*,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions,-bugprone-implicit-widening-of-multiplication-result,-bugprone-reserved-identifier,-bugprone-assignment-in-if-condition,-clang-analyzer-security.insecureAPI.*,-bugprone-switch-missing-default-case,-bugprone-branch-clone,-bugprone-multi-level-implicit-pointer-conversion,-performance-enum-size,-performance-no-int-to-ptr,-performance-avoid-endl,-clang-analyzer-core.FixedAddressDereference'
  # shellcheck disable=SC2086
  $CT -p "$BUILD" --quiet --checks="$CHECKS" $GCC_INC --extra-arg=--target=arm-none-eabi \
      --extra-arg=-Wno-unknown-warning-option --extra-arg=-Wno-unused-command-line-argument \
      --header-filter="$FW/src/.*" $SRCS 2>/dev/null \
    | grep -E "^$FW/src/.*(warning|error):" | sed "s|$FW/||" | sort -u || true
fi

if $CPPCHECK; then
  CC=$(tool cppcheck)
  [ -n "$CC" ] || { echo "cppcheck not found (tools/analyze.sh --install)" >&2; exit 1; }
  echo
  echo "== cppcheck ($($CC --version))"
  TMP=$(mktemp -d)
  trap 'rm -rf "$TMP"' EXIT
  # Our sources only (the SDK, BTstack, TinyUSB and libopus are not ours to fix).
  $CC --project="$BUILD/compile_commands.json" --file-filter="$FW/src/*" \
      --enable=warning,performance,portability --inline-suppr --std=c++20 \
      --suppress=missingIncludeSystem --suppress=unmatchedSuppression \
      --suppress='*:*/pico-sdk/*' --suppress='*:*/lib/*' \
      -j "$(nproc)" --cppcheck-build-dir="$TMP" --quiet \
      --template='{file}:{line}: {severity}: {message} [{id}]' 2>&1 | sed -E 's|^.*/src/|src/|' | sort -u
fi
