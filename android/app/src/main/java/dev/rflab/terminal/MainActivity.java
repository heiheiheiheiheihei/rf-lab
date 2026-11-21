package dev.rflab.terminal;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.View;
import android.view.WindowInsets;
import android.webkit.JavascriptInterface;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.widget.FrameLayout;
import android.widget.Toast;

import androidx.webkit.WebViewAssetLoader;
import androidx.webkit.WebViewClientCompat;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.util.List;
import java.util.Locale;

/**
 * 单 Activity + WebView:同一份 index.html,通过 RFBridge 走 BLE。
 * UI 完全复用 Web 版;原生只负责权限、扫描、GATT 和 NDJSON 转发。
 *
 * <p>另外负责 APP 自更新的用户界面:启动/回前台时后台查一次 GitHub Releases
 * (6 小时节流),有新版本弹「一键更新 / 忽略此版本 / 稍后」。下载与校验在
 * {@link Updater};安装交给系统安装器,由用户在系统弹窗里确认。
 */
public class MainActivity extends Activity {
    private static final int REQ_PERMS = 41;

    /** 自更新本地状态:上次检查时间、已忽略的版本。 */
    private static final String PREFS_UPDATE = "rflab_update";
    private static final String PREF_LAST_CHECK = "last_check_ms";
    private static final String PREF_IGNORED = "ignored_version";
    private static final long CHECK_INTERVAL_MS = 6L * 60 * 60 * 1000;
    /** 检查失败(没网/限流)不在本地记「已检查」,只在本次进程内退避 1 小时再试。 */
    private static final long RETRY_AFTER_FAILURE_MS = 60L * 60 * 1000;

    private WebView webView;
    private BleClient ble;
    private Runnable pendingPermissionAction;

    private final Updater updater = new Updater();
    private boolean checkingUpdate;
    private long lastCheckAttemptMs;
    private Updater.Release pendingRelease;
    private boolean awaitingInstallPermission;
    private AlertDialog progressDialog;

    /**
     * 安装器结果广播。系统通过我们创建的 PendingIntent 投递,身份仍是本 APP,
     * 所以 Android 13+ 用 {@code RECEIVER_NOT_EXPORTED} 注册即可。
     */
    private final BroadcastReceiver installReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS,
                    PackageInstaller.STATUS_FAILURE);
            String message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                Intent confirm = confirmIntent(intent);
                if (confirm != null) {
                    startActivity(confirm);
                } else {
                    showError("无法启动安装器", "系统没有返回安装确认页");
                }
            } else if (status == PackageInstaller.STATUS_SUCCESS) {
                toast("更新已安装,APP 会重新启动");
            } else {
                showError("安装失败", message == null ? ("status " + status) : message);
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().setStatusBarColor(Color.rgb(7, 9, 6));
        getWindow().setNavigationBarColor(Color.rgb(7, 9, 6));
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
        }

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(7, 9, 6));
        webView = new WebView(this);
        webView.setBackgroundColor(Color.rgb(7, 9, 6));
        webView.setOverScrollMode(View.OVER_SCROLL_NEVER);
        root.addView(webView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            root.setOnApplyWindowInsetsListener((v, insets) -> {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
                v.setPadding(0, bars.top, 0, bars.bottom);
                return insets;
            });
        }
        setContentView(root);

        ble = new BleClient(this, new BleClient.Listener() {
            @Override
            public void onScan(List<BleClient.Device> devices) {
                try {
                    JSONArray arr = new JSONArray();
                    for (BleClient.Device d : devices) {
                        arr.put(new JSONObject()
                                .put("name", d.name == null ? "" : d.name)
                                .put("address", d.address)
                                .put("rssi", d.rssi));
                    }
                    emit(new JSONObject().put("op", "scan").put("devices", arr));
                } catch (JSONException ignored) {
                }
            }

            @Override
            public void onConnected(String name, String address) {
                try {
                    emit(new JSONObject().put("op", "ble").put("state", "connected")
                            .put("name", name == null ? "" : name).put("address", address));
                } catch (JSONException ignored) {
                }
            }

            @Override
            public void onDisconnected() {
                try {
                    emit(new JSONObject().put("op", "ble").put("state", "disconnected"));
                } catch (JSONException ignored) {
                }
            }

            @Override
            public void onLine(String line) {
                try {
                    emit(new JSONObject().put("op", "line").put("line", line));
                } catch (JSONException ignored) {
                }
            }

            @Override
            public void onError(String message) {
                emitError(message);
            }
        });

        WebSettings s = webView.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(false);
        s.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        s.setCacheMode(WebSettings.LOAD_DEFAULT);
        s.setMediaPlaybackRequiresUserGesture(true);

        final WebViewAssetLoader loader = new WebViewAssetLoader.Builder()
                .addPathHandler("/assets/", new WebViewAssetLoader.AssetsPathHandler(this))
                .build();

        webView.setWebViewClient(new WebViewClientCompat() {
            @Override
            public android.webkit.WebResourceResponse shouldInterceptRequest(
                    WebView view, android.webkit.WebResourceRequest request) {
                return loader.shouldInterceptRequest(request.getUrl());
            }
        });

        webView.addJavascriptInterface(new Bridge(), "RFBridge");
        webView.loadUrl("https://appassets.androidplatform.net/assets/index.html");

        IntentFilter filter = new IntentFilter(Updater.ACTION_INSTALL_RESULT);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(installReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(installReceiver, filter);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (awaitingInstallPermission) {
            // 从「安装未知应用」设置页回来:允许了就接着下载,否则等用户下次点更新。
            awaitingInstallPermission = false;
            Updater.Release release = pendingRelease;
            pendingRelease = null;
            if (release != null && getPackageManager().canRequestPackageInstalls()) {
                downloadAndInstall(release);
            }
            return;
        }
        maybeCheckForUpdate();
    }

    // ---- 自更新 UI ------------------------------------------------------------

    /** 节流后的后台检查;有新版本且未被忽略时才弹窗。 */
    private void maybeCheckForUpdate() {
        if (checkingUpdate || isFinishing()) {
            return;
        }
        final SharedPreferences prefs = getSharedPreferences(PREFS_UPDATE, MODE_PRIVATE);
        long now = System.currentTimeMillis();
        if (now - prefs.getLong(PREF_LAST_CHECK, 0L) < CHECK_INTERVAL_MS) {
            return;
        }
        if (now - lastCheckAttemptMs < RETRY_AFTER_FAILURE_MS) {
            return;
        }
        lastCheckAttemptMs = now;
        checkingUpdate = true;
        updater.check(Updater.currentVersion(this), (release, error) -> {
            checkingUpdate = false;
            if (error == null) {
                // 只有真正拿到结果才记账;失败下次(换网络后)立刻可重试。
                prefs.edit().putLong(PREF_LAST_CHECK, System.currentTimeMillis()).apply();
            }
            if (error != null) {
                // 自动检查失败不打扰用户:没网/被限流时静默,下次回前台再试。
                return;
            }
            if (release == null || release.version.equals(prefs.getString(PREF_IGNORED, ""))) {
                return;
            }
            showUpdateDialog(release);
        });
    }

    private void showUpdateDialog(final Updater.Release release) {
        if (isFinishing()) {
            return;
        }
        new AlertDialog.Builder(this)
                .setTitle("发现新版本 " + release.version)
                .setMessage("当前 " + Updater.currentVersion(this) + "\n\n"
                        + "从 GitHub Releases 下载,校验 SHA-256 与签名后交给系统安装器;"
                        + "安装确认由系统弹出,APP 不会静默安装。\n\n"
                        + release.summary())
                .setPositiveButton("一键更新", (d, w) -> requestInstall(release))
                .setNeutralButton("忽略此版本", (d, w) -> {
                    getSharedPreferences(PREFS_UPDATE, MODE_PRIVATE).edit()
                            .putString(PREF_IGNORED, release.version).apply();
                    toast("已忽略 " + release.version + ",下一版会重新提示");
                })
                .setNegativeButton("稍后", null)
                .show();
    }

    /** 侧载更新需要用户允许「安装未知应用」;不允许时先去设置,回来自动续上。 */
    private void requestInstall(Updater.Release release) {
        if (!getPackageManager().canRequestPackageInstalls()) {
            pendingRelease = release;
            new AlertDialog.Builder(this)
                    .setTitle("需要「安装未知应用」权限")
                    .setMessage("系统默认不允许侧载更新。请在设置里允许 RF-Lab 安装应用,"
                            + "返回后会自动继续下载。")
                    .setPositiveButton("去设置", (d, w) -> {
                        awaitingInstallPermission = true;
                        try {
                            startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                                    Uri.parse("package:" + getPackageName())));
                        } catch (Exception e) {
                            awaitingInstallPermission = false;
                            showError("无法打开设置", "请手动允许 RF-Lab 安装未知应用后重试");
                        }
                    })
                    .setNegativeButton("取消", (d, w) -> pendingRelease = null)
                    .show();
            return;
        }
        downloadAndInstall(release);
    }

    private void downloadAndInstall(final Updater.Release release) {
        final AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("正在下载 " + release.version)
                .setMessage(progressText(0, release.apkSize))
                .setCancelable(false)
                .create();
        progressDialog = dialog;
        dialog.show();
        updater.download(this, release, (done, total) -> {
            if (dialog.isShowing()) {
                dialog.setMessage(progressText(done, total));
            }
        }, new Updater.DownloadCallback() {
            @Override
            public void onReady(File apk) {
                dismiss(dialog);
                updater.install(MainActivity.this, apk, new Updater.InstallCallback() {
                    @Override
                    public void onCommitted() {
                        toast("请在系统弹窗确认安装");
                    }

                    @Override
                    public void onFailed(String message) {
                        showError("无法启动安装器", message);
                    }
                });
            }

            @Override
            public void onFailed(String message) {
                dismiss(dialog);
                showError("更新失败", message);
            }
        });
    }

    private String progressText(long done, long total) {
        long safeTotal = Math.max(total, 1);
        int pct = (int) Math.min(100, done * 100 / safeTotal);
        return String.format(Locale.US, "%d%% · %.2f / %.2f MB\n\n校验通过后会自动交给系统安装。",
                pct, done / 1048576.0, total / 1048576.0);
    }

    private void showError(String title, String message) {
        if (isFinishing()) {
            return;
        }
        new AlertDialog.Builder(this)
                .setTitle(title)
                .setMessage(message == null ? "未知错误" : message)
                .setPositiveButton("知道了", null)
                .show();
    }

    private void dismiss(AlertDialog dialog) {
        if (dialog != null && dialog.isShowing()) {
            dialog.dismiss();     // 销毁时也要关,否则 WindowLeaked
        }
        if (dialog == progressDialog) {
            progressDialog = null;
        }
    }

    private void toast(String text) {
        Toast.makeText(this, text, Toast.LENGTH_LONG).show();
    }

    @SuppressWarnings("deprecation")
    private Intent confirmIntent(Intent intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent.class);
        }
        return intent.getParcelableExtra(Intent.EXTRA_INTENT);
    }

    private void emit(JSONObject obj) {
        final String js = "window.__rflabNative(" + JSONObject.quote(obj.toString()) + ")";
        runOnUiThread(() -> {
            if (webView != null) {
                webView.evaluateJavascript(js, null);
            }
        });
    }

    private void emitError(String message) {
        try {
            emit(new JSONObject().put("op", "error").put("message", message == null ? "error" : message));
        } catch (JSONException ignored) {
        }
    }

    public final class Bridge {
        @JavascriptInterface
        public void postMessage(String json) {
            runOnUiThread(() -> handleMessage(json));
        }
    }

    private void handleMessage(String json) {
            try {
                JSONObject o = new JSONObject(json);
                String op = o.optString("op");
                switch (op) {
                    case "scan":
                        withPermissions(() -> ble.scan());
                        break;
                    case "connect": {
                        final String address = o.optString("address");
                        withPermissions(() -> ble.connect(address));
                        break;
                    }
                    case "disconnect":
                        ble.disconnect();
                        break;
                    case "send": {
                        final String line = o.optString("line");
                        if (!ble.isConnected()) {
                            emitError("BLE not connected");
                        } else {
                            ble.send(line);
                        }
                        break;
                    }
                    default:
                        break;
                }
            } catch (JSONException e) {
                emitError("bad bridge message");
            }
    }

    private boolean hasPermissions() {
        for (String p : BleClient.requiredPermissions()) {
            if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) {
                return false;
            }
        }
        return true;
    }

    private void withPermissions(Runnable action) {
        if (hasPermissions()) {
            action.run();
            return;
        }
        pendingPermissionAction = action;
        requestPermissions(BleClient.requiredPermissions(), REQ_PERMS);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQ_PERMS) {
            return;
        }
        boolean all = grantResults.length > 0;
        for (int r : grantResults) {
            if (r != PackageManager.PERMISSION_GRANTED) {
                all = false;
            }
        }
        Runnable action = pendingPermissionAction;
        pendingPermissionAction = null;
        if (all && action != null) {
            action.run();
        } else {
            emitError("BLE permission denied");
            Toast.makeText(this, "需要蓝牙权限才能扫描/连接 RF-Lab", Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onDestroy() {
        dismiss(progressDialog);
        try {
            unregisterReceiver(installReceiver);
        } catch (IllegalArgumentException ignored) {
            // 没注册成功(极端初始化失败)时忽略。
        }
        if (ble != null) {
            ble.close();
        }
        if (webView != null) {
            webView.removeJavascriptInterface("RFBridge");
            webView.destroy();
            webView = null;
        }
        super.onDestroy();
    }
}
