package android.content.res;

/** API 82 XModuleResources 类型声明；模块资源装载不受支持。 */
public class XModuleResources extends Resources {
    protected XModuleResources(AssetManager assets, android.util.DisplayMetrics metrics,
                               Configuration configuration) {
        super(assets, metrics, configuration);
    }
    public static XModuleResources createInstance(String modulePath, XResources original) {
        throw new UnsupportedOperationException("Xposed module resources are not supported");
    }
    public XResForwarder fwd(int id) {
        throw new UnsupportedOperationException("Xposed resource hooks are not supported");
    }
}
