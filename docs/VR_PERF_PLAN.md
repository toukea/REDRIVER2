# Plan d'optimisation VR — REDRIVER2 (OpenXR, Quest 3 + Virtual Desktop, RTX 3060/4080 laptop)

**État (9 novembre 2026, branche `wheel-controller-vr-perf`)** : phase 0 implémentée (mesures `VR: perf`, voir VR_SUPPORT.md), non compilée sous Windows ni mesurée au casque. Les phases 1 à 4 attendent les premières mesures.

## Contexte

Le mode VR (`wheel-controller-vr`, commit `89b1b47b`) garde la simulation à 30 Hz : PsyCross enregistre les triangles PGXP de chaque image, puis `swapHook()` ([vr_openxr.cpp](../src_rebuild/utils/vr_openxr.cpp)) les rejoue pour chaque œil au rythme du casque avec la pose de tête récente. L'utilisateur constate au casque **des saccades régulières** et **un décor qui défile saccadé**. Objectif : une cadence stable à 80–90 Hz, un décor fluide et de la marge GPU, avec du rendu fovéal, sur des GPU portables.

## Diagnostic tiré du code (hypothèses à confirmer par la phase 0)

1. **Saccades régulières = image manquée à chaque pas de simulation.** Tout tourne sur un seul fil. Après le dernier `xrEndFrame`, le jeu exécute son pas de simulation, dessine l'image **plate** dans la fenêtre (`DrawAllSplits`, travail inutile en VR), enregistre l'image, puis ré-uploade tout le tampon (`glBufferSubData` sur un VBO encore utilisé par le GPU, ce qui peut bloquer). Ce n'est qu'après tout cela qu'il appelle `xrWaitFrame`. Si ce travail dépasse le reste de la période (11,1 ms à 90 Hz), une image saute toutes les 33 ms. À 72 ou 80 Hz, la fréquence n'est pas un multiple de 30 : la cadence est inégale (3-2-3…).
2. **Décor saccadé = contenu figé pendant 33 ms.** Entre deux pas, seule la différence de pose de tête est appliquée ; le mouvement de la voiture, lui, avance par paliers de 30 Hz.
3. **Coût GPU par œil élevé.** 2496×2688 par œil, avec des shaders PSX coûteux (lecture de palette CLUT et filtrage bilinéaire émulé). En plus, l'image enregistrée est parcourue **3 fois par œil** (passe monde, mini-carte, HUD), avec à chaque split des changements d'état redondants (`GR_VR_SetDisp`, `GR_VR_SetFlatProjection`).

## Phases (dans l'ordre ; chacune est mesurée avant la suivante)

Créer une branche `wheel-controller-vr-perf` depuis `wheel-controller-vr`, conformément à la règle de séparation des branches.

### Phase 0 — Instrumentation (prérequis)
- Dans `vr_openxr.cpp`, ajouter des chronos CPU (pas de jeu entre deux hooks, upload, rendu de chaque œil, `xrWaitFrame`) et des requêtes `GL_TIME_ELAPSED` par œil, lues en différé.
- Compter les images manquées à partir des écarts de `predictedDisplayTime` supérieurs à 1,5 période.
- Ajouter ces mesures à la ligne de statistiques existante, toutes les 5 s. Journaliser aussi la période détectée (Hz).
- Ajouter `perf_log=1` dans `vr.ini`.

### Phase 1 — Tenir la cadence (contre les saccades régulières)
1. **Ne plus dessiner l'image plate pendant le suivi de tête.** Ajouter `GR_VR_SetSkipFlat(int)` dans `patches/psycross-vr.patch`. Dans `DrawAllSplits`, l'enregistrement est conservé mais `DrawSplit` n'est plus appelé. L'activation se fait dans `VR_BeginTrackedFrame`. Le miroir du bureau reste fourni par `mirror()`, et le mode cinéma continue de dessiner l'image plate.
2. **Upload sans blocage** dans `VR_UploadRecord` : orpheliner le tampon (`glBufferData(NULL)` avant `SubData`) ou passer à un anneau de 2 VBO.
3. **Partitionner l'enregistrement.** `VR_RecordSplits` compte les sommets monde et 2D de chaque split. `GR_VR_DrawRecorded(pass)` saute alors les splits qui ne contiennent rien pour la passe en cours. `SetFlatProjection` et `SetDisp` ne sont plus appelés qu'une fois par shader et par passe, au lieu d'une fois par split.
4. **Boucle tenant compte du budget** dans `swapHook` : moyenne glissante du coût d'un pas de jeu. On sort de la boucle d'images assez tôt pour que le pas de jeu tienne avant le prochain `xrWaitFrame`. Le miroir n'est blitté qu'à la dernière image avant de rendre la main au jeu.
5. **Fréquence du casque multiple de 30.** Ajouter une clé `refresh_rate` dans `vr.ini`. Si le runtime propose `XR_FB_display_refresh_rate`, demander 90 Hz ; sinon, journaliser un avertissement et documenter « régler 90 ou 120 Hz dans Virtual Desktop ».
6. **Soumettre la profondeur** (`XR_KHR_composition_layer_depth`) : un swapchain de profondeur remplace le renderbuffer `D24S8`. Le runtime (VD ou Meta) peut ainsi faire une reprojection positionnelle correcte quand une image manque.

### Phase 2 — Fluidifier le décor (extrapolation du mouvement propre)
- Dans [vrview.c](../src_rebuild/Game/C/vrview.c), mémoriser à chaque pas la pose de la caméra du jeu (position et rotation) avec son horodatage. En déduire les vitesses linéaire et angulaire sur les deux derniers pas.
- Dans `drawEye`, pour un temps d'affichage `t`, calculer la transformation `T(t − t_pas)`, plafonnée à 1,5 pas. La passer au shader sous forme d'uniforme `VRExtrap` (mat4).
- Appliquer la transformation dans la passe 1 **seulement aux sommets situés au-delà de `attach_radius`** en espace caméra (environ 3 m, avec une transition douce). L'habitacle et la voiture du joueur, solidaires de la caméra, ne sont pas extrapolés.
- Ajouter la clé `motion_smoothing=1` dans `vr.ini`.
- Limite connue, à documenter : une voiture qui roule à la même vitesse que le joueur garde un léger à-coup à 30 Hz. Les bâtiments, la route et les véhicules garés deviennent fluides.
- Tests : étendre `tests/vr_math_tests.cpp` pour l'extrapolation (vitesse nulle → identité, rotation de lacet, plafond de durée).

### Phase 3 — Marge GPU : rendu fovéal et résolution dynamique
1. **VRS NVIDIA** (`GL_NV_shading_rate_image`, disponible sur les RTX 3060 et 4080 laptop) :
   - Une image de taux par œil, centrée sur le centre optique, qui dépend du FOV asymétrique de chaque œil.
   - Trois couronnes : 1×1 au centre, 2×2 au milieu, 4×4 en périphérie.
   - Le VRS est appliqué à la passe monde uniquement ; le HUD et la mini-carte restent à pleine résolution.
   - Clé `foveation=0..3` ; désactivation automatique si l'extension est absente.
   - Les shaders PSX étant limités par les fragments, c'est le meilleur gain attendu.
2. **Masque de visibilité** (`XR_KHR_visibility_mask`) : remplir à l'avance la profondeur et le stencil sur les zones invisibles à travers les lentilles (environ 15 à 20 % des pixels). Il faut d'abord vérifier dans PsyCross (sous-module absent sous Linux) si le test de profondeur est actif en passe 1 et quels bits de stencil sont libres, car `GR_SetStencilMode` utilise le stencil pour le bit de masque PSX.
3. **Résolution dynamique** : swapchain alloué à `resolution_scale_max` (par ex. 1,3). La zone `imageRect` et le viewport s'ajustent à chaque image selon le temps GPU mesuré en phase 0 (cible : 85 % de la période, avec hystérésis). Indispensable sur portable, à cause de la limitation thermique.
- Le fovéal à suivi du regard n'est pas possible : le Quest 3 n'a pas de suivi oculaire. Les couches « quad views » (OpenXR Toolkit, Quad-Views-Foveated) ne gèrent pas OpenGL.

### Phase 4 — Conditionnelle : découplage par thread de rendu
À entreprendre uniquement si la phase 0 montre des pics de pas de jeu (chargements, débuts de mission) que la phase 1 ne suffit pas à absorber.
- Créer un fil OpenXR avec un contexte GL partagé, propriétaire de la session.
- Double-tamponner l'enregistrement et prendre un instantané de la texture VRAM, synchronisés par des fences.
- Gros chantier, risqué : on ne s'y lance qu'avec des mesures à l'appui.

## Fichiers concernés
- [src_rebuild/utils/vr_openxr.cpp](../src_rebuild/utils/vr_openxr.cpp) : mesures, boucle, profondeur, fréquence, VRS, masque, résolution dynamique.
- [patches/psycross-vr.patch](../patches/psycross-vr.patch) : régénérer après modification du sous-module (`GR_VR_SetSkipFlat`, partition, orphelinage, uniformes `VRExtrap` et VRS). Ne pas publier de gitlink.
- [src_rebuild/Game/C/vrview.c](../src_rebuild/Game/C/vrview.c) : historique de la pose caméra.
- [src_rebuild/utils/vr_math.h](../src_rebuild/utils/vr_math.h) et [tests/vr_math_tests.cpp](../tests/vr_math_tests.cpp) : extrapolation.
- `data/vr.ini`, [VR_SUPPORT.md](../VR_SUPPORT.md), [docs/AGENT_HANDOFF.md](AGENT_HANDOFF.md) : nouvelles clés et limites.

## Vérification
- Windows : `./build-wheel.ps1 -TestsOnly`, puis `./build-wheel.ps1`.
- Protocole casque, répété à chaque phase :
  - Scène fixe : Chicago, vue cockpit, 90 Hz dans VD, mêmes réglages, sur les deux portables.
  - Relever dans le journal les images manquées, les ms GPU par œil, le pas de jeu et la période. Les comparer avec l'overlay de performance de Virtual Desktop.
- Dans le compte rendu, toujours séparer les mesures (journal), l'observation et le ressenti de l'utilisateur au casque, conformément à `AGENTS.md`.
