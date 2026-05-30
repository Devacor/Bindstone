#pragma once
// fast_json — a lean, arena-backed JSON DOM parser built for a head-to-head against
// rapidjson. Design goals:
//   * zero per-node heap allocation (all values live in one contiguous arena,
//     all string bytes in one contiguous char arena — both grow geometrically)
//   * std::from_chars for numbers (Ryu-class, locale-free)
//   * bulk copy of unescaped string runs (escape handling only on backslash)
//   * tight branchy whitespace skip (no locale, no function-call per char)
//   * index-based child references (realloc-safe; no pointer fix-ups)
//
// Container layout: a parser-local scratch stack collects child Values during
// recursion; when a container closes we know its exact child count and bulk-move
// the block into the arena as one contiguous run (this is the rapidjson trick).
// Objects store members interleaved as [key,value,key,value,...].

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <charconv>
#include <string_view>
#include <emmintrin.h>   // SSE2 (baseline on x64) for the string-run scanner
#include <intrin.h>      // _BitScanForward

namespace fastjson {

// Returns the first byte in [p,end) that is '"' or '\\' (a string terminator or escape),
// or end if none. Processes 16 bytes per step with SSE2 (always available on x64), which
// lets the common no-escape string body be skipped in bulk rather than byte-by-byte.
inline const char* scan_string_special(const char* p, const char* end) {
    const __m128i quote = _mm_set1_epi8('"');
    const __m128i bslash = _mm_set1_epi8('\\');
    while (end - p >= 16) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        __m128i hit = _mm_or_si128(_mm_cmpeq_epi8(chunk, quote), _mm_cmpeq_epi8(chunk, bslash));
        unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(hit));
        if (mask) {
            unsigned long idx;
            _BitScanForward(&idx, mask);
            return p + idx;
        }
        p += 16;
    }
    while (p < end && *p != '"' && *p != '\\') ++p;
    return p;
}

enum class Tag : uint8_t { Null, Bool, Int, Double, Str, Arr, Obj };

struct Value {
    Tag tag = Tag::Null;
    union {
        int64_t  i;                              // Int, Bool (0/1)
        double   d;                              // Double
        struct { uint32_t off, len; } str;       // Str: [off,len) into Document::chars
        struct { uint32_t first, count; } agg;   // Arr: count elems at [first..]; Obj: count members, 2*count nodes at [first..]
    } u{};
};
static_assert(sizeof(Value) == 16, "Value should be 16 bytes");

struct Document {
    std::vector<Value> nodes;   // value arena (root is the LAST element)
    std::string        chars;   // string-byte arena
    uint32_t           root = 0;
    bool               ok = false;

    const Value& at(uint32_t idx) const { return nodes[idx]; }
    std::string_view view(const Value& v) const {
        return std::string_view(chars.data() + v.u.str.off, v.u.str.len);
    }
};

class Parser {
public:
    // Parses [data,data+size). Returns true on success; on failure Document::ok is false.
    bool parse(const char* data, size_t size, Document& doc) {
        p_ = data;
        end_ = data + size;
        doc_ = &doc;
        doc.nodes.clear();
        doc.chars.clear();
        // Heuristic reservations: typical minified JSON has ~1 value per 8 bytes and
        // string bytes ~1/3 of input. Over-reserving slightly avoids reallocs entirely.
        if (doc.nodes.capacity() < size / 6 + 16) doc.nodes.reserve(size / 6 + 16);
        if (doc.chars.capacity() < size / 2 + 16) doc.chars.reserve(size / 2 + 16);
        stack_.clear();

        skip_ws();
        if (!parse_value()) { doc.ok = false; return false; }
        skip_ws();
        if (p_ != end_) { doc.ok = false; return false; }   // trailing garbage
        if (stack_.size() != 1) { doc.ok = false; return false; }

        doc.nodes.push_back(stack_[0]);
        doc.root = static_cast<uint32_t>(doc.nodes.size() - 1);
        doc.ok = true;
        return true;
    }

private:
    const char* p_ = nullptr;
    const char* end_ = nullptr;
    Document* doc_ = nullptr;
    std::vector<Value> stack_;

    inline void skip_ws() {
        while (p_ < end_) {
            char c = *p_;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++p_;
            else break;
        }
    }

    bool parse_value() {
        if (p_ >= end_) return false;
        switch (*p_) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': {
                Value v; v.tag = Tag::Str;
                if (!parse_string(v.u.str.off, v.u.str.len)) return false;
                stack_.push_back(v);
                return true;
            }
            case 't': return parse_literal("true", 4, make_bool(true));
            case 'f': return parse_literal("false", 5, make_bool(false));
            case 'n': return parse_literal("null", 4, make_null());
            default:
                if (*p_ == '-' || (*p_ >= '0' && *p_ <= '9')) return parse_number();
                return false;
        }
    }

    static Value make_null() { Value v; v.tag = Tag::Null; v.u.i = 0; return v; }
    static Value make_bool(bool b) { Value v; v.tag = Tag::Bool; v.u.i = b ? 1 : 0; return v; }

    bool parse_literal(const char* lit, size_t n, Value v) {
        if (static_cast<size_t>(end_ - p_) < n) return false;
        for (size_t k = 0; k < n; ++k) if (p_[k] != lit[k]) return false;
        p_ += n;
        stack_.push_back(v);
        return true;
    }

    void push_container(Tag t, size_t mark) {
        const uint32_t first = static_cast<uint32_t>(doc_->nodes.size());
        const size_t n = stack_.size() - mark;
        doc_->nodes.insert(doc_->nodes.end(), stack_.begin() + mark, stack_.end());
        stack_.resize(mark);
        Value v; v.tag = t;
        v.u.agg.first = first;
        v.u.agg.count = (t == Tag::Obj) ? static_cast<uint32_t>(n / 2) : static_cast<uint32_t>(n);
        stack_.push_back(v);
    }

    bool parse_object() {
        ++p_; // '{'
        const size_t mark = stack_.size();
        skip_ws();
        if (p_ < end_ && *p_ == '}') { ++p_; push_container(Tag::Obj, mark); return true; }
        for (;;) {
            skip_ws();
            if (p_ >= end_ || *p_ != '"') return false;
            Value k; k.tag = Tag::Str;
            if (!parse_string(k.u.str.off, k.u.str.len)) return false;
            stack_.push_back(k);
            skip_ws();
            if (p_ >= end_ || *p_ != ':') return false;
            ++p_;
            skip_ws();
            if (!parse_value()) return false;
            skip_ws();
            if (p_ >= end_) return false;
            char c = *p_++;
            if (c == ',') continue;
            if (c == '}') break;
            return false;
        }
        push_container(Tag::Obj, mark);
        return true;
    }

    bool parse_array() {
        ++p_; // '['
        const size_t mark = stack_.size();
        skip_ws();
        if (p_ < end_ && *p_ == ']') { ++p_; push_container(Tag::Arr, mark); return true; }
        for (;;) {
            skip_ws();
            if (!parse_value()) return false;
            skip_ws();
            if (p_ >= end_) return false;
            char c = *p_++;
            if (c == ',') continue;
            if (c == ']') break;
            return false;
        }
        push_container(Tag::Arr, mark);
        return true;
    }

    bool parse_string(uint32_t& off, uint32_t& len) {
        ++p_; // opening quote
        const uint32_t start = static_cast<uint32_t>(doc_->chars.size());
        for (;;) {
            const char* hit = scan_string_special(p_, end_);
            doc_->chars.append(p_, static_cast<size_t>(hit - p_));   // bulk-copy the clean run
            p_ = hit;
            if (p_ >= end_) return false;                            // unterminated
            if (*p_ == '"') {
                ++p_;
                off = start;
                len = static_cast<uint32_t>(doc_->chars.size() - start);
                return true;
            }
            // backslash escape
            ++p_;
            if (p_ >= end_) return false;
            char e = *p_++;
            switch (e) {
                case '"':  doc_->chars.push_back('"');  break;
                case '\\': doc_->chars.push_back('\\'); break;
                case '/':  doc_->chars.push_back('/');  break;
                case 'b':  doc_->chars.push_back('\b'); break;
                case 'f':  doc_->chars.push_back('\f'); break;
                case 'n':  doc_->chars.push_back('\n'); break;
                case 'r':  doc_->chars.push_back('\r'); break;
                case 't':  doc_->chars.push_back('\t'); break;
                case 'u':  if (!parse_unicode()) return false; break;
                default:   return false;
            }
        }
    }

    static bool hex4(const char* s, uint32_t& out) {
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[k];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        out = v;
        return true;
    }

    // p_ points just past the 'u'. Consumes 4 hex (and a trailing low surrogate if present).
    bool parse_unicode() {
        if (end_ - p_ < 4) return false;
        uint32_t cp;
        if (!hex4(p_, cp)) return false;
        p_ += 4;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            // high surrogate; need \uXXXX low surrogate
            if (end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
                uint32_t low;
                if (hex4(p_ + 2, low) && low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    p_ += 6;
                }
            }
        }
        append_utf8(cp);
        return true;
    }

    void append_utf8(uint32_t cp) {
        std::string& s = doc_->chars;
        if (cp < 0x80) {
            s.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parse_number() {
        const char* s = p_;
        const bool neg = (*p_ == '-');
        if (neg) ++p_;
        const char* digits = p_;
        bool is_double = false;
        while (p_ < end_) {
            char c = *p_;
            if (c >= '0' && c <= '9') { ++p_; }
            else if (c == '.' || c == 'e' || c == 'E') { is_double = true; ++p_; }
            else if (c == '+' || c == '-') { ++p_; }  // exponent sign
            else break;
        }
        Value v;
        if (!is_double) {
            // Integer fast path: any run of <=18 digits fits in int64 with no overflow check.
            const size_t ndig = static_cast<size_t>(p_ - digits);
            if (ndig >= 1 && ndig <= 18) {
                uint64_t acc = 0;
                for (const char* t = digits; t < p_; ++t) acc = acc * 10u + static_cast<uint64_t>(*t - '0');
                v.tag = Tag::Int;
                v.u.i = neg ? -static_cast<int64_t>(acc) : static_cast<int64_t>(acc);
                stack_.push_back(v);
                return true;
            }
            // 19+ digits: let from_chars decide int vs overflow->double.
            int64_t iv;
            auto r = std::from_chars(s, p_, iv);
            if (r.ec == std::errc() && r.ptr == p_) {
                v.tag = Tag::Int; v.u.i = iv;
                stack_.push_back(v);
                return true;
            }
            // fall through to double on overflow
        }
        double d;
        auto r = std::from_chars(s, p_, d);
        if (r.ec != std::errc() || r.ptr != p_) return false;
        v.tag = Tag::Double; v.u.d = d;
        stack_.push_back(v);
        return true;
    }
};

} // namespace fastjson
