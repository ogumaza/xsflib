// SPDX-License-Identifier: MIT

// Archive mode's source of sound: one sequence of a sound archive, played on a model of a console's sound library.
// Archive mode takes the same steps on every console. The source checks the set and finds the archive's files in it,
// loads the archive, starts the model and then the sequence, and then runs the model a frame at a time until the
// sequence ends.
// Some models also take snapshots. A Traits class gives each console's steps, and the words of every message:
//
//   using Set                  the format's loaded set
//   using Prepared             what Prepare finds in the set for the later steps: the descriptor, files and sound
//   using Archive              the console's sound archive
//   using Model                the console's model of its sound library
//   kKeepArchive               true to load the archive at the first start and keep it for later starts
//   Prepare(set, prepared)     checks the set: its archive descriptor, the files, any DSP code and the sound, in the
//                              console's order. Returns the error, or nothing.
//   LoadArchive(set, prepared, archive)
//                              sets `archive` from the set's files. Returns the error, or nothing. It may throw.
//   MakeModel(prepared, archive)
//                              returns the model. The model may hold `archive`.
//   StartModel(model, archive, prepared)
//                              starts the model. Returns the error, or nothing.
//   StartSequence(model, set, prepared)
//                              starts the sequence. Returns the error, or nothing.
//   RunFrame(model, out)       runs one frame, adds its samples to `out`, and returns false once the model fails
//   Busy(model)                false once the sequence has ended and nothing sounds
//
// and, for a model that takes snapshots:
//
//   using Snapshot             the model's snapshot
//   Save(model, previous)      returns a snapshot. `previous` is null or an earlier snapshot. The new snapshot may
//                              share what hasn't changed with it.
//   Restore(model, snapshot)   returns the model to `snapshot`. It may throw for a snapshot it can't return to.
//
// and, for a model that makes samples while it starts, such as an emulated DSP whose firmware boots:
//
//   Drain(model, out)          adds those samples to `out`. Start calls it once the sound has started. A snapshot taken
//                              then doesn't hold them. The player's output does.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xsflib::player
{

// A model that takes snapshots.
template <typename Traits>
concept TakesSnapshots = requires { typename Traits::Snapshot; };

template <typename Traits>
class ArchiveSource
{
public:
    using Set = typename Traits::Set;
    using Archive = typename Traits::Archive;
    using Model = typename Traits::Model;

    // Starts the set's sound on a new model, and adds the samples that the model made while it started to `out`.
    // Returns the error, or nothing. A failed check of the set leaves the last start's model as it was. The archive is
    // loaded again at each start. With kKeepArchive, the first start's archive is kept instead.
    std::optional<std::string> Start(Set& set, std::vector<int16_t>& out)
    {
        typename Traits::Prepared prepared;
        if (std::optional<std::string> error = Traits::Prepare(set, prepared))
        {
            return error;
        }

        // The model may hold the archive. The model therefore goes first.
        model_.reset();
        if (!Traits::kKeepArchive || !archive_)
        {
            archive_.reset();
            if (std::optional<std::string> error = Traits::LoadArchive(set, prepared, archive_))
            {
                archive_.reset();
                return error;
            }
        }

        model_ = Traits::MakeModel(prepared, *archive_);
        if (std::optional<std::string> error = Traits::StartModel(*model_, *archive_, prepared))
        {
            return error;
        }

        if (std::optional<std::string> error = Traits::StartSequence(*model_, set, prepared))
        {
            return error;
        }

        if constexpr (requires { Traits::Drain(*model_, out); })
        {
            Traits::Drain(*model_, out);
        }

        return std::nullopt;
    }

    // Runs one frame and adds its samples to `out`. Returns false once the model fails, or before any start.
    bool RunFrame(std::vector<int16_t>& out)
    {
        return model_ && Traits::RunFrame(*model_, out);
    }

    // False once the sequence has ended and nothing sounds, or before any start.
    bool Busy() const
    {
        return model_ && Traits::Busy(*model_);
    }

    // Takes a snapshot of the model between frames, after a start. The samples already added are no part of it. Only a
    // model that takes snapshots has these two. They're templates. A console without snapshots then never names its
    // Snapshot.
    template <typename T = Traits>
        requires TakesSnapshots<T>
    std::shared_ptr<typename T::Snapshot> Save(const typename T::Snapshot* previous) const
    {
        return std::make_shared<typename T::Snapshot>(T::Save(*model_, previous));
    }

    // Returns the model to a snapshot that it took after its current start.
    template <typename T = Traits>
        requires TakesSnapshots<T>
    void Restore(const typename T::Snapshot& snapshot)
    {
        T::Restore(*model_, snapshot);
    }

    // The model, or nullptr before any start.
    Model* GetModel()
    {
        return model_.get();
    }

private:
    // The model may hold the archive. The archive is declared first. It then goes after the model.
    std::optional<Archive> archive_;
    std::unique_ptr<Model> model_;
};

} // namespace xsflib::player
