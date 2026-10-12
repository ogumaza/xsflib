// SPDX-License-Identifier: MIT

// What a play test writes of a sound it plays: the output as a WAV file, and a log of the channels that sound. Two logs
// of the same sound can then be compared line for line. One can come from a game's code. The other can come from a
// model of that code.
//
// The channel log has a line for each channel that sounds after each frame, tab-separated: the frame, the state of the
// sound library's random numbers after it, the note, then each field of ChannelState. Floats have 9 significant digits.
// That's enough to give a float back exactly. A log with the sends has four more columns at the end of each line: the
// main send and the sends to aux buses A to C. A frame in which no channel sounds has a line with note 0 and the random
// numbers' state. The notes are numbered from 1 in the order they start. A note starts when its channel appears, or
// when a channel takes a new key or comes out of its release.

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <vector>

#include "xsflib/wav.h"

namespace xsflib::testing
{

// The fields that the log gives for a channel that sounds.
struct ChannelState
{
    uint32_t id = 0;                // the channel's address or place in the pool, the same while it sounds
    int track = -1;                 // the sequence track that played it, 0 to 15, or -1
    uint8_t key = 0;                // the note's key
    uint8_t original_key = 0;       // the key the wave sounds at its rate
    float velocity = 0.0f;          // the velocity's volume: velocity / 127
    uint32_t envelope = 0;          // the envelope's stage: 0 attack, 1 hold, 2 decay, 3 sustain, 4 release
    float level = 0.0f;             // its level, in 0.1 dB
    float volume = 0.0f;            // the volume the channel gave its voices this frame
    float pitch = 0.0f;             // and their pitch (a ratio)
    float pan = 0.0f;               // the pan the track gives the channel, -1 left to 1 right
    float span = 0.0f;              // and its surround pan, 0 front to 2 back
    float note_pan = 0.0f;          // the note's pan, from its region and the note: (note + region - 64) / 63
    float main_send = 0.0f;         // the main send the track gives the channel: the DB byte / 127 - 1, plus additions
    std::array<float, 3> fx_send{}; // and the sends to the aux buses A to C: D9, DA and DE / 127, plus additions
};

// Writes `samples`, stereo frames at 48 kHz, as a 16-bit WAV file.
inline void WriteWav(const std::filesystem::path& path, const std::vector<int16_t>& samples)
{
    const std::vector<uint8_t> file = WavFile(samples, 2, 48000);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
}

// A channel log, written a frame at a time (see the file comment).
class ChannelLog
{
public:
    // A log at `path`. With `sends`, each line ends with the four columns of the sends.
    ChannelLog(const std::filesystem::path& path, bool sends) : out_(path, std::ios::binary), sends_(sends)
    {
        out_
            << "frame\trandom\tnote\tchannel\ttrack\tkey\toriginal_key\tvelocity\tenvelope\tlevel\tvolume\tpitch\tpan\t"
               "span\tnote_pan"
            << (sends_ ? "\tmain_send\tfx_send_a\tfx_send_b\tfx_send_c\n" : "\n");
    }

    // The lines of frame `frame`, from 1, with the channels that sound after it in the order the sound library keeps
    // them.
    void Frame(int frame, uint32_t random, const std::vector<ChannelState>& channels)
    {
        char line[448];
        if (channels.empty())
        {
            std::snprintf(line, sizeof(line), "%d\t%08x\t0\t00000000\t-1\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0", frame, random);
            out_ << line << (sends_ ? "\t0\t0\t0\t0\n" : "\n");
        }

        std::map<uint32_t, Last> last;
        for (const ChannelState& c : channels)
        {
            // The note: the channel's last one. A new key, or a return from the release, starts another.
            const auto before = last_.find(c.id);
            const bool same = before != last_.end() && before->second.key == c.key &&
                              !(before->second.envelope == 4 && c.envelope < 4);
            const uint32_t note = same ? before->second.note : ++notes_;
            last[c.id] = {note, c.key, c.envelope};

            std::snprintf(line, sizeof(line),
                          "%d\t%08x\t%u\t%08x\t%d\t%d\t%d\t%.9g\t%u\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g", frame, random,
                          note, c.id, c.track, c.key, c.original_key, static_cast<double>(c.velocity), c.envelope,
                          static_cast<double>(c.level), static_cast<double>(c.volume), static_cast<double>(c.pitch),
                          static_cast<double>(c.pan), static_cast<double>(c.span), static_cast<double>(c.note_pan));
            out_ << line;
            if (sends_)
            {
                std::snprintf(line, sizeof(line), "\t%.9g\t%.9g\t%.9g\t%.9g", static_cast<double>(c.main_send),
                              static_cast<double>(c.fx_send[0]), static_cast<double>(c.fx_send[1]),
                              static_cast<double>(c.fx_send[2]));
                out_ << line;
            }
            out_ << '\n';
        }

        last_ = last;
    }

private:
    // What the numbering requires of a channel from the frame before.
    struct Last
    {
        uint32_t note;
        uint8_t key;
        uint32_t envelope;
    };

    std::ofstream out_;
    bool sends_;
    uint32_t notes_ = 0;
    std::map<uint32_t, Last> last_;
};

} // namespace xsflib::testing
