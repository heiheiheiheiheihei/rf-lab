package dev.rflab.terminal;

/**
 * {@code SHA256SUMS.txt} 解析(纯 Java,无 Android 依赖,主机可测)。
 *
 * <p>发布流水线里是 {@code (cd dist && sha256sum ./*.apk > SHA256SUMS.txt)},
 * 所以文件名形如 {@code ./rf-lab-android.apk};而 {@code sha256sum -c} 也接受纯文件名。
 * 这里按 basename 匹配,兼容 {@code ./}、{@code dist/} 与 {@code sha256sum -b} 的
 * {@code *name} 写法 —— 认不出就返回 null,由调用方拒绝安装(失败关闭)。
 */
final class Sums {
    private Sums() {
    }

    /** 取出 {@code name} 对应的 64 位十六进制 SHA-256;没有合法条目时返回 null。 */
    static String hashFor(String text, String name) {
        if (text == null || name == null) {
            return null;
        }
        for (String line : text.split("\n")) {
            String l = line.trim();
            if (l.isEmpty()) {
                continue;
            }
            String[] parts = l.split("\\s+", 2);
            if (parts.length != 2) {
                continue;
            }
            String hash = parts[0].trim();
            if (!hash.matches("[0-9a-fA-F]{64}")) {
                continue;
            }
            if (basename(parts[1].trim()).equals(name)) {
                return hash;
            }
        }
        return null;
    }

    private static String basename(String path) {
        String p = path.trim();
        if (p.startsWith("*")) {          // sha256sum -b:"hash *file"
            p = p.substring(1);
        }
        int slash = p.lastIndexOf('/');
        if (slash >= 0) {
            p = p.substring(slash + 1);
        }
        return p;
    }
}
