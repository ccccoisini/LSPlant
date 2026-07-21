import java.security.MessageDigest

plugins {
    id("com.android.library")
}

val projectCompileSdk = 35
val projectMinSdk = 26
val buildToolsVersionValue = "35.0.0"
val libxposedApiVersion = rootProject.extra["libxposedApiVersion"] as String

android {
    namespace = "com.example.hook.demo"
    compileSdk = projectCompileSdk
    buildToolsVersion = buildToolsVersionValue

    defaultConfig {
        minSdk = projectMinSdk
    }
}

dependencies {
    compileOnly(files(rootProject.file("libs/libxposed-api-$libxposedApiVersion.jar")))
}

tasks.configureEach {
    if (name == "extractReleaseAnnotations" || name.startsWith("lint")) {
        enabled = false
    }
}

tasks.register("buildHookDex") {
    group = "build"
    description = "使用 R8 混淆 Demo Hook 模块并生成 module.dex。"
    dependsOn("compileReleaseJavaWithJavac")

    doLast {
        val outDir = layout.buildDirectory.dir("outputs/hook").get().asFile
        val dexDir = layout.buildDirectory.dir("intermediates/module-r8").get().asFile
        delete(outDir)
        delete(dexDir)
        outDir.mkdirs()
        dexDir.mkdirs()

        val androidJar = File(android.sdkDirectory, "platforms/android-$projectCompileSdk/android.jar")
        val d8Jar = File(android.sdkDirectory, "build-tools/$buildToolsVersionValue/lib/d8.jar")
        val classesDir = layout.buildDirectory.dir("intermediates/javac/release/compileReleaseJavaWithJavac/classes").get().asFile
        val classesJar = layout.buildDirectory.file("intermediates/r8-input/module-classes.jar").get().asFile
        classesJar.parentFile.mkdirs()
        ant.withGroovyBuilder {
            "zip"("destfile" to classesJar.absolutePath, "basedir" to classesDir.absolutePath)
        }
        val apiJar = rootProject.file("libs/libxposed-api-$libxposedApiVersion.jar")
        val mapping = File(outDir, "module.mapping")

        javaexec {
            classpath = files(d8Jar)
            mainClass.set("com.android.tools.r8.R8")
            args(
                "--release",
                "--min-api", projectMinSdk.toString(),
                "--output", dexDir.absolutePath,
                "--pg-conf", file("proguard-rules.pro").absolutePath,
                "--pg-map-output", mapping.absolutePath,
                "--lib", androidJar.absolutePath,
                "--classpath", apiJar.absolutePath,
                classesJar.absolutePath
            )
        }

        copy {
            from(File(dexDir, "classes.dex"))
            into(outDir)
            rename { "module.dex" }
        }
        copy {
            from("src/main/resources/META-INF/xposed")
            into(File(outDir, "META-INF/xposed"))
        }
        rewriteJavaInitList(mapping, File(outDir, "META-INF/xposed/java_init.list"))

        val dex = File(outDir, "module.dex")
        val sha = sha256(dex)
        File(outDir, "module.sha256").writeText("$sha  module.dex\n")
        println("MODULE_DEX_SHA256: $sha")
    }
}

fun rewriteJavaInitList(mapping: File, initList: File) {
    val map = mapping.readLines()
        .filter { it.endsWith(":") && it.contains(" -> ") }
        .associate { line ->
            val clean = line.removeSuffix(":")
            val parts = clean.split(" -> ")
            parts[0].trim() to parts[1].trim()
        }
    val rewritten = initList.readLines()
        .map { line ->
            val trimmed = line.trim()
            if (trimmed.isEmpty() || trimmed.startsWith("#")) line else map[trimmed] ?: line
        }
        .joinToString(System.lineSeparator()) + System.lineSeparator()
    initList.writeText(rewritten)
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
