import { useState, useEffect } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogFooter } from '@/components/dialog';
import { ScrollArea } from '@/components/ui/scroll-area';
import { Button } from '@/components/ui/button';
import systemApis from '@/services/api/system';
import Loading from '@/components/loading';
import { downloadFile } from '@/utils';

export default function Log({ isOpen, setOpen }: {isOpen: boolean, setOpen: (open: boolean) => void}) {
    const { i18n } = useLingui();
    const { getLogsReq, exportLogsReq } = systemApis;
    const [logs, setLogs] = useState<string>('');
    const [loading, setLoading] = useState(false);
    const getLogs = async () => {
        try {
            setLoading(true);
            const res = await getLogsReq();
            setLogs(res.data.content);
        } catch (error) {
            console.error('getLogs', error);
        } finally {
            setLoading(false);
        }
    }
    useEffect(() => {
        if (isOpen) {
            getLogs();
        }
    }, [isOpen]);
    const exportLogs = async () => {
        try {
            const res = await exportLogsReq();
            const logFiles = res.data.log_files
            const currentTime = new Date().getTime();
            const mergedContent = logFiles.reverse().map((file: any) => file.content);
            await downloadFile(mergedContent, `logs_${currentTime}.log`);
        } catch (error) {
            console.error('exportLogs', error);
        }
    }
    return (
        <Dialog open={isOpen} onOpenChange={setOpen}>
            <DialogContent className="md:max-w-4xl  w-full mx-4">
                <DialogHeader>
                    <DialogTitle>{i18n._('sys.header.log')}</DialogTitle>
                </DialogHeader>
                <ScrollArea className="h-[70vh] pr-2 pt-2">
                {loading ? <div className="flex items-center justify-center h-[70vh]"><Loading /></div> : (
                    <div className="text-sm text-gray-500 whitespace-pre-wrap">
                        {logs}
                    </div>
                )}
                </ScrollArea>
                <DialogFooter className="mt-4">
                    <Button className="w-1/2 md:w-auto" variant="outline"  onClick={() => setOpen(false)}>
                        {i18n._('common.cancel')}
                    </Button>
                    <Button className="w-1/2 md:w-auto" variant="primary" onClick={exportLogs}>
                        {i18n._('common.export')}
                    </Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    )       
}