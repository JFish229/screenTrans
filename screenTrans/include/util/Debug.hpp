#pragma once

#ifndef NDEBUG
#	include <iostream>
	inline bool check(bool ok, const char* file, size_t line, const char* content) {
		if (!ok) {
			std::cerr << "[" << content << "] check false: " << file << ", line" << line << "\n";
		}
		return ok;
	}
#	define CHECK(ok) check(!!(ok), __FILE__, __LINE__)
#else
#	define CHECK(ok) (!!(ok))
#endif