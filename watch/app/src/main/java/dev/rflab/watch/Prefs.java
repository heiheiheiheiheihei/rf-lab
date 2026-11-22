package dev.rflab.watch;

import android.content.Context;
import android.content.SharedPreferences;

/** Local convenience state only: last device, remembered PIN, last selected code. */
final class Prefs {
    private static final String FILE = "rflab_watch";
    private static final String LAST_ADDR = "last_addr";
    private static final String LAST_NAME = "last_name";
    private static final String LAST_ID = "last_id";
    private static final String PIN_PREFIX = "pin_";

    private final SharedPreferences sp;

    Prefs(Context context) {
        sp = context.getApplicationContext().getSharedPreferences(FILE, Context.MODE_PRIVATE);
    }

    String lastAddress() {
        return sp.getString(LAST_ADDR, "");
    }

    String lastName() {
        return sp.getString(LAST_NAME, "");
    }

    String pinFor(String address) {
        if (address == null || address.isEmpty()) {
            return "";
        }
        return sp.getString(PIN_PREFIX + address, "");
    }

    void rememberDevice(String address, String name) {
        sp.edit().putString(LAST_ADDR, address == null ? "" : address)
                .putString(LAST_NAME, name == null ? "" : name)
                .apply();
    }

    void rememberPin(String address, String pin) {
        if (address == null || address.isEmpty() || !Protocol.isValidPin(pin)) {
            return;
        }
        sp.edit().putString(PIN_PREFIX + address, pin).apply();
    }

    void forgetPin(String address) {
        if (address == null || address.isEmpty()) {
            return;
        }
        sp.edit().remove(PIN_PREFIX + address).apply();
    }

    void setLastSelectedId(int id) {
        sp.edit().putInt(LAST_ID, id).apply();
    }

    int lastSelectedId() {
        return sp.getInt(LAST_ID, -1);
    }

    void forgetAll() {
        sp.edit().clear().apply();
    }
}
