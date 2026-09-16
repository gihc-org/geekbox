#!/bin/sh
# install.sh — installerer DejaVu-hooken i .git/hooks (én gang pr. klon).
#
# Bevidst en rigtig fil frem for et symlink ind i arbejdstræet. Et symlink
# dækker over problemet: skifter man til en branch hvor
# devuan/hooks/pre-commit ikke findes, dingler det, og git springer det tavst
# over — så tror man reglen er aktiv. Wrapperen nedenfor siger til i stedet.
#
# Brug: bash devuan/hooks/install.sh
set -eu

repo=$(git rev-parse --show-toplevel)
dest="$repo/.git/hooks/pre-commit"

# Fjern først: hvis der ligger et symlink (den gamle installation), ville
# "cat >" følge det og overskrive den versionsstyrede fil i arbejdstræet.
rm -f "$dest"

cat > "$dest" <<'WRAPPER'
#!/bin/sh
# Installeret af devuan/hooks/install.sh. Redigér ikke denne fil —
# ret devuan/hooks/install.sh og kør den igen.
repo=$(git rev-parse --show-toplevel) || exit 0
hook="$repo/devuan/hooks/pre-commit"
if [ ! -x "$hook" ]; then
    echo "ADVARSEL: devuan/hooks/pre-commit findes ikke på denne branch." >&2
    echo "          DejaVu-reglen er IKKE tjekket for denne commit." >&2
    exit 0
fi
exec "$hook"
WRAPPER

chmod 755 "$dest"
echo "OK: DejaVu-hook installeret i $dest"
