#!/bin/sh
# check_no_emoji.sh — forbyder emojier/symbol-ikoner i .md-dokumentation
# (DejaVu Sans/Mono-pandoc-regel, se AGENTS.md "Dokumentation undervejs").
# Brug: devuan/check_no_emoji.sh [fil...]   (uden filer: alle sporede *.md)
set -u

PAT='[\x{1F000}-\x{1FAFF}\x{2700}-\x{27BF}\x{2600}-\x{26FF}\x{FE0F}]'

if [ "$#" -gt 0 ]; then
    files=$*
else
    files=$(git ls-files '*.md')
fi

hits=
for f in $files; do
    [ -f "$f" ] || continue
    h=$(rg -n --pcre2 "$PAT" "$f" 2>/dev/null) || continue
    hits="${hits}${h}
"
done

if [ -n "$hits" ]; then
    echo "FEJL: emoji/symbol-ikoner i dokumentation (DejaVu-regel):" >&2
    printf '%s' "$hits" >&2
    exit 1
fi
exit 0
