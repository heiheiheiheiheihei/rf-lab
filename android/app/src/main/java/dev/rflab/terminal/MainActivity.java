package dev.rflab.terminal;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
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

import java.util.List;

/**
 * 单 Activity + WebView:同一份 index.html,通过 RFBridge 走 BLE。
 * UI 完全复用 Web 版;原生只负责权限、扫描、GATT 和 NDJSON 转发。
 */
public class MainActivity extends Activity {
    private static final int REQ_PERMS = 41;

    private WebView webView;
    private BleClient ble;
    private Runnable pendingPermissionAction;

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
