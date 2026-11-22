package dev.rflab.watch;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.location.LocationManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;

import org.json.JSONObject;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * RF-Lab watch client: one state owner for link, auth, ARM, code selection and TX.
 *
 * <p>Safety rules live here, not in the view: a HOLD becomes LONG only after the
 * view's 350ms gate; releasing sends STOP; leaving the foreground or losing the
 * link while TX is active sends STOP (or reconnects and sends STOP before
 * anything else).
 */
public final class WatchActivity extends Activity implements WatchView.Host {

    private static final int REQ_PERMS = 41;
    private static final long CONNECT_TIMEOUT_MS = 10_000L;
    private static final long HELLO_TIMEOUT_MS = 6_000L;
    private static final long FORGET_CONFIRM_MS = 4_000L;

    private final Handler main = new Handler(Looper.getMainLooper());
    private final List<Protocol.Code> codes = new ArrayList<>();
    private final List<BleClient.Device> devices = new ArrayList<>();

    private WatchView view;
    private BleClient ble;
    private Prefs prefs;
    private Updater updater;

    private int screen = WatchView.SCREEN_LINK;
    private boolean connected;
    private boolean authed;
    private boolean armed;
    private boolean scanning;
    private boolean authPending;
    private boolean menuOpen;
    private boolean listOpen;
    private boolean forgotPending;
    private boolean unsafeTx;
    private boolean holding;
    private boolean holdSent;
    private boolean txActive;
    private boolean pendingStopAfterReconnect;
    private boolean helloSeen;
    private boolean storedPinTried;
    private boolean permissionDeniedOnce;
    private boolean autoConnectTried;
    private boolean demo;
    private boolean destroyed;
    private boolean pinAborted;
    private boolean updateBusy;
    private String updatePhase = "";

    private int selectedId = -1;
    private String txKind = "";
    private int pendingSelectId = -1;
    private int pendingStopId;
    private int txId;
    private int holdId;
    private int connectGen;
    private int helloGen;

    private String deviceName = "";
    private String deviceAddress = "";
    private String fw = "";
    private String build = "";
    private String appVersion = "0.1.3";
    private String status = "READY";
    private int statusTone = WatchView.TONE_NEUTRAL;
    private String linkStatus = "IDLE";
    private int linkTone = WatchView.TONE_NEUTRAL;
    private String pin = "";
    private String pinError = "";

    private long lastEventAt;
    private long forgetAt;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        keepAwakeAndFullscreen();
        prefs = new Prefs(this);
        readAppVersion();
        updater = new Updater();

        view = new WatchView(this, this);
        setContentView(view);
        registerInstallReceiver();

        boolean debuggable = (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        demo = debuggable && getIntent() != null && getIntent().getBooleanExtra("demo", false);
        if (demo) {
            setupDemo(getIntent());
            view.invalidate();
            return;
        }

        ble = new BleClient(this, bleListener);
        if (!getPackageManager().hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)) {
            setStatus("NO BLE HARDWARE", WatchView.TONE_ERROR);
            linkStatus = "NO BLE";
            linkTone = WatchView.TONE_ERROR;
            return;
        }
        if (BleClient.hasPermissions(this)) {
            begin();
        } else {
            linkStatus = "BLE ACCESS NEEDED";
            linkTone = WatchView.TONE_WARN;
            requestBle(this::begin);
        }
    }

    private void keepAwakeAndFullscreen() {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setStatusBarColor(0xFF070906);
        getWindow().setNavigationBarColor(0xFF070906);
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    private void readAppVersion() {
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            if (pi.versionName != null && !pi.versionName.isEmpty()) {
                appVersion = pi.versionName;
            }
        } catch (PackageManager.NameNotFoundException ignored) {
        }
    }

    private void begin() {
        if (demo || ble == null) {
            return;
        }
        if (!ble.isEnabled()) {
            linkStatus = "BLUETOOTH OFF";
            linkTone = WatchView.TONE_WARN;
            setStatus("TURN ON BLUETOOTH", WatchView.TONE_WARN);
            return;
        }
        if (!locationReady()) {
            linkStatus = "LOCATION OFF · SCAN BLOCKED";
            linkTone = WatchView.TONE_WARN;
        }
        String saved = prefs.lastAddress();
        if (!saved.isEmpty() && !autoConnectTried) {
            autoConnectTried = true;
            deviceName = prefs.lastName();
            connectTo(saved);
            return;
        }
        if (screen == WatchView.SCREEN_LINK && !scanning && devices.isEmpty()) {
            if (!locationReady()) {
                setStatus("TURN ON LOCATION FOR BLE SCAN", WatchView.TONE_WARN);
                return;
            }
            startScan();
        }
    }

    // ---------------- BLE callbacks ----------------

    private final BleClient.Listener bleListener = new BleClient.Listener() {
        @Override
        public void onScan(List<BleClient.Device> list) {
            if (destroyed) {
                return;
            }
            devices.clear();
            if (list != null) {
                devices.addAll(list);
            }
            scanning = true;
            if (list != null && !list.isEmpty()) {
                linkStatus = list.size() + " FOUND · TAP TO CONNECT";
                linkTone = WatchView.TONE_OK;
            }
            view.invalidate();
        }

        @Override
        public void onScanStopped() {
            if (destroyed) {
                return;
            }
            scanning = false;
            if (screen == WatchView.SCREEN_LINK && devices.isEmpty()) {
                linkStatus = "SCAN DONE · NO DEVICE";
                linkTone = WatchView.TONE_NEUTRAL;
            }
            view.invalidate();
        }

        @Override
        public void onConnected(String name, String address) {
            if (destroyed) {
                return;
            }
            connectGen++;
            helloGen++;
            boolean lateHello = helloSeen;
            boolean lateStoredPin = storedPinTried;
            boolean lateAuthPending = authPending;
            connected = true;
            armed = false;
            // If hello was processed before this callback, do not discard the
            // auto-AUTH state it just started.
            storedPinTried = lateHello && lateStoredPin;
            authPending = lateHello && lateAuthPending;
            pinAborted = false;
            if (!lateHello) {
                authed = false;
                pin = "";
                pinError = "";
            }
            deviceAddress = address == null ? "" : address;
            if (name != null && !name.isEmpty()) {
                deviceName = name;
            }
            if (deviceName.isEmpty()) {
                deviceName = "RF-Lab";
            }
            prefs.rememberDevice(deviceAddress, deviceName);
            if (!lateHello) {
                screen = WatchView.SCREEN_PIN;
                status = "CONNECTED · WAIT HELLO";
                statusTone = WatchView.TONE_WARN;
                linkStatus = "CONNECTED · PAIRING";
                linkTone = WatchView.TONE_OK;
                final int gen = helloGen;
                main.postDelayed(() -> {
                    if (gen == helloGen && connected && !helloSeen) {
                        setStatus("NO HELLO FROM DEVICE", WatchView.TONE_ERROR);
                        ble.disconnect();
                    }
                }, HELLO_TIMEOUT_MS);
            }
            view.invalidate();
        }

        @Override
        public void onDisconnected() {
            if (destroyed) {
                return;
            }
            connectGen++;
            helloGen++;
            boolean wasTx = txActiveInternal();
            connected = false;
            authed = false;
            armed = false;
            authPending = false;
            scanning = false;
            menuOpen = false;
            listOpen = false;
            holding = false;
            holdSent = false;
            txActive = false;
            if (wasTx) {
                unsafeTx = true;
                pendingStopAfterReconnect = true;
                pendingStopId = txId > 0 ? txId : selectedId;
                screen = WatchView.SCREEN_LINK;
                status = "LINK LOST · TX MAY CONTINUE";
                statusTone = WatchView.TONE_ERROR;
                linkStatus = "RECONNECTING TO STOP…";
                linkTone = WatchView.TONE_ERROR;
                view.invalidate();
                final String addr = deviceAddress.isEmpty() ? prefs.lastAddress() : deviceAddress;
                if (!addr.isEmpty()) {
                    main.postDelayed(() -> {
                        if (!connected && screen == WatchView.SCREEN_LINK) {
                            connectTo(addr);
                        }
                    }, 800);
                }
            } else {
                unsafeTx = false;
                pendingStopAfterReconnect = false;
                screen = WatchView.SCREEN_LINK;
                status = "LINK LOST";
                statusTone = WatchView.TONE_WARN;
                if (!linkStatus.startsWith("PIN LOCKED")) {
                    linkStatus = "DISCONNECTED";
                    linkTone = WatchView.TONE_NEUTRAL;
                }
                view.invalidate();
            }
        }

        @Override
        public void onLine(String line) {
            if (destroyed) {
                return;
            }
            handleLine(line);
        }

        @Override
        public void onError(String message) {
            if (destroyed) {
                return;
            }
            if (message == null || message.isEmpty()) {
                return;
            }
            if (scanning && message.startsWith("BLE scan")) {
                scanning = false;
            }
            setStatus(message, WatchView.TONE_ERROR);
            if (screen == WatchView.SCREEN_LINK) {
                linkStatus = message;
                linkTone = WatchView.TONE_ERROR;
            }
            view.invalidate();
        }
    };

    private boolean txActiveInternal() {
        return txActive || holdSent || holding;
    }

    // ---------------- protocol ----------------

    private void handleLine(String line) {
        JSONObject o = Protocol.tryParse(line);
        if (o == null) {
            if (line.startsWith("OK") || line.startsWith("WARN")
                    || line.startsWith("ERR") || line.startsWith("LEARN")) {
                setStatus(line, line.startsWith("OK") ? WatchView.TONE_OK
                        : line.startsWith("ERR") ? WatchView.TONE_ERROR
                        : WatchView.TONE_WARN);
            }
            return;
        }
        String t = o.optString("t", "");
        try {
            switch (t) {
                case "hello":
                    handleHello(Protocol.parseHello(o));
                    break;
                case "auth":
                    handleAuth(Protocol.parseAuth(o));
                    break;
                case "codes":
                    handleCodes(Protocol.parseCodes(o));
                    break;
                case "state":
                    handleState(Protocol.parseState(o));
                    break;
                case "event":
                    handleEvent(Protocol.parseEvent(o));
                    break;
                case "log":
                    handleLog(Protocol.parseLog(o));
                    break;
                case "error":
                    handleError(Protocol.parseError(o));
                    break;
                default:
                    break;
            }
        } catch (RuntimeException e) {
            setStatus("BAD FRAME", WatchView.TONE_ERROR);
        }
    }

    private void handleHello(Protocol.Hello hello) {
        if (hello == null) {
            return;
        }
        helloSeen = true;
        pinAborted = false;
        helloGen++;
        fw = hello.fw;
        build = hello.build;
        if (!hello.name.isEmpty()) {
            deviceName = hello.name;
        }
        prefs.rememberDevice(deviceAddress, deviceName);
        if (hello.auth) {
            authed = true;
            screen = WatchView.SCREEN_CONTROL;
            status = "LINKED";
            statusTone = WatchView.TONE_OK;
            afterAuth();
        } else {
            authed = false;
            screen = WatchView.SCREEN_PIN;
            String saved = prefs.pinFor(deviceAddress);
            if (Protocol.isValidPin(saved) && !storedPinTried) {
                storedPinTried = true;
                pin = saved;
                authPending = true;
                sendCmd("AUTH " + saved);
                status = "AUTH…";
                statusTone = WatchView.TONE_WARN;
            } else {
                pin = "";
                pinError = "";
                status = "ENTER 4-DIGIT PIN";
                statusTone = WatchView.TONE_WARN;
            }
        }
        view.invalidate();
    }

    private void handleAuth(Protocol.Auth auth) {
        if (auth == null) {
            return;
        }
        if (pinAborted) {
            pinAborted = false;
            authPending = false;
            return;
        }
        authPending = false;
        if (auth.ok) {
            authed = true;
            pinError = "";
            prefs.rememberPin(deviceAddress, pin);
            screen = WatchView.SCREEN_CONTROL;
            status = "AUTH OK";
            statusTone = WatchView.TONE_OK;
            afterAuth();
        } else {
            if (storedPinTried) {
                prefs.forgetPin(deviceAddress);
                storedPinTried = false;
            }
            pin = "";
            if (auth.left > 0) {
                pinError = "WRONG PIN · " + auth.left + " LEFT";
                status = pinError;
                statusTone = WatchView.TONE_ERROR;
                screen = WatchView.SCREEN_PIN;
            } else {
                pinError = "LOCKED · RECONNECT";
                status = "5 WRONG PINS · WAIT 10s";
                statusTone = WatchView.TONE_ERROR;
                screen = WatchView.SCREEN_LINK;
                linkStatus = "PIN LOCKED · WAIT 10s";
                linkTone = WatchView.TONE_ERROR;
            }
        }
        view.invalidate();
    }

    private void afterAuth() {
        armed = false;
        if (pendingStopAfterReconnect) {
            int id = pendingStopId > 0 ? pendingStopId : selectedId;
            pendingStopAfterReconnect = false;
            unsafeTx = false;
            if (id > 0) {
                sendCmd("STOP " + id);
                status = "STOP SENT · LINK RESTORED";
                statusTone = WatchView.TONE_OK;
            } else {
                status = "LINK RESTORED";
                statusTone = WatchView.TONE_OK;
            }
        }
    }

    private void handleCodes(List<Protocol.Code> parsed) {
        int keep = selectedId;
        codes.clear();
        codes.addAll(parsed);
        if (pendingSelectId > 0 && Protocol.indexOfId(codes, pendingSelectId) >= 0) {
            selectedId = pendingSelectId;
        } else if (Protocol.indexOfId(codes, keep) < 0) {
            selectedId = codes.isEmpty() ? -1 : codes.get(0).id;
        }
        pendingSelectId = -1;
        if (selectedId < 0 && !codes.isEmpty()) {
            selectedId = codes.get(0).id;
        }
        prefs.setLastSelectedId(selectedId);
        if (listOpen && codes.isEmpty()) {
            listOpen = false;
        }
        status = codes.size() + " CODES";
        statusTone = WatchView.TONE_NEUTRAL;
        view.invalidate();
    }

    private void handleState(Protocol.State st) {
        if (st == null) {
            return;
        }
        boolean was = txActive;
        txActive = st.active;
        if (!st.kind.isEmpty()) {
            txKind = st.kind;
        }
        if (st.id > 0) {
            txId = st.id;
        }
        long now = SystemClock.uptimeMillis();
        if (txActive && !was && !holding) {
            status = ("long".equals(st.kind) ? "LONG #" + st.id : "TX #" + st.id)
                    + " · TAP STOP";
            statusTone = WatchView.TONE_WARN;
            lastEventAt = now;
        } else if (!txActive && was && !holding && now - lastEventAt > 1200) {
            status = "READY";
            statusTone = WatchView.TONE_NEUTRAL;
        }
        view.invalidate();
    }

    private void handleEvent(Protocol.Event e) {
        if (e == null) {
            return;
        }
        lastEventAt = SystemClock.uptimeMillis();
        if ("tx".equals(e.kind)) {
            txId = e.id > 0 ? e.id : txId;
            switch (e.tx) {
                case "send":
                    txActive = false;
                    status = e.ok ? "OK TX #" + e.id + " · " + e.ms + "ms"
                            : "WARN NO F0 · #" + e.id;
                    statusTone = e.ok ? WatchView.TONE_OK : WatchView.TONE_ERROR;
                    break;
                case "long":
                    txActive = true;
                    status = "LONG #" + e.id + " · TAP STOP";
                    statusTone = WatchView.TONE_WARN;
                    break;
                case "stop":
                    txActive = false;
                    holdSent = false;
                    holding = false;
                    status = "STOP #" + e.id;
                    statusTone = WatchView.TONE_OK;
                    break;
                default:
                    if (!e.line.isEmpty()) {
                        status = e.line;
                        statusTone = WatchView.TONE_NEUTRAL;
                    }
                    break;
            }
        } else if ("learn".equals(e.kind)) {
            pendingSelectId = e.id;
            status = "LEARNED #" + e.id;
            statusTone = WatchView.TONE_OK;
        } else if (!e.line.isEmpty()) {
            status = e.line;
            statusTone = WatchView.TONE_NEUTRAL;
        }
        view.invalidate();
    }

    private void handleLog(Protocol.Log log) {
        if (log == null || log.line.isEmpty()) {
            return;
        }
        String line = condenseLog(log.line);
        if (line == null) {
            return;
        }
        lastEventAt = SystemClock.uptimeMillis();
        int tone = line.startsWith("OK") ? WatchView.TONE_OK
                : line.startsWith("ERR") ? WatchView.TONE_ERROR
                : line.startsWith("WARN") ? WatchView.TONE_WARN
                : WatchView.TONE_NEUTRAL;
        setStatus(line, tone);
    }

    /**
     * Firmware BLE logs include both the raw ACK and the full serial line. Keep
     * the watch status readable instead of showing the entire 315M/code text.
     */
    private String condenseLog(String raw) {
        String line = raw;
        int ack = line.indexOf("ACK ");
        if (ack >= 0 && !line.substring(ack).startsWith("OK ")) {
            // "ACK 0xF0 TX_OK" without a following command result is noise.
            int ok = line.indexOf("OK ", ack);
            if (ok < 0) {
                return null;
            }
            line = line.substring(ok);
        }
        for (String kind : new String[]{"send", "long", "stop"}) {
            String marker = "OK " + kind + " id=";
            int at = line.indexOf(marker);
            if (at < 0) {
                continue;
            }
            int id = numberAt(line, at + marker.length());
            String label = "send".equals(kind) ? "OK TX" : kind.toUpperCase(java.util.Locale.US);
            int ms = millisAt(line);
            return label + " #" + (id > 0 ? id : "?") + (ms > 0 ? " · " + ms + "ms" : "");
        }
        return line;
    }

    private int numberAt(String s, int from) {
        int i = from;
        while (i < s.length() && !Character.isDigit(s.charAt(i))) {
            i++;
        }
        int start = i;
        while (i < s.length() && Character.isDigit(s.charAt(i))) {
            i++;
        }
        if (i == start) {
            return -1;
        }
        try {
            return Integer.parseInt(s.substring(start, i));
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    private int millisAt(String s) {
        int end = s.lastIndexOf("ms)");
        if (end < 0) {
            return -1;
        }
        int comma = s.lastIndexOf(',', end);
        if (comma < 0) {
            return -1;
        }
        int start = comma + 1;
        while (start < end && !Character.isDigit(s.charAt(start))) {
            start++;
        }
        int i = start;
        while (i < end && Character.isDigit(s.charAt(i))) {
            i++;
        }
        if (i == start) {
            return -1;
        }
        try {
            return Integer.parseInt(s.substring(start, i));
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    private void handleError(String message) {
        if (message == null || message.isEmpty()) {
            return;
        }
        lastEventAt = SystemClock.uptimeMillis();
        setStatus(message, WatchView.TONE_ERROR);
    }

    // ---------------- user actions ----------------

    @Override
    public void onArm() {
        if (demo) {
            armed = !armed;
            setStatus(armed ? "DEMO ARMED" : "DEMO SAFE", armed ? WatchView.TONE_WARN
                    : WatchView.TONE_NEUTRAL);
            return;
        }
        if (!connected || !authed) {
            setStatus("NOT LINKED", WatchView.TONE_WARN);
            return;
        }
        if (txActiveInternal()) {
            setStatus("STOP LONG FIRST", WatchView.TONE_WARN);
            return;
        }
        armed = !armed;
        setStatus(armed ? "ARMED" : "SAFE", armed ? WatchView.TONE_WARN
                : WatchView.TONE_NEUTRAL);
    }

    @Override
    public void onMenu() {
        if (txActiveInternal() && !demo) {
            setStatus("STOP LONG BEFORE MENU", WatchView.TONE_WARN);
            return;
        }
        menuOpen = true;
        listOpen = false;
        forgotPending = false;
        view.invalidate();
    }

    @Override
    public void onSend() {
        if (demo) {
            if (!armed) {
                setStatus("TAP SAFE → ARMED", WatchView.TONE_WARN);
                return;
            }
            Protocol.Code code = selectedCode();
            if (code != null) {
                setStatus("DEMO TX #" + code.id + " · 852ms", WatchView.TONE_OK);
            }
            return;
        }
        if (!connected || !authed) {
            setStatus("NOT LINKED", WatchView.TONE_WARN);
            return;
        }
        if (!armed) {
            setStatus("TAP SAFE → ARMED", WatchView.TONE_WARN);
            return;
        }
        if (txActiveInternal()) {
            setStatus("STOP LONG FIRST", WatchView.TONE_WARN);
            return;
        }
        Protocol.Code code = selectedCode();
        if (code == null) {
            setStatus("NO CODE", WatchView.TONE_WARN);
            return;
        }
        txId = code.id;
        sendCmd("SEND " + code.id);
        setStatus("TX #" + code.id + "…", WatchView.TONE_WARN);
    }

    @Override
    public void onHoldStart() {
        if (demo) {
            Protocol.Code code = selectedCode();
            if (code != null && armed) {
                txId = code.id;
                txActive = true;
                setStatus("DEMO LONG #" + code.id, WatchView.TONE_WARN);
            } else if (!armed) {
                setStatus("TAP SAFE → ARMED", WatchView.TONE_WARN);
            }
            return;
        }
        if (!connected || !authed) {
            setStatus("NOT LINKED", WatchView.TONE_WARN);
            return;
        }
        if (!armed) {
            setStatus("TAP SAFE → ARMED", WatchView.TONE_WARN);
            return;
        }
        if (txActive) {
            setStatus("STOP LONG FIRST", WatchView.TONE_WARN);
            return;
        }
        Protocol.Code code = selectedCode();
        if (code == null) {
            setStatus("NO CODE", WatchView.TONE_WARN);
            return;
        }
        holding = true;
        holdSent = true;
        holdId = code.id;
        txId = code.id;
        sendCmd("LONG " + code.id);
        setStatus("HOLDING #" + code.id + " · RELEASE TO STOP", WatchView.TONE_WARN);
    }

    @Override
    public void onHoldRelease() {
        if (demo) {
            if (holdSent || txActive) {
                holdSent = false;
                holding = false;
                txActive = false;
                setStatus("DEMO STOP #" + txId, WatchView.TONE_OK);
            }
            return;
        }
        if (!holdSent) {
            return;
        }
        int id = holdId;
        holdSent = false;
        holding = false;
        sendCmd("STOP " + id);
        // Keep txActive until the device's stop event/state arrives. If the
        // write fails or the link drops first, onDisconnected raises unsafeTx.
        txActive = true;
        setStatus("STOP #" + id + "…", WatchView.TONE_WARN);
    }

    @Override
    public void onStopTx() {
        if (demo) {
            txActive = false;
            holdSent = false;
            holding = false;
            setStatus("DEMO STOP #" + txId, WatchView.TONE_OK);
            return;
        }
        int id = txId > 0 ? txId : selectedId;
        if (!connected || id <= 0) {
            setStatus("NOTHING TO STOP", WatchView.TONE_WARN);
            return;
        }
        sendCmd("STOP " + id);
        holdSent = false;
        holding = false;
        txActive = true;
        setStatus("STOP #" + id + "…", WatchView.TONE_WARN);
    }

    @Override
    public void onSelect(int delta) {
        if (codes.isEmpty()) {
            return;
        }
        int idx = selectedIndex();
        int next = Protocol.step(idx, delta, codes.size());
        if (next >= 0) {
            selectedId = codes.get(next).id;
            prefs.setLastSelectedId(selectedId);
            setStatus("CODE #" + selectedId, WatchView.TONE_NEUTRAL);
        }
    }

    @Override
    public void onOpenList() {
        if (codes.isEmpty()) {
            setStatus("NO CODES", WatchView.TONE_WARN);
            return;
        }
        listOpen = true;
        menuOpen = false;
        view.invalidate();
    }

    @Override
    public void onCloseList() {
        listOpen = false;
        view.invalidate();
    }

    @Override
    public void onPick(int index) {
        if (index < 0 || index >= codes.size()) {
            return;
        }
        selectedId = codes.get(index).id;
        prefs.setLastSelectedId(selectedId);
        listOpen = false;
        setStatus("CODE #" + selectedId, WatchView.TONE_NEUTRAL);
    }

    @Override
    public void onScan() {
        if (demo) {
            scanning = !scanning;
            devices.clear();
            if (!scanning) {
                devices.add(new BleClient.Device("RF-Lab-AB12", "AA:BB:CC:DD:EE:01", -57));
                devices.add(new BleClient.Device("RF-Lab-CD34", "AA:BB:CC:DD:EE:02", -71));
                linkStatus = "2 FOUND · TAP TO CONNECT";
                linkTone = WatchView.TONE_OK;
            } else {
                linkStatus = "SCANNING…";
                linkTone = WatchView.TONE_WARN;
            }
            view.invalidate();
            return;
        }
        if (scanning) {
            if (ble != null) {
                ble.stopScan();
            }
            scanning = false;
            linkStatus = "SCAN STOPPED";
            linkTone = WatchView.TONE_NEUTRAL;
            view.invalidate();
            return;
        }
        if (!canScan()) {
            requestBle(this::begin);
            return;
        }
        if (!locationReady()) {
            setStatus("TURN ON LOCATION FOR BLE SCAN", WatchView.TONE_WARN);
            openLocationSettings();
            return;
        }
        startScan();
    }

    private void startScan() {
        if (demo || ble == null) {
            return;
        }
        if (!BleClient.hasPermissions(this)) {
            requestBle(this::begin);
            return;
        }
        if (!ble.isEnabled()) {
            setStatus("BLUETOOTH OFF", WatchView.TONE_WARN);
            openBluetoothSettings();
            return;
        }
        devices.clear();
        scanning = true;
        linkStatus = "SCANNING…";
        linkTone = WatchView.TONE_WARN;
        ble.scan();
        view.invalidate();
    }

    @Override
    public void onConnect(String address) {
        if (demo) {
            deviceName = address == null ? "RF-Lab-DEMO" : address;
            connected = true;
            authed = true;
            screen = WatchView.SCREEN_CONTROL;
            armed = true;
            setStatus("DEMO LINKED", WatchView.TONE_OK);
            return;
        }
        if (address == null || address.isEmpty()) {
            return;
        }
        autoConnectTried = true;
        connectTo(address);
    }

    private void connectTo(String address) {
        if (ble == null || address == null || address.isEmpty()) {
            startScan();
            return;
        }
        screen = WatchView.SCREEN_LINK;
        connected = false;
        authed = false;
        armed = false;
        helloSeen = false;
        storedPinTried = false;
        authPending = false;
        pin = "";
        pinError = "";
        deviceAddress = address;
        if (deviceName.isEmpty()) {
            deviceName = prefs.lastName();
        }
        linkStatus = "CONNECTING…";
        linkTone = WatchView.TONE_WARN;
        setStatus("CONNECTING…", WatchView.TONE_WARN);
        final int gen = ++connectGen;
        ble.connect(address);
        main.postDelayed(() -> {
            if (gen == connectGen && !connected) {
                if (ble != null) {
                    ble.disconnect();
                }
                linkStatus = "CONNECT TIMEOUT";
                linkTone = WatchView.TONE_ERROR;
                setStatus("CONNECT TIMEOUT", WatchView.TONE_ERROR);
            }
        }, CONNECT_TIMEOUT_MS);
        view.invalidate();
    }

    @Override
    public void onPinKey(char key) {
        if (authPending) {
            return;
        }
        if (key == 'C') {
            pin = "";
            pinError = "";
            view.invalidate();
            return;
        }
        if (key == 'D') {
            if (!pin.isEmpty()) {
                pin = pin.substring(0, pin.length() - 1);
            }
            pinError = "";
            view.invalidate();
            return;
        }
        if (key < '0' || key > '9' || pin.length() >= 4) {
            return;
        }
        pin += key;
        pinError = "";
        if (pin.length() == 4) {
            if (demo) {
                authPending = false;
                authed = true;
                connected = true;
                screen = WatchView.SCREEN_CONTROL;
                setStatus("DEMO AUTH OK", WatchView.TONE_OK);
            } else {
                authPending = true;
                sendCmd("AUTH " + pin);
                setStatus("AUTH…", WatchView.TONE_WARN);
            }
        }
        view.invalidate();
    }

    @Override
    public void onPinCancel() {
        pin = "";
        pinError = "";
        authPending = false;
        pinAborted = true;
        screen = WatchView.SCREEN_LINK;
        linkStatus = "PAIRING CANCELLED";
        linkTone = WatchView.TONE_NEUTRAL;
        setStatus("PAIRING CANCELLED", WatchView.TONE_NEUTRAL);
        if (ble != null) {
            ble.disconnect();
        }
        connected = false;
        authed = false;
        view.invalidate();
    }

    @Override
    public void onMenuAction(int action) {
        menuOpen = false;
        forgotPending = false;
        switch (action) {
            case WatchView.MENU_LINK:
                if (demo) {
                    connected = !connected;
                    setStatus(connected ? "DEMO LINKED" : "DEMO UNLINKED",
                            connected ? WatchView.TONE_OK : WatchView.TONE_WARN);
                } else if (connected) {
                    safeDisconnect("DISCONNECTED");
                } else {
                    String addr = prefs.lastAddress();
                    if (!addr.isEmpty()) {
                        connectTo(addr);
                    } else {
                        startScan();
                    }
                }
                break;
            case WatchView.MENU_OLED:
                if (connected && authed) {
                    sendCmd("DISP TEST 10");
                    setStatus("OLED TEST 10s", WatchView.TONE_WARN);
                } else {
                    setStatus("NOT LINKED", WatchView.TONE_WARN);
                }
                break;
            case WatchView.MENU_REFRESH:
                if (connected && authed) {
                    sendCmd("LIST LONG");
                    setStatus("REFRESH CODES…", WatchView.TONE_NEUTRAL);
                } else {
                    setStatus("NOT LINKED", WatchView.TONE_WARN);
                }
                break;
            case WatchView.MENU_UPDATE:
                // Keep the panel open: the row label carries check/download
                // progress, and it is the only feedback on the LINK screen.
                menuOpen = !demo;
                startUpdate();
                break;
            case WatchView.MENU_FORGET:
                long now = SystemClock.uptimeMillis();
                if (!forgotPending || now - forgetAt > FORGET_CONFIRM_MS) {
                    forgotPending = true;
                    forgetAt = now;
                    setStatus("TAP AGAIN TO FORGET", WatchView.TONE_WARN);
                    main.postDelayed(() -> {
                        if (forgotPending && SystemClock.uptimeMillis() - forgetAt >= FORGET_CONFIRM_MS) {
                            forgotPending = false;
                            view.invalidate();
                        }
                    }, FORGET_CONFIRM_MS + 50);
                } else {
                    doForget();
                }
                break;
            default:
                break;
        }
        view.invalidate();
    }

    // ---------------- self update ----------------

    /**
     * Manual only: the SYSTEM menu starts a check, download and verification.
     * Nothing runs in the background and the final install always needs the
     * system confirmation dialog.
     */
    private void startUpdate() {
        if (demo) {
            setStatus("DEMO · NO UPDATE", WatchView.TONE_NEUTRAL);
            return;
        }
        if (updateBusy) {
            setStatus("UPDATE BUSY", WatchView.TONE_WARN);
            return;
        }
        if (txActiveInternal()) {
            setStatus("STOP LONG FIRST", WatchView.TONE_WARN);
            return;
        }
        updateBusy = true;
        updatePhase = "CHECK…";
        setStatus("CHECKING UPDATE…", WatchView.TONE_NEUTRAL);
        updater.check(appVersion, (release, error) -> {
            if (destroyed) {
                return;
            }
            if (error != null) {
                updateBusy = false;
                updatePhase = "FAIL · " + error;
                setStatus("UPD FAIL · " + error, WatchView.TONE_ERROR);
                return;
            }
            if (release == null) {
                updateBusy = false;
                updatePhase = "UP TO DATE";
                setStatus("UP TO DATE · " + appVersion, WatchView.TONE_OK);
                return;
            }
            updatePhase = release.version + " · 0%";
            setStatus("UPDATE " + release.version + " · 0%", WatchView.TONE_WARN);
            updater.download(this, release, (done, total) -> {
                if (destroyed) {
                    return;
                }
                int pct = total <= 0 ? 0 : (int) (done * 100 / total);
                updatePhase = release.version + " · " + pct + "%";
                setStatus("UPDATE " + release.version + " · " + pct + "%",
                        WatchView.TONE_WARN);
            }, new Updater.DownloadCallback() {
                @Override
                public void onReady(File apk) {
                    if (destroyed) {
                        return;
                    }
                    if (txActiveInternal()) {
                        updateBusy = false;
                        updatePhase = "STOP LONG FIRST";
                        setStatus("STOP LONG FIRST", WatchView.TONE_WARN);
                        return;
                    }
                    updatePhase = "INSTALL…";
                    setStatus("VERIFIED · INSTALL…", WatchView.TONE_WARN);
                    beginInstall(apk);
                }

                @Override
                public void onFailed(String err) {
                    if (destroyed) {
                        return;
                    }
                    updateBusy = false;
                    updatePhase = "FAIL · " + err;
                    setStatus("UPD FAIL · " + err, WatchView.TONE_ERROR);
                }
            });
        });
    }

    private void beginInstall(File apk) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                && !getPackageManager().canRequestPackageInstalls()) {
            updateBusy = false;
            updatePhase = "ALLOW SOURCE";
            setStatus("ALLOW INSTALL SOURCE", WatchView.TONE_WARN);
            openInstallSettings();
            return;
        }
        updater.install(this, apk, new Updater.InstallCallback() {
            @Override
            public void onCommitted() {
                if (destroyed) {
                    return;
                }
                updatePhase = "CONFIRM…";
                setStatus("CONFIRM INSTALL IN SYSTEM", WatchView.TONE_WARN);
            }

            @Override
            public void onFailed(String err) {
                if (destroyed) {
                    return;
                }
                updateBusy = false;
                updatePhase = "FAIL · " + err;
                setStatus("INSTALL · " + err, WatchView.TONE_ERROR);
            }
        });
    }

    private void openInstallSettings() {
        try {
            Intent i = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:" + getPackageName()));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
        } catch (RuntimeException e) {
            openAppSettings();
        }
    }

    private final BroadcastReceiver installReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS,
                    PackageInstaller.STATUS_FAILURE);
            String message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            updateBusy = false;
            if (status == PackageInstaller.STATUS_SUCCESS) {
                updatePhase = "UPDATED";
                setStatus("UPDATED · REOPEN", WatchView.TONE_OK);
            } else if (status == PackageInstaller.STATUS_FAILURE_ABORTED) {
                updatePhase = "CANCELLED";
                setStatus("UPDATE CANCELLED", WatchView.TONE_WARN);
            } else {
                updatePhase = "INSTALL FAILED";
                Log.w("RF-Lab.Watch", "install failed: " + status + " " + message);
                setStatus("INSTALL FAILED", WatchView.TONE_ERROR);
            }
        }
    };

    private void registerInstallReceiver() {
        IntentFilter filter = new IntentFilter(Updater.ACTION_INSTALL_RESULT);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(installReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(installReceiver, filter);
        }
    }

    private void doForget() {
        forgotPending = false;
        armed = false;
        pin = "";
        pinError = "";
        pinAborted = true;
        if (ble != null) {
            ble.disconnect();
        }
        connected = false;
        authed = false;
        prefs.forgetAll();
        deviceName = "";
        deviceAddress = "";
        codes.clear();
        devices.clear();
        selectedId = -1;
        screen = WatchView.SCREEN_LINK;
        linkStatus = "FORGOTTEN";
        linkTone = WatchView.TONE_NEUTRAL;
        setStatus("DEVICE + PIN FORGOTTEN", WatchView.TONE_NEUTRAL);
        main.postDelayed(this::startScan, 500);
    }

    @Override
    public void onCloseMenu() {
        menuOpen = false;
        forgotPending = false;
        if (!updateBusy) {
            updatePhase = "";
        }
        view.invalidate();
    }

    private void safeDisconnect(String why) {
        if (txActiveInternal()) {
            setStatus("STOP LONG FIRST", WatchView.TONE_WARN);
            return;
        }
        if (screen == WatchView.SCREEN_PIN) {
            pinAborted = true;
        }
        if (holdSent || txActive) {
            int id = holdSent ? holdId : txId;
            if (connected && id > 0) {
                sendCmd("STOP " + id);
            }
            holdSent = false;
            holding = false;
            // Do not clear txActive optimistically; stop event/state owns it.
        }
        final int gen = ++connectGen;
        main.postDelayed(() -> {
            if (gen != connectGen) {
                return;
            }
            if (ble != null) {
                ble.disconnect();
            }
            connected = false;
            authed = false;
            armed = false;
            screen = WatchView.SCREEN_LINK;
            linkStatus = why == null ? "DISCONNECTED" : why;
            linkTone = WatchView.TONE_NEUTRAL;
            setStatus(linkStatus, WatchView.TONE_NEUTRAL);
        }, 250);
    }

    // ---------------- host state ----------------

    @Override
    public int screen() {
        return screen;
    }

    @Override
    public String appVersion() {
        return appVersion;
    }

    @Override
    public String deviceName() {
        return deviceName;
    }

    @Override
    public String fw() {
        return fw;
    }

    @Override
    public boolean connected() {
        return connected;
    }

    @Override
    public boolean scanning() {
        return scanning;
    }

    @Override
    public boolean canScan() {
        return demo || (ble != null && BleClient.hasPermissions(this));
    }

    @Override
    public String linkStatus() {
        return linkStatus;
    }

    @Override
    public int linkTone() {
        return linkTone;
    }

    @Override
    public boolean unsafeTx() {
        return unsafeTx;
    }

    @Override
    public List<BleClient.Device> devices() {
        return devices;
    }

    @Override
    public String savedAddress() {
        return demo ? "" : prefs.lastAddress();
    }

    @Override
    public String savedName() {
        String name = prefs.lastName();
        return name.isEmpty() ? "SAVED DEVICE" : name;
    }

    @Override
    public boolean authPending() {
        return authPending;
    }

    @Override
    public String pinError() {
        return pinError;
    }

    @Override
    public int pinLength() {
        return pin.length();
    }

    @Override
    public boolean armed() {
        return armed;
    }

    @Override
    public List<Protocol.Code> codes() {
        return codes;
    }

    @Override
    public int selectedIndex() {
        int i = Protocol.indexOfId(codes, selectedId);
        return i >= 0 ? i : 0;
    }

    @Override
    public boolean txActive() {
        return txActive;
    }

    @Override
    public String txKind() {
        return txKind;
    }

    @Override
    public int txId() {
        return txId;
    }

    @Override
    public String status() {
        return status;
    }

    @Override
    public int statusTone() {
        return statusTone;
    }

    @Override
    public String updateLine() {
        return updatePhase;
    }

    @Override
    public boolean menuOpen() {
        return menuOpen;
    }

    @Override
    public boolean listOpen() {
        return listOpen;
    }

    @Override
    public boolean forgotPending() {
        return forgotPending;
    }

    private Protocol.Code selectedCode() {
        int i = Protocol.indexOfId(codes, selectedId);
        return i >= 0 ? codes.get(i) : null;
    }

    // ---------------- system ----------------

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQ_PERMS) {
            return;
        }
        if (BleClient.hasPermissions(this)) {
            permissionDeniedOnce = false;
            begin();
        } else {
            permissionDeniedOnce = true;
            linkStatus = "BLE PERMISSION DENIED";
            linkTone = WatchView.TONE_ERROR;
            setStatus("BLE PERMISSION DENIED", WatchView.TONE_ERROR);
            view.invalidate();
        }
    }

    private void requestBle(Runnable after) {
        if (BleClient.hasPermissions(this)) {
            after.run();
            return;
        }
        if (permissionDeniedOnce && !shouldShowRequestPermissionRationale(
                BleClient.requiredPermissions()[0])) {
            openAppSettings();
            return;
        }
        requestPermissions(BleClient.requiredPermissions(), REQ_PERMS);
    }

    private boolean locationReady() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return true;
        }
        try {
            LocationManager lm = (LocationManager) getSystemService(LOCATION_SERVICE);
            return lm != null && (lm.isProviderEnabled(LocationManager.GPS_PROVIDER)
                    || lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER));
        } catch (RuntimeException e) {
            return false;
        }
    }

    private void openLocationSettings() {
        try {
            startActivity(new Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS));
        } catch (RuntimeException e) {
            setStatus("OPEN SETTINGS: LOCATION", WatchView.TONE_WARN);
        }
    }

    private void openBluetoothSettings() {
        try {
            startActivity(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS));
        } catch (RuntimeException e) {
            setStatus("TURN ON BLUETOOTH", WatchView.TONE_WARN);
        }
    }

    private void openAppSettings() {
        try {
            Intent i = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                    Uri.parse("package:" + getPackageName()));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
        } catch (RuntimeException e) {
            setStatus("OPEN APP PERMISSIONS", WatchView.TONE_WARN);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        keepAwakeAndFullscreen();
        if (view != null) {
            view.invalidate();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (view != null) {
            view.resetTransient();
        }
        if (holdSent || txActive) {
            int id = holdSent ? holdId : txId;
            if (connected && id > 0) {
                sendCmd("STOP " + id);
            }
            holdSent = false;
            holding = false;
            // Do not clear txActive optimistically; stop event/state owns it.
        }
    }

    @Override
    public void onBackPressed() {
        if (menuOpen) {
            menuOpen = false;
            forgotPending = false;
            view.invalidate();
            return;
        }
        if (listOpen) {
            listOpen = false;
            view.invalidate();
            return;
        }
        if (screen == WatchView.SCREEN_PIN) {
            onPinCancel();
            return;
        }
        if (screen == WatchView.SCREEN_CONTROL && txActiveInternal()) {
            onStopTx();
            main.postDelayed(this::finish, 200);
            return;
        }
        super.onBackPressed();
    }

    @Override
    protected void onDestroy() {
        destroyed = true;
        main.removeCallbacksAndMessages(null);
        try {
            unregisterReceiver(installReceiver);
        } catch (RuntimeException ignored) {
            // Not registered (demo/early exit) or already torn down.
        }
        if (ble != null) {
            if (connected && (holdSent || txActive)) {
                int id = holdSent ? holdId : txId;
                if (id > 0) {
                    ble.send("STOP " + id);
                }
            }
            ble.close();
            ble = null;
        }
        super.onDestroy();
    }

    private void sendCmd(String line) {
        if (demo) {
            return;
        }
        if (ble == null || !ble.isConnected()) {
            setStatus("NOT LINKED", WatchView.TONE_WARN);
            return;
        }
        ble.send(line);
    }

    private void setStatus(String text, int tone) {
        status = text == null ? "" : text;
        statusTone = tone;
        lastEventAt = SystemClock.uptimeMillis();
        if (view != null) {
            view.invalidate();
        }
    }

    // ---------------- demo (debug builds only) ----------------

    private void setupDemo(Intent intent) {
        demo = true;
        connected = true;
        authed = true;
        armed = true;
        deviceName = "RF-Lab-DEMO";
        deviceAddress = "DE:MO:00:00:00:01";
        fw = "0.7.1";
        build = "demo";
        codes.clear();
        codes.add(new Protocol.Code(1, "315M", "5555034C", "客厅大门", "家门", "🚪", false, false));
        codes.add(new Protocol.Code(2, "315M", "5555033C", "卧室灯", "楼上", "💡", false, false));
        codes.add(new Protocol.Code(3, "433M", "00000093", "车库门", "", "", true, false));
        codes.add(new Protocol.Code(4, "433M", "5555034B", "TEST KEY", "lab", "", false, true));
        if (intent.getBooleanExtra("demo_many", false)) {
            for (int i = 5; i <= 16; i++) {
                codes.add(new Protocol.Code(i, i % 2 == 0 ? "315M" : "433M",
                        String.format(java.util.Locale.US, "85563%03X", i),
                        "SENSOR " + i, i % 3 == 0 ? "lab" : "", "", false, false));
            }
        }
        selectedId = 1;
        if (intent.getBooleanExtra("demo_pin", false)) {
            screen = WatchView.SCREEN_PIN;
            connected = false;
            authed = false;
            deviceName = "RF-Lab-AB12";
            pin = "12";
            status = "DEMO PIN";
        } else if (intent.getBooleanExtra("demo_link", false)) {
            screen = WatchView.SCREEN_LINK;
            connected = false;
            authed = false;
            devices.add(new BleClient.Device("RF-Lab-AB12", "AA:BB:CC:DD:EE:01", -57));
            devices.add(new BleClient.Device("RF-Lab-CD34", "AA:BB:CC:DD:EE:02", -71));
            linkStatus = "2 FOUND · TAP TO CONNECT";
            linkTone = WatchView.TONE_OK;
            status = "DEMO LINK";
        } else {
            screen = WatchView.SCREEN_CONTROL;
            status = "DEMO · " + codes.size() + " CODES";
            statusTone = WatchView.TONE_OK;
            if (intent.getBooleanExtra("demo_tx", false)) {
                txActive = true;
                txId = 1;
                status = "LONG #1 · TAP STOP";
                statusTone = WatchView.TONE_WARN;
            }
        }
        linkTone = WatchView.TONE_OK;
    }
}
