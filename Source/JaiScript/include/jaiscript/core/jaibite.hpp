#pragma once

#ifndef __JAISCRIPT_CORE_JAIBITE_HPP__
#define __JAISCRIPT_CORE_JAIBITE_HPP__

#include <memory>
#include <vector>

namespace jai {

    class engine;
    class declaration;
    using declaration_ptr = std::shared_ptr<declaration>;
    class script_value;

    // Engine-bound pre-parsed script: engine->jaibite("...") lexes and parses once,
    // then execute() re-runs without re-lexing/parsing (and the vm backend caches its
    // compiled chunk in compiled_, so re-execution is pure dispatch). Never share a
    // jaibite across engines — interned symbol IDs are engine-specific.
    class jaibite {
    public:
        jaibite() = default;

        // Runs on the owning engine; throws script_exception if the engine is gone.
        script_value execute();

        bool valid() const { return !declarations_.empty(); }

    private:
        friend class engine;
        jaibite(std::weak_ptr<engine> owner, std::vector<declaration_ptr> declarations)
            : engine_(std::move(owner)), declarations_(std::move(declarations)) {}

        std::weak_ptr<engine> engine_;
        std::vector<declaration_ptr> declarations_;
        std::shared_ptr<void> compiled_;   // backend-owned compiled artifact (vm: chunk)
    };

} // namespace jai

#endif // __JAISCRIPT_CORE_JAIBITE_HPP__
