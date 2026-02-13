import { create } from 'zustand';

interface AiStatusActions {
  isAiInference: boolean;
  setIsAiInference: (isAiInference: boolean) => void;
}

interface AiStatusData {
  aiStatus: string;
  setAiStatus: (status: string) => void;
}

export const useAiStatusStore = create<AiStatusActions & AiStatusData>((set) => ({
  isAiInference: false,
  setIsAiInference: (isAiInference: boolean) => set({ isAiInference }),

  aiStatus: 'unloaded',
  setAiStatus: (status: string) => set({ aiStatus: status }),
}));