// needs add_one(int) -> int from the host: run with host/jai_kernel_host.exe
include "profile.jai";

int test_adds() {
	int count = 0;
	for (int i = 1; i <= 1000000; ++i) { count = add_one(count); }
	return count;
}

int result = 0;
profile_it("interop host calls", []() { result = test_adds(); });
print("checksum {}", result);
