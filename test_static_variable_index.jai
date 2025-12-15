class Config {
    static array<string> OPTIONS = ["a", "b", "c"];

    function test() -> string {
        print("Step 1: Get first element with literal index");
        auto elem0 = OPTIONS[0];
        print("elem0: " + elem0);
        
        print("Step 2: Get first element with variable index");
        auto i = 0;
        print("i = " + to_string(i));
        auto elem_i = OPTIONS[i];
        print("elem_i: " + elem_i);
        
        return elem_i;
    }
}

auto cfg = Config();
cfg.test()
