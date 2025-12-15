class Config {
    static array<string> OPTIONS = ["a", "b", "c"];

    function test() -> string {
        try {
            auto i = 0;
            print("About to access OPTIONS[i] where i=" + to_string(i));
            auto elem = OPTIONS[i];
            print("Success: " + elem);
            return elem;
        } catch (e) {
            print("CAUGHT ERROR: " + e);
            throw e;
        }
    }
}

auto cfg = Config();
cfg.test()
