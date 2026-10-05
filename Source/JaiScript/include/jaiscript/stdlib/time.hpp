#pragma once

#ifndef __JAISCRIPT_STDLIB_TIME_HPP__
#define __JAISCRIPT_STDLIB_TIME_HPP__

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/detail/format_spec.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace jai {
namespace stdlib {

    // time::stopwatch: running seconds, read from the engine's monotonic clock once per call; paused
    // intervals never count
    struct stopwatch {
        engine* eng = nullptr;
        std::chrono::nanoseconds accumulated{};   // running time before run_start
        std::chrono::nanoseconds run_start{};
        std::chrono::nanoseconds delta_mark{};    // elapsed at the last delta()
        bool running = false;
        bool paused = false;

        explicit stopwatch(engine* e) : eng(e) {}

        static double seconds(std::chrono::nanoseconds n) { return std::chrono::duration<double>(n).count(); }
        bool idle() const { return !running && !paused; }
        std::chrono::nanoseconds elapsed_now() const { return running ? accumulated + (eng->monotonic_now() - run_start) : accumulated; }

        void start() {
            accumulated = {};
            delta_mark = {};
            run_start = eng->monotonic_now();
            running = true;
            paused = false;
        }
        double stop() {
            if (idle()) return 0.0;
            const auto e = elapsed_now();
            accumulated = {};
            delta_mark = {};
            running = paused = false;
            return seconds(e);
        }
        double delta() {
            if (idle()) { start(); return 0.0; }
            const auto e = elapsed_now();
            const auto d = e - delta_mark;
            delta_mark = e;
            return seconds(d);
        }
        double elapsed() const { return idle() ? 0.0 : seconds(elapsed_now()); }
        double pause() {
            if (running) {
                accumulated += eng->monotonic_now() - run_start;
                running = false;
                paused = true;
            }
            return seconds(accumulated);
        }
        double resume() {
            if (idle()) { start(); return 0.0; }
            if (running) return seconds(elapsed_now());
            run_start = eng->monotonic_now();
            running = true;
            paused = false;
            return seconds(accumulated);
        }
    };

    namespace time_calendar {
        constexpr int64_t floor_div(int64_t a, int64_t b) noexcept { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
        constexpr bool is_leap(int64_t y) noexcept { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
        constexpr int64_t days_in_month(int64_t y, int64_t m) noexcept {
            constexpr int8_t lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return m == 2 && is_leap(y) ? 29 : lengths[m - 1];
        }
        // days since 1970-01-01 in the proleptic Gregorian calendar (H. Hinnant's civil algorithms)
        constexpr int64_t days_from_civil(int64_t y, int64_t m, int64_t d) noexcept {
            y -= m <= 2;
            const int64_t era = (y >= 0 ? y : y - 399) / 400;
            const int64_t yoe = y - era * 400;
            const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + doe - 719468;
        }
        struct civil { int64_t year, month, day; };
        constexpr civil civil_from_days(int64_t z) noexcept {
            z += 719468;
            const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
            const int64_t doe = z - era * 146097;
            const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const int64_t mp = (5 * doy + 2) / 153;
            const int64_t d = doy - (153 * mp + 2) / 5 + 1;
            const int64_t m = mp < 10 ? mp + 3 : mp - 9;
            return {yoe + era * 400 + (m <= 2), m, d};
        }
        constexpr int64_t min_year = 1;
        constexpr int64_t max_year = 9999;
        constexpr int64_t min_unix_ms = days_from_civil(min_year, 1, 1) * 86400000;
        constexpr int64_t max_unix_ms = (days_from_civil(max_year, 12, 31) + 1) * 86400000 - 1;

        inline const char* month_name(int64_t m) {
            static const char* const names[] = {"January", "February", "March", "April", "May", "June", "July",
                                                "August", "September", "October", "November", "December"};
            return names[m - 1];
        }
        inline const char* weekday_name(int64_t w) {
            static const char* const names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
            return names[w];
        }
    }

    // time::date: a calendar date and time of day to the millisecond, in UTC or in the local zone
    struct date {
        int64_t year = 1970;
        int64_t month = 1;
        int64_t day = 1;
        int64_t hour = 0;
        int64_t minute = 0;
        int64_t second = 0;
        int64_t millisecond = 0;
        bool utc = true;
        int8_t dst = -1;   // the zone's daylight flag when read from an instant: keeps a local round trip exact in a repeated hour

        int64_t days() const { return time_calendar::days_from_civil(year, month, day); }
        int64_t weekday() const { const int64_t w = (days() + 4) % 7; return w < 0 ? w + 7 : w; }   // 0 = Sunday; 1970-01-01 was a Thursday
        int64_t yearday() const { return days() - time_calendar::days_from_civil(year, 1, 1) + 1; }

        static date from_unix_ms_utc(int64_t ms) {
            date d;
            const int64_t days = time_calendar::floor_div(ms, 86400000);
            int64_t rest = ms - days * 86400000;
            const auto c = time_calendar::civil_from_days(days);
            d.year = c.year; d.month = c.month; d.day = c.day;
            d.hour = rest / 3600000; rest %= 3600000;
            d.minute = rest / 60000; rest %= 60000;
            d.second = rest / 1000;
            d.millisecond = rest % 1000;
            return d;
        }
        // false when the platform cannot place the instant in the local zone
        static bool from_unix_ms_local(int64_t ms, date& out) {
            const int64_t secs = time_calendar::floor_div(ms, 1000);
            const std::time_t t = static_cast<std::time_t>(secs);
            if (static_cast<int64_t>(t) != secs) return false;
            std::tm tm{};
#ifdef _WIN32
            if (localtime_s(&tm, &t) != 0) return false;
#else
            if (!localtime_r(&t, &tm)) return false;
#endif
            out.year = tm.tm_year + 1900; out.month = tm.tm_mon + 1; out.day = tm.tm_mday;
            out.hour = tm.tm_hour; out.minute = tm.tm_min; out.second = tm.tm_sec > 59 ? 59 : tm.tm_sec;
            out.millisecond = ms - secs * 1000;
            out.utc = false;
            out.dst = static_cast<int8_t>(tm.tm_isdst > 0 ? 1 : 0);
            return true;
        }
        static bool from_unix_ms(int64_t ms, bool in_utc, date& out) {
            if (ms < time_calendar::min_unix_ms || ms > time_calendar::max_unix_ms) return false;
            if (in_utc) { out = from_unix_ms_utc(ms); return true; }
            return from_unix_ms_local(ms, out) && out.year >= time_calendar::min_year && out.year <= time_calendar::max_year;
        }
        bool unix_ms(int64_t& out) const {
            if (utc) {
                out = days() * 86400000 + hour * 3600000 + minute * 60000 + second * 1000 + millisecond;
                return true;
            }
            std::tm tm{};
            tm.tm_year = static_cast<int>(year - 1900); tm.tm_mon = static_cast<int>(month - 1); tm.tm_mday = static_cast<int>(day);
            tm.tm_hour = static_cast<int>(hour); tm.tm_min = static_cast<int>(minute); tm.tm_sec = static_cast<int>(second);
            tm.tm_isdst = dst;
            const std::time_t t = std::mktime(&tm);
            if (t == static_cast<std::time_t>(-1) && !(year == 1969 && month == 12 && day == 31)) return false;
            out = static_cast<int64_t>(t) * 1000 + millisecond;
            return true;
        }
        int64_t unix_ms_or_throw() const {
            int64_t ms = 0;
            if (!unix_ms(ms)) throw runtime_error("time::date: " + to_string() + " is outside the local time the platform can represent");
            return ms;
        }
        date in_zone(bool in_utc) const {
            date out;
            if (!from_unix_ms(unix_ms_or_throw(), in_utc, out)) throw runtime_error("time::date: " + to_string() + " is outside the supported range (years 1 to 9999)");
            return out;
        }
        date plus_ms(int64_t delta) const {
            const int64_t ms = unix_ms_or_throw();
            date out;
            if ((delta > 0 && ms > time_calendar::max_unix_ms - delta) || (delta < 0 && ms < time_calendar::min_unix_ms - delta) ||
                !from_unix_ms(ms + delta, utc, out)) {
                throw runtime_error("time::date: the result is outside the supported range (years 1 to 9999)");
            }
            return out;
        }
        date plus_days(int64_t n) const {
            const int64_t limit = time_calendar::days_from_civil(time_calendar::max_year, 12, 31) - time_calendar::days_from_civil(time_calendar::min_year, 1, 1);
            if (n > limit || n < -limit) throw runtime_error("time::date: the result is outside the supported range (years 1 to 9999)");
            const auto c = time_calendar::civil_from_days(days() + n);
            if (c.year < time_calendar::min_year || c.year > time_calendar::max_year) throw runtime_error("time::date: the result is outside the supported range (years 1 to 9999)");
            date out = *this;
            out.year = c.year; out.month = c.month; out.day = c.day; out.dst = -1;
            return out;
        }
        // calendar months: the day clamps to the last day of the target month (Jan 31 + 1 month = Feb 28 or 29)
        date plus_months(int64_t n) const {
            const int64_t limit = (time_calendar::max_year - time_calendar::min_year + 1) * 12;
            if (n > limit || n < -limit) throw runtime_error("time::date: the result is outside the supported range (years 1 to 9999)");
            const int64_t index = year * 12 + (month - 1) + n;
            const int64_t y = time_calendar::floor_div(index, 12);
            if (y < time_calendar::min_year || y > time_calendar::max_year) throw runtime_error("time::date: the result is outside the supported range (years 1 to 9999)");
            date out = *this;
            out.year = y;
            out.month = index - y * 12 + 1;
            out.day = day > time_calendar::days_in_month(y, out.month) ? time_calendar::days_in_month(y, out.month) : day;
            out.dst = -1;
            return out;
        }

        std::string two(int64_t v) const { return (v < 10 ? "0" : "") + std::to_string(v); }
        std::string to_string() const {
            std::string y = std::to_string(year);
            while (y.size() < 4) y.insert(y.begin(), '0');
            std::string ms = std::to_string(millisecond);
            while (ms.size() < 3) ms.insert(ms.begin(), '0');
            return y + "-" + two(month) + "-" + two(day) + "T" + two(hour) + ":" + two(minute) + ":" + two(second) + "." + ms + (utc ? "Z" : "");
        }

        // A field store checks its own range, and a store that leaves the day past the month's end is refused;
        // each setter answers the refusal text, empty when the store happened
        static std::string range_error(const char* field, int64_t v, int64_t lo, int64_t hi) {
            if (v >= lo && v <= hi) return {};
            return std::string("time::date: ") + field + " must be " + std::to_string(lo) + " to " + std::to_string(hi) + ", got " + std::to_string(v);
        }
        static std::string day_error(int64_t y, int64_t m, int64_t d) {
            if (d <= time_calendar::days_in_month(y, m)) return {};
            std::string ys = std::to_string(y);
            while (ys.size() < 4) ys.insert(ys.begin(), '0');
            return "time::date: day " + std::to_string(d) + " does not exist in " + ys + "-" + (m < 10 ? "0" : "") + std::to_string(m);
        }
        std::string store(int64_t& field, int64_t v, std::string error) {
            if (error.empty()) { field = v; dst = -1; }
            return error;
        }
        std::string set_year(int64_t v) {
            std::string e = range_error("year", v, time_calendar::min_year, time_calendar::max_year);
            return store(year, v, e.empty() ? day_error(v, month, day) : e);
        }
        std::string set_month(int64_t v) {
            std::string e = range_error("month", v, 1, 12);
            return store(month, v, e.empty() ? day_error(year, v, day) : e);
        }
        std::string set_day(int64_t v) {
            std::string e = range_error("day", v, 1, 31);
            return store(day, v, e.empty() ? day_error(year, month, v) : e);
        }
        std::string set_hour(int64_t v) { return store(hour, v, range_error("hour", v, 0, 23)); }
        std::string set_minute(int64_t v) { return store(minute, v, range_error("minute", v, 0, 59)); }
        std::string set_second(int64_t v) { return store(second, v, range_error("second", v, 0, 59)); }
        std::string set_millisecond(int64_t v) { return store(millisecond, v, range_error("millisecond", v, 0, 999)); }
    };

    // Expands ${name} / ${name:spec} placeholders (the template-string spec mini-language); an empty result
    // means success, else the error text
    inline std::string format_date(engine* eng, const date& d, std::string_view tmpl, const char* who, std::string& out) {
        out.clear();
        size_t at = 0;
        while (at < tmpl.size()) {
            const size_t open = tmpl.find("${", at);
            if (open == std::string_view::npos) { out.append(tmpl.substr(at)); break; }
            out.append(tmpl.substr(at, open - at));
            const size_t close = tmpl.find('}', open + 2);
            if (close == std::string_view::npos) {
                return std::string(who) + ": unterminated placeholder '" + std::string(tmpl.substr(open)) + "'";
            }
            const std::string_view body = tmpl.substr(open + 2, close - open - 2);
            const size_t colon = body.find(':');
            const std::string_view name = body.substr(0, colon);
            const bool has_spec = colon != std::string_view::npos;
            const std::string_view spec = has_spec ? body.substr(colon + 1) : std::string_view{};
            int64_t number = 0;
            const char* text = nullptr;
            if (name == "year") number = d.year;
            else if (name == "month") number = d.month;
            else if (name == "month_name") text = time_calendar::month_name(d.month);
            else if (name == "month_short") text = time_calendar::month_name(d.month);
            else if (name == "day") number = d.day;
            else if (name == "weekday_name" || name == "weekday_short") text = time_calendar::weekday_name(d.weekday());
            else if (name == "hours") number = d.hour % 12 == 0 ? 12 : d.hour % 12;
            else if (name == "military_hours") number = d.hour;
            else if (name == "minutes") number = d.minute;
            else if (name == "seconds") number = d.second;
            else if (name == "milliseconds") number = d.millisecond;
            else if (name == "am_pm") text = d.hour < 12 ? "AM" : "PM";
            else return std::string(who) + ": unknown placeholder '${" + std::string(name) + "}'";
            std::string value = text ? std::string(text) : std::to_string(number);
            if (text && (name == "month_short" || name == "weekday_short")) value.resize(3);
            if (!has_spec) { out += value; }
            else {
                detail::format_spec fs;
                if (!detail::parse_format_spec(spec, fs)) {
                    return std::string(who) + ": unsupported format spec ':" + std::string(spec) + "' in '${" + std::string(body) + "}'";
                }
                try {
                    out += detail::format_value_with_spec(text ? script_value(value, eng) : script_value(number, eng), fs, spec);
                } catch (const std::exception& e) {
                    return std::string(who) + ": " + e.what();
                }
            }
            at = close + 1;
        }
        return {};
    }

    // time:: namespace (docs/site/guide/14-time.html): clocks through the engine's time source
    // (engine::time_source), stopwatches and calendar dates
    inline void register_time_functions(engine& eng_ref) {
        engine* eng = &eng_ref;
        using R = checked_result<script_value>;
        using args_t = std::vector<const script_value*>;

        dynamic_binder<stopwatch>(eng_ref, "time::stopwatch")
            .constructor<>()
            .method("start", [](stopwatch& s) { s.start(); })
            .method("stop", [](stopwatch& s) { return s.stop(); })
            .method("delta", [](stopwatch& s) { return s.delta(); })
            .method("elapsed", [](const stopwatch& s) { return s.elapsed(); })
            .method("pause", [](stopwatch& s) { return s.pause(); })
            .method("resume", [](stopwatch& s) { return s.resume(); })
            .method("is_running", [](const stopwatch& s) { return s.running; })
            .method("is_paused", [](const stopwatch& s) { return s.paused; })
            .build();

        auto whole = [](script_float v, const char* what) -> int64_t {
            if (!std::isfinite(v) || std::floor(v) != v || std::fabs(v) > 1e15) {
                throw runtime_error(std::string("time::date::") + what + " expects a whole number");
            }
            return static_cast<int64_t>(v);
        };
        auto to_ms = [](script_float v, double scale, const char* what) -> int64_t {
            const double ms = v * scale;
            if (!std::isfinite(ms) || std::fabs(ms) > 1e15) throw runtime_error(std::string("time::date::") + what + " is out of range");
            return static_cast<int64_t>(std::llround(ms));
        };
        auto instant = [](const date& d) { return d.unix_ms_or_throw(); };
        // The field setters and the operators are registered below as checked functions: a refused store or a
        // non-date operand raises a catchable error (a throw from those call sites would leave the script)
        auto unused_setter = [](date&, script_int) {};
        dynamic_binder<date>(eng_ref, "time::date")
            .property("year", [](const date& d) -> script_int { return d.year; }, unused_setter)
            .property("month", [](const date& d) -> script_int { return d.month; }, unused_setter)
            .property("day", [](const date& d) -> script_int { return d.day; }, unused_setter)
            .property("hour", [](const date& d) -> script_int { return d.hour; }, unused_setter)
            .property("minute", [](const date& d) -> script_int { return d.minute; }, unused_setter)
            .property("second", [](const date& d) -> script_int { return d.second; }, unused_setter)
            .property("millisecond", [](const date& d) -> script_int { return d.millisecond; }, unused_setter)
            .property("weekday", [](const date& d) -> script_int { return d.weekday(); }, nullptr)
            .property("yearday", [](const date& d) -> script_int { return d.yearday(); }, nullptr)
            .property("utc", [](const date& d) -> bool { return d.utc; }, nullptr)
            .method("unix", [instant](const date& d) -> script_float { return static_cast<script_float>(instant(d)) / 1000.0; })
            .method("unix_ms", [instant](const date& d) -> script_int { return instant(d); })
            .method("format", [eng](const date& d, const std::string& tmpl) -> std::string {
                std::string out;
                const std::string error = format_date(eng, d, tmpl, "time::date::format", out);
                if (!error.empty()) throw runtime_error(error);
                return out;
            })
            .method("to_string", [](const date& d) { return d.to_string(); })
            .method("to_utc", [](const date& d) { return d.in_zone(true); })
            .method("to_local", [](const date& d) { return d.in_zone(false); })
            .method("add_days", [whole](const date& d, script_float n) { return d.plus_days(whole(n, "add_days")); })
            .method("add_months", [whole](const date& d, script_float n) { return d.plus_months(whole(n, "add_months")); })
            .method("add_years", [whole](const date& d, script_float n) { return d.plus_months(whole(n, "add_years") * 12); })
            .method("add_hours", [to_ms](const date& d, script_float n) { return d.plus_ms(to_ms(n, 3600000.0, "add_hours")); })
            .method("add_minutes", [to_ms](const date& d, script_float n) { return d.plus_ms(to_ms(n, 60000.0, "add_minutes")); })
            .method("add_seconds", [to_ms](const date& d, script_float n) { return d.plus_ms(to_ms(n, 1000.0, "add_seconds")); })
            .method("between", [instant](const date& d, const date& a, const date& b) {
                const int64_t t = instant(d), x = instant(a), y = instant(b);
                return x <= y ? (x <= t && t <= y) : (y <= t && t <= x);
            })
            .build();

        const auto date_class = eng_ref.get_class_definition("time::date");
        const uint64_t date_type = date_class->get_type_id();
        auto as_date = [date_type](const script_value& v) -> date* {
            auto native = detail::checked_native_reference(v.deref(), typeid(date), date_type);
            return native ? static_cast<date*>(native.value()) : nullptr;
        };
        auto checked_text = [eng](const std::string& text) { return R(make_error_code(runtime_error_code::invalid_operation), "{0}", eng->park_error_text(text)); };
        for (const auto& [field, set] : {std::pair<const char*, std::string (date::*)(int64_t)>{"year", &date::set_year}, {"month", &date::set_month},
                                         {"day", &date::set_day}, {"hour", &date::set_hour}, {"minute", &date::set_minute},
                                         {"second", &date::set_second}, {"millisecond", &date::set_millisecond}}) {
            date_class->add_method(std::string("_set_") + field, [=](const std::vector<script_value>& a) -> R {
                date* d = a.size() == 2 ? as_date(a[0]) : nullptr;
                if (!d) return R(make_error_code(runtime_error_code::type_mismatch), "time::date: a field store needs a date and one value");
                const script_value& v = a[1].deref();
                if (!v.is_int()) return checked_text(std::string("time::date: ") + field + " takes an int, got " + detail::slot_source_type_name(v, eng));
                const std::string error = (d->*set)(v.as_int());
                if (!error.empty()) return checked_text(error);
                return script_value(std::monostate{}, eng);
            });
        }
        struct date_operator { const char* token; int kind; };   // kind: 0 compare, 1 equality, 2 subtraction
        for (const date_operator op : {date_operator{"<", 0}, {"<=", 0}, {">", 0}, {">=", 0}, {"==", 1}, {"!=", 1}, {"-", 2}}) {
            const std::string token = op.token;
            date_class->add_method(token, [=](const std::vector<script_value>& a) -> R {
                date* x = a.size() == 2 ? as_date(a[0]) : nullptr;
                date* y = a.size() == 2 ? as_date(a[1]) : nullptr;
                if (!x || !y) {
                    if (op.kind == 1 && x) return script_value(token == "!=", eng);
                    return checked_text("time::date: '" + token + "' needs two dates, got " + (a.size() == 2 ? detail::slot_source_type_name(a[1], eng) : std::string("no operand")));
                }
                int64_t tx = 0, ty = 0;
                if (!x->unix_ms(tx) || !y->unix_ms(ty)) return checked_text("time::date: a date is outside the local time the platform can represent");
                if (op.kind == 2) return script_value(static_cast<script_float>(tx - ty) / 1000.0, eng);
                const bool r = token == "<" ? tx < ty : token == "<=" ? tx <= ty : token == ">" ? tx > ty : token == ">=" ? tx >= ty : token == "==" ? tx == ty : tx != ty;
                return script_value(r, eng);
            });
        }

        auto& ns = eng_ref.script_namespaces()[eng_ref.symbolize("time")];
        if (!ns) { ns = std::make_shared<script_namespace_data>(); }
        using svt = script_value_type;
        auto sig = [](uint8_t min_args, uint8_t max_args, std::vector<svt> params, svt result, const char* result_class = "") {
            host_entry_signature s;
            s.min_args = min_args;
            s.max_args = max_args;
            s.params = std::move(params);
            s.result = result;
            s.result_class = result_class;
            return s;
        };

        auto def = [&](const char* name, host_entry_signature signature, auto fn) {
            const uint64_t id = eng_ref.symbolize(name);
            ns->variables[id] = script_value::make_function([eng, fn](const std::vector<script_value>& raw) -> R {
                args_t args;
                args.reserve(raw.size());
                for (const auto& a : raw) { args.push_back(&a.deref()); }
                return fn(eng, args);
            }, &eng_ref);
            ns->signatures[id] = std::move(signature);
        };
        auto count_error = [](const char* text) { return R(make_error_code(runtime_error_code::argument_count_mismatch), text); };   // static texts: the result holds a view
        auto type_error = [](const char* text) { return R(make_error_code(runtime_error_code::type_mismatch), text); };
        auto value_error = [](engine* e, const std::string& text) { return R(make_error_code(runtime_error_code::invalid_operation), "{0}", e->park_error_text(text)); };
        auto number = [](const script_value* v, double& out) {
            if (v->is_int()) { out = static_cast<double>(v->as_int()); return true; }
            if (v->is_float()) { out = v->as_float(); return true; }
            return false;
        };
        auto make_date = [](engine* e, const date& d) -> R { return detail::value_converter<date>::to(d, e); };
        auto wall_ms = [](engine* e) { return time_calendar::floor_div(static_cast<int64_t>(e->wall_now().count()), 1000000); };
        auto zoned_now = [=](engine* e, bool in_utc) -> R {
            date d;
            if (!date::from_unix_ms(wall_ms(e), in_utc, d)) return value_error(e, "time: the current time is outside the supported range");
            return make_date(e, d);
        };

        def("now", sig(0, 0, {}, svt::jai_float_type), [=](engine* e, const args_t& a) -> R {
            if (!a.empty()) return count_error("time::now expects no arguments");
            return script_value(e->engine_elapsed(), e);
        });
        def("engine_elapsed", sig(0, 0, {}, svt::jai_float_type), [=](engine* e, const args_t& a) -> R {
            if (!a.empty()) return count_error("time::engine_elapsed expects no arguments");
            return script_value(e->engine_elapsed(), e);
        });
        def("execute_elapsed", sig(0, 0, {}, svt::jai_float_type), [=](engine* e, const args_t& a) -> R {
            if (!a.empty()) return count_error("time::execute_elapsed expects no arguments");
            return script_value(e->execute_elapsed(), e);
        });
        def("unix_time", sig(0, 0, {}, svt::jai_float_type), [=](engine* e, const args_t& a) -> R {
            if (!a.empty()) return count_error("time::unix_time expects no arguments");
            return script_value(std::chrono::duration<double>(e->wall_now()).count(), e);
        });
        def("unix_ms", sig(0, 0, {}, svt::jai_int_type), [=](engine* e, const args_t& a) -> R {
            if (!a.empty()) return count_error("time::unix_ms expects no arguments");
            return script_value(static_cast<script_int>(wall_ms(e)), e);
        });
        def("date_now", sig(1, 1, {svt::jai_string_type}, svt::jai_string_type), [=](engine* e, const args_t& a) -> R {
            if (a.size() != 1) return count_error("time::date_now expects exactly 1 argument (template)");
            const std::string* tmpl = a[0]->text_bytes();
            if (!tmpl) return type_error("time::date_now expects a string template");
            date d;
            if (!date::from_unix_ms(wall_ms(e), false, d)) return value_error(e, "time::date_now: the current time is outside the supported range");
            std::string out;
            const std::string error = format_date(e, d, *tmpl, "time::date_now", out);
            if (!error.empty()) return value_error(e, error);
            return script_value(std::move(out), e);
        });
        def("local_now", sig(0, 0, {}, svt::jai_object_type, "time::date"), [=](engine* e, const args_t& a) -> R {
            if (!a.empty()) return count_error("time::local_now expects no arguments");
            return zoned_now(e, false);
        });
        def("utc_now", sig(0, 0, {}, svt::jai_object_type, "time::date"), [=](engine* e, const args_t& a) -> R {
            if (!a.empty()) return count_error("time::utc_now expects no arguments");
            return zoned_now(e, true);
        });
        def("date_from_unix", sig(1, 2, {svt::jai_float_type, svt::jai_bool_type}, svt::jai_object_type, "time::date"), [=](engine* e, const args_t& a) -> R {
            if (a.empty() || a.size() > 2) return count_error("time::date_from_unix expects 1 or 2 arguments (seconds, [utc])");
            double seconds = 0.0;
            if (!number(a[0], seconds)) return type_error("time::date_from_unix expects a number of seconds");
            if (a.size() == 2 && !a[1]->is_bool()) return type_error("time::date_from_unix expects a bool for utc");
            const bool in_utc = a.size() == 2 && a[1]->as<bool>();
            int64_t ms = 0;
            bool in_range = false;
            if (a[0]->is_int()) {
                const script_int s = a[0]->as_int();
                in_range = s >= -100000000000000LL && s <= 100000000000000LL;
                ms = in_range ? s * 1000 : 0;
            } else {
                const double rounded = std::round(seconds * 1000.0);
                in_range = std::isfinite(rounded) && std::fabs(rounded) <= 1e17;
                ms = in_range ? static_cast<int64_t>(rounded) : 0;
            }
            date d;
            if (!in_range || !date::from_unix_ms(ms, in_utc, d)) {
                return value_error(e, "time::date_from_unix: the instant is outside the supported range (years 1 to 9999)");
            }
            return make_date(e, d);
        });
        for (const bool in_utc : {false, true}) {
            const char* name = in_utc ? "utc_date" : "local_date";
            def(name, sig(3, 7, {svt::jai_int_type, svt::jai_int_type, svt::jai_int_type, svt::jai_int_type, svt::jai_int_type, svt::jai_int_type, svt::jai_int_type}, svt::jai_object_type, "time::date"), [=](engine* e, const args_t& a) -> R {
                if (a.size() < 3 || a.size() > 7) {
                    return count_error(in_utc ? "time::utc_date expects 3 to 7 arguments (year, month, day, [hour, minute, second, millisecond])"
                                              : "time::local_date expects 3 to 7 arguments (year, month, day, [hour, minute, second, millisecond])");
                }
                for (const auto* v : a) {
                    if (!v->is_int()) return type_error(in_utc ? "time::utc_date expects integer fields" : "time::local_date expects integer fields");
                }
                date d;
                d.utc = in_utc;
                std::string (date::* const setters[])(int64_t) = {&date::set_year, &date::set_month, &date::set_day, &date::set_hour,
                                                                    &date::set_minute, &date::set_second, &date::set_millisecond};
                for (size_t i = 0; i < a.size(); ++i) {
                    const std::string error = (d.*setters[i])(a[i]->as_int());
                    if (!error.empty()) return value_error(e, error);
                }
                return make_date(e, d);
            });
        }
        def("sleep", sig(1, 1, {svt::jai_float_type}, svt::jai_null_type), [=](engine* e, const args_t& a) -> R {
            if (a.size() != 1) return count_error("time::sleep expects exactly 1 argument (seconds)");
            double seconds = 0.0;
            if (!number(a[0], seconds)) return type_error("time::sleep expects a number of seconds");
            if (seconds > 0.0) { e->sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(seconds > 1e9 ? 1e9 : seconds))); }
            return script_value(std::monostate{}, e);
        });
        def("sleep_ms", sig(1, 1, {svt::jai_float_type}, svt::jai_null_type), [=](engine* e, const args_t& a) -> R {
            if (a.size() != 1) return count_error("time::sleep_ms expects exactly 1 argument (milliseconds)");
            double ms = 0.0;
            if (!number(a[0], ms)) return type_error("time::sleep_ms expects a number of milliseconds");
            if (ms > 0.0) { e->sleep_for(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double, std::milli>(ms > 1e12 ? 1e12 : ms))); }
            return script_value(std::monostate{}, e);
        });

        for (const char* name : {"stopwatch", "date"}) {
            const uint64_t id = eng_ref.symbolize(name);
            ns->variables[id] = eng_ref.get_variable(std::string("time::") + name);
            ns->signatures[id] = sig(0, 0, {}, svt::jai_object_type, name == std::string_view("date") ? "time::date" : "time::stopwatch");
        }
    }

    inline void register_time_functions(const std::shared_ptr<engine>& e) { register_time_functions(*e); }
    inline void register_time_functions(engine* e) { register_time_functions(*e); }

} // namespace stdlib
} // namespace jai

#endif // __JAISCRIPT_STDLIB_TIME_HPP__
