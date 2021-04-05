#include <memory>
#include <functional>
#include <mutex>
#include <rmath.h>

#include <game/Song.h>
#include <TextAndFileUtil.h>
#include <cassert>
#include "SongList.h"

#include "SongLoader.h"

ListEntry::ListEntry() {
	Kind = Directory;
	SelectedIndex = 0;
}

SongList::SongList(SongList* Parent)
    : mParent(Parent)
	, IsInUse(false)
{
}

SongList::~SongList()
{
}

void SongList::Clear()
{
	mChildren.clear();
}

void SongList::SetInUse(bool inuse)
{
	IsInUse = inuse;
}

bool SongList::InUse()
{
	return IsInUse;
}

void SongList::ClearEmpty()
{
	for (auto it = mChildren.begin(); it != mChildren.end(); ) {
		bool increase = true;

		if (it->Kind == it->Directory) {
			auto list = std::static_pointer_cast<SongList>(it->Data);
			if (list->GetNumEntries() == 0 && !list->InUse()) {
				it = mChildren.erase(it);
				increase = false;
			}
		}

		if (increase)
			++it;
	}
}

void SongList::AddSong(std::shared_ptr<rd::Song> Song)
{
    ListEntry NewEntry;
    NewEntry.Kind = ListEntry::Song;
    NewEntry.Data = Song;

    mChildren.push_back(NewEntry);
}

void SongList::AddEntry(ListEntry entry)
{
	mChildren.push_back(entry);
}

const std::vector<ListEntry>& SongList::GetEntries()
{
	return mChildren;
}

void SongList::AddNamedDirectory(
	std::mutex &loadMutex, 
	SongLoader *Loader, 
	std::filesystem::path Dir, 
	std::string Name,
	OnLoadNotifyFunc OnSongLoaded)
{
    bool EntryWasPushed = false;
    auto* NewList = new SongList(this);

    ListEntry NewEntry;

    NewEntry.EntryName = Name;
    NewEntry.Kind = ListEntry::Directory;
    NewEntry.Data = std::shared_ptr<void>(NewList);

    std::vector<rd::Song*> Songs7K;
    std::vector<std::string> Listing;

	// boost throws with nonexisting directories
	if (!std::filesystem::exists(Dir)) return;

	for (const auto& i : std::filesystem::directory_iterator (Dir))
    {
        if (i.path() == "." || i.path() == "..") continue;

		if (!std::filesystem::is_directory(i.path())) continue;

		Loader->LoadSong7KFromDir(i, Songs7K);

        if (!Songs7K.size()) // No songs, so, time to recursively search.
        {
            if (!EntryWasPushed)
            {
                std::unique_lock<std::mutex> lock(loadMutex);
                mChildren.push_back(NewEntry);
                EntryWasPushed = true;
            }

            NewList->AddDirectory(loadMutex, Loader, i, OnSongLoaded);

            {
                std::unique_lock<std::mutex> lock(loadMutex);
                if (!NewList->GetNumEntries() && !NewList->InUse())
                {
                    if (mChildren.size())
                        mChildren.erase(mChildren.end() - 1);
                    EntryWasPushed = false;
                }
            }
        }
        else
        {
            {
                std::unique_lock<std::mutex> lock(loadMutex);

                for (auto j = Songs7K.begin();
                j != Songs7K.end();
                    ++j)
                {
                    NewList->AddSong(std::shared_ptr<rd::Song>(*j));
                }

                Songs7K.clear();
            }

            if (!EntryWasPushed)
            {
                std::unique_lock<std::mutex> lock(loadMutex);
                mChildren.push_back(NewEntry);
                EntryWasPushed = true;
            }
        }

        if (EntryWasPushed) {
            if (OnSongLoaded)
                OnSongLoaded ();
        }
    }
}

void SongList::AddDirectory(std::mutex &loadMutex, SongLoader *Loader, std::filesystem::path Dir, OnLoadNotifyFunc OnSongLoaded)
{
    AddNamedDirectory(loadMutex, Loader, Dir, Conversion::ToU8(Dir.filename().wstring()), OnSongLoaded);
}

void SongList::AddVirtualDirectory(std::string NewEntryName, rd::Song* List, int Count)
{
    auto* NewList = new SongList(this);

    ListEntry NewEntry;
    NewEntry.EntryName = NewEntryName;
    NewEntry.Kind = ListEntry::Directory;
    NewEntry.Data = std::shared_ptr <void>(NewList);

    for (int i = 0; i < Count; i++)
        NewList->AddSong(std::shared_ptr<rd::Song>(&List[Count]));

    mChildren.push_back(NewEntry);
}

// if false, it's a song
bool SongList::IsDirectory(unsigned int Entry) const
{
    if (Entry >= mChildren.size()) return true;
    return mChildren[Entry].Kind == ListEntry::Directory;
}

std::shared_ptr<SongList> SongList::GetListEntry(unsigned int Entry)
{
    assert(IsDirectory(Entry));
    return std::static_pointer_cast<SongList> (mChildren[Entry].Data);
}

std::shared_ptr<rd::Song> SongList::GetSongEntry(unsigned int Entry)
{
    if (!IsDirectory(Entry))
        return std::static_pointer_cast<rd::Song> (mChildren[Entry].Data);
    else
        return nullptr;
}

std::string SongList::GetEntryTitle(unsigned int Entry)
{
    if (Entry >= mChildren.size())
        return "";

    if (mChildren[Entry].Kind == ListEntry::Directory)
        return mChildren[Entry].EntryName;
    else
    {
        std::shared_ptr<rd::Song> Song = std::static_pointer_cast<rd::Song>(mChildren[Entry].Data);
        if (Song)
            return Song->Title;
        else
            return "<no song>";
    }
}

unsigned int SongList::GetNumEntries() const
{
    return mChildren.size();
}

bool SongList::HasParentDirectory()
{
    return mParent != nullptr;
}

SongList* SongList::GetParentDirectory()
{
    return mParent;
}

void SongList::SortByFn(std::function<bool(const ListEntry&, const ListEntry&)> fn)
{
	std::stable_sort(mChildren.begin(), mChildren.end(), [&](const ListEntry&A, const ListEntry&B)
	{
		if (A.Kind == ListEntry::Directory && B.Kind != A.Kind)
		{
			return true;
		}

		if (A.Kind != ListEntry::Directory && B.Kind != A.Kind)
		{
			return false;
		}

		if (A.Kind == B.Kind && A.Kind == ListEntry::Directory)
			return A.EntryName < B.EntryName;

		return fn(A, B);
	});
};

void SongList::SortBy(ESortCriteria criteria)
{
	switch (criteria)
	{
	case SORT_TITLE:
		SortByFn([](const ListEntry&A, const ListEntry&B)
		{
			auto a = std::static_pointer_cast<rd::Song>(A.Data);
			auto b = std::static_pointer_cast<rd::Song>(B.Data);
			return a->Title < b->Title;
		});
		break;
	case SORT_AUTHOR:
		SortByFn([](const ListEntry&A, const ListEntry&B)
		{
			auto a = std::static_pointer_cast<rd::Song>(A.Data);
			auto b = std::static_pointer_cast<rd::Song>(B.Data);
			return a->Artist < b->Artist;
		});
		break;
	case SORT_LENGTH:
		SortByFn([](const ListEntry&A, const ListEntry&B)
		{
			auto dur = [](std::shared_ptr<rd::Song> a)
			{
				auto sng = std::static_pointer_cast<rd::Song>(a);
				auto dif = sng->GetDifficulty(0);
				if (dif) return dif->Duration;
				
				return 0.0;
			};

			auto a = std::static_pointer_cast<rd::Song>(A.Data);
			auto b = std::static_pointer_cast<rd::Song>(B.Data);
			float lena = dur(a);
			float lenb = dur(b);
		
			return lena < lenb;
		});
		break;
	case SORT_MINLEVEL:
		SortByFn([](const ListEntry&A, const ListEntry&B)
		{
			auto nps = [](std::shared_ptr<rd::Song> a)
			{
				auto sng = std::static_pointer_cast<rd::Song>(a);
				long long minnps = 10000000;
				for (auto diff : sng->Difficulties) {
					minnps = std::min(minnps, diff->Level);
				}

				return minnps;
			};

			auto a = std::static_pointer_cast<rd::Song>(A.Data);
			auto b = std::static_pointer_cast<rd::Song>(B.Data);
			float npsa = nps(a);
			float npsb = nps(b);
		
			return npsa < npsb;
		});
		break;
	case SORT_MAXLEVEL:
		SortByFn([](const ListEntry&A, const ListEntry&B)
		{
			auto nps = [](std::shared_ptr<rd::Song> a)
			{
				auto sng = std::static_pointer_cast<rd::Song>(a);
				long long maxnps = -10000000;
				for (auto diff : sng->Difficulties) {
					maxnps = std::max(maxnps, diff->Level);
				}

				return maxnps;
			};

			auto a = std::static_pointer_cast<rd::Song>(A.Data);
			auto b = std::static_pointer_cast<rd::Song>(B.Data);
			float npsa = nps(a);
			float npsb = nps(b);
		
			return npsa < npsb;
		});
		break;
	default:
		break;
	}

	// recursively sort
	for (auto &&ch: mChildren)
	{
		if (ch.Kind == ListEntry::Directory)
		{
			auto list = std::static_pointer_cast<SongList>(ch.Data);
			list->SortBy(criteria);
		}
	}
}