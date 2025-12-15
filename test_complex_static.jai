class Config {
    static array<string> VALID_OPTIONS = ["option1", "option2", "option3"];

    function isValidOption(string opt) -> bool {
        print("Checking option: " + opt);
        print("VALID_OPTIONS.size() = " + to_string(VALID_OPTIONS.size()));

        for (auto i = 0; i < VALID_OPTIONS.size(); i = i + 1) {
            print("  i=" + to_string(i) + ", VALID_OPTIONS[i]=" + VALID_OPTIONS[i]);
            if (VALID_OPTIONS[i] == opt) {
                print("  Found match!");
                return true;
            }
        }
        print("  No match found");
        return false;
    }
}

print("Testing static array access...");
print("Config::VALID_OPTIONS.size() = " + to_string(Config::VALID_OPTIONS.size()));
print("Config::VALID_OPTIONS[0] = " + Config::VALID_OPTIONS[0]);

print("\nCreating instance and calling method...");
auto cfg = Config();
auto result = cfg.isValidOption("option2");
print("Result: " + to_string(result));
result
