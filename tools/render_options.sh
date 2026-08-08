#!/usr/bin/env bash
# Render a screenshot for every options/*.json.
#
#   tools/render_options.sh [first] [last]
#
# The configs are ordered so that all the schemes sharing a background are
# adjacent; the layer regeneration is skipped whenever the background has not
# changed since the previous one, which is what keeps this to a few minutes
# rather than an hour.
#
# Main.qml and tools/make_bezel_layers.py are restored on exit, so running this
# does not leave the tree on whichever scheme happened to be last.
set -u

cd "$(dirname "$0")/.."
BUILD=build/Desktop_Qt_6_10_3-Debug
FIRST=${1:-1}
LAST=${2:-999}

SAVE=$(mktemp -d)
cp Main.qml "$SAVE/Main.qml"
cp tools/make_bezel_layers.py "$SAVE/make_bezel_layers.py"
restore() {
    pkill -x appCluster 2>/dev/null
    cp "$SAVE/Main.qml" Main.qml
    cp "$SAVE/make_bezel_layers.py" tools/make_bezel_layers.py
    cmake --build "$BUILD" -j8 >/dev/null 2>&1
    rm -rf "$SAVE"
    echo "restored Main.qml and make_bezel_layers.py"
}
trap restore EXIT

prev_bg=""
ok=0
for cfg in options/*.json; do
    id=$(basename "$cfg" .json)
    n=$((10#$id))
    [ "$n" -lt "$FIRST" ] && continue
    [ "$n" -gt "$LAST" ] && continue

    bg=$(python3 -c "import json;print(json.load(open('$cfg'))['background'])")
    python3 tools/apply_option.py "$cfg" >/dev/null

    if [ "$bg" != "$prev_bg" ]; then
        python3 tools/make_bezel_layers.py >/dev/null
        prev_bg=$bg
    fi
    cmake --build "$BUILD" -j8 >/dev/null 2>&1 || { echo "$id BUILD FAILED"; continue; }

    pkill -x appCluster 2>/dev/null
    sleep 0.3
    CLUSTER_SPEED=160 ./"$BUILD"/deploy/appCluster >/dev/null 2>&1 &
    sleep 3.2
    wid=$(xwininfo -root -tree | grep -F '"Cluster": ' | grep -oP '^\s*\K0x[0-9a-f]+')
    if [ -n "$wid" ]; then
        import -window "$wid" "options/$id.png" 2>/dev/null && ok=$((ok + 1))
    else
        echo "$id NO WINDOW"
    fi
    pkill -x appCluster 2>/dev/null
    sleep 0.2
    echo "$id $bg done"
done

echo "rendered $ok screenshots"
