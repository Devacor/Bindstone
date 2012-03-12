#include "sound.h"
#include <iostream>
/*************************\
| ------AudioPlayer------ |
\*************************/

int AudioRandom(int n){ //ensure srand affects random_shuffle on all implementations
   return int(n*rand()/(RAND_MAX + 1.0));
}


AudioPlayer* AudioPlayer::_instance = NULL;

AudioPlayer* AudioPlayer::instance(){
   if(_instance == NULL){
      _instance = new AudioPlayer;
   }
   return _instance;
}

AudioPlayer::AudioPlayer(){
   audio_rate = 44100;
   audio_format = AUDIO_S16SYS;
   audio_channels = 2;
   audio_buffers = 8192;
   maxChannels = -1;
   currentChannels = 0;
   initialized = 0;
   disableSounds = 0;
   angle = 0;
   distance = 0;
}

AudioPlayer::~AudioPlayer(){
   if(initialized){
      while(checkMusicPlaying()){
         Mix_HaltMusic();
      }
      Mix_HaltChannel(-1);
      if(!music.empty()){
         for(musicCell = music.begin();musicCell != music.end();musicCell++){
            if(musicCell->second.musicHandle!=NULL){
               Mix_FreeMusic(musicCell->second.musicHandle);
            }
         }
      }
      if(!sounds.empty()){
         for(soundCell = sounds.begin();soundCell != sounds.end();soundCell++){
            if(soundCell->second.soundHandle!=NULL){
               Mix_FreeChunk(soundCell->second.soundHandle);
            }
         }
      }
      int numtimesopened, frequency, channels;
      Uint16 format;
      numtimesopened = Mix_QuerySpec(&frequency, &format, &channels);
      for(int i = 0;i < numtimesopened;i++){
         Mix_CloseAudio();
      }
      initialized = 0;
   }
   if(_instance != NULL){
      delete _instance;
   }
}

void AudioPlayer::setMusicVolume(int volume){
   Mix_VolumeMusic(volume);
}

bool AudioPlayer::setSoundVolume(int volume, const std::string &identifier){
   if (Mix_VolumeChunk(sounds[identifier].soundHandle, volume) < 0){
      return false;
   }
   return true;
}

void AudioPlayer::setSoundVolume(int volume){
   for(soundCell = sounds.begin();soundCell!= sounds.end();soundCell++){
      Mix_VolumeChunk(soundCell->second.soundHandle, volume);
   }
}

bool AudioPlayer::initAudio(){
   if(SDL_Init(SDL_INIT_AUDIO)==-1){
      std::cerr << "SDL_Init: " << SDL_GetError();
      return false;
   }
   if(Mix_OpenAudio(audio_rate, audio_format, audio_channels, audio_buffers) != 0) {
      initialized = false;
      std::cerr << "Failed to initialize audio." << Mix_GetError();
      return false;
   }
   Mix_HookMusicFinished(AudioMusicHook);
   Mix_ChannelFinished(AudioSoundHook);
   initialized = true;
   return true;
}

bool AudioPlayer::loadMusic(const std::string &fileName, const std::string &identifier){
   if(!initialized){
      return false;
   }
   if(music.find(identifier) == music.end()){
      Mix_Music *musicTmp;
      musicTmp = NULL;
      musicTmp = Mix_LoadMUS(fileName.c_str());
      if(musicTmp == NULL) 
      {
         return false;
      }
      music[identifier].musicHandle = musicTmp;
      music[identifier].fileName = fileName;
      return true;
   }
   return false;
}

bool AudioPlayer::loadSound(const std::string &fileName, const std::string &identifier){
   if(!initialized){
      return false;
   }
   if(sounds.find(identifier) == sounds.end()){
      Mix_Chunk *sound;
      sound = NULL;
      sound = Mix_LoadWAV(fileName.c_str());
      if(sound == NULL) 
      {
         return false;
      }
      sounds[identifier].soundHandle = sound;
      sounds[identifier].fileName = fileName;
      return true;
   }
   return false;
}

bool AudioPlayer::playMusic(const std::string &identifier, int loop){
   if(!initialized){
      return false;
   }
   if(music.find(identifier) != music.end()){
      if(Mix_PlayMusic(music[identifier].musicHandle, loop) == -1) 
      {
         return false;
      }
   }
   currentSong = identifier;
   return true;
}

void AudioPlayer::setPosition(int degreeAngle, float distancePercent, int channel){
   int tmpDistance = int(distancePercent * 255.0);
   if(tmpDistance > 255){tmpDistance = 255;} if(tmpDistance < 0){tmpDistance = 0;}
   int tmpAngle = degreeAngle; //no need to bound check, SDL_Mixer does.
   if(channel == -1){
      angle = tmpAngle;
      distance = tmpDistance;
   }else{
      
      Mix_SetPosition(channel, tmpAngle, tmpDistance);
   }
}

void AudioPlayer::removePosition(int channel){
   if(channel == -1){
      angle = 0;
      distance = 0;
   }else{
      Mix_SetPosition(channel, 0, 0);
   }
}

bool AudioPlayer::playSound(const std::string &identifier, int channel, int loop, int ticks, AudioPlayList* playList){
   if(!initialized || disableSounds){
      return false;
   }
   if(sounds.find(identifier) != sounds.end()){
      channelLastPlayed = Mix_PlayChannelTimed(channel, sounds[identifier].soundHandle, loop, ticks);
      if(channelLastPlayed==-1) { //probably no channels available.
         if(currentChannels<maxChannels || maxChannels < 0){
            currentChannels++;
            Mix_AllocateChannels(currentChannels);
            channelLastPlayed = Mix_PlayChannelTimed(channel, sounds[identifier].soundHandle, loop, ticks);
            if(channelLastPlayed==-1){
               currentChannels--;
               Mix_AllocateChannels(currentChannels);
               return false;
            }
         }else{
            return false;
         }
      }
      if(channelLastPlayed >= 0){
         Mix_SetPosition(channelLastPlayed, angle, distance);
         channelsWithCallbacks[channelLastPlayed] = playList;
         if(playList!=0){
            playList->setPlayListType(SOUND_PLAYLIST);
         }
      }
      return true;
   }
   return false;
}

void AudioPlayer::stopSound(int channel){
   if(channel>=0){
      if(channelsWithCallbacks.find(channel) != channelsWithCallbacks.end()){
         channelsWithCallbacks[channel]->continuousPlay(false);
      }
   }else{
      std::map<int, AudioPlayList*>::iterator cell;
      for(cell = channelsWithCallbacks.begin();cell!=channelsWithCallbacks.end();cell++){
         cell->second->continuousPlay(false);
      }
   }
   Mix_HaltChannel(channel);
}

void AudioPlayer::stopMusic(){
   Mix_HaltMusic();
}

void AudioPlayer::pauseMusic(){
   Mix_PauseMusic();
}
void AudioPlayer::resumeMusic(){
   Mix_ResumeMusic();
}

void AudioPlayer::pauseSound(int channel){
   Mix_Pause(channel);
}

void AudioPlayer::resumeSound(int channel){
   Mix_Resume(channel);
}

bool AudioPlayer::checkMusicPlaying(std::string *songIdentifier){
   int MusicStatus = Mix_PlayingMusic();
   if(songIdentifier!=NULL){
      if(MusicStatus!=0){
         *songIdentifier = currentSong;
      }
   }
   return MusicStatus!=0;
}

bool AudioPlayer::checkMusicPaused(std::string *songIdentifier){
   int MusicStatus = Mix_PausedMusic();
   if(songIdentifier!=NULL){
      if(MusicStatus!=0){
         *songIdentifier = currentSong;
      }
   }
   return MusicStatus!=0;
}

bool AudioPlayer::setMusicPlayAtTime(double position){
   Mix_RewindMusic();
   if (Mix_SetMusicPosition(position) < 0)
   {
      return false;
   }
   return true;
}

void AudioPlayer::copyMusicToPlayList(AudioPlayList& playList){
   for(musicCell = music.begin();musicCell != music.end();musicCell++){
      playList.addSoundBack(musicCell->first);
   }
}

AudioPlayList* AudioPlayer::getMusicPlayList(){
   return currentMusicPlayList;
}

void AudioPlayer::setMusicPlayList( AudioPlayList* playList ){
   currentMusicPlayList = playList;
   if(playList!=0){
      playList->setPlayListType(MUSIC_PLAYLIST);
   }
}

AudioPlayList* AudioPlayer::getSoundPlayList(int channel){
   if(channelsWithCallbacks.find(channel) != channelsWithCallbacks.end()){
      return channelsWithCallbacks[channel];
   }
   return false;
}

void AudioPlayer::removeSoundPlayList(int channel){
   if(channelsWithCallbacks.find(channel) != channelsWithCallbacks.end()){
      channelsWithCallbacks.erase(channel);
   }
}

void AudioPlayer::removeSoundPlayList( AudioPlayList* playList ){
   std::map<int, AudioPlayList*>::iterator cell, delCell;
   bool deletePrevious = false;
   for(cell = channelsWithCallbacks.begin();cell != channelsWithCallbacks.end();cell++){
      if(deletePrevious){
         channelsWithCallbacks.erase(delCell);
         deletePrevious = false;
      }
      if(cell->second == playList){
         delCell = cell;
         deletePrevious = true;
      }
   }
   if(deletePrevious){
      channelsWithCallbacks.erase(delCell);
   }
}

void AudioPlayer::updateSoundPositions(){
   std::map<int, AudioPlayList*>::iterator cell, delCell;
   for(cell = channelsWithCallbacks.begin();cell != channelsWithCallbacks.end();cell++){
      cell->second->updatePosition();
   }
}

/*************************\
| -----AudioPlayList----- |
\*************************/

AudioPlayList::AudioPlayList(){
   loop = false;
   shuffle = false;
   play = false;
   type = SOUND_PLAYLIST;
   currentChannel = -1;
   currentAngle = 0;
   currentDistance = 0;
   called = 0;
}

AudioPlayList::~AudioPlayList(){
   AudioPlayer* activePlayList = AudioPlayer::instance();
   if(type == MUSIC_PLAYLIST){
      if(activePlayList->getMusicPlayList() == this){
         activePlayList->setMusicPlayList(0);
      }
   }else if(type == SOUND_PLAYLIST){
      activePlayList->removeSoundPlayList(this);
   }
}

void AudioPlayList::addSoundBack(const std::string &songName){
   songLineup.push_back(songName);
}

void AudioPlayList::addSoundFront(const std::string &songName){
   songLineup.push_front(songName);
}

void AudioPlayList::removeSound(const std::string &songName){
   std::list<std::string>::iterator cell;
   for(cell = songLineup.begin();cell!=songLineup.end() && (*cell) != songName;cell++){;}
   if(cell != songLineup.end()){
      songLineup.erase(cell);
   }
}

bool AudioPlayList::endOfList(){
   return currentSong == songLineup.end();
}

bool AudioPlayList::advancePlayList(){
   if(currentSong != songLineup.end()){
      currentSong++;
   }
   if(currentSong == songLineup.end() && loop){
      if(shuffle){
         performShuffle();
      }
      currentSong = songLineup.begin();
   }
   if(currentSong == songLineup.end()){
      currentChannel = -1;
   }
   return currentSong != songLineup.end();
}

std::string AudioPlayList::getCurrentSound(){
   return (*currentSong);
}

void AudioPlayList::clearSounds(){
   songLineup.clear();
}

void AudioPlayList::performShuffle(){
   std::vector<std::string> TmpSortContainer;
   std::list<std::string>::iterator cell;
   std::vector<std::string>::iterator cell2;
   for(cell = songLineup.begin();cell!=songLineup.end();cell++){
      TmpSortContainer.push_back(*cell);
   }
   std::random_shuffle(TmpSortContainer.begin(), TmpSortContainer.end(), AudioRandom);
   songLineup.clear();
   for(cell2 = TmpSortContainer.begin();cell2!=TmpSortContainer.end();cell2++){
      songLineup.push_back(*cell2);
   }
   resetPlayHead();
}

void AudioPlayList::resetPlayHead(){
   currentSong = songLineup.begin();
}

void AudioPlayList::beginPlaying(){
   if(type == MUSIC_PLAYLIST){
      AudioPlayer::instance()->playMusic(getCurrentSound());
   }else if(type == SOUND_PLAYLIST){
      AudioPlayer::instance()->playSound(getCurrentSound(), -1, 0, -1, this);
      currentChannel = AudioPlayer::instance()->getMostRecentlyUsedChannel();
   }
}

void AudioPlayList::resume(){
   if(type == MUSIC_PLAYLIST){
      if(AudioPlayer::instance()->getMusicPlayList() == this){
         AudioPlayer::instance()->resumeMusic();
      }
   }else if(type == SOUND_PLAYLIST){
      if(currentChannel >= 0){
         AudioPlayer::instance()->resumeSound(currentChannel);
      }
   }
}

void AudioPlayList::pause(){
   if(type == MUSIC_PLAYLIST){
      if(AudioPlayer::instance()->getMusicPlayList() == this){
         AudioPlayer::instance()->pauseMusic();
      }
   }else if(type == SOUND_PLAYLIST){
      if(currentChannel >= 0){
         AudioPlayer::instance()->pauseSound(currentChannel);
      }
   }
}

void AudioPlayList::removePosition(){
   currentAngle = 0;
   currentDistance = 0;
}

void AudioPlayList::setPosition( int degreeAngle, float distancePercent ){
   currentAngle = degreeAngle;
   currentDistance = distancePercent;
   if(currentChannel >= 0 && currentDistance > 0.0f){
      AudioPlayer::instance()->setPosition(currentAngle, currentDistance, currentChannel);
   }
}

void AudioPlayList::setPlayListType( PlayListType newType ){
   type = newType;
}

void AudioPlayList::updatePosition(){
   if(currentChannel >= 0){
      Mix_SetPosition(currentChannel, currentAngle, (Uint8)currentDistance);
   }
}

/*************************\
| -----SoundInstance----- |
\*************************/

void SoundInstance::setHandle( const std::string &soundName ){
   name = soundName;
}

std::string SoundInstance::getHandle(){
   return name;
}

void SoundInstance::play(){
   AudioPlayer::instance()->playSound(name);
   currentChannel = AudioPlayer::instance()->getMostRecentlyUsedChannel();
   AudioPlayer::instance()->registerSoundInstance(currentChannel, this);
   if(isPlaying()){
      AudioPlayer::instance()->setPosition(currentAngle, currentDistance, currentChannel);
   }
}

void SoundInstance::stop(){
   if(isPlaying()){
      AudioPlayer::instance()->stopSound(currentChannel);
   }
}

void SoundInstance::pause(){
   if(isPlaying()){
      AudioPlayer::instance()->pauseSound(currentChannel);
   }
}

void SoundInstance::resume(){
   if(isPlaying()){
      AudioPlayer::instance()->resumeSound(currentChannel);
   }
}

void SoundInstance::setPosition( int angle, float distance ){
   currentAngle = angle; currentDistance = distance;
   if(isPlaying()){
      AudioPlayer::instance()->setPosition(currentAngle, currentDistance, currentChannel);
   }
}

bool SoundInstance::isPlaying(){
   return currentChannel != -1;
}

void SoundInstance::donePlaying(){
   currentChannel = -1;
}

//Called upon the completion of a music file playing (or being stopped)
void AudioMusicHook(){
   static AudioPlayer *player = AudioPlayer::instance();
   AudioPlayList *activePlayList = player->getMusicPlayList();
   if(activePlayList !=0){
      if(activePlayList->isPlaying()){
         if(activePlayList->advancePlayList()){
            activePlayList->beginPlaying();
         }
      }
   }
}

//Called upon the completion of a sound file playing (or being stopped)
void AudioSoundHook(int channel){
   static AudioPlayer *player = AudioPlayer::instance();
   AudioPlayList *activePlayList = player->getSoundPlayList(channel);
   if(activePlayList!=0){
      player->removeSoundPlayList(channel);
      if(activePlayList->isPlaying()){
         if(activePlayList->advancePlayList()){
            activePlayList->beginPlaying();
         }
      }
   }else{
      SoundInstance *soundInstance = player->removeSoundInstance(channel);
      if(soundInstance != 0){
         soundInstance->donePlaying();
      }
   }
}
