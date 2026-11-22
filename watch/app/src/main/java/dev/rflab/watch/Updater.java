package dev.rflab.watch;

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
 * Watch self-update: GitHub Release → APK → verify → system installer.
 *
 * <p>Boundaries are deliberate:
 * <ul>
 *   <li>HTTPS only, repo and asset names are constants; the feed can never point
 *       the app at a different download host than GitHub;</li>
 *   <li>only stable {@code watch-vX.Y.Z} releases carrying both the APK and
 *       {@code SHA256SUMS.txt} are candidates;</li>
 *   <li>before the installer runs: asset size, SHA-256, package name,
 *       versionCode/versionName must advance, and the signing certificate must
 *       equal the installed app's certificate;</li>
 *   <li>installation goes through {@link PackageInstaller} — the system shows
 *       the confirmation, nothing is silent and no root is needed.</li>
 * </ul>
 */
final class Updater {
    private static final String TAG = "RF-Lab.Watch.Update";
    private static final String FEED =
            "https://api.github.com/repos/heiheiheiheiheihei/rf-lab/releases?per_page=30";
    private static final int MAX_TEXT_BYTES = 2 * 1024 * 1024;

    static final String ACTION_INSTALL_RESULT = "dev.rflab.watch.INSTALL_RESULT";

    private static final ExecutorService IO = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "rflab-watch-update");
        t.setDaemon(true);
        return t;
    });
    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    interface CheckCallback {
        /** {@code release != null} = newer build; both null = up to date. Main thread. */
        void onChecked(UpdateFeed.Release release, String error);
    }

    interface Progress {
        void onProgress(long done, long total);
    }

    interface DownloadCallback {
        void onReady(File apk);

        void onFailed(String error);
    }

    interface InstallCallback {
        void onCommitted();

        void onFailed(String error);
    }

    private interface ProgressSink {
        void onProgress(long done);
    }

    void check(final String currentVersion, final CheckCallback cb) {
        IO.execute(() -> {
            UpdateFeed.Release found = null;
            String error = null;
            try {
                String body = fetchText(FEED);
                found = UpdateFeed.latest(body, currentVersion);
            } catch (Exception e) {
                error = shortError(e);
                Log.w(TAG, "update check failed", e);
            }
            final UpdateFeed.Release release = found;
            final String err = error;
            MAIN.post(() -> cb.onChecked(release, err));
        });
    }

    void download(final Context context, final UpdateFeed.Release release,
                  final Progress progress, final DownloadCallback cb) {
        final Context app = context.getApplicationContext();
        IO.execute(() -> {
            File apk = null;
            String error = null;
            try {
                File dir = new File(app.getCacheDir(), "update");
                if (!dir.isDirectory() && !dir.mkdirs()) {
                    throw new IOException("cannot create " + dir);
                }
                File part = new File(dir, UpdateFeed.APK_ASSET + ".part");
                long got = downloadTo(release.apkUrl, part,
                        done -> MAIN.post(() -> progress.onProgress(done, release.size)));
                if (got != release.size) {
                    throw new IOException("size " + got + " != " + release.size);
                }
                String expected = UpdateFeed.hashFor(fetchAssetText(release.sumsUrl),
                        UpdateFeed.APK_ASSET);
                if (expected == null) {
                    throw new IOException("SHA256SUMS has no " + UpdateFeed.APK_ASSET);
                }
                if (!expected.equalsIgnoreCase(sha256(part))) {
                    throw new IOException("sha256 mismatch");
                }
                verifyApk(app, part);
                apk = new File(dir, "rf-lab-watch-" + release.version + ".apk");
                if (apk.exists() && !apk.delete()) {
                    throw new IOException("cannot replace " + apk.getName());
                }
                if (!part.renameTo(apk)) {
                    throw new IOException("cannot store " + apk.getName());
                }
            } catch (Exception e) {
                error = shortError(e);
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

    /** Writes the verified APK into a PackageInstaller session; the system asks the user. */
    void install(final Activity activity, final File apk, final InstallCallback cb) {
        IO.execute(() -> {
            String error = null;
            try {
                PackageInstaller installer = activity.getPackageManager().getPackageInstaller();
                PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(
                        PackageInstaller.SessionParams.MODE_FULL_INSTALL);
                params.setAppPackageName(activity.getPackageName());
                params.setSize(apk.length());
                int sessionId = installer.createSession(params);

                Intent status = new Intent(ACTION_INSTALL_RESULT)
                        .setPackage(activity.getPackageName());
                int flags = PendingIntent.FLAG_UPDATE_CURRENT;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    flags |= PendingIntent.FLAG_MUTABLE;
                }
                PendingIntent pending = PendingIntent.getBroadcast(activity, sessionId,
                        status, flags);

                try (PackageInstaller.Session session = installer.openSession(sessionId)) {
                    try (InputStream in = new FileInputStream(apk);
                         OutputStream out = session.openWrite("base.apk", 0, apk.length())) {
                        copy(in, out);
                        session.fsync(out);
                    }
                    session.commit(pending.getIntentSender());
                }
            } catch (Exception e) {
                error = shortError(e);
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

    // ---- network ------------------------------------------------------------

    private static String fetchText(String url) throws IOException {
        return readText(open(url, false));
    }

    private static String fetchAssetText(String url) throws IOException {
        return readText(open(url, true));
    }

    private static String readText(HttpURLConnection c) throws IOException {
        try (InputStream in = c.getInputStream()) {
            return new String(readLimited(in, MAX_TEXT_BYTES), StandardCharsets.UTF_8);
        } finally {
            c.disconnect();
        }
    }

    private static long downloadTo(String url, File dest, ProgressSink sink) throws IOException {
        HttpURLConnection c = open(url, true);
        try (InputStream in = c.getInputStream();
             OutputStream out = new FileOutputStream(dest)) {
            byte[] buf = new byte[16 * 1024];
            long total = 0;
            long reported = 0;
            int n;
            while ((n = in.read(buf)) > 0) {
                total += n;
                if (total > UpdateFeed.MAX_APK_BYTES) {
                    throw new IOException("apk too large");
                }
                out.write(buf, 0, n);
                if (sink != null && total - reported >= 16 * 1024) {
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

    private static HttpURLConnection open(String url, boolean asset) throws IOException {
        URL u = new URL(url);
        if (!"https".equals(u.getProtocol())) {
            throw new IOException("https only");
        }
        HttpURLConnection c = (HttpURLConnection) u.openConnection();
        c.setConnectTimeout(8000);
        c.setReadTimeout(20000);
        c.setInstanceFollowRedirects(true);
        c.setRequestProperty("User-Agent", "RF-Lab-Watch");
        c.setRequestProperty("Accept", asset ? "application/octet-stream"
                : "application/vnd.github+json");
        int code = c.getResponseCode();
        if (code != 200) {
            c.disconnect();
            throw new IOException("HTTP " + code);
        }
        return c;
    }

    private static byte[] readLimited(InputStream in, int limit) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[8 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
            if (out.size() > limit) {
                throw new IOException("response too large");
            }
        }
        return out.toByteArray();
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[16 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) {
            out.write(buf, 0, n);
        }
        out.flush();
    }

    // ---- verification -------------------------------------------------------

    private static String sha256(File file) throws IOException {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            try (InputStream in = new FileInputStream(file)) {
                byte[] buf = new byte[32 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) {
                    md.update(buf, 0, n);
                }
            }
            return hex(md.digest());
        } catch (IOException e) {
            throw e;
        } catch (Exception e) {
            throw new IOException("sha256 unavailable", e);
        }
    }

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
            throw new IOException("cannot read package", e);
        }
        if (archive == null) {
            throw new IOException("not an apk");
        }
        if (!ctx.getPackageName().equals(archive.packageName)) {
            throw new IOException("package " + archive.packageName);
        }
        long archiveCode = versionCode(archive);
        long selfCode = versionCode(self);
        if (archiveCode <= selfCode) {
            throw new IOException("versionCode " + archiveCode + " <= " + selfCode);
        }
        if (!AppVersion.isNewer(archive.versionName, self.versionName)) {
            throw new IOException("versionName " + archive.versionName
                    + " <= " + self.versionName);
        }
        List<String> archiveSigners = signerDigests(archive);
        List<String> selfSigners = signerDigests(self);
        if (archiveSigners.isEmpty() || selfSigners.isEmpty()
                || !archiveSigners.equals(selfSigners)) {
            throw new IOException("signer mismatch");
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
                // No digest = no trust; the caller rejects the empty list.
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

    // ---- errors -------------------------------------------------------------

    /** One short token that fits the watch status line; detail goes to logcat. */
    private static String shortError(Exception e) {
        String msg = e.getMessage() == null ? "" : e.getMessage();
        if (e instanceof java.net.UnknownHostException
                || e instanceof java.net.ConnectException
                || e instanceof java.net.SocketTimeoutException
                || e instanceof java.net.NoRouteToHostException) {
            return "NO NET";
        }
        if (msg.startsWith("HTTP 403") || msg.startsWith("HTTP 429")) {
            return "RATE LIMIT";
        }
        if (msg.startsWith("HTTP ")) {
            return msg;
        }
        if (msg.contains("sha256")) {
            return "BAD HASH";
        }
        if (msg.contains("signer")) {
            return "BAD SIGN";
        }
        if (msg.contains("versionCode") || msg.contains("versionName")) {
            return "NOT NEWER";
        }
        if (msg.contains("not an apk") || msg.contains("package ")
                || msg.contains("cannot read package")) {
            return "BAD APK";
        }
        if (msg.startsWith("size ")) {
            return "BAD SIZE";
        }
        if (e instanceof SecurityException) {
            return "NO PERMISSION";
        }
        if (e instanceof java.io.IOException) {
            return "IO " + msg;
        }
        return String.format(Locale.US, "%s", e.getClass().getSimpleName());
    }
}
