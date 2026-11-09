# Réalité virtuelle (OpenXR)

REDRIVER2 se joue au casque en conservant le moteur d'origine : la simulation reste à 30 Hz (volant, retour de force, missions et replays inchangés), et chaque image du jeu est réaffichée pour les deux yeux au rythme du casque, avec la position de tête la plus récente. Tourner la tête est donc fluide (72 à 120 Hz) ; les voitures et le décor avancent, eux, au rythme original de 30 images par seconde.

## Lancer

1. Démarrer le runtime du casque : Virtual Desktop, Meta Horizon Link ou SteamVR. Le jeu utilise le runtime OpenXR **actif** de Windows (celui choisi dans l'application du casque).
2. Lancer `start-wheel-game.cmd` : une fenêtre demande **Écran plat** ou **Réalité virtuelle (OpenXR)**.
   - `REDRIVER2.exe -vr` ou `-flat` saute la question ; `launcher=0` dans `data/vr.ini` la supprime.
3. Si le casque n'est pas prêt, un message l'indique et le jeu continue en écran plat ; **Ctrl+Maj+V** réessaie.

Le jeu est en 32 bits : il faut un runtime OpenXR 32 bits, ce que fournissent Virtual Desktop et Meta (fichiers `*-32.json`). Aucun SDK n'est à installer : le runtime est trouvé et chargé directement, comme le fait le chargeur Khronos. Un `openxr_loader.dll` 32 bits placé à côté de l'exécutable est utilisé à la place s'il existe (utile pour les couches comme OpenXR Toolkit).

## En jeu

| Touche | Action |
| --- | --- |
| Ctrl+Maj+V | Quitter la VR vers l'écran plat (et y revenir) |
| F9 | Recentrer la vue : regarder droit devant, assis, puis appuyer (ou appui long sur le bouton du casque) |
| Pavé num. 8 / 2 | Siège en avant / en arrière (1 cm) |
| Pavé num. 4 / 6 | Siège à gauche / à droite |
| Pavé num. 9 / 3 | Siège plus haut / plus bas |
| Pavé num. 5 | Remettre le siège à zéro |

Le réglage du siège est enregistré dans `data/vr.ini`. Un bouton du volant peut recentrer la vue (`recenter_button`, numéro de bouton vu dans WheelConfig).

- **Vue cockpit** : la tête est placée à l'œil du conducteur, à l'échelle réelle. Par défaut l'horizon reste de niveau (`horizon_lock=1`) : la caisse roule et tangue autour de toi comme dans Assetto Corsa avec « Lock onboard camera to horizon », ce qui fatigue beaucoup moins. `horizon_lock=0` fait suivre le roulis à la vue.
- **Mini-carte** : affichée sur un petit écran posé sur le tableau de bord (`map_on_dashboard`). Position réglable dans `cockpit.ini` (`map_screen_x/y/z`, `map_screen_size`, par voiture ou dans `[cockpit]`).
- **Barres de dégâts / recherche** : sur un panneau à quelques mètres devant la voiture (`hud_distance`, `hud_width`, `hud_height`).
- **Volant virtuel** : `hide_wheel=1` masque le volant et les bras si tu préfères ne voir que tes vraies mains sur le G29.
- **Vue poursuite** : la caméra du jeu sert d'ancrage, la tête regarde librement autour.
- **Menus, cinématiques, replays réalisés** : affichés sur un grand écran virtuel (`cinema_distance`, `cinema_width`).

La fenêtre du PC montre l'œil gauche (recadré) pendant la conduite et le jeu normal dans les menus.

## Réglages `data/vr.ini`

| Clé | Défaut | Rôle |
| --- | --- | --- |
| `resolution_scale` | 1.0 | Multiplie la résolution recommandée par le runtime |
| `units_per_metre` | 135 | Échelle du monde (unités du jeu par mètre). 0 = échelle de l'habitacle importé de chaque voiture |
| `world_scale` | 1.0 | > 1 : le monde paraît plus grand |
| `horizon_lock` | 1.0 | 1 = horizon fixe, 0 = la vue roule avec la voiture |
| `scr_z` | 90 | Distance de projection du jeu pendant le suivi de tête : plus petite = champ dessiné plus large |
| `near_plane` | 0.03 | Plan proche en mètres |

## Fonctionnement

- **PsyCross** (`patches/psycross-vr.patch`) enregistre la liste de triangles de chaque image avec leurs coordonnées PGXP (position 3D dans l'espace caméra, avant la perspective). Le shader a deux passes VR : les triangles 3D sont reprojetés pour un œil (pose OpenXR et champ de vision asymétrique), les éléments 2D du HUD sont posés sur un plan. Les sprites 2D du décor (fumée, lumières, débris) reçoivent la profondeur de leur point d'ancrage GTE pour être vus au bon endroit par chaque œil.
- **`src_rebuild/utils/vr_openxr.cpp`** gère le runtime, la session, les swapchains sRGB, la boucle d'images, le miroir, les touches et `vr.ini`.
- **`src_rebuild/Game/C/vrview.c`** applique la pose de tête à la caméra du jeu (cockpit, pare-chocs et poursuite), élargit le champ dessiné et calcule le rectangle de la mini-carte.
- Entre deux pas de simulation, la différence entre la pose de tête qui a servi à construire l'image et la pose actuelle des yeux est appliquée dans le shader : la tête reste réactive sans faire tourner le jeu plus vite.
- `tests/vr_math_tests.cpp` vérifie les conventions d'axes (tête à gauche = regard à gauche, haut = haut, parallaxe, recentrage, projections).

## Limites connues

- Les objets en mouvement (voitures, décor qui défile) sont mis à jour à 30 Hz : à haute vitesse, le défilement est moins fluide que la tête.
- Les rétroviseurs ne reflètent rien ; les habitacles importés n'ont pas tous une banquette arrière complète.
- Un fondu au noir plein écran du jeu s'affiche sur le panneau du HUD, pas sur toute la vue.
- Écran partagé à deux joueurs : non pris en charge en VR (écran virtuel).
