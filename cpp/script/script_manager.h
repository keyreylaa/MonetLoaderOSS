#pragma once
#include "logger.h"
#include "script.h"
#include <filesystem>
#include <list>

namespace lua {
class script_manager {
  public:
  static void init();
  static void shutdown();
  static void run(bool in_pause);
  static void run_queued_actions(); // needs to be run after exiting all lua contexts
  static void on_game_restart();

  // The script that runs at the moment. A crash inside a script kills the
  // process, so the client prints this name next to the backtrace: without
  // it the trace only says that a script was running and not which one.
  static void set_running(const std::string& name)
  {
    _running = name;
  }

  static const char* running()
  {
    return _running.empty() ? nullptr : _running.c_str();
  }

  static bool initialized()
  {
    return _initialized;
  }

  template <typename F>
  static void for_each_event_handler(const std::string& name, F fn)
  {
    // std::lock_guard<std::mutex> lock { _gil };
    for (auto it = _scripts.begin(); it != _scripts.end();) {
      bool ok = true;

      if (!it->second.dead) {
        auto it_2 = it->second.event_handlers.find(name);
        if (it_2 == it->second.event_handlers.end()) {
          ++it;
          continue;
        }

        _running = it->second.info.name;

        for (auto i : it_2->second) {
          std::optional<sol::error> e = fn(i);
          if (e.has_value() && !it->second.dying) {
            it->second.dying = true;
            logger::log<logger::LV_ERROR>(&it->second, "{}", e->what());
            ok = false;
            break;
          }
        }
      }

      if (ok) {
        ++it;
      } else {
        it = destroy_script(it, destroy_script_type::error);
      }
    }
  }

  static bool handle_command(std::string_view command)
  {
    if (!command.size() || command[0] != '/') {
      return false;
    }

    std::size_t sep = command.find(' ');
    std::string name;
    std::string params;
    if (sep != std::string_view::npos && sep < command.length() - 1) {
      name = command.substr(1, sep - 1);

      std::size_t sep_2 = sep = command.find_first_not_of(' ', sep);
      if (sep_2 != std::string::npos) {
        params = command.substr(sep_2);
      } else {
        params = "";
      }
    } else {
      name = command.substr(1);
      params = "";
    }

    bool ok = true;
    std::size_t owner_id = 0;

    {
      auto it = _commands.find(name);
      if (it == _commands.end()) {
        return false;
      }

      if (it->second.owner.dead) {
        return false;
      }

      // std::lock_guard<std::mutex> lock { _gil };
      auto pfr = it->second.callback(params);
      if (!pfr.valid() && !it->second.owner.dying) {
        it->second.owner.dying = true;
        sol::error e = pfr;
        logger::log<logger::LV_ERROR>(&it->second.owner, "{}", e.what());
        owner_id = it->second.owner.id;
        ok = false;
      }
    }

    if (!ok) {
      destroy_script(_scripts.find(owner_id), destroy_script_type::error);
    }

    return ok;
  }

  static pid_t get_main_thread_id()
  {
    return _main_thread_id;
  }

  static script* get_script(std::size_t id)
  {
    auto it = _scripts.find(id);
    if (it == _scripts.end()) {
      return nullptr;
    }

    return &it->second;
  }

  static void register_packages(sol::state_view st); // public for effil

  private:
  using script_map_type = std::unordered_map<std::size_t, script>;

  static void init_modules();

  static void register_core_functions(script& s);
  static void register_script_info_functions(script& s);
  static void register_thread_functions(script& s);
  static void register_memory_functions(script& s);

  static void register_opcode_extensions(script& s);
  static void register_cleo_opcodes(script& s);
  static void register_sampfuncs_opcodes(script& s);

  static void register_globals(script& s);
  static void register_handles(script& s);

  static std::optional<std::size_t> load(std::filesystem::path path);
  static void one_off_load(std::filesystem::path path);
  static void load_all();
  static std::optional<sol::error> start(script& s, const std::string& path);
  static std::optional<sol::error> start_main(script& s);
  static void start_main_all();

  static bool register_command(script& s, const std::string& name, sol::protected_function callback)
  {
    auto it = _commands.find(name);
    if (it != _commands.end()) {
      return false;
    }

    s.commands.insert(name);
    _commands.try_emplace(name, s, callback);
    return true;
  }
  static bool unregister_command(const std::string& name)
  {
    auto it = _commands.find(name);
    if (it == _commands.end()) {
      return false;
    }

    it->second.owner.commands.erase(name);
    _commands.erase(name);
    return true;
  }

  enum class destroy_script_type {
    normal,
    quit,
    error,
    silent
  };
  static script_map_type::iterator destroy_script(script_map_type::iterator siter, destroy_script_type type = destroy_script_type::normal);

  static inline std::unordered_map<std::string, lua_CFunction> _modules {};
  static inline script_map_type _scripts {};
  // static inline std::mutex _gil {};
  static inline pid_t _main_thread_id {};
  static inline bool _reload_all_later { false };
  static inline bool _initialized { false };
  static inline std::size_t _next_script_id { 1 };
  static inline std::unordered_map<std::string, std::size_t> _loaded_paths {};
  static inline std::string _running {};
  static inline std::filesystem::path _root_path {};

  struct script_action {
    enum class action_type {
      unload,
      reload,
      load,
      perform_real_unload
    };
    std::size_t id;
    std::string path;
    action_type type;
  };
  static inline std::list<script_action> _queued_actions {};

  struct command_info {
    command_info(script& owner, sol::protected_function callback)
        : owner(owner)
        , callback(sol::protected_function { owner.state, callback })
    {
    }

    script& owner;
    sol::protected_function callback;
  };
  static inline std::unordered_map<std::string, command_info> _commands {};

  friend struct script;
};
}