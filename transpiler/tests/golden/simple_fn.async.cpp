int helper(int x) { return x + 1; }

async int helper_async(int x) { return helper(x); }

async int compute(int x) {
    int y = helper_async(x).await;
    return y * 2;
}
