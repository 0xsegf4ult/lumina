module;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

export module lumina.core.log;

import std;

namespace lumina::log
{
	constexpr std::array<std::string_view, 5> size_units =
	{
		"B",
		"kiB",
		"MiB",
		"GiB",
		"TiB"
	};
}

export namespace lumina::log
{
	void init()
	{
		auto console = spdlog::stdout_color_mt("engine_log");

		console->set_pattern("[%t][%^%l%$] %v");
		console->set_level(spdlog::level::debug);

		spdlog::set_default_logger(console);
	}

	using spdlog::debug;
	using spdlog::info;
	using spdlog::warn;
	using spdlog::error;
	using spdlog::critical;

	struct FormattedSize
	{
		float size;
		std::string_view unit;
	};

	constexpr FormattedSize pretty_format_size(std::size_t sz)
	{
		std::size_t u = 0;

		auto uintpow = [](std::size_t mul, std::size_t pow)
		{
			std::size_t val = 1;

			for(std::size_t i = 0; i < pow; i++)
				val *= mul;

			return val;
		};

		for(std::size_t i = 1; i < size_units.size(); i++)
		{
			if(sz >= uintpow(1024, i))
				u = i;
		}

		return {static_cast<float>(sz) / static_cast<float>(uintpow(1024, u)), size_units[u]};
	}
}
