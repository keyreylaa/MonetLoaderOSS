#include "android.h"
#include "random.h"
#include "str.h"
#include <cstdio>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <jni.h>

#ifdef NEW_INPUT_SYSTEM
#include "InputSystem.dex.inl"
#endif

namespace {
// A pending Java exception stays in the JNIEnv of the calling thread and
// aborts the process at the next JNI call, which is usually somewhere
// completely unrelated to the code that caused it. The clipboard is guarded
// by the system and can refuse a read or a write, so drop the exception
// instead of carrying it into the game loop.
void clear_pending_exception(JNIEnv* env)
{
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
}

jobject g_context;
jobject g_classloader;

jobject g_clipboard_manager;
jclass g_clip_data_class;

#ifdef NEW_INPUT_SYSTEM
jclass g_input_system;
#endif

void store_package_name()
{
  std::ifstream f { "/proc/self/cmdline" };
  std::getline(f, andutils::get_package_name(), '\0');
  f.close();
}

void store_library_name()
{
  Dl_info info;
  dladdr(reinterpret_cast<void*>(store_library_name), &info);
  andutils::get_library_name() = info.dli_fname;
}

std::string jni_get_string(JNIEnv* env, jstring str)
{
  if (str == nullptr) {
    return "";
  }

  jboolean is_copy;
  const char* chars = env->GetStringUTFChars(str, &is_copy);
  std::string cpp_str = chars;
  env->ReleaseStringUTFChars(str, chars);
  return cpp_str;
}

void jni_init_context_and_classloader(JNIEnv* env)
{
  // Get Application context via ActivityThread.currentActivityThread().getApplication()
  jclass c_activity_thread = env->FindClass("android/app/ActivityThread");
  jmethodID m_current_activity_thread = env->GetStaticMethodID(c_activity_thread,
      "currentActivityThread", "()Landroid/app/ActivityThread;");
  jmethodID m_get_application = env->GetMethodID(c_activity_thread,
      "getApplication", "()Landroid/app/Application;");
  jobject o_current_thread = env->CallStaticObjectMethod(c_activity_thread, m_current_activity_thread);
  jobject o_context = env->CallObjectMethod(o_current_thread, m_get_application);
  g_context = env->NewGlobalRef(o_context);
  env->DeleteLocalRef(o_context);
  env->DeleteLocalRef(o_current_thread);
  env->DeleteLocalRef(c_activity_thread);

  // Get application ClassLoader via Context.getClassLoader()
  jclass c_context = env->GetObjectClass(g_context);
  jmethodID m_get_classloader = env->GetMethodID(c_context,
      "getClassLoader", "()Ljava/lang/ClassLoader;");

  jobject o_classloader = env->CallObjectMethod(g_context, m_get_classloader);
  g_classloader = env->NewGlobalRef(o_classloader);

  env->DeleteLocalRef(o_classloader);
  env->DeleteLocalRef(c_context);
}

void jni_init_external_storage(JNIEnv* env)
{
  jclass c_context = env->GetObjectClass(g_context);

  // Get objects
  jmethodID m_get_external_files_dir = env->GetMethodID(c_context,
      "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;");
  jobject o_external_files = env->CallObjectMethod(g_context, m_get_external_files_dir, nullptr);
  jmethodID m_get_external_media_dirs = env->GetMethodID(c_context,
      "getExternalMediaDirs", "()[Ljava/io/File;");
  auto o_external_media_array = static_cast<jobjectArray>(env->CallObjectMethod(g_context, m_get_external_media_dirs));
  jobject o_external_media = env->GetObjectArrayElement(o_external_media_array, 0);
  env->DeleteLocalRef(o_external_media_array);
  env->DeleteLocalRef(c_context);

  // Now, get paths from objects
  jclass c_file = env->GetObjectClass(o_external_files);
  jmethodID m_get_canonical_path = env->GetMethodID(c_file,
      "getCanonicalPath", "()Ljava/lang/String;");
  auto s_external_files_path = static_cast<jstring>(env->CallObjectMethod(o_external_files, m_get_canonical_path));
  auto s_external_media_path = static_cast<jstring>(env->CallObjectMethod(o_external_media, m_get_canonical_path));
  env->DeleteLocalRef(c_file);
  env->DeleteLocalRef(o_external_media);
  env->DeleteLocalRef(o_external_files);

  // And finally, convert paths to std::string
  andutils::jni::get_external_storage() = jni_get_string(env, s_external_files_path);
  andutils::jni::get_external_media() = jni_get_string(env, s_external_media_path);

  // Final cleanup
  env->DeleteLocalRef(s_external_media_path);
  env->DeleteLocalRef(s_external_files_path);
}

void jni_init_clipboard(JNIEnv* env)
{
  // Get clipboard service in UI thread
  jclass c_context = env->GetObjectClass(g_context);
  jmethodID m_get_system_service = env->GetMethodID(c_context,
      "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
  jstring s_clipboard_service = env->NewStringUTF("clipboard");
  jobject o_clipboard_manager = env->CallObjectMethod(g_context, m_get_system_service, s_clipboard_service);
  g_clipboard_manager = env->NewGlobalRef(o_clipboard_manager);
  env->DeleteLocalRef(o_clipboard_manager);
  env->DeleteLocalRef(s_clipboard_service);
  env->DeleteLocalRef(c_context);

  // FindClass is fucked in threads
  jclass c_clip_data = env->FindClass("android/content/ClipData");
  g_clip_data_class = static_cast<jclass>(env->NewGlobalRef(c_clip_data));
  env->DeleteLocalRef(c_clip_data);
}

// Returns a local reference to ClassLoader
jobject jni_load_dex(JNIEnv* env, void* data, std::size_t length)
{
  // First, try to load with InMemoryDexClassLoader
  jclass c_memory_dexloader = env->FindClass("dalvik/system/InMemoryDexClassLoader");
  if (c_memory_dexloader != nullptr) {
    jmethodID m_init = env->GetMethodID(c_memory_dexloader,
        "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    jobject o_byte_buffer = env->NewDirectByteBuffer(data, static_cast<jlong>(length));
    jobject o_classloader = env->NewObject(c_memory_dexloader, m_init, o_byte_buffer, g_classloader);
    env->DeleteLocalRef(o_byte_buffer);
    env->DeleteLocalRef(c_memory_dexloader);
    return o_classloader;
  }
  env->ExceptionClear();

  // If InMemoryDexClassLoader doesn't exist, generate temp file to load via PathClassLoader
  // file_manager is not available now, because it requires paths from Java
  std::filesystem::path base_path = "/data/data";
  base_path /= andutils::get_package_name();
  base_path /= "code_cache";

  std::filesystem::path file_path;
  do {
    file_path = base_path / randutils::string(10, "0123456789ABCDEFGHIKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_");
  } while (std::filesystem::exists(file_path));

  std::FILE* fp = std::fopen(file_path.c_str(), "wb");
  std::fwrite(data, 1, length, fp);
  std::fclose(fp);

  // And now, load via PathClassLoader
  jclass c_path_classloader = env->FindClass("dalvik/system/PathClassLoader");
  jmethodID m_init = env->GetMethodID(c_path_classloader,
      "<init>", "(Ljava/lang/String;Ljava/lang/ClassLoader;)V");
  jstring s_file_path = env->NewStringUTF(file_path.c_str());
  jobject o_classloader = env->NewObject(c_path_classloader, m_init, s_file_path, g_classloader);
  env->DeleteLocalRef(s_file_path);
  env->DeleteLocalRef(c_path_classloader);

  // Try to remove temp file, if we can
  std::error_code ec;
  std::filesystem::remove(file_path, ec);
  ec.clear();

  return o_classloader;
}

jclass jni_load_class(JNIEnv* env, jobject classloader, const char* classname)
{
  static jmethodID m_load_class = ([env]() {
    jclass c_classloader = env->FindClass("java/lang/ClassLoader");
    jmethodID result = env->GetMethodID(c_classloader,
        "loadClass", "(Ljava/lang/String;Z)Ljava/lang/Class;");
    env->DeleteLocalRef(c_classloader);
    return result;
  })();

  jstring s_classname = env->NewStringUTF(classname);
  auto o_class = static_cast<jclass>(env->CallObjectMethod(classloader, m_load_class, s_classname, true));
  env->DeleteLocalRef(s_classname);

  return o_class;
}

float jni_get_dpi(JNIEnv* env)
{
  jclass c_resources = env->FindClass("android/content/res/Resources");
  jclass c_dpy_metrics = env->FindClass("android/util/DisplayMetrics");
  jmethodID m_get_system = env->GetStaticMethodID(c_resources, "getSystem", "()Landroid/content/res/Resources;");
  jobject o_system = env->CallStaticObjectMethod(c_resources, m_get_system);
  jmethodID m_get_dpy_metrics = env->GetMethodID(c_resources, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
  jobject o_dpy_metrics = env->CallObjectMethod(o_system, m_get_dpy_metrics);

  jfieldID f_ydpi = env->GetFieldID(c_dpy_metrics, "ydpi", "F");
  float ydpi = env->GetFloatField(o_dpy_metrics, f_ydpi);
  env->DeleteLocalRef(o_system);
  env->DeleteLocalRef(o_dpy_metrics);
  env->DeleteLocalRef(c_dpy_metrics);
  env->DeleteLocalRef(c_resources);

  return ydpi;
}
}

void andutils::init()
{
  store_package_name();
  store_library_name();
}

std::string& andutils::get_package_name()
{
  static std::string package_name;
  return package_name;
}

std::string& andutils::get_library_name()
{
  static std::string library_name;
  return library_name;
}

float& andutils::jni::get_dpi()
{
  static float dpi;
  return dpi;
}

std::string& andutils::jni::get_external_storage()
{
  static std::string ext_storage;
  return ext_storage;
}

std::string& andutils::jni::get_external_media()
{
  static std::string ext_media;
  return ext_media;
}

std::string andutils::jni::get_clipboard(JNIEnv* env)
{
  // getPrimaryClip()
  jclass c_clipboard_manager = env->GetObjectClass(g_clipboard_manager);
  static jmethodID m_get_primary_clip = env->GetMethodID(c_clipboard_manager, "getPrimaryClip",
      "()Landroid/content/ClipData;");
  jobject primary_clip = env->CallObjectMethod(g_clipboard_manager, m_get_primary_clip);
  env->DeleteLocalRef(c_clipboard_manager);
  if (primary_clip == nullptr) {
    return "";
  }

  // getItemAt(0)
  jclass c_clip_data = env->GetObjectClass(primary_clip);
  static jmethodID m_get_item_at = env->GetMethodID(c_clip_data, "getItemAt",
      "(I)Landroid/content/ClipData$Item;");
  jobject clip_item = env->CallObjectMethod(primary_clip, m_get_item_at, 0);
  env->DeleteLocalRef(primary_clip);
  env->DeleteLocalRef(c_clip_data);

  if (env->ExceptionCheck()) {
    clear_pending_exception(env);
    clip_item = nullptr;
  }
  if (!clip_item) {
    return "";
  }

  // coerceToText()
  jclass c_clip_item = env->GetObjectClass(clip_item);
  static jmethodID m_coerce_to_text = env->GetMethodID(c_clip_item, "coerceToText",
      "(Landroid/content/Context;)Ljava/lang/CharSequence;");
  jobject text_char_sequence = env->CallObjectMethod(clip_item, m_coerce_to_text, g_context);
  env->DeleteLocalRef(clip_item);
  env->DeleteLocalRef(c_clip_item);
  clear_pending_exception(env);
  if (!text_char_sequence) {
    return "";
  }

  // toString()
  jclass c_char_sequence = env->GetObjectClass(text_char_sequence);
  static jmethodID m_to_string = env->GetMethodID(c_char_sequence, "toString",
      "()Ljava/lang/String;");
  auto text = static_cast<jstring>(env->CallObjectMethod(text_char_sequence, m_to_string));
  env->DeleteLocalRef(text_char_sequence);
  env->DeleteLocalRef(c_char_sequence);
  clear_pending_exception(env);

  // convert to std::string
  std::string result = jni_get_string(env, text);
  env->DeleteLocalRef(text);
  return result;
}

void andutils::jni::set_clipboard(JNIEnv* env, const char* value)
{
  // ClipData.newPlainText("default", "...")
  static jmethodID m_new_plaintext = env->GetStaticMethodID(g_clip_data_class, "newPlainText",
      "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;");
  jstring label = env->NewStringUTF("default");
  jstring text = env->NewStringUTF(value);
  clear_pending_exception(env);
  jobject clip = env->CallStaticObjectMethod(g_clip_data_class, m_new_plaintext, label, text);
  env->DeleteLocalRef(label);
  env->DeleteLocalRef(text);
  clear_pending_exception(env);

  // setPrimaryClip(...)
  jclass c_clipboard_manager = env->GetObjectClass(g_clipboard_manager);
  static jmethodID m_set_primary_clip = env->GetMethodID(c_clipboard_manager, "setPrimaryClip",
      "(Landroid/content/ClipData;)V");
  env->CallVoidMethod(g_clipboard_manager, m_set_primary_clip, clip);
  env->DeleteLocalRef(clip);
  env->DeleteLocalRef(c_clipboard_manager);
  clear_pending_exception(env);
}

#ifdef NEW_INPUT_SYSTEM
void andutils::jni::input_init(JNIEnv* env)
{
  jmethodID m_init = env->GetStaticMethodID(g_input_system, "init", "()V");
  env->CallStaticVoidMethod(g_input_system, m_init);
}

void andutils::jni::input_start(JNIEnv* env, const char* text, bool multiline, jint cursor)
{
  static jmethodID m_start_input = env->GetStaticMethodID(g_input_system,
      "startInput", "(Ljava/lang/String;ZI)V");

  jstring s_text = env->NewStringUTF(text);
  env->CallStaticVoidMethod(g_input_system, m_start_input,
      s_text, static_cast<jboolean>(multiline), cursor);
  env->DeleteLocalRef(s_text);
}

void andutils::jni::input_end(JNIEnv* env)
{
  static jmethodID m_end_input = env->GetStaticMethodID(g_input_system,
      "endInput", "(I)V");

  env->CallStaticVoidMethod(g_input_system, m_end_input, static_cast<jint>(INPUT_DEACTIVATED));
}

andutils::jni::InputStatus andutils::jni::input_status(JNIEnv* env)
{
  static jmethodID m_get_status = env->GetStaticMethodID(g_input_system,
      "getStatus", "()I");

  return static_cast<InputStatus>(env->CallStaticIntMethod(g_input_system, m_get_status));
}

void andutils::jni::input_set_text(JNIEnv* env, const char* text)
{
  static jmethodID m_set_text = env->GetStaticMethodID(g_input_system,
      "setText", "(Ljava/lang/String;)V");

  jstring s_text = env->NewStringUTF(text);
  env->CallStaticVoidMethod(g_input_system, m_set_text, s_text);
  env->DeleteLocalRef(s_text);
}

std::string andutils::jni::input_get_text(JNIEnv* env)
{
  static jmethodID m_get_text = env->GetStaticMethodID(g_input_system,
      "getText", "()Ljava/lang/String;");

  auto s_text = static_cast<jstring>(env->CallStaticObjectMethod(g_input_system, m_get_text));
  std::string result = jni_get_string(env, s_text);
  env->DeleteLocalRef(s_text);
  return result;
}

void andutils::jni::input_set_selection(JNIEnv* env, jint start, jint end)
{
  static jmethodID m_set_selection = env->GetStaticMethodID(g_input_system,
      "setSelection", "(II)V");

  return env->CallStaticVoidMethod(g_input_system, m_set_selection, start, end);
}

std::pair<int, int> andutils::jni::input_get_selection(JNIEnv* env)
{
  static jmethodID m_get_selection = env->GetStaticMethodID(g_input_system,
      "getSelection", "()J");

  jlong packed_value = env->CallStaticLongMethod(g_input_system, m_get_selection);

  return { static_cast<int>(packed_value >> 32), static_cast<int>(packed_value) };
}
#endif

void andutils::jni::init_jni(JNIEnv* env)
{
  env->GetJavaVM(&java_getter::vm());
  jni_init_context_and_classloader(env); // must be first
  jni_init_external_storage(env);
  jni_init_clipboard(env);
  get_dpi() = jni_get_dpi(env);

  // Load InputSystem
#ifdef NEW_INPUT_SYSTEM
  for (auto& i : input_system_data_xor_0xcc) {
    i ^= 0xCC;
  }
  jobject o_classloader = jni_load_dex(env, input_system_data_xor_0xcc, sizeof(input_system_data_xor_0xcc));
  jclass c_input_system = jni_load_class(env, o_classloader, "InputSystem");
  g_input_system = static_cast<jclass>(env->NewGlobalRef(c_input_system));
  env->DeleteLocalRef(c_input_system);
  env->DeleteLocalRef(o_classloader);
  std::memset(input_system_data_xor_0xcc, 0, sizeof(input_system_data_xor_0xcc));
#endif
}