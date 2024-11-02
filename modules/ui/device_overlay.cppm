export module lumina.ui:device_overlay;

import imgui;
import lumina.platform;
import lumina.vulkan;
import lumina.core.log;
import lumina.core.math;
import lumina.core.config;

import std;

export namespace lumina::ui
{

void draw_device_overlay(platform::Window& window, vulkan::Device& device, uvec2 root = {0u, 0u})
{
	auto [w, h] = window.get_extent();
	float fps = ImGui::GetIO().Framerate;
	ImDrawList* draw = ImGui::GetForegroundDrawList();

	char buf[128];

	auto [usage, budget] = device.get_memory_budget();
	auto [uv, uu] = log::pretty_format_size(usage);
	auto [bv, bu] = log::pretty_format_size(budget);

	std::uint32_t cur_y = root.y;

	std::format_to(buf, "lumina::engine git-{}{}", config::git_hash, '\0');
	draw->AddText(ImVec2(root.x, cur_y), ImColor(255, 255, 255, 255), buf);
	cur_y += ImGui::GetTextLineHeight();

	std::format_to(buf, "{}{}", device.get_name().data(), '\0');
	draw->AddText(ImVec2(root.x, cur_y), ImColor(255, 255, 255, 255), buf);
	cur_y += ImGui::GetTextLineHeight();

	std::format_to(buf, "vmem: {:.2f}{} / {:.2f}{}{}", uv, uu, bv, bu, '\0');
	draw->AddText(ImVec2(root.x, cur_y), ImColor(255, 255, 255, 255), buf);
	cur_y += ImGui::GetTextLineHeight();

	ImColor fps_color = ImColor(20, 220, 20, 255);
	if(fps < 45.0f)
		fps_color = ImColor(220, 20, 20, 255);
	else if(fps < 59.0f)
		fps_color = ImColor(180, 220, 20, 255);

	std::format_to(buf, "{:.0f} FPS ({:.2f} mspf){}", fps, 1000.0f / fps, '\0');
	draw->AddText(ImVec2(root.x, cur_y), fps_color, buf);
	cur_y += ImGui::GetTextLineHeight();

	auto evt_data = device.get_perf_events();
	for(auto& evt : evt_data)
	{
		std::format_to(buf, "{}: {:.2f}ms{}", evt.name, evt.time, '\0');
		draw->AddText(ImVec2(root.x, cur_y), ImColor(255, 255, 255, 255), buf);
		cur_y += ImGui::GetTextLineHeight();
	}
}

}
