package dev.rflab.terminal;

import android.Manifest;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

/**
 * RF-Lab BLE 客户端:NUS 风格 GATT,行协议。
 *
 * 线程:scan/GATT 回调在 binder 线程,所有 UI 输出通过 main handler 回到
 * Listener;MainActivity 的 Listener 再 evaluateJavascript。
 */
public final class BleClient {
    private static final String TAG = "RfLabBle";

    public static final UUID SERVICE = UUID.fromString("5a3d0001-7c1f-4b8e-9a55-52464c414231");
    public static final UUID RX = UUID.fromString("5a3d0002-7c1f-4b8e-9a55-52464c414231");
    public static final UUID TX = UUID.fromString("5a3d0003-7c1f-4b8e-9a55-52464c414231");
    private static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    public interface Listener {
        void onScan(List<Device> devices);
        void onConnected(String name, String address);
        void onDisconnected();
        void onLine(String line);
        void onError(String message);
    }

    public static final class Device {
        public final String name;
        public final String address;
        public final int rssi;

        Device(String name, String address, int rssi) {
            this.name = name;
            this.address = address;
            this.rssi = rssi;
        }
    }

    private final Context context;
    private final Listener listener;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final Map<String, Device> found = new LinkedHashMap<>();
    private final ArrayDeque<byte[]> writes = new ArrayDeque<>();
    private final StringBuilder rxLine = new StringBuilder();

    private BluetoothAdapter adapter;
    private BluetoothLeScanner scanner;
    private ScanCallback scanCallback;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic rxChar;
    private BluetoothGattCharacteristic txChar;
    private int mtu = 23;
    private boolean connected;
    private boolean scanning;
    private int scanGen;
    private long lastEmit;
    private String connectedName = "";
    private String connectedAddress = "";

    public BleClient(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
        BluetoothManager manager = (BluetoothManager) this.context.getSystemService(Context.BLUETOOTH_SERVICE);
        if (manager != null) {
            adapter = manager.getAdapter();
        }
    }

    public boolean isEnabled() {
        return adapter != null && adapter.isEnabled();
    }

    // ---------------- permissions ----------------

    public static String[] requiredPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return new String[]{Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT};
        }
        return new String[]{Manifest.permission.BLUETOOTH, Manifest.permission.BLUETOOTH_ADMIN,
                Manifest.permission.ACCESS_FINE_LOCATION};
    }

    public static boolean hasPermissions(Context ctx) {
        for (String p : requiredPermissions()) {
            if (ctx.checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) {
                return false;
            }
        }
        return true;
    }

    // ---------------- scan ----------------

    @SuppressLint("MissingPermission")
    public void scan() {
        if (!hasPermissions(context)) {
            listener.onError("BLE permission not granted");
            return;
        }
        if (adapter == null || !adapter.isEnabled()) {
            listener.onError("Bluetooth is off");
            return;
        }
        if (scanning) {
            return;
        }
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) {
            listener.onError("BLE scanner unavailable");
            return;
        }

        found.clear();
        main.post(() -> listener.onScan(new ArrayList<>()));
        scanning = true;
        lastEmit = 0;

        scanCallback = new ScanCallback() {
            @Override
            public void onScanResult(int callbackType, ScanResult result) {
                handleResult(result);
            }

            @Override
            public void onBatchScanResults(List<ScanResult> results) {
                for (ScanResult r : results) {
                    handleResult(r);
                }
            }

            @Override
            public void onScanFailed(int errorCode) {
                scanning = false;
                listener.onError("BLE scan failed: " + errorCode);
            }
        };

        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();
        // 不按 Service UUID 过滤:部分手机只看 primary adv,RF-Lab 把 128-bit
        // UUID 放在 scan response;这里按名字/Service UUID 二次过滤更稳。
        final int gen = ++scanGen;
        scanner.startScan(null, settings, scanCallback);
        main.postDelayed(() -> {
            if (gen == scanGen) {
                stopScan();
            }
        }, 15000);
    }

    @SuppressLint("MissingPermission")
    private void handleResult(ScanResult result) {
        BluetoothDevice d = result.getDevice();
        String name = null;
        try {
            name = d.getName();
        } catch (SecurityException ignored) {
            // Some OEMs still throw despite the permission.
        }
        boolean match = name != null && name.startsWith("RF-Lab-");
        if (!match && result.getScanRecord() != null) {
            List<ParcelUuid> uuids = result.getScanRecord().getServiceUuids();
            if (uuids != null) {
                for (ParcelUuid u : uuids) {
                    if (SERVICE.equals(u.getUuid())) {
                        match = true;
                        break;
                    }
                }
            }
        }
        if (!match) {
            return;
        }
        String addr = d.getAddress();
        found.put(addr, new Device(name, addr, result.getRssi()));
        long now = System.currentTimeMillis();
        if (now - lastEmit < 700) {
            return;
        }
        lastEmit = now;
        List<Device> list = new ArrayList<>(found.values());
        main.post(() -> listener.onScan(list));
    }

    @SuppressLint("MissingPermission")
    public void stopScan() {
        if (!scanning) {
            return;
        }
        scanning = false;
        try {
            if (scanner != null && scanCallback != null) {
                scanner.stopScan(scanCallback);
            }
        } catch (SecurityException ignored) {
        }
    }

    // ---------------- connect ----------------

    @SuppressLint("MissingPermission")
    public void connect(String address) {
        if (!hasPermissions(context)) {
            listener.onError("BLE permission not granted");
            return;
        }
        if (adapter == null || !adapter.isEnabled()) {
            listener.onError("Bluetooth is off");
            return;
        }
        stopScan();
        disconnect();
        BluetoothDevice device;
        try {
            device = adapter.getRemoteDevice(address);
        } catch (IllegalArgumentException e) {
            listener.onError("bad BLE address");
            return;
        }
        connectedName = "";
        connectedAddress = address;
        try {
            connectedName = device.getName() == null ? "" : device.getName();
        } catch (SecurityException ignored) {
        }
        gatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE);
        if (gatt == null) {
            listener.onError("connectGatt failed");
        }
    }

    @SuppressLint("MissingPermission")
    public void disconnect() {
        connected = false;
        writes.clear();
        rxLine.setLength(0);
        if (gatt != null) {
            try {
                gatt.disconnect();
                gatt.close();
            } catch (SecurityException ignored) {
            }
            gatt = null;
        }
        rxChar = null;
        txChar = null;
        mtu = 23;
    }

    public boolean isConnected() {
        return connected;
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                try {
                    g.discoverServices();
                } catch (SecurityException e) {
                    listener.onError("discoverServices: " + e.getMessage());
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                connected = false;
                writes.clear();
                rxLine.setLength(0);
                try {
                    g.close();
                } catch (Exception ignored) {
                }
                if (gatt == g) {
                    gatt = null;
                }
                main.post(listener::onDisconnected);
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt g, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                listener.onError("service discovery failed: " + status);
                return;
            }
            BluetoothGattService svc = g.getService(SERVICE);
            if (svc == null) {
                listener.onError("RF-Lab service not found");
                return;
            }
            rxChar = svc.getCharacteristic(RX);
            txChar = svc.getCharacteristic(TX);
            if (rxChar == null || txChar == null) {
                listener.onError("RF-Lab characteristics missing");
                return;
            }
            if (!g.requestMtu(247)) {
                enableNotify(g);
            }
        }

        @Override
        public void onMtuChanged(BluetoothGatt g, int newMtu, int status) {
            mtu = Math.max(23, newMtu);
            enableNotify(g);
        }

        @SuppressLint("MissingPermission")
        private void enableNotify(BluetoothGatt g) {
            if (txChar == null) {
                return;
            }
            try {
                g.setCharacteristicNotification(txChar, true);
                BluetoothGattDescriptor d = txChar.getDescriptor(CCCD);
                if (d != null) {
                    d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                    g.writeDescriptor(d);
                } else {
                    finishConnected();
                }
            } catch (SecurityException e) {
                listener.onError("enable notify: " + e.getMessage());
            }
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor descriptor, int status) {
            if (CCCD.equals(descriptor.getUuid())) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    finishConnected();
                } else {
                    listener.onError("CCCD write failed: " + status);
                }
            }
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic ch, int status) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                writeNext();
            } else {
                writes.clear();
                listener.onError("BLE write failed: " + status);
            }
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic ch, byte[] value) {
            handleNotify(value);
        }

        @SuppressWarnings("deprecation")
        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic ch) {
            handleNotify(ch.getValue());
        }
    };

    private void handleNotify(byte[] value) {
        if (value == null) {
            return;
        }
        rxLine.append(new String(value, StandardCharsets.UTF_8));
            int idx;
            while ((idx = rxLine.indexOf("\n")) >= 0) {
                String line = rxLine.substring(0, idx).trim();
                rxLine.delete(0, idx + 1);
                if (!line.isEmpty()) {
                    main.post(() -> listener.onLine(line));
                }
            }
            if (rxLine.length() > 8192) {
                rxLine.setLength(0);
            }
    }

    private void finishConnected() {
        if (connected) {
            return;
        }
        connected = true;
        final String name = connectedName;
        final String address = connectedAddress;
        main.post(() -> listener.onConnected(name, address));
    }

    // ---------------- write ----------------

    public void send(String line) {
        if (!connected || gatt == null || rxChar == null) {
            listener.onError("BLE not connected");
            return;
        }
        byte[] data = (line + "\n").getBytes(StandardCharsets.UTF_8);
        int chunk = Math.max(20, Math.min(mtu - 3, 180));
        boolean idle = writes.isEmpty();
        for (int i = 0; i < data.length; i += chunk) {
            int n = Math.min(chunk, data.length - i);
            byte[] part = new byte[n];
            System.arraycopy(data, i, part, 0, n);
            writes.add(part);
        }
        if (idle) {
            writeNext();
        }
    }

    @SuppressLint("MissingPermission")
    private void writeNext() {
        if (!connected || gatt == null || rxChar == null) {
            return;
        }
        byte[] next = writes.poll();
        if (next == null) {
            return;
        }
        try {
            rxChar.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
            rxChar.setValue(next);
            if (!gatt.writeCharacteristic(rxChar)) {
                writes.clear();
                listener.onError("writeCharacteristic returned false");
            }
        } catch (SecurityException e) {
            writes.clear();
            listener.onError("BLE write: " + e.getMessage());
        }
    }

    public JSONObject stateJson() throws org.json.JSONException {
        JSONObject o = new JSONObject();
        o.put("connected", connected);
        o.put("name", connectedName);
        o.put("address", connectedAddress);
        o.put("mtu", mtu);
        return o;
    }

    public void close() {
        stopScan();
        disconnect();
    }
}
