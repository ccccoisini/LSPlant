import java.security.MessageDigest
import java.time.Instant
import java.util.Properties

plugins {
    id("com.android.library") version "8.13.2" apply false
}

val moduleProperties = Properties().apply {
    file("magisk-module/module.prop").inputStream().use { load(it) }
}
val frameworkId = moduleProperties.getProperty("id")
    ?: error("magisk-module/module.prop is missing id")
val frameworkName = moduleProperties.getProperty("name")
    ?: error("magisk-module/module.prop is missing name")
val frameworkVersion = moduleProperties.getProperty("version")
    ?: error("magisk-module/module.prop is missing version")
val frameworkVersionCode = moduleProperties.getProperty("versionCode")?.toIntOrNull()
    ?: error("magisk-module/module.prop has an invalid versionCode")
val libxposedApiVersion = "102.0.0"
val compileSdkVersion = 35
val minSdkVersion = 26
val buildToolsVersionValue = "35.0.0"
val ndkVersionValue = "29.0.14206865"
val cmakeVersionValue = "3.31.0"

extra["frameworkId"] = frameworkId
extra["frameworkName"] = frameworkName
extra["frameworkVersion"] = frameworkVersion
extra["frameworkVersionCode"] = frameworkVersionCode
extra["libxposedApiVersion"] = libxposedApiVersion
extra["compileSdkVersion"] = compileSdkVersion
extra["minSdkVersion"] = minSdkVersion
extra["buildToolsVersionValue"] = buildToolsVersionValue
extra["ndkVersionValue"] = ndkVersionValue
extra["cmakeVersionValue"] = cmakeVersionValue

tasks.register("packageMagiskModule") {
    group = "build"
    description = "打包 Magisk/Zygisk 模块和所有 DEX 产物。"
    dependsOn(
        ":native-loader:externalNativeBuildRelease",
        ":framework-runtime:buildFrameworkDex",
        ":demo-hook-module:buildHookDex"
    )

    doLast {
        val distDir = layout.buildDirectory.dir("dist-work").get().asFile
        val rootDist = file("dist")
        delete(distDir)
        delete(rootDist.listFiles()?.filter { it.isFile &&
                (it.extension == "zip" || it.name in setOf(
                    "build-info.json", "framework.dex", "framework.mapping", "SHA256SUMS",
                    "device-verification-report.txt"))
        } ?: emptyList<File>())
        delete(File(rootDist, "demo-hook-module"))
        distDir.mkdirs()
        rootDist.mkdirs()

        copy {
            from("magisk-module")
            into(distDir)
        }
        copy {
            from("demo-hook-module/build/outputs/hook")
            into(File(distDir, "modules/hammer-demo"))
        }

        val soFiles = fileTree("native-loader/build").matching {
            include("**/libzygisk_framework.so")
        }.files
        val abiNames = listOf("arm64-v8a")
        abiNames.forEach { abi ->
            val source = soFiles.firstOrNull { it.invariantSeparatorsPath.contains("/$abi/") }
            if (source != null) {
                copy {
                    from(source)
                    into(File(distDir, "zygisk"))
                    rename { "$abi.so" }
                }
            }
        }

        val lsplantSha = readGitSha("third_party/LSPlant")
        val dobbySha = readGitSha("third_party/Dobby")
        val buildInfo = """
            {
              "moduleId": "$frameworkId",
              "moduleName": "$frameworkName",
              "frameworkVersion": "$frameworkVersion",
              "frameworkVersionCode": $frameworkVersionCode,
              "libxposedApi": "$libxposedApiVersion",
              "lsplantCommit": "$lsplantSha",
              "dobbyCommit": "$dobbySha",
              "zygiskApi": "4",
              "frameworkDelivery": "embedded-native-header",
              "ndkVersion": "$ndkVersionValue",
              "cmakeVersion": "$cmakeVersionValue",
              "abis": ["arm64-v8a"],
              "buildTimeUtc": "${Instant.now()}"
            }
        """.trimIndent()
        File(rootDist, "build-info.json").writeText(buildInfo)
        File(distDir, "zygisk/build-info.json").writeText(buildInfo)

        val zipFile = File(rootDist, "$frameworkId-$frameworkVersion.zip")
        delete(zipFile)
        ant.withGroovyBuilder {
            "zip"("destfile" to zipFile.absolutePath, "basedir" to distDir.absolutePath)
        }

        copy {
            from("framework-runtime/build/outputs/framework/framework.dex")
            into(rootDist)
        }
        copy {
            from("framework-runtime/build/outputs/framework/framework.mapping")
            into(rootDist)
        }
        copy {
            from("demo-hook-module/build/outputs/hook")
            into(File(rootDist, "demo-hook-module"))
        }

        val checksumFiles = fileTree(rootDist).files
            .filter { it.isFile && it.name != "SHA256SUMS" && it.name != "device-verification-report.txt" }
            .sortedBy { it.relativeTo(rootDist).invariantSeparatorsPath }
        File(rootDist, "SHA256SUMS").writeText(
            checksumFiles.joinToString(separator = System.lineSeparator()) {
                "${sha256(it)}  ${it.relativeTo(rootDist).invariantSeparatorsPath}"
            } + System.lineSeparator()
        )
    }
}

tasks.register("buildAll") {
    group = "build"
    description = "执行注释检查、单元测试、DEX 构建和 Magisk 模块打包。"
    dependsOn(
        "verifyPublicDocumentation",
        ":framework-runtime:testDebugUnitTest",
        "packageMagiskModule"
    )
}

tasks.register<Exec>("verifyPublicDocumentation") {
    group = "verification"
    description = "检查公开 Java/C++ API 是否带中文文档注释。"
    commandLine("python3", "scripts/verify_comments.py")
}

fun readGitSha(path: String): String {
    return providers.exec {
        commandLine("git", "-C", path, "rev-parse", "HEAD")
        isIgnoreExitValue = true
    }.standardOutput.asText.get().trim().ifEmpty { "unknown" }
}

fun sha256(file: File): String {
    val digest = MessageDigest.getInstance("SHA-256")
    file.inputStream().use { input ->
        val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
        while (true) {
            val read = input.read(buffer)
            if (read < 0) break
            digest.update(buffer, 0, read)
        }
    }
    return digest.digest().joinToString("") { "%02x".format(it) }
}
