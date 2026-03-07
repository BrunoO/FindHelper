/**
 * @file DragDrop_mac.mm
 * @brief macOS implementation of shell drag-and-drop utilities
 */

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "platform/windows/DragDropUtils.h"
#include "utils/Logger.h"

#include <string>
#include <string_view>

// Objective-C declarations must be at global scope (not inside C++ namespace).
/**
 * @interface MacDragSource
 * @brief Simple implementation of NSDraggingSource protocol for file drag-and-drop
 */
@interface MacDragSource : NSObject <NSDraggingSource>
@end

@implementation MacDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession *)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
  // We only support copy operations for now, matching Windows behavior
  return NSDragOperationCopy;
}
@end

namespace drag_drop {

bool StartFileDragDrop(std::string_view full_path_utf8) {
  if (full_path_utf8.empty()) {
    LOG_WARNING("StartFileDragDrop called with empty path");
    return false;
  }

  const std::string path(full_path_utf8);

  // Convert UTF-8 path to NSString
  NSString* ns_path = [NSString stringWithUTF8String:path.c_str()];
  if (!ns_path) {
    LOG_ERROR("StartFileDragDrop: Failed to convert path to NSString: " + path);
    return false;
  }

  // Validate that the path exists and is a file (not a directory)
  NSFileManager* file_manager = [NSFileManager defaultManager];
  BOOL is_directory = NO;
  if (![file_manager fileExistsAtPath:ns_path isDirectory:&is_directory]) {
    LOG_WARNING("StartFileDragDrop: Path does not exist: " + path);
    return false;
  }

  if (is_directory) {
    LOG_WARNING("StartFileDragDrop: Directories are not supported for drag-and-drop: " + path);
    return false;
  }

  // Perform the drag operation on the main thread
  __block bool started = false;
  dispatch_block_t drag_block = ^{
    NSWindow* window = [NSApp keyWindow];
    if (!window) {
      // Fallback to main window if key window is not available
      window = [NSApp mainWindow];
    }

    if (!window) {
      LOG_ERROR("StartFileDragDrop: No active window found to start drag session");
      return;
    }

    NSView* content_view = [window contentView];
    if (!content_view) {
      LOG_ERROR("StartFileDragDrop: No content view found for window");
      return;
    }

    // Create a file URL for the pasteboard
    NSURL* file_url = [NSURL fileURLWithPath:ns_path];

    // Create a dragging item with the file URL as a pasteboard writer
    NSDraggingItem* dragging_item = [[NSDraggingItem alloc] initWithPasteboardWriter:file_url];

    // Set a default dragging frame (icon size) and use the file's icon as drag image
    // so the user has a clear visual indication during the drag (macOS does not show one by default).
    NSEvent* current_event = [NSApp currentEvent];
    const CGFloat kDragIconSize = 32.0;
    NSRect dragging_frame = NSMakeRect(0, 0, kDragIconSize, kDragIconSize);

    NSImage* drag_image = [[NSWorkspace sharedWorkspace] iconForFile:ns_path];
    if (drag_image) {
      [drag_image setSize:NSMakeSize(kDragIconSize, kDragIconSize)];
    }

    if (current_event) {
      // Center the icon on the mouse cursor
      NSPoint mouse_location = [content_view convertPoint:[current_event locationInWindow] fromView:nil];
      dragging_frame.origin = NSMakePoint(mouse_location.x - kDragIconSize / 2.0,
                                            mouse_location.y - kDragIconSize / 2.0);
    }

    [dragging_item setDraggingFrame:dragging_frame contents:drag_image];

    // Start the dragging session
    static MacDragSource* source = [[MacDragSource alloc] init];
    [content_view beginDraggingSessionWithItems:@[dragging_item]
                                          event:current_event
                                         source:source];
    started = true;
    LOG_INFO("StartFileDragDrop: Drag session started for path: " + path);
  };

  if ([NSThread isMainThread]) {
    drag_block();
  } else {
    // If called from a background thread, dispatch to main thread and wait
    dispatch_sync(dispatch_get_main_queue(), drag_block);
  }

  return started;
}

} // namespace drag_drop
