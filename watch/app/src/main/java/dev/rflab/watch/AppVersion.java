package dev.rflab.watch;

import java.util.Locale;
import java.util.regex.Pattern;

/** Strict {@code x.y.z} comparison for watch releases; no Android dependencies. */
final class AppVersion {
    private static final Pattern NUM = Pattern.compile("\\d{1,4}");

    private AppVersion() {
    }

    /** {@code watch-v0.1.2}, {@code v0.1.2} or {@code 0.1.2} → {@code 0.1.2}; else null. */
    static String fromTag(String tag) {
        if (tag == null) {
            return null;
        }
        String s = tag.trim();
        if (s.startsWith("watch-")) {
            s = s.substring(6);
        }
        if (s.startsWith("v") || s.startsWith("V")) {
            s = s.substring(1);
        }
        return parse(s) == null ? null : s;
    }

    /** Numeric compare; {@code >0} when {@code a} is newer. Both must parse. */
    static int compare(String a, String b) {
        int[] x = parse(a);
        int[] y = parse(b);
        if (x == null || y == null) {
            return 0;
        }
        for (int i = 0; i < 3; i++) {
            if (x[i] != y[i]) {
                return x[i] < y[i] ? -1 : 1;
            }
        }
        return 0;
    }

    static boolean isNewer(String candidate, String current) {
        return compare(candidate, current) > 0;
    }

    static String normalize(String v) {
        int[] p = parse(v);
        return p == null ? null
                : String.format(Locale.US, "%d.%d.%d", p[0], p[1], p[2]);
    }

    private static int[] parse(String v) {
        if (v == null) {
            return null;
        }
        String s = v.trim();
        String[] parts = s.split("\\.", -1);
        if (parts.length != 3) {
            return null;
        }
        int[] out = new int[3];
        for (int i = 0; i < 3; i++) {
            if (!NUM.matcher(parts[i]).matches()) {
                return null;
            }
            try {
                out[i] = Integer.parseInt(parts[i]);
            } catch (NumberFormatException e) {
                return null;
            }
        }
        return out;
    }
}
