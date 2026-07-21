-dontwarn **
-repackageclasses zhm
-keep,allowobfuscation class com.example.hook.demo.HammerDemoModule {
    public <init>();
    public void onModuleLoaded(...);
    public void onPackageReady(...);
}
-keepclassmembers,allowobfuscation class * extends io.github.libxposed.api.XposedModule {
    public <init>();
}
