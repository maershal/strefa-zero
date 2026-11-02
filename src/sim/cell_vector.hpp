#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

namespace strefa::sim::detail {
// Per-cell SoA fields are ~480 KiB each. Large allocations come straight from the OS (mmap, VirtualAlloc) or not,
// depending on libc thresholds, and page-aligned arrays put element i of every field at the same offset in a 4 KiB
// page: L1D set conflicts and 4K store/load aliasing. CellAllocator places every array at its own 64-byte
// "colour" inside a 4 KiB-aligned block, so the layout no longer depends on the libc allocator or on earlier
// allocations. Colours follow allocation order and restart in State::allocate().
inline constexpr std::size_t cell_page = 4096, cell_line = 64, cell_colours = cell_page / cell_line;
inline thread_local std::size_t cell_colour = 0;
inline void reset_cell_colours() noexcept {
    cell_colour = 0;
}
template<class T> struct CellAllocator {
    using value_type = T;
    CellAllocator() noexcept = default;
    template<class U> CellAllocator(const CellAllocator<U>&) noexcept {}
    T* allocate(std::size_t count) {
        if (count > (static_cast<std::size_t>(-1) - cell_page) / sizeof(T)) throw std::bad_array_new_length{};
        auto* base =
                static_cast<std::byte*>(::operator new(count * sizeof(T) + cell_page, std::align_val_t{cell_page}));
        const std::size_t colour = cell_colour++ % cell_colours;
        return reinterpret_cast<T*>(base + colour * cell_line);
    }
    void deallocate(T* pointer, std::size_t) noexcept {
        const auto address = reinterpret_cast<std::uintptr_t>(pointer) & ~(std::uintptr_t{cell_page} - 1);
        ::operator delete(reinterpret_cast<void*>(address), std::align_val_t{cell_page});
    }
    template<class U> bool operator==(const CellAllocator<U>&) const noexcept { return true; }
};
template<class T> using CellVector = std::vector<T, CellAllocator<T>>;
} // namespace strefa::sim::detail
