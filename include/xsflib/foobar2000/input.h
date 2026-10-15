// SPDX-License-Identifier: MIT

// A foobar2000 input component: a template on each format's traits. Modelled on kode54's PSF-family inputs
// (foo_input_vio2sf, foo_input_ncsf). PSF tags are shown as metadata (game as album, year as date). Length and fade
// come from the tags, or from the defaults in Advanced preferences > Decoding > the format's decoder. The tags can be
// edited.
//
// The format's player does the emulation. While playing, the component renders ahead on a worker thread and buffers the
// audio (see xsflib/player/render_ahead.h). Seeks within the buffer are instant. Seeks further ahead wait for rendering
// to reach the target. A player with snapshots takes them every few seconds on tracks longer than the buffer. Seeking
// back before the buffered audio then restores the latest snapshot before the target and renders from there. Without
// snapshots, such a seek renders from the start of the track. With rendering ahead turned off, decode_run renders each
// block itself. A player with snapshots then takes them on every track.
//
// A format's component defines its traits, then a Settings<Traits> and an input factory for Input<Traits>, in that
// order. The component declares its file type and version with the SDK's macros. The traits provide:
//
//   using Player            the format's player (see xsflib/player/playback.h)
//   using Set               the set that the format's loader assembles and Player::Load takes
//   static LoadSet          (path, reader, set, separators) -> std::optional<std::string>, the loader's error
//   kName                   the format's name ("xyz"), in messages and the codec field
//   kDecoderName            the input's name and the settings branch's name ("xyz decoder")
//   kExtensions             the extensions that play, without dots
//   kPsfVersion             the format's PSF version byte
//   kMaxFileSize            the largest file the component reads into memory
//   kKeptTags               the tags that editing keeps as they are, besides the PSF system tags: a std::array of
//                           names, such as the format's variables tag, or an empty one
//   kInputGuid, kBranchGuid, kLengthGuid, kFadeGuid, kEndlessGuid, kRenderAheadGuid

#pragma once

#include <SDK/foobar2000.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "xsflib/ascii.h"
#include "xsflib/player/playback.h"
#include "xsflib/player/render_ahead.h"
#include "xsflib/psf/psf.h"

namespace xsflib::foobar2000
{

// Advanced preferences entries, under Decoding > the format's decoder. A component defines one Settings object before
// its input factory.
template <typename Traits>
class Settings
{
public:
    using Options = player::PlaybackOptions<typename Traits::Player>;

    Settings()
    {
        instance_ = this;
    }

    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    // The component's Settings object.
    static Settings& Get()
    {
        return *instance_;
    }

    advconfig_branch_factory branch{Traits::kDecoderName, Traits::kBranchGuid, advconfig_entry::guid_branch_decoding,
                                    0};
    advconfig_integer_factory default_length{"Default length for files without a length tag (seconds)",
                                             Traits::kLengthGuid,
                                             Traits::kBranchGuid,
                                             0,
                                             Options::kDefaultLengthMs / 1000,
                                             1,
                                             24 * 3600};
    advconfig_integer_factory default_fade{"Default fade for files without a length tag (seconds)",
                                           Traits::kFadeGuid,
                                           Traits::kBranchGuid,
                                           1,
                                           Options::kDefaultFadeMs / 1000,
                                           0,
                                           3600};
    advconfig_checkbox_factory endless{"Play endlessly (ignore lengths during playback only)", Traits::kEndlessGuid,
                                       Traits::kBranchGuid, 2, false};
    advconfig_checkbox_factory render_ahead{"Render ahead during playback (quicker seeking; uses more CPU and memory)",
                                            Traits::kRenderAheadGuid, Traits::kBranchGuid, 3, true};

private:
    inline static Settings* instance_ = nullptr;
};

namespace detail
{

constexpr std::size_t kBlockFrames = 1024; // frames decoded per decode_run call

// The foobar2000 field name for a PSF tag name.
inline std::string FieldFromTag(const std::string& tag)
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

// The PSF tag name for a foobar2000 field name.
inline std::string TagFromField(const std::string& field)
{
    if (field == "album")
    {
        return "game";
    }
    if (field == "date")
    {
        return "year";
    }

    return field;
}

// PSF tags are UTF-8 when the file has a utf8 tag. Without one, text that isn't valid UTF-8 is read as the system code
// page on Windows and as Windows-1252 elsewhere. pfc converts "ANSI" text the same way outside Windows.
inline pfc::string8 TagText(const std::string& value, bool utf8)
{
    if (utf8 || pfc::is_valid_utf8(value.c_str()))
    {
        return pfc::string8(value.c_str());
    }

    return pfc::string8(pfc::stringcvt::string_utf8_from_ansi(value.c_str()));
}

} // namespace detail

// foobar2000's input for the format's files: their info, decoding and seeking, and the editing of their tags.
template <typename Traits>
class Input : public input_stubs
{
public:
    void open(service_ptr_t<file> p_filehint, const char* p_path, t_input_open_reason p_reason, abort_callback& p_abort)
    {
        path_ = p_path;
        file_ = p_filehint;
        input_open_file_helper(file_, p_path, p_reason, p_abort);
        ReadWhole(file_, data_, p_abort);

        psf::PsfFile psf;
        if (auto err = psf::ParsePsf(data_, psf, false))
        {
            throw exception_io_unsupported_format();
        }

        if (psf.version != Traits::kPsfVersion)
        {
            throw exception_io_unsupported_format();
        }

        tags_ = std::move(psf.tags);
    }

    void get_info(file_info& p_info, abort_callback& /*p_abort*/)
    {
        const bool utf8 = tags_.count("utf8") != 0;
        replaygain_info rg = p_info.get_replaygain();
        bool have_rg = false;
        for (const auto& [name, value] : tags_)
        {
            if (IsSystemTag(name))
            {
                continue;
            }

            if (rg.set_from_meta(name.c_str(), value.c_str()))
            {
                have_rg = true;
                continue;
            }

            const std::string field = detail::FieldFromTag(name);

            // A multi-line value (repeated tag lines) becomes one field value per line.
            std::size_t start = 0;
            while (true)
            {
                const std::size_t nl = value.find('\n', start);
                const std::string line = value.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                p_info.meta_add(field.c_str(), detail::TagText(line, utf8));
                if (nl == std::string::npos)
                {
                    break;
                }

                start = nl + 1;
            }
        }
        if (have_rg)
        {
            p_info.set_replaygain(rg);
        }

        const player::TrackLength length = GetLength();
        p_info.set_length(static_cast<double>(length.play_ms + length.fade_ms) / 1000.0);
        p_info.info_set_int("samplerate", Playback::kSampleRate);
        p_info.info_set_int("channels", 2);
        p_info.info_set_int("bitspersample", 16);
        p_info.info_set("encoding", "synthesized");
        p_info.info_set("codec", Traits::kName);
        if (auto it = tags_.find("volume"); it != tags_.end())
        {
            p_info.info_set((AsciiLower(Traits::kName) + "_volume").c_str(), it->second.c_str());
        }
    }

    t_filestats2 get_stats2(uint32_t f, abort_callback& a)
    {
        return file_->get_stats2_(f, a);
    }

    void decode_initialize(unsigned p_flags, abort_callback& p_abort)
    {
        // foobar2000 may restart decoding with the same input. Stop the previous worker thread first.
        ahead_.reset();
        playback_.reset();

        // The file and its libraries, read through foobar2000's file system.
        const psf::FileReader reader = [&](const std::string& path, std::vector<uint8_t>& out) -> bool
        {
            if (path == path_)
            {
                out = data_;
                return true;
            }

            try
            {
                file::ptr f;
                filesystem::g_open_read(f, path.c_str(), p_abort);
                ReadWhole(f, out, p_abort);
                return true;
            }
            catch (const exception_aborted&)
            {
                throw;
            }
            catch (const std::exception&)
            {
                return false;
            }
        };

        // A file in an archive has a path like unpack://zip|<length>|<the archive's path>|<its path in the archive>.
        // Its libraries are therefore also found after the last |.
        const bool in_archive = std::string_view(path_).starts_with("unpack://");
        typename Traits::Set set;
        if (auto err = Traits::LoadSet(path_, reader, set, in_archive ? "/\\|" : "/\\"))
        {
            pfc::throw_exception_with_message<exception_io_data>(err->c_str());
        }

        // Enable endless playback, snapshots and rendering ahead only during playback. Conversion and scanning require
        // a finite length and never seek. A player without snapshots ignores that option.
        const bool playing = (p_flags & input_flag_playback) != 0;
        Options options = DefaultOptions();
        options.endless = Settings<Traits>::Get().endless.get() && playing && !(p_flags & input_flag_no_looping);
        options.snapshots = playing;

        playback_ = std::make_unique<Playback>();
        if (!playback_->Open(std::move(set), options))
        {
            pfc::throw_exception_with_message<exception_io_data>(std::string(playback_->Error()).c_str());
        }

        if (!playback_->Start())
        {
            pfc::throw_exception_with_message<exception_io_data>(std::string(playback_->Error()).c_str());
        }

        if (playing && Settings<Traits>::Get().render_ahead.get())
        {
            ahead_ = std::make_unique<player::RenderAhead>(std::move(playback_));
        }

        buffer_.resize(2 * detail::kBlockFrames);
    }

    bool decode_run(audio_chunk& p_chunk, abort_callback& p_abort)
    {
        p_abort.check();
        const std::size_t n =
            ahead_ ? ahead_->Read(buffer_.data(), detail::kBlockFrames, [&] { return p_abort.is_aborting(); })
                   : playback_->Render(buffer_.data(), detail::kBlockFrames);
        if (n == 0)
        {
            p_abort.check();
            std::string error;
            if (ahead_)
            {
                error = ahead_->Error();
            }
            else if (playback_->GetPlayer().GetState() == Player::State::kError)
            {
                error = playback_->Error();
            }

            if (!error.empty())
            {
                pfc::throw_exception_with_message<exception_io_data>(error.c_str());
            }

            return false;
        }

        p_chunk.set_data_fixedpoint(buffer_.data(), n * 2 * sizeof(int16_t), Playback::kSampleRate, 2, 16,
                                    audio_chunk::channel_config_stereo);

        return true;
    }

    void decode_seek(double p_seconds, abort_callback& p_abort)
    {
        const t_uint64 target = audio_math::time_to_samples(p_seconds, Playback::kSampleRate);
        if (ahead_)
        {
            ahead_->Seek(target); // decode_run waits for the audio there
            return;
        }

        if (!playback_->Seek(target, [&] { return p_abort.is_aborting(); }))
        {
            p_abort.check();
            pfc::throw_exception_with_message<exception_io_data>(std::string(playback_->Error()).c_str());
        }
    }

    bool decode_can_seek()
    {
        return true;
    }

    bool decode_get_dynamic_info(file_info& /*p_out*/, double& /*p_timestamp_delta*/)
    {
        return false;
    }

    bool decode_get_dynamic_info_track(file_info& /*p_out*/, double& /*p_timestamp_delta*/)
    {
        return false;
    }

    void decode_on_idle(abort_callback& p_abort)
    {
        file_->on_idle(p_abort);
    }

    // Rewrites the tag area with the edited tags. The program and the system tags (_lib, length, fade and volume) stay
    // as they are.
    void retag(const file_info& p_info, abort_callback& p_abort)
    {
        psf::Tags tags;
        for (const auto& [name, value] : tags_)
        {
            if (IsSystemTag(name))
            {
                tags[name] = value;
            }
        }

        for (t_size i = 0; i < p_info.meta_get_count(); i++)
        {
            const std::string tag = detail::TagFromField(AsciiLower(p_info.meta_enum_name(i)));
            if (tag.empty() || IsSystemTag(tag) || tag.find('=') != std::string::npos)
            {
                continue;
            }

            std::string value;
            for (t_size j = 0; j < p_info.meta_enum_value_count(i); j++)
            {
                if (j)
                {
                    value += '\n';
                }
                value += p_info.meta_enum_value(i, j);
            }

            tags[tag] = value;
        }

        const replaygain_info rg = p_info.get_replaygain();
        replaygain_info::t_text_buffer buf;
        if (rg.format_album_gain(buf))
        {
            tags["replaygain_album_gain"] = buf;
        }

        if (rg.format_album_peak(buf))
        {
            tags["replaygain_album_peak"] = buf;
        }

        if (rg.format_track_gain(buf))
        {
            tags["replaygain_track_gain"] = buf;
        }

        if (rg.format_track_peak(buf))
        {
            tags["replaygain_track_peak"] = buf;
        }

        tags["utf8"] = "1";
        WriteTags(tags, p_abort);
    }

    void remove_tags(abort_callback& p_abort)
    {
        psf::Tags tags;
        for (const auto& [name, value] : tags_)
        {
            if (IsSystemTag(name))
            {
                tags[name] = value;
            }
        }

        WriteTags(tags, p_abort);
    }

    static bool g_is_our_content_type(const char* /*p_content_type*/)
    {
        return false;
    }

    static bool g_is_our_path(const char* /*p_path*/, const char* p_extension)
    {
        for (const char* extension : Traits::kExtensions)
        {
            if (stricmp_utf8(p_extension, extension) == 0)
            {
                return true;
            }
        }

        return false;
    }

    static const char* g_get_name()
    {
        return Traits::kDecoderName;
    }

    static GUID g_get_guid()
    {
        return Traits::kInputGuid;
    }

private:
    using Player = typename Traits::Player;
    using Playback = player::Playback<Player>;
    using Options = player::PlaybackOptions<Player>;

    // PSF tags that aren't metadata, and the format's kept tags. Editing the tags leaves these unchanged. A sequence
    // that waits for its variables would go silent if removing the tags dropped them.
    static bool IsSystemTag(const std::string& name)
    {
        for (const char* tag : Traits::kKeptTags)
        {
            if (name == tag)
            {
                return true;
            }
        }

        return name.starts_with("_lib") || name == "length" || name == "fade" || name == "volume" || name == "utf8";
    }

    static void ReadWhole(file::ptr& f, std::vector<uint8_t>& out, abort_callback& abort)
    {
        f->reopen(abort);
        const t_filesize size = f->get_size_ex(abort);
        if (size > Traits::kMaxFileSize)
        {
            pfc::throw_exception_with_message<exception_io_data>(
                (std::string(Traits::kName) + " file too large").c_str());
        }

        out.resize(static_cast<std::size_t>(size));
        if (!out.empty())
        {
            f->read_object(out.data(), out.size(), abort);
        }
    }

    // The length and fade that Advanced preferences give files without a length tag.
    static Options DefaultOptions()
    {
        Options options;
        options.default_length_ms = static_cast<long long>(Settings<Traits>::Get().default_length.get()) * 1000;
        options.default_fade_ms = static_cast<long long>(Settings<Traits>::Get().default_fade.get()) * 1000;

        return options;
    }

    // The file's tagged length and fade, or the defaults. Playback uses these values.
    player::TrackLength GetLength() const
    {
        const auto length = tags_.find("length");
        const auto fade = tags_.find("fade");
        const long long length_ms = length != tags_.end() ? psf::ParseTime(length->second) : -1;
        const long long fade_ms = fade != tags_.end() ? psf::ParseTime(fade->second) : -1;
        return player::ResolveLength(length_ms, fade_ms, DefaultOptions());
    }

    void WriteTags(const psf::Tags& tags, abort_callback& p_abort)
    {
        const auto offset = psf::TagAreaOffset(data_);
        if (!offset)
        {
            pfc::throw_exception_with_message<exception_io_data>(
                ("corrupt " + std::string(Traits::kName) + " file").c_str());
        }

        const std::string text = psf::FormatTagArea(tags);
        file_->seek(*offset, p_abort);
        file_->set_eof(p_abort);
        if (!text.empty())
        {
            file_->write_object(text.data(), text.size(), p_abort);
        }

        // Keep the copy in data_ current for a following get_info.
        data_.resize(*offset);
        data_.insert(data_.end(), text.begin(), text.end());
        tags_ = tags;
    }

    std::string path_;
    service_ptr_t<file> file_;
    std::vector<uint8_t> data_; // the file as read (its program is small for a mini file)
    psf::Tags tags_;
    std::unique_ptr<Playback> playback_;         // when decode_run renders the blocks itself
    std::unique_ptr<player::RenderAhead> ahead_; // owns the Playback while rendering ahead
    std::vector<int16_t> buffer_;
};

} // namespace xsflib::foobar2000
