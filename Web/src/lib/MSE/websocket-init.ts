let ws: WebSocket | null = null;
const ctx: any = globalThis as any;

let currentUrl: string | null = null;
let shouldReconnect = false;

let retryCount = 0;
const maxRetryCount = 3;
const retryWindowMs = 60 * 1000;
let firstRetryTime: number | null = null;
const baseDelay = 1000; // ms
let retryTimer: number | null = null;
 
function clearRetryTimer(): void {
    if (retryTimer !== null) {
        clearTimeout(retryTimer);
        retryTimer = null;
    }
}

function safeClose(socket: WebSocket): void {
    try {
        if (socket.readyState === WebSocket.CONNECTING) {
            socket.addEventListener('open', () => {
                try {
                    socket.close(1000, 'teardown');
                } catch (error) {
                    ctx.postMessage({ type: 'error', error: String(error) });
                }
            }, { once: true });
        } else if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CLOSING) {
            console.log('safeClose', socket.readyState);
            socket.close(1000, 'teardown');
        }
    } catch (error) {
        ctx.postMessage({ type: 'error', error: String(error) });
    }
}

function teardown(resetUrl = false): void {
    if (ws) {
        const socket = ws;
        ws = null;
        safeClose(socket);
    }

    if (resetUrl) {
        currentUrl = null;
    }

    clearRetryTimer();
}

function scheduleReconnect(): void {
    if (!shouldReconnect || !currentUrl) {
        return;
    }

    const now = Date.now();
    
    // If this is the first reconnection, record the timestamp
    if (firstRetryTime === null) {
        firstRetryTime = now;
    }
    
    // Check if more than 1 minute have passed
    if (now - firstRetryTime >= retryWindowMs) {
        // If more than 10 minutes have passed, reset the count and timestamp
        retryCount = 0;
        firstRetryTime = now;
    }
    
    // Check the number of reconnection attempts in the last 1 minute
    if (retryCount >= maxRetryCount) {
        ctx.postMessage({ type: 'close', code: 4999, reason: 'Max retries reached in 10 minutes' });
        return;
    }

    retryCount += 1;
    const expo = Math.min(baseDelay * (2 ** (retryCount - 1)), 10000);
    const jitter = Math.floor(Math.random() * 300);
    const delay = expo + jitter;
    clearRetryTimer();
    retryTimer = setTimeout(() => {
        attemptConnect(currentUrl!);
    }, delay) as unknown as number;
}

function attemptConnect(url: string): void {
    teardown(false);

    try {
        ws = new WebSocket(url);
        ws.binaryType = 'arraybuffer';

        ws.onopen = () => {
            retryCount = 0;
            firstRetryTime = null; // Reset the time window
            clearRetryTimer();
            ctx.postMessage({ type: 'open' });
        };

        ws.onclose = (event: CloseEvent) => {
            const wasManual = !shouldReconnect;
            teardown(!shouldReconnect);

            if (wasManual) {
                ctx.postMessage({ type: 'close', code: event.code, reason: event.reason });
                return;
            }

            // 1000-1006: Normal/Strategic close does not reconnect
            if (event.code >= 1000 && event.code <= 1005) {
                ctx.postMessage({ type: 'close', code: event.code, reason: event.reason });
                return;
            }
            scheduleReconnect();
        };

        ws.onerror = () => {
            ctx.postMessage({ type: 'error', error: 'WebSocket error' });
            scheduleReconnect();
        };

        ws.onmessage = (event: MessageEvent) => {
            const payload = event.data;
            const message = { type: 'video-data', payload } as const;
            if (payload instanceof ArrayBuffer) {
                ctx.postMessage(message, [payload]);
            } else {
                ctx.postMessage(message);
            }
        };
    } catch (error) {
        ctx.postMessage({ type: 'error', error: String(error) });
        scheduleReconnect();
    }
}

(globalThis as any).onmessage = (e: MessageEvent) => {
    const data = e.data || {};
    switch (data.type) {
        case 'connect': {
            const { url } = data;
            if (!url) {
                ctx.postMessage({ type: 'error', error: 'Missing url for connect' });
                return;
            }

            if (ws && currentUrl === url) {
                const state = ws.readyState;
                if (state === WebSocket.OPEN || state === WebSocket.CONNECTING) {
                    // Already connected or connected to the same URL
                    return;
                }
            }

            shouldReconnect = true;
            currentUrl = url;
            retryCount = 0;
            firstRetryTime = null; // Reset the time window
            clearRetryTimer();
            attemptConnect(url);
            break;
        }
        case 'disconnect': {
            shouldReconnect = false;
            teardown(true);
            break;
        }
        case 'send': {
            const { payload } = data;
            if (ws && ws.readyState === WebSocket.OPEN) {
                try {
                    ws.send(payload as any);
                } catch (error) {
                    ctx.postMessage({ type: 'error', error: String(error) });
                }
            }
            break;
        }
        default:
            break;
    }
};