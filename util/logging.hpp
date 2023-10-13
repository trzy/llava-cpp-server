#pragma once
#ifndef INCLUDED_LOGGING_HPP
#define INCLUDED_LOGGING_HPP

#include <cstdlib>
#include <iostream>

// Temporary until a real logging system is put in place

#define LOG_INFO_F(FMT, ...)  do { printf(FMT "\n", ##__VA_ARGS__); } while (0)
#define LOG_ERROR_F(FMT, ...) do { fprintf(stderr, "Error: " FMT "\n",##__VA_ARGS__); } while (0)

#define LOG_INFO(S)           do { std::cout << S << std::endl; } while (0)
#define LOG_ERROR(S)          do { std::cerr << "Error: " << S << std::endl; } while (0)

#endif  // INCLUDED_LOGGING_HPP
