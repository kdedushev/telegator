/*
This file is part of Telegator, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telegator/telegator_file_preview.h"

#include "base/platform/mac/base_utilities_mac.h"
#include "core/mime_type.h"

#include <Cocoa/Cocoa.h>
#include <Quartz/Quartz.h>
#include <objc/runtime.h>

@interface TelegatorPreviewSource
	: NSObject<QLPreviewPanelDataSource, QLPreviewPanelDelegate> {
	NSURL *_url;
}
- (id)initWithURL:(NSURL*)url;
@end

@implementation TelegatorPreviewSource

- (id)initWithURL:(NSURL*)url {
	if (self = [super init]) {
		_url = [url retain];
	}
	return self;
}

- (void)dealloc {
	[_url release];
	[super dealloc];
}

- (NSInteger)numberOfPreviewItemsInPreviewPanel:(QLPreviewPanel*)panel {
	return 1;
}

- (id<QLPreviewItem>)previewPanel:(QLPreviewPanel*)panel
		previewItemAtIndex:(NSInteger)index {
	return _url;
}

@end

namespace Telegator {
namespace {

TelegatorPreviewSource *Source = nil;

// Quick Look asks the responder chain who controls the panel.
BOOL AcceptsPanel(id self, SEL _cmd, QLPreviewPanel *panel) {
	return Source != nil;
}

void BeginPanel(id self, SEL _cmd, QLPreviewPanel *panel) {
	panel.dataSource = Source;
	panel.delegate = Source;
}

// Control moves between the chains as windows become key, the file
// stays: Source lives until the next file replaces it.
void EndPanel(id self, SEL _cmd, QLPreviewPanel *panel) {
}

void AddControl(Class type) {
	if (!type) {
		return;
	}
	class_addMethod(
		type,
		@selector(acceptsPreviewPanelControl:),
		(IMP)AcceptsPanel,
		"c@:@");
	class_addMethod(
		type,
		@selector(beginPreviewPanelControl:),
		(IMP)BeginPanel,
		"v@:@");
	class_addMethod(
		type,
		@selector(endPreviewPanelControl:),
		(IMP)EndPanel,
		"v@:@");
}

// The key window and the application are in every responder chain
// Quick Look walks; class_addMethod keeps methods a class already has.
void BecomePanelController() {
	AddControl(object_getClass(NSApp));
	if (const auto window = [NSApp keyWindow]) {
		AddControl(object_getClass(window));
	}
}

} // namespace

bool PreviewFile(const QString &path) {
	// Programs keep Telegram's warning before they are launched.
	if (path.isEmpty()
		|| Core::DetectNameType(path) == Core::NameType::Executable) {
		return false;
	}
	@autoreleasepool {

	const auto url = [NSURL fileURLWithPath:Platform::Q2NSString(path)];
	if (!url) {
		return false;
	}
	BecomePanelController();
	auto panel = [QLPreviewPanel sharedPreviewPanel];
	if (!panel) {
		return false;
	}
	// The panel does not retain its data source: release the previous
	// one only after the panel is switched to the new one.
	const auto previous = Source;
	Source = [[TelegatorPreviewSource alloc] initWithURL:url];
	[panel makeKeyAndOrderFront:nil];
	// Set directly as well: without a controller found in the chain
	// the panel shows "No items selected".
	panel.dataSource = Source;
	panel.delegate = Source;
	[panel reloadData];
	[previous release];
	return true;

	}
}

} // namespace Telegator
