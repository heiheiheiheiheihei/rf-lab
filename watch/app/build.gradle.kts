plugins {
    id("com.android.application")
}

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
        versionCode = 1
        versionName = "0.1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
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
