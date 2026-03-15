plugins {
    id("com.android.application")
}

android {
    namespace = "com.sleipner"
    compileSdk = 35
    ndkVersion = "28.0.13004108"

    defaultConfig {
        applicationId = "com.sleipner"
        minSdk = 27
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH",
                    "-Draylib_DIR=${rootProject.projectDir.parentFile}/build/android/arm64-v8a/build/Release/generators"
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
