/*
 * Copyright 2013, The CyanogenMod Project
 *   Shareef Ali
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

#include "Custom-Platform_Api.h"
#include <pthread.h>
#include <audio_hw.h>

snd_device_t custom_platform_get_input_snd_device(void *platform, audio_devices_t out_device){
    return -2;
}
void custom_init_data(){return;}
