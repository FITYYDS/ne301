import { createContext } from 'preact';
import { createPortal } from 'preact/compat';
import { useContext, useRef, useState, useEffect, useMemo } from 'preact/hooks';
import { cn } from '@/lib/utils';
import { Button } from './ui/button';
import SvgIcon from './svg-icon';

// Dialog Context
interface DialogContextValue {
    open: boolean;
    onOpenChange: (open: boolean) => void;
}

const DialogContext = createContext<DialogContextValue | null>(null);

function useDialogContext() {
    const context = useContext(DialogContext);
    if (!context) {
        throw new Error('Dialog components must be used within a Dialog');
    }
    return context;
}
// const [curr]

// Dialog Root
interface DialogProps {
    open?: boolean;
    onOpenChange?: (open: boolean) => void;
    children: any;
}

function Dialog({ open = false, onOpenChange, children }: DialogProps) {
    const [internalOpen, setInternalOpen] = useState(open);
    const isControlled = onOpenChange !== undefined;
    const isOpen = isControlled ? open : internalOpen;

    const handleOpenChange = (newOpen: boolean) => {
        if (!isControlled) {
            setInternalOpen(newOpen);
        }
        onOpenChange?.(newOpen);
    };

    useEffect(() => {
        if (isControlled) {
            setInternalOpen(open);
        }
    }, [open, isControlled]);

    const contextValue = useMemo(() => ({
        open: isOpen,
        onOpenChange: handleOpenChange
    }), [isOpen, handleOpenChange]);

    return (
        <DialogContext.Provider value={contextValue}>
            {children}
        </DialogContext.Provider>
    );
}

// Dialog Trigger
interface DialogTriggerProps {
    asChild?: boolean;
    children: any;
    onClick?: () => void;
}

function DialogTrigger({ asChild = false, children, onClick }: DialogTriggerProps) {
    const { onOpenChange } = useDialogContext();

    const handleClick = () => {
        onOpenChange(true);
        onClick?.();
    };

    if (asChild && children && typeof children === 'object') {
        return children.props ? { ...children, props: { ...children.props, onClick: handleClick } } : children;
    }

    return (
        <button type="button" onClick={handleClick}>
            {children}
        </button>
    );
}

// Dialog Content - Now directly rendered to DOM
interface DialogContentProps {
    className?: string;
    children: any;
    onPointerDownOutside?: () => void;
    onEscapeKeyDown?: () => void;
    showCloseButton?: boolean;
}

function DialogContent({ 
    className, 
    children, 
    onPointerDownOutside,
    onEscapeKeyDown,
    showCloseButton = true
}: DialogContentProps) {
    const { open, onOpenChange } = useDialogContext();
    const contentRef = useRef<HTMLDivElement>(null);
    
    useEffect(() => {
        const handleEscape = (e: KeyboardEvent) => {
            if (e.key === 'Escape') {
                onEscapeKeyDown?.();
                onOpenChange(false);
            }
        };
        
        if (open) {
            document.addEventListener('keydown', handleEscape);
            return () => document.removeEventListener('keydown', handleEscape);
        }
        return undefined;
    }, [open, onOpenChange, onEscapeKeyDown]);

    const handlePointerDown = (e: any) => {
        if (contentRef.current && !contentRef.current.contains(e.target)) {
            onPointerDownOutside?.();
            onOpenChange(false);
        }
    };

    // If Dialog is not open, render nothing
    if (!open) return null;

    // Use Portal to render to body, avoid being affected by parent stacking context/transform
    return createPortal(
        (
            <div className="fixed inset-0 z-50 flex items-center justify-center p-4 overflow-y-auto">
                {/* Overlay */}
                <div 
                  className="fixed inset-0 bg-black/80 backdrop-blur-sm"
                  onClick={() => onOpenChange(false)}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter' || e.key === ' ') {
                      onOpenChange(false);
                    }
                  }}
                  role="button"
                  tabIndex={0}
                  aria-label="close dialog"
                />
                {/* Content */}
                <div
                  className={cn(
                        "relative z-50 w-full max-w-lg max-h-[calc(100vh-2rem)] gap-4 border bg-white dark:bg-gray-900 shadow-lg duration-200 rounded-lg overflow-hidden flex flex-col",
                        className
                    )}
                  ref={contentRef}
                  onPointerDown={handlePointerDown}
                >
                    {/* Close Button */}
                    {showCloseButton && (
                      <Button 
                        variant="ghost" 
                        onClick={() => onOpenChange(false)}
                        className="absolute right-4 top-4 h-8 w-8 p-0 rounded-full opacity-70 hover:opacity-100 transition-opacity z-10"
                      >
                       <SvgIcon icon="close" className="w-4 h-4" />
                      </Button>
                    )}
                    
                    {/* Scrollable Content Area */}
                    <div className="flex-1 overflow-y-auto overflow-x-auto p-6 min-h-0">
                        {children}
                    </div>
                </div>
            </div>
        ), document.body
    );
}

// Dialog Header
interface DialogHeaderProps {
    className?: string;
    children: any;
}

function DialogHeader({ className, children }: DialogHeaderProps) {
    return (
        <div className={cn("flex flex-col space-y-1.5 text-center sm:text-left", className)}>
            {children}
        </div>
    );
}

// Dialog Footer
interface DialogFooterProps {
    className?: string;
    children: any;
}

function DialogFooter({ className, children }: DialogFooterProps) {
    return (
        <div className={cn("flex flex-row justify-end space-x-2", className)}>
            {children}
        </div>
    );
}

// Dialog Title
interface DialogTitleProps {
    className?: string;
    children: any;
}

function DialogTitle({ className, children }: DialogTitleProps) {
    return (
        <h2 className={cn("text-lg font-semibold leading-none tracking-tight text-gray-900 dark:text-gray-100", className)}>
            {children}
        </h2>
    );
}

// Dialog Description
interface DialogDescriptionProps {
    className?: string;
    children: any;
}

function DialogDescription({ className, children }: DialogDescriptionProps) {
    return (
        <p className={cn("text-sm text-gray-600 dark:text-gray-400", className)}>
            {children}
        </p>
    );
}

// Dialog Close
interface DialogCloseProps {
    className?: string;
    children: any;
    onClick?: () => void;
    asChild?: boolean;
}

function DialogClose({ className, children, onClick, asChild = false }: DialogCloseProps) {
    const { onOpenChange } = useDialogContext();

    const handleClick = () => {
        onOpenChange(false);
        onClick?.();
    };

    if (asChild && children && typeof children === 'object') {
        return children.props ? { ...children, props: { ...children.props, onClick: handleClick } } : children;
    }

    return (
        <Button
          type="button"
          variant="primary"
          className={cn(
                "absolute right-4 top-4 rounded-sm opacity-70 ring-offset-background transition-opacity hover:opacity-100 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:ring-offset-2 disabled:pointer-events-none",
                className
            )}
          onClick={handleClick}
        >
            {children}
        </Button>
    );
}

export {
    Dialog,
    DialogClose,
    DialogContent,
    DialogDescription,
    DialogFooter,
    DialogHeader,
    DialogTitle,
    DialogTrigger,
};