# Support volant — branche wheel-controller

Cette première implémentation vise Windows et le Logitech G29 avec pédalier et shifter. La compilation et les tests automatisés sont vérifiés ; le matériel n'était pas disponible pendant le développement. Le sens du retour de force, les numéros d'axes/boutons et le comportement en conduite restent à valider sur le G29. La physique reste celle de Driver 2, enrichie d'une transmission simplifiée ; elle n'a pas la précision d'Assetto Corsa.

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
- Le frein à main accepte un bouton (bouton 2 par défaut, indexation à partir de zéro) ou un axe USB progressif. Le freinage à fond peut bloquer les roues ; cette version n'ajoute pas d'ABS.
- Le burnout Driver s'active en première, à plus de 97 % d'accélérateur, à faible vitesse et lorsque l'embrayage transmet du couple. Débrayé à fond, le moteur monte seul en régime. `driver_burnout=0` désactive cette assistance arcade.
- Le HUD affiche le rapport engagé, le régime et le calage. Les quatre boutons de face, les premières commandes auxiliaires et la croix directionnelle disposent d'une correspondance de manette standard. Le clavier reste disponible pour les menus et le personnage à pied ; la conduite au volant vise le joueur 1.

## Retour de force

Le couple est estimé à partir des forces latérales des pneus avant. Amortissement, irrégularités de suspension et surfaces complètent cet effort ; une butée logicielle est ajoutée pour les plages réduites. Ce n'est pas une simple conversion des vibrations de manette. Les chocs de carrosserie n'ont pas encore de modèle haptique distinct.

SDL doit exposer les effets de force constante pour ce périphérique. À défaut, les commandes restent utilisables sans retour de force et un message apparaît dans le journal. La compatibilité des autres volants doit être validée modèle par modèle ; des axes configurables ne garantissent pas à eux seuls le fonctionnement de leur retour de force.

Le gain initial est de 35 %. Vérifier sur le G29 que l'effort ramène le volant vers la trajectoire ; si son sens est inversé, arrêter l'essai et changer `ffb_invert`. Les forces sont arrêtées dans les menus, à la pause, à la perte de focus, en replay et à la déconnexion. Chaque effet expire après 120 ms sans renouvellement. La déconnexion en conduite libère l'accélérateur et applique le frein.

## Replays et compatibilité

Les replays enregistrés au volant utilisent le fichier du jeu **et son compagnon `.wheel`**. Il contient les commandes finales à chaque pas de simulation (braquage, propulsion, pédales, rapport, régime et burnout), sans la compression de direction originale. La lecture fonctionne sans volant branché et sans reprendre les réglages de transmission actuels.

Conserver les deux fichiers ensemble. Le replay est marqué pour refuser une lecture sans son compagnon ; la somme de contrôle l'associe au bon fichier de base. Ces replays nécessitent cette version du jeu. Les anciens replays restent acceptés. Les replays sur carte mémoire, dépourvus de fichier compagnon, ne prennent pas en charge ce mode. La durée supplémentaire est limitée à deux heures.

La couche PlayStation et les contrôles habituels sont conservés quand le mode volant n'est pas actif. Un correctif de deux points dans PsyCross empêche la destruction des handles haptiques au branchement d'une manette et évite l'envoi de vibrations de gamepad au volant. Il est conservé dans `patches/psycross-wheel.patch` et appliqué par le script de compilation ; le sous-module apparaît donc modifié localement, sans changer sa référence upstream.

## Vérification

`./build-wheel.ps1 -TestsOnly` exécute les tests du modèle, du pont avec le jeu, des replays et d'un volant SDL virtuel. `src_rebuild/bin/tests/wheel_runtime_tests.exe --devices` liste les périphériques visibles sans envoyer de force. Ces contrôles ne remplacent pas un essai de conduite avec les vrais périphériques et les données du jeu.

Les essais matériels à effectuer sont : course et sens des axes, rapports et neutre, démarrage au point de patinage, freinage avant/arrière, correspondance du braquage, sens et stabilité du retour de force, burnout, pause/déconnexion et comparaison conduite/replay. Les coefficients du modèle constituent des valeurs initiales à ajuster pendant ces essais.
