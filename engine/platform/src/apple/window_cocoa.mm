#include "next/platform/window.h"

#import <Cocoa/Cocoa.h>

namespace Next {
class WindowCocoa;
}

@interface NEXTWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) Next::WindowCocoa* owner;
@end

namespace Next {

class WindowCocoa : public Window {
public:
    WindowCocoa()
        : window_(nil)
        , contentView_(nil)
        , delegate_(nil)
        , width_(0)
        , height_(0)
        , shouldClose_(false) {}

    ~WindowCocoa() override {
        Shutdown();
    }

    bool Initialize(const WindowDesc& desc) override {
        @autoreleasepool {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

            const NSUInteger style = NSWindowStyleMaskTitled |
                                     NSWindowStyleMaskClosable |
                                     NSWindowStyleMaskMiniaturizable |
                                     (desc.resizable ? NSWindowStyleMaskResizable : 0);

            NSRect rect = NSMakeRect(0, 0, desc.width, desc.height);
            window_ = [[NSWindow alloc] initWithContentRect:rect
                                                  styleMask:style
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
            if (!window_) {
                return false;
            }
            [window_ setReleasedWhenClosed:NO];

            NSString* title = [NSString stringWithUTF8String:desc.title ? desc.title : "NEXT Engine"];
            [window_ setTitle:title];
            [window_ center];
            [window_ makeKeyAndOrderFront:nil];

            contentView_ = [window_ contentView];
            width_ = desc.width;
            height_ = desc.height;
            shouldClose_ = false;

            delegate_ = [[NEXTWindowDelegate alloc] init];
            delegate_.owner = this;
            [window_ setDelegate:delegate_];

            [NSApp activateIgnoringOtherApps:YES];
            return true;
        }
    }

    void Shutdown() override {
        @autoreleasepool {
            if (window_) {
                [window_ setDelegate:nil];
                if (contentView_) {
                    [contentView_ setLayer:nil];
                    [contentView_ setWantsLayer:NO];
                }
                [window_ orderOut:nil];
            }
            window_ = nil;
            contentView_ = nil;
            delegate_ = nil;
            shouldClose_ = true;
        }
    }

    void PollEvents() override {
        @autoreleasepool {
            for (;;) {
                NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                    untilDate:[NSDate distantPast]
                                                       inMode:NSDefaultRunLoopMode
                                                      dequeue:YES];
                if (!event) {
                    break;
                }
                [NSApp sendEvent:event];
            }
            [NSApp updateWindows];
        }
    }

    void SwapBuffers() override {}

    void SetTitle(const char* title) override {
        @autoreleasepool {
            if (window_) {
                [window_ setTitle:[NSString stringWithUTF8String:title ? title : "NEXT Engine"]];
            }
        }
    }

    void Resize(int width, int height) override {
        @autoreleasepool {
            if (!window_ || width <= 0 || height <= 0) {
                return;
            }

            NSRect frame = [window_ frameRectForContentRect:NSMakeRect(0, 0, width, height)];
            NSRect current = [window_ frame];
            frame.origin = current.origin;
            [window_ setFrame:frame display:YES animate:NO];
            UpdateCachedSize();
        }
    }

    int GetWidth() const override { return width_; }
    int GetHeight() const override { return height_; }
    bool ShouldClose() const override { return shouldClose_; }
    void* GetNativeHandle() const override { return (__bridge void*)contentView_; }

    void HandleClose() {
        shouldClose_ = true;
    }

    void HandleResize() {
        UpdateCachedSize();
        if (resizeCallback_) {
            resizeCallback_(width_, height_);
        }
    }

private:
    void UpdateCachedSize() {
        if (!contentView_) {
            return;
        }

        const NSRect bounds = [contentView_ bounds];
        width_ = static_cast<int>(bounds.size.width);
        height_ = static_cast<int>(bounds.size.height);
    }

    NSWindow* window_;
    NSView* contentView_;
    NEXTWindowDelegate* delegate_;
    int width_;
    int height_;
    bool shouldClose_;
};

Window* CreateWindow() {
    return new WindowCocoa();
}

} // namespace Next

@implementation NEXTWindowDelegate

- (BOOL)windowShouldClose:(id)sender {
    (void)sender;
    if (self.owner) {
        self.owner->HandleClose();
    }
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification {
    (void)notification;
    if (self.owner) {
        self.owner->HandleResize();
    }
}

@end
