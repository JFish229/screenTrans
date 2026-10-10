#pragma once
#include <type_traits>
#include <atomic>

template<typename T>
struct is_atomic : std::false_type {};

template<typename T>
struct is_atomic<std::atomic<T>> : std::true_type {};

template<typename T>
concept std_atomic = is_atomic<T>::value;

template<std_atomic T>
[[nodiscard]] static inline auto get(T& v, std::memory_order order = std::memory_order_acquire) noexcept -> typename T::value_type {
	return v.load(order);
}

template<std_atomic T>
[[nodiscard]] static inline auto get(const T& v, std::memory_order order = std::memory_order_acquire) noexcept -> typename T::value_type {
	return v.load(order);
}

template<std_atomic T>
static inline void set(T& v, typename T::value_type val, std::memory_order order = std::memory_order_release) noexcept {
	v.store(val, order);
}

#define COMP_ATOMIC(comp_func, label, atomic_var, ...) do { \
	static_assert(std_atomic<decltype(atomic_var)>); \
	using T = decltype(atomic_var)::value_type; \
	T mid___ = get(atomic_var); \
	comp_func(label, &mid___, ##__VA_ARGS__); \
	set(atomic_var, mid___); \
} while (false)

#define COMP_ATOMIC_EX(comp_func, label, atomic_var, ex_fn, ...) do { \
	static_assert(std_atomic<decltype(atomic_var)>); \
	using T = decltype(atomic_var)::value_type; \
	T mid___ = get(atomic_var); \
	comp_func(label, &mid___, ##__VA_ARGS__); \
	mid___ = ex_fn(mid___); \
	set(atomic_var, mid___); \
} while (false)

#define COMP_ATOMIC_T(comp_func, label, type, atomic_var, ...) do { \
	static_assert(std_atomic<decltype(atomic_var)>); \
	using T = decltype(atomic_var)::value_type; \
	T mid___ = get(atomic_var); \
	comp_func(label, type, &mid___, ##__VA_ARGS__); \
	set(atomic_var, mid___); \
} while (false)

#define COMP_ATOMIC_T_EX(comp_func, label, type, atomic_var, ex_fn, ...) do { \
	static_assert(std_atomic<decltype(atomic_var)>); \
	using T = decltype(atomic_var)::value_type; \
	T mid___ = get(atomic_var); \
	comp_func(label, type, &mid___, ##__VA_ARGS__); \
	mid___ = ex_fn(mid___); \
	set(atomic_var, mid___); \
} while (false)

#define COMP_ATOMIC_R(res, comp_func, label, atomic_var, ...) do { \
	static_assert(std_atomic<decltype(atomic_var)>); \
	using T = decltype(atomic_var)::value_type; \
	T mid___ = get(atomic_var); \
	res = comp_func(label, &mid___, ##__VA_ARGS__); \
	set(atomic_var, mid___); \
} while (false)

#define COMP_ATOMIC_EX_R(res, comp_func, label, atomic_var, ex_fn, ...) do { \
	static_assert(std_atomic<decltype(atomic_var)>); \
	using T = decltype(atomic_var)::value_type; \
	T mid___ = get(atomic_var); \
	res = comp_func(label, &mid___, ##__VA_ARGS__); \
	mid___ = ex_fn(mid___); \
	set(atomic_var, mid___); \
} while (false)

#define COMP_ATOMIC_T_R(res, comp_func, label, type, atomic_var, ...) do { \
	static_assert(std_atomic<decltype(atomic_var)>); \
	using T = decltype(atomic_var)::value_type; \
	T mid___ = get(atomic_var); \
	res = comp_func(label, type, &mid___, ##__VA_ARGS__); \
	set(atomic_var, mid___); \
} while (false)

#define COMP_ATOMIC_T_EX_R(res, comp_func, label, type, atomic_var, ex_fn, ...) do { \
	static_assert(std_atomic<decltype(atomic_var)>); \
	using T = decltype(atomic_var)::value_type; \
	T mid___ = get(atomic_var); \
	res = comp_func(label, type, &mid___, ##__VA_ARGS__); \
	mid___ = ex_fn(mid___); \
	set(atomic_var, mid___); \
} while (false)
