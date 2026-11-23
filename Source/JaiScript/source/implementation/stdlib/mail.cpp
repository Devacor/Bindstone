#include <jaiscript/stdlib/mail.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/detail/class_runtime.hpp>
#include <jaiscript/detail/os_random.hpp>
#include <jaiscript/serialization/convenience.hpp>
#include <jaiscript/jaiscript.hpp>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jai {
namespace stdlib {
namespace mail_detail {

	using function_result = checked_result<script_value>;

	// mail::smtp: a relay and the defaults its messages take. No member shows the password to script
	struct smtp_relay {
		std::string host;
		int port = 587;
		smtp_security security = smtp_security::starttls;
		std::string username;
		std::string password;
		std::string from;
		std::string from_name;
		std::string reply_to;
		double timeout_seconds = 30.0;
	};

	[[noreturn]] JAI_NOINLINE void refuse(const std::string& a_text) { throw runtime_error(a_text); }

	// name@domain in visible ASCII, at most 254 bytes, without a character that would end or split an address in a header
	bool is_address(std::string_view a_text) {
		const size_t at = a_text.find('@');
		if (a_text.size() > 254 || at == 0 || at == std::string_view::npos || at + 1 == a_text.size() || a_text.find('@', at + 1) != std::string_view::npos) { return false; }
		return std::all_of(a_text.begin(), a_text.end(), [](char a_c) { return a_c > ' ' && a_c <= '~' && std::string_view("<>()[]\\,;:\"").find(a_c) == std::string_view::npos; });
	}

	std::string address(const script_value& a_value, const std::string& a_field) {
		if (!a_value.is_text() || !is_address(a_value.unchecked_text())) {
			refuse(a_field + " holds a value that is not an email address (name@domain in ASCII, without spaces, quotes, brackets, commas or line breaks)");
		}
		return a_value.unchecked_text();
	}

	// one address, or an array of them
	void addresses(engine* a_engine, const script_value& a_value, const std::string& a_field, std::vector<std::string>& a_out) {
		if (!a_value.is_array()) {
			a_out.push_back(address(a_value, a_field));
			return;
		}
		std::vector<script_value> items;
		a_value.unchecked_array_node()->for_each(a_engine, [&](const script_value& a_item) { items.push_back(a_item.deref()); });
		for (const script_value& item : items) { a_out.push_back(address(item, a_field)); }
	}

	// a_header: the text goes in a header, so a line break or NUL in it would start a header of the script's choosing
	std::string text_field(const script_value& a_value, const std::string& a_field, bool a_header) {
		if (!a_value.is_text()) { refuse(a_field + " takes text"); }
		if (a_header && a_value.unchecked_text().find_first_of(std::string_view("\r\n\0", 3)) != std::string::npos) { refuse(a_field + " holds a line break or NUL"); }
		return a_value.unchecked_text();
	}

	smtp_relay read_relay(const script_value& a_options) {
		static const char* const k_options = "(host, port, security, username, password, from, from_name, reply_to, timeout)";
		if (!a_options.is_map()) { refuse(std::string("mail::smtp: the options are a map ") + k_options); }
		smtp_relay result;
		bool port_given = false;
		for (const auto& [key, raw] : a_options.as_map()) {
			const std::string* name = key.text_bytes();
			const script_value& value = raw.deref();
			const std::string option = "mail::smtp: option '" + (name ? *name : std::string()) + "'";
			if (name && *name == "host") {
				const std::string host = value.is_text() ? value.unchecked_text() : std::string();
				if (host.empty() || !std::all_of(host.begin(), host.end(), [](char a_c) { return a_c > ' ' && a_c <= '~'; })) { refuse(option + " takes a host name or address"); }
				result.host = host;
			} else if (name && *name == "port") {
				if (!value.is_int() || value.as_int() < 1 || value.as_int() > 65535) { refuse(option + " takes an int from 1 to 65535"); }
				result.port = static_cast<int>(value.as_int());
				port_given = true;
			} else if (name && *name == "security") {
				const std::string security = value.is_text() ? value.unchecked_text() : std::string();
				if (security != "starttls" && security != "tls" && security != "none") { refuse(option + " takes \"starttls\", \"tls\" or \"none\""); }
				result.security = security == "tls" ? smtp_security::tls : security == "none" ? smtp_security::none : smtp_security::starttls;
			} else if (name && *name == "username") {
				result.username = text_field(value, option, true);
			} else if (name && *name == "password") {
				result.password = text_field(value, option, true);
			} else if (name && *name == "from") {
				result.from = address(value, option);
			} else if (name && *name == "from_name") {
				result.from_name = text_field(value, option, true);
			} else if (name && *name == "reply_to") {
				result.reply_to = address(value, option);
			} else if (name && *name == "timeout") {
				const double seconds = value.is_int() ? static_cast<double>(value.as_int()) : value.is_float() ? value.as_float() : -1.0;
				if (!(seconds > 0.0)) { refuse(option + " takes a number of seconds above 0"); }
				result.timeout_seconds = seconds;
			} else {
				refuse("mail::smtp: unknown option '" + (name ? *name : std::string("?")) + "' " + k_options);
			}
		}
		if (result.host.empty()) { refuse("mail::smtp: the options need 'host'"); }
		if (result.username.empty() != result.password.empty()) { refuse("mail::smtp: options 'username' and 'password' go together: give both or neither"); }
		if (!port_given) { result.port = result.security == smtp_security::tls ? 465 : result.security == smtp_security::none ? 25 : 587; }
		return result;
	}

	// Header text goes as it is when it is printable ASCII short enough to keep its line far under SMTP's 998 octets
	bool is_plain(std::string_view a_text) {
		return a_text.size() <= 300 && std::all_of(a_text.begin(), a_text.end(), [](char a_c) { return a_c >= ' ' && a_c <= '~'; });
	}

	// Any other as RFC 2047 encoded words: base64, at most 45 bytes each, never splitting a UTF-8 sequence, each on a line of its own
	std::string encoded_words(std::string_view a_text) {
		std::string result;
		for (size_t start = 0; start < a_text.size();) {
			size_t end = std::min(start + 45, a_text.size());
			for (int back = 0; back < 3 && end < a_text.size() && (static_cast<unsigned char>(a_text[end]) & 0xC0) == 0x80; ++back) { --end; }
			if (!result.empty()) { result += "\r\n "; }
			result += "=?utf-8?B?" + detail::base64_encode(std::string(a_text.substr(start, end - start))) + "?=";
			start = end;
		}
		return result;
	}

	std::string mailbox(const std::string& a_name, const std::string& a_address) {
		if (a_name.empty()) { return a_address; }
		if (!is_plain(a_name)) { return encoded_words(a_name) + " <" + a_address + ">"; }
		std::string quoted = "\"";
		for (char c : a_name) {
			if (c == '"' || c == '\\') { quoted += '\\'; }
			quoted += c;
		}
		return quoted + "\" <" + a_address + ">";
	}

	std::string mailbox_list(const std::vector<std::string>& a_addresses, const std::string& a_first_name) {
		std::string result;
		for (size_t i = 0; i < a_addresses.size(); ++i) {
			if (i) { result += ",\r\n "; }
			result += mailbox(i == 0 ? a_first_name : std::string(), a_addresses[i]);
		}
		return result;
	}

	// RFC 2045 6.7: line breaks as CRLF, every other byte but printable ASCII as =XX, encoded lines of at most 76 characters
	std::string quoted_printable(std::string_view a_text) {
		static constexpr char k_hex[] = "0123456789ABCDEF";
		std::string result;
		size_t column = 0;
		for (size_t i = 0; i < a_text.size(); ++i) {
			const unsigned char c = static_cast<unsigned char>(a_text[i]);
			if (c == '\r' || c == '\n') {
				if (c == '\r' && i + 1 < a_text.size() && a_text[i + 1] == '\n') { ++i; }
				result += "\r\n";
				column = 0;
				continue;
			}
			const bool line_end = i + 1 == a_text.size() || a_text[i + 1] == '\r' || a_text[i + 1] == '\n';
			const bool literal = (c >= '!' && c <= '~' && c != '=') || ((c == ' ' || c == '\t') && !line_end);
			const size_t width = literal ? 1 : 3;
			if (column + width > 75) {
				result += "=\r\n";
				column = 0;
			}
			if (literal) {
				result += static_cast<char>(c);
			} else {
				result += '=';
				result += k_hex[c >> 4];
				result += k_hex[c & 15];
			}
			column += width;
		}
		return result;
	}

	std::string body_part(const char* a_type, const std::string& a_text) {
		return std::string("Content-Type: ") + a_type + "; charset=utf-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n" + quoted_printable(a_text) + "\r\n";
	}

	// RFC 5322 3.3, in UTC
	std::string message_date(std::time_t a_when) {
		std::tm parts {};
#ifdef _WIN32
		gmtime_s(&parts, &a_when);
#else
		gmtime_r(&a_when, &parts);
#endif
		static const char* const k_days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
		static const char* const k_months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
		char text[40];
		std::snprintf(text, sizeof(text), "%s, %02d %s %04d %02d:%02d:%02d +0000", k_days[parts.tm_wday % 7], parts.tm_mday,
			k_months[parts.tm_mon % 12], parts.tm_year + 1900, parts.tm_hour, parts.tm_min, parts.tm_sec);
		return text;
	}

	std::string random_hex() {
		unsigned char bytes[16];
		if (!detail::os_random_bytes(bytes, sizeof(bytes))) { refuse("mail::smtp.send: the operating system's secure random generator failed"); }
		static constexpr char k_hex[] = "0123456789abcdef";
		std::string result;
		for (unsigned char byte : bytes) {
			result += k_hex[byte >> 4];
			result += k_hex[byte & 15];
		}
		return result;
	}

	std::string seconds_text(double a_seconds) {
		char text[32];
		std::snprintf(text, sizeof(text), "%g", a_seconds);
		return text;
	}

	// One message: its fields read and checked, the MIME message composed, the conversation run through the host's transport;
	// the result is the server's reply to the message
	std::string send_message(engine* a_engine, const std::shared_ptr<smtp_transport>& a_transport, const smtp_relay& a_relay, const script_value& a_message) {
		static const char* const k_fields = "(to, to_name, cc, bcc, from, from_name, reply_to, subject, text, html)";
		if (!a_message.is_map()) { refuse(std::string("mail::smtp.send: the message is a map ") + k_fields); }
		std::vector<std::string> to;
		std::vector<std::string> cc;
		std::vector<std::string> bcc;
		std::string to_name;
		std::string from = a_relay.from;
		std::string from_name = a_relay.from_name;
		std::string reply_to = a_relay.reply_to;
		std::string subject;
		std::string plain;
		std::string html;
		bool has_plain = false;
		bool has_html = false;
		for (const auto& [key, raw] : a_message.as_map()) {
			const std::string* name = key.text_bytes();
			const script_value& value = raw.deref();
			const std::string field = "mail::smtp.send: '" + (name ? *name : std::string()) + "'";
			if (name && *name == "to") {
				addresses(a_engine, value, field, to);
			} else if (name && *name == "cc") {
				addresses(a_engine, value, field, cc);
			} else if (name && *name == "bcc") {
				addresses(a_engine, value, field, bcc);
			} else if (name && *name == "to_name") {
				to_name = text_field(value, field, true);
			} else if (name && *name == "from") {
				from = address(value, field);
			} else if (name && *name == "from_name") {
				from_name = text_field(value, field, true);
			} else if (name && *name == "reply_to") {
				reply_to = address(value, field);
			} else if (name && *name == "subject") {
				subject = text_field(value, field, true);
			} else if (name && *name == "text") {
				plain = text_field(value, field, false);
				has_plain = true;
			} else if (name && *name == "html") {
				html = text_field(value, field, false);
				has_html = true;
			} else {
				refuse("mail::smtp.send: unknown field '" + (name ? *name : std::string("?")) + "' " + k_fields);
			}
		}
		if (to.empty()) { refuse("mail::smtp.send: the message needs 'to'"); }
		if (from.empty()) { refuse("mail::smtp.send: the message needs 'from', in it or in mail::smtp's options"); }
		if (!has_plain && !has_html) { refuse("mail::smtp.send: the message needs 'text', 'html' or both"); }
		if (!to_name.empty() && to.size() != 1) { refuse("mail::smtp.send: 'to_name' names a single 'to': with an array of addresses, leave it out"); }
		if (!a_transport) { refuse("mail::smtp.send: no SMTP transport installed: the host passes one to jai::stdlib::register_mail"); }

		const std::string domain = from.substr(from.find('@') + 1);
		std::string message = "Date: " + message_date(std::time(nullptr)) + "\r\nFrom: " + mailbox(from_name, from) + "\r\n";
		if (!reply_to.empty()) { message += "Reply-To: " + reply_to + "\r\n"; }
		message += "To: " + mailbox_list(to, to_name) + "\r\n";
		if (!cc.empty()) { message += "Cc: " + mailbox_list(cc, std::string()) + "\r\n"; }
		if (!subject.empty()) { message += "Subject: " + (is_plain(subject) ? subject : encoded_words(subject)) + "\r\n"; }
		message += "Message-ID: <" + random_hex() + "@" + domain + ">\r\nMIME-Version: 1.0\r\n";
		if (has_plain && has_html) {
			// "=_" never occurs in quoted-printable text, so no part can hold the boundary
			const std::string boundary = "=_" + random_hex();
			message += "Content-Type: multipart/alternative; boundary=\"" + boundary + "\"\r\n\r\n--" + boundary + "\r\n" + body_part("text/plain", plain) + "--" + boundary + "\r\n" +
				body_part("text/html", html) + "--" + boundary + "--\r\n";
		} else {
			message += has_html ? body_part("text/html", html) : body_part("text/plain", plain);
		}

		smtp_request request;
		request.host = a_relay.host;
		request.port = a_relay.port;
		request.security = a_relay.security;
		request.username = a_relay.username;
		request.password = a_relay.password;
		request.hello = domain;
		request.from = from;
		request.recipients = to;
		request.recipients.insert(request.recipients.end(), cc.begin(), cc.end());
		request.recipients.insert(request.recipients.end(), bcc.begin(), bcc.end());
		request.message = std::move(message);
		request.timeout_seconds = a_relay.timeout_seconds;
		smtp_reply reply;
		try {
			reply = a_transport->send(request);
		} catch (const std::exception& e) {
			reply = smtp_reply {};
			reply.error = e.what();
		}
		if (!reply.error.empty()) {
			const std::string endpoint = (a_relay.host.find(':') != std::string::npos ? "[" + a_relay.host + "]" : a_relay.host) + ":" + std::to_string(a_relay.port);
			refuse("mail::smtp.send: " + endpoint + ": " + (reply.error == "timed out" ? "timed out after " + seconds_text(a_relay.timeout_seconds) + " s" : reply.error));
		}
		return reply.reply;
	}

} // namespace mail_detail

	void register_mail(engine& eng_ref, std::shared_ptr<smtp_transport> a_transport) {
		using namespace mail_detail;
		using svt = script_value_type;
		engine* eng = &eng_ref;

		dynamic_binder<smtp_relay>(eng_ref, "mail::smtp")
			.method("send", [eng, a_transport](const smtp_relay& a_self, const script_value& a_message) -> std::string { return send_message(eng, a_transport, a_self, a_message.deref()); },
				permissions::network)
			.build();

		detail::engine_access::serial_namespace(eng_ref, "mail");
		auto& ns = detail::engine_access::script_namespaces(eng_ref)[detail::engine_access::symbolize(eng_ref, "mail")];
		if (!ns) { ns = std::make_shared<script_namespace_data>(); }
		const uint64_t id = detail::engine_access::symbolize(eng_ref, "smtp");
		ns->variables[id] = script_value::make_function([eng](const std::vector<script_value>& a_args) -> function_result {
			if (a_args.size() != 1) { return function_result(make_error_code(runtime_error_code::argument_count_mismatch), "mail::smtp expects 1 argument (options)"); }
			return detail::engine_access::make_value_object(*eng, std::make_shared<smtp_relay>(read_relay(a_args[0].deref())));
		}, &eng_ref);
		host_entry_signature signature;
		signature.min_args = 1;
		signature.max_args = 1;
		signature.params = {svt::jai_map_type};
		signature.result = svt::jai_object_type;
		signature.result_class = "mail::smtp";
		ns->signatures[id] = std::move(signature);
	}

} // namespace stdlib
} // namespace jai
