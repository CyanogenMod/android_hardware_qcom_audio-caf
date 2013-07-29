/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (c) 2009-2012, The Linux Foundation. All rights reserved.
 * Copyright (c) 2012-2013, The CyanogenMod Project
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "AudioPolicyManager7x30"
//#define LOG_NDDEBUG 0

//#define VERY_VERBOSE_LOGGING
#ifdef VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

// A device mask for all audio input devices that are considered "virtual" when evaluating
// active inputs in getActiveInput()
#define APM_AUDIO_IN_DEVICE_VIRTUAL_ALL  AUDIO_DEVICE_IN_REMOTE_SUBMIX

#include <utils/Log.h>
#include "AudioPolicyManager.h"
#include <hardware/audio_effect.h>
#include <media/mediarecorder.h>
#include <hardware/audio.h>
#include <math.h>
#include <hardware_legacy/audio_policy_conf.h>

namespace android_audio_legacy {



// ----------------------------------------------------------------------------
// AudioPolicyManager for msm7x30 platform
// ----------------------------------------------------------------------------
audio_devices_t AudioPolicyManager::getDeviceForStrategy(routing_strategy strategy, bool fromCache)
{
    uint32_t device = 0;

    if (fromCache) {
        ALOGV("getDeviceForStrategy() from cache strategy %d, device %x",
              strategy, mDeviceForStrategy[strategy]);
        return mDeviceForStrategy[strategy];
    }

    switch (strategy) {

    case STRATEGY_SONIFICATION_RESPECTFUL:
        if (isInCall()) {
            device = getDeviceForStrategy(STRATEGY_SONIFICATION, false /*fromCache*/);
        } else if (isStreamActive(AudioSystem::MUSIC, SONIFICATION_RESPECTFUL_AFTER_MUSIC_DELAY)) {
            // while media is playing (or has recently played), use the same device
            device = getDeviceForStrategy(STRATEGY_MEDIA, false /*fromCache*/);
        } else {
            // when media is not playing anymore, fall back on the sonification behavior
            device = getDeviceForStrategy(STRATEGY_SONIFICATION, false /*fromCache*/);
        }

        break;

    case STRATEGY_DTMF:
        if (!isInCall()) {
            // when off call, DTMF strategy follows the same rules as MEDIA strategy
            device = getDeviceForStrategy(STRATEGY_MEDIA, false /*fromCache*/);
            break;
        }
        // when in call, DTMF and PHONE strategies follow the same rules
        // FALL THROUGH

    case STRATEGY_PHONE:
        // for phone strategy, we first consider the forced use and then the available devices by order
        // of priority
        switch (mForceUse[AudioSystem::FOR_COMMUNICATION]) {
        case AudioSystem::FORCE_BT_SCO:
            if (!isInCall() || strategy != STRATEGY_DTMF) {
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT;
                if (device) break;
            }
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO;
            if (device) break;
            // if SCO device is requested but no SCO device is available, fall back to default case
            // FALL THROUGH

        default:    // FORCE_NONE
            // when not in a phone call, phone strategy should route STREAM_VOICE_CALL to A2DP
            if (mHasA2dp && !isInCall() &&
                    (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_NO_BT_A2DP) &&
                    (getA2dpOutput() != 0) && !mA2dpSuspended) {
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP;
                if (device) break;
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
                if (device) break;
            }

            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_DEVICE;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_EARPIECE;
            if (device) break;
            device = mDefaultOutputDevice;
            if (device == 0) {
                ALOGE("getDeviceForStrategy() no device found for STRATEGY_PHONE");
            }
            break;

        case AudioSystem::FORCE_SPEAKER:
            // when not in a phone call, phone strategy should route STREAM_VOICE_CALL to
            // A2DP speaker when forcing to speaker output
            if (mHasA2dp && !isInCall() &&
                    (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_NO_BT_A2DP) &&
                    (getA2dpOutput() != 0) && !mA2dpSuspended) {
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
                if (device) break;
            }
            device = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_DEVICE;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            if (device) break;
            device = mDefaultOutputDevice;
            if (device == 0) {
                ALOGE("getDeviceForStrategy() no device found for STRATEGY_PHONE, FORCE_SPEAKER");
            }
            break;
        }
#ifdef QCOM_FM_ENABLED
        if (mAvailableOutputDevices & AudioSystem::DEVICE_OUT_FM) {
            device |= AudioSystem::DEVICE_OUT_FM;
            if (mForceUse[AudioSystem::FOR_MEDIA] == AudioSystem::FORCE_SPEAKER) {
                device &= ~(AudioSystem::DEVICE_OUT_WIRED_HEADSET);
                device |= AudioSystem::DEVICE_OUT_SPEAKER;
            }
        }
#endif
    break;

    case STRATEGY_SONIFICATION:

        // If incall, just select the STRATEGY_PHONE device: The rest of the behavior is handled by
        // handleIncallSonification().
        if (isInCall()) {
            device = getDeviceForStrategy(STRATEGY_PHONE, false /*fromCache*/);
            break;
        }
        // FALL THROUGH

    case STRATEGY_ENFORCED_AUDIBLE:
        // strategy STRATEGY_ENFORCED_AUDIBLE uses same routing policy as STRATEGY_SONIFICATION
        // except:
        //   - when in call where it doesn't default to STRATEGY_PHONE behavior
        //   - in countries where not enforced in which case it follows STRATEGY_MEDIA

        if (strategy == STRATEGY_SONIFICATION ||
                !mStreams[AUDIO_STREAM_ENFORCED_AUDIBLE].mCanBeMuted) {
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            if (device == 0) {
                ALOGE("getDeviceForStrategy() speaker device not found for STRATEGY_SONIFICATION");
            }
        }
        // The second device used for sonification is the same as the device used by media strategy
        // FALL THROUGH

    case STRATEGY_MEDIA: {
        //To route FM stream to speaker when headset is connected, a new switch case is added.
        //case AudioSystem::FORCE_SPEAKER for STRATEGY_MEDIA will come only when we need to route
        //FM stream to speaker.
        switch (mForceUse[AudioSystem::FOR_MEDIA]) {
        default:{
        uint32_t device2 = 0;
        if ((mHasA2dp && (mForceUse[AudioSystem::FOR_MEDIA] != AudioSystem::FORCE_NO_BT_A2DP) &&
                (getA2dpOutput() != 0) && !mA2dpSuspended )) {
            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP;
            if (device2 == 0) {
                device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
            }
            if (device2 == 0) {
                device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
            }
        }
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE;
        }
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET;
        }
#ifdef QCOM_FM_ENABLED
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_FM_TX;
        }
#endif
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_ACCESSORY;
        }
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AUDIO_DEVICE_OUT_USB_DEVICE;
        }
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_DGTL_DOCK_HEADSET;
        }
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_AUX_DIGITAL;
        }
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_ANLG_DOCK_HEADSET;
        }
        if (device2 == 0) {
            device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
        }

        // device is DEVICE_OUT_SPEAKER if we come from case STRATEGY_SONIFICATION or
        // STRATEGY_ENFORCED_AUDIBLE, 0 otherwise
        device |= device2;
        if (device) break;
        device = mDefaultOutputDevice;
      }
        break;

        case AudioSystem::FORCE_SPEAKER:
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            break;
        }
#ifdef QCOM_FM_ENABLED
        if (mAvailableOutputDevices & AudioSystem::DEVICE_OUT_FM) {
            device |= AudioSystem::DEVICE_OUT_FM;
        }
#endif
        // Do not play media stream if in call and the requested device would change the hardware
        // output routing
        if (mPhoneState == AudioSystem::MODE_IN_CALL &&
            !AudioSystem::isA2dpDevice((AudioSystem::audio_devices)device) &&
            device != getDeviceForStrategy(STRATEGY_PHONE)) {
            device = getDeviceForStrategy(STRATEGY_PHONE);
            ALOGV("getDeviceForStrategy() incompatible media and phone devices");
        }
        } break;

    default:
        ALOGW("getDeviceForStrategy() unknown strategy: %d", strategy);
        break;
    }

    ALOGVV("getDeviceForStrategy() strategy %d, device %x", strategy, device);

    return (audio_devices_t)device;
}

status_t AudioPolicyManager::setDeviceConnectionState(audio_devices_t device,
                                                  AudioSystem::device_connection_state state,
                                                  const char *device_address)
{
    SortedVector <audio_io_handle_t> outputs;

    ALOGV("setDeviceConnectionState() device: %x, state %d, address %s", device, state, device_address);

    // connect/disconnect only 1 device at a time
    if (!audio_is_output_device(device) && !audio_is_input_device(device)) return BAD_VALUE;

    if (strlen(device_address) >= MAX_DEVICE_ADDRESS_LEN) {
        ALOGE("setDeviceConnectionState() invalid address: %s", device_address);
        return BAD_VALUE;
    }

    // handle output devices
    if (audio_is_output_device(device)) {

        if (!mHasA2dp && audio_is_a2dp_device(device)) {
            ALOGE("setDeviceConnectionState() invalid A2DP device: %x", device);
            return BAD_VALUE;
        }
        if (!mHasUsb && audio_is_usb_device(device)) {
            ALOGE("setDeviceConnectionState() invalid USB audio device: %x", device);
            return BAD_VALUE;
        }
        if (!mHasRemoteSubmix && audio_is_remote_submix_device((audio_devices_t)device)) {
            ALOGE("setDeviceConnectionState() invalid remote submix audio device: %x", device);
            return BAD_VALUE;
        }

        // save a copy of the opened output descriptors before any output is opened or closed
        // by checkOutputsForDevice(). This will be needed by checkOutputForAllStrategies()
        mPreviousOutputs = mOutputs;
        switch (state)
        {
        // handle output device connection
        case AudioSystem::DEVICE_STATE_AVAILABLE:

            if (mAvailableOutputDevices & device) {
                ALOGW("setDeviceConnectionState() device already connected: %x", device);
                return INVALID_OPERATION;
            }
            ALOGV("setDeviceConnectionState() connecting device %x", device);

            if (checkOutputsForDevice(device, state, outputs) != NO_ERROR) {
                return INVALID_OPERATION;
            }
            ALOGV("setDeviceConnectionState() checkOutputsForDevice() returned %d outputs",
                  outputs.size());
            // register new device as available
            mAvailableOutputDevices = (audio_devices_t)(mAvailableOutputDevices | device);
            ALOGV("setDeviceConnectionState() connecting device %x", mAvailableOutputDevices);

            if (!outputs.isEmpty()) {
                String8 paramStr;
                if (mHasA2dp && audio_is_a2dp_device(device)) {
                    // handle A2DP device connection
                    AudioParameter param;
                    param.add(String8(AUDIO_PARAMETER_A2DP_SINK_ADDRESS), String8(device_address));
                    paramStr = param.toString();
                    mA2dpDeviceAddress = String8(device_address, MAX_DEVICE_ADDRESS_LEN);
                    mA2dpSuspended = false;
                } else if (audio_is_bluetooth_sco_device(device)) {
                    // handle SCO device connection
                    mScoDeviceAddress = String8(device_address, MAX_DEVICE_ADDRESS_LEN);
                } else if (mHasUsb && audio_is_usb_device(device)) {
                    // handle USB device connection
                    mUsbCardAndDevice = String8(device_address, MAX_DEVICE_ADDRESS_LEN);
                    paramStr = mUsbCardAndDevice;
                }
                if (!paramStr.isEmpty()) {
                    for (size_t i = 0; i < outputs.size(); i++) {
                        mpClientInterface->setParameters(outputs[i], paramStr);
                    }
                }
            }
            break;
        // handle output device disconnection
        case AudioSystem::DEVICE_STATE_UNAVAILABLE: {
            if (!(mAvailableOutputDevices & device)) {
                ALOGW("setDeviceConnectionState() device not connected: %x", device);
                return INVALID_OPERATION;
            }

            ALOGV("setDeviceConnectionState() disconnecting device %x", device);
            // remove device from available output devices
            mAvailableOutputDevices = (audio_devices_t)(mAvailableOutputDevices & ~device);

            checkOutputsForDevice(device, state, outputs);
            if (mHasA2dp && audio_is_a2dp_device(device)) {
                // handle A2DP device disconnection
                mA2dpDeviceAddress = "";
                mA2dpSuspended = false;
            } else if (audio_is_bluetooth_sco_device(device)) {
                // handle SCO device disconnection
                mScoDeviceAddress = "";
            } else if (mHasUsb && audio_is_usb_device(device)) {
                // handle USB device disconnection
                mUsbCardAndDevice = "";
            }
            } break;

        default:
            ALOGE("setDeviceConnectionState() invalid state: %x", state);
            return BAD_VALUE;
        }

#ifdef QCOM_FM_ENABLED
        audio_devices_t NewDevice = AudioPolicyManagerBase::getNewDevice(mPrimaryOutput, false /*fromCache*/);
        if (device == AudioSystem::DEVICE_OUT_FM) {
            if (state == AudioSystem::DEVICE_STATE_AVAILABLE) {
                mOutputs.valueFor(mPrimaryOutput)->changeRefCount(AudioSystem::FM, 1);
            }
            else {
                mOutputs.valueFor(mPrimaryOutput)->changeRefCount(AudioSystem::FM, -1);
            }
        }
#endif

        checkA2dpSuspend();
        AudioPolicyManagerBase::checkOutputForAllStrategies();
        // outputs must be closed after checkOutputForAllStrategies() is executed
        if (!outputs.isEmpty()) {
            for (size_t i = 0; i < outputs.size(); i++) {
                // close unused outputs after device disconnection or direct outputs that have been
                // opened by checkOutputsForDevice() to query dynamic parameters
                if ((state == AudioSystem::DEVICE_STATE_UNAVAILABLE) ||
                        (mOutputs.valueFor(outputs[i])->mFlags & AUDIO_OUTPUT_FLAG_DIRECT)) {
                    closeOutput(outputs[i]);
                }
            }
        }

        updateDevicesAndOutputs();
        for (size_t i = 0; i < mOutputs.size(); i++) {
            setOutputDevice(mOutputs.keyAt(i),
                            getNewDevice(mOutputs.keyAt(i), true /*fromCache*/),
                            true,
                            0);
        }

        if (device == AUDIO_DEVICE_OUT_WIRED_HEADSET) {
            device = AUDIO_DEVICE_IN_WIRED_HEADSET;
        } else if (device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO ||
                   device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET ||
                   device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            device = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else {
            return NO_ERROR;
        }
    }
    // handle input devices
    if (audio_is_input_device(device)) {

        switch (state)
        {
        // handle input device connection
        case AudioSystem::DEVICE_STATE_AVAILABLE: {
            if (mAvailableInputDevices & device) {
                ALOGW("setDeviceConnectionState() device already connected: %d", device);
                return INVALID_OPERATION;
            }
            mAvailableInputDevices = mAvailableInputDevices | (device & ~AUDIO_DEVICE_BIT_IN);
            }
            break;

        // handle input device disconnection
        case AudioSystem::DEVICE_STATE_UNAVAILABLE: {
            if (!(mAvailableInputDevices & device)) {
                ALOGW("setDeviceConnectionState() device not connected: %d", device);
                return INVALID_OPERATION;
            }
            mAvailableInputDevices = (audio_devices_t) (mAvailableInputDevices & ~device);
            } break;

        default:
            ALOGE("setDeviceConnectionState() invalid state: %x", state);
            return BAD_VALUE;
        }

        audio_io_handle_t activeInput = getActiveInput();
        if (activeInput != 0) {
            AudioInputDescriptor *inputDesc = mInputs.valueFor(activeInput);
            audio_devices_t newDevice = getDeviceForInputSource(inputDesc->mInputSource);
            if ((newDevice != AUDIO_DEVICE_NONE) && (newDevice != inputDesc->mDevice)) {
                ALOGV("setDeviceConnectionState() changing device from %x to %x for input %d",
                        inputDesc->mDevice, newDevice, activeInput);
                inputDesc->mDevice = newDevice;
                AudioParameter param = AudioParameter();
                param.addInt(String8(AudioParameter::keyRouting), (int)newDevice);
                mpClientInterface->setParameters(activeInput, param.toString());
            }
        }

        return NO_ERROR;
    }

    ALOGW("setDeviceConnectionState() invalid device: %x", device);
    return BAD_VALUE;
}

AudioSystem::device_connection_state AudioPolicyManager::getDeviceConnectionState(audio_devices_t device,
                                                  const char *device_address)
{
    AudioSystem::device_connection_state state = AudioSystem::DEVICE_STATE_UNAVAILABLE;
    String8 address = String8(device_address);
    if (audio_is_output_device(device)) {
        if (device & mAvailableOutputDevices) {
            if (audio_is_a2dp_device(device) &&
                (!mHasA2dp || (address != "" && mA2dpDeviceAddress != address))) {
                return state;
            }
            if (audio_is_bluetooth_sco_device(device) &&
                address != "" && mScoDeviceAddress != address) {
                return state;
            }
            if (audio_is_usb_device(device) &&
                (!mHasUsb || (address != "" && mUsbCardAndDevice != address))) {
                ALOGE("getDeviceConnectionState() invalid device: %x", device);
                return state;
            }
            if (audio_is_remote_submix_device((audio_devices_t)device) && !mHasRemoteSubmix) {
                return state;
            }
            state = AudioSystem::DEVICE_STATE_AVAILABLE;
        }
    } else if (audio_is_input_device(device)) {
        if (device & mAvailableInputDevices) {
            state = AudioSystem::DEVICE_STATE_AVAILABLE;
        }
    }

    return state;
}

void AudioPolicyManager::setForceUse(AudioSystem::force_use usage, AudioSystem::forced_config config)
{
    ALOGV("setForceUse() usage %d, config %d, mPhoneState %d", usage, config, mPhoneState);

    bool forceVolumeReeval = false;
    switch(usage) {
    case AudioSystem::FOR_COMMUNICATION:
        if (config != AudioSystem::FORCE_SPEAKER && config != AudioSystem::FORCE_BT_SCO &&
            config != AudioSystem::FORCE_NONE) {
            ALOGW("setForceUse() invalid config %d for FOR_COMMUNICATION", config);
            return;
        }
        forceVolumeReeval = true;
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_MEDIA:
        if (config != AudioSystem::FORCE_HEADPHONES && config != AudioSystem::FORCE_BT_A2DP &&
            config != AudioSystem::FORCE_WIRED_ACCESSORY &&
            config != AudioSystem::FORCE_ANALOG_DOCK &&
            config != AudioSystem::FORCE_DIGITAL_DOCK && config != AudioSystem::FORCE_NONE &&
            config != AudioSystem::FORCE_NO_BT_A2DP) {
            ALOGW("setForceUse() invalid config %d for FOR_MEDIA", config);
            return;
        }
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_RECORD:
        if (config != AudioSystem::FORCE_BT_SCO && config != AudioSystem::FORCE_WIRED_ACCESSORY &&
            config != AudioSystem::FORCE_NONE) {
            ALOGW("setForceUse() invalid config %d for FOR_RECORD", config);
            return;
        }
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_DOCK:
        if (config != AudioSystem::FORCE_NONE && config != AudioSystem::FORCE_BT_CAR_DOCK &&
            config != AudioSystem::FORCE_BT_DESK_DOCK &&
            config != AudioSystem::FORCE_WIRED_ACCESSORY &&
            config != AudioSystem::FORCE_ANALOG_DOCK &&
            config != AudioSystem::FORCE_DIGITAL_DOCK) {
            ALOGW("setForceUse() invalid config %d for FOR_DOCK", config);
        }
        forceVolumeReeval = true;
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_SYSTEM:
        if (config != AudioSystem::FORCE_NONE &&
            config != AudioSystem::FORCE_SYSTEM_ENFORCED) {
            ALOGW("setForceUse() invalid config %d for FOR_SYSTEM", config);
        }
        forceVolumeReeval = true;
        mForceUse[usage] = config;
        break;
    default:
        ALOGW("setForceUse() invalid usage %d", usage);
        break;
    }

    // check for device and output changes triggered by new force usage
    checkA2dpSuspend();
    checkOutputForAllStrategies();
    updateDevicesAndOutputs();
    for (size_t i = 0; i < mOutputs.size(); i++) {
        audio_io_handle_t output = mOutputs.keyAt(i);
        audio_devices_t newDevice = getNewDevice(output, true /*fromCache*/);
        setOutputDevice(output, newDevice, (newDevice != AUDIO_DEVICE_NONE));
        if (forceVolumeReeval && (newDevice != AUDIO_DEVICE_NONE)) {
            applyStreamVolumes(output, newDevice, 0, true);
        }
    }

    audio_io_handle_t activeInput = getActiveInput();
    if (activeInput != 0) {
        AudioInputDescriptor *inputDesc = mInputs.valueFor(activeInput);
        audio_devices_t newDevice = getDeviceForInputSource(inputDesc->mInputSource);
        if ((newDevice != AUDIO_DEVICE_NONE) && (newDevice != inputDesc->mDevice)) {
            ALOGV("setForceUse() changing device from %x to %x for input %d",
                    inputDesc->mDevice, newDevice, activeInput);
            inputDesc->mDevice = newDevice;
            AudioParameter param = AudioParameter();
            param.addInt(String8(AudioParameter::keyRouting), (int)newDevice);
            mpClientInterface->setParameters(activeInput, param.toString());
        }
    }

}

status_t AudioPolicyManager::startOutput(audio_io_handle_t output,
                                             AudioSystem::stream_type stream,
                                             int session)
{
    ALOGV("startOutput() output %d, stream %d, session %d", output, stream, session);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        ALOGW("startOutput() unknow output %d", output);
        return BAD_VALUE;
    }

    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);

    // increment usage count for this stream on the requested output:
    // NOTE that the usage count is the same for duplicated output and hardware output which is
    // necessary for a correct control of hardware output routing by startOutput() and stopOutput()
    outputDesc->changeRefCount(stream, 1);

    if (outputDesc->mRefCount[stream] == 1) {
        audio_devices_t prevDevice = outputDesc->device();
        audio_devices_t newDevice = AudioPolicyManagerBase::getNewDevice(output, false /*fromCache*/);
        routing_strategy strategy = AudioPolicyManagerBase::getStrategy(stream);
        bool shouldWait = (strategy == STRATEGY_SONIFICATION) ||
                            (strategy == STRATEGY_SONIFICATION_RESPECTFUL);
        uint32_t waitMs = 0;
        bool force = false;
        uint32_t muteWaitMs;
        for (size_t i = 0; i < mOutputs.size(); i++) {
            AudioOutputDescriptor *desc = mOutputs.valueAt(i);
            if (desc != outputDesc) {
                // force a device change if any other output is managed by the same hw
                // module and has a current device selection that differs from selected device.
                // In this case, the audio HAL must receive the new device selection so that it can
                // change the device currently selected by the other active output.
                if (outputDesc->sharesHwModuleWith(desc) &&
                    desc->device() != newDevice) {
                    force = true;
                }
                // wait for audio on other active outputs to be presented when starting
                // a notification so that audio focus effect can propagate.
                if (shouldWait && (desc->refCount() != 0) && (waitMs < desc->latency())) {
                    waitMs = desc->latency();
                }
            }
        }
#ifdef QCOM_FM_ENABLED
    if(stream == AudioSystem::FM && output == getA2dpOutput()) {
        setOutputDevice(output, AudioPolicyManagerBase::getNewDevice((output), true));
    } else
#endif
    {
        setOutputDevice(output, AudioPolicyManagerBase::getNewDevice((output), true));
    }

        // handle special case for sonification while in call
        if (isInCall()) {
            AudioPolicyManagerBase::handleIncallSonification(stream, true, false);
        }

        // apply volume rules for current stream and device if necessary
        checkAndSetVolume(stream,
                          mStreams[stream].getVolumeIndex(newDevice),
                          output,
                          newDevice);

        // update the outputs if starting an output with a stream that can affect notification
        // routing
        handleNotificationRoutingForStream(stream);
        if (waitMs > muteWaitMs) {
            usleep((waitMs - muteWaitMs) * 2 * 1000);
        }
    }
    return NO_ERROR;
}
status_t AudioPolicyManager::stopOutput(audio_io_handle_t output, AudioSystem::stream_type stream, int session)
{
    ALOGV("stopOutput() output %d, stream %d, session %d", output, stream, session);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        ALOGW("stopOutput() unknow output %d", output);
        return BAD_VALUE;
    }

    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);

    // handle special case for sonification while in call
    if (isInCall()) {
        handleIncallSonification(stream, false, false);
    }

    if (outputDesc->mRefCount[stream] > 0) {
        // decrement usage count of this stream on the output
        outputDesc->changeRefCount(stream, -1);
        // store time at which the stream was stopped - see isStreamActive()
        if (outputDesc->mRefCount[stream] == 0) {
            if (stream == AudioSystem::MUSIC) {
                outputDesc->mStopTime[stream] = systemTime();
            }
            audio_devices_t newDevice = getNewDevice(output, false /*fromCache*/);
            // delay the device switch by twice the latency because stopOutput() is executed when
            // the track stop() command is received and at that time the audio track buffer can
            // still contain data that needs to be drained. The latency only covers the audio HAL
            // and kernel buffers. Also the latency does not always include additional delay in the
            // audio path (audio DSP, CODEC ...)
            setOutputDevice(output, newDevice, false, outputDesc->mLatency*2);

            // force restoring the device selection on other active outputs if it differs from the
            // one being selected for this output
            for (size_t i = 0; i < mOutputs.size(); i++) {
                audio_io_handle_t curOutput = mOutputs.keyAt(i);
                AudioOutputDescriptor *desc = mOutputs.valueAt(i);
                if (curOutput != output &&
                        desc->refCount() != 0 &&
                        outputDesc->sharesHwModuleWith(desc) &&
                        newDevice != desc->device()) {
                    setOutputDevice(curOutput,
                                    getNewDevice(curOutput, false /*fromCache*/),
                                    true,
                                    outputDesc->mLatency*2);
                }
            }
            // update the outputs if stopping one with a stream that can affect notification routing
            handleNotificationRoutingForStream(stream);
        }
        return NO_ERROR;
    } else {
        ALOGW("stopOutput() refcount is already 0 for output %d", output);
        return INVALID_OPERATION;
    }
}

// ----------------------------------------------------------------------------
// AudioPolicyManager
// ----------------------------------------------------------------------------

// ---  class factory


extern "C" AudioPolicyInterface* createAudioPolicyManager(AudioPolicyClientInterface *clientInterface)
{
    return new AudioPolicyManager(clientInterface);
}

extern "C" void destroyAudioPolicyManager(AudioPolicyInterface *interface)
{
    delete interface;
}
uint32_t AudioPolicyManager::checkDeviceMuteStrategies(AudioOutputDescriptor *outputDesc,
                                                       audio_devices_t prevDevice,
                                                       uint32_t delayMs)
{
    // mute/unmute strategies using an incompatible device combination
    // if muting, wait for the audio in pcm buffer to be drained before proceeding
    // if unmuting, unmute only after the specified delay
    if (outputDesc->isDuplicated()) {
        return 0;
    }

    uint32_t muteWaitMs = 0;
    audio_devices_t device = outputDesc->device();
    bool shouldMute = (outputDesc->refCount() != 0) &&
                    (AudioSystem::popCount(device) >= 2);
    // temporary mute output if device selection changes to avoid volume bursts due to
    // different per device volumes
    bool tempMute = (outputDesc->refCount() != 0) && (device != prevDevice);

    for (size_t i = 0; i < NUM_STRATEGIES; i++) {
        audio_devices_t curDevice = getDeviceForStrategy((routing_strategy)i, false /*fromCache*/);
        bool mute = shouldMute && (curDevice & device) && (curDevice != device);
        bool doMute = false;

        if (mute && !outputDesc->mStrategyMutedByDevice[i]) {
            doMute = true;
            outputDesc->mStrategyMutedByDevice[i] = true;
        } else if (!mute && outputDesc->mStrategyMutedByDevice[i]){
            doMute = true;
            outputDesc->mStrategyMutedByDevice[i] = false;
        }
        if (doMute || tempMute) {
            for (size_t j = 0; j < mOutputs.size(); j++) {
                AudioOutputDescriptor *desc = mOutputs.valueAt(j);
                if ((desc->supportedDevices() & outputDesc->supportedDevices())
                        == AUDIO_DEVICE_NONE) {
                    continue;
                }
                audio_io_handle_t curOutput = mOutputs.keyAt(j);
                ALOGVV("checkDeviceMuteStrategies() %s strategy %d (curDevice %04x) on output %d",
                      mute ? "muting" : "unmuting", i, curDevice, curOutput);
                setStrategyMute((routing_strategy)i, mute, curOutput, mute ? 0 : delayMs * 4);
                if (desc->strategyRefCount((routing_strategy)i) != 0) {
                    if (tempMute) {
                        setStrategyMute((routing_strategy)i, true, curOutput);
                        setStrategyMute((routing_strategy)i, false, curOutput,
                                            desc->latency() * 4, device);
                    }
                    if (tempMute || mute) {
                        if (muteWaitMs < desc->latency()) {
                            muteWaitMs = desc->latency();
                        }
                    }
                }
            }
        }
    }

    // FIXME: should not need to double latency if volume could be applied immediately by the
    // audioflinger mixer. We must account for the delay between now and the next time
    // the audioflinger thread for this output will process a buffer (which corresponds to
    // one buffer size, usually 1/2 or 1/4 of the latency).
    muteWaitMs *= 2;
    // wait for the PCM output buffers to empty before proceeding with the rest of the command
    if (muteWaitMs > delayMs) {
        muteWaitMs -= delayMs;
        usleep(muteWaitMs * 1000);
        return muteWaitMs;
    }
    return 0;
}

// ---

uint32_t AudioPolicyManager::setOutputDevice(audio_io_handle_t output, audio_devices_t device, bool force, int delayMs)
{
    ALOGV("setOutputDevice() output %d device %04x delayMs %d", output, device, delayMs);
    AudioOutputDescriptor *outputDesc = mOutputs.valueFor(output);
    AudioParameter param;
    uint32_t muteWaitMs = 0;

    if (outputDesc->isDuplicated()) {
        muteWaitMs = setOutputDevice(outputDesc->mOutput1->mId, device, force, delayMs);
        muteWaitMs += setOutputDevice(outputDesc->mOutput2->mId, device, force, delayMs);
        return muteWaitMs;
    }
    // filter devices according to output selected
    device = (audio_devices_t)(device & outputDesc->mProfile->mSupportedDevices);

    audio_devices_t prevDevice = outputDesc->mDevice;

    ALOGV("setOutputDevice() prevDevice %04x", prevDevice);

    if (device != 0) {
        outputDesc->mDevice = device;
    }
    muteWaitMs = checkDeviceMuteStrategies(outputDesc, prevDevice, delayMs);

    // Do not change the routing if:
    //  - the requested device is 0
    //  - the requested device is the same as current device and force is not specified.
    // Doing this check here allows the caller to call setOutputDevice() without conditions
    if ((device == 0 || device == prevDevice) && !force) {
        ALOGV("setOutputDevice() setting same device %04x or null device for output %d", device, output);
        return muteWaitMs;
    }

    ALOGV("setOutputDevice() changing device");
    // do the routing
    param.addInt(String8(AudioParameter::keyRouting), (int)device);
    mpClientInterface->setParameters(output, param.toString(), delayMs);

    // update stream volumes according to new device
    applyStreamVolumes(output, device, delayMs);

    return muteWaitMs;
}
status_t AudioPolicyManager::checkAndSetVolume(int stream, int index, audio_io_handle_t output, audio_devices_t device, int delayMs, bool force)
{
    // do not change actual stream volume if the stream is muted
    if (mOutputs.valueFor(output)->mMuteCount[stream] != 0) {
        ALOGVV("checkAndSetVolume() stream %d muted count %d",
              stream, mOutputs.valueFor(output)->mMuteCount[stream]);
        return NO_ERROR;
    }

    // do not change in call volume if bluetooth is connected and vice versa
    if ((stream == AudioSystem::VOICE_CALL && mForceUse[AudioSystem::FOR_COMMUNICATION] == AudioSystem::FORCE_BT_SCO) ||
        (stream == AudioSystem::BLUETOOTH_SCO && mForceUse[AudioSystem::FOR_COMMUNICATION] != AudioSystem::FORCE_BT_SCO)) {
        ALOGV("checkAndSetVolume() cannot set stream %d volume with force use = %d for comm",
             stream, mForceUse[AudioSystem::FOR_COMMUNICATION]);
        return INVALID_OPERATION;
    }

    float volume = computeVolume(stream, index, output, device);
    // We actually change the volume if:
    // - the float value returned by computeVolume() changed
    // - the force flag is set
    if (volume != mOutputs.valueFor(output)->mCurVolume[stream]
#ifdef QCOM_FM_ENABLED
            || (stream == AudioSystem::FM)
#endif
            || force) {
        mOutputs.valueFor(output)->mCurVolume[stream] = volume;
        ALOGVV("checkAndSetVolume() for output %d stream %d, volume %f, delay %d", output, stream, volume, delayMs);
        if (stream == AudioSystem::VOICE_CALL ||
            stream == AudioSystem::DTMF ||
            stream == AudioSystem::BLUETOOTH_SCO) {
            float voiceVolume = -1.0;
            // offset value to reflect actual hardware volume that never reaches 0
            // 1% corresponds roughly to first step in VOICE_CALL stream volume setting (see AudioService.java)
            volume = 0.01 + 0.99 * volume;
            if (stream == AudioSystem::VOICE_CALL) {
                voiceVolume = (float)index/(float)mStreams[stream].mIndexMax;
            } else if (stream == AudioSystem::BLUETOOTH_SCO) {
                voiceVolume = 1.0;
            }
            if (voiceVolume >= 0 && output == mPrimaryOutput) {
                mpClientInterface->setVoiceVolume(voiceVolume, delayMs);
            }
#ifdef QCOM_FM_ENABLED
        } else if (stream == AudioSystem::FM) {
            float fmVolume = -1.0;
            fmVolume = computeVolume(stream, index, output, device);
            if (fmVolume >= 0) {
                if(output == mPrimaryOutput)
                    mpClientInterface->setFmVolume(fmVolume, delayMs);
                else if(mHasA2dp && output == getA2dpOutput())
                    mpClientInterface->setStreamVolume((AudioSystem::stream_type)stream, volume, output, delayMs);
            }
            return NO_ERROR;
#endif
        }
        mpClientInterface->setStreamVolume((AudioSystem::stream_type)stream, volume, output, delayMs);
    }
    return NO_ERROR;
}

audio_io_handle_t AudioPolicyManager::getInput(int inputSource,
                                    uint32_t samplingRate,
                                    uint32_t format,
                                    uint32_t channelMask,
                                    AudioSystem::audio_in_acoustics acoustics)
{
    audio_io_handle_t input = 0;
    audio_devices_t device = getDeviceForInputSource(inputSource);

    ALOGV("getInput() inputSource %d, samplingRate %d, format %d, channelMask %x, acoustics %x",
          inputSource, samplingRate, format, channelMask, acoustics);

    if (device == AUDIO_DEVICE_NONE) {
        ALOGW("getInput() could not find device for inputSource %d", inputSource);
        return 0;
    }

    // adapt channel selection to input source
    switch(inputSource) {
    case AUDIO_SOURCE_VOICE_UPLINK:
        channelMask |= AudioSystem::CHANNEL_IN_VOICE_UPLINK;
        break;
    case AUDIO_SOURCE_VOICE_DOWNLINK:
        channelMask |= AudioSystem::CHANNEL_IN_VOICE_DNLINK;
        break;
    case AUDIO_SOURCE_VOICE_CALL:
        channelMask |= (AudioSystem::CHANNEL_IN_VOICE_UPLINK | AudioSystem::CHANNEL_IN_VOICE_DNLINK);
        break;
    default:
        break;
    }

    IOProfile *profile = getInputProfile(device,
                                         samplingRate,
                                         format,
                                         channelMask);
    if (profile == NULL) {
        ALOGW("getInput() could not find profile for device %04x, samplingRate %d, format %d,"
                "channelMask %04x",
                device, samplingRate, format, channelMask);
        return 0;
    }

    if (profile->mModule->mHandle == 0) {
        ALOGE("getInput(): HW module %s not opened", profile->mModule->mName);
        return 0;
    }

    AudioInputDescriptor *inputDesc = new AudioInputDescriptor(profile);

    inputDesc->mInputSource = inputSource;
    inputDesc->mDevice = device;
    inputDesc->mSamplingRate = samplingRate;
    inputDesc->mFormat = (audio_format_t)format;
    inputDesc->mChannelMask = (audio_channel_mask_t)channelMask;
    inputDesc->mRefCount = 0;
    input = mpClientInterface->openInput(profile->mModule->mHandle,
                                    &inputDesc->mDevice,
                                    &inputDesc->mSamplingRate,
                                    &inputDesc->mFormat,
                                    &inputDesc->mChannelMask);

    // only accept input with the exact requested set of parameters
    if (input == 0 ||
        (samplingRate != inputDesc->mSamplingRate) ||
        (format != inputDesc->mFormat) ||
        (channelMask != inputDesc->mChannelMask)) {
        ALOGV("getInput() failed opening input: samplingRate %d, format %d, channelMask %d",
                samplingRate, format, channelMask);
        if (input != 0) {
            mpClientInterface->closeInput(input);
        }
        delete inputDesc;
        return 0;
    }
    mInputs.add(input, inputDesc);
    return input;
}

audio_devices_t AudioPolicyManager::getDeviceForInputSource(int inputSource)
{
    uint32_t device = AUDIO_DEVICE_NONE;

    switch(inputSource) {
    case AUDIO_SOURCE_DEFAULT:
    case AUDIO_SOURCE_MIC:
    case AUDIO_SOURCE_VOICE_RECOGNITION:
#ifndef WITH_QCOM_VOIP_OVER_MVS
    case AUDIO_SOURCE_VOICE_COMMUNICATION:
#endif
        if (mForceUse[AudioSystem::FOR_RECORD] == AudioSystem::FORCE_BT_SCO &&
            mAvailableInputDevices & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            device = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_WIRED_HEADSET) {
            device = AUDIO_DEVICE_IN_WIRED_HEADSET;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_BUILTIN_MIC) {
            device = AUDIO_DEVICE_IN_BUILTIN_MIC;
        }
        break;
#ifdef WITH_QCOM_VOIP_OVER_MVS
    case AUDIO_SOURCE_VOICE_COMMUNICATION:
        device = AUDIO_DEVICE_IN_COMMUNICATION;
        break;
#endif

    case AUDIO_SOURCE_CAMCORDER:
        if (mAvailableInputDevices & AUDIO_DEVICE_IN_BACK_MIC) {
            device = AUDIO_DEVICE_IN_BACK_MIC;
        } else if (mAvailableInputDevices & AUDIO_DEVICE_IN_BUILTIN_MIC) {
            device = AUDIO_DEVICE_IN_BUILTIN_MIC;
        }
        break;
    case AUDIO_SOURCE_VOICE_UPLINK:
    case AUDIO_SOURCE_VOICE_DOWNLINK:
    case AUDIO_SOURCE_VOICE_CALL:
        if (mAvailableInputDevices & AUDIO_DEVICE_IN_VOICE_CALL) {
            device = AUDIO_DEVICE_IN_VOICE_CALL;
        }
        break;
    case AUDIO_SOURCE_REMOTE_SUBMIX:
        if (mAvailableInputDevices & AUDIO_DEVICE_IN_REMOTE_SUBMIX) {
            device = AUDIO_DEVICE_IN_REMOTE_SUBMIX;
        }
        break;

#ifdef QCOM_FM_ENABLED
    case AUDIO_SOURCE_FM_RX:
        device = AudioSystem::DEVICE_IN_FM_RX;
        break;
    case AUDIO_SOURCE_FM_RX_A2DP:
        device = AudioSystem::DEVICE_IN_FM_RX_A2DP;
        break;
#endif
    default:
        ALOGW("getDeviceForInputSource() invalid input source %d", inputSource);
        break;
    }
    ALOGV("getDeviceForInputSource()input source %d, device %08x", inputSource, device);
    return device;
}
/*
Overwriting this function from base class to allow 2 acitve AudioRecord clients in case of FM.
One for FM A2DP playbck and other for FM recording.
*/
status_t AudioPolicyManager::startInput(audio_io_handle_t input)
{
    ALOGV("startInput() input %d", input);
    ssize_t index = mInputs.indexOfKey(input);
    if (index < 0) {
        ALOGW("startInput() unknow input %d", input);
        return BAD_VALUE;
    }
    AudioInputDescriptor *inputDesc = mInputs.valueAt(index);

#ifdef AUDIO_POLICY_TEST
    if (mTestInput == 0)
#endif //AUDIO_POLICY_TEST
    {
        // refuse 2 active AudioRecord clients at the same time
        if (getActiveInput() != 0) {
            ALOGW("startInput() input %d failed: other input already started", input);
            return INVALID_OPERATION;
        }
    }

    AudioParameter param = AudioParameter();
    param.addInt(String8(AudioParameter::keyRouting), (int)inputDesc->mDevice);
    // use Voice Recognition mode or not for this input based on input source
    int vr_enabled = inputDesc->mInputSource == AUDIO_SOURCE_VOICE_RECOGNITION ? 1 : 0;
    param.addInt(String8("vr_mode"), vr_enabled);
    ALOGV("AudioPolicyManager::startInput(%d), setting vr_mode to %d", inputDesc->mInputSource, vr_enabled);
    mpClientInterface->setParameters(input, param.toString());
    inputDesc->mRefCount = 1;
    return NO_ERROR;
}

void AudioPolicyManager::handleNotificationRoutingForStream(AudioSystem::stream_type stream) {
    switch(stream) {
    case AudioSystem::MUSIC:
        checkOutputForStrategy(STRATEGY_SONIFICATION_RESPECTFUL);
        updateDevicesAndOutputs();
        break;
    default:
        break;
    }
}


}; // namespace android_audio_legacy
