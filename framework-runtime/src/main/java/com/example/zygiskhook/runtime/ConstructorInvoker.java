package com.example.zygiskhook.runtime;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;

import io.github.libxposed.api.XposedInterface;

final class ConstructorInvoker<T> implements XposedInterface.CtorInvoker<T> {
    private final Constructor<T> constructor;

    ConstructorInvoker(Constructor<T> constructor) {
        this.constructor = constructor;
        this.constructor.setAccessible(true);
    }

    @Override
    public XposedInterface.CtorInvoker<T> setType(XposedInterface.Invoker.Type type) {
        if (!(type instanceof XposedInterface.Invoker.Type.Origin)) {
            throw new UnsupportedOperationException("Constructor chain invoker is not supported in MVP");
        }
        return this;
    }

    @Override
    public Object invoke(Object receiver, Object... args)
            throws InvocationTargetException, IllegalArgumentException, IllegalAccessException {
        throw new UnsupportedOperationException("Constructor invoke on existing object is not supported in MVP");
    }

    @Override
    public Object invokeSpecial(Object receiver, Object... args)
            throws InvocationTargetException, IllegalArgumentException, IllegalAccessException {
        throw new UnsupportedOperationException("Constructor invokeSpecial is not supported in MVP");
    }

    @Override
    public T newInstance(Object... args)
            throws InvocationTargetException, IllegalArgumentException,
            IllegalAccessException, InstantiationException {
        return constructor.newInstance(args);
    }

    @Override
    public <U> U newInstanceSpecial(Class<U> clazz, Object... args)
            throws InvocationTargetException, IllegalArgumentException,
            IllegalAccessException, InstantiationException {
        throw new UnsupportedOperationException("newInstanceSpecial is not supported in MVP");
    }
}
