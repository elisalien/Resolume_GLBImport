#!/bin/bash
# ---------------------------------------------------------------------------
#  GLB Model Source — installation pour Resolume Arena (macOS)
#
#  Le bundle est deja compile (binaire universel arm64 + x86_64).
#  Ce script enleve la quarantaine de Gatekeeper puis l'installe.
#
#  Double-clique ce fichier. La premiere fois macOS refusera peut-etre :
#  clic droit -> Ouvrir, puis confirme.
# ---------------------------------------------------------------------------
set -u
cd "$(dirname "$0")" || exit 1

cyan()  { printf '\n\033[36m== %s\033[0m\n' "$1"; }
ok()    { printf '   \033[32m[ok]\033[0m %s\n' "$1"; }
warn()  { printf '   \033[33m[!] \033[0m %s\n' "$1"; }
fail()  { printf '   \033[31m[X] \033[0m %s\n' "$1"; }
die()   { fail "$1"; printf '\nAppuie sur Entree pour fermer.\n'; read -r _; exit 1; }

printf '\033[1mGLB Model Source - installation\033[0m\n'

BUNDLE="GlbSource.bundle"
[ -d "$BUNDLE" ] || die "GlbSource.bundle est introuvable a cote de ce script. Dezippe l'archive entiere."

# ------------------------------------------------------------ 1. architectures
cyan "Verification du plugin"
BIN="$BUNDLE/Contents/MacOS/GlbSource"
[ -f "$BIN" ] || die "Le bundle est incomplet : $BIN manquant."
if command -v lipo >/dev/null 2>&1; then
    ARCHS=$(lipo -archs "$BIN" 2>/dev/null || echo "?")
    ok "Architectures : $ARCHS"
    HOST=$(uname -m)
    case "$ARCHS" in
        *"$HOST"*) : ;;
        *) warn "Ce Mac est en $HOST, absent du binaire. Resolume refusera le plugin." ;;
    esac
else
    warn "lipo indisponible, verification des architectures ignoree."
fi

# --------------------------------------------------- 2. Resolume doit etre ferme
cyan "Verification que Resolume est ferme"
if pgrep -x "Arena" >/dev/null 2>&1 || pgrep -x "Avenue" >/dev/null 2>&1; then
    fail "Resolume tourne. Ferme-le puis relance ce script :"
    die  "le scan des plugins ne se fait qu'au demarrage de toute facon."
fi
ok "Ferme"

# ------------------------------------------------------------- 3. installation
cyan "Installation"
TARGET="$HOME/Documents/Resolume Arena/Extra Effects"
if [ ! -d "$HOME/Documents/Resolume Arena" ] && [ -d "$HOME/Documents/Resolume Avenue" ]; then
    TARGET="$HOME/Documents/Resolume Avenue/Extra Effects"
    warn "Arena introuvable, installation dans le dossier d'Avenue."
fi
mkdir -p "$TARGET" || die "Impossible de creer $TARGET"

rm -rf "$TARGET/GlbSource.bundle"
cp -R "$BUNDLE" "$TARGET/GlbSource.bundle" || die "Copie impossible vers $TARGET"

# Sans ca, un bundle sorti d'une archive telechargee reste marque par Gatekeeper
# et Resolume echoue a le charger, sans message explicite.
xattr -dr com.apple.quarantine "$TARGET/GlbSource.bundle" 2>/dev/null
ok "Installe dans $TARGET"

printf '\n\033[32mTermine.\033[0m\n'
printf "Lance Resolume, puis onglet Sources du panneau Effects -> GLB Model Source.\n"
printf "Glisse-la dans un clip, puis parametre Model File -> choisis ton .glb.\n\n"
printf "Si elle n'apparait pas : Cmd+Shift+L ouvre la console de Resolume,\n"
printf "qui liste les plugins refuses et la raison du refus.\n"
printf '\nAppuie sur Entree pour fermer.\n'
read -r _
