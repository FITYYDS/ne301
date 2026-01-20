import { useState, useEffect } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Card, CardTitle, CardContent } from '@/components/ui/card';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Progress } from '@/components/ui/progress';
import storageManagement from '@/services/api/storageManagement'
import StorageManagementSkeleton from './skeleton'

type StorageStatus = {
  status: string;
  color: string;
}

// Storage information returned from backend
type StorageInfo = {
  available_capacity_gb: number;
  available_capacity_mb: number;
  cyclic_overwrite_enabled: boolean;
  overwrite_threshold_percent: number; // 0-100
  sd_card_connected: boolean;
  status: 'normal' | 'warning' | 'full' | string;
  total_capacity_gb: number;
  total_capacity_mb: number;
  usage_percent: number; // 0-1 or 0-100, depends on backend definition
  used_capacity_gb: number;
  used_capacity_mb: number;
}

// Structure used by page progress bar
type StorageProgress = {
  used: number; // Percentage 0-100
  total: number; // Fixed 100 or total capacity conversion
  color: string; // Color value
}
export default function StorageManagement() {
  const { i18n } = useLingui();
  const [isLoading, setIsLoading] = useState(true)
  const { getStorage } = storageManagement

  const [storageInfo, setStorageInfo] = useState<StorageInfo | null>(null)
  const [storageStatus, setStorageStatus] = useState<StorageStatus>({
    status: "",
    color: "",
  });
  //  <=80% green, >80% yellow,>95% red
  const colorList = ['#22c55e', '#FE9A00', '#ef4444'];
  const [storageUsed, setStorageUsed] = useState<StorageProgress>({ used: 0, total: 100, color: colorList[0] });
  const initData = async () => {
    try {
      setIsLoading(true)
      const res = await getStorage()
      const data = (res && 'data' in res) ? (res as any).data as StorageInfo : (res as any as StorageInfo)
      setStorageInfo(data)
      const color = data.usage_percent > 95 ? colorList[2] : data.usage_percent > 80 ? colorList[1] : colorList[0]
      setStorageUsed({ used: data.usage_percent, total: 100, color })
    } catch (error) {
      console.error(error)
    } finally {
      setIsLoading(false)
    }
  }
  const initStorageStatus = () => {
    setStorageStatus(() => {
      let newStatus = ''
      let newColor = ''
      if (storageInfo?.status === 'no_card') {
        newStatus = 'not_detected'
        newColor = 'red-500'
      } else {
        newStatus = storageInfo?.status === 'normal' ? 'normal' : storageInfo?.status === 'warning' ? 'warning' : 'full'
        newColor = storageInfo?.status === 'normal' ? colorList[0] : storageInfo?.status === 'warning' ? colorList[1] : colorList[2]
      }
      return {
        status: newStatus,
        color: newColor,
      }
    })
  }
  useEffect(() => {
    initStorageStatus()
  }, [storageInfo?.status])

  useEffect(() => {
    initData()
  }, [])
  return (
    <div className="flex justify-center pt-4">
      <Card className="sm:w-xl w-full mx-4">
        <CardTitle className="pl-6">{i18n._('sys.storage_management.storage_management_title')}</CardTitle>
        <CardContent>
          {isLoading ? <StorageManagementSkeleton /> : (
            <div className="border bg-gray-100 rounded-md p-3 space-y-2">
              <div className="flex items-center justify-between gap-4">
                <Label className="text-sm">{i18n._('sys.storage_management.sd_card_status')}</Label>
                {storageStatus.status ? <div className={`flex items-center text-sm text-${storageStatus.color}`}><div className={`rounded-full w-2 h-2 mr-2 bg-${storageStatus.color}`}></div><p className={`text-${storageStatus.color} text-sm`}>{i18n._(`sys.storage_management.${storageStatus.status}`)}</p></div> : <div>--</div>}
              </div>
              <Separator />
              <div className="flex items-center justify-between gap-4">
                <Label className="text-sm">{i18n._('sys.storage_management.capacity_info')}</Label>
                <p className="text-sm">
                  <span>{i18n._('sys.storage_management.used')}</span>
                  {" "}
                  <span>{(Math.ceil(((storageInfo?.used_capacity_gb ?? 0) * 100)) / 100)}GB</span>
                  {" "}/ {" "}
                  <span>{Number(storageInfo?.available_capacity_gb ?? 0).toFixed(2)}GB</span>
                </p>
              </div>
              <div className="mb-4">
                <Progress
                  className="h-2 bg-gray-200"
                  value={storageUsed.used}
                  indicatorColor={storageUsed.color}
                />
              </div>
              <Separator />
              <div className="flex items-center justify-between gap-4">
                <Label className="text-sm">{i18n._('sys.storage_management.storage_policy')}</Label>
                {storageInfo?.cyclic_overwrite_enabled ? <p className="text-sm">{i18n._('sys.storage_management.loop_coverage')}</p> : <p className="text-sm">--</p>}
              </div>
            </div>
          )}
        </CardContent>
      </Card>
    </div>
  );
} 