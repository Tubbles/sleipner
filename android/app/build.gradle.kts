plugins {
    id("com.android.application")
}

fun gitVersionCode(): Int = try {
    val process = ProcessBuilder("git", "rev-list", "--count", "HEAD")
        .directory(rootProject.projectDir)
        .start()
    process.inputStream.bufferedReader().readText().trim().toIntOrNull() ?: 1
} catch (_: Exception) { 1 }

fun gitDescribe(): String = try {
    val process = ProcessBuilder("git", "describe", "--tags", "--always", "--dirty")
        .directory(rootProject.projectDir)
        .start()
    process.inputStream.bufferedReader().readText().trim().ifEmpty { "unknown" }
} catch (_: Exception) { "unknown" }

android {
    namespace = "com.sleipner"
    compileSdk = 35
    ndkVersion = "28.0.13004108"

    defaultConfig {
        applicationId = "com.sleipner"
        minSdk = 27
        targetSdk = 35
        versionCode = gitVersionCode()
        versionName = gitDescribe()

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH",
                    "-Draylib_DIR=${rootProject.projectDir.parentFile}/build/android/arm64-v8a/build/Release/generators",
                    "-DSLEIPNER_GIT_VERSION=${gitDescribe()}"
                )
            }
        }
    }

    signingConfigs {
        create("release") {
            storeFile = file("${rootProject.projectDir}/keystore.jks")
            storePassword = "sleipner"
            keyAlias = "sleipner"
            keyPassword = "sleipner"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
