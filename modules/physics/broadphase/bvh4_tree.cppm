module;

#include <cassert>
#include <tracy/Tracy.hpp>

export module lumina.physics:bvh4_tree;

import :bvh4_node;
import :rigidbody_interface;
import lumina.core;
import std;

using std::uint32_t, std::memcpy;

namespace lumina::physics
{

struct BVH4NodeID
{
	BVH4NodeID() : handle{BVH4Node::invalid_index} {}
	BVH4NodeID(uint32_t h) : handle{h} {}
	BVH4NodeID(Handle<BVH4Node> h) : handle{h} {}
	BVH4NodeID(Handle<Rigidbody> r) : handle{r | BVH4Node::is_rigidbody_node_bit} {}

	operator uint32_t() const { return handle; }

	constexpr bool is_body() const noexcept
	{
		return handle & BVH4Node::is_rigidbody_node_bit;
	}

	constexpr bool is_node() const noexcept
	{
		return !is_body();
	}

	constexpr Handle<BVH4Node> as_node() const noexcept
	{
		assert(is_node());
		return Handle<BVH4Node>{handle};
	}

	constexpr Handle<Rigidbody> as_body() const noexcept
	{
		assert(is_body());
		return Handle<Rigidbody>{handle & (~BVH4Node::is_rigidbody_node_bit)};
	}
private:
	uint32_t handle;
};

struct TreeBuildResult
{
	BVH4NodeID root;
	AABB bounds;
};

struct TreeBuildContext
{
	ObjectPool<BVH4Node>& alloc;
	RigidbodyInterface& rr;
	std::span<BVH4NodeID> nodes;
	uint32_t force_dirty_level;
};

struct TreeNodeData
{
	uint32_t handle;
	AABB bounds;
};

struct TreeBuildRange
{
	BVH4NodeID node;
	AABB bounds;
	uint32_t first;
	uint32_t count;
	uint32_t level;
};

struct TreeBodyInfo
{
	Handle<Rigidbody> handle;
	vec3 center;
};

TreeBuildResult build_tree(TreeBuildContext&& ctx)
{
	ZoneScoped;
	assert(!ctx.nodes.empty());

	auto get_node_bounds = [&ctx](BVH4NodeID id) -> AABB
	{
		ZoneScoped;
		if(id.is_body())
			return ctx.rr.read_body(id.as_body()).get_transformed_bounds();
		else
		{
			BVH4Node& n = ctx.alloc.get(id.as_node());
			AABB bnd = n.get_child_bounds(0);
			for(uint32_t c = 1; c < 4; c++)
				bnd = AABB::merge(bnd, n.get_child_bounds(c));

			return bnd;
		}
	};

	if(ctx.nodes.size() == 1)
	{
		if(ctx.nodes[0].is_node())
			ctx.alloc.get(ctx.nodes[0].as_node()).parent = BVH4Node::invalid_index;

		return {ctx.nodes[0], get_node_bounds(ctx.nodes[0])};
	}

	Handle<BVH4Node> root = ctx.alloc.allocate();
	BVH4Node& root_node = ctx.alloc.get(root);
	root_node.invalidate();
	root_node.dirty = (ctx.force_dirty_level > 0u);

	auto node_data = std::make_unique_for_overwrite<TreeNodeData[]>(ctx.nodes.size());

	AABB root_bounds{vec3{1e30f}, vec3{-1e30f}};

	{

	ZoneScopedN("collect_nodes");
	for(uint32_t i = 0; i < ctx.nodes.size(); i++)
	{
		const AABB rbounds = get_node_bounds(ctx.nodes[i]);
		node_data[i].handle = ctx.nodes[i];
		node_data[i].bounds = rbounds;
		root_bounds = AABB::merge(root_bounds, rbounds);
	}

	}

	auto b_stack = std::make_unique_for_overwrite<TreeBuildRange[]>(128);
	b_stack[0] = {BVH4NodeID{root}, root_bounds, 0, static_cast<uint32_t>(ctx.nodes.size()), 0};
	uint32_t b_stack_top = 0;

	// might be inefficient since we merge AABBs both in the node containing the body and its parents
	
	auto spatial_partition_quad = [&node_data](TreeBuildRange& r)
	{
		ZoneScoped;
		auto spatial_partition_split = [&node_data](TreeBuildRange& range, bool increment_level = true)
		{
			ZoneScoped;
			AABB hlb{vec3{1e30f}, vec3{-1e30f}};
			AABB hrb = hlb;

			if(!range.count)
				return std::make_pair(TreeBuildRange{{}, hlb, range.first, 0, 0}, TreeBuildRange{{}, hrb, range.first, 0, 0});

			{

			ZoneScopedN("range_bounds");
			for(uint32_t i = range.first; i < range.first + range.count; i++)
				range.bounds = AABB::merge(range.bounds, node_data[i].bounds);

			}

			uint32_t axis = 0;
			vec3 len = range.bounds.maxs - range.bounds.mins;
			if(len.y > len.x)
				axis = 1;
			if(len.z > len.y && len.z > len.x)
				axis = 2;

			const float plane = 0.5f * (range.bounds.mins + range.bounds.maxs)[axis];

			TreeNodeData* part_begin;

			{
			ZoneScopedN("partition");

			part_begin = std::partition(node_data.get() + range.first, node_data.get() + range.first + range.count, [axis, plane](const TreeNodeData& in)
			{
				return (0.5f * (in.bounds.mins + in.bounds.maxs))[axis] < plane;
			});

			}

			uint32_t hl_count = static_cast<uint32_t>(part_begin - (node_data.get() + range.first));

			{
			ZoneScopedN("build_range_s0");
			
			for(uint32_t i = range.first; i < range.first + hl_count; i++)
				hlb = AABB::merge(hlb, node_data[i].bounds);
			}

			TreeBuildRange hl{{}, hlb, range.first, hl_count, range.level + increment_level};

			{
			ZoneScopedN("build_range_s1");

			for(uint32_t i = range.first + hl_count; i < range.first + range.count; i++)
				hrb = AABB::merge(hrb, node_data[i].bounds);
			}
			
			TreeBuildRange hr{{}, hrb, range.first + hl_count, range.count - hl_count, range.level + increment_level};

			return std::make_pair(hl, hr);
		};

		auto [hl, hr] = spatial_partition_split(r);

		auto [q0, q1] = spatial_partition_split(hl, false);
		auto [q2, q3] = spatial_partition_split(hr, false);

		return std::array<TreeBuildRange, 4>{q0, q1, q2, q3};
	};

	{
	ZoneScopedN("build_tree_stackwalk");

	for(;;)
	{
		if(b_stack_top >= 128)
		{
			log::critical("build_tree: temp stack overflow");
			break;
		}

		TreeBuildRange r = b_stack[b_stack_top];
		BVH4Node& node = ctx.alloc.get(r.node.as_node());

		if(r.count <= 4)
		{
			for(uint32_t i = r.first; i < r.first + r.count; i++)
			{
				BVH4NodeID cid = node_data[i].handle;
				node.children[i - r.first] = cid;
				node.set_child_bounds(i - r.first, node_data[i].bounds);
				if(cid.is_body())
					ctx.rr.get(cid.as_body()).userdata = (static_cast<uint64_t>(r.node) << 32) | (i - r.first);
				else
					ctx.alloc.get(cid.as_node()).parent = r.node;
			}
		}
		else
		{
			auto quads = spatial_partition_quad(r);

			for(uint32_t i = 0; i < 4; i++)
			{
				if(quads[i].count)
				{
					if(quads[i].count == 1)
					{
						BVH4NodeID cid = node_data[quads[i].first].handle;
						node.children[i] = cid;
						node.set_child_bounds(i, quads[i].bounds);
						if(cid.is_body())
							ctx.rr.get(cid.as_body()).userdata = (static_cast<uint64_t>(r.node) << 32) | i;
						else
							ctx.alloc.get(cid.as_node()).parent = r.node;
					}
					else
					{
						quads[i].node = ctx.alloc.allocate();
						BVH4Node& qn = ctx.alloc.get(quads[i].node.as_node());
						qn.invalidate();
						qn.parent = r.node;
						qn.dirty = ctx.force_dirty_level > quads[i].level;
						node.children[i] = quads[i].node;
						node.set_child_bounds(i, quads[i].bounds);
						b_stack[b_stack_top++] = quads[i];
					}
				}
			}
		}

		if(b_stack_top == 0)
			break;

		b_stack_top--;
	}
	
	}

	return {BVH4NodeID{root}, root_bounds};
}

void propagate_dirty_flag(ObjectPool<BVH4Node>& alloc, Handle<BVH4Node> node)
{
	ZoneScoped;
	uint32_t idx = node;

	for(;;)
	{
		if(idx == BVH4Node::invalid_index)
			return;

		BVH4Node& n = alloc.get(node);
		if(n.dirty)
			break;

		n.dirty = true;
		idx = n.parent;
	}
};

void propagate_node_bounds(ObjectPool<BVH4Node>& alloc, Handle<BVH4Node> node, AABB bounds)
{
	ZoneScoped;
	uint32_t idx = node;

	for(;;)
	{
		BVH4Node& n = alloc.get(Handle<BVH4Node>{idx});
		n.dirty = true;

		Handle<BVH4Node> pid{n.parent};
		assert(pid != idx);

		if(pid == BVH4Node::invalid_index)
			return;

		BVH4Node& parent = alloc.get(pid);
		uint32_t child = 4;
		for(uint32_t i = 0; i < 4; i++)
		{
			if(parent.children[i] == idx)
			{
				child = i;

				if(!parent.enlarge_child_bounds(child, bounds))
				{
					if(!parent.dirty)
						propagate_dirty_flag(alloc, Handle<BVH4Node>{pid});
					return;
				}

				break;
			}
		}

		idx = pid;
	}
}

export struct Raycast
{
	vec3 origin;
	vec3 dir;
};

export struct RaycastResult
{
	Handle<Rigidbody> body;
	float t;
};

export struct AABBCast
{
	AABB bounds;
	vec3 dir;
};

export struct AABBCastResult
{
	Handle<Rigidbody> body;
	float t;
};

export class BVH4Tree
{
public:
	friend class BroadphaseInterface;
	using NodeAllocator = ObjectPool<BVH4Node>;

	BVH4Tree() = default;

	void init(NodeAllocator* alloc, RigidbodyInterface* rr)
	{
		allocator = alloc;
		rigidbody_interface = rr;
		root_nodes[0] = allocator->allocate();
		allocator->get(Handle<BVH4Node>{root_nodes[0]}).invalidate();
	}

	void request_insert(std::span<Handle<Rigidbody>> bodies)
	{
		ZoneScoped;

		auto [subtree_root, subtree_bounds] = build_tree({*allocator, *rigidbody_interface, {reinterpret_cast<BVH4NodeID*>(bodies.data()), bodies.size()}, 0u});

		dirty = true;

		tree_bodies += bodies.size();

		for(;;)
		{
			ZoneScopedN("try_insert");

			if(insert_subtree(subtree_root, subtree_bounds))
				return;

			if(insert_new_root(subtree_root, subtree_bounds))
				return;
		}
	}

	void signal_body_updates(std::span<Handle<Rigidbody>> bodies)
	{
		ZoneScoped;

		for(auto body : bodies)
		{
			AABB rbounds = rigidbody_interface->read_body(body).get_transformed_bounds();
			uint64_t data = rigidbody_interface->read_body(body).userdata;
			BVH4NodeID nid;
			uint32_t cid;
			nid = data >> 32;
			cid = data & 0x3;

			assert(nid.is_node());

			BVH4Node& node = allocator->get(nid.as_node());
			if(node.enlarge_child_bounds(cid, rbounds))
			{
				dirty = true;
				propagate_node_bounds(*allocator, nid.as_node(), rbounds);
			}
		}
	}

	void remove_bodies(std::span<Handle<Rigidbody>> bodies)
	{
		ZoneScoped;

		dirty = true;

		for(auto body : bodies)
		{
			BVH4NodeID nid;
			uint32_t cid;

			uint64_t data = rigidbody_interface->read_body(body).userdata;
			rigidbody_interface->get(body).userdata = static_cast<uint64_t>(BVH4Node::invalid_index) << 32;
			nid = data >> 32;
			cid = data & 0x3;

			assert(nid.is_node());

			BVH4Node& node = allocator->get(nid.as_node());
			node.invalidate_child_bounds(cid);
			node.children[cid] = BVH4Node::invalid_index;
			propagate_dirty_flag(*allocator, nid.as_node());
		}

		tree_bodies -= bodies.size();
	}

	void rebuild_tree()
	{
		ZoneScoped;

		dirty = false;

		Handle<BVH4Node> root = get_current_root();

		std::array<BVH4NodeID, 128> n_stack;
		uint32_t n_stack_top = 0;
		n_stack[0] = root;

		auto ntree_nodes = std::make_unique_for_overwrite<BVH4NodeID[]>(tree_bodies);
		uint32_t ntree_top = 0;

		{
		ZoneScopedN("collect_dirty");
		for(;;)
		{
			BVH4NodeID id = n_stack[n_stack_top];
			if(id.is_body())
			{
				ntree_nodes[ntree_top++] = id;
			}
			else
			{
				const BVH4Node& node = allocator->get(id.as_node());

				if(node.dirty)
				{
					for(auto& child : node.children)
					{
						if(child == BVH4Node::invalid_index)
							continue;

						if(n_stack_top == 128)
						{
							log::warn("rebuild_tree: out of stack space");
							ntree_nodes[ntree_top++] = BVH4NodeID{child};
							continue;
						}

						n_stack[n_stack_top++] = BVH4NodeID{child};
					}

					discard_nodes.push_back(id.as_node());
				}
				else
				{
					ntree_nodes[ntree_top++] = id;
				}
			}

			if(n_stack_top == 0)
				break;

			n_stack_top--;
		}
		}

		BVH4NodeID new_root;
		if(ntree_top == 0)
		{
			new_root = allocator->allocate();
			allocator->get(new_root.as_node()).invalidate();
		}
		else
		{
			auto [rnode, rbounds] = build_tree({*allocator, *rigidbody_interface, {ntree_nodes.get(), ntree_top}, 5u});

			if(rnode.is_body())
			{
				new_root = allocator->allocate();
				BVH4Node& nr = allocator->get(new_root.as_node());
				nr.invalidate();
				nr.set_child_bounds(0, rbounds);
				nr.children[0] = rnode;
				rigidbody_interface->get(rnode.as_body()).userdata = (static_cast<uint64_t>(new_root) << 32) | 0;
			}
			else
			{
				new_root = rnode;
			}
		}

		root_switch_target = new_root.as_node();
	}

	void switch_root()
	{
		ZoneScoped;
		if(root_switch_target == BVH4Node::invalid_index)
			return;

		log::debug("switching root");

		uint32_t new_root = (active_root + 1) % 2;
		root_nodes[new_root].store(root_switch_target.load());
		root_switch_target = BVH4Node::invalid_index;
		active_root = new_root;

		{
		ZoneScopedN("discard_nodes");
		for(auto node : discard_nodes)
			allocator->deallocate(node);

		discard_nodes.clear();
		}
	}

	RaycastResult cast_ray(const Raycast& ray, Handle<Rigidbody> ignore = RigidbodyInterface::invalid_handle)
	{
		ZoneScoped;

		struct RCStackEntry
		{
			BVH4NodeID id;
			float t;
		};

		float inf = std::numeric_limits<float>::infinity();

		std::array<RCStackEntry, 128> c_stack;
		uint32_t c_stack_top = 0;
		c_stack[0] = {get_current_root(), -1.0f};
		float cur_ray_t = 1.0f;

		bool hit = false;
		RaycastResult res{RigidbodyInterface::invalid_handle, inf};

		for(;;)
		{
			RCStackEntry entry = c_stack[c_stack_top];

			if(entry.t < cur_ray_t)
			{
				if(entry.id.is_node())
				{
					BVH4Node& node = allocator->get(entry.id.as_node());
					const SIMD4AABB bnd = node.extract_bounds_simd4();

					vec3 inv_r_dir{1.0f};
					inv_r_dir.x /= ray.dir.x;
					inv_r_dir.y /= ray.dir.y;
					inv_r_dir.z /= ray.dir.z;

					const vec4 ray_t = ray_test_aabb_simd4(ray.origin, inv_r_dir, bnd);

					std::array<RCStackEntry, 4> tmp_cstack;
					for(uint32_t i = 0; i < 4; i++)
						tmp_cstack[i] = RCStackEntry{BVH4NodeID{node.children[i]}, ray_t[i]};

					std::sort(&tmp_cstack[0], &tmp_cstack[0] + 4, [](const RCStackEntry& lhs, const RCStackEntry& rhs)
					{
						return lhs.t > rhs.t;
					});

					for(uint32_t i = 0; i < 4; i++)
					{
						if(tmp_cstack[i].t < cur_ray_t && tmp_cstack[i].id != BVH4Node::invalid_index)
							c_stack[c_stack_top++] = tmp_cstack[i];
					}
				}
				else
				{
					if(entry.id.as_body() != ignore)
					{

						if(!hit || entry.t < res.t)
						{
							res.body = entry.id.as_body();
							res.t = entry.t;
							cur_ray_t = entry.t;
							hit = true;
						}
					}
				}
			}

			if(c_stack_top == 0)
				break;

			c_stack_top--;
		}

		return res;
	}

	void cast_aabb(const AABBCast& cast, std::vector<AABBCastResult>& out, Handle<Rigidbody> ignore = RigidbodyInterface::invalid_handle)
	{
		ZoneScoped;

		struct CStackEntry
		{
			BVH4NodeID id;
			float t;
		};

		std::array<CStackEntry, 128> c_stack;
		uint32_t c_stack_top = 0;
		c_stack[0] = {get_current_root(), -1.0f};
		float cur_ray_t = 1.0f;

		for(;;)
		{
			CStackEntry entry = c_stack[c_stack_top];

			if(entry.t < cur_ray_t)
			{
				if(entry.id.is_node())
				{
					const BVH4Node& node = allocator->get(entry.id.as_node());
					const vec3 extents = cast.bounds.get_extents();
					SIMD4AABB bnd = node.extract_bounds_simd4();

					bnd.minX -= vec4{extents.x};
					bnd.minY -= vec4{extents.y};
					bnd.minZ -= vec4{extents.z};
					bnd.maxX += vec4{extents.x};
					bnd.maxY += vec4{extents.y};
					bnd.maxZ += vec4{extents.z};

					vec3 inv_r_dir{1.0f};
					inv_r_dir.x /= cast.dir.x;
					inv_r_dir.y /= cast.dir.y;
					inv_r_dir.z /= cast.dir.z;

					vec4 ray_t = ray_test_aabb_simd4(cast.bounds.get_center(), inv_r_dir, bnd);

					std::array<CStackEntry, 4> tmp_cstack;
					for(uint32_t i = 0; i < 4; i++)
						tmp_cstack[i] = CStackEntry{BVH4NodeID{node.children[i]}, ray_t[i]};

					std::sort(&tmp_cstack[0], &tmp_cstack[0] + 4, [](const CStackEntry& lhs, const CStackEntry& rhs)
					{
						return lhs.t > rhs.t;
					});

					for(uint32_t i = 0; i < 4; i++)
					{
						if(tmp_cstack[i].t < cur_ray_t && tmp_cstack[i].id != BVH4Node::invalid_index)
						{
							c_stack[c_stack_top++] = tmp_cstack[i];
						}
					}
				}
				else
				{
					if(entry.id != ignore)
					{
						if(entry.t < cur_ray_t)
						{
							out.push_back({entry.id.as_body(), entry.t});
							cur_ray_t = entry.t;
						}
					}
				}
			}

			if(c_stack_top == 0)
				break;

			c_stack_top--;
		}
	}

	void collect_colliding_pairs(std::span<Handle<Rigidbody>> bodies, std::vector<RigidbodyPair>& pairs)
	{
		std::array<BVH4NodeID, 128> c_stack;
		uint32_t c_stack_top;

		for(auto body : bodies)
		{
			const AABB bnd1 = rigidbody_interface->read_body(body).get_transformed_bounds();
			c_stack_top = 0;
			c_stack[0] = get_current_root();

			for(;;)
			{
				BVH4NodeID entry = c_stack[c_stack_top];

				if(entry.is_body())
				{
					if(entry != body)
					{
						const AABB bnd2 = rigidbody_interface->read_body(entry.as_body()).get_transformed_bounds();
						if(AABB::check_intersect(bnd1, bnd2))
						{
							pairs.push_back({body, entry.as_body()});
						}
					}

				}
				else
				{
					const BVH4Node& node = allocator->get(entry.as_node());
					const SIMD4AABB bnd = node.extract_bounds_simd4();
					uvec4 children;
					memcpy(&children[0], &node.children[0], 16);
					const uvec4 box_t = aabb_test_aabb_simd4(bnd1, bnd);

					struct CStackEntry
					{
						BVH4NodeID id;
						uint32_t test;
					};

					std::array<CStackEntry, 4> tmp_c_stack;
					for(uint32_t i = 0; i < 4; i++)
						tmp_c_stack[i] = {children[i], box_t[i]};
					
					std::sort(&tmp_c_stack[0], &tmp_c_stack[0] + 4, [](const CStackEntry& lhs, const CStackEntry& rhs)
					{
						return lhs.test > rhs.test;
					});

					for(uint32_t i = 0; i < 4; i++)
					{
						if(tmp_c_stack[i].test && tmp_c_stack[i].id != BVH4Node::invalid_index)
							c_stack[c_stack_top++] = tmp_c_stack[i].id;
					}
				}

				if(c_stack_top == 0)
					break;

				c_stack_top--;
			}
		}
	}
private:
	bool insert_subtree(BVH4NodeID node, AABB& bounds)
	{
		ZoneScoped;

		Handle<BVH4Node> root = get_current_root();
		BVH4Node& rnode = allocator->get(root);

		if(node.is_node())
			allocator->get(node.as_node()).parent = root;

		uint32_t inv = BVH4Node::invalid_index;
		for(uint32_t i = 0; i < 4; i++)
		{
			if(rnode.children[i].compare_exchange_strong(inv, uint32_t(node)))
			{
				rnode.set_child_bounds(i, bounds);
				rnode.dirty = true;
				if(node.is_body())
					rigidbody_interface->get(node.as_body()).userdata = (static_cast<uint64_t>(root) << 32) | i;
				
				log::debug("added subtree");

				return true;
			}
		}

		return false;
	}

	bool insert_new_root(BVH4NodeID node, AABB& bounds)
	{
		ZoneScoped;

		Handle<BVH4Node> new_root = allocator->allocate();
		BVH4Node& nr_node = allocator->get(new_root);
		nr_node.invalidate();
		nr_node.dirty = true;

		Handle<BVH4Node> root = get_current_root();
		BVH4Node& rnode = allocator->get(root);

		if(node.is_node())
		{
			allocator->get(node.as_node()).parent = new_root;
		}
		else
		{
			rigidbody_interface->get(node.as_body()).userdata = (static_cast<uint64_t>(new_root) << 32) | 1;
		}

		nr_node.children[0] = root;
		nr_node.set_child_bounds(0, AABB{vec3{-1e30f}, vec3{1e30f}});
		nr_node.children[1] = node;
		nr_node.set_child_bounds(1, bounds);

		if(root_nodes[active_root].compare_exchange_strong(root, new_root))
		{
			rnode.parent = new_root;
			return true;
		}

		allocator->deallocate(new_root);
		return false;
	}

	NodeAllocator* allocator{nullptr};
	RigidbodyInterface* rigidbody_interface{nullptr};

	Handle<BVH4Node> get_current_root()	
	{
		return Handle<BVH4Node>{root_nodes[active_root]};
	}

	std::array<std::atomic<uint32_t>, 2> root_nodes;
	std::atomic<uint32_t> active_root{0u};
	std::atomic<uint32_t> root_switch_target{BVH4Node::invalid_index};

	bool dirty{false};
	uint32_t tree_bodies{0u};
	std::vector<Handle<BVH4Node>> discard_nodes;
};

};


