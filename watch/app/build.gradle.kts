import java.util.Properties

plugins {
    id("com.android.application")
}

// Watch has its own release key, independent from the phone APK:
//   ~/.local/share/rf-lab/watch/rf-lab-watch-release.jks
// Credentials live in watch/keystore.properties (gitignored) or the
// RFLAB_WATCH_* environment variables (CI). Missing both = unsigned release.
val watchKeyProps = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

fun watchSigningValue(prop: String, env: String): String? =
    (watchKeyProps.getProperty(prop) ?: System.getenv(env))?.takeIf { it.isNotBlank() }

val watchStoreFile = watchSigningValue("storeFile", "RFLAB_WATCH_KEYSTORE_PATH")
    ?.let { rootProject.file(it) }
    ?.takeIf { it.isFile }

android {
    namespace = "dev.rflab.watch"
    compileSdk = 35

    defaultConfig {
        applicationId = "dev.rflab.watch"
        // ColorOS Watch 2.0 is Android 8.1 (API 27); 26 keeps compatibility with
        // older Wear OS devices while staying above the BLE permission churn.
        minSdk = 26
        // targetSdk stays at 28: the watch ROM is API 27, and we do not want
        // Android 12+ nearby-device/foreground-service behavior on a phone build.
        targetSdk = 28
        versionCode = 3
        versionName = "0.1.2"
    }

    signingConfigs {
        if (watchStoreFile != null) {
            create("release") {
                storeFile = watchStoreFile
                storePassword = watchSigningValue("storePassword", "RFLAB_WATCH_KEYSTORE_PASSWORD")
                keyAlias = watchSigningValue("keyAlias", "RFLAB_WATCH_KEY_ALIAS")
                keyPassword = watchSigningValue("keyPassword", "RFLAB_WATCH_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            if (watchStoreFile != null) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }

    lint {
        // Deliberate: ColorOS Watch 2.0 is API 27. Raising targetSdk would
        // change Bluetooth/location permission behavior on the watch for no
        // benefit (this APK is not distributed through Google Play).
        disable += "ExpiredTargetSdkVersion"
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
}
