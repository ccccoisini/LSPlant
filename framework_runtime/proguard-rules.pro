-dontwarn **
-repackageclasses zhf
-keep class de.robv.android.xposed.** { *; }
-keep class de.robv.android.xposed.callbacks.** { *; }
-keep class android.app.AndroidAppHelper { *; }
-keep class android.content.res.XResources { *; }
-keep class android.content.res.XModuleResources { *; }
-keep class android.content.res.XResForwarder { *; }
-keep class external.org.apache.commons.lang3.** { *; }
-keep class com.zygisk.framework.runtime.FrameworkServices { *; }
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
