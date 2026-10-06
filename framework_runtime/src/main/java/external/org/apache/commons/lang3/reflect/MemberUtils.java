package external.org.apache.commons.lang3.reflect;

/** 按 Java 反射参数转换成本为 XposedHelpers 选择最匹配的成员。 */
public final class MemberUtils {
    private MemberUtils() { }

    /** 返回转换成本较低参数签名的比较结果。 */
    public static int compareParameterTypes(Class<?>[] left, Class<?>[] right, Class<?>[] actual) {
        double leftCost = totalCost(actual, left);
        double rightCost = totalCost(actual, right);
        return Double.compare(leftCost, rightCost);
    }

    private static double totalCost(Class<?>[] actual, Class<?>[] destination) {
        double total = 0;
        for (int index = 0; index < actual.length; index++) {
            Class<?> from = actual[index];
            Class<?> to = destination[index];
            if (from == null || from == to) continue;
            if (from.isPrimitive() && !to.isPrimitive()) from = wrapper(from);
            if (to.isAssignableFrom(from)) {
                Class<?> current = from;
                while (current != null && current != to) {
                    total += current.isInterface() ? 0.25 : 1.0;
                    current = current.getSuperclass();
                }
                continue;
            }
            Class<?> primitiveFrom = primitive(from);
            Class<?> primitiveTo = primitive(to);
            if (primitiveFrom != primitiveTo) total += primitiveDistance(primitiveFrom, primitiveTo);
        }
        return total;
    }

    private static double primitiveDistance(Class<?> from, Class<?> to) {
        if (from == null || to == null) return 10;
        Class<?>[] widening;
        if (from == byte.class) widening = new Class<?>[]{short.class, int.class, long.class, float.class, double.class};
        else if (from == short.class) widening = new Class<?>[]{int.class, long.class, float.class, double.class};
        else if (from == char.class) widening = new Class<?>[]{int.class, long.class, float.class, double.class};
        else if (from == int.class) widening = new Class<?>[]{long.class, float.class, double.class};
        else if (from == long.class) widening = new Class<?>[]{float.class, double.class};
        else if (from == float.class) widening = new Class<?>[]{double.class};
        else return 10;
        for (int i = 0; i < widening.length; i++) if (widening[i] == to) return (i + 1) * 0.1;
        return 10;
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
