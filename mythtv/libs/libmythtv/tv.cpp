#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "XJ.h"
#include "tv.h"

void *SpawnEncode(void *param)
{
    NuppelVideoRecorder *nvr = (NuppelVideoRecorder *)param;

    nvr->StartRecording();
  
    return NULL;
}

void *SpawnDecode(void *param)
{
    NuppelVideoPlayer *nvp = (NuppelVideoPlayer *)param;

    nvp->StartPlaying();

    return NULL;
}

TV::TV(void)
{
    settings = new Settings("settings.txt");

    channel = new Channel(settings->GetSetting("V4LDevice"));

    channel->Open();

    channel->SetFormat(settings->GetSetting("TVFormat"));
    channel->SetFreqTable(settings->GetSetting("FreqTable"));

    channel->SetChannelByString("3");

    channel->Close();  

    db_conn = NULL;
    nvr = NULL;
    nvp = NULL;
    rbuffer = NULL;

    ConnectDB();
}

TV::~TV(void)
{
    if (channel)
        delete channel;
    if (settings)
        delete settings;
    if (rbuffer)
        delete rbuffer;
    if (nvp)
        delete nvp;
    if (nvr)
        delete nvr;
    if (db_conn)
        DisconnectDB();
}

void TV::LiveTV(void)
{
    pthread_t encode, decode; 
  
    long long filesize = settings->GetNumSetting("BufferSize");
    filesize = filesize * 1024 * 1024 * 1024;
    long long smudge = settings->GetNumSetting("MaxBufferFill");
    smudge = smudge * 1024 * 1024; 

    osd_display_time = settings->GetNumSetting("OSDDisplayTime");
  
    rbuffer = new RingBuffer(settings->GetSetting("BufferName"), filesize, 
                             smudge);
  
    nvr = new NuppelVideoRecorder();
    nvr->SetRingBuffer(rbuffer);
    nvr->SetMotionLevels(settings->GetNumSetting("LumaFilter"), 
                         settings->GetNumSetting("ChromaFilter"));
    nvr->SetQuality(settings->GetNumSetting("Quality"));
    nvr->SetResolution(settings->GetNumSetting("Width"),
                     settings->GetNumSetting("Height"));
    nvr->SetMP3Quality(settings->GetNumSetting("MP3Quality"));
    nvr->Initialize();
 
    nvp = new NuppelVideoPlayer();
    nvp->SetRingBuffer(rbuffer);
    nvp->SetRecorder(nvr);
    nvp->SetDeinterlace((bool)settings->GetNumSetting("Deinterlace"));
    nvp->SetOSDFontName((char *)settings->GetSetting("OSDFont").c_str());
    
    pthread_create(&encode, NULL, SpawnEncode, nvr);

    while (!nvr->IsRecording())
        usleep(50);

    // evil.
    channel->SetFd(nvr->GetVideoFd());

    sleep(1);
    pthread_create(&decode, NULL, SpawnDecode, nvp);

    while (!nvp->IsPlaying())
        usleep(50);

    int keypressed;
    bool paused = false;
    float frameRate = nvp->GetFrameRate();
 
    channelqueued = false;
    channelKeys[0] = channelKeys[1] = channelKeys[2] = ' ';
    channelKeys[3] = 0;
    channelkeysstored = 0;    

    cout << endl;

    while (nvp->IsPlaying())
    {
        usleep(50);
        if ((keypressed = XJ_CheckEvents()))
        {
           switch (keypressed) {
	       case 'p': case 'P': paused = nvp->TogglePause(); break;
               case wsRight: nvp->FastForward(5); break;
               case wsLeft: nvp->Rewind(5); break;
               case wsEscape: nvp->StopPlaying(); break;
               case wsUp: ChangeChannel(true); break; 
               case wsDown: ChangeChannel(false); break;
               case wsZero: case wsOne: case wsTwo: case wsThree: case wsFour: 
               case wsFive: case wsSix: case wsSeven: case wsEight:
               case wsNine: ChannelKey(keypressed); break;
               case wsEnter: ChannelCommit(); break;
               default: break;
           }
        }
        if (paused)
        {
            fprintf(stderr, "\r Paused: %f seconds behind realtime (%f%% buffer left)", (float)(nvr->GetFramesWritten() - nvp->GetFramesPlayed()) / frameRate, (float)rbuffer->GetFreeSpace() / (float)rbuffer->GetFileSize() * 100.0);
        }
        else
        {
            fprintf(stderr, "\r                                                                      ");
            fprintf(stderr, "\r Playing: %f seconds behind realtime (%lld skipped frames)", (float)(nvr->GetFramesWritten() - nvp->GetFramesPlayed()) / frameRate, nvp->GetFramesSkipped());
        }
        if (channelqueued && !nvp->OSDVisible())
        {
            ChannelCommit();
        }
    }

    printf("exited for some reason\n");
  
    nvr->StopRecording();
  
    pthread_join(encode, NULL);
    pthread_join(decode, NULL);

    delete nvr;
    delete nvp;
    delete rbuffer;

    nvr = NULL;
    nvp = NULL;
    rbuffer = NULL;
}

void TV::ChangeChannel(bool up)
{
    nvp->Pause();
    while (!nvp->GetPause())
        usleep(5);

    nvr->Pause();
    while (!nvr->GetPause())
        usleep(5);

    rbuffer->Reset();

    if (up)
        channel->ChannelUp();
    else
        channel->ChannelDown();

    nvr->Reset();
    nvr->Unpause();

    usleep(500000);
    nvp->ResetPlaying();
    while (!nvp->ResetYet())
        usleep(5);

    string title, subtitle, desc, category, starttime, endtime;
    GetChannelInfo(channel->GetCurrent(), title, subtitle, desc, category, 
                   starttime, endtime);

    nvp->SetInfoText((char *)title.c_str(), osd_display_time);
    nvp->SetChannelText(channel->GetCurrentName(), osd_display_time);

    nvp->Unpause();

    channelqueued = false;
    channelKeys[0] = channelKeys[1] = channelKeys[2] = ' ';
    channelkeysstored = 0;
}

void TV::ChannelKey(int key)
{
    char thekey = key - 256 - 0xb0 + '0';

    if (channelkeysstored == 3)
    {
        channelKeys[0] = channelKeys[1];
        channelKeys[1] = channelKeys[2];
        channelKeys[2] = thekey;
    }
    else
    {
        channelKeys[channelkeysstored] = thekey; 
        channelkeysstored++;
    }
    channelKeys[3] = 0;
    nvp->SetChannelText(channelKeys, 2);

    channelqueued = true;
}

void TV::ChannelCommit(void)
{
    if (!channelqueued)
        return;

    ChangeChannel(channelKeys);

    channelqueued = false;
    channelKeys[0] = channelKeys[1] = channelKeys[2] = ' ';
    channelkeysstored = 0;
}

void TV::ChangeChannel(char *name)
{
    nvp->Pause();
    while (!nvp->GetPause())
        usleep(5);

    nvr->Pause();
    while (!nvr->GetPause())
        usleep(5);

    rbuffer->Reset();

    int channum = atoi(name) - 1;
    channel->SetChannel(channum);

    nvr->Reset();
    nvr->Unpause();

    usleep(500000);
    nvp->ResetPlaying();
    while (!nvp->ResetYet())
        usleep(5);

    string title, subtitle, desc, category, starttime, endtime;
    GetChannelInfo(channel->GetCurrent(), title, subtitle, desc, category,
                   starttime, endtime);

    nvp->SetInfoText((char *)title.c_str(), osd_display_time);
    nvp->SetChannelText(channel->GetCurrentName(), osd_display_time);

    nvp->Unpause();
}

void TV::GetChannelInfo(int lchannel, string &title, string &subtitle,
                        string &desc, string &category, string &starttime,
                        string &endtime)
{
    title = "";
    subtitle = "";
    desc = "";
    category = "";
    starttime = "";
    endtime = "";

    if (!db_conn)
        return;

    char curtimestr[128];
    time_t curtime;
    struct tm *loctime;

    curtime = time(NULL);
    loctime = localtime(&curtime);

    strftime(curtimestr, 128, "%Y%m%d%H%M%S", loctime);

    char query[1024];
    sprintf(query, "SELECT * FROM program WHERE channum = %d AND starttime < %s AND endtime > %s;", lchannel, curtimestr, curtimestr);

    MYSQL_RES *res_set;

    if (mysql_query(db_conn, query) == 0)
    {
        res_set = mysql_store_result(db_conn);
  
        MYSQL_ROW row;

        row = mysql_fetch_row(res_set);

        if (row != NULL)
        {
            for (unsigned int i = 0; i < mysql_num_fields(res_set); i++)
            {
                switch (i) 
                {
                    case 0: break;
                    case 1: starttime = row[i]; break;
                    case 2: endtime = row[i]; break;
                    case 3: if (row[i]) title = row[i]; break;
                    case 4: if (row[i]) subtitle = row[i]; break;
                    case 5: if (row[i]) desc = row[i]; break;
                    case 6: if (row[i]) category = row[i]; break;
                    default: break;
                }
            }
        }

        mysql_free_result(res_set);
    }

    printf("title: %s\n", title.c_str());
    printf("subtitle: %s\n", subtitle.c_str());
    printf("description: %s\n", desc.c_str());
    printf("category: %s\n", category.c_str()); 
}

void TV::ConnectDB(void)
{
    db_conn = mysql_init(NULL);
    if (!db_conn)
    {
        printf("Couldn't init mysql connection\n");
    }
    else
    {
        if (mysql_real_connect(db_conn, "localhost", "mythtv", "mythtv",
                               "mythconverg", 0, NULL, 0) == NULL)
        {
            printf("Couldn't connect to mysql database, no channel info\n");
            mysql_close(db_conn);
            db_conn = NULL;
        }
    }
}

void TV::DisconnectDB(void)
{
    if (db_conn)
        mysql_close(db_conn);
}
