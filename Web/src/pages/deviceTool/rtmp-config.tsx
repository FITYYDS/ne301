import { useState, useEffect, useRef } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Input } from '@/components/ui/input';
import deviceTool from '@/services/api/deviceTool';
import { Button } from '@/components/ui/button';
import { Skeleton } from '@/components/ui/skeleton';
import SvgIcon from '@/components/svg-icon';

type Errors = {
    url: ErrorType;
    stream_key: ErrorType;
}
type ErrorType = {
    error: boolean;
    message: string;
}
type RtmpStatus = {
    initialized: boolean;
    streaming: boolean;
    state: string;
}
type RtmpConfigDetail = {
    enabled: boolean;
    url: string;
    stream_key: string;
}
type RtmpConfig = {
    status: RtmpStatus;
    config: RtmpConfigDetail;
}
export default function RtmpConfig() {
    const { i18n } = useLingui();
    const { getRtmpConfigReq, startRtmpReq, stopRtmpReq, setRtmpConfigReq } = deviceTool;
    const [rtmpLoading, setRtmpLoading] = useState(false);
    const [configLoading, setConfigLoading] = useState(false);
    const [isPasswordVisible, setIsPasswordVisible] = useState(false);
    const handlePasswordVisible = () => {
        setIsPasswordVisible(!isPasswordVisible);
    }
    const [rtmpConfig, setRtmpConfig] = useState<RtmpConfig>({
        config: {
            enabled: false,
            url: "",
            stream_key: ""
        },
        status: {
            initialized: true,
            streaming: false,
            state: "idle"
        }
    });
    const [currentMode, setCurrentMode] = useState<string>('rtmp');
    const autoCheck = useRef(false);
    const [errors, setErrors] = useState<Errors>({
        url: {
            error: false,
            message: '',
        },
        stream_key: {
            error: false,
            message: '',
        },
    });
    const getRtmpConfig = async () => {
        try {
            setRtmpLoading(true);
            const res = await getRtmpConfigReq();
            setRtmpConfig(res.data);
        } catch (error) {
            console.error('initRtmpConfig', error);
            throw error;
        } finally {
            setRtmpLoading(false);
        }
    }

    const validateRtmpConfig = () => {
        let isValid = true;
        if (!rtmpConfig.config.url) {
            setErrors(prev => ({ ...prev, url: { error: true, message: 'sys.device_tool.url_illegal' } }));
            isValid = false;
        } else if (rtmpConfig.config.url.length > 256) {
            setErrors(prev => ({ ...prev, url: { error: true, message: 'sys.device_tool.url_length_error' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, url: { error: false, message: '' } }));
        }
        if (!rtmpConfig.config.stream_key) {
            setErrors(prev => ({ ...prev, stream_key: { error: true, message: 'sys.device_tool.stream_key_illegal' } }));
            isValid = false;
        } else if (rtmpConfig.config.stream_key.length > 128) {
            setErrors(prev => ({ ...prev, stream_key: { error: true, message: 'sys.device_tool.stream_key_length_error' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, stream_key: { error: false, message: '' } }));
        }
        return isValid;
    }
    useEffect(() => {
        if (autoCheck.current) {
            validateRtmpConfig();
        }
    }, [rtmpConfig]);
    useEffect(() => {
        getRtmpConfig();
    }, []);
    const handleStopRtmp = async () => {
        try {
            setConfigLoading(true);
            await stopRtmpReq();
            getRtmpConfig();
        } catch (error) {
            console.error('handleStopRtmp', error);
            throw error;
        } finally {
            setConfigLoading(false);
        }
    }

    const handleStartRtmp = async () => {
        try {
            setConfigLoading(true);
            if (!validateRtmpConfig()) return;
            await setRtmpConfigReq({ url: rtmpConfig.config.url, stream_key: rtmpConfig.config.stream_key, enabled: true });
            await startRtmpReq({});
            getRtmpConfig();
        } catch (error) {
            console.error('handleStartRtmp', error);
            throw error;
        } finally {
            setConfigLoading(false);
        }
    }

    const skeleton = () => (
        <div className="flex flex-col gap-2">
            <Skeleton className="h-10 w-full mb-2" />
            <Skeleton className="h-10 w-full mb-2" />
        </div>
    )
    return (
        <div>
            <div className="flex items-center  justify-between">
                <div className="flex items-center gap-2">
                    <Label className="text-sm text-text-primary"> {i18n._('sys.device_tool.media_stream_settings')}</Label>
                </div>
                <Select value={currentMode} onValueChange={(value) => setCurrentMode(value)}>
                    <SelectTrigger>
                        <SelectValue placeholder={i18n._('common.status')} />
                    </SelectTrigger>
                    <SelectContent>
                        <SelectItem value="rtmp">RTMP</SelectItem>
                    </SelectContent>
                </Select>
            </div>
            {/* {rtmpEnabled && ( */}
            <div className="border border-gray-200 border-solid p-4 rounded-md mt-2">
                {
                    rtmpLoading ? skeleton() : (
                        <>
                            <div className="flex flex-col gap-2">
                                <div className="flex justify-between gap-2 flex-1 pr-0">
                                    <Label className="text-sm text-text-primary shrink-0"> {i18n._('sys.device_tool.url')}</Label>
                                    <Input variant="ghost" placeholder={i18n._('common.please_enter')} type="text" value={rtmpConfig.config.url} onChange={(e) => setRtmpConfig({ ...rtmpConfig, config: { ...rtmpConfig.config, url: (e.target as HTMLInputElement).value } })} />
                                </div>
                                {errors.url.error && <p className="text-sm text-red-500 self-end pr-2">{i18n._(errors.url.message)}</p>}
                            </div>
                            <div className="flex flex-col gap-2">
                                <div className="flex justify-between gap-2 flex-1 pr-0">
                                    <Label className="text-sm text-text-primary shrink-0"> {i18n._('common.secret_key')}</Label>
                                    <div className="flex items-center justify-center relative">
                                        <Input className="pr-11" variant="ghost" placeholder={i18n._('common.please_enter')} type={isPasswordVisible ? 'text' : 'password'} value={rtmpConfig.config.stream_key} onChange={(e) => setRtmpConfig({ ...rtmpConfig, config: { ...rtmpConfig.config, stream_key: (e.target as HTMLInputElement).value } })} />

                                        <button
                                          type="button"
                                          onClick={handlePasswordVisible}
                                          className="absolute top-1/2 -translate-y-1/2 right-0 flex items-center bg-transparent pr-4 border-none cursor-pointer disabled:opacity-50"
                                          disabled={configLoading}
                                        >
                                            {isPasswordVisible ? (
                                                <SvgIcon className="w-5 h-5" icon="visibility" />
                                            ) : (
                                                <SvgIcon className="w-5 h-5" icon="visibility_off" />
                                            )}
                                        </button>
                                    </div>
                                </div>
                                {errors.stream_key.error && <p className="text-sm text-red-500 self-end pr-2">{i18n._(errors.stream_key.message)}</p>}
                            </div>
                        </>
                    )
                }
                <div className="flex justify-between mt-2">
                    <div className="flex items-center gap-2">
                        <Label className="text-sm text-text-primary"> {i18n._('common.status')}:</Label>
                        <div className={`flex items-center gap-2 text-sm ${rtmpConfig.status.streaming ? 'text-green-500' : 'text-gray-500'}`}><div className={`w-2 h-2 rounded-full ${rtmpConfig.status.streaming ? 'bg-green-500' : 'bg-gray-500'}`}></div>{rtmpConfig.status.streaming ? i18n._(`common.connected`) : i18n._('common.disconnected')}</div>
                    </div>
                    <Button variant="primary" disabled={configLoading} onClick={() => (rtmpConfig.status.streaming ? handleStopRtmp() : handleStartRtmp())}>
                        {configLoading ? (
                            <div className="w-full h-full flex items-center justify-center">
                                <div className="w-4 h-4 rounded-full border-2 border-[#f24a00] border-t-transparent animate-spin" aria-label="loading" />
                            </div>
                        ) : (
                            rtmpConfig.status.streaming ? i18n._('common.disconnect') : i18n._('common.connect')
                        )}
                    </Button>
                </div>
            </div>
            {/* )} */}
        </div>
    )
}