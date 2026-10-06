package android.content.res;

/** API 82 XResForwarder 类型声明；资源转发操作不受支持。 */
public class XResForwarder {
    private final Resources resources;
    private final int id;
    public XResForwarder(Resources resources, int id) {
        throw new UnsupportedOperationException("Xposed resource hooks are not supported");
    }
    public Resources getResources() { return resources; }
    public int getId() { return id; }
}
