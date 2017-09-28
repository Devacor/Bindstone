/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
| Use this code as you like.  Please do not remove this	 |
| comment (though if you do additions you may add to it).  |
| I love to hear from people who use my code so please do  |
| email me if this ends up finding its way into a project. |
| It encourages the release of more code if it is used.	 |
\**********************************************************/

#ifndef __SOUND_H__
#define __SOUND_H__

#include <SDL_mixer.h>
#include <SDL.h>
#include <string>
#include <map>
#include <list>
#include <vector>
#include <algorithm>
#include <memory>

namespace MV {
	class SoundInstance : std::enable_shared_from_this<SoundInstance> {
	public:
		SoundInstance();
		~SoundInstance();
		void setHandle(const std::string &soundName);
		std::string getHandle();

		void play();
		void stop();

		void pause();
		void resume();

		void setPosition(int angle, float distance);

		bool isPlaying();

		void donePlaying();
	private:
		int currentAngle;
		float currentDistance;
		int currentChannel;
		std::string name;
	};

	class MusicIdentity {
	public:
		std::string fileName;
		Mix_Music * musicHandle;
	};

	class SoundIdentity {
	public:
		std::string fileName;
		Mix_Chunk * soundHandle;
	};

	void AudioMusicHook();
	void AudioSoundHook(int channel);

	enum PlayListType { MUSIC_PLAYLIST, SOUND_PLAYLIST };

	class AudioPlayList : public std::enable_shared_from_this<AudioPlayList> {
	public:
		AudioPlayList();
		~AudioPlayList();

		void beginPlaying();

		void shuffleSounds(bool doShuffle) { shuffle = doShuffle; }
		void loopSounds(bool doLoop) { loop = doLoop; }
		void continuousPlay(bool doPlay) { play = doPlay; }

		bool advancePlayList();
		void performShuffle();
		void resetPlayHead();

		bool isShuffling() { return shuffle; }
		bool isLooping() { return loop; }
		bool isPlaying() { if (!play) { currentChannel = -1; }return play; }

		void addSoundFront(const std::string &songName);
		void addSoundBack(const std::string &songName);
		void removeSound(const std::string &songName);

		bool endOfList();
		std::string getCurrentSound();

		bool isEmpty() { return songLineup.empty(); }

		void clearSounds();

		void setPlayListType(PlayListType newType);

		void setPosition(int degreeAngle, float distancePercent);
		void updatePosition();
		void removePosition();
		void pause();
		void resume();
	private:
		std::vector<std::string> songLineup;
		size_t currentSong = 0;
		bool shuffle, loop, play;
		int currentChannel;
		int currentAngle;
		float currentDistance;
		PlayListType type;
		bool called;
	};

	class AudioPlayer {
	public:
		~AudioPlayer();
		static AudioPlayer* instance();

		bool initAudio();
		void copyMusicToPlayList(AudioPlayList& playList);

		bool loadMusic(const std::string &fileName, const std::string &identifier);
		bool playMusic(const std::string &identifier, int loop = 0);
		bool loadSound(const std::string &fileName, const std::string &identifier);

		//If channel is specified apply to that channel, else apply to all future sounds played.
		//distancePercent is a number from 0.0 to 1.0
		void setPosition(int degreeAngle, float distancePercent, int channel = -1);
		void removePosition(int channel = -1);

		bool playSound(const std::string &identifier, int channel = -1, int loop = 0, int ticks = -1, std::shared_ptr<AudioPlayList> playList = std::shared_ptr<AudioPlayList>());

		void setSoundDisabled(bool disableSound) { disableSounds = disableSound; }
		bool getSoundDisabled() { return disableSounds; }

		void stopSound(int channel = -1);
		void stopMusic();

		void pauseMusic();
		void resumeMusic();

		void pauseSound(int channel = -1);
		void resumeSound(int channel = -1);

		bool checkMusicPlaying(std::string *songIdentifier = NULL);
		bool checkMusicPaused(std::string *songIdentifier = NULL);
		bool setMusicPlayAtTime(double position);
		void setMusicVolume(int volume);
		//set one sound's volume
		bool setSoundVolume(int volume, const std::string &identifier);
		//set all sound's volume
		void setSoundVolume(int volume);

		//negative indicates unlimited
		void setMaxChannels(int channels) {
			maxChannels = channels;
		}

		int getMaxChannels() {
			return maxChannels;
		}
		int getAllocatedChannels() {
			return currentChannels;
		}
		int getMostRecentlyUsedChannel() {
			return channelLastPlayed;
		}
		std::string getLatestSong() {
			return currentSong;
		}

		std::shared_ptr<AudioPlayList> getMusicPlayList();
		void setMusicPlayList(std::shared_ptr<AudioPlayList> playList);

		std::shared_ptr<AudioPlayList> getSoundPlayList(int channel);
		void removeSoundPlayList(std::shared_ptr<AudioPlayList> playList);
		void removeSoundPlayList(int channel);

		void updateSoundPositions();

		void registerSoundInstance(int channel, std::shared_ptr<SoundInstance> soundReference) {
			std::map<int, std::shared_ptr<SoundInstance>>::iterator cell = soundInstances.find(channel);
			if (cell != soundInstances.end()) {
				cell->second->donePlaying();
				cell->second = soundReference;
			} else {
				soundInstances[channel] = soundReference;
			}
		}
		std::shared_ptr<SoundInstance> removeSoundInstance(int channel) {
			std::map<int, std::shared_ptr<SoundInstance>>::iterator cell = soundInstances.find(channel);
			std::shared_ptr<SoundInstance> soundReturn;
			if (cell != soundInstances.end()) {
				soundReturn = cell->second;
				soundInstances.erase(cell);
			}
			return soundReturn;
		}
		void removeSoundInstance(std::shared_ptr<SoundInstance> soundReference) {
			std::map<int, std::shared_ptr<SoundInstance>>::iterator cell;
			for (cell = soundInstances.begin(); cell != soundInstances.end() && cell->second != soundReference; ++cell) { ; }
			if (cell != soundInstances.end()) {
				soundInstances.erase(cell);
			}
		}

	protected:
		AudioPlayer();
	private:
		std::map<std::string, MusicIdentity> music;
		std::map<std::string, MusicIdentity>::iterator musicCell;
		std::map<std::string, SoundIdentity> sounds;
		std::map<std::string, SoundIdentity>::iterator soundCell;
		std::string currentSong;
		std::map<int, std::shared_ptr<AudioPlayList>> channelsWithCallbacks;
		std::map<int, std::shared_ptr<SoundInstance>> soundInstances;
		bool disableSounds;
		int audio_rate;
		int maxChannels;
		int currentChannels;
		Uint16 audio_format;
		int audio_channels;
		int audio_buffers;
		int channelLastPlayed;
		int distance;
		int angle;
		bool initialized;

		std::shared_ptr<AudioPlayList> currentMusicPlayList;

		static AudioPlayer *_instance;
	};
}

#endif
