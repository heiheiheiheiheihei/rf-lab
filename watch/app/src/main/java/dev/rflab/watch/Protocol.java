package dev.rflab.watch;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * RF-Lab BLE line protocol: parsing and pure selection rules.
 *
 * <p>This class deliberately has no Android imports so the host unit tests can
 * exercise the exact parser that ships in the APK.
 */
public final class Protocol {

    /** Same GATT contract as the phone app and firmware; see watch/README.md §5. */
    public static final String SERVICE_UUID = "5a3d0001-7c1f-4b8e-9a55-52464c414231";
    public static final String RX_UUID = "5a3d0002-7c1f-4b8e-9a55-52464c414231";
    public static final String TX_UUID = "5a3d0003-7c1f-4b8e-9a55-52464c414231";

    private Protocol() {
    }

    public static final class Code {
        public final int id;
        public final String band;
        public final String code;
        public final String name;
        public final String group;
        public final String sym;
        public final boolean manual;
        public final boolean edited;

        public Code(int id, String band, String code, String name, String group, String sym,
                    boolean manual, boolean edited) {
            this.id = id;
            this.band = band == null ? "" : band;
            this.code = code == null ? "" : code;
            this.name = name == null ? "" : name;
            this.group = group == null ? "" : group;
            this.sym = sym == null ? "" : sym;
            this.manual = manual;
            this.edited = edited;
        }

        public String bandLabel() {
            return band.isEmpty() ? "---" : band;
        }

        public String codeLabel() {
            return code.isEmpty() ? "--------" : code;
        }

        public String nameLabel() {
            if (!name.isEmpty()) {
                return name;
            }
            return String.format(java.util.Locale.US, "CODE %02d", id);
        }

        public String metaLabel() {
            StringBuilder b = new StringBuilder();
            if (!group.isEmpty()) {
                b.append(group);
            }
            if (!sym.isEmpty()) {
                if (b.length() > 0) {
                    b.append("  ");
                }
                b.append(sym);
            }
            if (manual) {
                if (b.length() > 0) {
                    b.append("  ");
                }
                b.append("MANUAL");
            } else if (edited) {
                if (b.length() > 0) {
                    b.append("  ");
                }
                b.append("EDITED");
            }
            return b.toString();
        }
    }

    public static final class Hello {
        public final String fw;
        public final String build;
        public final String ui;
        public final String name;
        public final int proto;
        public final int codebook;
        public final boolean auth;

        Hello(String fw, String build, String ui, String name, int proto, int codebook, boolean auth) {
            this.fw = fw;
            this.build = build;
            this.ui = ui;
            this.name = name;
            this.proto = proto;
            this.codebook = codebook;
            this.auth = auth;
        }
    }

    public static final class Auth {
        public final boolean ok;
        public final int left;

        Auth(boolean ok, int left) {
            this.ok = ok;
            this.left = left;
        }
    }

    public static final class State {
        public final boolean active;
        public final String kind;
        public final int id;
        public final boolean ok;
        public final int ms;
        public final int storeCount;
        public final int storeMax;

        State(boolean active, String kind, int id, boolean ok, int ms, int storeCount, int storeMax) {
            this.active = active;
            this.kind = kind == null ? "" : kind;
            this.id = id;
            this.ok = ok;
            this.ms = ms;
            this.storeCount = storeCount;
            this.storeMax = storeMax;
        }
    }

    public static final class Event {
        public final String kind;
        public final String tx;
        public final int id;
        public final boolean ok;
        public final int ms;
        public final String line;

        Event(String kind, String tx, int id, boolean ok, int ms, String line) {
            this.kind = kind == null ? "" : kind;
            this.tx = tx == null ? "" : tx;
            this.id = id;
            this.ok = ok;
            this.ms = ms;
            this.line = line == null ? "" : line;
        }
    }

    public static final class Log {
        public final String line;

        Log(String line) {
            this.line = line == null ? "" : line;
        }
    }

    /** The firmware text listing uses "-" for an empty v2 field; JSON normally omits it. */
    private static String norm(String s) {
        return "-".equals(s) ? "" : s;
    }

    public static JSONObject tryParse(String line) {
        if (line == null || line.isEmpty()) {
            return null;
        }
        try {
            return new JSONObject(line);
        } catch (JSONException e) {
            return null;
        }
    }

    public static Code parseCode(JSONObject o) {
        if (o == null) {
            return null;
        }
        int id = o.optInt("id", 0);
        if (id <= 0) {
            return null;
        }
        return new Code(
                id,
                o.optString("band", ""),
                o.optString("code", ""),
                norm(o.optString("name", "")),
                norm(o.optString("group", "")),
                norm(o.optString("sym", "")),
                o.optInt("manual", 0) != 0,
                o.optInt("edited", 0) != 0);
    }

    /** Parses a {@code {"t":"codes","items":[...]}} frame; bad items are skipped. */
    public static List<Code> parseCodes(JSONObject msg) {
        if (msg == null || !"codes".equals(msg.optString("t", ""))) {
            return Collections.emptyList();
        }
        JSONArray items = msg.optJSONArray("items");
        if (items == null) {
            return Collections.emptyList();
        }
        List<Code> out = new ArrayList<>(items.length());
        for (int i = 0; i < items.length(); i++) {
            JSONObject o = items.optJSONObject(i);
            Code c = parseCode(o);
            if (c != null) {
                out.add(c);
            }
        }
        return out;
    }

    public static Hello parseHello(JSONObject msg) {
        if (msg == null || !"hello".equals(msg.optString("t", ""))) {
            return null;
        }
        return new Hello(
                msg.optString("fw", ""),
                msg.optString("build", ""),
                msg.optString("ui", ""),
                msg.optString("name", ""),
                msg.optInt("proto", 0),
                msg.optInt("codebook", 0),
                msg.optInt("auth", 0) != 0);
    }

    public static Auth parseAuth(JSONObject msg) {
        if (msg == null || !"auth".equals(msg.optString("t", ""))) {
            return null;
        }
        return new Auth(msg.optInt("ok", 0) != 0, msg.optInt("left", 0));
    }

    public static State parseState(JSONObject msg) {
        if (msg == null || !"state".equals(msg.optString("t", ""))) {
            return null;
        }
        JSONObject data = msg.optJSONObject("data");
        if (data == null) {
            data = msg;
        }
        JSONObject tx = data.optJSONObject("tx");
        JSONObject store = data.optJSONObject("store");
        boolean active = tx != null && tx.optInt("active", 0) != 0;
        String kind = tx == null ? "" : tx.optString("kind", "");
        int id = tx == null ? 0 : tx.optInt("id", 0);
        boolean ok = tx != null && tx.optInt("ok", 0) != 0;
        int ms = tx == null ? 0 : tx.optInt("ms", 0);
        int count = store == null ? 0 : store.optInt("count", 0);
        int max = store == null ? 0 : store.optInt("max", 0);
        return new State(active, kind, id, ok, ms, count, max);
    }

    public static Event parseEvent(JSONObject msg) {
        if (msg == null || !"event".equals(msg.optString("t", ""))) {
            return null;
        }
        return new Event(
                msg.optString("kind", ""),
                msg.optString("tx", ""),
                msg.optInt("id", 0),
                msg.optInt("ok", 0) != 0,
                msg.optInt("ms", 0),
                msg.optString("line", ""));
    }

    public static Log parseLog(JSONObject msg) {
        if (msg == null || !"log".equals(msg.optString("t", ""))) {
            return null;
        }
        return new Log(msg.optString("line", ""));
    }

    public static String parseError(JSONObject msg) {
        if (msg == null || !"error".equals(msg.optString("t", ""))) {
            return null;
        }
        return msg.optString("line", "error");
    }

    public static boolean isValidPin(String pin) {
        if (pin == null || pin.length() != 4) {
            return false;
        }
        for (int i = 0; i < pin.length(); i++) {
            if (pin.charAt(i) < '0' || pin.charAt(i) > '9') {
                return false;
            }
        }
        return true;
    }

    public static int indexOfId(List<Code> codes, int id) {
        if (codes == null) {
            return -1;
        }
        for (int i = 0; i < codes.size(); i++) {
            if (codes.get(i).id == id) {
                return i;
            }
        }
        return -1;
    }

    /** Wraps a carousel index; returns -1 for an empty list. */
    public static int step(int index, int delta, int size) {
        if (size <= 0) {
            return -1;
        }
        int next = (index + delta) % size;
        if (next < 0) {
            next += size;
        }
        return next;
    }
}
