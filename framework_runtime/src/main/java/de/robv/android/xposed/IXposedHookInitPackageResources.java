package de.robv.android.xposed;

import android.content.res.XResources;
import de.robv.android.xposed.callbacks.XC_InitPackageResources;
import de.robv.android.xposed.callbacks.XC_InitPackageResources.InitPackageResourcesParam;

/** API 82 资源入口的类型声明；当前框架调用时会明确报告不支持。 */
public interface IXposedHookInitPackageResources extends IXposedMod {
    void handleInitPackageResources(InitPackageResourcesParam param) throws Throwable;

    final class Wrapper extends XC_InitPackageResources {
        private final IXposedHookInitPackageResources instance;
        public Wrapper(IXposedHookInitPackageResources instance) { this.instance = instance; }
        @Override public void handleInitPackageResources(InitPackageResourcesParam param) throws Throwable {
            throw new UnsupportedOperationException("Xposed resource hooks are not supported");
        }
    }
}
