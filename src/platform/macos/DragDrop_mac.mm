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
#include <vector>

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

// Validate a UTF-8 path for drag-drop: converts to NSString, checks existence, rejects dirs.
// Returns a valid NSString* on success, nil on failure (logs the reason).
NSString* ValidatePathForDrag(std::string_view path_view) {
  const std::string path(path_view);  // null-terminated copy needed for NSString + logging
  NSString* ns_path = [NSString stringWithUTF8String:path.c_str()];
  if (!ns_path) {
    LOG_ERROR("StartFileDragDrop: Failed to convert path to NSString: " + path);
    return nil;
  }
  NSFileManager* file_manager = [NSFileManager defaultManager];
  BOOL is_directory = NO;
  if (![file_manager fileExistsAtPath:ns_path isDirectory:&is_directory]) {
    LOG_WARNING("StartFileDragDrop: Path does not exist: " + path);
    return nil;
  }
  if (is_directory) {
    LOG_WARNING("StartFileDragDrop: Directories are not supported for drag-and-drop: " + path);
    return nil;
  }
  return ns_path;
}

// Build an NSDraggingItem for the given NSString path, centered on the mouse cursor.
NSDraggingItem* MakeDraggingItem(NSString* ns_path, NSView* content_view, NSEvent* current_event) {
  NSURL* file_url = [NSURL fileURLWithPath:ns_path];
  NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:file_url];

  const CGFloat kDragIconSize = 32.0;
  NSRect frame = NSMakeRect(0, 0, kDragIconSize, kDragIconSize);
  NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:ns_path];
  if (icon) {
    [icon setSize:NSMakeSize(kDragIconSize, kDragIconSize)];
  }
  if (current_event) {
    NSPoint mouse_location = [content_view convertPoint:[current_event locationInWindow] fromView:nil];
    frame.origin = NSMakePoint(mouse_location.x - kDragIconSize / 2.0,
                               mouse_location.y - kDragIconSize / 2.0);
  }
  [item setDraggingFrame:frame contents:icon];
  return item;
}

bool StartFileDragDrop(std::string_view full_path_utf8) {
  if (full_path_utf8.empty()) {
    LOG_WARNING("StartFileDragDrop called with empty path");
    return false;
  }
  NSString* ns_path = ValidatePathForDrag(full_path_utf8);
  if (!ns_path) {
    return false;
  }

  __block bool started = false;
  dispatch_block_t drag_block = ^{
    NSWindow* window = [NSApp keyWindow] ?: [NSApp mainWindow];
    if (!window) {
      LOG_ERROR("StartFileDragDrop: No active window found to start drag session");
      return;
    }
    NSView* content_view = [window contentView];
    if (!content_view) {
      LOG_ERROR("StartFileDragDrop: No content view found for window");
      return;
    }
    NSEvent* current_event = [NSApp currentEvent];
    NSDraggingItem* item = MakeDraggingItem(ns_path, content_view, current_event);
    static MacDragSource* source = [[MacDragSource alloc] init];
    [content_view beginDraggingSessionWithItems:@[item]
                                          event:current_event
                                         source:source];
    started = true;
    LOG_INFO(std::string("StartFileDragDrop: Drag session started for path: ") + [ns_path UTF8String]);
  };

  if ([NSThread isMainThread]) {
    drag_block();
  } else {
    dispatch_sync(dispatch_get_main_queue(), drag_block);
  }
  return started;
}

bool StartFileDragDrop(const std::vector<std::string_view>& full_paths_utf8) {
  if (full_paths_utf8.empty()) {
    LOG_WARNING("StartFileDragDrop: No paths provided");
    return false;
  }

  // Validate all paths up front (Obj-C objects collected into a __block-captured array).
  NSMutableArray<NSString*>* valid_ns_paths = [NSMutableArray array];
  for (const std::string_view sv : full_paths_utf8) {
    if (NSString* ns_path = ValidatePathForDrag(sv)) {
      [valid_ns_paths addObject:ns_path];
    }
  }
  if ([valid_ns_paths count] == 0) {
    LOG_WARNING("StartFileDragDrop: No valid paths to drag");
    return false;
  }

  __block bool started = false;
  dispatch_block_t drag_block = ^{
    NSWindow* window = [NSApp keyWindow] ?: [NSApp mainWindow];
    if (!window) {
      LOG_ERROR("StartFileDragDrop: No active window found to start drag session");
      return;
    }
    NSView* content_view = [window contentView];
    if (!content_view) {
      LOG_ERROR("StartFileDragDrop: No content view found for window");
      return;
    }
    NSEvent* current_event = [NSApp currentEvent];
    NSMutableArray<NSDraggingItem*>* items = [NSMutableArray array];
    for (NSString* ns_path in valid_ns_paths) {
      [items addObject:MakeDraggingItem(ns_path, content_view, current_event)];
    }
    static MacDragSource* source = [[MacDragSource alloc] init];
    [content_view beginDraggingSessionWithItems:items
                                          event:current_event
                                         source:source];
    started = true;
    LOG_INFO("StartFileDragDrop: Drag session started for " +
             std::to_string([valid_ns_paths count]) + " file(s)");
  };

  if ([NSThread isMainThread]) {
    drag_block();
  } else {
    dispatch_sync(dispatch_get_main_queue(), drag_block);
  }
  return started;
}

} // namespace drag_drop
