package com.personaai.license.server.license;

import java.util.Map;

import org.springframework.stereotype.Service;

import com.personaai.license.server.key.KeyVault;
import com.personaai.license.server.licensectl.Licensectl;

/** 고객 문의 대응용 검증: 업로드된 license.lic 를 이 서버의 공개키로 확인한다. 봉인 상태와 무관(공개키만 사용). */
@Service
public class VerifyService {
    private final KeyVault vault;
    private final Licensectl licensectl;

    public VerifyService(KeyVault vault, Licensectl licensectl) {
        this.vault = vault;
        this.licensectl = licensectl;
    }

    public Map<String, Object> verify(byte[] license) {
        String pub = vault.publicKey();
        if (pub == null) return Map.of("result", "KEY_MISSING", "detail", "sealed key file is not loaded");
        if (license.length == 0) return Map.of("result", "EMPTY", "detail", "no license content");
        return licensectl.verify(pub, license);
    }
}
