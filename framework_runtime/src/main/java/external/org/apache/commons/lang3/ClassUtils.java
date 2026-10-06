package external.org.apache.commons.lang3;

/** 仅实现 XposedHelpers 使用到的类型解析与参数可赋值判断。 */
public final class ClassUtils {
    private ClassUtils() { }

    /** 解析 Java 源码风格的类名、数组名和基本类型名。 */
    public static Class<?> getClass(ClassLoader loader, String name, boolean initialize)
            throws ClassNotFoundException {
        String value = name.replace('/', '.');
        if (value.startsWith("L") && value.endsWith(";")) value = value.substring(1, value.length() - 1);
        if (value.endsWith("[]")) return java.lang.reflect.Array.newInstance(
                getClass(loader, value.substring(0, value.length() - 2), initialize), 0).getClass();
        Class<?> primitive = primitive(value);
        return primitive == null ? Class.forName(value, initialize, loader) : primitive;
    }

    /** 判断实参类型是否可以转换为指定参数类型。 */
    public static boolean isAssignable(Class<?>[] source, Class<?>[] target, boolean autoboxing) {
        if (source.length != target.length) return false;
        for (int index = 0; index < source.length; index++) {
            Class<?> from = source[index];
            Class<?> to = target[index];
            if (from == null) {
                if (to.isPrimitive()) return false;
                continue;
            }
            if (autoboxing) {
                if (from.isPrimitive() && !to.isPrimitive()) from = wrapper(from);
                if (to.isPrimitive() && !from.isPrimitive()) from = primitive(from);
            }
            if (from == null || !assignable(from, to)) return false;
        }
        return true;
    }

    private static boolean assignable(Class<?> from, Class<?> to) {
        if (to.isAssignableFrom(from)) return true;
        if (!from.isPrimitive() || !to.isPrimitive()) return false;
        if (from == byte.class) return to == short.class || to == int.class || to == long.class
                || to == float.class || to == double.class;
        if (from == short.class || from == char.class) return to == int.class || to == long.class
                || to == float.class || to == double.class;
        if (from == int.class) return to == long.class || to == float.class || to == double.class;
        if (from == long.class) return to == float.class || to == double.class;
        return from == float.class && to == double.class;
    }

    private static Class<?> primitive(String name) {
        if (name.equals("boolean")) return boolean.class;
        if (name.equals("byte")) return byte.class;
        if (name.equals("char")) return char.class;
        if (name.equals("short")) return short.class;
        if (name.equals("int")) return int.class;
        if (name.equals("long")) return long.class;
        if (name.equals("float")) return float.class;
        if (name.equals("double")) return double.class;
        if (name.equals("void")) return void.class;
        return null;
    }

    private static Class<?> primitive(Class<?> type) {
        if (type == Boolean.class) return boolean.class;
        if (type == Byte.class) return byte.class;
        if (type == Character.class) return char.class;
        if (type == Short.class) return short.class;
        if (type == Integer.class) return int.class;
        if (type == Long.class) return long.class;
        if (type == Float.class) return float.class;
        if (type == Double.class) return double.class;
        return type;
    }

    private static Class<?> wrapper(Class<?> type) {
        if (type == boolean.class) return Boolean.class;
        if (type == byte.class) return Byte.class;
        if (type == char.class) return Character.class;
        if (type == short.class) return Short.class;
        if (type == int.class) return Integer.class;
        if (type == long.class) return Long.class;
        if (type == float.class) return Float.class;
        if (type == double.class) return Double.class;
        return type;
    }
}
