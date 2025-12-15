class Config {
    static array<string> OPTIONS = ["a", "b", "c"];

    function test() -> string {
        print("Getting size...");
        auto size = OPTIONS.size();
        print("Size: " + to_string(size));
        
        print("Getting first element...");
        auto first = OPTIONS[0];
        print("First: " + first);
        
        return first;
    }
}

auto cfg = Config();
cfg.test()
