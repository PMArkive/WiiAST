#include <alsa/asoundlib.h>
#include <thread>
#include <atomic>
#include <iostream>
#include "sound.h"
using namespace std;
snd_pcm_t *playback_handle=nullptr;

atomic<bool> stopFlag(false);
thread* playingThread;
void SoundCleanUp()
{
    if(playingThread)
    {
        stopFlag=true;
        playingThread->join();
    }
    delete playingThread;
    playingThread=nullptr;
    stopFlag=false;

    if(playback_handle)
    {
        snd_pcm_drain(playback_handle);
        snd_pcm_close(playback_handle);
        playback_handle=nullptr;
    }




}



void SoundWrite(const char* data,int length)
{

    int err=snd_pcm_writei (playback_handle, data, length/4);
    if(err==-EPIPE)
    {
        //cout<<"underrun!"<<endl;
        snd_pcm_prepare(playback_handle);
    }
    else if(err<0)
    {
        cout<<"snd_pcm_writei failed: "<<err<<endl;
    }

}


void SoundPause()
{
}
void SoundResume()
{
}
bool SoundInit(int sampleRate)
{

    snd_pcm_hw_params_t *hw_params;
    int err;

    if ((err = snd_pcm_open (&playback_handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0)
    {
        fprintf (stderr, "cannot open audio device %s (%s)\n",
                 "default",
                 snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0)
    {
        fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
                 snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params_any (playback_handle, hw_params)) < 0)
    {
        fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
                 snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params_set_access (playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
    {
        fprintf (stderr, "cannot set access type (%s)\n",
                 snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params_set_format (playback_handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0)
    {
        fprintf (stderr, "cannot set sample format (%s)\n",
                 snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params_set_rate (playback_handle, hw_params, sampleRate, 0)) < 0)
    {
        fprintf (stderr, "cannot set sample rate (%s)\n",
                 snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params_set_channels (playback_handle, hw_params, 2)) < 0)
    {
        fprintf (stderr, "cannot set channel count (%s)\n",
                 snd_strerror (err));
        return false;
    }
    snd_pcm_uframes_t maxbuffersize=sampleRate/10;
    if ((err = snd_pcm_hw_params_set_buffer_size_max (playback_handle, hw_params, &maxbuffersize)) < 0)
    {
        fprintf (stderr, "cannot set max buffer size (%s)\n",
                 snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params (playback_handle, hw_params)) < 0)
    {
        fprintf (stderr, "cannot set parameters (%s)\n",
                 snd_strerror (err));
        return false;
    }

    snd_pcm_hw_params_free (hw_params);

    if ((err = snd_pcm_prepare (playback_handle)) < 0)
    {
        fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
                 snd_strerror (err));
        return false;
    }
    return true;
}
void PlayingThreadEntry()
{

    while(!stopFlag)
    {


        WriteWave();
    }
}
void SoundStart()
{
    playingThread=new thread(PlayingThreadEntry);
}
