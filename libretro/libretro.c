#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include "libretro.h"
#include "retrodep/retroglue.h"
#include "libretro-mapper.h"
#include "libretro-glue.h"
#include "vkbd.h"
#include "graph.h"
#include "retro_files.h"
#include "retro_strings.h"
#include "retro_disk_control.h"
#include "string/stdstring.h"
#include "file/file_path.h"
#include "uae_types.h"

#include "retrodep/WHDLoad_files.zip.c"
#include "retrodep/WHDLoad_hdf.gz.c"
#include "retrodep/WHDSaves_hdf.gz.c"
#include "retrodep/WHDLoad_prefs.gz.c"

#include "sysdeps.h"
#include "uae.h"
#include "options.h"
#include "inputdevice.h"
#include "savestate.h"
#include "custom.h"
#include "xwin.h"
#include "drawing.h"
#include "akiko.h"
#include "blkdev.h"

#ifdef VITA
#include <psp2/types.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/threadmgr.h>
#define rmdir(name) sceIoRmdir(name)
#endif

int libretro_runloop_active = 0;

extern void check_changes(int unitnum);
extern int frame_redraw_necessary;
extern int bplcon0;
extern int diwlastword_total;
extern int diwfirstword_total;
extern int interlace_seen;
extern int m68k_go(int may_quit, int resume);
int cp(const char *to, const char *from);

int defaultw = EMULATOR_DEF_WIDTH;
int defaulth = EMULATOR_DEF_HEIGHT;
int retrow = 0;
int retroh = 0;
char key_state[512];
char key_state2[512];

unsigned int opt_mapping_options_display;
unsigned int opt_video_options_display;
unsigned int opt_audio_options_display;
char opt_model[10];
bool opt_video_resolution_auto = false;
bool opt_video_vresolution_auto = false;
unsigned int opt_use_whdload = 1;
unsigned int opt_use_whdload_prefs = 0;
unsigned int opt_use_boot_hd = 0;
bool opt_shared_nvram = 0;
bool opt_statusbar_enhanced = true;
bool opt_statusbar_minimal = false;
int opt_statusbar_position = 0;
int opt_statusbar_position_old = 0;
int opt_statusbar_position_offset = 0;
unsigned int opt_vkbd_theme = 0;
unsigned int opt_vkbd_alpha = 204;
bool opt_keyrahkeypad = false;
bool opt_keyboard_pass_through = false;
bool opt_multimouse = false;
unsigned int opt_dpadmouse_speed = 4;
unsigned int opt_analogmouse = 0;
unsigned int opt_analogmouse_deadzone = 15;
float opt_analogmouse_speed = 1.0;
unsigned int opt_cd32pad_options = 0;
unsigned int opt_retropad_options = 0;

#if defined(NATMEM_OFFSET)
extern uae_u8 *natmem_offset;
extern uae_u32 natmem_size;
#endif

static char RPATH[512] = {0};
static char full_path[512] = {0};
static char *uae_argv[] = { "puae", RPATH };
static int restart_pending = 0;

unsigned short int retro_bmp[RETRO_BMP_SIZE];
extern int STATUSON;
extern int prefs_changed;

extern int turbo_fire_button;
extern unsigned int turbo_pulse;
unsigned int inputdevice_finalized = 0;
int pix_bytes = 2;
static bool pix_bytes_initialized = false;
bool filter_type_update = true;
bool fake_ntsc = false;
bool real_ntsc = false;
bool forced_video = false;
bool retro_request_av_info_update = false;
bool retro_av_info_change_timing = false;
bool retro_av_info_is_ntsc = false;
bool request_reset_drawing = false;
unsigned int request_init_custom_timer = 0;
unsigned int request_check_prefs_timer = 0;
unsigned int zoom_mode_id = 0;
unsigned int opt_zoom_mode_id = 0;
int zoomed_height;

int opt_vertical_offset = 0;
bool opt_vertical_offset_auto = true;
extern int minfirstline;
extern int retro_thisframe_first_drawn_line;
static int retro_thisframe_first_drawn_line_old = -1;
extern int retro_thisframe_last_drawn_line;
static int retro_thisframe_last_drawn_line_old = -1;
extern int thisframe_y_adjust;
static int thisframe_y_adjust_old = 0;
static int thisframe_y_adjust_update_frame_timer = 3;

int opt_horizontal_offset = 0;
bool opt_horizontal_offset_auto = true;
static int retro_max_diwlastword_hires = 824;
static int retro_max_diwlastword = 824;
extern int retro_min_diwstart;
static int retro_min_diwstart_old = -1;
extern int retro_max_diwstop;
static int retro_max_diwstop_old = -1;
extern int visible_left_border;
static int visible_left_border_old = 0;
static int visible_left_border_update_frame_timer = 3;

unsigned int video_config = 0;
unsigned int video_config_old = 0;
unsigned int video_config_aspect = 0;
unsigned int video_config_geometry = 0;
unsigned int video_config_allow_hz_change = 0;

struct zfile *retro_deserialize_file = NULL;
static size_t save_state_file_size = 0;

#include "libretro-keyboard.i"
int keyId(const char *val)
{
   int i=0;
   while (keyDesc[i]!=NULL)
   {
      if (!strcmp(keyDesc[i],val))
         return keyVal[i];
      i++;
   }
   return 0;
}

extern void retro_poll_event(void);
unsigned int uae_devices[4];
extern int cd32_pad_enabled[NORMAL_JPORTS];
int mapper_keys[31]={0};
extern void display_current_image(const char *image, bool inserted);

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;

static char retro_save_directory[RETRO_PATH_MAX] = {0};
// retro_system_directory is extern, used in caps.c and driveclick.c,
// so cannot be static
char retro_system_directory[512] = {0};
static char retro_content_directory[RETRO_PATH_MAX] = {0};
static char retro_temp_directory[RETRO_PATH_MAX] = {0};

// Disk control context
dc_storage *retro_dc = NULL;

// Configs
static char uae_machine[256];
static char uae_kickstart[RETRO_PATH_MAX];
static char uae_kickstart_ext[RETRO_PATH_MAX];
static char uae_config[1024];

void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_controller_description p1_controllers[] = {
      { "CD32 Pad", RETRO_DEVICE_UAE_CD32PAD },
      { "Analog Joystick", RETRO_DEVICE_UAE_ANALOG },
      { "Joystick", RETRO_DEVICE_UAE_JOYSTICK },
      { "Keyboard", RETRO_DEVICE_UAE_KEYBOARD },
      { "None", RETRO_DEVICE_NONE },
   };
   static const struct retro_controller_description p2_controllers[] = {
      { "CD32 Pad", RETRO_DEVICE_UAE_CD32PAD },
      { "Analog Joystick", RETRO_DEVICE_UAE_ANALOG },
      { "Joystick", RETRO_DEVICE_UAE_JOYSTICK },
      { "Keyboard", RETRO_DEVICE_UAE_KEYBOARD },
      { "None", RETRO_DEVICE_NONE },
   };
   static const struct retro_controller_description p3_controllers[] = {
      { "Joystick", RETRO_DEVICE_UAE_JOYSTICK },
      { "Keyboard", RETRO_DEVICE_UAE_KEYBOARD },
      { "None", RETRO_DEVICE_NONE },
   };
   static const struct retro_controller_description p4_controllers[] = {
      { "Joystick", RETRO_DEVICE_UAE_JOYSTICK },
      { "Keyboard", RETRO_DEVICE_UAE_KEYBOARD },
      { "None", RETRO_DEVICE_NONE },
   };

   static const struct retro_controller_info ports[] = {
      { p1_controllers, 5 }, // port 1
      { p2_controllers, 5 }, // port 2
      { p3_controllers, 3 }, // port 3
      { p4_controllers, 3 }, // port 4
      { NULL, 0 }
   };

   static struct retro_core_option_definition core_options[] =
   {
      {
         "puae_model",
         "Model",
         "Automatic defaults to A500 when booting floppy disks and to A600 when booting hard drives.\nCore restart required.",
         {
            { "auto", "Automatic" },
            { "A500OG", "A500 (512KB Chip)" },
            { "A500", "A500 (512KB Chip + 512KB Slow)" },
            { "A500PLUS", "A500+ (1MB Chip)" },
            { "A600", "A600 (2MB Chip + 8MB Fast)" },
            { "A1200OG", "A1200 (2MB Chip)" },
            { "A1200", "A1200 (2MB Chip + 8MB Fast)" },
            { "A4030", "A4000/030 (2MB Chip + 8MB Fast)" },
            { "A4040", "A4000/040 (2MB Chip + 8MB Fast)" },
            { "CD32", "CD32 (2MB Chip)" },
            { "CD32FR", "CD32 (2MB Chip + 8MB Fast)" },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_cpu_compatibility",
         "CPU Compatibility",
         "",
         {
            { "normal", "Normal" },
            { "compatible", "More compatible" },
            { "exact", "Cycle-exact" },
            { NULL, NULL },
         },
#ifdef VITA
         "normal"
#else
         "exact"
#endif
      },
      {
         "puae_cpu_throttle",
         "CPU Speed",
         "Ignored with 'Cycle-exact'.",
         {
            { "-900.0", "-90\%" },
            { "-800.0", "-80\%" },
            { "-700.0", "-70\%" },
            { "-600.0", "-60\%" },
            { "-500.0", "-50\%" },
            { "-400.0", "-40\%" },
            { "-300.0", "-30\%" },
            { "-200.0", "-20\%" },
            { "-100.0", "-10\%" },
            { "0.0", "Default" },
            { "1000.0", "+100\%" },
            { "2000.0", "+200\%" },
            { "3000.0", "+300\%" },
            { "4000.0", "+400\%" },
            { "5000.0", "+500\%" },
            { "6000.0", "+600\%" },
            { "7000.0", "+700\%" },
            { "8000.0", "+800\%" },
            { "9000.0", "+900\%" },
            { "10000.0", "+1000\%" },
            { NULL, NULL },
         },
         "0.0"
      },
      {
         "puae_cpu_multiplier",
         "CPU Cycle-exact Speed",
         "Applies only with 'Cycle-exact'.",
         {
            { "0", "Default" },
            { "1", "3.546895 MHz" },
            { "2", "7.093790 MHz (A500)" },
            { "4", "14.187580 MHz (A1200)" },
            { "8", "28.375160 MHz" },
            { "10", "35.468950 MHz" },
            { "12", "42.562740 MHz" },
            { "16", "56.750320 MHz" },
            { NULL, NULL },
         },
         "0"
      },
      {
         "puae_video_options_display",
         "Show Video Options",
         "Core options page refresh required.",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_video_allow_hz_change",
         "Allow PAL/NTSC Hz Change",
         "Let Amiga decide the exact output Hz.",
         {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
         },
         "enabled"
      },
      {
         "puae_video_standard",
         "Video Standard",
         "Output Hz & height:\n- Single Line / Double Line.",
         {
            { "PAL", "PAL 50Hz - 288px / 576px" },
            { "NTSC", "NTSC 60Hz - 240px / 480px" },
            { NULL, NULL },
         },
         "PAL"
      },
      {
         "puae_video_resolution",
         "Video Resolution",
         "Output width:\n- Automatic defaults to High and switches to Super-High when needed.",
         {
            { "lores", "Low 360px" },
            { "hires", "High 720px" },
            { "superhires", "Super-High 1440px" },
            { "automatic", "Automatic" },
            { NULL, NULL },
         },
         "automatic"
      },
      {
         "puae_video_vresolution",
         "Video Line Mode",
         "Output height:\n- Automatic defaults to Single Line and switches to Double Line on interlaced screens.",
         {
            { "single", "Single Line" },
            { "double", "Double Line" },
            { "automatic", "Automatic" },
            { NULL, NULL },
         },
         "automatic"
      },
      {
         "puae_video_aspect",
         "Aspect Ratio",
         "PAR:\n- PAL 1/1 = 1.000\n- NTSC 44/52 = 0.846",
         {
            { "auto", "Automatic" },
            { "PAL", NULL },
            { "NTSC", NULL },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_zoom_mode",
         "Zoom Mode",
         "Requirements in RetroArch settings:\n- Aspect Ratio: Core provided,\n- Integer Scale: Off.",
         {
            { "none", "disabled" },
            { "minimum", "Minimum" },
            { "smaller", "Smaller" },
            { "small", "Small" },
            { "medium", "Medium" },
            { "large", "Large" },
            { "larger", "Larger" },
            { "maximum", "Maximum" },
            { "auto", "Automatic" },
            { NULL, NULL },
         },
         "none"
      },
      {
         "puae_vertical_pos",
         "Vertical Position",
         "Automatic keeps zoom modes centered. Positive values force the screen upward and negative values downward.",
         {
            { "auto", "Automatic" },
            { "0", NULL },
            { "2", NULL },
            { "4", NULL },
            { "6", NULL },
            { "8", NULL },
            { "10", NULL },
            { "12", NULL },
            { "14", NULL },
            { "16", NULL },
            { "18", NULL },
            { "20", NULL },
            { "22", NULL },
            { "24", NULL },
            { "26", NULL },
            { "28", NULL },
            { "30", NULL },
            { "32", NULL },
            { "34", NULL },
            { "36", NULL },
            { "38", NULL },
            { "40", NULL },
            { "-20", NULL },
            { "-18", NULL },
            { "-16", NULL },
            { "-14", NULL },
            { "-12", NULL },
            { "-10", NULL },
            { "-8", NULL },
            { "-6", NULL },
            { "-4", NULL },
            { "-2", NULL },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_horizontal_pos",
         "Horizontal Position",
         "Automatic keeps screen centered. Positive values force the screen right and negative values left.",
         {
            { "auto", "Automatic" },
            { "0", NULL },
            { "2", NULL },
            { "4", NULL },
            { "6", NULL },
            { "8", NULL },
            { "10", NULL },
            { "12", NULL },
            { "14", NULL },
            { "16", NULL },
            { "18", NULL },
            { "20", NULL },
            { "22", NULL },
            { "24", NULL },
            { "26", NULL },
            { "28", NULL },
            { "30", NULL },
            { "32", NULL },
            { "34", NULL },
            { "36", NULL },
            { "38", NULL },
            { "40", NULL },
            { "-40", NULL },
            { "-38", NULL },
            { "-36", NULL },
            { "-34", NULL },
            { "-32", NULL },
            { "-30", NULL },
            { "-28", NULL },
            { "-26", NULL },
            { "-24", NULL },
            { "-22", NULL },
            { "-20", NULL },
            { "-18", NULL },
            { "-16", NULL },
            { "-14", NULL },
            { "-12", NULL },
            { "-10", NULL },
            { "-8", NULL },
            { "-6", NULL },
            { "-4", NULL },
            { "-2", NULL },
            { NULL, NULL },
         },
         "auto"
      },
      {
         "puae_gfx_colors",
         "Color Depth",
         "24-bit is slower and not available on all platforms. Full restart required.",
         {
            { "16bit", "Thousands (16-bit)" },
            { "24bit", "Millions (24-bit)" },
            { NULL, NULL },
         },
         "16bit"
      },
      {
         "puae_collision_level",
         "Collision Level",
         "'Sprites and Playfields' is recommended.",
         {
            { "playfields", "Sprites and Playfields" },
            { "sprites", "Sprites only" },
            { "full", "Full" },
            { "none", "None" },
            { NULL, NULL },
         },
         "playfields"
      },
      {
         "puae_immediate_blits",
         "Immediate/Waiting Blits",
         "'Immediate Blitter' ignored with 'Cycle-exact'.",
         {
            { "false", "disabled" },
            { "immediate", "Immediate Blitter" },
            { "waiting", "Wait for Blitter" },
            { NULL, NULL },
         },
         "waiting"
      },
      {
         "puae_gfx_framerate",
         "Frameskip",
         "Not compatible with 'Cycle-exact'.",
         {
            { "disabled", NULL },
            { "1", NULL },
            { "2", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_statusbar",
         "Statusbar Position & Mode",
         "",
         {
            { "bottom", "Bottom Full" },
            { "bottom_minimal", "Bottom Full Minimal" },
            { "bottom_basic", "Bottom Basic" },
            { "bottom_basic_minimal", "Bottom Basic Minimal" },
            { "top", "Top Full" },
            { "top_minimal", "Top Full Minimal" },
            { "top_basic", "Top Basic" },
            { "top_basic_minimal", "Top Basic Minimal" },
            { NULL, NULL },
         },
         "bottom"
      },
      {
         "puae_vkbd_theme",
         "Virtual Keyboard Theme",
         "By default, the keyboard comes up with SELECT button or F11 key.",
         {
            { "0", "Classic" },
            { "1", "CD32" },
            { "2", "Dark" },
            { "3", "Light" },
            { NULL, NULL },
         },
         "0"
      },
      {
         "puae_vkbd_alpha",
         "Virtual Keyboard Transparency",
         "",
         {
            { "0\%", NULL },
            { "5\%", NULL },
            { "10\%", NULL },
            { "15\%", NULL },
            { "20\%", NULL },
            { "25\%", NULL },
            { "30\%", NULL },
            { "35\%", NULL },
            { "40\%", NULL },
            { "45\%", NULL },
            { "50\%", NULL },
            { "55\%", NULL },
            { "60\%", NULL },
            { "65\%", NULL },
            { "70\%", NULL },
            { "75\%", NULL },
            { "80\%", NULL },
            { "85\%", NULL },
            { "90\%", NULL },
            { "95\%", NULL },
            { NULL, NULL },
         },
         "20\%"
      },
      {
         "puae_audio_options_display",
         "Show Audio Options",
         "Core options page refresh required.",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_sound_output",
         "Sound Output",
         "",
         {
            { "none", "None" },
            { "interrupts", "Interrupts" },
            { "normal", "Normal" },
            { "exact", "Exact" },
            { NULL, NULL },
         },
         "exact"
      },
      {
         "puae_sound_stereo_separation",
         "Sound Stereo Separation",
         "Paula sound chip channel panning.",
         {
            { "0\%", NULL },
            { "10\%", NULL },
            { "20\%", NULL },
            { "30\%", NULL },
            { "40\%", NULL },
            { "50\%", NULL },
            { "60\%", NULL },
            { "70\%", NULL },
            { "80\%", NULL },
            { "90\%", NULL },
            { "100\%", NULL },
            { NULL, NULL },
         },
         "100\%"
      },
      {
         "puae_sound_interpol",
         "Sound Interpolation",
         "",
         {
            { "none", "None" },
            { "anti", "Anti" },
            { "sinc", "Sinc" },
            { "rh", "RH" },
            { "crux", "Crux" },
            { NULL, NULL },
         },
         "anti"
      },
      {
         "puae_sound_filter",
         "Sound Filter",
         "",
         {
            { "emulated", "Emulated" },
            { "off", "Always off" },
            { "on", "Always on" },
            { NULL, NULL },
         },
         "emulated"
      },
      {
         "puae_sound_filter_type",
         "Sound Filter Type",
         "",
         {
            { "auto", "Automatic" },
            { "standard", "A500" },
            { "enhanced", "A1200" },
            { NULL, NULL },
         },
         "auto",
      },
      {
         "puae_sound_volume_cd",
         "CD Audio Volume",
         "",
         {
            { "0\%", NULL },
            { "5\%", NULL },
            { "10\%", NULL },
            { "15\%", NULL },
            { "20\%", NULL },
            { "25\%", NULL },
            { "30\%", NULL },
            { "35\%", NULL },
            { "40\%", NULL },
            { "45\%", NULL },
            { "50\%", NULL },
            { "55\%", NULL },
            { "60\%", NULL },
            { "65\%", NULL },
            { "70\%", NULL },
            { "75\%", NULL },
            { "80\%", NULL },
            { "85\%", NULL },
            { "90\%", NULL },
            { "95\%", NULL },
            { "100\%", NULL },
            { NULL, NULL },
         },
         "100\%"
      },
      {
         "puae_floppy_sound",
         "Floppy Sound Emulation",
         "",
         {
            { "100", "disabled" },
            { "95", "5\% volume" },
            { "90", "10\% volume" },
            { "85", "15\% volume" },
            { "80", "20\% volume" },
            { "75", "25\% volume" },
            { "70", "30\% volume" },
            { "65", "35\% volume" },
            { "60", "40\% volume" },
            { "55", "45\% volume" },
            { "50", "50\% volume" },
            { "45", "55\% volume" },
            { "40", "60\% volume" },
            { "35", "65\% volume" },
            { "30", "70\% volume" },
            { "25", "75\% volume" },
            { "20", "80\% volume" },
            { "15", "85\% volume" },
            { "10", "90\% volume" },
            { "5", "95\% volume" },
            { "0", "100\% volume" },
            { NULL, NULL },
         },
         "100"
      },
      {
         "puae_floppy_sound_type",
         "Floppy Sound Emulation Type",
         "External file location is 'system/uae_data/'.",
         {
            { "internal", "Internal" },
            { "A500", "External: A500" },
            { "LOUD", "External: LOUD" },
            { NULL, NULL },
         },
         "internal"
      },
      {
         "puae_floppy_speed",
         "Floppy Speed",
         "",
         {
            { "100", "1x" },
            { "200", "2x" },
            { "400", "4x" },
            { "800", "8x" },
            { "0", "Turbo" },
            { NULL, NULL },
         },
         "100"
      },
      {
         "puae_cd_speed",
         "CD Speed",
         "",
         {
            { "100", "1x" },
            { "0", "Turbo" },
            { NULL, NULL },
         },
         "100"
      },
      {
         "puae_shared_nvram",
         "Shared CD32 NVRAM",
         "Disabled will save separate files per content. Enabled will use one shared file. Core restart required.",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_use_boot_hd",
         "Global Boot HD",
         "Keep a bootable hard drive attached with hard drive compatible setups. Enabling will change the automatic model to A600. Changing HDF sizes requires removing the old file manually.",
         {
            { "disabled", NULL },
            { "files", "Files" },
            { "hdf20", "HDF 20MB" },
            { "hdf40", "HDF 40MB" },
            { "hdf80", "HDF 80MB" },
            { "hdf128", "HDF 128MB" },
            { "hdf256", "HDF 256MB" },
            { "hdf512", "HDF 512MB" },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_use_whdload",
         "WHDLoad Support",
         "Enable launching pre-installed WHDLoad installs. Creates a helper image for loading content and an empty image for saving. Core restart required.\n- 'Files' creates the data in directories\n- 'HDFs' contains the data in images",
         {
            { "disabled", NULL },
            { "files", "Files" },
            { "hdfs", "HDFs" },
            { NULL, NULL },
         },
         "files"
      },
      {
         "puae_use_whdload_prefs",
         "WHDLoad Splash Screen Options",
         "Space/Enter/Fire work as the WHDLoad Start-button. Core restart required.\nOverride with buttons while booting:\n- 'Config': Hold 2nd fire / Blue.\n- 'Splash': Hold LMB.\n- 'Config + Splash': Hold RMB.",
         {
            { "disabled", NULL },
            { "config", "Config (Show only if available)" },
            { "splash", "Splash (Show briefly)" },
            { "both", "Config + Splash (Wait for user input)" },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_analogmouse",
         "Analog Stick Mouse",
         "",
         {
            { "disabled", NULL },
            { "left", "Left Analog" },
            { "right", "Right Analog" },
            { "both", "Both Analogs" },
            { NULL, NULL },
         },
         "right"
      },
      {
         "puae_analogmouse_deadzone",
         "Analog Stick Mouse Deadzone",
         "",
         {
            { "0", "0\%" },
            { "5", "5\%" },
            { "10", "10\%" },
            { "15", "15\%" },
            { "20", "20\%" },
            { "25", "25\%" },
            { "30", "30\%" },
            { "35", "35\%" },
            { "40", "40\%" },
            { "45", "45\%" },
            { "50", "50\%" },
            { NULL, NULL },
         },
         "15"
      },
      {
         "puae_analogmouse_speed",
         "Analog Stick Mouse Speed",
         "",
         {
            { "0.5", "50\%" },
            { "0.6", "60\%" },
            { "0.7", "70\%" },
            { "0.8", "80\%" },
            { "0.9", "90\%" },
            { "1.0", "100\%" },
            { "1.1", "110\%" },
            { "1.2", "120\%" },
            { "1.3", "130\%" },
            { "1.4", "140\%" },
            { "1.5", "150\%" },
            { NULL, NULL },
         },
         "1.0"
      },
      {
         "puae_dpadmouse_speed",
         "D-Pad Mouse Speed",
         "",
         {
            { "3", "50\%" },
            { "4", "66\%" },
            { "5", "83\%" },
            { "6", "100\%" },
            { "7", "116\%" },
            { "8", "133\%" },
            { "9", "150\%" },
            { "10", "166\%" },
            { "11", "183\%" },
            { "12", "200\%" },
            { NULL, NULL },
         },
         "6"
      },
      {
         "puae_mouse_speed",
         "Mouse Speed",
         "Affects mouse speed globally.",
         {
            { "10", "10\%" },
            { "20", "20\%" },
            { "30", "30\%" },
            { "40", "40\%" },
            { "50", "50\%" },
            { "60", "60\%" },
            { "70", "70\%" },
            { "80", "80\%" },
            { "90", "90\%" },
            { "100", "100\%" },
            { "110", "110\%" },
            { "120", "120\%" },
            { "130", "130\%" },
            { "140", "140\%" },
            { "150", "150\%" },
            { "160", "160\%" },
            { "170", "170\%" },
            { "180", "180\%" },
            { "190", "190\%" },
            { "200", "200\%" },
            { NULL, NULL },
         },
         "100"
      },
      {
         "puae_multimouse",
         "Multiple Physical Mouse",
         "Requirements: raw/udev input driver and proper mouse index in RA input configs.\nOnly for real mice, not RetroPad emulated.",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_keyrah_keypad_mappings",
         "Keyrah Keypad Mappings",
         "Hardcoded keypad to joy mappings for Keyrah hardware.",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_physical_keyboard_pass_through",
         "Physical Keyboard Pass-through",
         "Pass all physical keyboard events to the core. Disable this to prevent cursor keys and fire key from generating key events.",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_mapping_options_display",
         "Show Mapping Options",
         "Show options for hotkeys & RetroPad mappings.\nCore option page refresh required.",
         {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
         },
         "disabled"
      },
      /* Hotkeys */
      {
         "puae_mapper_vkbd",
         "Hotkey: Toggle Virtual Keyboard",
         "Press the mapped key to toggle the virtual keyboard.",
         {{ NULL, NULL }},
         "RETROK_F11"
      },
      {
         "puae_mapper_statusbar",
         "Hotkey: Toggle Statusbar",
         "Press the mapped key to toggle the statusbar.",
         {{ NULL, NULL }},
         "RETROK_F12"
      },
      {
         "puae_mapper_mouse_toggle",
         "Hotkey: Toggle Joystick/Mouse",
         "Press the mapped key to toggle between joystick and mouse control.",
         {{ NULL, NULL }},
         "RETROK_RCTRL"
      },
      {
         "puae_mapper_reset",
         "Hotkey: Reset",
         "Press the mapped key to trigger reset (Ctrl-Amiga-Amiga).",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_aspect_ratio_toggle",
         "Hotkey: Toggle Aspect Ratio",
         "Press the mapped key to toggle between PAL/NTSC aspect ratio.",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_zoom_mode_toggle",
         "Hotkey: Toggle Zoom Mode",
         "Press the mapped key to toggle zoom mode.",
         {{ NULL, NULL }},
         "---"
      },
      /* Button mappings */
      {
         "puae_mapper_select",
         "RetroPad Select",
         "",
         {{ NULL, NULL }},
         "TOGGLE_VKBD"
      },
      {
         "puae_mapper_start",
         "RetroPad Start",
         "",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_b",
         "RetroPad B",
         "Unmapped will default to fire button.",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_a",
         "RetroPad A",
         "Unmapped will default to 2nd fire button.",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_y",
         "RetroPad Y",
         "",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_x",
         "RetroPad X",
         "",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_l",
         "RetroPad L",
         "",
         {{ NULL, NULL }},
         ""
      },
      {
         "puae_mapper_r",
         "RetroPad R",
         "",
         {{ NULL, NULL }},
         ""
      },
      {
         "puae_mapper_l2",
         "RetroPad L2",
         "",
         {{ NULL, NULL }},
         "MOUSE_LEFT_BUTTON"
      },
      {
         "puae_mapper_r2",
         "RetroPad R2",
         "",
         {{ NULL, NULL }},
         "MOUSE_RIGHT_BUTTON"
      },
      {
         "puae_mapper_l3",
         "RetroPad L3",
         "",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_r3",
         "RetroPad R3",
         "",
         {{ NULL, NULL }},
         "---"
      },
      /* Left Stick */
      {
         "puae_mapper_lu",
         "RetroPad L-Up",
         "Mapping for left analog stick up.",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_ld",
         "RetroPad L-Down",
         "Mapping for left analog stick down.",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_ll",
         "RetroPad L-Left",
         "Mapping for left analog stick left.",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_lr",
         "RetroPad L-Right",
         "Mapping for left analog stick right.",
         {{ NULL, NULL }},
         "---"
      },
      /* Right Stick */
      {
         "puae_mapper_ru",
         "RetroPad R-Up",
         "Mapping for right analog stick up.",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_rd",
         "RetroPad R-Down",
         "Mapping for right analog stick down.",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_rl",
         "RetroPad R-Left",
         "Mapping for right analog stick left.",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_mapper_rr",
         "RetroPad R-Right",
         "Mapping for right analog stick right.",
         {{ NULL, NULL }},
         "---"
      },
      {
         "puae_turbo_fire_button",
         "RetroPad Turbo Fire",
         "Replaces the mapped button with a turbo fire button.",
         {
            { "disabled", NULL },
            { "A", "RetroPad A" },
            { "Y", "RetroPad Y" },
            { "X", "RetroPad X" },
            { "L", "RetroPad L" },
            { "R", "RetroPad R" },
            { "L2", "RetroPad L2" },
            { "R2", "RetroPad R2" },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_turbo_pulse",
         "RetroPad Turbo Pulse",
         "Frames in a button cycle. 2 equals button press on a frame and release on the next frame.",
         {
            { "2", NULL },
            { "4", NULL },
            { "6", NULL },
            { "8", NULL },
            { "10", NULL },
            { "12", NULL },
            { NULL, NULL },
         },
         "4"
      },
      {
         "puae_retropad_options",
         "RetroPad Face Button Options",
         "Rotate face buttons clockwise and/or make 2nd fire press up.",
         {
            { "disabled", "B = Fire, A = 2nd fire" },
            { "jump", "B = Fire, A = Up" },
            { "rotate", "Y = Fire, B = 2nd fire" },
            { "rotate_jump", "Y = Fire, B = Up" },
            { NULL, NULL },
         },
         "disabled"
      },
      {
         "puae_cd32pad_options",
         "CD32 Pad Face Button Options",
         "Rotate face buttons clockwise and/or make blue button press up.",
         {
            { "disabled", "B = Red, A = Blue" },
            { "jump", "B = Red, A = Up" },
            { "rotate", "Y = Red, B = Blue" },
            { "rotate_jump", "Y = Red, B = Up" },
            { NULL, NULL },
         },
         "disabled"
      },
      { NULL, NULL, NULL, {{0}}, NULL },
   };

   /* fill in the values for all the mappers */
   int i = 0;
   int j = 0;
   int hotkey = 0;
   while (core_options[i].key)
   {
      if (strstr(core_options[i].key, "puae_mapper_"))
      {
         /* Show different key list for hotkeys (special negatives removed) */
         if (  strstr(core_options[i].key, "puae_mapper_vkbd")
            || strstr(core_options[i].key, "puae_mapper_statusbar")
            || strstr(core_options[i].key, "puae_mapper_mouse_toggle")
            || strstr(core_options[i].key, "puae_mapper_reset")
            || strstr(core_options[i].key, "puae_mapper_aspect_ratio_toggle")
            || strstr(core_options[i].key, "puae_mapper_zoom_mode_toggle")
         )
            hotkey = 1;
         else
            hotkey = 0;

         j = 0;
         if (hotkey)
         {
             while (keyDescHotkeys[j] && j < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1)
             {
                core_options[i].values[j].value = keyDescHotkeys[j];
                core_options[i].values[j].label = NULL;
                ++j;
             };
         }
         else
         {
             while (keyDesc[j] && j < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1)
             {
                core_options[i].values[j].value = keyDesc[j];
                core_options[i].values[j].label = NULL;
                ++j;
             };
         }
         core_options[i].values[j].value = NULL;
         core_options[i].values[j].label = NULL;
      };
      ++i;
   }

   environ_cb = cb;
   unsigned version = 0;
   if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && (version == 1))
      cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, core_options);
   else
   {
      /* Fallback for older API */
      static char buf[sizeof(core_options) / sizeof(core_options[0])][4096] = { 0 };
      static struct retro_variable variables[sizeof(core_options) / sizeof(core_options[0])] = { 0 };
      i = 0;
      while (core_options[i].key)
      {
         buf[i][0] = 0;
         variables[i].key = core_options[i].key;
         strcpy(buf[i], core_options[i].desc);
         strcat(buf[i], "; ");
         strcat(buf[i], core_options[i].default_value);
         j = 0;
         while (core_options[i].values[j].value && j < RETRO_NUM_CORE_OPTION_VALUES_MAX)
         {
            strcat(buf[i], "|");
            strcat(buf[i], core_options[i].values[j].value);
            ++j;
         };
         variables[i].value = buf[i];
         ++i;
      };
      variables[i].key = NULL;
      variables[i].value = NULL;
      cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
   }

   static bool allowNoGameMode;
   allowNoGameMode = true;
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &allowNoGameMode);
   cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
}

static void update_variables(void)
{
   uae_machine[0] = '\0';
   uae_config[0] = '\0';

   struct retro_variable var = {0};
   struct retro_core_option_display option_display;

   static int video_config_region = 0;

   //var.key = "puae_model";
   //var.value = NULL;
   const char* model = "auto";
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   //{
      _tcscpy(opt_model, model);
   //}

   var.key = "puae_video_standard";
   var.value = NULL;
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   var.value = "NTSC";
   fprintf(stdout, "[libretro.c] update_variables: puae_video_standard value: %s\n", var.value);
   {
      /* video_config change only at start */
      if (video_config_old == 0)
      {
         if (strcmp(var.value, "PAL") == 0)
         {
            video_config |= PUAE_VIDEO_PAL;
            strcat(uae_config, "ntsc=false\n");
         }
         else
         {
            video_config |= PUAE_VIDEO_NTSC;
            strcat(uae_config, "ntsc=true\n");
            real_ntsc = true;
         }
         video_config_region = video_config;
      }
      else if (!forced_video)
      {
         if (strcmp(var.value, "PAL") == 0)
            changed_prefs.ntscmode=0;
         else
            changed_prefs.ntscmode=1;
      }
   }

   var.key = "puae_video_aspect";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_video_aspect value: %s\n", var.value);
   
      if (strcmp(var.value, "PAL") == 0) video_config_aspect = PUAE_VIDEO_PAL;
      else if (strcmp(var.value, "NTSC") == 0) video_config_aspect = PUAE_VIDEO_NTSC;
      else video_config_aspect = 0;
   }

   var.key = "puae_video_allow_hz_change";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_video_allow_hz_change value: %s\n", var.value);
      if (strcmp(var.value, "enabled") == 0) video_config_allow_hz_change = 1;
      else if (strcmp(var.value, "disabled") == 0) video_config_allow_hz_change = 0;
   }

   //var.key = "puae_video_resolution";
   //var.value = NULL;
   
   var.value= "lores";
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_video_resolution value: %s\n", var.value);
      opt_video_resolution_auto = false;

      if (strcmp(var.value, "lores") == 0)
      {
         video_config &= ~PUAE_VIDEO_HIRES;
         video_config &= ~PUAE_VIDEO_SUPERHIRES;
         retro_max_diwlastword = retro_max_diwlastword_hires / 2;
         if (libretro_runloop_active)
            changed_prefs.gfx_resolution=RES_LORES;
      }
      else if (strcmp(var.value, "hires") == 0)
      {
         video_config &= ~PUAE_VIDEO_SUPERHIRES;
         video_config |= PUAE_VIDEO_HIRES;
         retro_max_diwlastword = retro_max_diwlastword_hires;
         if (libretro_runloop_active)
            changed_prefs.gfx_resolution=RES_HIRES;
      }
      else if (strcmp(var.value, "superhires") == 0)
      {
         video_config &= ~PUAE_VIDEO_HIRES;
         video_config |= PUAE_VIDEO_SUPERHIRES;
         retro_max_diwlastword = retro_max_diwlastword_hires * 2;
         if (libretro_runloop_active)
            changed_prefs.gfx_resolution=RES_SUPERHIRES;
      }
      else if (strcmp(var.value, "automatic") == 0)
      {
         opt_video_resolution_auto = true;

         if (video_config_old & PUAE_VIDEO_SUPERHIRES)
         {
            video_config &= ~PUAE_VIDEO_HIRES;
            video_config |= PUAE_VIDEO_SUPERHIRES;
            retro_max_diwlastword = retro_max_diwlastword_hires * 2;
            if (libretro_runloop_active)
               changed_prefs.gfx_resolution=RES_SUPERHIRES;
         }
         else
         {
            video_config &= ~PUAE_VIDEO_SUPERHIRES;
            video_config |= PUAE_VIDEO_HIRES;
            retro_max_diwlastword = retro_max_diwlastword_hires;
            if (libretro_runloop_active)
               changed_prefs.gfx_resolution=RES_HIRES;
         }
      }

      /* Resolution change needs init_custom() to be done after reset_drawing() is done */
      if (libretro_runloop_active && video_config != video_config_old)
         request_init_custom_timer = 3;
   }

   var.key = "puae_video_vresolution";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_video_vresolution value: %s\n", var.value);
   
      opt_video_vresolution_auto = false;

      if (strcmp(var.value, "double") == 0)
      {
         video_config |= PUAE_VIDEO_DOUBLELINE;
         if (libretro_runloop_active)
            changed_prefs.gfx_vresolution=VRES_DOUBLE;
      }
      else if (strcmp(var.value, "single") == 0)
      {
         video_config &= ~PUAE_VIDEO_DOUBLELINE;
         if (libretro_runloop_active)
            changed_prefs.gfx_vresolution=VRES_NONDOUBLE;
      }
      else if (strcmp(var.value, "automatic") == 0)
      {
         opt_video_vresolution_auto = true;

         if (libretro_runloop_active)
         {
            if (video_config_old & PUAE_VIDEO_DOUBLELINE)
            {
               video_config |= PUAE_VIDEO_DOUBLELINE;
               changed_prefs.gfx_vresolution=VRES_DOUBLE;
            }
            else
            {
               video_config &= ~PUAE_VIDEO_DOUBLELINE;
               changed_prefs.gfx_vresolution=VRES_NONDOUBLE;
            }
         }
      }

      // Lores can not be double lined
      if (retro_max_diwlastword < retro_max_diwlastword_hires)
      {
         video_config &= ~PUAE_VIDEO_DOUBLELINE;
         changed_prefs.gfx_vresolution=VRES_NONDOUBLE;
      }

      /* Resolution change needs init_custom() to be done after reset_drawing() is done */
      if (libretro_runloop_active && video_config != video_config_old)
         request_init_custom_timer = 3;
   }

   var.key = "puae_statusbar";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_statusbar value: %s\n", var.value);
   
      if (strstr(var.value, "top"))
         opt_statusbar_position = -1;
      else if (strstr(var.value, "bottom"))
         opt_statusbar_position = 0;

      if (strstr(var.value, "basic"))
         opt_statusbar_enhanced = false;
      else
         opt_statusbar_enhanced = true;

      if (strstr(var.value, "minimal"))
         opt_statusbar_minimal = true;
      else
         opt_statusbar_minimal = false;

      opt_statusbar_position_old = opt_statusbar_position;
   }

   var.key = "puae_vkbd_theme";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_vkbd_theme value: %s\n", var.value);
   
      opt_vkbd_theme = atoi(var.value);
   }

   var.key = "puae_vkbd_alpha";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_vkbd_alpha value: %s\n", var.value);
   
      opt_vkbd_alpha = 255 - (255 * atoi(var.value) / 100);
   }

   //var.key = "puae_cpu_compatibility";
   //var.value = NULL;
   var.value = "compatible";
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_cpu_compatibility value: %s\n", var.value);
   
      if (strcmp(var.value, "normal") == 0)
      {
         strcat(uae_config, "cpu_compatible=false\n");
         strcat(uae_config, "cycle_exact=false\n");
      }
      else if (strcmp(var.value, "compatible") == 0)
      {
         strcat(uae_config, "cpu_compatible=true\n");
         strcat(uae_config, "cycle_exact=false\n");
      }
      else if (strcmp(var.value, "exact") == 0)
      {
         strcat(uae_config, "cpu_compatible=true\n");
         strcat(uae_config, "cycle_exact=true\n");
      }

      if (libretro_runloop_active)
      {
         if (strcmp(var.value, "normal") == 0)
         {
            changed_prefs.cpu_compatible=0;
            changed_prefs.cpu_cycle_exact=0;
            changed_prefs.blitter_cycle_exact=0;
         }
         else if (strcmp(var.value, "compatible") == 0)
         {
            changed_prefs.cpu_compatible=1;
            changed_prefs.cpu_cycle_exact=0;
            changed_prefs.blitter_cycle_exact=0;
         }
         else if (strcmp(var.value, "exact") == 0)
         {
            changed_prefs.cpu_compatible=1;
            changed_prefs.cpu_cycle_exact=1;
            changed_prefs.blitter_cycle_exact=1;
         }
      }
   }

   var.key = "puae_cpu_throttle";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_cpu_throttle value: %s\n", var.value);
   
   
      strcat(uae_config, "cpu_throttle=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
         changed_prefs.m68k_speed_throttle=atof(var.value);
   }

   var.key = "puae_cpu_multiplier";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_cpu_multiplier value: %s\n", var.value);
   
      strcat(uae_config, "cpu_multiplier=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
         changed_prefs.cpu_clock_multiplier=atoi(var.value) * 256;
   }

   var.key = "puae_sound_output";
   //var.value = NULL;
   var.value = "normal";
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_sound_output value: %s\n", var.value);
   
      strcat(uae_config, "sound_output=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
      {
         if (strcmp(var.value, "none") == 0) changed_prefs.produce_sound=0;
         else if (strcmp(var.value, "interrupts") == 0) changed_prefs.produce_sound=1;
         else if (strcmp(var.value, "normal") == 0) changed_prefs.produce_sound=2;
         else if (strcmp(var.value, "exact") == 0) changed_prefs.produce_sound=3;
      }
   }

   var.key = "puae_sound_stereo_separation";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_sound_stereo_separation value: %s\n", var.value);
   
      int val = atoi(var.value) / 10;
      char valbuf[10];
      snprintf(valbuf, 10, "%d", val);
      strcat(uae_config, "sound_stereo_separation=");
      strcat(uae_config, valbuf);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
         changed_prefs.sound_stereo_separation=val;
   }

   //var.key = "puae_sound_interpol";
   //var.value = NULL;
   var.value = "anti";
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_sound_interpol value: %s\n", var.value);
   
      strcat(uae_config, "sound_interpol=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
      {
         if (strcmp(var.value, "none") == 0) changed_prefs.sound_interpol=0;
         else if (strcmp(var.value, "anti") == 0) changed_prefs.sound_interpol=1;
         else if (strcmp(var.value, "sinc") == 0) changed_prefs.sound_interpol=2;
         else if (strcmp(var.value, "rh") == 0) changed_prefs.sound_interpol=3;
         else if (strcmp(var.value, "crux") == 0) changed_prefs.sound_interpol=4;
      }
   }

   var.key = "puae_sound_filter";
   //var.value = NULL;   
   var.value = "emulated";
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_sound_filter value: %s\n", var.value);
   
      strcat(uae_config, "sound_filter=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");
      
      if (libretro_runloop_active)
      {
         if (strcmp(var.value, "emulated") == 0) changed_prefs.sound_filter=FILTER_SOUND_EMUL;
         else if (strcmp(var.value, "off") == 0) changed_prefs.sound_filter=FILTER_SOUND_OFF;
         else if (strcmp(var.value, "on") == 0) changed_prefs.sound_filter=FILTER_SOUND_ON;
      }
   }

   var.key = "puae_sound_filter_type";
   //var.value = NULL;
   var.value = "standard";
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_sound_filter_type value: %s\n", var.value);
   
      if (strcmp(var.value, "auto"))
      {
         strcat(uae_config, "sound_filter_type=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active)
      {
         if (strcmp(var.value, "standard") == 0) changed_prefs.sound_filter_type=FILTER_SOUND_TYPE_A500;
         else if (strcmp(var.value, "enhanced") == 0) changed_prefs.sound_filter_type=FILTER_SOUND_TYPE_A1200;
         else if (strcmp(var.value, "auto") == 0)
         {
            if (currprefs.cpu_model == 68020)
               changed_prefs.sound_filter_type=FILTER_SOUND_TYPE_A1200;
            else
               changed_prefs.sound_filter_type=FILTER_SOUND_TYPE_A500;
         }
      }
   }

   var.key = "puae_sound_volume_cd";
   //var.value = NULL;
   var.value = "100%";
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_sound_volume_cd value: %s\n", var.value);
   
      /* 100 is mute, 0 is max */
      int val = 100 - atoi(var.value);
      char valbuf[10];
      snprintf(valbuf, 10, "%d", val);
      strcat(uae_config, "sound_volume_cd=");
      strcat(uae_config, valbuf);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
         changed_prefs.sound_volume_cd=val;
   }

   var.key = "puae_cd_speed";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_cd_speed value: %s\n", var.value);
   
      strcat(uae_config, "cd_speed=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
         changed_prefs.cd_speed=atoi(var.value);
   }

   var.key = "puae_floppy_speed";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_floppy_speed value: %s\n", var.value);
   
      strcat(uae_config, "floppy_speed=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
         changed_prefs.floppy_speed=atoi(var.value);
   }

   var.key = "puae_floppy_sound";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_floppy_sound value: %s\n", var.value);
   
      /* Sound is enabled by default if files are found, so this needs to be set always */
      /* 100 is mute, 0 is max */
      strcat(uae_config, "floppy_volume=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      /* Setting volume in realtime will crash on first pass */
      if (libretro_runloop_active)
         changed_prefs.dfxclickvolume=atoi(var.value);
   }

   var.key = "puae_floppy_sound_type";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_floppy_sound_type value: %s\n", var.value);
   
      if (strcmp(var.value, "internal") == 0)
      {
         strcat(uae_config, "floppy0sound=1\n");
         strcat(uae_config, "floppy1sound=1\n");
         strcat(uae_config, "floppy2sound=1\n");
         strcat(uae_config, "floppy3sound=1\n");
      }
      else
      {
         strcat(uae_config, "floppy0sound=-1\n");
         strcat(uae_config, "floppy1sound=-1\n");
         strcat(uae_config, "floppy2sound=-1\n");
         strcat(uae_config, "floppy3sound=-1\n");

         strcat(uae_config, "floppy0soundext=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
         strcat(uae_config, "floppy1soundext=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
         strcat(uae_config, "floppy2soundext=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
         strcat(uae_config, "floppy3soundext=");
         strcat(uae_config, var.value);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active)
      {
         if (strcmp(var.value, "internal") == 0)
         {
            for (int i = 0; i < 4; i++)
               changed_prefs.floppyslots[i].dfxclick=1;
         }
         else
         {
            for (int i = 0; i < 4; i++)
            {
               changed_prefs.floppyslots[i].dfxclick=-1;
               _tcscpy(changed_prefs.floppyslots[i].dfxclickexternal, var.value);
            }
         }
      }
   }

   var.key = "puae_mouse_speed";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mouse_speed value: %s\n", var.value);
      
      strcat(uae_config, "input.mouse_speed=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
      {
         int val;
         val = atoi(var.value);
         changed_prefs.input_mouse_speed=val;
      }
   }

   var.key = "puae_immediate_blits";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_immediate_blits value: %s\n", var.value);
   
      if (strcmp(var.value, "false") == 0)
      {
         strcat(uae_config, "immediate_blits=false\n");
         strcat(uae_config, "waiting_blits=false\n");
      }
      else if (strcmp(var.value, "immediate") == 0)
      {
         strcat(uae_config, "immediate_blits=true\n");
         strcat(uae_config, "waiting_blits=disabled\n");
      }
      else if (strcmp(var.value, "waiting") == 0)
      {
         strcat(uae_config, "immediate_blits=false\n");
         strcat(uae_config, "waiting_blits=automatic\n");
      }

      if (libretro_runloop_active)
      {
         if (strcmp(var.value, "false") == 0)
         {
            changed_prefs.immediate_blits=0;
            changed_prefs.waiting_blits=0;
         }
         else if (strcmp(var.value, "immediate") == 0)
         {
            changed_prefs.immediate_blits=1;
            changed_prefs.waiting_blits=0;
         }
         else if (strcmp(var.value, "waiting") == 0)
         {
            changed_prefs.immediate_blits=0;
            changed_prefs.waiting_blits=1;
         }
      }
   }

   var.key = "puae_collision_level";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_collision_level value: %s\n", var.value);
   
      strcat(uae_config, "collision_level=");
      strcat(uae_config, var.value);
      strcat(uae_config, "\n");

      if (libretro_runloop_active)
      {
         if (strcmp(var.value, "none") == 0) changed_prefs.collision_level=0;
         else if (strcmp(var.value, "sprites") == 0) changed_prefs.collision_level=1;
         else if (strcmp(var.value, "playfields") == 0) changed_prefs.collision_level=2;
         else if (strcmp(var.value, "full") == 0) changed_prefs.collision_level=3;
      }
   }

   var.key = "puae_gfx_framerate";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
   
      fprintf(stdout, "[libretro.c] update_variables: puae_gfx_framerate value: %s\n", var.value);
   
      int val;
      if (strcmp(var.value, "disabled") == 0) val=1;
      else if (strcmp(var.value, "1") == 0) val=2;
      else if (strcmp(var.value, "2") == 0) val=3;

      if (val>1)
      {
         char valbuf[10];
         snprintf(valbuf, 10, "%d", val);
         strcat(uae_config, "gfx_framerate=");
         strcat(uae_config, valbuf);
         strcat(uae_config, "\n");
      }

      if (libretro_runloop_active)
         changed_prefs.gfx_framerate=val;
   }

   var.key = "puae_gfx_colors";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_gfx_colors value: %s\n", var.value);
   
      // Only allow screenmode change after restart
      if (!pix_bytes_initialized)
      {
         if (strcmp(var.value, "16bit") == 0) pix_bytes=2;
         else if (strcmp(var.value, "24bit") == 0) pix_bytes=4;
      }
   }

   var.key = "puae_zoom_mode";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_zoom_mode value: %s\n", var.value);
   
      if (strcmp(var.value, "none") == 0) zoom_mode_id=0;
      else if (strcmp(var.value, "minimum") == 0) zoom_mode_id=1;
      else if (strcmp(var.value, "smaller") == 0) zoom_mode_id=2;
      else if (strcmp(var.value, "small") == 0) zoom_mode_id=3;
      else if (strcmp(var.value, "medium") == 0) zoom_mode_id=4;
      else if (strcmp(var.value, "large") == 0) zoom_mode_id=5;
      else if (strcmp(var.value, "larger") == 0) zoom_mode_id=6;
      else if (strcmp(var.value, "maximum") == 0) zoom_mode_id=7;
      else if (strcmp(var.value, "auto") == 0) zoom_mode_id=8;

      opt_zoom_mode_id = zoom_mode_id;
   }

   var.key = "puae_vertical_pos";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_vertical_pos value: %s\n", var.value);
   
      opt_vertical_offset = 0;
      if (strcmp(var.value, "auto") == 0)
      {
         opt_vertical_offset_auto = true;
         thisframe_y_adjust = minfirstline;
      }
      else
      {
         opt_vertical_offset_auto = false;
         int new_vertical_offset = atoi(var.value);
         if (new_vertical_offset >= -20 && new_vertical_offset <= 40)
         {
            /* This offset is used whenever minfirstline is reset on gfx mode changes in the init_hz() function */
            opt_vertical_offset = new_vertical_offset;
            thisframe_y_adjust = minfirstline + opt_vertical_offset;
         }
      }
   }

   var.key = "puae_horizontal_pos";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_horizontal_pos value: %s\n", var.value);
   
      opt_horizontal_offset = 0;
      if (strcmp(var.value, "auto") == 0)
      {
         opt_horizontal_offset_auto = true;
      }
      else
      {
         opt_horizontal_offset_auto = false;
         int new_horizontal_offset = atoi(var.value);
         int horizontal_multiplier = 1;
         if (video_config & PUAE_VIDEO_HIRES)
            horizontal_multiplier = 2;
         else if (video_config & PUAE_VIDEO_SUPERHIRES)
            horizontal_multiplier = 4;
         if (new_horizontal_offset >= -40 && new_horizontal_offset <= 40)
         {
            opt_horizontal_offset = new_horizontal_offset;
            visible_left_border = retro_max_diwlastword - retrow - (opt_horizontal_offset * horizontal_multiplier);
         }
      }
   }

   var.key = "puae_use_whdload";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_use_whdload value: %s\n", var.value);
   
      if (strcmp(var.value, "disabled") == 0) opt_use_whdload=0;
      else if (strcmp(var.value, "files") == 0) opt_use_whdload=1;
      else if (strcmp(var.value, "hdfs") == 0) opt_use_whdload=2;
   }

   var.key = "puae_use_whdload_prefs";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_use_whdload_prefs value: %s\n", var.value);
   
      if (strcmp(var.value, "disabled") == 0) opt_use_whdload_prefs=0;
      else if (strcmp(var.value, "config") == 0) opt_use_whdload_prefs=1;
      else if (strcmp(var.value, "splash") == 0) opt_use_whdload_prefs=2;
      else if (strcmp(var.value, "both") == 0) opt_use_whdload_prefs=3;
   }

   var.key = "puae_shared_nvram";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_shared_nvram value: %s\n", var.value);
   
      if (strcmp(var.value, "enabled") == 0) opt_shared_nvram=true;
      else if (strcmp(var.value, "disabled") == 0) opt_shared_nvram=false;
   }

   var.key = "puae_use_boot_hd";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_use_boot_hd value: %s\n", var.value);
    
      if (strcmp(var.value, "disabled") == 0) opt_use_boot_hd=0;
      else if (strcmp(var.value, "files") == 0) opt_use_boot_hd=1;
      else if (strcmp(var.value, "hdf20") == 0) opt_use_boot_hd=2;
      else if (strcmp(var.value, "hdf40") == 0) opt_use_boot_hd=3;
      else if (strcmp(var.value, "hdf80") == 0) opt_use_boot_hd=4;
      else if (strcmp(var.value, "hdf128") == 0) opt_use_boot_hd=5;
      else if (strcmp(var.value, "hdf256") == 0) opt_use_boot_hd=6;
      else if (strcmp(var.value, "hdf512") == 0) opt_use_boot_hd=7;
   }

   var.key = "puae_analogmouse";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_analogmouse value: %s\n", var.value);
   
      if (strcmp(var.value, "disabled") == 0) opt_analogmouse=0;
      else if (strcmp(var.value, "left") == 0) opt_analogmouse=1;
      else if (strcmp(var.value, "right") == 0) opt_analogmouse=2;
      else if (strcmp(var.value, "both") == 0) opt_analogmouse=3;
   }

   var.key = "puae_analogmouse_deadzone";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_analogmouse_deadzone value: %s\n", var.value);
   
      opt_analogmouse_deadzone = atoi(var.value);
   }

   var.key = "puae_analogmouse_speed";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_analogmouse_speed value: %s\n", var.value);
   
      opt_analogmouse_speed = atof(var.value);
   }

   var.key = "puae_dpadmouse_speed";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_dpadmouse_speed value: %s\n", var.value);
   
      opt_dpadmouse_speed = atoi(var.value);
   }

   var.key = "puae_multimouse";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_multimouse value: %s\n", var.value);
   
      if (strcmp(var.value, "disabled") == 0) opt_multimouse=false;
      else if (strcmp(var.value, "enabled") == 0) opt_multimouse=true;
   }

   var.key = "puae_keyrah_keypad_mappings";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_keyrah_keypad_mappings value: %s\n", var.value);
   
      if (strcmp(var.value, "disabled") == 0) opt_keyrahkeypad=false;
      else if (strcmp(var.value, "enabled") == 0) opt_keyrahkeypad=true;
   }

   var.key = "puae_physical_keyboard_pass_through";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_physical_keyboard_pass_through value: %s\n", var.value);
   
      if (strcmp(var.value, "disabled") == 0) opt_keyboard_pass_through=false;
      else if (strcmp(var.value, "enabled") == 0) opt_keyboard_pass_through=true;
   }

   var.key = "puae_mapping_options_display";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapping_options_display value: %s\n", var.value);
   
      if (strcmp(var.value, "disabled") == 0) opt_mapping_options_display=0;
      else if (strcmp(var.value, "enabled") == 0) opt_mapping_options_display=1;
   }

   var.key = "puae_video_options_display";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_video_options_display value: %s\n", var.value);
   
      if (strcmp(var.value, "disabled") == 0) opt_video_options_display=0;
      else if (strcmp(var.value, "enabled") == 0) opt_video_options_display=1;
   }

   var.key = "puae_audio_options_display";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_audio_options_display value: %s\n", var.value);
     
      if (strcmp(var.value, "disabled") == 0) opt_audio_options_display=0;
      else if (strcmp(var.value, "enabled") == 0) opt_audio_options_display=1;
   }

   var.key = "puae_retropad_options";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_retropad_options value: %s\n", var.value);
   
      if (strcmp(var.value, "disabled") == 0) opt_retropad_options=0;
      else if (strcmp(var.value, "rotate") == 0) opt_retropad_options=1;
      else if (strcmp(var.value, "jump") == 0) opt_retropad_options=2;
      else if (strcmp(var.value, "rotate_jump") == 0) opt_retropad_options=3;
   }

   var.key = "puae_cd32pad_options";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_cd32pad_options value: %s\n", var.value);
   
      if (strcmp(var.value, "disabled") == 0) opt_cd32pad_options=0;
      else if (strcmp(var.value, "rotate") == 0) opt_cd32pad_options=1;
      else if (strcmp(var.value, "jump") == 0) opt_cd32pad_options=2;
      else if (strcmp(var.value, "rotate_jump") == 0) opt_cd32pad_options=3;
   }

   var.key = "puae_turbo_fire_button";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_turbo_fire_button value: %s\n", var.value);
   
      if (strcmp(var.value, "disabled") == 0) turbo_fire_button=-1;
      else if (strcmp(var.value, "A") == 0) turbo_fire_button=RETRO_DEVICE_ID_JOYPAD_A;
      else if (strcmp(var.value, "Y") == 0) turbo_fire_button=RETRO_DEVICE_ID_JOYPAD_Y;
      else if (strcmp(var.value, "X") == 0) turbo_fire_button=RETRO_DEVICE_ID_JOYPAD_X;
      else if (strcmp(var.value, "L") == 0) turbo_fire_button=RETRO_DEVICE_ID_JOYPAD_L;
      else if (strcmp(var.value, "R") == 0) turbo_fire_button=RETRO_DEVICE_ID_JOYPAD_R;
      else if (strcmp(var.value, "L2") == 0) turbo_fire_button=RETRO_DEVICE_ID_JOYPAD_L2;
      else if (strcmp(var.value, "R2") == 0) turbo_fire_button=RETRO_DEVICE_ID_JOYPAD_R2;
      else if (strcmp(var.value, "L3") == 0) turbo_fire_button=RETRO_DEVICE_ID_JOYPAD_L3;
      else if (strcmp(var.value, "R3") == 0) turbo_fire_button=RETRO_DEVICE_ID_JOYPAD_R3;
   }

   var.key = "puae_turbo_pulse";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_turbo_pulse value: %s\n", var.value);
   
      if (strcmp(var.value, "2") == 0) turbo_pulse=2;
      else if (strcmp(var.value, "4") == 0) turbo_pulse=4;
      else if (strcmp(var.value, "6") == 0) turbo_pulse=6;
      else if (strcmp(var.value, "8") == 0) turbo_pulse=8;
      else if (strcmp(var.value, "10") == 0) turbo_pulse=10;
      else if (strcmp(var.value, "12") == 0) turbo_pulse=12;
   }

   /* Mapper */
   var.key = "puae_mapper_select";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_select value: %s\n", var.value);
   
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_SELECT] = keyId(var.value);
   }

   var.key = "puae_mapper_start";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_start value: %s\n", var.value);
   
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_START] = keyId(var.value);
   }

   var.key = "puae_mapper_b";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_b value: %s\n", var.value);
   
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_B] = keyId(var.value);
   }

   var.key = "puae_mapper_a";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_a value: %s\n", var.value);
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_A] = keyId(var.value);
   }

   var.key = "puae_mapper_y";
   //var.value = NULL;
   var.value = "MOUSE_LEFT_BUTTON";
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_y value: %s\n", var.value);
   
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_Y] = keyId(var.value);
   }

   var.key = "puae_mapper_x";
   //var.value = NULL;
   var.value = "MOUSE_LEFT_BUTTON";
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_x value: %s\n", var.value);
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_X] = keyId(var.value);
   }

   var.key = "puae_mapper_l";
   //var.value = NULL;
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   var.value = "TOGGLE_VKBD";
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_l value: %s\n", var.value);
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_L] = keyId(var.value);
   }

   var.key = "puae_mapper_r";
   //var.value = NULL;
   //if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   var.value = "TOGGLE_VKBD";
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_r value: %s\n", var.value);
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_R] = keyId(var.value);
   }

   var.key = "puae_mapper_l2";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_l2 value: %s\n", var.value);
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_L2] = keyId(var.value);
   }

   var.key = "puae_mapper_r2";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_r2 value: %s\n", var.value);
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_R2] = keyId(var.value);
   }

   var.key = "puae_mapper_l3";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_l3 value: %s\n", var.value);
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_L3] = keyId(var.value);
   }

   var.key = "puae_mapper_r3";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_r3 value: %s\n", var.value);
      mapper_keys[RETRO_DEVICE_ID_JOYPAD_R3] = keyId(var.value);
   }

   var.key = "puae_mapper_lr";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_lr value: %s\n", var.value);
      mapper_keys[16] = keyId(var.value);
   }

   var.key = "puae_mapper_ll";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_ll value: %s\n", var.value);
      mapper_keys[17] = keyId(var.value);
   }

   var.key = "puae_mapper_ld";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_ld value: %s\n", var.value);
      mapper_keys[18] = keyId(var.value);
   }

   var.key = "puae_mapper_lu";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_lu value: %s\n", var.value);
      mapper_keys[19] = keyId(var.value);
   }

   var.key = "puae_mapper_rr";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_rr value: %s\n", var.value);
      mapper_keys[20] = keyId(var.value);
   }

   var.key = "puae_mapper_rl";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_rl value: %s\n", var.value);
      mapper_keys[21] = keyId(var.value);
   }

   var.key = "puae_mapper_rd";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_rd value: %s\n", var.value);
      mapper_keys[22] = keyId(var.value);
   }

   var.key = "puae_mapper_ru";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_ru value: %s\n", var.value);
      mapper_keys[23] = keyId(var.value);
   }

   /* Mapper hotkeys */
   var.key = "puae_mapper_vkbd";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_vkbd value: %s\n", var.value);
      mapper_keys[24] = keyId(var.value);
   }

   var.key = "puae_mapper_statusbar";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_statusbar value: %s\n", var.value);
      mapper_keys[25] = keyId(var.value);
   }

   var.key = "puae_mapper_mouse_toggle";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   { 
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_mouse_toggle value: %s\n", var.value);
      mapper_keys[26] = keyId(var.value);
   }

   var.key = "puae_mapper_reset";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      fprintf(stdout, "[libretro.c] update_variables: puae_mapper_reset value: %s\n", var.value);
      mapper_keys[27] = keyId(var.value);
   }

   var.key = "puae_mapper_aspect_ratio_toggle";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      mapper_keys[28] = keyId(var.value);
   }

   var.key = "puae_mapper_zoom_mode_toggle";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      mapper_keys[29] = keyId(var.value);
   }

   /*** Options display ***/

   /* Mapping options */
   option_display.visible = opt_mapping_options_display;

   option_display.key = "puae_mapper_select";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_start";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_b";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_a";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_y";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_x";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_l";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_r";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_l2";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_r2";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_l3";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_r3";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_lu";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_ld";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_ll";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_lr";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_ru";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_rd";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_rl";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_rr";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_vkbd";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_statusbar";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_mouse_toggle";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_reset";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_aspect_ratio_toggle";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_mapper_zoom_mode_toggle";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

   /* Audio options */
   option_display.visible = opt_audio_options_display;

   option_display.key = "puae_sound_output";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_sound_stereo_separation";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_sound_interpol";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_sound_filter";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_sound_filter_type";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_sound_volume_cd";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_floppy_sound";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_floppy_sound_type";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

   /* Video options */
   option_display.visible = opt_video_options_display;

   option_display.key = "puae_video_resolution";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_video_vresolution";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_video_allow_hz_change";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_video_standard";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_video_aspect";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_zoom_mode";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_vertical_pos";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_horizontal_pos";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_gfx_colors";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_collision_level";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_immediate_blits";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_gfx_framerate";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_statusbar";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_vkbd_theme";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = "puae_vkbd_alpha";
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

   //video_config = PUAE_VIDEO_PAL_HI_DL;
   fprintf(stdout, "Video config: %d\n", video_config);
   
   /* Setting resolution */
   switch (video_config)
   {
		case PUAE_VIDEO_PAL_LO:
			defaultw = PUAE_VIDEO_WIDTH / 2;
			defaulth = PUAE_VIDEO_HEIGHT_PAL / 2;
			strcat(uae_config, "gfx_resolution=lores\n");
			strcat(uae_config, "gfx_linemode=none\n");
			break;
		case PUAE_VIDEO_PAL_HI:
			defaultw = PUAE_VIDEO_WIDTH;
			defaulth = PUAE_VIDEO_HEIGHT_PAL / 2;
			strcat(uae_config, "gfx_resolution=hires\n");
			strcat(uae_config, "gfx_linemode=none\n");
			break;
		case PUAE_VIDEO_PAL_HI_DL:
			defaultw = PUAE_VIDEO_WIDTH;
			defaulth = PUAE_VIDEO_HEIGHT_PAL;
			strcat(uae_config, "gfx_resolution=hires\n");
			strcat(uae_config, "gfx_linemode=double\n");
			break;
		case PUAE_VIDEO_PAL_SUHI:
			defaultw = PUAE_VIDEO_WIDTH * 2;
			defaulth = PUAE_VIDEO_HEIGHT_PAL / 2;
			strcat(uae_config, "gfx_resolution=superhires\n");
			strcat(uae_config, "gfx_linemode=none\n");
			break;
		case PUAE_VIDEO_PAL_SUHI_DL:
			defaultw = PUAE_VIDEO_WIDTH * 2;
			defaulth = PUAE_VIDEO_HEIGHT_PAL;
			strcat(uae_config, "gfx_resolution=superhires\n");
			strcat(uae_config, "gfx_linemode=double\n");
			break;

		case PUAE_VIDEO_NTSC_LO:
			defaultw = PUAE_VIDEO_WIDTH / 2;
			defaulth = PUAE_VIDEO_HEIGHT_NTSC / 2;
			strcat(uae_config, "gfx_resolution=lores\n");
			strcat(uae_config, "gfx_linemode=none\n");
			break;
		case PUAE_VIDEO_NTSC_HI:
			defaultw = PUAE_VIDEO_WIDTH;
			defaulth = PUAE_VIDEO_HEIGHT_NTSC / 2;
			strcat(uae_config, "gfx_resolution=hires\n");
			strcat(uae_config, "gfx_linemode=none\n");
			break;
		case PUAE_VIDEO_NTSC_HI_DL:
			defaultw = PUAE_VIDEO_WIDTH;
			defaulth = PUAE_VIDEO_HEIGHT_NTSC;
			strcat(uae_config, "gfx_resolution=hires\n");
			strcat(uae_config, "gfx_linemode=double\n");
			break;
		case PUAE_VIDEO_NTSC_SUHI:
			defaultw = PUAE_VIDEO_WIDTH * 2;
			defaulth = PUAE_VIDEO_HEIGHT_NTSC / 2;
			strcat(uae_config, "gfx_resolution=superhires\n");
			strcat(uae_config, "gfx_linemode=none\n");
			break;
		case PUAE_VIDEO_NTSC_SUHI_DL:
			defaultw = PUAE_VIDEO_WIDTH * 2;
			defaulth = PUAE_VIDEO_HEIGHT_NTSC;
			strcat(uae_config, "gfx_resolution=superhires\n");
			strcat(uae_config, "gfx_linemode=double\n");
			break;
   }

   /* Always update av_info geometry */
   retro_request_av_info_update = true;

   /* Always trigger changed prefs */
   config_changed = 1;
   check_prefs_changed_audio();
   check_prefs_changed_custom();
   check_prefs_changed_cpu();
   config_changed = 0;
}

//*****************************************************************************
//*****************************************************************************
// Disk control
extern void DISK_reinsert(int num);
extern void disk_eject (int num);

static bool disk_set_eject_state(bool ejected)
{
   if (retro_dc)
   {
      if (retro_dc->eject_state == ejected)
         return true;
      else
         retro_dc->eject_state = ejected;

      if (retro_dc->files[retro_dc->index] > 0 && file_exists(retro_dc->files[retro_dc->index]))
          display_current_image(((!retro_dc->eject_state) ? retro_dc->labels[retro_dc->index] : ""), !retro_dc->eject_state);

      if (retro_dc->eject_state)
      {
         if (retro_dc->files[retro_dc->index] > 0)
         {
            if (retro_dc->types[retro_dc->index] == DC_IMAGE_TYPE_FLOPPY)
            {
               changed_prefs.floppyslots[0].df[0] = 0;
               disk_eject(0);
            }
            else if (retro_dc->types[retro_dc->index] == DC_IMAGE_TYPE_CD)
            {
               changed_prefs.cdslots[0].name[0] = 0;
               check_changes(0);
            }
         }
      }
      else
      {
         if (retro_dc->files[retro_dc->index] > 0 && file_exists(retro_dc->files[retro_dc->index]))
         {
            if (retro_dc->types[retro_dc->index] == DC_IMAGE_TYPE_FLOPPY)
            {
               strcpy (changed_prefs.floppyslots[0].df, retro_dc->files[retro_dc->index]);
               DISK_reinsert(0);
            }
            else if (retro_dc->types[retro_dc->index] == DC_IMAGE_TYPE_CD)
            {
               strcpy (changed_prefs.cdslots[0].name, retro_dc->files[retro_dc->index]);
               check_changes(0);
            }
         }
      }
   }
   return true;
}

static bool disk_get_eject_state(void)
{
   if (retro_dc)
      return retro_dc->eject_state;

   return true;
}

static unsigned disk_get_image_index(void)
{
   if (retro_dc)
      return retro_dc->index;

   return 0;
}

static bool disk_set_image_index(unsigned index)
{
   // Insert disk
   if (retro_dc)
   {
      // Same disk...
      // This can mess things in the emu
      if (index == retro_dc->index)
         return true;

      if ((index < retro_dc->count) && (retro_dc->files[index]))
      {
         retro_dc->index = index;
         display_current_image(retro_dc->labels[retro_dc->index], false);
         fprintf(stdout, "[libretro-uae]: Disk (%d) inserted into drive DF0: '%s'\n", retro_dc->index+1, retro_dc->files[retro_dc->index]);
         return true;
      }
   }

   return false;
}

static unsigned disk_get_num_images(void)
{
   if (retro_dc)
      return retro_dc->count;

   return 0;
}

static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
   if (retro_dc)
   {
      if (index >= retro_dc->count)
         return false;

      if (retro_dc->files[index])
      {
         free(retro_dc->files[index]);
         retro_dc->files[index] = NULL;
      }

      if (retro_dc->labels[index])
      {
         free(retro_dc->labels[index]);
         retro_dc->labels[index] = NULL;
      }

      retro_dc->types[index] = DC_IMAGE_TYPE_NONE;

      // TODO : Handling removing of a disk image when info = NULL
      if (info != NULL)
      {
         if (!string_is_empty(info->path))
         {
            char image_label[RETRO_PATH_MAX];

            image_label[0] = '\0';

            // File path
            retro_dc->files[index] = strdup(info->path);

            // Image label
            fill_short_pathname_representation(image_label, info->path, sizeof(image_label));
            retro_dc->labels[index] = strdup(image_label);

            // Image type
            retro_dc->types[index] = dc_get_image_type(info->path);

            return true;
         }
      }
   }

   return false;
}

static bool disk_add_image_index(void)
{
   if (retro_dc)
   {
      if (retro_dc->count <= DC_MAX_SIZE)
      {
         retro_dc->files[retro_dc->count]  = NULL;
         retro_dc->labels[retro_dc->count] = NULL;
         retro_dc->types[retro_dc->count]  = DC_IMAGE_TYPE_NONE;
         retro_dc->count++;
         return true;
      }
   }

   return false;
}

static bool disk_get_image_path(unsigned index, char *path, size_t len)
{
   if (len < 1)
      return false;

   if (retro_dc)
   {
      if (index < retro_dc->count)
      {
         if (!string_is_empty(retro_dc->files[index]))
         {
            strlcpy(path, retro_dc->files[index], len);
            return true;
         }
      }
   }

   return false;
}

static bool disk_get_image_label(unsigned index, char *label, size_t len)
{
   if (len < 1)
      return false;

   if (retro_dc)
   {
      if (index < retro_dc->count)
      {
         if (!string_is_empty(retro_dc->labels[index]))
         {
            strlcpy(label, retro_dc->labels[index], len);
            return true;
         }
      }
   }

   return false;
}

static struct retro_disk_control_callback disk_interface = {
   disk_set_eject_state,
   disk_get_eject_state,
   disk_get_image_index,
   disk_set_image_index,
   disk_get_num_images,
   disk_replace_image_index,
   disk_add_image_index,
};

static struct retro_disk_control_ext_callback disk_interface_ext = {
   disk_set_eject_state,
   disk_get_eject_state,
   disk_get_image_index,
   disk_set_image_index,
   disk_get_num_images,
   disk_replace_image_index,
   disk_add_image_index,
   NULL, // set_initial_image
   disk_get_image_path,
   disk_get_image_label,
};

//*****************************************************************************
//*****************************************************************************
// Init
void retro_init(void)
{
   fprintf(stdout, "[libretro.c] retro_init: Initializing core.\n");

   uae_devices[0] = RETRO_DEVICE_JOYPAD;
   uae_devices[1] = RETRO_DEVICE_JOYPAD;

   path_mkdir("/tmp/amiga/WHDLoad");

   libretro_runloop_active = 0;

   const char *system_dir = "/tmp";
   strlcpy(
            retro_system_directory,
            system_dir,
            sizeof(retro_system_directory));   
   /*
   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir)
   {
      // if defined, use the system directory
      strlcpy(
            retro_system_directory,
            system_dir,
            sizeof(retro_system_directory));
   }
   */
   
   const char *content_dir = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY, &content_dir) && content_dir)
   {
      // if defined, use the system directory
      strlcpy(
            retro_content_directory,
            content_dir,
            sizeof(retro_content_directory));
   }

   const char *save_dir = "/tmp/amiga";
   strlcpy(
            retro_save_directory,
            string_is_empty(save_dir) ? retro_system_directory : save_dir,
            sizeof(retro_save_directory));   
   /*
   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) && save_dir)
   {
      // If save directory is defined use it, otherwise use system directory
      strlcpy(
            retro_save_directory,
            string_is_empty(save_dir) ? retro_system_directory : save_dir,
            sizeof(retro_save_directory));
   }
   else
   {
      // make retro_save_directory the same in case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY is not implemented by the frontend
      strlcpy(
            retro_save_directory,
            retro_system_directory,
            sizeof(retro_save_directory));
   }
   */
   //printf("Retro SYSTEM_DIRECTORY %s\n",retro_system_directory);
   //printf("Retro SAVE_DIRECTORY %s\n",retro_save_directory);
   //printf("Retro CONTENT_DIRECTORY %s\n",retro_content_directory);

   fprintf(stdout, "[libretro.c] retro_init: Creating disk controller interface.\n");
   // Disk control interface
   retro_dc = dc_create();

   unsigned dci_version = 0;
   if (environ_cb(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &dci_version) && (dci_version >= 1))
      environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &disk_interface_ext);
   else
      environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_interface);

   // Savestates
   // > Considered incomplete because runahead cannot
   //   be enabled until content is full loaded
   static uint64_t quirks = RETRO_SERIALIZATION_QUIRK_INCOMPLETE;
   environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &quirks);

   // > Ensure save state de-serialization file
   //   is closed/NULL
   //   (redundant safety check, possibly required
   //   for static builds...)
   if (retro_deserialize_file)
   {
      zfile_fclose(retro_deserialize_file);
      retro_deserialize_file = NULL;
   }

   // Inputs
   #define RETRO_DESCRIPTOR_BLOCK( _user )                                            \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A / 2nd fire / Blue" },\
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B / Fire / Red" },  \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "X / Yellow" },      \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y / Green" },       \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },     \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start / Play" },\
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },       \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },         \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },             \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },         \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R / Forward" },         \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L / Rewind" },         \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2" },             \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2" },             \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3" },             \
   { _user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3" },             \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },               \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },               \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },             \
   { _user, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" }

   static struct retro_input_descriptor input_descriptors[] =
   {
      RETRO_DESCRIPTOR_BLOCK( 0 ),
      RETRO_DESCRIPTOR_BLOCK( 1 ),
      RETRO_DESCRIPTOR_BLOCK( 2 ),
      RETRO_DESCRIPTOR_BLOCK( 3 ),
      { 0 },
   };
   #undef RETRO_DESCRIPTOR_BLOCK
   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, &input_descriptors);

   memset(key_state, 0, sizeof(key_state));
   memset(key_state2, 0, sizeof(key_state2));

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      fprintf(stderr, "[libretro-uae]: RGB565 is not supported.\n");
      exit(0);//return false;
   }

   memset(retro_bmp, 0, sizeof(retro_bmp));

   fprintf(stdout, "[libretro.c] retro_init: Updating variables.\n");

   update_variables();
   
   fprintf(stdout, "[libretro.c] retro_init: Initialization complete.\n");
}

static void remove_recurse(const char *path)
{
   struct dirent *dirp;
   char filename[RETRO_PATH_MAX];
   DIR *dir = opendir(path);
   if (dir == NULL)
      return;

   while ((dirp = readdir(dir)) != NULL)
   {
      if (dirp->d_name[0] == '.')
         continue;

      sprintf(filename, "%s%s%s", path, DIR_SEP_STR, dirp->d_name);
      fprintf(stdout, "Unzip clean: %s\n", filename);

      if (path_is_directory(filename))
         remove_recurse(filename);
      else
         remove(filename);
   }

   closedir(dir);
   rmdir(path);
}

void retro_deinit(void)
{	
   // Clean the m3u storage
   if (retro_dc)
      dc_free(retro_dc);

   // Clean ZIP temp
   if (retro_temp_directory && path_is_directory(retro_temp_directory))
      remove_recurse(retro_temp_directory);
   
   // Clean up WHDLoad and Kickstart files
   remove_recurse("/tmp/amiga");
   remove("/tmp/kick40068.A4000");  
   remove("/tmp/kick40068.A1200");  
   remove("/tmp/kick34005.A500");
   remove("/tmp/WHDLoad.zip"); 
   
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   /* ALU retroplayer does not call this method, so for testing consistency with RetroArch, comment it out.
   if (port<4)
   {
      uae_devices[port] = device;
      int uae_port;
      uae_port = (port==0) ? 1 : 0;
      cd32_pad_enabled[uae_port] = 0;
      switch (device)
      {
         case RETRO_DEVICE_JOYPAD:
            fprintf(stdout, "[libretro-uae]: Controller %u: RetroPad\n", (port+1));
            break;

         case RETRO_DEVICE_UAE_CD32PAD:
            fprintf(stdout, "[libretro-uae]: Controller %u: CD32 Pad\n", (port+1));
            cd32_pad_enabled[uae_port]=1;
            break;

         case RETRO_DEVICE_UAE_ANALOG:
            fprintf(stdout, "[libretro-uae]: Controller %u: Analog Joystick\n", (port+1));
            break;

         case RETRO_DEVICE_UAE_JOYSTICK:
            fprintf(stdout, "[libretro-uae]: Controller %u: Joystick\n", (port+1));
            break;

         case RETRO_DEVICE_UAE_KEYBOARD:
            fprintf(stdout, "[libretro-uae]: Controller %u: Keyboard\n", (port+1));
            break;

         case RETRO_DEVICE_NONE:
            fprintf(stdout, "[libretro-uae]: Controller %u: Unplugged\n", (port+1));
            break;
      }

      if (inputdevice_finalized)
         inputdevice_updateconfig(NULL, &currprefs);
   }
   */
}

void retro_get_system_info(struct retro_system_info *info)
{
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   memset(info, 0, sizeof(*info));
   info->library_name     = "PUAE";
   info->library_version  = "2.6.1" GIT_VERSION;
   info->need_fullpath    = true;
   info->block_extract    = true;
   info->valid_extensions = "adf|adz|dms|fdi|ipf|hdf|hdz|lha|cue|ccd|nrg|mds|iso|uae|m3u|zip";
}

float retro_get_aspect_ratio(int w, int h)
{
   static float ar = 1;

   if (video_config_geometry & PUAE_VIDEO_NTSC)
      ar = ((float)w / (float)h) * (44.0 / 52.0);
   else
      ar = ((float)w / (float)h);

   if (video_config_geometry & PUAE_VIDEO_DOUBLELINE)
   {
      if (video_config_geometry & PUAE_VIDEO_HIRES)
         ;
      else if (video_config_geometry & PUAE_VIDEO_SUPERHIRES)
         ar = ar / 2;
   }
   else
   {
      if (video_config_geometry & PUAE_VIDEO_HIRES)
         ar = ar / 2;
      else if (video_config_geometry & PUAE_VIDEO_SUPERHIRES)
         ar = ar / 4;
   }

   return ar;
}

static bool retro_update_av_info(void)
{
   bool av_log        = false;
   bool change_timing = retro_av_info_change_timing;
   bool isntsc        = retro_av_info_is_ntsc;
   float hz           = currprefs.chipset_refreshrate;

   /* Reset global parameters ready for the
    * next update */
   retro_request_av_info_update = false;
   retro_av_info_change_timing  = false;
   retro_av_info_is_ntsc        = false;

   if (av_log)
      fprintf(stdout, "[libretro-uae]: Trying to update AV timing:%d to: ntsc:%d hz:%0.4f, from video_config:%d, video_aspect:%d\n", change_timing, isntsc, hz, video_config, video_config_aspect);

   /* Change PAL/NTSC with a twist, thanks to Dyna Blaster

      Early Startup switch looks proper:
         PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
         NTSC mode V=59.8859Hz H=15590.7473Hz (227x262+1) IDX=11 (NTSC) D=0 RTG=0/0
         PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0

      Dyna Blaster switch looks unorthodox:
         PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
         PAL mode V=59.4106Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
         PAL mode V=49.9201Hz H=15625.0881Hz (227x312+1) IDX=10 (PAL) D=0 RTG=0/0
   */

   video_config_old = video_config;
   video_config_geometry = video_config;

   /* When timing & geometry is changed */
   if (change_timing)
   {
      /* Change to NTSC if not NTSC */
      if (isntsc && (video_config & PUAE_VIDEO_PAL) && !fake_ntsc)
      {
         video_config |= PUAE_VIDEO_NTSC;
         video_config &= ~PUAE_VIDEO_PAL;
      }
      /* Change to PAL if not PAL */
      else if (!isntsc && (video_config & PUAE_VIDEO_NTSC) && !fake_ntsc)
      {
         video_config |= PUAE_VIDEO_PAL;
         video_config &= ~PUAE_VIDEO_NTSC;
      }

      /* Main video config will be changed too */
      video_config_geometry = video_config;
   }

   /* Aspect ratio override always changes only temporary video config */
   if (video_config_aspect == PUAE_VIDEO_NTSC)
   {
      video_config_geometry |= PUAE_VIDEO_NTSC;
      video_config_geometry &= ~PUAE_VIDEO_PAL;
   }
   else if (video_config_aspect == PUAE_VIDEO_PAL)
   {
      video_config_geometry |= PUAE_VIDEO_PAL;
      video_config_geometry &= ~PUAE_VIDEO_NTSC;
   }

   /* Do nothing if timing has not changed, unless Hz switched without isntsc */
   if (video_config_old == video_config && change_timing)
   {
      /* Dyna Blaster and the like stays at fake NTSC to prevent pointless switching back and forth */
      if (!isntsc && hz > 55)
      {
         video_config |= PUAE_VIDEO_NTSC;
         video_config &= ~PUAE_VIDEO_PAL;
         video_config_geometry = video_config;
         fake_ntsc=true;
      }

      /* If still no change */
      if (video_config_old == video_config)
      {
         if (av_log)
            fprintf(stdout, "[libretro-uae]: Already at wanted AV\n");
         change_timing = false; // Allow other calculations but don't alter timing
      }
   }

   /* Horizontal centering thresholds */
   static int min_diwstart_limit_hires = 220;
   static int max_diwstop_limit_hires = 600;
   static int min_diwstart_limit = 220;
   static int max_diwstop_limit = 600;

   /* Geometry dimensions */
   switch (video_config_geometry)
   {
      case PUAE_VIDEO_PAL_LO:
         retrow = PUAE_VIDEO_WIDTH / 2;
         retroh = PUAE_VIDEO_HEIGHT_PAL / 2;
         min_diwstart_limit = min_diwstart_limit_hires / 2;
         max_diwstop_limit = max_diwstop_limit_hires / 2;
         break;
      case PUAE_VIDEO_PAL_HI:
         retrow = PUAE_VIDEO_WIDTH;
         retroh = PUAE_VIDEO_HEIGHT_PAL / 2;
         min_diwstart_limit = min_diwstart_limit_hires;
         max_diwstop_limit = max_diwstop_limit_hires;
         break;
      case PUAE_VIDEO_PAL_HI_DL:
         retrow = PUAE_VIDEO_WIDTH;
         retroh = PUAE_VIDEO_HEIGHT_PAL;
         min_diwstart_limit = min_diwstart_limit_hires;
         max_diwstop_limit = max_diwstop_limit_hires;
         break;
      case PUAE_VIDEO_PAL_SUHI:
         retrow = PUAE_VIDEO_WIDTH * 2;
         retroh = PUAE_VIDEO_HEIGHT_PAL / 2;
         min_diwstart_limit = min_diwstart_limit_hires * 2;
         max_diwstop_limit = max_diwstop_limit_hires * 2;
         break;
      case PUAE_VIDEO_PAL_SUHI_DL:
         retrow = PUAE_VIDEO_WIDTH * 2;
         retroh = PUAE_VIDEO_HEIGHT_PAL;
         min_diwstart_limit = min_diwstart_limit_hires * 2;
         max_diwstop_limit = max_diwstop_limit_hires * 2;
         break;

      case PUAE_VIDEO_NTSC_LO:
         retrow = PUAE_VIDEO_WIDTH / 2;
         retroh = PUAE_VIDEO_HEIGHT_NTSC / 2;
         min_diwstart_limit = min_diwstart_limit_hires / 2;
         max_diwstop_limit = max_diwstop_limit_hires / 2;
         break;
      case PUAE_VIDEO_NTSC_HI:
         retrow = PUAE_VIDEO_WIDTH;
         retroh = PUAE_VIDEO_HEIGHT_NTSC / 2;
         min_diwstart_limit = min_diwstart_limit_hires;
         max_diwstop_limit = max_diwstop_limit_hires;
         break;
      case PUAE_VIDEO_NTSC_HI_DL:
         retrow = PUAE_VIDEO_WIDTH;
         retroh = PUAE_VIDEO_HEIGHT_NTSC;
         min_diwstart_limit = min_diwstart_limit_hires;
         max_diwstop_limit = max_diwstop_limit_hires;
         break;
      case PUAE_VIDEO_NTSC_SUHI:
         retrow = PUAE_VIDEO_WIDTH * 2;
         retroh = PUAE_VIDEO_HEIGHT_NTSC / 2;
         min_diwstart_limit = min_diwstart_limit_hires * 2;
         max_diwstop_limit = max_diwstop_limit_hires * 2;
         break;
      case PUAE_VIDEO_NTSC_SUHI_DL:
         retrow = PUAE_VIDEO_WIDTH * 2;
         retroh = PUAE_VIDEO_HEIGHT_NTSC;
         min_diwstart_limit = min_diwstart_limit_hires * 2;
         max_diwstop_limit = max_diwstop_limit_hires * 2;
         break;
   }

   /* Exception for Dyna Blaster */
   if (fake_ntsc)
      retroh = (video_config & PUAE_VIDEO_DOUBLELINE) ? 476 : 238;

   /* When the actual dimensions change and not just the view */
   if (change_timing)
   {
      defaultw = retrow;
      defaulth = retroh;
   }

   static struct retro_system_av_info new_av_info;
   retro_get_system_av_info(&new_av_info);

   /* Disable Hz change if not allowed */
   if (!video_config_allow_hz_change)
      change_timing = false;

   /* Timing or geometry update */
   if (change_timing)
   {
      new_av_info.timing.fps = hz;
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &new_av_info);
   }
   else
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &new_av_info);

   /* Ensure statusbar stays visible at the bottom */
   opt_statusbar_position_offset = 0;
   opt_statusbar_position = opt_statusbar_position_old;
   if (!change_timing)
      if (retroh < defaulth)
         if (opt_statusbar_position >= 0 && (defaulth - retroh) >= opt_statusbar_position)
            opt_statusbar_position = defaulth - retroh;

   /* Aspect offset for zoom mode */
   opt_statusbar_position_offset = opt_statusbar_position_old - opt_statusbar_position;

   /* Compensate for the PAL last line  */
   if (video_config_geometry & PUAE_VIDEO_PAL && retroh == defaulth && opt_statusbar_position == 0)
   {
      if (interlace_seen)
      {
         if (video_config_geometry & PUAE_VIDEO_DOUBLELINE)
         {
            opt_statusbar_position += 2;
            opt_statusbar_position_offset += 2;
         }
         else
         {
            opt_statusbar_position += 1;
            opt_statusbar_position_offset += 1;
         }
      }
      else
      {
         if (video_config_geometry & PUAE_VIDEO_DOUBLELINE)
         {
            opt_statusbar_position += 1;
            opt_statusbar_position_offset += 1;
         }
         else
         {
            opt_statusbar_position += 0;
            opt_statusbar_position_offset += 0;
         }
      }
   }

   //fprintf(stdout, "statusbar:%3d old:%3d offset:%3d, defaulth:%d retroh:%d\n", opt_statusbar_position, opt_statusbar_position_old, opt_statusbar_position_offset, defaulth, retroh);

   /* Apply zoom mode if necessary */
   switch (zoom_mode_id)
   {
      case 1:
         if (video_config & PUAE_VIDEO_DOUBLELINE)
            zoomed_height = (video_config_geometry & PUAE_VIDEO_NTSC) ? 480 : 540;
         else
            zoomed_height = (video_config_geometry & PUAE_VIDEO_NTSC) ? 240 : 270;
         break;
      case 2:
         if (video_config & PUAE_VIDEO_DOUBLELINE)
            zoomed_height = (video_config_geometry & PUAE_VIDEO_NTSC) ? 474 : 524;
         else
            zoomed_height = (video_config_geometry & PUAE_VIDEO_NTSC) ? 237 : 262;
         break;
      case 3:
         if (video_config & PUAE_VIDEO_DOUBLELINE)
            zoomed_height = (video_config_geometry & PUAE_VIDEO_NTSC) ? 470 : 512;
         else
            zoomed_height = (video_config_geometry & PUAE_VIDEO_NTSC) ? 235 : 256;
         break;
      case 4:
         if (video_config & PUAE_VIDEO_DOUBLELINE)
            zoomed_height = (video_config_geometry & PUAE_VIDEO_NTSC) ? 460 : 480;
         else
            zoomed_height = (video_config_geometry & PUAE_VIDEO_NTSC) ? 230 : 240;
         break;
      case 5:
         if (video_config & PUAE_VIDEO_DOUBLELINE)
            zoomed_height = 448;
         else
            zoomed_height = 224;
         break;
      case 6:
         if (video_config & PUAE_VIDEO_DOUBLELINE)
            zoomed_height = 432;
         else
            zoomed_height = 216;
         break;
      case 7:
         if (video_config & PUAE_VIDEO_DOUBLELINE)
            zoomed_height = 400;
         else
            zoomed_height = 200;
         break;
      case 8:
         if (retro_thisframe_first_drawn_line != retro_thisframe_last_drawn_line
          && retro_thisframe_first_drawn_line > 0 && retro_thisframe_last_drawn_line > 0
         )
         {
            zoomed_height = retro_thisframe_last_drawn_line - retro_thisframe_first_drawn_line + 1;
            zoomed_height = (video_config & PUAE_VIDEO_DOUBLELINE) ? zoomed_height * 2 : zoomed_height;
         }

         if (video_config & PUAE_VIDEO_DOUBLELINE)
            zoomed_height = (zoomed_height < 400) ? 400 : zoomed_height;
         else
            zoomed_height = (zoomed_height < 200) ? 200 : zoomed_height;
         break;
      default:
         zoomed_height = retroh;
         break;
   }

   if (zoomed_height > retroh)
      zoomed_height = retroh;

   if (zoomed_height != retroh)
   {
      new_av_info.geometry.base_height = zoomed_height;
      new_av_info.geometry.aspect_ratio = retro_get_aspect_ratio(retrow, zoomed_height);
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &new_av_info);

      /* Ensure statusbar stays visible at the bottom */
      if (opt_statusbar_position >= 0 && (retroh - zoomed_height - opt_statusbar_position_offset) >= opt_statusbar_position)
         opt_statusbar_position = retroh - zoomed_height - opt_statusbar_position_offset;

      //fprintf(stdout, "ztatusbar:%3d old:%3d offset:%3d, defaulth:%d retroz:%d\n", opt_statusbar_position, opt_statusbar_position_old, opt_statusbar_position_offset, defaulth, zoomed_height);
   }

   /* If zoom mode should be vertically centered automagically */
   if (opt_vertical_offset_auto && (zoom_mode_id != 0 || zoomed_height != retroh))
   {
      int zoomed_height_normal = (video_config & PUAE_VIDEO_DOUBLELINE) ? zoomed_height / 2 : zoomed_height;
      int thisframe_y_adjust_new = minfirstline;

      /* Need proper values for calculations */
      if (retro_thisframe_first_drawn_line != retro_thisframe_last_drawn_line
       && retro_thisframe_first_drawn_line > 0 && retro_thisframe_last_drawn_line > 0
       && (retro_thisframe_first_drawn_line < 150 || retro_thisframe_last_drawn_line > 150)
      )
         thisframe_y_adjust_new = (retro_thisframe_last_drawn_line - retro_thisframe_first_drawn_line - zoomed_height_normal) / 2 + retro_thisframe_first_drawn_line; // Smart
         //thisframe_y_adjust_new = retro_thisframe_first_drawn_line + ((retro_thisframe_last_drawn_line - retro_thisframe_first_drawn_line) - zoomed_height_normal) / 2; // Simple

      /* Sensible limits */
      thisframe_y_adjust_new = (thisframe_y_adjust_new < 0) ? 0 : thisframe_y_adjust_new;
      thisframe_y_adjust_new = (thisframe_y_adjust_new > (minfirstline + 50)) ? (minfirstline + 50) : thisframe_y_adjust_new;
      if (retro_thisframe_first_drawn_line == -1 && retro_thisframe_last_drawn_line == -1)
          thisframe_y_adjust_new = thisframe_y_adjust_old;

      /* Change value only if altered */
      if (thisframe_y_adjust != thisframe_y_adjust_new)
         thisframe_y_adjust = thisframe_y_adjust_new;

      //fprintf(stdout, "FIRSTDRAWN:%6d LASTDRAWN:%6d   yadjust:%d old:%d zoomed_height:%d\n", retro_thisframe_first_drawn_line, retro_thisframe_last_drawn_line, thisframe_y_adjust, thisframe_y_adjust_old, zoomed_height);

      /* Remember the previous value */
      thisframe_y_adjust_old = thisframe_y_adjust;
   }
   else
      thisframe_y_adjust = minfirstline + opt_vertical_offset;

   /* Horizontal centering */
   if (opt_horizontal_offset_auto)
   {
      int visible_left_border_new = retro_max_diwlastword - retrow;
      int diw_multiplier = 1;
      if (video_config_geometry & PUAE_VIDEO_HIRES)
         diw_multiplier = 2;
      else if (video_config_geometry & PUAE_VIDEO_SUPERHIRES)
         diw_multiplier = 4;

      /* Need proper values for calculations */
      if (retro_min_diwstart != retro_max_diwstop
       && retro_min_diwstart > 0 && retro_max_diwstop > 0
       && retro_min_diwstart < min_diwstart_limit
       && retro_max_diwstop > max_diwstop_limit
       && (retro_max_diwstop - retro_min_diwstart) <= (retrow + 2*diw_multiplier)
      )
      {
         visible_left_border_new = (retro_max_diwstop - retro_min_diwstart - retrow) / 2 + retro_min_diwstart; // Smart
         //visible_left_border_new = retro_max_diwstop - retrow - (retro_max_diwstop - retro_min_diwstart - retrow) / 2; // Simple
      }
      else if (retro_min_diwstart == 30000 && retro_max_diwstop == 0)
         visible_left_border_new = visible_left_border;

      /* Change value only if altered */
      if (visible_left_border != visible_left_border_new)
         visible_left_border = visible_left_border_new;

      //fprintf(stdout, "DIWSTART  :%6d DIWSTOP  :%6d   left_border:%d old:%d\n", retro_min_diwstart, retro_max_diwstop, visible_left_border, visible_left_border_old);

      /* Remember the previous value */
      visible_left_border_old = visible_left_border;
   }

   /* Logging */
   if (av_log)
   {
      if (change_timing)
         fprintf(stdout, "[libretro-uae]: Update av_info: %dx%d %0.4fHz, zoomed_height:%d, video_config:%d\n", retrow, retroh, hz, zoomed_height, video_config_geometry);
      else
         fprintf(stdout, "[libretro-uae]: Update geometry: %dx%d zoomed_height:%d, video_config:%d\n", retrow, retroh, zoomed_height, video_config_geometry);
   }

   /* Triggers check_prefs_changed_gfx() in vsync_handle_check() */
   prefs_changed = 1;

   /* Changing any drawing/offset parameters requires
    * a drawing reset - it is safest to just do this
    * whenever retro_update_av_info() is called */
   request_reset_drawing = true;

   return true;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   /* need to do this here because core option values are not available in retro_init */
   if (!pix_bytes_initialized)
   {
      pix_bytes_initialized = true;
      if (pix_bytes == 4)
      {
         enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
         if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
         {
            pix_bytes = 2;
            fprintf(stderr, "[libretro-uae]: XRGB8888 is not supported. Trying RGB565\n");
            fmt = RETRO_PIXEL_FORMAT_RGB565;
            if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
            {
               fprintf(stderr, "[libretro-uae]: RGB565 is not supported\n");
               exit(0);//return false;
            }
         }
      }
   }

   static struct retro_game_geometry geom;
   geom.base_width = retrow;
   geom.base_height = retroh;
   geom.max_width = EMULATOR_MAX_WIDTH;
   geom.max_height = EMULATOR_MAX_HEIGHT;
   geom.aspect_ratio = retro_get_aspect_ratio(retrow, retroh);

   info->geometry = geom;
   info->timing.sample_rate = 44100.0;
   info->timing.fps = (retro_get_region() == RETRO_REGION_NTSC) ? PUAE_VIDEO_HZ_NTSC : PUAE_VIDEO_HZ_PAL;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_audio_cb(short l, short r)
{
   audio_cb(l, r);
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_audio_batch_cb(const int16_t *data, size_t frames)
{
   audio_batch_cb(data, frames);
}

bool retro_create_config()
{
   RPATH[0] = '\0';
   path_join((char*)&RPATH, retro_save_directory, LIBRETRO_PUAE_PREFIX ".uae");
   fprintf(stdout, "[libretro-uae]: Generating temporary config file '%s'\n", (const char*)&RPATH);

   if (strcmp(opt_model, "A500") == 0)
   {
      strcat(uae_machine, A500_CONFIG);
      strcpy(uae_kickstart, A500_ROM);
   }
   else if (strcmp(opt_model, "A500OG") == 0)
   {
      strcat(uae_machine, A500OG_CONFIG);
      strcpy(uae_kickstart, A500_ROM);
   }
   else if (strcmp(opt_model, "A500PLUS") == 0)
   {
      strcat(uae_machine, A500PLUS_CONFIG);
      strcpy(uae_kickstart, A500KS2_ROM);
   }
   else if (strcmp(opt_model, "A600") == 0)
   {
      strcat(uae_machine, A600_CONFIG);
      strcpy(uae_kickstart, A600_ROM);
   }
   else if (strcmp(opt_model, "A1200") == 0)
   {
      strcat(uae_machine, A1200_CONFIG);
      strcpy(uae_kickstart, A1200_ROM);
   }
   else if (strcmp(opt_model, "A1200OG") == 0)
   {
      strcat(uae_machine, A1200OG_CONFIG);
      strcpy(uae_kickstart, A1200_ROM);
   }
   else if (strcmp(opt_model, "A4030") == 0)
   {
      strcat(uae_machine, A4030_CONFIG);
      strcpy(uae_kickstart, A4000_ROM);
   }
   else if (strcmp(opt_model, "A4040") == 0)
   {
      strcat(uae_machine, A4040_CONFIG);
      strcpy(uae_kickstart, A4000_ROM);
   }
   else if (strcmp(opt_model, "CD32") == 0)
   {
      strcat(uae_machine, CD32_CONFIG);
      strcpy(uae_kickstart, CD32_ROM);
      strcpy(uae_kickstart_ext, CD32_ROM_EXT);
   }
   else if (strcmp(opt_model, "CD32FR") == 0)
   {
      strcat(uae_machine, CD32FR_CONFIG);
      strcpy(uae_kickstart, CD32_ROM);
      strcpy(uae_kickstart_ext, CD32_ROM_EXT);
   }
   else if (strcmp(opt_model, "auto") == 0)
   {
      if (opt_use_boot_hd)
      {
         strcat(uae_machine, A600_CONFIG);
         strcpy(uae_kickstart, A600_ROM);
      }
      else
      {
         strcat(uae_machine, A500_CONFIG);
         strcpy(uae_kickstart, A500_ROM);
      }
   }

   char boothd_size[5] = {0};
   snprintf(boothd_size, sizeof(boothd_size), "%dM", 0);
   if (opt_use_boot_hd > 1)
   {
      switch (opt_use_boot_hd)
      {
         case 2:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 20);
            break;
         case 3:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 40);
            break;
         case 4:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 80);
            break;
         case 5:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 128);
            break;
         case 6:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 256);
            break;
         case 7:
            snprintf(boothd_size, sizeof(boothd_size), "%dM", 512);
            break;
      }
   }

   if (!string_is_empty(full_path) && (file_exists(full_path) || path_is_directory(full_path)))
   {
      // Extract ZIP for examination
      if (strendswith(full_path, "zip"))
      {
         char zip_basename[RETRO_PATH_MAX];
         snprintf(zip_basename, sizeof(zip_basename), "%s", path_basename(full_path));
         snprintf(zip_basename, sizeof(zip_basename), "%s", path_remove_extension(zip_basename));
         snprintf(retro_temp_directory, sizeof(retro_temp_directory), "%s%s%s", retro_save_directory, DIR_SEP_STR, "ZIP");
         char zip_path[RETRO_PATH_MAX];
         snprintf(zip_path, sizeof(zip_path), "%s%s%s", retro_temp_directory, DIR_SEP_STR, zip_basename);

         path_mkdir(zip_path);
         zip_uncompress(full_path, zip_path);

         int zip_mode = 0;
         FILE * zip_m3u;
         char zip_m3u_path[RETRO_PATH_MAX];
         snprintf(zip_m3u_path, sizeof(zip_m3u_path), "%s%s%s.m3u", zip_path, DIR_SEP_STR, zip_basename);
         zip_m3u = fopen(zip_m3u_path, "w");

         DIR *zip_dir;
         struct dirent *zip_dirp;
         zip_dir = opendir(zip_path);
         while ((zip_dirp = readdir(zip_dir)) != NULL && zip_mode == 0)
         {
            if (zip_dirp->d_name[0] == '.' || strendswith(zip_dirp->d_name, M3U_FILE_EXT))
               continue;

            // Disk mode, create M3U
            if (strendswith(zip_dirp->d_name, ADF_FILE_EXT)
             || strendswith(zip_dirp->d_name, FDI_FILE_EXT)
             || strendswith(zip_dirp->d_name, DMS_FILE_EXT)
             || strendswith(zip_dirp->d_name, IPF_FILE_EXT))
            {
               fprintf(zip_m3u, "%s\n", zip_dirp->d_name);
               snprintf(full_path, sizeof(full_path), "%s", zip_m3u_path);
               zip_mode = 0;
            }
            // Single file mode
            else if (strendswith(zip_dirp->d_name, HDF_FILE_EXT)
                  || strendswith(zip_dirp->d_name, CUE_FILE_EXT)
                  || strendswith(zip_dirp->d_name, CCD_FILE_EXT)
                  || strendswith(zip_dirp->d_name, NRG_FILE_EXT)
                  || strendswith(zip_dirp->d_name, MDS_FILE_EXT)
                  || strendswith(zip_dirp->d_name, ISO_FILE_EXT))
            {
               snprintf(full_path, sizeof(full_path), "%s%s%s", zip_path, DIR_SEP_STR, zip_dirp->d_name);
               zip_mode = 1;
            }
            // Directory mode
            else
            {
               snprintf(full_path, sizeof(full_path), "%s", zip_path);
               zip_mode = 2;
            }
         }

         fclose(zip_m3u);
         if (zip_mode > 0)
            remove(zip_m3u_path);
         closedir(zip_dir);
      }

      // If argument is a disk or hard drive image file
      if (strendswith(full_path, ADF_FILE_EXT)
       || strendswith(full_path, ADZ_FILE_EXT)
       || strendswith(full_path, FDI_FILE_EXT)
       || strendswith(full_path, DMS_FILE_EXT)
       || strendswith(full_path, IPF_FILE_EXT)
       || strendswith(full_path, HDF_FILE_EXT)
       || strendswith(full_path, HDZ_FILE_EXT)
       || strendswith(full_path, LHA_FILE_EXT)
       || strendswith(full_path, M3U_FILE_EXT)
       || path_is_directory(full_path))
      {
	     // Open tmp config file
	     FILE * configfile;
	     if (configfile = fopen(RPATH, "w"))
	     {
	        char kickstart[RETRO_PATH_MAX];

            // If a machine was specified in the name of the game
            if (strstr(full_path, "(A4030)") != NULL || strstr(full_path, "(030)") != NULL)
            {
               // Use A4000/030
               fprintf(stdout, "[libretro-uae]: Found '(A4030)' or '(030)' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting A4000/030 with Kickstart 3.1 r40.068\n");
               fprintf(configfile, A4030_CONFIG);
               path_join((char*)&kickstart, retro_system_directory, A4000_ROM);
            }
            else if (strstr(full_path, "(A4040)") != NULL || strstr(full_path, "(040)") != NULL)
            {
               // Use A4000/040
               fprintf(stdout, "[libretro-uae]: Found '(A4040)' or '(040)' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting A4000/040 with Kickstart 3.1 r40.068\n");
               fprintf(configfile, A4040_CONFIG);
               path_join((char*)&kickstart, retro_system_directory, A4000_ROM);
            }
            else if (strstr(full_path, "(A1200OG)") != NULL || strstr(full_path, "(A1200NF)") != NULL)
            {
               // Use A1200 barebone
               fprintf(stdout, "[libretro-uae]: Found '(A1200OG)' or '(A1200NF)' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting A1200 NoFast with Kickstart 3.1 r40.068\n");
               fprintf(configfile, A1200OG_CONFIG);
               path_join((char*)&kickstart, retro_system_directory, A1200_ROM);
            }
            else if (strstr(full_path, "(A1200)") != NULL || strstr(full_path, "AGA") != NULL || strstr(full_path, "CD32") != NULL || strstr(full_path, "AmigaCD") != NULL)
            {
               // Use A1200
               fprintf(stdout, "[libretro-uae]: Found '(A1200)', 'AGA', 'CD32', or 'AmigaCD' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting A1200 with Kickstart 3.1 r40.068\n");
               fprintf(configfile, A1200_CONFIG);
               path_join((char*)&kickstart, retro_system_directory, A1200_ROM);
            }
            else if (strstr(full_path, "(A600)") != NULL || strstr(full_path, "ECS") != NULL)
            {
               // Use A600
               fprintf(stdout, "[libretro-uae]: Found '(A600)' or 'ECS' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting A600 with Kickstart 3.1 r40.063\n");
               fprintf(configfile, A600_CONFIG);
               path_join((char*)&kickstart, retro_system_directory, A600_ROM);
            }
            else if (strstr(full_path, "(A500+)") != NULL || strstr(full_path, "(A500PLUS)") != NULL)
            {
               // Use A500+
               fprintf(stdout, "[libretro-uae]: Found '(A500+)' or '(A500PLUS)' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting A500+ with Kickstart 2.04 r37.175\n");
               fprintf(configfile, A500PLUS_CONFIG);
               path_join((char*)&kickstart, retro_system_directory, A500KS2_ROM);
            }
            else if (strstr(full_path, "(A500OG)") != NULL || strstr(full_path, "(512K)") != NULL)
            {
               // Use A500 barebone
               fprintf(stdout, "[libretro-uae]: Found '(A500OG)' or '(512K)' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting A500 512K with Kickstart 1.3 r34.005\n");
               fprintf(configfile, A500OG_CONFIG);
               path_join((char*)&kickstart, retro_system_directory, A500_ROM);
            }
            else if (strstr(full_path, "(A500)") != NULL || strstr(full_path, "OCS") != NULL)
            {
               // Use A500
               fprintf(stdout, "[libretro-uae]: Found '(A500)' or 'OCS' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting A500 with Kickstart 1.3 r34.005\n");
               fprintf(configfile, A500_CONFIG);
               path_join((char*)&kickstart, retro_system_directory, A500_ROM);
            }
            else
            {
               if (strcmp(opt_model, "auto") == 0)
               {
                  if (opt_use_boot_hd)
                  {
                     // A600 required for a hard disk
                     uae_machine[0] = '\0';
                     strcat(uae_machine, A4040_CONFIG);
                     strcpy(uae_kickstart, A4000_ROM);
                  }
                  else
                  {
                     // Hard disk defaults to A600
                     if (  strendswith(full_path, HDF_FILE_EXT)
                        || strendswith(full_path, HDZ_FILE_EXT)
                        || strendswith(full_path, LHA_FILE_EXT)
                        || path_is_directory(full_path))
                     {
                        uae_machine[0] = '\0';
                        strcat(uae_machine, A4040_CONFIG);
                        strcpy(uae_kickstart, A4000_ROM);
                     }
                     // Floppy disk defaults to A500
                     else
                     {
                        uae_machine[0] = '\0';
                        strcat(uae_machine, A4040_CONFIG);
                        strcpy(uae_kickstart, A4000_ROM);
                     }
                  }
               }

               // No machine specified
               fprintf(stdout, "[libretro-uae]: No machine specified in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting default configuration\n");
               fprintf(configfile, uae_machine);
               path_join((char*)&kickstart, retro_system_directory, uae_kickstart);
            }

            // Write common config
            fprintf(configfile, uae_config);

            // If region was specified in the name of the game
            if (strstr(full_path, "NTSC") != NULL)
            {
               fprintf(stdout, "[libretro-uae]: Found 'NTSC' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Forcing NTSC mode\n");
               fprintf(configfile, "ntsc=true\n");
               real_ntsc=true;
               forced_video=true;
            }
            else if (strstr(full_path, "PAL") != NULL)
            {
               fprintf(stdout, "[libretro-uae]: Found 'PAL' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Forcing PAL mode\n");
               fprintf(configfile, "ntsc=false\n");
               forced_video=true;
            }

            // Verify Kickstart
            /*
            if (!file_exists(kickstart))
            {
               // Kickstart ROM not found
               fprintf(stderr, "Kickstart ROM '%s' not found!\n", (const char*)&kickstart);
               fclose(configfile);
               return false;
            }
            */
            fprintf(configfile, "kickstart_rom_file=%s\n", (const char*)&kickstart);

            // Bootable HD exception
            if (opt_use_boot_hd)
            {
               char *tmp_str = NULL;

               // HDF mode
               if (opt_use_boot_hd > 1)
               {
                  // Init Boot HD
                  char boothd_hdf[RETRO_PATH_MAX];
                  path_join((char*)&boothd_hdf, retro_save_directory, LIBRETRO_PUAE_PREFIX ".hdf");
                  if (!file_exists(boothd_hdf))
                  {
                     fprintf(stdout, "[libretro-uae]: Boot HD image file '%s' not found, attempting to create one\n", (const char*)&boothd_hdf);

                     if (make_hdf(boothd_hdf, boothd_size, "BOOT"))
                        fprintf(stderr, "Error creating Boot HD image '%s'!\n", (const char*)&boothd_hdf);
                  }
                  if (file_exists(boothd_hdf))
                  {
                     tmp_str = string_replace_substring(boothd_hdf, "\\", "\\\\");
                     fprintf(configfile, "hardfile2=rw,BOOT:\"%s\",32,1,2,512,0,,uae0\n", (const char*)tmp_str);
                     free(tmp_str);
                     tmp_str = NULL;
                  }
                  else
                     fprintf(stderr, "Boot HD image file '%s' not found!\n", (const char*)&boothd_hdf);
               }
               // Directory mode
               else if (opt_use_boot_hd == 1)
               {
                  char boothd_path[RETRO_PATH_MAX];
                  path_join((char*)&boothd_path, retro_save_directory, "BootHD");

                  if (!path_is_directory(boothd_path))
                  {
                     fprintf(stdout, "[libretro-uae]: Boot HD image directory '%s' not found, attempting to create one\n", (const char*)&boothd_path);
                     path_mkdir(boothd_path);
                  }
                  if (path_is_directory(boothd_path))
                  {
                     tmp_str = string_replace_substring(boothd_path, "\\", "\\\\");
                     fprintf(configfile, "filesystem2=rw,BOOT:Boot:\"%s\",-1\n", (const char*)tmp_str);
                     free(tmp_str);
                     tmp_str = NULL;
                  }
                  else
                     fprintf(stderr, "Error creating Boot HD directory in '%s'!\n", (const char*)&boothd_path);
               }
            }

            // If argument is a hard drive image file
            if (strendswith(full_path, HDF_FILE_EXT)
             || strendswith(full_path, HDZ_FILE_EXT)
             || strendswith(full_path, LHA_FILE_EXT)
             || path_is_directory(full_path))
            {
               char *tmp_str = NULL;

               // WHDLoad support
               if (opt_use_whdload)
               {
                  // WHDLoad HDF mode
                  if (opt_use_whdload == 2)
                  {
                     // Init WHDLoad
                     char whdload_hdf[RETRO_PATH_MAX];
                     path_join((char*)&whdload_hdf, retro_system_directory, "WHDLoad.hdf");

                     // Verify WHDLoad
                     // Windows needs double backslashes when paths are in quotes, hence the string_replace_substring()
                     if (!file_exists(whdload_hdf))
                        path_join((char*)&whdload_hdf, retro_save_directory, "WHDLoad.hdf");
                     if (!file_exists(whdload_hdf))
                     {
                        fprintf(stdout, "[libretro-uae]: WHDLoad image file '%s' not found, attempting to create one\n", (const char*)&whdload_hdf);

                        char whdload_hdf_gz[RETRO_PATH_MAX];
                        path_join((char*)&whdload_hdf_gz, retro_save_directory, "WHDLoad.hdf.gz");

                        FILE *whdload_hdf_gz_fp;
                        if (whdload_hdf_gz_fp = fopen(whdload_hdf_gz, "wb"))
                        {
                           // Write GZ
                           fwrite(___whdload_WHDLoad_hdf_gz, ___whdload_WHDLoad_hdf_gz_len, 1, whdload_hdf_gz_fp);
                           fclose(whdload_hdf_gz_fp);

                           // Extract GZ
                           struct gzFile_s *whdload_hdf_gz_fp;
                           if (whdload_hdf_gz_fp = gzopen(whdload_hdf_gz, "r"))
                           {
                              FILE *whdload_hdf_fp;
                              if (whdload_hdf_fp = fopen(whdload_hdf, "wb"))
                              {
                                 gz_uncompress(whdload_hdf_gz_fp, whdload_hdf_fp);
                                 fclose(whdload_hdf_fp);
                              }
                              gzclose(whdload_hdf_gz_fp);
                           }
                           remove(whdload_hdf_gz);
                        }
                        else
                           fprintf(stderr, "Error creating WHDLoad.hdf '%s'!\n", (const char*)&whdload_hdf);
                     }
                     if (file_exists(whdload_hdf))
                     {
                        tmp_str = string_replace_substring(whdload_hdf, "\\", "\\\\");
                        fprintf(configfile, "hardfile2=rw,WHDLoad:\"%s\",32,1,2,512,0,,uae0\n", (const char*)tmp_str);
                        free(tmp_str);
                        tmp_str = NULL;
                     }
                     else
                        fprintf(stderr, "WHDLoad image file '%s' not found!\n", (const char*)&whdload_hdf);
                  }
                  // WHDLoad File mode
                  else
                  {
                     char whdload_path[RETRO_PATH_MAX];
                     path_join((char*)&whdload_path, retro_save_directory, "WHDLoad");

                     char whdload_c_path[RETRO_PATH_MAX];
                     path_join((char*)&whdload_c_path, retro_save_directory, "WHDLoad/C");

                     if (!path_is_directory(whdload_path) || (path_is_directory(whdload_path) && !path_is_directory(whdload_c_path)))
                     {
                        fprintf(stdout, "[libretro-uae]: WHDLoad image directory '%s' not found, attempting to create one\n", (const char*)&whdload_path);
                        path_mkdir(whdload_path);

                        char whdload_files_zip[RETRO_PATH_MAX];
                        path_join((char*)&whdload_files_zip, retro_save_directory, "WHDLoad_files.zip");

                        fprintf(stdout, "[libretro.c]: Exploding WHDLoad_files.zip\n");
                        fflush(stdout);
                        fsync(fileno(stdout));
                        FILE *whdload_files_zip_fp;
                        if (whdload_files_zip_fp = fopen(whdload_files_zip, "wb"))
                        {
                           // Write ZIP
                           fwrite(___whdload_WHDLoad_files_zip, ___whdload_WHDLoad_files_zip_len, 1, whdload_files_zip_fp);
                           fclose(whdload_files_zip_fp);

                           // Extract ZIP
                           zip_uncompress(whdload_files_zip, whdload_path);

                           cp("/tmp/kick40068.A4000", "/tmp/amiga/WHDLoad/Devs/Kickstarts/kick40068.A4000");
                           cp("/tmp/kick40068.A1200", "/tmp/amiga/WHDLoad/Devs/Kickstarts/kick40068.A1200");
                           cp("/tmp/kick34005.A500", "/tmp/amiga/WHDLoad/Devs/Kickstarts/kick34005.A500");
                           remove(whdload_files_zip);
                           
                           fprintf(stdout, "[libretro.c]: Done exploding WHDLoad_files.zip\n");
                           fflush(stdout);
                           fsync(fileno(stdout));
                        }
                        else
                           fprintf(stderr, "Error extracting WHDLoad directory '%s'!\n", (const char*)&whdload_path);
                     }
                     if (path_is_directory(whdload_path) && path_is_directory(whdload_c_path))
                     {
                        tmp_str = string_replace_substring(whdload_path, "\\", "\\\\");
                        fprintf(configfile, "filesystem2=rw,WHDLoad:WHDLoad:\"%s\",0\n", (const char*)tmp_str);
                        free(tmp_str);
                        tmp_str = NULL;
                     }
                     else
                        fprintf(stderr, "Error creating WHDLoad directory in '%s'!\n", (const char*)&whdload_path);
                  }

                  // Attach game image
                  tmp_str = string_replace_substring(full_path, "\\", "\\\\");

                  if (strendswith(full_path, LHA_FILE_EXT))
                     fprintf(configfile, "filesystem2=ro,DH0:LHA:\"%s\",0\n", (const char*)tmp_str);
                  else if (path_is_directory(full_path))
                     fprintf(configfile, "filesystem2=rw,DH0:%s:\"%s\",0\n", path_basename(tmp_str), (const char*)tmp_str);
                  else
                     fprintf(configfile, "hardfile2=rw,DH0:\"%s\",32,1,2,512,0,,uae1\n", (const char*)tmp_str);
                  free(tmp_str);
                  tmp_str = NULL;

                  // Attach retro_system_directory as a read only hard drive for WHDLoad kickstarts/prefs/key
#ifdef WIN32
                  tmp_str = string_replace_substring(retro_system_directory, "\\", "\\\\");
                  fprintf(configfile, "filesystem2=ro,RASystem:RASystem:\"%s\",-128\n", (const char*)tmp_str);
                  free(tmp_str);
                  tmp_str = NULL;
#else
                  // Force the ending slash to make sure the path is not treated as a file
                  fprintf(configfile, "filesystem2=ro,RASystem:RASystem:\"%s%s\",-128\n", retro_system_directory, "/");
#endif
                  // WHDSaves HDF mode
                  if (opt_use_whdload == 2)
                  {
                     // Attach WHDSaves.hdf if available
                     char whdsaves_hdf[RETRO_PATH_MAX];
                     path_join((char*)&whdsaves_hdf, retro_system_directory, "WHDSaves.hdf");
                     if (!file_exists(whdsaves_hdf))
                        path_join((char*)&whdsaves_hdf, retro_save_directory, "WHDSaves.hdf");
                     if (!file_exists(whdsaves_hdf))
                     {
                        fprintf(stdout, "[libretro-uae]: WHDSaves image file '%s' not found, attempting to create one\n", (const char*)&whdsaves_hdf);

                        char whdsaves_hdf_gz[RETRO_PATH_MAX];
                        path_join((char*)&whdsaves_hdf_gz, retro_save_directory, "WHDSaves.hdf.gz");

                        FILE *whdsaves_hdf_gz_fp;
                        if (whdsaves_hdf_gz_fp = fopen(whdsaves_hdf_gz, "wb"))
                        {
                           // Write GZ
                           fwrite(___whdload_WHDSaves_hdf_gz, ___whdload_WHDSaves_hdf_gz_len, 1, whdsaves_hdf_gz_fp);
                           fclose(whdsaves_hdf_gz_fp);

                           // Extract GZ
                           struct gzFile_s *whdsaves_hdf_gz_fp;
                           if (whdsaves_hdf_gz_fp = gzopen(whdsaves_hdf_gz, "r"))
                           {
                              FILE *whdsaves_hdf_fp;
                              if (whdsaves_hdf_fp = fopen(whdsaves_hdf, "wb"))
                              {
                                 gz_uncompress(whdsaves_hdf_gz_fp, whdsaves_hdf_fp);
                                 fclose(whdsaves_hdf_fp);
                              }
                              gzclose(whdsaves_hdf_gz_fp);
                           }
                           remove(whdsaves_hdf_gz);
                        }
                        else
                           fprintf(stderr, "Error creating WHDSaves.hdf '%s'!\n", (const char*)&whdsaves_hdf);
                     }
                     if (file_exists(whdsaves_hdf))
                     {
                        tmp_str = string_replace_substring(whdsaves_hdf, "\\", "\\\\");
                        fprintf(configfile, "hardfile2=rw,WHDSaves:\"%s\",32,1,2,512,0,,uae2\n", (const char*)tmp_str);
                        free(tmp_str);
                        tmp_str = NULL;
                     }
                  }
                  // WHDSaves file mode
                  else
                  {
                     char whdsaves_path[RETRO_PATH_MAX];
                     path_join((char*)&whdsaves_path, retro_save_directory, "WHDSaves");
                     if (!path_is_directory(whdsaves_path))
                        path_mkdir(whdsaves_path);
                     if (path_is_directory(whdsaves_path))
                     {
                        tmp_str = string_replace_substring(whdsaves_path, "\\", "\\\\");
                        fprintf(configfile, "filesystem2=rw,WHDSaves:WHDSaves:\"%s\",-128\n", (const char*)tmp_str);
                        free(tmp_str);
                        tmp_str = NULL;
                     }
                     else
                        fprintf(stderr, "Error creating WHDSaves directory in '%s'!\n", (const char*)&whdsaves_path);
                  }

                  // Manipulate WHDLoad.prefs
                  int WHDLoad_ConfigDelay = 0;
                  int WHDLoad_SplashDelay = 0;

                  switch (opt_use_whdload_prefs)
                  {
                     case 1:
                        WHDLoad_ConfigDelay = -1;
                        break;
                     case 2:
                        WHDLoad_SplashDelay = 150;
                        break;
                     case 3:
                        WHDLoad_ConfigDelay = -1;
                        WHDLoad_SplashDelay = -1;
                        break;
                  }

                  FILE *whdload_prefs;
                  char whdload_prefs_path[RETRO_PATH_MAX];
                  path_join((char*)&whdload_prefs_path, retro_system_directory, "WHDLoad.prefs");

                  if (!file_exists(whdload_prefs_path))
                  {
                     fprintf(stdout, "[libretro-uae]: WHDLoad prefs '%s' not found, attempting to create one\n", (const char*)&whdload_prefs_path);

                     char whdload_prefs_gz[RETRO_PATH_MAX];
                     path_join((char*)&whdload_prefs_gz, retro_system_directory, "WHDLoad.prefs.gz");

                     FILE *whdload_prefs_gz_fp;
                     if (whdload_prefs_gz_fp = fopen(whdload_prefs_gz, "wb"))
                     {
                        // Write GZ
                        fwrite(___whdload_WHDLoad_prefs_gz, ___whdload_WHDLoad_prefs_gz_len, 1, whdload_prefs_gz_fp);
                        fclose(whdload_prefs_gz_fp);

                        // Extract GZ
                        struct gzFile_s *whdload_prefs_gz_fp;
                        if (whdload_prefs_gz_fp = gzopen(whdload_prefs_gz, "r"))
                        {
                           FILE *whdload_prefs_fp;
                           if (whdload_prefs_fp = fopen(whdload_prefs_path, "wb"))
                           {
                              gz_uncompress(whdload_prefs_gz_fp, whdload_prefs_fp);
                              fclose(whdload_prefs_fp);
                           }
                           gzclose(whdload_prefs_gz_fp);
                        }
                        remove(whdload_prefs_gz);
                     }
                     else
                        fprintf(stderr, "Error creating WHDLoad prefs '%s'!\n", (const char*)&whdload_prefs_path);
                  }

                  FILE *whdload_prefs_new;
                  char whdload_prefs_new_path[RETRO_PATH_MAX];
                  path_join((char*)&whdload_prefs_new_path, retro_system_directory, "WHDLoad.prefs_new");

                  char whdload_prefs_backup_path[RETRO_PATH_MAX];
                  path_join((char*)&whdload_prefs_backup_path, retro_system_directory, "WHDLoad.prefs_backup");

                  char whdload_filebuf[4096];
                  if (whdload_prefs = fopen(whdload_prefs_path, "r"))
                  {
                     if (whdload_prefs_new = fopen(whdload_prefs_new_path, "w"))
                     {
                        while (fgets(whdload_filebuf, sizeof(whdload_filebuf), whdload_prefs))
                        {
                           if (strstr(whdload_filebuf, ";ConfigDelay=") || strstr(whdload_filebuf, ";SplashDelay="))
                              fprintf(whdload_prefs_new, whdload_filebuf);
                           else if (strstr(whdload_filebuf, "ConfigDelay="))
                              fprintf(whdload_prefs_new, "%s%d\n", "ConfigDelay=", WHDLoad_ConfigDelay);
                           else if (strstr(whdload_filebuf, "SplashDelay="))
                              fprintf(whdload_prefs_new, "%s%d\n", "SplashDelay=", WHDLoad_SplashDelay);
                           else
                              fprintf(whdload_prefs_new, whdload_filebuf);
                        }
                        fclose(whdload_prefs_new);
                        fclose(whdload_prefs);

                        // Remove backup config
                        remove(whdload_prefs_backup_path);

                        // Replace old and new config
                        rename(whdload_prefs_path, whdload_prefs_backup_path);
                        rename(whdload_prefs_new_path, whdload_prefs_path);
                     }
                     else
                     {
                        fprintf(stderr, "Error creating new WHDLoad.prefs '%s'!\n", (const char*)&whdload_prefs_new_path);
                        fclose(whdload_prefs);
                     }
                  }
                  else
                     fprintf(stderr, "WHDLoad.prefs '%s' not found!\n", (const char*)&whdload_prefs_path);
               }
               else
               {
                  tmp_str = string_replace_substring(full_path, "\\", "\\\\");
                  if (path_is_directory(full_path))
                     fprintf(configfile, "filesystem2=rw,DH0:%s:\"%s\",0\n", path_basename(tmp_str), (const char*)tmp_str);
                  else
                     fprintf(configfile, "hardfile2=rw,DH0:\"%s\",32,1,2,512,0,,uae0\n", (const char*)tmp_str);
                  free(tmp_str);
                  tmp_str = NULL;
               }
            }
            else
            {
               // If argument is a M3U playlist
               if (strendswith(full_path, M3U_FILE_EXT))
               {
                  // Parse the M3U file
                  dc_parse_m3u(retro_dc, full_path, retro_save_directory);

                  // Some debugging
                  fprintf(stdout, "[libretro-uae]: M3U file parsed, %d file(s) found\n", retro_dc->count);
                  //for (unsigned i = 0; i < retro_dc->count; i++)
                     //printf("File %d: %s\n", i+1, retro_dc->files[i]);
               }
               else
               {
                  // Add the file to disk control context
                  char disk_image_label[RETRO_PATH_MAX];
                  disk_image_label[0] = '\0';

                  if (!string_is_empty(full_path))
                     fill_short_pathname_representation(
                           disk_image_label, full_path, sizeof(disk_image_label));

                  // Must reset disk control struct here,
                  // otherwise duplicate entries will be
                  // added when calling retro_reset()
                  dc_reset(retro_dc);
                  dc_add_file(retro_dc, full_path, disk_image_label);
               }

               // Init only existing disks
               if (retro_dc->count)
               {
                  // Init first disk
                  retro_dc->index = 0;
                  retro_dc->eject_state = false;
                  display_current_image(retro_dc->labels[retro_dc->index], true);
                  fprintf(stdout, "[libretro-uae]: Disk (%d) inserted into drive DF0: '%s'\n", retro_dc->index+1, retro_dc->files[retro_dc->index]);
                  fprintf(configfile, "floppy0=%s\n", retro_dc->files[0]);

                  // Append rest of the disks to the config if M3U is a MultiDrive-M3U
                  if (strstr(full_path, "(MD)") != NULL)
                  {
                     for (unsigned i = 1; i < retro_dc->count; i++)
                     {
                        retro_dc->index = i;
                        if (i <= 3)
                        {
                           fprintf(stdout, "[libretro-uae]: Disk (%d) inserted into drive DF%d: '%s'\n", retro_dc->index+1, i, retro_dc->files[retro_dc->index]);
                           fprintf(configfile, "floppy%d=%s\n", i, retro_dc->files[i]);

                           // By default only DF0: is enabled, so floppyXtype needs to be set on the extra drives
                           if (i > 0)
                              fprintf(configfile, "floppy%dtype=%d\n", i, 0); // 0 = 3.5" DD
                        }
                        else
                        {
                           fprintf(stderr, "Too many disks for MultiDrive!\n");
                           fclose(configfile);
                           return false;
                        }
                     }
                     // Reset index to first disk
                     retro_dc->index = 0;
                  }
               }
            }
            fclose(configfile);
         }
         else
         {
            // Error
            fprintf(stderr, "Error while writing file '%s'!\n", (const char*)&RPATH);
            return false;
         }
      }
      // If argument is a CD image
      else if (strendswith(full_path, CUE_FILE_EXT)
            || strendswith(full_path, CCD_FILE_EXT)
            || strendswith(full_path, NRG_FILE_EXT)
            || strendswith(full_path, MDS_FILE_EXT)
            || strendswith(full_path, ISO_FILE_EXT))
      {
         // Open tmp config file
         FILE * configfile;
         if (configfile = fopen(RPATH, "w"))
         {
            char kickstart[RETRO_PATH_MAX];
            char kickstart_ext[RETRO_PATH_MAX];

            // If a machine was specified in the name of the game
            if (strstr(full_path, "(CD32FR)") != NULL || strstr(full_path, "FastRAM") != NULL)
            {
               // Use CD32 with Fast RAM
               fprintf(stdout, "[libretro-uae]: Found '(CD32FR)' or 'FastRAM' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting CD32 FastRAM with Kickstart 3.1 r40.060\n");
               fprintf(configfile, CD32FR_CONFIG);
               path_join((char*)&kickstart, retro_system_directory, CD32_ROM);
               path_join((char*)&kickstart_ext, retro_system_directory, CD32_ROM_EXT);
            }
            else if (strstr(full_path, "(CD32)") != NULL || strstr(full_path, "(CD32NF)") != NULL)
            {
               // Use CD32 barebone
               fprintf(stdout, "[libretro-uae]: Found '(CD32)' or '(CD32NF)' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting CD32 with Kickstart 3.1 r40.060\n");
               fprintf(configfile, CD32_CONFIG);
               path_join((char*)&kickstart, retro_system_directory, CD32_ROM);
               path_join((char*)&kickstart_ext, retro_system_directory, CD32_ROM_EXT);
            }
            else
            {
               if (strcmp(opt_model, "auto") == 0)
               {
                  uae_machine[0] = '\0';
                  strcat(uae_machine, CD32_CONFIG);
                  strcpy(uae_kickstart, CD32_ROM);
                  strcpy(uae_kickstart_ext, CD32_ROM_EXT);
               }

               // No machine specified
               fprintf(stdout, "[libretro-uae]: No machine specified in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Booting default configuration\n");
               fprintf(configfile, uae_machine);
               path_join((char*)&kickstart, retro_system_directory, uae_kickstart);
               path_join((char*)&kickstart_ext, retro_system_directory, uae_kickstart_ext);
            }

            // Write common config
            fprintf(configfile, uae_config);

            // If region was specified in the name of the game
            if (strstr(full_path, "NTSC") != NULL)
            {
               fprintf(stdout, "[libretro-uae]: Found 'NTSC' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Forcing NTSC mode\n");
               fprintf(configfile, "ntsc=true\n");
               real_ntsc=true;
               forced_video=true;
            }
            else if (strstr(full_path, "PAL") != NULL)
            {
               fprintf(stdout, "[libretro-uae]: Found 'PAL' in filename '%s'\n", full_path);
               fprintf(stdout, "[libretro-uae]: Forcing PAL mode\n");
               fprintf(configfile, "ntsc=false\n");
               forced_video=true;
            }

            // Verify Kickstart
            /*
            if (!file_exists(kickstart))
            {
               // Kickstart ROM not found
               fprintf(stderr, "Kickstart ROM '%s' not found!\n", (const char*)&kickstart);
               fclose(configfile);
               return false;
            }
            else
            */
               fprintf(configfile, "kickstart_rom_file=%s\n", (const char*)&kickstart);

            // Decide if CD32 ROM is combined based on filesize
            struct stat kickstart_st;
            stat(kickstart, &kickstart_st);

            // Verify extended ROM if external
            if (kickstart_st.st_size == 524288)
            {
               if (!file_exists(kickstart_ext))
               {
                  // Kickstart extended ROM not found
                  fprintf(stderr, "Kickstart extended ROM '%s' not found!\n", (const char*)&kickstart_ext);
                  fclose(configfile);
                  return false;
               }
               else
                  fprintf(configfile, "kickstart_ext_rom_file=%s\n", (const char*)&kickstart_ext);
            }

            // NVRAM
            char flash_file[RETRO_PATH_MAX];
            char flash_filepath[RETRO_PATH_MAX];
            if (opt_shared_nvram)
            {
               // Shared
               path_join((char*)&flash_file, retro_save_directory, LIBRETRO_PUAE_PREFIX);
            }
            else
            {
               // Per game
               snprintf(flash_filepath, RETRO_PATH_MAX, "%s", full_path);
               path_remove_extension((char*)flash_filepath);
               path_join((char*)&flash_file, retro_save_directory, path_basename(flash_filepath));
            }
            fprintf(stdout, "[libretro-uae]: Using Flash RAM: '%s.nvr'\n", flash_file);
            fprintf(configfile, "flash_file=%s.nvr\n", (const char*)&flash_file);

            // Add the file to disk control context
            char cd_image_label[RETRO_PATH_MAX];
            cd_image_label[0] = '\0';

            if (!string_is_empty(full_path))
               fill_short_pathname_representation(
                     cd_image_label, full_path, sizeof(cd_image_label));

            // Must reset disk control struct here,
            // otherwise duplicate entries will be
            // added when calling retro_reset()
            dc_reset(retro_dc);
            dc_add_file(retro_dc, full_path, cd_image_label);

            // Init first disk
            retro_dc->index = 0;
            retro_dc->eject_state = false;
            display_current_image(retro_dc->labels[retro_dc->index], true);
            fprintf(stdout, "[libretro-uae]: CD (%d) inserted into drive CD0: '%s'\n", retro_dc->index+1, retro_dc->files[retro_dc->index]);
            fprintf(configfile, "cdimage0=%s,\n", retro_dc->files[0]); // ","-suffix needed if filename contains ","

            fclose(configfile);
         }
         else
         {
            // Error
            fprintf(stderr, "Error while writing file '%s'!\n", (const char*)&RPATH);
            return false;
         }
      }
      // If argument is a config file
	  else if (strendswith(full_path, UAE_FILE_EXT))
	  {
	     // Open tmp config file
	     FILE * configfile;
	     if (configfile = fopen(RPATH, "w"))
	     {
	        char kickstart[RETRO_PATH_MAX];

	        fprintf(configfile, uae_machine);
	        path_join((char*)&kickstart, retro_system_directory, uae_kickstart);

	        // Write common config
	        fprintf(configfile, uae_config);
	        fprintf(configfile, "kickstart_rom_file=%s\n", (const char*)&kickstart);

	        // Separator row for clarity
	        fprintf(configfile, "\n");

	        // Iterate parsed file and append all rows to the temporary config
	        FILE * configfile_custom;

	        char filebuf[4096];
	        if (configfile_custom = fopen (full_path, "r"))
	        {
	           while (fgets(filebuf, sizeof(filebuf), configfile_custom))
	           {
	              fprintf(configfile, filebuf);
               }
               fclose(configfile_custom);
            }
            fclose(configfile);
         }
         else
         {
            // Error
            fprintf(stderr, "Error while writing file '%s'!\n", (const char*)&RPATH);
            return false;
         }
      }
	  // Other extensions
	  else
	  {
	     // Unsupported file format
	     fprintf(stderr, "Unsupported file format '%s'!\n", full_path);
	     return false;
	  }
   }
   // Empty content
   else
   {
      // Open tmp config file
      FILE * configfile;
      if (configfile = fopen(RPATH, "w"))
      {
         char kickstart[RETRO_PATH_MAX];

         // No machine specified
         fprintf(stdout, "[libretro-uae]: Booting default configuration\n");
         fprintf(configfile, uae_machine);
         path_join((char*)&kickstart, retro_system_directory, uae_kickstart);

         // Write common config
         fprintf(configfile, uae_config);

         // Bootable HD exception
         if (opt_use_boot_hd)
         {
            char *tmp_str = NULL;

            // HDF mode
            if (opt_use_boot_hd > 1)
            {
               // Init Boot HD
               char boothd_hdf[RETRO_PATH_MAX];
               path_join((char*)&boothd_hdf, retro_save_directory, LIBRETRO_PUAE_PREFIX ".hdf");
               if (!file_exists(boothd_hdf))
               {
                  fprintf(stdout, "[libretro-uae]: Boot HD image file '%s' not found, attempting to create one\n", (const char*)&boothd_hdf);

                  if (make_hdf(boothd_hdf, boothd_size, "BOOT"))
                     fprintf(stderr, "Error creating Boot HD image '%s'!\n", (const char*)&boothd_hdf);
               }
               if (file_exists(boothd_hdf))
               {
                  tmp_str = string_replace_substring(boothd_hdf, "\\", "\\\\");
                  fprintf(configfile, "hardfile2=rw,BOOT:\"%s\",32,1,2,512,0,,uae0\n", (const char*)tmp_str);
                  free(tmp_str);
                  tmp_str = NULL;
               }
               else
                  fprintf(stderr, "Boot HD image file '%s' not found!\n", (const char*)&boothd_hdf);
            }
            // Directory mode
            else if (opt_use_boot_hd == 1)
            {
               char boothd_path[RETRO_PATH_MAX];
               path_join((char*)&boothd_path, retro_save_directory, "BootHD");

               if (!path_is_directory(boothd_path))
               {
                  fprintf(stdout, "[libretro-uae]: Boot HD image directory '%s' not found, attempting to create one\n", (const char*)&boothd_path);
                  path_mkdir(boothd_path);
               }
               if (path_is_directory(boothd_path))
               {
                  tmp_str = string_replace_substring(boothd_path, "\\", "\\\\");
                  fprintf(configfile, "filesystem2=rw,BOOT:Boot:\"%s\",-1\n", (const char*)tmp_str);
                  free(tmp_str);
                  tmp_str = NULL;
               }
               else
                  fprintf(stderr, "Error creating Boot HD directory in '%s'!\n", (const char*)&boothd_path);
            }
         }

         // CD32 exception
         if (strcmp(opt_model, "CD32") == 0 || strcmp(opt_model, "CD32FR") == 0)
         {
            char kickstart_ext[RETRO_PATH_MAX];
            path_join((char*)&kickstart_ext, retro_system_directory, uae_kickstart_ext);

            // Verify kickstart
            /*
            if (!file_exists(kickstart))
            {
               // Kickstart ROM not found
               fprintf(stderr, "Kickstart ROM '%s' not found!\n", (const char*)&kickstart);
               fclose(configfile);
               return false;
            }
            else
            */
               fprintf(configfile, "kickstart_rom_file=%s\n", (const char*)&kickstart);

            // Decide if CD32 ROM is combined based on filesize
            struct stat kickstart_st;
            stat(kickstart, &kickstart_st);

            // Verify extended ROM if external
            if (kickstart_st.st_size == 524288)
            {
               if (!file_exists(kickstart_ext))
               {
                  // Kickstart extended ROM not found
                  fprintf(stderr, "Kickstart extended ROM '%s' not found!\n", (const char*)&kickstart_ext);
                  fclose(configfile);
                  return false;
               }
               else
                  fprintf(configfile, "kickstart_ext_rom_file=%s\n", (const char*)&kickstart_ext);
            }

            // NVRAM always shared without content
            char flash_file[RETRO_PATH_MAX];
            char flash_filepath[RETRO_PATH_MAX];
            path_join((char*)&flash_file, retro_save_directory, LIBRETRO_PUAE_PREFIX);
            fprintf(stdout, "[libretro-uae]: Using Flash RAM: '%s.nvr'\n", flash_file);
            fprintf(configfile, "flash_file=%s.nvr\n", (const char*)&flash_file);
         }
         else
         {
            // Verify Kickstart
            /*
            if (!file_exists(kickstart))
            {
               // Kickstart ROM not found
               fprintf(stderr, "Kickstart ROM '%s' not found!\n", (const char*)&kickstart);
               fclose(configfile);
               return false;
            }
            else
            */
               fprintf(configfile, "kickstart_rom_file=%s\n", (const char*)&kickstart);
         }

         fclose(configfile);
      }
   }
   return true;
}

void retro_reset(void)
{
   fake_ntsc = false;
   video_config_old = 0;
   update_variables();
   retro_create_config();
   uae_restart(1, (const char*)&RPATH); /* 1=nogui */
}

void update_audiovideo(void)
{
   // Statusbar disk display timer
   if (imagename_timer > 0)
      imagename_timer--;

   // Update audio settings
   if (filter_type_update)
   {
      filter_type_update = false;
      if (currprefs.cpu_model == 68020)
         changed_prefs.sound_filter_type=FILTER_SOUND_TYPE_A1200;
      else
         changed_prefs.sound_filter_type=FILTER_SOUND_TYPE_A500;
      config_changed = 0;
   }

   // Automatic video resolution
   if (opt_video_resolution_auto && request_init_custom_timer == 0)
   {
      int current_resolution = GET_RES_DENISE (bplcon0);
      //printf("BPLCON0: %d, %d, %d %d\n", bplcon0, current_resolution, diwfirstword_total, diwlastword_total);

      // Super Skidmarks force to SuperHires
      if (current_resolution == 1 && bplcon0 == 0xC201 && ((diwfirstword_total == 210 && diwlastword_total && 786) || (diwfirstword_total == 420 && diwlastword_total && 1572)))
         current_resolution = 2;
      // Super Stardust force to SuperHires, rather pointless and causes a false positive on The Settlers
      //else if (current_resolution == 0 && (bplcon0 == 0 /*CD32*/|| bplcon0 == 512 /*AGA*/) && ((diwfirstword_total == 114 && diwlastword_total && 818) || (diwfirstword_total == 228 && diwlastword_total && 1636)))
         //current_resolution = 2;
      // Lores force to Hires
      else if (current_resolution == 0)
         current_resolution = 1;

      switch (current_resolution)
      {
         case 1:
            if (!(video_config & PUAE_VIDEO_HIRES))
            {
               changed_prefs.gfx_resolution = RES_HIRES;
               video_config |= PUAE_VIDEO_HIRES;
               video_config &= ~PUAE_VIDEO_SUPERHIRES;
               defaultw = retrow = PUAE_VIDEO_WIDTH;
               retro_max_diwlastword = retro_max_diwlastword_hires;
               request_init_custom_timer = 3;
            }
            break;
         case 2:
            if (!(video_config & PUAE_VIDEO_SUPERHIRES))
            {
               changed_prefs.gfx_resolution = RES_SUPERHIRES;
               video_config |= PUAE_VIDEO_SUPERHIRES;
               video_config &= ~PUAE_VIDEO_HIRES;
               defaultw = retrow = PUAE_VIDEO_WIDTH * 2;
               retro_max_diwlastword = retro_max_diwlastword_hires * 2;
               request_init_custom_timer = 3;
            }
            break;
      }

      // Horizontal centering calculation needs to be forced due to retro_max_diwlastword change, which is crucial for visible_left_border
      if (request_init_custom_timer > 0)
      {
         retro_min_diwstart_old = -1;
         retro_max_diwstop_old = -1;
         visible_left_border = retro_max_diwlastword - retrow;
      }
   }

   // Automatic video vresolution (Line Mode)
   if (opt_video_vresolution_auto && request_init_custom_timer == 0)
   {
      int current_interlace = interlace_seen;

      // Lores force to single line
      if (!(video_config & PUAE_VIDEO_HIRES) && !(video_config & PUAE_VIDEO_SUPERHIRES))
         current_interlace = 0;

      switch (current_interlace)
      {
         case -1:
         case 1:
            if (!(video_config & PUAE_VIDEO_DOUBLELINE))
            {
               changed_prefs.gfx_vresolution = VRES_DOUBLE;
               video_config |= PUAE_VIDEO_DOUBLELINE;
               defaulth = retroh = (video_config & PUAE_VIDEO_NTSC) ? PUAE_VIDEO_HEIGHT_NTSC : PUAE_VIDEO_HEIGHT_PAL;
               request_init_custom_timer = 3;
            }
            break;
         case 0:
            if ((video_config & PUAE_VIDEO_DOUBLELINE))
            {
               changed_prefs.gfx_vresolution = VRES_NONDOUBLE;
               video_config &= ~PUAE_VIDEO_DOUBLELINE;
               defaulth = retroh = (video_config & PUAE_VIDEO_NTSC) ? PUAE_VIDEO_HEIGHT_NTSC / 2 : PUAE_VIDEO_HEIGHT_PAL / 2;
               request_init_custom_timer = 3;
            }
            break;
      }

      // Update av_info
      if (request_init_custom_timer > 0)
         retro_request_av_info_update = true;
   }

   // Automatic vertical offset
   if (opt_vertical_offset_auto && zoom_mode_id != 0)
   {
      if (((retro_thisframe_first_drawn_line != retro_thisframe_first_drawn_line_old)
         ||(retro_thisframe_last_drawn_line  != retro_thisframe_last_drawn_line_old))
         && retro_thisframe_first_drawn_line != -1
         && retro_thisframe_last_drawn_line  != -1
      )
      {
         // Prevent interlace stuttering by requiring a change of at least 2 lines
         if (abs(retro_thisframe_first_drawn_line_old - retro_thisframe_first_drawn_line) > 1)
         {
            retro_thisframe_first_drawn_line_old = retro_thisframe_first_drawn_line;
            retro_request_av_info_update = true;
         }
         if (abs(retro_thisframe_last_drawn_line_old - retro_thisframe_last_drawn_line) > 1)
         {
            retro_thisframe_last_drawn_line_old = retro_thisframe_last_drawn_line;
            retro_request_av_info_update = true;
         }
      }
   }
   else
   {
      // Vertical offset must not be set too early
      if (thisframe_y_adjust_update_frame_timer > 0)
      {
         thisframe_y_adjust_update_frame_timer--;
         if ((thisframe_y_adjust_update_frame_timer == 0) && (opt_vertical_offset != 0))
         {
            thisframe_y_adjust = minfirstline + opt_vertical_offset;
            request_reset_drawing = true;
         }
      }
   }

   // Automatic horizontal offset
   if (opt_horizontal_offset_auto)
   {
      if ((retro_min_diwstart != retro_min_diwstart_old) ||
          (retro_max_diwstop != retro_max_diwstop_old))
      {
         retro_min_diwstart_old = retro_min_diwstart;
         retro_max_diwstop_old = retro_max_diwstop;
         retro_request_av_info_update = true;
      }
   }
   else
   {
      // Horizontal offset must not be set too early
      if (visible_left_border_update_frame_timer > 0)
      {
         visible_left_border_update_frame_timer--;
         if (visible_left_border_update_frame_timer == 0)
         {
            visible_left_border = retro_max_diwlastword - retrow - opt_horizontal_offset;
            request_reset_drawing = true;
         }
      }
   }
}

void retro_run(void)
{
   // Core options
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();

   // Handle statusbar text, audio filter type & video geometry + resolution
   update_audiovideo();

   // AV info change is requested
   if (retro_request_av_info_update)
      retro_update_av_info();

   // Poll inputs
   retro_poll_event();

   // If any drawing parameters/offsets have been modified,
   // must call reset_drawing() to ensure that the changes
   // are 'registered' by center_image() in drawing.c
   // > If we don't do this, the wrong parameters may be
   //   used on the next frame, which can lead to out of
   //   bounds video buffer access (memory corruption)
   // > This check must come *after* horizontal/vertical
   //   offset calculation, retro_update_av_info() and
   //   retro_poll_event()
   if (request_reset_drawing)
   {
      request_reset_drawing = false;
      reset_drawing();
   }

   // Dynamic resolution changing requires a frame breather after reset_drawing()
   if (request_init_custom_timer > 0)
   {
      if (request_init_custom_timer == 2)
         request_reset_drawing = true;
      request_init_custom_timer--;
      if (request_init_custom_timer == 0)
         init_custom ();
   }

   // Refresh CPU prefs
   if (request_check_prefs_timer > 0)
   {
      request_check_prefs_timer--;
      if (request_check_prefs_timer == 0)
      {
         update_variables();
         config_changed = 1;
         check_prefs_changed_audio();
         check_prefs_changed_custom();
         check_prefs_changed_cpu();
         config_changed = 0;
      }
   }

   // Check if a restart is required
   if (restart_pending)
   {
      restart_pending = 0;
      libretro_do_restart(sizeof(uae_argv)/sizeof(*uae_argv), uae_argv);
      // Re-run emulation first pass
      restart_pending = m68k_go(1, 0);
      video_cb(retro_bmp, retrow, zoomed_height, retrow << (pix_bytes / 2));
      return;
   }

   // Resume emulation for 1 frame
   restart_pending = m68k_go(1, 1);

   if (STATUSON == 1)
      print_statusbar();
   if (SHOWKEY == 1)
   {
      // Virtual keyboard requires a graceful redraw, blunt reset_drawing() interferes with zoom
      frame_redraw_necessary = 2;
      print_virtual_kbd(retro_bmp);
   }
   // Maximum 288p/576p PAL shenanigans:
   // Mask the last line(s), since UAE does not refresh the last line, and even its own OSD will leave trails
   if (video_config & PUAE_VIDEO_PAL)
   {
      if (video_config & PUAE_VIDEO_DOUBLELINE)
      {
         if (interlace_seen)
         {
            DrawHline(retro_bmp, 0, 572, retrow, 0, 0);
            DrawHline(retro_bmp, 0, 573, retrow, 0, 0);
            DrawHline(retro_bmp, 0, 574, retrow, 0, 0);
            DrawHline(retro_bmp, 0, 575, retrow, 0, 0);
         }
         else
         {
            DrawHline(retro_bmp, 0, 574, retrow, 0, 0);
            DrawHline(retro_bmp, 0, 575, retrow, 0, 0);
         }
      }
      else
      {
         DrawHline(retro_bmp, 0, 287, retrow, 0, 0);
      }
   }
   video_cb(retro_bmp, retrow, zoomed_height, retrow << (pix_bytes / 2));
}

bool retro_load_game(const struct retro_game_info *info)
{
   // UAE config
   if (info)
      strcpy(full_path, (char*)info->path);
   static bool retro_return;
   retro_return = retro_create_config();
   if (!retro_return)
      return false;

   // Screen resolution
   fprintf(stderr, "[libretro-uae]: Resolution selected: %dx%d\n", defaultw, defaulth);
   retrow = defaultw;
   retroh = defaulth;

   // Initialise emulation
   umain(sizeof(uae_argv)/sizeof(*uae_argv), uae_argv);

   // Run emulation first pass
   restart_pending = m68k_go(1, 0);
   // > We are now ready to enter the run loop
   libretro_runloop_active = 1;

   // Save states
   // > Ensure that save state file path is empty,
   //   since we use memory based save states
   savestate_fname[0] = '\0';
   // > Get save state size
   //   Here we use initial size + 5%
   //   Should be sufficient in all cases
   // NOTE: It would be better to calculate the
   // state size based on current config parameters,
   // but while
   //   - currprefs.chipmem_size
   //   - currprefs.bogomem_size
   //   - currprefs.fastmem_size
   // account for *most* of the size, there are
   // simply too many other factors to rely on this
   // alone (i.e. mem size + 5% is fine in most cases,
   // but if the user supplies a custom uae config file
   // then this is not adequate at all). Untangling the
   // full set of values that are recorded is beyond
   // my patience...
   struct zfile *state_file = save_state("libretro", 0);

   if (state_file)
   {
      save_state_file_size  = (size_t)zfile_size(state_file);
      save_state_file_size += (size_t)(((float)save_state_file_size * 0.05f) + 0.5f);
      zfile_fclose(state_file);
   }

   return true;
}

void retro_unload_game(void)
{
   // Ensure save state de-serialization file
   // is closed/NULL
   // Note: Have to do this here (not in retro_deinit())
   // since leave_program() calls zfile_exit()
   if (retro_deserialize_file)
   {
      zfile_fclose(retro_deserialize_file);
      retro_deserialize_file = NULL;
   }

   leave_program();

   libretro_runloop_active = 0;
}

unsigned retro_get_region(void)
{
   return (video_config & PUAE_VIDEO_NTSC) ? RETRO_REGION_NTSC : RETRO_REGION_PAL;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t retro_serialize_size(void)
{
   return save_state_file_size;
}

bool retro_serialize(void *data_, size_t size)
{
   struct zfile *state_file = save_state("libretro", (uae_u64)save_state_file_size);
   bool success = false;

   if (state_file)
   {
      uae_s64 state_file_size = zfile_size(state_file);

      if (size >= state_file_size)
      {
         size_t len = zfile_fread(data_, 1, state_file_size, state_file);

         if (len == state_file_size)
            success = true;
      }

      zfile_fclose(state_file);
   }

   return success;
}

bool retro_unserialize(const void *data_, size_t size)
{
   // TODO: When attempting to use runahead, CD32
   // and WHDLoad content will hang on boot. It seems
   // we cannot restore a state until the system has
   // passed some level of initialisation - but the
   // point at which a restore becomes 'safe' is
   // unknown (for CD32 content, for example, we have
   // to wait ~300 frames before runahead can be enabled)
   bool success = false;

   // Cannot restore state while any 'savestate'
   // operation is underway
   // > Actual restore is deferred until m68k_go(),
   //   so we have to use a shared shared state file
   //   object - this cannot be modified until the
   //   restore is complete
   // > Note that this condition should never be
   //   true - if a save state operation is underway
   //   at this point then we are dealing with an
   //   unknown error
   if (!savestate_state)
   {
      // Savestates also save CPU prefs, therefore refresh core options, but skip it for now
      //request_check_prefs_timer = 4;

      if (retro_deserialize_file)
      {
         zfile_fclose(retro_deserialize_file);
         retro_deserialize_file = NULL;
      }

      retro_deserialize_file = zfile_fopen_empty(NULL, "libretro", size);

      if (retro_deserialize_file)
      {
         size_t len = zfile_fwrite(data_, 1, size, retro_deserialize_file);

         if (len == size)
         {
            unsigned frame_counter = 0;
            unsigned max_frames    = 50;

            zfile_fseek(retro_deserialize_file, 0, SEEK_SET);
            savestate_state = STATE_DORESTORE;

            // For correct operation of the frontend,
            // the save state restore must be completed
            // by the time this function returns.
            // Since P-UAE requires several (2) frames to get
            // itself in order during a restore event, we
            // have to keep emulating frames until the
            // restore is complete...
            // > Note that we set a 'timeout' of 50 frames
            //   here (1s of emulated time at 50Hz) to
            //   prevent lock-ups in the event of unexpected
            //   errors
            // > Temporarily 'deactivate' runloop - this lets
            //   us call m68k_go() without accessing frontend
            //   features - specifically, it disables the audio
            //   callback functionality
            libretro_runloop_active = 0;
            while (savestate_state && (frame_counter < max_frames))
            {
               // Note that retro_deserialize_file will be
               // closed inside m68k_go() upon successful
               // completion of the restore event
               restart_pending = m68k_go(1, 1);
               frame_counter++;
            }
            libretro_runloop_active = 1;

            // If the above while loop times out, then
            // everything is completely broken. We cannot
            // handle this here, so just assume the restore
            // completed successfully...
            request_reset_drawing = true;
            success               = true;
         }
         else
         {
            zfile_fclose(retro_deserialize_file);
            retro_deserialize_file = NULL;
         }
      }
   }

   return success;
}

void *retro_get_memory_data(unsigned id)
{
#if defined(NATMEM_OFFSET)
   if ( id == RETRO_MEMORY_SYSTEM_RAM )
      return natmem_offset;
#endif
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
#if defined(NATMEM_OFFSET)
   if ( id == RETRO_MEMORY_SYSTEM_RAM )
      return natmem_size;
#endif
   return 0;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

int cp(const char *to, const char *from)
{
    int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;
    int saved_errno;

    fd_from = open(from, O_RDONLY);
    if (fd_from < 0)
        return -1;

    fd_to = open(to, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd_to < 0)
        goto out_error;

    while (nread = read(fd_from, buf, sizeof buf), nread > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;

        do {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);
    }

    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            goto out_error;
        }
        close(fd_from);

        /* Success! */
        return 0;
    }

  out_error:
    saved_errno = errno;

    close(fd_from);
    if (fd_to >= 0)
        close(fd_to);

    errno = saved_errno;
    return -1;
}

#if defined(ANDROID) || defined(__SWITCH__) || defined(WIIU)
#include <sys/timeb.h>

int ftime(struct timeb *tb)
{
    struct timeval  tv;
    struct timezone tz;

    if (gettimeofday (&tv, &tz) < 0)
        return -1;

    tb->time    = tv.tv_sec;
    tb->millitm = (tv.tv_usec + 500) / 1000;

    if (tb->millitm == 1000)
    {
        ++tb->time;
        tb->millitm = 0;
    }
    tb->timezone = tz.tz_minuteswest;
    tb->dstflag  = tz.tz_dsttime;

    return 0;
}
#endif

