export module lumina.ui:device_overlay;

import imgui;
import lumina.platform;
import lumina.vulkan;
import lumina.core.log;

import std;

export namespace lumina::ui
{

void draw_device_overlay(platform::Window& window, vulkan::Device& device)
{
	ImGui::Begin("test");
	ImGui::End();

	auto [w, h] = window.get_extent();
	float fps = ImGui::GetIO().Framerate;
	ImDrawList* draw = ImGui::GetForegroundDrawList();

	char buf[128];

	auto [usage, budget] = device.get_memory_budget();
	auto [uv, uu] = log::pretty_format_size(usage);
	auto [bv, bu] = log::pretty_format_size(budget);

	std::format_to(buf, "{}{}", device.get_name().data(), '\0');
	draw->AddText(ImVec2(0, 0), ImColor(255, 255, 255, 255), buf);

	std::format_to(buf, "vmem: {:.2f}{} / {:.2f}{}{}", uv, uu, bv, bu, '\0');
	draw->AddText(ImVec2(0, ImGui::GetTextLineHeight()), ImColor(255, 255, 255, 255), buf);

	ImColor fps_color = ImColor(20, 220, 20, 255);
	if(fps < 59.0f)
		fps_color = ImColor(180, 220, 20, 255);
	else if(fps < 45.0f)
		fps_color = ImColor(220, 20, 20, 255);

	std::format_to(buf, "{:.0f} FPS ({:.2f} mspf){}", fps, 1000.0f / fps, '\0');
	draw->AddText(ImVec2(0, ImGui::GetTextLineHeight() * 2), fps_color, buf);
}

}
