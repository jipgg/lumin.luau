#include "lib.hpp"
#include <shared.hpp>
#include <filesystem>
#include <ranges>
namespace fs = std::filesystem;
namespace rn = std::ranges;

namespace util {
Opt<String> find_module_path(const String& dllname,  std::string_view priority_dir) {
    if (not priority_dir.empty()) {
        fs::path potential_path = fs::path(priority_dir) / dllname;
        if (not potential_path.has_extension()) {
            potential_path.replace_extension(".dll");
        }
        auto standardize = [&potential_path]() -> std::string {
            std::string standardized = fs::absolute(potential_path).string();
            rn::replace(standardized, '\\', '/');
            return standardized;
        };
        if (not fs::exists(potential_path)) return std::nullopt;
        std::string path = fs::absolute(potential_path).string();
        rn::replace(path, '\\', '/');
        return path;
    }
    char buffer[MAX_PATH];
    DWORD result = SearchPath(nullptr, dllname.c_str(), nullptr, MAX_PATH, buffer, nullptr);
    if (result == 0 or result > MAX_PATH) {
        if (dllname.ends_with(".dll")) {
            for (const auto& [state, path] : shared::script_paths) {
                fs::path dllpath = fs::path(path).parent_path() / dllname;
                if (fs::exists(dllpath)) return fs::absolute(dllpath).string();
            }
            return std::nullopt;
        }
        return find_module_path(dllname + ".dll");
    }
    String path{buffer};
    rn::replace(path, '\\', '/');
    return path;
}
Dlmodule* init_or_find_module(const String& name, std::string_view priority_dir) {
    auto found_path = find_module_path(name, priority_dir);
    if (not found_path) return nullptr;
    if (auto it = glob::loaded.find(*found_path); it == glob::loaded.end()) {
        HMODULE hm = LoadLibrary(found_path->c_str());
        if (not hm) [[unlikely]] return nullptr;
        glob::loaded.emplace(*found_path, make_unique<Dlmodule>(hm, name, *found_path));
    }
    return glob::loaded[*found_path].get();
}
Opt<uintptr_t> find_proc_address(Dlmodule& module, const String& symbol) {
    auto& cached = module.cached;
    if (auto found = cached.find(symbol); found != cached.end()) return cached.at(symbol);
    FARPROC proc = GetProcAddress(module.handle, symbol.c_str());
    if (proc) cached.emplace(symbol, reinterpret_cast<uintptr_t>(proc));
    return proc ? std::make_optional(reinterpret_cast<uintptr_t>(proc)) : std::nullopt;
}
Dlmodule* lua_tomodule(lua_State* L, int idx) {
    if (lua_lightuserdatatag(L, idx) != Dlmodule::tag) {
        luaL_typeerrorL(L, idx, Dlmodule::tname);
    }
    auto mod = static_cast<Dlmodule*>(lua_tolightuserdatatagged(L, idx, Dlmodule::tag));
    return mod;
}
Dlmodule* lua_pushmodule(lua_State* L, Dlmodule* module) {
    lua_pushlightuserdatatagged(L, module, Dlmodule::tag);
    luaL_getmetatable(L, Dlmodule::tname);
    lua_setmetatable(L, -2);
    return module;
}
}
