class Config {
    static array<string> VALID_OPTIONS = ["option1", "option2", "option3"];

    function isValidOption(string opt) -> bool {
        print("Step 1: Entered method");
        print("Step 2: About to access VALID_OPTIONS");
        auto size = VALID_OPTIONS.size();
        print("Step 3: Got size: " + to_string(size));
        
        for (auto i = 0; i < size; i = i + 1) {
            print("Step 4: Loop iteration " + to_string(i));
            if (VALID_OPTIONS[i] == opt) {
                print("Step 5: Found match!");
                return true;
            }
        }
        print("Step 6: No match, returning false");
        return false;
    }
}

auto cfg = Config();
cfg.isValidOption("option2")
