#import <Cocoa/Cocoa.h>
#include "core/search.hpp"

@interface FileWindowController : NSWindowController
- (instancetype)initWithModel:(const opg::SearchResult&)model;
@end