package licenseharness;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * JNI 결합 검증. 인자: <workdir> — valid.lic(offline 3), more.lic(offline 5), tampered.lic, expired.lic 가 준비돼 있어야 한다.
 * 실행: java -Djava.library.path=<build> -cp <classes> licenseharness.HarnessTest <workdir>
 */
public class HarnessTest {
  static int failures = 0;

  static void check(boolean cond, String what) {
    System.out.println((cond ? "  PASS " : "  FAIL ") + what);
    if (!cond) failures++;
  }

  static int result(String json) {
    Matcher m = Pattern.compile("\"result\":(\\d+)").matcher(json);
    return m.find() ? Integer.parseInt(m.group(1)) : -1;
  }

  static String field(String json, String key) {
    Matcher m = Pattern.compile("\"" + key + "\":\"([^\"]*)\"").matcher(json);
    return m.find() ? m.group(1) : null;
  }

  static int intField(String json, String key) {
    Matcher m = Pattern.compile("\"" + key + "\":(\\d+)").matcher(json);
    return m.find() ? Integer.parseInt(m.group(1)) : -1;
  }

  public static void main(String[] args) throws Exception {
    Path dir = Path.of(args[0]);
    Path live = dir.resolve("license.lic");
    Files.copy(dir.resolve("valid.lic"), live, StandardCopyOption.REPLACE_EXISTING);
    SttHarness sdk = new SttHarness();

    System.out.println("[기동 게이트]");
    check(sdk.connect(dir.resolve("tampered.lic").toString(), 2, 8) == 1004, "변조 파일 → 1004 BAD_SIGNATURE");
    check(sdk.connect(dir.resolve("expired.lic").toString(), 2, 8) == 1008, "만료 파일 → 1008 EXPIRED");
    check(sdk.connect(dir.resolve("missing.lic").toString(), 2, 8) == 1001, "파일 없음 → 1001 FILE_NOT_FOUND");
    check(sdk.connect(null, 2, 8) == 2, "null 경로 → 2 INVALID_ARGUMENT (JVM 크래시 없음)");
    check(result(sdk.getIdleSession("whisper")) == 3, "연결 전 getIdleSession → NOT_CONNECTED");
    check(sdk.connect(live.toString(), 2, 8) == 0, "정상 파일 → 0");

    String info = sdk.getSTTSessionInfo();
    check(intField(info, "sessions_total") == 5, "세션 풀 = min(sonastt 2,3) + min(whisper 8,3) = 5");
    check("VALID".equals(field(info, "state")), "상태 VALID");
    check(intField(info, "offline_max") == 3, "offline_max 3");

    System.out.println("[채널 한도: 두 엔진 합계]");
    String s1 = sdk.getIdleSession("sonastt");
    String s2 = sdk.getIdleSession("whisper");
    String s3 = sdk.getIdleSession("whisper");
    check(result(s1) == 0 && result(s2) == 0 && result(s3) == 0, "3개 예약 성공");
    String s4 = sdk.getIdleSession("whisper");
    check(result(s4) == 1009 && "LICENSE_CHANNEL_LIMIT".equals(field(s4, "error")),
        "4번째(유휴 세션은 남아 있음) → 1009 LICENSE_CHANNEL_LIMIT");
    check(sdk.finishTask(field(s2, "session_id")) == 0, "작업 종료 → 반납");
    check(sdk.finishTask(field(s2, "session_id")) == 2, "같은 세션 중복 종료 → 거부");
    check(sdk.finishTask(null) == 2, "null 세션 → 거부");
    check(result(sdk.getIdleSession("whisper")) == 0, "반납 후 재예약 성공");
    check(result(sdk.getIdleSession(null)) == 2, "null 엔진 → INVALID_ARGUMENT");

    System.out.println("[무중단 갱신]");
    Path tmp = dir.resolve("license.lic.tmp");
    Files.copy(dir.resolve("tampered.lic"), tmp, StandardCopyOption.REPLACE_EXISTING);
    Files.move(tmp, live, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
    check(sdk.reloadLicense() == 1004, "변조본으로 교체 → reload 실패 1004");
    info = sdk.getSTTSessionInfo();
    check(intField(info, "offline_max") == 3 && "LICENSE_BAD_SIGNATURE".equals(field(info, "last_reload_error")),
        "기존 라이선스 유지 + last_reload_error 기록");
    Files.copy(dir.resolve("more.lic"), tmp, StandardCopyOption.REPLACE_EXISTING);
    Files.move(tmp, live, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
    check(sdk.reloadLicense() == 0, "정상 갱신본(offline 5) → reload 성공");
    info = sdk.getSTTSessionInfo();
    check(intField(info, "offline_max") == 5 && "OK".equals(field(info, "last_reload_error")), "offline_max 5 반영");

    System.out.println("[동시성: 16 스레드 × 500회]");
    sdk.disconnect();
    check(sdk.connect(live.toString(), 10, 10) == 0, "재연결 (풀 10+10, 한도 5)");
    AtomicInteger inside = new AtomicInteger(), peak = new AtomicInteger(), limitHits = new AtomicInteger();
    ConcurrentLinkedQueue<String> errors = new ConcurrentLinkedQueue<>();
    List<Thread> ts = new ArrayList<>();
    for (int t = 0; t < 16; t++) {
      final String engine = t % 2 == 0 ? "sonastt" : "whisper";
      ts.add(new Thread(() -> {
        for (int i = 0; i < 500; i++) {
          String r = sdk.getIdleSession(engine);
          int code = result(r);
          if (code == 1009 || code == 4) { limitHits.incrementAndGet(); continue; }
          if (code != 0) { errors.add(r); continue; }
          int now = inside.incrementAndGet();
          peak.accumulateAndGet(now, Math::max);
          inside.decrementAndGet();
          if (sdk.finishTask(field(r, "session_id")) != 0) errors.add("finish " + r);
        }
      }));
    }
    ts.forEach(Thread::start);
    for (Thread t : ts) t.join();
    info = sdk.getSTTSessionInfo();
    check(errors.isEmpty(), "예상 밖 오류 없음 " + errors);
    check(peak.get() <= 5, "동시 점유 최대 " + peak.get() + " ≤ 5");
    check(intField(info, "offline_used") == 0 && intField(info, "sessions_busy") == 0, "종료 후 사용량 0 (누수 없음)");
    check(limitHits.get() > 0, "한도 초과 거부 발생 " + limitHits.get() + "회");

    System.out.println("[hostLicense 문자열 → 서명만 검증 (실제 connect(callback, sttHomePath, hostLicense) 형태)]");
    // 서버 측 권장 읽기 방식: 바이트를 UTF-8 로 디코드 (BOM 이 있으면 코어가 제거)
    String valid = new String(Files.readAllBytes(dir.resolve("valid.lic")), java.nio.charset.StandardCharsets.UTF_8);
    String tampered = new String(Files.readAllBytes(dir.resolve("tampered.lic")), java.nio.charset.StandardCharsets.UTF_8);
    String emoji = new String(Files.readAllBytes(dir.resolve("emoji.lic")), java.nio.charset.StandardCharsets.UTF_8);
    check(sdk.connectHostLicense(null, "/app/home", valid) == 0, "정상 라이선스 문자열 → 0");
    String hinfo = sdk.hostLicenseInfo();
    check(intField(hinfo, "offline_stt") == 3 && "OK".equals(field(hinfo, "standard")),
        "SDK 가 서명된 값 사용: offline_stt=3, 표준 규칙 판정 OK " + hinfo);
    check(sdk.connectHostLicense(null, "/app/home", tampered) == 1004, "변조 문자열 → 1004 BAD_SIGNATURE");
    check(sdk.connectHostLicense(null, "/app/home", null) == 2, "null → 2 INVALID_ARGUMENT");
    check(sdk.connectHostLicense(null, "/app/home", "") == 1003, "빈 문자열 → 1003 PARSE_ERROR");
    check(sdk.connectHostLicense(null, "/app/home", "\uFEFF" + valid.replace("\n", "\r\n")) == 0,
        "BOM + CRLF 로 바뀐 문자열도 0 (서명은 정규화된 내용 기준)");
    check(sdk.connectHostLicense(null, "/app/home", "x".repeat(70_000)) == 1002, "64K 문자 초과 → 1002");
    check(sdk.connectHostLicense(null, "/app/home", emoji) == 0, "보조 평면 문자(😀) 포함 라이선스 → 0 (UTF-16 경로)");
    check(field(sdk.hostLicenseInfo(), "project_name").contains("😀"), "project_name 의 😀 가 그대로 복원됨");
    int modified = sdk.verifyViaModifiedUtf8(emoji);
    check(modified != 0, "대조군: 같은 라이선스를 GetStringUTFChars(변형 UTF-8)로 받으면 실패 → " + modified);
    check(sdk.verifyViaModifiedUtf8(valid) == 0, "대조군: ASCII·한글만 있으면 변형 UTF-8 도 통과 (그래서 놓치기 쉬움)");

    System.out.println("[검증 수준 선택: configFile 의 --license-verify-mode (서명만 / 값까지)]");
    String expired = new String(Files.readAllBytes(dir.resolve("expired.lic")), java.nio.charset.StandardCharsets.UTF_8);
    String sigCfg = dir.resolve("sig.cfg").toString(), fullCfg = dir.resolve("full.cfg").toString();
    String badCfg = dir.resolve("bad.cfg").toString(), noCfg = dir.resolve("none.cfg").toString();
    check(sdk.connectHostLicense(null, noCfg, expired) == 0
        && "signature".equals(field(sdk.hostLicenseInfo(), "mode")), "설정 없음 → 기본 signature: 만료 라이선스도 서명만 맞으면 0");
    check(sdk.connectHostLicense(null, sigCfg, expired) == 0, "signature 모드: 만료 라이선스 → 0 (값 판정은 SDK 몫)");
    check(sdk.connectHostLicense(null, fullCfg, expired) == 1008, "full 모드: 만료 라이선스 → 1008 EXPIRED");
    check(sdk.connectHostLicense(null, fullCfg, valid) == 0
        && "full".equals(field(sdk.hostLicenseInfo(), "mode")), "full 모드: 유효 라이선스 → 0");
    check(sdk.connectHostLicense(null, fullCfg, tampered) == 1004, "full 모드: 변조 → 1004 (서명 검증이 먼저)");
    check(sdk.connectHostLicense(null, badCfg, valid) == 2, "잘못된 모드 값(오타) → 2, 조용히 약한 검증으로 넘어가지 않음");

    sdk.disconnect();
    check(result(sdk.getSTTSessionInfo()) == 3, "disconnect 후 NOT_CONNECTED");

    System.out.println(failures == 0 ? "ALL PASSED" : failures + " FAILED");
    System.exit(failures == 0 ? 0 : 1);
  }
}
