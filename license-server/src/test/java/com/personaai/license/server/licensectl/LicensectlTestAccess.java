package com.personaai.license.server.licensectl;

import java.nio.file.Path;
import java.time.Duration;

/** 다른 테스트 패키지에서 package-private 생성자를 쓰기 위한 통로. */
public final class LicensectlTestAccess {
    private LicensectlTestAccess() {}

    public static Licensectl create(Path executable, Duration timeout) {
        return new Licensectl(executable, timeout);
    }
}
