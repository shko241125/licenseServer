package com.personaai.license.server.web;

import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.security.test.web.servlet.request.SecurityMockMvcRequestPostProcessors.csrf;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.multipart;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.header;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.redirectedUrl;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import java.time.Duration;
import java.util.UUID;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.webmvc.test.autoconfigure.AutoConfigureMockMvc;
import org.springframework.mock.web.MockHttpSession;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.MvcResult;

import com.personaai.license.server.IntegrationTestBase;
import com.personaai.license.server.MutableClock;
import com.personaai.license.server.TestFixture;
import com.personaai.license.server.crypto.Base32;
import com.personaai.license.server.key.KeyVault;
import com.personaai.license.server.security.Totp;
import com.personaai.license.server.user.AppUser;
import com.personaai.license.server.user.UserRepository;
import com.personaai.license.server.user.UserService;

@AutoConfigureMockMvc
class WebSecurityIT extends IntegrationTestBase {
    static final String PW = "Initial-Password-2026";
    static final String PW2 = "Changed-Password-2026";

    @Autowired MockMvc mvc;
    @Autowired UserService userService;
    @Autowired UserRepository users;
    @Autowired MutableClock clock;
    @Autowired KeyVault vault;

    /** 완전히 설정된 사용자(비밀번호 변경·OTP 등록 완료)를 만들고 OTP 비밀값을 돌려준다. */
    String readyUser(String name, AppUser.Role role) {
        userService.create(name, role, PW);
        userService.changePassword(users.findByUsername(name).orElseThrow(), PW, PW2);
        String secret = UserService.newTotpSecret();
        assertThat(userService.enrollTotp(users.findByUsername(name).orElseThrow(), secret, code(secret))).isTrue();
        return secret;
    }

    /** 다음 30초 구간으로 시계를 옮기고 그 구간 코드를 만든다(같은 구간 재사용은 서버가 거부). */
    String nextCode(String secret) {
        clock.advance(Duration.ofSeconds(Totp.PERIOD_SECONDS));
        return code(secret);
    }

    String code(String secret) {
        return Totp.code(Base32.decode(secret), Totp.step(clock.instant()), 6);
    }

    MockHttpSession login(String name, String password, String secret) throws Exception {
        MvcResult r = mvc.perform(post("/login").param("username", name).param("password", password).with(csrf()))
                .andExpect(status().is3xxRedirection()).andExpect(redirectedUrl("/mfa")).andReturn();
        MockHttpSession s = (MockHttpSession) r.getRequest().getSession();
        mvc.perform(post("/mfa").param("code", nextCode(secret)).session(s).with(csrf()))
                .andExpect(redirectedUrl("/"));
        return s;
    }

    @Test
    void anonymousIsRedirectedAndHeadersAreSet() throws Exception {
        mvc.perform(get("/")).andExpect(status().is3xxRedirection()).andExpect(redirectedUrl("/login"));
        mvc.perform(get("/login").secure(true))
                .andExpect(status().isOk())
                .andExpect(header().string("Content-Security-Policy", org.hamcrest.Matchers.containsString("frame-ancestors 'none'")))
                .andExpect(header().string("X-Frame-Options", "DENY"))
                .andExpect(header().string("X-Content-Type-Options", "nosniff"))
                .andExpect(header().string("Referrer-Policy", "no-referrer"))
                .andExpect(header().string("Strict-Transport-Security", org.hamcrest.Matchers.containsString("max-age=31536000")))
                .andExpect(header().string("Cache-Control", org.hamcrest.Matchers.containsString("no-store")));
        mvc.perform(post("/login").param("username", "x").param("password", "y")).andExpect(status().isForbidden()); // CSRF
    }

    @Test
    void bootstrapAdminMustChangePasswordAndEnrollOtp() throws Exception {
        MvcResult r = mvc.perform(post("/login").param("username", "root.admin")
                        .param("password", TestFixture.BOOTSTRAP_PASSWORD).with(csrf()))
                .andExpect(redirectedUrl("/account/password")).andReturn();
        MockHttpSession s = (MockHttpSession) r.getRequest().getSession();
        mvc.perform(get("/").session(s)).andExpect(status().isForbidden());           // 단계 중에는 다른 화면 불가
        mvc.perform(get("/licenses").session(s)).andExpect(status().isForbidden());
        mvc.perform(post("/account/password").session(s).with(csrf()).param("current", TestFixture.BOOTSTRAP_PASSWORD)
                .param("next", "short").param("confirm", "short")).andExpect(status().isOk()); // 정책 위반 → 같은 화면
        mvc.perform(post("/account/password").session(s).with(csrf()).param("current", TestFixture.BOOTSTRAP_PASSWORD)
                .param("next", PW2).param("confirm", PW2)).andExpect(redirectedUrl("/account/totp"));
        mvc.perform(get("/").session(s)).andExpect(status().isForbidden());
        MvcResult page = mvc.perform(get("/account/totp").session(s)).andExpect(status().isOk()).andReturn();
        String secret = ((String) page.getModelAndView().getModel().get("secret")).replace(" ", "");
        mvc.perform(post("/account/totp").session(s).with(csrf()).param("code", "000000")).andExpect(status().isOk());
        mvc.perform(post("/account/totp").session(s).with(csrf()).param("code", nextCode(secret)))
                .andExpect(redirectedUrl("/"));
        mvc.perform(get("/").session(s)).andExpect(status().isOk());
    }

    @Test
    void lockoutAfterFiveFailures() throws Exception {
        String secret = readyUser("lock.me", AppUser.Role.VIEWER);
        for (int i = 0; i < 5; i++) {
            mvc.perform(post("/login").param("username", "lock.me").param("password", "wrong-password-xx").with(csrf())
                    .with(rq -> { rq.setRemoteAddr("10.1.1.1"); return rq; })).andExpect(redirectedUrl("/login?error"));
        }
        mvc.perform(post("/login").param("username", "lock.me").param("password", PW2).with(csrf()))
                .andExpect(redirectedUrl("/login?error")); // 올바른 비밀번호여도 잠김
        clock.advance(UserService.LOCK.plusSeconds(1));
        login("lock.me", PW2, secret);
    }

    @Test
    void wrongOtpFiveTimesEndsSession() throws Exception {
        readyUser("otp.fail", AppUser.Role.VIEWER);
        MvcResult r = mvc.perform(post("/login").param("username", "otp.fail").param("password", PW2).with(csrf()))
                .andExpect(redirectedUrl("/mfa")).andReturn();
        MockHttpSession s = (MockHttpSession) r.getRequest().getSession();
        for (int i = 0; i < 4; i++) mvc.perform(post("/mfa").session(s).with(csrf()).param("code", "123456")).andExpect(status().isOk());
        mvc.perform(post("/mfa").session(s).with(csrf()).param("code", "123456")).andExpect(redirectedUrl("/login?error"));
        assertThat(s.isInvalid()).isTrue();
    }

    @Test
    void roleBoundaries() throws Exception {
        MockHttpSession viewer = login("role.viewer", PW2, readyUser("role.viewer", AppUser.Role.VIEWER));
        mvc.perform(get("/licenses").session(viewer)).andExpect(status().isOk());
        mvc.perform(get("/licenses/new").session(viewer)).andExpect(status().isForbidden());
        mvc.perform(get("/users").session(viewer)).andExpect(status().isForbidden());
        MockHttpSession issuer = login("role.issuer", PW2, readyUser("role.issuer", AppUser.Role.ISSUER));
        mvc.perform(get("/licenses/new").session(issuer)).andExpect(status().isOk());
        mvc.perform(get("/audit").session(issuer)).andExpect(status().isForbidden());
        mvc.perform(post("/keys/seal").session(issuer).with(csrf())).andExpect(status().isForbidden());
    }

    @Test
    void disablingUserEndsTheirSessionAndBlocksLogin() throws Exception {
        String adminSecret = readyUser("dis.admin", AppUser.Role.ADMIN);
        MockHttpSession admin = login("dis.admin", PW2, adminSecret);
        MockHttpSession viewer = login("dis.viewer", PW2, readyUser("dis.viewer", AppUser.Role.VIEWER));
        mvc.perform(get("/").session(viewer)).andExpect(status().isOk());
        long id = users.findByUsername("dis.viewer").orElseThrow().id();
        mvc.perform(post("/users/" + id + "/enabled").session(admin).with(csrf()).param("enabled", "false")
                .param("password", PW2).param("totp", nextCode(adminSecret))).andExpect(redirectedUrl("/users"));
        assertThat(users.findByUsername("dis.viewer").orElseThrow().enabled()).isFalse();
        mvc.perform(get("/").session(viewer)).andExpect(redirectedUrl("/login?expired"));
        mvc.perform(post("/login").param("username", "dis.viewer").param("password", PW2).with(csrf()))
                .andExpect(redirectedUrl("/login?error"));
        // 자기 자신 비활성화는 거부
        long self = users.findByUsername("dis.admin").orElseThrow().id();
        mvc.perform(post("/users/" + self + "/enabled").session(admin).with(csrf()).param("enabled", "false")
                .param("password", PW2).param("totp", nextCode(adminSecret))).andExpect(redirectedUrl("/users"));
        assertThat(users.findByUsername("dis.admin").orElseThrow().enabled()).isTrue();
    }

    @Test
    void loginRateLimitPerIp() throws Exception {
        for (int i = 0; i < 20; i++) {
            mvc.perform(post("/login").param("username", "nobody" + i).param("password", "x").with(csrf())
                    .with(rq -> { rq.setRemoteAddr("10.9.9.9"); return rq; })).andExpect(status().is3xxRedirection());
        }
        mvc.perform(post("/login").param("username", "nobody").param("password", "x").with(csrf())
                .with(rq -> { rq.setRemoteAddr("10.9.9.9"); return rq; })).andExpect(status().isTooManyRequests());
    }

    @Test
    void absoluteSessionTimeout() throws Exception {
        MockHttpSession s = login("abs.timeout", PW2, readyUser("abs.timeout", AppUser.Role.VIEWER));
        mvc.perform(get("/").session(s)).andExpect(status().isOk());
        clock.advance(Duration.ofHours(9));
        mvc.perform(get("/").session(s)).andExpect(redirectedUrl("/login?expired"));
    }

    @Test
    void endToEndIssueDownloadVerifyWithEscaping() throws Exception {
        String secret = readyUser("e2e.admin", AppUser.Role.ADMIN);
        MockHttpSession s = login("e2e.admin", PW2, secret);
        // 재인증 실패 → 해제 안 됨
        mvc.perform(post("/keys/unseal").session(s).with(csrf()).param("passphrase", TestFixture.PASSPHRASE)
                .param("password", "bad").param("totp", "000000")).andExpect(redirectedUrl("/keys"));
        assertThat(vault.state()).isEqualTo(KeyVault.State.SEALED);
        mvc.perform(post("/keys/unseal").session(s).with(csrf()).param("passphrase", TestFixture.PASSPHRASE)
                .param("password", PW2).param("totp", nextCode(secret))).andExpect(redirectedUrl("/keys"));
        assertThat(vault.state()).isEqualTo(KeyVault.State.UNSEALED);
        try {
            String token = UUID.randomUUID().toString();
            var form = post("/licenses/issue").session(s).with(csrf()).param("submissionToken", token)
                    .param("projectName", "<script>alert(1)</script>").param("siteId", "SITE-A").param("licenseType", "trial")
                    .param("onlineStt", "2").param("offlineStt", "3").param("notBeforeDate", "2026-01-01")
                    .param("notBeforeTime", "00:00").param("notAfterDate", "2099-12-31").param("notAfterTime", "23:59:59")
                    .param("gracePeriodDays", "7").param("warningNotice", "notice");
            MvcResult issued = mvc.perform(form).andExpect(status().is3xxRedirection()).andReturn();
            String location = issued.getResponse().getRedirectedUrl();
            assertThat(location).matches("/licenses/[0-9a-f]{32}\\?issued");
            String id = location.substring(10, 42);
            mvc.perform(get("/licenses/" + id).session(s)).andExpect(status().isOk())
                    .andExpect(content().string(org.hamcrest.Matchers.containsString("&lt;script&gt;alert(1)&lt;/script&gt;")))
                    .andExpect(content().string(org.hamcrest.Matchers.not(org.hamcrest.Matchers.containsString("<script>alert"))));
            MvcResult dl = mvc.perform(get("/licenses/" + id + "/download").session(s)).andExpect(status().isOk())
                    .andExpect(header().string("Content-Disposition", org.hamcrest.Matchers.containsString("SITE-A_" + id + ".lic")))
                    .andReturn();
            String lic = dl.getResponse().getContentAsString(java.nio.charset.StandardCharsets.UTF_8);
            mvc.perform(multipart("/verify").file("file", lic.getBytes()).session(s).with(csrf()))
                    .andExpect(content().string(org.hamcrest.Matchers.containsString(">OK<")));
            mvc.perform(post("/verify").session(s).with(csrf()).param("text", lic.replace("\"offline_stt\": 3", "\"offline_stt\": 30")))
                    .andExpect(content().string(org.hamcrest.Matchers.containsString("LICENSE_BAD_SIGNATURE")));
            // 같은 폼 재전송 → 같은 건
            assertThat(mvc.perform(form).andReturn().getResponse().getRedirectedUrl()).isEqualTo(location);
        } finally {
            vault.seal("test", null, "cleanup");
        }
    }
}
