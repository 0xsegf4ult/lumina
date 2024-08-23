module;

#include <GLFW/glfw3.h>

export module lumina.platform:input;

import std;

export namespace lumina::platform
{

enum class KeyState : std::uint8_t
{
	Up = 0,
	Down = 1
};

enum class Key
{
        UNKNOWN,
        NUM0,
        NUM1, NUM2, NUM3,
        NUM4, NUM5, NUM6,
        NUM7, NUM8, NUM9,
        A, B, C, D, E, F, G,
        H, I, J, K, L, M, N,
        O, P, Q, R, S, T, U,
        V, W, X, Y, Z,
        GRAVE, MINUS, EQUAL,
        LEFT_BRACKET, RIGHT_BRACKET,
        SEMICOLON, APOSTROPHE, BACKSLASH,
        COMMA, PERIOD, SLASH,
        F1, F2, F3, F4,
        F5, F6, F7, F8,
        F9, F10, F11, F12,
        ESCAPE, TAB, CAPS_LOCK,
        L_SHIFT,L_CONTROL, L_ALT, L_SUPER, SPACE,
        R_ALT, R_SUPER, MENU, R_CONTROL, R_SHIFT,
        RETURN, BACKSPACE,
        PRINT, SCROLL_LOCK, PAUSE,
        INSERT, HOME, PG_UP,
        DELETE, END, PG_DOWN,
        UP_ARROW, LEFT_ARROW,
        DOWN_ARROW, RIGHT_ARROW,
        KP_NUMLOCK, KP_DIVIDE, KP_MULTIPLY,
        KP_MINUS, KP_PLUS, KP_RETURN,
        KP_DELETE, KP_0,
        KP_1, KP_2, KP_3,
        KP_4, KP_5, KP_6,
        KP_7, KP_8, KP_9
};

enum class ButtonState : std::uint8_t
{
	Up = 0,
	Down = 1
};

enum class MouseButton : std::uint8_t
{
	Left,
	Right,
	Middle
};

const std::array<Key, 349> keymap = []()
{
	std::array<Key, 349> ret;
	ret.fill(Key::UNKNOWN);

	ret[32] = Key::SPACE; ret[39] = Key::APOSTROPHE; ret[44] = Key::COMMA;
        ret[45] = Key::MINUS; ret[46] = Key::PERIOD; ret[47] = Key::SLASH;
        ret[48] = Key::NUM0; ret[49] = Key::NUM1; ret[50] = Key::NUM2;
        ret[51] = Key::NUM3; ret[52] = Key::NUM4; ret[53] = Key::NUM5;
        ret[54] = Key::NUM6; ret[55] = Key::NUM7; ret[56] = Key::NUM8;
        ret[57] = Key::NUM9; ret[59] = Key::SEMICOLON; ret[61] = Key::EQUAL;
        ret[65] = Key::A; ret[66] = Key::B; ret[67] = Key::C; ret[68] = Key::D;
        ret[69] = Key::E; ret[70] = Key::F; ret[71] = Key::G; ret[72] = Key::H;
        ret[73] = Key::I; ret[74] = Key::J; ret[75] = Key::K; ret[76] = Key::L;
        ret[77] = Key::M; ret[78] = Key::N; ret[79] = Key::O; ret[80] = Key::P;
        ret[81] = Key::Q; ret[82] = Key::R; ret[83] = Key::S; ret[84] = Key::T;
        ret[85] = Key::U; ret[86] = Key::V; ret[87] = Key::W; ret[88] = Key::X;
        ret[89] = Key::Y; ret[90] = Key::Z; ret[91] = Key::LEFT_BRACKET;
        ret[92] = Key::BACKSLASH; ret[93] = Key::RIGHT_BRACKET; ret[96] = Key::GRAVE;
        ret[256] = Key::ESCAPE; ret[257] = Key::RETURN; ret[258] = Key::TAB;
        ret[259] = Key::BACKSPACE; ret[260] = Key::INSERT; ret[261] = Key::DELETE;
        ret[262] = Key::RIGHT_ARROW; ret[263] = Key::LEFT_ARROW; ret[264] = Key::DOWN_ARROW;
        ret[265] = Key::UP_ARROW; ret[266] = Key::PG_UP; ret[267] = Key::PG_DOWN;
        ret[268] = Key::HOME; ret[269] = Key::END; ret[280] = Key::CAPS_LOCK;
        ret[281] = Key::SCROLL_LOCK; ret[282] = Key::KP_NUMLOCK; ret[283] = Key::PRINT;
        ret[284] = Key::PAUSE; ret[290] = Key::F1; ret[291] = Key::F2; ret[292] = Key::F3;
        ret[293] = Key::F4; ret[294] = Key::F5; ret[295] = Key::F6; ret[296] = Key::F7;
        ret[297] = Key::F8; ret[298] = Key::F9; ret[299] = Key::F10; ret[300] = Key::F11;
        ret[301] = Key::F12; ret[320] = Key::KP_0; ret[321] = Key::KP_1; ret[322] = Key::KP_2;
        ret[323] = Key::KP_3; ret[324] = Key::KP_4; ret[325] = Key::KP_5; ret[326] = Key::KP_6;
        ret[327] = Key::KP_7; ret[328] = Key::KP_8; ret[329] = Key::KP_9; ret[330] = Key::KP_DELETE;
        ret[331] = Key::KP_DIVIDE; ret[332] = Key::KP_MULTIPLY; ret[333] = Key::KP_MINUS;
        ret[334] = Key::KP_PLUS; ret[335] = Key::KP_RETURN; ret[340] = Key::L_SHIFT;
        ret[341] = Key::L_CONTROL; ret[342] = Key::L_ALT; ret[343] = Key::L_SUPER;
        ret[344] = Key::R_SHIFT; ret[345] = Key::R_CONTROL; ret[346] = Key::R_ALT;
        ret[347] = Key::R_SUPER; ret[348] = Key::MENU;

	return ret;
}();

}
