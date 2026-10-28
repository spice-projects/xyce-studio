#import <AppKit/AppKit.h>

#include "clipboard.h"

// write the text to the general (default) pasteboard
void copy_to_clipboard(const std::string& text) {
    @autoreleasepool {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
        NSString* string = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
        if (string != nil) {
            [pasteboard setString:string forType:NSPasteboardTypeString];
        }
    }
}
