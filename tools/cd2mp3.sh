#!/bin/bash
# Rip the audio tracks of a single-BIN cue image to TrackNN.mp3 for the shim.
# Usage: cd2mp3.sh <image.cue> <out-dir> [lame options, default -V 2]
# Writes <out-dir>/Audio.cue (a copy, kept as the TOC) and one MP3 per audio
# track. The BIN is then no longer needed in <out-dir>.
set -euo pipefail

[ $# -ge 2 ] || { sed -n '2,5s/^# //p' "$0"; exit 1; }
cue=$1 out=$2
shift 2
opts=("$@")
[ ${#opts[@]} -gt 0 ] || opts=(-V 2)

bin=$(tr -d "\r" < "$cue" | sed -n 's/^FILE "\(.*\)" BINARY.*/\1/p' | head -1)
[ -n "$bin" ] || { echo "no FILE ... BINARY line in $cue" >&2; exit 1; }
case $bin in /*) ;; *) bin=$(dirname "$cue")/$bin ;; esac
total=$(( $(stat -c %s "$bin") / 2352 ))

# "track type start-frame" per track, in cue order.
mapfile -t tracks < <(awk '
    { sub(/\r$/, "") }
    $1 == "TRACK" { n = $2 + 0; type = $3 }
    $1 == "INDEX" && $2 == "01" { split($3, t, ":"); print n, type, (t[1] * 60 + t[2]) * 75 + t[3] }
' "$cue")

mkdir -p "$out"
cp "$cue" "$out/Audio.cue"
for i in "${!tracks[@]}"; do
    read -r n type start <<< "${tracks[$i]}"
    [ "$type" = AUDIO ] || continue
    if [ $((i + 1)) -lt ${#tracks[@]} ]; then
        read -r _ _ end <<< "${tracks[$((i + 1))]}"
    else
        end=$total
    fi
    mp3=$(printf '%s/Track%02d.mp3' "$out" "$n")
    echo "track $n: $((end - start)) frames -> $mp3"
    dd if="$bin" bs=2352 skip="$start" count=$((end - start)) status=none \
        | lame --quiet -r -s 44.1 --bitwidth 16 --signed --little-endian -m j "${opts[@]}" - "$mp3"
done
