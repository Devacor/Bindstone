// Head-to-head JSON DOM parse benchmark: fast_json (ours) vs rapidjson (cereal-bundled, scalar).
//
// Build (from a VS x64 dev shell), single TU, header-only deps:
//   cl /nologo /O2 /Ob2 /Oi /Ot /std:c++20 /EHsc /DNDEBUG ^
//      /I"D:\git\Bindstone\External\cereal\include" bench_json.cpp /Fe:bench_json.exe
//
// Run:  bench_json.exe [path-to-json] [iterations]
//   defaults: D:\git\Bindstone\Scenes\map.scene2, 60 iterations
//
// Both parsers build a full in-memory DOM from an owned copy of the input and free it
// each iteration (representative of a cold scene load). We measure parse+build only.

#define NDEBUG
#include <cereal/external/rapidjson/document.h>

#include "fast_json.hpp"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <fstream>

namespace rj = rapidjson;

struct Checksum {
    uint64_t nodes = 0;
    uint64_t strings = 0;
    uint64_t strLenSum = 0;
    int64_t  intSum = 0;
    uint64_t members = 0;
    uint64_t elements = 0;
    double   dblSum = 0.0;
    uint64_t ints = 0, dbls = 0, bools = 0, nulls = 0;
    bool structurallyEqual(const Checksum& o) const {
        return nodes == o.nodes && strings == o.strings && strLenSum == o.strLenSum &&
               intSum == o.intSum && members == o.members && elements == o.elements;
    }
};

static void fjWalk(const fastjson::Document& d, uint32_t idx, Checksum& cs) {
    const fastjson::Value& v = d.nodes[idx];
    cs.nodes++;
    using T = fastjson::Tag;
    switch (v.tag) {
        case T::Str:    cs.strings++; cs.strLenSum += v.u.str.len; break;
        case T::Int:    cs.intSum += v.u.i; cs.ints++; break;
        case T::Bool:   cs.intSum += v.u.i; cs.bools++; break;
        case T::Double: cs.dblSum += v.u.d; cs.dbls++; break;
        case T::Null:   cs.nulls++; break;
        case T::Arr:
            cs.elements += v.u.agg.count;
            for (uint32_t k = 0; k < v.u.agg.count; ++k) fjWalk(d, v.u.agg.first + k, cs);
            break;
        case T::Obj:
            cs.members += v.u.agg.count;
            for (uint32_t k = 0; k < v.u.agg.count; ++k) {
                fjWalk(d, v.u.agg.first + 2 * k, cs);
                fjWalk(d, v.u.agg.first + 2 * k + 1, cs);
            }
            break;
    }
}

static void rjWalk(const rj::Value& v, Checksum& cs) {
    cs.nodes++;
    if (v.IsString())      { cs.strings++; cs.strLenSum += v.GetStringLength(); }
    else if (v.IsBool())   { cs.intSum += v.GetBool() ? 1 : 0; cs.bools++; }
    else if (v.IsInt64())  { cs.intSum += v.GetInt64(); cs.ints++; }
    else if (v.IsUint64()) { cs.intSum += static_cast<int64_t>(v.GetUint64()); cs.ints++; }
    else if (v.IsDouble()) { cs.dblSum += v.GetDouble(); cs.dbls++; }
    else if (v.IsNull())   { cs.nulls++; }
    else if (v.IsArray())  {
        cs.elements += v.Size();
        for (auto& e : v.GetArray()) rjWalk(e, cs);
    } else if (v.IsObject()) {
        cs.members += v.MemberCount();
        for (auto& m : v.GetObject()) {
            cs.nodes++; cs.strings++; cs.strLenSum += m.name.GetStringLength();
            rjWalk(m.value, cs);
        }
    }
}

struct Timing { double minMs; double medianMs; };

template <typename F>
static Timing timeIt(int iters, F&& fn) {
    std::vector<double> t;
    t.reserve(iters);
    for (int i = 0; i < iters; ++i) {
        auto a = std::chrono::steady_clock::now();
        fn();
        auto b = std::chrono::steady_clock::now();
        t.push_back(std::chrono::duration<double, std::milli>(b - a).count());
    }
    std::sort(t.begin(), t.end());
    return { t.front(), t[t.size() / 2] };
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "D:\\git\\Bindstone\\Scenes\\map.scene2";
    int iters = (argc > 2) ? std::atoi(argv[2]) : 60;
    if (iters < 5) iters = 5;

    std::ifstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "Cannot open %s\n", path); return 1; }
    std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.empty()) { std::fprintf(stderr, "Empty file %s\n", path); return 1; }

    const double mb = static_cast<double>(buf.size()) / (1024.0 * 1024.0);
    std::printf("File: %s\n", path);
    std::printf("Size: %zu bytes (%.2f MiB), iterations: %d\n\n", buf.size(), mb, iters);

    // ---- correctness / structural parity ----
    Checksum fjCs, rjCs;
    bool fjOk = false, rjOk = false;
    {
        fastjson::Document d;
        fastjson::Parser p;
        fjOk = p.parse(buf.data(), buf.size(), d);
        if (fjOk) fjWalk(d, d.root, fjCs);
    }
    {
        rj::Document d;
        d.Parse(buf.data(), buf.size());
        rjOk = !d.HasParseError();
        if (rjOk) rjWalk(d, rjCs);
    }
    std::printf("Parse OK:        fast_json=%s  rapidjson=%s\n", fjOk ? "yes" : "NO", rjOk ? "yes" : "NO");
    std::printf("              %14s %14s\n", "fast_json", "rapidjson");
    std::printf("  nodes       %14llu %14llu\n", (unsigned long long)fjCs.nodes,    (unsigned long long)rjCs.nodes);
    std::printf("  members     %14llu %14llu\n", (unsigned long long)fjCs.members,  (unsigned long long)rjCs.members);
    std::printf("  elements    %14llu %14llu\n", (unsigned long long)fjCs.elements, (unsigned long long)rjCs.elements);
    std::printf("  strings     %14llu %14llu\n", (unsigned long long)fjCs.strings,  (unsigned long long)rjCs.strings);
    std::printf("  strLenSum   %14llu %14llu\n", (unsigned long long)fjCs.strLenSum,(unsigned long long)rjCs.strLenSum);
    std::printf("  intSum      %14lld %14lld\n", (long long)fjCs.intSum,            (long long)rjCs.intSum);
    std::printf("  dblSum      %14.4f %14.4f\n", fjCs.dblSum, rjCs.dblSum);
    std::printf("  ints        %14llu %14llu\n", (unsigned long long)fjCs.ints,     (unsigned long long)rjCs.ints);
    std::printf("  doubles     %14llu %14llu\n", (unsigned long long)fjCs.dbls,     (unsigned long long)rjCs.dbls);
    std::printf("  bools       %14llu %14llu\n", (unsigned long long)fjCs.bools,    (unsigned long long)rjCs.bools);
    std::printf("  nulls       %14llu %14llu\n", (unsigned long long)fjCs.nulls,    (unsigned long long)rjCs.nulls);
    bool parity = fjOk && rjOk && fjCs.structurallyEqual(rjCs);
    std::printf("Structural parity: %s\n\n", parity ? "MATCH" : "*** MISMATCH ***");

    if (!fjOk || !rjOk) {
        std::fprintf(stderr, "A parser failed; aborting timing.\n");
        return 2;
    }

    // ---- timing (parse + DOM build, fresh DOM each iter) ----
    volatile uint64_t sink = 0;

    Timing rjT = timeIt(iters, [&] {
        rj::Document d;
        d.Parse(buf.data(), buf.size());
        sink += d.IsObject() ? d.MemberCount() : (d.IsArray() ? d.Size() : 0u);
    });

    Timing fjT = timeIt(iters, [&] {
        fastjson::Document d;
        fastjson::Parser p;
        p.parse(buf.data(), buf.size(), d);
        sink += d.nodes.size();
    });

    auto mbps = [&](double ms) { return mb / (ms / 1000.0); };

    std::printf("                  median(ms)   min(ms)     MiB/s\n");
    std::printf("  rapidjson  %12.3f %10.3f %9.0f\n", rjT.medianMs, rjT.minMs, mbps(rjT.medianMs));
    std::printf("  fast_json  %12.3f %10.3f %9.0f\n", fjT.medianMs, fjT.minMs, mbps(fjT.medianMs));
    std::printf("\n");
    std::printf("  fast_json speedup vs rapidjson (median): %.3fx  (>1.0 = we are faster)\n",
                rjT.medianMs / fjT.medianMs);
    std::printf("  [sink=%llu]\n", (unsigned long long)sink);
    return 0;
}
