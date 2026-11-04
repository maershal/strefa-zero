// CellAllocator places each per-cell array at its own 64-byte colour inside a 4 KiB-aligned block.
#include "sim/cell_vector.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <new>
#include <numeric>

int main() {
    using namespace strefa::sim::detail;
    int failures = 0;
    const auto check = [&](bool good, const char* label) {
        if (!good) {
            std::cerr << label << '\n';
            ++failures;
        }
    };
    const auto offset = [](const void* pointer) { return reinterpret_cast<std::uintptr_t>(pointer) % cell_page; };

    CellAllocator<double> allocator;
    reset_cell_colours();
    for (std::size_t i = 0; i < cell_colours + 3; ++i) {
        double* p = allocator.allocate(1000);
        check(offset(p) == (i % cell_colours) * cell_line, "colour offset follows allocation order mod 64");
        check(reinterpret_cast<std::uintptr_t>(p) % cell_line == 0, "array is 64-byte aligned");
        p[0] = 1;
        p[999] = 2; // the whole array fits behind the colour offset
        allocator.deallocate(p, 1000);
    }
    reset_cell_colours();
    double* first = allocator.allocate(1);
    check(offset(first) == 0, "reset_cell_colours restarts at colour 0");
    allocator.deallocate(first, 1);

    check(CellAllocator<double>::max_size() == (static_cast<std::size_t>(-1) - cell_page) / sizeof(double),
          "max_size leaves a page for the colour offset");
    check(std::allocator_traits<CellAllocator<float>>::max_size(CellAllocator<float>{})
                  == CellAllocator<float>::max_size(),
          "allocator_traits reports the allocator's max_size");
    bool threw = false;
    try {
        static_cast<void>(allocator.allocate(CellAllocator<double>::max_size() + 1));
    } catch (const std::bad_array_new_length&) {
        threw = true;
    }
    check(threw, "count above max_size throws bad_array_new_length");

    reset_cell_colours();
    CellVector<float> a(4096), b(4096);
    std::iota(a.begin(), a.end(), 0.F);
    b = a;
    check(b[4095] == 4095.F && b.size() == 4096, "CellVector copies its values");
    check(offset(a.data()) != offset(b.data()), "two vectors get different colours");
    CellVector<float> moved = std::move(a);
    check(moved[17] == 17.F, "CellVector moves its storage");

    if (failures) return 1;
    std::cout << "cell_allocator ok\n";
    return 0;
}
