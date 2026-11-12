-- The virtual key codes of the desktop moonloader.
--
-- The scripts that read the keyboard ask for a key by its code, for
-- example key.VK_RBUTTON. The codes are the ones of windows, they are the
-- same on every platform.

local key = {}

key.VK_LBUTTON = 1
key.VK_RBUTTON = 2
key.VK_CANCEL = 3
key.VK_MBUTTON = 4
key.VK_BACK = 8
key.VK_TAB = 9
key.VK_CLEAR = 12
key.VK_RETURN = 13
key.VK_SHIFT = 16
key.VK_CONTROL = 17
key.VK_MENU = 18
key.VK_PAUSE = 19
key.VK_CAPITAL = 20
key.VK_ESCAPE = 27
key.VK_SPACE = 32
key.VK_PRIOR = 33
key.VK_NEXT = 34
key.VK_END = 35
key.VK_HOME = 36
key.VK_LEFT = 37
key.VK_UP = 38
key.VK_RIGHT = 39
key.VK_DOWN = 40
key.VK_SELECT = 41
key.VK_PRINT = 42
key.VK_EXECUTE = 43
key.VK_SNAPSHOT = 44
key.VK_INSERT = 45
key.VK_DELETE = 46

key.VK_0 = 48
key.VK_1 = 49
key.VK_2 = 50
key.VK_3 = 51
key.VK_4 = 52
key.VK_5 = 53
key.VK_6 = 54
key.VK_7 = 55
key.VK_8 = 56
key.VK_9 = 57

key.VK_A = 65
key.VK_B = 66
key.VK_C = 67
key.VK_D = 68
key.VK_E = 69
key.VK_F = 70
key.VK_G = 71
key.VK_H = 72
key.VK_I = 73
key.VK_J = 74
key.VK_K = 75
key.VK_L = 76
key.VK_M = 77
key.VK_N = 78
key.VK_O = 79
key.VK_P = 80
key.VK_Q = 81
key.VK_R = 82
key.VK_S = 83
key.VK_T = 84
key.VK_U = 85
key.VK_V = 86
key.VK_W = 87
key.VK_X = 88
key.VK_Y = 89
key.VK_Z = 90

key.VK_LWIN = 91
key.VK_RWIN = 92
key.VK_APPS = 93

key.VK_NUMPAD0 = 96
key.VK_NUMPAD1 = 97
key.VK_NUMPAD2 = 98
key.VK_NUMPAD3 = 99
key.VK_NUMPAD4 = 100
key.VK_NUMPAD5 = 101
key.VK_NUMPAD6 = 102
key.VK_NUMPAD7 = 103
key.VK_NUMPAD8 = 104
key.VK_NUMPAD9 = 105
key.VK_MULTIPLY = 106
key.VK_ADD = 107
key.VK_SEPARATOR = 108
key.VK_SUBTRACT = 109
key.VK_DECIMAL = 110
key.VK_DIVIDE = 111

key.VK_F1 = 112
key.VK_F2 = 113
key.VK_F3 = 114
key.VK_F4 = 115
key.VK_F5 = 116
key.VK_F6 = 117
key.VK_F7 = 118
key.VK_F8 = 119
key.VK_F9 = 120
key.VK_F10 = 121
key.VK_F11 = 122
key.VK_F12 = 123

key.VK_NUMLOCK = 144
key.VK_SCROLL = 145

key.VK_OEM_1 = 186
key.VK_OEM_PLUS = 187
key.VK_OEM_COMMA = 188
key.VK_OEM_MINUS = 189
key.VK_OEM_PERIOD = 190
key.VK_OEM_2 = 191
key.VK_OEM_3 = 192
key.VK_OEM_4 = 219
key.VK_OEM_5 = 220
key.VK_OEM_6 = 221
key.VK_OEM_7 = 222
key.VK_OEM_8 = 223

return key
