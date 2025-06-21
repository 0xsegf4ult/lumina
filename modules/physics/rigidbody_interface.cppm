export module lumina.physics:rigidbody_interface;

import lumina.core;
import std;
import lumina.physics.collision;

export namespace lumina::physics
{

enum class BodyType
{
	Static,
	Kinematic,
	Dynamic
};

enum class MotionType
{
	Discrete,
	LinearCCD
};

struct RigidbodyDescription
{
	Transform initial_transform;
	const RefCounted<CShape>& shape;
	BodyType body_type{BodyType::Dynamic};
	MotionType motion_type{MotionType::Discrete};
};

struct Rigidbody
{
	constexpr static std::uint32_t broadphase_node_bit = (1u << 30);
	constexpr static std::uint32_t handle_mask = 0x3FFFFFFF;
	
	Transform transform;
	
	vec3 velocity{0.0f};
	vec3 angular_velocity{0.0f};

	vec3 forces{0.0f};
	vec3 torque{0.0f};
	
	float inverse_mass = 1.0f;
	
	mat3 inv_inertia_tensor_world;
	mat3 inv_inertia_tensor_local;

	RefCounted<CShape> collider;
	BodyType body_type;
	MotionType motion_type;
	std::uint64_t userdata;

	[[nodiscard]] constexpr AABB get_transformed_bounds() const
	{
		vec3 mins = collider->get_bounds().mins + transform.translation;
		vec3 maxs = collider->get_bounds().maxs + transform.translation;
		
		if(transform.rotation != Quaternion{0.0f, 0.0f, 0.0f, 1.0f})
		{
			const vec3 xn{-1.0f, 0.0f, 0.0f};
			const vec3 yn{0.0f, -1.0f, 0.0f};
			const vec3 zn{0.0f, 0.0f, -1.0f};
			const vec3 xp{1.0f, 0.0f, 0.0f};
			const vec3 yp{0.0f, 1.0f, 0.0f};
			const vec3 zp{0.0f, 0.0f, 1.0f};

			mat4 local_to_world = transform.as_matrix();
			const mat3 world_to_local = mat3::transpose(Quaternion::make_mat3(transform.rotation));

			const vec3 xn_loc = (xn * world_to_local);
			const vec3 yn_loc = (yn * world_to_local);
			const vec3 zn_loc = (zn * world_to_local);
			const vec3 xp_loc = (xp * world_to_local);
			const vec3 yp_loc = (yp * world_to_local);
			const vec3 zp_loc = (zp * world_to_local);

			mins.x = (vec4{collider->get_support(xn_loc), 1.0f} * local_to_world).x; 
			mins.y = (vec4{collider->get_support(yn_loc), 1.0f} * local_to_world).y;
			mins.z = (vec4{collider->get_support(zn_loc), 1.0f} * local_to_world).z;
			maxs.x = (vec4{collider->get_support(xp_loc), 1.0f} * local_to_world).x; 
			maxs.y = (vec4{collider->get_support(yp_loc), 1.0f} * local_to_world).y;
			maxs.z = (vec4{collider->get_support(zp_loc), 1.0f} * local_to_world).z; 
		}
	
		return AABB{mins, maxs};
	}
};

struct RigidbodyPair
{
	Handle<Rigidbody> r0;
	Handle<Rigidbody> r1;

	constexpr bool operator==(const RigidbodyPair& rhs) const noexcept
	{
		return (r0 == rhs.r0 && r1 == rhs.r1) || (r0 == rhs.r1 && r1 == rhs.r0);
	}

	constexpr bool operator<(const RigidbodyPair& rhs) const noexcept
	{
		return std::tie(r0, r1) < std::tie(rhs.r0, rhs.r1);
	}
};

using RigidbodyAllocator = ObjectPool<Rigidbody>;

class RigidbodyInterface
{
public:
	constexpr static Handle<Rigidbody> invalid_handle = Handle<Rigidbody>{RigidbodyAllocator::invalid_object};
	constexpr static std::uint32_t capacity = 1024u;
	constexpr static std::size_t mutex_slots = 32;

	RigidbodyInterface() : allocator{"rigidbody_allocator"}
	{
		allocator.init(capacity, capacity);
	}

	Handle<Rigidbody> create_rigidbody(const RigidbodyDescription& desc)
	{
		auto alloc = allocator.allocate();
		Rigidbody& rb = allocator.get(alloc);

		rb.collider = desc.shape;
		rb.transform = desc.initial_transform;
		rb.body_type = desc.body_type;
		rb.motion_type = desc.motion_type;

		const float mass = rb.collider->get_mass();

		if(mass == 0.0f)
			rb.inverse_mass = 0.0f;
		else
			rb.inverse_mass = 1.0f / mass;

		mat3 inertia_tensor = rb.collider->get_inertia_tensor();

		mat3 rotM = Quaternion::make_mat3(rb.transform.rotation);
		inertia_tensor = mat3::transpose(rotM) * inertia_tensor * rotM;

		rb.inv_inertia_tensor_local = mat3::inverse(inertia_tensor);

		return Handle<Rigidbody>{alloc | Rigidbody::broadphase_node_bit};
	}

	void destroy_bodies(std::span<Handle<Rigidbody>> bodies)
	{
		for(auto& body : bodies)
		{
			const std::uint32_t index = body & Rigidbody::handle_mask;
			std::unique_lock<std::shared_mutex> lock{body_locks[index % mutex_slots]};
			allocator.deallocate(Handle<Rigidbody>{index});
		}
	}

	Rigidbody& get(Handle<Rigidbody> handle)
	{
		const std::uint32_t index = handle & Rigidbody::handle_mask;
		std::unique_lock<std::shared_mutex> lock{body_locks[index % mutex_slots]};

		return allocator.get(Handle<Rigidbody>{index});
	}

	const Rigidbody& read_body(Handle<Rigidbody> handle) 
	{
		const std::uint32_t index = handle & Rigidbody::handle_mask;
		std::shared_lock<std::shared_mutex> lock{body_locks[index & mutex_slots]};

		return allocator.get(Handle<Rigidbody>{index});
	}
private:
	RigidbodyAllocator allocator;
	std::array<std::shared_mutex, mutex_slots> body_locks;
};

}
