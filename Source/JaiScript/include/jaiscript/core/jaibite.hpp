#pragma once

#ifndef __JAISCRIPT_CORE_JAIBITE_HPP__
#define __JAISCRIPT_CORE_JAIBITE_HPP__

#include <memory>
#include <vector>
#include <string>
#include <cstdint>

namespace jai {

    class engine;
    class declaration;
    using declaration_ptr = std::shared_ptr<declaration>;
    class script_value;

    // Engine-bound pre-parsed script: engine->jaibite("...") lexes and parses once,
    // then execute() re-runs without re-lexing/parsing (and the vm backend caches its
    // compiled chunk in compiled_, so re-execution is pure dispatch). Never share a
    // jaibite across engines — interned symbol IDs are engine-specific; to move a bite
    // between engines go through save()/engine::jaibite_load(), which relocates symbols.
    class jaibite {
    public:
        jaibite() = default;

        // Runs on the owning engine; throws script_exception if the engine is gone.
        script_value execute();

        bool valid() const { return !declarations_.empty(); }

        // Serialize the parsed AST (never the compiled chunk — a loaded bite recompiles
        // lazily like a fresh one). Throws jai::runtime_error on IO failure or if the
        // owning engine is gone. Load side: engine::jaibite_load / jaibite_load_bytes.
        void save(const std::string& path) const;
        std::vector<uint8_t> save_bytes() const;  // byte-level, for host VFS routing

        // True when the loading engine's registered classes/functions differ from the
        // saving engine's. Advisory only — the bite still loads and runs (the script may
        // not touch the missing registrations); errors surface at execute() if it does.
        bool registration_mismatch() const { return registration_mismatch_; }

    private:
        friend class engine;
        jaibite(std::weak_ptr<engine> owner, std::vector<declaration_ptr> declarations)
            : engine_(std::move(owner)), declarations_(std::move(declarations)) {}

        std::weak_ptr<engine> engine_;
        std::vector<declaration_ptr> declarations_;
        std::shared_ptr<void> compiled_;   // backend-owned compiled artifact (vm: chunk)
        bool registration_mismatch_ = false;
    };

} // namespace jai

#endif // __JAISCRIPT_CORE_JAIBITE_HPP__
