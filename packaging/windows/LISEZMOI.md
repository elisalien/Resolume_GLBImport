# GLB Model Source — Windows 64 bits

Source FFGL pour Resolume qui charge un fichier `.glb` / `.gltf` directement dans
un clip : décimation du maillage, animations du fichier, modes de rendu, et
paramètres pilotables par l'audio.

## Installer

Double-clique **`installer.bat`**, et c'est tout. Il s'occupe de deux choses :

1. **Le runtime Visual C++ 2015-2022 (x64).** Le plugin en dépend
   (`MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`). S'il manque,
   Resolume ignore le plugin *sans afficher d'erreur* — c'est le piège classique.
   Le script vérifie, et le télécharge depuis Microsoft si besoin. La plupart des
   machines qui font tourner Resolume l'ont déjà.
2. **La copie du plugin** dans `Documents\Resolume Arena\Extra Effects`. Le script
   retrouve le vrai dossier Documents même s'il est redirigé vers OneDrive.

Ferme Resolume avant de lancer l'installation : un plugin chargé est verrouillé,
et de toute façon le scan des plugins ne se fait qu'au démarrage.

Installation manuelle si tu préfères : copie `GlbSource.dll` dans
`Documents\Resolume Arena\Extra Effects\` (ou `Resolume Avenue` selon ta version)
et relance Resolume.

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
Anti Alias (Off / 2x / 4x / 8x). Le wireframe et les points se superposent à
n'importe quel mode.

**Material** — Tint, Light Yaw / Pitch / Intensity, Ambient, Metallic, Roughness,
Background (alpha du fond, 0 = transparent pour empiler sur d'autres couches).

**Audio** — Band (Volume / Low / Mid / High), Gain, Smooth, et quatre liaisons
Audio To Scale / Spin / Anim / Displace.

### Deux façons de réagir au son

**Interne** — le plugin demande le FFT à Resolume et applique lui-même les quatre
liaisons `Audio To …`. Rien à câbler, ça marche dès qu'un clip joue.

**Resolume** — clic droit sur n'importe quel paramètre → Audio Analysis, avec
bandes L/M/H, gain, fall et direction. Plus fin, et disponible sur *tous* les
paramètres, y compris ceux que le mapping interne ne couvre pas (Field Of View,
Wire Width, Roughness…). Les deux se cumulent.

## Ce qui marche, ce qui ne marche pas

Pris en charge : `.glb` et `.gltf`, buffers externes et base64, textures embarquées
ou externes, hiérarchie de nœuds, PBR metallic-roughness (base color, normal map,
metallic-roughness, emissive), alpha OPAQUE et MASK, animations de nœuds en
LINEAR / STEP / CUBICSPLINE, skinning GPU jusqu'à 128 joints.

Pas pris en charge : morph targets / shape keys, caméras et lumières du fichier,
alpha BLEND trié en profondeur, extensions KHR autres que metallic-roughness.

## Prérequis

- Windows 64 bits
- Resolume Arena ou Avenue **7.4.1 minimum** (la liste des clips d'animation
  utilise les *dynamic option elements*, arrivés dans cette version)
- Un GPU OpenGL 4.1 avec geometry shaders — soit à peu près tout depuis 2012
- Visual C++ Redistributable 2015-2022 x64 (l'installeur s'en charge)

## Si ça coince

**Le plugin n'apparaît pas dans Resolume.** Ouvre la console avec `Ctrl+Shift+L` :
Resolume y liste les plugins refusés et la raison. Le plugin y écrit aussi ses
propres messages de chargement (`GLB Source: loaded …` avec le nombre de triangles
et de clips), ce qui permet de voir tout de suite si le fichier a été lu.

**Le modèle est noir.** Soit Light Intensity et Ambient sont à zéro, soit le
matériau est full metallic sans environnement : monte Ambient, ou baisse Metallic.

**Le modèle n'apparaît pas à l'écran.** Auto Fit coupé sur un modèle en unités
Blender (mètres) avec Camera Distance à 3 : il est soit énorme soit minuscule.
Réactive Auto Fit.

**Mesh Density ne change rien.** Un maillage déjà très pauvre, ou dont toutes les
arêtes sont des bords ouverts, ne se simplifie pas. La valeur affichée sous le
slider donne le nombre réel de triangles, c'est le meilleur diagnostic.

**Le fil de fer scintille sur la surface.** Z-fighting résiduel ; augmenter
légèrement Wire Width aide.

## Partager une composition

Le chemin du modèle est stocké en **absolu** dans la composition. Une compo
sauvegardée ici s'ouvrira ailleurs avec un slot de modèle vide : il faut livrer
les `.glb` avec, et re-pointer le fichier sur l'autre machine.

## Licences

Le plugin embarque cgltf (MIT), stb_image (domaine public) et meshoptimizer (MIT).
Voir `THIRD_PARTY.md`. Construit sur le SDK FFGL de Resolume.
