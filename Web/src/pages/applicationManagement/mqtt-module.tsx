import { useState, useEffect } from 'preact/hooks';
import { Label } from '@/components/ui/label';
import { Input } from '@/components/ui/input';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Button } from '@/components/ui/button';
import { Switch } from '@/components/ui/switch';
import { Separator } from '@/components/ui/separator';
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/tooltip';
import Upload from '@/components/upload';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import applicationManagement, { type SetMqttConfigReq } from '@/services/api/applicationManagement';
import ApplicationManagementSkeleton from './skeleton';
import { toast } from 'sonner';
import { readCertificateFile } from '@/utils/readFile';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogFooter } from '@/components/dialog';
// import { toast } from 'sonner';
import { isValidMqttHost } from '@/utils/verify';

type ProtocolType = 'mqtt' | 'mqtts';
type ErrorType = {
    error: boolean;
    message: string;
}
type Errors = {
    hostname: ErrorType;
    port: ErrorType;
    topicReceive: ErrorType;
    topicReport: ErrorType;
    clientId: ErrorType;
    caCert: ErrorType;
    clientCert: ErrorType;
    privateKey: ErrorType;
}
export default function MqttModule() {
    const { i18n } = useLingui();
    const { getMqttConfigReq, setMqttConfigReq, connectMqttReq, disconnectMqttReq } = applicationManagement;

    // const [mqtt, setMqtt] = useState('mqtt');
    const [loading, setLoading] = useState(false);
    const [connectLoading, setConnectLoading] = useState(false);
    const [isPasswordVisible, setIsPasswordVisible] = useState(false);
    const handlePasswordVisible = (e: MouseEvent) => {
        e.preventDefault();
        setIsPasswordVisible(!isPasswordVisible);
    };
    const [autoCheck, setAutoCheck] = useState(false);
    const [mqttConfig, setMqttConfig] = useState<SetMqttConfigReq>({
        connection: {
            hostname: '',
            port: 0,
            client_id: '',
            protocol_type: 'mqtt' as ProtocolType,
        },
        authentication: {
            username: '',
            password: '',
            ca_cert_path: '',
            client_cert_path: '',
            client_key_path: '',
            ca_data: '',
            client_cert_data: '',
            client_key_data: '',
            sni: false,
        },
        qos: {
            data_receive_qos: 0,
            data_report_qos: 0,
        },
        topics: {
            data_receive_topic: '',
            data_report_topic: '',
        },
        status: {
            connected: false,
            running: false,
            state: 0,
            version: '',
        },
    });

    const [errors, setErrors] = useState<Errors>({
        hostname: {
            error: false,
            message: '',
        },
        port: {
            error: false,
            message: '',
        },
        topicReceive: {
            error: false,
            message: '',
        },
        topicReport: {
            error: false,
            message: '',
        },
        clientId: {
            error: false,
            message: '',
        },
        caCert: {
            error: false,
            message: '',
        },
        clientCert: {
            error: false,
            message: '',
        },
        privateKey: {
            error: false,
            message: '',
        },
    });
    
    const validateMqttConfig = (): boolean => {
        let isValid = true;
        const { hostname } = mqttConfig.connection;
        const port = String(mqttConfig.connection.port || '');
        const topicReceive = mqttConfig.topics.data_receive_topic;
        const topicReport = mqttConfig.topics.data_report_topic;
        const isMqtts = mqttConfig.connection.protocol_type === 'mqtts';
        if (!hostname) {
            setErrors(prev => ({ ...prev, hostname: { error: true, message: 'sys.application_management.server_address_placeholder' } }));
            isValid = false;
        } else if (hostname.length <= 0 || hostname.length > 64) {
            setErrors(prev => ({ ...prev, hostname: { error: true, message: 'sys.application_management.server_address_error' } }));
            isValid = false;
        } else if (!isValidMqttHost(hostname)) {
            setErrors(prev => ({ ...prev, hostname: { error: true, message: 'sys.application_management.invalid_url' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, hostname: { error: false, message: '' } }));
        }

        if (!port) {
            setErrors(prev => ({ ...prev, port: { error: true, message: 'sys.application_management.port_error' } }));
            isValid = false;
        } else if (parseInt(String(port), 10) < 1 || parseInt(String(port), 10) > 65535) {
            setErrors(prev => ({ ...prev, port: { error: true, message: 'sys.application_management.port_error' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, port: { error: false, message: '' } }));
        }

        if (!topicReceive) {
            setErrors(prev => ({ ...prev, topicReceive: { error: true, message: 'sys.application_management.topic_receive_error' } }));
            isValid = false;
        } else if (topicReceive && topicReceive.length > 128) {
            setErrors(prev => ({ ...prev, topicReceive: { error: true, message: 'sys.application_management.topic_receive_error' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, topicReceive: { error: false, message: '' } }));
        }

        if (!topicReport) {
            setErrors(prev => ({ ...prev, topicReport: { error: true, message: 'sys.application_management.topic_report_error' } }));
            isValid = false;
        } else if (topicReport && topicReport.length > 128) {
            setErrors(prev => ({ ...prev, topicReport: { error: true, message: 'sys.application_management.topic_report_error' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, topicReport: { error: false, message: '' } }));
        }

        if (mqttConfig.connection.client_id && mqttConfig.connection.client_id.length > 23) {
            setErrors(prev => ({ ...prev, clientId: { error: true, message: 'sys.application_management.client_id_error' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, clientId: { error: false, message: '' } }));
        }

        if (!mqttConfig.authentication.ca_cert_path && isMqtts) {
            setErrors(prev => ({ ...prev, caCert: { error: true, message: 'sys.application_management.ca_cert_error' } }));
            isValid = false;
        } else if (!mqttConfig.authentication.ca_data && isMqtts) {
            setErrors(prev => ({ ...prev, caCert: { error: true, message: 'sys.application_management.ca_cert_data_error' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, caCert: { error: false, message: '' } }));
        }

        if (mqttConfig.authentication.client_cert_path && !mqttConfig.authentication.client_cert_data && isMqtts) {
            setErrors(prev => ({ ...prev, clientCert: { error: true, message: 'sys.application_management.client_cert_data_error' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, clientCert: { error: false, message: '' } }));
        }

        if (mqttConfig.authentication.client_key_path && !mqttConfig.authentication.client_key_data && isMqtts) {
            setErrors(prev => ({ ...prev, privateKey: { error: true, message: 'sys.application_management.private_key_data_error' } }));
            isValid = false;
        } else {
            setErrors(prev => ({ ...prev, privateKey: { error: false, message: '' } }));
        }
        return isValid;
    }

    useEffect(() => {
        if (autoCheck) {
            validateMqttConfig();
        }
    }, [mqttConfig]);

    const uploadSlot = (
        <>
            <SvgIcon icon="upload" />
            {i18n._('common.upload')}
        </>
    );
    /**
     * @HACK 
     * Handle incomplete certificate initialization
     */
    const dealMqttConfig = (config: SetMqttConfigReq) => {
        if (!config.authentication.ca_data || !config.authentication.ca_cert_path) {
            config.authentication.ca_data = '';
            config.authentication.ca_cert_path = '';
        }
        if (!config.authentication.client_cert_data || !config.authentication.client_cert_path) {
            config.authentication.client_cert_data = '';
            config.authentication.client_cert_path = '';
        }
        if (!config.authentication.client_key_data || !config.authentication.client_key_path) {
            config.authentication.client_key_data = '';
            config.authentication.client_key_path = '';
        }   
        return config;
    }

    const initMqttConfig = async () => {
        try {
            setLoading(true);
            const res = await getMqttConfigReq();
            setMqttConfig(dealMqttConfig(res.data));
        } catch (error) {
            console.error('initMqttConfig failed', error as Error);
        } finally {
            setLoading(false);
        }
    }
    useEffect(() => {
        initMqttConfig();
    }, []);

    const certAccept = {
        'application/x-x509-ca-cert': ['.crt'],
        'application/x-pkcs12': ['.pfx'],
        'application/x-pkcs7-certificates': ['.p7b'],
        'application/x-pem-file': ['.key', '.pem'],
        'application/x-java-keystore': ['.jks'],
        'application/x-x509-user-cert': ['.der', '.cer'],
    }
    const keyAccept = {
        'application/x-x509-ca-cert': ['.crt'],
        'application/x-pkcs12': ['.pfx'],
        'application/x-pkcs7-certificates': ['.p7b'],
        'application/x-pem-file': ['.key', '.pem'],
        'application/x-java-keystore': ['.jks'],
        'application/x-x509-user-cert': ['.der', '.cer'],
    }
    const handleSaveCaCert = async (file: File) => {
        const data = await readCertificateFile(file);
        if (!data) {
            setErrors(prev => ({ ...prev, caCert: { error: true, message: 'sys.application_management.ca_cert_data_error' } }));
            return;
        }
        setMqttConfig({ ...mqttConfig, authentication: { ...mqttConfig.authentication, ca_data: data, ca_cert_path: file.name } });
    }
    const handleSaveClientCert = async (file: File) => {
        const data = await readCertificateFile(file);
        if (!data) {
            setErrors(prev => ({ ...prev, clientCert: { error: true, message: 'sys.application_management.client_cert_data_error' } }));
            return;
        }
        setMqttConfig({ ...mqttConfig, authentication: { ...mqttConfig.authentication, client_cert_data: data, client_cert_path: file.name } });
    }
    const handleSavePrivateKey = async (file: File) => {
        const data = await readCertificateFile(file);
        if (!data) {
            setErrors(prev => ({ ...prev, privateKey: { error: true, message: 'sys.application_management.private_key_data_error' } }));
            return;
        }
        setMqttConfig({ ...mqttConfig, authentication: { ...mqttConfig.authentication, client_key_data: data, client_key_path: file.name } });
    }

    const handleSaveMqttConfig = async () => {
        try {
            setAutoCheck(true);
            const isValid = validateMqttConfig();
            if (!isValid) return false;
            const params = { ...mqttConfig };
            if (params.connection.protocol_type === 'mqtt') {
                delete (params.authentication as any).ca_data;
                delete (params.authentication as any).ca_cert_path;
                delete (params.authentication as any).client_cert_data;
                delete (params.authentication as any).client_cert_path;
                delete (params.authentication as any).client_key_data;
                delete (params.authentication as any).client_key_path;
            }
            await setMqttConfigReq(params);
            initMqttConfig();
            toast.success(i18n._('sys.application_management.configSuccess'));
        } catch (error) {
            console.error('saveMqttConfig failed', error as Error);
            throw error;
        } finally {
            setLoading(false);
            // setAutoCheck(false);
        }
    }
    const connectMqtt = async () => {
        try {
            setConnectLoading(true);

            const isValid = validateMqttConfig();
            if (!isValid) return false;
            const params = { ...mqttConfig };
            if (params.connection.protocol_type === 'mqtt') {
                delete (params.authentication as any).ca_data;
                delete (params.authentication as any).ca_cert_path;
                delete (params.authentication as any).client_cert_data;
                delete (params.authentication as any).client_cert_path;
                delete (params.authentication as any).client_key_data;
                delete (params.authentication as any).client_key_path;
            }
            await setMqttConfigReq(params);
            const res = await connectMqttReq();
            await initMqttConfig();
            if (res.data.connected) {
                toast.success(i18n._('sys.application_management.connectSuccess'));
            } else {
                toast.error(i18n._('sys.application_management.connectTimeout'));
            }
        } catch (error) {
            console.error('connectMqtt failed', error as Error);
        } finally {
            setConnectLoading(false);
            setAutoCheck(false);
        }
    }
    const disconnectMqtt = async () => {
        try {
            setConnectLoading(true);
            await disconnectMqttReq();
            await initMqttConfig();
            toast.success(i18n._('sys.application_management.disconnectSuccess'));
        } catch (error) {
            console.error('disconnectMqtt failed', error as Error);
        } finally {
            setConnectLoading(false);
        }
    }
    const mqttsModule = () => (
        <div>
            <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg mt-4">
                <div className="flex justify-between">
                    <div className="flex items-center gap-2">
                        <Label>{i18n._('sys.application_management.ca_cert')}</Label>
                        <Tooltip>
                            <TooltipTrigger>
                                <SvgIcon icon="info" className="w-4 h-4" />
                            </TooltipTrigger>
                            <TooltipContent className="max-w-80 text-pretty">{i18n._('sys.application_management.ca_cert_tooltip')}</TooltipContent>
                        </Tooltip>
                    </div>
                    <div className="flex items-center gap-2">
                        {mqttConfig.authentication.ca_cert_path ? <p className="text-sm text-text-primary">{mqttConfig.authentication.ca_cert_path}</p>
                            : <p className={`text-sm  ${errors.caCert.error ? 'text-red-500' : 'text-gray-400'}`}>{errors.caCert.error ? i18n._(errors.caCert.message) : i18n._('sys.application_management.ca_cert_placeholder')}</p>}
                        <Upload slot={uploadSlot} type="button" onFileChange={handleSaveCaCert} accept={certAccept} fileType={['.crt']} maxSize={1024 * 10} multiple={false} />
                        <Button variant="outline" onClick={() => setMqttConfig({ ...mqttConfig, authentication: { ...mqttConfig.authentication, ca_data: '', ca_cert_path: '' } })}>{i18n._('common.clear')}</Button>
                    </div>
                </div>
                <Separator />
                <div className="flex justify-between">
                    <Label>{i18n._('sys.application_management.client_cert')}</Label>
                    <div className="flex items-center gap-2">
                        {mqttConfig.authentication.client_cert_path ? <p className="text-sm text-text-primary">{mqttConfig.authentication.client_cert_path}</p>
                            : <p className={`text-sm  ${errors.clientCert.error ? 'text-red-500' : 'text-gray-400'}`}>{errors.clientCert.error ? i18n._(errors.clientCert.message) : i18n._('sys.application_management.client_cert_placeholder')}</p>}
                        <Upload slot={uploadSlot} type="button" onFileChange={handleSaveClientCert} accept={certAccept} fileType={['.crt']} maxSize={1024 * 10} multiple={false} />
                        <Button variant="outline" onClick={() => setMqttConfig({ ...mqttConfig, authentication: { ...mqttConfig.authentication, client_cert_data: '', client_cert_path: '' } })}>{i18n._('common.clear')}</Button>
                    </div>
                </div>
                <Separator />
                <div className="flex justify-between">
                    <Label>{i18n._('sys.application_management.private_key')}</Label>
                    <div className="flex items-center gap-2">
                        {mqttConfig.authentication.client_key_path ? <p className="text-sm text-text-primary">{mqttConfig.authentication.client_key_path}</p>
                            : <p className={`text-sm  ${errors.privateKey.error ? 'text-red-500' : 'text-gray-400'}`}>{errors.privateKey.error ? i18n._(errors.privateKey.message) : i18n._('sys.application_management.private_key_placeholder')}</p>}
                        <Upload slot={uploadSlot} type="button" onFileChange={handleSavePrivateKey} fileType={['.key']} accept={keyAccept} maxSize={1024 * 10} multiple={false} />
                        <Button variant="outline" onClick={() => setMqttConfig({ ...mqttConfig, authentication: { ...mqttConfig.authentication, client_key_data: '', client_key_path: '' } })}>{i18n._('common.clear')}</Button>
                    </div>
                </div>
            </div>
            <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg mt-4">
                <div className="flex justify-between">
                    <Label>{i18n._('sys.application_management.sni')}</Label>
                    <Switch checked={mqttConfig.authentication.sni} onCheckedChange={(e) => setMqttConfig({ ...mqttConfig, authentication: { ...mqttConfig.authentication, sni: e } })} />
                </div>
            </div>
        </div>
    )
    const [confirmDialogOpen, setConfirmDialogOpen] = useState(false);
    const handleProtocolChange = (e: string) => {
        setAutoCheck(false);
        if (mqttConfig.status.connected) {
            setConfirmDialogOpen(true);
            return;
        }
        if (e === 'mqtts') {
            setMqttConfig({ ...mqttConfig, connection: { ...mqttConfig.connection, protocol_type: e as ProtocolType, port: 8883 } });
        } else {
            setMqttConfig({ ...mqttConfig, connection: { ...mqttConfig.connection, protocol_type: e as ProtocolType, port: 1883 } });
        }
    }
    const confirmDisconnectMqtt = async () => {
        await disconnectMqtt();
        getMqttConfigReq().then(res => {
            const newMqttConfig = res.data;
            if (res.data.connection.protocol_type === 'mqtts') {
                newMqttConfig.connection.protocol_type = 'mqtt' as ProtocolType;
                newMqttConfig.connection.port = 1883;
            } else {
                newMqttConfig.connection.protocol_type = 'mqtts' as ProtocolType;
                newMqttConfig.connection.port = 8883;
            }
            setMqttConfig(newMqttConfig);
        });
        setConfirmDialogOpen(false);
    }
    const cancelConfirmDialog = () => {
        setConfirmDialogOpen(false);
    }
    const comfirmDialog = () => (
        <Dialog open={confirmDialogOpen} onOpenChange={setConfirmDialogOpen}>
            <DialogContent>
                <DialogHeader>
                    <DialogTitle>{i18n._('sys.application_management.confirm_protocol_change')}</DialogTitle>
                </DialogHeader>
                <div className="my-4">
                    <p className="text-text-secondary">{i18n._('sys.application_management.confirm_protocol_change_description')}</p>
                </div>
                <DialogFooter>
                    <Button variant="outline" onClick={() => cancelConfirmDialog()}>{i18n._('common.cancel')}</Button>
                    <Button variant="primary" onClick={() => confirmDisconnectMqtt()}>{i18n._('common.confirm')}</Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    )

    return (
        <div>
            {loading ? (
                <ApplicationManagementSkeleton />
            ) : (
                <>
                    <div className="flex flex-col gap-2 mt-4 bg-gray-100 p-4 rounded-lg relative">
                        <div className="flex justify-between">
                            <Label>{i18n._('sys.application_management.protocol')}</Label>
                            <Select value={mqttConfig.connection.protocol_type} onValueChange={(e) => handleProtocolChange(e)}>
                                <SelectTrigger
                                  value={mqttConfig.connection.protocol_type}
                                  className="bg-transparent border-0 !shadow-none !outline-none focus:!outline-none focus:!ring-0 focus:!ring-offset-0 focus:!shadow-none focus:!border-transparent focus-visible:!outline-none focus-visible:!ring-0 focus-visible:!ring-offset-0 text-right"
                                >
                                    <SelectValue />
                                </SelectTrigger>
                                <SelectContent>
                                    <SelectItem value="mqtt">MQTT</SelectItem>
                                    <SelectItem value="mqtts">MQTTS</SelectItem>
                                </SelectContent>
                            </Select>
                        </div>
                        <Separator />
                        <div className="flex justify-between">
                            <Label>{i18n._('sys.application_management.connection_status')}</Label>
                            <div className="flex items-center gap-2">
                                <div className={`w-2 h-2 rounded-full ${mqttConfig.status.connected ? 'bg-green-500' : 'bg-gray-500'}`}></div>
                                <p className={`${mqttConfig.status.connected ? 'text-green-500' : 'text-gray-500'}`}>{i18n._(`common.${mqttConfig.status.connected ? 'connected' : 'disconnected'}`)}</p>
                            </div>
                        </div>
                        <Separator />
                        <div className="flex flex-col gap-1">
                            <div className="flex gap-2 justify-between">
                                <Label className="shrink-0">{i18n._('sys.application_management.server_address')}</Label>
                                <Input
                                  placeholder={i18n._('common.please_enter')}
                                  variant="ghost"
                                  value={mqttConfig.connection.hostname}
                                  onChange={(e) => setMqttConfig({ ...mqttConfig, connection: { ...mqttConfig.connection, hostname: (e.target as HTMLInputElement).value } })}
                                />
                            </div>
                            {errors.hostname.error && <p className="text-red-500 text-sm self-end">{i18n._(errors.hostname.message)}</p>}
                        </div>
                        <Separator />
                        <div className="flex flex-col gap-1">
                            <div className="flex gap-2 justify-between">
                                <Label className="shrink-0">{i18n._('sys.application_management.port')}</Label>

                                <Input
                                  placeholder={i18n._('common.please_enter')}
                                  variant="ghost"
                                  value={mqttConfig.connection.port === 0 ? '' : mqttConfig.connection.port}
                                  onChange={(e) => {
                                        const v = (e.target as HTMLInputElement).value;
                                        setMqttConfig({
                                            ...mqttConfig,
                                            connection: {
                                                ...mqttConfig.connection,
                                                port: (v === '' || !Number(v)) ? 0 : parseInt(v, 10)
                                            }
                                        })
                                    }}
                                />
                            </div>
                            {errors.port.error && <p className="text-red-500 self-end text-sm">{i18n._('sys.application_management.port_error')}</p>}
                        </div>
                        <Separator />
                        <div className="flex flex-col gap-1">
                            <div className="flex gap-2 justify-between">
                                <Label className="shrink-0">{i18n._('sys.application_management.topic_receive')}</Label>
                                <Input
                                  variant="ghost"
                                  placeholder={i18n._('common.please_enter')}
                                  value={mqttConfig.topics.data_receive_topic}
                                  onChange={(e) => setMqttConfig({ ...mqttConfig, topics: { ...mqttConfig.topics, data_receive_topic: (e.target as HTMLInputElement).value } })}
                                />
                            </div>
                            {errors.topicReceive.error && <p className="text-red-500 self-end text-sm">{i18n._('sys.application_management.topic_receive_error')}</p>}
                        </div>
                        <Separator />
                        <div className="flex flex-col gap-1">
                            <div className="flex gap-2 justify-between">
                                <Label className="shrink-0">{i18n._('sys.application_management.topic_report')}</Label>
                                <Input
                                  variant="ghost"
                                  placeholder={i18n._('common.please_enter')}
                                  value={mqttConfig.topics.data_report_topic}
                                  onChange={(e) => setMqttConfig({ ...mqttConfig, topics: { ...mqttConfig.topics, data_report_topic: (e.target as HTMLInputElement).value } })}
                                />
                            </div>
                            {errors.topicReport.error && <p className="text-red-500 self-end text-sm">{i18n._('sys.application_management.topic_report_error')}</p>}
                        </div>
                        <Separator />
                        <div className="flex flex-col gap-1">
                            <div className="flex gap-2 justify-between">
                                <Label className="shrink-0">{i18n._('sys.application_management.client_id')}</Label>
                                <Input
                                  variant="ghost"
                                  placeholder={i18n._('common.please_enter')}
                                  value={mqttConfig.connection.client_id}
                                  onChange={(e) => setMqttConfig({ ...mqttConfig, connection: { ...mqttConfig.connection, client_id: (e.target as HTMLInputElement).value } })}
                                />
                            </div>
                            {errors.clientId.error && <p className="text-red-500 self-end text-sm">{i18n._('sys.application_management.client_id_error')}</p>}

                        </div>
                        <Separator />
                        <div className="flex gap-2 justify-between">
                            <Label>{i18n._('sys.application_management.qos')}</Label>
                            <Select
                              value={String(mqttConfig.qos.data_receive_qos)}
                              onValueChange={(e) => setMqttConfig({ ...mqttConfig, qos: { ...mqttConfig.qos, data_receive_qos: parseInt(e, 10) as 0 | 1 | 2, data_report_qos: parseInt(e, 10) as 0 | 1 | 2 } })}
                            >
                                <SelectTrigger className="bg-transparent border-0 !shadow-none !outline-none focus:!outline-none focus:!ring-0 focus:!ring-offset-0 focus:!shadow-none focus:!border-transparent focus-visible:!outline-none focus-visible:!ring-0 focus-visible:!ring-offset-0 text-right">
                                    <SelectValue />
                                </SelectTrigger>
                                <SelectContent>
                                    <SelectItem value="0">Qos0</SelectItem>
                                    <SelectItem value="1">Qos1</SelectItem>
                                    <SelectItem value="2">Qos2</SelectItem>
                                </SelectContent>
                            </Select>
                        </div>
                        <Separator />
                        <div className="flex gap-2 justify-between">
                            <Label className="shrink-0">{i18n._('sys.application_management.username')}</Label>
                            <Input
                              placeholder={i18n._('common.please_enter')}
                              variant="ghost"
                              value={mqttConfig.authentication.username}
                              onChange={(e) => setMqttConfig({ ...mqttConfig, authentication: { ...mqttConfig.authentication, username: (e.target as HTMLInputElement).value } })}
                            />
                        </div>
                        <Separator />
                        <div className="flex gap-2 justify-between">
                            <Label className="shrink-0">{i18n._('sys.application_management.password')}</Label>
                            <div className="flex flex-1 gap-2 justify-between">
                                <Input
                                  placeholder={i18n._('common.please_enter')}
                                  type={isPasswordVisible ? 'text' : 'password'}
                                  variant="ghost"
                                  value={mqttConfig.authentication.password}
                                  onChange={(e) => setMqttConfig({ ...mqttConfig, authentication: { ...mqttConfig.authentication, password: (e.target as HTMLInputElement).value } })}
                                />
                                <button
                                  type="button"
                                  onClick={handlePasswordVisible}
                                  className="flex items-center bg-transparent pr-4 border-none cursor-pointer disabled:opacity-50"
                                >
                                    {isPasswordVisible ? (
                                        <SvgIcon className="w-4 h-4" icon="visibility" />
                                    ) : (
                                        <SvgIcon className="w-4 h-4" icon="visibility_off" />
                                    )}
                                </button>
                            </div>
                        </div>
                    </div>
                    {mqttConfig.connection.protocol_type === 'mqtts' && mqttsModule()}
                    <div className="w-full flex justify-end gap-2">
                        <Button variant="outline" className="w-20 mt-4" onClick={handleSaveMqttConfig}>
                            {i18n._('common.save')}
                        </Button>
                        <Button variant="primary" disabled={connectLoading} className="w-20 mt-4" onClick={() => (mqttConfig.status.connected ? disconnectMqtt() : connectMqtt())}>
                            {connectLoading ? (
                                <div className="w-full h-full flex items-center justify-center">
                                    <div className="w-4 h-4 rounded-full border-2 border-[#f24a00] border-t-transparent animate-spin" aria-label="loading" />

                                </div>
                            ) : (
                                mqttConfig.status.connected ? i18n._('common.disconnect') : i18n._('common.connect')
                            )}
                        </Button>
                    </div>
                </>
            )}
            {comfirmDialog()}
        </div>
    )
}