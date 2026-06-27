# SensorV26 Companion (Android)

App Android (Kotlin + Jetpack Compose) qui dialogue avec la carte nRF54L15
(firmware `source/example_apps/rs485_bridge`).

## Fonctions

1. **NFC** — lit l'adresse du nœud (tag NDEF Text) et écrit les credentials de
   commissioning (`net=AABBCC;ch=7;addr=...`). Après écriture, la carte
   redémarre pour rejoindre le réseau.
2. **Scan BLE** — détecte/filtre les beacons de la carte (UUID `0x1234`,
   manufacturer `0xFFFF`/ver `0x01`) et décode `node / vbat / température /
   charge / valid`.
3. **Émission BLE** — émet un beacon au même format pour que la carte le lise.

Le protocole exact est centralisé dans
[`Protocol.kt`](app/src/main/java/com/sensorv26/companion/Protocol.kt) et doit
rester synchronisé avec `app.c` côté firmware.

## Build

Ouvrir le dossier `android/` dans **Android Studio** (Giraffe+), il génère le
Gradle wrapper et synchronise. En ligne de commande :

```bash
cd android
gradle wrapper          # une seule fois, génère ./gradlew
./gradlew assembleDebug
./gradlew installDebug  # avec un appareil branché
```

## Prérequis appareil

- **NFC** activé pour le commissioning.
- **Bluetooth** activé. L'émission de beacon nécessite un téléphone supportant
  le rôle *BLE peripheral/advertiser* (la plupart des appareils récents).
- Permissions runtime demandées au lancement : `BLUETOOTH_SCAN`,
  `BLUETOOTH_ADVERTISE` (Android 12+) ou `ACCESS_FINE_LOCATION` (≤ Android 11).

## Notes

- `minSdk = 26`, `targetSdk = 35`.
- Le scan utilise un filtre OS sur la manufacturer data `0xFFFF` quand
  « Filtrer la carte » est activé, sinon il liste tous les appareils.
