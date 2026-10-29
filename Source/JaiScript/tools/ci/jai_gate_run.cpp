// jai_gate_run: runs one gate program under a watchdog (tools/ci/ci_lib.sh runs every gate under it).
//
//   jai_gate_run --timeout=<seconds> [--dump-dir=<dir>] -- <program> <args...>
//
// The exit code is the program's, or 124 when it outlived the timeout (0: no timeout). On Windows the program and every
// process it starts run in a job object: past the timeout each one is written as a minidump to --dump-dir and the job is
// killed, and a watchdog that dies takes the job with it. Elsewhere the program runs under timeout(1) where present.

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif

namespace {

constexpr int timed_out_exit = 124;

template<class String>
struct gate_run_options {
	unsigned long timeout_seconds = 0;
	String dump_dir;
	std::vector<String> command;
};

template<class String>
String string_of(const char* a_text) {
	return String(a_text, a_text + std::char_traits<char>::length(a_text));
}

template<class String>
bool parse_gate_run_options(const std::vector<String>& a_args, gate_run_options<String>& a_options) {
	const String timeout_flag = string_of<String>("--timeout=");
	const String dump_dir_flag = string_of<String>("--dump-dir=");
	const String separator = string_of<String>("--");
	size_t index = 1;
	for (; index < a_args.size() && a_args[index] != separator; ++index) {
		const String& arg = a_args[index];
		if (arg.compare(0, timeout_flag.size(), timeout_flag) == 0) {
			String digits = arg.substr(timeout_flag.size());
			if (digits.empty() || digits.size() > 9 || digits.find_first_not_of(string_of<String>("0123456789")) != String::npos) {
				return false;
			}
			a_options.timeout_seconds = std::stoul(digits);
		} else if (arg.compare(0, dump_dir_flag.size(), dump_dir_flag) == 0) {
			a_options.dump_dir = arg.substr(dump_dir_flag.size());
		} else {
			return false;
		}
	}
	if (index + 1 >= a_args.size()) {
		return false;
	}
	a_options.command.assign(a_args.begin() + static_cast<std::ptrdiff_t>(index) + 1, a_args.end());
	return true;
}

int usage() {
	std::fputs("usage: jai_gate_run --timeout=<seconds> [--dump-dir=<dir>] -- <program> <args...>\n", stderr);
	return 2;
}

#ifdef _WIN32

std::string utf8_of(const std::wstring& a_text) {
	std::string result;
	int size = WideCharToMultiByte(CP_UTF8, 0, a_text.c_str(), static_cast<int>(a_text.size()), nullptr, 0, nullptr, nullptr);
	if (size > 0) {
		result.resize(static_cast<size_t>(size));
		WideCharToMultiByte(CP_UTF8, 0, a_text.c_str(), static_cast<int>(a_text.size()), result.data(), size, nullptr, nullptr);
	}
	return result;
}

// an MSYS child (bash) re-reads its command line with shell rules: outside quotes only these characters mean themselves,
// and inside quotes it halves every doubled backslash, so a plain word goes bare and anything else goes quoted
bool plain_argument(const std::wstring& a_arg) {
	return !a_arg.empty() && a_arg.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-./\\:,=+%") ==
		std::wstring::npos;
}

// the quoting CommandLineToArgvW and the CRT undo: backslashes double only where a quote follows them
void append_quoted_argument(std::wstring& a_line, const std::wstring& a_arg) {
	if (!a_line.empty()) {
		a_line += L' ';
	}
	if (plain_argument(a_arg)) {
		a_line += a_arg;
		return;
	}
	a_line += L'"';
	for (auto it = a_arg.begin();; ++it) {
		size_t backslashes = 0;
		while (it != a_arg.end() && *it == L'\\') {
			++it;
			++backslashes;
		}
		if (it == a_arg.end()) {
			a_line.append(backslashes * 2, L'\\');
			break;
		}
		if (*it == L'"') {
			a_line.append(backslashes * 2 + 1, L'\\');
		} else {
			a_line.append(backslashes, L'\\');
		}
		a_line += *it;
	}
	a_line += L'"';
}

std::vector<DWORD> job_process_ids(HANDLE a_job) {
	std::vector<DWORD> result;
	std::vector<unsigned char> buffer(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + 1024 * sizeof(ULONG_PTR));
	auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer.data());
	if (QueryInformationJobObject(a_job, JobObjectBasicProcessIdList, list, static_cast<DWORD>(buffer.size()), nullptr) ||
		GetLastError() == ERROR_MORE_DATA) {
		for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
			result.push_back(static_cast<DWORD>(list->ProcessIdList[i]));
		}
	}
	return result;
}

DWORD job_active_processes(HANDLE a_job) {
	JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting {};
	if (!QueryInformationJobObject(a_job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr)) {
		return 0;
	}
	return accounting.ActiveProcesses;
}

void dump_job_processes(HANDLE a_job, const std::wstring& a_dump_dir) {
	std::error_code created;
	std::filesystem::create_directories(a_dump_dir, created);
	if (created) {
		std::fprintf(stderr, "watchdog: no minidumps: cannot create %s (%s)\n", utf8_of(a_dump_dir).c_str(), created.message().c_str());
		return;
	}
	std::wstring written_names;
	for (DWORD pid : job_process_ids(a_job)) {
		HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
		if (!process) {
			continue;
		}
		std::wstring image(MAX_PATH, L'\0');
		DWORD image_size = MAX_PATH;
		std::wstring stem = L"process";
		if (QueryFullProcessImageNameW(process, 0, image.data(), &image_size)) {
			image.resize(image_size);
			stem = image.substr(image.find_last_of(L"\\/") + 1);
			stem = stem.substr(0, stem.rfind(L'.'));
		}
		std::wstring name = stem + L"_" + std::to_wstring(pid) + L".dmp";
		std::wstring path = a_dump_dir + L"\\" + name;
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			std::fprintf(stderr, "watchdog: no minidump of %lu: cannot create %s (%lu)\n", pid, utf8_of(path).c_str(), GetLastError());
		} else {
			auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules);
			bool written = MiniDumpWriteDump(process, pid, file, type, nullptr, nullptr, nullptr) != FALSE;
			DWORD dump_error = written ? 0 : GetLastError();
			CloseHandle(file);
			if (written) {
				written_names += L" " + name;
			} else {
				DeleteFileW(path.c_str());
				std::fprintf(stderr, "watchdog: no minidump of %lu: MiniDumpWriteDump failed (0x%lX)\n", pid, dump_error);
			}
		}
		CloseHandle(process);
	}
	if (!written_names.empty()) {
		std::fprintf(stderr, "watchdog: minidumps in %s:%s\n", utf8_of(a_dump_dir).c_str(), utf8_of(written_names).c_str());
	}
}

// a killed process holds its image file until it is gone, and the next build's link needs that file
void kill_job(HANDLE a_job) {
	TerminateJobObject(a_job, timed_out_exit);
	for (int waited = 0; waited < 200 && job_active_processes(a_job) > 0; ++waited) {
		Sleep(50);
	}
}

void make_inheritable(DWORD a_which, STARTUPINFOW& a_startup, HANDLE STARTUPINFOW::* a_slot) {
	HANDLE handle = GetStdHandle(a_which);
	if (handle && handle != INVALID_HANDLE_VALUE) {
		SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
	}
	a_startup.*a_slot = handle;
}

// the caller (under MSYS, the stub process bash waits on) dying means nobody reads the verdict: the job must not outlive it
HANDLE open_parent_process() {
	HANDLE result = nullptr;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return result;
	}
	PROCESSENTRY32W entry {};
	entry.dwSize = sizeof(entry);
	DWORD self = GetCurrentProcessId();
	for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
		if (entry.th32ProcessID == self) {
			result = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ParentProcessID);
			break;
		}
	}
	CloseHandle(snapshot);
	FILETIME parent_created {}, self_created {}, unused {};
	if (result && (!GetProcessTimes(result, &parent_created, &unused, &unused, &unused) ||
		!GetProcessTimes(GetCurrentProcess(), &self_created, &unused, &unused, &unused) || CompareFileTime(&parent_created, &self_created) > 0)) {
		CloseHandle(result);
		result = nullptr;
	}
	return result;
}

int run_gate(const gate_run_options<std::wstring>& a_options) {
	HANDLE parent = open_parent_process();
	HANDLE job = CreateJobObjectW(nullptr, nullptr);
	if (!job) {
		std::fprintf(stderr, "watchdog: CreateJobObject failed (%lu)\n", GetLastError());
		return 125;
	}
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
	// DIE_ON_UNHANDLED_EXCEPTION: a crash exits at once instead of waiting on an error-report dialog
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
	if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
		std::fprintf(stderr, "watchdog: SetInformationJobObject failed (%lu)\n", GetLastError());
		return 125;
	}

	std::wstring line;
	for (const std::wstring& arg : a_options.command) {
		append_quoted_argument(line, arg);
	}
	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	make_inheritable(STD_INPUT_HANDLE, startup, &STARTUPINFOW::hStdInput);
	make_inheritable(STD_OUTPUT_HANDLE, startup, &STARTUPINFOW::hStdOutput);
	make_inheritable(STD_ERROR_HANDLE, startup, &STARTUPINFOW::hStdError);
	PROCESS_INFORMATION child {};
	if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &startup, &child)) {
		std::fprintf(stderr, "watchdog: cannot start %s (%lu)\n", utf8_of(a_options.command[0]).c_str(), GetLastError());
		return 127;
	}
	if (!AssignProcessToJobObject(job, child.hProcess)) {
		std::fprintf(stderr, "watchdog: AssignProcessToJobObject failed (%lu)\n", GetLastError());
		TerminateProcess(child.hProcess, 125);
		return 125;
	}
	ResumeThread(child.hThread);
	CloseHandle(child.hThread);

	DWORD wait_ms = a_options.timeout_seconds == 0 ? INFINITE : static_cast<DWORD>(std::min<unsigned long long>(a_options.timeout_seconds * 1000ull, INFINITE - 1));
	HANDLE waited[] {child.hProcess, parent};
	DWORD woke = WaitForMultipleObjects(parent ? 2 : 1, waited, FALSE, wait_ms);
	if (woke == WAIT_OBJECT_0 + 1) {
		std::fprintf(stderr, "watchdog: the caller of %s exited; killed it\n", utf8_of(a_options.command[0]).c_str());
		kill_job(job);
		return 125;
	}
	if (woke == WAIT_TIMEOUT) {
		std::string shown = utf8_of(line);
		if (shown.size() > 200) {
			shown = shown.substr(0, 200) + "...";
		}
		std::fprintf(stderr, "watchdog: %s exceeded %lu s\n", shown.c_str(), a_options.timeout_seconds);
		if (!a_options.dump_dir.empty()) {
			dump_job_processes(job, a_options.dump_dir);
		}
		kill_job(job);
		return timed_out_exit;
	}
	DWORD result = 0;
	GetExitCodeProcess(child.hProcess, &result);
	CloseHandle(child.hProcess);
	if (DWORD left = job_active_processes(job); left > 0) {
		std::fprintf(stderr, "watchdog: %s exited and left %lu process(es) running; killed\n", utf8_of(a_options.command[0]).c_str(), left);
		kill_job(job);
	}
	return static_cast<int>(result);
}

#endif

}

int main(int argc, char** argv) {
#ifdef _WIN32
	(void)argc;
	(void)argv;
	_setmode(_fileno(stderr), _O_BINARY);   // LF lines, like the logs they land in
	int count = 0;
	wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
	std::vector<std::wstring> args(wide, wide + count);
	LocalFree(wide);
	gate_run_options<std::wstring> options;
	if (!parse_gate_run_options(args, options)) {
		return usage();
	}
	return run_gate(options);
#else
	gate_run_options<std::string> options;
	if (!parse_gate_run_options(std::vector<std::string>(argv, argv + argc), options)) {
		return usage();
	}
	std::vector<char*> plain;
	for (std::string& arg : options.command) {
		plain.push_back(arg.data());
	}
	plain.push_back(nullptr);
	// no --foreground: with it timeout(1) kills only the program at the timeout, never the processes it started
	if (options.timeout_seconds > 0) {
		std::string seconds = std::to_string(options.timeout_seconds);
		std::vector<char*> timed {const_cast<char*>("timeout"), const_cast<char*>("-k"), const_cast<char*>("10"), seconds.data()};
		timed.insert(timed.end(), plain.begin(), plain.end());
		execvp("timeout", timed.data());
	}
	execvp(plain[0], plain.data());
	std::perror(plain[0]);
	return 127;
#endif
}
