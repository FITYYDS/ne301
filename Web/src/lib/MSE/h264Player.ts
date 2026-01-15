import {
    playerStateIdle,
    specialDuration,
    maxDuration
} from './constant'
import browser from './utils/myBrowser.js';
import { getValueFromArrayBuffer } from './utils/binary.js'
import MsMediaSource from './media'
import { sleep } from '@/utils/index.js';

type SaveAsFn = (data: Blob | File | MediaSource | string, filename?: string, options?: any) => void;
let saveAs: SaveAsFn | null = null;

// Dynamically load FileSaver
const loadFileSaver = async () => {
    try {
        const script = document.createElement('script');
        script.src = '/libs/FileSaver.min.js';
        script.async = true;
        document.head.appendChild(script);

        return new Promise<void>((resolve, reject) => {
            script.onload = () => {
                // eslint-disable-next-line @typescript-eslint/ban-ts-comment
                // @ts-expect-error
                saveAs = window.saveAs;
                resolve();
            };
            script.onerror = reject;
        });
    } catch (err) {
        console.warn('FileSaver load failed:', err);
    }
};

loadFileSaver();
/**
 * @TODO
 * Player log panel
 * - WebSocket packet transmission rate
 * - Video stream frame rate
 * - Packet loss rate
 * - Latency
 * - Buffer data amount
 * - 
 */
// Type definitions
interface CallbackEvent {
    t: string;
}

type CallbackFunction = (event: CallbackEvent) => void;

interface FrameData {
    cmd: string;
    data: Uint8Array;
    videoTime: number;
    iChannelId: number;
    // userData: {
    //     iframe: number | number[];
    //     timeUsec: number;
    //     index: number;
    //     duration: number;
    //     size: number;
    //     realDuration: number;
    // };
}

interface ReturnResult {
    e: number;
    m: string;
}

interface WsWorkerMessage {
    type: 'open' | 'close' | 'error' | 'video-data';
    error?: string;
    payload?: ArrayBuffer;
    code?: number;
    reason?: string;
}

interface DecWorkerMessage {
    type: 'ready' | 'error';
    error?: string;
}

/**
 * @description: H264 player class
 */
export default class H264Player {
    private videoPlayer: MsMediaSource | null = null;

    private decodeWorker: Worker | null = null;

    private webSocketWorker: Worker | null = null;

    private decoderWorker: Worker | null = null;

    private firstFrame: number = 0;

    private frameIndex: number = 0;

    private lastVideoTime: number = 0;

    private isPlayback: boolean = false;

    private playSpeed: number = 1;

    private direction: number = 0;

    private cb: CallbackFunction;

    private type: string = 'video';

    private snapshotFlag: number = 0;

    private lastSec: number = 0;

    private videoFrameCnt: number = 0;

    private playerState: number = playerStateIdle;

    private iChannelId: number = 0;

    private videoElement: HTMLVideoElement | null = null;

    private wsRetryCount: number = 3;

    private wsUrl: string = '';

    private isStarted: boolean = false;

    private isConnected: boolean = false;

    // private hederBits: Uint8Array = new Uint8Array(0);

    public packetCount: number = 0;

    public packetsPerSecond: number = 0;

    private lastPacketSec: number = 0;

    public currentTime: number = 0;

    public currentFrame: number = 0;

    // H264 raw stream capture
    private captureActive: boolean = false;

    private captureStartMs: number = 0;

    private captureDurationMs: number = 0;

    private captureParts: Uint8Array[] = [];

    private captureTotalBytes: number = 0;

    // Add getter method for debugging
    getCurrentTime(): number {
        return this.currentTime;
    }

    constructor(cb: CallbackFunction) {
        this.cb = cb;
        this.creatVideoPlayer();
        this.webSocketWorker = null;
        this.decodeWorker = null;
        this.wsRetryCount = 3;
        this.wsUrl = '';
        this.isStarted = false;
        this.isConnected = false;
        // this.hederBits = new Uint8Array(0);
        this.packetCount = 0;
        this.packetsPerSecond = 0;
        this.lastPacketSec = 0;
        this.currentTime = 0;
        this.currentFrame = 0;
    }

    /**
     * Setup WebSocket worker message handler
     */
    private setupWebSocketWorkerHandler(): void {
        if (!this.webSocketWorker) return;
        
        this.webSocketWorker.onmessage = (ev: MessageEvent<WsWorkerMessage>) => {
            const msg = ev.data;
            if (!msg || typeof msg !== 'object') return;
            
            switch (msg.type) {
                case 'open':
                    window.dispatchEvent(new CustomEvent('wv_open'));
                    break;
                case 'close':
                    window.dispatchEvent(new CustomEvent('wv_close', { 
                        detail: { code: msg.code, reason: msg.reason } 
                    }));
                    break;
                case 'error':
                    window.dispatchEvent(new CustomEvent('wv_error', { detail: msg.error }));
                    this.wsRetryCount -= 1;
                    if (this.wsRetryCount > 0) {
                        this.resetStartState().start(this.wsUrl);
                    }
                    break;
                case 'video-data': {
                    // Count packets per second
                    const nowSec = Math.floor(Date.now() / 1000);
                    if (this.lastPacketSec !== nowSec) {
                        if (this.lastPacketSec > 0) {
                            this.packetsPerSecond = this.packetCount;
                        }
                        this.lastPacketSec = nowSec;
                        this.packetCount = 0;
                    }
                    this.packetCount++;
                    
                    if (msg.payload instanceof ArrayBuffer) {
                        const frameData = this.dealVideoData(msg.payload);
                        if (frameData) {
                            this.decoderWorker?.postMessage(frameData);
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        };
    }

    /**
     * Setup decoder worker message handler
     */
    private setupDecoderWorkerHandler(): void {
        if (!this.decoderWorker) return;
        
        this.decoderWorker.onmessage = async (ev: MessageEvent<DecWorkerMessage>) => {
            this.videoMsgCallback(ev as unknown as MessageEvent);
        };
    }

    /**
     * Initialize workers (idempotent - safe to call multiple times)
     */
    initWorkers(): this {
        const needWsWorker = !this.webSocketWorker;
        const needDecoderWorker = !this.decoderWorker;
        
        // If both workers exist, just ensure handlers are set up
        if (!needWsWorker && !needDecoderWorker) {
            this.setupWebSocketWorkerHandler();
            this.setupDecoderWorkerHandler();
            return this;
        }

        // Start connection, notify external to show loading
        window.dispatchEvent(new CustomEvent('wv_work', { detail: false }));
        if (needWsWorker) {
            this.webSocketWorker = new Worker(new URL('./websocket-init.ts', import.meta.url), { type: 'module' });
        }
        
        if (needDecoderWorker) {
            this.decoderWorker = new Worker(new URL('./videoWorker.ts', import.meta.url));
        }
        this.setupWebSocketWorkerHandler();
        this.setupDecoderWorkerHandler();
        
        return this;
    }

    initPlayer(videoElement: HTMLVideoElement): this {
        this.videoElement = videoElement;
        this.videoPlayer = new MsMediaSource((msg: CallbackEvent) => {
            this.mediaSrcCallback(msg);
        });
        this.videoPlayer.setVideoElement(this.videoElement);
        return this;
    }

    wsDisconnect(): this {
        // debugger;
        this.isConnected = false;
        this.webSocketWorker?.postMessage({ type: 'disconnect' });
        return this;
    }

    wsConnect(url: string): this {
        this.isConnected = true;
        this.webSocketWorker?.postMessage({ type: 'connect', url });
        return this;
    }

    resetStartState(): this {
        this.isStarted = false;
        return this;
    }

    async start(url: string): Promise<this> {
        this.wsUrl = url;
        if (this.isStarted) return this;
        if (this.isConnected) {
            this.wsDisconnect();
            await sleep(500);
        }
        this.isStarted = true;
        this.initWorkers();
        this.wsConnect(url);
        this.decodeWorker?.postMessage({ type: 'init' });
        window.dispatchEvent(new CustomEvent('wv_work', { detail: true }));
        return this;
    }

    destroy(): void {
        this.stopPlay();
        if (this.webSocketWorker) {
            this.wsDisconnect();
            try {
                this.webSocketWorker.terminate();
            } catch (error) {
                console.error('Error terminating websocket worker', error);
            }
            this.webSocketWorker = null;
        }
        if (this.decoderWorker) {
            try {
                this.decoderWorker.terminate();
            } catch (error) {
                console.error('Error terminating decoder worker', error);
            }
            this.decoderWorker = null;
        }
        if (this.videoPlayer) {
            this.videoPlayer.uninitMse();
            this.videoPlayer = null;
        }
        
        this.videoElement = null;
        this.isStarted = false;
        this.isConnected = false;
        this.wsUrl = '';
    }

   /**
    * Pause video stream
    * - close websocket connection
    * - don't destroy mse, just stop feed stream to mse
    */
    pause(): void {
        // Disconnect websocket
        if (this.webSocketWorker) {
            this.wsDisconnect();
            setTimeout(() => {
                this.webSocketWorker?.terminate();
                this.webSocketWorker = null;
            }, 100);
        }
        this.videoPlayer?.clearBuffer();
        if (this.videoElement && !this.videoElement.paused) {
            this.videoElement.pause();
        }
        this.isConnected = false;
        this.isStarted = false;
    }
    
    /**
     * Restart video stream
     * - init websocket connection
     * - close mse buffer and feed stream to mse
     */
    async reStart(): Promise<void> {
        if (!this.wsUrl || !this.videoElement) return;

        // Disconnect if already connected
        if (this.isConnected && this.webSocketWorker) {
            this.wsDisconnect();
            await sleep(500);
        }

        // Initialize workers (idempotent - will create missing ones and setup handlers)
        this.initWorkers();

        // Ensure videoPlayer exists and is initialized
        if (!this.videoPlayer) {
            this.creatVideoPlayer();
        }
        
        if (this.videoPlayer && this.videoElement) {
            this.videoPlayer.setVideoElement(this.videoElement);
        }

        // Clear MSE buffer before restarting
        this.videoPlayer?.clearBuffer();

        // Reconnect websocket - this will start feeding data to the stream
        this.wsConnect(this.wsUrl);
        
        // Reinitialize decoder worker to prepare for new stream
        this.decoderWorker?.postMessage({ type: 'init' });
        
        // Notify external that work has started
        window.dispatchEvent(new CustomEvent('wv_work', { detail: true }));
        
        // Reset state
        this.isStarted = true;
        this.isConnected = true;
    }

    videoMsgCallback(event: MessageEvent): void {
        this.videoPlayer?.processMp4VideoData(event, this.snapshotFlag);
    }

    mediaSrcCallback(msg: CallbackEvent): void {
        this.cb(msg);
    }

    creatVideoPlayer(): void {
        this.videoPlayer = new MsMediaSource((msg: CallbackEvent) => {
            this.mediaSrcCallback(msg);
        });
    }

    initDecodeWorker(): void {
        this.decodeWorker = new Worker(new URL('./videoWorker.ts', import.meta.url));
        this.decodeWorker.onmessage = (event: MessageEvent) => {
            this.videoMsgCallback(event);
        };
    }

    dealVideoData(frameData: ArrayBuffer): FrameData | false {
        const header = frameData.slice(0, 64);
        // Timestamp is at bytes 8-15 in header (8 bytes data)
        let timestamp = 0;
        try {
            // Read 8 bytes from bytes 8-15
            const timestampBytes = header.slice(8, 16);
            const view = new DataView(timestampBytes);
            const timestamp32 = view.getUint32(4, false); // Read last 4 bytes, big-endian

            // Use last 4 bytes timestamp (usually second-level timestamp)
            timestamp = timestamp32;
            this.currentTime = timestamp;
        } catch (e) {
            console.error('Error getting timestamp from offset 8:', e);
        }

        // If no valid timestamp found, use current time
        if (timestamp === 0) {
            timestamp = Math.floor(Date.now() / 1000);
        }

        this.currentTime = timestamp;
        const bytes = new Uint8Array(frameData);
        const frameHeader = new ArrayBuffer(88); // frameHeaderSize = 88
        const streamData = new ArrayBuffer(frameHeader.byteLength + bytes.byteLength);
        const fullView = new Uint8Array(streamData);

        // Set some basic frame header information
        const headerView = new DataView(frameHeader);
        headerView.setUint32(56, 1, true); // streamCodec = H264 (codecType.H264 = 1)
        headerView.setUint32(60, 1, true); // iframe = 1 (assume I-frame)

        // Set timestamp (current time in microseconds)
        const now = Date.now() * 1000; // Convert to microseconds
        headerView.setUint32(80, now & 0xFFFFFFFF, true);
        headerView.setUint32(84, (now / 0x100000000) >>> 0, true);

        // Copy frame header and video data
        fullView.set(new Uint8Array(frameHeader), 0);
        fullView.set(bytes, frameHeader.byteLength);

        let duration = 0; // us

        // 1. Parse frame header information
        const timeUsec = Date.now() * 1000;
        // const dataLength = streamData.byteLength - frameHeaderSize;
        const iframe = getValueFromArrayBuffer('int', streamData, 60, 1);
        const nowSec = parseInt((timeUsec / 1000).toString(), 10);
        if (this.lastSec !== nowSec) {
            this.lastSec = nowSec;
            this.videoFrameCnt = 0;
        }
        this.videoFrameCnt++;
        // Set single frame playback duration
        if (this.firstFrame === 0) {
            if (iframe !== 0) {
                // return;
            }
            this.firstFrame = 1;
            // 25fps: 1 second / 25 frames = 40 milliseconds = 40000 microseconds
            duration = 40000;
        } else if (!this.isPlayback) {
            // Preview page - keep microsecond unit
            duration = parseInt(((timeUsec - this.lastVideoTime) / 1000).toString(), 10);
            if (duration < 0 || duration > maxDuration * 1000000) {  // Changed to microseconds
                duration = specialDuration * 1000000;  // Changed to microseconds
            }
        } else if (this.direction === 0) {
            if (this.playSpeed >= 4 || this.playSpeed <= 0.125) {
                // speed >= 4 or <= 1/8, fixed duration 1s, MSE set currentTime. @initSourceBuffer
                duration = specialDuration * 1000000;
            } else {
                duration = parseInt((timeUsec - this.lastVideoTime).toString(), 10);
                duration = parseInt((duration / this.playSpeed).toString(), 10);
                if (duration < 0 || duration > maxDuration * 1000000) {
                    duration = maxDuration * 1000000;
                }
            }
        } else {
            // rewind, only I frame, fixed duration 1s, MSE set currentTime. @initSourceBuffer
            duration = specialDuration * 1000000;
        }

        this.frameIndex++;
        this.lastVideoTime = timeUsec;
        // Return a hexadecimal (using bytes to avoid uninitialized variable)
        // const hexData = Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join(' ');
        // console.log('hexData', hexData);
        // Construct data format expected by Worker
        // JMuxer requires H.264 NAL unit data with start code
        const headSize = 64
        const h264Data = new Uint8Array(frameData, headSize);

        if (!H264Player.checkFrameData(frameData)) {
            return false;
        }

        // If capture is active, extract AnnexB raw stream and accumulate
        if (this.captureActive) {
            this.appendH264Capture(frameData);
        }

        const objData = {
            cmd: 'video',
            data: h264Data,
            videoTime: duration,
            iChannelId: this.iChannelId,

        };
        return objData;
    }

    // Start capturing H264 raw stream (Annex B), default 20 seconds
    public startH264Capture(durationSeconds: number = 20): void {
        this.captureActive = true;
        this.captureStartMs = Date.now();
        this.captureDurationMs = Math.max(1, durationSeconds) * 1000;
        this.captureParts = [];
        this.captureTotalBytes = 0;
    }

    // Immediately export and stop capture
    public stopAndExportH264Capture(): void {
        if (!this.captureActive) return;
        this.exportH264Capture();
    }

    private appendH264Capture(frameData: ArrayBuffer): void {
        // Extract all bytes starting from the first 0x00000001 start code after 64 bytes
        const totalLen = frameData.byteLength;
        if (totalLen <= 68) return;
        const dv = new DataView(frameData);
        const start = 64;
        const end = totalLen - 4;
        let pos = -1;
        for (let i = start; i <= end; i += 1) {
            if (dv.getUint32(i, false) === 0x00000001) {
                pos = i;
                break;
            }
        }
        if (pos === -1) return;

        const raw = new Uint8Array(frameData, pos);
        // Make a copy to avoid data changes due to source buffer reuse
        const copy = new Uint8Array(raw.byteLength);
        copy.set(raw);
        this.captureParts.push(copy);
        this.captureTotalBytes += copy.byteLength;

        if (Date.now() - this.captureStartMs >= this.captureDurationMs) {
            this.exportH264Capture();
        }
    }

    private exportH264Capture(): void {
        // Merge into a single Uint8Array
        const out = new Uint8Array(this.captureTotalBytes);
        let offset = 0;
        for (let i = 0; i < this.captureParts.length; i += 1) {
            const part = this.captureParts[i];
            out.set(part, offset);
            offset += part.byteLength;
        }

        const blob = new Blob([out], { type: 'application/octet-stream' });
        const filename = `capture_${new Date().toISOString().replace(/[:.]/g, '-')}.h264`;
        if (saveAs) {
            saveAs(blob, filename);
        } else {
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = filename;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
        }

        // Reset capture state
        this.captureActive = false;
        this.captureStartMs = 0;
        this.captureDurationMs = 0;
        this.captureParts = [];
        this.captureTotalBytes = 0;
    }

    static checkFrameData(frameData: ArrayBuffer): boolean {
        const len = frameData.byteLength;
        if (len === 0) return false;

        // First scan in 4-byte blocks
        const u32 = new Uint32Array(frameData, 0, (len / 4) | 0);
        for (let i = 0; i < u32.length; i += 1) {
            if (u32[i] !== 0) return true;
        }

        // Handle remaining tail bytes
        const bytes = new Uint8Array(frameData, (u32.length << 2));
        for (let i = 0; i < bytes.length; i += 1) {
            if (bytes[i] !== 0) return true;
        }
        return false;
    }

    doSnapshot() {
        let interval = 0;
        this.snapshotFlag = 1
        if (this.snapshotFlag === 0) {
            return 0;
        }

        if (this.snapshotFlag === 1) {
            this.snapshotFlag = 2;
            const video = this.videoElement as HTMLVideoElement;
            const canvas = document.createElement("canvas");
            const canvasCtx = canvas.getContext("2d");
            if (!canvasCtx) {
                console.error("Failed to get canvas 2D context");
                this.snapshotFlag = 0;
                return 0;
            }
            canvas.width = video?.videoWidth || 0;
            canvas.height = video?.videoHeight || 0;
            if (browser.isBrowserFirefox()) {
                const size = canvas.width * canvas.height;
                if (size >= 3000 * 3000) {
                    interval = 1500;
                } else if (size >= 2560 * 2560) {
                    interval = 1200;
                } else if (size >= 1920 * 1920) {
                    interval = 800;
                } else {
                    interval = 200;
                }
            }
            window.setTimeout(() => {
                canvasCtx.drawImage(video, 0, 0, canvas.width, canvas.height);
                canvas.toBlob((blob) => {
                    this.snapshotFlag = 0;
                    const snapshotName = `snapshot_${Date.now()}.jpeg`;
                    if (saveAs) {
                        saveAs(blob as Blob, snapshotName);
                    } else {
                        // Fallback: create download link
                        const url = URL.createObjectURL(blob as Blob);
                        const a = document.createElement('a');
                        a.href = url;
                        a.download = snapshotName;
                        document.body.appendChild(a);
                        a.click();
                        document.body.removeChild(a);
                        URL.revokeObjectURL(url);
                    }
                });
            }, interval);
        }

        return 1;
    }

    stopPlay(): ReturnResult {
        const ret = {
            e: 0,
            m: "Stop h264 Success"
        };

        this.firstFrame = 0;
        this.frameIndex = 0;
        this.lastVideoTime = 0;
        this.isPlayback = false;
        this.playSpeed = 1;
        // this.isPause = 0;
        this.direction = 0;// 0=forward, 1=reverse
        this.playerState = playerStateIdle
        this.snapshotFlag = 0;
        this.lastSec = 0;
        this.videoFrameCnt = 0;
        this.videoPlayer?.uninitMse();
        this.videoPlayer = null;
        if (this.type === 'audio') {
            if (this.videoElement) {
                document.body.removeChild(this.videoElement);
                this.videoElement = null;
            }
        }
        this.type = 'video';
        if (this.decodeWorker !== null) {
            this.decodeWorker.postMessage({ cmd: 'stop' });
            this.decodeWorker.terminate();
            this.decodeWorker = null
        }
        return ret;
    }

    displayLoop(opt: number): void {
        if (opt && this.playerState !== playerStateIdle) {
            requestAnimationFrame(this.displayLoop.bind(this));
        }
        this.videoPlayer?.updateSourceBuffer();
    }

    setPlayMode(opt: boolean): void {
        this.isPlayback = opt;
    }

    // setPlayPaused(opt: number): void {
    //     this.isPause = opt;
    // }
} 