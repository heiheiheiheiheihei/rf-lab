package dev.rflab.terminal;

/**
 * 主机测试(不需要 Android SDK / Gradle):{@code tools/app_update_test.sh} 用 javac 编译
 * {@link AppVersion}、{@link Sums} 与本文件后直接运行 main,任何断言失败以非 0 退出。
 *
 * <p>覆盖自更新里两条会静默出错的纯逻辑:
 * <ol>
 *   <li>Release tag → 版本比较(决定弹不弹「发现新版本」);</li>
 *   <li>{@code SHA256SUMS.txt} 查表(决定安装包能不能过校验)。发布流水线写的是
 *       {@code ./rf-lab-android.apk},按纯文件名匹配会永远失败 —— 这里锁住这个格式。</li>
 * </ol>
 */
public final class UpdateLogicTest {
    private static int checks;

    private UpdateLogicTest() {
    }

    public static void main(String[] args) {
        versionChecks();
        sumChecks();
        System.out.println("UpdateLogic OK (" + checks + " checks)");
    }

    private static void versionChecks() {
        // tag → version
        tag("v0.7.0", "0.7.0");
        tag("0.7.0", "0.7.0");
        tag("V1.2", "1.2");
        tag(" v1.2.3-rc1 ", "1.2.3-rc1");
        tag("release-0.7.0", null);
        tag("v", null);
        tag("", null);
        tag(null, null);

        // 新/旧
        newer("0.7.0", "0.6.0", true);
        newer("0.6.0", "0.7.0", false);
        newer("0.7.0", "0.7.0", false);
        newer("v0.7.0", "0.6.0", true);
        newer("1.0.0", "0.99.99", true);
        newer("0.10.0", "0.9.0", true);        // 数值比较,不是字典序
        newer("0.7", "0.7.0", false);          // 缺位补 0
        newer("0.7.0", "0.7", false);
        newer("0.7.0-rc1", "0.7.0", false);    // 预发布 < 正式版
        newer("0.7.0", "0.7.0-rc1", true);
        newer("0.7.1", "0.7.0-rc1", true);
        newer("0.7.0+build5", "0.7.0", false); // build 元数据不参与比较
        newer("latest", "0.6.0", false);       // 非法 candidate 永远不算更新
        newer("0.7.0", "dev", true);           // 当前版本非法时保守地认为有更新

        // 同值
        same("1.2", "1.2.0");
        same("v0.7.0", "0.7.0");
        same("0.7.0", "v0.7.0");
    }

    private static void sumChecks() {
        String h = "e68db13b849e5b314c3964ab15f0f1936c0adfbf35b6330b76887ae635c40ffa";
        // 真实发布格式:sha256sum ./*.apk —— 文件名带 ./ 前缀。
        String real = h + "  ./rf-lab-0.6.0.apk\n" + h + "  ./rf-lab-android.apk\n";
        assertEquals("hashFor real ./name", h, Sums.hashFor(real, "rf-lab-android.apk"));
        assertEquals("hashFor real versioned", h, Sums.hashFor(real, "rf-lab-0.6.0.apk"));

        // 其它 sha256sum 写法。
        assertEquals("hashFor plain", h, Sums.hashFor(h + "  rf-lab-android.apk", "rf-lab-android.apk"));
        assertEquals("hashFor binary *", h, Sums.hashFor(h + " *rf-lab-android.apk", "rf-lab-android.apk"));
        assertEquals("hashFor path", h, Sums.hashFor(h + "  dist/rf-lab-android.apk", "rf-lab-android.apk"));
        assertEquals("hashFor crlf", h, Sums.hashFor(h + "  ./rf-lab-android.apk\r\n", "rf-lab-android.apk"));

        // 认不出就返回 null(调用方据此拒绝安装)。
        assertEquals("hashFor missing", null, Sums.hashFor(real, "rf-lab-0.8.0.apk"));
        assertEquals("hashFor no text", null, Sums.hashFor(null, "rf-lab-android.apk"));
        assertEquals("hashFor empty", null, Sums.hashFor("", "rf-lab-android.apk"));
        assertEquals("hashFor short hash", null, Sums.hashFor("deadbeef  ./rf-lab-android.apk", "rf-lab-android.apk"));
        assertEquals("hashFor garbage", null, Sums.hashFor("not a sum file", "rf-lab-android.apk"));
    }

    private static void tag(String tag, String expected) {
        String actual = AppVersion.fromTag(tag);
        assertEquals("fromTag(" + quote(tag) + ")", expected, actual);
    }

    private static void newer(String candidate, String current, boolean expected) {
        boolean actual = AppVersion.isNewer(candidate, current);
        assertEquals("isNewer(" + quote(candidate) + ", " + quote(current) + ")", expected, actual);
    }

    private static void same(String a, String b) {
        int actual = AppVersion.compare(a, b);
        assertEquals("compare(" + quote(a) + ", " + quote(b) + ")", 0, actual);
    }

    private static void assertEquals(String what, Object expected, Object actual) {
        checks++;
        boolean ok = expected == null ? actual == null : expected.equals(actual);
        if (!ok) {
            throw new AssertionError(what + ": expected " + expected + ", got " + actual);
        }
    }

    private static String quote(String s) {
        return s == null ? "null" : "\"" + s + "\"";
    }
}
