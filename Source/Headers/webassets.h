#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void WebAssets_EnsureTrackLoaded(int trackNum);
void WebAssets_PrefetchTrack(int trackNum);
void WebAssets_EnsureSongLoaded(int songNum);

#ifdef __cplusplus
}
#endif
