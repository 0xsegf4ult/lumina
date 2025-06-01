module;

#include <GLFW/glfw3.h>

export module lumina.platform:window;
import :input;
import lumina.core;

import std;

using std::uint32_t, std::size_t;

export namespace lumina::platform
{

void wm_init()
{
	glfwInit();
	glfwSetErrorCallback([](int error, const char* description) { log::error("glfw_error: {}({})", description, error); });
}

void wm_shutdown()
{
	glfwTerminate();
}


void wm_request_extensions(std::vector<const char*>& ext)
{
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	for(uint32_t i = 0; i < glfwExtensionCount; i++)
		ext.push_back(glfwExtensions[i]);
}

enum class InputLayers
{
	Engine = 1,
	UI = 1 << 1
};

using InputLayer = typesafe_flags<InputLayers>;

using KeyEventCallback = std::function<void(Key, KeyState)>;
using CharEventCallback = std::function<void(unsigned int)>;
using MouseButtonEventCallback = std::function<void(MouseButton, ButtonState)>;
using MouseMoveEventCallback = std::function<void(double x, double y, double dx, double dy)>;
using ScrollEventCallback = std::function<void(double dx, double dy)>;
using ResizeEventCallback = std::function<void(uint32_t w, uint32_t h)>;

template <typename T>
struct EventListener
{
	InputLayer layer;
	T callback;
};

class Window
{
public:
	Window(uint32_t _w, uint32_t _h) : w{_w}, h{_h}
	{
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		handle = glfwCreateWindow(static_cast<int>(w), static_cast<int>(h), "lumina::engine", nullptr, nullptr);
		if(!handle)
		{
			log::error("failed to create engine window");
		}

		glfwSetWindowUserPointer(handle, this);
		glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		if(glfwRawMouseMotionSupported())
		{
			log::info("platform: raw input supported + enabled");
			glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
		}

		glfwSetKeyCallback(handle, [](GLFWwindow* wptr, int key, [[maybe_unused]] int scancode, int action, [[maybe_unused]] int mods)
		{
			if(action == GLFW_REPEAT)
				return;

			Window* wnd = static_cast<Window*>(glfwGetWindowUserPointer(wptr));					if(key < 0)
				key = 0;	
		
			wnd->keyboard_input(keymap[static_cast<size_t>(key)], (action == GLFW_PRESS) ? KeyState::Down : KeyState::Up);
		});

		glfwSetCharCallback(handle, [](GLFWwindow* wptr, unsigned int code)
		{
			static_cast<Window*>(glfwGetWindowUserPointer(wptr))->char_input(code);
		});

		glfwSetMouseButtonCallback(handle, [](GLFWwindow* wptr, int button, int action, [[maybe_unused]]int mods)
		{
			Window* wnd = static_cast<Window*>(glfwGetWindowUserPointer(wptr));
			wnd->mouse_input(MouseButton(button), (action == GLFW_PRESS) ? ButtonState::Down : ButtonState::Up);
		});

		glfwSetCursorPosCallback(handle, [](GLFWwindow* wptr, double xpos, double ypos)
		{
			Window* wnd = static_cast<Window*>(glfwGetWindowUserPointer(wptr));
			wnd->mouse_move_input(xpos, ypos);
		});

		glfwSetScrollCallback(handle, [](GLFWwindow* wptr, double dx, double dy)
		{
			Window* wnd = static_cast<Window*>(glfwGetWindowUserPointer(wptr));
			wnd->scroll_input(dx, dy);
		});
	}

	~Window()
	{
		glfwDestroyWindow(handle);
	}

	bool requested_close()
	{
		return glfwWindowShouldClose(handle);
	}

	void pump_events()
	{
		glfwPollEvents();
	}

	void await_wm_resize()
	{
		int nw = 0;
		int nh = 0;
		while(nw == 0 || nh == 0)
		{
			glfwGetFramebufferSize(handle, &nw, &nh);
			glfwWaitEvents();
		}

		w = static_cast<uint32_t>(nw);
		h = static_cast<uint32_t>(nh);
	}

	void signal_wm_resize()
	{
		log::info("platform::window: resize event {}x{}", w, h);
		for(auto& callback : resize_event_listeners)
			callback(w, h);
	}

	GLFWwindow* native_handle() const
	{
		return handle;
	}

	void keyboard_input(Key key, KeyState state)
	{
		key_state[key] = state;

		for(auto& [layer, callback] : key_event_listeners)
		{
			if(cur_layer & layer)
				callback(key, state);
		}
	}

	void char_input(unsigned int c)
	{
		for(auto& [layer, callback] : char_event_listeners)
		{
			if(cur_layer & layer)
				callback(c);
		}	
	}

	void mouse_input(MouseButton button, ButtonState state)
	{
		for(auto& [layer, callback] : mouse_button_event_listeners)
		{
			if(cur_layer & layer)
				callback(button, state);
		}
	}

	void mouse_move_input(double x, double y)
	{
		if(invalidate_mouse)
		{
			m_lastX = x;
			m_lastY = y;
			invalidate_mouse = false;
		}

		m_deltaX = x - m_lastX;
		m_deltaY = y - m_lastY;
		m_lastX = x;
		m_lastY = y;

		for(auto& [layer, callback] : mouse_move_event_listeners)
		{
			if(cur_layer & layer)
				callback(x, y, m_deltaX, m_deltaY);
		}
	}

	void set_mouse_pos(double x, double y)
	{
		m_deltaX = 0.0;
		m_deltaY = 0.0;
		
		auto dx = x - m_lastX;
		auto dy = y - m_lastY;
		
		m_lastX = x;
		m_lastY = y;

		for(auto& [layer, callback] : mouse_move_event_listeners)
		{
			if(cur_layer & layer)
				callback(x, y, dx, dy);
		}
	}

	void scroll_input(double dx, double dy)
	{
		for(auto& [layer, callback] : scroll_event_listeners)
		{
			if(cur_layer & layer)
				callback(dx, dy);
		}
	}

	void set_input_layer(InputLayer layer)
	{
		cur_layer = layer;
		if(layer & InputLayers::Engine)
			glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		else
			glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}

	void register_key_event_listener(InputLayer layer, KeyEventCallback callback)
	{
		key_event_listeners.push_back({layer, callback});
	}

	void register_char_event_listener(InputLayer layer, CharEventCallback callback)
	{
		char_event_listeners.push_back({layer, callback});
	}

	void register_mouse_button_event_listener(InputLayer layer, MouseButtonEventCallback callback)
	{
		mouse_button_event_listeners.push_back({layer, callback});
	}

	void register_mouse_move_event_listener(InputLayer layer, MouseMoveEventCallback callback)
	{
		mouse_move_event_listeners.push_back({layer, callback});
	}
	
	void register_scroll_event_listener(InputLayer layer, ScrollEventCallback callback)
	{
		scroll_event_listeners.push_back({layer, callback});
	}

	void register_resize_event_listener(ResizeEventCallback callback)
	{
		resize_event_listeners.push_back(callback);
	}

	bool is_key_down(Key k)
	{
		return key_state[k] == KeyState::Down;
	}

	bool is_key_down(Key k, InputLayer layer)
	{
		if(!(layer & cur_layer))
			return false;

		return key_state[k] == KeyState::Down;
	}

	std::pair<double, double> get_mouse_pos()
	{
		return std::make_pair(m_lastX, m_lastY);
	}

	std::pair<double, double> get_mouse_delta(InputLayer layer = InputLayers::Engine)
	{
		if(!(layer & cur_layer))
			return std::make_pair(0.0, 0.0);

		double dx = m_deltaX;
		double dy = m_deltaY;

		m_deltaX = 0.0;
		m_deltaY = 0.0;

		return std::make_pair(dx, dy);
	}

	std::pair<uint32_t, uint32_t> get_extent()
	{
		return std::make_pair(w, h);
	}
private:
	bool invalidate_mouse = true;
	double m_lastX = 0.0;
	double m_lastY = 0.0;
	double m_deltaX = 0.0;
	double m_deltaY = 0.0;
	std::map<Key, KeyState> key_state;

	std::vector<EventListener<KeyEventCallback>> key_event_listeners;
	std::vector<EventListener<CharEventCallback>> char_event_listeners;
	std::vector<EventListener<MouseButtonEventCallback>> mouse_button_event_listeners;
	std::vector<EventListener<MouseMoveEventCallback>> mouse_move_event_listeners;
	std::vector<EventListener<ScrollEventCallback>> scroll_event_listeners;
	std::vector<ResizeEventCallback> resize_event_listeners;

	InputLayer cur_layer{InputLayers::Engine};

	uint32_t w;
	uint32_t h;
	GLFWwindow* handle{nullptr};
};

}

namespace lumina
{

export template<>
struct typesafe_flag_traits<platform::InputLayers>
{
	constexpr static bool bitmask_enabled = true;
};

}
