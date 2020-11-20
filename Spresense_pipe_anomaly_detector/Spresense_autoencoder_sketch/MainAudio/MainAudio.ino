/*
 *  MainAudio.ino - MP Example for Audio FFT 
 *  Copyright 2019 Sony Semiconductor Solutions Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifdef SUBCORE
#error "Core selection is wrong!!"
#endif

#include <MP.h>
#include <MPMutex.h>
#include <Audio.h>
#include <FFT.h>
#include <IIR.h>
#include <pthread.h>

#define SND_OK   100
#define SND_NG   101

#include <SDHCI.h>
SDClass theSD;

#include <DNNRT.h>
DNNRT dnnrt;

#define LPF_ENABLE

#define FFT_LEN 1024
#define SMA_WINDOW 8
#define CHANNEL_NUM 1

#ifdef LPF_ENABLE
#define LPF_CUTOFF 3000
#define LPF_QVALUE 0.70710678
#endif

AudioClass *theAudio = AudioClass::getInstance();
static const int32_t buffer_sample = FFT_LEN;
static const int32_t buffer_size = buffer_sample * sizeof(int16_t);
static char buff[buffer_size];
uint32_t read_size;

const int subcore = 1;
static int g_loop = 0;

#define MAXLOOP 200

static float pDst[FFT_LEN];
static float pData[FFT_LEN/2];

#ifdef SMA_WINDOW
static float pSMA[SMA_WINDOW][FFT_LEN];
#endif

MPMutex mutex(MP_MUTEX_ID0);
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

FFTClass<CHANNEL_NUM, FFT_LEN> FFT;
#ifdef LPF_ENABLE
IIRClass LPF;
#endif

#ifdef SMA_WINDOW
void applySMA(float sma[SMA_WINDOW][FFT_LEN], float dst[FFT_LEN]) 
{
  int i, j;
  static int g_counter = 0;
  if (g_counter == SMA_WINDOW) g_counter = 0;
  for (i = 0; i < FFT_LEN; ++i) {
    sma[g_counter][i] = dst[i];
    float sum = 0;
    for (j = 0; j < SMA_WINDOW; ++j) {
      sum += sma[j][i];
    }
    dst[i] = sum / SMA_WINDOW;
  }
  ++g_counter;
}
#endif

void audioReadFrames() 
{
#ifdef LPF_ENABLE
  static q15_t pLPFSig[FFT_LEN];
#endif

  int ret;
  while (1) {
    int err = theAudio->readFrames(buff, buffer_size, &read_size);
    if (err != AUDIOLIB_ECODE_OK && err != AUDIOLIB_ECODE_INSUFFICIENT_BUFFER_AREA) {
      Serial.println("Error err = " + String(err));
      theAudio->stopRecorder();
      exit(1);
    }

    if (read_size < FFT_LEN) {
      Serial.println("read_size: " + String(read_size));
      usleep(1);
      continue;
    }
    
    ret = pthread_mutex_lock(&m);
    if (ret != 0) Serial.println("Mutex Lock Error");
#ifdef LPF_ENABLE
    LPF.put((q15_t*)buff, FFT_LEN);
    LPF.get(pLPFSig, 0);
    FFT.put(pLPFSig, FFT_LEN);
#else
    FFT.put((q15_t*)buff, FFT_LEN);
#endif
    FFT.get(pDst, 0);
#ifdef SMA_WINDOW
    applySMA(pSMA, pDst);
#endif
    ret = pthread_mutex_unlock(&m);
    if (ret != 0) Serial.println("Mutex UnLock Error");
    
    usleep(8000);
  }
}

void setup()
{
  Serial.begin(115200);
  theSD.begin();
#ifdef LPF_ENABLE
  LPF.begin(TYPE_LPF, CHANNEL_NUM, LPF_CUTOFF, LPF_QVALUE);
#endif
  FFT.begin(WindowHamming, 1, (FFT_LEN/4));

  Serial.println("Subcore start");
  MP.begin(subcore);
  MP.RecvTimeout(MP_RECV_POLLING); 

  Serial.println("Init Audio Recorder");
  theAudio->begin();
  theAudio->setRecorderMode(AS_SETRECDR_STS_INPUTDEVICE_MIC, 200);
  int err = theAudio->initRecorder(AS_CODECTYPE_PCM ,"/mnt/sd0/BIN" 
                           ,AS_SAMPLINGRATE_48000 ,AS_CHANNEL_MONO);                             
  if (err != AUDIOLIB_ECODE_OK) {
    Serial.println("Recorder initialize error");
    while(1);
  }

  Serial.println("Initialize DNNRT");
  File nnbfile("model.nnb");
  if (!nnbfile) {
    Serial.print("nnb not found");
    while(1);
  }

  int ret = dnnrt.begin(nnbfile);
  if (ret < 0) {
    Serial.print("DNN Runtime begin fail: " + String(ret));
    while(1);
  }

  Serial.println("Start Recorder");
  theAudio->startRecorder(); 

  Serial.println("Start recording loop");
  task_create("audio recording", 120, 1024, audioReadFrames, NULL);
  sleep(1);
}

#define MOVING_AVERAGE 16

void loop()
{
  int ret;
  int8_t rcvid;
  int8_t sndid;
  bool bNG = false;
  static float average[MOVING_AVERAGE];
  static float data[FFT_LEN/2];
  static uint8_t gCounter = 0;

  ret = pthread_mutex_lock(&m);
  if (ret != 0) Serial.println("Mutex Lock Error");
  memcpy(pData, pDst, FFT_LEN/2);
  ret = pthread_mutex_unlock(&m);
  if (ret != 0) Serial.println("Mutex UnLock Error");

  float f_max = 0.0;
  float f_min = 1000.0;
  for (int i = 0; i < FFT_LEN/2; ++i) {
    if (!isnan(pData[i]) && pData[i] > 0.0) { 
      if (pData[i] > f_max) f_max = pData[i];
      if (pData[i] < f_min) f_min = pData[i];
    } else {
      pData[i] = 0.0;
    }
  }

  /* normalize */
  for (int i = 0; i < FFT_LEN/2; ++i) {
    pData[i] = (pData[i] - f_min)/(f_max - f_min);
  }
  
  /* Cutoff frequency is 3kHz (64tap)
   * df = Sampling Rate/FFT Length = 48kHz/1024 = 46.875Hz
   * Cutoff tap = Cutoff Freq / df = 3kHz/46.875Hz = 64 tap
   */
  DNNVariable input(FFT_LEN/8); 
  float *dnnbuf = input.data();
  for (int i = 0; i < FFT_LEN/8; ++i) {
    dnnbuf[i] = pData[i];
  }

  dnnrt.inputVariable(input, 0);
  dnnrt.forward();
  
  DNNVariable output = dnnrt.outputVariable(0);
  float sqr_err = 0.0;
  for (int i = 0; i < FFT_LEN/8; ++i) {
    float err = pData[i] - output[i];
    sqr_err += sqrt(err*err);
  } 
   
  average[gCounter++] = sqr_err / (FFT_LEN/8);
  if (gCounter == MOVING_AVERAGE) gCounter = 0;
  for (int i = 0, sqr_err = 0.0; i < MOVING_AVERAGE; ++i) {
    sqr_err += average[i];
  }
  sqr_err /= MOVING_AVERAGE;
  Serial.println("Result: " + String(sqr_err, 7));
  
  sqr_err > 0.6 ? bNG = true : bNG = false;

  ret = mutex.Trylock();
  if (ret != 0) return;

  if (bNG) sndid = SND_NG;
  else     sndid = SND_OK;
  memcpy(data, pData, FFT_LEN/2);
  ret = MP.Send(sndid, &data, subcore);
  if (ret < 0) Serial.println("MP.Send Error");
  mutex.Unlock();
}
