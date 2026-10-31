#pragma once
// ミュージックの状態を読む AppleScript。
// GitHub Actions のビルドで osacompile にかけて、文法が正しいかを毎回確かめている（build.yml）。
// 変数名は、ミュージックの用語とぶつからないように rm で始める。
static const char* kMusicPollScript = R"APPLESCRIPT(
if application "Music" is running then
  with timeout of 2 seconds
    tell application "Music"
      set rmPlayerState to (player state as string)
      set rmPosition to "0"
      set rmTitle to ""
      set rmArtist to ""
      set rmDuration to "0"
      set rmTrackID to ""
      try
        set rmPosition to (player position as string)
      end try
      try
        set rmTrack to current track
        try
          set rmTitle to (name of rmTrack)
        end try
        try
          set rmArtist to (artist of rmTrack)
        end try
        try
          set rmDuration to ((duration of rmTrack) as string)
        end try
        try
          set rmTrackID to (persistent ID of rmTrack)
        end try
      end try
      if rmTitle is "" then
        try
          set rmTitle to (current stream title)
        end try
      end if
      return {rmPlayerState, rmPosition, rmTitle, rmArtist, rmDuration, rmTrackID}
    end tell
  end timeout
else
  return {"notrunning", "0", "", "", "0", ""}
end if
)APPLESCRIPT";
