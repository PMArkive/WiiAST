#ifdef WIN32
#include "sound.h"
#include <Windows.h> //Audio output (using mmsystem)
#include <atomic>
#include <iostream>
using namespace std;

#define BUFFER_COUNT 4

//mmsystem waveout device
HWAVEOUT hwo = NULL;
WAVEFORMATEX wfe = {WAVE_FORMAT_PCM,2,44100,44100*4,4,16,sizeof(WAVEFORMATEX)};
atomic<bool> stopPlayFlag(false);
void SoundCleanUp(){
    if(hwo){
        stopPlayFlag = true;
        waveOutReset(hwo);
        waveOutClose(hwo);
        stopPlayFlag = false;
        hwo = NULL;
    }
}

void SoundWrite(const char* buf,int length){
    static int swap = 0;
    static char* data[BUFFER_COUNT] = {0};
    static WAVEHDR wh[BUFFER_COUNT] = {0};
    if(data[swap])delete[] data[swap];
    data[swap] = new char[length];
    memcpy(data[swap],buf,length);
    wh[swap].dwBufferLength = length;
    wh[swap].lpData = data[swap];
    waveOutPrepareHeader(hwo,&wh[swap],sizeof(WAVEHDR));
    waveOutWrite(hwo,&wh[swap],sizeof(WAVEHDR));
    waveOutUnprepareHeader(hwo,&wh[swap],sizeof(WAVEHDR));
    ++swap;
    if(swap==BUFFER_COUNT)swap=0;
}

//Callback Proc for mmsystem
void CALLBACK waveOutProc(
    HWAVEOUT hwo,
    UINT uMsg,
    DWORD_PTR dwInstance,
    DWORD_PTR dwParam1,
    DWORD_PTR dwParam2
    ){
    if(uMsg==WOM_DONE && !stopPlayFlag){
        //Send another block when a block is done
        WriteWave();
    }
}

void SoundPause(){
    waveOutPause(hwo);
}
void SoundResume(){
    waveOutRestart(hwo);
}
bool SoundInit(int sampleRade){
    cout<<"Init..."<<endl;
    wfe.nSamplesPerSec = sampleRade;
    wfe.nAvgBytesPerSec = wfe.nSamplesPerSec*4;
    MMRESULT mmresult;
    mmresult = waveOutOpen(&hwo,WAVE_MAPPER,&wfe,(DWORD_PTR)waveOutProc,0,
        CALLBACK_FUNCTION);
    if(mmresult){
        cout<<"Failed to init mmsystem. Code="<<mmresult<<endl;
        return false;
    }
    return true;
}
void SoundStart(){
    //Sent the first BUFFER_COUNT blocks to mmsystem to start playing
    for(int i = 0; i<BUFFER_COUNT; i++)WriteWave();
}
#endif