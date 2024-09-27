module;

#include <imgui_internal.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h> 

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
	
	using ImGui::IsItemHovered;
	using ImGui::IsMouseDown;
	using ImGui::GetWindowWidth;
	using ImGui::GetWindowHeight;
	using ImGui::SetNextWindowSize;
	using ImGui::IsWindowFocused;
	using ImGui::SetNextWindowPos;
	using ImGui::GetMainViewport;
	using ImGui::SetNextWindowViewport;

	using ImGui::Begin;
	using ImGui::End;
	using ImGui::DockSpace;
	using ImGui::BeginMenuBar;
	using ImGui::BeginMenu;
	using ImGui::MenuItem;
	using ImGui::EndMenu;
	using ImGui::EndMenuBar;
	using ImGui::OpenPopup;
	using ImGui::BeginPopupModal;
	using ImGui::CloseCurrentPopup;
	using ImGui::EndPopup;
	using ImGui::TreeNode;
	using ImGui::TreeNodeEx;
	using ImGui::TreePop;

	using ImGui::GetForegroundDrawList;
	using ImGui::GetTextLineHeight;
	using ImGui::GetID;
	using ImGui::SameLine;
	using ImGui::PushStyleVar;
	using ImGui::PopStyleVar;
	using ImGui::DockBuilderDockWindow;
	using ImGui::DockBuilderFinish;
	using ImGui::PushID;
	using ImGui::PopID;

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
	using ImGui::InputText;
	using ImGui::Image;
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
export using ::ImGuiID;
export using ::ImGuiViewport;
export using ::ImGuiWindowFlags;
export using ::ImGuiWindowFlags_NoCollapse;
export using ::ImGuiWindowFlags_NoScrollbar;
export using ::ImGuiWindowFlags_NoScrollWithMouse;
export using ::ImGuiWindowFlags_MenuBar;
export using ::ImGuiWindowFlags_NoDocking;
export using ::ImGuiWindowFlags_NoTitleBar;
export using ::ImGuiWindowFlags_NoResize;
export using ::ImGuiWindowFlags_NoMove;
export using ::ImGuiWindowFlags_NoBringToFrontOnFocus;
export using ::ImGuiWindowFlags_NoNavFocus;
export using ::ImGuiWindowFlags_AlwaysAutoResize;
export using ::ImGuiDockNodeFlags;
export using ::ImGuiDockNodeFlags_None;
export using ::ImGuiStyleVar;
export using ::ImGuiStyleVar_WindowRounding;
export using ::ImGuiStyleVar_WindowBorderSize;
export using ::ImGuiStyleVar_WindowPadding;
export using ::ImGuiMouseButton_Left;
export using ::ImGuiMouseButton_Right;
export using ::ImGuiTreeNodeFlags;
export using ::ImGuiTreeNodeFlags_OpenOnArrow;
export using ::ImGuiTreeNodeFlags_Leaf;
export using ::ImGuiTreeNodeFlags_Selected;
