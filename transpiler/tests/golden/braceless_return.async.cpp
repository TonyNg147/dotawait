async int fetch(int v) { return v * 10; }

async int classify(int k) {
    switch (k) {
        case 0: return 100;
        case 1:
            return fetch(k).await;
        default: return -1;
    }
}

async int sign(int x) {
    if (x > 0) return 1;
    else if (x < 0) return -1;
    else return 0;
}
