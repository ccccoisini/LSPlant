-dontwarn **
-repackageclasses zhf
-keep class io.github.libxposed.api.** { *; }
-keep class io.github.libxposed.api.error.** { *; }
-keep,allowobfuscation class com.example.zygiskhook.runtime.NativeBridge { *; }
-keep,allowobfuscation class com.example.zygiskhook.runtime.RuntimeBootstrap { *; }
-keepclassmembers class com.example.zygiskhook.runtime.NativeBridge {
    native <methods>;
    public static *;
}
-keepclassmembers class com.example.zygiskhook.runtime.RuntimeBootstrap {
    public static *;
}
-keepclassmembers class * {
    public java.lang.Object dispatch(java.lang.Object[]);
}
