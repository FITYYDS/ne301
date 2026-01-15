import { useState, useEffect, useRef } from "preact/hooks";
import { useLingui } from "@lingui/react";
import CommunicationSkeleton from './skeleton';
import { Separator } from "@/components/ui/separator";
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Button } from '@/components/ui/button';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Dialog, DialogContent, DialogHeader, DialogTitle } from '@/components/dialog';
import systemSettings from '@/services/api/systemSettings';
import { toast } from 'sonner';
import SvgIcon from "@/components/svg-icon";
import { isValidPoeIp, isValidHostname } from "@/utils/verify";
import { useCommunicationData } from '@/store/communicationData';

type PoeConfig = {
    auto_reconnect: boolean;
    detect_ip_conflict: boolean;
    dhcp_retry_count: number;
    dhcp_retry_interval_ms: number;
    dhcp_timeout_ms: number;
    dns_primary: string;
    dns_secondary: string;
    gateway: string;
    hostname: string;
    ip_address: string;
    ip_mode: "static" | "dhcp";
    netmask: string;
    persist_last_ip: boolean;
    power_recovery_delay_ms: number;
    validate_gateway: boolean;
}
type ErrorType = {
    error: boolean;
    message: string;
}
type Errors = {
    ip_address: ErrorType;
    netmask: ErrorType;
    gateway: ErrorType;
    dns_primary: ErrorType;
    dns_secondary: ErrorType;
    hostname: ErrorType;
}
export default function PoeNetworkPage() {
    const { i18n } = useLingui();
    const [poeStatus, setPoeStatus] = useState<any>(null);
    const [poeConfig, setPoeConfig] = useState<PoeConfig>({
        auto_reconnect: false,
        detect_ip_conflict: false,
        dhcp_retry_count: 0,
        dhcp_retry_interval_ms: 0,
        dhcp_timeout_ms: 0,
        dns_primary: '',
        dns_secondary: '',
        gateway: '',
        hostname: '',
        ip_address: '',
        ip_mode: 'static',
        netmask: '',
        persist_last_ip: false,
        power_recovery_delay_ms: 0,
        validate_gateway: false,
    });
    const { getCommunicationData } = useCommunicationData();
    const [saveLoading, setSaveLoading] = useState(false);
    const [isLoading, setIsLoading] = useState(false);
    const autoCheck = useRef(false);
    const [connectPoeLoading, setConnectPoeLoading] = useState(false);

    // validate ip
    const [errors, setErrors] = useState<Errors>({
        ip_address: {
            error: false,
            message: '',
        },
        netmask: {
            error: false,
            message: '',
        },
        gateway: {
            error: false,
            message: '',
        },
        dns_primary: {
            error: false,
            message: '',
        },
        dns_secondary: {
            error: false,
            message: '',
        },
        hostname: {
            error: false,
            message: '',
        }
    });
    const { getPoeStatusReq, getPoeInfoReq, getPoeConfigReq, setPoeConfigReq, validatePoeConfigReq, connectPoeReq, disconnectPoeReq } = systemSettings;
    const getPoeStatus = async () => {
        try {
            setIsLoading(true);
            const res = await getPoeStatusReq();
            setPoeStatus(res.data);
        } catch (error) {
            console.error('getPoeStatus', error);
        } finally {
            setIsLoading(false);
        }
    }
    const getPoeConfig = async () => {
        try {
            setIsLoading(true);
            const res = await getPoeConfigReq();
            setPoeConfig(res.data);
        } catch (error) {
            console.error('getPoeConfig', error);
            throw error;
        } finally {
            setIsLoading(false);
        }
    }
    useEffect(() => {
        getPoeStatus();
        getPoeConfig();
    }, []);
    const validatePoeConfig = async () => {
        try {
            await validatePoeConfigReq(poeConfig);
        } catch (error) {
            toast.error(i18n._('errors.poe.parameter_configuration_error'));
            console.error('validatePoeConfig', error);
        }
    }

    const validatePoeConfigParams = () => {
        let isValid = true;
        const { ip_address, netmask, gateway, dns_primary, dns_secondary, hostname } = poeConfig;
        if (ip_address && !isValidPoeIp(ip_address)) {
            setErrors(prev => ({ ...prev, ip_address: { error: true, message: 'sys.system_management.format_error' } }));
            isValid = false;
        } else if (ip_address) {
            setErrors(prev => ({ ...prev, ip_address: { error: false, message: '' } }));
        }
        if (netmask && !isValidPoeIp(netmask)) {
            setErrors(prev => ({ ...prev, netmask: { error: true, message: 'sys.system_management.format_error' } }));
            isValid = false;
        } else if (netmask) {
            setErrors(prev => ({ ...prev, netmask: { error: false, message: '' } }));
        }
        if (gateway && !isValidPoeIp(gateway)) {
            setErrors(prev => ({ ...prev, gateway: { error: true, message: 'sys.system_management.format_error' } }));
            isValid = false;
        } else if (gateway) {
            setErrors(prev => ({ ...prev, gateway: { error: false, message: '' } }));
        }
        if (dns_primary && !isValidHostname(dns_primary)) {
            setErrors(prev => ({ ...prev, dns_primary: { error: true, message: 'sys.system_management.format_error' } }));
            isValid = false;
        } else if (dns_primary) {
            setErrors(prev => ({ ...prev, dns_primary: { error: false, message: '' } }));
        }
        if (dns_secondary && !isValidPoeIp(dns_secondary)) {
            setErrors(prev => ({ ...prev, dns_secondary: { error: true, message: 'sys.system_management.format_error' } }));
            isValid = false;
        } else if (dns_secondary) {
            setErrors(prev => ({ ...prev, dns_secondary: { error: false, message: '' } }));
        }
        if (hostname && !isValidHostname(hostname)) {
            setErrors(prev => ({ ...prev, hostname: { error: true, message: 'sys.system_management.format_error' } }));
            isValid = false;
        } else if (hostname) {
            setErrors(prev => ({ ...prev, hostname: { error: false, message: '' } }));
        }
        return isValid;
    }

    useEffect(() => {
        if (autoCheck.current) {
            validatePoeConfigParams();
        }
    }, [poeConfig]);
    const handleSavePoe = async () => {
        try {
            autoCheck.current = true;
            setSaveLoading(true);
            const isValid = validatePoeConfigParams();
            if (!isValid) {
                return;
            }
            await validatePoeConfig();
            await setPoeConfigReq(poeConfig);
            toast.success(i18n._('sys.system_management.save_success'));
        } catch (error) {
            console.error('handleSavePoe', error);
        } finally {
            autoCheck.current = false;
            setSaveLoading(false);
        }
    }
    const handleConnectPoe = async () => {
        try {
            setConnectPoeLoading(true);
            const isValid = validatePoeConfigParams();
            if (!isValid) {
                return;
            }
            await validatePoeConfig();
            await setPoeConfigReq(poeConfig);
            await connectPoeReq();
            getPoeStatus();
            getCommunicationData();
        } catch (error) {
            console.error('handleConnectPoe', error);
        } finally {
            setConnectPoeLoading(false);
        }
    }
    const handleDisconnectPoe = async () => {
        try {
            setConnectPoeLoading(true);
            await disconnectPoeReq();
            getPoeStatus();
            getCommunicationData();
        } catch (error) {
            console.error('handleDisconnectPoe', error);
        } finally {
            setConnectPoeLoading(false);
        }
    }

    const [detailsOpen, setDetailsOpen] = useState(false);
    const [detailsLoading, setDetailsLoading] = useState(false);
    const [poeInfo, setPoeInfo] = useState<any>(null);

    const handleGetPoeInfo = async () => {
        try {
            setDetailsLoading(true);
            const res = await getPoeInfoReq();
            setPoeInfo(res.data);
        } catch (error) {
            console.error('handleGetPoeInfo', error);
        } finally {
            setDetailsLoading(false);
        }
    }

    return (
        <div>
            {isLoading && <CommunicationSkeleton />}
            {!isLoading && (
                <div>
                    {poeStatus?.status === 'Connected' && (
                        <div className="mb-4">
                            <p className="text-sm font-bold mb-2">{i18n._('sys.system_management.poe_mode')}</p>
                            <div className="flex justify-between items-center gap-2 bg-gray-100 p-4 rounded-lg">
                                <div className="flex items-center">
                                    <div className="flex items-center justify-center rounded-md bg-primary w-6 h-6">
                                        <SvgIcon icon="ethernet_port" className="w-4 h-4 text-white" />
                                    </div>
                                    <p className="text-sm text-text-primary font-bold ml-2">{i18n._('sys.system_management.poe')}</p>
                                </div>
                                <p className={`text-sm font-medium ${poeStatus?.status === 'Connected' ? 'text-green-500' : 'text-red-500'}`}>{i18n._(`common.${poeStatus?.status === 'Connected' ? 'connected' : 'disconnected'}`)}</p>
                            </div>
                        </div>
                    )}
                    <p className="text-sm font-bold mb-2">{i18n._('sys.system_management.connection_settings')}</p>
                    <div className="flex flex-col gap-2 w-full bg-gray-100 p-4 rounded-lg">
                        <div className="flex gap-2 w-full justify-between items-center">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.poe_mode')}</Label>
                            <Select value={poeConfig.ip_mode} onValueChange={(value: "static" | "dhcp") => setPoeConfig({ ...poeConfig, ip_mode: value })}>
                                <SelectTrigger>
                                    <SelectValue />
                                </SelectTrigger>
                                <SelectContent>
                                    <SelectItem value="static">{i18n._('sys.system_management.static')}</SelectItem>
                                    <SelectItem value="dhcp">{i18n._('sys.system_management.dhcp')}</SelectItem>
                                </SelectContent>
                            </Select>
                        </div>
                        {poeConfig.ip_mode === 'static' && (
                            <>
                                <Separator />
                                <div className="flex flex-col gap-2">
                                    <div className="flex justify-between gap-2 flex-1 pr-0">
                                        <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.ip_address')}</Label>
                                        <Input type="text" variant="ghost" value={poeConfig.ip_address} onChange={(e) => setPoeConfig({ ...poeConfig, ip_address: (e.target as HTMLInputElement).value })} placeholder={i18n._('sys.system_management.ip_address')} />
                                    </div>
                                    {errors.ip_address.error && <p className="text-sm text-red-500 self-end pr-2">{i18n._(errors.ip_address.message)}</p>}
                                </div>
                                <Separator />
                                <div className="flex flex-col gap-2">
                                    <div className="flex justify-between gap-2 flex-1 pr-0">
                                        <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.netmask')}</Label>
                                        <Input type="text" variant="ghost" value={poeConfig.netmask} onChange={(e) => setPoeConfig({ ...poeConfig, netmask: (e.target as HTMLInputElement).value })} placeholder={i18n._('sys.system_management.netmask')} />
                                    </div>
                                    {errors.netmask.error && <p className="text-sm text-red-500 self-end pr-2">{i18n._(errors.netmask.message)}</p>}
                                </div>
                                <Separator />
                                <div className="flex flex-col gap-2">
                                    <div className="flex justify-between gap-2 flex-1 pr-0">
                                        <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.gateway')}</Label>
                                        <Input type="text" variant="ghost" value={poeConfig.gateway} onChange={(e) => setPoeConfig({ ...poeConfig, gateway: (e.target as HTMLInputElement).value })} placeholder={i18n._('sys.system_management.gateway')} />
                                    </div>
                                    {errors.gateway.error && <p className="text-sm text-red-500 self-end pr-2">{i18n._(errors.gateway.message)}</p>}
                                </div>
                                <Separator />
                                <div className="flex flex-col gap-2">
                                    <div className="flex justify-between gap-2 flex-1 pr-0">
                                        <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.dns_primary')}</Label>
                                        <Input type="text" variant="ghost" value={poeConfig.dns_primary} onChange={(e) => setPoeConfig({ ...poeConfig, dns_primary: (e.target as HTMLInputElement).value })} placeholder={i18n._('sys.system_management.dns_primary')} />
                                    </div>
                                    {errors.dns_primary.error && <p className="text-sm text-red-500 self-end pr-2">{i18n._(errors.dns_primary.message)}</p>}
                                </div>
                                <Separator />
                                <div className="flex flex-col gap-2">
                                    <div className="flex justify-between gap-2 flex-1 pr-0">
                                        <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.dns_secondary')}</Label>
                                        <Input type="text" variant="ghost" value={poeConfig.dns_secondary} onChange={(e) => setPoeConfig({ ...poeConfig, dns_secondary: (e.target as HTMLInputElement).value })} placeholder={i18n._('sys.system_management.dns_secondary')} />
                                    </div>
                                    {errors.dns_secondary.error && <p className="text-sm text-red-500 self-end pr-2">{i18n._(errors.dns_secondary.message)}</p>}
                                </div>
                            </>
                        )}
                    </div>
                    <div className="flex gap-2 w-full mt-4 justify-between">
                        <Button variant="outline" onClick={() => { setDetailsOpen(true); handleGetPoeInfo(); }}>{i18n._('common.details')}</Button>
                        <div className="flex gap-2">
                            <Button variant="outline" disabled={saveLoading} onClick={handleSavePoe}> {saveLoading ? (
                                <div className="w-full h-full flex items-center justify-center">
                                    <div className="w-4 h-4 rounded-full border-2 border-[#f24a00] border-t-transparent animate-spin" aria-label="loading" />
                                </div>
                            ) : i18n._('common.save')}
                            </Button>
                            <Button variant="primary" disabled={connectPoeLoading} onClick={() => (poeStatus?.status === 'Connected' ? handleDisconnectPoe() : handleConnectPoe())}>
                                {connectPoeLoading ? (
                                    <div className="w-full h-full flex items-center justify-center">
                                        <div className="w-4 h-4 rounded-full border-2 border-[#f24a00] border-t-transparent animate-spin" aria-label="loading" />
                                    </div>
                                ) : (
                                    poeStatus?.status === 'Connected' ? i18n._('common.disconnect') : i18n._('common.connect')
                                )}
                            </Button>
                        </div>
                    </div>
                    <Dialog open={detailsOpen} onOpenChange={setDetailsOpen}>
                        <DialogContent>
                            <DialogHeader>
                                <DialogTitle>{i18n._('common.details')}</DialogTitle>
                            </DialogHeader>
                            <div className="mt-4">
                                {detailsLoading && <CommunicationSkeleton />}
                                {!detailsLoading && (
                                    <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg">
                                        <div className="flex justify-between my-2">
                                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.poe_mode')}</Label>
                                            <p>{poeInfo?.ip_mode === 'static' ? i18n._('sys.system_management.static') : i18n._('sys.system_management.dhcp')}</p>
                                        </div>
                                        <Separator />
                                        <div className="flex justify-between my-2">
                                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.ip_address')}</Label>
                                            <p>{poeInfo?.ip_address}</p>
                                        </div>
                                        <Separator />
                                        <div className="flex justify-between my-2">
                                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.netmask')}</Label>
                                            <p>{poeInfo?.netmask}</p>
                                        </div>
                                        <Separator />
                                        <div className="flex justify-between my-2">
                                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.gateway')}</Label>
                                            <p>{poeInfo?.gateway}</p>
                                        </div>
                                        <Separator />
                                        <div className="flex justify-between my-2">
                                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.dns_primary')}</Label>
                                            <p>{poeInfo?.dns_primary}</p>
                                        </div>
                                        <Separator />
                                        <div className="flex justify-between my-2">
                                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.dns_secondary')}</Label>
                                            <p>{poeInfo?.dns_secondary}</p>
                                        </div>
                                        <Separator />
                                        {poeInfo?.hostname && (
                                            <div className="flex justify-between my-2">
                                                <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.hostname')}</Label>
                                                <p>{poeInfo?.hostname}</p>
                                            </div>
                                        )}
                                        <Separator />
                                        <div className="flex justify-between my-2">
                                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.mac_address')}</Label>
                                            <p>{poeInfo?.mac_address}</p>
                                        </div>
                                    </div>
                                )}

                            </div>
                        </DialogContent>
                    </Dialog>
                </div>
            )}
        </div>
    )
}