package dev.rflab.watch;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.Locale;

/**
 * Pure parsing of the public GitHub Releases feed for the watch APK.
 *
 * <p>Only stable {@code watch-vX.Y.Z} releases that carry both
 * {@value #APK_ASSET} and {@value #SUMS_ASSET} are installable. A phone-only or
 * firmware-only release can never be mistaken for a watch build.
 */
final class UpdateFeed {
    static final String TAG_PREFIX = "watch-v";
    static final String APK_ASSET = "rf-lab-watch.apk";
    static final String SUMS_ASSET = "SHA256SUMS.txt";
    static final long MAX_APK_BYTES = 20L * 1024 * 1024;

    static final class Release {
        final String version;
        final String tag;
        final String apkUrl;
        final String sumsUrl;
        final long size;

        Release(String version, String tag, String apkUrl, String sumsUrl, long size) {
            this.version = version;
            this.tag = tag;
            this.apkUrl = apkUrl;
            this.sumsUrl = sumsUrl;
            this.size = size;
        }
    }

    private UpdateFeed() {
    }

    /** Highest stable watch release newer than {@code current}; null when up to date. */
    static Release latest(String json, String current) throws JSONException {
        JSONArray list = new JSONArray(json);
        Release best = null;
        for (int i = 0; i < list.length(); i++) {
            JSONObject o = list.optJSONObject(i);
            if (o == null || o.optBoolean("draft") || o.optBoolean("prerelease")) {
                continue;
            }
            String tag = o.optString("tag_name", "");
            if (!tag.startsWith(TAG_PREFIX)) {
                continue;
            }
            String version = AppVersion.fromTag(tag);
            if (version == null) {
                continue;
            }
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
                if (APK_ASSET.equals(name)) {
                    apkUrl = url;
                    size = asset.optLong("size");
                } else if (SUMS_ASSET.equals(name)) {
                    sumsUrl = url;
                }
            }
            if (apkUrl == null || sumsUrl == null || size <= 0 || size > MAX_APK_BYTES) {
                continue;
            }
            if (AppVersion.isNewer(version, current)
                    && (best == null || AppVersion.compare(version, best.version) > 0)) {
                best = new Release(version, tag, apkUrl, sumsUrl, size);
            }
        }
        return best;
    }

    /** SHA-256 for {@code apkName} from a sha256sum file; null when missing/malformed. */
    static String hashFor(String sums, String apkName) {
        if (sums == null || apkName == null) {
            return null;
        }
        for (String raw : sums.split("\n")) {
            String line = raw.trim();
            int sp = line.indexOf(' ');
            if (sp != 64 || line.length() <= sp + 1) {
                continue;
            }
            String hex = line.substring(0, 64);
            if (!isHex(hex)) {
                continue;
            }
            String name = line.substring(sp).trim();
            if (name.startsWith("*")) {
                name = name.substring(1);
            }
            if (apkName.equals(name)) {
                return hex.toLowerCase(Locale.US);
            }
        }
        return null;
    }

    private static boolean isHex(String s) {
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            boolean ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                    || (c >= 'A' && c <= 'F');
            if (!ok) {
                return false;
            }
        }
        return !s.isEmpty();
    }
}
