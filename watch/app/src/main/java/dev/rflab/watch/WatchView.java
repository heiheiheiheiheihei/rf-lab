package dev.rflab.watch;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.util.Collections;
import java.util.List;

/**
 * Single custom-drawn watch surface. Layout is computed from the real pixel size
 * in dp, so the same code covers 402x476 (46mm) and 372x430 (42mm).
 *
 * <p>The view owns only ephemeral UI state (presses, hold progress, scroll).
 * Connection, auth, ARM and TX state live in {@link WatchActivity}.
 */
@SuppressLint("ViewConstructor")
public final class WatchView extends View {

    public static final int SCREEN_LINK = 0;
    public static final int SCREEN_PIN = 1;
    public static final int SCREEN_CONTROL = 2;

    public static final int MENU_LINK = 0;
    public static final int MENU_OLED = 1;
    public static final int MENU_REFRESH = 2;
    public static final int MENU_FORGET = 3;

    public static final int TONE_NEUTRAL = 0;
    public static final int TONE_OK = 1;
    public static final int TONE_WARN = 2;
    public static final int TONE_ERROR = 3;

    private static final int C_INK = 0xFF070906;
    private static final int C_PANEL = 0xFF10140C;
    private static final int C_PANEL_HI = 0xFF1A1F12;
    private static final int C_LINE = 0xFF303823;
    private static final int C_LINE_HI = 0xFF46522F;
    private static final int C_AMBER = 0xFFFFB000;
    private static final int C_AMBER_HI = 0xFFFFD24A;
    private static final int C_BONE = 0xFFECE6D4;
    private static final int C_KHAKI = 0xFFB4AE96;
    private static final int C_DIM = 0xFF817D6B;
    private static final int C_GREEN = 0xFF9BF05C;
    private static final int C_RED = 0xFFFF5D50;
    private static final int C_GRID = 0xFF0D110A;

    private static final long HOLD_MS = 350L;

    public interface Host {
        int screen();

        String appVersion();

        String deviceName();

        String fw();

        boolean connected();

        boolean scanning();

        boolean canScan();

        String linkStatus();

        int linkTone();

        boolean unsafeTx();

        List<BleClient.Device> devices();

        String savedAddress();

        String savedName();

        boolean authPending();

        String pinError();

        int pinLength();

        boolean armed();

        List<Protocol.Code> codes();

        int selectedIndex();

        boolean txActive();

        String txKind();

        int txId();

        String status();

        int statusTone();

        boolean menuOpen();

        boolean listOpen();

        boolean forgotPending();

        void onArm();

        void onMenu();

        void onSend();

        void onStopTx();

        void onHoldStart();

        void onHoldRelease();

        void onSelect(int delta);

        void onOpenList();

        void onCloseList();

        void onPick(int index);

        void onScan();

        void onConnect(String address);

        void onPinKey(char key);

        void onPinCancel();

        void onMenuAction(int action);

        void onCloseMenu();
    }

    private final Host host;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint glow = new Paint(Paint.ANTI_ALIAS_FLAG);

    private final Typeface sans = Typeface.create("sans-serif", Typeface.NORMAL);
    private final Typeface sansBold = Typeface.create("sans-serif", Typeface.BOLD);
    private final Typeface mono = Typeface.MONOSPACE;
    private final Typeface condensed = Typeface.create("sans-serif-condensed", Typeface.BOLD);

    private final RectF menuBtn = new RectF();
    private final RectF menuHit = new RectF();
    private final RectF armBtn = new RectF();
    private final RectF armHit = new RectF();
    private final RectF card = new RectF();
    private final RectF sendBtn = new RectF();
    private final RectF holdBtn = new RectF();
    private final RectF stopBtn = new RectF();
    private final RectF scanBtn = new RectF();
    private final RectF savedBtn = new RectF();
    private final RectF listClose = new RectF();
    private final RectF listCloseHit = new RectF();
    private final RectF listPanel = new RectF();
    private final RectF menuPanel = new RectF();
    private final RectF[] menuRows = {new RectF(), new RectF(), new RectF(), new RectF()};
    private final RectF[] pinKeys = new RectF[12];

    private float linkListTop;
    private float linkListBottom;
    private float codeListTop;
    private float codeListBottom;
    private float rowH;

    private int screen = -1;
    private int pressed = 0;
    private float startX;
    private float startY;
    private float lastY;
    private boolean moved;
    private boolean holding;
    private boolean holdFired;
    private long holdStartAt;
    private float listScroll;
    private float linkScroll;
    private float flashUntil;
    private int flashColor;
    private int lastListSize;
    private int lastCodeId;

    private static final int PRESS_NONE = 0;
    private static final int PRESS_MENU = 1;
    private static final int PRESS_ARM = 2;
    private static final int PRESS_SEND = 3;
    private static final int PRESS_HOLD = 4;
    private static final int PRESS_STOP = 5;
    private static final int PRESS_SCAN = 6;
    private static final int PRESS_SAVED = 7;
    private static final int PRESS_LIST_CLOSE = 8;
    private static final int PRESS_MENU_ROW = 9;
    private static final int PRESS_KEY = 10;
    private static final int PRESS_CARD = 11;

    private int pressedMenuRow = -1;
    private int pressedKey = -1;

    private final Runnable holdStart;

    public WatchView(Context context, Host host) {
        super(context);
        this.host = host;
        holdStart = () -> {
            if (!holding || holdFired) {
                return;
            }
            holdFired = true;
            performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
            host.onHoldStart();
            invalidate();
        };
        setFocusable(true);
        setClickable(true);
        setContentDescription("RF-Lab watch control");
        p.setSubpixelText(true);
        fill.setStyle(Paint.Style.FILL);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(dp(1));
        for (int i = 0; i < pinKeys.length; i++) {
            pinKeys[i] = new RectF();
        }
        updateGlow();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        updateGlow();
        listScroll = 0;
        linkScroll = 0;
    }

    private void updateGlow() {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0) {
            return;
        }
        glow.setShader(new RadialGradient(w * 0.82f, h * 0.04f, Math.max(w, h) * 0.85f,
                new int[]{0x22FFB000, 0x00070906}, new float[]{0f, 1f},
                Shader.TileMode.CLAMP));
    }

    // ---------------- drawing ----------------

    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);
        syncScreen();
        c.drawColor(C_INK);
        drawGrid(c);
        if (glow.getShader() != null) {
            c.drawRect(0, 0, getWidth(), getHeight(), glow);
        }

        switch (host.screen()) {
            case SCREEN_LINK:
                drawLink(c);
                break;
            case SCREEN_PIN:
                drawPin(c);
                break;
            default:
                drawControl(c);
                break;
        }

        if (host.listOpen()) {
            drawList(c);
        }
        if (host.menuOpen()) {
            drawMenu(c);
        }
        scheduleFrame();
    }

    private void syncScreen() {
        if (screen != host.screen()) {
            screen = host.screen();
            cancelHold();
            pressed = PRESS_NONE;
            pressedMenuRow = -1;
            pressedKey = -1;
        }
        List<Protocol.Code> codes = host.codes();
        int size = codes == null ? 0 : codes.size();
        int idx = Math.max(0, Math.min(size - 1, host.selectedIndex()));
        int id = size == 0 ? -1 : codes.get(idx).id;
        if (size != lastListSize || id != lastCodeId) {
            lastListSize = size;
            lastCodeId = id;
            listScroll = clamp(listScroll, 0, Math.max(0, size * rowH - (codeListBottom - codeListTop)));
        }
    }

    private void scheduleFrame() {
        boolean active = host.scanning() || holding || host.txActive()
                || host.authPending() || SystemClock.uptimeMillis() < flashUntil;
        if (active && isAttachedToWindow()) {
            postInvalidateOnAnimation();
        }
    }

    private void drawGrid(Canvas c) {
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(dp(0.5f));
        p.setColor(C_GRID);
        float step = dp(24);
        for (float x = 0; x < getWidth(); x += step) {
            c.drawLine(x, 0, x, getHeight(), p);
        }
        for (float y = 0; y < getHeight(); y += step) {
            c.drawLine(0, y, getWidth(), y, p);
        }
    }

    private void drawHeader(Canvas c, boolean withMenu, boolean withArm, String right) {
        float pad = dp(8);
        float top = dp(6);
        menuBtn.set(pad, top + dp(2), pad + dp(24), top + dp(24));
        menuHit.set(pad - dp(4), top - dp(2), pad + dp(30), top + dp(36));
        if (withMenu) {
            p.setColor(pressed == PRESS_MENU ? C_AMBER_HI : C_AMBER);
            p.setStrokeWidth(dp(1.5f));
            for (int i = 0; i < 3; i++) {
                float y = menuBtn.centerY() - dp(5) + i * dp(5);
                c.drawLine(menuBtn.left + dp(2), y, menuBtn.right - dp(2), y, p);
            }
        } else if (host.screen() == SCREEN_PIN) {
            text(c, "‹", pad + dp(2), top + dp(19), 20, C_AMBER, sans, Paint.Align.LEFT);
        }

        float brandX = withMenu ? pad + dp(28) : pad;
        text(c, "RF-LAB", brandX, top + dp(19), 11, C_AMBER, condensed, Paint.Align.LEFT);

        if (withArm) {
            float w = dp(62);
            armBtn.set(getWidth() - pad - w, top + dp(1), getWidth() - pad, top + dp(25));
            armHit.set(armBtn.left - dp(4), armBtn.top - dp(2),
                    armBtn.right + dp(4), armBtn.bottom + dp(12));
            boolean armed = host.armed();
            drawPanel(c, armBtn, armed ? C_AMBER : C_PANEL,
                    armed ? C_AMBER : C_LINE_HI, pressed == PRESS_ARM);
            text(c, armed ? "ARMED" : "SAFE", armBtn.centerX(), baseline(armBtn, 10),
                    10, armed ? C_INK : C_KHAKI, condensed, Paint.Align.CENTER);
        } else if (right != null && !right.isEmpty()) {
            text(c, right, getWidth() - pad, top + dp(18), 8, C_DIM, mono, Paint.Align.RIGHT);
        }
    }

    private void drawLink(Canvas c) {
        float pad = dp(8);
        float top = dp(6);
        drawHeader(c, false, false, "W/" + host.appVersion());

        text(c, "LINK RF-LAB", pad, top + dp(38), 16, C_BONE, sansBold, Paint.Align.LEFT);
        text(c, "BLE · 4-DIGIT PIN", pad, top + dp(50), 8, C_DIM, mono, Paint.Align.LEFT);

        if (host.unsafeTx()) {
            text(c, "TX MAY CONTINUE — RECONNECT TO STOP", pad, top + dp(62), 8, C_RED,
                    sansBold, Paint.Align.LEFT);
        } else {
            text(c, fit(host.linkStatus(), sans, getWidth() - pad * 2, 9),
                    pad, top + dp(62), 9, tone(host.linkTone()), sans, Paint.Align.LEFT);
        }

        scanBtn.set(pad, top + dp(68), getWidth() - pad, top + dp(102));
        String scanLabel = !host.canScan() ? "GRANT BLE ACCESS"
                : host.scanning() ? "STOP SCAN" : "SCAN";
        drawPanel(c, scanBtn, host.canScan() ? C_PANEL : C_PANEL_HI,
                host.scanning() ? C_AMBER : C_LINE_HI, pressed == PRESS_SCAN);
        text(c, scanLabel, scanBtn.centerX(), baseline(scanBtn, 11),
                11, host.canScan() ? C_AMBER : C_KHAKI, condensed, Paint.Align.CENTER);

        List<BleClient.Device> devices = host.devices();
        if (devices == null) {
            devices = Collections.emptyList();
        }
        String savedAddr = host.savedAddress();
        boolean showSaved = !savedAddr.isEmpty() && devices.isEmpty();
        float listTop;
        if (showSaved) {
            savedBtn.set(pad, scanBtn.bottom + dp(5), getWidth() - pad, scanBtn.bottom + dp(35));
            drawPanel(c, savedBtn, C_PANEL, C_LINE, pressed == PRESS_SAVED);
            text(c, "LAST  " + host.savedName(), savedBtn.left + dp(7),
                    baseline(savedBtn, 9), 9, C_AMBER, sans, Paint.Align.LEFT);
            text(c, "TAP", savedBtn.right - dp(7), baseline(savedBtn, 8),
                    8, C_DIM, mono, Paint.Align.RIGHT);
            listTop = savedBtn.bottom + dp(5);
        } else {
            savedBtn.setEmpty();
            listTop = scanBtn.bottom + dp(6);
        }

        linkListTop = listTop;
        linkListBottom = getHeight() - pad - dp(12);
        float available = Math.max(0, linkListBottom - linkListTop);
        rowH = dp(34);
        int maxScroll = Math.max(0, (int) Math.ceil(devices.size() * rowH - available));
        linkScroll = clamp(linkScroll, 0, maxScroll);

        if (devices.isEmpty()) {
            if (available >= dp(80) && !showSaved) {
                drawRadar(c, pad, linkListTop, getWidth() - pad, linkListBottom,
                        host.scanning() ? "SCANNING FOR RF-LAB" : "NO RF-LAB FOUND");
                if (!host.scanning() && available >= dp(110)) {
                    text(c, "KEEP WATCH AWAKE · MOVE CLOSER", getWidth() / 2f,
                            linkListBottom - dp(6), 7, C_DIM, mono, Paint.Align.CENTER);
                }
            } else {
                String empty = showSaved ? "NO DEVICE · TAP LAST TO RECONNECT"
                        : host.scanning() ? "SCANNING…" : "NO RF-LAB FOUND";
                text(c, empty, getWidth() / 2f,
                        linkListTop + Math.max(dp(12), available / 2f + dp(3)), 8,
                        host.scanning() ? C_AMBER : C_DIM, mono, Paint.Align.CENTER);
            }
        } else {
            c.save();
            c.clipRect(pad, linkListTop, getWidth() - pad, linkListBottom);
            for (int i = 0; i < devices.size(); i++) {
                float ry = linkListTop + i * rowH - linkScroll;
                if (ry + rowH < linkListTop || ry > linkListBottom) {
                    continue;
                }
                RectF r = new RectF(pad, ry + dp(1), getWidth() - pad, ry + rowH - dp(2));
                drawPanel(c, r, C_PANEL, C_LINE, false);
                BleClient.Device d = devices.get(i);
                text(c, fit(d.name, sans, r.width() - dp(60), 10), r.left + dp(7),
                        r.top + dp(14), 10, C_BONE, sans, Paint.Align.LEFT);
                text(c, d.rssi + " dBm", r.right - dp(7), r.top + dp(14),
                        8, rssiColor(d.rssi), mono, Paint.Align.RIGHT);
                text(c, "TAP TO CONNECT", r.left + dp(7), r.top + dp(26),
                        7, C_DIM, mono, Paint.Align.LEFT);
            }
            c.restore();
        }
        text(c, "PIN: OLED BOOT / SERIAL PIN", pad, getHeight() - dp(4),
                7, C_DIM, mono, Paint.Align.LEFT);
    }

    private void drawRadar(Canvas c, float l, float t, float r, float b, String label) {
        float cx = (l + r) / 2f;
        float cy = (t + b) / 2f - dp(6);
        float radius = Math.min(r - l, b - t) * 0.28f;
        stroke.setColor(C_LINE);
        stroke.setStrokeWidth(dp(1));
        for (int i = 1; i <= 3; i++) {
            c.drawCircle(cx, cy, radius * i / 3f, stroke);
        }
        if (host.scanning()) {
            float angle = (SystemClock.uptimeMillis() / 16f) % 360f;
            fill.setColor(0x33FFB000);
            c.drawArc(new RectF(cx - radius, cy - radius, cx + radius, cy + radius),
                    angle, 42, true, fill);
        }
        text(c, label, cx, cy + radius + dp(15), 8,
                host.scanning() ? C_AMBER : C_DIM, mono, Paint.Align.CENTER);
    }

    private void drawPin(Canvas c) {
        float pad = dp(8);
        float top = dp(6);
        menuBtn.set(pad, top + dp(1), pad + dp(26), top + dp(25));
        menuHit.set(pad - dp(4), top - dp(2), pad + dp(30), top + dp(36));
        text(c, "‹", pad + dp(1), top + dp(19), 20, C_AMBER, sans, Paint.Align.LEFT);
        text(c, "PAIR", getWidth() / 2f, top + dp(18), 14, C_BONE, condensed,
                Paint.Align.CENTER);
        text(c, "PIN", getWidth() - pad, top + dp(17), 8, C_DIM, mono, Paint.Align.RIGHT);
        text(c, fit(host.deviceName(), sans, getWidth() - dp(40), 8), getWidth() / 2f,
                top + dp(30), 8, C_DIM, mono, Paint.Align.CENTER);

        int n = Math.max(0, Math.min(4, host.pinLength()));
        float box = dp(22);
        float gap = dp(10);
        float total = box * 4 + gap * 3;
        float sx = (getWidth() - total) / 2f;
        float sy = top + dp(36);
        boolean pending = host.authPending();
        for (int i = 0; i < 4; i++) {
            RectF r = new RectF(sx + i * (box + gap), sy, sx + i * (box + gap) + box, sy + box);
            if (i < n) {
                fill.setColor(pending ? (0x80 | (C_AMBER & 0x00FFFFFF)) : C_AMBER);
                c.drawRect(r, fill);
            } else {
                stroke.setColor(C_LINE_HI);
                stroke.setStrokeWidth(dp(1));
                c.drawRect(r, stroke);
            }
        }

        String err = host.pinError();
        if (err != null && !err.isEmpty()) {
            text(c, err, getWidth() / 2f, sy + box + dp(10), 8, C_RED, sansBold,
                    Paint.Align.CENTER);
        } else if (pending) {
            text(c, "AUTH…", getWidth() / 2f, sy + box + dp(10), 8, C_AMBER, mono,
                    Paint.Align.CENTER);
        }

        float keyTop = sy + box + dp(16);
        float keyBottom = getHeight() - pad - dp(6);
        float keyGap = dp(4);
        float keyW = (getWidth() - pad * 2 - keyGap * 2) / 3f;
        float keyH = Math.max(dp(22), (keyBottom - keyTop - keyGap * 3) / 4f);
        String[] labels = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "⌫", "0", "CLR"};
        for (int i = 0; i < 12; i++) {
            int row = i / 3;
            int col = i % 3;
            RectF r = pinKeys[i];
            r.set(pad + col * (keyW + keyGap), keyTop + row * (keyH + keyGap),
                    pad + col * (keyW + keyGap) + keyW,
                    keyTop + row * (keyH + keyGap) + keyH);
            boolean special = i >= 9;
            boolean active = pressed == PRESS_KEY && pressedKey == i;
            drawPanel(c, r, active ? C_PANEL_HI : C_PANEL,
                    special ? C_LINE : C_LINE_HI, false);
            text(c, labels[i], r.centerX(), baseline(r, special ? 10 : 17),
                    special ? 10 : 17, special ? C_KHAKI : C_BONE,
                    special ? condensed : mono, Paint.Align.CENTER);
        }
        text(c, "PIN ON OLED BOOT / SERIAL PIN", getWidth() / 2f, getHeight() - dp(3),
                7, C_DIM, mono, Paint.Align.CENTER);
    }

    private void drawControl(Canvas c) {
        float pad = dp(8);
        float top = dp(6);
        drawHeader(c, true, true, null);

        float linkY = top + dp(30);
        fill.setColor(C_LINE_HI);
        c.drawCircle(pad + dp(3), linkY + dp(6), dp(2.5f), fill);
        text(c, fit(host.deviceName(), mono, getWidth() - pad * 2 - dp(78), 9),
                pad + dp(10), linkY + dp(10), 9, C_KHAKI, mono, Paint.Align.LEFT);
        text(c, host.connected() ? "ONLINE" : "OFFLINE",
                getWidth() - pad, linkY + dp(10), 8,
                host.connected() ? C_GREEN : C_RED, mono, Paint.Align.RIGHT);

        float actionH = dp(46);
        float actionY = getHeight() - pad - actionH;
        float statusH = dp(18);
        float statusY = actionY - dp(5) - statusH;
        card.set(pad, linkY + dp(20), getWidth() - pad, statusY - dp(5));
        if (card.height() < dp(54)) {
            card.top = Math.max(linkY + dp(16), card.bottom - dp(54));
        }

        List<Protocol.Code> codes = host.codes();
        if (codes == null) {
            codes = Collections.emptyList();
        }
        int idx = host.selectedIndex();
        Protocol.Code code = (idx >= 0 && idx < codes.size()) ? codes.get(idx) : null;

        boolean tx = host.txActive();
        boolean pulse = tx && ((SystemClock.uptimeMillis() / 300) % 2 == 0);
        int border = tx ? (pulse ? C_RED : 0xFFFF8A80) : C_LINE;
        drawPanel(c, card, tx ? 0xFF1A0C0B : C_PANEL, border, false);
        if (host.armed() && !tx) {
            fill.setColor(C_AMBER);
            c.drawRect(card.left, card.top, card.left + dp(2), card.bottom, fill);
        }

        float cardPad = dp(9);
        text(c, codes.isEmpty() ? "00/00" : String.format(java.util.Locale.US, "%02d/%02d",
                        idx + 1, codes.size()),
                card.left + cardPad, card.top + dp(14), 8, C_DIM, mono, Paint.Align.LEFT);

        String band = code == null ? "---" : code.bandLabel();
        RectF bandRect = new RectF(card.right - cardPad - dp(40), card.top + dp(5),
                card.right - cardPad, card.top + dp(23));
        drawPanel(c, bandRect, C_PANEL_HI, tx ? C_RED : C_LINE_HI, false);
        text(c, band, bandRect.centerX(), baseline(bandRect, 9), 9,
                tx ? C_RED : C_AMBER, mono, Paint.Align.CENTER);

        String name = code == null ? "NO CODES" : code.nameLabel();
        String hex = code == null ? "LEARN A REMOTE" : code.codeLabel();
        float nameY = card.top + dp(38);
        text(c, fit(name, sansBold, card.width() - cardPad * 2, 16), card.left + cardPad,
                nameY, 16, C_BONE, sansBold, Paint.Align.LEFT);
        text(c, hex, card.left + cardPad, nameY + dp(18), 12,
                code == null ? C_DIM : (tx ? C_RED : C_AMBER_HI), mono, Paint.Align.LEFT);
        if (card.height() >= dp(80) && code != null) {
            String meta = watchMeta(code);
            if (!meta.isEmpty()) {
                text(c, fit(meta, sans, card.width() - cardPad * 2 - dp(26), 8),
                        card.left + cardPad, card.bottom - dp(7), 8, C_DIM, sans,
                        Paint.Align.LEFT);
            }
            text(c, "▲▼", card.right - cardPad, card.bottom - dp(7), 8, C_DIM, mono,
                    Paint.Align.RIGHT);
        }

        String status = host.status();
        if (status == null) {
            status = "";
        }
        int tone = host.statusTone();
        fill.setColor(tone(tone));
        c.drawRect(pad, statusY + dp(4), pad + dp(2), statusY + statusH - dp(3), fill);
        long now = SystemClock.uptimeMillis();
        int color = now < flashUntil ? flashColor : tone(tone);
        text(c, fit(status, sans, getWidth() - pad * 2 - dp(8), 9), pad + dp(7),
                baseline(new RectF(pad, statusY, getWidth() - pad, statusY + statusH), 9),
                9, color, sans, Paint.Align.LEFT);

        if (tx && !holding) {
            stopBtn.set(pad, actionY, getWidth() - pad, actionY + actionH);
            drawPanel(c, stopBtn, C_RED, C_RED, pressed == PRESS_STOP);
            text(c, "STOP", stopBtn.centerX(), baseline(stopBtn, 16), 16, C_INK,
                    condensed, Paint.Align.CENTER);
            text(c, "ID " + host.txId(), stopBtn.centerX(), baseline(stopBtn, 6) + dp(11),
                    7, 0xAA000000, mono, Paint.Align.CENTER);
        } else {
            float half = (getWidth() - pad * 2 - dp(6)) / 2f;
            sendBtn.set(pad, actionY, pad + half, actionY + actionH);
            holdBtn.set(pad + half + dp(6), actionY, getWidth() - pad, actionY + actionH);
            boolean can = host.armed() && !codes.isEmpty() && host.connected() && !tx && !holding;
            drawPanel(c, sendBtn, can ? C_AMBER : C_PANEL,
                    can ? C_AMBER : C_LINE, pressed == PRESS_SEND);
            text(c, "SEND", sendBtn.centerX(), baseline(sendBtn, 15), 15,
                    can ? C_INK : C_DIM, condensed, Paint.Align.CENTER);
            String sendSub = can ? "SINGLE"
                    : (host.armed() && (tx || holding)) ? "BUSY" : "ARM FIRST";
            text(c, sendSub, sendBtn.centerX(),
                    baseline(sendBtn, 7) + dp(12), 7,
                    can ? 0xAA000000 : C_DIM, mono, Paint.Align.CENTER);

            int holdFg = holding ? C_RED : (can ? C_AMBER : C_DIM);
            drawPanel(c, holdBtn, holding ? 0xFF1A0C0B : C_PANEL,
                    holding ? C_RED : (can ? C_LINE_HI : C_LINE), pressed == PRESS_HOLD);
            text(c, holding ? "RELEASE" : "HOLD", holdBtn.centerX(),
                    baseline(holdBtn, 15), 15, holdFg, condensed, Paint.Align.CENTER);
            text(c, holding ? "STOP" : "350ms LONG", holdBtn.centerX(),
                    baseline(holdBtn, 7) + dp(12), 7, holdFg, mono, Paint.Align.CENTER);
            if (holding) {
                float frac = holdFired ? 1f
                        : Math.min(1f, (SystemClock.uptimeMillis() - holdStartAt) / (float) HOLD_MS);
                fill.setColor(C_RED);
                c.drawRect(holdBtn.left + dp(2), holdBtn.bottom - dp(3),
                        holdBtn.left + dp(2) + (holdBtn.width() - dp(4)) * frac,
                        holdBtn.bottom - dp(1), fill);
            }
        }
    }

    private void drawList(Canvas c) {
        float pad = dp(8);
        float top = dp(6);
        fill.setColor(0xCC070906);
        c.drawRect(0, 0, getWidth(), getHeight(), fill);

        listPanel.set(pad, top + dp(2), getWidth() - pad, getHeight() - pad);
        drawPanel(c, listPanel, C_PANEL, C_LINE_HI, false);
        text(c, "CODEBOOK", listPanel.left + dp(9), listPanel.top + dp(18), 10,
                C_AMBER, condensed, Paint.Align.LEFT);
        List<Protocol.Code> codes = host.codes();
        if (codes == null) {
            codes = Collections.emptyList();
        }
        text(c, codes.size() + " CODES", listPanel.right - dp(36), listPanel.top + dp(18),
                8, C_DIM, mono, Paint.Align.RIGHT);
        listClose.set(listPanel.right - dp(28), listPanel.top + dp(4),
                listPanel.right - dp(4), listPanel.top + dp(24));
        listCloseHit.set(listClose.left - dp(6), listClose.top - dp(6),
                listClose.right + dp(4), listClose.bottom + dp(8));
        text(c, "×", listClose.centerX(), listClose.centerY() + dp(7), 18,
                pressed == PRESS_LIST_CLOSE ? C_AMBER : C_KHAKI, sans, Paint.Align.CENTER);

        codeListTop = listPanel.top + dp(28);
        codeListBottom = listPanel.bottom - dp(6);
        float available = Math.max(0, codeListBottom - codeListTop);
        rowH = dp(38);
        int maxScroll = Math.max(0, (int) Math.ceil(codes.size() * rowH - available));
        listScroll = clamp(listScroll, 0, maxScroll);

        if (codes.isEmpty()) {
            text(c, "NO CODES", listPanel.centerX(), listPanel.centerY() - dp(8),
                    12, C_DIM, sansBold, Paint.Align.CENTER);
            text(c, "PRESS A REMOTE TO LEARN", listPanel.centerX(), listPanel.centerY() + dp(8),
                    8, C_DIM, mono, Paint.Align.CENTER);
            return;
        }

        c.save();
        c.clipRect(listPanel.left + dp(4), codeListTop, listPanel.right - dp(4), codeListBottom);
        for (int i = 0; i < codes.size(); i++) {
            float ry = codeListTop + i * rowH - listScroll;
            if (ry + rowH < codeListTop || ry > codeListBottom) {
                continue;
            }
            Protocol.Code code = codes.get(i);
            boolean selected = i == host.selectedIndex();
            RectF r = new RectF(listPanel.left + dp(4), ry + dp(1),
                    listPanel.right - dp(4), ry + rowH - dp(2));
            drawPanel(c, r, selected ? C_PANEL_HI : C_PANEL, selected ? C_AMBER : C_LINE, false);
            if (selected) {
                fill.setColor(C_AMBER);
                c.drawRect(r.left, r.top, r.left + dp(2), r.bottom, fill);
            }
            text(c, fit(code.nameLabel(), sans, r.width() - dp(52), 11), r.left + dp(8),
                    r.top + dp(15), 11, selected ? C_BONE : C_KHAKI, sans, Paint.Align.LEFT);
            text(c, "#" + code.id, r.right - dp(7), r.top + dp(14), 8,
                    C_DIM, mono, Paint.Align.RIGHT);
            text(c, code.bandLabel() + "  " + code.codeLabel(), r.left + dp(8),
                    r.top + dp(28), 8, selected ? C_AMBER : C_DIM, mono, Paint.Align.LEFT);
        }
        c.restore();
    }

    private void drawMenu(Canvas c) {
        float pad = dp(8);
        float top = dp(6);
        fill.setColor(0xCC070906);
        c.drawRect(0, 0, getWidth(), getHeight(), fill);

        menuPanel.set(pad, top + dp(30), getWidth() - pad, getHeight() - pad);
        drawPanel(c, menuPanel, C_PANEL, C_LINE_HI, false);
        text(c, "SYSTEM", menuPanel.left + dp(9), menuPanel.top + dp(17), 10,
                C_AMBER, condensed, Paint.Align.LEFT);
        text(c, "RF-LAB", menuPanel.right - dp(9), menuPanel.top + dp(17), 8,
                C_DIM, mono, Paint.Align.RIGHT);

        float rowTop = menuPanel.top + dp(24);
        float footerH = dp(24);
        float rowSpace = Math.max(0, menuPanel.bottom - footerH - rowTop);
        float rh = Math.min(dp(34), rowSpace / 4f);
        String[] labels = {
                host.connected() ? "DISCONNECT" : "RECONNECT",
                "OLED TEST 10s",
                "REFRESH CODES",
                host.forgotPending() ? "CONFIRM FORGET" : "FORGET DEVICE + PIN"
        };
        for (int i = 0; i < 4; i++) {
            RectF r = menuRows[i];
            r.set(menuPanel.left + dp(4), rowTop + i * rh,
                    menuPanel.right - dp(4), rowTop + (i + 1) * rh - dp(2));
            boolean active = pressed == PRESS_MENU_ROW && pressedMenuRow == i;
            boolean danger = i == 3;
            boolean enabled = i == 0 || host.connected();
            drawPanel(c, r, active ? C_PANEL_HI : C_PANEL,
                    danger && host.forgotPending() ? C_RED : C_LINE, false);
            int color = danger ? (host.forgotPending() ? C_RED : C_KHAKI)
                    : (enabled ? C_BONE : C_DIM);
            text(c, labels[i], r.left + dp(8), baseline(r, 10), 10, color, sans,
                    Paint.Align.LEFT);
            text(c, i == 3 ? "2×" : "›", r.right - dp(8), baseline(r, 9), 9,
                    C_DIM, mono, Paint.Align.RIGHT);
        }

        float fy = menuPanel.bottom - dp(9);
        text(c, "APP " + host.appVersion() + " · FW " + host.fw(),
                menuPanel.left + dp(9), fy, 7, C_DIM, mono, Paint.Align.LEFT);
        text(c, "BLE ONLY", menuPanel.right - dp(9), fy, 7, C_DIM, mono,
                Paint.Align.RIGHT);
    }

    private void drawPanel(Canvas c, RectF r, int bg, int border, boolean press) {
        if (bg != Color.TRANSPARENT) {
            fill.setColor(bg);
            c.drawRect(r, fill);
        }
        if (border != Color.TRANSPARENT) {
            stroke.setColor(border);
            stroke.setStrokeWidth(dp(1));
            c.drawRect(r, stroke);
        }
        if (press) {
            fill.setColor(0x22FFFFFF);
            c.drawRect(r, fill);
        }
    }

    private void text(Canvas c, String s, float x, float baseline, float size, int color,
                      Typeface tf, Paint.Align align) {
        if (s == null || s.isEmpty()) {
            return;
        }
        p.setStyle(Paint.Style.FILL);
        p.setTypeface(tf);
        p.setTextSize(sp(size));
        p.setColor(color);
        p.setTextAlign(align);
        c.drawText(s, x, baseline, p);
    }

    private float baseline(RectF r, float size) {
        p.setTypeface(sans);
        p.setTextSize(sp(size));
        Paint.FontMetrics fm = p.getFontMetrics();
        return r.centerY() - (fm.ascent + fm.descent) / 2f;
    }

    private String fit(String s, Typeface tf, float maxWidth, float size) {
        if (s == null || s.isEmpty()) {
            return "";
        }
        p.setTypeface(tf);
        p.setTextSize(sp(size));
        if (p.measureText(s) <= maxWidth) {
            return s;
        }
        String ell = "…";
        float ew = p.measureText(ell);
        int lo = 0;
        int hi = s.length();
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (p.measureText(s.substring(0, mid)) + ew <= maxWidth) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        return s.substring(0, Math.max(0, lo)) + ell;
    }

    /** The watch shows group/manual tags; emoji symbols are left to the phone UI. */
    private String watchMeta(Protocol.Code code) {
        StringBuilder b = new StringBuilder();
        if (!code.group.isEmpty()) {
            b.append(code.group);
        }
        if (code.manual || code.edited) {
            if (b.length() > 0) {
                b.append("  ");
            }
            b.append(code.manual ? "MANUAL" : "EDITED");
        }
        return b.toString();
    }

    private int tone(int tone) {
        switch (tone) {
            case TONE_OK:
                return C_GREEN;
            case TONE_WARN:
                return C_AMBER;
            case TONE_ERROR:
                return C_RED;
            default:
                return C_KHAKI;
        }
    }

    private int rssiColor(int rssi) {
        if (rssi >= -60) {
            return C_GREEN;
        }
        if (rssi >= -75) {
            return C_AMBER;
        }
        return C_DIM;
    }

    private float clamp(float v, float min, float max) {
        return Math.max(min, Math.min(max, v));
    }

    private float dp(float v) {
        return v * getResources().getDisplayMetrics().density;
    }

    private float sp(float v) {
        return v * getResources().getDisplayMetrics().scaledDensity;
    }

    // ---------------- touch ----------------

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        float x = e.getX();
        float y = e.getY();
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                startX = x;
                startY = y;
                lastY = y;
                moved = false;
                return onDown(x, y);
            case MotionEvent.ACTION_MOVE:
                if (Math.abs(x - startX) > dp(8) || Math.abs(y - startY) > dp(8)) {
                    moved = true;
                }
                return onMove(x, y);
            case MotionEvent.ACTION_UP:
                if (!moved) {
                    performClick();
                }
                return onUp(x, y);
            case MotionEvent.ACTION_CANCEL:
                if (holding && holdFired) {
                    host.onHoldRelease();
                }
                cancelHold();
                pressed = PRESS_NONE;
                pressedKey = -1;
                pressedMenuRow = -1;
                invalidate();
                return true;
            default:
                return super.onTouchEvent(e);
        }
    }

    private boolean onDown(float x, float y) {
        if (host.menuOpen()) {
            for (int i = 0; i < menuRows.length; i++) {
                if (menuRows[i].contains(x, y)) {
                    pressed = PRESS_MENU_ROW;
                    pressedMenuRow = i;
                    invalidate();
                    return true;
                }
            }
            return true;
        }
        if (host.listOpen()) {
            if (listCloseHit.contains(x, y)) {
                pressed = PRESS_LIST_CLOSE;
            } else {
                pressed = PRESS_NONE;
            }
            return true;
        }
        if (hit(menuHit, x, y)) {
            pressed = PRESS_MENU;
            return true;
        }
        switch (host.screen()) {
            case SCREEN_LINK:
                if (hit(scanBtn, x, y)) {
                    pressed = PRESS_SCAN;
                } else if (hit(savedBtn, x, y)) {
                    pressed = PRESS_SAVED;
                }
                return true;
            case SCREEN_PIN:
                if (hit(menuHit, x, y)) {
                    pressed = PRESS_MENU;
                    return true;
                }
                for (int i = 0; i < pinKeys.length; i++) {
                    if (hit(pinKeys[i], x, y)) {
                        pressed = PRESS_KEY;
                        pressedKey = i;
                        invalidate();
                        return true;
                    }
                }
                return true;
            default:
                if (hit(armHit, x, y)) {
                    pressed = PRESS_ARM;
                } else if (host.txActive() && !holding) {
                    if (hit(stopBtn, x, y)) {
                        pressed = PRESS_STOP;
                    }
                } else if (hit(sendBtn, x, y)) {
                    pressed = PRESS_SEND;
                } else if (hit(holdBtn, x, y)) {
                    pressed = PRESS_HOLD;
                    startHold();
                } else if (hit(card, x, y)) {
                    pressed = PRESS_CARD;
                }
                invalidate();
                return true;
        }
    }

    private boolean onMove(float x, float y) {
        if (host.menuOpen()) {
            return true;
        }
        if (host.listOpen()) {
            listScroll = clamp(listScroll - (y - lastY), 0, maxListScroll());
            lastY = y;
            invalidate();
            return true;
        }
        if (host.screen() == SCREEN_LINK && !host.menuOpen()) {
            linkScroll = clamp(linkScroll - (y - lastY), 0, maxLinkScroll());
            lastY = y;
            invalidate();
            return true;
        }
        if (holding && (Math.abs(y - startY) > dp(24) || Math.abs(x - startX) > dp(24))) {
            // A finger sliding off the HOLD button must stop RF immediately.
            if (holdFired) {
                host.onHoldRelease();
            }
            cancelHold();
            invalidate();
        }
        return true;
    }

    private boolean onUp(float x, float y) {
        if (host.menuOpen()) {
            int row = pressed == PRESS_MENU_ROW ? pressedMenuRow : -1;
            pressed = PRESS_NONE;
            pressedMenuRow = -1;
            if (row >= 0 && menuRows[row].contains(x, y)) {
                host.onMenuAction(row);
            } else if (!menuPanel.contains(x, y)) {
                host.onCloseMenu();
            }
            invalidate();
            return true;
        }
        if (host.listOpen()) {
            boolean close = pressed == PRESS_LIST_CLOSE && listCloseHit.contains(x, y);
            int picked = -1;
            if (!close && !moved && !listCloseHit.contains(x, y)) {
                int index = (int) ((y - codeListTop + listScroll) / rowH);
                if (index >= 0 && index < host.codes().size() && y >= codeListTop
                        && y <= codeListBottom) {
                    picked = index;
                }
            }
            pressed = PRESS_NONE;
            if (close) {
                host.onCloseList();
            } else if (picked >= 0) {
                haptic();
                host.onPick(picked);
            }
            invalidate();
            return true;
        }

        switch (host.screen()) {
            case SCREEN_LINK:
                if (pressed == PRESS_SCAN && hit(scanBtn, x, y)) {
                    haptic();
                    host.onScan();
                } else if (pressed == PRESS_SAVED && hit(savedBtn, x, y)) {
                    haptic();
                    host.onConnect(host.savedAddress());
                } else if (!moved) {
                    int index = (int) ((y - linkListTop + linkScroll) / rowH);
                    List<BleClient.Device> devices = host.devices();
                    if (index >= 0 && devices != null && index < devices.size()
                            && y >= linkListTop && y <= linkListBottom) {
                        haptic();
                        host.onConnect(devices.get(index).address);
                    }
                }
                pressed = PRESS_NONE;
                invalidate();
                return true;
            case SCREEN_PIN:
                if (pressed == PRESS_KEY) {
                    int key = pressedKey;
                    if (key >= 0 && hit(pinKeys[key], x, y)) {
                        haptic();
                        if (key == 9) {
                            host.onPinKey('D');
                        } else if (key == 11) {
                            host.onPinKey('C');
                        } else {
                            char c = (char) ('0' + (key == 10 ? 0 : key + 1));
                            host.onPinKey(c);
                        }
                    }
                } else if (hit(menuHit, x, y)) {
                    host.onPinCancel();
                }
                pressed = PRESS_NONE;
                pressedKey = -1;
                invalidate();
                return true;
            default:
                if (pressed == PRESS_MENU && hit(menuHit, x, y)) {
                    haptic();
                    host.onMenu();
                } else if (pressed == PRESS_ARM && hit(armHit, x, y)) {
                    haptic();
                    host.onArm();
                } else if (pressed == PRESS_SEND && hit(sendBtn, x, y)) {
                    haptic();
                    host.onSend();
                    flash(C_GREEN);
                } else if (pressed == PRESS_STOP && hit(stopBtn, x, y)) {
                    haptic();
                    host.onStopTx();
                } else if (pressed == PRESS_HOLD) {
                    if (holdFired) {
                        host.onHoldRelease();
                        flash(C_RED);
                    }
                    cancelHold();
                } else if (pressed == PRESS_CARD) {
                    float dy = y - startY;
                    float dx = x - startX;
                    if (!moved) {
                        host.onOpenList();
                    } else if (Math.abs(dy) >= dp(14) && Math.abs(dy) > Math.abs(dx)) {
                        host.onSelect(dy < 0 ? 1 : -1);
                    }
                }
                pressed = PRESS_NONE;
                invalidate();
                return true;
        }
    }

    private void startHold() {
        holding = true;
        holdFired = false;
        holdStartAt = SystemClock.uptimeMillis();
        postDelayed(holdStart, HOLD_MS);
        invalidate();
    }

    private void cancelHold() {
        holding = false;
        holdFired = false;
        removeCallbacks(holdStart);
        invalidate();
    }

    private void flash(int color) {
        flashColor = color;
        flashUntil = SystemClock.uptimeMillis() + 260;
        invalidate();
    }

    private void haptic() {
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }

    private boolean hit(RectF r, float x, float y) {
        return !r.isEmpty() && r.contains(x, y);
    }

    private float maxListScroll() {
        List<Protocol.Code> codes = host.codes();
        int n = codes == null ? 0 : codes.size();
        return Math.max(0, n * rowH - (codeListBottom - codeListTop));
    }

    private float maxLinkScroll() {
        List<BleClient.Device> devices = host.devices();
        int n = devices == null ? 0 : devices.size();
        return Math.max(0, n * rowH - (linkListBottom - linkListTop));
    }

    /** Called when the activity pauses: a HOLD must not keep looking active. */
    public void resetTransient() {
        cancelHold();
        pressed = PRESS_NONE;
        pressedKey = -1;
        pressedMenuRow = -1;
        invalidate();
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }
}
