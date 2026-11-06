#ifndef __COMMON_HPP__
#define __COMMON_HPP__

#include <type_traits>
#include <cstddef>
#include <cstdint>
#ifdef _MSC_VER
	#include <Uefi/UefiBaseType.h>
#elif __GNUC__
	#include <efi/efi.h>
#endif

// We need UINT64 handling because of use cases
// like EFI_PHYSICAL_ADDRESS

// Helper trait: true if T is pointer or uint64_t 
template <typename T>
struct is_ptr_or_uint64 : std::disjunction<
    std::is_pointer<T>,
    std::is_same<UINT64, std::remove_cv_t<T>>
> {};

// raw_diff for pointers and uint64_t 
template <typename T,
          std::enable_if_t<is_ptr_or_uint64<T>::value, int> = 0>
std::ptrdiff_t raw_diff(T p1, T p2)
{
    if constexpr (std::is_pointer<T>::value)
    {
        return reinterpret_cast<std::byte*>(p1) - reinterpret_cast<std::byte*>(p2);
    }
    else
    {
        return static_cast<std::ptrdiff_t>(p1 - p2);
    }
}

// raw_offset: add byte offset to pointer or integer address
template <typename T, typename U,
          std::enable_if_t<is_ptr_or_uint64<T>::value && is_ptr_or_uint64<U>::value, int> = 0>
T raw_offset(U p1, std::ptrdiff_t offset)
{
    if constexpr (std::is_pointer<T>::value)
    {
        return reinterpret_cast<T>(
        	reinterpret_cast<std::byte*>(p1) + offset
    	);
    }
    else
    {
        return static_cast<T>(p1 + offset);
    }
}

// mem_after: get next element
template <typename T,
          std::enable_if_t<is_ptr_or_uint64<T>::value, int> = 0>
T mem_after(T p1)
{
    if constexpr (std::is_pointer<T>::value)
    {
        return p1 + 1;
    }
    else
    {
        return p1 + 1;
    }
}

// Macros for the same
#define RAW_OFFSET(type, x, offset)  (type)((size_t)x + offset)
#define RAW_DIFF(p1,p2) ((intptr_t)p1 - (intptr_t)p2)
#define MEM_AFTER(type, p) ((type)(&(p)[1]))

#define DIV_ROUND_UP(x, y) \
	((x + y - 1) / y)

#define ALIGN_UP(x, y) (DIV_ROUND_UP(x,y)*y)

#endif // __COMMON_HPP__