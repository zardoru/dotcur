#include "gamecore-pch.h"
#include "fs.h"
#include "rmath.h"

#include "GameConstants.h"
#include "Song.h"
#include "TrackNote.h"
#include "Song7K.h"

#include "NoteTransformations.h"
#include <random>

namespace Game {
	namespace VSRG {
		namespace NoteTransform
		{
			void Randomize(Game::VSRG::VectorTN &Notes, int ChannelCount, bool RespectScratch, int Seed)
			{
				std::vector<int> s;

				// perform action to channel index minus scratch ones if applicable
				auto toChannels = [&](std::function<void(int)> fn) {
					for (auto i = 0; i < ChannelCount; i++)
					{
						if (i == 0 && RespectScratch)
							if (ChannelCount == 6 || ChannelCount == 8) {
								continue;
							}

						if (i == 5 && ChannelCount == 12 && RespectScratch)
							continue;

						if (i == 8 && ChannelCount == 16 && RespectScratch)
							continue;

						fn(i);
					}
				};


				// get indices of all applicable channels
				toChannels([&](int index)
				{
					s.push_back(index);
				});

				std::mt19937 mt(Seed);
				std::uniform_int_distribution<int> dev;

				// perform random
				// note: random_shuffle's limitation of only swapping with earlier entries
				// makes this preferable.
				for (size_t i = 0; i < s.size(); i++)
					std::swap(s[i], s[dev(mt) % s.size()]);

				int limit = int(ceil(s.size() / 2.0));
				int v = 0;
				// to all applicable channels swap with applicable channels
				toChannels([&](int index)
				{
					if (v <= limit) { // avoid cycles
						swap(Notes[index], Notes[s[v]]);
						v++;
					}
				});
			}

			void Mirror(Game::VSRG::VectorTN &Notes, int ChannelCount, bool RespectScratch)
			{
				int k;
				if (RespectScratch) k = 1; else k = 0;
				for (int v = ChannelCount - 1; k < ChannelCount / 2; k++, v--)
					swap(Notes[k], Notes[v]);
			}

			void MoveKeysoundsToBGM(unsigned char channels, Game::VSRG::VectorTN notes_by_channel, std::vector<AutoplaySound> &bg_ms, double drift)
			{
				for (auto k = 0; k < channels; k++)
				{
					for (auto&& n : notes_by_channel[k])
					{
						bg_ms.push_back(AutoplaySound{ float(double(n.GetStartTime()) - drift), n.GetSound() });
						n.RemoveSound();
					}
				}
			}

			void TransformToBeats(unsigned char channels,
				Game::VSRG::VectorTN notes_by_channel,
				const TimingData &BPS) {
				for (uint8_t k = 0; k < channels; k++)
				{
					for (auto m = notes_by_channel[k].begin();
					m != notes_by_channel[k].end(); ++m)
					{
						double beatStart = IntegrateToTime(BPS, m->GetDataStartTime());
						double beatEnd = IntegrateToTime(BPS, m->GetDataEndTime());
						m->GetDataStartTime() = beatStart;
						if (m->GetDataEndTime())
							m->GetDataEndTime() = beatEnd;
					}
				}
			}
		}
	}
}