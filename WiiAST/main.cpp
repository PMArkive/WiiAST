#define _CRT_SECURE_NO_WARNINGS
#include <iostream>  //Console i/o
#include <string>    //Console i/o parsing
#include <Windows.h> //Audio output (using mmsystem)
using namespace std;

//Wii types
using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned long;
using s8 = signed char;
using s16 = signed short;
using s32 = signed char;

//Number of buffer for autio output
#define BUFFER_COUNT 4

//Flag for wii's big endian structure
#define BIG_ENDIAN_32_STRUCT struct

//Exception type def
class bad_file:public exception{
public:
    bad_file(const char* msg):exception(msg){

    }
};

/*
[From wiibrew]
Offset	Size	Description
0x0000	4	"STRM" (0x5354524D)
0x0004	4	Size of all the BLCK chunks (size of the file minus 64)
0x0008	4	Unknown (0x00010010)
0x000C	2	Number of channels (typically 2 = stereo)
0x000E	2	Unknown (0xFFFF)
0x0010	4	Sampling rate in Hz (typically 32000)
0x0014	4	Total number of samples
0x0018	4	Loopstart position in samples/bytes?
0x001C	4	Unknown (typically same as entry 0x0014)
0x0020	4	Block size for the first chunk? (typically 0x2760)
0x0024	28	Unknown (Usually all zeros except 0x0028, which is 0x7F)

*/
BIG_ENDIAN_32_STRUCT HEADER{
    u32 magic;
    u32 dataSize;
    u32 Unk;
    //swap UnkFFFF and channelCount for 
    //convenience to use ChangeEndian32 directly
    u16 UnkFFFF;
    u16 channelCount;

    u32 sampleRate;// in Hz for one channel
    u32 sampleCount;
    u32 loopPosition;// in samples for one channel
    u32 _sampleCount;

    u32 firstBlockSize;
    u32 Zero;
    u32 Unk7F;
    u32 _Zero;

    u32 Zeros[4];

    void AssertMagic(){//Call After ChangeEndian32
        if(magic!=0x5354524D)throw bad_file("STRM magic failed");
    }

} header;

/*
[From wiibrew]
Offset
(from beginning of BLCK chunk)

        Size	Description
0x0000	4	"BLCK" (0x424C434B)
0x0004	4	Block size (typically 0x2760)
0x0008	24	Padding (zero)
0x0020	variable	PCM16 data blocks
*/
BIG_ENDIAN_32_STRUCT BLOCKHEADER{
    u32 magic;
    u32 blockSize;// in byte
    u32 Padding[6];
    //s16 PCM16DATA[ChannelCount][blockSize/2];

    void AssertMagic(){//Call After ChangeEndian32
        if(magic!=0x424C434B)throw bad_file("BLCK magic failed");
    }
};


//The .AST file
FILE* volatile curStream = nullptr;

//mmsystem waveout device
HWAVEOUT hwo = NULL;
WAVEFORMATEX wfe = { WAVE_FORMAT_PCM,2,44100,44100*4,4,16,sizeof(WAVEFORMATEX) };

//surroundAlpha for 4-channel music
volatile float surroundAlpha = 0.8f;

//Multi-thread conversation flag
//Set before call waveOutReset
volatile bool stopPlayFlag = false;

//Loop position
volatile long LoopBeginBlockOffset = 64;// in bytes
volatile u32 LoopBeginOffset = 0;//in samples for 1 channel

//The .ast file name
//Used when coverting to .wav file
string astFileName;

//Because Wii use Big Endian, difference from Windows
//We need these ChangeEndian function to parse data from the stream
void ChangeEndian32(void *pdata,u32 count){
    u32* p;
    p = (u32*)pdata;
    for(u32 i = 0; i<count; ++i){
        *p =
            ((*p&0x000000FF)<<24)|
            ((*p&0x0000FF00)<<8)|
            ((*p&0x00FF0000)>>8)|
            ((*p&0xFF000000)>>24);
        p++;
    }
}
void ChangeEndian16(void *pdata,u32 count){
    u16* p = (u16*)pdata;
    for(u32 i = 0; i<count; ++i){
        *p =
            ((*p&0x00FF)<<8)|
            ((*p&0xFF00)>>8);
        p++;
    }
}

void AssertNeof(FILE* file){
    if(feof(file)){
        throw bad_file("Unexpected end of file.");
    }
}

//Cleanup()
//Stop the music and close everything
void Cleanup(){

    if(hwo){
        stopPlayFlag = true;
        waveOutReset(hwo);
        waveOutClose(hwo);
        stopPlayFlag = false;
        hwo = NULL;
    }
    if(curStream){
        fclose(curStream);
        curStream = nullptr;
    }
}

//WriteWave()
//Read a block from the stream and send it to mmsystem
void WriteWave(){
    //The index of the buffer to write
    //Count up after each WriteWave() call
    static int swap = 0;

    //Data buffer
    //"data" for waveout
    //"buffer" for ast in
    static s16* data[BUFFER_COUNT] = { 0 },*buffer = nullptr;

    //WAVEHDR structure for waveout
    static WAVEHDR wh[BUFFER_COUNT] = { 0 };

    //The flag set when a loop begin
    bool AnotherLoop = false;

    //AST block structure
    BLOCKHEADER header;

    try{
        if(ftell(curStream)==::header.dataSize+64){
            //If we hit the end of the file, then do the loop
            cout<<"Looping..."<<endl;
            AnotherLoop = true;

            //Seek to the block in which the loop begin
            fseek(curStream,LoopBeginBlockOffset,SEEK_SET);
        }

        //Read the block header
        AssertNeof(curStream);
        fread(&header,sizeof(header),1,curStream);
        ChangeEndian32(&header,sizeof(header)/4);
        header.AssertMagic();

        //Prepare the buffer
        if(data[swap])delete[] data[swap];
        //"sampleCount" - sample count for wavout, 2 channels totally
        //"inSampleCount" - sample count read from AST, 1/2/4 channels totally
        u32 sampleCount,inSampleCount;
        inSampleCount = header.blockSize*::header.channelCount/2;
        sampleCount = header.blockSize/* *2 /2 */;
        data[swap] = new s16[sampleCount];
        buffer = new s16[inSampleCount];

        //Read the PCM16 datablock
        AssertNeof(curStream);
        fread(buffer,2,inSampleCount,curStream);
        ChangeEndian16(buffer,inSampleCount);

        //Rearrange the data from "buffer" to "data"
        for(u32 i = 0; i<sampleCount; i++){
            data[swap][i] = buffer[(
                i%2)*header.blockSize/2
                +i/2];
            if(::header.channelCount==4){
                data[swap][i] = (s16)(
                    surroundAlpha*data[swap][i]+
                    (1-surroundAlpha)*buffer[(
                        i%2+2)*header.blockSize/2
                    +i/2]
                    );
            }
        }
        delete[]buffer;

        //Send the PCM data to mmsystem
        if(AnotherLoop){
            //If it is a begining of a loop
            //send from the loop position
            wh[swap].dwBufferLength = (sampleCount-LoopBeginOffset*2)*2;
            wh[swap].lpData = (LPSTR)(data[swap]+LoopBeginOffset*2);
        }
        else{
            //Otherwise send the whole block
            wh[swap].dwBufferLength = (sampleCount)*2;
            wh[swap].lpData = (LPSTR)(data[swap]);
        }
        waveOutPrepareHeader(hwo,&wh[swap],sizeof(WAVEHDR));
        waveOutWrite(hwo,&wh[swap],sizeof(WAVEHDR));
        waveOutUnprepareHeader(hwo,&wh[swap],sizeof(WAVEHDR));

        //Counting up the buffer index
        swap++;
        if(swap==BUFFER_COUNT)swap = 0;
    }
    catch(bad_file e){
        //Oops! The AST file is out of control!
        cout<<e.what()<<endl;

        //Nuclear bomb
        Cleanup();
    }


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

void SaveToWav(FILE* wav){
    struct WAVFILEHEADER{
        u32 _RIFF;
        u32 dataSize;
        u32 _WAVE;
        u32 _fmt_;

        u32 _10H;
        u16 format;
        u16 channel;
        u32 sampleRate;
        u32 dataRate;

        u16 blockSize;
        u16 bitSize;
        u32 _data;
        u32 dataSize2;

    }wavfileheader{
        0x46464952,
        ::header.sampleCount*4+36,
        0x45564157,
        0x20746D66,

        0x00000010,
        1,2,
        ::header.sampleRate,
        ::header.sampleRate*4,

        4,16,
        0x61746164,
        ::header.sampleCount*4
    };
    fseek(wav,0,SEEK_SET);
    fwrite(&wavfileheader,sizeof(wavfileheader),1,wav);
    fseek(curStream,64,SEEK_SET);


    while(ftell(curStream)!=::header.dataSize+64){
        BLOCKHEADER header;
        s16* buffer;
        fread(&header,sizeof(header),1,curStream);
        ChangeEndian32(&header,sizeof(header)/4);
        header.AssertMagic();

        u32 SampleCount,outSampleCount;
        SampleCount = header.blockSize*::header.channelCount/2;
        outSampleCount = header.blockSize/2;
        buffer = new s16[SampleCount];
        AssertNeof(curStream);
        fread(buffer,2,SampleCount,curStream);
        ChangeEndian16(buffer,SampleCount);
        for(u32 i = 0; i<outSampleCount; i++){
            s16 left,right;
            left = buffer[i];
            right = buffer[i+outSampleCount];
            if(::header.channelCount==4){
                left = (s16)(
                    surroundAlpha*left+
                    (1-surroundAlpha)*buffer[i+2*outSampleCount]
                    );
                right = (s16)(
                    surroundAlpha*right+
                    (1-surroundAlpha)*buffer[i+3*outSampleCount]
                    );
            }
            fwrite(&left,2,1,wav);
            fwrite(&right,2,1,wav);
        }


        delete[] buffer;
    }
}

void OnExit(){
    //Cleanup();
}

int main(){

    //Init
    atexit(OnExit);

    //Welcome
    cout<<"===WiiAST===by wwylele"<<endl;
    cout<<"Command:"<<endl;
    cout<<"-a[###=0~1] - Set surroundAlpha to ###. Defualt is 0.8."<<endl;
    cout<<"-s - Stop."<<endl;
    cout<<"-q - Quit."<<endl;
    cout<<"-w - Convert to wave file and save"<<endl;
    cout<<"[File] - Open an AST file and play."<<endl;
    cout<<"===WiiAST===\n"<<endl;


    //Input looping
    while(1){
        string input;

        //Read user's input
        getline(cin,input);
        cout<<"<"<<endl;
        if(input.length()==0)continue;//user just like to press ENTER

        if(input[0]=='-'){//Recieve a special command
            switch(input[1]){
            case 'a':case 'A':
                //Change surroundAlpha
                surroundAlpha = (float)atof(input.c_str()+2);
                cout<<"Set surroundAlpha="<<surroundAlpha<<endl;
                if(header.channelCount!=4){
                    cout<<"surroundAlpha does not affect because channelCount!=4"<<endl;
                }
                else if(surroundAlpha<0.0f || surroundAlpha >1.0f){
                    cout<<"Bad surroundAlpha, but still keep playing."<<endl;
                    //Yep, that's what it means.
                    //Because I'd like to try.
                }
                break;
            case 's':case 'S':
                //Stop the music
                cout<<"Stop"<<endl;
                Cleanup();
                break;
            case 'w':
                //Convert to wav
            {
                if(!curStream){
                    cout<<"No valid .ast file is opened"<<endl;
                    break;
                }
                cout<<"Converting..."<<endl;
                //The stream cannot be used to
                //play and convert at the same time.
                //So we pause the music, badlly
                waveOutPause(hwo);
                Sleep(1000);//Wait mmsystem to react

                //Save the play position
                long prevFilePos;
                prevFilePos = ftell(curStream);

                //Create and write the wave file
                FILE* fwav;
                fwav = fopen((astFileName+".wav").c_str(),"wb");
                if(fwav){
                    try{
                        SaveToWav(fwav);
                    }
                    catch(bad_file e){
                        cout<<e.what()<<endl;
                    }
                    fclose(fwav);
                    cout<<"Done"<<endl;
                }
                else{
                    //Sorry
                    cout<<"Failed to create the wav file"<<endl;
                }

                //Resume the music
                fseek(curStream,prevFilePos,SEEK_SET);
                waveOutRestart(hwo);
                break;

            }


            case 'q':case 'Q':
                cout<<"Quit"<<endl;
                Cleanup();
                return 0;
                break;
            default:
                //No, you are not good.
                cout<<"Invalid command"<<endl;
            }

            continue;
        }

        //No special command recieved
        //Try to open the .AST file
        FILE* file;
        file = fopen(input.c_str(),"rb");
        if(!file){
            //Sorry
            cout<<"Failed to open the file."<<endl;
            continue;
        }
        Cleanup();
        curStream = file;
        astFileName = input;

        //Read the AST Header
        fread(&header,sizeof(header),1,curStream);
        ChangeEndian32(&header,sizeof(header)/4);
        try{
            header.AssertMagic();

            cout<<"===File Information==="<<endl;
            cout<<"dataSize="<<header.dataSize
                <<" bytes"<<endl;
            cout<<"channelCount="<<header.channelCount<<endl;
            cout<<"sampleRate="<<header.sampleRate
                <<" Hz per channel"<<endl;
            cout<<"sampleCount="<<header.sampleCount
                <<" per channel"<<endl;
            cout<<"*sampleTime="<<(double)header.sampleCount/header.sampleRate
                <<" seconds"<<endl;
            cout<<"loopPosition="<<header.loopPosition
                <<" samples per channel"<<endl;
            cout<<"*loopPostion="<<(double)header.loopPosition/header.sampleRate
                <<" seconds"<<endl;
            cout<<"firstBlockSize="<<header.firstBlockSize
                <<" bytes per channel"<<endl;
            cout<<"*firstBlockSize="<<(double)header.firstBlockSize/2/header.sampleRate
                <<" seconds"<<endl;

            //Find the actual loop point
            cout<<"Building loop..."<<endl;
            LoopBeginOffset = ::header.loopPosition;
            BLOCKHEADER bheader;
            while(1){
                AssertNeof(curStream);
                fread(&bheader,sizeof(bheader),1,curStream);
                ChangeEndian32(&bheader,sizeof(bheader)/4);
                header.AssertMagic();
                if(LoopBeginOffset>=bheader.blockSize/2){
                    LoopBeginOffset -= bheader.blockSize/2;
                    fseek(curStream,bheader.blockSize*::header.channelCount,SEEK_CUR);
                }
                else break;
            }
            LoopBeginBlockOffset = ftell(curStream)-32;
            fseek(curStream,64,SEEK_SET);
            cout<<"LoopBeginBlockOffset="<<LoopBeginBlockOffset<<" bytes"<<endl;
            cout<<"LoopBeginOffset="<<LoopBeginOffset<<" samples"<<endl;
        }
        catch(bad_file e){
            cout<<e.what()<<endl;
            continue;
        }
        cout<<"===Play==="<<endl;

        //Init mmsystem
        cout<<"Init..."<<endl;
        wfe.nSamplesPerSec = header.sampleRate;
        wfe.nAvgBytesPerSec = wfe.nSamplesPerSec*4;
        MMRESULT mmresult;
        mmresult = waveOutOpen(&hwo,WAVE_MAPPER,&wfe,(DWORD_PTR)waveOutProc,0,
            CALLBACK_FUNCTION);
        if(mmresult){
            cout<<"Failed to init mmsystem. Code="<<mmresult<<endl;
            continue;
        }

        //Sent the first BUFFER_COUNT blocks to mmsystem to start playing
        cout<<"Playing..."<<endl;
        for(int i = 0; i<BUFFER_COUNT; i++)WriteWave();

    }


}