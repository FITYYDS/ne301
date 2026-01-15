import { create } from 'zustand'
import systemSettings from '@/services/api/systemSettings'

interface CommunicationDataState {
    error: string | null
    communicationData: any
    apConfig: any
    getAPConfig: () => Promise<any>
    getCommunicationData: () => Promise<any>
}

export const useCommunicationData = create<CommunicationDataState>((set) => ({
    error: null,
    communicationData: null,
    apConfig: null,
    async getAPConfig() {
        set({ error: null })
        try {
            const res = await systemSettings.getAPConfigReq()
            set({ apConfig: res.data })
            return res
        } catch (error: any) {
            set({ error: error?.message ?? 'Failed to get communication data' })
        }
    },
    async getCommunicationData() {
        try {
            const res = await systemSettings.getNetworkStatusReq()
            set({ communicationData: res.data })
            return res
        } catch (error: any) {
            set({ error: error?.message ?? 'Failed to get communication data' })
        }
    }
}))