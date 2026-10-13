#!/usr/bin/env bash
# lib/opus and lib/WDL must stay submodule gitlinks (mode 160000): a symlink
# (120000) or plain files committed in their place builds locally but breaks
# every fresh checkout (5d869f1 did, 19dd1f1 fixed it). Checks the index, so
# it works in CI (index = HEAD) and before a commit. Exit 1 on a problem.
#   tools/check_gitlinks.sh [repo dir]
set -euo pipefail
cd "${1:-$(dirname "$0")/..}"
bad=0
while read -r path; do
  mode="$(git ls-files -s -- "$path" | awk 'NR==1 {print $1}')"
  if [ "$mode" != 160000 ]; then
    echo "check_gitlinks: $path is ${mode:-not in the index}, must be a submodule gitlink (160000)" >&2
    bad=1
  fi
done < <(git config -f .gitmodules --get-regexp '^submodule\..*\.path$' | awk '{print $2}')
links="$(git ls-files -s -- lib | awk '$1 == "120000" {print $4}')"
if [ -n "$links" ]; then
  echo "check_gitlinks: symlinks under lib/ (never commit these): $links" >&2
  bad=1
fi
[ "$bad" = 0 ] && echo "check_gitlinks: lib/* are submodule gitlinks"
exit "$bad"
