#import "file_window.h"
#import <Cocoa/Cocoa.h>
#include <string>
#include <vector>
#include <thread>

#include "core/http_client.hpp"
#include "core/url.hpp"
#include "core/format.hpp"
#include "core/process.hpp"
#include "core/search.hpp"

@interface FileWindowController () <NSTableViewDataSource, NSTableViewDelegate>
@property (assign) opg::SearchResult searchResult;
@property (strong) NSTableView* table;
@property (strong) NSScrollView* scroll;
@property (strong) NSTextField* statusLabel;
@property (strong) NSProgressIndicator* progressBar;
@property (strong) NSButton* downloadButton;
@property (strong) NSMutableArray<NSDictionary*>* files;
@property (assign) BOOL downloading;
@end

@implementation FileWindowController

- (instancetype)initWithModel:(const opg::SearchResult&)model {
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 760, 500)
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered defer:NO];
    window.title = [NSString stringWithFormat:@"选择 GGUF 文件 - %s/%s", model.owner.c_str(), model.repository.c_str()];
    [window center];

    if ((self = [super initWithWindow:window])) {
        _searchResult = model;
        _files = [NSMutableArray array];
        [self setupUI];
        [self fetchFiles];
    }
    return self;
}

- (void)setupUI {
    NSView* v = self.window.contentView;

    self.table = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 760, 400)];
    self.table.usesAlternatingRowBackgroundColors = YES;
    self.table.dataSource = self;
    self.table.delegate = self;
    
    NSTableColumn* colName = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    colName.title = @"文件名";
    colName.width = 450;
    [self.table addTableColumn:colName];

    NSTableColumn* colSize = [[NSTableColumn alloc] initWithIdentifier:@"size"];
    colSize.title = @"大小";
    colSize.width = 150;
    [self.table addTableColumn:colSize];

    self.scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(20, 80, 720, 400)];
    self.scroll.hasVerticalScroller = YES;
    self.scroll.borderType = NSBezelBorder;
    self.scroll.documentView = self.table;
    self.scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [v addSubview:self.scroll];

    self.statusLabel = [NSTextField labelWithString:@"正在读取文件列表..."];
    self.statusLabel.frame = NSMakeRect(20, 50, 720, 20);
    [v addSubview:self.statusLabel];

    self.progressBar = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 25, 600, 20)];
    [self.progressBar setStyle:NSProgressIndicatorStyleBar];
    [self.progressBar setIndeterminate:NO];
    [self.progressBar setMinValue:0];
    [self.progressBar setMaxValue:100];
    [self.progressBar setHidden:YES];
    [v addSubview:self.progressBar];

    self.downloadButton = [[NSButton alloc] initWithFrame:NSMakeRect(630, 20, 110, 32)];
    [self.downloadButton setTitle:@"下载并导入"];
    [self.downloadButton setBezelStyle:NSBezelStyleRounded];
    [self.downloadButton setTarget:self];
    [self.downloadButton setAction:@selector(onDownload:)];
    [self.downloadButton setEnabled:NO];
    [v addSubview:self.downloadButton];
}

- (void)fetchFiles {
    __weak FileWindowController* weakSelf = self;
    opg::SearchResult model = self.searchResult;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        std::string err;
        std::vector<opg::GgufFile> files = opg::fetchModelFiles(model, err);
        
        dispatch_async(dispatch_get_main_queue(), ^{
            FileWindowController* s = weakSelf;
            if (!s) return;
            if (!err.empty()) {
                s.statusLabel.stringValue = [NSString stringWithFormat:@"读取失败: %s", err.c_str()];
                return;
            }
            for (const auto& f : files) {
                NSDictionary* dict = @{
                    @"name": [NSString stringWithUTF8String:f.name.c_str()],
                    @"path": [NSString stringWithUTF8String:f.path.c_str()],
                    @"size": @(f.size)
                };
                [s.files addObject:dict];
            }
            [s.table reloadData];
            s.statusLabel.stringValue = [NSString stringWithFormat:@"找到 %zu 个 GGUF 文件。", files.size()];
            s.downloadButton.enabled = (files.size() > 0);
        });
    });
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    return self.files.count;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
    NSString* ident = tableColumn.identifier;
    NSTextField* cell = [tableView makeViewWithIdentifier:ident owner:self];
    if (!cell) {
        cell = [NSTextField labelWithString:@""];
        cell.identifier = ident;
    }
    if (row < 0 || row >= self.files.count) return cell;

    NSDictionary* dict = self.files[row];
    if ([ident isEqualToString:@"name"]) {
        cell.stringValue = dict[@"name"];
    } else if ([ident isEqualToString:@"size"]) {
        std::string s = opg::formatBytes([dict[@"size"] longLongValue]);
        cell.stringValue = [NSString stringWithUTF8String:s.c_str()];
    }
    return cell;
}

- (void)onDownload:(id)sender {
    NSInteger row = self.table.selectedRow;
    if (row < 0 || row >= self.files.count) {
        self.statusLabel.stringValue = @"请先选择一个文件。";
        return;
    }
    if (self.downloading) return;

    NSDictionary* dict = self.files[row];
    NSString* filename = dict[@"name"];

    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = filename;
    panel.allowedFileTypes = @[@"gguf"];
    panel.title = @"选择保存位置";

    if ([panel runModal] != NSModalResponseOK) return;
    NSString* savePath = panel.URL.path;

    std::string url = opg::buildDownloadUrl(
        self.searchResult.owner, self.searchResult.repository, self.searchResult.revision,
        [dict[@"path"] UTF8String]);
    std::string dest = [savePath UTF8String];

    self.downloading = YES;
    self.downloadButton.enabled = NO;
    self.statusLabel.stringValue = @"准备下载...";
    [self.progressBar setHidden:NO];
    [self.progressBar setDoubleValue:0];

    __weak FileWindowController* weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        auto http = opg::createPlatformHttpClient();
        std::string err;
        
        bool success = http->download(url, dest, [weakSelf](uint64_t downloaded, uint64_t total) -> bool {
            dispatch_async(dispatch_get_main_queue(), ^{
                FileWindowController* s = weakSelf;
                if (!s) return;
                if (total > 0) {
                    [s.progressBar setDoubleValue:(100.0 * downloaded / total)];
                    std::string got = opg::formatBytes(downloaded);
                    std::string tot = opg::formatBytes(total);
                    s.statusLabel.stringValue = [NSString stringWithFormat:@"下载中: %s / %s", got.c_str(), tot.c_str()];
                } else {
                    s.statusLabel.stringValue = [NSString stringWithFormat:@"下载中: %s", opg::formatBytes(downloaded).c_str()];
                }
            });
            return YES;
        }, err);

        dispatch_async(dispatch_get_main_queue(), ^{
            FileWindowController* s = weakSelf;
            if (!s) return;
            s.downloading = NO;
            s.downloadButton.enabled = YES;
            
            if (!success) {
                s.statusLabel.stringValue = [NSString stringWithFormat:@"下载失败: %s", err.c_str()];
                [s.progressBar setHidden:YES];
                return;
            }
            s.statusLabel.stringValue = @"下载完成！正在准备导入 Ollama...";
            [s importToOllama:savePath];
        });
    });
}

- (void)importToOllama:(NSString*)ggufPath {
    // 生成 Modelfile
    NSString* dir = [ggufPath stringByDeletingLastPathComponent];
    NSString* modelfile = [dir stringByAppendingPathComponent:@"Modelfile"];
    NSString* content = [NSString stringWithFormat:@"FROM \"%@\"\n", ggufPath];
    [content writeToFile:modelfile atomically:YES encoding:NSUTF8StringEncoding error:nil];

    // 通过 Terminal 导入（等同原 Windows 版打开 cmd）
    NSString* script = [NSString stringWithFormat:
        @"cd \"%@\"\nollama create %s-%s -f \"%@\"\n", 
        dir, self.searchResult.owner.c_str(), self.searchResult.repository.c_str(), modelfile];
    
    NSString* tmpScript = [NSTemporaryDirectory() stringByAppendingPathComponent:@"opg_import.command"];
    [script writeToFile:tmpScript atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [[NSFileManager defaultManager] setAttributes:@{NSFilePosixPermissions: @0755} ofItemAtPath:tmpScript error:nil];

    [[NSWorkspace sharedWorkspace] openFile:tmpScript withApplication:@"Terminal"];
    [self.window close];
}

@end