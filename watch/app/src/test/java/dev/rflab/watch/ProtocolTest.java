package dev.rflab.watch;

import org.json.JSONObject;
import org.junit.Test;

import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class ProtocolTest {

    @Test
    public void parsesCodebookWithChineseAndEmoji() throws Exception {
        JSONObject msg = new JSONObject("{\"t\":\"codes\",\"seq\":4,\"items\":["
                + "{\"id\":1,\"band\":\"315M\",\"code\":\"5555034C\",\"name\":\"客厅大门\",\"group\":\"家门\",\"sym\":\"\\uD83D\\uDEAA\"},"
                + "{\"id\":2,\"band\":\"433M\",\"code\":\"00000093\",\"manual\":1},"
                + "{\"id\":0,\"band\":\"x\"},"
                + "{\"id\":3}"
                + "]}");
        List<Protocol.Code> codes = Protocol.parseCodes(msg);
        assertEquals(3, codes.size());
        assertEquals(1, codes.get(0).id);
        assertEquals("客厅大门", codes.get(0).name);
        assertEquals("家门", codes.get(0).group);
        assertEquals("🚪", codes.get(0).sym);
        assertTrue(codes.get(1).manual);
        assertEquals("CODE 03", codes.get(2).nameLabel());
    }

    @Test
    public void parsesFull32CodebookAndWraps() throws Exception {
        StringBuilder json = new StringBuilder("{\"t\":\"codes\",\"items\":[");
        for (int i = 1; i <= 32; i++) {
            if (i > 1) {
                json.append(',');
            }
            json.append("{\"id\":").append(i)
                    .append(",\"band\":\"").append(i % 2 == 0 ? "315M" : "433M")
                    .append("\",\"code\":\"5555033C\"}");
        }
        json.append("]}");
        List<Protocol.Code> codes = Protocol.parseCodes(new JSONObject(json.toString()));
        assertEquals(32, codes.size());
        assertEquals(1, codes.get(0).id);
        assertEquals(32, codes.get(31).id);
        assertEquals("CODE 32", codes.get(31).nameLabel());
        assertEquals(0, Protocol.step(31, 1, 32));
    }

    @Test
    public void nonCodesFrameYieldsEmptyList() throws Exception {
        assertTrue(Protocol.parseCodes(new JSONObject("{\"t\":\"state\"}")).isEmpty());
        assertTrue(Protocol.parseCodes(null).isEmpty());
    }

    @Test
    public void parsesHelloAndAuth() throws Exception {
        Protocol.Hello hello = Protocol.parseHello(new JSONObject(
                "{\"t\":\"hello\",\"fw\":\"0.7.1\",\"build\":\"a7b4bf3\",\"ui\":\"85a1a9b4\","
                        + "\"proto\":1,\"codebook\":2,\"name\":\"RF-Lab-AB12\",\"auth\":0}"));
        assertNotNull(hello);
        assertEquals("0.7.1", hello.fw);
        assertEquals("RF-Lab-AB12", hello.name);
        assertEquals(2, hello.codebook);
        assertFalse(hello.auth);

        Protocol.Auth bad = Protocol.parseAuth(new JSONObject("{\"t\":\"auth\",\"ok\":0,\"left\":4}"));
        assertNotNull(bad);
        assertFalse(bad.ok);
        assertEquals(4, bad.left);

        Protocol.Auth good = Protocol.parseAuth(new JSONObject("{\"t\":\"auth\",\"ok\":1}"));
        assertNotNull(good);
        assertTrue(good.ok);
    }

    @Test
    public void parsesTxStateAndEvent() throws Exception {
        Protocol.State state = Protocol.parseState(new JSONObject(
                "{\"t\":\"state\",\"seq\":11,\"data\":{\"tx\":{\"active\":1,\"kind\":\"long\","
                        + "\"id\":2,\"ok\":0,\"ms\":224},\"store\":{\"count\":9,\"max\":32}}}"));
        assertNotNull(state);
        assertTrue(state.active);
        assertEquals("long", state.kind);
        assertEquals(2, state.id);
        assertEquals(9, state.storeCount);
        assertEquals(32, state.storeMax);

        Protocol.Event event = Protocol.parseEvent(new JSONObject(
                "{\"t\":\"event\",\"kind\":\"tx\",\"tx\":\"send\",\"id\":1,"
                        + "\"band\":\"315M\",\"code\":\"5555034C\",\"ok\":1,\"ms\":852,"
                        + "\"line\":\"OK tx id=1\"}"));
        assertNotNull(event);
        assertEquals("tx", event.kind);
        assertEquals("send", event.tx);
        assertEquals(1, event.id);
        assertTrue(event.ok);
        assertEquals(852, event.ms);

        Protocol.Event learn = Protocol.parseEvent(new JSONObject(
                "{\"t\":\"event\",\"kind\":\"learn\",\"id\":6,\"line\":\"LEARN id=6\"}"));
        assertNotNull(learn);
        assertEquals("learn", learn.kind);
        assertEquals(6, learn.id);
    }

    @Test
    public void parsesLogAndError() throws Exception {
        Protocol.Log log = Protocol.parseLog(new JSONObject("{\"t\":\"log\",\"line\":\"OK disp=test 10s\"}"));
        assertNotNull(log);
        assertEquals("OK disp=test 10s", log.line);

        assertEquals("AUTH required",
                Protocol.parseError(new JSONObject("{\"t\":\"error\",\"line\":\"AUTH required\"}")));
    }

    @Test
    public void malformedFrameIsRejectedWithoutThrowing() {
        assertNull(Protocol.tryParse("not json"));
        assertNull(Protocol.tryParse(""));
        assertNotNull(Protocol.tryParse("{\"t\":\"log\",\"line\":\"OK\"}"));
    }

    @Test
    public void pinValidationIsStrict() {
        assertTrue(Protocol.isValidPin("0123"));
        assertFalse(Protocol.isValidPin("123"));
        assertFalse(Protocol.isValidPin("12345"));
        assertFalse(Protocol.isValidPin("12a4"));
        assertFalse(Protocol.isValidPin(null));
    }

    @Test
    public void selectionWrapsAndFindsIds() throws Exception {
        List<Protocol.Code> codes = Protocol.parseCodes(new JSONObject(
                "{\"t\":\"codes\",\"items\":[{\"id\":4},{\"id\":7},{\"id\":9}]}"));
        assertEquals(0, Protocol.indexOfId(codes, 4));
        assertEquals(2, Protocol.indexOfId(codes, 9));
        assertEquals(-1, Protocol.indexOfId(codes, 8));
        assertEquals(1, Protocol.step(0, 1, 3));
        assertEquals(0, Protocol.step(2, 1, 3));
        assertEquals(2, Protocol.step(0, -1, 3));
        assertEquals(-1, Protocol.step(0, 1, 0));
    }
}
