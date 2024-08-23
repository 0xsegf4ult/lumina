module;

#include <imgui.h>

export module imgui;

export namespace ImGui
{
	using ImGui::GetIO;
	using ImGui::CreateContext;
	using ImGui::NewFrame;
	using ImGui::Render;
	using ImGui::GetDrawData;
	using ImGui::EndFrame;
	using ImGui::DestroyContext;
	
	using ImGui::Begin;
	using ImGui::End;

	using ImGui::Button;
	using ImGui::DragFloat;
	using ImGui::DragFloat2;
	using ImGui::DragFloat3;
	using ImGui::DragFloat4;
	using ImGui::SliderFloat;
	using ImGui::SliderFloat2;
	using ImGui::SliderFloat3;
	using ImGui::SliderFloat4;
	using ImGui::SliderInt;

	using ImGui::GetForegroundDrawList;

	using ImGui::GetTextLineHeight;
}

export using ::ImGuiKey;
export using ::ImGuiIO;
export using ::ImGuiContext;
export using ::ImDrawVert;
export using ::ImDrawIdx;
export using ::ImVec2;
export using ::ImColor;
export using ::ImDrawList;
export using ::ImDrawCmd;
