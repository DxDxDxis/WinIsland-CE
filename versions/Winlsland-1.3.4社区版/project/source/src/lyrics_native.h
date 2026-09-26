#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wi {

// Stable, renderer-independent representation of synchronized lyrics.  The
// legacy Lyrics::lines field remains the compatibility projection consumed by
// the island renderer; this document carries syllables and auxiliary tracks.
struct LyricsSyllable {
    double start = 0;
    double end = 0;
    std::wstring text;
};

struct LyricsLine {
    double start = 0;
    double end = 0;
    std::wstring text;
    std::wstring translation;
    std::wstring romanization;
    std::vector<LyricsSyllable> syllables;
    std::vector<LyricsLine> background;
    std::wstring agent;
};

struct LyricsMetadata {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    double duration = 0;
};

struct LyricsDocument {
    std::vector<LyricsLine> lines;
    LyricsMetadata metadata;
    std::wstring provider;
    std::wstring format;
    int offsetMs = 0;
    bool hasTranslation = false;
    bool hasRomanization = false;
    bool characterSynchronized = false;
    bool degradedToLines = false;
    std::wstring plain;
    std::wstring language;
    std::string original;
};

// Format/decryption errors are represented by an empty optional.  Parsers are
// bounded and never throw across the provider boundary.
std::optional<LyricsDocument> parseNativeLyrics(const std::string &raw,
                                                const std::string &format = {});
std::optional<std::string> decryptKrc(const std::string &base64);
std::optional<std::string> decryptQrc(const std::string &hex);
std::optional<LyricsDocument> parseTtmlLyrics(const std::string &xml);
std::optional<LyricsDocument> parseProviderLyrics(const std::string &raw, const std::wstring &provider);
void mergeLyricsTrack(LyricsDocument &, const LyricsDocument &, bool romanization);
std::string qrcTransform(std::string input, bool encrypt);
std::string lyricsBase64(const std::string &, bool decode = false);

// Bounded XmlLite tree shared by QRC and TTML. DTD/entities are prohibited.
struct LyricsXml {
    std::wstring name, value;
    std::vector<std::pair<std::wstring,std::wstring>> attributes;
    std::vector<LyricsXml> children;
    std::wstring attribute(const wchar_t *) const;
    std::wstring text() const;
};
std::optional<LyricsXml> readLyricsXml(const std::string &);

} // namespace wi
