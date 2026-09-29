#include <cstdio>

extern "C" int mock_connect(const char* path);
extern "C" int mock_acquire_release();

// 사용: mock_sdk_smoke <valid.lic> <tampered.lic>
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  int bad = mock_connect(argv[2]);
  int good = mock_connect(argv[1]);
  int acq = mock_acquire_release();
  std::printf("tampered=%d valid=%d acquire=%d\n", bad, good, acq);
  return (bad == 1004 && good == 0 && acq == 0) ? 0 : 1;
}
