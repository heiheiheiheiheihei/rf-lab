package dev.rflab.watch;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class AppVersionTest {

    @Test
    public void parsesWatchTags() {
        assertEquals("0.1.2", AppVersion.fromTag("watch-v0.1.2"));
        assertEquals("0.1.2", AppVersion.fromTag("v0.1.2"));
        assertEquals("0.1.2", AppVersion.fromTag("0.1.2"));
        assertNull(AppVersion.fromTag("watch-v0.1"));
        assertNull(AppVersion.fromTag("watch-v0.1.2.3"));
        assertNull(AppVersion.fromTag("watch-vX.Y.Z"));
        assertNull(AppVersion.fromTag("watch-v0.1.2-rc1"));
        assertNull(AppVersion.fromTag(null));
    }

    @Test
    public void comparesNumericallyNotLexically() {
        assertTrue(AppVersion.isNewer("0.1.10", "0.1.9"));
        assertTrue(AppVersion.isNewer("0.2.0", "0.1.99"));
        assertTrue(AppVersion.isNewer("1.0.0", "0.9.9"));
        assertFalse(AppVersion.isNewer("0.1.2", "0.1.2"));
        assertFalse(AppVersion.isNewer("0.1.2", "0.1.10"));
        assertEquals(0, AppVersion.compare("0.1.2", "v0.1.2"));
    }

    @Test
    public void rejectsMalformedVersions() {
        assertEquals(0, AppVersion.compare("0.1", "0.1.0"));
        assertEquals(0, AppVersion.compare("x", "0.1.0"));
        assertNull(AppVersion.normalize("0.1"));
        assertEquals("0.1.2", AppVersion.normalize("0.01.002"));
    }
}
