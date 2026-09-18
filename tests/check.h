#pragma once
#include <cstdlib>
#include <iostream>

#define CHECK(condition) do { if(!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " << #condition << '\n'; \
    std::abort(); \
} } while(false)
