#include "input_system.h"
#ifdef NEW_INPUT_SYSTEM
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "utils/android.h"
#include <cstring>

namespace {
thread_local static JNIEnv* g_env {};

void* g_owning_context {};
bool g_shown_keyboard {};
ImGuiID g_shown_keyboard_id {};
ImGuiInputTextFlags g_shown_keyboard_flags {};

int g_last_sel_start {};
int g_last_sel_end {};
std::string g_last_text {};

bool g_input_ready {};

const char* remove_null(const char* s)
{
  return s == nullptr ? "" : s;
}
}

void input_system::end_frame()
{
  if (!g_env) {
    andutils::java_getter env {};
    env.retain();
    g_env = env.env();
  }

  if (g_owning_context != GImGui) {
    if (GImGui && GImGui->IO.WantTextInput) {
      if (g_shown_keyboard) {
        andutils::jni::input_end(g_env);
      }
      g_shown_keyboard = false;
    } else {
      return;
    }
  }

  g_owning_context = GImGui;
  ImGuiContext& g = *GImGui;
  ImGuiInputTextState& input = g.InputTextState;

  if (g_shown_keyboard && (input.ID != g_shown_keyboard_id || g.ActiveId != g_shown_keyboard_id || input.IMEExFlags != g_shown_keyboard_flags)) {
    andutils::jni::input_end(g_env);
    g_shown_keyboard = false;
  }

  if (!g_shown_keyboard && input.ID == g.ActiveId && !(input.IMEExFlags & ImGuiInputTextFlags_ReadOnly)) {
    // The java side writes the text into an edit text of its own, which is
    // made on the ui thread. Without it the keyboard has nothing to work
    // with, so the input waits for it.
    if (!g_input_ready) {
      andutils::jni::input_init(g_env);
      g_input_ready = andutils::jni::input_ready(g_env);
      if (!g_input_ready) {
        return;
      }
    }

    const char* text = input.TextAIsValid ? remove_null(input.TextA.Data) : remove_null(input.InitialTextA.Data);

    andutils::jni::input_start(g_env,
        text,
        input.IMEExFlags & ImGuiInputTextFlags_Multiline,
        input.Stb.cursor);

    g_shown_keyboard = true;
    g_shown_keyboard_id = g.ActiveId;
    g_shown_keyboard_flags = input.IMEExFlags;
    g_last_sel_start = g_last_sel_end = input.Stb.cursor;
    g_last_text = text;
  }

  if (!g_shown_keyboard) {
    return;
  }

  andutils::jni::InputStatus status = andutils::jni::input_status(g_env);
  if (status == andutils::jni::INPUT_DONE) {
    input.IMEExEndInputWithEnter = true;
    g_shown_keyboard = false;
  } else if (status == andutils::jni::INPUT_EXITED) {
    input.IMEExEndInputWithUnfocus = true;
    g_shown_keyboard = false;
  }

  if (status == andutils::jni::INPUT_NEW_TEXT) {
    std::string new_text = andutils::jni::input_get_text(g_env);

    bool text_changed = new_text != g_last_text;
    bool strip_text = new_text.size() + 1 > input.BufCapacityA && !(input.IMEExFlags & ImGuiInputTextFlags_CallbackResize);

    if (text_changed) {
      int copy_size = 0;
      g_last_text = std::move(new_text);

      if (strip_text) {
        // Cut to nearest codepoint.
        // if keep_last_codepoint is true, then naively cut string is valid,
        // and we don't have to trim last codepoint, otherwise it is
        // invalid and we have to trim last codepoint

        int i_start = input.BufCapacityA - 1 - 1; // < note to me: must be signed
        int i = i_start; // < same
        bool keep_last_codepoint = false;
        for (; i >= 0; --i) {
          auto b = static_cast<unsigned char>(g_last_text[i]);
          if ((b & 0xC0) != 0x80) {
            // Check if we need to trim this codepoint

            int additional_bytes = 0;
            if ((b & 0xE0) == 0xC0) {
              additional_bytes = 1;
            } else if ((b & 0xF0) == 0xE0) {
              additional_bytes = 2;
            } else if ((b & 0xF8) == 0xF0) {
              additional_bytes = 3;
            } else if ((b & 0xFC) == 0xF8) {
              additional_bytes = 4;
            } else if ((b & 0xFE) == 0xFC) {
              additional_bytes = 5;
            }

            int skipped = i_start - i;
            keep_last_codepoint = additional_bytes <= skipped;
            break;
          }
        }

        copy_size = keep_last_codepoint ? i_start + 1 : i;
      } else {
        copy_size = static_cast<int>(g_last_text.size());
        input.TextW.resize(copy_size + 1);
        input.TextAIsValid = false; // Trigger resize below
      }

      if (!input.TextAIsValid) {
        input.TextAIsValid = true;
        input.TextA.resize(std::max(input.TextW.Size * 4 + 1, copy_size + 1));
      }
      std::memcpy(input.TextA.Data, g_last_text.data(), copy_size);
      input.TextA.Data[copy_size] = '\0';

      input.IMEExNewInput = true;

      // DO NOT CHANGE LENGTHS HERE, they will be handled by IMEExNewInput code.
      // This is needed to handle resizing correctly.
    }

    std::pair<int, int> new_selection = andutils::jni::input_get_selection(g_env);
    input.Stb.select_start = new_selection.first;
    input.Stb.select_end = new_selection.second;
    input.Stb.cursor = input.Stb.select_end;
    input.Stb.has_preferred_x = 0;
    input.CursorFollow = true;
    input.CursorAnimReset();
    if (!text_changed) {
      input.CursorClamp();
    }
    // else clamp is performed in IMEExNewInput code

    // Store new selection in sorted order.
    g_last_sel_start = new_selection.first;
    g_last_sel_end = new_selection.second;
  } else if (status == andutils::jni::INPUT_ACTIVE) {
    bool force_resync = false;

    const char* text = input.TextAIsValid ? remove_null(input.TextA.Data) : remove_null(input.InitialTextA.Data);
    if (g_last_text != text) {
      andutils::jni::input_set_text(g_env, text);
      g_last_text = text;
      force_resync = true;
    }

    int im_cursor = input.Stb.cursor;
    int im_selection_start = input.HasSelection() ? input.Stb.select_start : im_cursor;
    int im_selection_end = input.HasSelection() ? input.Stb.select_end : im_cursor;

    if (force_resync || im_selection_start != g_last_sel_start || im_selection_end != g_last_sel_end) {
      andutils::jni::input_set_selection(g_env, im_selection_start, im_selection_end);

      g_last_sel_start = im_selection_start;
      g_last_sel_end = im_selection_end;
    }
  }
}

void input_system::destroyed_context()
{
  if (!g_env) {
    return;
  }

  if (g_shown_keyboard) {
    andutils::jni::input_end(g_env);
  }
  g_shown_keyboard = false;
}
#else
void input_system::end_frame()
{
}

void input_system::destroyed_context()
{
}
#endif