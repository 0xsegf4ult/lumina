export module lumina.physics:broadphase_interface;

export import :bvh4_tree;
import :bvh4_node;
import :rigidbody_interface;

import lumina.core;

import std;

namespace lumina::physics
{

export class BroadphaseInterface
{
public:
	BroadphaseInterface(RigidbodyInterface& rr) : allocator{"bvh4_node_allocator"}
	{
		uint32_t estimated_max_nodes = 512;
		allocator.init(2 * estimated_max_nodes, 2 * estimated_max_nodes);

		num_layers = 1u;
		layers = new BVH4Tree[num_layers];
		for(uint32_t i = 0; i < num_layers; i++)
			layers[i].init(&allocator, &rr);
	}

	~BroadphaseInterface()
	{
		delete[] layers;
	}

	void request_insert(std::span<Handle<Rigidbody>> rigidbodies)
	{
		layers[0].request_insert(rigidbodies);
	}

	void signal_body_updates(std::span<Handle<Rigidbody>> rigidbodies)
	{
		layers[0].signal_body_updates(rigidbodies);
	}

	void remove_bodies(std::span<Handle<Rigidbody>> rigidbodies)
	{
		layers[0].remove_bodies(rigidbodies);
	}

	RaycastResult cast_ray(const Raycast& ray, Handle<Rigidbody> ignore = RigidbodyInterface::invalid_handle)
	{
		return layers[0].cast_ray(ray, ignore);
	}

	void cast_aabb(const AABBCast& cast, std::vector<AABBCastResult>& out, Handle<Rigidbody> ignore = RigidbodyInterface::invalid_handle)
	{
		return layers[0].cast_aabb(cast, out, ignore);
	}

	void collect_colliding_pairs(std::span<Handle<Rigidbody>> bodies, std::vector<RigidbodyPair>& pairs)
	{
		layers[0].collect_colliding_pairs(bodies, pairs);
	}

	void ready_update()
	{
		for(uint32_t i = 0; i < num_layers; i++)
		{
			if(layers[i].dirty)
				layers[i].rebuild_tree();
		}
	}

	void finalize_update()
	{
		for(uint32_t i = 0; i < num_layers; i++)
			layers[i].switch_root();
	}
private:
	BVH4Tree::NodeAllocator allocator;

	BVH4Tree* layers{nullptr};
	uint32_t num_layers{0u};	
};

}		
