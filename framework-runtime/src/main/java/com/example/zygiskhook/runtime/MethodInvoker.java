package com.example.zygiskhook.runtime;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

import io.github.libxposed.api.XposedInterface;

final class MethodInvoker implements XposedInterface.Invoker<MethodInvoker, Method> {
    private final HookRegistry registry;
    private final Method method;
    private XposedInterface.Invoker.Type type = XposedInterface.Invoker.Type.ORIGIN;

    MethodInvoker(HookRegistry registry, Method method) {
        this.registry = registry;
        this.method = method;
        this.method.setAccessible(true);
    }

    @Override
    public MethodInvoker setType(XposedInterface.Invoker.Type type) {
        this.type = type == null ? XposedInterface.Invoker.Type.ORIGIN : type;
        return this;
    }

    @Override
    public Object invoke(Object receiver, Object... args)
            throws InvocationTargetException, IllegalArgumentException, IllegalAccessException {
        if (type instanceof XposedInterface.Invoker.Type.Chain) {
            try {
                Object[] lsplantArgs = Modifier.isStatic(method.getModifiers())
                        ? normalize(args)
                        : prepend(receiver, normalize(args));
                return registry.dispatchForTests(method, lsplantArgs);
            } catch (Throwable throwable) {
                throw new InvocationTargetException(throwable);
            }
        }
        return method.invoke(Modifier.isStatic(method.getModifiers()) ? null : receiver, args);
    }

    @Override
    public Object invokeSpecial(Object receiver, Object... args)
            throws InvocationTargetException, IllegalArgumentException, IllegalAccessException {
        throw new UnsupportedOperationException("invokeSpecial is not supported in MVP");
    }

    private static Object[] normalize(Object[] args) {
        return args == null ? new Object[0] : args;
    }

    private static Object[] prepend(Object receiver, Object[] args) {
        Object[] result = new Object[args.length + 1];
        result[0] = receiver;
        System.arraycopy(args, 0, result, 1, args.length);
        return result;
    }
}
