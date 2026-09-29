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

// Quick Look asks the responder chain who controls the panel; the
// application object is the end of every chain, whatever window is key.
BOOL AcceptsPanel(id self, SEL _cmd, QLPreviewPanel *panel) {
	return Source != nil;
}

void BeginPanel(id self, SEL _cmd, QLPreviewPanel *panel) {
	panel.dataSource = Source;
	panel.delegate = Source;
}

void EndPanel(id self, SEL _cmd, QLPreviewPanel *panel) {
	panel.dataSource = nil;
	panel.delegate = nil;
}

bool BecomePanelController() {
	static const auto result = [] {
		const auto app = object_getClass(NSApp);
		if (!app) {
			return false;
		}
		class_addMethod(
			app,
			@selector(acceptsPreviewPanelControl:),
			(IMP)AcceptsPanel,
			"c@:@");
		class_addMethod(
			app,
			@selector(beginPreviewPanelControl:),
			(IMP)BeginPanel,
			"v@:@");
		class_addMethod(
			app,
			@selector(endPreviewPanelControl:),
			(IMP)EndPanel,
			"v@:@");
		return true;
	}();
	return result;
}

} // namespace

bool PreviewFile(const QString &path) {
	// Programs keep Telegram's warning before they are launched.
	if (path.isEmpty()
		|| Core::DetectNameType(path) == Core::NameType::Executable
		|| !BecomePanelController()) {
		return false;
	}
	@autoreleasepool {

	const auto url = [NSURL fileURLWithPath:Platform::Q2NSString(path)];
	if (!url) {
		return false;
	}
	auto panel = [QLPreviewPanel sharedPreviewPanel];
	if (!panel) {
		return false;
	}
	// The panel does not retain its data source: release the previous
	// one only after the panel is switched to the new one.
	const auto previous = Source;
	Source = [[TelegatorPreviewSource alloc] initWithURL:url];
	if ([panel isVisible]) {
		panel.dataSource = Source;
		panel.delegate = Source;
		[panel reloadData];
	}
	[panel makeKeyAndOrderFront:nil];
	[panel reloadData];
	[previous release];
	return true;

	}
}

} // namespace Telegator
