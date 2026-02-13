import SvgIcon from '@/components/svg-icon'
import { cn } from '@/lib/utils';
import { Button } from '@/components/ui/button';
import { useLingui } from '@lingui/react';
import deviceTool from '@/services/api/deviceTool';
import { toast } from 'sonner';
import { useAiStatusStore } from '@/store/aiStatus';
import { debounce } from 'throttle-debounce';
import { useMemo, useEffect, useState } from 'preact/hooks';

type PlayerPanelProps = {
    handleReload: () => void;
    snapshot: () => void;
    className?: string;
    isFullscreen: boolean;
    fullscreen: () => void;
    isControlPanel: boolean;
    isShowStreamStats: boolean;
    toggleStreamStats: () => void;
}
export default function PlayerPanel({ handleReload, isFullscreen, snapshot, fullscreen, isControlPanel, className, isShowStreamStats, toggleStreamStats }: PlayerPanelProps) {
    const { i18n } = useLingui();
    const { toggleAiReq } = deviceTool;
    const { isAiInference, setIsAiInference, aiStatus } = useAiStatusStore();
    const { photoCaptureReq } = deviceTool;
    const [captureDisabled, setCaptureDisabled] = useState(false);
    const handleAiInferenceChange = async (value: boolean) => {
        try {
            await toggleAiReq({ ai_enabled: value })
            setIsAiInference(value)
        } catch (error) {
            console.error('handleAiInferenceChange', error)
            throw error
        }
    }
    const handlePhotoCameraClick = async () => {
        try {
            setCaptureDisabled(true);
            await photoCaptureReq({ enable_ai: true, chunk_size: 0, store_to_sd: false })
            toast.success(i18n._('sys.device_tool.photo_capture_success'))
        } catch (error) {
            console.error('handlePhotoCameraClick', error)
            throw error
        } finally {
            setCaptureDisabled(false);
        }
    }

    const debouncedHandlePhotoCameraClick = useMemo(
        () => debounce(2000, handlePhotoCameraClick, { atBegin: true }),
        []
    )

    useEffect(() => () => {
        debouncedHandlePhotoCameraClick.cancel?.()
    }, [debouncedHandlePhotoCameraClick])
    return (
        <div className={cn("w-full md:h-[60px] h-[40px] md:px-12 px-8 flex items-center justify-between transition-all duration-300 ease-in-out bg-gradient-to-t from-black/70 via-black/30 to-transparent", className)}>
            <div className="flex items-center gap-4">
                <div className="relative md:w-8 w-6 md:h-8 h-6 flex items-center justify-center cursor-pointer group">
                    <Button
                      variant="ghost"
                      onClick={handleReload}
                      className="md:w-8 w-6 md:h-8 h-6 !p-0 flex items-center justify-center cursor-pointer"
                    >
                        <SvgIcon className="w-full !h-full flex-1" icon="reload" />
                    </Button>
                    <span className="pointer-events-none absolute -top-7 left-1/2 -translate-x-1/2 whitespace-nowrap rounded bg-black/75 px-2 py-0.5 text-[10px] text-white opacity-0 transition-opacity group-hover:opacity-100">
                        {i18n._('sys.device_tool.reload')}
                    </span>
                </div>
            </div>
            <div className="flex gap-3">
                <div className="relative md:w-8 w-6 md:h-8 h-6 flex items-center justify-center cursor-pointer group">
                    <Button
                      disabled={!isControlPanel || captureDisabled}
                      variant="ghost"
                      onClick={debouncedHandlePhotoCameraClick}
                      className="md:w-8 w-6 md:h-7 h-6 !p-0"
                    >
                        <SvgIcon className="w-full !h-full flex-1 text-[#f3f2f3]" icon="photo_camera" />
                    </Button>
                    <span className="pointer-events-none absolute -top-7 left-1/2 -translate-x-1/2 whitespace-nowrap rounded bg-black/75 px-2 py-0.5 text-[10px] text-white opacity-0 transition-opacity group-hover:opacity-100">
                        {i18n._('sys.device_tool.photo_capture')}
                    </span>
                </div>
                <div className="relative md:w-8 w-6 md:h-8 h-6 flex items-center justify-center cursor-pointer group">
                    <Button
                      disabled={!isControlPanel || aiStatus === 'unloaded'}
                      variant="ghost"
                      onClick={() => handleAiInferenceChange(!isAiInference)}
                      className="md:w-8 w-6 md:h-7 h-6 !p-0"
                    >
                        {(isAiInference && aiStatus === 'loaded') ? <SvgIcon className="w-full !h-full flex-1" icon="ai" /> : <SvgIcon className="w-full !h-full flex-1 text-[#f3f2f3]" icon="ai_off" />}
                    </Button>
                    <span className="pointer-events-none absolute -top-7 left-1/2 -translate-x-1/2 whitespace-nowrap rounded bg-black/75 px-2 py-0.5 text-[10px] text-white opacity-0 transition-opacity group-hover:opacity-100">
                        {i18n._('sys.device_tool.ai')}
                    </span>
                </div>
                <div className="relative md:w-8 w-6 md:h-8 h-6 flex items-center justify-center cursor-pointer group">
                    <Button
                      disabled={!isControlPanel}
                      variant="ghost"
                      onClick={toggleStreamStats}
                      className="!p-0 !w-full !h-full"
                    >
                        {isShowStreamStats ? <SvgIcon className="!w-full !h-full flex-1 text-[#f3f2f3]" icon="show_info" /> : <SvgIcon className="w-full !h-full flex-1 text-[#E6E6E6]" icon="close_info" />}
                    </Button>
                    <span className="pointer-events-none absolute -top-7 left-1/2 -translate-x-1/2 whitespace-nowrap rounded bg-black/75 px-2 py-0.5 text-[10px] text-white opacity-0 transition-opacity group-hover:opacity-100">
                        {i18n._('sys.device_tool.stream_info')}
                    </span>
                </div>
                <div className="relative md:w-8 w-6 md:h-8 h-6 flex items-center justify-center cursor-pointer group">
                    <Button
                      disabled={!isControlPanel}
                      variant="ghost"
                      onClick={snapshot}
                      className="!p-0 !w-full !h-full"
                    >
                        <SvgIcon className="w-full !h-full flex-1 text-[#f3f2f3]" icon="screenshot" />
                    </Button>
                    <span className="pointer-events-none absolute -top-7 left-1/2 -translate-x-1/2 whitespace-nowrap rounded bg-black/75 px-2 py-0.5 text-[10px] text-white opacity-0 transition-opacity group-hover:opacity-100">
                        {i18n._('sys.device_tool.snapshot')}
                    </span>
                </div>
                <div className="relative md:w-8 w-6 md:h-8 h-6 flex items-center justify-center cursor-pointer group">
                    <Button
                      disabled={!isControlPanel}
                      variant="ghost"
                      onClick={fullscreen}
                      className="!p-0 !w-full !h-full"
                    >
                        {isFullscreen
                            ? <SvgIcon className="w-full !h-full flex-1" icon="fullscreen_exit" />
                            : <SvgIcon className="w-full !h-full flex-1" icon="fullscreen" />}
                    </Button>
                    <span
                      className="pointer-events-none absolute -top-7 left-1/2 -translate-x-1/2 
               whitespace-nowrap rounded bg-black/75 px-2 py-0.5 
               text-[10px] text-white opacity-0 transition-opacity
               group-hover:opacity-100"
                    >
                        {i18n._('sys.device_tool.fullscreen')}
                    </span>
                </div>

            </div>
        </div>
    )
}