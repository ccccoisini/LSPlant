package com.example.hook.template;

import android.util.Log;

import io.github.libxposed.api.XposedModule;
import io.github.libxposed.api.XposedModuleInterface.ModuleLoadedParam;

/**
 * 独立 Hook 模块模板入口，业务模块可复制该类作为开发起点。
 */
public class TemplateHookModule extends XposedModule {
    /**
     * 创建模板模块入口。
     */
    public TemplateHookModule() {
    }

    /**
     * 在模块装载后输出一条模板日志，实际业务可在此准备轻量状态。
     *
     * @param param 当前进程的模块装载参数
     */
    @Override
    public void onModuleLoaded(ModuleLoadedParam param) {
        log(Log.INFO, "zygisk_framework.Module", "TEMPLATE_MODULE_LOADED process=" + param.getProcessName());
    }
}
