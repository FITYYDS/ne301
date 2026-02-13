import { useState, useMemo, useEffect } from 'preact/hooks';
import { setItem } from '@/utils/storage';
import { createPortal } from 'preact/compat';
import { useLingui } from '@lingui/react'
import { Button } from '@/components/ui/button';
import type { ToolGuideProps } from './index'

interface Hole {
    x: number;
    y: number;
    width: number;
    height: number;
    rx?: number;

}

export default function ToolGuide({ refList, scollRef, isLoading, onClose }: ToolGuideProps) {
    const { i18n } = useLingui()

    /**
 * @TODO
 * Edge cases:
 * 1. If boxY is greater than half the viewport, set dialog above box
 * 2. If boxY is less than or equal to half the viewport, set dialog below box
 * 3. If dialog width is greater than viewport width, recalculate dialogX position
 * 4. If box area is outside view, smoothly scroll into view
    */

    // Internal state management
    const [currentStep, setCurrentStep] = useState(0);
    const [isShow, setIsShow] = useState(true);
    const [viewportTick, setViewportTick] = useState(0);

    const getViewport = () => {
        const vv = window.visualViewport
        return {
            width: vv?.width ?? window.innerWidth,
            height: vv?.height ?? window.innerHeight,
            offsetTop: vv?.offsetTop ?? 0,
            offsetLeft: vv?.offsetLeft ?? 0,
        }
    }

    // Add mask to body
    useEffect(() => {
        document.body.style.backgroundColor = 'rgba(0, 0, 0, 0.5)'
        return () => {
            document.body.style.backgroundColor = ''
        }
    }, [])

    // iPhone in landscape mode will trigger viewport change
    useEffect(() => {
        const vv = window.visualViewport
        const onViewportChange = () => setViewportTick((v) => v + 1)

        vv?.addEventListener('resize', onViewportChange)
        vv?.addEventListener('scroll', onViewportChange)
        window.addEventListener('resize', onViewportChange)

        return () => {
            vv?.removeEventListener('resize', onViewportChange)
            vv?.removeEventListener('scroll', onViewportChange)
            window.removeEventListener('resize', onViewportChange)
        }
    }, [])

    const guideList = {
        // aiInferenceRef: {
        //     title: i18n._('sys.device_tool.ai_inference'),
        //     description: i18n._('sys.device_tool.ai_desc'),
        //     footer: i18n._('common.next_step'),
        // },
        currentModelRef: {
            title: i18n._('sys.device_tool.select_model'),
            description: i18n._('sys.device_tool.model_desc'),
            footer: i18n._('common.next_step'),
        },
        patternRef: {
            title: i18n._('sys.device_tool.camera_title'),
            description: i18n._('sys.device_tool.camera_desc'),
            footer: i18n._('common.next_step'),
        },
        triggerRef: {
            title: i18n._('sys.device_tool.trigger'),
            description: i18n._('sys.device_tool.trigger_desc'),
            footer: i18n._('common.next_step'),
        },
        // importAndOutput: {
        //     title: i18n._('sys.device_tool.import_and_output'),
        //     description: i18n._('sys.device_tool.import_and_output_desc'),
        //     footer: i18n._('common.next_step'),
        // },
    }

    const totalStep = Object.keys(guideList).length;
    const content = {
        title: Object.values(guideList)[currentStep].title,
        description: Object.values(guideList)[currentStep].description,
        footer: Object.values(guideList)[currentStep].footer,
    };

    const [toolGuideProps, setToolGuideProps] = useState({
        boxWidth: 0,
        boxHeight: 0,
        boxX: 0,
        boxY: 0,
        dialogWidth: 300,
        dialogHeight: 150,
        dialogX: 0,
        dialogY: 0,
    })

    const calcDialogY = (rect: DOMRect, viewportHeight: number, offsetTop: number) => {
        const padding = 8
        const gap = 25
        const baseTop = rect.top + offsetTop
        const belowY = baseTop + rect.height + gap
        const aboveY = baseTop - toolGuideProps.dialogHeight - gap

        // put below if possible, otherwise put above
        const maxY = offsetTop + viewportHeight - toolGuideProps.dialogHeight - padding
        const minY = offsetTop + padding

        if (belowY <= maxY) return belowY
        if (aboveY >= minY) return aboveY

        // if both sides are not fully satisfied,尽量保证可见（夹逼到 viewport 内）
        return Math.max(minY, Math.min(belowY, maxY))
    }

    // Region boundary adjustment
    function resetViewPort(rect: DOMRect) {
        return new Promise<{ rect: DOMRect; dialogY: number } | boolean>((resolve, _reject) => {
            //  Need to scroll down
            setToolGuideProps(p => ({
                ...p,
                isShow: false,
            }))
            const el = refList?.[currentStep]?.current;
            if (!el) return;
            const parentEl = scollRef?.current
            const parentrect = parentEl?.getBoundingClientRect();
            const { height: viewportHeight, offsetTop } = getViewport()
            // rect is based on visual viewport, use visual viewport height to determine if scrolling is needed
            if (rect.bottom > viewportHeight) {
                const delta = rect.bottom - viewportHeight + 25
                const currentScrollTop = (parentEl as any)?.scrollTop ?? 0
                parentEl?.scrollTo({
                    top: currentScrollTop + delta,
                    behavior: 'smooth',
                })
                setTimeout(() => {
                    const newRect = el.getBoundingClientRect();
                    const resetDialogY = calcDialogY(newRect, viewportHeight, offsetTop)
                    setToolGuideProps(p => ({
                        ...p,
                        isShow: true,
                    }))
                    resolve({ rect: newRect, dialogY: resetDialogY })
                }, 500)
            } else if (rect && parentrect && rect.top < parentrect?.top) {
                const delta = parentrect.top - rect.top + 25
                const currentScrollTop = (parentEl as any)?.scrollTop ?? 0
                parentEl?.scrollTo({
                    top: Math.max(0, currentScrollTop - delta),
                    behavior: 'smooth',
                })
                setToolGuideProps(p => ({
                    ...p,
                    isShow: false,
                }))
                setTimeout(() => {
                    const newRect = el.getBoundingClientRect();
                    const scrollDialogY = calcDialogY(newRect, viewportHeight, offsetTop)
                    setToolGuideProps(p => ({
                        ...p,
                        isShow: true,
                    }))
                    resolve({ rect: newRect, dialogY: scrollDialogY })
                }, 500)
            } else {
                resolve(true)
            }
        })
    }
    useEffect(() => {
        const updateToolGuide = async () => {
            // if (currentStep === 3) {
            //     setImportAndOutput()
            //     return
            // }
            const el = refList?.[currentStep]?.current;  // Use currentStep instead of fixed 0
            if (!el) return;
            let rect = el.getBoundingClientRect();
            const { height: viewportHeight, offsetTop, offsetLeft } = getViewport()
            let updateDialogY = calcDialogY(rect, viewportHeight, offsetTop)
            // If box area is outside view, smoothly scroll into view
            const res = await resetViewPort(rect)
            if (res && typeof res === 'object' && 'dialogY' in res && 'rect' in res) {
                updateDialogY = (res as { rect: DOMRect; dialogY: number }).dialogY
                rect = (res as { rect: DOMRect; dialogY: number }).rect
            }
            setToolGuideProps(p => ({
                ...p,
                boxWidth: rect.width + 20,
                boxHeight: rect.height + 20,
                // use visualViewport offset to correct fixed coordinate system
                boxX: rect.left + offsetLeft - 10,
                boxY: rect.top + offsetTop - 10,
                dialogX: rect.left + offsetLeft - 10,
                dialogY: updateDialogY,
                dialogWidth: rect.width + 50,
                isShow: true,
            }));
        };

        updateToolGuide();
    }, [currentStep, isLoading, viewportTick]);

    // ----------base 
    const [isAnimating, setIsAnimating] = useState(false);

    // Trigger animation when step is clicked
    const handleStepChange = (newStep: number) => {
        setIsAnimating(true);
        setTimeout(() => {
            setStep(newStep);
            setIsAnimating(false);
        }, 200); // Animation duration
    };

    const setStep = (step: number) => {
        setCurrentStep(step);
    };

    const closeToolGuide = () => {
        setIsShow(false);
        onClose?.();
    };

    const handleClose = () => {
        setItem('guideFlag', 'false');
        closeToolGuide();
    }

    const handleNext = () => handleStepChange(currentStep + 1);
    const handlePrevious = () => handleStepChange(currentStep - 1);
    const handleDescription = () => {
        if (content.description.includes('\b')) {
            const parts = content.description.split('\b');
            return parts.map((part, index) => {
                // even indices are normal text, odd indices are bold text
                if (index % 2 === 1) {
                    // Bold text uses span
                    if (part.includes('\n')) {
                        return (
                            <span key={index}>
                                {part.split('\n').map((line, lineIndex) => (
                                    <span key={lineIndex}>
                                        <strong>{line}</strong>
                                        {lineIndex < part.split('\n').length - 1 && <br />}
                                    </span>
                                ))}
                            </span>
                        );
                    }
                    return <span key={index}><strong>{part}</strong></span>;
                }
                // Normal text also uses span to avoid nesting issues
                if (part.includes('\n')) {
                    return (
                        <span key={index}>
                            {part.split('\n').map((line, lineIndex) => (
                                <span key={lineIndex}>
                                    {line}
                                    {lineIndex < part.split('\n').length - 1 && <br />}
                                </span>
                            ))}
                        </span>
                    );
                }
                return <span key={index}>{part}</span>;
            });
        }

        // Handle line break only case, use div to avoid p nesting
        if (content.description.includes('\n')) {
            return (
                <div>
                    {content.description.split('\n').map((line, lineIndex) => (
                        <span key={lineIndex}>
                            {line}
                            {lineIndex < content.description.split('\n').length - 1 && <br />}
                        </span>
                    ))}
                </div>
            );
        }

        return <span>{content.description}</span>;
    };

    const boxZone = useMemo(() => (
        <div
          className="fixed transition-transform duration-500 ease-out"
          style={{
                width: toolGuideProps.boxWidth,
                height: toolGuideProps.boxHeight,
                transform: `translate(${toolGuideProps.boxX}px, ${toolGuideProps.boxY}px)`,
            }}
        />
    ), [toolGuideProps.boxWidth, toolGuideProps.boxHeight, toolGuideProps.boxX, toolGuideProps.boxY]);

    const dialogZone = useMemo(() => (
        <>
            {/* Dialog above */}
            {toolGuideProps.dialogY < toolGuideProps.boxY ? (
                <div
                  className={`fixed transition-all duration-300 ${isAnimating ? 'opacity-0 translate-y-2' : 'opacity-100 translate-y-0'
                        }`}
                  style={{
                        left: toolGuideProps.dialogX + toolGuideProps.dialogWidth / 2 - 20,
                        top: toolGuideProps.dialogY + toolGuideProps.dialogHeight,
                        width: 0,
                        height: 20
                    }}
                >
                    <svg width="30" height="20">
                        <polygon points="0,0 10,10 20,0" fill="#fff" />
                    </svg>
                </div>
            ) : (
                <div
                  className={`fixed transition-all duration-300 ${isAnimating ? 'opacity-0 translate-y-2' : 'opacity-100 translate-y-0'
                        }`}
                  style={{
                        left: toolGuideProps.dialogX + toolGuideProps.dialogWidth / 2 - 20,
                        top: toolGuideProps.dialogY - 10,
                        width: 0,
                        height: 20
                    }}
                >
                    <svg width="30" height="20">
                        <polygon points="10,0 0,10 20,10" fill="#fff" />
                    </svg>
                </div>
            )}

            <div
              className={`fixed flex flex-col bg-white rounded-lg p-4 transition-all duration-300 ${isAnimating ? 'opacity-0 translate-y-2' : 'opacity-100 translate-y-0'
                    }`}
              style={{ width: toolGuideProps.dialogWidth, left: toolGuideProps.dialogX, top: toolGuideProps.dialogY }}
            >
                <div className="flex justify-between items-center">
                    <p>{content.title}</p>
                    <Button
                      onClick={() => handleClose()}
                      variant="ghost"
                      className="h-8 w-8 p-0 rounded-full opacity-70 hover:opacity-100 transition-opacity"
                    >
                        <svg
                          xmlns="http://www.w3.org/2000/svg"
                          height="24px"
                          viewBox="0 -960 960 960"
                          width="24px"
                          fill="currentColor"
                          className="h-4 w-4"
                        >
                            <path d="m256-200-56-56 224-224-224-224 56-56 224 224 224-224 56 56-224 224 224 224-56 56-224-224-224 224Z" />
                        </svg>
                    </Button>
                </div>
                <div className="my-2">
                    <p className="text-sm text-text-secondary">{handleDescription()}</p>
                </div>
                <div className="flex justify-between flex-1 items-end">
                    <div className="text-sm text-gray-400"><span>{currentStep + 1}</span>/<span>{totalStep}</span></div>
                    <div className="flex gap-2">
                        {currentStep === 0 ? (
                            <Button variant="outline" onClick={() => handleClose()}>
                                {i18n._('common.skip')}
                            </Button>
                        ) : (
                            <Button variant="outline" onClick={() => handlePrevious()}>
                                {i18n._('common.previous_step')}
                            </Button>
                        )}

                        {/* Second button */}
                        {currentStep === totalStep - 1 ? (
                            <Button variant="primary" onClick={() => handleClose()}>
                                {i18n._('common.finish')}
                            </Button>
                        ) : (
                            <Button variant="primary" onClick={() => handleNext()}>
                                {i18n._('common.next_step')}
                            </Button>
                        )}
                    </div>
                </div>
            </div>
        </>
    ), [toolGuideProps.dialogY, toolGuideProps.boxY, toolGuideProps.dialogX, toolGuideProps.dialogWidth, toolGuideProps.dialogHeight, isAnimating, content.title, currentStep, totalStep, handleDescription, handleClose, handlePrevious, handleNext, i18n]);

    function Mask({
        box,
        opacity = 0.5,
        zIndex = 999,
    }: {
        box: Hole;
        opacity?: number;
        zIndex?: number;
    }) {
        if (typeof document === 'undefined') return null;

        return createPortal(
            <svg
              width="100%"
              height="100%"
              className="fixed inset-0"
              style={{ zIndex }}
            >
                <defs>
                    <mask id="mask-holes">
                        {/* White = show mask, black = cutout */}
                        <rect x="0" y="0" width="100%" height="100%" fill="white" />
                        <rect
                          x={box.x}
                          y={box.y}
                          width={box.width}
                          height={box.height}
                          rx={box.rx ?? 8}
                          fill="black"
                        />
                    </mask>
                </defs>
                {/* Semi-transparent mask, after applying mask the two areas become transparent without mask */}
                <rect x="0" y="0" width="100%" height="100%" fill="black" opacity={opacity} mask="url(#mask-holes)" />
            </svg>,
            document.body
        );
    }

    return (

        <>

            <Mask
              box={isShow ? { x: toolGuideProps.boxX, y: toolGuideProps.boxY, width: toolGuideProps.boxWidth, height: toolGuideProps.boxHeight } : { x: 0, y: 0, width: 0, height: 0 }}
              opacity={0.5}
              zIndex={999}
            />
            {isShow && (
                <div className="z-[1000]">
                    {boxZone}
                    {dialogZone}
                </div>
            )}
        </>

    )
}