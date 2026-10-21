// SPDX-License-Identifier: MIT

// Unit tests for archive mode's source of sound (xsflib/player/archive_source.h), on fake consoles: the order of the
// steps and their errors, an archive kept for later starts or loaded at each one, the archive outliving the model, the
// frames' samples, the end of the sound, and snapshots.

#include "xsflib/player/archive_source.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/testing/check.h"

namespace
{

// What happened, in order: loads, models made and gone, starts.
std::vector<std::string> g_log;

// A set: its files, by path, and the sound to play.
struct FakeSet
{
    std::map<std::string, std::vector<uint8_t>> files;
    std::optional<uint32_t> sound;
};

// A sound archive whose bytes give the number of frames its sequence lasts.
struct FakeArchive
{
    explicit FakeArchive(int frame_count) : frames(frame_count)
    {
        g_log.push_back("load archive");
    }

    FakeArchive(const FakeArchive&) = delete;
    FakeArchive& operator=(const FakeArchive&) = delete;

    ~FakeArchive()
    {
        g_log.push_back("archive gone");
    }

    int frames;
};

// A model that plays its archive's sequence: each frame adds two samples, the frame's number twice.
struct FakeModel
{
    explicit FakeModel(const FakeArchive& sound_archive) : archive(sound_archive)
    {
        g_log.push_back("make model");
    }

    ~FakeModel()
    {
        // The archive is still there: its frames can be read.
        g_log.push_back("model gone, archive of " + std::to_string(archive.frames) + " frames");
    }

    const FakeArchive& archive;
    bool started = false;
    int frame = 0;
    std::vector<int16_t> pending; // the samples made while the model started
};

// The steps that both fake consoles share. `Console` gives the words.
template <typename Console>
struct FakeTraits
{
    using Set = FakeSet;
    using Archive = FakeArchive;
    using Model = FakeModel;

    struct Prepared
    {
        uint32_t sound = 0;
    };

    static std::optional<std::string> Prepare(Set& set, Prepared& prepared)
    {
        if (!set.files.count("archive"))
        {
            return std::string(Console::kNoArchive);
        }
        if (!set.sound)
        {
            return std::string("no sequence selected");
        }

        prepared.sound = *set.sound;

        return std::nullopt;
    }

    static std::optional<std::string> LoadArchive(Set& set, const Prepared&, std::optional<Archive>& archive)
    {
        if (set.files.count("group") && set.files["group"].empty())
        {
            return std::string("the set has no file group");
        }

        archive.emplace(static_cast<int>(set.files["archive"].size()));

        return std::nullopt;
    }

    static std::unique_ptr<Model> MakeModel(const Prepared&, const Archive& archive)
    {
        return std::make_unique<Model>(archive);
    }

    static std::optional<std::string> StartModel(Model& model, const Archive&, const Prepared& prepared)
    {
        if (prepared.sound == 99)
        {
            return std::string("the DSP code didn't start");
        }

        model.started = true;

        return std::nullopt;
    }

    static std::optional<std::string> StartSequence(Model& model, const Set&, const Prepared& prepared)
    {
        g_log.push_back("start sound " + std::to_string(prepared.sound));
        if (prepared.sound >= 10)
        {
            return "sound " + std::to_string(prepared.sound) + " isn't a sequence";
        }

        model.frame = 0;

        return std::nullopt;
    }

    static bool RunFrame(Model& model, std::vector<int16_t>& out)
    {
        if (!model.started)
        {
            return false;
        }

        model.frame++;
        out.push_back(static_cast<int16_t>(model.frame));
        out.push_back(static_cast<int16_t>(model.frame));

        return true;
    }

    static bool Busy(const Model& model)
    {
        return model.started && model.frame < model.archive.frames;
    }
};

// A console that keeps its archive for later starts and takes no snapshots.
struct KeepingConsole
{
    static constexpr const char* kNoArchive = "the set has no file archive";
};

struct KeepingTraits : FakeTraits<KeepingConsole>
{
    static constexpr bool kKeepArchive = true;
};

// A console that loads its archive at each start, takes snapshots, and makes samples while it starts.
struct SnapshotConsole
{
    static constexpr const char* kNoArchive = "the sound archive (archive) is missing";
};

struct SnapshotTraits : FakeTraits<SnapshotConsole>
{
    struct Snapshot
    {
        int frame;
    };

    static constexpr bool kKeepArchive = false;

    static Snapshot Save(const Model& model, const Snapshot*)
    {
        return Snapshot{model.frame};
    }

    static void Restore(Model& model, const Snapshot& snapshot)
    {
        model.frame = snapshot.frame;
    }

    // The model makes a sample of -1 while it starts.
    static std::optional<std::string> StartModel(Model& model, const Archive& archive, const Prepared& prepared)
    {
        std::optional<std::string> error = FakeTraits<SnapshotConsole>::StartModel(model, archive, prepared);
        if (!error)
        {
            model.pending.push_back(-1);
        }

        return error;
    }

    static void Drain(Model& model, std::vector<int16_t>& out)
    {
        out.insert(out.end(), model.pending.begin(), model.pending.end());
        model.pending.clear();
    }
};

static_assert(!xsflib::player::TakesSnapshots<KeepingTraits>);
static_assert(xsflib::player::TakesSnapshots<SnapshotTraits>);

void TestErrors()
{
    xsflib::player::ArchiveSource<SnapshotTraits> source;
    std::vector<int16_t> out;
    XSFLIB_CHECK(!source.RunFrame(out) && !source.Busy() && !source.GetModel() && out.empty());

    // Each step's error comes back as the console words it.
    FakeSet set;
    XSFLIB_CHECK(source.Start(set, out) == "the sound archive (archive) is missing");
    set.files["archive"] = std::vector<uint8_t>(3);
    XSFLIB_CHECK(source.Start(set, out) == "no sequence selected");
    set.sound = 99;
    XSFLIB_CHECK(source.Start(set, out) == "the DSP code didn't start");
    set.sound = 12;
    XSFLIB_CHECK(source.Start(set, out) == "sound 12 isn't a sequence");

    // A failed check of the set leaves the last start's model as it was.
    set.sound = 1;
    XSFLIB_CHECK(!source.Start(set, out));
    FakeModel* model = source.GetModel();
    set.files.erase("archive");

    XSFLIB_CHECK(source.Start(set, out) == "the sound archive (archive) is missing");
    XSFLIB_CHECK(source.GetModel() == model);
}

void TestKeptArchive()
{
    g_log.clear();
    {
        xsflib::player::ArchiveSource<KeepingTraits> source;
        FakeSet set;
        set.files["archive"] = std::vector<uint8_t>(3);
        set.files["group"] = {};
        set.sound = 2;
        std::vector<int16_t> out = {7};

        // A group file that isn't there stops the start. Nothing is kept.
        XSFLIB_CHECK(source.Start(set, out) == "the set has no file group");
        XSFLIB_CHECK(!source.GetModel());

        set.files["group"] = {1};
        XSFLIB_CHECK(!source.Start(set, out));

        // The sequence lasts 3 frames: the samples of each frame are added to the caller's samples. This console makes
        // no samples while it starts.
        XSFLIB_CHECK(out == std::vector<int16_t>({7}));

        int frames = 0;
        while (source.Busy() && source.RunFrame(out))
        {
            frames++;
        }

        XSFLIB_CHECK(frames == 3);
        XSFLIB_CHECK(out == std::vector<int16_t>({7, 1, 1, 2, 2, 3, 3}));

        // A restart makes a new model on the archive that the first start loaded.
        XSFLIB_CHECK(!source.Start(set, out));
        XSFLIB_CHECK(source.Busy());
    }

    // One load. Each model went before the archive it played.
    XSFLIB_CHECK(g_log == std::vector<std::string>({"load archive", "make model", "start sound 2",
                                                    "model gone, archive of 3 frames", "make model", "start sound 2",
                                                    "model gone, archive of 3 frames", "archive gone"}));
}

void TestSnapshotsAndReloads()
{
    g_log.clear();
    {
        xsflib::player::ArchiveSource<SnapshotTraits> source;
        FakeSet set;
        set.files["archive"] = std::vector<uint8_t>(5);
        set.sound = 4;
        std::vector<int16_t> out;

        XSFLIB_CHECK(!source.Start(set, out));

        // The samples that the model made while it started go to the caller at once. A snapshot taken now doesn't hold
        // them.
        XSFLIB_CHECK(out == std::vector<int16_t>({-1}) && source.GetModel()->pending.empty());

        source.RunFrame(out);
        source.RunFrame(out);
        const std::shared_ptr<SnapshotTraits::Snapshot> snapshot = source.Save(nullptr);
        source.RunFrame(out);

        XSFLIB_CHECK(source.GetModel()->frame == 3 && snapshot->frame == 2);

        source.Restore(*snapshot);
        XSFLIB_CHECK(source.GetModel()->frame == 2);

        // A restart loads the archive again, after the old model has gone.
        set.files["archive"] = std::vector<uint8_t>(6);

        XSFLIB_CHECK(!source.Start(set, out));
        XSFLIB_CHECK(source.GetModel()->archive.frames == 6);
    }

    XSFLIB_CHECK(g_log == std::vector<std::string>({"load archive", "make model", "start sound 4",
                                                    "model gone, archive of 5 frames", "archive gone", "load archive",
                                                    "make model", "start sound 4", "model gone, archive of 6 frames",
                                                    "archive gone"}));
}

} // namespace

int main()
{
    TestErrors();
    TestKeptArchive();
    TestSnapshotsAndReloads();

    return xsflib::testing::Summary("archive source");
}
