#!/bin/bash
# sync_shared_op.sh - copies a canonical "shared op" source (per
# shared-ops-manifest.txt, this same directory) into a target project
# directory. Replaces build.sh referencing shared-ops/ directly across a
# directory boundary - every project keeps a real, local copy instead,
# fully self-contained/solo-shippable. Run this explicitly whenever the
# canonical source changes and a project's own local copy needs to catch
# up - NOT run automatically at build time.
#
# Usage: sync_shared_op.sh <op_name> <target_dir> [target_filename]
#   op_name        - name as it appears in shared-ops-manifest.txt
#   target_dir     - directory to copy into (created if missing)
#   target_filename - optional; defaults to the canonical file's own
#                     basename (so "prisc+x.c" lands as "prisc+x.c", not
#                     renamed to "prisc+x")
#
# Examples:
#   sync_shared_op.sh chtpm_parser_pal ../../y0.mutaclsym+06.01/mutaclsym/system
#   sync_shared_op.sh lib ../../y0.mutaclsym+06.01/mutaclsym/ops/lib
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MANIFEST="$SCRIPT_DIR/shared-ops-manifest.txt"

if [ $# -lt 2 ]; then
    echo "Usage: $0 <op_name> <target_dir> [target_filename]" >&2
    echo "See $MANIFEST for available op_name values." >&2
    exit 1
fi

OP_NAME="$1"
TARGET_DIR="$2"
TARGET_FILENAME="${3:-}"

if [ ! -f "$MANIFEST" ]; then
    echo "Manifest not found: $MANIFEST" >&2
    exit 1
fi

# Parse manifest: first non-comment, non-blank line whose first
# pipe-delimited field (trimmed) matches OP_NAME exactly. Pure shell
# whitespace trimming (not xargs) - manifest comment text may contain
# quotes that xargs treats specially.
trim() {
    local s="$1"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

REL_PATH=""
while IFS='|' read -r name path _rest; do
    name="$(trim "$name")"
    [ -z "$name" ] && continue
    case "$name" in \#*) continue ;; esac
    if [ "$name" = "$OP_NAME" ]; then
        REL_PATH="$(trim "$path")"
        break
    fi
done < "$MANIFEST"

if [ -z "$REL_PATH" ]; then
    echo "Unknown op_name '$OP_NAME' - not found in $MANIFEST" >&2
    exit 1
fi

SRC="$SCRIPT_DIR/$REL_PATH"
if [ ! -e "$SRC" ]; then
    echo "Canonical source missing on disk: $SRC" >&2
    exit 1
fi

mkdir -p "$TARGET_DIR"

if [ -d "$SRC" ]; then
    # Directory entry (e.g. "lib") - copy recursively, replacing any
    # existing copy at that name under TARGET_DIR.
    DEST="$TARGET_DIR/$(basename "$SRC")"
    rm -rf "$DEST"
    cp -r "$SRC" "$DEST"
    echo "Synced (dir): $SRC -> $DEST"
else
    DEST_NAME="${TARGET_FILENAME:-$(basename "$SRC")}"
    DEST="$TARGET_DIR/$DEST_NAME"
    cp "$SRC" "$DEST"
    echo "Synced (file): $SRC -> $DEST"
fi
