// The APK's packaging (tools/ob/android.sh package): Gradle builds the Java side (java/, OutbreakActivity) and packs
// and signs the APK around the native libraries CMake built, which android.sh stages under C:\ob\android-apk\stage.
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "outbreak-modern"
