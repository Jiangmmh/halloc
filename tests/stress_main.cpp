#include <iostream>

int main(int argc, char** argv) {
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--list-cases") {
            std::cout << "same_thread_hot_path\n";
            std::cout << "thread_cache_cap_enforcement\n";
            std::cout << "remote_free_matrix\n";
            std::cout << "thread_exit_trim\n";
            return 0;
        }
    }
    std::cout << "halloc stress tests placeholder" << std::endl;
    return 0;
}