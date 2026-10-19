// SPDX-License-Identifier: MIT

// Tests the play tools' frame (xsflib/cli/play.h) with a stand-in player: a render to WAV, --info, a library among the
// inputs, a folder, and bad arguments. The checks look at exit codes and the files written. DescribeChunks describes a
// set of memory chunks and files.

#include "xsflib/cli/play.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "xsflib/psf/psf.h"
#include "xsflib/testing/check.h"

using xsflib::testing::Check;
namespace fs = std::filesystem;

namespace
{

// A set of memory chunks and files, for DescribeChunks.
struct ChunkSet
{
    std::vector<int> memory{1, 2, 3};
    std::vector<int> files{4, 5};
};

// The files that a stand-in set loads.
struct StandInSet
{
    std::vector<std::string> sources;
};

// A player that plays one second of silence from any file it can read.
class StandInPlayer
{
public:
    enum class State
    {
        kIdle,
        kPlaying,
        kFinished,
        kError,
    };

    static constexpr int kSampleRate = 1000;

    bool Load(const std::string& path, const xsflib::psf::FileReader& reader)
    {
        std::vector<uint8_t> data;
        if (!reader(path, data))
        {
            error_ = path + ": can't read the file";
            return false;
        }

        set_.sources = {path};
        tags_["length"] = "1";

        return true;
    }

    bool Start()
    {
        state_ = State::kPlaying;

        return true;
    }

    std::size_t Render(int16_t* out, std::size_t frames)
    {
        std::fill(out, out + 2 * frames, int16_t{0});

        return frames;
    }

    long long LengthMs() const
    {
        return 1000;
    }

    long long FadeMs() const
    {
        return 0;
    }

    double Volume() const
    {
        return 1.0;
    }

    State GetState() const
    {
        return state_;
    }

    uint64_t FinishedFrame() const
    {
        return 0;
    }

    const xsflib::psf::Tags& GetTags() const
    {
        return tags_;
    }

    const std::string& Error() const
    {
        return error_;
    }

    const StandInSet& GetLoadedSet() const
    {
        return set_;
    }

private:
    State state_ = State::kIdle;
    StandInSet set_;
    xsflib::psf::Tags tags_;
    std::string error_;
};

// Runs the stand-in's play tool with `args` and returns its exit code.
int RunWith(std::vector<std::string> args)
{
    xsflib::cli::PlayTool<StandInPlayer> tool;
    tool.name = "testplay";
    tool.version = "1.0.0";
    tool.description = "Renders test files.\n";
    tool.library_extension = ".testlib";
    tool.mini_extension = ".minitest";
    tool.a_file = "a test file";
    tool.describe_set = [](xsflib::player::Playback<StandInPlayer>&)
    {
        return std::string("a stand-in set");
    };

    args.insert(args.begin(), "testplay");
    std::vector<char*> argv;
    for (std::string& a : args)
    {
        argv.push_back(a.data());
    }

    return xsflib::cli::RunPlayTool(tool, static_cast<int>(argv.size()), argv.data());
}

} // namespace

int main()
{
    const fs::path dir = fs::temp_directory_path() / "xsflib_play_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path mini = dir / "song.minitest";
    const fs::path library = dir / "set.testlib";
    std::ofstream(mini, std::ios::binary) << "x";
    std::ofstream(library, std::ios::binary) << "x";

    const int rendered = RunWith({mini.string()});
    const int info = RunWith({"--info", mini.string()});
    const int skipped = RunWith({library.string()});
    const int folder = RunWith({dir.string()});
    const int no_value = RunWith({"--length"});
    const int bad_time = RunWith({"--length", "x", mini.string()});
    const int missing = RunWith({(dir / "missing.minitest").string()});

    Check(rendered == 0, "a render succeeds");
    Check(fs::exists(dir / "song.wav") && fs::file_size(dir / "song.wav") == 44 + 1000 * 4, "the WAV file holds 1 s");
    Check(info == 0, "--info succeeds");
    Check(skipped == 0, "a library is skipped");
    Check(folder == 1, "a folder fails");
    Check(no_value == 2 && bad_time == 2, "bad arguments exit with 2");
    Check(missing == 1, "a missing input fails");
    Check(xsflib::cli::DescribeChunks(ChunkSet{}) == "3 memory chunks, 2 files", "the description of a set's chunks");

    fs::remove_all(dir);

    return xsflib::testing::Summary("play tool");
}
