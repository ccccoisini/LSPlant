import java.security.MessageDigest

plugins {
    id("com.android.library")
}

val projectCompileSdk = 35
val projectMinSdk = 26
val buildToolsVersionValue = "35.0.0"
val libxposedApiVersion = rootProject.extra["libxposedApiVersion"] as String

android {
    namespace = "com.example.zygiskhook.runtime"
    compileSdk = projectCompileSdk
    buildToolsVersion = buildToolsVersionValue

    defaultConfig {
        minSdk = projectMinSdk
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
}

dependencies {
    implementation(files(rootProject.file("libs/libxposed-api-$libxposedApiVersion.jar")))
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
        val apiJar = rootProject.file("libs/libxposed-api-$libxposedApiVersion.jar")
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
                apiJar.absolutePath
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
