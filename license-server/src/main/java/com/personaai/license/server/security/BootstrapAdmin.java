package com.personaai.license.server.security;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.stereotype.Component;

import com.personaai.license.server.audit.AuditService;
import com.personaai.license.server.config.LicenseProperties;
import com.personaai.license.server.user.AppUser;
import com.personaai.license.server.user.UserRepository;
import com.personaai.license.server.user.UserService;

/** 사용자가 한 명도 없을 때만, secret 파일의 초기 비밀번호로 관리자 1명을 만든다(첫 로그인 때 변경 강제). */
@Component
public class BootstrapAdmin implements ApplicationRunner {
    private static final Logger log = LoggerFactory.getLogger(BootstrapAdmin.class);

    private final UserRepository users;
    private final UserService userService;
    private final AuditService audit;
    private final LicenseProperties props;

    public BootstrapAdmin(UserRepository users, UserService userService, AuditService audit, LicenseProperties props) {
        this.users = users;
        this.userService = userService;
        this.audit = audit;
        this.props = props;
    }

    @Override
    public void run(ApplicationArguments args) throws Exception {
        if (users.count() > 0) return;
        var file = props.bootstrap().passwordFile();
        if (!Files.isRegularFile(file)) {
            log.warn("no users exist and bootstrap password file {} is missing; nobody can log in", file);
            return;
        }
        String password = Files.readString(file, StandardCharsets.UTF_8).strip();
        String username = props.bootstrap().adminUsername();
        userService.create(username, AppUser.Role.ADMIN, password);
        audit.record("system", "USER_BOOTSTRAP", username, null, Map.of("role", "ADMIN"));
        log.info("bootstrap admin '{}' created; password change is required at first login", username);
    }
}
