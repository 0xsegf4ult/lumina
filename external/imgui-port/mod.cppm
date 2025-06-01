module;

#include <imgui_internal.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h> 
#include <ImGuizmo.h>

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

	using ImGui::IsItemEdited;	
	using ImGui::IsItemHovered;
	using ImGui::IsItemDeactivatedAfterEdit;
	using ImGui::IsMouseDown;
	using ImGui::IsMouseReleased;
	using ImGui::GetWindowWidth;
	using ImGui::GetWindowHeight;
	using ImGui::SetNextWindowSize;
	using ImGui::IsWindowFocused;
	using ImGui::SetNextWindowPos;
	using ImGui::GetMainViewport;
	using ImGui::SetNextWindowViewport;
	using ImGui::GetWindowPos;
	using ImGui::GetCursorPosX;
	using ImGui::GetCursorPosY;

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
	using ImGui::BeginPopupContextItem;
	using ImGui::CloseCurrentPopup;
	using ImGui::EndPopup;
	using ImGui::TreeNode;
	using ImGui::TreeNodeEx;
	using ImGui::TreePop;
	using ImGui::CollapsingHeader;

	using ImGui::GetForegroundDrawList;
	using ImGui::GetBackgroundDrawList;
	using ImGui::GetTextLineHeight;
	using ImGui::GetID;
	using ImGui::SameLine;
	using ImGui::PushStyleVar;
	using ImGui::PopStyleVar;
	using ImGui::DockBuilderDockWindow;
	using ImGui::DockBuilderFinish;
	using ImGui::PushID;
	using ImGui::PopID;
	using ImGui::BeginDisabled;
	using ImGui::EndDisabled;
	
	using ImGui::CalcTextSize;

	using ImGui::Button;
	using ImGui::InputInt;
	using ImGui::DragFloat;
	using ImGui::DragFloat2;
	using ImGui::DragFloat3;
	using ImGui::DragFloat4;
	using ImGui::DragScalar;
	using ImGui::SliderScalar;
	using ImGui::SliderFloat;
	using ImGui::SliderFloat2;
	using ImGui::SliderFloat3;
	using ImGui::SliderFloat4;
	using ImGui::SliderInt;
	using ImGui::ColorEdit3;
	using ImGui::ColorEdit4;
	using ImGui::Selectable;
	using ImGui::InputText;
	using ImGui::Image;
	using ImGui::Checkbox;
	using ImGui::Combo;
	using ImGui::Separator;
	using ImGui::SeparatorText;
	using ImGui::Text;
	using ImGui::TextColored;
}

export namespace ImGuizmo
{
	using ImGuizmo::BeginFrame;
	using ImGuizmo::SetOrthographic;
	using ImGuizmo::SetDrawlist;
	using ImGuizmo::SetRect;
	using ImGuizmo::IsUsing;
	using ImGuizmo::Manipulate;
	using ImGuizmo::OPERATION;
	using ImGuizmo::LOCAL;
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
export using ::ImTextureID;
export using ::ImGuiViewport;
export using ::ImGuiWindowFlags;
export using ::ImGuiWindowFlags_NoCollapse;
export using ::ImGuiWindowFlags_NoScrollbar;
export using ::ImGuiWindowFlags_NoScrollWithMouse;
export using ::ImGuiWindowFlags_MenuBar;
export using ::ImGuiWindowFlags_NoDocking;
export using ::ImGuiWindowFlags_NoDecoration;
export using ::ImGuiWindowFlags_NoBackground;
export using ::ImGuiWindowFlags_NoTitleBar;
export using ::ImGuiWindowFlags_NoResize;
export using ::ImGuiWindowFlags_NoMove;
export using ::ImGuiWindowFlags_NoFocusOnAppearing;
export using ::ImGuiWindowFlags_NoBringToFrontOnFocus;
export using ::ImGuiWindowFlags_NoNav;
export using ::ImGuiWindowFlags_NoNavFocus;
export using ::ImGuiWindowFlags_NoInputs;
export using ::ImGuiWindowFlags_AlwaysAutoResize;
export using ::ImGuiWindowFlags_NoSavedSettings;
export using ::ImGuiDockNodeFlags;
export using ::ImGuiDockNodeFlags_None;
export using ::ImGuiStyleVar;
export using ::ImGuiStyleVar_WindowRounding;
export using ::ImGuiStyleVar_WindowBorderSize;
export using ::ImGuiStyleVar_WindowPadding;
export using ::ImGuiStyleVar_ItemSpacing;
export using ::ImGuiMouseButton_Left;
export using ::ImGuiMouseButton_Right;
export using ::ImGuiMouseButton_Middle;
export using ::ImGuiTreeNodeFlags;
export using ::ImGuiTreeNodeFlags_DefaultOpen;
export using ::ImGuiTreeNodeFlags_OpenOnArrow;
export using ::ImGuiTreeNodeFlags_Leaf;
export using ::ImGuiTreeNodeFlags_Selected;
export using ::ImGuiColorEditFlags;
export using ::ImGuiColorEditFlags_Float;
export using ::ImGuiColorEditFlags_HDR;
export using ::ImGuiSliderFlags;
export using ::ImGuiSliderFlags_AlwaysClamp;
export using ::ImGuiSliderFlags_NoInput;
export using ::ImGuiSliderFlags_Logarithmic;
export using ::ImGuiInputTextFlags;
export using ::ImGuiInputTextFlags_None;
export using ::ImGuiInputTextFlags_EnterReturnsTrue;
export using ::ImGuiInputTextFlags_CallbackCompletion;
export using ::ImGuiInputTextFlags_CallbackHistory;
export using ::ImGuiInputTextFlags_CallbackAlways;
export using ::ImGuiInputTextFlags_CallbackCharFilter;
export using ::ImGuiInputTextFlags_ReadOnly;
export using ::ImGuiInputTextFlags_CallbackResize;
export using ::ImGuiDataType;
export using ::ImGuiDataType_S8;
export using ::ImGuiDataType_U8;
export using ::ImGuiDataType_S16;
export using ::ImGuiDataType_U16;
export using ::ImGuiDataType_S32;
export using ::ImGuiDataType_U32;
export using ::ImGuiDataType_S64;
export using ::ImGuiDataType_U64;
export using ::ImGuiDataType_Float;
export using ::ImGuiDataType_Double;
export using ::ImGuiBackendFlags_HasMouseCursors;
export using ::ImGuiBackendFlags_HasSetMousePos;
export using ::ImGuiConfigFlags_NavEnableKeyboard;
export using ::ImGuiConfigFlags_IsSRGB;
export using ::ImGuiConfigFlags_DockingEnable;
export using ::ImGuiMouseButton_COUNT;
export using ::ImGuiCond_FirstUseEver;
