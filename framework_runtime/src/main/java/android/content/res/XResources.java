package android.content.res;

import de.robv.android.xposed.callbacks.XC_LayoutInflated;

/** API 82 XResources 签名兼容层；资源替换和布局回调不受支持。 */
public class XResources extends Resources {
    public XResources(AssetManager assets, android.util.DisplayMetrics metrics,
                      Configuration configuration) {
        super(assets, metrics, configuration);
    }

    public static final class ResourceNames {
        public final int id;
        public final String pkg;
        public final String name;
        public final String type;
        public final String fullName;

        ResourceNames(int id, String pkg, String name, String type) {
            this.id = id;
            this.pkg = pkg;
            this.name = name;
            this.type = type;
            this.fullName = pkg + ":" + type + "/" + name;
        }

        /** 按非空包名、资源名、类型和非零 ID 匹配 API 82 资源信息。 */
        public boolean equals(String pkg, String name, String type, int id) {
            return (pkg == null || pkg.equals(this.pkg))
                    && (name == null || name.equals(this.name))
                    && (type == null || type.equals(this.type))
                    && (id == 0 || id == this.id);
        }
    }

    /** 旧 API 的 drawable 替换回调类型；调用资源替换能力仍会明确失败。 */
    public abstract static class DrawableLoader {
        public DrawableLoader() { }
        public abstract android.graphics.drawable.Drawable newDrawable(XResources resources, int id)
                throws Throwable;
        public android.graphics.drawable.Drawable newDrawableForDensity(
                XResources resources, int id, int density) throws Throwable {
            return newDrawable(resources, id);
        }
    }

    /** 旧 API 的尺寸替换值类型；当前不执行资源替换。 */
    public static class DimensionReplacement {
        private final float value;
        private final int unit;

        public DimensionReplacement(float value, int unit) {
            this.value = value;
            this.unit = unit;
        }

        public float getDimension(android.util.DisplayMetrics metrics) {
            return android.util.TypedValue.applyDimension(unit, value, metrics);
        }

        public int getDimensionPixelOffset(android.util.DisplayMetrics metrics) {
            return (int) android.util.TypedValue.applyDimension(unit, value, metrics);
        }

        public int getDimensionPixelSize(android.util.DisplayMetrics metrics) {
            float pixels = android.util.TypedValue.applyDimension(unit, value, metrics);
            int result = (int) (pixels + 0.5f);
            if (result != 0 || value == 0) return result;
            return value > 0 ? 1 : -1;
        }
    }

    /** 返回旧版 XResources 对象关联的包名；当前兼容层没有资源替换上下文。 */
    public String getPackageName() { return null; }
    /** 在资源对象构造期间提供旧 API 的包名查询占位。 */
    public static String getPackageNameDuringConstruction() { return null; }
    /** 当前不支持资源替换，调用时明确失败。 */
    public void setReplacement(int id, Object replacement) { unsupported(); }
    /** 当前不支持资源替换，调用时明确失败。 */
    public void setReplacement(String name, Object replacement) { unsupported(); }
    /** 当前不支持资源替换，调用时明确失败。 */
    public void setReplacement(String packageName, String type, String name, Object replacement) { unsupported(); }
    /** 当前不支持系统级资源替换，调用时明确失败。 */
    public static void setSystemWideReplacement(int id, Object replacement) { unsupported(); }
    /** 当前不支持系统级资源替换，调用时明确失败。 */
    public static void setSystemWideReplacement(String name, Object replacement) { unsupported(); }
    /** 当前不支持系统级资源替换，调用时明确失败。 */
    public static void setSystemWideReplacement(String packageName, String type,
                                                String name, Object replacement) { unsupported(); }
    /** 当前不支持创建伪资源 ID，调用时明确失败。 */
    public static int getFakeResId(String name) { unsupported(); return 0; }
    /** 当前不支持创建伪资源 ID，调用时明确失败。 */
    public static int getFakeResId(Resources resources, int id) { unsupported(); return 0; }
    /** 当前不支持追加资源，调用时明确失败。 */
    public int addResource(Resources resources, int id) { unsupported(); return 0; }
    /** 当前不支持布局 Hook，调用时明确失败。 */
    public XC_LayoutInflated.Unhook hookLayout(int id, XC_LayoutInflated callback) { unsupported(); return null; }
    /** 当前不支持布局 Hook，调用时明确失败。 */
    public XC_LayoutInflated.Unhook hookLayout(String name, XC_LayoutInflated callback) { unsupported(); return null; }
    /** 当前不支持布局 Hook，调用时明确失败。 */
    public XC_LayoutInflated.Unhook hookLayout(String packageName, String type, String name,
                                                XC_LayoutInflated callback) { unsupported(); return null; }
    /** 当前不支持系统级布局 Hook，调用时明确失败。 */
    public static XC_LayoutInflated.Unhook hookSystemWideLayout(int id, XC_LayoutInflated callback) {
        unsupported(); return null;
    }
    /** 当前不支持系统级布局 Hook，调用时明确失败。 */
    public static XC_LayoutInflated.Unhook hookSystemWideLayout(String name, XC_LayoutInflated callback) {
        unsupported(); return null;
    }
    /** 当前不支持系统级布局 Hook，调用时明确失败。 */
    public static XC_LayoutInflated.Unhook hookSystemWideLayout(String packageName, String type,
                                                                 String name, XC_LayoutInflated callback) {
        unsupported(); return null;
    }
    /** 当前不支持布局 Hook 卸载，调用时明确失败。 */
    public static void unhookLayout(String resourceDirectory, int id, XC_LayoutInflated callback) { unsupported(); }

    private static void unsupported() {
        throw new UnsupportedOperationException("Xposed resource hooks are not supported");
    }
}
