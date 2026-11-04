#pragma once
#include "diagnostic_cli.hpp"
#include "ps2native/sha256.hpp"
#include <map>
#include <set>
#include <sstream>

namespace ps2native::dev {
namespace fs = std::filesystem;
inline std::string read_text(const fs::path& path) { const auto bytes = read_file(path); return {bytes.begin(), bytes.end()}; }
inline std::string digest(std::string_view value) {
    return sha256_hex({reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
}
inline void save_text(const fs::path& path, std::string_view text) {
    write_file_atomically(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}
inline fs::path absolute(const fs::path& value, const fs::path& base) {
    return fs::absolute(value.is_absolute() ? value : base / value).lexically_normal();
}
struct Profile {
    std::map<std::string,std::string> values;
    std::vector<std::string> runtime_args, aot_args, cmake_args;
    fs::path origin;
    static Profile load(const fs::path& file) {
        Profile p; p.origin = fs::absolute(file);
        std::istringstream input(read_text(file)); std::string line;
        while (std::getline(input,line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line.size() > 32768 || line.find('\0') != std::string::npos) throw std::runtime_error("invalid profile line");
            const auto equal = line.find('=');
            if (equal == std::string::npos) throw std::runtime_error("profile requires key=value, one argument per line");
            const auto key = line.substr(0,equal), value = line.substr(equal+1);
            if (key == "runtime_arg") p.runtime_args.push_back(value);
            else if (key == "aot_arg") p.aot_args.push_back(value);
            else if (key == "cmake_arg") p.cmake_args.push_back(value);
            else {
                static const std::set<std::string> keys{"schema","name","kind","input","input_sha256","source","build","config","generator","architecture","cmake","timeout","ring","stream_bytes","engine","ghidra"};
                if (!keys.contains(key) || !p.values.emplace(key,value).second) throw std::runtime_error("unknown or duplicate profile key: " + key);
            }
        }
        if (p.get("schema") != "ps2n-dev-v1") throw std::runtime_error("unsupported profile schema");
        for (const auto* key : {"input","source","build","cmake"}) {
            if (p.get(key).empty()) throw std::runtime_error(std::string("profile needs ") + key);
            p.values[key] = path_utf8(absolute(cli::utf8_path(p.get(key)),p.origin.parent_path()));
        }
        if(!p.get("ghidra").empty())p.values["ghidra"]=path_utf8(absolute(cli::utf8_path(p.get("ghidra")),p.origin.parent_path()));
        return p;
    }
    std::string get(const std::string& key, std::string fallback = {}) const {
        const auto it=values.find(key); return it == values.end() ? fallback : it->second;
    }
    fs::path path(const std::string& key) const { return cli::utf8_path(get(key)); }
    std::string serialize() const {
        std::string out = "# Local developer profile; never commit user paths or identities.\n";
        for (const auto& [k,v] : values) out += k+'='+v+'\n';
        for (const auto& v : cmake_args) out += "cmake_arg="+v+'\n';
        for (const auto& v : aot_args) out += "aot_arg="+v+'\n';
        for (const auto& v : runtime_args) out += "runtime_arg="+v+'\n';
        return out;
    }
};
} // namespace ps2native::dev
