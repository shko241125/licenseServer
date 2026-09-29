package com.personaai.license.server.license;

import java.nio.charset.StandardCharsets;
import java.time.Clock;
import java.util.HexFormat;
import java.util.Map;
import java.util.Objects;

import org.springframework.stereotype.Service;

import com.personaai.license.server.audit.AuditService;
import com.personaai.license.server.config.LicenseProperties;
import com.personaai.license.server.crypto.Crypto;
import com.personaai.license.server.key.KeyVault;
import com.personaai.license.server.licensectl.Licensectl;
import com.personaai.license.server.licensectl.Licensectl.LicensectlException;
import com.personaai.license.server.security.AppPrincipal;

/**
 * 발급: 검증 → PENDING 기록(제출 토큰으로 이중 제출 차단) → licensectl sign → 공개키로 재검증 → ISSUED.
 * 각 단계는 자동 커밋이다(서명 중 장애가 나도 PENDING/FAILED 로 흔적이 남도록).
 */
@Service
public class IssueService {
    private final LicenseRepository licenses;
    private final KeyVault vault;
    private final Licensectl licensectl;
    private final AuditService audit;
    private final LicenseProperties props;
    private final Clock clock;

    public IssueService(LicenseRepository licenses, KeyVault vault, Licensectl licensectl, AuditService audit,
                        LicenseProperties props, Clock clock) {
        this.licenses = licenses;
        this.vault = vault;
        this.licensectl = licensectl;
        this.audit = audit;
        this.props = props;
        this.clock = clock;
    }

    public static class IssueException extends RuntimeException {
        public IssueException(String message) {
            super(message);
        }
    }

    public ContractValidator.Result validate(IssueForm form) {
        ContractValidator.Result r = ContractValidator.validate(form, props.zone(), clock.instant());
        if (r.ok() && r.contract().renewedFrom() != null && licenses.findByLicenseId(r.contract().renewedFrom()).isEmpty()) {
            r.errors().put("renewedFrom", "원본 라이선스를 찾을 수 없습니다.");
        }
        return r;
    }

    public LicenseRecord issue(IssueForm form, AppPrincipal by, String ip) {
        ContractValidator.Result v = validate(form);
        if (!v.ok()) throw new IssueException("입력값을 확인하세요: " + String.join(", ", v.errors().values()));
        if (vault.state() != KeyVault.State.UNSEALED) throw new IssueException("서명 키가 봉인 상태입니다. 관리자가 키를 해제해야 발급할 수 있습니다.");
        Contract c = v.contract();
        String licenseId = HexFormat.of().formatHex(Crypto.randomBytes(16));
        String publicKey = Objects.requireNonNull(vault.publicKey());

        if (!licenses.insertPending(licenseId, form.getSubmissionToken(), c, vault.publicKeyFingerprint(), by.id())) {
            return licenses.findByToken(form.getSubmissionToken()).orElseThrow(); // 이미 제출된 폼(새로고침·재전송)
        }
        try {
            String text = vault.withPrivateKey(key -> licensectl.sign(key, c.toJson(licenseId)));
            Map<String, Object> check = licensectl.verify(publicKey, text.getBytes(StandardCharsets.UTF_8));
            if (!"OK".equals(check.get("result")) || !licenseId.equals(check.get("license_id"))) {
                throw new LicensectlException("VERIFY_MISMATCH", "issued license failed public-key verification: " + check);
            }
            licenses.markIssued(licenseId, text, Crypto.sha256Hex(text));
            Map<String, Object> detail = new java.util.HashMap<>(Map.of(
                    "project_name", c.projectName(), "site_id", c.siteId(), "license_type", c.licenseType(),
                    "online_stt", c.onlineStt(), "offline_stt", c.offlineStt(), "not_after", Contract.utc(c.notAfter())));
            if (c.renewedFrom() != null) detail.put("renewed_from", c.renewedFrom()); // 없으면 키를 넣지 않는다("null" 문자열 방지)
            audit.record(by.getUsername(), "ISSUE", licenseId, ip, detail);
        } catch (LicensectlException | IllegalStateException e) {
            String name = e instanceof LicensectlException le ? le.errorName() : "KEY_SEALED";
            licenses.markFailed(licenseId, name + ": " + e.getMessage());
            audit.record(by.getUsername(), "ISSUE_FAILED", licenseId, ip, Map.of("error", name, "detail", String.valueOf(e.getMessage())));
            throw new IssueException("발급 실패 (" + name + "): " + e.getMessage());
        }
        return licenses.findByLicenseId(licenseId).orElseThrow();
    }

    public boolean voidLicense(String licenseId, String reason, AppPrincipal by, String ip) {
        if (reason == null || reason.isBlank() || reason.length() > 500) throw new IssueException("무효 사유(1~500자)를 입력하세요.");
        boolean ok = licenses.markVoid(licenseId, reason.strip(), by.id(), clock.instant());
        if (ok) audit.record(by.getUsername(), "VOID", licenseId, ip, Map.of("reason", reason.strip()));
        return ok;
    }
}
