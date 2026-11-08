# Vue cockpit — rendu et poste conducteur

Travail du 8 novembre 2026 sur `wheel-controller-cockpit`, créée depuis `wheel-controller-ffb`. Cette branche conserve le support G29/FFB existant ; elle ne fusionne pas les branches d'essais précédentes. OpenXR n'est pas implémenté ici.

## Utilisation

Compiler avec `./build-wheel.ps1`, puis utiliser `start-wheel-game.cmd`. En conduite, la commande de changement de caméra (touche **C** dans `data/config.ini`) parcourt poursuite → première personne dégagée → cockpit → caméra historique fixe/suivi → poursuite. Les commandes de regard existantes restent disponibles : A/D sur le profil clavier pour regarder latéralement. Le cockpit n'est proposé qu'en véhicule. En quittant la voiture, la vue piéton reste celle du jeu ; la préférence voiture est reprise à la prochaine entrée.

La vue utilise une géométrie 3D originale et un atlas de textures pixel de 256×256, générés par le code. Le style reste celui d'un habitacle ancien à faible nombre de polygones : tableau de bord rembourré, casquette d'instruments, aiguilles vitesse/régime, radio analogique, aérateurs, garniture bois sur les voitures, vinyle sur les camions, sièges en tissu, panneaux de portes et poignées. Les bras sont des volumes effilés, avec paumes, pouces et doigts simplifiés. Le volant est plus fin et plus éloigné pour dégager la route. La lumière ambiante varie avec le jour/la nuit ; les faces des cadrans restent éclairées. Les deux petits instruments et la radio sont décoratifs ; les aiguilles principales utilisent les données de conduite, avec une conversion de vitesse approximative héritée du pont G29. Aucun modèle ou animation d'Assetto Corsa ou de Sketchfab n'a été copié.

Le fichier fourni active `start_in_cockpit=1` : une nouvelle conduite démarre directement dans cette vue. Mettre 0 pour commencer en poursuite. Le cycle avec C reste disponible.

## Les trois familles

- `family=0` : deux places, deux sièges avant, pas de banquette arrière ni de montant intermédiaire.
- `family=1` : quatre places, deux sièges avant, banquette et montants intermédiaires.
- `family=2` : cabine camion, conducteur avancé, volant plus incliné, deux sièges et cloison arrière. Les bus reçoivent provisoirement cette cabine ; ils n'ont pas de compartiment passagers.

Les dimensions de collision servent au classement initial et au gabarit de repli. Le classement utilise hauteur >145 ou longueur >580 avec hauteur >120 pour les gros véhicules ; sinon longueur <260 ou hauteur <78 donne deux places, et le reste quatre places. Ce classement est **une heuristique**, pas un inventaire fiable du nombre de sièges. Une limousine basse reste une voiture. Les exceptions connues Chicago 11 (camion) et Vegas 12 (pickup deux places) sont configurées explicitement.

Avec `fit_model=1`, la position est affinée à partir des sommets du modèle extérieur intact chargé par le jeu. Les points latéraux en partie haute identifient les bords du toit ; les petits éléments centraux plus hauts, comme les gyrophares, sont exclus. La géométrie supérieure définit la hauteur et la largeur du toit et son extrémité avant, puis la position des yeux sous celui-ci. Pour les camions, la recherche se limite à l'avant du véhicule pour éviter d'asseoir le conducteur au milieu d'une benne ou d'une remorque. Un modèle absent ou insuffisant conserve le repli. C'est une estimation géométrique, pas une reconnaissance sémantique parfaite du pare-brise : les carrosseries spéciales et les bus restent à ajuster au cas par cas. Les dégâts n'altèrent pas le siège, car le modèle non déformé est utilisé.

`data/cockpit.ini` est chargé au démarrage. Les sections utilisent le nom de ville du moteur et le numéro original du modèle, par exemple `[CHICAGO.11]`, et non son emplacement temporaire en mémoire. Le modèle interchangeable 13 est résolu comme dans le chargement des cosmetics. Une exception camion explicite est fournie pour Chicago 11, identifié comme tel par le sélecteur du jeu. Les autres associations doivent être vérifiées visuellement et corrigées au besoin.

| Clé | Fonction |
| --- | --- |
| `[cockpit] enabled=1` | Inclure la nouvelle vue dans le cycle ; 0 conserve les vues historiques |
| `[cockpit] driver=1` | Afficher le corps et les mains ; 0 les masque |
| `[cockpit] pitch=3` | Inclinaison initiale du regard, en degrés vers le bas |
| `[cockpit] start_in_cockpit=1` | Commencer une nouvelle conduite dans le cockpit |
| `fit_model=1` | Utiliser la silhouette du modèle chargé ; réglage global ou par modèle |
| `family` | Famille d'habitacle, 0/1/2 |
| `half_width`, `half_height`, `half_length` | Gabarit de départ avant calcul du profil |
| `eye_x`, `eye_y`, `eye_z` | Position des yeux |
| `wheel_x`, `wheel_y`, `wheel_z` | Pivot du volant |
| `wheel_radius`, `wheel_tilt` | Rayon et inclinaison du volant |

Les distances sont en unités de modèle Driver, **pas en mètres calibrés**. Axes cockpit : X à droite, Y vers le haut, Z vers l'avant, origine au centre de géométrie utilisé par `DrawCarObject`. Les valeurs sont bornées pour éviter une caméra hors de la cabine. Les clés `eye_*` et `wheel_*` remplacent les valeurs calculées ; pour utiliser les clés `half_*` sans ajustement par le modèle, mettre `fit_model=0` dans la section du véhicule. Redémarrer le programme après modification du fichier. Les fenêtres sont des ouvertures sans polygone : le monde est réellement visible à travers. Les vitres noires et la carrosserie extérieure du véhicule observé restent masquées depuis l'intérieur. Aucun verre réfléchissant ou salissant n'est simulé. Le rétroviseur est une petite coque décorative avec une surface colorée ; il n'affiche pas la scène arrière.

## Volant et conducteur

L'angle visuel est calculé depuis la commande finale normalisée, multipliée par la demi-plage de jeu `rotation`, avec les mêmes limites 180–2520° que le traitement d'entrée. À 900°, +50 % donne +225° ; les roues avant gardent leur propre braquage `steering_lock`. Il n'y a pas de lecture supplémentaire du périphérique pendant le rendu, ni de modification de la physique/FFB par le cockpit.

Cet angle est enregistré au dixième de degré dans les deux octets précédemment réservés des frames `.wheel` v2, signalé par le bit 32. La taille reste 28 octets. Les anciens compagnons v2, sans ce bit, restent lisibles et utilisent un rapport visuel approximatif à partir du braquage. Les nouveaux conservent l'angle visuel même sans volant branché ou avec un réglage de plage différent en replay. Les anciens lecteurs v2 ignorent ce champ ; leurs fichiers caméra ne connaissent cependant pas la nouvelle vue 3. Les replays caméra utilisant le cockpit doivent donc être lus avec cette branche.

Les mains ont des cibles sur la jante inclinée ; une cinématique inverse à deux segments place les coudes à partir des épaules. Corps, siège et pivot sont dans le repère du véhicule. Le conducteur n'a ni tête ni cou visible. Pour ce premier jet, la prise accompagne la rotation jusqu'à ±100°, puis reste glissante sur la jante afin d'éviter les torsions impossibles. **Le passage des mains l'une au-dessus de l'autre sur plusieurs tours n'est pas encore animé.** Il n'y a pas de suivi des mains réelles, d'animation des doigts, du changement de rapport ou des pédales.

## Inspiration Assetto Corsa 1

Les principes retenus sont la séparation entre pose conducteur, pivot du volant, angle de direction et animation des bras, ainsi que le masquage de la tête en vue interne. REDRIVER2 ne charge pas les formats KN5/KNH/KSANIM.

- Le [code source de BakeryOptiX, par les auteurs de Custom Shaders Patch](https://github.com/ac-custom-shaders-patch/acc-bakeryoptix/blob/master/bakeryoptix/main.cpp) traite séparément le cockpit/volant haute résolution, la hiérarchie `driver_base_pos.knh` et l'animation `steer.ksanim`, appliquée à sa pose centrale. Cette séparation inspire l'organisation du prototype ; aucune implémentation n'en est reprise.
- Le [SDK Lua de Custom Shaders Patch](https://github.com/ac-custom-shaders-patch/acc-lua-sdk/blob/main/common/ac_game.lua) distingue également les données de caméra conducteur et de direction.
- La [republication communautaire du pipeline SDK Kunos AC1](https://docs.assetto.cn/en/pipeline/04-functional-mesh-elements) et son [chapitre animations](https://docs.assetto.cn/en/pipeline/07-animations) ont servi à orienter la recherche. Ce miroir n'est pas le site officiel Kunos ; le rig et le PDF SDK originaux ne sont pas installés ou importés dans ce dépôt. Le lien historique du forum Kunos redirigeait vers l'accueil lors de cette session et n'a pas permis de lire le tutoriel original.
- La [Porsche proposée par l'utilisateur, de n.brizitskaya](https://sketchfab.com/3d-models/porsche-911-with-interior-877b1bc1739f4a2bb65d62fd7ffd9f75) a été identifiée dans les résultats de recherche ; son visualiseur répondait 403 lors de l'inspection. Aucun téléchargement de son modèle n'a été effectué. La [référence de Porsche 911 de 1964 publiée par W modeling studio](https://www.renderhub.com/w-modeling-studio/porsche-911-1964-3d-model) a pu être examinée visuellement : instruments ronds, bandeau de bois et volant mince ont orienté la composition. Ces références ne sont pas des assets du jeu modifié.

## Intégration et vérification

`utils/cockpit_model.h` contient profils, ajustement à la silhouette, géométrie, pose et découpe au plan proche avec interpolation des UV. `utils/cockpit_atlas.h` génère les matériaux originaux et le dessin pixel des instruments. `Game/C/cockpit.c` charge la configuration et soumet les triangles GTE/PGXP. Une précision de 1/8 d'unité réduit les tremblements des surfaces proches. La texture PC est créée avec l'API PsyCross existante, sans réserver un emplacement dans la VRAM PlayStation. Son activation est groupée par profondeur et désactivée avant les primitives du monde et du HUD. La capacité du tampon de primitives est vérifiée. Les tests sont inclus dans `build-wheel.ps1`.

Le nouvel identifiant caméra est 3, compatible avec le champ caméra sur trois bits existant. Les points d'intégration caméra, masquage du véhicule, lumières, son et certaines opérations du directeur de replay reconnaissent les deux vues internes. Le directeur historique ne propose pas encore un nouveau bouton cockpit dans son interface d'édition.

Validation automatisée : compilation Windows Win32 du jeu et tests modèle volant, pont/replays, SDL virtuel et cockpit. Le test SDL utilise les événements de fond et le GUID du périphérique virtuel pour ne pas sélectionner un G29 physique arrivé tardivement pendant l'énumération Windows. Cela ne change pas la politique de focus du jeu.

Validation automatisée : **510 contrôles passent** (46 modèle volant, 394 géométrie/pose/clipping/atlas cockpit, 39 pont jeu/replays, 31 SDL). Le jeu complet compile en Win32, en Release et Release_dev ; `git diff --check` passe. Des avertissements upstream subsistent.

Validation visuelle : la version texturée a été observée sur Chicago 1 (berline), Chicago 8 (cabine camion) et Vegas 12 (pickup deux places). Les diagnostics confirment l'ajustement par le modèle extérieur : toits/yeux respectivement 77/49, 169/133 et 86/58 unités. Les textures de la route, des autres véhicules et du HUD restent visuellement cohérentes après le passage cockpit. Les cadrans, la radio, le volant et les bras sont visibles ; le camion offre un point de vue nettement plus haut. La correction des couleurs de peau/garnitures et la réduction du rétroviseur ont été observées sur le pickup. Chicago 1 a aussi été observée de nuit : les garnitures sont assombries et les instruments restent lisibles. La rotation des branches du volant et le déplacement des mains ont été observés en conduite. Cela ne valide pas tous les modèles, la correspondance physique exacte des rotations ou les replays complets en conduite.

Avant de considérer ce prototype prêt à l'usage : vérifier le cycle de caméras, le poste conducteur et les cadrans sur les trois familles, les regards latéraux/arrière, les fortes rotations, l'entrée/sortie de voiture, pause/reprise, jour/nuit, replay et clavier/manette. Vérifier aussi occultations et plans proches avec PGXP/Z-buffer activés et désactivés. Les vitres extérieures, le capot visible, les rétroviseurs fonctionnels, les dégâts intérieurs, les véhicules miniatures et OpenXR restent à traiter. L'échelle VR et la pose de tête ne sont pas calibrées.
