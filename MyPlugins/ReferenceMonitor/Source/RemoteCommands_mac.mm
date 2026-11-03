#include "RemoteCommands.h"
#import <Foundation/Foundation.h>

struct RemoteCommands::Impl
{
    std::function<void (const std::string&)> callback;
    id observer = nil;
};

RemoteCommands::RemoteCommands (std::function<void (const std::string&)> onCommand)
    : impl (std::make_unique<Impl>())
{
    impl->callback = std::move (onCommand);
    Impl* self = impl.get();
    impl->observer = [[NSDistributedNotificationCenter defaultCenter]
        addObserverForName: @"com.myplugins.referencemonitor.command"
                     object: nil
                      queue: [NSOperationQueue mainQueue]
                 usingBlock: ^(NSNotification* note)
                 {
                     id obj = note.object;
                     if ([obj isKindOfClass: [NSString class]] && self->callback)
                         self->callback (std::string ([(NSString*) obj UTF8String]));
                 }];
}

RemoteCommands::~RemoteCommands()
{
    if (impl->observer != nil)
        [[NSDistributedNotificationCenter defaultCenter] removeObserver: impl->observer];
}
