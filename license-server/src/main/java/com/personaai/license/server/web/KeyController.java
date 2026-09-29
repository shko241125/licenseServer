package com.personaai.license.server.web;

import java.nio.charset.StandardCharsets;

import jakarta.servlet.http.HttpServletRequest;

import org.springframework.http.ContentDisposition;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.servlet.mvc.support.RedirectAttributes;

import com.personaai.license.server.key.KeyVault;
import com.personaai.license.server.security.AppPrincipal;
import com.personaai.license.server.security.ClientIp;
import com.personaai.license.server.security.ReauthService;

@Controller
public class KeyController {
    private final KeyVault vault;
    private final ReauthService reauth;

    public KeyController(KeyVault vault, ReauthService reauth) {
        this.vault = vault;
        this.reauth = reauth;
    }

    @GetMapping("/keys")
    public String page(Model model) {
        model.addAttribute("keyState", vault.state());
        model.addAttribute("keyError", vault.loadError());
        model.addAttribute("publicKey", vault.publicKey());
        model.addAttribute("fingerprint", vault.publicKeyFingerprint());
        model.addAttribute("unsealedAt", vault.unsealedAt());
        model.addAttribute("lockedUntil", vault.lockedUntil());
        return "keys";
    }

    @PostMapping("/keys/unseal")
    // char[] 로 받으면 Spring 변환기가 쉼표로 쪼갠다. 서블릿이 이미 String 으로 보관하므로 String 으로 받는다.
    public String unseal(@RequestParam String passphrase, @RequestParam String password, @RequestParam String totp,
                         @AuthenticationPrincipal AppPrincipal p, HttpServletRequest req, RedirectAttributes ra) {
        if (!reauth.verify(p, password, totp)) {
            ra.addFlashAttribute("error", "재인증에 실패했습니다.");
            return "redirect:/keys";
        }
        KeyVault.UnsealResult r = vault.unseal(passphrase.toCharArray(), p.getUsername(), ClientIp.of(req));
        ra.addFlashAttribute(r == KeyVault.UnsealResult.OK ? "info" : "error", switch (r) {
            case OK -> "서명 키를 해제했습니다. 일정 시간 발급이 없으면 자동으로 다시 봉인됩니다.";
            case ALREADY_UNSEALED -> "이미 해제된 상태입니다.";
            case WRONG_PASSPHRASE -> "패스프레이즈가 올바르지 않습니다.";
            case LOCKED -> "실패가 반복되어 잠시 잠겼습니다. 나중에 다시 시도하세요.";
            case KEY_MISMATCH -> "봉인 파일의 비밀키와 공개키가 짝이 맞지 않습니다. 파일을 확인하세요.";
            case MISSING -> "봉인 키 파일이 없습니다.";
        });
        return "redirect:/keys";
    }

    @PostMapping("/keys/seal")
    public String seal(@AuthenticationPrincipal AppPrincipal p, HttpServletRequest req, RedirectAttributes ra) {
        vault.seal(p.getUsername(), ClientIp.of(req), "manual");
        ra.addFlashAttribute("info", "서명 키를 봉인했습니다.");
        return "redirect:/keys";
    }

    @GetMapping("/keys/public.key")
    public ResponseEntity<byte[]> publicKey() {
        String pub = vault.publicKey();
        if (pub == null) return ResponseEntity.notFound().build();
        return ResponseEntity.ok()
                .header(HttpHeaders.CONTENT_DISPOSITION, ContentDisposition.attachment().filename("public.key").build().toString())
                .contentType(MediaType.TEXT_PLAIN)
                .body((pub + "\n").getBytes(StandardCharsets.US_ASCII));
    }
}
