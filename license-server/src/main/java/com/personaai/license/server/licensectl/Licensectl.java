package com.personaai.license.server.licensectl;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.springframework.stereotype.Component;

import com.personaai.license.server.config.LicenseProperties;

import tools.jackson.core.type.TypeReference;
import tools.jackson.databind.json.JsonMapper;

/**
 * licensectl 하위 프로세스 실행기. 셸을 거치지 않고(고정 인자), 환경변수를 비우고(LD_PRELOAD 등 차단),
 * 비밀값은 stdin 으로만 넘기며, 출력 크기와 실행 시간을 제한한다.
 */
@Component
public class Licensectl {
    private static final int MAX_OUTPUT = 256 * 1024;
    private static final Duration DEFAULT_TIMEOUT = Duration.ofSeconds(10);
    private static final Pattern ERROR_LINE = Pattern.compile("^error: ([A-Z_]+): (.*)$", Pattern.MULTILINE);
    private static final JsonMapper JSON = JsonMapper.builder().build();

    private final Path executable;
    private final Duration timeout;

    @org.springframework.beans.factory.annotation.Autowired
    public Licensectl(LicenseProperties props) {
        this(props.licensectlPath(), DEFAULT_TIMEOUT);
    }

    Licensectl(Path executable, Duration timeout) {
        this.executable = executable;
        this.timeout = timeout;
    }

    public record Result(int exitCode, String stdout, String stderr) {}

    /** 서명 실패 시 licensectl 오류 이름(LICENSE_INVALID_FIELD 등)과 상세를 담는다. */
    public static class LicensectlException extends RuntimeException {
        private final String errorName;

        public LicensectlException(String errorName, String detail) {
            super(detail);
            this.errorName = errorName;
        }

        public String errorName() {
            return errorName;
        }
    }

    /** 비밀키 문자열과 계약 JSON 으로 license.lic 본문을 만든다. privateKeyLine 은 호출자가 소유·삭제한다. */
    public String sign(byte[] privateKeyLine, byte[] contractJson) {
        byte[] stdin = concatLine(privateKeyLine, contractJson);
        Result r;
        try {
            r = run(stdin, "sign");
        } finally {
            Arrays.fill(stdin, (byte) 0);
        }
        if (r.exitCode() != 0) throw toException(r);
        return r.stdout();
    }

    public String publicKeyOf(byte[] privateKeyLine) {
        byte[] stdin = concatLine(privateKeyLine, new byte[0]);
        Result r;
        try {
            r = run(stdin, "pubkey");
        } finally {
            Arrays.fill(stdin, (byte) 0);
        }
        if (r.exitCode() != 0) throw toException(r);
        return r.stdout().strip();
    }

    /** verify -json 결과. result 가 "OK" 가 아니면 서명·형식 오류, state 로 기간 판정. */
    public Map<String, Object> verify(String publicKeyB64, byte[] license) {
        Result r = run(license, "verify", "-pubkey", publicKeyB64, "-json", "-");
        // exit 1 은 "검증 실패/만료" 도 포함하므로 JSON 이 있으면 그것을 신뢰한다.
        if (r.stdout().isBlank()) throw toException(r);
        return JSON.readValue(r.stdout(), new TypeReference<Map<String, Object>>() {});
    }

    Result run(byte[] stdin, String... args) {
        List<String> cmd = new ArrayList<>();
        cmd.add(executable.toString());
        cmd.addAll(List.of(args));
        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.environment().clear();
        Process p;
        try {
            p = pb.start();
        } catch (IOException e) {
            throw new LicensectlException("LICENSECTL_UNAVAILABLE", "cannot start " + executable + ": " + e.getMessage());
        }
        // 종료 시 읽기 스레드를 기다리지 않는다(ExecutorService.close() 는 작업 종료까지 대기하므로 쓰지 않음).
        ExecutorService io = Executors.newVirtualThreadPerTaskExecutor();
        try {
            Future<byte[]> out = io.submit(() -> readCapped(p, p.getInputStream()));
            Future<byte[]> err = io.submit(() -> readCapped(p, p.getErrorStream()));
            try (OutputStream in = p.getOutputStream()) {
                in.write(stdin);
            } catch (IOException e) {
                // 프로세스가 먼저 종료(입력 거부)한 경우. 종료 코드와 stderr 로 판단한다.
            }
            if (!p.waitFor(timeout.toMillis(), TimeUnit.MILLISECONDS)) {
                kill(p);
                throw new LicensectlException("LICENSECTL_TIMEOUT", "licensectl did not finish in " + timeout);
            }
            return new Result(p.exitValue(),
                    new String(out.get(timeout.toMillis(), TimeUnit.MILLISECONDS), StandardCharsets.UTF_8),
                    new String(err.get(timeout.toMillis(), TimeUnit.MILLISECONDS), StandardCharsets.UTF_8));
        } catch (InterruptedException e) {
            kill(p);
            Thread.currentThread().interrupt();
            throw new LicensectlException("LICENSECTL_INTERRUPTED", "interrupted");
        } catch (java.util.concurrent.ExecutionException | java.util.concurrent.TimeoutException e) {
            kill(p);
            throw new LicensectlException("LICENSECTL_IO", String.valueOf(e.getCause() != null ? e.getCause().getMessage() : e.getMessage()));
        } finally {
            io.shutdownNow();
        }
    }

    /** 프로세스와 그 자손을 모두 종료한다(자손이 파이프를 쥐고 있으면 읽기가 끝나지 않는다). */
    private static void kill(Process p) {
        p.descendants().forEach(ProcessHandle::destroyForcibly);
        p.destroyForcibly();
    }

    /** 상한을 넘으면 즉시 프로세스를 죽인다(읽기를 멈추면 자식이 가득 찬 파이프에서 멈춰 타임아웃까지 기다리게 됨). */
    private static byte[] readCapped(Process p, InputStream in) throws IOException {
        byte[] data = in.readNBytes(MAX_OUTPUT + 1);
        if (data.length > MAX_OUTPUT) {
            kill(p);
            throw new IOException("licensectl output exceeds " + MAX_OUTPUT + " bytes");
        }
        return data;
    }

    private static byte[] concatLine(byte[] first, byte[] rest) {
        byte[] out = new byte[first.length + 1 + rest.length];
        System.arraycopy(first, 0, out, 0, first.length);
        out[first.length] = '\n';
        System.arraycopy(rest, 0, out, first.length + 1, rest.length);
        return out;
    }

    private static LicensectlException toException(Result r) {
        Matcher m = ERROR_LINE.matcher(r.stderr());
        if (m.find()) return new LicensectlException(m.group(1), m.group(2));
        String msg = r.stderr().isBlank() ? "exit code " + r.exitCode() : r.stderr().strip();
        return new LicensectlException("LICENSECTL_ERROR", msg);
    }
}
