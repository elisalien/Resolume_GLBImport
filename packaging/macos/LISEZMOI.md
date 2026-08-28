# GLB Model Source — macOS

Source FFGL pour Resolume qui charge un fichier `.glb` / `.gltf` directement dans
un clip : décimation du maillage, animations du fichier, modes de rendu, et
paramètres pilotables par l'audio.

## Installer

Le plugin est **déjà compilé** en binaire universel (Apple Silicon + Intel), par
l'intégration continue du dépôt GitHub, à partir des mêmes sources que la version
Windows.

Double-clique **`installer.command`**.

macOS affichera probablement *« impossible d'ouvrir car il provient d'un
développeur non identifié »* — c'est normal pour un script sorti d'une archive
téléchargée. Fais **clic droit → Ouvrir**, puis confirme. Alternative en Terminal :

```bash
xattr -dr com.apple.quarantine .
./installer.command
```

Le script vérifie que les architectures du binaire couvrent bien ce Mac, enlève
l'attribut de quarantaine — sans quoi Resolume échoue à charger le bundle sans
message explicite — et copie `GlbSource.bundle` dans
`~/Documents/Resolume Arena/Extra Effects`.

Ferme Resolume avant : le scan des plugins ne se fait qu'au démarrage.

### Compiler soi-même

Le code source est sur https://github.com/elisalien/Resolume_GTBImport

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --build build --target install-plugin
```

Il faut les Xcode Command Line Tools (`xcode-select --install`), CMake 3.20+ et
Git. Le SDK FFGL de Resolume est téléchargé automatiquement dans `build/_deps`.

## Utiliser

Onglet **Sources** du panneau Effects → **GLB Model Source** → glisser dans un clip.
Puis paramètre **Model File** → parcourir → choisir un `.glb` ou un `.gltf`.

Un `.glb` est autonome (géométrie + textures dans un seul fichier), c'est le format
à privilégier en live. Un `.gltf` va chercher ses `.bin` et ses textures dans le
dossier à côté de lui.

### Les groupes de paramètres

**Model** — Model File, Reload, Mesh Density (décimation, l'affichage sous le
slider donne le nombre réel de triangles), Auto Fit.

**Transform** — Scale, Rotation X/Y/Z, Spin X/Y/Z en tours par seconde,
Position X/Y, Camera Distance, Field Of View.

**Animation** — Clip (la liste se remplit avec les noms des animations du fichier
une fois chargé), Anim Speed, Play, Restart.

**Render** — Render Mode (Shaded / Flat Low Poly / Unlit / Wireframe Only),
Wireframe + largeur + opacité, Points + taille, Displace, Backface Cull,
Anti Alias. Le wireframe et les points se superposent à n'importe quel mode.

**Material** — Tint, Light Yaw / Pitch / Intensity, Ambient, Metallic, Roughness,
Background (alpha du fond, 0 = transparent pour empiler sur d'autres couches).

**Audio** — Band (Volume / Low / Mid / High), Gain, Smooth, et quatre liaisons
Audio To Scale / Spin / Anim / Displace.

### Deux façons de réagir au son

**Interne** — le plugin demande le FFT à Resolume et applique lui-même les quatre
liaisons `Audio To …`. Rien à câbler, ça marche dès qu'un clip joue.

**Resolume** — clic droit sur n'importe quel paramètre → Audio Analysis, avec
bandes L/M/H, gain, fall et direction. Plus fin, et disponible sur *tous* les
paramètres. Les deux se cumulent.

## Ce qui marche, ce qui ne marche pas

Pris en charge : `.glb` et `.gltf`, buffers externes et base64, textures embarquées
ou externes, hiérarchie de nœuds, PBR metallic-roughness (base color, normal map,
metallic-roughness, emissive), alpha OPAQUE et MASK, animations de nœuds en
LINEAR / STEP / CUBICSPLINE, skinning GPU jusqu'à 128 joints.

Pas pris en charge : morph targets / shape keys, caméras et lumières du fichier,
alpha BLEND trié en profondeur, extensions KHR autres que metallic-roughness.

## Prérequis

- macOS 10.15 ou plus récent
- Resolume Arena ou Avenue **7.4.1 minimum** (la liste des clips d'animation
  utilise les *dynamic option elements*, arrivés dans cette version)
- Sur Apple Silicon, Resolume 7.11+ tourne en ARM natif et refuse un plugin
  Intel seul — d'où le binaire universel que produit le script.

Le rendu vise OpenGL 4.1 core, qui est exactement le plafond de macOS : tous les
appels utilisés ont été vérifiés contre cette version, geometry shaders compris.

## Si ça coince

**Le plugin n'apparaît pas dans Resolume.** Ouvre la console avec `Cmd+Shift+L` :
Resolume y liste les plugins refusés et la raison. Le plugin y écrit aussi ses
messages de chargement (`GLB Source: loaded …` avec le nombre de triangles et de
clips). Vérifie aussi que `lipo -archs` sur le binaire du bundle donne bien
`x86_64 arm64` si tu es sur Apple Silicon.

**Resolume refuse le bundle sans rien dire.** C'est presque toujours la
quarantaine Gatekeeper. Relance `installer.command`, ou à la main :
`xattr -dr com.apple.quarantine "$HOME/Documents/Resolume Arena/Extra Effects/GlbSource.bundle"`.

**Le modèle est noir.** Soit Light Intensity et Ambient sont à zéro, soit le
matériau est full metallic sans environnement : monte Ambient, ou baisse Metallic.

**Le modèle n'apparaît pas à l'écran.** Auto Fit coupé sur un modèle en unités
Blender (mètres) avec Camera Distance à 3 : il est soit énorme soit minuscule.
Réactive Auto Fit.

**Mesh Density ne change rien.** Un maillage déjà très pauvre, ou dont toutes les
arêtes sont des bords ouverts, ne se simplifie pas. La valeur affichée sous le
slider donne le nombre réel de triangles, c'est le meilleur diagnostic.

## Partager une composition

Le chemin du modèle est stocké en **absolu** dans la composition. Une compo
sauvegardée sur une machine s'ouvrira ailleurs avec un slot de modèle vide : il
faut livrer les `.glb` avec, et re-pointer le fichier.

## Licences

Le plugin embarque cgltf (MIT), stb_image (domaine public) et meshoptimizer (MIT).
Voir `THIRD_PARTY.md`. Construit sur le SDK FFGL de Resolume.
