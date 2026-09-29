package com.personaai.license.server.web;

import java.nio.charset.StandardCharsets;
import java.time.Clock;
import java.time.LocalDate;
import java.util.Map;

import jakarta.servlet.http.HttpServletRequest;

import org.springframework.http.ContentDisposition;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.ModelAttribute;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.servlet.mvc.support.RedirectAttributes;

import com.personaai.license.server.audit.AuditService;
import com.personaai.license.server.config.LicenseProperties;
import com.personaai.license.server.key.KeyVault;
import com.personaai.license.server.license.ContractValidator;
import com.personaai.license.server.license.IssueForm;
import com.personaai.license.server.license.IssueService;
import com.personaai.license.server.license.LicenseRecord;
import com.personaai.license.server.license.LicenseRepository;
import com.personaai.license.server.security.AppPrincipal;
import com.personaai.license.server.security.ClientIp;
import com.personaai.license.server.security.ReauthService;

@Controller
public class LicenseController {
    static final int PAGE = 50;
    static final String ID = "{licenseId:[0-9a-f]{32}}";

    private final LicenseRepository licenses;
    private final IssueService issue;
    private final KeyVault vault;
    private final ReauthService reauth;
    private final AuditService audit;
    private final LicenseProperties props;
    private final Clock clock;

    public LicenseController(LicenseRepository licenses, IssueService issue, KeyVault vault, ReauthService reauth,
                             AuditService audit, LicenseProperties props, Clock clock) {
        this.licenses = licenses;
        this.issue = issue;
        this.vault = vault;
        this.reauth = reauth;
        this.audit = audit;
        this.props = props;
        this.clock = clock;
    }

    @GetMapping("/licenses")
    public String list(@RequestParam(required = false) String q, @RequestParam(required = false) String status,
                       @RequestParam(defaultValue = "0") int page, Model model) {
        if (status != null && !status.isBlank() && !status.matches("PENDING|ISSUED|FAILED|VOID")) status = null;
        page = Math.max(0, page);
        model.addAttribute("items", licenses.search(q, status, PAGE + 1, page * PAGE));
        model.addAttribute("q", q);
        model.addAttribute("status", status);
        model.addAttribute("page", page);
        model.addAttribute("pageSize", PAGE);
        return "licenses/list";
    }

    @GetMapping("/licenses/new")
    public String newForm(@RequestParam(required = false) String from, Model model) {
        IssueForm f;
        if (from != null && from.matches("[0-9a-f]{32}")) {
            f = licenses.findByLicenseId(from).map(r -> IssueForm.copyOf(r, props.zone())).orElseGet(IssueForm::new);
        } else {
            f = new IssueForm();
            LocalDate today = LocalDate.now(clock.withZone(props.zone()));
            f.setNotBeforeDate(today.toString());
            f.setNotAfterDate(today.plusYears(1).minusDays(1).toString());
        }
        if (f.getWarningNotice() == null) f.setWarningNotice(props.defaultWarningNotice());
        return form(f, model, Map.of(), null);
    }

    private String form(IssueForm f, Model model, Map<String, String> errors, String message) {
        model.addAttribute("form", f);
        model.addAttribute("errors", errors);
        model.addAttribute("message", message);
        model.addAttribute("types", ContractValidator.TYPES.stream().sorted().toList());
        model.addAttribute("projects", licenses.distinctValues("project_name"));
        model.addAttribute("sites", licenses.distinctValues("site_id"));
        model.addAttribute("keyState", vault.state());
        return "licenses/form";
    }

    @PostMapping("/licenses/preview")
    public String preview(@ModelAttribute("form") IssueForm f, Model model) {
        ContractValidator.Result r = issue.validate(f);
        if (!r.ok()) return form(f, model, r.errors(), null);
        model.addAttribute("form", f);
        model.addAttribute("contract", r.contract());
        model.addAttribute("warnings", r.warnings());
        model.addAttribute("contractJson", new String(r.contract().toJson("(발급 시 생성)"), StandardCharsets.UTF_8));
        model.addAttribute("keyState", vault.state());
        return "licenses/preview";
    }

    /** 미리보기에서 "수정": 입력값을 유지한 채 폼으로 돌아간다. */
    @PostMapping("/licenses/edit")
    public String edit(@ModelAttribute("form") IssueForm f, Model model) {
        return form(f, model, Map.of(), null);
    }

    @PostMapping("/licenses/issue")
    public String issue(@ModelAttribute("form") IssueForm f, @AuthenticationPrincipal AppPrincipal p,
                        HttpServletRequest req, Model model) {
        try {
            LicenseRecord r = issue.issue(f, p, ClientIp.of(req));
            return "redirect:/licenses/" + r.licenseId() + "?issued";
        } catch (IssueService.IssueException e) {
            return form(f, model, Map.of(), e.getMessage());
        }
    }

    @GetMapping("/licenses/" + ID)
    public String detail(@PathVariable String licenseId, Model model) {
        LicenseRecord r = licenses.findByLicenseId(licenseId).orElseThrow(NotFound::new);
        model.addAttribute("lic", r);
        return "licenses/detail";
    }

    @GetMapping("/licenses/" + ID + "/download")
    public ResponseEntity<byte[]> download(@PathVariable String licenseId, @AuthenticationPrincipal AppPrincipal p,
                                           HttpServletRequest req) {
        LicenseRecord r = licenses.findByLicenseId(licenseId).orElseThrow(NotFound::new);
        if (r.licenseText() == null) throw new NotFound();
        audit.record(p.getUsername(), "DOWNLOAD", licenseId, ClientIp.of(req), Map.of("status", r.status().name()));
        return ResponseEntity.ok()
                .header(HttpHeaders.CONTENT_DISPOSITION, ContentDisposition.attachment()
                        .filename(r.downloadFileName(), StandardCharsets.UTF_8).build().toString())
                .contentType(MediaType.APPLICATION_OCTET_STREAM)
                .body(r.licenseText().getBytes(StandardCharsets.UTF_8));
    }

    @PostMapping("/licenses/" + ID + "/void")
    public String voidLicense(@PathVariable String licenseId, @RequestParam String reason, @RequestParam String password,
                              @RequestParam String totp, @AuthenticationPrincipal AppPrincipal p,
                              HttpServletRequest req, RedirectAttributes ra) {
        if (!reauth.verify(p, password, totp)) {
            ra.addFlashAttribute("error", "재인증에 실패했습니다.");
        } else {
            try {
                boolean ok = issue.voidLicense(licenseId, reason, p, ClientIp.of(req));
                ra.addFlashAttribute(ok ? "info" : "error",
                        ok ? "무효 처리했습니다." : "발급 완료(ISSUED) 상태에서만 무효 처리할 수 있습니다.");
            } catch (IssueService.IssueException e) {
                ra.addFlashAttribute("error", e.getMessage());
            }
        }
        return "redirect:/licenses/" + licenseId;
    }

    @org.springframework.web.bind.annotation.ResponseStatus(org.springframework.http.HttpStatus.NOT_FOUND)
    static class NotFound extends RuntimeException {}
}
