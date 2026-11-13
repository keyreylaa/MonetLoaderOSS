#pragma once
#include <cassert>
#include <jni.h>
#include <string>
#include <utility>

namespace andutils {
void init();
std::string& get_package_name();
std::string& get_library_name();

class java_getter {
  public:
  java_getter()
  {
    if (vm()->GetEnv(reinterpret_cast<void**>(&_env), JNI_VERSION_1_6) != JNI_OK) {
      jint retcode = vm()->AttachCurrentThread(&_env, nullptr);
      (void)retcode;
      assert(retcode == JNI_OK);
      _attached = true;
    }
  }

  ~java_getter()
  {
    if (_attached) {
      vm()->DetachCurrentThread();
    }
  }

  JNIEnv* env()
  {
    return _env;
  }

  void retain()
  {
    _attached = false;
  }

  static JavaVM*& vm()
  {
    static JavaVM* jvm;
    return jvm;
  }

  private:
  JNIEnv* _env { nullptr };
  bool _attached { false };
};

namespace jni {
  enum InputStatus {
    INPUT_DEACTIVATED = -3, // Input was either never activated, or was manually ended.
    INPUT_DONE = -2, // Input was closed using "Done" by user.
    INPUT_EXITED = -1, // Input was exited by either bringing keyboard down, or unfocusing input.
    INPUT_ACTIVE = 1, // Input is active, and no text was changed since last get text call.
    INPUT_NEW_TEXT = 2 // Input is active, and new text is available for getting.
  };

  void init_jni(JNIEnv* env);
  
  float& get_dpi();
  std::string& get_external_storage();
  std::string& get_external_media();

  std::string get_clipboard(JNIEnv* env);
  void set_clipboard(JNIEnv* env, const char* value);

#ifdef NEW_INPUT_SYSTEM
  void input_init(JNIEnv* env);
  bool input_ready(JNIEnv* env);
  void input_start(JNIEnv* env, const char* text, bool multiline, jint cursor);
  void input_end(JNIEnv* env);
  InputStatus input_status(JNIEnv* env);
  void input_set_text(JNIEnv* env, const char* text);
  std::string input_get_text(JNIEnv* env);
  void input_set_selection(JNIEnv* env, jint start, jint end);
  std::pair<int, int> input_get_selection(JNIEnv* env);
#endif
}
}