// Type definitions
interface FrameData {
    data: ArrayBuffer;
    codec?: string;
}

interface CallbackEvent {
    t: 'mseError' | 'startPlay';
}

type CallbackFunction = (event: CallbackEvent) => void;

interface Mp4EventData {
    data: ArrayBuffer;
    codec: string;
}

type MediaSourceConstructor = {
    new (): MediaSource;
    isTypeSupported?(codec: string): boolean;
};
declare global {
    interface Window {
        ManagedMediaSource?: MediaSourceConstructor;
    }
}
class MsMediaSource {
    private mediaSource: MediaSource | null = null;

    private videoElement: HTMLVideoElement | null = null;

    private sourceBuffer: SourceBuffer | null = null;

    private frameBuffer: FrameData[] = [];

    private updateend: number = 1;

    private mimeCodec: string = "";

    private initFlag: number = 0;

    private cb: CallbackFunction;

    private currentSegmentIndex: number = 0;

    private skipDistance: number = MsMediaSource.skipCount;

    private isPlayback: boolean = false; // false: preview, true: playback

    // Buffer management optimization
    private readonly MAX_FRAME_BUFFER_SIZE: number = 300; // Limit frame buffer size

    private readonly BUFFER_WINDOW_SIZE: number = 15; // Keep 15 seconds of buffer (Frigate strategy)

    constructor(cb: CallbackFunction) {
        this.cb = cb;
    }

    static get statusIdel(): number { return 0; }

    static get statusWait(): number { return 1; }

    static get statusNormal(): number { return 2; }

    static get statusError(): number { return 3; }

    static get statusDestroy(): number { return 4; }

    static get skipCount(): number { return 5; } // Frame skip catch-up count

    initMse(codec: string): boolean {
        // Unified selection of available MediaSource constructor (prefer ManagedMediaSource)
        const MediaSourceCtor = (window.ManagedMediaSource ?? window.MediaSource) as MediaSourceConstructor | undefined;
        if (!MediaSourceCtor) {
            console.error("MediaSource API is not supported!");
            return false;
        }

        // if (!window.MediaSource.isTypeSupported(codec)) {
        //     console.log(codec);
        //     console.error("Unsupported MIME type or codec: ", codec);
        //     return false;
        // }
        this.mimeCodec = codec;

        try {
            // create video
            this.videoElement?.addEventListener("error", this.videoErrorCallback.bind(this));

            // create mse
            this.mediaSource = new MediaSourceCtor();

            // video url
            if (this.videoElement) {
                this.videoElement.src = window.URL.createObjectURL(this.mediaSource);
            }

            // mse event
            this.mediaSource.addEventListener("sourceopen", () => {
                console.log("ms mse open.");
                this.uninitSourceBuffer();
                this.initSourceBuffer();
            });

            this.mediaSource.addEventListener("sourceclose", () => {
                console.log("ms mse close.");
            });

            this.mediaSource.addEventListener("sourceended", () => {
                console.log("ms mse ended.");
            });

            this.mediaSource.addEventListener("error", () => {
                console.log("ms mse error.");
            });

            this.mediaSource.addEventListener("abort", () => {
                console.log("ms mse abort.");
            });
        } catch (e) {
            console.log((e as Error).message);
            return false;
        }

        this.videoElement?.pause();
        return true;
    }

    videoErrorCallback(e: Event): void {
        try {
            const target = e.target as HTMLVideoElement;
            if (target?.error) {
                // Suppress errors during cleanup (when src is empty or element is being destroyed)
                if (target.src === '' || !this.videoElement) {
                    return;
                }
                
                switch (target.error.code) {
                    case target.error.MEDIA_ERR_ABORTED:
                        // Suppress abort errors during cleanup
                        return;
                    case target.error.MEDIA_ERR_NETWORK:
                        console.error("video tag error : A network error caused the media download to fail.");
                        break;
                    case target.error.MEDIA_ERR_DECODE:
                        console.error("video tag error : The media playback was aborted due to a corruption problem or because the media used features your browser did not support.");
                        break;
                    case target.error.MEDIA_ERR_SRC_NOT_SUPPORTED:
                        console.error("video tag error : The media could not be loaded, either because the server or network failed or because the format is not supported.");
                        break;
                    default:
                        console.error(`video tag error : An unknown media error occurred.${target.error.code}`);
                        break;
                }
            }

            // Only try to reinitialize if videoElement still exists and is not being destroyed
            if (!this.videoElement || this.initFlag === MsMediaSource.statusDestroy) {
                return;
            }

            // Mark as destroyed and notify external
            this.initFlag = MsMediaSource.statusDestroy;
            this.cb({ t: 'mseError' });

            // Try to reinitialize MSE (preserve existing mimeCodec and videoElement)
            const codec = this.mimeCodec;
            // First completely clean up to avoid residual state
            this.uninitMse();
            this.initFlag = MsMediaSource.statusIdel;
            if (codec && this.videoElement) {
                // Slight delay to avoid immediate rebuild in the same event loop as error trigger
                setTimeout(() => {
                    // Double check videoElement still exists before reinitializing
                    if (this.videoElement && this.initMse(codec)) {
                        this.initFlag = MsMediaSource.statusNormal;
                        // If there are buffered frames, continue driving playback
                        this.updateSourceBuffer();
                    } else {
                        this.initFlag = MsMediaSource.statusError;
                    }
                }, 300);
            }
        } catch {
            // Ignore errors during cleanup
        }
    }

    static makeBuffer(buffer1: Uint8Array, buffer2: Uint8Array): Uint8Array {
        const tmp = new Uint8Array(buffer1.byteLength + buffer2.byteLength);
        tmp.set(new Uint8Array(buffer1), 0);
        tmp.set(new Uint8Array(buffer2), buffer1.byteLength);
        return tmp;
    }

    initSourceBuffer(): number {
        if (this.sourceBuffer !== null) {
            return -1;
        }

        if (!this.mediaSource) {
            return -1;
        }

        this.sourceBuffer = this.mediaSource.addSourceBuffer(this.mimeCodec);
        this.currentSegmentIndex = 0;
        const curMode = this.sourceBuffer.mode;
        if (curMode === 'segments') {
            this.sourceBuffer.mode = 'sequence';
        }
        this.skipDistance = MsMediaSource.skipCount;
        
        this.sourceBuffer.addEventListener("updateend", () => {
            try {
                if (this.sourceBuffer !== null && this.mediaSource?.readyState === 'open' && this.videoElement) {
                    const { buffered } = this.sourceBuffer;
                    // Guard: no ranges available
                    if (buffered.length === 0) {
                        this.updateend = 1;
                        return;
                    }
                    // Clamp currentSegmentIndex
                    if (this.currentSegmentIndex >= buffered.length) {
                        this.currentSegmentIndex = buffered.length - 1;
                    }
                    this.handleTimeUpdate();
                    const end = this.sourceBuffer.buffered.end(this.currentSegmentIndex);
                    const { currentTime } = this.videoElement;

                    // Adaptive skip strategy for live preview
                    if (!this.isPlayback) {
                        const bufferTime = end - currentTime;

                        // Reset skip distance when buffer is large
                        if (bufferTime >= 1 && this.skipDistance === 0) {
                            this.skipDistance = MsMediaSource.skipCount;
                        }

                        // Adaptive jump based on buffer size
                        if (bufferTime >= 0.5 && this.skipDistance) {
                            // Jump closer to live edge as buffer grows
                            const jumpOffset = Math.min(0.4, bufferTime * 0.8);
                            this.videoElement.currentTime = end - jumpOffset;
                            this.skipDistance--;
                        }
                    }

                    // Optimized buffer cleanup: use 15-second rolling window (Frigate strategy)
                    if (!this.sourceBuffer.updating && buffered.length > 0) {
                        const bufferEnd = buffered.end(buffered.length - 1);
                        const bufferStart = buffered.start(0);
                        const removeEnd = bufferEnd - this.BUFFER_WINDOW_SIZE;

                        // Remove old data if we have more than 15 seconds buffered
                        if (removeEnd > bufferStart && currentTime > bufferStart) {
                            const safeRemoveEnd = Math.min(removeEnd, currentTime - 1);
                            if (safeRemoveEnd > bufferStart) {
                                this.sourceBuffer.remove(bufferStart, safeRemoveEnd);

                                // Set live seekable range for better latency calculation
                                if (this.mediaSource && 'setLiveSeekableRange' in this.mediaSource) {
                                    try {
                                        (this.mediaSource as any).setLiveSeekableRange(safeRemoveEnd, bufferEnd);
                                    } catch {
                                        // Ignore if not supported
                                    }
                                }
                            }
                        }
                    }
                }
            } catch (error) {
                console.log(error);
            }
            this.updateend = 1;
        });

        return 0;
    }

    handleTimeUpdate(): void {
        if (!this.sourceBuffer || !this.videoElement) return;
        
        const { buffered } = this.sourceBuffer;
        if (buffered.length === 0 || this.currentSegmentIndex === buffered.length - 1 || this.isPlayback) {
            return;
        }
        if (buffered.length && this.currentSegmentIndex >= buffered.length) {
            this.currentSegmentIndex = buffered.length - 1;
            return;
        }
        const { currentTime } = this.videoElement;
        const nextSegmentIndex = this.currentSegmentIndex + 1;
        const currentStart = buffered.start(this.currentSegmentIndex);
        const currentEnd = buffered.end(this.currentSegmentIndex);
        const nextStart = buffered.start(nextSegmentIndex);
        // const nextEnd = buffered.end(nextSegmentIndex);
        
        console.log(`currentTime=${currentTime}, currentStart=${currentStart}, currentEnd=${currentEnd}, nextStart=${nextStart}`);
        
        // If current time has exceeded next segment start time, delete current cache
        this.currentSegmentIndex += 1;
        this.videoElement.currentTime = nextStart;
        this.sourceBuffer.remove(0, currentEnd);
        this.videoElement.play();
        this.skipDistance = MsMediaSource.skipCount;
    }

    uninitSourceBuffer(): void {
        if (this.sourceBuffer === null || !this.mediaSource) {
            return;
        }
        // this.sourceBuffer.removeEventListener("updateend", this.removeUpdateCallback);
        for (let i = 0; i < this.mediaSource.sourceBuffers.length; i++) {
            this.mediaSource.removeSourceBuffer(this.mediaSource.sourceBuffers[i]);
        }
        this.sourceBuffer = null;
    }

    updateSourceBuffer(): void {
        if (this.sourceBuffer === null || this.updateend !== 1 || this.sourceBuffer.updating) {
            return;
        }

        const len = this.frameBuffer.length;
        if (len === 0) {
            return;
        }

        // Optimized: calculate total size first, then allocate once
        let totalSize = 0;
        for (let i = 0; i < len; i++) {
            totalSize += this.frameBuffer[i].data.byteLength;
        }

        // Allocate buffer once instead of repeatedly
        const segmentBuffer = new Uint8Array(totalSize);
        let offset = 0;

        // Copy all frames in one pass
        for (let i = 0; i < len; i++) {
            const frameData = new Uint8Array(this.frameBuffer[i].data);
            segmentBuffer.set(frameData, offset);
            offset += frameData.byteLength;
        }

        // Clear buffer after copying (more efficient than repeated shift())
        this.frameBuffer = [];

        try {
            this.sourceBuffer.appendBuffer(segmentBuffer);
            this.updateend = 0;
            if (this.videoElement?.paused) {
                this.videoElement.style.display = "";
                this.videoElement.play();
                this.cb({
                    t: 'startPlay',
                });
            }
        } catch (e) {
            console.error(`appending error: [update=${this.sourceBuffer.updating}, updateend=${this.updateend}, length=${len}, buffered.length=${this.sourceBuffer.buffered.length}]==>${e}`);
            this.initFlag = MsMediaSource.statusDestroy;
            this.cb({
                t: 'mseError',
            });
        }
    }

    processMp4VideoData(event: { data: Mp4EventData }, snapshotFlag: number): void {
        const objData = event.data;

        if (this.initFlag === MsMediaSource.statusIdel) {
            this.frameBuffer = [];
            this.initFlag = MsMediaSource.statusWait;
            if (this.initMse(objData.codec)) {
                this.initFlag = MsMediaSource.statusNormal;
            } else {
                this.initFlag = MsMediaSource.statusError;
            }
        }

        if (document.hidden) {
            this.skipDistance = MsMediaSource.skipCount;
        }

        // Buffer size limit: prevent memory overflow
        if (this.frameBuffer.length >= this.MAX_FRAME_BUFFER_SIZE) {
            // Drop oldest frames if buffer is full (backpressure)
            console.warn(`Frame buffer full (${this.frameBuffer.length}), dropping oldest frames`);
            this.frameBuffer.splice(0, Math.floor(this.MAX_FRAME_BUFFER_SIZE * 0.3)); // Drop 30%
        }

        this.frameBuffer.push(objData);
        if (snapshotFlag === 0) {
            this.updateSourceBuffer();
        }
    }

    processMp4AudioData(event: { data: Mp4EventData }): void {
        const objData = event.data;

        if (this.initFlag === MsMediaSource.statusIdel) {
            this.frameBuffer = [];
            this.initFlag = MsMediaSource.statusWait;
            if (this.initMse(objData.codec)) {
                this.initFlag = MsMediaSource.statusNormal;
            } else {
                this.initFlag = MsMediaSource.statusError;
            }
        }

        // Buffer size limit for audio as well
        if (this.frameBuffer.length >= this.MAX_FRAME_BUFFER_SIZE) {
            console.warn(`Audio frame buffer full (${this.frameBuffer.length}), dropping oldest frames`);
            this.frameBuffer.splice(0, Math.floor(this.MAX_FRAME_BUFFER_SIZE * 0.3));
        }

        this.frameBuffer.push(objData);
        this.updateSourceBuffer();
    }

    setVideoElement(video: HTMLVideoElement): void {
        this.videoElement = video;
    }

    setPlayMode(playback: boolean): void {
        this.isPlayback = playback;
    }

    clearBuffer(): void {
        // Clear frame buffer to stop processing new frames
        this.frameBuffer = [];
        // Clear source buffer if it exists and is not updating
        if (this.sourceBuffer && !this.sourceBuffer.updating && this.mediaSource && this.mediaSource.readyState === 'open') {
            try {
                const { buffered } = this.sourceBuffer;
                if (buffered.length > 0) {
                    const end = buffered.end(buffered.length - 1);
                    this.sourceBuffer.remove(0, end);
                }
            } catch {
                // Ignore errors during buffer clearing
            }
        }
        this.currentSegmentIndex = 0;
    }

    uninitMse(): void {
        if (this.videoElement !== null) {
            this.videoElement.removeEventListener("error", this.videoErrorCallback);
            window.URL.revokeObjectURL(this.videoElement.src);
            this.videoElement.src = "";
        }

        this.uninitSourceBuffer();
        this.mediaSource = null;
        this.videoElement = null;
        this.sourceBuffer = null;
        this.frameBuffer = [];
        this.updateend = 1;
        this.mimeCodec = "";
        this.initFlag = MsMediaSource.statusIdel;
    }
}

export default MsMediaSource;