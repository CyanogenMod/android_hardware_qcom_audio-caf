/*
** Copyright 2010, The Android Open-Source Project
** Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef _AUDIO_H_
#define _AUDIO_H_

#include <sound/asound.h>
#define PCM_ERROR_MAX 128

struct pcm {
    int fd;
    int timer_fd;
    unsigned rate;
    unsigned channels;
    unsigned flags;
    unsigned format;
    unsigned running:1;
    int underruns;
    unsigned buffer_size;
    unsigned period_size;
    unsigned period_cnt;
    char error[PCM_ERROR_MAX];
    struct snd_pcm_hw_params *hw_p;
    struct snd_pcm_sw_params *sw_p;
    struct snd_pcm_sync_ptr *sync_ptr;
    struct snd_pcm_channel_info ch[2];
    void *addr;
    int card_no;
    int device_no;
    int start;
};
#define FORMAT(v) SNDRV_PCM_FORMAT_##v

#define PCM_OUT        0x00000000
#define PCM_IN         0x10000000

#define PCM_STEREO     0x00000000
#define PCM_MONO       0x01000000
#define PCM_QUAD       0x02000000
#define PCM_5POINT1    0x04000000
#define PCM_7POINT1    0x08000000

#define PCM_44100HZ    0x00000000
#define PCM_48000HZ    0x00100000
#define PCM_8000HZ     0x00200000
#define PCM_RATE_MASK  0x00F00000

#define PCM_MMAP       0x00010000
#define PCM_NMMAP      0x00000000

#define DEBUG_ON       0x00000001
#define DEBUG_OFF      0x00000000

#define PCM_PERIOD_CNT_MIN 2
#define PCM_PERIOD_CNT_SHIFT 16
#define PCM_PERIOD_CNT_MASK (0xF << PCM_PERIOD_CNT_SHIFT)
#define PCM_PERIOD_SZ_MIN 128
#define PCM_PERIOD_SZ_SHIFT 12
#define PCM_PERIOD_SZ_MASK (0xF << PCM_PERIOD_SZ_SHIFT)

#define TIMEOUT_INFINITE  -1

/* Acquire/release a pcm channel.
 * Returns non-zero on error
 */

struct mixer_ctl {
    struct mixer *mixer;
    struct snd_ctl_elem_info *info;
    char **ename;
};

#define __snd_alloca(ptr,type) do { *ptr = (type *) alloca(sizeof(type)); memset(*ptr, 0, sizeof(type)); } while (0)
#define snd_ctl_elem_id_alloca(ptr) __snd_alloca(ptr, snd_ctl_elem_id)
#define snd_ctl_card_info_alloca(ptr) __snd_alloca(ptr, snd_ctl_card_info)
#define snd_ctl_event_alloca(ptr) __snd_alloca(ptr, snd_ctl_event)
#define snd_ctl_elem_list_alloca(ptr) __snd_alloca(ptr, snd_ctl_elem_list)
#define snd_ctl_elem_info_alloca(ptr) __snd_alloca(ptr, snd_ctl_elem_info)
#define snd_ctl_elem_value_alloca(ptr) __snd_alloca(ptr, snd_ctl_elem_value)


enum snd_pcm_stream_t {
        /** Playback stream */
        SND_PCM_STREAM_PLAYBACK = 0,
        /** Capture stream */
        SND_PCM_STREAM_CAPTURE,
        SND_PCM_STREAM_LAST = SND_PCM_STREAM_CAPTURE
};

enum _snd_ctl_elem_iface {
        /** Card level */
        SND_CTL_ELEM_IFACE_CARD = 0,
        /** Hardware dependent device */
        SND_CTL_ELEM_IFACE_HWDEP,
        /** Mixer */
        SND_CTL_ELEM_IFACE_MIXER,
        /** PCM */
        SND_CTL_ELEM_IFACE_PCM,
        /** RawMidi */
        SND_CTL_ELEM_IFACE_RAWMIDI,
        /** Timer */
        SND_CTL_ELEM_IFACE_TIMER,
        /** Sequencer */
        SND_CTL_ELEM_IFACE_SEQUENCER,
        SND_CTL_ELEM_IFACE_LAST = SND_CTL_ELEM_IFACE_SEQUENCER
};

struct mixer {
    int fd;
    struct snd_ctl_elem_info *info;
    struct mixer_ctl *ctl;
    unsigned count;
};

int get_format(const char* name);
const char *get_format_name(int format);
const char *get_format_desc(int format);
struct pcm *pcm_open(unsigned flags, char *device);
int pcm_close(struct pcm *pcm);
int pcm_ready(struct pcm *pcm);
int mmap_buffer(struct pcm *pcm);
u_int8_t *dst_address(struct pcm *pcm);
int sync_ptr(struct pcm *pcm);

void param_init(struct snd_pcm_hw_params *p);
void param_set_mask(struct snd_pcm_hw_params *p, int n, unsigned bit);
void param_set_min(struct snd_pcm_hw_params *p, int n, unsigned val);
void param_set_int(struct snd_pcm_hw_params *p, int n, unsigned val);
void param_set_max(struct snd_pcm_hw_params *p, int n, unsigned val);
int param_set_hw_refine(struct pcm *pcm, struct snd_pcm_hw_params *params);
int param_set_hw_params(struct pcm *pcm, struct snd_pcm_hw_params *params);
int param_set_sw_params(struct pcm *pcm, struct snd_pcm_sw_params *sparams);
int get_compressed_format(const char *format);
void param_dump(struct snd_pcm_hw_params *p);
int pcm_prepare(struct pcm *pcm);
long pcm_avail(struct pcm *pcm);
int pcm_set_channel_map(struct pcm *pcm, struct mixer *mixer,
                        int max_channels, char *chmap);

/* Returns a human readable reason for the last error. */
const char *pcm_error(struct pcm *pcm);

/* Returns the buffer size (int bytes) that should be used for pcm_write.
 * This will be 1/2 of the actual fifo size.
 */
int pcm_buffer_size(struct snd_pcm_hw_params *params);
int pcm_period_size(struct snd_pcm_hw_params *params);

/* Write data to the fifo.
 * Will start playback on the first write or on a write that
 * occurs after a fifo underrun.
 */
int pcm_write(struct pcm *pcm, void *data, unsigned count);
int pcm_read(struct pcm *pcm, void *data, unsigned count);

struct mixer;
struct mixer_ctl;

struct mixer *mixer_open(const char *device);
void mixer_close(struct mixer *mixer);
void mixer_dump(struct mixer *mixer);

struct mixer_ctl *mixer_get_control(struct mixer *mixer,
                                    const char *name, unsigned index);
struct mixer_ctl *mixer_get_nth_control(struct mixer *mixer, unsigned n);

int mixer_ctl_set(struct mixer_ctl *ctl, unsigned percent);
int mixer_ctl_select(struct mixer_ctl *ctl, const char *value);
void mixer_ctl_get(struct mixer_ctl *ctl, unsigned *value);
void mixer_ctl_get_mulvalues(struct mixer_ctl *ctl, unsigned **value, unsigned *count);
int mixer_ctl_set_value(struct mixer_ctl *ctl, int count, char ** argv);


#define MAX_NUM_CODECS 32

#ifndef QCOM_COMPRESSED_AUDIO_ENABLED
/* compressed audio support */

/* AUDIO CODECS SUPPORTED */
#define MAX_NUM_CODECS 32
#define MAX_NUM_CODEC_DESCRIPTORS 32
#define MAX_NUM_BITRATES 32

/* compressed TX */
#define MAX_NUM_FRAMES_PER_BUFFER 1
#define COMPRESSED_META_DATA_MODE 0x10
#define META_DATA_LEN_BYTES 36
#define Q6_AC3_DECODER	0x00010BF6
#define Q6_EAC3_DECODER 0x00010C3C
#define Q6_DTS		0x00010D88
#define Q6_DTS_LBR	0x00010DBB

/* Codecs are listed linearly to allow for extensibility */
#define SND_AUDIOCODEC_PCM                   ((__u32) 0x00000001)
#define SND_AUDIOCODEC_MP3                   ((__u32) 0x00000002)
#define SND_AUDIOCODEC_AMR                   ((__u32) 0x00000003)
#define SND_AUDIOCODEC_AMRWB                 ((__u32) 0x00000004)
#define SND_AUDIOCODEC_AMRWBPLUS             ((__u32) 0x00000005)
#define SND_AUDIOCODEC_AAC                   ((__u32) 0x00000006)
#define SND_AUDIOCODEC_WMA                   ((__u32) 0x00000007)
#define SND_AUDIOCODEC_REAL                  ((__u32) 0x00000008)
#define SND_AUDIOCODEC_VORBIS                ((__u32) 0x00000009)
#define SND_AUDIOCODEC_FLAC                  ((__u32) 0x0000000A)
#define SND_AUDIOCODEC_IEC61937              ((__u32) 0x0000000B)
#define SND_AUDIOCODEC_G723_1                ((__u32) 0x0000000C)
#define SND_AUDIOCODEC_G729                  ((__u32) 0x0000000D)
#define SND_AUDIOCODEC_AC3                   ((__u32) 0x0000000E)
#define SND_AUDIOCODEC_DTS                   ((__u32) 0x0000000F)
#define SND_AUDIOCODEC_AC3_PASS_THROUGH      ((__u32) 0x00000010)
#define SND_AUDIOCODEC_WMA_PRO               ((__u32) 0x00000011)
#define SND_AUDIOCODEC_DTS_PASS_THROUGH      ((__u32) 0x00000012)
#define SND_AUDIOCODEC_DTS_LBR               ((__u32) 0x00000013)
#define SND_AUDIOCODEC_DTS_TRANSCODE_LOOPBACK ((__u32) 0x00000014)
#define SND_AUDIOCODEC_PASS_THROUGH          ((__u32) 0x00000015)
#define SND_AUDIOCODEC_MP2                   ((__u32) 0x00000016)
#define SND_AUDIOCODEC_DTS_LBR_PASS_THROUGH  ((__u32) 0x00000017)
/*
 * Profile and modes are listed with bit masks. This allows for a
 * more compact representation of fields that will not evolve
 * (in contrast to the list of codecs)
 */

#define SND_AUDIOPROFILE_PCM                 ((__u32) 0x00000001)

/* MP3 modes are only useful for encoders */
#define SND_AUDIOCHANMODE_MP3_MONO           ((__u32) 0x00000001)
#define SND_AUDIOCHANMODE_MP3_STEREO         ((__u32) 0x00000002)
#define SND_AUDIOCHANMODE_MP3_JOINTSTEREO    ((__u32) 0x00000004)
#define SND_AUDIOCHANMODE_MP3_DUAL           ((__u32) 0x00000008)

#define SND_AUDIOPROFILE_AMR                 ((__u32) 0x00000001)

/* AMR modes are only useful for encoders */
#define SND_AUDIOMODE_AMR_DTX_OFF            ((__u32) 0x00000001)
#define SND_AUDIOMODE_AMR_VAD1               ((__u32) 0x00000002)
#define SND_AUDIOMODE_AMR_VAD2               ((__u32) 0x00000004)

#define SND_AUDIOSTREAMFORMAT_UNDEFINED	     ((__u32) 0x00000000)
#define SND_AUDIOSTREAMFORMAT_CONFORMANCE    ((__u32) 0x00000001)
#define SND_AUDIOSTREAMFORMAT_IF1            ((__u32) 0x00000002)
#define SND_AUDIOSTREAMFORMAT_IF2            ((__u32) 0x00000004)
#define SND_AUDIOSTREAMFORMAT_FSF            ((__u32) 0x00000008)
#define SND_AUDIOSTREAMFORMAT_RTPPAYLOAD     ((__u32) 0x00000010)
#define SND_AUDIOSTREAMFORMAT_ITU            ((__u32) 0x00000020)

#define SND_AUDIOPROFILE_AMRWB               ((__u32) 0x00000001)

/* AMRWB modes are only useful for encoders */
#define SND_AUDIOMODE_AMRWB_DTX_OFF          ((__u32) 0x00000001)
#define SND_AUDIOMODE_AMRWB_VAD1             ((__u32) 0x00000002)
#define SND_AUDIOMODE_AMRWB_VAD2             ((__u32) 0x00000004)

#define SND_AUDIOPROFILE_AMRWBPLUS           ((__u32) 0x00000001)

#define SND_AUDIOPROFILE_AAC                 ((__u32) 0x00000001)

/* AAC modes are required for encoders and decoders */
#define SND_AUDIOMODE_AAC_MAIN               ((__u32) 0x00000001)
#define SND_AUDIOMODE_AAC_LC                 ((__u32) 0x00000002)
#define SND_AUDIOMODE_AAC_SSR                ((__u32) 0x00000004)
#define SND_AUDIOMODE_AAC_LTP                ((__u32) 0x00000008)
#define SND_AUDIOMODE_AAC_HE                 ((__u32) 0x00000010)
#define SND_AUDIOMODE_AAC_SCALABLE           ((__u32) 0x00000020)
#define SND_AUDIOMODE_AAC_ERLC               ((__u32) 0x00000040)
#define SND_AUDIOMODE_AAC_LD                 ((__u32) 0x00000080)
#define SND_AUDIOMODE_AAC_HE_PS              ((__u32) 0x00000100)
#define SND_AUDIOMODE_AAC_HE_MPS             ((__u32) 0x00000200)

/* AAC formats are required for encoders and decoders */
#define SND_AUDIOSTREAMFORMAT_MP2ADTS        ((__u32) 0x00000001)
#define SND_AUDIOSTREAMFORMAT_MP4ADTS        ((__u32) 0x00000002)
#define SND_AUDIOSTREAMFORMAT_MP4LOAS        ((__u32) 0x00000004)
#define SND_AUDIOSTREAMFORMAT_MP4LATM        ((__u32) 0x00000008)
#define SND_AUDIOSTREAMFORMAT_ADIF           ((__u32) 0x00000010)
#define SND_AUDIOSTREAMFORMAT_MP4FF          ((__u32) 0x00000020)
#define SND_AUDIOSTREAMFORMAT_RAW            ((__u32) 0x00000040)

#define SND_AUDIOPROFILE_WMA7                ((__u32) 0x00000001)
#define SND_AUDIOPROFILE_WMA8                ((__u32) 0x00000002)
#define SND_AUDIOPROFILE_WMA9                ((__u32) 0x00000004)
#define SND_AUDIOPROFILE_WMA10               ((__u32) 0x00000008)

#define SND_AUDIOMODE_WMA_LEVEL1             ((__u32) 0x00000001)
#define SND_AUDIOMODE_WMA_LEVEL2             ((__u32) 0x00000002)
#define SND_AUDIOMODE_WMA_LEVEL3             ((__u32) 0x00000004)
#define SND_AUDIOMODE_WMA_LEVEL4             ((__u32) 0x00000008)
#define SND_AUDIOMODE_WMAPRO_LEVELM0         ((__u32) 0x00000010)
#define SND_AUDIOMODE_WMAPRO_LEVELM1         ((__u32) 0x00000020)
#define SND_AUDIOMODE_WMAPRO_LEVELM2         ((__u32) 0x00000040)
#define SND_AUDIOMODE_WMAPRO_LEVELM3         ((__u32) 0x00000080)

#define SND_AUDIOSTREAMFORMAT_WMA_ASF        ((__u32) 0x00000001)
/*
 * Some implementations strip the ASF header and only send ASF packets
 * to the DSP
 */
#define SND_AUDIOSTREAMFORMAT_WMA_NOASF_HDR  ((__u32) 0x00000002)

#define SND_AUDIOPROFILE_REALAUDIO           ((__u32) 0x00000001)

#define SND_AUDIOMODE_REALAUDIO_G2           ((__u32) 0x00000001)
#define SND_AUDIOMODE_REALAUDIO_8            ((__u32) 0x00000002)
#define SND_AUDIOMODE_REALAUDIO_10           ((__u32) 0x00000004)
#define SND_AUDIOMODE_REALAUDIO_SURROUND     ((__u32) 0x00000008)

#define SND_AUDIOPROFILE_VORBIS              ((__u32) 0x00000001)

#define SND_AUDIOMODE_VORBIS                 ((__u32) 0x00000001)

#define SND_AUDIOPROFILE_FLAC                ((__u32) 0x00000001)

/*
 * Define quality levels for FLAC encoders, from LEVEL0 (fast)
 * to LEVEL8 (best)
 */
#define SND_AUDIOMODE_FLAC_LEVEL0            ((__u32) 0x00000001)
#define SND_AUDIOMODE_FLAC_LEVEL1            ((__u32) 0x00000002)
#define SND_AUDIOMODE_FLAC_LEVEL2            ((__u32) 0x00000004)
#define SND_AUDIOMODE_FLAC_LEVEL3            ((__u32) 0x00000008)
#define SND_AUDIOMODE_FLAC_LEVEL4            ((__u32) 0x00000010)
#define SND_AUDIOMODE_FLAC_LEVEL5            ((__u32) 0x00000020)
#define SND_AUDIOMODE_FLAC_LEVEL6            ((__u32) 0x00000040)
#define SND_AUDIOMODE_FLAC_LEVEL7            ((__u32) 0x00000080)
#define SND_AUDIOMODE_FLAC_LEVEL8            ((__u32) 0x00000100)

#define SND_AUDIOSTREAMFORMAT_FLAC           ((__u32) 0x00000001)
#define SND_AUDIOSTREAMFORMAT_FLAC_OGG       ((__u32) 0x00000002)

/* IEC61937 payloads without CUVP and preambles */
#define SND_AUDIOPROFILE_IEC61937            ((__u32) 0x00000001)
/* IEC61937 with S/PDIF preambles+CUVP bits in 32-bit containers */
#define SND_AUDIOPROFILE_IEC61937_SPDIF      ((__u32) 0x00000002)

/*
 * IEC modes are mandatory for decoders. Format autodetection
 * will only happen on the DSP side with mode 0. The PCM mode should
 * not be used, the PCM codec should be used instead.
 */
#define SND_AUDIOMODE_IEC_REF_STREAM_HEADER  ((__u32) 0x00000000)
#define SND_AUDIOMODE_IEC_LPCM		     ((__u32) 0x00000001)
#define SND_AUDIOMODE_IEC_AC3		     ((__u32) 0x00000002)
#define SND_AUDIOMODE_IEC_MPEG1		     ((__u32) 0x00000004)
#define SND_AUDIOMODE_IEC_MP3		     ((__u32) 0x00000008)
#define SND_AUDIOMODE_IEC_MPEG2		     ((__u32) 0x00000010)
#define SND_AUDIOMODE_IEC_AACLC		     ((__u32) 0x00000020)
#define SND_AUDIOMODE_IEC_DTS		     ((__u32) 0x00000040)
#define SND_AUDIOMODE_IEC_ATRAC		     ((__u32) 0x00000080)
#define SND_AUDIOMODE_IEC_SACD		     ((__u32) 0x00000100)
#define SND_AUDIOMODE_IEC_EAC3		     ((__u32) 0x00000200)
#define SND_AUDIOMODE_IEC_DTS_HD	     ((__u32) 0x00000400)
#define SND_AUDIOMODE_IEC_MLP		     ((__u32) 0x00000800)
#define SND_AUDIOMODE_IEC_DST		     ((__u32) 0x00001000)
#define SND_AUDIOMODE_IEC_WMAPRO	     ((__u32) 0x00002000)
#define SND_AUDIOMODE_IEC_REF_CXT            ((__u32) 0x00004000)
#define SND_AUDIOMODE_IEC_HE_AAC	     ((__u32) 0x00008000)
#define SND_AUDIOMODE_IEC_HE_AAC2	     ((__u32) 0x00010000)
#define SND_AUDIOMODE_IEC_MPEG_SURROUND	     ((__u32) 0x00020000)

#define SND_AUDIOPROFILE_G723_1              ((__u32) 0x00000001)

#define SND_AUDIOMODE_G723_1_ANNEX_A         ((__u32) 0x00000001)
#define SND_AUDIOMODE_G723_1_ANNEX_B         ((__u32) 0x00000002)
#define SND_AUDIOMODE_G723_1_ANNEX_C         ((__u32) 0x00000004)

#define SND_AUDIOPROFILE_G729                ((__u32) 0x00000001)

#define SND_AUDIOMODE_G729_ANNEX_A           ((__u32) 0x00000001)
#define SND_AUDIOMODE_G729_ANNEX_B           ((__u32) 0x00000002)

/* <FIXME: multichannel encoders aren't supported for now. Would need
   an additional definition of channel arrangement> */

/* VBR/CBR definitions */
#define SND_RATECONTROLMODE_CONSTANTBITRATE  ((__u32) 0x00000001)
#define SND_RATECONTROLMODE_VARIABLEBITRATE  ((__u32) 0x00000002)

/* Encoder options */

struct snd_enc_wma {
	__u32 super_block_align; /* WMA Type-specific data */
	__u32 bits_per_sample;
	__u32 channelmask;
	__u32 encodeopt;
	__u32 encodeopt1;
	__u32 encodeopt2;
};


/**
 * struct snd_enc_vorbis
 * @quality: Sets encoding quality to n, between -1 (low) and 10 (high).
 * In the default mode of operation, the quality level is 3.
 * Normal quality range is 0 - 10.
 * @managed: Boolean. Set  bitrate  management  mode. This turns off the
 * normal VBR encoding, but allows hard or soft bitrate constraints to be
 * enforced by the encoder. This mode can be slower, and may also be
 * lower quality. It is primarily useful for streaming.
 * @max_bit_rate: Enabled only if managed is TRUE
 * @min_bit_rate: Enabled only if managed is TRUE
 * @downmix: Boolean. Downmix input from stereo to mono (has no effect on
 * non-stereo streams). Useful for lower-bitrate encoding.
 *
 * These options were extracted from the OpenMAX IL spec and Gstreamer vorbisenc
 * properties
 *
 * For best quality users should specify VBR mode and set quality levels.
 */

struct snd_enc_vorbis {
	__s32 quality;
	__u32 managed;
	__u32 max_bit_rate;
	__u32 min_bit_rate;
	__u32 downmix;
};


/**
 * struct snd_enc_real
 * @quant_bits: number of coupling quantization bits in the stream
 * @start_region: coupling start region in the stream
 * @num_regions: number of regions value
 *
 * These options were extracted from the OpenMAX IL spec
 */

struct snd_enc_real {
	__u32 quant_bits;
	__u32 start_region;
	__u32 num_regions;
};

/**
 * struct snd_enc_flac
 * @num: serial number, valid only for OGG formats
 *	needs to be set by application
 * @gain: Add replay gain tags
 *
 * These options were extracted from the FLAC online documentation
 * at http://flac.sourceforge.net/documentation_tools_flac.html
 *
 * To make the API simpler, it is assumed that the user will select quality
 * profiles. Additional options that affect encoding quality and speed can
 * be added at a later stage if needed.
 *
 * By default the Subset format is used by encoders.
 *
 * TAGS such as pictures, etc, cannot be handled by an offloaded encoder and are
 * not supported in this API.
 */

struct snd_enc_flac {
	__u32 num;
	__u32 gain;
};

struct snd_enc_generic {
	__u32 bw;	/* encoder bandwidth */
	__s32 reserved[15];
};
struct snd_dec_dts {
	__u32 modelIdLength;
	__u8 *modelId;
};

union snd_codec_options {
	struct snd_enc_wma wma;
	struct snd_enc_vorbis vorbis;
	struct snd_enc_real real;
	struct snd_enc_flac flac;
	struct snd_enc_generic generic;
	struct snd_dec_dts dts;
};

/** struct snd_codec_desc - description of codec capabilities
 * @max_ch: Maximum number of audio channels
 * @sample_rates: Sampling rates in Hz, use SNDRV_PCM_RATE_xxx for this
 * @bit_rate: Indexed array containing supported bit rates
 * @num_bitrates: Number of valid values in bit_rate array
 * @rate_control: value is specified by SND_RATECONTROLMODE defines.
 * @profiles: Supported profiles. See SND_AUDIOPROFILE defines.
 * @modes: Supported modes. See SND_AUDIOMODE defines
 * @formats: Supported formats. See SND_AUDIOSTREAMFORMAT defines
 * @min_buffer: Minimum buffer size handled by codec implementation
 * @reserved: reserved for future use
 *
 * This structure provides a scalar value for profiles, modes and stream
 * format fields.
 * If an implementation supports multiple combinations, they will be listed as
 * codecs with different descriptors, for example there would be 2 descriptors
 * for AAC-RAW and AAC-ADTS.
 * This entails some redundancy but makes it easier to avoid invalid
 * configurations.
 *
 */

struct snd_codec_desc {
	__u32 max_ch;
	__u32 sample_rates;
	__u32 bit_rate[MAX_NUM_BITRATES];
	__u32 num_bitrates;
	__u32 rate_control;
	__u32 profiles;
	__u32 modes;
	__u32 formats;
	__u32 min_buffer;
	__u32 reserved[15];
};

/** struct snd_codec
 * @id: Identifies the supported audio encoder/decoder.
 *		See SND_AUDIOCODEC macros.
 * @ch_in: Number of input audio channels
 * @ch_out: Number of output channels. In case of contradiction between
 *		this field and the channelMode field, the channelMode field
 *		overrides.
 * @sample_rate: Audio sample rate of input data
 * @bit_rate: Bitrate of encoded data. May be ignored by decoders
 * @rate_control: Encoding rate control. See SND_RATECONTROLMODE defines.
 *               Encoders may rely on profiles for quality levels.
 *		 May be ignored by decoders.
 * @profile: Mandatory for encoders, can be mandatory for specific
 *		decoders as well. See SND_AUDIOPROFILE defines.
 * @level: Supported level (Only used by WMA at the moment)
 * @ch_mode: Channel mode for encoder. See SND_AUDIOCHANMODE defines
 * @format: Format of encoded bistream. Mandatory when defined.
 *		See SND_AUDIOSTREAMFORMAT defines.
 * @align: Block alignment in bytes of an audio sample.
 *		Only required for PCM or IEC formats.
 * @options: encoder-specific settings
 * @reserved: reserved for future use
 */

struct snd_codec {
	__u32 id;
	__u32 ch_in;
	__u32 ch_out;
	__u32 sample_rate;
	__u32 bit_rate;
	__u32 rate_control;
	__u32 profile;
	__u32 level;
	__u32 ch_mode;
	__u32 format;
	__u32 align;
	__u32 transcode_dts;
	struct snd_dec_dts dts;
	union snd_codec_options options;
	__u32 reserved[3];
};



#define SNDRV_COMPRESS_VERSION SNDRV_PROTOCOL_VERSION(0, 1, 0)
/**
 * struct snd_compressed_buffer: compressed buffer
 * @fragment_size: size of buffer fragment in bytes
 * @fragments: number of such fragments
 */
struct snd_compressed_buffer {
	__u32 fragment_size;
	__u32 fragments;
};

/**
 * struct snd_compr_params: compressed stream params
 * @buffer: buffer description
 * @codec: codec parameters
 * @no_wake_mode: dont wake on fragment elapsed
 */
struct snd_compr_params {
	struct snd_compressed_buffer buffer;
	struct snd_codec codec;
	__u8 no_wake_mode;
};

/**
 * struct snd_compr_tstamp: timestamp descriptor
 * @byte_offset: Byte offset in ring buffer to DSP
 * @copied_total: Total number of bytes copied from/to ring buffer to/by DSP
 * @pcm_frames: Frames decoded or encoded by DSP. This field will evolve by
 *	large steps and should only be used to monitor encoding/decoding
 *	progress. It shall not be used for timing estimates.
 * @pcm_io_frames: Frames rendered or received by DSP into a mixer or an audio
 * output/input. This field should be used for A/V sync or time estimates.
 * @sampling_rate: sampling rate of audio
 */
struct snd_compr_tstamp {
	__u32 byte_offset;
	__u32 copied_total;
	snd_pcm_uframes_t pcm_frames;
	snd_pcm_uframes_t pcm_io_frames;
	__u32 sampling_rate;
	uint64_t timestamp;
};

/**
 * struct snd_compr_avail: avail descriptor
 * @avail: Number of bytes available in ring buffer for writing/reading
 * @tstamp: timestamp infomation
 */
struct snd_compr_avail {
	__u64 avail;
	struct snd_compr_tstamp tstamp;
};

enum snd_compr_direction {
	SND_COMPRESS_PLAYBACK = 0,
	SND_COMPRESS_CAPTURE
};

/**
 * struct snd_compr_caps: caps descriptor
 * @codecs: pointer to array of codecs
 * @direction: direction supported. Of type snd_compr_direction
 * @min_fragment_size: minimum fragment supported by DSP
 * @max_fragment_size: maximum fragment supported by DSP
 * @min_fragments: min fragments supported by DSP
 * @max_fragments: max fragments supported by DSP
 * @num_codecs: number of codecs supported
 * @reserved: reserved field
 */
struct snd_compr_caps {
	__u32 num_codecs;
	__u32 direction;
	__u32 min_fragment_size;
	__u32 max_fragment_size;
	__u32 min_fragments;
	__u32 max_fragments;
	__u32 codecs[MAX_NUM_CODECS];
	__u32 reserved[11];
};

/**
 * struct snd_compr_codec_caps: query capability of codec
 * @codec: codec for which capability is queried
 * @num_descriptors: number of codec descriptors
 * @descriptor: array of codec capability descriptor
 */
struct snd_compr_codec_caps {
	__u32 codec;
	__u32 num_descriptors;
	struct snd_codec_desc descriptor[MAX_NUM_CODEC_DESCRIPTORS];
};

/**
 * struct snd_compr_audio_info: compressed input audio information
 * @frame_size: legth of the encoded frame with valid data
 * @reserved: reserved for furture use
 */
struct snd_compr_audio_info {
	uint32_t frame_size;
	uint32_t reserved[15];
};

/**
 * compress path ioctl definitions
 * SNDRV_COMPRESS_GET_CAPS: Query capability of DSP
 * SNDRV_COMPRESS_GET_CODEC_CAPS: Query capability of a codec
 * SNDRV_COMPRESS_SET_PARAMS: Set codec and stream parameters
 * Note: only codec params can be changed runtime and stream params cant be
 * SNDRV_COMPRESS_GET_PARAMS: Query codec params
 * SNDRV_COMPRESS_TSTAMP: get the current timestamp value
 * SNDRV_COMPRESS_AVAIL: get the current buffer avail value.
 * This also queries the tstamp properties
 * SNDRV_COMPRESS_PAUSE: Pause the running stream
 * SNDRV_COMPRESS_RESUME: resume a paused stream
 * SNDRV_COMPRESS_START: Start a stream
 * SNDRV_COMPRESS_STOP: stop a running stream, discarding ring buffer content
 * and the buffers currently with DSP
 * SNDRV_COMPRESS_DRAIN: Play till end of buffers and stop after that
 * SNDRV_COMPRESS_IOCTL_VERSION: Query the API version
 */
#define SNDRV_COMPRESS_GET_CAPS         _IOWR('C', 0x00, struct snd_compr_caps *)
#define SNDRV_COMPRESS_GET_CODEC_CAPS   _IOWR('C', 0x01, struct snd_compr_codec_caps *)
#define SNDRV_COMPRESS_SET_PARAMS       _IOW('C', 0x02, struct snd_compr_params *)
#define SNDRV_COMPRESS_GET_PARAMS       _IOR('C', 0x03, struct snd_compr_params *)
#define SNDRV_COMPRESS_TSTAMP           _IOR('C', 0x10, struct snd_compr_tstamp *)
#define SNDRV_COMPRESS_AVAIL            _IOR('C', 0x11, struct snd_compr_avail *)
#define SNDRV_COMPRESS_PAUSE            _IO('C', 0x20)
#define SNDRV_COMPRESS_RESUME           _IO('C', 0x21)
#define SNDRV_COMPRESS_START            _IO('C', 0x22)
#define SNDRV_COMPRESS_STOP             _IO('C', 0x23)
#define SNDRV_COMPRESS_DRAIN            _IO('C', 0x24)
/*
 * TODO
 * 1. add mmap support
 *
 */
#define SND_COMPR_TRIGGER_DRAIN 7 /*FIXME move this to pcm.h */

#endif

#endif
