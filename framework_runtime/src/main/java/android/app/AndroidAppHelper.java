package android.app;

import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.res.Resources;

import com.zygisk.framework.runtime.FrameworkServices;
import de.robv.android.xposed.XSharedPreferences;

/** API 82 AndroidAppHelper 的应用进程信息实现。 */
public final class AndroidAppHelper {
    private AndroidAppHelper() { }

    /** 当前目标应用进程名。 */
    public static String currentProcessName() { return FrameworkServices.currentProcessName(); }

    /** 当前包的 ApplicationInfo。 */
    public static ApplicationInfo currentApplicationInfo() { return FrameworkServices.currentApplicationInfo(); }

    /** 当前目标包名。 */
    public static String currentPackageName() { return FrameworkServices.currentPackageName(); }

    /** Application.attach 之前返回 null。 */
    public static Application currentApplication() { return FrameworkServices.currentApplication(); }

    /** 读取 CLI 管理的只读配置 group。 */
    public static SharedPreferences getSharedPreferencesForPackage(
            String packageName, String prefFileName, int mode) {
        return FrameworkServices.getRemotePreferences(
                FrameworkServices.moduleIdForPackage(packageName), prefFileName);
    }

    /** 读取传统默认 preference group。 */
    public static SharedPreferences getDefaultSharedPreferencesForPackage(String packageName) {
        return getSharedPreferencesForPackage(packageName, packageName + "_preferences", 0);
    }

    /** 刷新本框架支持的旧版共享配置快照。 */
    public static void reloadSharedPreferencesIfNeeded(SharedPreferences preferences) {
        if (preferences instanceof XSharedPreferences) {
            XSharedPreferences xposed = (XSharedPreferences) preferences;
            if (xposed.hasFileChanged()) xposed.reload();
        }
    }

    /** 资源注入不属于当前 API 82 应用进程支持范围。 */
    public static void addActiveResource(String resDir, float scale,
                                         boolean isThemeable, Resources resources) {
        throw new UnsupportedOperationException("Xposed resource hooks are not supported");
    }
}
