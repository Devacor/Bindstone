// Optional ETW markers. Each smoke execution owns its provider registration.
#pragma once

#ifdef GLOOM_PROFILE_CAPTURE
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <evntprov.h>
#include <string>

#pragma comment(lib, "advapi32.lib")

namespace gloom {
class profile_capture {
public:
	explicit profile_capture(const wchar_t* a_label = L"GLOOM measured loop") :
		label_(a_label) {
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
		EventWriteString(registration_, 4, 1, (label_ + L" begin").c_str());
	}
	void stop() {
		if (registration_ && active_) {
			EventWriteString(registration_, 4, 1, (label_ + L" end").c_str());
			active_ = false;
		}
	}
private:
	std::wstring label_;
	REGHANDLE registration_ = 0;
	bool active_ = false;
};
} // namespace gloom
#endif
