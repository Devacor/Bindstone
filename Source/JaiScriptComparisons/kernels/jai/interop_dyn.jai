// needs add_one(int) -> int from the host: run with host/jai_kernel_host.exe
include "profile.jai";

function test_adds() {
	var count = 0;
	for (var i = 1; i <= 1000000; ++i) { count = add_one(count); }
	return count;
}

var result = 0;
profile_it("interop host calls", []() { result = test_adds(); });
print("checksum {}", result);
