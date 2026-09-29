package com.personaai.license.server.audit;

import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import org.springframework.jdbc.core.simple.JdbcClient;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Propagation;
import org.springframework.transaction.annotation.Transactional;

import com.personaai.license.server.crypto.Crypto;

import tools.jackson.databind.json.JsonMapper;

/**
 * 추가만 가능한 감사 로그 + 해시 체인.
 * hash = SHA-256(prev_hash + "\n" + canonical), canonical = [seq, at, actor, action, target, client_ip, detail] 의 JSON.
 * 순서 보장은 advisory lock 으로 한다(앱 계정은 audit_event 에 UPDATE 권한이 없어 SELECT ... FOR UPDATE 를 쓸 수 없다).
 */
@Service
public class AuditService {
    public static final String GENESIS = "0".repeat(64);
    private static final long LOCK_KEY = 0x4C4943454E534541L; // "LICENSEA"
    private static final DateTimeFormatter AT = DateTimeFormatter.ofPattern("uuuu-MM-dd'T'HH:mm:ss.SSSSSS'Z'");
    private static final JsonMapper JSON = JsonMapper.builder().build();

    private final JdbcClient jdbc;

    public AuditService(JdbcClient jdbc) {
        this.jdbc = jdbc;
    }

    public record Entry(long seq, Instant at, String actor, String action, String target, String clientIp,
                        String detailJson, String prevHash, String hash) {}

    public record VerifyResult(boolean ok, long checked, Long brokenAtSeq, String reason, String lastHash) {}

    /** 호출 트랜잭션이 롤백돼도(예: 발급 실패) 기록은 남도록 별도 트랜잭션으로 쓴다. */
    @Transactional(propagation = Propagation.REQUIRES_NEW)
    public void record(String actor, String action, String target, String clientIp, Map<String, ?> detail) {
        jdbc.sql("SELECT pg_advisory_xact_lock(?)").param(LOCK_KEY).query(Object.class).single();
        String prev = jdbc.sql("SELECT hash FROM audit_event ORDER BY seq DESC LIMIT 1")
                .query(String.class).optional().orElse(GENESIS);
        long seq = jdbc.sql("SELECT nextval('audit_event_seq')").query(Long.class).single();
        Instant at = Instant.now().truncatedTo(ChronoUnit.MICROS); // PostgreSQL 저장 정밀도에 맞춤
        String detailJson = canonicalDetail(detail);
        String hash = hash(prev, seq, at, actor, action, target, clientIp, detailJson);
        jdbc.sql("""
                INSERT INTO audit_event (seq, at, actor, action, target, client_ip, detail_json, prev_hash, hash)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)""")
                .params(seq, OffsetDateTime.ofInstant(at, ZoneOffset.UTC), actor, action, target, clientIp,
                        detailJson, prev, hash)
                .update();
    }

    public List<Entry> recent(int limit, String actionFilter) {
        String where = actionFilter == null || actionFilter.isBlank() ? "" : "WHERE action = :action ";
        var spec = jdbc.sql("SELECT * FROM audit_event " + where + "ORDER BY seq DESC LIMIT :limit")
                .param("limit", limit);
        if (!where.isEmpty()) spec = spec.param("action", actionFilter);
        return spec.query((rs, i) -> new Entry(rs.getLong("seq"), rs.getObject("at", OffsetDateTime.class).toInstant(),
                rs.getString("actor"), rs.getString("action"), rs.getString("target"), rs.getString("client_ip"),
                rs.getString("detail_json"), rs.getString("prev_hash"), rs.getString("hash"))).list();
    }

    /** 처음부터 다시 계산해 끊긴 지점을 찾는다. */
    @Transactional(readOnly = true)
    public VerifyResult verifyChain() {
        List<Entry> all = new ArrayList<>();
        jdbc.sql("SELECT * FROM audit_event ORDER BY seq").query(rs -> {
            all.add(new Entry(rs.getLong("seq"), rs.getObject("at", OffsetDateTime.class).toInstant(),
                    rs.getString("actor"), rs.getString("action"), rs.getString("target"), rs.getString("client_ip"),
                    rs.getString("detail_json"), rs.getString("prev_hash"), rs.getString("hash")));
        });
        String prev = GENESIS;
        for (Entry e : all) {
            if (!e.prevHash().equals(prev)) {
                return new VerifyResult(false, all.size(), e.seq(), "prev_hash does not link to previous row", prev);
            }
            String expect = hash(prev, e.seq(), e.at(), e.actor(), e.action(), e.target(), e.clientIp(), e.detailJson());
            if (!expect.equals(e.hash())) {
                return new VerifyResult(false, all.size(), e.seq(), "row content does not match its hash", prev);
            }
            prev = e.hash();
        }
        return new VerifyResult(true, all.size(), null, null, prev);
    }

    static String hash(String prev, long seq, Instant at, String actor, String action, String target,
                       String clientIp, String detailJson) {
        List<Object> fields = new ArrayList<>();
        fields.add(seq);
        fields.add(AT.format(at.atOffset(ZoneOffset.UTC)));
        fields.add(actor);
        fields.add(action);
        fields.add(target);
        fields.add(clientIp);
        fields.add(detailJson);
        return Crypto.sha256Hex(prev + "\n" + JSON.writeValueAsString(fields));
    }

    static String canonicalDetail(Map<String, ?> detail) {
        return JSON.writeValueAsString(detail == null ? Map.of() : new TreeMap<>(detail));
    }
}
