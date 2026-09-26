#pragma once
#include <string>
namespace wi {
std::string neteaseEapi(const std::string& path, const std::string& json);
std::string neteaseWeapi(const std::string& json, const std::string& testKey = {});
std::wstring protectLyricsSecret(const std::wstring&);
std::wstring unprotectLyricsSecret(const std::wstring&);
}
