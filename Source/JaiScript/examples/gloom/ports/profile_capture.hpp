// Optional ETW markers. Each smoke execution owns its provider registration.
#pragma once

#ifdef GLOOM_PROFILE_CAPTURE
#include <windows.h>
#include <evntprov.h>
#include <string>

#pragma comment(lib, "advapi32.lib")

namespace gloom {
class profile_capture {
public:
	profile_capture() {
		const GUID provider{0x744b3f90, 0x7d83, 0x4b7b, {0xa8, 0x61, 0x40, 0x63, 0x76, 0x23, 0xc5, 0x92}};
		EventRegister(&provider, nullptr, nullptr, &registration_);
	}
	~profile_capture() {
		stop();
		if (registration_) { EventUnregister(registration_); }
	}
	void begin() {
		if (!registration_) { return; }
		active_ = true;
		EventWriteString(registration_, 4, 1, L"GLOOM measured loop begin");
	}
	void stop() {
		if (registration_ && active_) {
			EventWriteString(registration_, 4, 1, L"GLOOM measured loop end");
			active_ = false;
		}
	}
private:
	REGHANDLE registration_ = 0;
	bool active_ = false;
};
} // namespace gloom
#endif
