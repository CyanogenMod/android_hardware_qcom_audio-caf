/* AudioHardwareALSA.cpp
 **
 ** Copyright 2008-2010 Wind River Systems
 ** Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
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

#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <math.h>

#define LOG_TAG "AudioHardwareALSA"
//#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0
#include <utils/Log.h>
#include <utils/String8.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <cutils/properties.h>
#include <media/AudioRecord.h>
#include <hardware_legacy/power.h>
#include <pthread.h>

#include "AudioHardwareALSA.h"
#ifdef QCOM_USBAUDIO_ENABLED
#include "AudioUsbALSA.h"
#endif
#include "AudioUtil.h"

//#define OUTPUT_BUFFER_LOG
#ifdef OUTPUT_BUFFER_LOG
    FILE *outputBufferFile1;
    char outputfilename [50] = "/data/output_proxy";
    static int number = 0;
#endif

extern "C"
{
    //
    // Function for dlsym() to look up for creating a new AudioHardwareInterface.
    //
    android_audio_legacy::AudioHardwareInterface *createAudioHardware(void) {
        return android_audio_legacy::AudioHardwareALSA::create();
    }
#ifdef QCOM_ACDB_ENABLED
    static int (*acdb_init)();
    static void (*acdb_deallocate)();
#endif
#ifdef QCOM_CSDCLIENT_ENABLED
    static int (*csd_client_init)();
    static int (*csd_client_deinit)();
    static int (*csd_start_playback)();
    static int (*csd_stop_playback)();
#endif
}         // extern "C"

namespace android_audio_legacy
{

// ----------------------------------------------------------------------------

AudioHardwareInterface *AudioHardwareALSA::create() {
    return new AudioHardwareALSA();
}

AudioHardwareALSA::AudioHardwareALSA() :
    mALSADevice(0),mVoipStreamCount(0),mVoipMicMute(false),mVoipBitRate(0)
    ,mCallState(0),mAcdbHandle(NULL),mCsdHandle(NULL),mMicMute(0)
{
    FILE *fp;
    char soundCardInfo[200];
    char platform[128], baseband[128], audio_init[128], platformVer[128];
    int codec_rev = 2, verNum = 0;
    mALSADevice = new ALSADevice();
    mDeviceList.clear();
    mCSCallActive = 0;
    mVolteCallActive = 0;
    mSGLTECallActive = 0;
    mIsFmActive = 0;
    mDevSettingsFlag = 0;
    bool audio_init_done = false;
    int sleep_retry = 0;
#ifdef QCOM_USBAUDIO_ENABLED
    mAudioUsbALSA = new AudioUsbALSA();
    musbPlaybackState = 0;
    musbRecordingState = 0;
#endif
#ifdef USES_FLUENCE_INCALL
    mDevSettingsFlag |= TTY_OFF | DMIC_FLAG;
#else
    mDevSettingsFlag |= TTY_OFF;
#endif
    mBluetoothVGS = false;
    mFusion3Platform = false;

#ifdef QCOM_ACDB_ENABLED
    mAcdbHandle = ::dlopen("/system/lib/libacdbloader.so", RTLD_NOW);
    if (mAcdbHandle == NULL) {
        ALOGE("AudioHardware: DLOPEN not successful for ACDBLOADER");
    } else {
        ALOGD("AudioHardware: DLOPEN successful for ACDBLOADER");
        acdb_init = (int (*)())::dlsym(mAcdbHandle,"acdb_loader_init_ACDB");
        if (acdb_init == NULL) {
            ALOGE("dlsym:Error:%s Loading acdb_loader_init_ACDB", dlerror());
        }else {
           acdb_init();
           acdb_deallocate = (void (*)())::dlsym(mAcdbHandle,"acdb_loader_deallocate_ACDB");
        }
    }
    mALSADevice->setACDBHandle(mAcdbHandle);
#endif

#ifdef QCOM_CSDCLIENT_ENABLED
             mCsdHandle = ::dlopen("/system/lib/libcsd-client.so", RTLD_NOW);
             if (mCsdHandle == NULL) {
                 ALOGE("AudioHardware: DLOPEN not successful for CSD CLIENT");
             } else {
                 ALOGD("AudioHardware: DLOPEN successful for CSD CLIENT");
                 csd_client_init = (int (*)())::dlsym(mCsdHandle,"csd_client_init");
                 csd_client_deinit = (int (*)())::dlsym(mCsdHandle,"csd_client_deinit");
                 csd_start_playback = (int (*)())::dlsym(mCsdHandle,"csd_client_start_playback");
                 csd_stop_playback = (int (*)())::dlsym(mCsdHandle,"csd_client_stop_playback");

                 if (csd_client_init == NULL) {
                     ALOGE("dlsym: Error:%s Loading csd_client_init", dlerror());
                 } else {
                     csd_client_init();
                 }

             }
             mALSADevice->setCsdHandle(mCsdHandle);
#endif
    if((fp = fopen("/proc/asound/cards","r")) == NULL) {
        ALOGE("Cannot open /proc/asound/cards file to get sound card info");
    } else {
        while((fgets(soundCardInfo, sizeof(soundCardInfo), fp) != NULL)) {
            ALOGV("SoundCardInfo %s", soundCardInfo);
            if (strstr(soundCardInfo, "msm8960-tabla1x-snd-card")) {
                codec_rev = 1;
                break;
            } else if (strstr(soundCardInfo, "msm-snd-card")) {
                codec_rev = 2;
                break;
            } else if (strstr(soundCardInfo, "msm8930-sitar-snd-card")) {
                codec_rev = 3;
                break;
            } else if (strstr(soundCardInfo, "msm8974-taiko-mtp-snd-card")) {
                codec_rev = 40;
                break;
            } else if (strstr(soundCardInfo, "msm8974-taiko-cdp-snd-card")) {
                codec_rev = 41;
                break;
            } else if (strstr(soundCardInfo, "msm8974-taiko-fluid-snd-card")) {
                codec_rev = 42;
                break;
            } else if (strstr(soundCardInfo, "msm8974-taiko-liquid-snd-card")) {
                codec_rev = 43;
                break;
            }
        }
        fclose(fp);
    }

#if 0
    while (audio_init_done == false && sleep_retry < MAX_SLEEP_RETRY) {
        property_get("qcom.audio.init", audio_init, NULL);
        ALOGD("qcom.audio.init is set to %s\n",audio_init);
        if(!strncmp(audio_init, "complete", sizeof("complete"))) {
            audio_init_done = true;
        } else {
            ALOGD("Sleeping for 50 ms");
            usleep(AUDIO_INIT_SLEEP_WAIT*1000);
            sleep_retry++;
        }
    }
#endif

    if (codec_rev == 1) {
        ALOGV("Detected tabla 1.x sound card");
        snd_use_case_mgr_open(&mUcMgr, "snd_soc_msm");
    } else if (codec_rev == 3) {
        ALOGV("Detected sitar 1.x sound card");
        snd_use_case_mgr_open(&mUcMgr, "snd_soc_msm_Sitar");
    } else if (codec_rev == 40) {
        ALOGV("Detected taiko sound card");
        snd_use_case_mgr_open(&mUcMgr, "snd_soc_msm_Taiko");
    } else if (codec_rev == 41) {
        ALOGV("Detected taiko sound card");
        snd_use_case_mgr_open(&mUcMgr, "snd_soc_msm_Taiko_CDP");
    } else if (codec_rev == 42) {
        ALOGV("Detected taiko sound card");
        snd_use_case_mgr_open(&mUcMgr, "snd_soc_msm_Taiko_Fluid");
    } else if (codec_rev == 43) {
        ALOGV("Detected taiko liquid sound card");
        snd_use_case_mgr_open(&mUcMgr, "snd_soc_msm_Taiko_liquid");
    } else {
        property_get("ro.board.platform", platform, "");
        property_get("ro.baseband", baseband, "");
        if (!strcmp("msm8960", platform) && !strcmp("mdm", baseband)) {
            ALOGV("Detected Fusion tabla 2.x");
            mFusion3Platform = true;
            if((fp = fopen("/sys/devices/system/soc/soc0/platform_version","r")) == NULL) {
                ALOGE("Cannot open /sys/devices/system/soc/soc0/platform_version file");

                snd_use_case_mgr_open(&mUcMgr, "snd_soc_msm_2x_Fusion3");
            } else {
                while((fgets(platformVer, sizeof(platformVer), fp) != NULL)) {
                    ALOGV("platformVer %s", platformVer);

                    verNum = atoi(platformVer);
                    if (verNum == 0x10001) {
                        snd_use_case_mgr_open(&mUcMgr, "snd_soc_msm_I2SFusion");
                        break;
                    } else {
                        snd_use_case_mgr_open(&mUcMgr, "snd_soc_msm_2x_Fusion3");
                        break;
                    }
                }
            }
            fclose(fp);
        } else {
            ALOGV("Detected tabla 2.x sound card");
            snd_use_case_mgr_open(&mUcMgr, "snd_soc_msm_2x");
        }
    }

    if (mUcMgr < 0) {
        ALOGE("Failed to open ucm instance: %d", errno);
    } else {
        ALOGI("ucm instance opened: %u", (unsigned)mUcMgr);
        mUcMgr->acdb_handle = NULL;
#ifdef QCOM_ACDB_ENABLED
        if (mAcdbHandle) {
            mUcMgr->acdb_handle = static_cast<void*> (mAcdbHandle);
        }
#endif
        mUcMgr->isFusion3Platform = mFusion3Platform;
    }

    //set default AudioParameters
    AudioParameter param;
    String8 key;
    String8 value;

    //Set default AudioParameter for fluencetype
    key  = String8(FLUENCE_KEY);
    char fluence_key[20] = "none";
    property_get("ro.qc.sdk.audio.fluencetype",fluence_key,"0");
    if (0 == strncmp("fluencepro", fluence_key, sizeof("fluencepro"))) {
        mDevSettingsFlag |= QMIC_FLAG;
        mDevSettingsFlag &= (~DMIC_FLAG);
        value = String8("fluencepro");
        ALOGD("FluencePro quadMic feature Enabled");
    } else if (0 == strncmp("fluence", fluence_key, sizeof("fluence"))) {
        mDevSettingsFlag |= DMIC_FLAG;
        mDevSettingsFlag &= (~QMIC_FLAG);
        value = String8("fluence");
        ALOGD("Fluence dualmic feature Enabled");
    } else if (0 == strncmp("none", fluence_key, sizeof("none"))) {
        mDevSettingsFlag &= (~DMIC_FLAG);
        mDevSettingsFlag &= (~QMIC_FLAG);
        value = String8("none");
        ALOGD("Fluence feature Disabled");
    }
    param.add(key, value);
    mALSADevice->setFlags(mDevSettingsFlag);

    //mALSADevice->setDeviceList(&mDeviceList);
    mRouteAudioToExtOut = false;
    mA2dpDevice = NULL;
    mA2dpStream = NULL;
    mUsbDevice = NULL;
    mUsbStream = NULL;
    mExtOutStream = NULL;
    mExtOutActiveUseCases = USECASE_NONE;
    mActiveExtOut = 0;
    mIsExtOutEnabled = false;
    mKillExtOutThread = false;
    mExtOutThreadAlive = false;
    mExtOutThread = NULL;
}

AudioHardwareALSA::~AudioHardwareALSA()
{
    if (mUcMgr != NULL) {
        ALOGV("closing ucm instance: %u", (unsigned)mUcMgr);
        snd_use_case_mgr_close(mUcMgr);
    }
    if (mALSADevice) {
        delete mALSADevice;
    }
    for(ALSAHandleList::iterator it = mDeviceList.begin();
            it != mDeviceList.end(); ++it) {
        it->useCase[0] = 0;
        mDeviceList.erase(it);
    }
#ifdef QCOM_ACDB_ENABLED
     if (acdb_deallocate == NULL) {
        ALOGE("dlsym: Error:%s Loading acdb_deallocate_ACDB", dlerror());
     } else {
        acdb_deallocate();
     }
     if (mAcdbHandle) {
        ::dlclose(mAcdbHandle);
        mAcdbHandle = NULL;
     }
#endif
#ifdef QCOM_USBAUDIO_ENABLED
    delete mAudioUsbALSA;
#endif

#ifdef QCOM_CSDCLEINT_ENABLED
     if (mCsdHandle) {
        if (csd_client_deinit == NULL) {
            ALOGE("dlsym: Error:%s Loading csd_client_deinit", dlerror());
        } else {
            csd_client_deinit();
        }
        ::dlclose(mCsdHandle);
        mCsdHandle = NULL;
     }
#endif
}

status_t AudioHardwareALSA::initCheck()
{
    if (!mALSADevice)
        return NO_INIT;

    return NO_ERROR;
}

status_t AudioHardwareALSA::setVoiceVolume(float v)
{
    ALOGV("setVoiceVolume(%f)\n", v);
    if (v < 0.0) {
        ALOGW("setVoiceVolume(%f) under 0.0, assuming 0.0\n", v);
        v = 0.0;
    } else if (v > 1.0) {
        ALOGW("setVoiceVolume(%f) over 1.0, assuming 1.0\n", v);
        v = 1.0;
    }

    int newMode = mode();
    ALOGV("setVoiceVolume  newMode %d",newMode);
    int vol = lrint(v * 100.0);

    // Voice volume levels from android are mapped to driver volume levels as follows.
    // 0 -> 5, 20 -> 4, 40 ->3, 60 -> 2, 80 -> 1, 100 -> 0
    // So adjust the volume to get the correct volume index in driver
    vol = 100 - vol;

    if (mALSADevice) {
        if(newMode == AUDIO_MODE_IN_COMMUNICATION) {
            mALSADevice->setVoipVolume(vol);
        } else if (newMode == AUDIO_MODE_IN_CALL){
               if (mCSCallActive == CS_ACTIVE)
                   mALSADevice->setVoiceVolume(vol);
               else if (mSGLTECallActive == CS_ACTIVE_SESSION2)
                   mALSADevice->setSGLTEVolume(vol);
               if (mVolteCallActive == IMS_ACTIVE)
                   mALSADevice->setVoLTEVolume(vol);
        }
    }

    return NO_ERROR;
}

#ifdef QCOM_FM_ENABLED
status_t  AudioHardwareALSA::setFmVolume(float value)
{
    Mutex::Autolock autoLock(mLock);
    status_t status = NO_ERROR;

    int vol;

    if (value < 0.0) {
        ALOGW("setFmVolume(%f) under 0.0, assuming 0.0\n", value);
        value = 0.0;
    } else if (value > 1.0) {
        ALOGW("setFmVolume(%f) over 1.0, assuming 1.0\n", value);
        value = 1.0;
    }
    vol  = lrint((value * 0x2000) + 0.5);

    ALOGV("setFmVolume(%f)\n", value);
    ALOGV("Setting FM volume to %d (available range is 0 to 0x2000)\n", vol);

    mALSADevice->setFmVolume(vol);

    return status;
}
#endif

status_t AudioHardwareALSA::setMasterVolume(float volume)
{
    return NO_ERROR;
}

status_t AudioHardwareALSA::setMode(int mode)
{
    status_t status = NO_ERROR;

    if (mode != mMode) {
        status = AudioHardwareBase::setMode(mode);
    }
    if (mode == AUDIO_MODE_IN_CALL) {
        if (mCallState == CS_INACTIVE)
            mCallState = CS_ACTIVE;
    }else if (mode == AUDIO_MODE_NORMAL) {
        mCallState = 0;
    }

    return status;
}

status_t AudioHardwareALSA::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key;
    String8 value;
    status_t status = NO_ERROR;
    int device;
    int btRate;
    int state;
    ALOGV("setParameters() %s", keyValuePairs.string());

    key = String8(TTY_MODE_KEY);
    if (param.get(key, value) == NO_ERROR) {
        mDevSettingsFlag &= TTY_CLEAR;
        if (value == "full" || value == "tty_full") {
            mDevSettingsFlag |= TTY_FULL;
        } else if (value == "hco" || value == "tty_hco") {
            mDevSettingsFlag |= TTY_HCO;
        } else if (value == "vco" || value == "tty_vco") {
            mDevSettingsFlag |= TTY_VCO;
        } else {
            mDevSettingsFlag |= TTY_OFF;
        }
        ALOGI("Changed TTY Mode=%s", value.string());
        mALSADevice->setFlags(mDevSettingsFlag);
        if(mMode != AUDIO_MODE_IN_CALL){
           return NO_ERROR;
        }
        doRouting(0);
    }

    key = String8(FLUENCE_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "quadmic") {
            mDevSettingsFlag |= QMIC_FLAG;
            mDevSettingsFlag &= (~DMIC_FLAG);
            ALOGV("Fluence quadMic feature Enabled");
        } else if (value == "dualmic") {
            mDevSettingsFlag |= DMIC_FLAG;
            mDevSettingsFlag &= (~QMIC_FLAG);
            ALOGV("Fluence dualmic feature Enabled");
        } else if (value == "none") {
            mDevSettingsFlag &= (~DMIC_FLAG);
            mDevSettingsFlag &= (~QMIC_FLAG);
            ALOGV("Fluence feature Disabled");
        }
        mALSADevice->setFlags(mDevSettingsFlag);
        doRouting(0);
    }

#ifdef QCOM_CSDCLIENT_ENABLED
    if (mFusion3Platform) {
        key = String8(INCALLMUSIC_KEY);
        if (param.get(key, value) == NO_ERROR) {
            if (value == "true") {
                ALOGV("Enabling Incall Music setting in the setparameter\n");
                if (csd_start_playback == NULL) {
                    ALOGE("dlsym: Error:%s Loading csd_client_start_playback", dlerror());
                } else {
                    csd_start_playback();
                }
            } else {
                ALOGV("Disabling Incall Music setting in the setparameter\n");
                if (csd_stop_playback == NULL) {
                    ALOGE("dlsym: Error:%s Loading csd_client_stop_playback", dlerror());
                } else {
                    csd_stop_playback();
                }
            }
        }
    }
#endif

    key = String8(ANC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "true") {
            ALOGV("Enabling ANC setting in the setparameter\n");
            mDevSettingsFlag |= ANC_FLAG;
        } else {
            ALOGV("Disabling ANC setting in the setparameter\n");
            mDevSettingsFlag &= (~ANC_FLAG);
        }
        mALSADevice->setFlags(mDevSettingsFlag);
        doRouting(0);
    }

    key = String8(AudioParameter::keyRouting);
    if (param.getInt(key, device) == NO_ERROR) {
        // Ignore routing if device is 0.
        if(device) {
            doRouting(device);
        }
        param.remove(key);
    }

    key = String8(BT_SAMPLERATE_KEY);
    if (param.getInt(key, btRate) == NO_ERROR) {
        mALSADevice->setBtscoRate(btRate);
        param.remove(key);
    }

    key = String8(BTHEADSET_VGS);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "on") {
            mBluetoothVGS = true;
        } else {
            mBluetoothVGS = false;
        }
    }

    key = String8(WIDEVOICE_KEY);
    if (param.get(key, value) == NO_ERROR) {
        bool flag = false;
        if (value == "true") {
            flag = true;
        }
        if(mALSADevice) {
            mALSADevice->enableWideVoice(flag);
        }
        param.remove(key);
    }

    key = String8("a2dp_connected");
    if (param.get(key, value) == NO_ERROR) {
        if (value == "true") {
            status_t err = openExtOutput(AudioSystem::DEVICE_OUT_ALL_A2DP);
        } else {
            status_t err = closeExtOutput(AudioSystem::DEVICE_OUT_ALL_A2DP);
        }
        param.remove(key);
    }

    key = String8("A2dpSuspended");
    if (param.get(key, value) == NO_ERROR) {
        if (mA2dpDevice != NULL) {
            mA2dpDevice->set_parameters(mA2dpDevice,keyValuePairs);
        }
        param.remove(key);
    }

    key = String8("a2dp_sink_address");
    if (param.get(key, value) == NO_ERROR) {
        if (mA2dpStream != NULL) {
            mA2dpStream->common.set_parameters(&mA2dpStream->common,keyValuePairs);
        }
        param.remove(key);
    }

    key = String8("usb_connected");
    if (param.get(key, value) == NO_ERROR) {
        if (value == "true") {
            status_t err = openExtOutput(AUDIO_DEVICE_OUT_ALL_USB);
        } else {
            status_t err = closeExtOutput(AUDIO_DEVICE_OUT_ALL_USB);
        }
        param.remove(key);
    }

    key = String8("card");
    if (param.get(key, value) == NO_ERROR) {
        if (mUsbStream != NULL) {
            ALOGV("mUsbStream->common.set_parameters");
            mUsbStream->common.set_parameters(&mUsbStream->common,keyValuePairs);
        }
        param.remove(key);
    }

    key = String8(VOIPRATE_KEY);
    if (param.get(key, value) == NO_ERROR) {
            mVoipBitRate = atoi(value);
        param.remove(key);
    }

    key = String8(FENS_KEY);
    if (param.get(key, value) == NO_ERROR) {
        bool flag = false;
        if (value == "true") {
            flag = true;
        }
        if(mALSADevice) {
            mALSADevice->enableFENS(flag);
        }
        param.remove(key);
    }

#ifdef QCOM_FM_ENABLED
    key = String8(AudioParameter::keyHandleFm);
    if (param.getInt(key, device) == NO_ERROR) {
        // Ignore if device is 0
        if(device) {
            handleFm(device);
        }
        param.remove(key);
    }
#endif

    key = String8(ST_KEY);
    if (param.get(key, value) == NO_ERROR) {
        bool flag = false;
        if (value == "true") {
            flag = true;
        }
        if(mALSADevice) {
            mALSADevice->enableSlowTalk(flag);
        }
        param.remove(key);
    }
    key = String8(MODE_CALL_KEY);
    if (param.getInt(key,state) == NO_ERROR) {
        if (mCallState != state) {
            mCallState = state;
            doRouting(0);
        }
        mCallState = state;
    }
    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardwareALSA::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    int device;

    String8 key = String8(DUALMIC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        value = String8("false");
        param.add(key, value);
    }

    key = String8(FLUENCE_KEY);
    if (param.get(key, value) == NO_ERROR) {
    if ((mDevSettingsFlag & QMIC_FLAG) &&
                               (mDevSettingsFlag & ~DMIC_FLAG))
            value = String8("quadmic");
    else if ((mDevSettingsFlag & DMIC_FLAG) &&
                                (mDevSettingsFlag & ~QMIC_FLAG))
            value = String8("dualmic");
    else if ((mDevSettingsFlag & ~DMIC_FLAG) &&
                                (mDevSettingsFlag & ~QMIC_FLAG))
            value = String8("none");
        param.add(key, value);
    }

#ifdef QCOM_FM_ENABLED

    key = String8(AudioParameter::keyHandleA2dpDevice);
    if ( param.get(key,value) == NO_ERROR ) {
        param.add(key, String8("true"));
    }

    key = String8("Fm-radio");
    if ( param.get(key,value) == NO_ERROR ) {
        if ( mIsFmActive ) {
            param.addInt(String8("isFMON"), true );
        }
    }
#endif

    key = String8(BTHEADSET_VGS);
    if (param.get(key, value) == NO_ERROR) {
        if(mBluetoothVGS)
           param.addInt(String8("isVGS"), true);
    }
#ifdef QCOM_SSR_ENABLED
    key = String8(AudioParameter::keySSR);
    if (param.get(key, value) == NO_ERROR) {
        char ssr_enabled[6] = "false";
        property_get("ro.qc.sdk.audio.ssr",ssr_enabled,"0");
        if (!strncmp("true", ssr_enabled, 4)) {
            value = String8("true");
        }
        param.add(key, value);
    }
#endif

    key = String8("A2dpSuspended");
    if (param.get(key, value) == NO_ERROR) {
        if (mA2dpDevice != NULL) {
            value = mA2dpDevice->get_parameters(mA2dpDevice,key);
        }
        param.add(key, value);
    }

    key = String8("a2dp_sink_address");
    if (param.get(key, value) == NO_ERROR) {
        if (mA2dpStream != NULL) {
            value = mA2dpStream->common.get_parameters(&mA2dpStream->common,key);
        }
        param.add(key, value);
    }

    key = String8("tunneled-input-formats");
    if ( param.get(key,value) == NO_ERROR ) {
        ALOGD("Add tunnel AWB to audio parameter");
        param.addInt(String8("AWB"), true );
    }

    key = String8(AudioParameter::keyRouting);
    if (param.getInt(key, device) == NO_ERROR) {
        param.addInt(key, mCurDevice);
    }

    ALOGV("AudioHardwareALSA::getParameters() %s", param.toString().string());
    return param.toString();
}

#ifdef QCOM_USBAUDIO_ENABLED
void AudioHardwareALSA::closeUSBPlayback()
{
    ALOGV("closeUSBPlayback, musbPlaybackState: %d", musbPlaybackState);
    musbPlaybackState = 0;
    mAudioUsbALSA->exitPlaybackThread(SIGNAL_EVENT_KILLTHREAD);
}

void AudioHardwareALSA::closeUSBRecording()
{
    ALOGV("closeUSBRecording");
    musbRecordingState = 0;
    mAudioUsbALSA->exitRecordingThread(SIGNAL_EVENT_KILLTHREAD);
}

void AudioHardwareALSA::closeUsbPlaybackIfNothingActive(){
    ALOGV("closeUsbPlaybackIfNothingActive, musbPlaybackState: %d", musbPlaybackState);
    if(!musbPlaybackState && mAudioUsbALSA != NULL) {
        mAudioUsbALSA->exitPlaybackThread(SIGNAL_EVENT_TIMEOUT);
    }
}

void AudioHardwareALSA::closeUsbRecordingIfNothingActive(){
    ALOGV("closeUsbRecordingIfNothingActive, musbRecordingState: %d", musbRecordingState);
    if(!musbRecordingState && mAudioUsbALSA != NULL) {
        ALOGD("Closing USB Recording Session as no stream is active");
        mAudioUsbALSA->setkillUsbRecordingThread(true);
    }
}

void AudioHardwareALSA::startUsbPlaybackIfNotStarted(){
    ALOGV("Starting the USB playback %d kill %d", musbPlaybackState,
             mAudioUsbALSA->getkillUsbPlaybackThread());
    if((!musbPlaybackState) || (mAudioUsbALSA->getkillUsbPlaybackThread() == true)) {
        mAudioUsbALSA->startPlayback();
    }
}

void AudioHardwareALSA::startUsbRecordingIfNotStarted(){
    ALOGV("Starting the recording musbRecordingState: %d killUsbRecordingThread %d",
          musbRecordingState, mAudioUsbALSA->getkillUsbRecordingThread());
    if((!musbRecordingState) || (mAudioUsbALSA->getkillUsbRecordingThread() == true)) {
        mAudioUsbALSA->startRecording();
    }
}
#endif

status_t AudioHardwareALSA::doRouting(int device)
{
    Mutex::Autolock autoLock(mLock);
    int newMode = mode();
    bool isRouted = false;

    if ((device == AudioSystem::DEVICE_IN_VOICE_CALL)
#ifdef QCOM_FM_ENABLED
        || (device == AudioSystem::DEVICE_IN_FM_RX)
        || (device == AudioSystem::DEVICE_IN_FM_RX_A2DP)
#endif
        || (device == AudioSystem::DEVICE_IN_COMMUNICATION)
        ) {
        ALOGV("Ignoring routing for FM/INCALL/VOIP recording");
        return NO_ERROR;
    }
    ALOGV("device = 0x%x,mCurDevice 0x%x", device, mCurDevice);
    if (device == 0)
        device = mCurDevice;
    ALOGV("doRouting: device %#x newMode %d mCSCallActive %d mVolteCallActive %d"
          "mSGLTECallActive %d mIsFmActive %d", device, newMode, mCSCallActive,
          mVolteCallActive, mSGLTECallActive, mIsFmActive);
    isRouted = routeVoLTECall(device, newMode);
    isRouted |= routeVoiceCall(device, newMode);
    isRouted |= routeSGLTECall(device, newMode);

    if(!isRouted) {
#ifdef QCOM_USBAUDIO_ENABLED
        if(!(device & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET) &&
            !(device & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET) &&
            !(device & AudioSystem::DEVICE_IN_ANLG_DOCK_HEADSET) &&
             (musbPlaybackState || musbRecordingState)){
                // mExtOutStream should be initialized before calling route
                // when switching form USB headset to ExtOut device
                if( mRouteAudioToExtOut == true ) {
                    switchExtOut(device);
                }
                ALSAHandleList::iterator it = mDeviceList.end();
                it--;
                mALSADevice->route(&(*it), (uint32_t)device, newMode);
                ALOGD("USB UNPLUGGED, setting musbPlaybackState to 0");
                closeUSBRecording();
                closeUSBPlayback();
        } else if((device & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET)||
                  (device & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET)){
                    ALOGD("Routing everything to prox now");
                    // stopPlaybackOnExtOut should be called to close
                    // ExtOutthread when switching from ExtOut device to USB headset
                    if( mRouteAudioToExtOut == true ) {
                        uint32_t activeUsecase = getExtOutActiveUseCases_l();
                        status_t err = stopPlaybackOnExtOut_l(activeUsecase);
                        if(err) {
                            ALOGW("stopPlaybackOnExtOut_l failed = %d", err);
                            return err;
                        }
                    }
                    ALSAHandleList::iterator it = mDeviceList.end();
                    it--;
                    if (device != mCurDevice) {
                        if(musbPlaybackState)
                            closeUSBPlayback();
                    }
                    mALSADevice->route(&(*it), device, newMode);
                    for(it = mDeviceList.begin(); it != mDeviceList.end(); ++it) {
                         if((!strcmp(it->useCase, SND_USE_CASE_VERB_HIFI_LOW_POWER)) ||
                            (!strcmp(it->useCase, SND_USE_CASE_MOD_PLAY_LPA))) {
                                 ALOGV("doRouting: LPA device switch to proxy");
                                 startUsbPlaybackIfNotStarted();
                                 musbPlaybackState |= USBPLAYBACKBIT_LPA;
                                 break;
                         } else if((!strcmp(it->useCase, SND_USE_CASE_VERB_HIFI_TUNNEL)) ||
                                   (!strcmp(it->useCase, SND_USE_CASE_MOD_PLAY_TUNNEL))) {
                                    ALOGD("doRouting: Tunnel Player device switch to proxy");
                                    startUsbPlaybackIfNotStarted();
                                    musbPlaybackState |= USBPLAYBACKBIT_TUNNEL;
                                    break;
                         } else if((!strcmp(it->useCase, SND_USE_CASE_VERB_VOICECALL)) ||
                                   (!strcmp(it->useCase, SND_USE_CASE_MOD_PLAY_VOICE))) {
                                    ALOGV("doRouting: VOICE device switch to proxy");
                                    startUsbRecordingIfNotStarted();
                                    startUsbPlaybackIfNotStarted();
                                    musbPlaybackState |= USBPLAYBACKBIT_VOICECALL;
                                    musbRecordingState |= USBPLAYBACKBIT_VOICECALL;
                                    break;
                        }else if((!strcmp(it->useCase, SND_USE_CASE_VERB_DIGITAL_RADIO)) ||
                                 (!strcmp(it->useCase, SND_USE_CASE_MOD_PLAY_FM))) {
                                    ALOGV("doRouting: FM device switch to proxy");
                                    startUsbPlaybackIfNotStarted();
                                    musbPlaybackState |= USBPLAYBACKBIT_FM;
                                    break;
                         }
                    }
        } else
#endif
        if ((isExtOutDevice(device)) && mRouteAudioToExtOut == true)  {
            ALOGV(" External Output Enabled - Routing everything to proxy now");
            switchExtOut(device);
            ALSAHandleList::iterator it = mDeviceList.end();
            it--;
            status_t err = NO_ERROR;
            uint32_t activeUsecase = useCaseStringToEnum(it->useCase);
            if (!((device & AudioSystem::DEVICE_OUT_ALL_A2DP) &&
                  (mCurRxDevice & AUDIO_DEVICE_OUT_ALL_USB))) {
                if ((activeUsecase == USECASE_HIFI_LOW_POWER) ||
                    (activeUsecase == USECASE_HIFI_TUNNEL)) {
                    if (device != mCurRxDevice) {
                        if((isExtOutDevice(mCurRxDevice)) &&
                           (isExtOutDevice(device))) {
                            activeUsecase = getExtOutActiveUseCases_l();
                            stopPlaybackOnExtOut_l(activeUsecase);
                            mRouteAudioToExtOut = true;
                        }
                        mALSADevice->route(&(*it),(uint32_t)device, newMode);
                    }
                    err = startPlaybackOnExtOut_l(activeUsecase);
                } else {
                    //WHY NO check for prev device here?
                    if (device != mCurRxDevice) {
                        if((isExtOutDevice(mCurRxDevice)) &&
                            (isExtOutDevice(device))) {
                            activeUsecase = getExtOutActiveUseCases_l();
                            stopPlaybackOnExtOut_l(activeUsecase);
                            mALSADevice->route(&(*it),(uint32_t)device, newMode);
                            mRouteAudioToExtOut = true;
                            startPlaybackOnExtOut_l(activeUsecase);
                        } else {
                           mALSADevice->route(&(*it),(uint32_t)device, newMode);
                        }
                    }
                    if (activeUsecase == USECASE_FM){
                        err = startPlaybackOnExtOut_l(activeUsecase);
                    }
                }
                if(err) {
                    ALOGW("startPlaybackOnExtOut_l for hardware output failed err = %d", err);
                    stopPlaybackOnExtOut_l(activeUsecase);
                    mALSADevice->route(&(*it),(uint32_t)mCurRxDevice, newMode);
                    return err;
                }
            }
        } else if((device & AudioSystem::DEVICE_OUT_ALL) &&
                  (!isExtOutDevice(device)) &&
                   mRouteAudioToExtOut == true ) {
            ALOGV(" ExtOut Disable on hardware output");
            ALSAHandleList::iterator it = mDeviceList.end();
            it--;
            status_t err;
            uint32_t activeUsecase = getExtOutActiveUseCases_l();
            err = stopPlaybackOnExtOut_l(activeUsecase);
            if(err) {
                ALOGW("stopPlaybackOnExtOut_l failed = %d", err);
                return err;
            }
            if (device != mCurDevice) {
                mALSADevice->route(&(*it),(uint32_t)device, newMode);
            }
        } else {
             setInChannels(device);
             ALSAHandleList::iterator it = mDeviceList.end();
             it--;
             mALSADevice->route(&(*it), (uint32_t)device, newMode);
        }
    }
    mCurDevice = device;
    if (device & AudioSystem::DEVICE_OUT_ALL) {
        mCurRxDevice = device;
    }
    return NO_ERROR;
}

void AudioHardwareALSA::setInChannels(int device)
{
     ALSAHandleList::iterator it;

     if (device & AudioSystem::DEVICE_IN_BUILTIN_MIC) {
         for(it = mDeviceList.begin(); it != mDeviceList.end(); ++it) {
             if (!strncmp(it->useCase, SND_USE_CASE_VERB_HIFI_REC,
                 strlen(SND_USE_CASE_VERB_HIFI_REC)) ||
                 !strncmp(it->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC,
                 strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC))) {
                 mALSADevice->setInChannels(it->channels);
                 return;
             }
         }
     }

     mALSADevice->setInChannels(1);
}

uint32_t AudioHardwareALSA::getVoipMode(int format)
{
    switch(format) {
    case AUDIO_FORMAT_PCM_16_BIT:
               return MODE_PCM;
         break;
    case AUDIO_FORMAT_AMR_NB:
               return MODE_AMR;
         break;
    case AUDIO_FORMAT_AMR_WB:
               return MODE_AMR_WB;
         break;

#ifdef QCOM_AUDIO_FORMAT_ENABLED
    case AUDIO_FORMAT_EVRC:
               return MODE_IS127;
         break;

    case AUDIO_FORMAT_EVRCB:
               return MODE_4GV_NB;
         break;
    case AUDIO_FORMAT_EVRCWB:
               return MODE_4GV_WB;
         break;
#endif
    default:
               return MODE_PCM;
    }
}

AudioStreamOut *
AudioHardwareALSA::openOutputStream(uint32_t devices,
                                    int *format,
                                    uint32_t *channels,
                                    uint32_t *sampleRate,
                                    status_t *status)
{
    Mutex::Autolock autoLock(mLock);
    ALOGV("openOutputStream: devices 0x%x channels %d sampleRate %d",
         devices, *channels, *sampleRate);

    audio_output_flags_t flags = static_cast<audio_output_flags_t> (*status);

    status_t err = BAD_VALUE;
#ifdef QCOM_OUTPUT_FLAGS_ENABLED
    if (flags & (AUDIO_OUTPUT_FLAG_LPA | AUDIO_OUTPUT_FLAG_TUNNEL)) {
        int type = !(flags & AUDIO_OUTPUT_FLAG_LPA); //0 for LPA, 1 for tunnel
        AudioSessionOutALSA *out = new AudioSessionOutALSA(this, devices, *format, *channels,
                                                           *sampleRate, type, &err);
        if(err != NO_ERROR) {
            mLock.unlock();
            delete out;
            out = NULL;
            mLock.lock();
        }
        if (status) *status = err;
        return out;
    }
#endif
    AudioStreamOutALSA *out = 0;
    ALSAHandleList::iterator it;

    if (devices & (devices - 1)) {
        if (status) *status = err;
        ALOGE("openOutputStream called with bad devices");
        return out;
    }

    if(isExtOutDevice(devices)) {
        ALOGV("Set Capture from proxy true");
        mRouteAudioToExtOut = true;
    }

#ifdef QCOM_OUTPUT_FLAGS_ENABLED
    if((flags & AUDIO_OUTPUT_FLAG_DIRECT) && (flags & AUDIO_OUTPUT_FLAG_VOIP_RX)&&
       ((*sampleRate == VOIP_SAMPLING_RATE_8K) || (*sampleRate == VOIP_SAMPLING_RATE_16K))) {
        bool voipstream_active = false;
        for(it = mDeviceList.begin();
            it != mDeviceList.end(); ++it) {
                if((!strcmp(it->useCase, SND_USE_CASE_VERB_IP_VOICECALL)) ||
                   (!strcmp(it->useCase, SND_USE_CASE_MOD_PLAY_VOIP))) {
                    ALOGD("openOutput:  it->rxHandle %d it->handle %d",it->rxHandle,it->handle);
                    voipstream_active = true;
                    break;
                }
        }
      if(voipstream_active == false) {
         mVoipStreamCount = 0;
         mVoipMicMute = false;
         alsa_handle_t alsa_handle;
         unsigned long bufferSize;
         if(*sampleRate == VOIP_SAMPLING_RATE_8K) {
             bufferSize = VOIP_BUFFER_SIZE_8K;
         }
         else if(*sampleRate == VOIP_SAMPLING_RATE_16K) {
             bufferSize = VOIP_BUFFER_SIZE_16K;
         }
         else {
             ALOGE("unsupported samplerate %d for voip",*sampleRate);
             if (status) *status = err;
                 return out;
          }
          alsa_handle.module = mALSADevice;
          alsa_handle.bufferSize = bufferSize;
          alsa_handle.devices = devices;
          alsa_handle.handle = 0;
          if(*format == AUDIO_FORMAT_PCM_16_BIT)
              alsa_handle.format = SNDRV_PCM_FORMAT_S16_LE;
          else
              alsa_handle.format = *format;
          alsa_handle.channels = VOIP_DEFAULT_CHANNEL_MODE;
          alsa_handle.sampleRate = *sampleRate;
          alsa_handle.latency = VOIP_PLAYBACK_LATENCY;
          alsa_handle.rxHandle = 0;
          alsa_handle.ucMgr = mUcMgr;
          mALSADevice->setVoipConfig(getVoipMode(*format), mVoipBitRate);
          char *use_case;
          snd_use_case_get(mUcMgr, "_verb", (const char **)&use_case);
          if ((use_case == NULL) || (!strcmp(use_case, SND_USE_CASE_VERB_INACTIVE))) {
              strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_IP_VOICECALL, sizeof(alsa_handle.useCase));
          } else {
              strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_PLAY_VOIP, sizeof(alsa_handle.useCase));
          }
          free(use_case);
          mDeviceList.push_back(alsa_handle);
          it = mDeviceList.end();
          it--;
          ALOGV("openoutput: mALSADevice->route useCase %s mCurDevice %d mVoipStreamCount %d mode %d", it->useCase,mCurDevice,mVoipStreamCount, mode());
          if((mCurDevice & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET)||
             (mCurDevice & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET)||
             (mCurDevice & AudioSystem::DEVICE_OUT_PROXY)){
              ALOGD("Routing to proxy for normal voip call in openOutputStream");
              mALSADevice->route(&(*it), mCurDevice, AUDIO_MODE_IN_COMMUNICATION);
#ifdef QCOM_USBAUDIO_ENABLED
                ALOGD("enabling VOIP in openoutputstream, musbPlaybackState: %d", musbPlaybackState);
              startUsbPlaybackIfNotStarted();
              musbPlaybackState |= USBPLAYBACKBIT_VOIPCALL;
              ALOGD("Starting recording in openoutputstream, musbRecordingState: %d", musbRecordingState);
              startUsbRecordingIfNotStarted();
              musbRecordingState |= USBRECBIT_VOIPCALL;
#endif
           } else{
              mALSADevice->route(&(*it), mCurDevice, AUDIO_MODE_IN_COMMUNICATION);
          }
          if(!strcmp(it->useCase, SND_USE_CASE_VERB_IP_VOICECALL)) {
              snd_use_case_set(mUcMgr, "_verb", SND_USE_CASE_VERB_IP_VOICECALL);
          } else {
              snd_use_case_set(mUcMgr, "_enamod", SND_USE_CASE_MOD_PLAY_VOIP);
          }
          err = mALSADevice->startVoipCall(&(*it));
          if (err) {
              ALOGE("Device open failed");
              return NULL;
          }
      }
      out = new AudioStreamOutALSA(this, &(*it));
      err = out->set(format, channels, sampleRate, devices);
      if(err == NO_ERROR) {
          mVoipStreamCount++;   //increment VoipstreamCount only if success
          ALOGD("openoutput mVoipStreamCount %d",mVoipStreamCount);
      }
      if (status) *status = err;
      return out;
    } else
#endif
    if ((flags & AUDIO_OUTPUT_FLAG_DIRECT) &&
        (devices == AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        ALOGD("Multi channel PCM");
        alsa_handle_t alsa_handle;
        EDID_AUDIO_INFO info = { 0 };

        alsa_handle.module = mALSADevice;
        alsa_handle.devices = devices;
        alsa_handle.handle = 0;
        alsa_handle.format = SNDRV_PCM_FORMAT_S16_LE;

        if (!AudioUtil::getHDMIAudioSinkCaps(&info)) {
            ALOGE("openOutputStream: Failed to get HDMI sink capabilities");
            return NULL;
        }
        if (0 == *channels) {
            alsa_handle.channels = info.AudioBlocksArray[info.nAudioBlocks-1].nChannels;
            if (alsa_handle.channels > 6) {
                alsa_handle.channels = 6;
            }
            *channels = audio_channel_out_mask_from_count(alsa_handle.channels);
        } else {
            alsa_handle.channels = AudioSystem::popCount(*channels);
        }
        if (6 == alsa_handle.channels) {
            alsa_handle.bufferSize = DEFAULT_MULTI_CHANNEL_BUF_SIZE;
        } else {
            alsa_handle.bufferSize = DEFAULT_BUFFER_SIZE;
        }
        if (0 == *sampleRate) {
            alsa_handle.sampleRate = info.AudioBlocksArray[info.nAudioBlocks-1].nSamplingFreq;
            *sampleRate = alsa_handle.sampleRate;
        } else {
            alsa_handle.sampleRate = *sampleRate;
        }
        alsa_handle.latency = PLAYBACK_LATENCY;
        alsa_handle.rxHandle = 0;
        alsa_handle.ucMgr = mUcMgr;
        ALOGD("alsa_handle.channels %d alsa_handle.sampleRate %d",alsa_handle.channels,alsa_handle.sampleRate);

        char *use_case;
        snd_use_case_get(mUcMgr, "_verb", (const char **)&use_case);
        if ((use_case == NULL) || (!strcmp(use_case, SND_USE_CASE_VERB_INACTIVE))) {
            strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_HIFI2 , sizeof(alsa_handle.useCase));
        } else {
            strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_PLAY_MUSIC2, sizeof(alsa_handle.useCase));
        }
        free(use_case);
        mDeviceList.push_back(alsa_handle);
        ALSAHandleList::iterator it = mDeviceList.end();
        it--;
        ALOGD("it->useCase %s", it->useCase);
        mALSADevice->route(&(*it), devices, mode());
        if(!strcmp(it->useCase, SND_USE_CASE_VERB_HIFI2)) {
            snd_use_case_set(mUcMgr, "_verb", SND_USE_CASE_VERB_HIFI2 );
        } else {
            snd_use_case_set(mUcMgr, "_enamod", SND_USE_CASE_MOD_PLAY_MUSIC2);
        }
        ALOGD("channels: %d", AudioSystem::popCount(*channels));
        err = mALSADevice->open(&(*it));

        if (err) {
            ALOGE("Device open failed err:%d",err);
        } else {
            out = new AudioStreamOutALSA(this, &(*it));
            err = out->set(format, channels, sampleRate, devices);
        }
        if (status) *status = err;
        return out;
    } else {
      alsa_handle_t alsa_handle;
      unsigned long bufferSize = DEFAULT_BUFFER_SIZE;

      for (size_t b = 1; (bufferSize & ~b) != 0; b <<= 1)
          bufferSize &= ~b;

      alsa_handle.module = mALSADevice;
      alsa_handle.bufferSize = bufferSize;
      alsa_handle.devices = devices;
      alsa_handle.handle = 0;
      alsa_handle.format = SNDRV_PCM_FORMAT_S16_LE;
      alsa_handle.channels = DEFAULT_CHANNEL_MODE;
      alsa_handle.sampleRate = DEFAULT_SAMPLING_RATE;
      alsa_handle.latency = PLAYBACK_LATENCY;
      alsa_handle.rxHandle = 0;
      alsa_handle.ucMgr = mUcMgr;
      alsa_handle.session = NULL;
      alsa_handle.isFastOutput = false;

      char *use_case;
      snd_use_case_get(mUcMgr, "_verb", (const char **)&use_case);

#ifdef QCOM_LOW_LATENCY_AUDIO_ENABLED
      if (flags & AUDIO_OUTPUT_FLAG_FAST) {
          alsa_handle.bufferSize = PLAYBACK_LOW_LATENCY_BUFFER_SIZE;
          alsa_handle.latency = PLAYBACK_LOW_LATENCY;
          alsa_handle.isFastOutput = true;
          if ((use_case == NULL) || (!strcmp(use_case, SND_USE_CASE_VERB_INACTIVE))) {
               strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_HIFI_LOWLATENCY_MUSIC, sizeof(alsa_handle.useCase));
          } else {
               strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_PLAY_LOWLATENCY_MUSIC, sizeof(alsa_handle.useCase));
          }
      } else
#endif
      {
          if ((use_case == NULL) || (!strcmp(use_case, SND_USE_CASE_VERB_INACTIVE))) {
               strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_HIFI, sizeof(alsa_handle.useCase));
          } else {
               strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_PLAY_MUSIC, sizeof(alsa_handle.useCase));
          }
      }
      free(use_case);
      mDeviceList.push_back(alsa_handle);
      ALSAHandleList::iterator it = mDeviceList.end();
      it--;
      ALOGD("useCase %s", it->useCase);
      mALSADevice->route(&(*it), devices, mode());
#ifdef QCOM_LOW_LATENCY_AUDIO_ENABLED
      if (flags & AUDIO_OUTPUT_FLAG_FAST) {
          if(!strcmp(it->useCase, SND_USE_CASE_VERB_HIFI_LOWLATENCY_MUSIC)) {
             snd_use_case_set(mUcMgr, "_verb", SND_USE_CASE_VERB_HIFI_LOWLATENCY_MUSIC);
          } else {
             snd_use_case_set(mUcMgr, "_enamod", SND_USE_CASE_MOD_PLAY_LOWLATENCY_MUSIC);
          }
      } else
#endif
      {
          if(!strcmp(it->useCase, SND_USE_CASE_VERB_HIFI)) {
             snd_use_case_set(mUcMgr, "_verb", SND_USE_CASE_VERB_HIFI);
          } else {
             snd_use_case_set(mUcMgr, "_enamod", SND_USE_CASE_MOD_PLAY_MUSIC);
          }
      }
      err = mALSADevice->open(&(*it));
      if (err) {
          ALOGE("Device open failed");
      } else {
          out = new AudioStreamOutALSA(this, &(*it));
          err = out->set(format, channels, sampleRate, devices);
      }

      if (status) *status = err;
      return out;
    }
}

void
AudioHardwareALSA::closeOutputStream(AudioStreamOut* out)
{
    delete out;
}

#ifdef QCOM_TUNNEL_LPA_ENABLED
AudioStreamOut *
AudioHardwareALSA::openOutputSession(uint32_t devices,
                                     int *format,
                                     status_t *status,
                                     int sessionId,
                                     uint32_t samplingRate,
                                     uint32_t channels)
{
    Mutex::Autolock autoLock(mLock);
    ALOGD("openOutputSession = %d" ,sessionId);
    AudioStreamOutALSA *out = 0;
    status_t err = BAD_VALUE;

    alsa_handle_t alsa_handle;
    unsigned long bufferSize = DEFAULT_BUFFER_SIZE;

    for (size_t b = 1; (bufferSize & ~b) != 0; b <<= 1)
        bufferSize &= ~b;

    alsa_handle.module = mALSADevice;
    alsa_handle.bufferSize = bufferSize;
    alsa_handle.devices = devices;
    alsa_handle.handle = 0;
    alsa_handle.format = SNDRV_PCM_FORMAT_S16_LE;
    alsa_handle.channels = DEFAULT_CHANNEL_MODE;
    alsa_handle.sampleRate = DEFAULT_SAMPLING_RATE;
    alsa_handle.latency = VOICE_LATENCY;
    alsa_handle.rxHandle = 0;
    alsa_handle.ucMgr = mUcMgr;

    char *use_case;
    if(sessionId == TUNNEL_SESSION_ID) {
        snd_use_case_get(mUcMgr, "_verb", (const char **)&use_case);
        if ((use_case == NULL) || (!strcmp(use_case, SND_USE_CASE_VERB_INACTIVE))) {
            strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_HIFI_TUNNEL, sizeof(alsa_handle.useCase));
        } else {
            strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_PLAY_TUNNEL, sizeof(alsa_handle.useCase));
        }
    } else {
        snd_use_case_get(mUcMgr, "_verb", (const char **)&use_case);
        if ((use_case == NULL) || (!strcmp(use_case, SND_USE_CASE_VERB_INACTIVE))) {
            strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_HIFI_LOW_POWER, sizeof(alsa_handle.useCase));
        } else {
            strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_PLAY_LPA, sizeof(alsa_handle.useCase));
        }
    }
    free(use_case);
    mDeviceList.push_back(alsa_handle);
    ALSAHandleList::iterator it = mDeviceList.end();
    it--;
    ALOGD("useCase %s", it->useCase);
#ifdef QCOM_USBAUDIO_ENABLED
    if((devices & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET)||
       (devices & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET)){
        ALOGE("Routing to proxy for LPA in openOutputSession");
        mALSADevice->route(&(*it), devices, mode());
        devices = AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET;
        ALOGD("Starting USBPlayback for LPA");
        startUsbPlaybackIfNotStarted();
        musbPlaybackState |= USBPLAYBACKBIT_LPA;
    } else
#endif
    {
        mALSADevice->route(&(*it), devices, mode());
    }
    if(sessionId == TUNNEL_SESSION_ID) {
        if(!strcmp(it->useCase, SND_USE_CASE_VERB_HIFI_TUNNEL)) {
            snd_use_case_set(mUcMgr, "_verb", SND_USE_CASE_VERB_HIFI_TUNNEL);
        } else {
            snd_use_case_set(mUcMgr, "_enamod", SND_USE_CASE_MOD_PLAY_TUNNEL);
        }
    }
    else {
        if(!strcmp(it->useCase, SND_USE_CASE_VERB_HIFI_LOW_POWER)) {
            snd_use_case_set(mUcMgr, "_verb", SND_USE_CASE_VERB_HIFI_LOW_POWER);
        } else {
            snd_use_case_set(mUcMgr, "_enamod", SND_USE_CASE_MOD_PLAY_LPA);
        }
    }
    err = mALSADevice->open(&(*it));
    out = new AudioStreamOutALSA(this, &(*it));

    if (status) *status = err;
       return out;
}

void
AudioHardwareALSA::closeOutputSession(AudioStreamOut* out)
{
    delete out;
}
#endif

AudioStreamIn *
AudioHardwareALSA::openInputStream(uint32_t devices,
                                   int *format,
                                   uint32_t *channels,
                                   uint32_t *sampleRate,
                                   status_t *status,
                                   AudioSystem::audio_in_acoustics acoustics)
{
    Mutex::Autolock autoLock(mLock);
    char *use_case;
    int newMode = mode();
    uint32_t route_devices;

    status_t err = BAD_VALUE;
    AudioStreamInALSA *in = 0;
    ALSAHandleList::iterator it;

    ALOGD("openInputStream: devices 0x%x format 0x%x channels %d sampleRate %d", devices, *format, *channels, *sampleRate);
    if (devices & (devices - 1)) {
        if (status) *status = err;
        ALOGE("openInputStream failed error:0x%x devices:%x",err,devices);
        return in;
    }

    if((devices == AudioSystem::DEVICE_IN_COMMUNICATION) &&
       ((*sampleRate == VOIP_SAMPLING_RATE_8K) || (*sampleRate == VOIP_SAMPLING_RATE_16K))) {
        bool voipstream_active = false;
        for(it = mDeviceList.begin();
            it != mDeviceList.end(); ++it) {
                if((!strcmp(it->useCase, SND_USE_CASE_VERB_IP_VOICECALL)) ||
                   (!strcmp(it->useCase, SND_USE_CASE_MOD_PLAY_VOIP))) {
                    ALOGD("openInput:  it->rxHandle %p it->handle %p",it->rxHandle,it->handle);
                    voipstream_active = true;
                    break;
                }
        }
        if(voipstream_active == false) {
           mVoipStreamCount = 0;
           mVoipMicMute = false;
           alsa_handle_t alsa_handle;
           unsigned long bufferSize;
           if(*sampleRate == VOIP_SAMPLING_RATE_8K) {
               bufferSize = VOIP_BUFFER_SIZE_8K;
           }
           else if(*sampleRate == VOIP_SAMPLING_RATE_16K) {
               bufferSize = VOIP_BUFFER_SIZE_16K;
           }
           else {
               ALOGE("unsupported samplerate %d for voip",*sampleRate);
               if (status) *status = err;
               return in;
           }
           alsa_handle.module = mALSADevice;
           alsa_handle.bufferSize = bufferSize;
           alsa_handle.devices = devices;
           alsa_handle.handle = 0;
          if(*format == AUDIO_FORMAT_PCM_16_BIT)
              alsa_handle.format = SNDRV_PCM_FORMAT_S16_LE;
          else
              alsa_handle.format = *format;
           alsa_handle.channels = VOIP_DEFAULT_CHANNEL_MODE;
           alsa_handle.sampleRate = *sampleRate;
           alsa_handle.latency = VOIP_RECORD_LATENCY;
           alsa_handle.rxHandle = 0;
           alsa_handle.ucMgr = mUcMgr;
          mALSADevice->setVoipConfig(getVoipMode(*format), mVoipBitRate);
           snd_use_case_get(mUcMgr, "_verb", (const char **)&use_case);
           if ((use_case != NULL) && (strcmp(use_case, SND_USE_CASE_VERB_INACTIVE))) {
                strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_PLAY_VOIP, sizeof(alsa_handle.useCase));
           } else {
                strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_IP_VOICECALL, sizeof(alsa_handle.useCase));
           }
           free(use_case);
           mDeviceList.push_back(alsa_handle);
           it = mDeviceList.end();
           it--;
           ALOGD("mCurrDevice: %d", mCurDevice);
#ifdef QCOM_USBAUDIO_ENABLED
           if((mCurDevice == AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET)||
              (mCurDevice == AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET)){
              ALOGD("Routing everything from proxy for voipcall");
              mALSADevice->route(&(*it), AudioSystem::DEVICE_IN_PROXY, AUDIO_MODE_IN_COMMUNICATION);
              ALOGD("enabling VOIP in openInputstream, musbPlaybackState: %d", musbPlaybackState);
              startUsbPlaybackIfNotStarted();
              musbPlaybackState |= USBPLAYBACKBIT_VOIPCALL;
              ALOGD("Starting recording in openoutputstream, musbRecordingState: %d", musbRecordingState);
              startUsbRecordingIfNotStarted();
              musbRecordingState |= USBRECBIT_VOIPCALL;
           } else
#endif
           {
               mALSADevice->route(&(*it),mCurDevice, AUDIO_MODE_IN_COMMUNICATION);
           }
           if(!strcmp(it->useCase, SND_USE_CASE_VERB_IP_VOICECALL)) {
               snd_use_case_set(mUcMgr, "_verb", SND_USE_CASE_VERB_IP_VOICECALL);
           } else {
               snd_use_case_set(mUcMgr, "_enamod", SND_USE_CASE_MOD_PLAY_VOIP);
           }
           if(sampleRate) {
               it->sampleRate = *sampleRate;
           }
           if(channels) {
               it->channels = AudioSystem::popCount(*channels);
               setInChannels(devices);
           }
           err = mALSADevice->startVoipCall(&(*it));
           if (err) {
               ALOGE("Error opening pcm input device");
               return NULL;
           }
        }
        in = new AudioStreamInALSA(this, &(*it), acoustics);
        err = in->set(format, channels, sampleRate, devices);
        if(err == NO_ERROR) {
            mVoipStreamCount++;   //increment VoipstreamCount only if success
            ALOGD("OpenInput mVoipStreamCount %d",mVoipStreamCount);
        }
        ALOGD("openInput: After Get alsahandle");
        if (status) *status = err;
        return in;
      } else {
        alsa_handle_t alsa_handle;
        unsigned long bufferSize = MIN_CAPTURE_BUFFER_SIZE_PER_CH;

        alsa_handle.module = mALSADevice;
        alsa_handle.bufferSize = bufferSize;
        alsa_handle.devices = devices;
        alsa_handle.handle = 0;
        if(*format == AUDIO_FORMAT_PCM_16_BIT)
            alsa_handle.format = SNDRV_PCM_FORMAT_S16_LE;
        else
            alsa_handle.format = *format;
        alsa_handle.channels = VOICE_CHANNEL_MODE;
        alsa_handle.sampleRate = android::AudioRecord::DEFAULT_SAMPLE_RATE;
        alsa_handle.latency = RECORD_LATENCY;
        alsa_handle.rxHandle = 0;
        alsa_handle.ucMgr = mUcMgr;
        snd_use_case_get(mUcMgr, "_verb", (const char **)&use_case);
        if ((use_case != NULL) && (strcmp(use_case, SND_USE_CASE_VERB_INACTIVE))) {
            if ((devices == AudioSystem::DEVICE_IN_VOICE_CALL) &&
                (newMode == AUDIO_MODE_IN_CALL)) {
                ALOGD("openInputStream: into incall recording, channels %d", *channels);
                mIncallMode = *channels;
                if ((*channels & AUDIO_CHANNEL_IN_VOICE_UPLINK) &&
                    (*channels & AUDIO_CHANNEL_IN_VOICE_DNLINK)) {
                    if (mFusion3Platform) {
                        mALSADevice->setVocRecMode(INCALL_REC_STEREO);
                        strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_CAPTURE_VOICE,
                                sizeof(alsa_handle.useCase));
                    } else {
                        if (*format == AUDIO_FORMAT_AMR_WB) {
                            strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_CAPTURE_COMPRESSED_VOICE_UL_DL,
                                    sizeof(alsa_handle.useCase));
                        } else {
                            strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_CAPTURE_VOICE_UL_DL,
                                    sizeof(alsa_handle.useCase));
                        }
                    }
                } else if (*channels & AUDIO_CHANNEL_IN_VOICE_DNLINK) {
                    if (mFusion3Platform) {
                        mALSADevice->setVocRecMode(INCALL_REC_MONO);
                        strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_CAPTURE_VOICE,
                                sizeof(alsa_handle.useCase));
                    } else {
                        if (*format == AUDIO_FORMAT_AMR_WB) {
                            strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_CAPTURE_COMPRESSED_VOICE_DL,
                                    sizeof(alsa_handle.useCase));
                        } else {
                            strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_CAPTURE_VOICE_DL,
                                    sizeof(alsa_handle.useCase));
                        }
                    }
                }
#ifdef QCOM_FM_ENABLED
            } else if((devices == AudioSystem::DEVICE_IN_FM_RX)) {
                strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_CAPTURE_FM, sizeof(alsa_handle.useCase));
            } else if(devices == AudioSystem::DEVICE_IN_FM_RX_A2DP) {
                strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_CAPTURE_A2DP_FM, sizeof(alsa_handle.useCase));
#endif
            } else {
                char value[128];
                property_get("persist.audio.lowlatency.rec",value,"0");
                if (!strcmp("true", value)) {
                    strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_CAPTURE_LOWLATENCY_MUSIC, sizeof(alsa_handle.useCase));
                } else if (*format == AUDIO_FORMAT_AMR_WB) {
                    ALOGV("Format AMR_WB, open compressed capture");
                    strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED, sizeof(alsa_handle.useCase));
                } else {
                    strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC, sizeof(alsa_handle.useCase));
                }
            }
        } else {
            if ((devices == AudioSystem::DEVICE_IN_VOICE_CALL) &&
                (newMode == AUDIO_MODE_IN_CALL)) {
                ALOGD("openInputStream: incall recording, channels %d", *channels);
                mIncallMode = *channels;
                if ((*channels & AUDIO_CHANNEL_IN_VOICE_UPLINK) &&
                    (*channels & AUDIO_CHANNEL_IN_VOICE_DNLINK)) {
                    if (mFusion3Platform) {
                        mALSADevice->setVocRecMode(INCALL_REC_STEREO);
                        strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_INCALL_REC,
                                sizeof(alsa_handle.useCase));
                    } else {
                        if (*format == AUDIO_FORMAT_AMR_WB) {
                            strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_CAPTURE_COMPRESSED_VOICE_UL_DL,
                                    sizeof(alsa_handle.useCase));
                        } else {
                            strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_UL_DL_REC,
                                    sizeof(alsa_handle.useCase));
                        }
                    }
                } else if (*channels & AUDIO_CHANNEL_IN_VOICE_DNLINK) {
                    if (mFusion3Platform) {
                        mALSADevice->setVocRecMode(INCALL_REC_MONO);
                        strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_INCALL_REC,
                                sizeof(alsa_handle.useCase));
                    } else {
                        if (*format == AUDIO_FORMAT_AMR_WB) {
                            strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_CAPTURE_COMPRESSED_VOICE_DL,
                                    sizeof(alsa_handle.useCase));
                        } else {
                            strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_DL_REC,
                                    sizeof(alsa_handle.useCase));
                        }
                    }
                }
#ifdef QCOM_FM_ENABLED
            } else if(devices == AudioSystem::DEVICE_IN_FM_RX) {
                strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_FM_REC, sizeof(alsa_handle.useCase));
            } else if (devices == AudioSystem::DEVICE_IN_FM_RX_A2DP) {
                strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_FM_A2DP_REC, sizeof(alsa_handle.useCase));
#endif
            } else {
                char value[128];
                property_get("persist.audio.lowlatency.rec",value,"0");
                if (!strcmp("true", value)) {
                    strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_HIFI_LOWLATENCY_REC, sizeof(alsa_handle.useCase));
                } else if (*format == AUDIO_FORMAT_AMR_WB) {
                    strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED, sizeof(alsa_handle.useCase));
                } else {
                    strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_HIFI_REC, sizeof(alsa_handle.useCase));
                }
            }
        }
        free(use_case);
        mDeviceList.push_back(alsa_handle);
        ALSAHandleList::iterator it = mDeviceList.end();
        it--;
        //update channel info before do routing
        if(channels) {
            it->channels = AudioSystem::popCount((*channels) &
                      (AUDIO_CHANNEL_IN_STEREO
                       | AUDIO_CHANNEL_IN_MONO
#ifdef QCOM_SSR_ENABLED
                       | AUDIO_CHANNEL_IN_5POINT1
#endif
                       ));
            ALOGV("updated channel info: channels=%d", it->channels);
            setInChannels(devices);
        }
        if (devices == AudioSystem::DEVICE_IN_VOICE_CALL){
           /* Add current devices info to devices to do route */
#ifdef QCOM_USBAUDIO_ENABLED
            if(mCurDevice == AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET ||
               mCurDevice == AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET){
                ALOGD("Routing everything from proxy for VOIP call");
                route_devices = devices | AudioSystem::DEVICE_IN_PROXY;
            } else
#endif
            {
            route_devices = devices | mCurDevice;
            }
            mALSADevice->route(&(*it), route_devices, mode());
        } else {
#ifdef QCOM_USBAUDIO_ENABLED
            if(devices & AudioSystem::DEVICE_IN_ANLG_DOCK_HEADSET ||
               devices & AudioSystem::DEVICE_IN_PROXY) {
                devices |= AudioSystem::DEVICE_IN_PROXY;
                ALOGD("routing everything from proxy");
            mALSADevice->route(&(*it), devices, mode());
            } else
#endif
            {
                mALSADevice->route(&(*it), devices, mode());
            }
        }

        if(!strcmp(it->useCase, SND_USE_CASE_VERB_HIFI_REC) ||
           !strcmp(it->useCase, SND_USE_CASE_VERB_HIFI_LOWLATENCY_REC) ||
           !strcmp(it->useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED) ||
#ifdef QCOM_FM_ENABLED
           !strcmp(it->useCase, SND_USE_CASE_VERB_FM_REC) ||
           !strcmp(it->useCase, SND_USE_CASE_VERB_FM_A2DP_REC) ||
#endif
           !strcmp(it->useCase, SND_USE_CASE_VERB_DL_REC) ||
           !strcmp(it->useCase, SND_USE_CASE_VERB_UL_DL_REC) ||
           !strcmp(it->useCase, SND_USE_CASE_VERB_CAPTURE_COMPRESSED_VOICE_DL) ||
           !strcmp(it->useCase, SND_USE_CASE_VERB_CAPTURE_COMPRESSED_VOICE_UL_DL) ||
           !strcmp(it->useCase, SND_USE_CASE_VERB_INCALL_REC)) {
            snd_use_case_set(mUcMgr, "_verb", it->useCase);
        } else {
            snd_use_case_set(mUcMgr, "_enamod", it->useCase);
        }
        if(sampleRate) {
            it->sampleRate = *sampleRate;
        }
        if (!strncmp(it->useCase, SND_USE_CASE_VERB_HIFI_REC, strlen(SND_USE_CASE_VERB_HIFI_REC))
            || !strncmp(it->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC, strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC))) {
            ALOGV("OpenInoutStream: getInputBufferSize sampleRate:%d format:%d, channels:%d", it->sampleRate,*format,it->channels);
            it->bufferSize = getInputBufferSize(it->sampleRate,*format,it->channels);
        }

#ifdef QCOM_SSR_ENABLED
        if (6 == it->channels) {
            if (!strncmp(it->useCase, SND_USE_CASE_VERB_HIFI_REC, strlen(SND_USE_CASE_VERB_HIFI_REC))
                || !strncmp(it->useCase, SND_USE_CASE_VERB_HIFI_REC_COMPRESSED, strlen(SND_USE_CASE_VERB_HIFI_REC_COMPRESSED))
                || !strncmp(it->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC, strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC))
                || !strncmp(it->useCase, SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED, strlen(SND_USE_CASE_MOD_CAPTURE_MUSIC_COMPRESSED))) {
                //Check if SSR is supported by reading system property
                char ssr_enabled[6] = "false";
                property_get("ro.qc.sdk.audio.ssr",ssr_enabled,"0");
                if (strncmp("true", ssr_enabled, 4)) {
                    if (status) *status = err;
                    ALOGE("openInputStream: FAILED:%d. Surround sound recording is not supported",*status);
                }
            }
        }
#endif
        err = mALSADevice->open(&(*it));
        if (*format == AUDIO_FORMAT_AMR_WB) {
             ALOGV("### Setting bufsize to 61");
             it->bufferSize = 61;
        }
        if (err) {
           ALOGE("Error opening pcm input device");
        } else {
           in = new AudioStreamInALSA(this, &(*it), acoustics);
           err = in->set(format, channels, sampleRate, devices);
        }
        if (status) *status = err;
        return in;
      }
}

void
AudioHardwareALSA::closeInputStream(AudioStreamIn* in)
{
    delete in;
}

status_t AudioHardwareALSA::setMicMute(bool state)
{
    int newMode = mode();
    ALOGD("setMicMute  newMode %d state:%d",newMode,state);
    if(newMode == AUDIO_MODE_IN_COMMUNICATION) {
        if (mVoipMicMute != state) {
             mVoipMicMute = state;
            ALOGD("setMicMute: mVoipMicMute %d", mVoipMicMute);
            if(mALSADevice) {
                mALSADevice->setVoipMicMute(state);
            }
        }
    } else {
        if (mMicMute != state) {
              mMicMute = state;
              ALOGD("setMicMute: mMicMute %d", mMicMute);
              if(mALSADevice) {
                 if(mCSCallActive == CS_ACTIVE)
                    mALSADevice->setMicMute(state);
                 else if(mSGLTECallActive == CS_ACTIVE_SESSION2)
                    mALSADevice->setSGLTEMicMute(state);
                 if(mVolteCallActive == IMS_ACTIVE)
                    mALSADevice->setVoLTEMicMute(state);
              }
        }
    }
    return NO_ERROR;
}

status_t AudioHardwareALSA::getMicMute(bool *state)
{
    int newMode = mode();
    if(newMode == AUDIO_MODE_IN_COMMUNICATION) {
        *state = mVoipMicMute;
    } else {
        *state = mMicMute;
    }
    return NO_ERROR;
}

status_t AudioHardwareALSA::dump(int fd, const Vector<String16>& args)
{
    return NO_ERROR;
}

size_t AudioHardwareALSA::getInputBufferSize(uint32_t sampleRate, int format, int channelCount)
{
    size_t bufferSize = 0;
    if (format == AUDIO_FORMAT_PCM_16_BIT) {
        if(sampleRate == 8000 || sampleRate == 16000 || sampleRate == 32000) {
            bufferSize = (sampleRate * channelCount * 20 * sizeof(int16_t)) / 1000;
        } else if (sampleRate == 11025 || sampleRate == 12000) {
            bufferSize = 256 * sizeof(int16_t)  * channelCount;
        } else if (sampleRate == 22050 || sampleRate == 24000) {
            bufferSize = 512 * sizeof(int16_t)  * channelCount;
        } else if (sampleRate == 44100 || sampleRate == 48000) {
            bufferSize = 1024 * sizeof(int16_t) * channelCount;
        }
    } else {
        ALOGE("getInputBufferSize bad format: %d", format);
    }
    return bufferSize;
}

#ifdef QCOM_FM_ENABLED
void AudioHardwareALSA::handleFm(int device)
{
    int newMode = mode();
    uint32_t activeUsecase = USECASE_NONE;

    if(device & AUDIO_DEVICE_OUT_FM && mIsFmActive == 0) {
        // Start FM Radio on current active device
        unsigned long bufferSize = FM_BUFFER_SIZE;
        alsa_handle_t alsa_handle;
        char *use_case;
        ALOGV("Start FM");
        snd_use_case_get(mUcMgr, "_verb", (const char **)&use_case);
        if ((use_case == NULL) || (!strcmp(use_case, SND_USE_CASE_VERB_INACTIVE))) {
            strlcpy(alsa_handle.useCase, SND_USE_CASE_VERB_DIGITAL_RADIO, sizeof(alsa_handle.useCase));
        } else {
            strlcpy(alsa_handle.useCase, SND_USE_CASE_MOD_PLAY_FM, sizeof(alsa_handle.useCase));
        }
        free(use_case);

        for (size_t b = 1; (bufferSize & ~b) != 0; b <<= 1)
        bufferSize &= ~b;
        alsa_handle.module = mALSADevice;
        alsa_handle.bufferSize = bufferSize;
        alsa_handle.devices = device;
        alsa_handle.handle = 0;
        alsa_handle.format = SNDRV_PCM_FORMAT_S16_LE;
        alsa_handle.channels = DEFAULT_CHANNEL_MODE;
        alsa_handle.sampleRate = DEFAULT_SAMPLING_RATE;
        alsa_handle.latency = VOICE_LATENCY;
        alsa_handle.rxHandle = 0;
        alsa_handle.ucMgr = mUcMgr;
        mIsFmActive = 1;
        mDeviceList.push_back(alsa_handle);
        ALSAHandleList::iterator it = mDeviceList.end();
        it--;
        mALSADevice->route(&(*it), (uint32_t)device, newMode);
        if(!strcmp(it->useCase, SND_USE_CASE_VERB_DIGITAL_RADIO)) {
            snd_use_case_set(mUcMgr, "_verb", SND_USE_CASE_VERB_DIGITAL_RADIO);
        } else {
            snd_use_case_set(mUcMgr, "_enamod", SND_USE_CASE_MOD_PLAY_FM);
        }
        mALSADevice->startFm(&(*it));
        activeUsecase = useCaseStringToEnum(it->useCase);
#ifdef QCOM_USBAUDIO_ENABLED
        if((device & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET)||
           (device & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET)){
            ALOGD("Starting FM, musbPlaybackState %d", musbPlaybackState);
            startUsbPlaybackIfNotStarted();
            musbPlaybackState |= USBPLAYBACKBIT_FM;
        }
#endif
        if(isExtOutDevice(device)) {
            status_t err = NO_ERROR;
            mRouteAudioToExtOut = true;
            err = startPlaybackOnExtOut_l(activeUsecase);
            if(err) {
                ALOGE("startPlaybackOnExtOut_l for hardware output failed err = %d", err);
                stopPlaybackOnExtOut_l(activeUsecase);
            }
        }

    } else if (!(device & AUDIO_DEVICE_OUT_FM) && mIsFmActive == 1) {
        //i Stop FM Radio
        ALOGV("Stop FM");
        for(ALSAHandleList::iterator it = mDeviceList.begin();
            it != mDeviceList.end(); ++it) {
            if((!strcmp(it->useCase, SND_USE_CASE_VERB_DIGITAL_RADIO)) ||
              (!strcmp(it->useCase, SND_USE_CASE_MOD_PLAY_FM))) {
                mALSADevice->close(&(*it));
                activeUsecase = useCaseStringToEnum(it->useCase);
                //mALSADevice->route(&(*it), (uint32_t)device, newMode);
                mDeviceList.erase(it);
                break;
            }
        }
        mIsFmActive = 0;
#ifdef QCOM_USBAUDIO_ENABLED
        musbPlaybackState &= ~USBPLAYBACKBIT_FM;
        if((device & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET)||
           (device & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET)){
            closeUsbPlaybackIfNothingActive();
        }
#endif
        if(mRouteAudioToExtOut == true) {
            status_t err = NO_ERROR;
            err = stopPlaybackOnExtOut_l(activeUsecase);
            if(err)
                ALOGE("stopPlaybackOnExtOut_l for hardware output failed err = %d", err);
        }

    }
}
#endif

void AudioHardwareALSA::disableVoiceCall(char* verb, char* modifier, int mode, int device)
{
    for(ALSAHandleList::iterator it = mDeviceList.begin();
         it != mDeviceList.end(); ++it) {
        if((!strcmp(it->useCase, verb)) ||
           (!strcmp(it->useCase, modifier))) {
            ALOGV("Disabling voice call");
            mALSADevice->close(&(*it));
            mALSADevice->route(&(*it), (uint32_t)device, mode);
            mDeviceList.erase(it);
            break;
        }
    }

#ifdef QCOM_USBAUDIO_ENABLED
   if(musbPlaybackState & USBPLAYBACKBIT_VOICECALL) {
          ALOGD("Voice call ended on USB");
          musbPlaybackState &= ~USBPLAYBACKBIT_VOICECALL;
          musbRecordingState &= ~USBRECBIT_VOICECALL;
          closeUsbRecordingIfNothingActive();
          closeUsbPlaybackIfNothingActive();
   }
#endif
}
void AudioHardwareALSA::enableVoiceCall(char* verb, char* modifier, int mode, int device)
{
// Start voice call
unsigned long bufferSize = DEFAULT_VOICE_BUFFER_SIZE;
alsa_handle_t alsa_handle;
char *use_case;
    snd_use_case_get(mUcMgr, "_verb", (const char **)&use_case);
    if ((use_case == NULL) || (!strcmp(use_case, SND_USE_CASE_VERB_INACTIVE))) {
        strlcpy(alsa_handle.useCase, verb, sizeof(alsa_handle.useCase));
    } else {
        strlcpy(alsa_handle.useCase, modifier, sizeof(alsa_handle.useCase));
    }
    free(use_case);

    for (size_t b = 1; (bufferSize & ~b) != 0; b <<= 1)
    bufferSize &= ~b;
    alsa_handle.module = mALSADevice;
    alsa_handle.bufferSize = bufferSize;
    alsa_handle.devices = device;
    alsa_handle.handle = 0;
    alsa_handle.format = SNDRV_PCM_FORMAT_S16_LE;
    alsa_handle.channels = VOICE_CHANNEL_MODE;
    alsa_handle.sampleRate = VOICE_SAMPLING_RATE;
    alsa_handle.latency = VOICE_LATENCY;
    alsa_handle.rxHandle = 0;
    alsa_handle.ucMgr = mUcMgr;
    mDeviceList.push_back(alsa_handle);
    ALSAHandleList::iterator it = mDeviceList.end();
    it--;
    setInChannels(device);
    mALSADevice->route(&(*it), (uint32_t)device, mode);
    if (!strcmp(it->useCase, verb)) {
        snd_use_case_set(mUcMgr, "_verb", verb);
    } else {
        snd_use_case_set(mUcMgr, "_enamod", modifier);
    }
    mALSADevice->startVoiceCall(&(*it));

#ifdef QCOM_USBAUDIO_ENABLED
    if((device & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET)||
       (device & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET)){
       startUsbRecordingIfNotStarted();
       startUsbPlaybackIfNotStarted();
       musbPlaybackState |= USBPLAYBACKBIT_VOICECALL;
       musbRecordingState |= USBRECBIT_VOICECALL;
    }
#endif
}

bool AudioHardwareALSA::routeVoiceCall(int device, int newMode)
{
int csCallState = mCallState&0xF;
 bool isRouted = false;
 switch (csCallState) {
    case CS_INACTIVE:
        if (mCSCallActive != CS_INACTIVE) {
            ALOGD("doRouting: Disabling voice call");
            disableVoiceCall((char *)SND_USE_CASE_VERB_VOICECALL,
                (char *)SND_USE_CASE_MOD_PLAY_VOICE, newMode, device);
            isRouted = true;
            mCSCallActive = CS_INACTIVE;
        }
    break;
    case CS_ACTIVE:
        if (mCSCallActive == CS_INACTIVE) {
            ALOGD("doRouting: Enabling CS voice call ");
            enableVoiceCall((char *)SND_USE_CASE_VERB_VOICECALL,
                (char *)SND_USE_CASE_MOD_PLAY_VOICE, newMode, device);
            isRouted = true;
            mCSCallActive = CS_ACTIVE;
        } else if (mCSCallActive == CS_HOLD) {
             ALOGD("doRouting: Resume voice call from hold state");
             ALSAHandleList::iterator vt_it;
             for(vt_it = mDeviceList.begin();
                 vt_it != mDeviceList.end(); ++vt_it) {
                 if((!strncmp(vt_it->useCase, SND_USE_CASE_VERB_VOICECALL,
                     strlen(SND_USE_CASE_VERB_VOICECALL))) ||
                     (!strncmp(vt_it->useCase, SND_USE_CASE_MOD_PLAY_VOICE,
                     strlen(SND_USE_CASE_MOD_PLAY_VOICE)))) {
                     alsa_handle_t *handle = (alsa_handle_t *)(&(*vt_it));
                     mCSCallActive = CS_ACTIVE;
                     if(ioctl((int)handle->handle->fd,SNDRV_PCM_IOCTL_PAUSE,0)<0)
                                   ALOGE("VoLTE resume failed");
                     break;
                 }
             }
        }
    break;
    case CS_HOLD:
        if (mCSCallActive == CS_ACTIVE) {
            ALOGD("doRouting: Voice call going to Hold");
             ALSAHandleList::iterator vt_it;
             for(vt_it = mDeviceList.begin();
                 vt_it != mDeviceList.end(); ++vt_it) {
                 if((!strncmp(vt_it->useCase, SND_USE_CASE_VERB_VOICECALL,
                     strlen(SND_USE_CASE_VERB_VOICECALL))) ||
                     (!strncmp(vt_it->useCase, SND_USE_CASE_MOD_PLAY_VOICE,
                         strlen(SND_USE_CASE_MOD_PLAY_VOICE)))) {
                         mCSCallActive = CS_HOLD;
                         alsa_handle_t *handle = (alsa_handle_t *)(&(*vt_it));
                         if(ioctl((int)handle->handle->fd,SNDRV_PCM_IOCTL_PAUSE,1)<0)
                                   ALOGE("Voice pause failed");
                         break;
                }
            }
        }
    break;
    }
    return isRouted;
}

bool AudioHardwareALSA::routeSGLTECall(int device, int newMode)
{
int SGLTECallState = mCallState&0xF00;
 bool isRouted = false;
 switch (SGLTECallState) {
    case CS_INACTIVE_SESSION2:
        if (mSGLTECallActive != CS_INACTIVE_SESSION2) {
            ALOGV("doRouting: Disabling voice call session2");
            disableVoiceCall((char *)SND_USE_CASE_VERB_SGLTECALL,
                (char *)SND_USE_CASE_MOD_PLAY_SGLTE, newMode, device);
            isRouted = true;
            mSGLTECallActive = CS_INACTIVE_SESSION2;
        }
    break;
    case CS_ACTIVE_SESSION2:
        if (mSGLTECallActive == CS_INACTIVE_SESSION2) {
            ALOGV("doRouting: Enabling CS voice call session2 ");
            enableVoiceCall((char *)SND_USE_CASE_VERB_SGLTECALL,
                (char *)SND_USE_CASE_MOD_PLAY_SGLTE, newMode, device);
            isRouted = true;
            mSGLTECallActive = CS_ACTIVE_SESSION2;
        } else if (mSGLTECallActive == CS_HOLD_SESSION2) {
             ALOGV("doRouting: Resume voice call session2 from hold state");
             ALSAHandleList::iterator vt_it;
             for(vt_it = mDeviceList.begin();
                 vt_it != mDeviceList.end(); ++vt_it) {
                 if((!strncmp(vt_it->useCase, SND_USE_CASE_VERB_SGLTECALL,
                     strlen(SND_USE_CASE_VERB_SGLTECALL))) ||
                     (!strncmp(vt_it->useCase, SND_USE_CASE_MOD_PLAY_SGLTE,
                     strlen(SND_USE_CASE_MOD_PLAY_SGLTE)))) {
                     alsa_handle_t *handle = (alsa_handle_t *)(&(*vt_it));
                     mSGLTECallActive = CS_ACTIVE_SESSION2;
                     if(ioctl((int)handle->handle->fd,SNDRV_PCM_IOCTL_PAUSE,0)<0)
                         ALOGE("SGLTE resume failed");
                     break;
                 }
             }
        }
    break;
    case CS_HOLD_SESSION2:
        if (mSGLTECallActive == CS_ACTIVE_SESSION2) {
            ALOGV("doRouting: Voice call session2 going to Hold");
             ALSAHandleList::iterator vt_it;
             for(vt_it = mDeviceList.begin();
                 vt_it != mDeviceList.end(); ++vt_it) {
                 if((!strncmp(vt_it->useCase, SND_USE_CASE_VERB_SGLTECALL,
                     strlen(SND_USE_CASE_VERB_SGLTECALL))) ||
                     (!strncmp(vt_it->useCase, SND_USE_CASE_MOD_PLAY_SGLTE,
                         strlen(SND_USE_CASE_MOD_PLAY_SGLTE)))) {
                         mSGLTECallActive = CS_HOLD_SESSION2;
                         alsa_handle_t *handle = (alsa_handle_t *)(&(*vt_it));
                         if(ioctl((int)handle->handle->fd,SNDRV_PCM_IOCTL_PAUSE,1)<0)
                             ALOGE("Voice session2 pause failed");
                         break;
                }
            }
        }
    break;
    }
    return isRouted;
}


bool AudioHardwareALSA::routeVoLTECall(int device, int newMode)
{
int volteCallState = mCallState&0xF0;
bool isRouted = false;
switch (volteCallState) {
    case IMS_INACTIVE:
        if (mVolteCallActive != IMS_INACTIVE) {
            ALOGD("doRouting: Disabling IMS call");
            disableVoiceCall((char *)SND_USE_CASE_VERB_VOLTE,
                (char *)SND_USE_CASE_MOD_PLAY_VOLTE, newMode, device);
            isRouted = true;
            mVolteCallActive = IMS_INACTIVE;
        }
    break;
    case IMS_ACTIVE:
        if (mVolteCallActive == IMS_INACTIVE) {
            ALOGD("doRouting: Enabling IMS voice call ");
            enableVoiceCall((char *)SND_USE_CASE_VERB_VOLTE,
                (char *)SND_USE_CASE_MOD_PLAY_VOLTE, newMode, device);
            isRouted = true;
            mVolteCallActive = IMS_ACTIVE;
        } else if (mVolteCallActive == IMS_HOLD) {
             ALOGD("doRouting: Resume IMS call from hold state");
             ALSAHandleList::iterator vt_it;
             for(vt_it = mDeviceList.begin();
                 vt_it != mDeviceList.end(); ++vt_it) {
                 if((!strncmp(vt_it->useCase, SND_USE_CASE_VERB_VOLTE,
                     strlen(SND_USE_CASE_VERB_VOLTE))) ||
                     (!strncmp(vt_it->useCase, SND_USE_CASE_MOD_PLAY_VOLTE,
                     strlen(SND_USE_CASE_MOD_PLAY_VOLTE)))) {
                     alsa_handle_t *handle = (alsa_handle_t *)(&(*vt_it));
                     mVolteCallActive = IMS_ACTIVE;
                     if(ioctl((int)handle->handle->fd,SNDRV_PCM_IOCTL_PAUSE,0)<0)
                                   ALOGE("VoLTE resume failed");
                     break;
                 }
             }
        }
    break;
    case IMS_HOLD:
        if (mVolteCallActive == IMS_ACTIVE) {
             ALOGD("doRouting: IMS ACTIVE going to HOLD");
             ALSAHandleList::iterator vt_it;
             for(vt_it = mDeviceList.begin();
                 vt_it != mDeviceList.end(); ++vt_it) {
                 if((!strncmp(vt_it->useCase, SND_USE_CASE_VERB_VOLTE,
                     strlen(SND_USE_CASE_VERB_VOLTE))) ||
                     (!strncmp(vt_it->useCase, SND_USE_CASE_MOD_PLAY_VOLTE,
                         strlen(SND_USE_CASE_MOD_PLAY_VOLTE)))) {
                          mVolteCallActive = IMS_HOLD;
                         alsa_handle_t *handle = (alsa_handle_t *)(&(*vt_it));
                         if(ioctl((int)handle->handle->fd,SNDRV_PCM_IOCTL_PAUSE,1)<0)
                                   ALOGE("VoLTE Pause failed");
                    break;
                }
            }
        }
    break;
    }
    return isRouted;
}

void AudioHardwareALSA::pauseIfUseCaseTunnelOrLPA() {
    for (ALSAHandleList::iterator it = mDeviceList.begin();
           it != mDeviceList.end(); it++) {
        if((!strncmp(it->useCase, SND_USE_CASE_VERB_HIFI_TUNNEL,
                strlen(SND_USE_CASE_VERB_HIFI_TUNNEL))) ||
            (!strncmp(it->useCase, SND_USE_CASE_MOD_PLAY_TUNNEL,
                strlen(SND_USE_CASE_MOD_PLAY_TUNNEL))) ||
            (!strncmp(it->useCase, SND_USE_CASE_VERB_HIFI_LOW_POWER,
                strlen(SND_USE_CASE_VERB_HIFI_LOW_POWER))) ||
            (!strncmp(it->useCase, SND_USE_CASE_MOD_PLAY_LPA,
                strlen(SND_USE_CASE_MOD_PLAY_LPA)))) {
                it->session->pause_l();
        }
    }
}

void AudioHardwareALSA::resumeIfUseCaseTunnelOrLPA() {
    for (ALSAHandleList::iterator it = mDeviceList.begin();
           it != mDeviceList.end(); it++) {
        if((!strncmp(it->useCase, SND_USE_CASE_VERB_HIFI_TUNNEL,
                strlen(SND_USE_CASE_VERB_HIFI_TUNNEL))) ||
            (!strncmp(it->useCase, SND_USE_CASE_MOD_PLAY_TUNNEL,
                strlen(SND_USE_CASE_MOD_PLAY_TUNNEL))) ||
            (!strncmp(it->useCase, SND_USE_CASE_VERB_HIFI_LOW_POWER,
                strlen(SND_USE_CASE_VERB_HIFI_LOW_POWER))) ||
            (!strncmp(it->useCase, SND_USE_CASE_MOD_PLAY_LPA,
                strlen(SND_USE_CASE_MOD_PLAY_LPA)))) {
                it->session->resume_l();
        }
    }
}

status_t AudioHardwareALSA::startPlaybackOnExtOut(uint32_t activeUsecase) {

    Mutex::Autolock autoLock(mLock);
    status_t err = startPlaybackOnExtOut_l(activeUsecase);
    if(err) {
        ALOGE("startPlaybackOnExtOut_l  = %d", err);
    }
    return err;
}
status_t AudioHardwareALSA::startPlaybackOnExtOut_l(uint32_t activeUsecase) {

    ALOGV("startPlaybackOnExtOut_l::usecase = %d ", activeUsecase);
    status_t err = NO_ERROR;

    if (!mExtOutStream) {
        ALOGE("Unable to open ExtOut stream");
        return err;
    }
    if (activeUsecase != USECASE_NONE && !mIsExtOutEnabled) {
        Mutex::Autolock autolock1(mExtOutMutex);
        err = mALSADevice->openProxyDevice();
        if(err) {
            ALOGE("openProxyDevice failed = %d", err);
        }

        mKillExtOutThread = false;
        err = pthread_create(&mExtOutThread, (const pthread_attr_t *) NULL,
                extOutThreadWrapper,
                this);
        if(err) {
            ALOGE("thread create failed = %d", err);
            return err;
        }
        mExtOutThreadAlive = true;
        mIsExtOutEnabled = true;

#ifdef OUTPUT_BUFFER_LOG
    sprintf(outputfilename, "%s%d%s", outputfilename, number,".pcm");
    outputBufferFile1 = fopen (outputfilename, "ab");
    number++;
#endif
    }

    setExtOutActiveUseCases_l(activeUsecase);
    mALSADevice->resumeProxy();

    Mutex::Autolock autolock1(mExtOutMutex);
    ALOGV("ExtOut signal");
    mExtOutCv.signal();
    return err;
}

status_t AudioHardwareALSA::stopPlaybackOnExtOut(uint32_t activeUsecase) {
     Mutex::Autolock autoLock(mLock);
     status_t err = stopPlaybackOnExtOut_l(activeUsecase);
     if(err) {
         ALOGE("stopPlaybackOnExtOut = %d", err);
     }
     return err;
}

status_t AudioHardwareALSA::stopPlaybackOnExtOut_l(uint32_t activeUsecase) {

     ALOGV("stopPlaybackOnExtOut  = %d", activeUsecase);
     status_t err = NO_ERROR;
     suspendPlaybackOnExtOut_l(activeUsecase);
     {
         Mutex::Autolock autolock1(mExtOutMutex);
         ALOGV("stopPlaybackOnExtOut  getExtOutActiveUseCases_l = %d",
                getExtOutActiveUseCases_l());

         if(!getExtOutActiveUseCases_l()) {
             mIsExtOutEnabled = false;

             mExtOutMutex.unlock();
             err = stopExtOutThread();
             if(err) {
                 ALOGE("stopExtOutThread Failed :: err = %d" ,err);
             }
             mExtOutMutex.lock();

             if (mExtOutStream != NULL) {
                 ALOGV(" External Output Stream Standby called");
                 mExtOutStream->common.standby(&mExtOutStream->common);
             }

             err = mALSADevice->closeProxyDevice();
             if(err) {
                 ALOGE("closeProxyDevice failed = %d", err);
             }

             mExtOutActiveUseCases = 0x0;
             mRouteAudioToExtOut = false;

#ifdef OUTPUT_BUFFER_LOG
    ALOGV("close file output");
    fclose (outputBufferFile1);
#endif
         }
     }
     return err;
}

status_t AudioHardwareALSA::openExtOutput(int device) {

    ALOGV("openExtOutput");
    status_t err = NO_ERROR;
    Mutex::Autolock autolock1(mExtOutMutex);
    if (device & AudioSystem::DEVICE_OUT_ALL_A2DP) {
        err= openA2dpOutput();
        if(err) {
            ALOGE("openA2DPOutput failed = %d",err);
            return err;
        }
    } else if (device & AUDIO_DEVICE_OUT_ALL_USB) {
        err= openUsbOutput();
        if(err) {
        ALOGE("openUsbPOutput failed = %d",err);
        return err;
        }
    }
    return err;
}

status_t AudioHardwareALSA::closeExtOutput(int device) {

    ALOGV("closeExtOutput");
    status_t err = NO_ERROR;
    Mutex::Autolock autolock1(mExtOutMutex);
    mExtOutStream = NULL;
    if (device & AudioSystem::DEVICE_OUT_ALL_A2DP) {
        err= closeA2dpOutput();
        if(err) {
            ALOGE("closeA2DPOutput failed = %d",err);
            return err;
        }
    } else if (device & AUDIO_DEVICE_OUT_ALL_USB) {
        err= closeUsbOutput();
        if(err) {
            ALOGE("closeUsbPOutput failed = %d",err);
            return err;
        }
    }
    return err;
}

status_t AudioHardwareALSA::openA2dpOutput()
{
    hw_module_t *mod;
    int      format = AUDIO_FORMAT_PCM_16_BIT;
    uint32_t channels = AUDIO_CHANNEL_OUT_STEREO;
    uint32_t sampleRate = AFE_PROXY_SAMPLE_RATE;
    status_t status;
    ALOGV("openA2dpOutput");
    struct audio_config config;
    config.sample_rate = AFE_PROXY_SAMPLE_RATE;
    config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    config.format = AUDIO_FORMAT_PCM_16_BIT;

    //TODO : Confirm AUDIO_HARDWARE_MODULE_ID_A2DP ???
    int rc = hw_get_module_by_class(AUDIO_HARDWARE_MODULE_ID/*_A2DP*/, (const char*)"a2dp",
                                    (const hw_module_t**)&mod);
    if (rc) {
        ALOGE("Could not get a2dp hardware module");
        return NO_INIT;
    }

    rc = audio_hw_device_open(mod, &mA2dpDevice);
    if(rc) {
        ALOGE("couldn't open a2dp audio hw device");
        return NO_INIT;
    }
    //TODO: unique id 0?
    status = mA2dpDevice->open_output_stream(mA2dpDevice, 0,((audio_devices_t)(AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP)),
                                    (audio_output_flags_t)AUDIO_OUTPUT_FLAG_NONE, &config, &mA2dpStream);
    if(status != NO_ERROR) {
        ALOGE("Failed to open output stream for a2dp: status %d", status);
    }
    return status;
}

status_t AudioHardwareALSA::closeA2dpOutput()
{
    ALOGV("closeA2dpOutput");
    if(!mA2dpDevice){
        ALOGE("No Aactive A2dp output found");
        return NO_ERROR;
    }

    mA2dpDevice->close_output_stream(mA2dpDevice, mA2dpStream);
    mA2dpStream = NULL;

    audio_hw_device_close(mA2dpDevice);
    mA2dpDevice = NULL;
    return NO_ERROR;
}

status_t AudioHardwareALSA::openUsbOutput()
{
    hw_module_t *mod;
    int      format = AUDIO_FORMAT_PCM_16_BIT;
    uint32_t channels = AUDIO_CHANNEL_OUT_STEREO;
    uint32_t sampleRate = AFE_PROXY_SAMPLE_RATE;
    status_t status;
    ALOGV("openUsbOutput");
    struct audio_config config;
    config.sample_rate = AFE_PROXY_SAMPLE_RATE;
    config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    config.format = AUDIO_FORMAT_PCM_16_BIT;

    int rc = hw_get_module_by_class(AUDIO_HARDWARE_MODULE_ID/*_USB*/, (const char*)"usb",
                                    (const hw_module_t**)&mod);
    if (rc) {
        ALOGE("Could not get usb hardware module");
        return NO_INIT;
    }

    rc = audio_hw_device_open(mod, &mUsbDevice);
    if(rc) {
        ALOGE("couldn't open Usb audio hw device");
        return NO_INIT;
    }

    status = mUsbDevice->open_output_stream(mUsbDevice, 0,((audio_devices_t)(AUDIO_DEVICE_OUT_USB_ACCESSORY)),
                                    (audio_output_flags_t)AUDIO_OUTPUT_FLAG_NONE, &config, &mUsbStream);
    if(status != NO_ERROR) {
        ALOGE("Failed to open output stream for USB: status %d", status);
    }

    return status;
}

status_t AudioHardwareALSA::closeUsbOutput()
{
    ALOGV("closeUsbOutput");
    if(!mUsbDevice){
        ALOGE("No Aactive Usb output found");
        return NO_ERROR;
    }

    mUsbDevice->close_output_stream(mUsbDevice, mUsbStream);
    mUsbStream = NULL;

    audio_hw_device_close(mUsbDevice);
    mUsbDevice = NULL;
    return NO_ERROR;
}

status_t AudioHardwareALSA::stopExtOutThread()
{
    ALOGV("stopExtOutThread");
    status_t err = NO_ERROR;
    if (!mExtOutThreadAlive) {
        ALOGD("Return - thread not live");
        return NO_ERROR;
    }
    mExtOutMutex.lock();
    mKillExtOutThread = true;
    err = mALSADevice->exitReadFromProxy();
    if(err) {
        ALOGE("exitReadFromProxy failed = %d", err);
    }
    mExtOutCv.signal();
    mExtOutMutex.unlock();
    int ret = pthread_join(mExtOutThread,NULL);
    ALOGD("ExtOut thread killed = %d", ret);
    return err;
}

void AudioHardwareALSA::switchExtOut(int device) {

    ALOGV("switchExtOut");
    Mutex::Autolock autolock1(mExtOutMutex);
    if (device & AudioSystem::DEVICE_OUT_ALL_A2DP) {
        mExtOutStream = mA2dpStream;
    } else if (device & AUDIO_DEVICE_OUT_ALL_USB) {
        mExtOutStream = mUsbStream;
    } else {
        mExtOutStream = NULL;
    }
}

status_t AudioHardwareALSA::isExtOutDevice(int device) {
    return ((device & AudioSystem::DEVICE_OUT_ALL_A2DP) ||
            (device & AUDIO_DEVICE_OUT_ALL_USB)) ;
}

void *AudioHardwareALSA::extOutThreadWrapper(void *me) {
    static_cast<AudioHardwareALSA *>(me)->extOutThreadFunc();
    return NULL;
}

void AudioHardwareALSA::extOutThreadFunc() {
    if(!mExtOutStream) {
        ALOGE("No valid External output stream found");
        return;
    }
    if(!mALSADevice->isProxyDeviceOpened()) {
        ALOGE("No valid mProxyPcmHandle found");
        return;
    }

    pid_t tid  = gettid();
    androidSetThreadPriority(tid, ANDROID_PRIORITY_AUDIO);
    prctl(PR_SET_NAME, (unsigned long)"ExtOutThread", 0, 0, 0);

    int ionBufCount = 0;
    int32_t bytesWritten = 0;
    uint32_t numBytesRemaining = 0;
    uint32_t bytesAvailInBuffer = 0;
    uint32_t proxyBufferTime = 0;
    void  *data;
    int err = NO_ERROR;
    ssize_t size = 0;

    mALSADevice->resetProxyVariables();

    ALOGV("mKillExtOutThread = %d", mKillExtOutThread);
    while(!mKillExtOutThread) {

        {
            Mutex::Autolock autolock1(mExtOutMutex);
            if (mKillExtOutThread) {
                break;
            }
            if (!mExtOutStream || !mIsExtOutEnabled ||
                !mALSADevice->isProxyDeviceOpened() ||
                (mALSADevice->isProxyDeviceSuspended()) ||
                (err != NO_ERROR)) {
                ALOGD("ExtOutThreadEntry:: proxy opened = %d,\
                        proxy suspended = %d,err =%d,\
                        mExtOutStream = %p mIsExtOutEnabled = %d",\
                        mALSADevice->isProxyDeviceOpened(),\
                        mALSADevice->isProxyDeviceSuspended(),err,mExtOutStream, mIsExtOutEnabled);
                ALOGD("ExtOutThreadEntry:: Waiting on mExtOutCv");
                mExtOutCv.wait(mExtOutMutex);
                ALOGD("ExtOutThreadEntry:: received signal to wake up");
                mExtOutMutex.unlock();
                continue;
            }
        }
        err = mALSADevice->readFromProxy(&data, &size);
        if(err < 0) {
           ALOGE("ALSADevice readFromProxy returned err = %d,data = %p,\
                    size = %ld", err, data, size);
           continue;
        }

#ifdef OUTPUT_BUFFER_LOG
    if (outputBufferFile1)
    {
        fwrite (data,1,size,outputBufferFile1);
    }
#endif
        void *copyBuffer = data;
        numBytesRemaining = size;
        proxyBufferTime = mALSADevice->mProxyParams.mBufferTime;
        while (err == OK && (numBytesRemaining  > 0) && !mKillExtOutThread
                && mIsExtOutEnabled ) {
            {
                Mutex::Autolock autolock1(mExtOutMutex);
                if(mExtOutStream != NULL ) {

                    bytesAvailInBuffer = mExtOutStream->common.get_buffer_size(&mExtOutStream->common);
                    uint32_t writeLen = bytesAvailInBuffer > numBytesRemaining ?
                                    numBytesRemaining : bytesAvailInBuffer;
                    ALOGV("Writing %d bytes to External Output ", writeLen);
                    bytesWritten = mExtOutStream->write(mExtOutStream,copyBuffer, writeLen);
                } else {
                    ALOGV(" No External output to write  ");
                    usleep(proxyBufferTime*1000);
                    bytesWritten = numBytesRemaining;
                }
            }
            //If the write fails make this thread sleep and let other
            //thread (eg: stopA2DP) to acquire lock to prevent a deadlock.
            if(bytesWritten == -1) {
                ALOGV("bytesWritten = %d",bytesWritten);
                usleep(10000);
                break;
            }
            //Need to check warning here - void used in arithmetic
            copyBuffer = (char *)copyBuffer + bytesWritten;
            numBytesRemaining -= bytesWritten;
            ALOGV("@_@bytes To write2:%d",numBytesRemaining);
        }
    }

    mALSADevice->resetProxyVariables();
    mExtOutThreadAlive = false;
    ALOGD("ExtOut Thread is dying");
}

void AudioHardwareALSA::setExtOutActiveUseCases_l(uint32_t activeUsecase)
{
   mExtOutActiveUseCases |= activeUsecase;
   ALOGV("mExtOutActiveUseCases = %u, activeUsecase = %u", mExtOutActiveUseCases, activeUsecase);
}

uint32_t AudioHardwareALSA::getExtOutActiveUseCases_l()
{
   ALOGV("getExtOutActiveUseCases_l: mExtOutActiveUseCases = %u", mExtOutActiveUseCases);
   return mExtOutActiveUseCases;
}

void AudioHardwareALSA::clearExtOutActiveUseCases_l(uint32_t activeUsecase) {

   mExtOutActiveUseCases &= ~activeUsecase;
   ALOGV("clear - mExtOutActiveUseCases = %u, activeUsecase = %u", mExtOutActiveUseCases, activeUsecase);

}

uint32_t AudioHardwareALSA::useCaseStringToEnum(const char *usecase)
{
   ALOGV("useCaseStringToEnum usecase:%s",usecase);
   uint32_t activeUsecase = USECASE_NONE;

   if ((!strncmp(usecase, SND_USE_CASE_VERB_HIFI_LOW_POWER,
                    strlen(SND_USE_CASE_VERB_HIFI_LOW_POWER))) ||
       (!strncmp(usecase, SND_USE_CASE_MOD_PLAY_LPA,
                    strlen(SND_USE_CASE_MOD_PLAY_LPA)))) {
       activeUsecase = USECASE_HIFI_LOW_POWER;
   } else if ((!strncmp(usecase, SND_USE_CASE_VERB_HIFI_TUNNEL,
                           strlen(SND_USE_CASE_VERB_HIFI_TUNNEL))) ||
              (!strncmp(usecase, SND_USE_CASE_MOD_PLAY_TUNNEL,
                           strlen(SND_USE_CASE_MOD_PLAY_TUNNEL)))) {
       activeUsecase = USECASE_HIFI_TUNNEL;
   } else if ((!strncmp(usecase, SND_USE_CASE_VERB_HIFI_LOWLATENCY_MUSIC,
                           strlen(SND_USE_CASE_VERB_HIFI_LOWLATENCY_MUSIC )))||
               (!strncmp(usecase, SND_USE_CASE_MOD_PLAY_LOWLATENCY_MUSIC,
                           strlen(SND_USE_CASE_MOD_PLAY_LOWLATENCY_MUSIC)))) {
       activeUsecase = USECASE_HIFI_LOWLATENCY;
   } else if ((!strncmp(usecase, SND_USE_CASE_VERB_DIGITAL_RADIO,
                           strlen(SND_USE_CASE_VERB_DIGITAL_RADIO))) ||
               (!strncmp(usecase, SND_USE_CASE_MOD_PLAY_FM,
                           strlen(SND_USE_CASE_MOD_PLAY_FM)))||
               (!strncmp(usecase, SND_USE_CASE_VERB_FM_REC,
                           strlen(SND_USE_CASE_VERB_FM_REC)))||
               (!strncmp(usecase, SND_USE_CASE_MOD_CAPTURE_FM,
                           strlen(SND_USE_CASE_MOD_CAPTURE_FM)))){
       activeUsecase = USECASE_FM;
    } else if ((!strncmp(usecase, SND_USE_CASE_VERB_HIFI,
                           strlen(SND_USE_CASE_VERB_HIFI)))||
               (!strncmp(usecase, SND_USE_CASE_MOD_PLAY_MUSIC,
                           strlen(SND_USE_CASE_MOD_PLAY_MUSIC)))) {
       activeUsecase = USECASE_HIFI;
    }
    return activeUsecase;
}

bool  AudioHardwareALSA::suspendPlaybackOnExtOut(uint32_t activeUsecase) {

    Mutex::Autolock autoLock(mLock);
    suspendPlaybackOnExtOut_l(activeUsecase);
    return NO_ERROR;
}

bool  AudioHardwareALSA::suspendPlaybackOnExtOut_l(uint32_t activeUsecase) {

    Mutex::Autolock autolock1(mExtOutMutex);
    ALOGV("suspendPlaybackOnExtOut_l activeUsecase = %d, mRouteAudioToExtOut = %d",\
            activeUsecase, mRouteAudioToExtOut);
    clearExtOutActiveUseCases_l(activeUsecase);
    if((!getExtOutActiveUseCases_l()) && mIsExtOutEnabled )
        return mALSADevice->suspendProxy();
    return NO_ERROR;
}

}       // namespace android_audio_legacy
