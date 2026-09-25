-dontwarn **
-repackageclasses zhf
-keep class io.github.libxposed.api.** { *; }
-keep class io.github.libxposed.api.error.** { *; }
-keep,allowobfuscation class com.zygisk.framework.runtime.NativeBridge { *; }
-keep,allowobfuscation class com.zygisk.framework.runtime.RuntimeBootstrap { *; }
-keepclassmembers class com.zygisk.framework.runtime.NativeBridge {
    native <methods>;
    public static *;
}
-keepclassmembers class com.zygisk.framework.runtime.RuntimeBootstrap {
    public static *;
}
-keepclassmembers class * {
    public java.lang.Object dispatch(java.lang.Object[]);
}
