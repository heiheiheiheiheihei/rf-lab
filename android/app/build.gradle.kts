import java.util.Properties

plugins {
    id("com.android.application")
}

// release 签名凭据只从仓库外读取:优先 android/keystore.properties(本地,已 gitignore),
// 其次环境变量(CI 用 GitHub Secrets 注入)。两者都没有时 assembleRelease 产出
// 未签名 APK —— 构建能过,但不可安装,发布流水线会显式检查签名。
val keystoreProps = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

fun signingValue(prop: String, env: String): String? =
    (keystoreProps.getProperty(prop) ?: System.getenv(env))?.takeIf { it.isNotBlank() }

val releaseStoreFile = signingValue("storeFile", "RFLAB_KEYSTORE_PATH")
    ?.let { rootProject.file(it) }
    ?.takeIf { it.isFile }

android {
    namespace = "dev.rflab.terminal"
    compileSdk = 35

    defaultConfig {
        applicationId = "dev.rflab.terminal"
        minSdk = 26
        targetSdk = 35
        versionCode = 3
        versionName = "0.7.1"
    }

    signingConfigs {
        if (releaseStoreFile != null) {
            create("release") {
                storeFile = releaseStoreFile
                storePassword = signingValue("storePassword", "RFLAB_KEYSTORE_PASSWORD")
                keyAlias = signingValue("keyAlias", "RFLAB_KEY_ALIAS")
                keyPassword = signingValue("keyPassword", "RFLAB_KEY_PASSWORD")
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
            if (releaseStoreFile != null) {
                signingConfig = signingConfigs.getByName("release")
            }
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
