#include <iostream>

#include <httplib.h>

int main() {
    httplib::Client cli("https://readmanganato.com");
    auto res = cli.Get("/manga-cs979853");
    std::cout << res->status << '\n';
}
