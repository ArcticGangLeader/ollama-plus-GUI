#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <string>
#include <vector>
#include <sstream>

#include "core/ollama.hpp"
#include "core/capacity.hpp"
#include "core/search.hpp"
#include "core/format.hpp"
#include "core/process.hpp"
#include "core/proxy_server.hpp"
#import "file_window.h"

// 全局代理对象
static opg::ProxyServer g_proxy;

// 来自 main_mac.mm
namespace appinfo {
extern std::string ollamaPath;
extern std::string ollamaDescription;
extern std::string capacityDescription;
}

// ================= 搜索页 =================

@interface SearchViewController : NSViewController <NSTableViewDataSource, NSTableViewDelegate>
@property (strong) NSTextField* searchField;
@property (strong) NSButton* searchButton;
@property (strong) NSTableView* table;
@property (strong) NSScrollView* scroll;
@property (strong) NSTextField* statusLabel;
@property (strong) NSMutableArray<NSString*>* rowsModel;
@property (strong) NSMutableArray<NSDictionary*>* resultsModel;
@property (strong) id fileWindow;
@property (assign) BOOL searching;
@end

@implementation SearchViewController

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)loadView {
    NSView* v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 560)];

    NSTextField* title = [NSTextField labelWithString:@"搜索魔搭社区模型（仅显示 GGUF 模型）："];
    title.frame = NSMakeRect(20, 530, 500, 20);
    [v addSubview:title];

    self.searchField = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 495, 600, 26)];
    self.searchField.placeholderString = @"例如 qwen gguf";
    [self.searchField setTarget:self];
    [self.searchField setAction:@selector(onSearch:)];
    [v addSubview:self.searchField];

    self.searchButton = [[NSButton alloc] initWithFrame:NSMakeRect(630, 493, 90, 30)];
    [self.searchButton setButtonType:NSButtonTypeMomentaryPushIn];
    [self.searchButton setBezelStyle:NSBezelStyleRounded];
    [self.searchButton setTitle:@"搜索"];
    [self.searchButton setTarget:self];
    [self.searchButton setAction:@selector(onSearch:)];
    [v addSubview:self.searchButton];

    self.table = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 800, 420)];
    self.table.allowsMultipleSelection = NO;
    self.table.allowsEmptySelection = YES;
    self.table.usesAlternatingRowBackgroundColors = YES;
    self.table.dataSource = self;
    self.table.delegate = self;
    self.table.target = self;
    self.table.doubleAction = @selector(onTableDoubleClick:);

    NSTableColumn* colModel = [[NSTableColumn alloc] initWithIdentifier:@"model"];
    colModel.title = @"模型";
    colModel.width = 360;
    [self.table addTableColumn:colModel];

    NSTableColumn* colOwner = [[NSTableColumn alloc] initWithIdentifier:@"owner"];
    colOwner.title = @"作者";
    colOwner.width = 140;
    [self.table addTableColumn:colOwner];

    NSTableColumn* colSize = [[NSTableColumn alloc] initWithIdentifier:@"size"];
    colSize.title = @"仓库大小";
    colSize.width = 120;
    [self.table addTableColumn:colSize];

    NSTableColumn* colStars = [[NSTableColumn alloc] initWithIdentifier:@"stars"];
    colStars.title = @"Stars";
    colStars.width = 80;
    [self.table addTableColumn:colStars];

    self.scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(20, 60, 760, 420)];
    self.scroll.hasVerticalScroller = YES;
    self.scroll.borderType = NSBezelBorder;
    self.scroll.documentView = self.table;
    self.scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [v addSubview:self.scroll];

    self.statusLabel = [NSTextField labelWithString:@"就绪"];
    self.statusLabel.frame = NSMakeRect(20, 30, 760, 20);
    self.statusLabel.autoresizingMask = NSViewWidthSizable;
    [v addSubview:self.statusLabel];

    self.rowsModel = [NSMutableArray array];
    self.resultsModel = [NSMutableArray array];

    self.view = v;
}

- (IBAction)onSearch:(id)sender {
    (void)sender;
    if (self.searching) return;
    
    NSString* q = self.searchField.stringValue;
    if (q.length == 0) {
        self.statusLabel.stringValue = @"请输入搜索关键词";
        return;
    }
    
    self.searching = YES;
    self.searchButton.enabled = NO;
    self.statusLabel.stringValue = @"正在搜索魔搭社区...";

    std::string query = q.UTF8String;
    __weak SearchViewController* weakSelf = self;

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        std::string err;
        std::vector<opg::SearchResult> results = opg::searchModelScope(query, err);
        
        dispatch_async(dispatch_get_main_queue(), ^{
            SearchViewController* strongSelf = weakSelf;
            if (!strongSelf) return;
            
            strongSelf.searching = NO;
            strongSelf.searchButton.enabled = YES;
            [strongSelf.rowsModel removeAllObjects];
            [strongSelf.resultsModel removeAllObjects];

            if (!err.empty()) {
                strongSelf.statusLabel.stringValue = [NSString stringWithFormat:@"搜索失败: %s", err.c_str()];
                [strongSelf.table reloadData];
                return;
            }
            
            for (const auto& r : results) {
                std::string label = r.owner + "/" + r.repository;
                [strongSelf.rowsModel addObject:[NSString stringWithUTF8String:label.c_str()]];
                
                NSDictionary* dict = @{
                    @"owner": [NSString stringWithUTF8String:r.owner.c_str()],
                    @"repository": [NSString stringWithUTF8String:r.repository.c_str()],
                    @"revision": [NSString stringWithUTF8String:r.revision.c_str()],
                    @"size": @(r.estimatedSize > 0 ? r.estimatedSize : r.size),
                    @"stars": @(r.stars)
                };
                [strongSelf.resultsModel addObject:dict];
            }
            strongSelf.statusLabel.stringValue = [NSString stringWithFormat:@"找到 %zu 个 GGUF 模型，双击可选择文件", results.size()];
            [strongSelf.table reloadData];
        });
    });
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
    (void)tableView;
    return (NSInteger)self.resultsModel.count;
}

- (NSView*)tableView:(NSTableView*)tableView
   viewForTableColumn:(NSTableColumn*)tableColumn
                  row:(NSInteger)row {
    NSString* ident = tableColumn.identifier;
    NSTextField* cell = [tableView makeViewWithIdentifier:ident owner:self];
    if (!cell) {
        cell = [NSTextField labelWithString:@""];
        cell.identifier = ident;
    }
    if (row < 0 || row >= (NSInteger)self.resultsModel.count) return cell;

    NSDictionary* dict = self.resultsModel[row];
    NSString* owner = dict[@"owner"];
    NSString* repo = dict[@"repository"];
    
    if ([ident isEqualToString:@"model"]) {
        cell.stringValue = [NSString stringWithFormat:@"%@/%@", owner, repo];
    } else if ([ident isEqualToString:@"owner"]) {
        cell.stringValue = owner;
    } else if ([ident isEqualToString:@"size"]) {
        long long size = [dict[@"size"] longLongValue];
        std::string s = opg::formatBytes(size);
        cell.stringValue = [NSString stringWithUTF8String:s.c_str()];
    } else if ([ident isEqualToString:@"stars"]) {
        cell.stringValue = [NSString stringWithFormat:@"%@", dict[@"stars"]];
    }
    return cell;
}

- (IBAction)onTableDoubleClick:(id)sender {
    (void)sender;
    NSInteger row = self.table.clickedRow;
    if (row < 0 || row >= (NSInteger)self.resultsModel.count) return;

    NSDictionary* dict = self.resultsModel[row];
    opg::SearchResult model;
    model.owner = [dict[@"owner"] UTF8String];
    model.repository = [dict[@"repository"] UTF8String];
    model.revision = [dict[@"revision"] UTF8String];
    model.size = [dict[@"size"] longLongValue];
    
    FileWindowController* fw = [[FileWindowController alloc] initWithModel:model];
    self.fileWindow = fw;
    [fw showWindow:nil];
}

@end

// ================= 本地模型页 =================

@interface ModelsViewController : NSViewController <NSTableViewDataSource, NSTableViewDelegate>
@property (strong) NSTableView* table;
@property (strong) NSScrollView* scroll;
@property (strong) NSTextField* statusLabel;
@property (strong) NSButton* refreshBtn;
@property (strong) NSMutableArray<NSDictionary*>* models;
@end

@implementation ModelsViewController

- (void)loadView {
    NSView* v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 560)];

    NSTextField* title = [NSTextField labelWithString:@"本地 Ollama 模型"];
    title.font = [NSFont boldSystemFontOfSize:18];
    title.frame = NSMakeRect(20, 520, 300, 24);
    [v addSubview:title];

    self.refreshBtn = [[NSButton alloc] initWithFrame:NSMakeRect(20, 480, 100, 30)];
    [self.refreshBtn setTitle:@"刷新列表"];
    [self.refreshBtn setBezelStyle:NSBezelStyleRounded];
    [self.refreshBtn setTarget:self];
    [self.refreshBtn setAction:@selector(onRefresh:)];
    [v addSubview:self.refreshBtn];

    self.table = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 800, 400)];
    self.table.usesAlternatingRowBackgroundColors = YES;
    self.table.dataSource = self;
    self.table.delegate = self;

    NSTableColumn* colName = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    colName.title = @"模型名称";
    colName.width = 300;
    [self.table addTableColumn:colName];

    NSTableColumn* colId = [[NSTableColumn alloc] initWithIdentifier:@"id"];
    colId.title = @"ID";
    colId.width = 200;
    [self.table addTableColumn:colId];

    NSTableColumn* colSize = [[NSTableColumn alloc] initWithIdentifier:@"size"];
    colSize.title = @"大小";
    colSize.width = 100;
    [self.table addTableColumn:colSize];

    self.scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(20, 60, 760, 400)];
    self.scroll.hasVerticalScroller = YES;
    self.scroll.borderType = NSBezelBorder;
    self.scroll.documentView = self.table;
    [v addSubview:self.scroll];

    self.statusLabel = [NSTextField labelWithString:@"就绪"];
    self.statusLabel.frame = NSMakeRect(20, 30, 760, 20);
    [v addSubview:self.statusLabel];

    self.models = [NSMutableArray array];
    self.view = v;

    NSString* ollamaText = [NSString stringWithUTF8String:appinfo::ollamaDescription.c_str()];
    self.statusLabel.stringValue = ollamaText ? ollamaText : @"";
}

- (void)onRefresh:(id)sender {
    (void)sender;
    self.refreshBtn.enabled = NO;
    self.statusLabel.stringValue = @"正在读取 Ollama 模型列表...";

    __weak ModelsViewController* weakSelf = self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        std::string cmd = opg::ollamaCommand();
        opg::CommandResult result;
        if (!cmd.empty()) {
            result = opg::runCommand(cmd + " list");
        } else {
            result.error = "未找到 ollama 命令。请先安装。";
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            ModelsViewController* s = weakSelf;
            if (!s) return;
            s.refreshBtn.enabled = YES;
            [s.models removeAllObjects];

            if (!result.error.empty()) {
                s.statusLabel.stringValue = [NSString stringWithFormat:@"错误: %s", result.error.c_str()];
                [s.table reloadData];
                return;
            }

            std::string out = result.output;
            size_t lineStart = out.find('\n');
            if (lineStart != std::string::npos) {
                out = out.substr(lineStart + 1);
            }
            
            std::stringstream ss(out);
            std::string line;
            int count = 0;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                std::stringstream lss(line);
                std::string name, id, size, modified;
                if (lss >> name >> id >> size >> modified) {
                    [s.models addObject:@{
                        @"name": [NSString stringWithUTF8String:name.c_str()],
                        @"id": [NSString stringWithUTF8String:id.c_str()],
                        @"size": [NSString stringWithUTF8String:size.c_str()]
                    }];
                    count++;
                }
            }
            [s.table reloadData];
            s.statusLabel.stringValue = [NSString stringWithFormat:@"共 %d 个本地模型", count];
        });
    });
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    (void)tableView;
    return self.models.count;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row {
    NSString* ident = tableColumn.identifier;
    NSTextField* cell = [tableView makeViewWithIdentifier:ident owner:self];
    if (!cell) {
        cell = [NSTextField labelWithString:@""];
        cell.identifier = ident;
    }
    if (row < 0 || row >= (NSInteger)self.models.count) return cell;

    NSDictionary* dict = self.models[row];
    if ([ident isEqualToString:@"name"]) cell.stringValue = dict[@"name"];
    else if ([ident isEqualToString:@"id"]) cell.stringValue = dict[@"id"];
    else if ([ident isEqualToString:@"size"]) cell.stringValue = dict[@"size"];
    return cell;
}
@end

// ================= 服务端页 =================

@interface ServiceViewController : NSViewController
@property (strong) NSTextField* portField;
@property (strong) NSButton* startBtn;
@property (strong) NSButton* stopBtn;
@property (strong) NSTextField* statusLabel;
@property (strong) NSTextField* keyField;
@property (strong) NSButton* saveKeyBtn;
@property (strong) NSTextField* statsLabel;
@property (strong) NSTimer* timer;
@end

@implementation ServiceViewController

- (void)loadView {
    NSView* v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 560)];

    NSTextField* title = [NSTextField labelWithString:@"服务端反向代理"];
    title.font = [NSFont boldSystemFontOfSize:18];
    title.frame = NSMakeRect(20, 520, 300, 24);
    [v addSubview:title];

    NSTextField* portTitle = [NSTextField labelWithString:@"外部端口："];
    portTitle.frame = NSMakeRect(20, 480, 80, 20);
    [v addSubview:portTitle];

    self.portField = [[NSTextField alloc] initWithFrame:NSMakeRect(100, 478, 100, 24)];
    self.portField.stringValue = @"11435";
    [v addSubview:self.portField];

    self.startBtn = [[NSButton alloc] initWithFrame:NSMakeRect(220, 476, 100, 30)];
    [self.startBtn setTitle:@"开启代理"];
    [self.startBtn setTarget:self];
    [self.startBtn setAction:@selector(onStart:)];
    [v addSubview:self.startBtn];

    self.stopBtn = [[NSButton alloc] initWithFrame:NSMakeRect(330, 476, 100, 30)];
    [self.stopBtn setTitle:@"关闭代理"];
    [self.stopBtn setTarget:self];
    [self.stopBtn setAction:@selector(onStop:)];
    [self.stopBtn setEnabled:NO];
    [v addSubview:self.stopBtn];

    self.statusLabel = [NSTextField labelWithString:@"服务端已关闭。"];
    self.statusLabel.frame = NSMakeRect(20, 440, 760, 20);
    [v addSubview:self.statusLabel];

    NSTextField* keyTitle = [NSTextField labelWithString:@"API Key："];
    keyTitle.frame = NSMakeRect(20, 400, 80, 20);
    [v addSubview:keyTitle];

    self.keyField = [[NSTextField alloc] initWithFrame:NSMakeRect(100, 398, 300, 24)];
    self.keyField.placeholderString = @"留空则不验证";
    [v addSubview:self.keyField];

    self.saveKeyBtn = [[NSButton alloc] initWithFrame:NSMakeRect(410, 396, 100, 30)];
    [self.saveKeyBtn setTitle:@"保存设置"];
    [self.saveKeyBtn setTarget:self];
    [self.saveKeyBtn setAction:@selector(onSaveKey:)];
    [v addSubview:self.saveKeyBtn];

    NSString* cap = [NSString stringWithUTF8String:appinfo::capacityDescription.c_str()];
    NSTextField* capLabel = [NSTextField labelWithString:cap ? cap : @""];
    capLabel.frame = NSMakeRect(20, 300, 760, 80);
    capLabel.lineBreakMode = NSLineBreakByWordWrapping;
    [capLabel setMaximumNumberOfLines:0];
    [v addSubview:capLabel];

    self.statsLabel = [NSTextField labelWithString:@"总 Token：0 | 请求：0"];
    self.statsLabel.frame = NSMakeRect(20, 250, 760, 40);
    [v addSubview:self.statsLabel];

    self.view = v;
}

- (void)onStart:(id)sender {
    (void)sender;
    int port = [self.portField intValue];
    if (port < 1 || port > 65535) {
        self.statusLabel.stringValue = @"端口无效。";
        return;
    }
    
    g_proxy.requireKey = self.keyField.stringValue.length > 0;
    g_proxy.apiKey = [self.keyField.stringValue UTF8String];

    std::string err;
    if (!opg::startProxyServer(g_proxy, port, err)) {
        self.statusLabel.stringValue = [NSString stringWithFormat:@"启动失败: %s", err.c_str()];
        return;
    }
    
    self.startBtn.enabled = NO;
    self.stopBtn.enabled = YES;
    self.portField.enabled = NO;
    self.statusLabel.stringValue = [NSString stringWithFormat:@"服务端已开放：0.0.0.0:%d -> 127.0.0.1:11434", port];

    self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(updateStats:) userInfo:nil repeats:YES];
}

- (void)onStop:(id)sender {
    (void)sender;
    opg::stopProxyServer(g_proxy);
    [self.timer invalidate];
    self.timer = nil;

    self.startBtn.enabled = YES;
    self.stopBtn.enabled = NO;
    self.portField.enabled = YES;
    self.statusLabel.stringValue = @"服务端已关闭。";
}

- (void)onSaveKey:(id)sender {
    (void)sender;
    g_proxy.requireKey = self.keyField.stringValue.length > 0;
    g_proxy.apiKey = [self.keyField.stringValue UTF8String];
    self.statusLabel.stringValue = g_proxy.requireKey ? @"Key 验证已启用" : @"Key 验证已关闭";
}

- (void)updateStats:(NSTimer*)timer {
    (void)timer;
    if (!g_proxy.running) return;
    unsigned long long tokens = g_proxy.stats.totalTokens.load();
    unsigned long long reqs = g_proxy.stats.totalRequests.load();
    unsigned long long blocked = g_proxy.stats.blockedRequests.load();
    self.statsLabel.stringValue = [NSString stringWithFormat:
        @"总 Token：%llu | 总请求：%llu | 拦截：%llu", tokens, reqs, blocked];
}
@end

// ================= 主窗口 =================

@interface MainWindowController : NSWindowController
@property (strong) SearchViewController* searchVC;
@property (strong) ModelsViewController* modelsVC;
@property (strong) ServiceViewController* serviceVC;
@end

@implementation MainWindowController
- (instancetype)init {
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 900, 620)
                  styleMask:(NSWindowStyleMaskTitled |
                             NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable |
                             NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.title = @"Ollama Plus GUI - 魔搭 GGUF 搜索与模型管理";
    window.minSize = NSMakeSize(760, 520);
    [window center];

    if ((self = [super initWithWindow:window])) {
        self.searchVC = [[SearchViewController alloc] init];
        self.modelsVC = [[ModelsViewController alloc] init];
        self.serviceVC = [[ServiceViewController alloc] init];

        NSTabView* tab = [[NSTabView alloc] initWithFrame:window.contentView.bounds];
        tab.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        tab.tabViewType = NSTopTabsBezelBorder;

        [tab addTabViewItem:[self makeTabItem:@"魔搭搜索" viewController:self.searchVC]];
        [tab addTabViewItem:[self makeTabItem:@"本地模型" viewController:self.modelsVC]];
        [tab addTabViewItem:[self makeTabItem:@"服务端" viewController:self.serviceVC]];

        [window.contentView addSubview:tab];
    }
    return self;
}

- (NSTabViewItem*)makeTabItem:(NSString*)label viewController:(NSViewController*)vc {
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:label];
    item.label = label;
    item.view = vc.view;
    return item;
}
@end

// ================= AppDelegate =================

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (strong) MainWindowController* mainWC;
@end

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    self.mainWC = [[MainWindowController alloc] init];
    [self.mainWC showWindow:nil];
    [NSApp activateIgnoringOtherApps:YES];

    NSMenu* menubar = [[NSMenu alloc] init];
    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [menubar addItem:appItem];
    [NSApp setMainMenu:menubar];

    NSMenu* appMenu = [[NSMenu alloc] init];
    NSMenuItem* quitItem = [[NSMenuItem alloc]
        initWithTitle:@"退出 Ollama Plus GUI"
               action:@selector(terminate:)
        keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    appItem.submenu = appMenu;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}
@end

// ================= 入口桥 =================

int runGUI(int argc, const char* argv[]) {
    (void)argc; (void)argv;
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}