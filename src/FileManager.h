#ifndef FILEMAN_H_
#define FILEMAN_H_

#include <vector>
#include <fstream>
#include <string>

class SongDC;
class Song7K;

class FileManager
{
	static String CurrentSkin;
public:
	static void GetSongList(std::vector<SongDC*> &OutVec);
	static void GetSongList7K(std::vector<Song7K*> &OutVec);
	static std::fstream& OpenFile(String Directory);
	static String GetDirectoryPrefix();
	static String GetSkinPrefix();
	static void SetSkin(String NextSkin);
};

#endif
