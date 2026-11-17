#include "webassets.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <SDL3/SDL.h>
#include <string>
#include <vector>

#include "Pomme.h"

extern "C" {
#include "game.h"
}

EM_ASYNC_JS(int, Web_FetchFilesAsync, (const char* pathsCsv, const char* label), {
	const paths = UTF8ToString(pathsCsv).split('|').filter(Boolean);
	const labelText = UTF8ToString(label);

	const missing = [];
	for (const vfsPath of paths) {
		try {
			if (FS.analyzePath(vfsPath).exists) continue;
		} catch (e) {}
		missing.push(vfsPath);
	}
	if (missing.length === 0) return 1;

	if (typeof window !== 'undefined' && typeof window.__croMagShowAlert === 'function') {
		window.__croMagShowAlert(labelText || 'Loading track assets...');
	}

	try {
		await Promise.all(missing.map(async (vfsPath) => {
			const relUrl = vfsPath.startsWith('/') ? vfsPath.slice(1) : vfsPath;
			const resp = await fetch(relUrl);
			if (!resp.ok) {
				throw new Error('HTTP ' + resp.status + ' fetching ' + relUrl);
			}
			const buf = new Uint8Array(await resp.arrayBuffer());
			const slash = vfsPath.lastIndexOf('/');
			if (slash > 0) {
				FS.mkdirTree(vfsPath.slice(0, slash));
			}
			FS.writeFile(vfsPath, buf);
		}));
		return 1;
	} catch (err) {
		console.error('WebAssets fetch error:', err);
		if (typeof window !== 'undefined' && typeof window.__croMagShowFatal === 'function') {
			window.__croMagShowFatal('Failed to download track assets: ' + (err && err.message ? err.message : err));
		}
		return 0;
	}
});

static const char* kTrackTerrainBase[NUM_TRACKS] =
{
	"StoneAge_Desert",
	"StoneAge_Jungle",
	"StoneAge_Ice",

	"BronzeAge_Crete",
	"BronzeAge_China",
	"BronzeAge_Egypt",

	"IronAge_Europe",
	"IronAge_Scandinavia",
	"IronAge_Atlantis",

	"Battle_StoneHenge",
	"Battle_Aztec",
	"Battle_Coliseum",
	"Battle_Maze",
	"Battle_Celtic",
	"Battle_TarPits",
	"Battle_Spiral",
	"Battle_Ramps",
};

static const char* kTrackModelName[NUM_TRACKS] =
{
	"desert",
	"jungle",
	"ice",

	"crete",
	"china",
	"egypt",

	"europe",
	"scandinavia",
	"atlantis",

	"stonehenge",
	"aztec",
	"coliseum",
	"crete",
	"crete",
	"tarpits",
	"crete",
	"ramps",
};

static const char* kSongBaseName[MAX_SONGS] =
{
	"WinSong",      // SONG_WIN
	"DesertSong",   // SONG_DESERT
	"JungleSong",   // SONG_JUNGLE
	"ThemeSong",    // SONG_THEME
	"AtlantisSong", // SONG_ATLANTIS
	"ChinaSong",    // SONG_CHINA
	"CreteSong",    // SONG_CRETE
	"EuroSong",     // SONG_EUROPE
	"IceSong",      // SONG_ICE
	"EgyptSong",    // SONG_EGYPT
	"VikingSong",   // SONG_VIKING
};

static int GetSongForTrack(int trackNum)
{
	switch (trackNum)
	{
		case TRACK_NUM_DESERT:      return SONG_DESERT;
		case TRACK_NUM_JUNGLE:
		case TRACK_NUM_AZTEC:       return SONG_JUNGLE;
		case TRACK_NUM_ATLANTIS:
		case TRACK_NUM_TARPITS:     return SONG_ATLANTIS;
		case TRACK_NUM_CHINA:
		case TRACK_NUM_SPIRAL:      return SONG_CHINA;
		case TRACK_NUM_EGYPT:       return SONG_EGYPT;
		case TRACK_NUM_CRETE:
		case TRACK_NUM_CELTIC:
		case TRACK_NUM_COLISEUM:    return SONG_CRETE;
		case TRACK_NUM_ICE:
		case TRACK_NUM_RAMPS:       return SONG_ICE;
		case TRACK_NUM_EUROPE:
		case TRACK_NUM_STONEHENGE:  return SONG_EUROPE;
		case TRACK_NUM_SCANDINAVIA: return SONG_VIKING;
		default:                    return SONG_DESERT;
	}
}

extern "C" void WebAssets_EnsureTrackLoaded(int trackNum)
{
	if (trackNum < 0 || trackNum >= NUM_TRACKS)
		return;

	std::string csv;
	csv += "/Data/Terrain/";
	csv += kTrackTerrainBase[trackNum];
	csv += ".ter|/Data/Terrain/";
	csv += kTrackTerrainBase[trackNum];
	csv += ".ter.rsrc|";

	if (trackNum >= TRACK_NUM_STONEHENGE)
	{
		csv += "/Data/Terrain/";
		csv += kTrackTerrainBase[trackNum];
		csv += ".nav|";
	}

	csv += "/Data/Models/";
	csv += kTrackModelName[trackNum];
	csv += ".bg3d|";

	int songNum = GetSongForTrack(trackNum);
	if (songNum >= 0 && songNum < MAX_SONGS)
	{
		csv += "/Data/Audio/";
		csv += kSongBaseName[songNum];
		csv += ".ogg";
	}

	Web_FetchFilesAsync(csv.c_str(), "Downloading track assets...");
}

extern "C" void WebAssets_EnsureSongLoaded(int songNum)
{
	if (songNum < 0 || songNum >= MAX_SONGS)
		return;

	std::string path = "/Data/Audio/";
	path += kSongBaseName[songNum];
	path += ".ogg";

	Web_FetchFilesAsync(path.c_str(), "Loading music...");
}

#else

extern "C" void WebAssets_EnsureTrackLoaded(int trackNum)
{
	(void)trackNum;
}

extern "C" void WebAssets_EnsureSongLoaded(int songNum)
{
	(void)songNum;
}

#endif // __EMSCRIPTEN__
