#include <fstream>

#include "Global.h"
#include "NoteLoaderSM.h"
#include "NoteLoader.h"

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/split.hpp>


/* Stepmania uses a format with a lot of information, so we'll start with the basics*/

using namespace NoteLoaderSM;
typedef std::vector<String> SplitResult;

#define ModeType(m,keys) if(mode==#m) return keys;

int GetTracksByMode(String mode)
{
	ModeType(kb7-single, 7);
	ModeType(dance-single, 4);
	ModeType(dance-solo, 6);
	ModeType(dance-double, 8);
	ModeType(pump-single, 5);
	ModeType(pump-double, 10);
	Utility::DebugBreak();
	return 1;
}

#undef ModeType

String RemoveComments(const String Str)
{
	String Result;
	int k = 0;
	int AwatingEOL = 0;
	for (uint32 i = 0; i < Str.length()-1; i++)
	{
		if (AwatingEOL)
		{
			if (Str[i] != '\n')
				continue;
			else
			{
				AwatingEOL = 0;
				continue;
			}
		}else
		{
			if (Str[i] == '/' && Str[i+1] == '/')
			{
				AwatingEOL = true;
				continue;
			}else
			{
				Result.push_back(Str.at(i));
				k++;
			}
		}
	}
	return Result;
}

void LoadTracksSM(Song7K *Out, SongInternal::TDifficulty<TrackNote> *Difficulty, String line)
{
	String CommandContents = line.substr(line.find_first_of(":") + 1);
	SplitResult Mainline;

	/* Remove newlines and comments */
	CommandContents = RemoveComments(CommandContents);
	boost::replace_all(CommandContents, "\n", "");
	boost::replace_all(CommandContents, " ", "");

	/* Split contents */
	boost::split(Mainline, CommandContents, boost::is_any_of(":"));

	/* What we'll work with */
	String NoteString = Mainline[5];
	int Keys = GetTracksByMode(Mainline[0]);

	Difficulty->Channels = Keys;
	Difficulty->Name = Mainline[2];
	
	/* Now we should have our notes within NoteString. 
	We'll split them by measure using , as a separator.*/
	SplitResult MeasureText;
	boost::split(MeasureText, NoteString, boost::is_any_of(","));

	/* Hold data */
	float KeyStartTime[16];
	int KeyMeasure[16];
	int KeyFraction[16];
	
	/* For each measure of the song */
	for (uint32 i = 0; i < MeasureText.size(); i++) /* i = current measure */
	{
		int MeasureFractions = MeasureText[i].length() / Keys;
		SongInternal::Measure<TrackNote> Measure[16];
		
		for (int32 k = 0; k < 16; k++)
			Measure[k].Fraction = MeasureFractions;

		if (MeasureText[i].length())
		{
			/* For each fraction of the measure*/
			for (int32 m = 0; m < MeasureFractions; m++) /* m = current fraction */
			{
				double Beat = (double)i * (double)Out->MeasureLength + (double)m * (double)Out->MeasureLength / (double)MeasureFractions; /* Current beat */
				double StopsTime = StopTimeAtBeat(Difficulty->StopsTiming, Beat);
				double Time = TimeAtBeat(Difficulty->Timing, Difficulty->Offset, Beat) + StopsTime;
				/* For every track of the fraction */
				for (int k = 0; k < Keys; k++) /* k = current track */
				{
					TrackNote Note;
					Note.AssignTrack(k);
					switch (MeasureText[i].at(0))
					{
					case '1': /* Taps */
						Note.AssignSongPosition(i, m);
						Note.AssignTime(Time, 0);
						Measure[k].MeasureNotes.push_back(Note);
						break;
					case '2': /* Holds */
					case '4':
						KeyStartTime[k] = Time;
						KeyMeasure[k] = i;
						KeyFraction[k] = m;
						break;
					case '3': /* Hold releases */
						Note.AssignTime(KeyStartTime[k], Time);
						Note.AssignSongPosition(KeyMeasure[k], KeyFraction[k]);
						Measure[k].MeasureNotes.push_back(Note);
						break;
					default:
						break;
					}

					if (MeasureText[i].length())
						MeasureText[i].erase(0, 1);
				}
			}
		}

		for (int32 k = 0; k < 16; k++)
			Difficulty->Measures[k].push_back(Measure[k]);
	}

	/* 
		Through here we can make a few assumptions.
		->The measures are in order from start to finish
		->Each measure has all potential playable tracks, even if that track is empty during that measure.
		->Measures are internally ordered
	*/

	double maxTime = 0;

	for (int k = 0; k < 16; k++)
	{
		if (Difficulty->Measures[k].size())
		{
			int LastMeasure = Difficulty->Measures[k].size();
			
			maxTime = std::max(TimeAtBeat(Difficulty->Timing, Difficulty->Offset, LastMeasure * Out->MeasureLength), maxTime);
		}
	}

	Difficulty->Duration = maxTime;
}

Song7K* NoteLoaderSM::LoadObjectsFromFile(String filename, String prefix)
{
	std::ifstream filein (filename.c_str());
	Song7K *Out = new Song7K();
	SongInternal::TDifficulty<TrackNote> *Difficulty = new SongInternal::TDifficulty<TrackNote>();

	if (!filein.is_open())
	{
#ifndef NDEBUG
		std::stringstream serr;
		serr << "couldn't open \"" << filename << "\" for reading";
		throw; // std::exception(serr.str().c_str());
#else
		return NULL;
#endif
	}

	Out->SongDirectory = prefix;
	Out->UseSeparateTimingData = false;

	String line;
	while (!filein.eof())
	{
		std::getline(filein, line, ';'); 

		if (line.size() < 3)
			continue;

		String command = line.substr(line.find_first_of("#"), line.find_first_of(":") - line.find_first_of("#"));

		boost::replace_all(command, "\n", "");

#define OnCommand(x) if(command == #x || command == #x + std::string(":"))

		String CommandContents = line.substr(line.find_first_of(":") + 1);
		

		OnCommand(#TITLE)
		{
			Out->SongName = CommandContents;
		}

		OnCommand(#ARTIST)
		{
			Out->SongAuthor = CommandContents;
		}

		OnCommand(#BACKGROUND)
		{
			Out->BackgroundRelativeDir = CommandContents;
			Out->BackgroundDir = prefix + "/" + CommandContents;
		}

		OnCommand(#MUSIC)
		{
			Out->SongFilename = prefix + "/" + CommandContents;
			Out->SongRelativePath = CommandContents;
		}

		OnCommand(#OFFSET)
		{
			std::stringstream str (CommandContents);
			str >> Difficulty->Offset;
			Difficulty->Offset = -Difficulty->Offset;

			Out->LeadInTime = Difficulty->Offset < 0? abs(Difficulty->Offset) + 3 : 0;
		}

		OnCommand(#BPMS)
		{
			LoadTimingList(Out->BPMData, line);
		}

		OnCommand(#STOPS)
		{
			LoadTimingList(Out->StopsData, line);
		}

		/* Stops: TBD */

		OnCommand(#NOTES)
		{
			Difficulty->Timing = Out->BPMData;
			Difficulty->StopsTiming = Out->StopsData;

			LoadTracksSM(Out, Difficulty, line);
			Out->Difficulties.push_back(Difficulty);

			Difficulty = new SongInternal::TDifficulty<TrackNote>();
		}
	}

	delete Difficulty;

	Out->Process();
	return Out;
}
