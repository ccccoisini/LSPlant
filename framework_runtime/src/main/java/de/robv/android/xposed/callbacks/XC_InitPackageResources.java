package de.robv.android.xposed.callbacks;

import android.content.res.XResources;
import de.robv.android.xposed.IXposedHookInitPackageResources;
import de.robv.android.xposed.XposedBridge.CopyOnWriteSortedSet;

/** API 82 资源回调参数声明。 */
public abstract class XC_InitPackageResources extends XCallback implements IXposedHookInitPackageResources {
    public XC_InitPackageResources() { super(); }
    public XC_InitPackageResources(int priority) { super(priority); }

    public static final class InitPackageResourcesParam extends XCallback.Param {
        public InitPackageResourcesParam(CopyOnWriteSortedSet<XC_InitPackageResources> callbacks) { super(callbacks); }
        public String packageName;
        public XResources res;
    }

    @Override protected void call(Param param) throws Throwable {
        if (param instanceof InitPackageResourcesParam) {
            throw new UnsupportedOperationException("Xposed resource hooks are not supported");
        }
    }
}
