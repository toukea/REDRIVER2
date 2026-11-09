# Reprise du travail REDRIVER2 / G29

Lire [docs/AGENT_HANDOFF.md](docs/AGENT_HANDOFF.md) avant de modifier le support volant. Ce document donne le contexte utilisateur, les décisions, les points d'intégration et les limites de validation. [WHEEL_SUPPORT.md](WHEEL_SUPPORT.md) est le guide d'utilisation et de calibration.

- La base des premiers essais est sur `wheel-controller` (`a423d93e`). Les corrections de conduite sont sur `wheel-controller-drive-feel` (code : `25483e6b`). Conserver cette séparation demandée par l'utilisateur ; ne pas fusionner dans la base sans instruction. Le modèle FFB v2 est sur `wheel-controller-ffb`, créée depuis `wheel-controller-drive-feel` ; même règle.
- Le mode VR OpenXR est sur `wheel-controller-vr` (voir [VR_SUPPORT.md](VR_SUPPORT.md) et la section VR du document de reprise) ; il dépend de `patches/psycross-vr.patch`. Le jeu reste Win32 (runtimes OpenXR 32 bits).
- Les fichiers originaux du jeu, images disque, journaux, sauvegardes locales de calibration et binaires ne sont pas des sources à publier. Les images dans `data/install/disc1/` et `disc2/` sont ignorées par Git.
- `src_rebuild/PsyCross` est un sous-module. Son état local modifié est attendu après application de `patches/psycross-wheel.patch`. Ne pas le réinitialiser ni publier un gitlink vers un commit seulement local. Utiliser `build-wheel.ps1` pour reproduire le correctif.
- Préserver le travail utilisateur hors périmètre, notamment `VR_OPENXR_ANALYSE.md` et `data/wheel.ini.bak` s'ils existent. Examiner les différences avant chaque staging ; ne pas faire de `git add .` aveugle.
- Les pédales G29/G HUB ont été identifiées séparément sur le matériel : accélérateur axe 1, frein 2, embrayage 3, volant 0. Ne pas revenir aux hypothèses initiales. Pour un autre appareil, mesurer les axes avant de modifier le profil.
- Les tests automatisés ne prouvent pas les sensations au volant. Distinguer systématiquement compilation/tests, observation à l'écran, retour utilisateur et hypothèse physique.
- Vérification Windows : `./build-wheel.ps1 -TestsOnly` ; compilation complète : `./build-wheel.ps1`. Voir le document de reprise pour les prérequis et le protocole matériel. Une modification documentaire seule ne nécessite pas de reconstruire le jeu.
