plugins {
    id("com.android.library")
}

val projectCompileSdk = 35
val projectMinSdk = 26
val buildToolsVersionValue = "35.0.0"
val libxposedApiVersion = rootProject.extra["libxposedApiVersion"] as String

android {
    namespace = "com.example.hook.template"
    compileSdk = projectCompileSdk
    buildToolsVersion = buildToolsVersionValue

    defaultConfig {
        minSdk = projectMinSdk
    }
}

dependencies {
    compileOnly(files(rootProject.file("libs/libxposed-api-$libxposedApiVersion.jar")))
}
