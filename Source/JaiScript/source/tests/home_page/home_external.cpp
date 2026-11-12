// The landing page's non-intrusive tab (docs/site/index.html#one-declaration): the regions below are the page's code
#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/overload.hpp>
#include <jaiscript/signals/signal.hpp>
#include <cstdint>
#include <string>

// [sample home-bind-external]
// spawner.h belongs to another module: you bind it without editing it
class Spawner {
public:
    std::string id = "north-gate";
    int64_t level = 1;
    jai::signal_emitter<void(int64_t)> onWave;
    void spawn_wave() { spawn_wave(level, false); }
    void spawn_wave(int64_t size, bool elite) { onWave(elite ? size * 2 : size); }
};

// your binding file
static jai::registrar<Spawner, void> spawnerBinding("Spawner", [](jai::dynamic_binder<Spawner>& b) {
    b.property("id", &Spawner::id)          // scripted and saved
     .property("level", &Spawner::level)
     .property("onWave", &Spawner::onWave)  // script-connectable
     .method("spawn_wave", jai::overload<>(&Spawner::spawn_wave))
     .method("spawn_wave", jai::overload<int64_t, bool>(&Spawner::spawn_wave));
});
// [/sample]

namespace home_page {
// [sample home-bind-run-external]
int main() {
    auto engine = jai::engine::make();
    jai::bind_registrar(*engine);   // every property_owner and registrar in the program
    engine->execute(R"(
        Spawner gate;
        gate.onWave.connect("hud", [](int size) { print("wave of ", size); });
        gate.level = 3;
        gate.spawn_wave(); gate.spawn_wave(4, true);
    )");
    return 0;
}
// [/sample]
}

#include "home_main.hpp"
