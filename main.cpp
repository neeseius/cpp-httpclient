#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <future>
#include <mutex>

#include "http/client.hpp"

class Counter {
    std::mutex  mut;
    std::size_t count;

public:
    Counter() : count(0) {}

    void Set(std::size_t i) {
        count = i;
    }

    void Decrement() {
        std::lock_guard<std::mutex> guard(mut);
        count--;
    }

    std::size_t Count() {
        std::lock_guard<std::mutex> guard(mut);
        std::size_t n = count;
        return n;
    }
};

URL         *url;
char        *method;
Counter     counter;


void Worker() {
    while (counter.Count() > 0) {
        counter.Decrement();
        HttpClient cli{};
        Request req(url, (char*)"GET");
        // req.AddHeader("Connection", "close");

        auto [resp, err] = cli.request(&req);
        if (err) {
            std::cout << "connection error: " << err.Error() << '\n';
        }
    }
}


int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("make-request [url] [method] [nworkers] [count]");
        return -1;
    }

    url            = new URL(strdup(argv[1]));
    method         = argv[2];
    int nworkers   = std::stoi(argv[3]);

    std::size_t count = std::stoi(argv[4]);
    std::vector<std::future<void>> tasks;
    counter.Set(count);

    for (int i = 0; i < nworkers; i++) {
        std::future<void> f = std::async(std::launch::async, Worker);
        tasks.push_back(std::move(f));
    }

    for (auto& fut : tasks)
        fut.wait();
}
