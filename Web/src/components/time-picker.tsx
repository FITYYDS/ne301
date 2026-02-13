import { useCallback, useEffect, useRef, useState } from 'preact/hooks';
import { Popover, PopoverContent, PopoverTrigger } from '@/components/ui/popover';
import { cn } from '@/lib/utils'
import { ScrollArea } from "@/components/ui/scroll-area"
import SvgIcon from '@/components/svg-icon';
import { Input } from '@/components/ui/input';
import { Button } from '@/components/ui/button';
import { Separator } from '@/components/ui/separator';
import { useLingui } from '@lingui/react';

/**
 * Time picker
 * @TODO
 * - Do not use third-party time library
 * - Current time button
 * - Parameters: 12/24 hour format, hour/minute/second wheel
 */
type WheelType = 'hour' | 'minute' | 'second' | 'period' | 'weekUnit';

export interface TimePickerProps {
    timeType?: '12h' | '24h';
    value: string;
    format?: 'HH:mm:ss' | 'HH:mm';
    onChange: (value: string) => void;
    disabled?: boolean;
    className?: string;
    customSlot?: (cb: () => void) => React.ReactNode;
    showWeekSelect?: boolean;
}

interface TimeValue {
    hour: number;
    minute: number;
    second: number;
    period?: 'AM' | 'PM';
    weekUnit?: number;
}

export default function TimePicker({
    timeType = '24h',
    value,
    onChange,
    format = 'HH:mm',
    disabled = false,
    className,
    customSlot,
    showWeekSelect = false
}: TimePickerProps) {
    const { i18n } = useLingui();
    const [isOpen, setIsOpen] = useState(false);
    const scrollRefs = useRef<{ [key in WheelType]?: HTMLDivElement }>({});

    // ----------Extended day of week selector---------
    const WeekUnitMap = {
        '0': i18n._('common.everyday').toString(),
        '1': i18n._('common.monday').toString(),
        '2': i18n._('common.tuesday').toString(),
        '3': i18n._('common.wednesday').toString(),
        '4': i18n._('common.thursday').toString(),
        '5': i18n._('common.friday').toString(),
        '6': i18n._('common.saturday').toString(),
        '7': i18n._('common.sunday').toString(),
    }

    const weekSelect = () => (
        <ScrollArea
          ref={(el: HTMLDivElement | null) => {
                if (el) scrollRefs.current.weekUnit = el.querySelector('[data-radix-scroll-area-viewport]') as HTMLDivElement;
            }}
          className="h-full w-full"
        >
            <div className="flex flex-col py-2">
                {Object.keys(WeekUnitMap).map((key) => (
                    <Button
                      key={key}
                      variant={currentTime.weekUnit === Number(key) ? "default" : "ghost"}
                      className={cn(
                            "h-8 w-20 mx-2 flex items-center justify-center text-sm cursor-pointer transition-colors",
                            "hover:opacity-100 hover:bg-gray-100",
                            currentTime.weekUnit === Number(key) ? "!bg-primary/50  text-primary-foreground font-medium" : ""
                        )}
                      onClick={() => handleItemClick('weekUnit', Number(key) as number)}
                    >
                        {WeekUnitMap[key as keyof typeof WeekUnitMap]}
                    </Button>
                ))}
            </div>
        </ScrollArea>
    )

    // Parse time string
    const parseTime = useCallback((timeStr: string): TimeValue => {
        let timeStrValue = timeStr;
        let foundWeekUnit = 0;
        if (!timeStr) {
            const now = new Date();
            return {
                hour: timeType === '12h' ? (now.getHours() % 12 || 12) : now.getHours(),
                minute: now.getMinutes(),
                second: now.getSeconds(),
                period: timeType === '12h' ? (now.getHours() >= 12 ? 'PM' : 'AM') : undefined,
                weekUnit: showWeekSelect ? 0 : undefined,
            };
        }
        if (showWeekSelect) {
            const weekUnitStr = timeStr.split(' ')[0];
            [, timeStrValue] = timeStr.split(' ');
            foundWeekUnit = Number(Object.keys(WeekUnitMap).find(key => WeekUnitMap[key as keyof typeof WeekUnitMap] === weekUnitStr));
        }
        const timeRegex = format === 'HH:mm'
            ? (timeType === '12h'
                ? /^(\d{1,2}):(\d{2})\s*(AM|PM)$/i
                : /^(\d{1,2}):(\d{2})$/)
            : (timeType === '12h'
                ? /^(\d{1,2}):(\d{2}):(\d{2})\s*(AM|PM)$/i
                : /^(\d{1,2}):(\d{2}):(\d{2})$/);

        const match = timeStrValue.match(timeRegex);
        if (match) {
            return {
                hour: parseInt(match[1], 10),
                minute: parseInt(match[2], 10),
                second: format === 'HH:mm' ? 0 : parseInt(match[3], 10),
                period: timeType === '12h'
                    ? (format === 'HH:mm' ? match[3]?.toUpperCase() : match[4]?.toUpperCase()) as ('AM' | 'PM' | undefined) : undefined,
                weekUnit: showWeekSelect ? foundWeekUnit : undefined,
            };
        }
        return {
            hour: timeType === '12h' ? 12 : 0,
            minute: 0,
            second: 0,
            period: timeType === '12h' ? 'AM' : undefined,
            weekUnit: showWeekSelect ? foundWeekUnit : undefined,
        };
    }, [timeType, format]);

    // Format time to string
    const formatTime = useCallback((time: TimeValue): string => {
        const hour = time.hour.toString().padStart(2, '0');
        const minute = time.minute.toString().padStart(2, '0');
        const second = time.second.toString().padStart(2, '0');
        const timeStr = format === 'HH:mm' ? `${hour}:${minute}` : `${hour}:${minute}:${second}`;
        const formateTimeStr = timeType === '12h' ? `${timeStr} ${time.period}` : timeStr;
        const weekUnitStr = Object.entries(WeekUnitMap).find(([key]) => Number(key) === time.weekUnit);
        return showWeekSelect ? `${weekUnitStr?.[1]} ${formateTimeStr}` : formateTimeStr;
    }, [timeType, format, showWeekSelect]);

    const [currentTime, setCurrentTime] = useState<TimeValue>(() => parseTime(value));

    // Update time value
    const updateTime = useCallback((newTime: TimeValue) => {
        setCurrentTime(newTime);
        onChange(formatTime(newTime));
    }, [onChange, formatTime, showWeekSelect]);

    // Scroll to specified item
    const scrollToItem = useCallback((type: WheelType, index: number) => {
        const container = scrollRefs.current[type];
        if (container) {
            const itemHeight = 32;
            const containerHeight = container.clientHeight;
            const scrollTop = index * itemHeight - (containerHeight / 2) + (itemHeight / 2);
            container.scrollTo({ top: scrollTop, behavior: 'smooth' });
        }
    }, []);

    // Handle wheel item click
    const handleItemClick = useCallback((type: WheelType, newValue: number | string) => {
        const newTime = { ...currentTime };
        switch (type) {
            case 'hour':
                newTime.hour = newValue as number;
                break;
            case 'minute':
                newTime.minute = newValue as number;
                break;
            case 'second':
                newTime.second = newValue as number;
                break;
            case 'period':
                newTime.period = newValue as 'AM' | 'PM';
                break;
            case 'weekUnit':
                newTime.weekUnit = newValue as number;
                break;
            default:
                break;
        }
        updateTime(newTime);
    }, [currentTime, updateTime]);

    // Set current time
    const setNowTime = useCallback(() => {
        const now = new Date();
        const newTime: TimeValue = {
            hour: timeType === '12h' ? (now.getHours() % 12 || 12) : now.getHours(),
            minute: now.getMinutes(),
            second: now.getSeconds(),
            period: timeType === '12h' ? (now.getHours() >= 12 ? 'PM' : 'AM') : undefined
        };
        updateTime(newTime);
    }, [timeType, updateTime]);

    // Generate wheel items
    const wheelItems = useCallback((type: WheelType) => {
        let items: (number | string)[] = [];
        let currentValue: number | string = 0;

        switch (type) {
            case 'hour': {
                const hourScale = timeType === '24h' ? 24 : 12;
                items = Array.from({ length: hourScale }, (_, i) => (timeType === '24h' ? i : (i === 0 ? 12 : i)));
                currentValue = currentTime.hour;
                break;
            }
            case 'minute':
                items = Array.from({ length: 60 }, (_, i) => i);
                currentValue = currentTime.minute;
                break;
            case 'second':
                items = Array.from({ length: 60 }, (_, i) => i);
                currentValue = currentTime.second;
                break;
            case 'period':
                items = ['AM', 'PM'];
                currentValue = currentTime.period || 'AM';
                break;
            case 'weekUnit':
                items = Object.keys(WeekUnitMap).map(key => Number(key));
                currentValue = WeekUnitMap[currentTime.weekUnit as unknown as string as keyof typeof WeekUnitMap] || 0;
                break;
            default:
                break;
        }

        return (
            <ScrollArea
              className="h-full w-full py-2"
              ref={(el: HTMLDivElement | null) => {
                    if (el) scrollRefs.current[type] = el.querySelector('[data-radix-scroll-area-viewport]') as HTMLDivElement;
                }}
            >
                <div className="flex flex-col">
                    {items.map((item, index) => {
                        const displayValue = typeof item === 'number'
                            ? item.toString().padStart(2, '0') : item;
                        const isSelected = item === currentValue;

                        return (
                            <Button
                              variant="ghost"
                              key={index}
                              className={cn(
                                    "h-8 w-14 mx-2 flex items-center justify-center text-sm cursor-pointer transition-colors",
                                    "hover:opacity-100 hover:bg-gray-100",
                                    isSelected && "!bg-primary/50  text-primary-foreground font-medium"
                                )}
                              onClick={() => handleItemClick(type, item)}
                            >
                                {displayValue}
                            </Button>
                        );
                    })}
                </div>
            </ScrollArea>
        );
    }, [timeType, currentTime, handleItemClick]);

    const handleConfirm = useCallback(() => {
        setIsOpen(false);
        onChange(formatTime(currentTime));
    }, [currentTime, onChange, formatTime]);

    // Scroll to current value when popup opens
    useEffect(() => {
        if (isOpen) {
            setTimeout(() => {
                scrollToItem('hour', timeType === '24h' ? currentTime.hour
                    : (currentTime.hour === 12 ? 0 : currentTime.hour - 1));
                scrollToItem('minute', currentTime.minute);
                if (format === 'HH:mm:ss') {
                    scrollToItem('second', currentTime.second);
                }
                if (timeType === '12h' && currentTime.period) {
                    scrollToItem('period', currentTime.period === 'AM' ? 0 : 1);
                }
                if (showWeekSelect && currentTime.weekUnit) {
                    scrollToItem('weekUnit', currentTime.weekUnit);
                }

                if (showWeekSelect && typeof currentTime.weekUnit === 'number') {
                    scrollToItem('weekUnit', currentTime.weekUnit);
                }
            }, 100);
        }
    }, [isOpen, currentTime, showWeekSelect]);

    // Listen for external value changes
    useEffect(() => {
        const newTime = parseTime(value);
        setCurrentTime(newTime);
    }, [value, parseTime]);

    const displayValue = value || '';

    return (
        <Popover open={isOpen} onOpenChange={setIsOpen}>
            <PopoverTrigger asChild>
                <div className={cn("relative", className)}>
                    <Input
                      type="text"
                      value={displayValue}
                      placeholder={i18n._('common.selectTime')}
                      readOnly
                      disabled={disabled}
                      className="pr-10 cursor-pointer"
                    />
                    <Button
                      variant="ghost"
                      size="sm"
                      className="absolute right-0 top-0 h-full px-3 py-2"
                      disabled={disabled}
                    >
                        <SvgIcon icon="clock" className="h-4 w-4" />
                    </Button>
                </div>
            </PopoverTrigger>
            <PopoverContent className="p-0 w-auto">

                <div className="flex h-48">
                    {showWeekSelect && weekSelect()}
                    <div className="flex-1">
                        {wheelItems('hour')}
                    </div>
                    <Separator orientation="vertical" />
                    <div className="flex-1">
                        {wheelItems('minute')}
                    </div>
                    <Separator orientation="vertical" />
                    {format === 'HH:mm:ss' && (
                        <div className="flex-1">
                            {wheelItems('second')}
                        </div>
                    )}
                    {timeType === '12h' && (
                        <>
                            <Separator orientation="vertical" />
                            <div className="w-16">
                                {wheelItems('period')}
                            </div>
                        </>
                    )}
                </div>
                <div className="p-3 border-t">
                    <div className="flex gap-2 items-center justify-between">
                        {customSlot ? customSlot(() => { }) : (
                            <Button
                              variant="outline"
                              size="sm"
                              onClick={setNowTime}
                            >
                                {i18n._('common.now')}
                            </Button>
                        )}

                        <Button
                          variant="primary"
                          size="sm"
                          onClick={handleConfirm}
                        >
                            {i18n._('common.confirm')}
                        </Button>
                    </div>
                </div>
            </PopoverContent>
        </Popover>
    );
}