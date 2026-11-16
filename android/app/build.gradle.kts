plugins {
    id("com.android.application")
}

android {
    namespace = "dev.rflab.terminal"
    compileSdk = 35

    defaultConfig {
        applicationId = "dev.rflab.terminal"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.6.0"
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

    // 同一份 Web UI:由 tools/build_web.py 生成到 assets/index.html。
    androidResources {
        noCompress += "html"
    }
}

dependencies {
    implementation("androidx.webkit:webkit:1.12.1")
}
