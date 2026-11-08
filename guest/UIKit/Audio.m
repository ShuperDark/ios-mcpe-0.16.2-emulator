// AVFoundation (AVAudioSession) and AudioToolbox.
//
// FMOD drives a RemoteIO AudioUnit: we run its render callback on a guest
// thread and push the produced PCM (converted to interleaved int16) to the
// host audio device. AudioQueue/AudioFile are not available (FMOD decodes FSB
// banks itself).
#import <emu/Foundation.h>
#include <emu/host.h>

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STR(n) EMU_EXPORT NSString* const n = @#n
STR(AVAudioSessionCategoryAmbient);
STR(AVAudioSessionCategorySoloAmbient);
STR(AVAudioSessionCategoryPlayback);
STR(AVAudioSessionCategoryRecord);
STR(AVAudioSessionCategoryPlayAndRecord);
STR(AVAudioSessionModeDefault);
STR(AVAudioSessionInterruptionNotification);
STR(AVAudioSessionRouteChangeNotification);
STR(AVAudioSessionMediaServicesWereResetNotification);
STR(AVAudioSessionInterruptionTypeKey);
STR(AVAudioSessionInterruptionOptionKey);
STR(AVAudioSessionSilenceSecondaryAudioHintNotification);

@interface AVAudioSession : NSObject {
    NSString* _cat;
}
@end
@implementation AVAudioSession
+ (AVAudioSession*)sharedInstance {
    static AVAudioSession* s;
    if (!s) s = [[AVAudioSession alloc] init];
    return s;
}
- (BOOL)setActive:(BOOL)a error:(NSError**)e { (void)a; if (e) *e = nil; return YES; }
- (BOOL)setActive:(BOOL)a withOptions:(NSUInteger)o error:(NSError**)e { (void)o; return [self setActive:a error:e]; }
- (BOOL)setCategory:(NSString*)c error:(NSError**)e { [_cat release]; _cat = [c copy]; if (e) *e = nil; return YES; }
- (BOOL)setCategory:(NSString*)c withOptions:(NSUInteger)o error:(NSError**)e { (void)o; return [self setCategory:c error:e]; }
- (BOOL)setMode:(NSString*)m error:(NSError**)e { (void)m; if (e) *e = nil; return YES; }
- (NSString*)category { return _cat ? _cat : AVAudioSessionCategorySoloAmbient; }
- (NSString*)mode { return AVAudioSessionModeDefault; }
- (BOOL)isOtherAudioPlaying { return NO; }
- (BOOL)secondaryAudioShouldBeSilencedHint { return NO; }
- (double)sampleRate { return 48000.0; }
- (double)preferredSampleRate { return 48000.0; }
- (BOOL)setPreferredSampleRate:(double)r error:(NSError**)e { (void)r; if (e) *e = nil; return YES; }
- (double)IOBufferDuration { return 512.0 / 48000.0; }
- (BOOL)setPreferredIOBufferDuration:(double)d error:(NSError**)e { (void)d; if (e) *e = nil; return YES; }
- (double)outputLatency { return 0.02; }
- (double)inputLatency { return 0.0; }
- (NSInteger)outputNumberOfChannels { return 2; }
- (NSInteger)maximumOutputNumberOfChannels { return 2; }
- (BOOL)setPreferredOutputNumberOfChannels:(NSInteger)n error:(NSError**)e { (void)n; if (e) *e = nil; return YES; }
- (float)outputVolume { return 1.0f; }
- (BOOL)inputAvailable { return NO; }
- (BOOL)isInputAvailable { return NO; }
- (id)currentRoute { return nil; }
- (void)requestRecordPermission:(void (^)(BOOL))b { if (b) b(NO); }
- (BOOL)overrideOutputAudioPort:(NSUInteger)p error:(NSError**)e { (void)p; if (e) *e = nil; return YES; }
- (void)setDelegate:(id)d { (void)d; }
@end

// ===================================================================== AudioToolbox
typedef int32_t OSStatus;
typedef uint32_t UInt32;
typedef double Float64;
typedef struct {
    Float64 mSampleRate;
    UInt32 mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame, mChannelsPerFrame, mBitsPerChannel, mReserved;
} ASBD;
typedef struct {
    UInt32 componentType, componentSubType, componentManufacturer, componentFlags, componentFlagsMask;
} AudioComponentDescription;
typedef struct {
    UInt32 mNumberChannels, mDataByteSize;
    void* mData;
} AudioBuffer;
typedef struct {
    UInt32 mNumberBuffers;
    AudioBuffer mBuffers[2];
} AudioBufferList2;
typedef struct {
    Float64 mSampleTime;
    uint64_t mHostTime;
    Float64 mRateScalar;
    uint64_t mWordClockTime;
    uint8_t mSMPTETime[24];
    UInt32 mFlags, mReserved;
} AudioTimeStamp;
typedef OSStatus (*AURenderCallback)(void* refCon, UInt32* flags, const AudioTimeStamp* ts, UInt32 bus, UInt32 frames, void* ioData);

enum {
    kAudioFormatLinearPCM = 0x6C70636D,
    kAudioFormatFlagIsFloat = 1,
    kAudioFormatFlagIsNonInterleaved = 1u << 5,
};

typedef struct emu_audio_unit {
    AudioComponentDescription desc;
    ASBD fmt;
    AURenderCallback cb;
    void* ref;
    UInt32 max_frames;
    int running;
    pthread_t thread;
} emu_audio_unit;

static AudioComponentDescription g_component = {0x61756F75, 0x72696F63, 0x6170706C, 0, 0};  // 'auou' 'rioc' 'appl'

EMU_EXPORT void* AudioComponentFindNext(void* prev, const AudioComponentDescription* d) {
    if (prev) return NULL;
    if (d && d->componentType != 0x61756F75) return NULL;  // only output units
    return &g_component;
}
EMU_EXPORT OSStatus AudioComponentGetDescription(void* c, AudioComponentDescription* d) { (void)c; *d = g_component; return 0; }
EMU_EXPORT OSStatus AudioComponentInstanceNew(void* comp, void** out) {
    (void)comp;
    emu_audio_unit* u = calloc(1, sizeof(emu_audio_unit));
    u->desc = g_component;
    u->fmt.mSampleRate = 48000;
    u->fmt.mFormatID = kAudioFormatLinearPCM;
    u->fmt.mFormatFlags = kAudioFormatFlagIsFloat | 8 /*packed*/ | kAudioFormatFlagIsNonInterleaved;
    u->fmt.mBytesPerPacket = u->fmt.mBytesPerFrame = 4;
    u->fmt.mFramesPerPacket = 1;
    u->fmt.mChannelsPerFrame = 2;
    u->fmt.mBitsPerChannel = 32;
    u->max_frames = 4096;
    *out = u;
    return 0;
}
EMU_EXPORT OSStatus AudioComponentInstanceDispose(void* u) {
    extern OSStatus AudioOutputUnitStop(void*);
    AudioOutputUnitStop(u);
    free(u);
    return 0;
}
EMU_EXPORT OSStatus AudioUnitInitialize(void* u) { (void)u; return 0; }
EMU_EXPORT OSStatus AudioUnitUninitialize(void* u) { (void)u; return 0; }
EMU_EXPORT OSStatus AudioUnitSetProperty(void* uv, UInt32 prop, UInt32 scope, UInt32 elem, const void* data, UInt32 size) {
    (void)scope; (void)elem;
    emu_audio_unit* u = uv;
    switch (prop) {
    case 8:  // kAudioUnitProperty_StreamFormat
        if (size >= sizeof(ASBD)) memcpy(&u->fmt, data, sizeof(ASBD));
        break;
    case 23: {  // kAudioUnitProperty_SetRenderCallback
        const struct { AURenderCallback p; void* r; }* s = data;
        u->cb = s->p;
        u->ref = s->r;
    } break;
    case 14: u->max_frames = *(const UInt32*)data; break;  // MaximumFramesPerSlice
    default: break;
    }
    return 0;
}
EMU_EXPORT OSStatus AudioUnitGetProperty(void* uv, UInt32 prop, UInt32 scope, UInt32 elem, void* data, UInt32* size) {
    (void)scope; (void)elem;
    emu_audio_unit* u = uv;
    switch (prop) {
    case 8:
        if (*size >= sizeof(ASBD)) { memcpy(data, &u->fmt, sizeof(ASBD)); *size = sizeof(ASBD); }
        return 0;
    case 14: *(UInt32*)data = u->max_frames; *size = 4; return 0;
    case 2: *(Float64*)data = u->fmt.mSampleRate; *size = 8; return 0;  // SampleRate
    default: memset(data, 0, *size); return 0;
    }
}
EMU_EXPORT OSStatus AudioUnitGetPropertyInfo(void* u, UInt32 prop, UInt32 scope, UInt32 elem, UInt32* size, uint8_t* writable) {
    (void)u; (void)prop; (void)scope; (void)elem;
    if (size) *size = prop == 8 ? sizeof(ASBD) : 4;
    if (writable) *writable = 1;
    return 0;
}
EMU_EXPORT OSStatus AudioUnitAddPropertyListener(void* u, UInt32 p, void* l, void* r) { (void)u; (void)p; (void)l; (void)r; return 0; }
EMU_EXPORT OSStatus AudioUnitRemovePropertyListenerWithUserData(void* u, UInt32 p, void* l, void* r) { (void)u; (void)p; (void)l; (void)r; return 0; }
EMU_EXPORT OSStatus AudioUnitSetParameter(void* u, UInt32 id, UInt32 s, UInt32 e, float v, UInt32 o) { (void)u; (void)id; (void)s; (void)e; (void)v; (void)o; return 0; }
EMU_EXPORT OSStatus AudioUnitRender(void* u, UInt32* f, const AudioTimeStamp* ts, UInt32 bus, UInt32 n, void* data) {
    (void)u; (void)f; (void)ts; (void)bus; (void)n; (void)data;
    return -10863;  // kAudioUnitErr_CannotDoInCurrentContext (no input)
}

static int16_t clamp16(float v) {
    v *= 32767.0f;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    return (int16_t)v;
}

static void* render_main(void* arg) {
    emu_audio_unit* u = arg;
    pthread_setname_np("AURemoteIO::render");
    const UInt32 frames = 512;
    int ch = (int)(u->fmt.mChannelsPerFrame ? u->fmt.mChannelsPerFrame : 2);
    if (ch > 2) ch = 2;
    BOOL isFloat = (u->fmt.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
    BOOL nonInter = (u->fmt.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
    UInt32 bps = u->fmt.mBitsPerChannel ? u->fmt.mBitsPerChannel / 8 : 4;
    void* bufs[2];
    for (int i = 0; i < 2; i++) bufs[i] = calloc(1, frames * bps * 2);
    int16_t* out = calloc(1, frames * 2 * sizeof(int16_t));
    if (!emu_audio_open((int)u->fmt.mSampleRate, 2)) {
        NSLog(@"[AudioToolbox] host audio device unavailable");
    }
    AudioTimeStamp ts;
    memset(&ts, 0, sizeof ts);
    ts.mFlags = 1 | 2;  // sample time + host time valid
    while (u->running) {
        AudioBufferList2 abl;
        if (nonInter) {
            abl.mNumberBuffers = (UInt32)ch;
            for (int i = 0; i < ch; i++) {
                abl.mBuffers[i].mNumberChannels = 1;
                abl.mBuffers[i].mDataByteSize = frames * bps;
                abl.mBuffers[i].mData = bufs[i];
            }
        } else {
            abl.mNumberBuffers = 1;
            abl.mBuffers[0].mNumberChannels = (UInt32)ch;
            abl.mBuffers[0].mDataByteSize = frames * bps * (UInt32)ch;
            abl.mBuffers[0].mData = bufs[0];
        }
        UInt32 flags = 0;
        extern uint64_t mach_absolute_time(void);
        ts.mHostTime = mach_absolute_time();
        if (u->cb) u->cb(u->ref, &flags, &ts, 0, frames, &abl);
        ts.mSampleTime += frames;
        for (UInt32 f = 0; f < frames; f++) {
            for (int c = 0; c < 2; c++) {
                int sc = c < ch ? c : 0;
                float v = 0;
                if (nonInter) {
                    void* b = abl.mBuffers[sc].mData;
                    v = isFloat ? ((float*)b)[f] : (bps == 2 ? ((int16_t*)b)[f] / 32768.0f : ((int32_t*)b)[f] / 2147483648.0f);
                } else {
                    void* b = abl.mBuffers[0].mData;
                    size_t idx = (size_t)f * (size_t)ch + (size_t)sc;
                    v = isFloat ? ((float*)b)[idx] : (bps == 2 ? ((int16_t*)b)[idx] / 32768.0f : ((int32_t*)b)[idx] / 2147483648.0f);
                }
                if (flags & 0x10) v = 0;  // kAudioUnitRenderAction_OutputIsSilence
                out[f * 2 + c] = clamp16(v);
            }
        }
        emu_audio_write(out, (int)frames);
    }
    for (int i = 0; i < 2; i++) free(bufs[i]);
    free(out);
    return NULL;
}
EMU_EXPORT OSStatus AudioOutputUnitStart(void* uv) {
    emu_audio_unit* u = uv;
    if (u->running) return 0;
    u->running = 1;
    pthread_create(&u->thread, NULL, render_main, u);
    return 0;
}
EMU_EXPORT OSStatus AudioOutputUnitStop(void* uv) {
    emu_audio_unit* u = uv;
    if (!u->running) return 0;
    u->running = 0;
    pthread_join(u->thread, NULL);
    return 0;
}

// legacy AudioSession C API
EMU_EXPORT OSStatus AudioSessionInitialize(void* rl, void* mode, void* listener, void* data) { (void)rl; (void)mode; (void)listener; (void)data; return 0; }
EMU_EXPORT OSStatus AudioSessionSetActive(uint8_t a) { (void)a; return 0; }
EMU_EXPORT OSStatus AudioSessionSetProperty(UInt32 id, UInt32 size, const void* data) { (void)id; (void)size; (void)data; return 0; }
EMU_EXPORT OSStatus AudioSessionAddPropertyListener(UInt32 id, void* p, void* d) { (void)id; (void)p; (void)d; return 0; }
EMU_EXPORT OSStatus AudioSessionRemovePropertyListenerWithUserData(UInt32 id, void* p, void* d) { (void)id; (void)p; (void)d; return 0; }
EMU_EXPORT OSStatus AudioSessionGetPropertySize(UInt32 id, UInt32* size) { (void)id; *size = 8; return 0; }
EMU_EXPORT OSStatus AudioSessionGetProperty(UInt32 id, UInt32* size, void* data) {
    switch (id) {
    case 0x63687372:  // 'chsr' CurrentHardwareSampleRate (Float64)
        if (*size >= 8) { *(Float64*)data = 48000.0; *size = 8; }
        return 0;
    case 0x63686264:  // 'chbd' CurrentHardwareIOBufferDuration (Float32)
        *(float*)data = 512.0f / 48000.0f;
        *size = 4;
        return 0;
    case 0x63686f63:  // 'choc' CurrentHardwareOutputNumberChannels
        *(UInt32*)data = 2;
        *size = 4;
        return 0;
    case 0x63686f6c:  // 'chol' output latency (Float32)
        *(float*)data = 0.02f;
        *size = 4;
        return 0;
    default:
        memset(data, 0, *size);
        return 0;
    }
}

// AudioQueue / AudioFile: unsupported (FMOD falls back to its own codecs)
EMU_EXPORT OSStatus AudioQueueNewOutput(const void* f, void* cb, void* ud, void* rl, void* mode, UInt32 flags, void** q) {
    (void)f; (void)cb; (void)ud; (void)rl; (void)mode; (void)flags;
    if (q) *q = NULL;
    return -66680;  // kAudioQueueErr_InvalidDevice
}
#define AQ_FAIL(name) EMU_EXPORT OSStatus name() { return -66687; }
AQ_FAIL(AudioQueueAllocateBuffer)
AQ_FAIL(AudioQueueAllocateBufferWithPacketDescriptions)
AQ_FAIL(AudioQueueDispose)
AQ_FAIL(AudioQueueEnqueueBuffer)
AQ_FAIL(AudioQueueFlush)
AQ_FAIL(AudioQueueOfflineRender)
AQ_FAIL(AudioQueueSetOfflineRenderFormat)
AQ_FAIL(AudioQueueSetProperty)
AQ_FAIL(AudioQueueStart)
AQ_FAIL(AudioQueueStop)
EMU_EXPORT OSStatus AudioFileOpenWithCallbacks(void* c, void* r, void* w, void* gs, void* ss, UInt32 hint, void** out) {
    (void)c; (void)r; (void)w; (void)gs; (void)ss; (void)hint;
    if (out) *out = NULL;
    return 0x7479703f;  // kAudioFileUnsupportedFileTypeError 'typ?'
}
EMU_EXPORT OSStatus AudioFileClose(void* f) { (void)f; return 0; }
EMU_EXPORT OSStatus AudioFileGetProperty(void* f, UInt32 id, UInt32* size, void* data) { (void)f; (void)id; (void)size; (void)data; return -50; }
EMU_EXPORT OSStatus AudioFileGetPropertyInfo(void* f, UInt32 id, UInt32* size, UInt32* w) { (void)f; (void)id; (void)size; (void)w; return -50; }
EMU_EXPORT OSStatus AudioFileReadPacketData(void* f, uint8_t c, UInt32* n, void* d, int64_t s, UInt32* p, void* b) {
    (void)f; (void)c; (void)n; (void)d; (void)s; (void)p; (void)b;
    return -50;
}
