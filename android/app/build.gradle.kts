import org.gradle.process.ExecOperations
import javax.inject.Inject

plugins {
    id("com.android.application")
}

val ndkDir = android.ndkDirectory.absolutePath

fun abiToConanArch(abi: String): String = when (abi) {
    "arm64-v8a" -> "armv8"
    "armeabi-v7a" -> "armv7"
    "x86_64" -> "x86_64"
    "x86" -> "x86"
    else -> error("Unsupported ABI: $abi")
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
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

abstract class ConanInstallTask @Inject constructor(
    private val execOps: ExecOperations
) : DefaultTask() {

    @get:Input
    abstract val projectRoot: org.gradle.api.provider.Property<String>

    @get:Input
    abstract val ndkPath: org.gradle.api.provider.Property<String>

    @get:OutputDirectory
    abstract val conanDir: org.gradle.api.file.DirectoryProperty

    @TaskAction
    fun run() {
        for (abi in listOf("arm64-v8a")) {
            val arch = abiToConanArch(abi)
            val outDir = File(conanDir.get().asFile, abi)
            outDir.mkdirs()

            execOps.exec {
                workingDir = File(projectRoot.get())
                commandLine(
                    "conan", "install", ".",
                    "--output-folder=${outDir.absolutePath}",
                    "--build=missing",
                    "-s", "os=Android",
                    "-s", "os.api_level=27",
                    "-s", "arch=$arch",
                    "-s", "compiler=clang",
                    "-s", "compiler.version=18",
                    "-s", "compiler.libcxx=c++_static",
                    "-s", "compiler.cppstd=17",
                    "-s", "build_type=Release",
                    "-c", "tools.android:ndk_path=${ndkPath.get()}"
                )
            }
        }
    }
}

tasks.register<ConanInstallTask>("conanInstall") {
    projectRoot.set(rootProject.projectDir.parentFile.absolutePath)
    ndkPath.set(ndkDir)
    conanDir.set(layout.buildDirectory.dir("conan"))
}

tasks.matching { it.name.startsWith("configureCMake") || it.name.startsWith("buildCMake") }.configureEach {
    dependsOn("conanInstall")
}
