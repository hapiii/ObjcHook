//
//  LocationHelper.h
//  fishhook_demo
//
//  Created by hapii on 2021/5/10.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface LocationHelper : NSObject

- (instancetype)initSuperController:(UIViewController *)vc;

- (void)determineLocation;
@end

NS_ASSUME_NONNULL_END
