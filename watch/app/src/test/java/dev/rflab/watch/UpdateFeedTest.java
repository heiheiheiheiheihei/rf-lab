package dev.rflab.watch;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

public class UpdateFeedTest {

    private static final String FEED = "["
            + "{\"tag_name\":\"v0.7.2\",\"draft\":false,\"prerelease\":false,\"assets\":["
            + "{\"name\":\"rf-lab-android.apk\",\"size\":1050176,"
            + "\"browser_download_url\":\"https://example.test/phone.apk\"},"
            + "{\"name\":\"SHA256SUMS.txt\",\"size\":120,"
            + "\"browser_download_url\":\"https://example.test/phone-sums.txt\"}]},"
            + "{\"tag_name\":\"watch-v0.1.0\",\"draft\":false,\"prerelease\":false,\"assets\":["
            + "{\"name\":\"rf-lab-watch.apk\",\"size\":48244,"
            + "\"browser_download_url\":\"https://example.test/w010.apk\"},"
            + "{\"name\":\"SHA256SUMS.txt\",\"size\":90,"
            + "\"browser_download_url\":\"https://example.test/w010-sums.txt\"}]},"
            + "{\"tag_name\":\"watch-v0.1.1\",\"draft\":false,\"prerelease\":false,\"assets\":["
            + "{\"name\":\"rf-lab-watch.apk\",\"size\":51000,"
            + "\"browser_download_url\":\"https://example.test/w011.apk\"},"
            + "{\"name\":\"SHA256SUMS.txt\",\"size\":90,"
            + "\"browser_download_url\":\"https://example.test/w011-sums.txt\"}]},"
            + "{\"tag_name\":\"watch-v0.1.2\",\"draft\":true,\"prerelease\":false,\"assets\":["
            + "{\"name\":\"rf-lab-watch.apk\",\"size\":51000,"
            + "\"browser_download_url\":\"https://example.test/draft.apk\"}]},"
            + "{\"tag_name\":\"watch-v0.1.3\",\"draft\":false,\"prerelease\":true,\"assets\":["
            + "{\"name\":\"rf-lab-watch.apk\",\"size\":51000,"
            + "\"browser_download_url\":\"https://example.test/pre.apk\"}]},"
            + "{\"tag_name\":\"watch-v0.1.4\",\"draft\":false,\"prerelease\":false,\"assets\":["
            + "{\"name\":\"rf-lab-watch.apk\",\"size\":51000,"
            + "\"browser_download_url\":\"https://example.test/nosums.apk\"}]}"
            + "]";

    @Test
    public void picksHighestStableWatchReleaseWithSums() throws Exception {
        UpdateFeed.Release r = UpdateFeed.latest(FEED, "0.1.0");
        assertNotNull(r);
        assertEquals("0.1.1", r.version);
        assertEquals("watch-v0.1.1", r.tag);
        assertEquals("https://example.test/w011.apk", r.apkUrl);
        assertEquals("https://example.test/w011-sums.txt", r.sumsUrl);
        assertEquals(51000, r.size);
    }

    @Test
    public void returnsNullWhenCurrentIsLatestOrNewer() throws Exception {
        assertNull(UpdateFeed.latest(FEED, "0.1.1"));
        assertNull(UpdateFeed.latest(FEED, "0.2.0"));
    }

    @Test
    public void parsesSha256SumsVariants() {
        String hex = "739afa68e675bbca0f4452d66389eaa7d33babb46842d0d4f94886ca55d6ba1d";
        assertEquals(hex, UpdateFeed.hashFor(hex + "  rf-lab-watch.apk\n", "rf-lab-watch.apk"));
        assertEquals(hex, UpdateFeed.hashFor(hex + " *rf-lab-watch.apk\n", "rf-lab-watch.apk"));
        assertNull(UpdateFeed.hashFor(hex + "  other.apk\n", "rf-lab-watch.apk"));
        assertNull(UpdateFeed.hashFor("not-a-hash  rf-lab-watch.apk\n", "rf-lab-watch.apk"));
        assertNull(UpdateFeed.hashFor("", "rf-lab-watch.apk"));
    }

    @Test
    public void rejectsWatchReleaseWithoutSumsOrWithHugeAsset() throws Exception {
        String json = "[{\"tag_name\":\"watch-v0.2.0\",\"assets\":["
                + "{\"name\":\"rf-lab-watch.apk\",\"size\":999999999,"
                + "\"browser_download_url\":\"https://example.test/x.apk\"},"
                + "{\"name\":\"SHA256SUMS.txt\",\"size\":90,"
                + "\"browser_download_url\":\"https://example.test/x.txt\"}]}]";
        assertNull(UpdateFeed.latest(json, "0.1.0"));
    }
}
