/* Sonic library
   Copyright 2010
   Bill Cox
   This file is part of the Sonic Library.

   This file is licensed under the Apache 2.0 license, and also placed into the public domain.
   Use it either way, at your option.
*/

/*
The Sonic Library implements a new algorithm invented by Bill Cox for the
specific purpose of speeding up speech by high factors at high quality.  It
generates smooth speech at speed up factors as high as 6X, possibly more.  It is
also capable of slowing down speech, and generates high quality results
regardless of the speed up or slow down factor.  For speeding up speech by 2X or
more, the following equation is used:

    newSamples = period/(speed - 1.0)
    scale = 1.0/newSamples;

where period is the current pitch period, determined using AMDF or any other
pitch estimator, and speed is the speedup factor.  If the current position in
the input stream is pointed to by "samples", and the current output stream
position is pointed to by "out", then newSamples number of samples can be
generated with:

    out[t] = (samples[t]*(newSamples - t) + samples[t + period]*t)/newSamples;

where t = 0 to newSamples - 1.

For speed factors < 2X, the PICOLA algorithm is used.  The above
algorithm is first used to double the speed of one pitch period.  Then, enough
input is directly copied from the input to the output to achieve the desired
speed up factor, where 1.0 < speed < 2.0.  The amount of data copied is derived:

    speed = (2*period + length)/(period + length)
    speed*length + speed*period = 2*period + length
    length(speed - 1) = 2*period - speed*period
    length = period*(2 - speed)/(speed - 1)

For slowing down speech where 0.5 < speed < 1.0, a pitch period is inserted into
the output twice, and length of input is copied from the input to the output
until the output desired speed is reached.  The length of data copied is:

    length = period*(speed - 0.5)/(1 - speed)

For slow down factors below 0.5, no data is copied, and an algorithm
similar to high speed factors is used.
*/

#ifdef  __cplusplus
extern "C" {
#endif

/* Uncomment this to use sin-wav based overlap add which in theory can improve
   sound quality slightly, at the expense of lots of floating point math. */
/* #define SONIC_USE_SIN */

/* This specifies the range of voice pitches we try to match.
   Note that if we go lower than 65, we could overflow in findPitchInRange */
#define SONIC_MIN_PITCH 65
#define SONIC_MAX_PITCH 400

/* These are used to down-sample some inputs to improve speed */
#define SONIC_AMDF_FREQ 4000

struct sonicStreamStruct;
typedef struct sonicStreamStruct *sonicStream;

/* For all of the following functions, numChannels is multiplied by numSamples
   to determine the actual number of values read or returned. */

/* Create a sonic stream.  Return NULL only if we are out of memory and cannot
  allocate the stream. Set numChannels to 1 for mono, and 2 for stereo. */
// 鍒涘缓涓€涓煶棰戞祦锛屽鏋滃唴瀛樻孩鍑轰笉鑳藉垱寤烘祦浼氳繑鍥濶ULL锛宯umCHannels琛ㄧず澹伴亾鐨勪釜鏁帮紝1涓哄崟澹伴亾锛?涓哄弻澹伴亾
sonicStream sonicCreateStream(int sampleRate, int numChannels);
/* Destroy the sonic stream. */
// 閿€姣佷竴涓煶棰戞祦
void sonicDestroyStream(sonicStream stream);
/* Use this to write floating point data to be speed up or down into the stream.
   Values must be between -1 and 1.  Return 0 if memory realloc failed, otherwise 1 */
//
int sonicWriteFloatToStream(sonicStream stream, float *samples, int numSamples);
/* Use this to write 16-bit data to be speed up or down into the stream.
   Return 0 if memory realloc failed, otherwise 1 */
int sonicWriteShortToStream(sonicStream stream, short *samples, int numSamples);
/* Use this to write 8-bit unsigned data to be speed up or down into the stream.
   Return 0 if memory realloc failed, otherwise 1 */
int sonicWriteUnsignedCharToStream(sonicStream stream, unsigned char *samples, int numSamples);
/* Use this to read floating point data out of the stream.  Sometimes no data
   will be available, and zero is returned, which is not an error condition. */
int sonicReadFloatFromStream(sonicStream stream, float *samples, int maxSamples);
/* Use this to read 16-bit data out of the stream.  Sometimes no data will
   be available, and zero is returned, which is not an error condition. */
int sonicReadShortFromStream(sonicStream stream, short *samples, int maxSamples);
/* Use this to read 8-bit unsigned data out of the stream.  Sometimes no data will
   be available, and zero is returned, which is not an error condition. */
int sonicReadUnsignedCharFromStream(sonicStream stream, unsigned char *samples, int maxSamples);
/* Force the sonic stream to generate output using whatever data it currently
   has.  No extra delay will be added to the output, but flushing in the middle of
   words could introduce distortion. */
// 绔嬪嵆寮哄埗鍒锋柊娴?
int sonicFlushStream(sonicStream stream);
/* Return the number of samples in the output buffer */
// 杩斿洖杈撳嚭缂撳啿涓殑閲囨牱鐐规暟鐩?
int sonicSamplesAvailable(sonicStream stream);
/* Get the speed of the stream. */
// 寰楀埌闊抽娴佺殑閫熷害
float sonicGetSpeed(sonicStream stream);
/* Set the speed of the stream. */
// 璁剧疆闊抽娴佺殑閫熷害
void sonicSetSpeed(sonicStream stream, float speed);
/* Get the pitch of the stream. */
float sonicGetPitch(sonicStream stream);
/* Set the pitch of the stream. */
void sonicSetPitch(sonicStream stream, float pitch);
/* Get the rate of the stream. */
float sonicGetRate(sonicStream stream);
/* Set the rate of the stream. */
void sonicSetRate(sonicStream stream, float rate);
/* Get the scaling factor of the stream. */
float sonicGetVolume(sonicStream stream);
/* Set the scaling factor of the stream. */
void sonicSetVolume(sonicStream stream, float volume);
/* Get the chord pitch setting. */
int sonicGetChordPitch(sonicStream stream);
/* Set chord pitch mode on or off.  Default is off.  See the documentation
   page for a description of this feature. */
void sonicSetChordPitch(sonicStream stream, int useChordPitch);
/* Get the quality setting. */
// 寰楀埌闊抽娴佺殑璐ㄩ噺
int sonicGetQuality(sonicStream stream);
/* Set the "quality".  Default 0 is virtually as good as 1, but very much faster. */
// 璁剧疆闊抽娴佺殑璐ㄩ噺锛岄粯璁ょ殑0鐨勮川閲忓嚑涔庡拰1鐨勪竴鏍峰ソ锛屼絾鏄洿蹇?
void sonicSetQuality(sonicStream stream, int quality);
/* Get the sample rate of the stream. */
// 寰楀埌闊抽娴佺殑閲囨牱鐜?
int sonicGetSampleRate(sonicStream stream);
/* Set the sample rate of the stream.  This will drop any samples that have not been read. */
// 璁剧疆闊抽娴佺殑閲囨牱鐜?
void sonicSetSampleRate(sonicStream stream, int sampleRate);
/* Get the number of channels. */
// 寰楀埌闊抽鐨勫０閬撴暟
int sonicGetNumChannels(sonicStream stream);
/* Set the number of channels.  This will drop any samples that have not been read. */
// 璁剧疆闊抽娴佺殑澹伴亾鏁?
void sonicSetNumChannels(sonicStream stream, int numChannels);
/* This is a non-stream oriented interface to just change the speed of a sound
   sample.  It works in-place on the sample array, so there must be at least
   speed*numSamples available space in the array. Returns the new number of samples. */
// 杩欐槸涓€涓潪闈㈠悜娴佺殑鍊熷彛锛屽彧鏄敼鍙樺０闊抽噰鏍风殑閫熺巼銆傚畠宸ヤ綔鍦ㄩ噰鏍锋暟缁勫唴閮紝
//鎵€浠ュ湪鏁扮粍鍐呰嚦灏戣鏈塻peed*numSampes澶у皬鐨勭┖闂淬€傝繑鍥炲€兼槸鏂扮殑閲囨牱鐐圭殑鏁扮洰

int sonicChangeFloatSpeed(float *samples, int numSamples, float speed, float pitch,
    float rate, float volume, int useChordPitch, int sampleRate, int numChannels);
/* This is a non-stream oriented interface to just change the speed of a sound
   sample.  It works in-place on the sample array, so there must be at least
   speed*numSamples available space in the array. Returns the new number of samples. */
int sonicChangeShortSpeed(short *samples, int numSamples, float speed, float pitch,
    float rate, float volume, int useChordPitch, int sampleRate, int numChannels);

#ifdef  __cplusplus
}
#endif

#ifdef SONIC_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
    The following code was used to generate the following sinc lookup table.

    #include <math.h>
    #include <limits.h>
    #include <stdio.h>

    double findHannWeight(int N, double x) {
        return 0.5*(1.0 - cos(2*M_PI*x/N));
    }

    double findSincCoefficient(int N, double x) {
        double hannWindowWeight = findHannWeight(N, x);
        double sincWeight;

        x -= N/2.0;
        if (x > 1e-9 || x < -1e-9) {
            sincWeight = sin(M_PI*x)/(M_PI*x);
        } else {
            sincWeight = 1.0;
        }
        return hannWindowWeight*sincWeight;
    }

    int main() {
        double x;
        int i;
        int N = 12;

        for (i = 0, x = 0.0; x <= N; x += 0.02, i++) {
            printf("%u %d\n", i, (int)(SHRT_MAX*findSincCoefficient(N, x)));
        }
        return 0;
    }
*/

/* The number of points to use in the sinc FIR filter for resampling. */
#define SINC_FILTER_POINTS 12 /* I am not able to hear improvement with higher N. */
#define SINC_TABLE_SIZE 601

/* Lookup table for windowed sinc function of SINC_FILTER_POINTS points. */
static short sincTable[SINC_TABLE_SIZE] = {
    0, 0, 0, 0, 0, 0, 0, -1, -1, -2, -2, -3, -4, -6, -7, -9, -10, -12, -14,
    -17, -19, -21, -24, -26, -29, -32, -34, -37, -40, -42, -44, -47, -48, -50,
    -51, -52, -53, -53, -53, -52, -50, -48, -46, -43, -39, -34, -29, -22, -16,
    -8, 0, 9, 19, 29, 41, 53, 65, 79, 92, 107, 121, 137, 152, 168, 184, 200,
    215, 231, 247, 262, 276, 291, 304, 317, 328, 339, 348, 357, 363, 369, 372,
    374, 375, 373, 369, 363, 355, 345, 332, 318, 300, 281, 259, 234, 208, 178,
    147, 113, 77, 39, 0, -41, -85, -130, -177, -225, -274, -324, -375, -426,
    -478, -530, -581, -632, -682, -731, -779, -825, -870, -912, -951, -989,
    -1023, -1053, -1080, -1104, -1123, -1138, -1149, -1154, -1155, -1151,
    -1141, -1125, -1105, -1078, -1046, -1007, -963, -913, -857, -796, -728,
    -655, -576, -492, -403, -309, -210, -107, 0, 111, 225, 342, 462, 584, 708,
    833, 958, 1084, 1209, 1333, 1455, 1575, 1693, 1807, 1916, 2022, 2122, 2216,
    2304, 2384, 2457, 2522, 2579, 2625, 2663, 2689, 2706, 2711, 2705, 2687,
    2657, 2614, 2559, 2491, 2411, 2317, 2211, 2092, 1960, 1815, 1658, 1489,
    1308, 1115, 912, 698, 474, 241, 0, -249, -506, -769, -1037, -1310, -1586,
    -1864, -2144, -2424, -2703, -2980, -3254, -3523, -3787, -4043, -4291,
    -4529, -4757, -4972, -5174, -5360, -5531, -5685, -5819, -5935, -6029,
    -6101, -6150, -6175, -6175, -6149, -6096, -6015, -5905, -5767, -5599,
    -5401, -5172, -4912, -4621, -4298, -3944, -3558, -3141, -2693, -2214,
    -1705, -1166, -597, 0, 625, 1277, 1955, 2658, 3386, 4135, 4906, 5697, 6506,
    7332, 8173, 9027, 9893, 10769, 11654, 12544, 13439, 14335, 15232, 16128,
    17019, 17904, 18782, 19649, 20504, 21345, 22170, 22977, 23763, 24527,
    25268, 25982, 26669, 27327, 27953, 28547, 29107, 29632, 30119, 30569,
    30979, 31349, 31678, 31964, 32208, 32408, 32565, 32677, 32744, 32767,
    32744, 32677, 32565, 32408, 32208, 31964, 31678, 31349, 30979, 30569,
    30119, 29632, 29107, 28547, 27953, 27327, 26669, 25982, 25268, 24527,
    23763, 22977, 22170, 21345, 20504, 19649, 18782, 17904, 17019, 16128,
    15232, 14335, 13439, 12544, 11654, 10769, 9893, 9027, 8173, 7332, 6506,
    5697, 4906, 4135, 3386, 2658, 1955, 1277, 625, 0, -597, -1166, -1705,
    -2214, -2693, -3141, -3558, -3944, -4298, -4621, -4912, -5172, -5401,
    -5599, -5767, -5905, -6015, -6096, -6149, -6175, -6175, -6150, -6101,
    -6029, -5935, -5819, -5685, -5531, -5360, -5174, -4972, -4757, -4529,
    -4291, -4043, -3787, -3523, -3254, -2980, -2703, -2424, -2144, -1864,
    -1586, -1310, -1037, -769, -506, -249, 0, 241, 474, 698, 912, 1115, 1308,
    1489, 1658, 1815, 1960, 2092, 2211, 2317, 2411, 2491, 2559, 2614, 2657,
    2687, 2705, 2711, 2706, 2689, 2663, 2625, 2579, 2522, 2457, 2384, 2304,
    2216, 2122, 2022, 1916, 1807, 1693, 1575, 1455, 1333, 1209, 1084, 958, 833,
    708, 584, 462, 342, 225, 111, 0, -107, -210, -309, -403, -492, -576, -655,
    -728, -796, -857, -913, -963, -1007, -1046, -1078, -1105, -1125, -1141,
    -1151, -1155, -1154, -1149, -1138, -1123, -1104, -1080, -1053, -1023, -989,
    -951, -912, -870, -825, -779, -731, -682, -632, -581, -530, -478, -426,
    -375, -324, -274, -225, -177, -130, -85, -41, 0, 39, 77, 113, 147, 178,
    208, 234, 259, 281, 300, 318, 332, 345, 355, 363, 369, 373, 375, 374, 372,
    369, 363, 357, 348, 339, 328, 317, 304, 291, 276, 262, 247, 231, 215, 200,
    184, 168, 152, 137, 121, 107, 92, 79, 65, 53, 41, 29, 19, 9, 0, -8, -16,
    -22, -29, -34, -39, -43, -46, -48, -50, -52, -53, -53, -53, -52, -51, -50,
    -48, -47, -44, -42, -40, -37, -34, -32, -29, -26, -24, -21, -19, -17, -14,
    -12, -10, -9, -7, -6, -4, -3, -2, -2, -1, -1, 0, 0, 0, 0, 0, 0, 0
};

struct sonicStreamStruct {
    short *inputBuffer;
    short *outputBuffer;
    short *pitchBuffer;
    short *downSampleBuffer;
    float speed;
    float volume;
    float pitch;
    float rate;
    int oldRatePosition;
    int newRatePosition;
    int useChordPitch;
    int quality;
    int numChannels;
    int inputBufferSize;
    int pitchBufferSize;
    int outputBufferSize;
    int numInputSamples;
    int numOutputSamples;
    int numPitchSamples;
    int minPeriod;
    int maxPeriod;
    int maxRequired;
    int remainingInputToCopy;
    int sampleRate;
    int prevPeriod;
    int prevMinDiff;
    float avePower;
};

/* Scale the samples by the factor. */
// 鏀瑰彉闊抽噺
static void scaleSamples(
    short *samples,
    int numSamples,
    float volume)
{
    int fixedPointVolume = volume*4096.0f;
    int value;

    while(numSamples--) {
        value = (*samples*fixedPointVolume) >> 12;
        if(value > 32767) {
            value = 32767;
        } else if(value < -32767) {
            value = -32767;
        }
        *samples++ = value;
    }
}

/* Get the speed of the stream. */
// 寰楀埌娴佺殑閫熷害
float sonicGetSpeed(
    sonicStream stream)
{
    return stream->speed;
}

/* Set the speed of the stream. */
// 璁剧疆娴佺殑閫熷害
void sonicSetSpeed(
    sonicStream stream,
    float speed)
{
    stream->speed = speed;
}

/* Get the pitch of the stream. */
// 寰楀埌娴佺殑闊宠皟
float sonicGetPitch(
    sonicStream stream)
{
    return stream->pitch;
}

/* Set the pitch of the stream. */
// 璁剧疆娴佺殑闊宠皟
void sonicSetPitch(
    sonicStream stream,
    float pitch)
{
    stream->pitch = pitch;
}

/* Get the rate of the stream. */
// 寰楀埌娴佺殑閫熺巼
float sonicGetRate(
    sonicStream stream)
{
    return stream->rate;
}

/* Set the playback rate of the stream. This scales pitch and speed at the same time. */
// 璁剧疆鍥炴斁娴佺殑閫熺巼锛屽悓鏃朵篃閲嶈pitch鍜宻peed
void sonicSetRate(
    sonicStream stream,
    float rate)
{
    stream->rate = rate;

    stream->oldRatePosition = 0;
    stream->newRatePosition = 0;
}

/* Get the vocal chord pitch setting. */
//
int sonicGetChordPitch(
    sonicStream stream)
{
    return stream->useChordPitch;
}

/* Set the vocal chord mode for pitch computation.  Default is off. */
void sonicSetChordPitch(
    sonicStream stream,
    int useChordPitch)
{
    stream->useChordPitch = useChordPitch;
}

/* Get the quality setting. */
int sonicGetQuality(
    sonicStream stream)
{
    return stream->quality;
}

/* Set the "quality".  Default 0 is virtually as good as 1, but very much faster. */
void sonicSetQuality(
    sonicStream stream,
    int quality)
{
    stream->quality = quality;
}

/* Get the scaling factor of the stream. */
float sonicGetVolume(
    sonicStream stream)
{
    return stream->volume;
}

/* Set the scaling factor of the stream. */
// 璁剧疆娴佺殑闊抽噺
void sonicSetVolume(
    sonicStream stream,
    float volume)
{
    stream->volume = volume;
}

/* Free stream buffers. */
// 閲婃斁娴佸唴鐨勭紦鍐插尯
static void freeStreamBuffers(
    sonicStream stream)
{
    if(stream->inputBuffer != NULL) {
        free(stream->inputBuffer);
    }
    if(stream->outputBuffer != NULL) {
        free(stream->outputBuffer);
    }
    if(stream->pitchBuffer != NULL) {
        free(stream->pitchBuffer);
    }
    if(stream->downSampleBuffer != NULL) {
        free(stream->downSampleBuffer);
    }
}

/* Destroy the sonic stream. */
// 閿€姣佹祦
void sonicDestroyStream(
    sonicStream stream)
{
    freeStreamBuffers(stream);
    free(stream);
}

/* Allocate stream buffers. */
/**
 * 寮€杈熸祦鐨勬暟鎹紦瀛樼┖闂?
 * stream 娴?
 * sampleRate 閲囨牱鐜?
 * numChnnels 澹伴亾鏁?
 */
static int allocateStreamBuffers(
    sonicStream stream,
    int sampleRate,
    int numChannels)
{   // 鏈€灏忕殑pitch鍛ㄦ湡 44100/400 = 110
    int minPeriod = sampleRate/SONIC_MAX_PITCH;
    // 鏈€澶х殑pitch鍛ㄦ湡 44100/65 = 678 涓噰鏍风偣
    int maxPeriod = sampleRate/SONIC_MIN_PITCH;
    // 鏈€澶?1356
    int maxRequired = 2*maxPeriod;
    // 杈撳叆缂撳啿鍖虹殑澶у皬 = maxRequired
    stream->inputBufferSize = maxRequired;
    // 涓篿nputBuffer寮€杈熺┖闂村苟鍒濆鍖栦负0
    stream->inputBuffer = (short *)calloc(maxRequired, sizeof(short)*numChannels);
    // 濡傛灉寮€杈熷け璐ヨ繑鍥?
    if(stream->inputBuffer == NULL) {
        sonicDestroyStream(stream);
        return 0;
    }
    // 杈撳嚭缂撳啿鍖虹殑澶у皬= maxRequired
    stream->outputBufferSize = maxRequired;
    // 涓簅ututBUffer寮€杈熺┖闂?
    stream->outputBuffer = (short *)calloc(maxRequired, sizeof(short)*numChannels);
    if(stream->outputBuffer == NULL) {
        sonicDestroyStream(stream);
        return 0;
    }
    // 涓簆itchBuffer寮€杈熺┖闂?
    stream->pitchBufferSize = maxRequired;
    stream->pitchBuffer = (short *)calloc(maxRequired, sizeof(short)*numChannels);
    if(stream->pitchBuffer == NULL) {
        sonicDestroyStream(stream);
        return 0;
    }
    // 涓篸ownSampleBuffer锛堥檷閲囨牱锛夊紑杈熺┖闂?
    stream->downSampleBuffer = (short *)calloc(maxRequired, sizeof(short));
    if(stream->downSampleBuffer == NULL) {
        sonicDestroyStream(stream);
        return 0;
    }
    // 鍒濆鍖栧悇椤瑰弬鏁?
    stream->sampleRate = sampleRate;
    stream->numChannels = numChannels;
    stream->oldRatePosition = 0;
    stream->newRatePosition = 0;
    stream->minPeriod = minPeriod;
    stream->maxPeriod = maxPeriod;
    stream->maxRequired = maxRequired;
    stream->prevPeriod = 0;
    return 1;
}

/* Create a sonic stream.  Return NULL only if we are out of memory and cannot
   allocate the stream. */
// 鍒涘缓涓€涓煶棰戞祦
sonicStream sonicCreateStream(
    int sampleRate,
    int numChannels)
{
    // 寮€杈熶竴涓猻onicStreamStruct澶у皬鐨勭┖闂?
    sonicStream stream = (sonicStream)calloc(1, sizeof(struct sonicStreamStruct));
    // 濡傛灉娴佷负绌猴紝璇佹槑寮€杈熷け璐?
    if(stream == NULL) {
        return NULL;
    }
    if(!allocateStreamBuffers(stream, sampleRate, numChannels)) {
        return NULL;
    }
    // 鍒濆鍖栧悇椤瑰弬鏁?
    stream->speed = 1.0f;
    stream->pitch = 1.0f;
    stream->volume = 1.0f;
    stream->rate = 1.0f;
    stream->oldRatePosition = 0;
    stream->newRatePosition = 0;
    stream->useChordPitch = 0;
    stream->quality = 0;
    stream->avePower = 50.0f;
    return stream;
}

/* Get the sample rate of the stream. */
// 鍙栧緱娴佺殑閲囨牱鐜?
int sonicGetSampleRate(
    sonicStream stream)
{
    return stream->sampleRate;
}

/* Set the sample rate of the stream.  This will cause samples buffered in the stream to
   be lost. */
// 璁剧疆娴佺殑閲囨牱鐜囷紝鍙兘浣挎祦涓殑宸茬粡缂撳啿鐨勬暟鎹涪澶?
void sonicSetSampleRate(
    sonicStream stream,
    int sampleRate)
{
    freeStreamBuffers(stream);
    allocateStreamBuffers(stream, sampleRate, stream->numChannels);
}

/* Get the number of channels. */
// 鍙栧緱娴佺殑澹伴亾鐨勬暟閲?
int sonicGetNumChannels(
    sonicStream stream)
{
    return stream->numChannels;
}

/* Set the num channels of the stream.  This will cause samples buffered in the stream to
   be lost. */
// 璁剧疆娴佺殑澹伴亾鏁伴噺锛屽彲鑳介€犳垚娴佷腑宸茬紦瀛樼殑棰濇暟鎹殑涓㈠け
void sonicSetNumChannels(
    sonicStream stream,
    int numChannels)
{
    freeStreamBuffers(stream);
    allocateStreamBuffers(stream, stream->sampleRate, numChannels);
}

/* Enlarge the output buffer if needed. */
// 鏍规嵁闇€瑕佹墿澶ц緭鍑虹紦鍐插尯
static int enlargeOutputBufferIfNeeded(
    sonicStream stream,
    int numSamples)
{
    if(stream->numOutputSamples + numSamples > stream->outputBufferSize) {
        stream->outputBufferSize += (stream->outputBufferSize >> 1) + numSamples;
        stream->outputBuffer = (short *)realloc(stream->outputBuffer,
            stream->outputBufferSize*sizeof(short)*stream->numChannels);
        if(stream->outputBuffer == NULL) {
            return 0;
        }
    }
    return 1;
}

/* Enlarge the input buffer if needed. */
// 濡傛灉闇€瑕佺殑璇濆澶ц緭鍏ョ紦鍐插尯
static int enlargeInputBufferIfNeeded(
    sonicStream stream,
    int numSamples)
{
    // 娴佷腑宸茬粡鏈夌殑閲囨牱鏁版嵁鐨勫ぇ灏?+ 鏂扮殑閲囨牱鐐逛釜鏁?
    if(stream->numInputSamples + numSamples > stream->inputBufferSize) {
        stream->inputBufferSize += (stream->inputBufferSize >> 1) + numSamples;
        // 閲嶆柊璁剧疆鍐呭瓨绌洪棿鐨勫ぇ灏?
        stream->inputBuffer = (short *)realloc(stream->inputBuffer,
            stream->inputBufferSize*sizeof(short)*stream->numChannels);
        if(stream->inputBuffer == NULL) {
            return 0;
        }
    }
    return 1;
}

/* Add the input samples to the input buffer. */
// 鍚戞祦鐨勮緭鍏ョ紦鍐插尯涓啓鍏loat鏍煎紡鐨勯噰鏍锋暟鎹?
static int addFloatSamplesToInputBuffer(
    sonicStream stream,
    float *samples,
    int numSamples)
{
    short *buffer;
    int count = numSamples*stream->numChannels;

    if(numSamples == 0) {
        return 1;
    }
    if(!enlargeInputBufferIfNeeded(stream, numSamples)) {
        return 0;
    }
    buffer = stream->inputBuffer + stream->numInputSamples*stream->numChannels;
    while(count--) {
        *buffer++ = (*samples++)*32767.0f;
    }
    stream->numInputSamples += numSamples;
    return 1;
}

/* Add the input samples to the input buffer. */
// 鍚戞祦鐨勮緭鍏ョ紦鍐插尯涓啓鍏hort绫诲瀷鐨勬暟鎹?
static int addShortSamplesToInputBuffer(
    sonicStream stream,
    short *samples,
    int numSamples)
{
    if(numSamples == 0) {
        return 1;
    }
    if(!enlargeInputBufferIfNeeded(stream, numSamples)) {
        return 0;
    }
    // 鍚戣緭鍏ョ紦鍐插尯鎷疯礉鏁版嵁锛岄噸璁緉umInputSamples澶у皬
    memcpy(stream->inputBuffer + stream->numInputSamples*stream->numChannels, samples,
        numSamples*sizeof(short)*stream->numChannels);
    stream->numInputSamples += numSamples;
    return 1;
}

/* Add the input samples to the input buffer. */
// 鍚戞祦鐨勮緭濡傜紦鍐插尯涓啓鍏nsigned鏍煎紡鐨勯噰鏍锋暟鎹?
static int addUnsignedCharSamplesToInputBuffer(
    sonicStream stream,
    unsigned char *samples,
    int numSamples)
{
    short *buffer;
    int count = numSamples*stream->numChannels;

    if(numSamples == 0) {
        return 1;
    }
    if(!enlargeInputBufferIfNeeded(stream, numSamples)) {
        return 0;
    }
    buffer = stream->inputBuffer + stream->numInputSamples*stream->numChannels;
    while(count--) {
        *buffer++ = (*samples++ - 128) << 8;
    }
    stream->numInputSamples += numSamples;
    return 1;
}

/* Remove input samples that we have already processed. */
// 绉婚櫎宸茬粡澶勭悊杩囩殑杈撳叆缂撳啿鍖轰腑鐨勬暟鎹?
static void removeInputSamples(
    sonicStream stream,
    int position)
{
    int remainingSamples = stream->numInputSamples - position;

    if(remainingSamples > 0) {
        memmove(stream->inputBuffer, stream->inputBuffer + position*stream->numChannels,
            remainingSamples*sizeof(short)*stream->numChannels);
    }
    stream->numInputSamples = remainingSamples;
}

/* Just copy from the array to the output buffer */
// 鎷疯礉鏁扮粍鍒拌緭鍑虹紦鍐插尯
static int copyToOutput(
    sonicStream stream,
    short *samples,
    int numSamples)
{
    if(!enlargeOutputBufferIfNeeded(stream, numSamples)) {
        return 0;
    }
    memcpy(stream->outputBuffer + stream->numOutputSamples*stream->numChannels,
        samples, numSamples*sizeof(short)*stream->numChannels);
    stream->numOutputSamples += numSamples;
    return 1;
}

/* Just copy from the input buffer to the output buffer.  Return 0 if we fail to
   resize the output buffer.  Otherwise, return numSamples */
// 浠呬粎鎶婅緭鍏ョ紦鍐插尯涓殑鏁版嵁鎷疯礉鍒拌緭鍑虹紦鍐插尯涓紝杩斿洖杞Щ浜嗙殑閲囨牱鐐圭殑涓暟
// position琛ㄧず鍋忕Щ閲?
static int copyInputToOutput(
    sonicStream stream,
    int position)
{
    int numSamples = stream->remainingInputToCopy;
    //
    if(numSamples > stream->maxRequired) {
        numSamples = stream->maxRequired;
    }
    if(!copyToOutput(stream, stream->inputBuffer + position*stream->numChannels,
            numSamples)) {
        return 0;
    }
    // 鍓╀綑闇€瑕佹嫹璐濈殑杈撳叆缂撳啿鍖虹殑閲囨牱鐐规暟
    stream->remainingInputToCopy -= numSamples;
    return numSamples;
}

/* Read data out of the stream.  Sometimes no data will be available, and zero
   is returned, which is not an error condition. */
int sonicReadFloatFromStream(
    sonicStream stream,
    float *samples,
    int maxSamples)
{
    int numSamples = stream->numOutputSamples;
    int remainingSamples = 0;
    short *buffer;
    int count;

    if(numSamples == 0) {
        return 0;
    }
    if(numSamples > maxSamples) {
        remainingSamples = numSamples - maxSamples;
        numSamples = maxSamples;
    }
    buffer = stream->outputBuffer;
    count = numSamples*stream->numChannels;
    while(count--) {
        *samples++ = (*buffer++)/32767.0f;
    }
    if(remainingSamples > 0) {
        memmove(stream->outputBuffer, stream->outputBuffer + numSamples*stream->numChannels,
            remainingSamples*sizeof(short)*stream->numChannels);
    }
    stream->numOutputSamples = remainingSamples;
    return numSamples;
}

/* Read short data out of the stream.  Sometimes no data will be available, and zero
   is returned, which is not an error condition. */
// 浠庢祦涓鍙杝hort绫诲瀷鐨勬暟鎹紝濡傛灉娌℃湁鏁版嵁杩斿洖0
int sonicReadShortFromStream(
    sonicStream stream,
    short *samples,
    int maxSamples)
{
    int numSamples = stream->numOutputSamples;
    int remainingSamples = 0;

    if(numSamples == 0) {
        return 0;
    }
    if(numSamples > maxSamples) {
        remainingSamples = numSamples - maxSamples;
        numSamples = maxSamples;
    }
    memcpy(samples, stream->outputBuffer, numSamples*sizeof(short)*stream->numChannels);
    if(remainingSamples > 0) {
        memmove(stream->outputBuffer, stream->outputBuffer + numSamples*stream->numChannels,
            remainingSamples*sizeof(short)*stream->numChannels);
    }
    stream->numOutputSamples = remainingSamples;
    return numSamples;
}

/* Read unsigned char data out of the stream.  Sometimes no data will be available, and zero
   is returned, which is not an error condition. */
int sonicReadUnsignedCharFromStream(
    sonicStream stream,
    unsigned char *samples,
    int maxSamples)
{
    int numSamples = stream->numOutputSamples;
    int remainingSamples = 0;
    short *buffer;
    int count;

    if(numSamples == 0) {
        return 0;
    }
    if(numSamples > maxSamples) {
        remainingSamples = numSamples - maxSamples;
        numSamples = maxSamples;
    }
    buffer = stream->outputBuffer;
    count = numSamples*stream->numChannels;
    while(count--) {
        *samples++ = (char)((*buffer++) >> 8) + 128;
    }
    if(remainingSamples > 0) {
        memmove(stream->outputBuffer, stream->outputBuffer + numSamples*stream->numChannels,
            remainingSamples*sizeof(short)*stream->numChannels);
    }
    stream->numOutputSamples = remainingSamples;
    return numSamples;
}

/* Force the sonic stream to generate output using whatever data it currently
   has.  No extra delay will be added to the output, but flushing in the middle of
   words could introduce distortion. */
int sonicFlushStream(
    sonicStream stream)
{
    int maxRequired = stream->maxRequired;
    int remainingSamples = stream->numInputSamples;
    float speed = stream->speed/stream->pitch;
    float rate = stream->rate*stream->pitch;
    int expectedOutputSamples = stream->numOutputSamples +
        (int)((remainingSamples/speed + stream->numPitchSamples)/rate + 0.5f);

    /* Add enough silence to flush both input and pitch buffers. */
    if(!enlargeInputBufferIfNeeded(stream, remainingSamples + 2*maxRequired)) {
        return 0;
    }
    memset(stream->inputBuffer + remainingSamples*stream->numChannels, 0,
        2*maxRequired*sizeof(short)*stream->numChannels);
    stream->numInputSamples += 2*maxRequired;
    if(!sonicWriteShortToStream(stream, NULL, 0)) {
        return 0;
    }
    /* Throw away any extra samples we generated due to the silence we added */
    if(stream->numOutputSamples > expectedOutputSamples) {
        stream->numOutputSamples = expectedOutputSamples;
    }
    /* Empty input and pitch buffers */
    stream->numInputSamples = 0;
    stream->remainingInputToCopy = 0;
    stream->numPitchSamples = 0;
    return 1;
}

/* Return the number of samples in the output buffer */
int sonicSamplesAvailable(
   sonicStream stream)
{
    return stream->numOutputSamples;
}

/* If skip is greater than one, average skip samples together and write them to
   the down-sample buffer.  If numChannels is greater than one, mix the channels
   together as we down sample. */
static void downSampleInput(
    sonicStream stream,
    short *samples,
    int skip)
{
    int numSamples = stream->maxRequired/skip;
    int samplesPerValue = stream->numChannels*skip;
    int i, j;
    int value;
    short *downSamples = stream->downSampleBuffer;

    for(i = 0; i < numSamples; i++) {
        value = 0;
        for(j = 0; j < samplesPerValue; j++) {
            value += *samples++;
        }
        value /= samplesPerValue;
        *downSamples++ = value;
    }
}

/* Find the best frequency match in the range, and given a sample skip multiple.
   For now, just find the pitch of the first channel. */
static int findPitchPeriodInRange(
    short *samples,
    int minPeriod,
    int maxPeriod,
    int *retMinDiff,
    int *retMaxDiff)
{
    int period, bestPeriod = 0, worstPeriod = 255;
    short *s, *p, sVal, pVal;
    unsigned long diff, minDiff = 1, maxDiff = 0;
    int i;

    for(period = minPeriod; period <= maxPeriod; period++) {
        diff = 0;
        s = samples;
        p = samples + period;
        for(i = 0; i < period; i++) {
            sVal = *s++;
            pVal = *p++;
            diff += sVal >= pVal? (unsigned short)(sVal - pVal) :
                (unsigned short)(pVal - sVal);
        }
        /* Note that the highest number of samples we add into diff will be less
           than 256, since we skip samples.  Thus, diff is a 24 bit number, and
           we can safely multiply by numSamples without overflow */
        /* if (bestPeriod == 0 || (bestPeriod*3/2 > period && diff*bestPeriod < minDiff*period) ||
                diff*bestPeriod < (minDiff >> 1)*period) {*/
        if (bestPeriod == 0 || diff*bestPeriod < minDiff*period) {
            minDiff = diff;
            bestPeriod = period;
        }
        if(diff*worstPeriod > maxDiff*period) {
            maxDiff = diff;
            worstPeriod = period;
        }
    }
    *retMinDiff = minDiff/bestPeriod;
    *retMaxDiff = maxDiff/worstPeriod;
    return bestPeriod;
}

/* At abrupt ends of voiced words, we can have pitch periods that are better
   approximated by the previous pitch period estimate.  Try to detect this case. */
static int prevPeriodBetter(
    sonicStream stream,
    int period,
    int minDiff,
    int maxDiff,
    int preferNewPeriod)
{
    if(minDiff == 0 || stream->prevPeriod == 0) {
        return 0;
    }
    if(preferNewPeriod) {
        if(maxDiff > minDiff*3) {
            /* Got a reasonable match this period */
            return 0;
        }
        if(minDiff*2 <= stream->prevMinDiff*3) {
            /* Mismatch is not that much greater this period */
            return 0;
        }
    } else {
        if(minDiff <= stream->prevMinDiff) {
            return 0;
        }
    }
    return 1;
}

/* Find the pitch period.  This is a critical step, and we may have to try
   multiple ways to get a good answer.  This version uses Average Magnitude
   Difference Function (AMDF).  To improve speed, we down sample by an integer
   factor get in the 11KHz range, and then do it again with a narrower
   frequency range without down sampling */
static int findPitchPeriod(
    sonicStream stream,
    short *samples,
    int preferNewPeriod)
{
    int minPeriod = stream->minPeriod;
    int maxPeriod = stream->maxPeriod;
    int sampleRate = stream->sampleRate;
    int minDiff, maxDiff, retPeriod;
    int skip = 1;
    int period;

    if(sampleRate > SONIC_AMDF_FREQ && stream->quality == 0) {
        skip = sampleRate/SONIC_AMDF_FREQ;
    }
    if(stream->numChannels == 1 && skip == 1) {
        period = findPitchPeriodInRange(samples, minPeriod, maxPeriod, &minDiff, &maxDiff);
    } else {
        downSampleInput(stream, samples, skip);
        period = findPitchPeriodInRange(stream->downSampleBuffer, minPeriod/skip,
            maxPeriod/skip, &minDiff, &maxDiff);
        if(skip != 1) {
            period *= skip;
            minPeriod = period - (skip << 2);
            maxPeriod = period + (skip << 2);
            if(minPeriod < stream->minPeriod) {
                minPeriod = stream->minPeriod;
            }
            if(maxPeriod > stream->maxPeriod) {
                maxPeriod = stream->maxPeriod;
            }
            if(stream->numChannels == 1) {
                period = findPitchPeriodInRange(samples, minPeriod, maxPeriod,
                    &minDiff, &maxDiff);
            } else {
                downSampleInput(stream, samples, 1);
                period = findPitchPeriodInRange(stream->downSampleBuffer, minPeriod,
                    maxPeriod, &minDiff, &maxDiff);
            }
        }
    }
    if(prevPeriodBetter(stream, period, minDiff, maxDiff, preferNewPeriod)) {
        retPeriod = stream->prevPeriod;
    } else {
        retPeriod = period;
    }
    stream->prevMinDiff = minDiff;
    stream->prevPeriod = period;
    return retPeriod;
}

/* Overlap two sound segments, ramp the volume of one down, while ramping the
   other one from zero up, and add them, storing the result at the output. */
static void overlapAdd(
    int numSamples,
    int numChannels,
    short *out,
    short *rampDown,
    short *rampUp)
{
    short *o, *u, *d;
    int i, t;

    for(i = 0; i < numChannels; i++) {
        o = out + i;
        u = rampUp + i;
        d = rampDown + i;
        for(t = 0; t < numSamples; t++) {
#ifdef SONIC_USE_SIN
            float ratio = sin(t*M_PI/(2*numSamples));
            *o = *d*(1.0f - ratio) + *u*ratio;
#else
            *o = (*d*(numSamples - t) + *u*t)/numSamples;
#endif
            o += numChannels;
            d += numChannels;
            u += numChannels;
        }
    }
}

/* Overlap two sound segments, ramp the volume of one down, while ramping the
   other one from zero up, and add them, storing the result at the output. */
static void overlapAddWithSeparation(
    int numSamples,
    int numChannels,
    int separation,
    short *out,
    short *rampDown,
    short *rampUp)
{
    short *o, *u, *d;
    int i, t;

    for(i = 0; i < numChannels; i++) {
        o = out + i;
        u = rampUp + i;
        d = rampDown + i;
        for(t = 0; t < numSamples + separation; t++) {
            if(t < separation) {
                *o = *d*(numSamples - t)/numSamples;
                d += numChannels;
            } else if(t < numSamples) {
                *o = (*d*(numSamples - t) + *u*(t - separation))/numSamples;
                d += numChannels;
                u += numChannels;
            } else {
                *o = *u*(t - separation)/numSamples;
                u += numChannels;
            }
            o += numChannels;
        }
    }
}

/* Just move the new samples in the output buffer to the pitch buffer */
static int moveNewSamplesToPitchBuffer(
    sonicStream stream,
    int originalNumOutputSamples)
{
    int numSamples = stream->numOutputSamples - originalNumOutputSamples;
    int numChannels = stream->numChannels;

    if(stream->numPitchSamples + numSamples > stream->pitchBufferSize) {
        stream->pitchBufferSize += (stream->pitchBufferSize >> 1) + numSamples;
        stream->pitchBuffer = (short *)realloc(stream->pitchBuffer,
            stream->pitchBufferSize*sizeof(short)*numChannels);
        if(stream->pitchBuffer == NULL) {
            return 0;
        }
    }
    memcpy(stream->pitchBuffer + stream->numPitchSamples*numChannels,
        stream->outputBuffer + originalNumOutputSamples*numChannels,
        numSamples*sizeof(short)*numChannels);
    stream->numOutputSamples = originalNumOutputSamples;
    stream->numPitchSamples += numSamples;
    return 1;
}

/* Remove processed samples from the pitch buffer. */
static void removePitchSamples(
    sonicStream stream,
    int numSamples)
{
    int numChannels = stream->numChannels;
    short *source = stream->pitchBuffer + numSamples*numChannels;

    if(numSamples == 0) {
        return;
    }
    if(numSamples != stream->numPitchSamples) {
        memmove(stream->pitchBuffer, source, (stream->numPitchSamples -
            numSamples)*sizeof(short)*numChannels);
    }
    stream->numPitchSamples -= numSamples;
}

/* Change the pitch.  The latency this introduces could be reduced by looking at
   past samples to determine pitch, rather than future. */
static int adjustPitch(
    sonicStream stream,
    int originalNumOutputSamples)
{
    float pitch = stream->pitch;
    int numChannels = stream->numChannels;
    int period, newPeriod, separation;
    int position = 0;
    short *out, *rampDown, *rampUp;

    if(stream->numOutputSamples == originalNumOutputSamples) {
        return 1;
    }
    if(!moveNewSamplesToPitchBuffer(stream, originalNumOutputSamples)) {
        return 0;
    }
    while(stream->numPitchSamples - position >= stream->maxRequired) {
        period = findPitchPeriod(stream, stream->pitchBuffer + position*numChannels, 0);
        newPeriod = period/pitch;
        if(!enlargeOutputBufferIfNeeded(stream, newPeriod)) {
            return 0;
        }
        out = stream->outputBuffer + stream->numOutputSamples*numChannels;
        if(pitch >= 1.0f) {
            rampDown = stream->pitchBuffer + position*numChannels;
            rampUp = stream->pitchBuffer + (position + period - newPeriod)*numChannels;
            overlapAdd(newPeriod, numChannels, out, rampDown, rampUp);
        } else {
            rampDown = stream->pitchBuffer + position*numChannels;
            rampUp = stream->pitchBuffer + position*numChannels;
            separation = newPeriod - period;
            overlapAddWithSeparation(period, numChannels, separation, out, rampDown, rampUp);
        }
        stream->numOutputSamples += newPeriod;
        position += period;
    }
    removePitchSamples(stream, position);
    return 1;
}

/* Aproximate the sinc function times a Hann window from the sinc table. */
static int findSincCoefficient(int i, int ratio, int width) {
    int lobePoints = (SINC_TABLE_SIZE-1)/SINC_FILTER_POINTS;
    int left = i*lobePoints + (ratio*lobePoints)/width;
    int right = left + 1;
    int position = i*lobePoints*width + ratio*lobePoints - left*width;
    int leftVal = sincTable[left];
    int rightVal = sincTable[right];

    return ((leftVal*(width - position) + rightVal*position) << 1)/width;
}

/* Return 1 if value >= 0, else -1.  This represents the sign of value. */
static int getSign(int value) {
    return value >= 0? 1 : 0;
}

/* Interpolate the new output sample. */
static short interpolate(
    sonicStream stream,
    short *in,
    int oldSampleRate,
    int newSampleRate)
{
    /* Compute N-point sinc FIR-filter here.  Clip rather than overflow. */
    int i;
    int total = 0;
    int position = stream->newRatePosition*oldSampleRate;
    int leftPosition = stream->oldRatePosition*newSampleRate;
    int rightPosition = (stream->oldRatePosition + 1)*newSampleRate;
    int ratio = rightPosition - position - 1;
    int width = rightPosition - leftPosition;
    int weight, value;
    int oldSign;
    int overflowCount = 0;

    for (i = 0; i < SINC_FILTER_POINTS; i++) {
        weight = findSincCoefficient(i, ratio, width);
        /* printf("%u %f\n", i, weight); */
        value = in[i*stream->numChannels]*weight;
        oldSign = getSign(total);
        total += value;
        if (oldSign != getSign(total) && getSign(value) == oldSign) {
            /* We must have overflowed.  This can happen with a sinc filter. */
            overflowCount += oldSign;
        }
    }
    /* It is better to clip than to wrap if there was a overflow. */
    if (overflowCount > 0) {
        return SHRT_MAX;
    } else if (overflowCount < 0) {
        return SHRT_MIN;
    }
    return total >> 16;
}

/* Change the rate.  Interpolate with a sinc FIR filter using a Hann window. */
static int adjustRate(
    sonicStream stream,
    float rate,
    int originalNumOutputSamples)
{
    int newSampleRate = stream->sampleRate/rate;
    int oldSampleRate = stream->sampleRate;
    int numChannels = stream->numChannels;
    int position = 0;
    short *in, *out;
    int i;
    int N = SINC_FILTER_POINTS;

    /* Set these values to help with the integer math */
    while(newSampleRate > (1 << 14) || oldSampleRate > (1 << 14)) {
        newSampleRate >>= 1;
        oldSampleRate >>= 1;
    }
    if(stream->numOutputSamples == originalNumOutputSamples) {
        return 1;
    }
    if(!moveNewSamplesToPitchBuffer(stream, originalNumOutputSamples)) {
        return 0;
    }
    /* Leave at least N pitch sample in the buffer */
    for(position = 0; position < stream->numPitchSamples - N; position++) {
        while((stream->oldRatePosition + 1)*newSampleRate >
                stream->newRatePosition*oldSampleRate) {
            if(!enlargeOutputBufferIfNeeded(stream, 1)) {
                return 0;
            }
            out = stream->outputBuffer + stream->numOutputSamples*numChannels;
            in = stream->pitchBuffer + position*numChannels;
            for(i = 0; i < numChannels; i++) {
                *out++ = interpolate(stream, in, oldSampleRate, newSampleRate);
                in++;
            }
            stream->newRatePosition++;
            stream->numOutputSamples++;
        }
        stream->oldRatePosition++;
        if(stream->oldRatePosition == oldSampleRate) {
            stream->oldRatePosition = 0;
            if(stream->newRatePosition != newSampleRate) {
                fprintf(stderr,
                    "Assertion failed: stream->newRatePosition != newSampleRate\n");
                exit(1);
            }
            stream->newRatePosition = 0;
        }
    }
    removePitchSamples(stream, position);
    return 1;
}

/* Skip over a pitch period, and copy period/speed samples to the output */
static int skipPitchPeriod(
    sonicStream stream,
    short *samples,
    float speed,
    int period)
{
    long newSamples;
    int numChannels = stream->numChannels;

    if(speed >= 2.0f) {
        newSamples = period/(speed - 1.0f);
    } else {
        newSamples = period;
        stream->remainingInputToCopy = period*(2.0f - speed)/(speed - 1.0f);
    }
    if(!enlargeOutputBufferIfNeeded(stream, newSamples)) {
        return 0;
    }
    overlapAdd(newSamples, numChannels, stream->outputBuffer +
        stream->numOutputSamples*numChannels, samples, samples + period*numChannels);
    stream->numOutputSamples += newSamples;
    return newSamples;
}

/* Insert a pitch period, and determine how much input to copy directly. */
static int insertPitchPeriod(
    sonicStream stream,
    short *samples,
    float speed,
    int period)
{
    long newSamples;
    short *out;
    int numChannels = stream->numChannels;

    if(speed < 0.5f) {
        newSamples = period*speed/(1.0f - speed);
    } else {
        newSamples = period;
        stream->remainingInputToCopy = period*(2.0f*speed - 1.0f)/(1.0f - speed);
    }
    if(!enlargeOutputBufferIfNeeded(stream, period + newSamples)) {
        return 0;
    }
    out = stream->outputBuffer + stream->numOutputSamples*numChannels;
    memcpy(out, samples, period*sizeof(short)*numChannels);
    out = stream->outputBuffer + (stream->numOutputSamples + period)*numChannels;
    overlapAdd(newSamples, numChannels, out, samples + period*numChannels, samples);
    stream->numOutputSamples += period + newSamples;
    return newSamples;
}

/* Resample as many pitch periods as we have buffered on the input.  Return 0 if
   we fail to resize an input or output buffer. */
// 灏藉彲鑳藉鐨勯噸閲囨牱杈撳叆缂撳啿鍖轰腑鐨勫熀闊冲懆鏈燂紝濡傛灉鎴愬姛杩斿洖闈?
static int changeSpeed(
    sonicStream stream,
    float speed)
{
    short *samples;
    int numSamples = stream->numInputSamples;
    int position = 0, period, newSamples;
    int maxRequired = stream->maxRequired;

    /* printf("Changing speed to %f\n", speed); */
    if(stream->numInputSamples < maxRequired) {
        return 1;
    }
    do {
        // 娴佷腑鍓╀綑鐨勯噰鏍风偣鐨勪釜鏁?
        if(stream->remainingInputToCopy > 0) {
            //
            newSamples = copyInputToOutput(stream, position);
            position += newSamples;
        } else {
            samples = stream->inputBuffer + position*stream->numChannels;
            period = findPitchPeriod(stream, samples, 1);
            if(speed > 1.0) {
                newSamples = skipPitchPeriod(stream, samples, speed, period);
                position += period + newSamples;
            } else {
                newSamples = insertPitchPeriod(stream, samples, speed, period);
                position += newSamples;
            }
        }
        if(newSamples == 0) {
            return 0; /* Failed to resize output buffer */
        }
    } while(position + maxRequired <= numSamples);
    removeInputSamples(stream, position);
    return 1;
}

/* Resample as many pitch periods as we have buffered on the input.  Return 0 if
   we fail to resize an input or output buffer.  Also scale the output by the volume. */
// 灏藉彲鑳藉鐨勫皢杈撳叆缂撳啿鍖轰腑鐨勫熀闊冲懆鏈熻繘琛岄噸閲囨牱锛屽鏋滃け璐ヨ繑鍥?锛屽鏋滄垚鍔熻繑鍥?銆傚悓鏃朵篃scale浜嗛煶閲?
static int processStreamInput(
    sonicStream stream)
{
    // 娴佷腑杈撳嚭缂撳啿鍖轰腑鍘熸湁鐨勯噰鏍风偣鐨勬暟閲?
    int originalNumOutputSamples = stream->numOutputSamples;
    // 閫熷害
    float speed = stream->speed/stream->pitch;
    float rate = stream->rate;
    // 濡傛灉涓嶇敤chordPitch
    if(!stream->useChordPitch) {
        rate *= stream->pitch;
    }
    // 鏀瑰彉閫熷害
    if(speed > 1.00001 || speed < 0.99999) {
        changeSpeed(stream, speed);
    } else {
        if(!copyToOutput(stream, stream->inputBuffer, stream->numInputSamples)) {
            return 0;
        }
        stream->numInputSamples = 0;
    }
    if(stream->useChordPitch) {
        if(stream->pitch != 1.0f) {
            if(!adjustPitch(stream, originalNumOutputSamples)) {
                return 0;
            }
        }
    } else if(rate != 1.0f) {
        if(!adjustRate(stream, rate, originalNumOutputSamples)) {
            return 0;
        }
    }
    if(stream->volume != 1.0f) {
        /* Adjust output volume. */
        scaleSamples(stream->outputBuffer + originalNumOutputSamples*stream->numChannels,
            (stream->numOutputSamples - originalNumOutputSamples)*stream->numChannels,
            stream->volume);
    }
    return 1;
}

/* Write floating point data to the input buffer and process it. */
int sonicWriteFloatToStream(
    sonicStream stream,
    float *samples,
    int numSamples)
{
    if(!addFloatSamplesToInputBuffer(stream, samples, numSamples)) {
        return 0;
    }
    return processStreamInput(stream);
}

/* Simple wrapper around sonicWriteFloatToStream that does the short to float
   conversion for you. */
   // 鍚戞祦涓啓鍏hort绫诲瀷鐨勬暟鎹苟杩涜澶勭悊
int sonicWriteShortToStream(
    sonicStream stream,
    short *samples,
    int numSamples)
{
    if(!addShortSamplesToInputBuffer(stream, samples, numSamples)) {
        return 0;
    }
    // 澶勭悊杈撳叆娴佺殑鏁版嵁
    return processStreamInput(stream);
}

/* Simple wrapper around sonicWriteFloatToStream that does the unsigned char to float
   conversion for you. */
int sonicWriteUnsignedCharToStream(
    sonicStream stream,
    unsigned char *samples,
    int numSamples)
{
    if(!addUnsignedCharSamplesToInputBuffer(stream, samples, numSamples)) {
        return 0;
    }
    return processStreamInput(stream);
}

/* This is a non-stream oriented interface to just change the speed of a sound sample */
int sonicChangeFloatSpeed(
    float *samples,
    int numSamples,
    float speed,
    float pitch,
    float rate,
    float volume,
    int useChordPitch,
    int sampleRate,
    int numChannels)
{
    sonicStream stream = sonicCreateStream(sampleRate, numChannels);

    sonicSetSpeed(stream, speed);
    sonicSetPitch(stream, pitch);
    sonicSetRate(stream, rate);
    sonicSetVolume(stream, volume);
    sonicSetChordPitch(stream, useChordPitch);
    sonicWriteFloatToStream(stream, samples, numSamples);
    sonicFlushStream(stream);
    numSamples = sonicSamplesAvailable(stream);
    sonicReadFloatFromStream(stream, samples, numSamples);
    sonicDestroyStream(stream);
    return numSamples;
}

/* This is a non-stream oriented interface to just change the speed of a sound sample */
int sonicChangeShortSpeed(
    short *samples,
    int numSamples,
    float speed,
    float pitch,
    float rate,
    float volume,
    int useChordPitch,
    int sampleRate,
    int numChannels)
{   // 鍒涘缓骞跺垵濮嬪寲娴?
    sonicStream stream = sonicCreateStream(sampleRate, numChannels);
    // 璁剧疆娴佺殑閫熷害
    sonicSetSpeed(stream, speed);
    // 璁剧疆娴佺殑闊宠皟
    sonicSetPitch(stream, pitch);
    // 璁剧疆娴佺殑閫熺巼
    sonicSetRate(stream, rate);
    // 璁剧疆娴佺殑闊抽噺
    sonicSetVolume(stream, volume);
    // 璁剧疆
    sonicSetChordPitch(stream, useChordPitch);
    // 鍚戞祦涓啓鍏hort绫诲瀷鐨勬暟鎹?
    sonicWriteShortToStream(stream, samples, numSamples);
    sonicFlushStream(stream);
    numSamples = sonicSamplesAvailable(stream);
    sonicReadShortFromStream(stream, samples, numSamples);
    sonicDestroyStream(stream);
    return numSamples;
}

#endif // SONIC_IMPLEMENTATION
