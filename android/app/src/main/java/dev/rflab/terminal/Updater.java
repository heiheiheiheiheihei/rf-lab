package dev.rflab.terminal;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * APP 自更新:查 GitHub Releases → 下载 APK → 校验 → 交给系统安装器。
 *
 * <p>边界(写死):
 * <ul>
 *   <li>只走 HTTPS,仓库固定在 {@link #REPO},不接受运行时下发的下载地址;</li>
 *   <li>只认带固定资产 {@value #APK_ASSET} 的正式 Release(跳过 draft/prerelease),
 *       纯固件/文档 Release 不会被当成 APP 版本;</li>
 *   <li>安装前三道校验:Release 的 {@value #SUMS_ASSET}(SHA-256)、APK 内包名 +
 *       versionCode/versionName 必须前进、签名证书与已装 APP 一致(系统还会再验一次);</li>
 *   <li>安装交给 {@link PackageInstaller},由系统弹确认框;不静默安装、不需要 root。</li>
 * </ul>
 *
 * <p>本类不碰 UI:网络/文件在后台线程,回调统一切回主线程,弹窗与文案在
 * {@link MainActivity}。
 */
final class Updater {
    private static final String TAG = "RF-Lab.Update";
    static final String REPO = "heiheiheiheiheihei/rf-lab";
    /** 发布流水线固定资产名(见 .github/workflows/android-release.yml)。 */
    private static final String APK_ASSET = "rf-lab-android.apk";
    private static final String SUMS_ASSET = "SHA256SUMS.txt";
    private static final long MAX_APK_BYTES = 40L * 1024 * 1024;
    private static final int MAX_TEXT_BYTES = 4 * 1024 * 1024;
    /** PackageInstaller 状态广播的 action(只发给本 APP,见 MainActivity)。 */
    static final String ACTION_INSTALL_RESULT = "dev.rflab.terminal.INSTALL_RESULT";

    private static final ExecutorService IO = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "rflab-update");
        t.setDaemon(true);
        return t;
    });
    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    /** 一个可安装的 Release。 */
    static final class Release {
        final String version;
        final String tag;
        final String notes;
        final String apkName;
        final String apkUrl;
        final String sumsUrl;
        final long apkSize;

        Release(String version, String tag, String notes, String apkName,
                String apkUrl, String sumsUrl, long apkSize) {
            this.version = version;
            this.tag = tag;
            this.notes = notes;
            this.apkName = apkName;
            this.apkUrl = apkUrl;
            this.sumsUrl = sumsUrl;
            this.apkSize = apkSize;
        }

        /** 弹窗正文:版本 + 体积 + Release 说明(截断到一段可读长度)。 */
        String summary() {
            StringBuilder sb = new StringBuilder();
            sb.append(releaseLine()).append(" · ").append(megabytes(apkSize));
            String body = notes == null ? "" : notes.trim();
            if (body.length() > 700) {
                body = body.substring(0, 700) + "…";
            }
            if (!body.isEmpty()) {
                sb.append("\n\n").append(body);
            }
            return sb.toString();
        }

        private String releaseLine() {
            return tag == null || tag.isEmpty() ? "v" + version : tag;
        }
    }

    interface CheckCallback {
        /** release 非空 = 有新版本;两者都空 = 已是最新;error 非空 = 检查失败。主线程回调。 */
        void onChecked(Release release, String error);
    }

    interface Progress {
        void onProgress(long done, long total);
    }

    /** 内部单参进度(下载循环里只知道自己读了多少字节)。 */
    private interface ProgressSink {
        void onProgress(long done);
    }

    interface DownloadCallback {
        void onReady(File apk);

        void onFailed(String message);
    }

    interface InstallCallback {
        void onCommitted();

        void onFailed(String message);
    }

    /** 当前已安装 APP 的 versionName;读不到时返回 "0"。 */
    static String currentVersion(Context context) {
        try {
            PackageInfo info = context.getPackageManager()
                    .getPackageInfo(context.getPackageName(), 0);
            return info.versionName == null ? "0" : info.versionName;
        } catch (PackageManager.NameNotFoundException e) {
            return "0";
        }
    }

    /** 后台查询最新 Release;只有比 {@code currentVersion} 新时才回调 release。 */
    void check(final String currentVersion, final CheckCallback cb) {
        IO.execute(() -> {
            Release found = null;
            String error = null;
            try {
                found = fetchLatest();
                if (found != null && !AppVersion.isNewer(found.version, currentVersion)) {
                    found = null;
                }
            } catch (Exception e) {
                error = describe(e);
                Log.w(TAG, "update check failed", e);
            }
            final Release release = found;
            final String err = error;
            MAIN.post(() -> cb.onChecked(release, err));
        });
    }

    /** 后台下载 + 校验;成功回调的是已校验的本地 APK 路径。 */
    void download(final Context context, final Release release, final Progress progress,
                  final DownloadCallback cb) {
        final Context app = context.getApplicationContext();
        IO.execute(() -> {
            File apk = null;
            String error = null;
            try {
                File dir = new File(app.getCacheDir(), "update");
                if (!dir.isDirectory() && !dir.mkdirs()) {
                    throw new IOException("无法创建缓存目录 " + dir);
                }
                File part = new File(dir, release.apkName + ".part");
                long got = downloadTo(release.apkUrl, part,
                        done -> MAIN.post(() -> progress.onProgress(done, release.apkSize)));
                if (got != release.apkSize) {
                    throw new IOException("下载大小不符:收到 " + got + " B,Release 标注 "
                            + release.apkSize + " B");
                }
                String sums = release.sumsUrl == null ? null : fetchText(release.sumsUrl);
                String expected = Sums.hashFor(sums, release.apkName);
                if (expected == null) {
                    throw new IOException("Release 缺少 " + SUMS_ASSET + " 中 "
                            + release.apkName + " 的 SHA-256,拒绝安装");
                }
                String actual = sha256(part);
                if (!actual.equalsIgnoreCase(expected)) {
                    throw new IOException("SHA-256 不匹配,安装包已损坏或被替换");
                }
                verifyApk(app, part);
                apk = new File(dir, "rf-lab-" + release.version + ".apk");
                if (apk.exists() && !apk.delete()) {
                    throw new IOException("无法替换旧的安装包 " + apk.getName());
                }
                if (!part.renameTo(apk)) {
                    throw new IOException("无法保存安装包 " + apk.getName());
                }
            } catch (Exception e) {
                error = describe(e);
                Log.w(TAG, "update download failed", e);
            }
            final File file = apk;
            final String err = error;
            MAIN.post(() -> {
                if (err != null) {
                    cb.onFailed(err);
                } else {
                    cb.onReady(file);
                }
            });
        });
    }

    /** 把已校验的 APK 写进 PackageInstaller 会话并提交;确认框由系统弹。 */
    void install(final Activity activity, final File apk, final InstallCallback cb) {
        IO.execute(() -> {
            String error = null;
            try {
                PackageInstaller installer = activity.getPackageManager().getPackageInstaller();
                PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(
                        PackageInstaller.SessionParams.MODE_FULL_INSTALL);
                params.setAppPackageName(activity.getPackageName());
                int sessionId = installer.createSession(params);

                Intent status = new Intent(ACTION_INSTALL_RESULT).setPackage(activity.getPackageName());
                PendingIntent pending = PendingIntent.getBroadcast(activity, sessionId, status,
                        PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE);

                try (PackageInstaller.Session session = installer.openSession(sessionId)) {
                    try (InputStream in = new FileInputStream(apk);
                         OutputStream out = session.openWrite("base.apk", 0, apk.length())) {
                        copy(in, out);
                        session.fsync(out);
                    }
                    session.commit(pending.getIntentSender());
                }
            } catch (Exception e) {
                error = describe(e);
                Log.w(TAG, "install session failed", e);
            }
            final String err = error;
            MAIN.post(() -> {
                if (err != null) {
                    cb.onFailed(err);
                } else {
                    cb.onCommitted();
                }
            });
        });
    }

    // ---- 网络 ----------------------------------------------------------------

    private static Release fetchLatest() throws IOException {
        String body = fetchText("https://api.github.com/repos/" + REPO + "/releases?per_page=30");
        JSONArray list;
        try {
            list = new JSONArray(body);
        } catch (JSONException e) {
            throw new IOException("Release 接口返回的不是 JSON", e);
        }
        Release best = null;
        for (int i = 0; i < list.length(); i++) {
            JSONObject o = list.optJSONObject(i);
            if (o == null || o.optBoolean("draft") || o.optBoolean("prerelease")) {
                continue;
            }
            String tag = o.optString("tag_name", "");
            String version = AppVersion.fromTag(tag);
            if (version == null) {
                continue;
            }
            String apkName = null;
            String apkUrl = null;
            String sumsUrl = null;
            long size = 0;
            JSONArray assets = o.optJSONArray("assets");
            for (int j = 0; assets != null && j < assets.length(); j++) {
                JSONObject asset = assets.optJSONObject(j);
                if (asset == null) {
                    continue;
                }
                String name = asset.optString("name", "");
                String url = asset.optString("browser_download_url", "");
                if (url.isEmpty()) {
                    continue;
                }
                if (SUMS_ASSET.equals(name)) {
                    sumsUrl = url;
                } else if (APK_ASSET.equals(name)) {
                    apkName = name;
                    apkUrl = url;
                    size = asset.optLong("size");
                } else if (("rf-lab-" + version + ".apk").equals(name) && apkUrl == null) {
                    apkName = name;
                    apkUrl = url;
                    size = asset.optLong("size");
                }
            }
            // 没有 APK 资产的 Release(纯固件/文档)不是 APP 版本。
            if (apkUrl == null || apkName == null || size <= 0 || size > MAX_APK_BYTES) {
                continue;
            }
            if (best == null || AppVersion.compare(version, best.version) > 0) {
                best = new Release(version, tag, o.optString("body", ""), apkName,
                        apkUrl, sumsUrl, size);
            }
        }
        return best;
    }

    private static String fetchText(String url) throws IOException {
        HttpURLConnection c = open(url);
        try (InputStream in = c.getInputStream()) {
            return new String(readLimited(in, MAX_TEXT_BYTES), StandardCharsets.UTF_8);
        } finally {
            c.disconnect();
        }
    }

    private static long downloadTo(String url, File dest, ProgressSink sink) throws IOException {
        HttpURLConnection c = open(url);
        try (InputStream in = c.getInputStream();
             OutputStream out = new FileOutputStream(dest)) {
            byte[] buf = new byte[32 * 1024];
            long total = 0;
            long reported = 0;
            int n;
            while ((n = in.read(buf)) > 0) {
                total += n;
                if (total > MAX_APK_BYTES) {
                    throw new IOException("安装包超过 " + megabytes(MAX_APK_BYTES) + " 上限");
                }
                out.write(buf, 0, n);
                if (sink != null && total - reported >= 128 * 1024) {
                    reported = total;
                    sink.onProgress(total);
                }
            }
            out.flush();
            if (sink != null) {
                sink.onProgress(total);
            }
            return total;
        } finally {
            c.disconnect();
        }
    }

    private static HttpURLConnection open(String url) throws IOException {
        URL u = new URL(url);
        if (!"https".equals(u.getProtocol())) {
            throw new IOException("只接受 https 地址");
        }
        HttpURLConnection c = (HttpURLConnection) u.openConnection();
        c.setConnectTimeout(8000);
        c.setReadTimeout(20000);
        c.setInstanceFollowRedirects(true);
        c.setRequestProperty("User-Agent", "RF-Lab-Android");
        c.setRequestProperty("Accept", "application/vnd.github+json");
        int code = c.getResponseCode();
        if (code != 200) {
            c.disconnect();
            throw new IOException("HTTP " + code + "(可能是网络被拦或 GitHub 限流)");
        }
        return c;
    }

    private static byte[] readLimited(InputStream in, int limit) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[16 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
            if (out.size() > limit) {
                throw new IOException("响应超过 " + limit + " B 上限");
            }
        }
        return out.toByteArray();
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[32 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
        }
        out.flush();
    }

    // ---- 校验 ----------------------------------------------------------------

    private static String sha256(File file) throws IOException {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            try (InputStream in = new FileInputStream(file)) {
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) {
                    md.update(buf, 0, n);
                }
            }
            return hex(md.digest());
        } catch (IOException e) {
            throw e;
        } catch (Exception e) {
            throw new IOException("无法计算 SHA-256", e);
        }
    }

    /**
     * 安装包自检:包名、版本必须前进,签名证书必须与已装 APP 一致。
     * 系统在真正安装时还会再验一次签名,这里是提前失败、给出可读原因。
     */
    private static void verifyApk(Context ctx, File apk) throws IOException {
        PackageManager pm = ctx.getPackageManager();
        int flags = Build.VERSION.SDK_INT >= Build.VERSION_CODES.P
                ? PackageManager.GET_SIGNING_CERTIFICATES
                : PackageManager.GET_SIGNATURES;
        PackageInfo archive;
        PackageInfo self;
        try {
            archive = pm.getPackageArchiveInfo(apk.getAbsolutePath(), flags);
            self = pm.getPackageInfo(ctx.getPackageName(), flags);
        } catch (Exception e) {
            throw new IOException("无法读取安装包信息", e);
        }
        if (archive == null) {
            throw new IOException("下载的文件不是有效 APK");
        }
        if (!ctx.getPackageName().equals(archive.packageName)) {
            throw new IOException("包名不符:期望 " + ctx.getPackageName()
                    + ",实际 " + archive.packageName);
        }
        long archiveCode = versionCode(archive);
        long selfCode = versionCode(self);
        if (archiveCode <= selfCode) {
            throw new IOException("不是更高版本(versionCode " + archiveCode
                    + " ≤ 当前 " + selfCode + ")");
        }
        if (!AppVersion.isNewer(archive.versionName, self.versionName)) {
            throw new IOException("versionName 未提升(" + archive.versionName
                    + " ≤ " + self.versionName + ")");
        }
        List<String> archiveSigners = signerDigests(archive);
        List<String> selfSigners = signerDigests(self);
        if (archiveSigners.isEmpty() || selfSigners.isEmpty()
                || !archiveSigners.equals(selfSigners)) {
            throw new IOException("签名证书与已装 APP 不一致,系统会拒绝覆盖安装");
        }
    }

    @SuppressWarnings("deprecation")
    private static long versionCode(PackageInfo info) {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.P
                ? info.getLongVersionCode()
                : info.versionCode;
    }

    @SuppressWarnings("deprecation")
    private static Signature[] signersOf(PackageInfo info) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P && info.signingInfo != null) {
            return info.signingInfo.hasMultipleSigners()
                    ? info.signingInfo.getApkContentsSigners()
                    : info.signingInfo.getSigningCertificateHistory();
        }
        return info.signatures;
    }

    private static List<String> signerDigests(PackageInfo info) {
        Signature[] sigs = signersOf(info);
        List<String> out = new ArrayList<>();
        if (sigs == null) {
            return out;
        }
        for (Signature sig : sigs) {
            try {
                MessageDigest md = MessageDigest.getInstance("SHA-256");
                out.add(hex(md.digest(sig.toByteArray())));
            } catch (Exception ignored) {
                // 拿不到摘要就当没有签名,交给调用方拒绝安装。
            }
        }
        Collections.sort(out);
        return out;
    }

    private static String hex(byte[] bytes) {
        StringBuilder sb = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) {
            sb.append(Character.forDigit((b >> 4) & 0xf, 16));
            sb.append(Character.forDigit(b & 0xf, 16));
        }
        return sb.toString();
    }

    private static String megabytes(long bytes) {
        return String.format(Locale.US, "%.2f MB", bytes / 1048576.0);
    }

    private static String describe(Exception e) {
        String msg = e.getMessage();
        if (e instanceof java.net.UnknownHostException) {
            msg = "域名解析失败,请检查网络";
        } else if (e instanceof java.net.SocketTimeoutException) {
            msg = "连接超时,请稍后重试";
        }
        if (msg == null || msg.isEmpty()) {
            msg = e.getClass().getSimpleName();
        }
        return msg;
    }
}
