#import <AVFoundation/AVFoundation.h>
#include "MacSupport.h"

namespace mac
{
    void requestMic (std::function<void (bool)> done)
    {
        const auto st = [AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio];
        if (st == AVAuthorizationStatusAuthorized) { done (true); return; }
        if (st == AVAuthorizationStatusDenied || st == AVAuthorizationStatusRestricted) { done (false); return; }
        auto* copy = new std::function<void (bool)> (std::move (done));
        [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio completionHandler: ^(BOOL granted) {
            dispatch_async (dispatch_get_main_queue(), ^{ (*copy) (granted == YES); delete copy; });
        }];
    }
}
