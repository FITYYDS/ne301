import { useState, useEffect, useRef } from 'preact/hooks';
import H264Player from '@/lib/MSE/h264Player';
import Loading from '@/components/loading';
import PlayerPanel from './player-panel';
import deviceTool from '@/services/api/deviceTool';
import { toast } from 'sonner';
import { useLingui } from '@lingui/react';

type PlayerProps = {
  videoUrl: string;
  videoRendererInstance: React.RefObject<H264Player | null>;
  // setVideoRendererInstance: (inst: H264Player | null) => void;
}
export default function Player({ videoUrl, videoRendererInstance }: PlayerProps) {
  const [loading, setLoading] = useState(false);
  const [isPlay, setIsPlay] = useState(false);
  const { i18n } = useLingui();
  // isloading and isReload are mutually exclusive
  // const [isReload, setIsReload] = useState(false);
  const [isShowPanel, setIsShowPanel] = useState(false);
  // const [isShowFps, setIsShowFps] = useState(true);
  const [isFullscreen, setIsFullscreen] = useState(false);
  const videoRef = useRef<HTMLVideoElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const idleTimerRef = useRef<number | null>(null);
  const [isShowFps, setIsShowFps] = useState(false);
  const [fps, setFps] = useState(0);
  const [isControlPanel, setIsControlPanel] = useState(false);
  const { startVideoStreamReq, stopVideoStreamReq } = deviceTool;
  const [sysTime, setSysTime] = useState('');
  useEffect(() => {
    const initializePlayer = async () => {
      await startVideoStreamReq();
      if (!videoUrl) return;
      const video = videoRef.current; // HTMLVideoElement | null
      if (!video) return;
      videoRendererInstance.current = new H264Player(() => { });
      videoRendererInstance.current?.initPlayer(video);
      videoRendererInstance.current?.start(videoUrl);
      setIsPlay(true);  
    };
    initializePlayer();
    return () => {
      videoRendererInstance.current?.destroy();
      videoRendererInstance.current = null;
      stopVideoStreamReq();
    };
  }, [videoUrl]);
  // Add real-time time updates
  useEffect(() => {
    if (!videoRendererInstance.current) return;
    const interval = setInterval(() => {
      // If timestamp is 0 or invalid, use current system time
      let timestamp = videoRendererInstance.current?.currentTime ?? 0;
      // console.log('timestamp', timestamp);
      if (timestamp === 0 || timestamp < 1000000000 || timestamp > 2000000000) {
        // Get current UTC timestamp
        timestamp = Math.floor(Date.now() / 1000);
      }

      // Convert timestamp to year-month-day hour:minute:second (UTC time)
      const date = new Date(timestamp * 1000);
      const year = date.getUTCFullYear();
      const month = String(date.getUTCMonth() + 1).padStart(2, '0');
      const day = String(date.getUTCDate()).padStart(2, '0');
      const hour = String(date.getUTCHours()).padStart(2, '0');
      const minute = String(date.getUTCMinutes()).padStart(2, '0');
      const second = String(date.getUTCSeconds()).padStart(2, '0');
      const newSysTime = `${year}-${month}-${day} ${hour}:${minute}:${second}`;

      setSysTime(newSysTime);
      const packetsPerSecond = videoRendererInstance.current?.packetsPerSecond ?? 0;
      const currentPackets = videoRendererInstance.current?.packetCount ?? 0;
      setFps(packetsPerSecond || currentPackets);
    }, 1000); // Update every second
    return () => {
      clearInterval(interval);
    };
  }, [videoRendererInstance.current]);

  const handleReload = () => {
    videoRendererInstance.current?.resetStartState().start(videoUrl);
    // setIsReload(false);
    setIsPlay(true);
    setLoading(false);
  };
  useEffect(() => {
    const handlWvClose = (e: Event) => {
      const event = e as CustomEvent<{ code?: number; reason?: string }>;
      if (event.detail.reason === 'Connection replaced') {
        toast.error(i18n._('sys.device_tool.preview_disconnected'));
      }
      setIsControlPanel(false);
      setLoading(false);
    };

    const handleWvWork = (e: Event) => {
      setLoading(!(e as CustomEvent<boolean>).detail);
      if ((e as CustomEvent<boolean>).detail) {
        setIsControlPanel(true);
      } else {
        setIsControlPanel(false);
      }
    };
    const handleWvError = (e: Event) => {
      console.error('handleWvError', e);
    };

    window.addEventListener('wv_work', handleWvWork);
    window.addEventListener('wv_close', handlWvClose);
    window.addEventListener('wv_error', handleWvError);
    return () => {
      window.removeEventListener('wv_work', handleWvWork);
      window.removeEventListener('wv_close', handlWvClose);
    };
  }, [isPlay]);
  const handleSnapshot = () => {
    videoRendererInstance.current?.doSnapshot();
  };
  const handleFullscreen = async () => {
    try {
      if (!document.fullscreenElement) {
        await containerRef.current?.requestFullscreen?.();
      } else {
        await document.exitFullscreen();
      }
    } catch {
      setIsFullscreen(false);
    }
  };
  useEffect(() => {
    const onFsChange = () => {
      const isFs = Boolean(document.fullscreenElement);
      setIsFullscreen(isFs);
    };
    document.addEventListener('fullscreenchange', onFsChange);
    return () => document.removeEventListener('fullscreenchange', onFsChange);
  }, []);

  // Double-click to fullscreen
  useEffect(() => {
    const el = videoRef.current;
    if (!el) return;
    const onDbl = () => {
      handleFullscreen().catch(e => {
        console.error(e);
      });
    };
    el.addEventListener('dblclick', onDbl);
    return () => {
      el.removeEventListener('dblclick', onDbl);
    };
  }, [videoRef, handleFullscreen]);

  // Hide panel on mouse enter
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const clearIdle = () => {
      if (idleTimerRef.current !== null) {
        clearTimeout(idleTimerRef.current);
        idleTimerRef.current = null;
      }
    };
    const startIdle = () => {
      clearIdle();
      idleTimerRef.current = window.setTimeout(() => {
        setIsShowPanel(false);
      }, 3000);
    };
    const onEnter = () => {
      setIsShowPanel(true);
      startIdle();
    };
    const onLeave = () => {
      setIsShowPanel(false);
      clearIdle();
    };
    const onMove = () => {
      setIsShowPanel(true);
      startIdle();
    };
    el.addEventListener('mouseenter', onEnter);
    el.addEventListener('mouseleave', onLeave);
    el.addEventListener('mousemove', onMove);
    return () => {
      el.removeEventListener('mouseenter', onEnter);
      el.removeEventListener('mouseleave', onLeave);
      el.removeEventListener('mousemove', onMove);
      clearIdle();
    };
  }, []);

  return (
    <div className="w-full h-full">
      <div
        ref={containerRef}
        className="relative flex w-full h-full m-auto justify-center items-center overflow-hidden"
      >
        <div className="w-full h-full flex items-center justify-center bg-black">
          <div className="relative w-full">
            <video
              ref={videoRef}
              className="block w-full aspect-video "
              id="videoPlayer"
              muted
              playsInline
              autoPlay
              disableRemotePlayback
            />
            {/* Time display in the top-left corner of video */}
            {sysTime && (
              <div className="absolute md:top-4 top-2 md:left-4 left-2 bg-gray-800/50 text-white px-3 py-1 rounded text-sm font-mono">
                {sysTime}
              </div>
            )}
            {isShowFps && (
              <div className="absolute md:top-4 top-2 md:right-4 right-2 text-red-600 px-2 py-1 rounded text-sm font-mono">
                {fps} FPS
              </div>
            )}
          </div>
        </div>
        <PlayerPanel
          handleReload={handleReload}
          className={`absolute bottom-0 left-0 transition-all duration-300 ease-in-out ${isShowPanel
            ? 'opacity-100 translate-y-0'
            : 'opacity-0 translate-y-full'
            }`}
          isFullscreen={isFullscreen}
          snapshot={handleSnapshot}
          fullscreen={handleFullscreen}
          isControlPanel={isControlPanel}
          isShowFps={isShowFps}
          showFps={() => setIsShowFps(!isShowFps)}
        />
        {loading && (
          <div className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2">
            <Loading placeholder="Loading..." />
          </div>
        )}
      </div>
    </div>
  );
}
