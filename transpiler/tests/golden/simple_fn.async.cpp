int helper(int x) { return x + 1; }

async int compute(int x) {
    int y = helper(x).await;
    return y * 2;
}
