// One application module: the manifest, the Kotlin side (kotlin/: the Compose launcher and in-game menu, the game
// activity), and the staged native libraries and assets. android.sh passes -PobStage=<stage dir> (lib/<abi>/*.so,
// assets/), -PobBuildDir=<build dir, outside the repo> and -PobKeystore=<debug keystore>; nothing is built inside
// the repo. Kotlin, Compose BOM and library versions follow PlayFieldPortal (gradle/libs.versions.toml there), so they
// come from the same local Gradle cache.
plugins {
    id("com.android.application") version "9.4.0"
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.10"
}

val obStage = providers.gradleProperty("obStage").get()
val obKeystore = providers.gradleProperty("obKeystore").get()
layout.buildDirectory.set(file(providers.gradleProperty("obBuildDir").get()))

android {
    namespace = "com.outbreakmodern.app"
    compileSdk = 37

    defaultConfig {
        applicationId = "com.outbreakmodern.app"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "0.1-a1"
        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }

    signingConfigs {
        getByName("debug") {
            storeFile = file(obKeystore)
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    sourceSets {
        getByName("main") {
            manifest.srcFile("AndroidManifest.xml")
            kotlin.directories += "kotlin"
            jniLibs.directories += "$obStage/lib"
            assets.directories += "$obStage/assets"
            res.directories += "res" // launcher icons (tools/ob/make_icons.py)
        }
    }

    packaging {
        jniLibs {
            // Libraries extracted at install (extractNativeLibs): libadrenotools loads its hooks from
            // nativeLibraryDir, and a custom GPU driver needs a real directory (docs/ops/android.md "GPU driver").
            useLegacyPackaging = true
            // Stripped by android.sh (llvm-strip --strip-unneeded) already.
            keepDebugSymbols += "**/*.so"
        }
    }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2026.08.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.core:core-ktx:1.19.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.11.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.11.0")
}
