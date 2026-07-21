import org.gradle.api.tasks.Exec

plugins {
    id("com.android.library")
}

val projectCompileSdk = 35
val projectMinSdk = 26
val ndkVersionValue = "29.0.14206865"
val cmakeVersionValue = "3.31.0"
val generatedFrameworkDir = layout.buildDirectory.dir("generated/zhook")

android {
    namespace = "com.example.zygiskhook.native_loader"
    compileSdk = projectCompileSdk
    ndkVersion = ndkVersionValue

    defaultConfig {
        minSdk = projectMinSdk
        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++23", "-fvisibility=hidden")
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
                    "-DZH_GENERATED_FRAMEWORK_DIR=${generatedFrameworkDir.get().asFile.absolutePath}"
                )
                abiFilters += listOf("arm64-v8a")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = cmakeVersionValue
        }
    }
}

val generateFrameworkDexHeader by tasks.registering(Exec::class) {
    group = "build"
    description = "把 framework.dex 导出为 native 编译使用的 C++ header。"
    dependsOn(":framework-runtime:buildFrameworkDex")
    val inputDex = rootProject.file("framework-runtime/build/outputs/framework/framework.dex")
    val outputHeader = generatedFrameworkDir.map { it.file("framework_dex.h").asFile }
    inputs.file(inputDex)
    outputs.file(outputHeader)
    commandLine(
        "bash",
        rootProject.file("tools/generate_binary_header.sh").absolutePath,
        inputDex.absolutePath,
        outputHeader.get().absolutePath,
        "zhook_generated::framework_dex",
        "kBytes"
    )
}

val generateFrameworkMappingHeader by tasks.registering(Exec::class) {
    group = "build"
    description = "把 framework.mapping 导出为 native 编译使用的 C++ header。"
    dependsOn(":framework-runtime:buildFrameworkDex")
    val inputMapping = rootProject.file("framework-runtime/build/outputs/framework/framework.mapping")
    val outputHeader = generatedFrameworkDir.map { it.file("framework_mapping.h").asFile }
    inputs.file(inputMapping)
    outputs.file(outputHeader)
    commandLine(
        "bash",
        rootProject.file("tools/generate_binary_header.sh").absolutePath,
        inputMapping.absolutePath,
        outputHeader.get().absolutePath,
        "zhook_generated::framework_mapping",
        "kBytes"
    )
}

tasks.matching { it.name.startsWith("externalNativeBuild") || it.name.contains("CMake") }.configureEach {
    dependsOn(generateFrameworkDexHeader, generateFrameworkMappingHeader)
}
