#pragma once
template<typename Fn>
struct Defer {
	Fn m_fn;
	Defer(Fn fn) : m_fn(std::move(fn)) {}
	Defer(const Defer&) = delete;
	Defer(Defer&&) = delete;
	~Defer() { m_fn(); }
};

#define XCONCAT(x, y) x##y
#define CONCAT(x, y) XCONCAT(x, y) 
#define NAME(name) CONCAT(CONCAT(name##_, __LINE__), __)
#define DEFER Defer NAME(defer)
