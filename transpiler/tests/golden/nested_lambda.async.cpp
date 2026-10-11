async int run_all(int x) {
    auto add_one = [](int a) {
        return a + 1;
    };
    int v = add_one(x);
    if (v > 0) {
        return fetch(v).await;
    }
    return 0;
}
