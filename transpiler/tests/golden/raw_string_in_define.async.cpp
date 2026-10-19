// A raw string literal in a #define may span lines (GCC keeps the whole
// literal in the directive). Nothing inside it is code, so the `async` text
// in it must pass through untouched.
#define DOC R"(
async int not_code() { return 0; }
)"
async int f() { return 1; }
const char* doc = DOC;
int main() {}
