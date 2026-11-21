package dev.rflab.terminal;

/**
 * 版本号规则(纯 Java,无 Android 依赖 —— 主机可单独编译测试)。
 *
 * <p>Release tag 形如 {@code v0.7.0},APP 的 {@code versionName} 形如 {@code 0.7.0}。
 * 比较只看点分数字段,缺位补 0({@code 1.2 == 1.2.0});数字段全等时,不带后缀的版本更大
 * ({@code 0.7.0 > 0.7.0-rc1}),后缀本身按字典序比较。数字段按数值比,不是字典序
 * ({@code 0.10.0 > 0.9.0})。
 */
final class AppVersion {
    private AppVersion() {
    }

    /** tag → version(去掉 {@code v}/{@code V} 前缀);不是合法版本号返回 null。 */
    static String fromTag(String tag) {
        if (tag == null) {
            return null;
        }
        String t = tag.trim();
        if (t.startsWith("v") || t.startsWith("V")) {
            t = t.substring(1);
        }
        if (!t.matches("\\d+(\\.\\d+)*(-[0-9A-Za-z.]+)?")) {
            return null;
        }
        return t;
    }

    /** 数值比较:返回 -1 / 0 / 1(只看数字段与预发布后缀,忽略 {@code +build} 之后的内容)。 */
    static int compare(String a, String b) {
        String[] sa = split(a);
        String[] sb = split(b);
        String[] ca = sa[0].split("\\.");
        String[] cb = sb[0].split("\\.");
        int n = Math.max(ca.length, cb.length);
        for (int i = 0; i < n; i++) {
            long x = i < ca.length ? num(ca[i]) : 0L;
            long y = i < cb.length ? num(cb[i]) : 0L;
            if (x != y) {
                return x < y ? -1 : 1;
            }
        }
        boolean pa = !sa[1].isEmpty();
        boolean pb = !sb[1].isEmpty();
        if (!pa && !pb) {
            return 0;
        }
        if (!pa) {
            return 1;   // 正式版 > 预发布
        }
        if (!pb) {
            return -1;
        }
        int c = sa[1].compareTo(sb[1]);
        return c == 0 ? 0 : (c < 0 ? -1 : 1);
    }

    /** {@code candidate} 是否比 {@code current} 新;任一非法时按保守方向返回。 */
    static boolean isNewer(String candidate, String current) {
        if (fromTag(candidate) == null) {
            return false;
        }
        if (fromTag(current) == null) {
            return true;
        }
        return compare(candidate, current) > 0;
    }

    private static String[] split(String v) {
        String s = v == null ? "" : v.trim();
        if (s.startsWith("v") || s.startsWith("V")) {
            s = s.substring(1);
        }
        int cutoff = s.length();
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            if (ch == '-' || ch == '+') {
                cutoff = i;
                break;
            }
        }
        String core = s.substring(0, cutoff);
        String suffix = cutoff < s.length() && s.charAt(cutoff) == '-'
                ? s.substring(cutoff + 1) : "";
        return new String[]{core, suffix};
    }

    private static long num(String s) {
        try {
            return Long.parseLong(s.trim());
        } catch (NumberFormatException e) {
            return 0L;
        }
    }
}
