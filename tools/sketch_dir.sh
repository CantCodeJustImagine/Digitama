# Sourced, not run: prints the directory to hand arduino-cli for this sketch.
#
#   . tools/sketch_dir.sh
#   SKETCH="$(tama_sketch_dir "$ROOT")"
#
# arduino-cli refuses a sketch whose folder is not named after its main file:
# "main file missing from sketch: .../Digitama/Digitama.ino". The main file is
# TamaDigi.ino, so a checkout only compiles from a folder called TamaDigi -- and
# this fork's repository is called Digitama, which is what `git clone` and
# GitHub Actions both name the folder. Every compile in CI (the test gate, the
# Pages installer, the release) and on a fresh clone failed before reaching the
# compiler. A symlink does not help: arduino-cli resolves it first.
#
# So a checkout under any other name compiles from a fresh copy in build/sketch/
# (gitignored), made at every call so it can never go stale. Arduino builds the
# root-level files plus src/, so that is exactly what gets copied. It is the ONE
# answer to the question, for build_web.sh, flash.sh and tests/run.sh alike.
tama_sketch_dir() {
  local root="$1" stage
  if [ "$(basename "$root")" = TamaDigi ]; then
    printf '%s\n' "$root"
    return 0
  fi
  stage="$root/build/sketch/TamaDigi"
  rm -rf "$stage" && mkdir -p "$stage" || return 1
  find "$root" -maxdepth 1 -type f -exec cp {} "$stage/" \; || return 1
  if [ -d "$root/src" ]; then cp -R "$root/src" "$stage/" || return 1; fi
  [ -f "$stage/TamaDigi.ino" ] || { echo "tama_sketch_dir: no TamaDigi.ino in $root" >&2; return 1; }
  printf '%s\n' "$stage"
}
