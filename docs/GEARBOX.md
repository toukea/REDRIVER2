# Boîte de vitesses v2 (branche `wheel-controller-gearbox`)

Branche créée le 21 novembre 2026 depuis `wheel-controller-vr-perf` (`8a75ec20`). Même règle que les autres branches : ne pas fusionner sans instruction de l'utilisateur.

## Problème signalé

En conduite au G29 : la cinquième peine à atteindre la vitesse maximale, l'accélération reste molle même avec des passages rapides, et les missions de poursuite échouent.

## Causes trouvées dans le code

1. **Rubber banding de poursuite perdu.** Dans `handling.c` (`CAR_PAD_ACCEL`), un joueur à la manette reçoit en poursuite la puissance de la cible si la sienne dépasse 3050, puis ×8000/7000 (+14 %) loin derrière, ×7400/7000 à mi-distance et ×6700/7000 collé à la cible. `Wheel_GameApply` est appelé après ce code et réécrivait `cp->thrust` à partir de la seule puissance de la voiture. Au volant, on perdait donc jusqu'à 14 % de poussée, plus l'écart de puissance avec la cible, dans chaque poursuite.
2. **Jamais plus que la poussée d'origine.** Le multiplicateur de rapport était borné à `[1 ; 1,3]` mais valait 1 dans tous les rapports avant. La première et la deuxième perdaient en plus 20 à 45 % avec la courbe de couple, les autres rapports subissaient le sous-régime. Changer de rapport n'apportait rien.
3. **Limiteur en cinquième.** L'étagement était fixe (`final_drive=3.7`). Le jeu a une traînée linéaire : la vitesse de pointe vaut `2 × 4096 × poussée / (4 × 12)` unités, soit environ `0,015 × powerRatio` m/s avec notre conversion. Au-delà de `powerRatio ≈ 4600`, la cinquième atteignait 6500 tr/min avant la vitesse de pointe, et la coupure franche (`revLimit = 0`) faisait plafonner la voiture.
4. **Passages rapides refusés.** Le rapport n'était accepté que si l'embrayage dépassait le point de patinage *à l'instant même* où le levier entrait dans la grille. Sur un passage rapide, la pédale remonte déjà : le passage était refusé et on restait au point mort (`CLUTCH!`).
5. **Embrayage linéaire.** La transmission baissait linéairement sur toute la course `0 → bite`. Un pied posé sur la pédale (10 à 20 %) coûtait 15 à 30 % de poussée.

## Nouveau modèle (`utils/wheel_model.h`)

- **Étagement automatique par voiture** (`auto_gearing=1`). Le pont calcule la vitesse de pointe native de la voiture (`nativeTopSpeed`) ; la cinquième y tourne à `1/1,2` du régime maxi (5417 tr/min pour 6500). Les autres rapports gardent l'échelonnement `ratio1..ratio5`. `final_drive` et `tyre_radius` ne servent plus qu'avec `auto_gearing=0`.
- **Multiplication de couple** (`gear_torque=0.5`). Force aux roues relative au X natif : `(ratio_i / ratio_5)^gear_torque × couple(régime) / couple(charge cinquième)`. Elle vaut exactement 1 en cinquième à la vitesse de pointe native, qui est donc conservée. Avec les rapports par défaut : environ ×1,9 en première, ×1,5 en deuxième, ×1,3 en troisième, ×1,1 en quatrième. Plafond ×2,5. `gear_torque=0` redonne la poussée constante de Driver.
- **Courbe moteur** `1 − 0,5 × ((u − 0,6)/0,6)²` (u = régime/maxi), pic à 60 %, 78 % au maxi : la puissance monte presque jusqu'à la zone rouge, donc on passe le rapport au limiteur.
- **Limiteur progressif** : la poussée s'efface entre maxi − 150 et maxi + 150 tr/min au lieu d'être coupée.
- **Embrayage** : transmission totale sous `clutch_bite − clutch_window` (0,35 par défaut), fondu en smoothstep jusqu'à nulle à `clutch_bite`.
- **Fenêtre de passage** (`shift_window=0.35` s) : un rapport est accepté si l'embrayage a dépassé le point de patinage dans les 0,35 s précédentes.
- **Indicateur `SHIFT`** dans le HUD à 92 % du régime maxi, ou plus tôt si le rapport supérieur tire déjà plus fort (bit 64 des frames replay).
- Burnout inchangé. Une fois terminé, la première à fond donne `max(rapport normal, X natif)` : accélérer à 100 % ne donne jamais moins qu'à 96 %.
- Marche arrière et frein moteur : poussée propre de la voiture, sans banding.

## Pont jeu (`Game/C/wheel.c`)

`nativeThrust(cp, distanceBanding)` reproduit le calcul `CAR_PAD_ACCEL` de `handling.c`, banding de poursuite compris (gardé si `targetCarId` vaut l'id du joueur). La propulsion avant vaut `nativeThrust(cp, true) × drive`. L'étagement utilise la version sans banding de distance, qui varie à chaque frame et ferait sauter le régime.

## Vérifications

- Compilés avec g++ 11 sous Linux : `wheel_model_tests` 58 contrôles, `wheel_game_tests` 42 contrôles (dont trois sur le banding de poursuite). `wheel.cpp` compile (contrôle syntaxique).
- **Non fait :** compilation MSVC complète, essai au volant. La formule de vitesse de pointe vient de la lecture de `AddWheelForces` (traction sur les deux roues arrière, traînée `vel × 12` sur quatre contacts). Elle n'a pas été mesurée en jeu : si la cinquième bute encore sur le limiteur, la conversion de vitesse `× 0,3 / 4096` ou la traînée réelle diffèrent.

## Réglages pour l'essai

| Clé | Défaut | Effet |
|---|---|---|
| `auto_gearing` | 1 | Étagement calé sur la vitesse de pointe de chaque voiture |
| `gear_torque` | 0.5 | 0 = poussée constante Driver ; 0,7 = reprises plus franches |
| `clutch_window` | 0.30 | Largeur de la zone de patinage sous `clutch_bite` |
| `shift_window` | 0.35 | Tolérance (s) entre le débrayage et l'entrée du levier |

Protocole proposé : même voiture, Chicago, *Take A Ride* puis une poursuite. Noter les passages refusés (`CLUTCH!`), le rapport où apparaît `SHIFT`, et si la cinquième atteint un plafond sans `SHIFT` ni chute de régime.
