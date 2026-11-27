# Shared by the scripts that read cores/*.env. Sourced, not run.
root=$(readlink -f "$(dirname "${BASH_SOURCE[0]}")/..")

die() { echo "$(basename "$0"): $*" >&2; exit 1; }

# pin NAME KEY: a value from cores/NAME.env
pin() { (. "$root/cores/$1.env"; eval "echo \"\$$2\""); }

# clone NAME DIR: the pinned tag of the core, checked against the pinned commit
clone() {
  local repo tag commit
  repo=$(pin "$1" REPO) tag=$(pin "$1" TAG) commit=$(pin "$1" COMMIT)
  git clone -q -c advice.detachedHead=false --depth 1 --branch "$tag" "https://github.com/$repo.git" "$2"
  [ "$(git -C "$2" rev-parse HEAD)" = "$commit" ] || die "$repo $tag is not commit $commit"
}
