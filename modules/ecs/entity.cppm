export module lumina.ecs:entity;

import std;

export namespace lumina::ecs
{

struct entity
{
public:
	using handle_type = std::uint32_t;
	using version_type = std::uint8_t;

	static constexpr std::uint32_t handle_mask = 0xFFFFFF;
	static constexpr std::uint32_t version_shift = 24;
	static constexpr std::uint32_t null = 0xFFFFFFFF;

	constexpr entity() noexcept : internal{null} {}
	constexpr entity(handle_type data) noexcept : internal{data} {}
	constexpr entity(handle_type handle, version_type version) noexcept : internal{handle | (version << version_shift)} {}
	constexpr entity(const entity& ent) noexcept : internal{ent.internal} {}
	constexpr entity(entity&& ent) noexcept : internal{ent.internal} {}

	constexpr entity& operator=(const entity& rhs) noexcept
	{
		internal = rhs.internal;
		return *this;
	}

	constexpr entity& operator=(entity&& rhs) noexcept
	{
		internal = rhs.internal;
		return *this;
	}

	constexpr bool operator==(const entity& rhs) const noexcept
	{
		return internal == rhs.internal;
	}

	constexpr bool operator!=(const entity& rhs) const noexcept
	{
		return internal != rhs.internal;
	}

	constexpr handle_type as_handle() const noexcept
	{
		return internal & handle_mask;
	}

	constexpr version_type as_version() const noexcept
	{
		return internal >> version_shift;
	}

	constexpr handle_type data() const noexcept
	{
		return internal;
	}

	constexpr bool is_valid() const noexcept
	{
		return internal != null;
	}
private:
	handle_type internal;
};

}

export template<>
struct std::formatter<lumina::ecs::entity>
{
	template <class ParseContext>
	constexpr ParseContext::iterator parse(ParseContext& ctx)
	{
		return ctx.begin();
	}

	template <class FmtContext>
	FmtContext::iterator format(const lumina::ecs::entity ent, FmtContext& ctx) const
	{
		if(ent.is_valid())
			std::format_to(ctx.out(), "entity: h {} | v {}", ent.as_handle(), ent.as_version());
		else
			std::format_to(ctx.out(), "entity: null");

		return ctx.out();
	}
};

