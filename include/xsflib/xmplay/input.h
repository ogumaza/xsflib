// SPDX-License-Identifier: MIT

// An XMPlay input plugin: a template on each format's traits. XMPlay is a 32-bit Windows program. The plugin is
// therefore a 32-bit DLL (see xsflib_add_xmplay_plugin). XMPlay calls it through the input interface of XMPlay's
// plugin SDK (xmpin.h): to check a file, to read its length and tags, and to open, decode and seek it. PSF tags become
// XMPlay's tags (game as album, year as date). Length and fade come from the tags, or from the defaults on the plugin's
// options page.
//
// The format's player does the emulation. By default the plugin renders ahead on a worker thread and buffers the audio
// (see xsflib/player/render_ahead.h). Seeks within the buffer are instant. Seeks further ahead wait for rendering to
// reach the target. A player with snapshots takes them every few seconds on tracks longer than the buffer. Seeking back
// before the buffered audio then restores the latest snapshot before the target and renders from there. Without
// snapshots, such a seek renders from the start of the track. With rendering ahead turned off, Process renders each
// block itself. A player with snapshots then takes them on every track.
//
// The options page is dialog 1000 in cmake/templates/xmplay_options.rc. It sets the length and fade of files without a
// length tag, endless playback and rendering ahead. XMPlay stores the settings among the rest of its settings
// (XMPFUNC_REGISTRY).
//
// A format's plugin defines its traits and XMPIN_GetInterface. XMPIN_GetInterface returns
// Plugin<Traits>::GetInterface, and the SDK's xmpin.def exports it by that name. The traits provide:
//
//   using Player            the format's player (see xsflib/player/playback.h)
//   using Set               the set that the format's loader assembles and Player::Load takes
//   static LoadSet          (path, reader, set, separators) -> std::optional<std::string>, the loader's error
//   kName                   the format's name ("xyz"): the plugin's name in XMPlay, and the filetype tag
//   kExtensions             the extensions that play, without dots
//   kPlaybackTags           the format's tags that set how a file plays, such as a sequence's variables. They aren't
//                           metadata, and XMPlay doesn't show them
//   kPsfVersion             the format's PSF version byte
//   kMaxFileSize            the largest file the plugin reads into memory
//   kVersion                the plugin's version, for its About box
//   kAbout                  the About box's text

#pragma once

#include <windows.h>
#include <xmpin.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "xsflib/player/playback.h"
#include "xsflib/player/render_ahead.h"
#include "xsflib/psf/psf.h"
#include "xsflib/utf8.h"

namespace xsflib::xmplay
{

namespace detail
{

constexpr std::size_t kBlockFrames = 1024; // frames that Process renders or reads at a time

// The first XMPlay with 64-bit file sizes and seeks in XMPFUNC_FILE (GetSize64 and Seek64): 3.8.2.
constexpr DWORD kMinXmplayVersion = 0x03080200;

// The most bytes that an info window's text takes. XMPlay's buffers hold about 40,000.
constexpr std::size_t kInfoLimit = 32000;

// The options page's controls, numbered as in cmake/templates/xmplay_options.rc. XMPlay's options pages have their
// Apply button at 1000.
constexpr int kLengthEdit = 10;
constexpr int kFadeEdit = 11;
constexpr int kEndlessBox = 12;
constexpr int kRenderAheadBox = 13;
constexpr int kApplyButton = 1000;

// The ranges of the default length and fade, in seconds.
constexpr int kMinLength = 1;
constexpr int kMaxLength = 24 * 3600;
constexpr int kMaxFade = 3600;

// The XMPlay tag for a PSF tag: game is album, and year is date.
inline std::string_view FieldFromTag(std::string_view tag)
{
    if (tag == "game")
    {
        return "album";
    }
    if (tag == "year")
    {
        return "date";
    }

    return tag;
}

// `text` in UTF-16, for Windows' functions.
inline std::wstring Wide(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(size, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);

    return wide;
}

// A whole number of seconds from an options page's edit, clamped to [low, high], or `old` when the edit holds none.
inline int SecondsFromEdit(UINT value, BOOL valid, int low, int high, int old)
{
    if (!valid)
    {
        return old;
    }

    return std::max(low, static_cast<int>(std::min<UINT>(value, static_cast<UINT>(high))));
}

} // namespace detail

// XMPlay's input plugin for the format's files. XMPlay plays one file at a time: Open, then Process and SetPosition on
// its decoding thread, then Close. Meanwhile it may check and read other files on other threads (CheckFile and
// GetFileInfo), and show the open file's tags and info on its main thread. No function lets an exception reach XMPlay.
template <typename Traits>
class Plugin
{
public:
    // XMPIN_GetInterface: the plugin's functions, or nullptr when XMPlay asks for another version of the interface
    // (`face`) or is older than 3.8.2. `faceproc` gives XMPlay's functions.
    static XMPIN* GetInterface(DWORD face, InterfaceProc faceproc)
    {
        if (face != XMPIN_FACE || faceproc == nullptr)
        {
            return nullptr;
        }

        try
        {
            in_ = static_cast<XMPFUNC_IN*>(faceproc(XMPFUNC_IN_FACE));
            misc_ = static_cast<XMPFUNC_MISC*>(faceproc(XMPFUNC_MISC_FACE));
            file_ = static_cast<XMPFUNC_FILE*>(faceproc(XMPFUNC_FILE_FACE));
            text_ = static_cast<XMPFUNC_TEXT*>(faceproc(XMPFUNC_TEXT_FACE));
            registry_ = static_cast<XMPFUNC_REGISTRY*>(faceproc(XMPFUNC_REGISTRY_FACE));
            if (in_ == nullptr || misc_ == nullptr || file_ == nullptr || text_ == nullptr ||
                misc_->GetVersion() < detail::kMinXmplayVersion)
            {
                return nullptr;
            }

            LoadSettings();

            static const std::string extensions = Extensions();
            static XMPIN plugin = Interface(extensions.c_str());
            return &plugin;
        }
        catch (...)
        {
            return nullptr;
        }
    }

private:
    using Player = typename Traits::Player;
    using Playback = player::Playback<Player>;
    using Options = player::PlaybackOptions<Player>;

    // The settings on the options page.
    struct Settings
    {
        int default_length_s = static_cast<int>(Options::kDefaultLengthMs / 1000);
        int default_fade_s = static_cast<int>(Options::kDefaultFadeMs / 1000);
        bool endless = false;
        bool render_ahead = true;
    };

    // Closes an XMPFILE that the plugin opened.
    struct FileCloser
    {
        void operator()(void* file) const
        {
            file_->Close(file);
        }
    };

    using OpenedFile = std::unique_ptr<void, FileCloser>;

    // The settings' keys in XMPlay's settings, in the section that Section() names.
    static constexpr const char* kLengthKey = "Default length";
    static constexpr const char* kFadeKey = "Default fade";
    static constexpr const char* kEndlessKey = "Endless";
    static constexpr const char* kRenderAheadKey = "Render ahead";

    // Windows paths never hold '|'. With it among the separators, a set in an archive whose files' paths read
    // archive|file finds its libraries in the archive.
    static constexpr std::string_view kSeparators = "/\\|";

    static constexpr float kSampleScale = 1.0f / 32768.0f; // 16-bit samples to XMPlay's floats

    // Whether XMPlay leaves the tag `name` out: a PSF tag that isn't metadata, or one of the format's playback tags.
    static bool IsHiddenTag(std::string_view name)
    {
        return psf::IsSystemTag(name) || std::any_of(Traits::kPlaybackTags.begin(), Traits::kPlaybackTags.end(),
                                                     [name](const char* tag) { return name == tag; });
    }

    // The interface that GetInterface gives XMPlay.
    static XMPIN Interface(const char* extensions)
    {
        XMPIN plugin{};
        plugin.flags = XMPIN_FLAG_OPTIONS;
        plugin.name = Traits::kName;
        plugin.exts = extensions;
        plugin.About = About;
        plugin.CheckFile = CheckFile;
        plugin.GetFileInfo = FileInfo;
        plugin.Open = Open;
        plugin.Close = Close;
        plugin.SetFormat = SetFormat;
        plugin.GetTags = OpenTags;
        plugin.GetInfoText = InfoText;
        plugin.GetGeneralInfo = GeneralInfo;
        plugin.GetMessage = MessageInfo;
        plugin.SetPosition = SetPosition;
        plugin.Process = Process;
        plugin.Options = OptionsPage;

        return plugin;
    }

    // XMPIN's exts: the file type's name, then the extensions separated by '/'. c_str() ends the list with '\0'.
    static std::string Extensions()
    {
        std::string list = Traits::kName;
        list += '\0';
        for (std::size_t i = 0; i < Traits::kExtensions.size(); i++)
        {
            if (i != 0)
            {
                list += '/';
            }
            list += Traits::kExtensions[i];
        }

        return list;
    }

    static void WINAPI About(HWND window)
    {
        try
        {
            const std::string caption = std::string(Traits::kName) + " plugin";
            const std::string text = caption + " " + Traits::kVersion + "\n\n" + Traits::kAbout;
            MessageBoxW(window, detail::Wide(text).c_str(), detail::Wide(caption).c_str(), MB_OK | MB_ICONINFORMATION);
        }
        catch (...)
        {
        }
    }

    // Whether `file` starts with the PSF signature and the format's version.
    static BOOL WINAPI CheckFile(const char* /*filename*/, XMPFILE file)
    {
        if (file == nullptr)
        {
            return FALSE;
        }

        uint8_t head[4] = {};
        file_->Seek64(file, 0);
        if (file_->Read(file, head, sizeof(head)) != sizeof(head))
        {
            return FALSE;
        }

        return head[0] == 'P' && head[1] == 'S' && head[2] == 'F' && head[3] == Traits::kPsfVersion;
    }

    // The number of tracks in `file`: 1, or 0 when it isn't one of the format's files. Gives the track's length and
    // tags in memory from XMPlay's Alloc, which XMPlay frees.
    static DWORD WINAPI FileInfo(const char* /*filename*/, XMPFILE file, float** length, char** tags)
    {
        try
        {
            psf::PsfFile psf;
            if (file == nullptr || !ReadPsf(file, psf))
            {
                return 0;
            }

            const float seconds = Seconds(LengthOf(psf.tags, CurrentSettings()));
            const std::string block = TagBlock(psf.tags);
            if (length != nullptr)
            {
                auto* lengths = static_cast<float*>(misc_->Alloc(sizeof(float)));
                if (lengths != nullptr)
                {
                    lengths[0] = seconds;
                }
                *length = lengths;
            }
            if (tags != nullptr)
            {
                *tags = Copy(block);
            }

            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    // Opens a file for playback: reads it and its libraries, and starts the player. Returns 2 (XMPlay may close
    // `file`, which the plugin has read), or 0 when the file doesn't play.
    static DWORD WINAPI Open(const char* filename, XMPFILE file)
    {
        CloseTrack();
        if (filename == nullptr)
        {
            return 0;
        }

        try
        {
            const std::string path = filename;
            std::vector<uint8_t> data;
            if (file != nullptr ? !ReadWhole(file, data) : !ReadPath(path, data))
            {
                return 0;
            }

            psf::PsfFile psf;
            if (psf::ParsePsf(data, psf, false) || psf.version != Traits::kPsfVersion)
            {
                return 0;
            }

            // The file and its libraries, read through XMPlay's file functions.
            const psf::FileReader reader = [&](const std::string& library, std::vector<uint8_t>& out)
            {
                if (library == path)
                {
                    out = data;
                    return true;
                }

                return ReadPath(library, out);
            };
            typename Traits::Set set;
            if (Traits::LoadSet(path, reader, set, kSeparators))
            {
                return 0;
            }

            const Settings settings = CurrentSettings();
            auto playback = std::make_unique<Playback>();
            if (!playback->Open(std::move(set), OptionsFrom(settings)) || !playback->Start())
            {
                return 0;
            }

            const player::TrackLength length = LengthOf(psf.tags, settings);
            if (settings.render_ahead)
            {
                ahead_ = std::make_unique<player::RenderAhead>(std::move(playback));
            }
            else
            {
                playback_ = std::move(playback);
            }
            buffer_.resize(2 * detail::kBlockFrames);

            {
                std::lock_guard lock(track_mutex_);
                tags_ = std::move(psf.tags);
                length_ = length;
                open_ = true;
            }

            in_->SetLength(Seconds(length), TRUE);

            return 2;
        }
        catch (...)
        {
            CloseTrack();
            return 0;
        }
    }

    static void WINAPI Close()
    {
        CloseTrack();
    }

    // The format of Process's samples: stereo at the player's rate, from 16-bit samples.
    static void WINAPI SetFormat(XMPFORMAT* format)
    {
        if (format == nullptr)
        {
            return;
        }

        format->rate = static_cast<DWORD>(Playback::kSampleRate);
        format->chan = 2;
        format->res = 2;
    }

    // The open file's tags, in memory from XMPlay's Alloc, or nullptr when no file is open.
    static char* WINAPI OpenTags()
    {
        try
        {
            std::lock_guard lock(track_mutex_);
            return open_ ? Copy(TagBlock(tags_)) : nullptr;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    // The main window's info: the format and the sample rate. XMPlay fills in the length.
    static void WINAPI InfoText(char* format, char* /*length*/)
    {
        if (format != nullptr)
        {
            std::snprintf(format, 64, "%s - %d Hz", Traits::kName, Playback::kSampleRate);
        }
    }

    // The General info window's text: lines of a heading and a value, separated by a tab.
    static void WINAPI GeneralInfo(char* buffer)
    {
        if (buffer == nullptr)
        {
            return;
        }

        try
        {
            std::string text = "Format\t" + std::string(Traits::kName) + "\r";
            text += "Sample rate\t" + std::to_string(Playback::kSampleRate) + " Hz\r";
            text += "Channels\t2\rResolution\t16 bit\r";
            {
                std::lock_guard lock(track_mutex_);
                if (open_)
                {
                    text += "Length\t" + psf::FormatTime(length_.play_ms) + "\r";
                    text += "Fade\t" + psf::FormatTime(length_.fade_ms) + "\r";
                    if (const auto it = tags_.find("volume"); it != tags_.end())
                    {
                        text += "Volume\t" + it->second + "\r";
                    }
                }
            }

            text.resize(std::min(text.size(), detail::kInfoLimit - 1));
            std::memcpy(buffer, text.c_str(), text.size() + 1);
        }
        catch (...)
        {
            buffer[0] = '\0';
        }
    }

    // The Message info window's text: the open file's tags, by their PSF names.
    static void WINAPI MessageInfo(char* buffer)
    {
        if (buffer == nullptr)
        {
            return;
        }

        buffer[0] = '\0';
        try
        {
            std::lock_guard lock(track_mutex_);
            if (!open_)
            {
                return;
            }

            const bool utf8 = tags_.count("utf8") != 0;
            char* end = buffer;
            for (const auto& [name, value] : tags_)
            {
                if (IsHiddenTag(name))
                {
                    continue;
                }

                // FormatInfoText may write each line break as two characters.
                const std::string text = NativeText(value, utf8);
                const std::size_t most = name.size() + 2 * text.size() + 4;
                if (static_cast<std::size_t>(end - buffer) + most >= detail::kInfoLimit)
                {
                    break;
                }

                end = misc_->FormatInfoText(end, name.c_str(), text.c_str());
            }
        }
        catch (...)
        {
        }
    }

    // Seeks to `position` milliseconds. Returns the position in seconds, or -1 when the seek fails.
    static double WINAPI SetPosition(DWORD position)
    {
        // XMPlay's special positions (loops, tails and subsongs) have the top bit set. The plugin offers none of them.
        if ((position & XMPIN_POS_SUBSONG) != 0 || (!ahead_ && !playback_))
        {
            return -1;
        }

        try
        {
            const uint64_t frame =
                static_cast<uint64_t>(position) * static_cast<uint64_t>(Playback::kSampleRate) / 1000;
            if (ahead_)
            {
                ahead_->Seek(frame); // Process waits for the audio there
                return position / 1000.0;
            }

            if (!playback_->Seek(frame, Cancelled))
            {
                return -1;
            }

            return static_cast<double>(playback_->Position()) / Playback::kSampleRate;
        }
        catch (...)
        {
            return -1;
        }
    }

    // Writes up to `count` floats: interleaved stereo samples. Fewer than `count` ends the track.
    static DWORD WINAPI Process(float* out, DWORD count)
    {
        if (out == nullptr || (!ahead_ && !playback_))
        {
            return 0;
        }

        try
        {
            const std::size_t frames = count / 2;
            std::size_t done = 0;
            while (done < frames)
            {
                const std::size_t wanted = std::min(frames - done, detail::kBlockFrames);
                const std::size_t got = ahead_ ? ahead_->Read(buffer_.data(), wanted, Cancelled)
                                               : playback_->Render(buffer_.data(), wanted);
                if (got == 0)
                {
                    break;
                }

                for (std::size_t i = 0; i < 2 * got; i++)
                {
                    out[2 * done + i] = static_cast<float>(buffer_[i]) * kSampleScale;
                }
                done += got;
            }

            return static_cast<DWORD>(2 * done);
        }
        catch (...)
        {
            return 0;
        }
    }

    // The options page's handler. XMPlay shows the page in its options window.
    static INT_PTR CALLBACK OptionsPage(HWND page, UINT message, WPARAM wparam, LPARAM /*lparam*/)
    {
        try
        {
            if (message == WM_INITDIALOG)
            {
                ShowSettings(page, CurrentSettings());
                return TRUE;
            }

            if (message != WM_COMMAND)
            {
                return FALSE;
            }

            const int control = LOWORD(wparam);
            const int notification = HIWORD(wparam);
            if (control == detail::kApplyButton && notification == BN_CLICKED)
            {
                ApplySettings(page);
                return TRUE;
            }

            const bool edited =
                (control == detail::kLengthEdit || control == detail::kFadeEdit) && notification == EN_CHANGE;
            const bool clicked =
                (control == detail::kEndlessBox || control == detail::kRenderAheadBox) && notification == BN_CLICKED;
            if (edited || clicked)
            {
                EnableWindow(GetDlgItem(page, detail::kApplyButton), TRUE);
                return TRUE;
            }
        }
        catch (...)
        {
        }

        return FALSE;
    }

    static void CloseTrack()
    {
        ahead_.reset();
        playback_.reset();

        std::lock_guard lock(track_mutex_);
        open_ = false;
        tags_.clear();
        length_ = {};
    }

    // Whether XMPlay wants the plugin to stop waiting, such as when playback stops during a long seek.
    static bool Cancelled()
    {
        return misc_->CheckCancel() != FALSE;
    }

    // Reads the whole of `file` from its start. Returns false when it can't be read or is larger than kMaxFileSize.
    static bool ReadWhole(XMPFILE file, std::vector<uint8_t>& data)
    {
        const uint64_t size = file_->GetSize64(file);
        if (size > Traits::kMaxFileSize || !file_->Seek64(file, 0))
        {
            return false;
        }

        data.resize(static_cast<std::size_t>(size));
        std::size_t done = 0;
        while (done < data.size())
        {
            const auto wanted = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, std::size_t{1} << 20));
            const DWORD got = file_->Read(file, data.data() + done, wanted);
            if (got == 0)
            {
                break;
            }

            done += got;
        }

        return done == data.size();
    }

    // Reads the file at `path` through XMPlay's file functions.
    static bool ReadPath(const std::string& path, std::vector<uint8_t>& data)
    {
        const OpenedFile file(file_->Open(path.c_str()));
        return file != nullptr && ReadWhole(file.get(), data);
    }

    // Reads the tags of `file`, and checks that it's one of the format's files. The program isn't read.
    static bool ReadPsf(XMPFILE file, psf::PsfFile& psf)
    {
        std::vector<uint8_t> data;
        return ReadWhole(file, data) && !psf::ParsePsf(data, psf, false) && psf.version == Traits::kPsfVersion;
    }

    // A tag's value in XMPlay's text, which is UTF-8. PSF tags are UTF-8 when the file has a utf8 tag. Without one,
    // text that isn't valid UTF-8 is read in the system's code page.
    static std::string NativeText(const std::string& value, bool utf8)
    {
        if (utf8 || IsUtf8(value))
        {
            return value;
        }

        char* converted = text_->Ansi(value.data(), static_cast<int>(value.size()));
        if (converted == nullptr)
        {
            return {};
        }

        std::string text = converted;
        misc_->Free(converted);

        return text;
    }

    // XMPlay's tags for `tags`: the filetype tag, then the metadata, as names and values that each end with '\0'.
    // Copy() adds the final '\0'.
    static std::string TagBlock(const psf::Tags& tags)
    {
        const bool utf8 = tags.count("utf8") != 0;
        std::string block = "filetype";
        block += '\0';
        block += Traits::kName;
        block += '\0';
        for (const auto& [name, value] : tags)
        {
            if (IsHiddenTag(name) || name == "filetype")
            {
                continue;
            }

            block += detail::FieldFromTag(name);
            block += '\0';
            block += NativeText(value, utf8);
            block += '\0';
        }

        return block;
    }

    // `text` and a final '\0' in memory from XMPlay's Alloc, or nullptr when Alloc fails.
    static char* Copy(const std::string& text)
    {
        auto* copy = static_cast<char*>(misc_->Alloc(static_cast<DWORD>(text.size() + 1)));
        if (copy != nullptr)
        {
            std::memcpy(copy, text.c_str(), text.size() + 1);
        }

        return copy;
    }

    // The length and fade of a file with `tags`: from its length and fade tags, or the defaults.
    static player::TrackLength LengthOf(const psf::Tags& tags, const Settings& settings)
    {
        return player::ResolveLength(psf::TagTimeMs(tags, "length"), psf::TagTimeMs(tags, "fade"),
                                     OptionsFrom(settings));
    }

    static float Seconds(const player::TrackLength& length)
    {
        return static_cast<float>(static_cast<double>(length.play_ms + length.fade_ms) / 1000.0);
    }

    // How a track plays with `settings`. Every track takes snapshots: XMPlay opens files only to play them.
    static Options OptionsFrom(const Settings& settings)
    {
        Options options;
        options.default_length_ms = settings.default_length_s * 1000LL;
        options.default_fade_ms = settings.default_fade_s * 1000LL;
        options.endless = settings.endless;

        return options;
    }

    static Settings CurrentSettings()
    {
        std::lock_guard lock(settings_mutex_);
        return settings_;
    }

    // The section of XMPlay's settings that holds the plugin's.
    static std::string Section()
    {
        return std::string(Traits::kName) + " plugin";
    }

    // Reads the settings from XMPlay's settings. A setting that XMPlay doesn't hold keeps its default.
    static void LoadSettings()
    {
        Settings settings;
        if (registry_ != nullptr)
        {
            const std::string section = Section();
            int value = 0;
            if (registry_->GetInt(section.c_str(), kLengthKey, &value))
            {
                settings.default_length_s = std::clamp(value, detail::kMinLength, detail::kMaxLength);
            }
            if (registry_->GetInt(section.c_str(), kFadeKey, &value))
            {
                settings.default_fade_s = std::clamp(value, 0, detail::kMaxFade);
            }
            if (registry_->GetInt(section.c_str(), kEndlessKey, &value))
            {
                settings.endless = value != 0;
            }
            if (registry_->GetInt(section.c_str(), kRenderAheadKey, &value))
            {
                settings.render_ahead = value != 0;
            }
        }

        std::lock_guard lock(settings_mutex_);
        settings_ = settings;
    }

    static void SaveSettings(const Settings& settings)
    {
        if (registry_ == nullptr)
        {
            return;
        }

        const std::string section = Section();
        const int endless = settings.endless ? 1 : 0;
        const int render_ahead = settings.render_ahead ? 1 : 0;
        registry_->SetInt(section.c_str(), kLengthKey, &settings.default_length_s);
        registry_->SetInt(section.c_str(), kFadeKey, &settings.default_fade_s);
        registry_->SetInt(section.c_str(), kEndlessKey, &endless);
        registry_->SetInt(section.c_str(), kRenderAheadKey, &render_ahead);
    }

    // Shows `settings` on the options page. Nothing is left to apply.
    static void ShowSettings(HWND page, const Settings& settings)
    {
        SetDlgItemInt(page, detail::kLengthEdit, static_cast<UINT>(settings.default_length_s), FALSE);
        SetDlgItemInt(page, detail::kFadeEdit, static_cast<UINT>(settings.default_fade_s), FALSE);
        CheckDlgButton(page, detail::kEndlessBox, settings.endless ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(page, detail::kRenderAheadBox, settings.render_ahead ? BST_CHECKED : BST_UNCHECKED);

        // Setting the edits' text enabled the Apply button.
        EnableWindow(GetDlgItem(page, detail::kApplyButton), FALSE);
    }

    // Takes the settings from the options page, keeps them in XMPlay's settings, and shows them as they apply. The next
    // file that XMPlay opens plays with them.
    static void ApplySettings(HWND page)
    {
        Settings settings = CurrentSettings();
        BOOL valid = FALSE;
        UINT value = GetDlgItemInt(page, detail::kLengthEdit, &valid, FALSE);
        settings.default_length_s =
            detail::SecondsFromEdit(value, valid, detail::kMinLength, detail::kMaxLength, settings.default_length_s);
        value = GetDlgItemInt(page, detail::kFadeEdit, &valid, FALSE);
        settings.default_fade_s = detail::SecondsFromEdit(value, valid, 0, detail::kMaxFade, settings.default_fade_s);
        settings.endless = IsDlgButtonChecked(page, detail::kEndlessBox) == BST_CHECKED;
        settings.render_ahead = IsDlgButtonChecked(page, detail::kRenderAheadBox) == BST_CHECKED;

        {
            std::lock_guard lock(settings_mutex_);
            settings_ = settings;
        }
        SaveSettings(settings);
        ShowSettings(page, settings);
    }

    // XMPlay's functions, from GetInterface.
    inline static XMPFUNC_IN* in_ = nullptr;
    inline static XMPFUNC_MISC* misc_ = nullptr;
    inline static XMPFUNC_FILE* file_ = nullptr;
    inline static XMPFUNC_TEXT* text_ = nullptr;
    inline static XMPFUNC_REGISTRY* registry_ = nullptr; // without it, the settings aren't kept

    inline static std::mutex settings_mutex_;
    inline static Settings settings_; // guarded by settings_mutex_

    // The open track's playback, which XMPlay's decoding thread uses.
    inline static std::unique_ptr<Playback> playback_;         // when Process renders the blocks itself
    inline static std::unique_ptr<player::RenderAhead> ahead_; // owns the Playback while rendering ahead
    inline static std::vector<int16_t> buffer_;

    // The open track's tags and length, which the info functions read on other threads.
    inline static std::mutex track_mutex_;
    inline static bool open_ = false;          // guarded by track_mutex_
    inline static psf::Tags tags_;             // guarded by track_mutex_
    inline static player::TrackLength length_; // guarded by track_mutex_
};

} // namespace xsflib::xmplay
