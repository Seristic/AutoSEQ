#pragma once

// This file is required.

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <format>

using namespace std::literals;


#define LOG_INFO(...) SKSE::log::info(__VA_ARGS__)
#define LOG_WARN(...) SKSE::log::warn(__VA_ARGS__)
#define LOG_ERROR(...) SKSE::log::error(__VA_ARGS__)
#define LOG_DEBUG(...) SKSE::log::debug(__VA_ARGS__)