# Support volant — branche wheel-controller-drive-feel

## Révision après les premiers essais en conduite

La base testée est conservée sur `wheel-controller` (`a423d93e`). Cette branche contient les corrections de comportement suivantes, à revalider sur le G29 :

- Burnout : la première à plein accélérateur conserve l'effet Circle jusqu'au seuil natif (`452952` unités de vitesse), puis délivre l'accélération normale. Une baisse de vitesse ne réactive pas le burnout tant que l'accélérateur n'a pas été relâché. L'embrayage garde la priorité. Le mode de lancement Driver évite que le limiteur synthétique de première interrompe cette transition.
- Frein à main : la course déclenche le verrouillage arrière Triangle à 30 %, maintenu jusqu'au retour sous 20 %. `handbrake_on` et `handbrake_off` sont configurables. La répartition d'adhérence et la coupure de propulsion suivent désormais le comportement original. Le frein de service reste analogique.
- Transmission : suppression de la seconde réduction de propulsion, qui ne laissait que 27 % de la force originale en cinquième, avant même la courbe de couple. Les rapports élevés retrouvent la propulsion Driver dans leur plage utile ; le sous-régime, le limiteur, l'embrayage et le frein moteur subsistent. C'est un compromis avec le moteur arcade, pas une nouvelle simulation mécanique complète.
- Retour de force : remplacé sur la branche `wheel-controller-ffb` par un modèle de couple d'auto-alignement (voir la section Retour de force). Le sens et l'intensité nécessitent un nouvel essai matériel.

Les nouveaux replays portent la version 2 du compagnon `.wheel`. Les compagnons expérimentaux version 1 sont refusés car la physique a changé ; les replays natifs sans compagnon restent compatibles. Les sections ci-dessous décrivent aussi la version initiale et son processus d'installation.

Cette implémentation vise Windows et le Logitech G29 avec pédalier et shifter. Le matériel, indisponible au début, a ensuite servi à identifier séparément les pédales et à réaliser les premiers essais de conduite. Les corrections de cette branche passent 75 contrôles automatisés, mais leur comportement en conduite et leur retour de force restent à revalider. La physique reste celle de Driver 2, enrichie d'une transmission simplifiée ; elle n'a pas la précision d'Assetto Corsa. Pour reprendre le développement sans l'historique du chat, lire [le document technique de reprise](docs/AGENT_HANDOFF.md).

## Démarrage

1. Compiler depuis la racine : `./build-wheel.ps1` dans PowerShell, avec Visual Studio 2022 et les outils C++ installés. Le script récupère les dépendances officielles manquantes, applique le correctif PsyCross et produit la version Windows 32 bits dans `src_rebuild/bin/Release`.
2. Brancher et alimenter le G29, le pédalier et le shifter. Régler la plage physique dans le pilote (900 degrés pour le profil fourni).
3. Ouvrir `calibrate-wheel.cmd`. Cet outil fonctionne sans les données du jeu. Vérifier que les pédales vont de zéro au repos à un enfoncées et que le volant atteint les deux butées.
4. Affecter les six positions du shifter : 1 à 5 sont les rapports avant, **la position 6 est la marche arrière**. La position de marche arrière physique supplémentaire du shifter n'est pas utilisée par ce profil.
5. Enregistrer avec **S**, fermer avec **Échap**, puis utiliser `start-wheel-game.cmd`.

Les fichiers originaux de Driver 2 doivent être installés selon les instructions du projet. Pour les essais locaux, les deux images fournies ont été extraites dans `data/install/disc1` et `data/install/disc2`, et `data/config.ini` pointe vers l'image du disque 1. Le lancement et le chargement d'une voiture en mode Take A Ride ont été vérifiés. Ces images restent locales et sont exclues de Git ; la compilation seule ne fournit pas le contenu du jeu. Le passage aux villes nécessitant le disque 2 reste à vérifier.

## Configuration et calibration

L'outil affiche les entrées en temps réel. Ses libellés sont en anglais.

| Touche | Fonction |
| --- | --- |
| Haut / Bas | Sélectionner une entrée |
| Entrée | Affecter un axe en le déplaçant, ou un rapport/bouton en l'activant |
| C | Commencer la calibration ; parcourir toute la course, centrer le volant ou relâcher la pédale, puis C pour terminer |
| I | Inverser l'axe sélectionné |
| Suppr | Retirer une affectation |
| A | Basculer entre embrayage manuel et assisté |
| - / + | Régler l'intensité du retour de force |
| [ / ] | Diminuer / augmenter la plage utilisée en jeu |
| S | Enregistrer `wheel.ini` dans le dossier de travail, avec sauvegarde `.bak` |
| Échap | Annuler l'affectation ou quitter |

Utiliser les lanceurs à la racine garantit que les réglages sont lus et enregistrés dans `data/wheel.ini`. Le profil G29 fourni est un point de départ : les pilotes peuvent présenter d'autres numéros d'axes. Les axes sont lus sur 16 bits avant normalisation et ne passent pas par les valeurs compressées de la manette PlayStation.

`hardware_rotation` doit correspondre à la plage réellement configurée dans le pilote. `rotation` détermine la plage utilisée en jeu, et `steering_lock` le braquage maximal des roues, en degrés par côté. À plage identique, la relation est linéaire entre les deux butées. Une plage de jeu plus courte ajoute une butée logicielle, dans la limite de l'intensité disponible. L'outil ne modifie pas le pilote du volant.

Le fichier permet aussi de régler la zone morte (`*_deadzone`), la courbe (`*_gamma`), les minima/centres/maxima, le sens du retour de force (`ffb_invert`), son filtrage, les rapports de boîte et le point de patinage.

Les essais séparés sur le G29 connecté avec Logitech G HUB ont identifié les axes suivants : accélérateur **1**, frein **2**, embrayage **3** (indexation à partir de zéro). Chaque pédale va de `32767` au repos à `-32768` enfoncée ; les trois axes de pédale sont donc inversés lors de la normalisation. Le profil fourni et les valeurs par défaut correspondent à ces mesures. Le volant utilise l'axe **0**. Sur un autre pilote, vérifier ces affectations par des mouvements séparés avant de les modifier.

Le volant, les pédales, le shifter et le frein à main peuvent appartenir à des périphériques SDL distincts. Les affectations de l'outil mémorisent leur GUID. Un pédalier constitue un groupe de périphérique ; les trois pédales sont attendues sur ce même périphérique. Pour plusieurs appareils identiques présentant le même GUID, la sélection du premier appareil est une limite de cette version.

## Conduite

- Le profil fourni utilise l'embrayage **manuel**. Débrayer, engager la première, accélérer et relâcher progressivement l'embrayage. Un changement de rapport sans débrayer est refusé ; le HUD affiche `CLUTCH!`.
- Le neutre n'entraîne pas les roues. Un départ mal géré peut faire caler. Pour redémarrer, débrayer complètement (ou passer au neutre) et accélérer légèrement.
- Les rapports modifient le couple aux roues et le régime. Le moteur dispose d'un ralenti, d'une courbe de couple, d'un limiteur et d'un frein moteur simplifiés. La marche arrière est refusée si la voiture avance trop vite ; l'inverse est également protégé.
- Les pédales d'accélérateur et de frein sont indépendantes et progressives. Le frein ne commande jamais la marche arrière.
- Le frein à main accepte un bouton (bouton 2 par défaut, indexation à partir de zéro) ou un axe USB qui déclenche désormais l'effet Triangle aux seuils décrits plus haut. Le freinage de service à fond peut bloquer les roues ; cette version n'ajoute pas d'ABS.
- Le burnout Driver s'active en première, à plus de 97 % d'accélérateur, à faible vitesse et lorsque l'embrayage transmet du couple. Débrayé à fond, le moteur monte seul en régime. `driver_burnout=0` désactive cette assistance arcade.
- Le HUD affiche le rapport engagé, le régime et le calage. Les quatre boutons de face, les premières commandes auxiliaires et la croix directionnelle disposent d'une correspondance de manette standard. Le clavier reste disponible pour les menus et le personnage à pied ; la conduite au volant vise le joueur 1.

## Retour de force (modèle v2, branche `wheel-controller-ffb`)

Le couple ressenti est calculé à partir des pneus avant du moteur physique de Driver 2, comme dans les simulateurs actuels, et non à partir d'un ressort de rappel synthétique.

- **Couple d'auto-alignement.** La force latérale de chaque pneu avant (linéaire jusqu'à la limite d'adhérence du jeu, puis saturée) agit à travers une chasse pneumatique qui diminue avec le glissement et une chasse mécanique qui reste. Le volant s'alourdit avec la charge latérale, culmine avant la limite (environ 65 % du glissement maximal), puis **s'allège nettement en sous-virage**. En survirage, le couple tire naturellement le volant vers le contre-braquage : aucun ressort ne s'y oppose.
- **Transfert de charge.** Le jeu donne plus d'adhérence à l'avant au freinage et moins sous accélération. La direction s'alourdit au freinage et s'allège à l'accélération, avec un lissage pour éviter les à-coups (`ffb_load`).
- **Roues bloquées / en l'air / marche arrière.** Roues avant bloquées : direction légère. Roues en l'air : aucun couple de pneu. En marche arrière, le couple, physiquement déstabilisant, est réduit à 35 %.
- **Effets passifs dans le firmware.** Amortisseur (`ffb_damping`) et frottement (`ffb_friction`) sont des effets SDL exécutés par le volant lui-même, stables quelle que soit la cadence du jeu (30 Hz). Le frottement simule le frottement des pneus à l'arrêt et s'estompe vers 30 km/h. Sans prise en charge du matériel, un amortissement logiciel prend le relais (journal : `Wheel FFB: constant force + damper + friction`).
- **Compression douce.** Au-delà de `ffb_knee`, les pics sont compressés progressivement au lieu d'être écrêtés : les détails restent perceptibles même à fort gain.
- **Zone morte du G29.** `ffb_minimum` relève les petites forces au-dessus de la zone insensible des moteurs à engrenages, avec une rampe continue sur les 3 premiers pour cent : pas de saut de force au centre, donc pas de cliquetis.
- **Route.** Compression des suspensions avant et texture des surfaces meubles (herbe, sable), proportionnelle à la vitesse et absente à l'arrêt (`ffb_road`).

### Réglages

| Clé | Défaut | Rôle | Si… |
| --- | --- | --- | --- |
| `ffb_gain` | 0.80 | Force globale (jusqu'à 1.5, compression douce au-delà de 1) | trop mou partout : augmenter ; trop dur partout : diminuer |
| `ffb_caster` | 0.25 | Poids conservé à la limite d'adhérence | l'allègement en sous-virage est trop brutal : augmenter (0.4–0.6) ; pas assez perceptible : diminuer (0.1–0.15) |
| `ffb_self_center` | 0.08 | Rappel géométrique indépendant de la vitesse | volant flottant à basse vitesse : augmenter légèrement |
| `ffb_load` | 0.50 | Sensibilité au transfert de charge | alourdissement au freinage trop/peu marqué |
| `ffb_damping` | 0.12 | Amortisseur | oscillations en ligne droite en lâchant le volant : augmenter ; direction « visqueuse » : diminuer |
| `ffb_friction` | 0.15 | Frottement à l'arrêt | manœuvres trop lourdes/légères |
| `ffb_filter` | 0.70 | Lissage du couple (1 = aucun) | grain ou à-coups : diminuer ; retard ressenti : augmenter |
| `ffb_road` | 0.25 | Bosses et surfaces | trop/peu de vibrations |
| `ffb_minimum` | 0.08 | Compensation de la zone morte | centre mort : 0.10–0.12 ; cliquetis au centre : 0.05 |
| `ffb_knee` | 0.75 | Début de la compression | `CLIP` affiché souvent : baisser `ffb_gain` d'abord |
| `ffb_hud` | 1 | Affiche `FFB <niveau> GRIP <glissement avant> CLIP` | 0 pour masquer |
| `ffb_log` | 0 | Écrit `data/ffb_telemetry.csv` (20 min max) | 1 pour une session d'analyse |

**Réglage à chaud :** `data/wheel.ini` est relu automatiquement moins d'une seconde après son enregistrement, pendant la conduite. On peut ajuster un paramètre, sauvegarder, et ressentir le changement sans relancer le jeu. L'ancienne clé `ffb_centering` est ignorée : un ressort vers le centre contredit le contre-braquage en glisse.

**Logitech G HUB :** désactiver « Ressort de centrage dans les jeux avec retour de force », Sensibilité 50, Intensité au maximum, plage 900°. Le jeu règle aussi le gain matériel à 100 % et désactive l'autocentrage.

Les forces sont arrêtées dans les menus, à la pause, à la perte de focus, en replay et à la déconnexion. La force constante expire après 120 ms sans renouvellement. Les chocs de carrosserie n'ont pas encore de signal propre. Comme toujours, le sens est à vérifier en premier : si le volant pousse vers l'extérieur du virage, arrêter et changer `ffb_invert`.

## Replays et compatibilité

Les replays enregistrés au volant utilisent le fichier du jeu **et son compagnon `.wheel`**. Il contient les commandes finales à chaque pas de simulation (braquage, propulsion, pédales, rapport, régime et burnout), sans la compression de direction originale. La lecture fonctionne sans volant branché et sans reprendre les réglages de transmission actuels.

Conserver les deux fichiers ensemble. Le replay est marqué pour refuser une lecture sans son compagnon ; la somme de contrôle l'associe au bon fichier de base. Ces replays nécessitent cette version du jeu. Les anciens replays natifs restent acceptés ; les compagnons expérimentaux version 1 doivent être utilisés avec la branche de base. Les replays sur carte mémoire, dépourvus de fichier compagnon, ne prennent pas en charge ce mode. La durée supplémentaire est limitée à deux heures.

La couche PlayStation et les contrôles habituels sont conservés quand le mode volant n'est pas actif. Un correctif de deux points dans PsyCross empêche la destruction des handles haptiques au branchement d'une manette et évite l'envoi de vibrations de gamepad au volant. Il est conservé dans `patches/psycross-wheel.patch` et appliqué par le script de compilation ; le sous-module apparaît donc modifié localement, sans changer sa référence upstream.

## Vérification

`./build-wheel.ps1 -TestsOnly` exécute les tests du modèle, du pont avec le jeu, des replays et d'un volant SDL virtuel. `src_rebuild/bin/tests/wheel_runtime_tests.exe --devices` liste les périphériques visibles sans envoyer de force. Ces contrôles ne remplacent pas un essai de conduite avec les vrais périphériques et les données du jeu.

Les essais matériels à effectuer sont : course et sens des axes, rapports et neutre, démarrage au point de patinage, freinage avant/arrière, correspondance du braquage, sens et stabilité du retour de force, burnout, pause/déconnexion et comparaison conduite/replay. Les coefficients du modèle constituent des valeurs initiales à ajuster pendant ces essais.
