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
	const float fps = ImGui::GetIO().Framerate;
	static bool p_open = true;
	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(root.x), static_cast<float>(root.y)), ImGuiCond_Always);
	const ImGuiWindowFlags wflags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1.0f, 1.0f));
	ImGui::Begin("device_overlay", &p_open, wflags);

	auto [usage, budget] = device.get_memory_budget();
	auto [uv, uu] = log::pretty_format_size(usage);
	auto [bv, bu] = log::pretty_format_size(budget);

	ImGui::Text("lumina::engine git-%s", config::git_hash);
	ImGui::Text("%s", device.get_name().data());
	ImGui::Text("vmem: %.2f%s / %.2f%s", uv, uu.data(), bv, bu.data());

	ImColor fps_color = ImColor(20, 220, 20, 255);
	if(fps < 45.0f)
		fps_color = ImColor(220, 20, 20, 255);
	else if(fps < 59.0f)
		fps_color = ImColor(180, 220, 20, 255);

	ImGui::TextColored(fps_color, "%.0f FPS (%.2f mspf)", fps, 1000.0f / fps);

	auto evt_data = device.get_perf_events();

	float wt = 0.0f;
	for(auto& evt : evt_data)
	{
		ImGui::Text("%s: %.2fms", evt.name.data(), evt.time);
		wt += evt.time;
	}

	if(wt != 0.0f)
		ImGui::Text("workload time: %.2fms", wt);

	ImGui::End();
	ImGui::PopStyleVar(2);
}

}
