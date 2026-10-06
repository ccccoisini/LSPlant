import java.security.MessageDigest

plugins {
    id("com.android.library")
}

val projectCompileSdk = 35
val projectMinSdk = 26
val buildToolsVersionValue = "35.0.0"
val xposedApiVersion = rootProject.extra["xposedApiVersion"] as String
val frameworkId = rootProject.extra["frameworkId"] as String
val frameworkName = rootProject.extra["frameworkName"] as String
val frameworkVersion = rootProject.extra["frameworkVersion"] as String
val frameworkVersionCode = rootProject.extra["frameworkVersionCode"] as Int

fun buildConfigString(value: String): String =
    "\"${value.replace("\\", "\\\\").replace("\"", "\\\"")}\""

android {
    namespace = "com.zygisk.framework.runtime"
    compileSdk = projectCompileSdk
    buildToolsVersion = buildToolsVersionValue

    buildFeatures {
        buildConfig = true
    }

    defaultConfig {
        minSdk = projectMinSdk
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        buildConfigField("String", "FRAMEWORK_ID", buildConfigString(frameworkId))
        buildConfigField("String", "FRAMEWORK_NAME", buildConfigString(frameworkName))
        buildConfigField("String", "FRAMEWORK_VERSION", buildConfigString(frameworkVersion))
        buildConfigField("int", "FRAMEWORK_VERSION_CODE", frameworkVersionCode.toString())
    }
}

dependencies {
    compileOnly(files(rootProject.file("libs/libxposed-api-$xposedApiVersion.jar")))
    testImplementation("junit:junit:4.13.2")
}

tasks.configureEach {
    if (name == "extractReleaseAnnotations" || name.startsWith("lint")) {
        enabled = false
    }
}

tasks.register("buildFrameworkDex") {
    group = "build"
    description = "使用 R8 混淆并生成 framework.dex 与 framework.mapping。"
    dependsOn("compileReleaseJavaWithJavac")

    doLast {
        val outDir = layout.buildDirectory.dir("outputs/framework").get().asFile
        val dexDir = layout.buildDirectory.dir("intermediates/framework-r8").get().asFile
        delete(outDir)
        delete(dexDir)
        outDir.mkdirs()
        dexDir.mkdirs()

        val androidJar = File(android.sdkDirectory, "platforms/android-$projectCompileSdk/android.jar")
        val d8Jar = File(android.sdkDirectory, "build-tools/$buildToolsVersionValue/lib/d8.jar")
        val classesDir = layout.buildDirectory.dir("intermediates/javac/release/compileReleaseJavaWithJavac/classes").get().asFile
        val classesJar = layout.buildDirectory.file("intermediates/r8-input/framework-classes.jar").get().asFile
        classesJar.parentFile.mkdirs()
        ant.withGroovyBuilder {
            "zip"("destfile" to classesJar.absolutePath, "basedir" to classesDir.absolutePath)
        }
        val rules = file("proguard-rules.pro")
        val mapping = File(outDir, "framework.mapping")

        javaexec {
            classpath = files(d8Jar)
            mainClass.set("com.android.tools.r8.R8")
            args(
                "--release",
                "--min-api", projectMinSdk.toString(),
                "--output", dexDir.absolutePath,
                "--pg-conf", rules.absolutePath,
                "--pg-map-output", mapping.absolutePath,
                "--lib", androidJar.absolutePath,
                classesJar.absolutePath,
            )
        }

        copy {
            from(File(dexDir, "classes.dex"))
            into(outDir)
            rename { "framework.dex" }
        }

        val dex = File(outDir, "framework.dex")
        val sha = sha256(dex)
        File(outDir, "framework.sha256").writeText("$sha  framework.dex\n")
        println("FRAMEWORK_DEX_SHA256: $sha")
    }
}

tasks.register("exportApi82TestRuntime") {
    group = "build"
    description = "导出供 hook_template JVM/Android 单元测试使用的 API 82 runtime。"
    dependsOn("compileDebugJavaWithJavac")
    doLast {
        val classesDir = layout.buildDirectory.dir(
            "intermediates/javac/debug/compileDebugJavaWithJavac/classes").get().asFile
        val destination = rootProject.file("hook_template/libs/xposed-api82-runtime-test.jar")
        destination.parentFile.mkdirs()
        delete(destination)
        ant.withGroovyBuilder {
            "zip"("destfile" to destination.absolutePath, "basedir" to classesDir.absolutePath,
                "includes" to "com/zygisk/framework/runtime/**,de/robv/android/xposed/**,android/app/AndroidAppHelper.class," +
                        "android/content/res/XResources*.class,android/content/res/XModuleResources.class," +
                        "android/content/res/XResForwarder.class,external/org/apache/commons/lang3/**")
        }
        println("API82_TEST_RUNTIME: ${destination.absolutePath}")
    }
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
