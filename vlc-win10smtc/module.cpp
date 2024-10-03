#include "msvc-compat/poll.h"
#include "msvc-compat/types.h"

#include <vlc_common.h>
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_playlist.h>
#include <vlc_input.h>
#include <libvlc.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <regex>
#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")
#define DEFAULT_THUMBNAIL_URI L"https://upload.wikimedia.org/wikipedia/commons/3/38/VLC_icon.png"

// uh
using namespace winrt::Windows::Media;


struct intf_sys_t
{
    intf_sys_t(const intf_sys_t&) = delete;
    void operator=(const intf_sys_t&) = delete;

    explicit intf_sys_t(intf_thread_t* intf) :
        mediaPlayer{ nullptr },
        defaultArt{ nullptr },
        intf{ intf },
        playlist{ pl_Get(intf) },
        input{ nullptr },
        advertise{ false },
        metadata_advertised{ false }
    {
    }

    void InitializeMediaPlayer()
    {
        winrt::init_apartment();

        mediaPlayer = Playback::MediaPlayer();
        mediaPlayer.CommandManager().IsEnabled(false);

        SMTC().ButtonPressed(
            [this](SystemMediaTransportControls sender, SystemMediaTransportControlsButtonPressedEventArgs args) {
                playlist_Lock(playlist);

                switch (args.Button()) {
                case SystemMediaTransportControlsButton::Play:
                    playlist_Play(playlist);
                    break;

                case SystemMediaTransportControlsButton::Pause:
                    playlist_Pause(playlist);
                    break;

                case SystemMediaTransportControlsButton::Stop:
                    playlist_Stop(playlist);
                    break;

                case SystemMediaTransportControlsButton::Next:
                    playlist_Next(playlist);
                    break;

                case SystemMediaTransportControlsButton::Previous:
                    playlist_Prev(playlist);
                    break;
                }

                playlist_Unlock(playlist);
            }
        );

        SMTC().IsPlayEnabled(true);
        SMTC().IsPauseEnabled(true);
        SMTC().IsStopEnabled(true);
        SMTC().IsPreviousEnabled(true);
        SMTC().IsNextEnabled(true);

        SMTC().PlaybackStatus(MediaPlaybackStatus::Closed);
        SMTC().IsEnabled(true);

        winrt::Windows::Foundation::Uri uri{ DEFAULT_THUMBNAIL_URI };
        defaultArt = winrt::Windows::Storage::Streams::RandomAccessStreamReference::CreateFromUri(uri);
        
        Disp().Thumbnail(defaultArt);
        Disp().Type(MediaPlaybackType::Music);
        Disp().Update();
    }

    void UninitializeMediaPlayer()
    {
        mediaPlayer = Playback::MediaPlayer(nullptr);
        winrt::uninit_apartment();
    }

    void AdvertiseState()
    {
        static_assert((int)MediaPlaybackStatus::Closed == 0, "Treat default case explicitely");

        static std::unordered_map<input_state_e, MediaPlaybackStatus> map = {
            {OPENING_S, MediaPlaybackStatus::Changing},
            {PLAYING_S, MediaPlaybackStatus::Playing},
            {PAUSE_S, MediaPlaybackStatus::Paused},
            {END_S, MediaPlaybackStatus::Stopped}
        };
        // Default/implicit case: set playback status to `Closed`

        SMTC().PlaybackStatus(map[input_state]);
        Disp().Update();
    }

    void ReadAndAdvertiseMetadata()
    {
        if (!input)
            return;

        input_item_t* item = input_GetItem(input);
        winrt::hstring title, artist, album, thumburi, path;
        char pathbuff[MAX_PATH];
        std::string pathstr;

        auto to_hstring = [](char* buf, winrt::hstring def) {
            winrt::hstring ret;

            if (buf) {
                ret = winrt::to_hstring(buf);
                libvlc_free(buf);
            }
            else {
                ret = def;
            }

            return ret;
        };

        title = to_hstring(input_item_GetTitleFbName(item), L"Unknown Title");
        artist = to_hstring(input_item_GetArtist(item), L"Unknown Artist");
        album = to_hstring(input_item_GetAlbum(item), L"Unknown Album");

        Disp().MusicProperties().Title(title);
        Disp().MusicProperties().Artist(artist);
        Disp().MusicProperties().AlbumTitle(album);

        // TODO: use artwork provided by ID3tag (if exists)
        //input_attachment_t* p_attachment = input_GetAttachment(p_input, name);
        thumburi = to_hstring(input_item_GetArtworkURL(item), L"");
        int pathlen = 0;
        unsigned long pathlen2 = ARRAYSIZE(pathbuff);
        
        if (thumburi == L"") { 
            thumburi = DEFAULT_THUMBNAIL_URI; 
            winrt::Windows::Foundation::Uri uri{ thumburi };
            winrt::Windows::Storage::Streams::RandomAccessStreamReference thumbnail = winrt::Windows::Storage::Streams::RandomAccessStreamReference::CreateFromUri(uri);
            Disp().Thumbnail(thumbnail);
        }
        else {
            winrt::Windows::Foundation::Uri uri{ thumburi };
            PathCreateFromUrlA(winrt::to_string(thumburi).c_str(), pathbuff, &pathlen2, NULL);
            std::string str(pathbuff, pathlen2);

            path = winrt::to_hstring(str);
            pathlen = path.size();

            // Uncomment to test crashes, and use Casterlabs Caffinated WMC tool to check outputs
            //thumburi = DEFAULT_THUMBNAIL_URI;
            //uri = winrt::Windows::Foundation::Uri::Uri(thumburi);
            //winrt::Windows::Storage::Streams::RandomAccessStreamReference thumbnail = winrt::Windows::Storage::Streams::RandomAccessStreamReference::CreateFromUri(uri);
            //Disp().MusicProperties().AlbumArtist(path + winrt::to_hstring(pathlen)); // set this to check the path being used
            
            // Comment when testing with above
            winrt::Windows::Storage::Streams::IRandomAccessStream storagefile = winrt::Windows::Storage::Streams::FileRandomAccessStream::OpenAsync(path, winrt::Windows::Storage::FileAccessMode::Read).get();
            winrt::Windows::Storage::Streams::RandomAccessStreamReference thumbnail = winrt::Windows::Storage::Streams::RandomAccessStreamReference::CreateFromStream(storagefile);
            
            Disp().Thumbnail(thumbnail);
        }

        Disp().Update();
    }

    SystemMediaTransportControls SMTC() {
        return mediaPlayer.SystemMediaTransportControls();
    }

    SystemMediaTransportControlsDisplayUpdater Disp() {
        return SMTC().DisplayUpdater();
    }

    Playback::MediaPlayer mediaPlayer;
    winrt::Windows::Storage::Streams::RandomAccessStreamReference defaultArt;

    intf_thread_t* intf;
    playlist_t* playlist;
    input_thread_t* input;
    input_state_e input_state;
    vlc_thread_t thread;
    vlc_mutex_t lock;
    vlc_cond_t wait;

    bool advertise;
    bool metadata_advertised; // was the last song advertised to Windows?
};


int InputEvent(vlc_object_t* object, char const* cmd,
    vlc_value_t oldval, vlc_value_t newval, void* data)
{
    VLC_UNUSED(cmd);
    VLC_UNUSED(oldval);

    intf_thread_t* intf = (intf_thread_t*)data;
    intf_sys_t* sys = intf->p_sys;
    input_thread_t* input = (input_thread_t*)object;

    if (newval.i_int == INPUT_EVENT_STATE) {
        input_state_e state = (input_state_e)var_GetInteger(input, "state");

        // send update to winrt thread
        vlc_mutex_lock(&sys->lock);
        sys->advertise = true;
        sys->input_state = state;
        vlc_cond_signal(&sys->wait);
        vlc_mutex_unlock(&sys->lock);
    }
    else if (newval.i_int == INPUT_EVENT_DEAD) {
        assert(sys->input);
        vlc_object_release(sys->input);
        sys->input = nullptr;
    }

    return VLC_SUCCESS;
}

int PlaylistEvent(vlc_object_t* object, char const* cmd,
    vlc_value_t oldval, vlc_value_t newval, void* data)
{
    VLC_UNUSED(object); VLC_UNUSED(cmd); VLC_UNUSED(oldval);

    intf_thread_t* intf = (intf_thread_t*)data;
    intf_sys_t* sys = intf->p_sys;
    input_thread_t* input = (input_thread_t*)newval.p_address;

    if (input == nullptr)
        return VLC_SUCCESS;

    sys->metadata_advertised = false; // new song, mark it as unadvertised
    sys->input = (input_thread_t*)vlc_object_hold(input);
    var_AddCallback(input, "intf-event", InputEvent, intf);

    return VLC_SUCCESS;
}

void* Thread(void* handle)
{
    intf_thread_t* intf = (intf_thread_t*)handle;
    intf_sys_t* sys = intf->p_sys;
    int canc;

    sys->InitializeMediaPlayer();
    vlc_cleanup_push(
        [](void* sys) {
            ((intf_sys_t*)sys)->UninitializeMediaPlayer();
        },
        sys
    );

    while (1) {
        vlc_mutex_lock(&sys->lock);
        mutex_cleanup_push(&sys->lock);

        while (!sys->advertise)
            vlc_cond_wait(&sys->wait, &sys->lock);

        canc = vlc_savecancel();

        sys->AdvertiseState();
        if (sys->input_state >= PLAYING_S && !sys->metadata_advertised) {
            sys->ReadAndAdvertiseMetadata();
            sys->metadata_advertised = true;
        }
        sys->advertise = false;

        vlc_restorecancel(canc);

        vlc_cleanup_pop();
        vlc_mutex_unlock(&sys->lock);
    }

    vlc_cleanup_pop();
    sys->UninitializeMediaPlayer(); // irrelevant; control flow shouldn't get here unless some UB occurs
    return nullptr;
}

int Open(vlc_object_t* object)
{
    intf_thread_t* intf = (intf_thread_t*)object;
    intf_sys_t* sys = new intf_sys_t(intf);

    intf->p_sys = sys;

    if (!sys)
        return VLC_EGENERIC;

    vlc_mutex_init(&sys->lock);
    vlc_cond_init(&sys->wait);

    if (vlc_clone(&sys->thread, Thread, intf, VLC_THREAD_PRIORITY_LOW)) {
        vlc_mutex_destroy(&sys->lock);
        vlc_cond_destroy(&sys->wait);
        delete sys;
        return VLC_EGENERIC;
    }

    var_AddCallback(sys->playlist, "input-current", PlaylistEvent, intf);
    return VLC_SUCCESS;
}

void Close(vlc_object_t* object)
{
    intf_thread_t* intf = (intf_thread_t*)object;
    intf_sys_t* sys = intf->p_sys;

    assert(!sys->input);

    vlc_cancel(sys->thread);
    vlc_join(sys->thread, nullptr);
    vlc_mutex_destroy(&sys->lock);
    vlc_cond_destroy(&sys->wait);

    var_DelCallback(sys->playlist, "input-current", PlaylistEvent, intf);
    delete intf->p_sys;
}

vlc_module_begin()
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_CONTROL)
    set_shortname("win10smtc")
    set_description("Windows 10 SMTC integration")
    set_capability("interface", 0)
    set_callbacks(Open, Close)
vlc_module_end()
