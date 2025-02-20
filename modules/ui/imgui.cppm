export module lumina.ui:imgui;

import lumina.core;
import lumina.platform;
import lumina.vulkan;

import std;
import imgui;

using std::uint32_t, std::int32_t, std::uint16_t, std::size_t, std::uint8_t, std::memcpy;

namespace lumina::ui
{

ImGuiKey platform_key_to_imgui_key(platform::Key key)
{
	using enum platform::Key;
        switch(key)
        {
        case SPACE: return ImGuiKey_Space;
        case APOSTROPHE: return ImGuiKey_Apostrophe;
        case COMMA: return ImGuiKey_Comma;
        case MINUS: return ImGuiKey_Minus;
        case PERIOD: return ImGuiKey_Period;
        case SLASH: return ImGuiKey_Slash;
        case NUM0: return ImGuiKey_0;
        case NUM1: return ImGuiKey_1;
        case NUM2: return ImGuiKey_2;
        case NUM3: return ImGuiKey_3;
        case NUM4: return ImGuiKey_4;
        case NUM5: return ImGuiKey_5;
        case NUM6: return ImGuiKey_6;
        case NUM7: return ImGuiKey_7;
        case NUM8: return ImGuiKey_8;
        case NUM9: return ImGuiKey_9;
        case SEMICOLON: return ImGuiKey_Semicolon;
        case EQUAL: return ImGuiKey_Equal;
        case A: return ImGuiKey_A;
        case B: return ImGuiKey_B;
        case C: return ImGuiKey_C;
        case D: return ImGuiKey_D;
        case E: return ImGuiKey_E;
        case F: return ImGuiKey_F;
        case G: return ImGuiKey_G;
        case H: return ImGuiKey_H;
        case I: return ImGuiKey_I;
        case J: return ImGuiKey_J;
        case K: return ImGuiKey_K;
        case L: return ImGuiKey_L;
        case M: return ImGuiKey_M;
        case N: return ImGuiKey_N;
        case O: return ImGuiKey_O;
        case P: return ImGuiKey_P;
        case Q: return ImGuiKey_Q;
        case R: return ImGuiKey_R;
        case S: return ImGuiKey_S;
        case T: return ImGuiKey_T;
        case U: return ImGuiKey_U;
        case V: return ImGuiKey_V;
        case W: return ImGuiKey_W;
        case X: return ImGuiKey_X;
        case Y: return ImGuiKey_Y;
        case Z: return ImGuiKey_Z;
	case LEFT_BRACKET: return ImGuiKey_LeftBracket;
        case BACKSLASH: return ImGuiKey_Backslash;
        case RIGHT_BRACKET: return ImGuiKey_RightBracket;
        case GRAVE: return ImGuiKey_GraveAccent;
        case ESCAPE: return ImGuiKey_Escape;
        case RETURN: return ImGuiKey_Enter;
        case TAB: return ImGuiKey_Tab;
        case BACKSPACE: return ImGuiKey_Backspace;
        case INSERT: return ImGuiKey_Insert;
        case DELETE: return ImGuiKey_Delete;
        case RIGHT_ARROW: return ImGuiKey_RightArrow;
        case LEFT_ARROW: return ImGuiKey_LeftArrow;
        case DOWN_ARROW: return ImGuiKey_DownArrow;
        case UP_ARROW: return ImGuiKey_UpArrow;
        case PG_UP: return ImGuiKey_PageUp;
        case PG_DOWN: return ImGuiKey_PageDown;
        case HOME: return ImGuiKey_Home;
        case END: return ImGuiKey_End;
        case CAPS_LOCK: return ImGuiKey_CapsLock;
        case SCROLL_LOCK: return ImGuiKey_ScrollLock;
        case KP_NUMLOCK: return ImGuiKey_NumLock;
        case PRINT: return ImGuiKey_PrintScreen;
        case PAUSE: return ImGuiKey_Pause;
        case F1: return ImGuiKey_F1;
        case F2: return ImGuiKey_F2;
        case F3: return ImGuiKey_F3;
        case F4: return ImGuiKey_F4;
        case F5: return ImGuiKey_F5;
        case F6: return ImGuiKey_F6;
        case F7: return ImGuiKey_F7;
        case F8: return ImGuiKey_F8;
        case F9: return ImGuiKey_F9;
        case F10: return ImGuiKey_F10;
        case F11: return ImGuiKey_F11;
        case F12: return ImGuiKey_F12;
        case KP_0: return ImGuiKey_Keypad0;
        case KP_1: return ImGuiKey_Keypad1;
        case KP_2: return ImGuiKey_Keypad2;
        case KP_3: return ImGuiKey_Keypad3;
        case KP_4: return ImGuiKey_Keypad4;
        case KP_5: return ImGuiKey_Keypad5;
        case KP_6: return ImGuiKey_Keypad6;
        case KP_7: return ImGuiKey_Keypad7;
        case KP_8: return ImGuiKey_Keypad8;
        case KP_9: return ImGuiKey_Keypad9;
        case KP_DELETE: return ImGuiKey_KeypadDecimal;
        case KP_DIVIDE: return ImGuiKey_KeypadDivide;
        case KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case KP_MINUS: return ImGuiKey_KeypadSubtract;
        case KP_PLUS: return ImGuiKey_KeypadAdd;
        case KP_RETURN: return ImGuiKey_KeypadEnter;
	case L_SHIFT: return ImGuiKey_LeftShift;
        case L_CONTROL: return ImGuiKey_LeftCtrl;
        case L_ALT: return ImGuiKey_LeftAlt;
        case L_SUPER: return ImGuiKey_LeftSuper;
        case R_SHIFT: return ImGuiKey_RightShift;
        case R_CONTROL: return ImGuiKey_RightCtrl;
        case R_ALT: return ImGuiKey_RightAlt;
        case R_SUPER: return ImGuiKey_RightSuper;
        case MENU: return ImGuiKey_Menu;
        default: return ImGuiKey_None;
        }
}

}

export namespace lumina::ui
{

class imgui_backend
{
public:
	imgui_backend(platform::Window* wnd, vulkan::Device* dev) : window{wnd}, device{dev}
	{
		imgui_context = ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();

		io.BackendPlatformName = "lumina::platform";
		io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
		io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigWindowsResizeFromEdges = true;

		io.Fonts->AddFontDefault();
		
		int width = 0;
		int height = 0;
		uint8_t* pixels = nullptr;

		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		font_texture = device->create_image
		({
			.width = static_cast<uint32_t>(width),
			.height = static_cast<uint32_t>(height),
			.format = vk::Format::eR8G8B8A8Unorm,
			.usage = vulkan::ImageUsage::ShaderRead,
			.debug_name = "imgui::font_atlas",
			.initial_data = pixels
		});

		io.Fonts->TexID = static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(font_texture.get()));

		for(auto& fd : perframe_data)
		{
			fd.vertex = device->create_buffer
			({
				.domain = vulkan::BufferDomain::Host,
				.usage = vulkan::BufferUsage::VertexBuffer,
				.size = sizeof(Vertex) * max_vertices,
				.debug_name = "imgui::vertex_buffer"
			});

			fd.index = device->create_buffer
			({
				.domain = vulkan::BufferDomain::Host,
				.usage = vulkan::BufferUsage::IndexBuffer,
				.size = sizeof(uint16_t) * max_indices,
				.debug_name = "imgui::index_buffer"
			});
		}
		
		window->register_key_event_listener
		(platform::InputLayers::UI, [](platform::Key key, platform::KeyState state)
		{
			ImGuiKey imgui_key = platform_key_to_imgui_key(key);
			ImGui::GetIO().AddKeyEvent(imgui_key, (state == platform::KeyState::Down));
		});

		window->register_char_event_listener
		(platform::InputLayers::UI, [](unsigned int c)
		{
			ImGui::GetIO().AddInputCharacter(c);
		});

		window->register_mouse_button_event_listener
		(platform::InputLayers::UI, [](platform::MouseButton button, platform::ButtonState state)
		{
			int btn = static_cast<int>(button);
			if(btn < ImGuiMouseButton_COUNT)
				ImGui::GetIO().AddMouseButtonEvent(btn, state == platform::ButtonState::Down);
		});

		window->register_mouse_move_event_listener
		(platform::InputLayers::UI, [](double x, double y, double, double)
		{
			ImGui::GetIO().AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
		});

		window->register_scroll_event_listener
		(platform::InputLayers::UI, [](double dx, double dy)
		{
			ImGui::GetIO().AddMouseWheelEvent(static_cast<float>(dx), static_cast<float>(dy));
		});
	}

	~imgui_backend()
	{
		ImGui::DestroyContext(imgui_context);
	}

	imgui_backend(const imgui_backend&) = delete;
	imgui_backend(imgui_backend&&) = delete;

	imgui_backend& operator=(const imgui_backend&) = delete;
	imgui_backend& operator=(imgui_backend&&) = delete;

	void add_ui_hook(std::function<void()>&& hook)
	{
		ui_hooks.push_back(std::move(hook));
	}

	void render(vulkan::CommandBuffer& cb, double dt)
	{
		ImGuiIO& io = ImGui::GetIO();

		io.DeltaTime = dt > 0.0 ? static_cast<float>(dt) : (1.0f / 60.0f);

		auto [w, h] = window->get_extent();	
		auto [fw, fh] = window->get_extent();

		io.DisplaySize.x = static_cast<float>(w);
		io.DisplaySize.y = static_cast<float>(h);
		io.DisplayFramebufferScale.x = io.DisplaySize.x / static_cast<float>(fw);
		io.DisplayFramebufferScale.y = io.DisplaySize.y / static_cast<float>(fh);

		ImGui::NewFrame();

		for(auto& hook : ui_hooks)
			hook();

		ImGui::Render();
		auto draw_data = ImGui::GetDrawData();

		ImDrawVert* vmem = static_cast<ImDrawVert*>(perframe_data[cb.ctx_index].vertex->mapped);
		ImDrawIdx* imem = static_cast<ImDrawIdx*>(perframe_data[cb.ctx_index].index->mapped);
		for(int i = 0; i < draw_data->CmdListsCount; i++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[i];

			memcpy(vmem, cmd_list->VtxBuffer.Data, static_cast<size_t>(cmd_list->VtxBuffer.Size) * sizeof(ImDrawVert));
			memcpy(imem, cmd_list->IdxBuffer.Data, static_cast<size_t>(cmd_list->IdxBuffer.Size) * sizeof(ImDrawIdx));

			vmem += cmd_list->VtxBuffer.Size;
			imem += cmd_list->IdxBuffer.Size;
		}

		device->start_perf_event("ImGUI pass", cb);
		{
			cb.bind_pipeline
			({
				.vert_desc = {{vk::Format::eR32G32Sfloat, vk::Format::eR32G32Sfloat, vk::Format::eR8G8B8A8Unorm}},
				.depth_mode = vulkan::DepthMode::Disabled,
				.blend_modes = {vulkan::BlendMode::AlphaBlend},
				.att_formats = {{vk::Format::eB8G8R8A8Srgb}},
				.shaders = {"imgui.vert", "imgui.frag"}
			});

			mat4 proj = mat4::make_ortho(0.0f, static_cast<float>(fw), static_cast<float>(fh), 0.0f, 0.0f, 1.0f);
			ImVec2 clip_off = draw_data->DisplayPos;
			ImVec2 clip_scale = draw_data->FramebufferScale;

			uint32_t idx_offset = 0;
			int32_t vtx_offset = 0;

			for(int idx = 0; idx < draw_data->CmdListsCount; idx++)
			{
				const ImDrawList* cmd_list = draw_data->CmdLists[idx];
				for(int i = 0; i < cmd_list->CmdBuffer.Size; i++)
				{
					const ImDrawCmd* draw_cmd = &cmd_list->CmdBuffer[i];
					if(draw_cmd->UserCallback)
						draw_cmd->UserCallback(cmd_list, draw_cmd);
					else
					{
						ImVec2 clip_min((draw_cmd->ClipRect.x - clip_off.x) * clip_scale.x, (draw_cmd->ClipRect.y - clip_off.y) * clip_scale.y);
						ImVec2 clip_max((draw_cmd->ClipRect.z - clip_off.x) * clip_scale.x, (draw_cmd->ClipRect.w - clip_off.y) * clip_scale.y);

						if(clip_min.x < 0.0f) { clip_min.x = 0.0f; }
						if(clip_min.y < 0.0f) { clip_min.y = 0.0f; }
						if(clip_max.x > static_cast<float>(fw)) { clip_max.x = static_cast<float>(fw); }
						if(clip_max.y > static_cast<float>(fh)) { clip_max.y = static_cast<float>(fh); }
						if(clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
							continue;

						vk::Rect2D scissor;
						scissor.offset.x = static_cast<int32_t>(clip_min.x);
						scissor.offset.y = static_cast<int32_t>(clip_min.y);
						scissor.extent.width = static_cast<uint32_t>(clip_max.x - clip_min.x);
						scissor.extent.height = static_cast<uint32_t>(clip_max.y - clip_min.y);

						cb.set_scissor(0, scissor);
						cb.push_constant(&proj, sizeof(mat4));
						
						vulkan::Image* tex = reinterpret_cast<vulkan::Image*>(draw_cmd->TextureId);
						
						cb.push_descriptor_set
						({	
							.sampled_images = 
							{
								{
								0,
								tex->get_default_view(), 
								device->get_prefab_sampler(vulkan::SamplerPrefab::Texture)
								}
							}
						
						});

						cb.bind_vertex_buffers
						({
							perframe_data[cb.ctx_index].vertex.get()
						});

						cb.bind_index_buffer(perframe_data[cb.ctx_index].index.get(), vk::IndexType::eUint16);
						cb.draw_indexed(draw_cmd->ElemCount, 1, idx_offset + static_cast<uint32_t>(draw_cmd->IdxOffset), vtx_offset + static_cast<int32_t>(draw_cmd->VtxOffset), 0);
					}
				}
				idx_offset += static_cast<uint32_t>(cmd_list->IdxBuffer.Size);
				vtx_offset += static_cast<int32_t>(cmd_list->VtxBuffer.Size);
			}
		}
		device->end_perf_event(cb);
		
		ImGui::EndFrame();
	}	
private:
	platform::Window* window;
	vulkan::Device* device;
	ImGuiContext* imgui_context;

	std::vector<std::function<void()>> ui_hooks;

	struct Vertex
	{
		vec2 pos;
		vec2 uv;
		uint32_t color;
	};

	constexpr static uint32_t max_vertices = 65536;
	constexpr static uint32_t max_indices = 65536;

	struct PerFrame
	{
		vulkan::BufferHandle vertex;
		vulkan::BufferHandle index;
	};
	std::array<PerFrame, 2> perframe_data;

	vulkan::ImageHandle font_texture;
};

}
