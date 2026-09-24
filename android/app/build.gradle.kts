plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.sensorv26.companion"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.sensorv26.companion"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.09.00")
    implementation(composeBom)
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.5")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.5")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.5")
    implementation("androidx.activity:activity-compose:1.9.2")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.compose.material3:material3")
    debugImplementation("androidx.compose.ui:ui-tooling")

    // AES-CMAC / AES-CBC for the SE050 SCP03 (GlobalPlatform) handshake -
    // minSdk 26 predates the guaranteed availability of
    // Mac.getInstance("AESCMAC") (API 28+), and CMAC is exactly the kind
    // of primitive not worth hand-rolling. See nfc/se050/Scp03Crypto.kt.
    implementation("org.bouncycastle:bcprov-jdk18on:1.78.1")

    // QR scan for a manually-supplied, per-device SCP03 key (rotated
    // devices only - see nfc/se050/Se050QrKey.kt). Self-contained scanning
    // Activity (camera preview + ZXing decode + permission prompt), no
    // Google Play Services dependency - consistent with the rest of this
    // app.
    implementation("com.journeyapps:zxing-android-embedded:4.3.0")
}
