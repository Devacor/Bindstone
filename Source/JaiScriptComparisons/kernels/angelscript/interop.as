// add_one(int) -> int is the host's: angelscript_run registers it
#include "profile.as"

int test_adds() {
	int count = 0;
	for (int i = 1; i <= 1000000; ++i) {
		count = add_one(count);
	}
	return count;
}

int result = 0;

void main() {
	profile_it("interop host calls", function() {
		result = test_adds();
		if (result != 1000000) {
			print("failed " + result);
		}
	});
	print("checksum " + result);
}
