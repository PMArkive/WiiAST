
void WriteWave();

void SoundCleanUp();
void SoundWrite(const char* buf,int length);
void SoundPause();
void SoundResume();
bool SoundInit(int sampleRade);
void SoundStart();
#define BUFFER_COUNT 4