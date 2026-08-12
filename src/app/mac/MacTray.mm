#include "app/mac/MacTray.h"

#import <AppKit/AppKit.h>

@class KnobTrayTarget;

struct MacTray::Impl {
    NSStatusItem *item = nil;
    NSMenuItem *statusLine = nil;
    NSMenuItem *permissionItem = nil;
    NSMenuItem *enabledItem = nil;
    KnobTrayTarget *target = nil;
    MacTray::Callbacks callbacks;
};

@interface KnobTrayTarget : NSObject
@property(nonatomic, assign) MacTray::Impl *impl;
@end

@implementation KnobTrayTarget
- (void)toggleEnabled:(id)sender {
    NSMenuItem *item = (NSMenuItem *)sender;
    const bool nowOn = item.state != NSControlStateValueOn;
    item.state = nowOn ? NSControlStateValueOn : NSControlStateValueOff;
    if (self.impl->callbacks.toggleEnabled) {
        self.impl->callbacks.toggleEnabled(nowOn);
    }
}
- (void)openSettings:(id)sender {
    if (self.impl->callbacks.openSettings) {
        self.impl->callbacks.openSettings();
    }
}
- (void)grantPermission:(id)sender {
    if (self.impl->callbacks.grantPermission) {
        self.impl->callbacks.grantPermission();
    }
}
- (void)quitApp:(id)sender {
    if (self.impl->callbacks.quit) {
        self.impl->callbacks.quit();
    }
}
@end

namespace {

// Knob glyph, drawn as a template image so the menu bar tints it for
// light/dark and for the highlighted state.
NSImage *makeKnobIcon(bool connected) {
    NSImage *image = [NSImage imageWithSize:NSMakeSize(18, 18)
                                    flipped:NO
                             drawingHandler:^BOOL(NSRect) {
                                 [[NSColor blackColor] set];
                                 NSBezierPath *circle = [NSBezierPath
                                     bezierPathWithOvalInRect:NSMakeRect(2, 1.5, 14, 14)];
                                 circle.lineWidth = 1.5;
                                 [circle stroke];

                                 NSBezierPath *notch = [NSBezierPath bezierPath];
                                 [notch moveToPoint:NSMakePoint(9, 15.5)];
                                 [notch lineToPoint:NSMakePoint(9, 12)];
                                 notch.lineWidth = 1.5;
                                 [notch stroke];

                                 if (connected) {
                                     [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(7.5, 7,
                                                                                        3, 3)]
                                         fill];
                                 }
                                 return YES;
                             }];
    [image setTemplate:YES];
    return image;
}

} // namespace

MacTray::MacTray(bool enabledChecked, Callbacks callbacks) : impl_(std::make_unique<Impl>()) {
    impl_->callbacks = std::move(callbacks);

    impl_->target = [KnobTrayTarget new];
    impl_->target.impl = impl_.get();

    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Knob"];
    menu.autoenablesItems = NO;

    impl_->statusLine = [[NSMenuItem alloc] initWithTitle:@"Knob: not connected"
                                                   action:nil
                                            keyEquivalent:@""];
    impl_->statusLine.enabled = NO;
    [menu addItem:impl_->statusLine];

    impl_->permissionItem =
        [[NSMenuItem alloc] initWithTitle:@"Grant Accessibility permission…"
                                   action:@selector(grantPermission:)
                            keyEquivalent:@""];
    impl_->permissionItem.target = impl_->target;
    impl_->permissionItem.hidden = YES;
    [menu addItem:impl_->permissionItem];

    [menu addItem:[NSMenuItem separatorItem]];

    impl_->enabledItem = [[NSMenuItem alloc] initWithTitle:@"Smooth scrolling"
                                                    action:@selector(toggleEnabled:)
                                             keyEquivalent:@""];
    impl_->enabledItem.target = impl_->target;
    impl_->enabledItem.state = enabledChecked ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:impl_->enabledItem];

    NSMenuItem *settingsItem = [[NSMenuItem alloc] initWithTitle:@"Settings…"
                                                          action:@selector(openSettings:)
                                                   keyEquivalent:@""];
    settingsItem.target = impl_->target;
    [menu addItem:settingsItem];

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                                      action:@selector(quitApp:)
                                               keyEquivalent:@"q"];
    quitItem.target = impl_->target;
    [menu addItem:quitItem];

    impl_->item =
        [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    impl_->item.button.image = makeKnobIcon(false);
    impl_->item.button.toolTip = @"Knob";
    impl_->item.menu = menu;
}

MacTray::~MacTray() {
    if (impl_->item) {
        [[NSStatusBar systemStatusBar] removeStatusItem:impl_->item];
        impl_->item = nil;
    }
}

void MacTray::setStatusText(const QString &text) {
    impl_->statusLine.title = text.toNSString();
}

void MacTray::setPermissionItemVisible(bool visible) {
    impl_->permissionItem.hidden = !visible;
}

void MacTray::setConnected(bool connected) {
    impl_->item.button.image = makeKnobIcon(connected);
}
