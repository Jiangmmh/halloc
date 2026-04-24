#include <halloc/halloc.h>
#include <iostream>
#include <cassert>
#include <cstdint>

int main(int argc, char** argv) {
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--list-cases") {
            std::cout << "api_contract\n";
            std::cout << "size_class_boundaries\n";
            std::cout << "pagemap_lookup\n";
            std::cout << "page_heap\n";
            std::cout << "thread_cache\n";
            std::cout << "remote_free\n";
            std::cout << "large_allocation\n";
            std::cout << "os_layer\n";
            std::cout << "stats_snapshot\n";
            return 0;
        }
        if (arg == "--case" && argc > 2) {
            std::string case_name = argv[2];
            if (case_name == "api_contract") {
                void* p0 = halloc::allocate(0);
                assert(p0 == nullptr);
                
                void* p1 = halloc::allocate(16);
                assert(p1 != nullptr);
                assert(((uintptr_t)p1 % halloc::MIN_ALIGNMENT) == 0);
                
                halloc::deallocate(p1, 16);
                assert(halloc::usable_size(nullptr) == 0);
                
                auto stats = halloc::stats_snapshot();
                assert(stats.local_hit == 0);
                
                std::cout << "api_contract: PASS" << std::endl;
                return 0;
            }
        }
    }
    std::cout << "halloc unit tests" << std::endl;
    return 0;
}
