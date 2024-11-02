module;

#include <cassert>

export module lumina.renderer:animation;

import lumina.core;
import std;

export namespace lumina::render
{

struct Skeleton
{
	std::string name;

	std::vector<std::string> bone_names;
	std::vector<Transform> bone_transforms;
	std::vector<std::uint16_t> bone_parents;
	std::vector<mat4> bone_inv_bind_matrices;

	std::uint16_t bone_count;
};

enum class AnimationPath
{
	Translation,
	Rotation,
	Scale
};

enum class AnimationInterp
{
	Constant,
	Linear,
	CubicSpline
};

struct AnimationChannel
{
	std::uint32_t bone;
	AnimationPath path;
	AnimationInterp interp;

	std::vector<float> timestamps;
	std::vector<float> values;

	std::span<vec3> values_as_vec3()
	{
		assert(path == AnimationPath::Translation || path == AnimationPath::Scale);
		return {reinterpret_cast<vec3*>(values.data()), values.size() / 3};
	}

	std::span<Quaternion> values_as_quat()
	{
		assert(path == AnimationPath::Rotation);
		return {reinterpret_cast<Quaternion*>(values.data()), values.size() / 4};
	}
};

struct Animation
{
	std::string name;
	Handle<Skeleton> ref_skeleton;

	std::vector<AnimationChannel> channels;

	float start_time = std::numeric_limits<float>::max();
	float end_time = std::numeric_limits<float>::lowest();
};

}
