package dev.rflab.watch;

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
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;

import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

/**
 * Minimal BLE central for RF-Lab on Android 8.1.
 *
 * <p>Threading: scan/GATT callbacks arrive on a binder thread. Every listener
 * callback is posted to the main thread. Writes are queued and issued one at a
 * time; notifications are re-assembled into {@code \n}-terminated lines.
 */
public final class BleClient {
    public static final UUID SERVICE = UUID.fromString(Protocol.SERVICE_UUID);
    public static final UUID RX = UUID.fromString(Protocol.RX_UUID);
    public static final UUID TX = UUID.fromString(Protocol.TX_UUID);
    private static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final long SCAN_MS = 12_000L;

    public interface Listener {
        void onScan(List<Device> devices);

        void onScanStopped();

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
            this.name = name == null ? "" : name;
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
    private volatile int mtu = 23;
    private volatile boolean connected;
    private boolean scanning;
    private boolean notifyEnabled;
    private int scanGen;
    private long lastEmit;
    private String connectedName = "";
    private String connectedAddress = "";

    public BleClient(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
        BluetoothManager manager =
                (BluetoothManager) this.context.getSystemService(Context.BLUETOOTH_SERVICE);
        if (manager != null) {
            adapter = manager.getAdapter();
        }
    }

    public boolean isEnabled() {
        return adapter != null && adapter.isEnabled();
    }

    public static String[] requiredPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return new String[]{Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT};
        }
        return new String[]{Manifest.permission.BLUETOOTH,
                Manifest.permission.BLUETOOTH_ADMIN,
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
            stopScan();
        }
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) {
            listener.onError("BLE scanner unavailable");
            return;
        }

        found.clear();
        scanning = true;
        lastEmit = 0;
        main.post(() -> listener.onScan(new ArrayList<>()));

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
                main.post(() -> {
                    listener.onError("BLE scan failed: " + errorCode);
                    listener.onScanStopped();
                });
            }
        };

        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();
        final int gen = ++scanGen;
        try {
            // No service filter: the 128-bit UUID is in the scan response and
            // some stacks hide it from the primary advertisement.
            scanner.startScan(null, settings, scanCallback);
        } catch (SecurityException e) {
            scanning = false;
            listener.onError("BLE scan permission: " + e.getMessage());
            return;
        }
        main.postDelayed(() -> {
            if (gen == scanGen) {
                stopScan();
            }
        }, SCAN_MS);
    }

    @SuppressLint("MissingPermission")
    private void handleResult(ScanResult result) {
        if (result == null || result.getDevice() == null) {
            return;
        }
        BluetoothDevice d = result.getDevice();
        ScanRecord record = result.getScanRecord();
        String name = record == null ? null : record.getDeviceName();
        if (name == null || name.isEmpty()) {
            try {
                name = d.getName();
            } catch (SecurityException ignored) {
            }
        }
        boolean match = name != null && name.startsWith("RF-Lab-");
        if (!match && record != null) {
            List<ParcelUuid> uuids = record.getServiceUuids();
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
        String safeName = name == null ? "RF-Lab" : name;
        found.put(addr, new Device(safeName, addr, result.getRssi()));
        long now = System.currentTimeMillis();
        if (now - lastEmit < 500) {
            return;
        }
        lastEmit = now;
        List<Device> list = new ArrayList<>(found.values());
        main.post(() -> listener.onScan(list));
    }

    @SuppressLint("MissingPermission")
    public void stopScan() {
        scanGen++;
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
        scanCallback = null;
        main.post(listener::onScanStopped);
    }

    public boolean isScanning() {
        return scanning;
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
        if (address == null || address.isEmpty()) {
            listener.onError("No device selected");
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
        try {
            gatt = device.connectGatt(context, false, gattCallback,
                    BluetoothDevice.TRANSPORT_LE);
        } catch (SecurityException e) {
            listener.onError("connectGatt: " + e.getMessage());
            return;
        }
        if (gatt == null) {
            listener.onError("connectGatt failed");
        }
    }

    @SuppressLint("MissingPermission")
    public void disconnect() {
        connected = false;
        notifyEnabled = false;
        writes.clear();
        rxLine.setLength(0);
        mtu = 23;
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
    }

    public boolean isConnected() {
        return connected;
    }

    public int mtu() {
        return mtu;
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int status, int newState) {
            // A callback from a previous gatt can arrive after a reconnect.
            if (g != gatt) {
                return;
            }
            if (status != BluetoothGatt.GATT_SUCCESS
                    && newState != BluetoothProfile.STATE_CONNECTED) {
                main.post(() -> listener.onError("GATT status " + status));
            }
            if (newState == BluetoothProfile.STATE_CONNECTED
                    && status == BluetoothGatt.GATT_SUCCESS) {
                try {
                    g.discoverServices();
                } catch (SecurityException e) {
                    main.post(() -> listener.onError("discoverServices: " + e.getMessage()));
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                connected = false;
                notifyEnabled = false;
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
                main.post(() -> listener.onError("service discovery failed: " + status));
                return;
            }
            BluetoothGattService svc = g.getService(SERVICE);
            if (svc == null) {
                main.post(() -> listener.onError("RF-Lab service not found"));
                return;
            }
            rxChar = svc.getCharacteristic(RX);
            txChar = svc.getCharacteristic(TX);
            if (rxChar == null || txChar == null) {
                main.post(() -> listener.onError("RF-Lab characteristics missing"));
                return;
            }
            try {
                if (!g.requestMtu(247)) {
                    enableNotify(g);
                } else {
                    // Some Android 8.x stacks never deliver onMtuChanged.
                    main.postDelayed(() -> {
                        if (gatt == g && !connected) {
                            enableNotify(g);
                        }
                    }, 1500);
                }
            } catch (SecurityException e) {
                main.post(() -> listener.onError("requestMtu: " + e.getMessage()));
            }
        }

        @Override
        public void onMtuChanged(BluetoothGatt g, int newMtu, int status) {
            mtu = Math.max(23, newMtu);
            enableNotify(g);
        }

        @SuppressLint("MissingPermission")
        private void enableNotify(BluetoothGatt g) {
            if (txChar == null || notifyEnabled) {
                return;
            }
            try {
                if (!g.setCharacteristicNotification(txChar, true)) {
                    main.post(() -> listener.onError("setCharacteristicNotification failed"));
                    return;
                }
                BluetoothGattDescriptor d = txChar.getDescriptor(CCCD);
                if (d == null) {
                    finishConnected();
                    return;
                }
                d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                if (!g.writeDescriptor(d)) {
                    main.post(() -> listener.onError("CCCD write returned false"));
                }
            } catch (SecurityException e) {
                main.post(() -> listener.onError("enable notify: " + e.getMessage()));
            }
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor descriptor, int status) {
            if (descriptor != null && CCCD.equals(descriptor.getUuid())) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    finishConnected();
                } else {
                    main.post(() -> listener.onError("CCCD write failed: " + status));
                }
            }
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic ch, int status) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                writeNext();
            } else {
                writes.clear();
                main.post(() -> listener.onError("BLE write failed: " + status));
            }
        }

        // Only the legacy overload is overridden. On API 33+ the new overload's
        // default implementation calls this one; overriding both would append
        // every notification twice.
        @SuppressWarnings("deprecation")
        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic ch) {
            handleNotify(ch.getValue());
        }
    };

    private void handleNotify(byte[] value) {
        if (value == null || value.length == 0) {
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
        if (connected || notifyEnabled) {
            return;
        }
        notifyEnabled = true;
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
                main.post(() -> listener.onError("writeCharacteristic returned false"));
            }
        } catch (SecurityException e) {
            writes.clear();
            main.post(() -> listener.onError("BLE write: " + e.getMessage()));
        }
    }

    public void close() {
        stopScan();
        disconnect();
    }
}
