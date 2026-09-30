package licenseharness;

/** 실제 SonaSttAPI 와 같은 호출 모델의 모의 SDK. 라이선스 코어와의 결합 지점만 실제 코드다. */
public class SttHarness {
  static {
    System.loadLibrary("stt_harness_jni");
  }

  /** 0 = 성공, 1000+ = 라이선스 오류(stt::license::Error 순번), 그 외 = SDK 오류. */
  public native int connect(String licensePath, int sonasttSessions, int whisperSessions);

  /** {"result":0,"session_id":"..."} 또는 {"result":code,"error":"...","message":"..."} */
  public native String getIdleSession(String engine);

  /** 작업 종료(onResult/onError/abortTask) → 세션·채널 반환. */
  public native int finishTask(String sessionId);

  public native String getSTTSessionInfo();

  public native int reloadLicense();

  /**
   * 실제 STT 서버 API 와 같은 형태: connect(SonaSttListener callback, String configFile, String hostLicense).
   * hostLicense = license.lic 파일 내용. 검증 수준은 configFile 의 --license-verify-mode=signature|full
   * (없으면 signature). 0 = 성공, 1000+ = 라이선스 오류, 2 = 잘못된 인자·설정.
   */
  public native int connectHostLicense(Object callback, String configFile, String hostLicense);

  /** SDK 가 서명 검증된 라이선스에서 꺼낸 값(JSON). */
  public native String hostLicenseInfo();

  /** 대조군: GetStringUTFChars(변형 UTF-8) 경로로 검증. 실제 SDK 에서 쓰면 안 되는 방식. */
  public native int verifyViaModifiedUtf8(String hostLicense);

  /** 문자열 → UTF-8 바이트 변환 결과. modifiedUtf8=false 는 SDK 권장 경로(UTF-16), true 는 대조군. */
  public native byte[] toUtf8Bytes(String s, boolean modifiedUtf8);

  public native void disconnect();
}
