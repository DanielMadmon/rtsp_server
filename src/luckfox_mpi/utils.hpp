#pragma once
#include <type_traits>
#include <string>
#include <cstdint>
#include <limits>
// Source - https://stackoverflow.com/a/60092954
// Posted by NutCracker, modified by community. See post 'Timeline' for change history
// Retrieved 2026-05-24, License - CC BY-SA 4.0

#define LOC_FNAME __FUNCTION__
#define LOC_FFNAME __PRETTY_FUNCTION__
#define FORCE_INLINE [[gnu::always_inline]] inline


namespace lf_mpi{
namespace utils{
template <typename Enum>
    constexpr typename std::enable_if<std::is_enum<Enum>::value, 
    typename std::underlying_type<Enum>::type>::type
    get_underlying(Enum const& value) {
        return static_cast<typename std::underlying_type<Enum>::type>(value);
    }
    
    bool file_is_larger_than(std::string& filename,uintmax_t size) noexcept;
    bool file_is_larger_than(std::string& filename,uintmax_t size,const size_t count_max,size_t& count) noexcept;
    
    template <typename IntType = int32_t,
              IntType Min = std::numeric_limits<IntType>::max(), 
              IntType Max = std::numeric_limits<IntType>::min()>
    class SafeIntRange{
        public:
        constexpr explicit SafeIntRange(IntType val):
        value(val >= Min && val <= Max ? val : Min)
        {} 
        SafeIntRange() = delete;
        SafeIntRange(SafeIntRange& other) = default;
        const IntType value{};
    };
}
}
