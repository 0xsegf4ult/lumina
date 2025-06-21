export module lumina.renderer:camera;

import lumina.core.math;

import std;
using std::uint32_t;

export namespace lumina::render
{

enum class CameraProjection
{
	Perspective,
	Orthographic
};

enum class CameraMode
{
	Flycam,
	FPS
};

struct PerspectiveCameraInfo
{
	float fov;
	float znear;
	float zfar;
	float width{1366};
	float height{768};
	CameraMode mode{CameraMode::Flycam};
};

struct OrthographicCameraInfo
{
	float left;
	float right;
	float bottom;
	float top;
	float znear{0.0};
	float zfar{1.0};
};

class Camera
{
public:
	friend Camera make_perspective_camera(const PerspectiveCameraInfo&);
	friend Camera make_orthographic_camera(const OrthographicCameraInfo&);

	struct Vectors
	{
		vec3 front{vector_world_forward};
		vec3 right{vector_world_right};
		vec3 up{vector_world_up};
	};

	[[nodiscard]] constexpr mat4 get_view_matrix() const
	{
		return mat4::make_translation(-1.0f * pos) * mat4::make_rotY(yaw) * mat4::make_rotX(pitch);
	}

	[[nodiscard]] constexpr mat4 get_projection_matrix() const
	{
		if(proj == CameraProjection::Perspective)
		{
			const float focal_length = 1.0f / std::tan(fov / 2.0f);
			const float aspect_ratio = width / height;
			const float x = focal_length / aspect_ratio;
			const float y = -focal_length;

			return
			{
				vec4{x,     0.0f,  0.0f,  0.0f},
				vec4{0.0f,     y,  0.0f,  0.0f},
				vec4{0.0f,  0.0f,  0.0f, -1.0f},
				vec4{0.0f,  0.0f,  near,  0.0f}
			};
		}
		else
		{
			return mat4::make_ortho(left, right, bottom, top, near, far);
		}
	}

	[[nodiscard]] mat4 get_persp_mtx_farplane() const
	{
		const float focal_length = 1.0f / std::tan(fov / 2.0f);
		const float aspect_ratio = width / height;
		const float x = focal_length / aspect_ratio;
		const float y = -focal_length;

		const float t1 = near / (far - near);
		const float t2 = (far * near) / (far - near);

		return
		{
			vec4{x,     0.0f,  0.0f,  0.0f},
			vec4{0.0f,     y,  0.0f,  0.0f},
			vec4{0.0f,  0.0f,    t1, -1.0f},
			vec4{0.0f,  0.0f,    t2,  0.0f}
		};
	}

	[[nodiscard]] constexpr mat4 get_matrix() const
	{
		return get_view_matrix() * get_projection_matrix();
	}

	[[nodiscard]] constexpr vec3 get_pos() const
	{
		return pos;
	}

	void update_pos(const vec3& new_pos)
	{
		pos = new_pos;
	}

	void look_at(const vec3& at)
	{
		const vec3 direction = vec3::normalize(at - pos);
		pitch = -std::asin(direction.y);
		yaw = std::atan2(direction.x, -direction.z);
		update_vectors();
	}

	void update_rot(float nyaw, float npitch)
	{
		yaw = nyaw;
		pitch = npitch;
		update_vectors();
	}

	void update_res(uint32_t w, uint32_t h)
	{
		width = static_cast<float>(w);
		height = static_cast<float>(h);
	}

	void update_res(float w, float h)
	{
		width = w;
		height = h;
	}

	void update_ortho_bounds(const OrthographicCameraInfo& info)
	{
		left = info.left;
		right = info.right;
		bottom = info.bottom;
		top = info.top;
		near = info.znear;
		far = info.zfar;	
	}

	[[nodiscard]] constexpr OrthographicCameraInfo get_ortho_bounds() const
	{
		return {left, right, bottom, top, near, far};
	}

	[[nodiscard]] constexpr uvec2 get_res() const
	{
		return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
	}

	[[nodiscard]] constexpr const Camera::Vectors& get_vectors() const
	{
		return vectors;
	}

	[[nodiscard]] constexpr const vec2 get_clip_planes() const
	{
		return {near, far};
	}

	void update_mode(CameraMode m)
	{
		mode = m;
	}
private:
	void update_vectors()
	{
		vectors.front = vec3::normalize 
		({
			std::sin(yaw),
			(proj == CameraProjection::Perspective && mode == CameraMode::FPS) ? 0.0f : -std::sin(pitch),
			-std::cos(yaw)
		});
		
		vectors.right = vec3::normalize(vec3::cross(vectors.front, vector_world_up));
		vectors.up = vec3::normalize(vec3::cross(vectors.right, vectors.front));
	}

	CameraProjection proj{};

	union
	{
		CameraMode mode{CameraMode::Flycam};
		float left;
	};

	union
	{
		float width;
		float right;
	};

	union
	{
		float height;
		float bottom;
	};

	union
	{
		float fov;
		float top;
	};

	float near;
	float far;

	float yaw = 0.0f;
	float pitch = 0.0f;
	vec3 pos{0.0f};

	Vectors vectors;	
};

Camera make_perspective_camera(const PerspectiveCameraInfo& info)
{
	Camera cam;
	cam.fov = to_radians(info.fov);
	cam.near = info.znear;
	cam.far = info.zfar;
	cam.width = info.width;
	cam.height = info.height;
	cam.proj = CameraProjection::Perspective;
	cam.mode = info.mode;
	cam.update_vectors();

	return cam;
}

Camera make_orthographic_camera(const OrthographicCameraInfo& info)
{
	Camera cam;
	cam.left = info.left;
	cam.right = info.right;
	cam.bottom = info.bottom;
	cam.top = info.top;
	cam.near = info.znear;
	cam.far = info.zfar;
	cam.proj = CameraProjection::Orthographic;
	cam.update_vectors();

	return cam;
}

}
